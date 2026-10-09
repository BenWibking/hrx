#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Matched-input HIP versus Loom replay on a gfx942 (MI300X) ROCm machine.
#
#   run_gpu.sh [--repeats N] [--replicas N] [--no-counters]
#
# 0. In the HRX checkout, configures and builds loom-compile with ROCm's
#    clang when it is missing (same settings as ../run_rocm_comparison.sh;
#    CHEM_BUILD_JOBS sets parallelism). Skipped when LOOM_COMPILE is set.
# 1. Compiles the Loom kernels with compile.sh unless LOOM_HSACO_DIR names a
#    directory that already holds chemistry-{prepare,advance}.hsaco.
# 2. Builds two replay drivers with hipcc: one against the original HIP
#    kernels (reference.cpp) and one against the HIP-rewritten control, the
#    same source Loom compiles.
# 3. Replays saved advance launches at steps 0, 100, 500, 920 and 999, and
#    the step-500 prepare launch. Each driver checks HIP against Loom results
#    before reporting any time.
# 4. Unless --no-counters, collects rocprofv3 counters for the step-500
#    advance of each backend.
# 5. With --replicas N (N > 1), recompiles Loom with workgroup_count.x=N and
#    replays step 500 across N independent copies of the 128-cell snapshot.
#
# Outputs go to $GPU_WORK_DIR (default build/loom-chemistry-advance-spill-repro/gpu,
# or out/gpu in an extracted tarball).
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
chem=$(cd -- "$here/.." && pwd)
if repo=$(git -C "$chem" rev-parse --show-toplevel 2>/dev/null); then
  default_work=$repo/build/loom-chemistry-advance-spill-repro/gpu
else
  repo='' default_work=$here/out/gpu
fi
rocm=${ROCM_PATH:-/opt/rocm}
hipcc=${HIPCC:-$rocm/bin/hipcc}
rocprof=${ROCPROFV3:-$rocm/bin/rocprofv3}
work=${GPU_WORK_DIR:-$default_work}
repeats=5
replicas=1
counters=1
while (($#)); do
  case $1 in
    --repeats) repeats=$2; shift 2 ;;
    --replicas) replicas=$2; shift 2 ;;
    --no-counters) counters=0; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ -x $hipcc ]] || { echo "HIP compiler missing: $hipcc (set ROCM_PATH or HIPCC)" >&2; exit 2; }
mkdir -p -- "$work"

loom=${LOOM_HSACO_DIR:-$work/loom}
need_compile=0
[[ -f $loom/chemistry-advance.hsaco && -f $loom/chemistry-prepare.hsaco ]] || need_compile=1
((replicas > 1)) && need_compile=1
if ((need_compile)) && [[ -z ${LOOM_COMPILE:-} ]]; then
  [[ -n ${repo:-} ]] || { echo "Outside the HRX checkout: set LOOM_COMPILE" >&2; exit 2; }
  loom_build=${LOOM_BUILD_DIR:-$repo/build/cmake}
  export LOOM_COMPILE=$loom_build/loom/src/loom/tools/loom-compile/loom-compile
  if [[ ! -x $LOOM_COMPILE ]]; then
    echo "Configuring and building loom-compile for gfx942 (log: $work/loom-build.log)..."
    {
      cmake -S "$repo" -B "$loom_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_C_COMPILER="$rocm/llvm/bin/clang" \
        -DCMAKE_CXX_COMPILER="$rocm/llvm/bin/clang++" \
        -DCMAKE_ASM_COMPILER="$rocm/llvm/bin/clang" \
        -DCMAKE_AR="$rocm/llvm/bin/llvm-ar" \
        -DCMAKE_RANLIB="$rocm/llvm/bin/llvm-ranlib" \
        -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
        -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \
        -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld \
        -DIREE_ROCM_PATH="$rocm" -DIREE_ROCM_DEPENDENCY_MODE=package \
        -DLOOM_BUILD=ON -DLOOM_IMPORT_CXX=ON \
        -DLOOM_TARGET_DEFAULTS=OFF -DLOOM_TARGET_AMDGPU=ON \
        -DIREE_HAL_DRIVER_AMDGPU=ON -DIREE_HAL_AMDGPU_TARGETS=gfx942 &&
      cmake --build "$loom_build" --target loom-compile -j "${CHEM_BUILD_JOBS:-4}"
    } >"$work/loom-build.log" 2>&1 || { tail -n 30 "$work/loom-build.log" >&2; exit 1; }
  fi
fi
if [[ ! -f $loom/chemistry-advance.hsaco || ! -f $loom/chemistry-prepare.hsaco ]]; then
  CHEM_WORK_DIR=$loom "$here/compile.sh"
fi

# Same flags as the pinned HIP reference; contraction off for both HIP builds
# so neither HIP control gets FMA contraction that strict Loom does not.
hipflags=(--offload-arch=gfx942 -std=c++20 -O3 -ffp-contract=off
  -DPRIMORDIAL_ROS2S_ENABLE_HIP=1 -DPRIMORDIAL_ROS2S_NO_MAIN=1)
