# Chemistry reproducer for the Loom C++ importer

`reproducer.cpp` contains both rewritten device kernels and their reachable
chemistry/ROS2S routines. It imports into verified Loom High IR with native
`double` math operations and real integer atomics. Target compilation of f64 math
remains unresolved. This is a device-source rewrite, not a complete
Loom GPU application or a replacement HIP runtime.

The original `/Users/benwibking/amrex_codes/mojo-chemistry/reproducer.cpp` is
unchanged. `reference.cpp` is its byte-for-byte snapshot, SHA256
`6c7ca23933b211980e831e8d2bfc4328245ab0e3c75f6b07a5e0ed40307843d0`.
The source carries BSD-3-Clause SPDX notices. The rewrite specializes the
ordinary HIP path: 15 equations, 14 species, device redshift 30, LP64 source
layout, and the original default 128-thread block. The optional structured CUDA
driver and host-only adjustable redshift are not the selected program.

## Source organization

- `generate.py` extracts the original bodies and applies checked transformations.
  A pinned digest and exact-match replacements reject source drift. No chemistry
  coefficients or floating-point expressions are manually re-derived.
- `CellRecord` groups a named `BurnRecord`, time, step count, and statistics.
  `ScratchRecord` holds per-cell solver arrays, both 15×15 matrices, a temporary
  `BurnRecord`, and integer counters. Generated code accesses these named
  fields directly. Initialization follows the original solver defaults.
- `integrate.inc` retains the solver arithmetic and replaces nested loop exits
  with `done`, `retry`, and a result code. No additional solver iteration executes
  after an original return, break, or continue point.
- `support.h` supplies by-value min/max templates and the original recursive
  `powi` template. Their operand-selection rules and multiplication grouping
  match the original, including min/max behavior for NaNs and signed zero.
- The small `EosSums` aggregate again returns the EOS sums by value, so both
  EOS routines share the source calculation.
- The generator outlines nested ternaries into small helpers with deduced return
  types, preserving conditional evaluation. This avoids Loom's 32-region nesting
  limit without turning lazy branches into eager evaluations. Their captured
  scalar temporaries are already initialized and do not change during the
  expression; species input is read-only.
- Generated chemistry expressions use direct array accesses; the source's
  literal one-based output indices are converted to zero-based indices.
- Buffer and record-field compound assignments retain the source spelling.

Regenerate or check the committed output:

```sh
python3 experimental/loom-chemistry/generate.py
python3 experimental/loom-chemistry/generate.py --check
```

## Buffer and launch contract

Both kernels take the following leading arguments. Allocate disjoint, correctly
aligned buffers for the whole grid; each cell owns its indicated record.

| Argument | Elements per cell | Contents |
| --- | ---: | --- |
| `cells` | 1 `CellRecord` (216 bytes under LP64) | Named rho, T, e, species 0–13, time, density_driver, completed_steps, and seven unsigned 64-bit counters |
| `scratch` | 1 `ScratchRecord` (4,960 bytes under LP64) | Solver state and counters, two 15×15 matrices, temporary burn state, and normalization workspace |

Persistent cell fields and counters must be packed from the original initialized
states. Scratch need not be initialized by the caller: the solver initializes
its fields on every burn; preparation writes its normalization workspace before
reading it. Per-cell scratch is global storage, not shared between lanes. The
record includes four bytes of tail padding under LP64 and is intentionally not
optimized for memory footprint or performance. Buffer sizes and the original
integer cell index/launch bounds remain caller responsibilities.

The remaining arguments retain the original kernel meanings. Supply
`ceil(num_cells / 128)` workgroups along x and one along y/z; specialize the
generated workgroup-count config declarations accordingly. Keep the original
prepare/synchronize/copy-candidates/host-minimum/advance/synchronize ordering.
Do not replace atomic failure publication with a host reduction: it changes
failure selection and cancellation behavior. `reference.cpp` retains the
original host driver and serialization code for a later runtime integration.

## Native equivalence checks

From the repository root:

```sh
cmake -S experimental/loom-chemistry -B /tmp/chemistry-check -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCHEM_SANITIZE=ON \
  -DLOOM_IMPORT_CXX_TOOL=/path/to/loom-import-cxx
cmake --build /tmp/chemistry-check
ctest --test-dir /tmp/chemistry-check --output-on-failure
/tmp/chemistry-check/chemistry-validate --full-grid
```

