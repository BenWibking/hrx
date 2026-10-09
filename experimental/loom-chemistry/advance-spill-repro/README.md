# Loom advance-kernel spill and performance reproducer (gfx942)

Loom compiles the chemistry `advance_collapse_gridwide_kernel` to gfx942 code
with about 4x the instructions, 5x the scratch traffic sites and 1,400x the
unconditional branches of HIP compiling the **same source**. On an MI300X,
the Loom kernel was 6–7x slower than HIP with identical inputs and identical
solver work (2026-10-01 build; see [GPU results](#gpu-results)). This
directory reproduces both observations from a pinned compiler input.

Everything here is the actual application kernel. Small synthetic reductions
of the allocator failure did not reproduce the full-kernel behavior (see
`build/loom-chemistry-gfx942/diagnose-spill-20261008/diagnosis.json` in the
originating checkout), so the full imported module is the reproducer.

## Contents

| File | Purpose |
| --- | --- |
| `chemistry.loom` | Compiler input: both kernels imported from `../reproducer.cpp` with `loom-import-cxx --data-model=lp64 --approximate-functions=false` at HRX `0be406433a` (upstream f64 lowering; exp/log/cbrt from `../f64_math.h`). SHA256 `ab3289ef…9cca`. |
| `compile.sh` | Compiles both kernels for gfx942 and prints static metrics beside the pinned HIP object. No GPU needed. `--reimport` regenerates the module from source first. |
| `static_counts.py` | Static code-object metrics (scratch, branches, EXEC writes, waits, resources) plus a summary of Loom's `BACKEND/009` spill diagnostics. |
| `hip_code_objects.sh` | Builds device code objects for the original HIP kernels and the HIP-rewritten control. Needs `hipcc` only (the `../Dockerfile.rocm10` container works). |
| `make_hip_rewritten.py` | Generates the HIP-rewritten control: Loom's input source (`../reproducer.cpp`, `../integrate.inc`) compiled by `hipcc`, with only HIP declarations added to `support.h`. |
| `replay.cpp` | Matched-input GPU replay: loads a saved 128-cell state, runs HIP and Loom from that state, checks the results agree, then times each kernel. |
| `run_gpu.sh` | GPU driver: compiles, builds both replays, runs the saved launches, collects `rocprofv3` counters. |
| `counters.txt` | `rocprofv3` counter sets (two passes). |
| `snapshots/` | Inputs extracted from a 1,000-step HIP full-collapse run: advance inputs at steps 0, 100, 500, 920, 999 and the step-500 prepare input, 27,648 bytes each, with time, dt and SHA256 in `snapshots.csv`. |
| `extract_snapshots.py` | Provenance: how `snapshots/` was cut from the full-run state dump. |
| `results/static-2026-10-08.json` | The static metrics below, machine-readable. |
| `package.sh` | Packs this directory and the shared `../` sources into a standalone tarball. |

The replay and HIP controls also use `../compare_rocm.cpp`,
`../reference.cpp` (the pinned original HIP source), and the generated
`../reproducer.cpp`, `../integrate.inc` and `../support.h`.

## Reproduce without a GPU

From the HRX checkout, with `loom-compile` built (see `../README.md`):

```sh
experimental/loom-chemistry/advance-spill-repro/compile.sh
```

This writes the code objects, compile logs, `--compile-report=summary`
JSON, `static-counts.{md,json}` and `build-manifest.json` (HEAD, tool and
artifact hashes, wall time, spill-warning count) to
`build/loom-chemistry-advance-spill-repro/`. The advance compile takes
about 3.5 minutes and emits about 8,300 `BACKEND/009` warnings. Outside the
checkout, set `LOOM_COMPILE=/path/to/loom-compile`.

The kernels declare their workgroup count as compile-time config; the
compiler rejects the module with `CONFIG/INVALID` unless all three are bound.
`compile.sh` binds them to `(1, 1, 1)`, one 128-thread workgroup for 128 cells:

```sh
loom-compile chemistry.loom --root=chemistry.advance_collapse_gridwide_kernel \
  --format=amdgpu-hsaco --target=amdgpu:gfx942 \
  --config=chemistry.advance_collapse_gridwide_kernel.workgroup_count.x=1 \
  --config=chemistry.advance_collapse_gridwide_kernel.workgroup_count.y=1 \
  --config=chemistry.advance_collapse_gridwide_kernel.workgroup_count.z=1 \
  --output=chemistry-advance.hsaco
```

To add the HIP-rewritten column, build its code object in the ROCm 10
container (Apple `container` shown; Docker is equivalent), then rerun
`static_counts.py`:

```sh
container build --arch amd64 --memory 8G --cpus 8 --tag loom-hip-rocm10 \
  --file "$PWD/experimental/loom-chemistry/Dockerfile.rocm10" experimental/loom-chemistry
container run --arch amd64 --rosetta --rm --memory 10G --cpus 8 \
  --volume "$PWD/experimental/loom-chemistry:/src/experimental/loom-chemistry:ro" \
  --volume "$PWD/build/loom-chemistry-advance-spill-repro/hip:/out" \
  loom-hip-rocm10 /src/experimental/loom-chemistry/advance-spill-repro/hip_code_objects.sh /out
O=build/loom-chemistry-advance-spill-repro
python3 experimental/loom-chemistry/advance-spill-repro/static_counts.py \
  --log Loom:advance=$O/advance-compile.log \
  Loom=$O/chemistry-advance.hsaco \
  HIP=experimental/loom-chemistry/reference-gfx942-device.hsaco \
  HIP-rewritten=$O/hip/hip-rewritten.hsaco
```

## Static results (2026-10-08, HRX `383e88d8b6`)

Advance kernel. Counts are static instruction sites in the disassembly, not
executed instructions. Scratch bytes are per-lane operand widths summed over
sites. HIP is the pinned `../reference-gfx942-device.hsaco` (TheRock ROCm
10.0.0, `-O3 -ffp-contract=off`); HIP-rewritten is the same compiler on
Loom's input source.

| Metric | Loom | HIP | HIP-rewritten | Loom / HIP-rewritten |
| --- | ---: | ---: | ---: | ---: |
| Total instructions | 318,454 | 73,230 | 78,934 | 4.0x |
| Scratch store sites | 10,972 | 1,863 | 2,515 | 4.4x |
| Scratch store bytes | 64,200 | 19,896 | 25,916 | 2.5x |
| Scratch load sites | 27,083 | 4,129 | 4,907 | 5.5x |
| Scratch load bytes | 155,024 | 36,436 | 45,336 | 3.4x |
| `s_waitcnt` | 31,524 | 3,694 | 4,265 | 7.4x |
| `s_branch` (unconditional) | 39,531 | 22 | 28 | 1,412x |
| `s_branch` whose target is `s_branch` | 19,034 | 0 | 0 | — |
| `s_cbranch_*` | 1,244 | 439 | 499 | 2.5x |
| EXEC writes (incl. `saveexec`) | 18,808 | 776 | 903 | 20.8x |
| `v_readfirstlane` | 10,360 | 0 | 0 | — |
| `v_readlane`/`v_writelane` | 0 | 3,139 | 2,568 | — |
| Private bytes per lane | 61,624 | 8,008 | 7,024 | 8.8x |
| SGPRs / VGPRs | 102 / 256 | 106 / 128 | 106 / 128 | |

Loom's compile log has 8,314 `BACKEND/009` spill storages: 1,262 SGPR
(1,499 stores for 11,904 bytes, 6,368 reloads for 41,440 bytes) and 7,052 VGPR
(7,078 stores for 45,020 bytes, 15,890 reloads for 106,932 bytes). These are
spill-storage operations, not ISA counts. Loom's scratch ISA now also
includes private solver arrays, so it exceeds the logged spill bytes. HIP
scratch likewise includes private arrays, so neither column isolates spills.

The prepare kernel is small but shows the same control-flow pattern: 337
unconditional branches and 415 EXEC writes, against 1 and 21 for HIP, and 112
private bytes against 0.

### Trend

| Advance build | Spill warnings | Private bytes / lane | Instructions | Compile time |
| --- | ---: | ---: | ---: | ---: |
| 2026-09-24, global scratch buffer | 5,735 | 42,236 | — | — |
| 2026-10-01, `af901efb55` (GPU-measured) | — | 51,968 | 364,734 | — |
| 2026-10-02, `4bbc173125` | 6,598 | — | — | 130 s |
| 2026-10-08, `383e88d8b6` | 8,314 | 61,624 | 318,454 | 200–213 s |
| 2026-10-09, `0be406433a`, source exp/log/cbrt | 8,364 | 62,000 | 322,477 | — |

Instruction count fell while spills and private storage grew. The 2026-10-09
row replaces the compiler's f64 exp/log/cbrt recipes with the same operations
written in `../f64_math.h`. The imported functions match the recipes
operation for operation, with selects and no branches. On the same compiler,
the recipe build still reproduces the 2026-10-08 counts exactly, so the
difference comes from inlining and scheduling the source functions. The GPU timings
below predate this growth.

## What the ISA shows

These are leads for the compiler work, not established causes; no ablation
assigns runtime to any of them.

1. **Branch structure.** Half of Loom's 39,531 unconditional branches jump to
   another unconditional branch. These are branch-island chains from
   relaxation in `loom/src/loom/target/emit/native/amdgpu/branch_layout.c`,
   amplified by a large function with distant merge blocks. On 2026-10-01,
   Loom issued 23x HIP's dynamic `SQ_INSTS_BRANCH`.
2. **Spill transfer shape.** Each spill transfer saves EXEC, sets it to all
   lanes, materializes an address, transfers and restores EXEC: 18,808 EXEC
   writes against HIP's 903. Loom spills SGPRs to scratch memory and reloads
   them through a VGPR and `v_readfirstlane` (10,360 sites). LLVM instead
   keeps SGPR spills in VGPR lanes (`v_writelane`/`v_readlane`, 2,568–3,139
   sites) without memory traffic.
3. **Register budget.** Loom uses 256 VGPRs (rocprof reported 128 arch + 128
   accumulation registers) and still spills 7,052 VGPR values; HIP fits in
   128 VGPRs with 3–4x fewer scratch operand bytes. At 256 VGPRs a SIMD
   holds half as many waves as at 128.
4. **Missed rematerialization and reload reuse.** On the 2026-10-01 build, one
   division sequence stored a value at scratch offset `0x4d70` and reloaded it
   three times within a few instructions, and constant halves were spilled
   instead of rematerialized (`../repro-spill-clobber/PERFORMANCE-RESULTS.txt`).
   This has not been rechecked on the current object.
5. **Spill-address scheduling.** The 2026-10-08 diagnosis found that
   resource-stall scheduling hoists `low.storage.address` results early; 212
   of them occupied mandatory VGPRs when allocation failed. Classifying them as
   storage setup let resource-stall compile. That experiment
   (`address-setup-experiment.patch`) is not in HEAD in that form.

## Reproduce on a gfx942 GPU

On an MI300X with ROCm at `/opt/rocm` (or `ROCM_PATH`), CMake, Ninja, Python 3
and a C++ toolchain, one command from the checkout does everything:

```sh
experimental/loom-chemistry/advance-spill-repro/run_gpu.sh --repeats 5 --replicas 64
```

Omit `--replicas 64` to skip the second advance compile; add `--no-counters`
without `rocprofv3`. If `build/cmake` has no `loom-compile`, the script first
configures and builds it with ROCm's clang, using the settings in
`../run_rocm_comparison.sh` (`CHEM_BUILD_JOBS` sets parallelism, default 4;
log in `gpu/loom-build.log`). Set `LOOM_COMPILE` to use an existing tool;
outside the checkout it is required. It then compiles the Loom kernels unless
`LOOM_HSACO_DIR` already holds them,
builds `replay` (original HIP kernels) and `replay-rewritten` (HIP-rewritten
control) with `hipcc -O3 -ffp-contract=off`, and writes to
`build/loom-chemistry-advance-spill-repro/gpu/`:

- `advance-{0,100,500,920,999}.csv`, `prepare-500.csv`: per-sample kernel
  times for HIP and Loom; `rewritten-advance-500.csv`: HIP-rewritten versus
  Loom. A median/ratio table is printed at the end.
- `counters-{hip,loom,hip-rewritten}/`: raw `rocprofv3` CSVs for one warmup
  plus one measured step-500 advance dispatch; use the second dispatch of
  each kernel.
- `static-counts.{md,json}` for the Loom object and both HIP objects built on
  that machine.
- With `--replicas 64`: step 500 copied across 64 workgroups (8,192 cells),
  with Loom recompiled for `workgroup_count.x=64`.

Replay protocol. Each sample resets the backend to the same snapshot, so
every sample does the same solver work. Only the kernel is timed with HIP
events, and backend order alternates. The first (warmup) result of each
backend is compared field by field before any time is reported: relative
tolerance `2e-4`, abundance absolute tolerance `1e-14`, exact integer counters
and status. A mismatch exits nonzero. Advance launches use the saved grid
time and timestep of their step.

The replay sources compile with TheRock ROCm 10.0.0 `hipcc` (checked in the
container), but the container cannot link them, and they have not yet been
run on a GPU.

## GPU results

Measured 2026-10-01 on an MI300X (gfx942, ROCm 10.0, HIP 7.15) with the
`af901efb55` Loom build and the predecessor of `replay.cpp`. **These numbers
predate the current object** (51,968 versus 61,624 private bytes per lane)
and must be remeasured with `run_gpu.sh`.

Advance medians in ms, 128 cells, one workgroup, identical work:

| Step | HIP | Loom | Loom / HIP |
| ---: | ---: | ---: | ---: |
| 0 | 6.651 | 40.477 | 6.09x |
| 100 | 4.834 | 30.975 | 6.41x |
| 500 | 4.679 | 30.336 | 6.48x |
| 920 | 46.667 | 314.803 | 6.75x |
| 999 | 5.168 | 34.477 | 6.67x |

At step 500 both backends integrate 127 cells with 134 internal steps, 402
RHS calls and 131 Jacobians. Over 64 replicated workgroups the ratio was
6.50x. The HIP-rewritten control ran step 500 in 3.194 ms against Loom's 30.413
ms (9.52x), so the rewritten source is faster under HIP than the original;
that control was compiled without `-ffp-contract=off`, which `run_gpu.sh` now
adds.

Step-500 dynamic counters (rocprofv3, second dispatch):

| Counter | HIP | Loom | Ratio |
| --- | ---: | ---: | ---: |
| `SQ_INSTS_VALU` | 337,388 | 1,607,865 | 4.77x |
| `SQ_INSTS_SALU` | 182,650 | 910,073 | 4.98x |
| `SQ_INSTS_VMEM` | 79,242 | 294,841 | 3.72x |
| `SQ_INSTS_BRANCH` | 18,478 | 424,377 | 22.97x |
| `SQ_WAIT_ANY` | 2,049,493 | 14,512,818 | 7.08x |

PC sampling produced no usable attribution, so there is no hot-spot breakdown.

## Packaging

```sh
experimental/loom-chemistry/advance-spill-repro/package.sh
```

writes `build/loom-chemistry-advance-spill-repro.tar.gz` (this directory
plus the shared `../` sources it uses, the pinned HIP object and the
Dockerfile). Extract it, set `LOOM_COMPILE`, and run
`loom-chemistry/advance-spill-repro/compile.sh`; outputs go to `out/`.
