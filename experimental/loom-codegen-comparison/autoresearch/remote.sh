#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# moth side of the autoresearch harness. Only `ar` calls this, over one ssh
# session per command; see README.md.
#
#   remote.sh setup-start TAG BASE        (stdin: nothing; bin/ and src.tar.gz
#                                         were unpacked into the campaign dir)
#   remote.sh eval-start TAG RUN BEST PAIRS STEPS BAR CONTENDED GPU_WAIT
#                                         (stdin: kernel tar.gz; stdout: bundle)
#   remote.sh resume TAG RUN              (stdout: bundle)
#   remote.sh import TAG                  (stdin: kernel tar.gz; stdout: .loom)
#   remote.sh screen TAG ID BEST          (stdin: tar.gz of a kernel dir's
#                                         contents; stdout: bundle; no GPU)
#
# Long jobs run detached (setsid nohup) and log to jobs/; the ssh session only
# follows the log, so a dropped connection loses nothing and `ar resume`
# reattaches. Progress goes to stderr; stdout carries only data.
set -uo pipefail
R=${AR_DATA:-/mnt/data/bwibking}
ROOT=$R/ar
V=$R/venv-py312/bin
ROCM=/opt/rocm/core-10.1
LLVM=$ROCM/lib/llvm/bin
HIPCC=$ROCM/bin/hipcc
AMDSMI=$ROCM/bin/amd-smi
# The vendored C++ parser in loom-import-cxx needs C++23 library support that
# moth's GCC 11 libstdc++ lacks; conda-forge GCC 14 provides it.
GCC14=$R/gcc14
GCC_INSTALL_DIR=$GCC14/lib/gcc/x86_64-conda-linux-gnu/14.4.0
TARGET=gfx90a
WORKGROUPS=2048  # ceil(64^3 / 128), compare_rocm's default grid
# The MI210 sits on NUMA node 1 (CPUs 24-47). Builds stay on node 0; timing
# runs pin to two node-1 cores.
BUILD_CPUS=0-23
TIMING_CPUS=46,47
KREL=experimental/loom-codegen-comparison/kernel
export XDG_CACHE_HOME=$R/cache TMPDIR=$R/tmp PYTHONPYCACHEPREFIX=$R/cache/pycache
export PIP_NO_CACHE_DIR=1 PATH=$V:$PATH
mkdir -p "$TMPDIR"

cmd=${1:?command}; tag=${2:?tag}; shift 2
[[ $tag =~ ^[A-Za-z0-9._-]+$ ]] || { echo "bad tag: $tag" >&2; exit 2; }
D=$ROOT/$tag
pin=(taskset -c "$BUILD_CPUS")

log() { echo "[$(date +%H:%M:%S)] $*"; }

detach() {  # detach NAME ARGS... : run `remote.sh ARGS...` detached
  mkdir -p "$D/jobs"
  setsid nohup bash "$D/bin/remote.sh" "${@:2}" > "$D/jobs/$1.log" 2>&1 < /dev/null &
  echo $! > "$D/jobs/$1.pid"
}

follow() {  # follow NAME : stream the job log to stderr until the job exits
  local pid
  pid=$(cat "$D/jobs/$1.pid" 2>/dev/null) || { echo "no job $1" >&2; return 1; }
  tail -n +1 -f --pid="$pid" "$D/jobs/$1.log" >&2
}

kernel_stats() {  # kernel_stats HSACO : one line of resource and static counts
  local notes asm
  notes=$("$LLVM/llvm-readelf" --notes "$1")
  asm=$("$LLVM/llvm-objdump" -d --mcpu=$TARGET "$1")
  printf 'vgpr=%s agpr=%s sgpr=%s scratch_bytes=%s insts=%s scratch_ops=%s waitcnt=%s branches=%s\n' \
    "$(sed -nE 's/^ *\.vgpr_count: *([0-9]+)/\1/p' <<<"$notes" | head -1)" \
    "$(sed -nE 's/^ *\.agpr_count: *([0-9]+)/\1/p' <<<"$notes" | head -1 | grep . || echo 0)" \
    "$(sed -nE 's/^ *\.sgpr_count: *([0-9]+)/\1/p' <<<"$notes" | head -1)" \
    "$(sed -nE 's/^ *\.private_segment_fixed_size: *([0-9]+)/\1/p' <<<"$notes" | head -1)" \
    "$(grep -cE '^\s+[sv]_|^\s+scratch_|^\s+global_|^\s+buffer_|^\s+flat_' <<<"$asm")" \
    "$(grep -cE '^\s+scratch_|^\s+buffer_(load|store)' <<<"$asm")" \
    "$(grep -cE '^\s+s_waitcnt' <<<"$asm")" \
    "$(grep -cE '^\s+s_(c)?branch' <<<"$asm")"
}

