#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Build the gfx942 device code objects of both HIP controls, for static
# comparison with Loom. Needs hipcc but no GPU (the ROCm 10 container in
# ../Dockerfile.rocm10 is enough).
#
#   hip_code_objects.sh OUT_DIR
#
# OUT_DIR/hip-original.hsaco   original kernels from ../reference.cpp
#                              (should match ../reference-gfx942-device.hsaco
#                              with the same ROCm release)
# OUT_DIR/hip-rewritten.hsaco  rewritten kernels: the source Loom compiles
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
chem=$(cd -- "$here/.." && pwd)
(($# == 1)) || { echo "usage: hip_code_objects.sh OUT_DIR" >&2; exit 2; }
out=$1
hipcc=${HIPCC:-$(command -v hipcc || echo /opt/rocm/bin/hipcc)}
bundler=${CLANG_OFFLOAD_BUNDLER:-$("$hipcc" -print-prog-name=clang-offload-bundler)}
[[ -x $bundler ]] || { echo "clang-offload-bundler not found (set CLANG_OFFLOAD_BUNDLER)" >&2; exit 2; }
mkdir -p -- "$out"
flags=(--offload-arch=gfx942 -std=c++20 -O3 -ffp-contract=off
  -DPRIMORDIAL_ROS2S_ENABLE_HIP=1 -DPRIMORDIAL_ROS2S_NO_MAIN=1 --genco)

device() {  # source name
  "$hipcc" "${flags[@]}" "$1" -o "$out/$2.bundle"
  "$bundler" --unbundle --type=o --targets=hipv4-amdgcn-amd-amdhsa--gfx942 \
    --input="$out/$2.bundle" --output="$out/$2.hsaco"
  rm -f -- "$out/$2.bundle"
}
device "$chem/reference.cpp" hip-original
python3 "$here/make_hip_rewritten.py" "$out/rewritten-src"
device "$out/rewritten-src/hip-rewritten/reproducer.cpp" hip-rewritten
