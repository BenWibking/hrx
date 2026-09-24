#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Run on a Hot Aisle gfx942/MI300X ROCm VM from the repository checkout.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$here/../.." && pwd)
rocm=${ROCM_PATH:-/opt/rocm}
work=${CHEM_WORK_DIR:-$repo/build/loom-chemistry-rocm}
loom_build=${LOOM_BUILD_DIR:-$repo/build/cmake}
importer=${LOOM_IMPORT_CXX:-$loom_build/loom/src/loom/tools/loom-import-cxx/loom-import-cxx}
compiler=${LOOM_COMPILE:-$loom_build/loom/src/loom/tools/loom-compile/loom-compile}
hipcc=${HIPCC:-$rocm/bin/hipcc}

if [[ ! -x $hipcc ]]; then
  echo "HIP compiler missing: $hipcc (set ROCM_PATH or HIPCC)" >&2
  exit 2
fi
if [[ ! -x $importer || ! -x $compiler ]]; then
  echo "Configuring Loom tools for ROCm gfx942..."
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
    -DIREE_HAL_DRIVER_AMDGPU=ON -DIREE_HAL_AMDGPU_TARGETS=gfx942
  cmake --build "$loom_build" --target loom-import-cxx loom-compile -j "${CHEM_BUILD_JOBS:-4}"
fi
if [[ ! -x $importer || ! -x $compiler ]]; then
  echo "Loom tools missing. Configure this checkout's CMake build (see root README)," >&2
  echo "then set LOOM_BUILD_DIR or LOOM_IMPORT_CXX and LOOM_COMPILE." >&2
  exit 2
fi

mkdir -p -- "$work"
python3 "$here/generate.py" --check
echo "Importing chemistry into Loom IR..."
"$importer" --data-model=lp64 --approximate-functions=false \
  --root=chemistry::prepare_grid_timestep_kernel \
  --root=chemistry::advance_collapse_gridwide_kernel \
  --output="$work/chemistry.loom" "$here/reproducer.cpp"
echo "Compiling Loom prepare kernel for gfx942..."
if ! "$compiler" "$work/chemistry.loom" --product=kernel \
  --root=chemistry.prepare_grid_timestep_kernel --format=amdgpu-hsaco \
  --target=amdgpu:gfx942 --output="$work/chemistry-prepare.hsaco" \
  2>"$work/prepare-compile.log"; then
  tail -n 40 "$work/prepare-compile.log" >&2
  exit 1
fi
echo "Compiling Loom advance kernel for gfx942 (this can take minutes)..."
if ! "$compiler" "$work/chemistry.loom" --product=kernel \
  --root=chemistry.advance_collapse_gridwide_kernel --format=amdgpu-hsaco \
  --target=amdgpu:gfx942 --output="$work/chemistry-advance.hsaco" \
  2>"$work/advance-compile.log"; then
  tail -n 40 "$work/advance-compile.log" >&2
  exit 1
fi

echo "Compiling original HIP kernels and comparison harness..."
"$hipcc" --offload-arch=gfx942 -std=c++20 -O3 -ffp-contract=off \
  -DPRIMORDIAL_ROS2S_ENABLE_HIP=1 -DPRIMORDIAL_ROS2S_NO_MAIN=1 \
  "$here/compare_rocm.cpp" -o "$work/compare_rocm"

echo "Running GPU correctness and performance comparison..."
"$work/compare_rocm" "$work/chemistry-prepare.hsaco" \
  "$work/chemistry-advance.hsaco" "$@"