The tool argument is optional. Without it, CTest runs generation consistency and
native differential validation. Validation requires Clang or GCC and a 64-bit
LP64 host. Floating-point contraction is disabled in both implementations so
the comparison measures the rewrite rather than different contraction choices.

`validate.cpp` compiles the original routines beside the rewrite, uses the same
system double math, and compares double bit patterns and integer values. It
checks RHS/Jacobian values over 18 temperature/density cases, EOS and species
normalization, pivoted and singular LU, solver work arrays and counters, adaptive
retries, early errors, the fifth singular-decomposition failure, both kernel
bodies, inactive lanes, and perturbation step 20. A synthetic diagonal Jacobian
tests the exact same generated solver-control body against the original generic
integrator's singular-retry path. `reference_kernels.inc` is extracted directly
from the original kernels, with only topology and atomic-call adapters, so the
reference is the HIP gridwide policy rather than the different CPU main loop.

The kernel differential check uses the same serial lane order for both versions.
CPU atomic operations remain atomic, but these tests do not validate concurrent
GPU ordering, ROCm libm results, native code generation, or performance. Bitwise
agreement on these cases is evidence for the transformation, not an exhaustive
proof for all inputs.

## Import check and remaining target work

The actual importer can process both roots and all their helpers:

```sh
loom-import-cxx \
  --data-model=lp64 --approximate-functions=false \
  --root=chemistry::prepare_grid_timestep_kernel \
  --root=chemistry::advance_collapse_gridwide_kernel \
  --output=/tmp/chemistry.loom \
  experimental/loom-chemistry/reproducer.cpp
```

`support.h` binds the original integer atomic calls through Loom's typed atomic API.
They emit `view.atomic.rmw<addi>` and `view.atomic.cmpxchg` with relaxed ordering
and device scope, returning the old value. There are no non-atomic stand-ins;
the old `CHEM_IMPORT_STRUCTURE_ONLY` mode has been removed.

The import check keeps `double`, exp/log/sqrt/cbrt/abs, and finite-value testing;
it does not substitute float or grant approximate-math permissions. The gfx942
backend has focused source-low and assembly coverage for f64 arithmetic,
comparisons, conversions, sqrt, and division, as well as signed constant
remainder. Strict f64 division uses the target's DIV_SCALE,
reciprocal-refinement, DIV_FMAS, and DIV_FIXUP sequence.

In the current compiler worktree, `cfg-converge` reconstructs a non-owning
`buffer.view` in its consuming successor, and the HAL kernel ABI places f64
direct arguments in SGPR pairs. Strict f64 math legalization handles `expf`,
`logf`, and `cbrtf`; the focused
`loom/src/loom/target/arch/amdgpu/test/source_low/source_low_f64_strict_math_gfx942.loom-test`
tracks all three. Native AMDGPU emission still has no device-library call/link
path for OCML, so these operations use target recipes.

Both unmodified kernel roots now emit gfx942 HSACO files from a fresh import:
about 21 KB for prepare and 2.1 MB for advance. The advance kernel needs
source-priority scheduling, a 16-round spill-materialization limit, sparse
storage-lifetime-aware scratch selection, and scalable branch-island layout.
Its measured 2.05 MB native instruction stream has 3,805 branches and required
23,185 branch islands. The current source-priority selection applies to all
AMDGPU kernels; its broader performance effect has not been qualified. Advance
compilation still takes minutes and emits thousands of spill warnings.
The [gfx942 Loom versus HIP spill-traffic comparison](SPILL-TRAFFIC-COMPARISON.md)
records static scratch ISA counts from a ROCm 10.0.0 HIP build.

```sh
loom-compile /tmp/chemistry.loom --product=kernel \
  --root=chemistry.prepare_grid_timestep_kernel --format=amdgpu-hsaco \
  --target=amdgpu:gfx942 --output=/tmp/chemistry-prepare.hsaco
loom-compile /tmp/chemistry.loom --product=kernel \
  --root=chemistry.advance_collapse_gridwide_kernel --format=amdgpu-hsaco \
  --target=amdgpu:gfx942 --output=/tmp/chemistry-advance.hsaco
```