interface_of() {  # interface_of LOOM : kernel headers with SSA names erased
  awk '/^kernel\.def /{on=1} on{print} on && /^} launch\(/{on=0}' "$1" |
    sed -E 's/%[A-Za-z0-9_.]+/%/g'
}

layout_of() {  # layout_of KERNEL_DIR OUT_BIN : host CellRecord layout probe
  "$LLVM/clang++" --gcc-install-dir="$GCC_INSTALL_DIR" -std=c++20 -w \
    -Wl,-rpath,"$GCC14/lib" -I "$1" "$D/bin/layout.cpp" -o "$2" && "$2"
}

foreign_gpu_procs() {  # GPU processes that are not ours (compare_rocm)
  "$AMDSMI" process 2>/dev/null |
    awk '/NAME:/{n=$2} /PID:/{print $2 " " n}' | grep -v 'compare_rocm' || true
}

import_kernel() {  # import_kernel KERNEL_DIR OUT_LOOM LOG
  "${pin[@]}" "$D/tools/loom-import-cxx" --data-model=lp64 --fp-model=precise \
    --root=chemistry::prepare_grid_timestep_kernel \
    --root=chemistry::advance_collapse_gridwide_kernel \
    --I="$1" --output="$2" "$1/reproducer.cpp" > "$3" 2>&1
}

compile_kernel() {  # compile_kernel LOOM NAME ROOT OUT_DIR
  local cfg="--config=chemistry.$3.workgroup_count"
  "${pin[@]}" timeout 3600 "$D/tools/loom-compile" "$1" --root="chemistry.$3" \
    "$cfg.x=$WORKGROUPS" "$cfg.y=1" "$cfg.z=1" \
    --format=amdgpu-hsaco --target=amdgpu:$TARGET --output="$4/$2.hsaco" \
    --compile-report=text-summary --compile-report-output="$4/$2.report.txt" \
    > "$4/$2.compile.log" 2>&1
}

candidate_ir() {  # candidate_ir KERNEL_DIR OUT : OUT/chemistry.loom, interface-checked
  # On failure, writes "STATUS [KEY=VALUE]" to OUT/fail.txt and returns 1.
  local k=$1 out=$2
  if [[ -f $k/chemistry.loom ]]; then
    log "IR mode: using kernel/chemistry.loom"
    cp "$k/chemistry.loom" "$out/chemistry.loom"
  else
    log "C++ mode: layout check and import"
    layout_of "$k" "$out/layout-probe" > "$out/layout.txt" 2>&1
    diff "$D/harness/layout.txt" "$out/layout.txt" > "$out/layout.diff" ||
      { echo "interface-fail reason=cellrecord-layout" > "$out/fail.txt"; return 1; }
    import_kernel "$k" "$out/chemistry.loom" "$out/import.log" ||
      { tail -20 "$out/import.log"; echo "import-fail" > "$out/fail.txt"; return 1; }
  fi
  interface_of "$out/chemistry.loom" | diff "$D/harness/interface.txt" - > "$out/interface.diff" ||
    { cat "$out/interface.diff"; echo "interface-fail reason=kernel-signature" > "$out/fail.txt"; return 1; }
}

