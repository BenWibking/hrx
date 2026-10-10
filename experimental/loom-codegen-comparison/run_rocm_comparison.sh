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
# Hot Aisle VMs (Ubuntu + packaged ROCm) need three workarounds to build the
# Loom tools: cmake/ninja are not preinstalled; the ROCm package lacks the AQL
# profile SDK headers, so package-only ROCm dependencies fail to configure; and
# ROCm's clang has no clang-scan-deps and crashes on the vendored C++ parser,
# so the host tools use Ubuntu's clang-20. Set CHEM_HOTAISLE=0/1 to override.
hotaisle=${CHEM_HOTAISLE:-}
if [[ -z $hotaisle ]]; then
  hotaisle=0
  [[ -e /etc/ssh/sshd_config.d/70-hotaisle-auth-keys.conf ]] && hotaisle=1
fi
host_llvm=$rocm/llvm/bin
rocm_dependency_mode=package
if [[ $hotaisle == 1 ]]; then
  host_llvm=/usr/lib/llvm-20/bin
  rocm_dependency_mode=auto
fi

if [[ ! -x $importer || ! -x $compiler ]]; then
  if [[ $hotaisle == 1 ]]; then
    echo "Hot Aisle VM detected: using clang-20 host tools and auto ROCm dependencies."
    missing=()
    command -v cmake >/dev/null || missing+=(cmake)
    command -v ninja >/dev/null || missing+=(ninja-build)
    [[ -x $host_llvm/clang++ ]] || missing+=(clang-20 llvm-20)
    [[ -x $host_llvm/ld.lld ]] || missing+=(lld-20)
    if ((${#missing[@]})); then
      echo "Installing ${missing[*]}..."
      sudo apt-get update -q
      sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q "${missing[@]}"
    fi
  fi
  echo "Configuring Loom tools for ROCm gfx942..."
  cmake -S "$repo" -B "$loom_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_C_COMPILER="$host_llvm/clang" \
    -DCMAKE_CXX_COMPILER="$host_llvm/clang++" \
    -DCMAKE_ASM_COMPILER="$host_llvm/clang" \
    -DCMAKE_AR="$host_llvm/llvm-ar" \
    -DCMAKE_RANLIB="$host_llvm/llvm-ranlib" \
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \
    -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld \
    -DIREE_ROCM_PATH="$rocm" -DIREE_ROCM_DEPENDENCY_MODE="$rocm_dependency_mode" \
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
generated="$work/generated"
python3 "$here/generate.py" --output-dir="$generated"
echo "Importing chemistry into Loom IR..."
"$importer" --data-model=lp64 --approximate-functions=false \
  --root=chemistry::prepare_grid_timestep_kernel \
  --root=chemistry::advance_collapse_gridwide_kernel \
  --I="$here" --output="$work/chemistry.loom" "$generated/reproducer.cpp"
# The kernels take their workgroup count as compile-time config; match the
# ceil(cells / 128) workgroups compare_rocm launches.
# Match reference.cpp's default_grid_dim=64 and the comparison harness default.
cells=$((64 * 64 * 64))
args=("$@")
for ((i = 0; i + 1 < ${#args[@]}; ++i)); do
  [[ ${args[i]} == --cells ]] && cells=${args[i + 1]}
done
[[ $cells =~ ^[1-9][0-9]*$ ]] || { echo "--cells must be a positive integer" >&2; exit 2; }
workgroups=$(((cells + 127) / 128))
workgroup_config() {
  echo "--config=chemistry.$1.workgroup_count.x=$workgroups" \
    "--config=chemistry.$1.workgroup_count.y=1" \
    "--config=chemistry.$1.workgroup_count.z=1"
}
echo "Compiling Loom prepare kernel for gfx942 ($workgroups workgroups)..."
# shellcheck disable=SC2046
if ! "$compiler" "$work/chemistry.loom" $(workgroup_config prepare_grid_timestep_kernel) \
  --root=chemistry.prepare_grid_timestep_kernel --format=amdgpu-hsaco \
  --target=amdgpu:gfx942 --output="$work/chemistry-prepare.hsaco" \
  2>"$work/prepare-compile.log"; then
  tail -n 40 "$work/prepare-compile.log" >&2
  exit 1
fi
echo "Compiling Loom advance kernel for gfx942 (this can take minutes)..."
# shellcheck disable=SC2046
if ! "$compiler" "$work/chemistry.loom" $(workgroup_config advance_collapse_gridwide_kernel) \
  --root=chemistry.advance_collapse_gridwide_kernel --format=amdgpu-hsaco \
  --target=amdgpu:gfx942 --output="$work/chemistry-advance.hsaco" \
  2>"$work/advance-compile.log"; then
  tail -n 40 "$work/advance-compile.log" >&2
  exit 1
fi

echo "Compiling original HIP kernels and comparison harness..."
"$hipcc" --offload-arch=gfx942 -std=c++20 -O3 -ffp-contract=off \
  -DPRIMORDIAL_ROS2S_ENABLE_HIP=1 -DPRIMORDIAL_ROS2S_NO_MAIN=1 \
  -I "$generated" -I "$here" \
  "$here/compare_rocm.cpp" -o "$work/compare_rocm"

echo "Running GPU correctness and performance comparison..."
"$work/compare_rocm" "$work/chemistry-prepare.hsaco" \
  "$work/chemistry-advance.hsaco" "$@"
