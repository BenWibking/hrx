#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Rebuilds loom-import-cxx and loom-compile for the tasks in README.md, then
# optionally configures, builds, and tests the chemistry check tree against them.
#
#   ./rebuild_loom_tools.sh [options] [-- extra cmake configure args]
#
# Options:
#   --clean        Delete the Loom build dir's CMakeCache.txt and reconfigure
#   --check        Also configure/build the README check tree with both tools
#                  wired in and run ctest (includes the multi-minute
#                  production-advance-compile test)
#   --quick-check  Like --check but skips production-advance-compile
#   -j N           Build parallelism (default: ninja's default)
#
# Environment:
#   LOOM_BUILD_DIR  Loom CMake tree (default: <repo>/build/cmake; the same
#                   default run_rocm_comparison.sh uses)
#   CHEM_CHECK_DIR  Check tree for --check (default: /tmp/chemistry-check)
#   PYTHON          Python interpreter (default: <repo>/build/py312-venv/bin/python
#                   when present, else python3)
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$here/../.." && pwd)
loom_build=${LOOM_BUILD_DIR:-$repo/build/cmake}
check_dir=${CHEM_CHECK_DIR:-/tmp/chemistry-check}
importer=$loom_build/loom/src/loom/tools/loom-import-cxx/loom-import-cxx
compiler=$loom_build/loom/src/loom/tools/loom-compile/loom-compile

python=${PYTHON:-}
if [[ -z $python ]]; then
  python=python3
  [[ -x $repo/build/py312-venv/bin/python ]] && python=$repo/build/py312-venv/bin/python
fi

clean=0
check=none
jobs=()
extra_cmake=()
while (($#)); do
  case $1 in
    --clean) clean=1 ;;
    --check) check=full ;;
    --quick-check) check=quick ;;
    -j) jobs=(-j "$2"); shift ;;
    -j*) jobs=(-j "${1#-j}") ;;
    --) shift; extra_cmake=("$@"); break ;;
    -h|--help) sed -n '3,22p' "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
  esac
  shift
done

for tool in cmake ninja "$python"; do
  command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 2; }
done

log() { echo "==> $*"; }

if ((clean)); then
  log "Removing $loom_build/CMakeCache.txt"
  rm -f -- "$loom_build/CMakeCache.txt"
fi

# An existing tree keeps its cached configuration (compilers, ROCm settings
# from run_rocm_comparison.sh or bootstrap_hotaisle.sh, ...); cmake --build
# reruns configure on its own when CMake inputs change.
if [[ ! -f $loom_build/CMakeCache.txt || ${#extra_cmake[@]} -gt 0 ]]; then
  log "Configuring Loom tools in $loom_build"
  cmake -S "$repo" -B "$loom_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$(command -v "$python")" \
    -DLOOM_BUILD=ON -DLOOM_IMPORT_CXX=ON \
    -DLOOM_TARGET_DEFAULTS=OFF -DLOOM_TARGET_AMDGPU=ON \
    ${extra_cmake[@]+"${extra_cmake[@]}"}
fi

log "Building loom-import-cxx and loom-compile"
cmake --build "$loom_build" --target loom-import-cxx loom-compile ${jobs[@]+"${jobs[@]}"}

for tool in "$importer" "$compiler"; do
  [[ -x $tool ]] || { echo "build finished but $tool is missing" >&2; exit 1; }
done

if [[ $check != none ]]; then
  log "Configuring chemistry check tree in $check_dir"
  cmake -S "$here" -B "$check_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCHEM_SANITIZE=ON \
    -DPython3_EXECUTABLE="$(command -v "$python")" \
    -DLOOM_IMPORT_CXX_TOOL="$importer" -DLOOM_COMPILE_TOOL="$compiler"
  cmake --build "$check_dir" ${jobs[@]+"${jobs[@]}"}
  ctest_args=(--test-dir "$check_dir" --output-on-failure)
  if [[ $check == quick ]]; then
    ctest_args+=(-E '^production-advance-compile$')
  else
    log "production-advance-compile takes several minutes"
  fi
  ctest "${ctest_args[@]}"
fi

cat <<EOF

Loom tools are ready:
  $importer
  $compiler

For the README's bare loom-import-cxx / loom-compile commands:
  export PATH="$(dirname "$importer"):$(dirname "$compiler"):\$PATH"
For run_rocm_comparison.sh (default already matches when unset):
  export LOOM_BUILD_DIR="$loom_build"
For the README check build:
  cmake -S . -B $check_dir -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCHEM_SANITIZE=ON \\
    -DLOOM_IMPORT_CXX_TOOL=$importer \\
    -DLOOM_COMPILE_TOOL=$compiler
EOF
