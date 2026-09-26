# Loom versus HIP scratch traffic on gfx942

Measured 2026-09-24 from the advance and prepare kernels in this directory. This
is a static code-generation comparison; neither code object was executed on an
AMD GPU for this analysis.

## Inputs and toolchains

- Loom: `chemistry.advance_collapse_gridwide_kernel` compiled to gfx942 HSACO
  from the generated Loom source. The 5,735 `BACKEND/009` diagnostics are in
  [`../../compile_warnings.txt`](../../compile_warnings.txt). The inspected code
  object was `/private/tmp/chemistry-advance-clean.hsaco`; its separate clean
  diagnostic log has the same 5,735 spill entries and byte totals.
- HIP: `advance_collapse_gridwide_kernel` in the pinned `reference.cpp` (SHA256
  `6c7ca23933b211980e831e8d2bfc4328245ab0e3c75f6b07a5e0ed40307843d0`).
  It was compiled with `hipcc --offload-arch=gfx942 -std=c++20 -O3
  -ffp-contract=off -DPRIMORDIAL_ROS2S_ENABLE_HIP=1
  -DPRIMORDIAL_ROS2S_NO_MAIN=1 --genco`. The compiler came from TheRock's
  stable `rocm[libraries,devel,device-gfx942]==10.0.0` Linux x86_64 wheels in
  an amd64 [Apple container](https://github.com/apple/container) under Rosetta.
  `hipcc --version` reported HIP `7.15.26333-0000000` and AMD clang `23.0.0git`
  inside that ROCm 10.0.0 installation. The HIP device object was extracted
  from the `--genco` offload bundle.

The HIP flags match those in `run_rocm_comparison.sh`. The HIP and Loom kernels
implement the same chemistry, but have different data layouts: Loom has a
4,960-byte per-cell global `ScratchRecord` for solver state.

## Advance kernel results

The instruction counts below come from disassembled `scratch_load_dword[xN]`
and `scratch_store_dword[xN]` instructions. Operand bytes are the sum of their
per-work-item widths across **static instruction sites**, with `dword` = 4 bytes.
They are not bytes transferred by a launch.

| Static scratch access | Loom | HIP | Loom / HIP |
| --- | ---: | ---: | ---: |
| Store instructions | 6,973 | 1,863 | 3.74x |
| Store operand bytes | 42,444 | 19,896 | 2.13x |
| Load instructions | 24,813 | 4,129 | 6.01x |
| Load operand bytes | 141,656 | 36,436 | 3.89x |
| Total instructions | 31,786 | 5,992 | 5.30x |
| Total operand bytes | 184,100 | 56,332 | 3.27x |

Loom's diagnostic log describes 722 SGPR spill storages (970 stores for 7,684
bytes; 4,916 reloads for 30,456 bytes) and 5,013 VGPR spill storages (5,052
stores for 34,760 bytes; 17,199 reloads for 111,200 bytes). Thus the logged
byte totals exactly equal the Loom scratch ISA byte totals. The warning-level
operation counts differ from ISA instruction counts because memory operations
can be split or combined during lowering.

| Code-object resource metadata | Loom | HIP |
| --- | ---: | ---: |
| Private segment bytes per work item | 42,236 | 8,008 |
| SGPR count | 102 | 106 |
| VGPR count | 256 | 128 |
| SGPR spill-store count | not emitted | 1,593 |
| VGPR spill-store count | not emitted | 2,265 |

LLVM defines its `.sgpr_spill_count` and `.vgpr_spill_count` fields as the
number of stores to register-allocator-created spill locations, not the number
of scratch ISA instructions or bytes. See the [LLVM AMDGPU metadata
documentation](https://llvm.org/docs/AMDGPUUsage.html). Those HIP counters
therefore should not be divided into Loom's warning counts.

The **3.27x figure compares all emitted scratch ISA operand bytes**. Loom's
scratch ISA byte totals are attributable to its reported spills. HIP's scratch
ISA can include accesses to ordinary private arrays as well as spills, so its
spill-only bytes cannot be isolated from this count. Conversely, the Loom
kernel's global `ScratchRecord` traffic is excluded. Branches and loops can
execute each static access zero, one, or many times; dynamic memory traffic
requires execution or profiling on gfx942 hardware.

## Resource-stall scheduling retry

On 2026-09-24, I rebuilt the current `loom-compile` with only the AMDGPU
`schedule_strategy` in `hal_kernel_library.c` changed from `SOURCE_PRIORITY`
to `RESOURCE_STALL`. I then compiled the same
`/private/tmp/chemistry-fresh.loom` advance root for gfx942, with the current
16-round spill-materialization limit and backend fixes. The source setting was
restored after copying the experimental compiler to
`/private/tmp/loom-compile-resource-stall-20260924`.

This compilation failed before emitting an HSACO. The diagnostic log at
`/private/tmp/chemistry-advance-resource-stall-diagnostics.log` contains 7,279
`BACKEND/009` spill-storage warnings, followed by `BACKEND/005
unspillable-register-exhausted`: allocation of `amdgpu.sgpr` values required
108 registers against a 102-register budget. The successful source-priority
compile logged 5,735 spill storages. These warning counts are not an ISA
scratch-traffic comparison because resource-stall scheduling produced no code
object. This retry establishes a current compile failure for resource-stall;
it does not isolate which scheduling decisions caused the excess SGPR live
range.

## Prepare kernel

The Loom prepare compilation emitted no spill diagnostics and its code object
declared a zero-byte private segment. The HIP prepare kernel likewise declared
zero private bytes and zero SGPR/VGPR spill stores. Its disassembly contained
no scratch load or store instruction.

## Reproduction

The adjacent `Dockerfile.rocm10` is the exact container recipe used for the HIP
compiler. From the repository root, with Apple's `container` CLI installed:

```sh
container system start
container build --arch amd64 --memory 8G --cpus 8 \
  --tag loom-hip-rocm10 \
  --file "$PWD/experimental/loom-chemistry/Dockerfile.rocm10" \
  experimental/loom-chemistry
mkdir -p build/loom-chemistry-spill
container run --arch amd64 --rosetta --rm --memory 10G --cpus 8 \
  --volume "$PWD/experimental/loom-chemistry:/src:ro" \
  --volume "$PWD/build/loom-chemistry-spill:/out" \
  loom-hip-rocm10 /bin/sh -lc \
  'hipcc --offload-arch=gfx942 -std=c++20 -O3 -ffp-contract=off \
    -DPRIMORDIAL_ROS2S_ENABLE_HIP=1 -DPRIMORDIAL_ROS2S_NO_MAIN=1 \
    --genco /src/reference.cpp -o /out/reference-gfx942.bundle'
```

`--genco` produces a Clang offload bundle, not a bare ELF. Extract its
`hipv4-amdgcn-amd-amdhsa--gfx942` member with `clang-offload-bundler
--unbundle --type=o`, then use `llvm-readelf -n` for code-object metadata and
`llvm-objdump -d` for ISA. The measured temporary artifacts are
`/private/tmp/loom-hip-rocm10/reference-gfx942-device.hsaco`,
`/private/tmp/loom-hip-rocm10/hip.asm`, and
`/private/tmp/loom-hip-rocm10/loom.asm` on the analysis host. The Loom
compilation command is documented in this directory's `README.md`.
