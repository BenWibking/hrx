#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Bootstraps a fresh Hot Aisle MI300X (gfx942) VM for the Loom/HIP chemistry
# comparison, running every independent step concurrently. Run from the laptop:
#
#   experimental/loom-codegen-comparison/bootstrap_hotaisle.sh [user@host]
#
# Dependency graph (arrows are waits; everything else overlaps):
#   apt toolchain ----------------------------\
#   working-copy rsync --+-> generate + hipcc   +-> configure -> Loom tools --+-> kernels --+-> compare
#                        \-> harness upload      |                           \-> loom tests|
#   ccache restore (optional) -----------------/                                           |
#                                     compare_rocm (from generate + hipcc) ----------------/
#
# Environment:
#   HOTAISLE_HOST      ssh destination (default hotaisle@23.183.40.70; arg 1 wins)
#   CCACHE_ARCHIVE_URL optional .tar.zst/.tar.gz of a ccache dir, fetched on the VM
#   HARNESS_DIR        local dir of extra scripts to copy to ~ (default
#                      .notes/advance-autoresearch/harness when it exists)
#   SKIP_TESTS=1       skip building and running the Loom test subset
#   SKIP_RUN=1         skip the final GPU comparison run
#   JOBS               remote build parallelism (default: remote nproc)
set -euo pipefail

host=${1:-${HOTAISLE_HOST:-hotaisle@23.183.40.70}}
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$here/../.." && pwd)
# shellcheck disable=SC2016  # $HOME must expand on the VM.
remote_repo='$HOME/hrx-system'
harness_dir=${HARNESS_DIR:-$repo/.notes/advance-autoresearch/harness}
logs=$(mktemp -d "${TMPDIR:-/tmp}/hotaisle-bootstrap.XXXXXX")

# One multiplexed SSH connection keeps parallel steps from paying handshake cost.
control="$logs/ssh-control"
ssh_opts=(-o ConnectTimeout=20 -o ServerAliveInterval=30
          -o StrictHostKeyChecking=accept-new
          -o ControlMaster=auto -o "ControlPath=$control" -o ControlPersist=600)
rsh="ssh ${ssh_opts[*]}"
# shellcheck disable=SC2029  # Remote commands are built for remote expansion.
remote() { ssh "${ssh_opts[@]}" "$host" "$@"; }
trap 'ssh "${ssh_opts[@]}" -O exit "$host" >/dev/null 2>&1 || true' EXIT

log() { echo "[$(date +%H:%M:%S)] $*"; }

# Runs "$@" in the background, logging to $logs/<name>.log; records the PID
# in $logs/<name>.pid (portable to the bash 3.2 that ships with macOS).
start() {
  local name=$1; shift
  ( "$@" ) >"$logs/$name.log" 2>&1 &
  echo $! >"$logs/$name.pid"
  log "started $name"
}
# Waits for the named steps; prints the tail of any failed log and exits.
await() {
  local name
  for name in "$@"; do
    if ! wait "$(cat "$logs/$name.pid")"; then
      log "FAILED $name (log: $logs/$name.log)"
      tail -n 40 "$logs/$name.log" >&2
      exit 1
    fi
    log "done $name"
  done
}

log "host $host, logs in $logs"
remote true  # Open the control master and surface host-key problems early.

# --- Phase 1: independent of each other -------------------------------------
step_apt() {
  remote 'set -e
    sudo apt-get update -q
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q \
      cmake ninja-build clang-20 lld-20 llvm-20 ccache'
}
step_sync() {
  # Working copy only: the build never reads .git history.
  remote "mkdir -p $remote_repo"
  rsync -a --delete -e "$rsh" \
    --exclude=/.git/ --exclude=/build/ --exclude='/build-*/' \
    --exclude=/.claude/ --exclude=/.notes/ --exclude='/bazel-*' \
    --exclude=/.tmp/ --exclude=.DS_Store --exclude=/.beads/ --exclude=/.bv/ \
    "$repo/" "$host:hrx-system/"
}
step_ccache() {
  if [[ -z ${CCACHE_ARCHIVE_URL:-} ]]; then
    echo "no CCACHE_ARCHIVE_URL; starting with an empty cache"
    return 0
  fi
  remote "set -e; mkdir -p \$HOME/.cache/ccache
    curl -fsSL -o /tmp/ccache-archive '$CCACHE_ARCHIVE_URL'
    tar -xf /tmp/ccache-archive -C \$HOME/.cache/ccache
    rm -f /tmp/ccache-archive"
}
start apt step_apt
start sync step_sync
start ccache step_ccache

# --- Phase 2a: needs sources only (ROCm and python3 ship with the image) ----
await sync
step_harness() {
  if [[ -d $harness_dir ]]; then
    rsync -a -e "$rsh" "$harness_dir/" "$host:"
  else
    echo "no harness dir at $harness_dir"
  fi
}
step_hip() {
  remote "set -e
    here=$remote_repo/experimental/loom-codegen-comparison
    work=$remote_repo/build/loom-chemistry-rocm
    mkdir -p \$work
    python3 \$here/generate.py --output-dir=\$work/generated
    /opt/rocm/bin/hipcc --offload-arch=gfx942 -std=c++20 -O3 -ffp-contract=off \
      -DPRIMORDIAL_ROS2S_ENABLE_HIP=1 -DPRIMORDIAL_ROS2S_NO_MAIN=1 \
      -I \$work/generated -I \$here \$here/compare_rocm.cpp -o \$work/compare_rocm"
}
start harness step_harness
start hip step_hip

