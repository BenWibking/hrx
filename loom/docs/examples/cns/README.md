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

- `cns.cxx`: 20 Loom C++ kernels. Fifteen are ported line by line from CNS's
  `CNS_K.H`, `hydro/CNS_hydro_K.H` and the Sod `cns_prob.H`:
  - initial data and temperature;
  - the ghost-cell fill: neighbor boxes, periodic in y and z,
    first-order extrapolation in x;
  - per box, `ctoprim`, the slope and Riemann kernels for each direction,
    and `flux_to_dudt`;
  - the two RK2 updates, and the time-step and totals reductions.

  The other five port the device code of AMReX's `AmrLevel::FillPatch`, used
  with `--fillpatch=amrex`.
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
cns_loom cns.hsaco [--n=64] [--bs=32] [--steps=20] [--streams=1]
         [--host-sync | --host-sync=flush] [--immediate] [--graph]
         [--fillpatch=amrex] [--phase-times]
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
- `--graph`: record each RK2 step once as an HRX graph and replay it every
  step. The graph is one recorded command buffer with explicit dependencies:
  each box's kernels in order, with joins at the phase boundaries. The time
  step and the totals are still computed with stream launches. The RK
  kernels read the time step from device memory, so a recorded step can be
  replayed with a new time step.
- `--host-sync=flush`: as `--host-sync`, but submit every stream before
  waiting on any, as AMReX's LOOM runtime does at each completion point.
  `--host-sync` submits and waits for one stream at a time, which serializes
  the streams.
- `--immediate`: submit each launch as its own `hrx_queue_dispatch` on the
  stream's queue, chained on the stream's timeline with no barriers, as AMReX's
  LOOM runtime does by default. Before the host waits, each stream gets an
  empty recording whose system-scope release publishes the kernels' writes.
- `--fillpatch=amrex`: fill ghost cells with AMReX's single-level FillPatch
  instead of one in-place kernel per box (below).
- `--phase-times`: with `--host-sync`, print the wall time of each phase,
  summed over all steps.

A phase is one group of launches over all boxes that must finish before the
next group starts: the ghost-cell fill, the per-box flux chains, an RK
update, or the temperature update. An RK2 step has eight phases plus the
reductions.

The output follows AMReX: the `[STEP n] Coarse TimeStep time:` lines cover the
advance, the totals and the next time step, and `[CNS] Total ...` prints the
conserved totals. With the defaults (AMReX's `inputs-ci`: 64³ cells in 32³
boxes, 20 steps), the driver compares the totals after step 20 with the CPU
reference of AMReX's test and prints `PASS` within a relative error of 1e-12.

## AMReX FillPatch

`--fillpatch=amrex` reproduces what `AmrLevel::FillPatch` runs for one level
on AMReX's LOOM backend, with the same launches, launch sizes, transfers and
host synchronizations:

1. A temporary MultiFab with two ghost cells.
2. `FabArray::setDomainBndry(NaN)` in an `MFIter` loop: one `setComplement`
   kernel per box at the non-periodic x boundary, over its grown box.
3. `FillPatchSingleLevel`'s periodic `ParallelCopy` from the state: the copy
   tags are copied from pinned memory, then one fused kernel gives each tag
   whole 64-lane warps, and the stream is synchronized when the tag vector is
   released.
4. `StateDataPhysBCFunct` in an `MFIter` loop. Each box at the x boundary
   copies its boundary conditions from pinned memory and launches one
   `ccfcdoit` face kernel with `FilccCell`. A box that also touches a periodic
   face repairs its corners through a temporary slab: a copy in, a boundary
   fill and a copy out.
5. `MultiFab::Copy` back into the state. The source's `Array4` table is copied
   from pinned memory and its LOOM offset table synchronously from pageable
   memory, then one fused kernel runs over all boxes, followed by a stream
   synchronization.

An `MFIter` loop synchronizes the current stream before it starts and each
stream it used at the end, one at a time. Arena allocations become
preallocated slots that are reused only after a host synchronization. Over a
20-step run with 32³ boxes, the five kernels have the same dispatch and
workgroup counts as AMReX LOOM's. The totals match the in-place fill exactly.

## Results

One MI250X GCD (gfx90a) on Setonix, 128³ cells, steps 2–20, compared with
AMReX's CNS built with ROCm 10.1 HIP at the same problem size and box size.
For HIP, "default" means its default streams.

| Configuration | 32³ boxes | 64³ boxes |
|---|---|---|
| AMReX CNS on HIP, default streams | 0.248 s | 0.200 s |
| AMReX CNS on HIP, 1 stream | 0.310 s | 0.210 s |
| This example, 4 streams | 0.239 s | 0.159 s |
| This example, 4 streams, `--host-sync` | 0.531 s | 0.212 s |
| This example, 1 stream | 0.466 s | 0.184 s |
| This example, `--graph` | 0.347 s | 0.169 s |
| This example, 4 streams, agent-scope acquires¹ | 0.196 s | 0.151 s |
| This example, 1 stream, agent-scope acquires¹ | 0.341 s | 0.168 s |
| This example, `--graph`, agent-scope acquires¹ | 0.345 s | 0.166 s |

¹ A local libhrx change that lowers the system-scope acquire fences in AQL
packet headers to agent scope. Totals are unchanged.

Between two operations, a stream records an ordering barrier with memory
scopes, which the AMD GPU driver turns into system-scope fences. A graph
records agent-scope barriers between dependent nodes, so on upstream libhrx
`--graph` runs as fast as one stream with agent-scope acquires, and lowering
acquires further changes it by less than 1%. The graph does not run
independent boxes concurrently, so four streams on four queues remain the
fastest mode.

The 64³ totals pass the reference check in every configuration (relative error
8e-15 for mass, 5e-15 for energy), with identical totals in every mode.

### Against AMReX LOOM CNS

128³ cells, steps 2–20, median of three rounds on one MI250X GCD (Setonix job
53193338). The example runs with the libhrx and Loom compiler of the AMReX LOOM
kit (HRX `459be5f93c`), with `--immediate --host-sync=flush`. AMReX LOOM CNS
uses its default immediate submission.

| Configuration | 32³ boxes, 4 streams | 64³ boxes, 4 streams | 32³ boxes, 1 stream | 64³ boxes, 1 stream |
|---|---|---|---|---|
| This example, in-place fill | 0.194 s | 0.158 s | 0.288 s | 0.174 s |
| This example, `--fillpatch=amrex` | 0.280 s | 0.204 s | 0.414 s | 0.245 s |
| AMReX LOOM CNS | 0.286 s | 0.216 s | 0.501 s | 0.264 s |

The ghost fill alone accounts for nearly all of the difference between the
example and AMReX LOOM CNS on four streams: with AMReX's FillPatch the fill
phase takes 0.110 s over 20 steps instead of 0.019 s, and the example comes
within 2% (32³ boxes) and 6% (64³ boxes) of AMReX LOOM. On one stream,
AMReX's generated `compute_dSdt` kernels also run slower than this example's.

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
