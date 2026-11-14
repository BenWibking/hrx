# Compressible Navier–Stokes (CNS) on Loom and HRX

This directory holds a standalone port of AMReX's GPU CNS test (`Tests/GPU/CNS`
in AMReX: Sod shock tube, single level, no viscosity) to Loom C++, with a small
host driver that runs it on HRX. It needs no AMReX code, no `loom-cxx` driver
and no fork-only importer features. Upstream `loom-import-cxx`,
`loom-compile` and the public libhrx API are enough.

It reproduces how a block-structured mesh code submits GPU work. Every time
step launches a chain of short, dependent kernels for each box of the mesh,
one launch per box per kernel. The intermediate data of each box fits in L2.
That makes it a useful test for launch cost, queue ordering and cache
maintenance between dependent dispatches.

## Files

- `cns.cxx`: 15 Loom C++ kernels, ported line by line from CNS's
  `CNS_K.H`, `hydro/CNS_hydro_K.H` and the Sod `cns_prob.H`:
  - initial data and temperature;
  - the ghost-cell fill: neighbor boxes, periodic in y and z,
    first-order extrapolation in x;
  - per box, `ctoprim`, the slope and Riemann kernels for each direction,
    and `flux_to_dudt`;
  - the two RK2 updates, and the time-step and totals reductions.
- `cns_loom.cpp`: the host driver. It loads the code object, allocates the
  state, and runs AMReX's RK2 step with one `hrx_stream_dispatch` per box per
  kernel.

The kernels use only pointer and scalar parameters. Each array is one buffer
holding every box at computed offsets, so no pointer is ever stored in memory.

## Build

Compile the kernels into a gfx90a code object:

```shell
loom-import-cxx --I=loom/src/loom/import/cxx/include \
  loom/docs/examples/cns/cns.cxx --output=cns.loom
loom-compile cns.loom --target=amdgpu:gfx90a --format=amdgpu-hsaco \
  --output=cns.hsaco
```

Build libhrx (CMake target `hrx`) and compile the driver against it:

```shell
clang++ -std=c++20 -O2 -I libhrx/include loom/docs/examples/cns/cns_loom.cpp \
  -L <libhrx-dir> -lhrx -Wl,-rpath,<libhrx-dir> -o cns_loom
```

## Run

```shell
cns_loom cns.hsaco [--n=64] [--bs=32] [--steps=20] [--streams=1] [--host-sync]
```

- `--n`: cells per side of the cubic domain [0, 1]³.
- `--bs`: cells per side of each box; `--n` must be a multiple.
- `--steps`: number of time steps.
- `--streams`: number of HRX streams; boxes are assigned round-robin, one
  stream per provisioned queue.
- `--host-sync`: synchronize every stream from the host after each phase of
  the step, as AMReX does at the end of each `MFIter` loop. By default the
  phases are ordered with device-side event waits
  (`hrx_event_record` / `hrx_stream_wait_event`), and the host waits only to
  read the time step and the totals.

A phase is one group of launches over all boxes that must finish before the
next group starts: the ghost-cell fill, the per-box flux chains, an RK
update, or the temperature update. An RK2 step has eight phases plus the
reductions.

The output follows AMReX: the `[STEP n] Coarse TimeStep time:` lines cover the
advance, the totals and the next time step, and `[CNS] Total ...` prints the
conserved totals. With the defaults (AMReX's `inputs-ci`: 64³ cells in 32³
boxes, 20 steps), the driver compares the totals after step 20 with the CPU
reference of AMReX's test and prints `PASS` within a relative error of 1e-12.

## Results

One MI250X GCD (gfx90a) on Setonix, 128³ cells, steps 2–20, compared with
AMReX's CNS built with ROCm 10.1 HIP at the same problem size and box size.
For HIP, "default" means its default streams.

| Configuration | 32³ boxes | 64³ boxes |
|---|---|---|
| AMReX CNS on HIP, default streams | 0.248 s | 0.200 s |
| AMReX CNS on HIP, 1 stream | 0.310 s | 0.210 s |
| This example, 4 streams | 0.249 s | 0.158 s |
| This example, 4 streams, `--host-sync` | 0.531 s | 0.212 s |
| This example, 1 stream | 0.458 s | 0.184 s |
| This example, 4 streams, agent-scope acquires¹ | 0.203 s | 0.151 s |
| This example, 1 stream, agent-scope acquires¹ | 0.344 s | 0.168 s |

¹ A local libhrx change that lowers the system-scope acquire fences in AQL
packet headers to agent scope. Totals are unchanged.

The 64³ totals pass the reference check in every configuration (relative error
8e-15 for mass, 5e-15 for energy).

## Notes

- The constants block of `hrx_stream_dispatch` holds a kernel's scalar
  parameters in parameter order, packed without padding. Each pointer
  parameter is one binding, in parameter order. The driver checks both
  against `hrx_executable_export_info` before launching.
- The five workgroup reductions in `cns_sums` are written out one by one. A
  workgroup reduction inside a loop is rejected, because its workgroup scratch
  slot would be reused without a rendezvous.
- The driver passes `host_numa_node` in the device URI. Without it, HRX pins
  its device threads to NUMA node 0, which fails when the process may not run
  there.