screen_job() {  # screen_job ID BEST : import + advance compile only; no GPU
  local id=$1 best=$2 out=$D/screens/$1 t_start
  t_start=$(date +%s)
  sfinish() {  # sfinish STATUS [KEY=VALUE...]
    local stats=""
    [[ -f $out/stats.txt ]] && stats=$(sed -n 's/^advance //p' "$out/stats.txt")
    echo "SCREEN status=$1 ${*:2} $stats seconds=$(( $(date +%s) - t_start ))" > "$out/result.txt"
    (cd "$out" || exit
     [[ -f advance.s ]] && gzip -kf advance.s
     [[ -f chemistry.loom ]] && gzip -kf chemistry.loom
     local f files=()
     for f in result.txt stats.txt advance.report.txt advance.compile.log import.log \
              interface.diff layout.diff advance.s.gz chemistry.loom.gz; do
       [[ -e $f ]] && files+=("$f")
     done
     tar czf bundle.tgz "${files[@]}")
    log "$(cat "$out/result.txt")"
  }
  log "screen $tag $id (compile only; best=$best)"
  [[ -f $D/setup.ok ]] || { sfinish harness-fail reason=no-setup; return; }
  (cd "$D/tools" && md5sum -c --quiet tools.md5) || { sfinish harness-fail reason=tools-changed; return; }
  # shellcheck disable=SC2046  # the failure file holds "STATUS [KEY=VALUE]"
  candidate_ir "$out/src" "$out" || { sfinish $(cat "$out/fail.txt"); return; }
  log "compile advance"
  local c0 c1
  c0=$(date +%s)
  compile_kernel "$out/chemistry.loom" advance advance_collapse_gridwide_kernel "$out" ||
    { tail -15 "$out/advance.compile.log"; sfinish compile-fail; return; }
  c1=$(date +%s)
  echo "advance $(kernel_stats "$out/advance.hsaco") compile_s=$((c1 - c0))" > "$out/stats.txt"
  "$LLVM/llvm-objdump" -d --mcpu=$TARGET "$out/advance.hsaco" > "$out/advance.s"
  local same=0
  [[ $best != none ]] && cmp -s "$out/advance.hsaco" "$D/runs/$best/advance.hsaco" && same=1
  sfinish ok identical_to_best=$same
}