echo "Building HIP replay drivers..."
build_failed() { tail -n 30 "$1" >&2; echo "HIP build failed; see $1" >&2; exit 1; }
"$hipcc" "${hipflags[@]}" "$here/replay.cpp" -o "$work/replay" 2>"$work/replay-build.log" ||
  build_failed "$work/replay-build.log"
python3 "$here/make_hip_rewritten.py" "$work/rewritten-src"
"$hipcc" "${hipflags[@]}" \
  "-DREPLAY_HARNESS=\"$work/rewritten-src/compare_hip_rewritten.cpp\"" \
  '-DREPLAY_HIP_LABEL="HIP-rewritten"' \
  "$here/replay.cpp" -o "$work/replay-rewritten" 2>"$work/replay-rewritten-build.log" ||
  build_failed "$work/replay-rewritten-build.log"
HIPCC=$hipcc "$here/hip_code_objects.sh" "$work/hip"
spill_log=()
[[ -f $loom/advance-compile.log ]] && spill_log=(--log "Loom:advance=$loom/advance-compile.log")
LLVM_BIN=${LLVM_BIN:-$rocm/llvm/bin} python3 "$here/static_counts.py" \
  --json "$work/static-counts.json" ${spill_log[@]+"${spill_log[@]}"} \
  "Loom=$loom/chemistry-advance.hsaco" "HIP=$work/hip/hip-original.hsaco" \
  "HIP-rewritten=$work/hip/hip-rewritten.hsaco" >"$work/static-counts.md"

replay() {  # driver name args...
  local driver=$1 name=$2; shift 2
  echo "  $name"
  "$work/$driver" "$loom/chemistry-prepare.hsaco" "$loom/chemistry-advance.hsaco" \
    "$here/snapshots" --warmup 1 --repeats "$repeats" "$@" >"$work/$name.csv"
}
echo "Replaying saved launches..."
for step in 0 100 500 920 999; do
  replay replay "advance-$step" --step "$step"
done
replay replay prepare-500 --step 500 --phase prepare
replay replay-rewritten rewritten-advance-500 --step 500

if ((counters)); then
  [[ -x $rocprof ]] || { echo "rocprofv3 missing: $rocprof (set ROCPROFV3 or pass --no-counters)" >&2; exit 2; }
  echo "Collecting hardware counters (step 500 advance)..."
  for backend in hip loom; do
    "$rocprof" -i "$here/counters.txt" -d "$work/counters-$backend" -o counters \
      --output-format csv -- "$work/replay" "$loom/chemistry-prepare.hsaco" \
      "$loom/chemistry-advance.hsaco" "$here/snapshots" --step 500 \
      --backend "$backend" --warmup 1 --repeats 1 >"$work/counters-$backend.log" 2>&1
  done
  "$rocprof" -i "$here/counters.txt" -d "$work/counters-hip-rewritten" -o counters \
    --output-format csv -- "$work/replay-rewritten" "$loom/chemistry-prepare.hsaco" \
    "$loom/chemistry-advance.hsaco" "$here/snapshots" --step 500 \
    --backend hip --warmup 1 --repeats 1 >"$work/counters-hip-rewritten.log" 2>&1
fi

if ((replicas > 1)); then
  echo "Replaying step 500 across $replicas workgroups..."
  CHEM_WORK_DIR=$work/loom-replicas-$replicas "$here/compile.sh" --replicas "$replicas"
  "$work/replay" "$work/loom-replicas-$replicas/chemistry-prepare.hsaco" \
    "$work/loom-replicas-$replicas/chemistry-advance.hsaco" "$here/snapshots" \
    --step 500 --replicas "$replicas" --warmup 1 --repeats "$repeats" >"$work/replicas-$replicas.csv"
fi

# Median kernel time per backend for each replay, excluding warmups.
python3 - "$work" <<'EOF'
import csv, statistics, sys
from pathlib import Path
work = Path(sys.argv[1])
print(f'\n{"replay":<24} {"backend":<14} {"median ms":>12} {"ratio":>8}  work (integrated/steps/rhs/jac)')
for path in sorted(work.glob('*.csv')):
    rows = [r for r in csv.DictReader(path.open()) if int(r['repeat']) >= 0]
    medians = {}
    for backend in dict.fromkeys(r['backend'] for r in rows):
        sample = [r for r in rows if r['backend'] == backend]
        medians[backend] = statistics.median(float(r['ms']) for r in sample)
    base = next((v for k, v in medians.items() if k != 'Loom'), None)
    for backend, ms in medians.items():
        r = next(r for r in rows if r['backend'] == backend)
        ratio = f'{ms / base:.2f}x' if base and backend == 'Loom' else ''
        print(f'{path.stem:<24} {backend:<14} {ms:>12.4f} {ratio:>8}  '
              f'{r["integrated"]}/{r["internal_steps"]}/{r["rhs_calls"]}/{r["jacobians"]}')
EOF
echo "Outputs in $work"
