# f64 chemistry kernels for HIP and Loom

This directory contains the original HIP chemistry reproducer and its generated
Loom C++ translation. Both use `double` and retain the prepare/advance gridwide
kernel pair: 15 equations, 14 species, device redshift 30, and 128-thread blocks.

`reference.cpp` is the unchanged HIP source snapshot, SHA256
`6c7ca23933b211980e831e8d2bfc4328245ab0e3c75f6b07a5e0ed40307843d0`.
`reproducer.cpp` contains the Loom kernel roots and reachable chemistry/ROS2S
routines. Sources carry their original license notices.

## Files

| Files | Purpose |
| --- | --- |
| `reference.cpp` | Original HIP kernels and standalone host driver |
| `reproducer.cpp`, `integrate.inc` | Generated Loom device program and solver control flow |
| `support.h`, `f64_math.h` | Loom math, atomics, and strict f64 exp/log/cbrt source recipes |
| `generate.py` | Checked translation from the pinned HIP source |
| `validate.cpp`, `reference_kernels.inc`, `CMakeLists.txt` | Native differential validation against HIP source bodies |
| `check_import.py` | Checks both imported roots, f64 math, private storage, and atomics |
| `compare_rocm.cpp`, `run_rocm_comparison.sh` | Build and run the HIP/Loom GPU comparison |
| `HIP-TO-LOOM-REWRITES.md` | Translation and numerical-contract notes |

## Generate and validate

Run from this directory:

```sh
python3 generate.py --check
# After editing translation rules:
python3 generate.py

cmake -S . -B /tmp/chemistry-check -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCHEM_SANITIZE=ON
cmake --build /tmp/chemistry-check
ctest --test-dir /tmp/chemistry-check --output-on-failure
/tmp/chemistry-check/chemistry-validate --full-grid
```

Clang or GCC and a 64-bit LP64 host are required. Set
`-DLOOM_IMPORT_CXX_TOOL=/path/to/loom-import-cxx` when configuring to include the
import check. Native validation compares double bit patterns and integer fields
using the same host math and serial lane order; it does not establish GPU math
results, concurrent atomic behavior, or GPU performance.

## Build the HIP reproducer

On a ROCm machine, compile the original standalone driver:

```sh
hipcc --offload-arch=gfx942 -std=c++20 -O3 -ffp-contract=off \
  -DPRIMORDIAL_ROS2S_ENABLE_HIP=1 reference.cpp -o /tmp/chemistry-hip
/tmp/chemistry-hip --help
```

The standalone HIP driver defaults to a `64^3` grid (262,144 cells). Use
`--grid N` to override the number of cells along each axis.

## Import and compile the Loom kernels

```sh
loom-import-cxx --data-model=lp64 --approximate-functions=false \
  --root=chemistry::prepare_grid_timestep_kernel \
  --root=chemistry::advance_collapse_gridwide_kernel \
  --output=/tmp/chemistry.loom reproducer.cpp

for root in prepare_grid_timestep_kernel advance_collapse_gridwide_kernel; do
  loom-compile /tmp/chemistry.loom --root=chemistry.$root \
    --config=chemistry.$root.workgroup_count.x=2048 \
    --config=chemistry.$root.workgroup_count.y=1 \
    --config=chemistry.$root.workgroup_count.z=1 \
    --format=amdgpu-hsaco --target=amdgpu:gfx942 \
    --output=/tmp/chemistry-$root.hsaco
done
```

The example configures a `64^3` grid: 262,144 cells in 2,048 workgroups of 128
threads. For other sizes, bind the x workgroup count
to `ceil(num_cells / 128)`. Each cell requires a disjoint, correctly aligned
`CellRecord` (216 bytes under LP64); solver work arrays are private to each lane.
The caller must preserve prepare, synchronize, copy candidates, host minimum,
advance, and synchronize ordering, including atomic failure publication.

### Workgroup size and register occupancy

Each lane evolves one cell, and `KERNEL` fixes the workgroup size at 128 threads
(`[[loom::workgroup_size(128, 1, 1)]]` in `support.h`), which is two wave64
waves on gfx942. 128 threads per block was historically the best-performing
choice for the HIP kernel, so both versions keep it.

A wave can address at most 256 VGPRs (`v0`-`v255`) whatever the workgroup size.
On gfx942 a workgroup is resident on one compute unit with four SIMDs, each
holding 512 unified VGPR/AGPR slots per lane, so the workgroup size limits
registers only once more than one wave must share a SIMD:

| Threads per workgroup | Waves per SIMD | Slots per wave | VGPRs per wave |
| --- | --- | --- | --- |
| 64-256 | 1 | 512 | 256, plus up to 256 AGPRs |
| 512 | 2 | 256 | 256, with no AGPRs |
| 1,024 | 4 | 128 | 128 |

The tradeoff is registers against occupancy. A kernel that uses fewer registers
per wave lets more waves stay resident on each SIMD to hide memory and
instruction latency, but a register-heavy kernel like this one spills more when
its budget shrinks. At 128 threads the advance kernel already has the full
256-VGPR budget, so a smaller workgroup would not give it more registers, and a
larger one would not relieve the allocation failure below.

### Known limitation: production-sized advance kernel

Loom cannot currently compile the advance kernel for the production `64^3`
grid. With `workgroup_count.x=2048`, gfx942 VGPR allocation fails on both macOS
and Linux hosts from the same imported module:

```text
error [BACKEND/005]: ... failed to allocate amdgpu.vgpr registers for
'@chemistry.advance_collapse_gridwide_kernel' with budget 256, peak 531,
and failure code 'spill-traffic-register-exhausted'
```

The same module compiles with `workgroup_count.x=1` (up to 128 cells). The
workgroup count is compile-time config, so the cell count selects which kernel
is compiled. The prepare kernel compiles at both sizes.

## Compare HIP and Loom on a GPU

From a repository checkout on a gfx942 ROCm machine:

```sh
./run_rocm_comparison.sh
# Override the default cell count or number of correctness steps:
./run_rocm_comparison.sh --cells 128 --steps 1000
```

Both HIP and Loom default to 262,144 cells (`64^3`), with one correctness step,
one warmup, and five timing repeats. The script matches Loom's compiled
workgroup count to the cell count used by the comparison harness. Cells are
stored as a flat array; the chemistry kernels evolve each cell independently.
The default size does not yet compile in Loom (see the known limitation above);
use `--cells 128` until it does.

The script checks generated sources, imports and compiles both Loom roots,
builds the original HIP kernels with the comparison harness, and runs numerical
and timing comparisons. Set `ROCM_PATH` or `HIPCC` for ROCm, and `LOOM_BUILD_DIR`
or `LOOM_IMPORT_CXX` and `LOOM_COMPILE` for checkout-local Loom tools. Outputs
default to `build/loom-chemistry-rocm` at the repository root; override with
`CHEM_WORK_DIR`. If Loom tools are missing, the script configures and builds them.

`f64_math.h` supplies exp/log/cbrt recipes for the Loom device path; native
validation uses system math. Import checks and code-object emission alone do
not validate those recipes against HIP device math; use the GPU comparison for
that evidence.