setup_job() {  # setup_job BASE
  local base=$1 s=$D/src b=$D/build
  log "setup $tag base=$base"
  [[ -d $GCC_INSTALL_DIR ]] || { log "missing $GCC_INSTALL_DIR (conda-forge gxx_impl_linux-64=14)"; echo "SETUP fail"; return 1; }
  rm -rf "$s" "$b" "$D/tools" "$D/harness" "$D/setup.ok"
  mkdir -p "$s" "$D/tools" "$D/harness" "$D/runs" "$D/inbox"
  tar xzf "$D/src.tar.gz" -C "$s" || { echo "SETUP fail untar"; return 1; }
  rm -f "$D/src.tar.gz"
  log "configure"
  "$V/cmake" -G Ninja -S "$s" -B "$b" \
    -DCMAKE_MAKE_PROGRAM="$V/ninja" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$ROCM/llvm/bin/clang" -DCMAKE_CXX_COMPILER="$ROCM/llvm/bin/clang++" \
    "-DCMAKE_C_FLAGS=--gcc-install-dir=$GCC_INSTALL_DIR" \
    "-DCMAKE_CXX_FLAGS=--gcc-install-dir=$GCC_INSTALL_DIR" \
    "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,$GCC14/lib" \
    "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-rpath,$GCC14/lib" \
    "-DCMAKE_MODULE_LINKER_FLAGS=-Wl,-rpath,$GCC14/lib" \
    -DPython3_EXECUTABLE="$V/python3.12" -DIREE_ROCM_PATH="$ROCM" \
    -DIREE_HAL_DRIVER_DEFAULTS=OFF -DIREE_HAL_DRIVER_AMDGPU=ON -DIREE_HAL_DRIVER_TASK=ON \
    "-DIREE_HAL_AMDGPU_TARGETS=$TARGET" -DLOOM_IMPORT_CXX=ON \
    > "$D/jobs/configure.log" 2>&1 || { tail -30 "$D/jobs/configure.log"; echo "SETUP fail configure"; return 1; }
  log "build loom-import-cxx loom-compile"
  "${pin[@]}" "$V/cmake" --build "$b" --target loom-import-cxx loom-compile -j 24 \
    > "$D/jobs/build.log" 2>&1 || { grep -A12 FAILED: "$D/jobs/build.log" | head -40; echo "SETUP fail build"; return 1; }
  cp "$b/loom/src/loom/tools/loom-import-cxx/loom-import-cxx" \
     "$b/loom/src/loom/tools/loom-compile/loom-compile" "$D/tools/"
  (cd "$D/tools" && md5sum loom-import-cxx loom-compile > tools.md5)
  # compare_rocm is built once from the seed kernel. Candidates reach it only
  # as code objects, so they cannot change the host side of the measurement.
  log "build compare_rocm"
  local c=$s/experimental/loom-codegen-comparison
  python3 "$c/generate.py" --output-dir="$D/harness/generated" > /dev/null || { echo "SETUP fail generate"; return 1; }
  "$HIPCC" --offload-arch=$TARGET -std=c++20 -O3 -ffp-contract=off \
    -DPRIMORDIAL_ROS2S_ENABLE_HIP=1 -DPRIMORDIAL_ROS2S_NO_MAIN=1 \
    -I "$D/harness/generated" -I "$c" "$c/compare_rocm.cpp" -o "$D/harness/compare_rocm" \
    > "$D/jobs/hipcc.log" 2>&1 || { tail -30 "$D/jobs/hipcc.log"; echo "SETUP fail hipcc"; return 1; }
  # Interface references from the seed kernel.
  local k=$D/harness/seed-kernel
  rm -rf "$k"; mkdir -p "$k"
  cp "$D/harness/generated/reproducer.cpp" "$D/harness/generated/integrate.inc" \
     "$c/support.h" "$c/f64_math.h" "$k/"
  layout_of "$k" "$D/harness/layout-seed" > "$D/harness/layout.txt" || { echo "SETUP fail layout"; return 1; }
  import_kernel "$k" "$D/harness/seed.loom" "$D/jobs/seed-import.log" || { tail -20 "$D/jobs/seed-import.log"; echo "SETUP fail import"; return 1; }
  interface_of "$D/harness/seed.loom" > "$D/harness/interface.txt"
  [[ -s $D/harness/interface.txt ]] || { echo "SETUP fail interface"; return 1; }
  echo "base=$base tools=$(md5sum < "$D/tools/loom-compile" | cut -c1-32) host=$(hostname)" > "$D/setup.ok"
  cat "$D/harness/layout.txt"
  log "setup done: $(cat "$D/setup.ok")"
  echo "SETUP ok"
}

time_one() {  # time_one IDX LABEL RUN_DIR TIMING_DIR
  local log=$4/run$1-$2.log t0 t1 rc f
  f=$(foreign_gpu_procs | wc -l)
  t0=$(date +%s.%N)
  timeout 600 taskset -c "$TIMING_CPUS" "$D/harness/compare_rocm" \
    "$3/prepare.hsaco" "$3/advance.hsaco" --arch $TARGET --repeats 10 > "$log" 2>&1
  rc=$?
  t1=$(date +%s.%N)
  f=$((f + $(foreign_gpu_procs | wc -l)))
  echo "$1 $2 $rc $t0 $t1 $log $f" >> "$4/runs.txt"
  log "  run $1 $2 rc=$rc foreign=$f $(grep -h '^advance kernel' "$log" | sed 's/advance kernel GPU median: //')"
}