# --- Phase 2b: configure and build the Loom tools ----------------------------
await apt ccache
step_tools() {
  # Matches run_rocm_comparison.sh's Hot Aisle configuration so that script
  # later finds the tools and skips its own configure.
  remote "set -e
    repo=$remote_repo; b=\$repo/build/cmake; llvm=/usr/lib/llvm-20/bin; rocm=/opt/rocm
    jobs=\${JOBS:-\$(nproc)}
    cmake -S \$repo -B \$b -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib \
      -DCMAKE_C_COMPILER=\$llvm/clang -DCMAKE_CXX_COMPILER=\$llvm/clang++ \
      -DCMAKE_ASM_COMPILER=\$llvm/clang \
      -DCMAKE_AR=\$llvm/llvm-ar -DCMAKE_RANLIB=\$llvm/llvm-ranlib \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
      -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
      -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \
      -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld \
      -DIREE_ROCM_PATH=\$rocm -DIREE_ROCM_DEPENDENCY_MODE=auto \
      -DLOOM_BUILD=ON -DLOOM_IMPORT_CXX=ON \
      -DLOOM_TARGET_DEFAULTS=OFF -DLOOM_TARGET_AMDGPU=ON \
      -DIREE_HAL_DRIVER_AMDGPU=ON -DIREE_HAL_AMDGPU_TARGETS=gfx942
    ninja -C \$b -j \$jobs loom-import-cxx loom-compile"
}
start tools step_tools
await tools

# --- Phase 3: kernels and tests overlap (kernel compiles are single-threaded) -
step_kernels() {
  remote "set -e
    repo=$remote_repo; b=\$repo/build/cmake; work=\$repo/build/loom-chemistry-rocm
    here=\$repo/experimental/loom-codegen-comparison
    mkdir -p \$work/bin
    # Private copies so a concurrent test build cannot relink them mid-run.
    cp \$b/loom/src/loom/tools/loom-import-cxx/loom-import-cxx \$work/bin/
    cp \$b/loom/src/loom/tools/loom-compile/loom-compile \$work/bin/
    [[ -d \$work/generated ]] || python3 \$here/generate.py --output-dir=\$work/generated
    \$work/bin/loom-import-cxx --data-model=lp64 --approximate-functions=false \
      --root=chemistry::prepare_grid_timestep_kernel \
      --root=chemistry::advance_collapse_gridwide_kernel \
      --I=\$here --output=\$work/chemistry.loom \$work/generated/reproducer.cpp
    cfg() { echo --config=chemistry.\$1.workgroup_count.x=2048 \
      --config=chemistry.\$1.workgroup_count.y=1 --config=chemistry.\$1.workgroup_count.z=1; }
    \$work/bin/loom-compile \$work/chemistry.loom \$(cfg prepare_grid_timestep_kernel) \
      --root=chemistry.prepare_grid_timestep_kernel --format=amdgpu-hsaco \
      --target=amdgpu:gfx942 --output=\$work/chemistry-prepare.hsaco &
    prep=\$!
    \$work/bin/loom-compile \$work/chemistry.loom \$(cfg advance_collapse_gridwide_kernel) \
      --root=chemistry.advance_collapse_gridwide_kernel --format=amdgpu-hsaco \
      --target=amdgpu:gfx942 --output=\$work/chemistry-advance.hsaco
    wait \$prep"
}
step_tests() {
  if [[ ${SKIP_TESTS:-0} == 1 ]]; then echo "skipped"; return 0; fi
  # The IREE HAL AMDGPU runtime does not build against ROCm 7.2.4 HSA headers,
  # so build only Loom targets and fail only on Loom build failures.
  remote "set -uo pipefail
    b=$remote_repo/build/cmake; jobs=\${JOBS:-\$(nproc)}
    ninja -C \$b -k 0 -j \$((jobs > 2 ? jobs - 2 : 1)) loom/src/loom/all > \$b/bootstrap-tests-build.log 2>&1
    nfail=\$(grep '^FAILED' \$b/bootstrap-tests-build.log | grep -vc 'runtime/src/iree/hal' || true)
    echo \"loom build failures: \$nfail\"
    [[ \$nfail == 0 ]] || { grep '^FAILED' \$b/bootstrap-tests-build.log | grep -v runtime/src/iree/hal | head; exit 1; }
    cd \$b && ctest -j \$jobs --output-on-failure -R \
      '^loom/(codegen/low/(allocation|test/allocation|transforms/test/allocation)|target/arch/amdgpu/(test/source_low|planning)|target/emit/native/amdgpu)' \
      | tail -n 5"
}
start kernels step_kernels
start tests step_tests
await harness hip kernels

# --- Phase 4: validate on the GPU while tests finish -------------------------
if [[ ${SKIP_RUN:-0} != 1 ]]; then
  log "running GPU comparison"
  remote "w=$remote_repo/build/loom-chemistry-rocm
    \$w/compare_rocm \$w/chemistry-prepare.hsaco \$w/chemistry-advance.hsaco" \
    | tee "$logs/compare.log" | grep -E '^(GPU|PASS|FAIL)|median'
  grep -q '^PASS' "$logs/compare.log" || { log "comparison did not PASS"; exit 1; }
fi
await tests
cat "$logs/tests.log"
remote 'ccache -s 2>/dev/null | grep -iE "hits|size" | head -4' || true
log "bootstrap complete"