The earlier resource-stall schedule could fail after extensive VGPR spilling
with `BACKEND/005 spill-traffic-register-exhausted`, or exhaust the original
eight-round spill-materialization limit. The
`repro/amdgpu-sgpr-spill-traffic` branch contains a standalone Low-IR
reduction of that allocation shape at
`loom/src/loom/target/arch/amdgpu/test/source_low/low_sgpr_spill_traffic_pressure_gfx942.loom-test`.
It keeps the scalar address-add/concat sequence from a lowered scratch load
and places 51 live SGPR pairs around it. The case is XFAIL because allocation
reports `BACKEND/005` with `spill-traffic-register-exhausted`; a 50-pair
control passes within the 102-SGPR budget. Run it with `loom-check` on that
branch; it tests the allocator directly, not the full chemistry pipeline.

The two atomic helpers remain force-inlined. A separately compiled integrator
under an experimental direct-call policy still needs target-specific control
flow, call emission, and ABI support before it can relieve this kernel's
inlining pressure. The emitted HSACO files have not been run on an AMD GPU;
device numerical behavior and performance remain unverified.

## ROCm VM comparison

On a Hot Aisle gfx942/MI300X VM with ROCm installed at `/opt/rocm` and CMake,
Ninja, Python 3, and a C++ build toolchain available, run from this repository
checkout:

```sh
experimental/loom-chemistry/run_rocm_comparison.sh \
  --cells 128 --steps 1 --warmup 1 --repeats 5
```

The script configures and builds `loom-import-cxx` and `loom-compile` when they
are absent, checks generated-source consistency, imports both
kernel roots, compiles separate gfx942 HSACO files, and compiles the pinned
`reference.cpp` HIP kernels with `hipcc`. It writes all artifacts and compiler
logs under `build/loom-chemistry-rocm`. Set `LOOM_BUILD_DIR`, `LOOM_IMPORT_CXX`,
`LOOM_COMPILE`, `ROCM_PATH`, `HIPCC`, or `CHEM_WORK_DIR` to override defaults.
`CHEM_BUILD_JOBS` controls Loom tool build parallelism. Keep enough free memory
and disk space for the large advance-kernel compile.

The executable requires a visible gfx942 device. Use `HIP_VISIBLE_DEVICES` to
select one if the VM exposes multiple GPUs. It initializes the same cells for
both layouts, runs the original prepare/host minimum/advance sequence, and
compares each prepare and advance result. Floating fields use configurable
`--rtol` (default `2e-4`) and `--atol` (default `1e-40`); integer counters and
status values must match exactly. On any mismatch it exits nonzero without
reporting performance. `--steps 25` includes the first perturbation event.

For timing, it resets each backend to the same initial state before every
sample, alternates backend order, and reports median HIP-event device time for
each kernel separately. It also reports median wall time for one complete
prepare/host-minimum/advance step, including synchronization and candidate
copies. Allocation, initial-state reset, and compilation are excluded from all
timings. The benchmark uses step-zero inputs and the
original kernel's first grid timestep for both advance launches; increase
`--cells` to measure larger grids. Results are local to the VM and ROCm stack.

`shared_failure_branch_gfx942.loom-test` is a standalone reduction of the
prepare-kernel branch failure. Run it with `loom-check` to verify both expected
`TARGET/034` diagnostics. Its two lane-dependent checks read separate i32
elements and branch to one store block; it contains no f64 operation or atomic.
The shared block has two incoming CFG edges. AMDGPU's masked-region fallback
requires the true entry to have the current guard as its unique external
predecessor, so neither conditional can use that plan. Giving the two edges
separate copies of the store block removes these branch-planner diagnostics in
the isolated source-to-low run. That experiment does not qualify a full kernel
code object or establish a preferred compiler transformation.

`check_import.py` checks that both roots import and verify with cleanup enabled,
that the four transcendental operations retain f64 types, and that real i32
atomics retain relaxed ordering and device scope. Public entry names are
`@chemistry.prepare_grid_timestep_kernel` and
`@chemistry.advance_collapse_gridwide_kernel`.

The typed-record rewrite passed generation consistency, native differential
tests under AddressSanitizer/UBSan on macOS arm64 with Apple Clang 21.0.0, and
both kernel-root imports using a fresh tool built from this checkout. The
four-cell, 1,000-step run passed 235,705 exact field comparisons with contraction
disabled. These native and frontend results do not establish device correctness.