eval_job() {  # eval_job RUN BEST PAIRS STEPS BAR CONTENDED GPU_WAIT
  local run=$1 best=$2 pairs=$3 steps=$4 bar=$5 contended=$6 gpu_wait=$7
  local out t_start k
  t_start=$(date +%s)
  out=$D/runs/$run
  finish() {  # finish STATUS [KEY=VALUE...]
    local stats=""
    [[ -f $out/stats.txt ]] && stats=$(sed -n 's/^advance //p' "$out/stats.txt")
    echo "RESULT status=$1 ${*:2} $stats minutes=$(( ($(date +%s) - t_start + 30) / 60 ))" > "$out/result.txt"
    (cd "$out" || exit
     [[ -f advance.s ]] && gzip -kf advance.s
     [[ -f chemistry.loom ]] && gzip -kf chemistry.loom
     local f files=()
     for f in result.txt provenance.txt stats.txt advance.report.txt prepare.report.txt \
              advance.compile.log prepare.compile.log import.log interface.diff layout.diff \
              correctness.log timing advance.s.gz chemistry.loom.gz; do
       [[ -e $f ]] && files+=("$f")
     done
     tar czf bundle.tgz "${files[@]}")
    log "$(cat "$out/result.txt")"
  }
  mkdir -p "$out/src"
  log "eval $tag run=$run best=$best pairs=$pairs steps=$steps bar=$bar"
  tar xzf "$D/inbox/$run.tgz" -C "$out/src" && rm -f "$D/inbox/$run.tgz" || { finish harness-fail reason=untar; return; }
  k=$out/src/$KREL
  [[ -f $D/setup.ok ]] || { finish harness-fail reason=no-setup; return; }
  (cd "$D/tools" && md5sum -c --quiet tools.md5) || { finish harness-fail reason=tools-changed; return; }
  if [[ $best != none && ! -f $D/runs/$best/advance.hsaco ]]; then finish harness-fail reason=best-missing; return; fi
  echo "run=$run best=$best $(cat "$D/setup.ok") kernel_sha256=$(cd "$k" && find . -type f | LC_ALL=C sort | xargs cat | sha256sum | cut -c1-16)" > "$out/provenance.txt"

  # shellcheck disable=SC2046  # the failure file holds "STATUS [KEY=VALUE]"
  candidate_ir "$k" "$out" || { finish $(cat "$out/fail.txt"); return; }

  log "compile prepare + advance"
  local c0 c1
  c0=$(date +%s)
  compile_kernel "$out/chemistry.loom" prepare prepare_grid_timestep_kernel "$out" &
  local ppid=$!
  compile_kernel "$out/chemistry.loom" advance advance_collapse_gridwide_kernel "$out"
  local arc=$?
  wait $ppid; local prc=$?
  c1=$(date +%s)
  if [[ $arc != 0 || $prc != 0 ]]; then
    tail -15 "$out/advance.compile.log" "$out/prepare.compile.log"
    finish compile-fail advance_rc=$arc prepare_rc=$prc; return
  fi
  { echo "advance $(kernel_stats "$out/advance.hsaco") compile_s=$((c1 - c0))"
    echo "prepare $(kernel_stats "$out/prepare.hsaco")"; } > "$out/stats.txt"
  "$LLVM/llvm-objdump" -d --mcpu=$TARGET "$out/advance.hsaco" > "$out/advance.s"
  cat "$out/stats.txt"
  if [[ $best != none ]] && cmp -s "$out/advance.hsaco" "$D/runs/$best/advance.hsaco" &&
     cmp -s "$out/prepare.hsaco" "$D/runs/$best/prepare.hsaco"; then
    finish identical to=$best; return
  fi

  # Everything below holds the GPU lock: one GPU job at a time on moth.
  exec 9> "$ROOT/gpu.lock"
  flock 9
  local waited=0 f
  while f=$(foreign_gpu_procs); [[ -n $f ]]; do
    if (( waited >= gpu_wait )); then
      if [[ $contended == 1 ]]; then log "GPU still shared after ${waited}s; timing anyway (contended runs are flagged)"; break; fi
      log "GPU busy with: $f"; finish gpu-busy waited_s=$waited; return
    fi
    (( waited == 0 )) && log "GPU in use by another user ($f); waiting up to ${gpu_wait}s"
    sleep 30; waited=$((waited + 30))
  done

  log "correctness: $steps gridwide steps"
  timeout 300 taskset -c "$TIMING_CPUS" "$D/harness/compare_rocm" "$out/prepare.hsaco" \
    "$out/advance.hsaco" --arch $TARGET --steps "$steps" --warmup 0 --repeats 1 > "$out/correctness.log" 2>&1
  local rc=$?
  cat "$out/correctness.log"
  [[ $rc == 124 ]] && { finish hang stage=correctness; return; }
  grep -q '^PASS' "$out/correctness.log" || { finish incorrect rc=$rc; return; }

  local t=$out/timing ref=$D/runs/$best idx=0 p
  [[ $best == none ]] && ref=$out  # baseline: A/A pairs measure the noise floor
  mkdir -p "$t"; : > "$t/runs.txt"
  PYTHONUNBUFFERED=1 "$AMDSMI" monitor -p -u -t -w 1 --csv > "$t/telemetry.csv" 2>/dev/null &
  local tel=$!
  pair() {  # odd pairs run candidate first, even pairs best first
    if (( $1 % 2 )); then idx=$((idx+1)); time_one $idx cand "$out" "$t"; idx=$((idx+1)); time_one $idx best "$ref" "$t"
    else idx=$((idx+1)); time_one $idx best "$ref" "$t"; idx=$((idx+1)); time_one $idx cand "$out" "$t"; fi
  }
  log "timing: $pairs interleaved pairs against $([[ $best == none ]] && echo "itself (A/A)" || echo "$best")"
  for ((p = 1; p <= pairs; ++p)); do pair $p; done
  sleep 2
  python3 "$D/bin/analyze.py" "$t" "$bar" "$contended" > "$t/summary.txt"
  local extra
  extra=$(sed -nE 's/^FLAGGED_PAIRS ([0-9]+)$/\1/p' "$t/summary.txt")
  if (( ${extra:-0} > 0 )); then
    log "re-timing $extra flagged pair(s)"
    for ((p = pairs + 1; p <= pairs + extra && p <= pairs + 3; ++p)); do pair $p; done
    sleep 2
    python3 "$D/bin/analyze.py" "$t" "$bar" "$contended" > "$t/summary.txt"
  fi
  kill $tel 2>/dev/null
  flock -u 9
  cat "$t/summary.txt"
  local res verdict
  res=$(sed -n 's/^RESULT //p' "$t/summary.txt")
  if grep -q 'incorrect_runs=[1-9]' <<<"$res"; then verdict=incorrect
  elif grep -q 'clean_pairs=[01] ' <<<"$res"; then verdict=gpu-busy
  elif [[ $best == none ]]; then verdict=baseline
  elif grep -q 'meets_bar=1' <<<"$res"; then verdict=keep
  else verdict=discard; fi
  finish $verdict $res
}

case $cmd in
  setup-start)
    detach setup setup-job "$tag" "$1"
    follow setup ;;
  setup-job) setup_job "$1"; echo "JOB-EXIT $?" ;;
  eval-start)
    run=$1
    [[ -f $D/setup.ok ]] || { echo "RESULT status=harness-fail reason=no-setup" >&2; exit 1; }
    [[ -e $D/runs/$run ]] && { echo "RESULT status=harness-fail reason=run-exists" >&2; exit 1; }
    mkdir -p "$D/inbox"; cat > "$D/inbox/$run.tgz"
    detach "eval-$run" eval-job "$tag" "$@"
    follow "eval-$run"
    cat "$D/runs/$run/bundle.tgz" 2>/dev/null ;;
  eval-job) eval_job "$@"; echo "JOB-EXIT $?" ;;
  resume)
    follow "eval-$1"
    cat "$D/runs/$1/bundle.tgz" 2>/dev/null ;;
  screen)
    # Synchronous: a screen is short and cheap to repeat, so a dropped
    # connection just means running it again.
    id=$1 best=$2
    [[ $id =~ ^s[0-9]+$ ]] || { echo "bad screen id: $id" >&2; exit 2; }
    out=$D/screens/$id
    rm -rf "$out"; mkdir -p "$out/src"
    tar xzf - -C "$out/src" || { echo "SCREEN status=harness-fail reason=untar" >&2; exit 1; }
    screen_job "$id" "$best" >&2
    cat "$out/bundle.tgz" 2>/dev/null
    rm -rf "$out" ;;
  import)
    w=$(mktemp -d "$TMPDIR/ar-import.XXXXXX")
    tar xzf - -C "$w" && import_kernel "$w/$KREL" "$w/out.loom" "$w/import.log" &&
      cat "$w/out.loom" || { tail -20 "$w/import.log" >&2; rm -rf "$w"; exit 1; }
    rm -rf "$w" ;;
  *) echo "unknown command $cmd" >&2; exit 2 ;;
esac
