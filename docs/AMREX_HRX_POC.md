# AMReX on HRX Toy GPU Proof of Concept

## Summary

This document defines the first proof of concept for running a minimal AMReX HIP
GPU executable on HRX's HIP-compatible runtime library instead of the production
HIP runtime.

The proof is intentionally narrow. It should validate one raw-pointer
`amrex::ParallelFor`, device allocation, host-to-device copy, device-to-host
copy, synchronization, and host-side result verification. It does not attempt
MPI, `MultiFab`, particles, linear solvers, random numbers, managed memory,
GPU-aware MPI, or ROCm library workloads.

AMReX still uses the normal ROCm HIP compiler and headers for device
compilation. HRX replaces the HIP runtime library at execution time.

## Required HRX Work

- Add `hipStreamAddCallback` to HRX's HIP binding.
  - Match HIP's callback API signature.
  - Enqueue the callback after prior stream work completes.
  - Invoke the user callback as `callback(stream, hipSuccess, userData)`.
  - Implement it as a compatibility wrapper over HRX's existing host-function
    launch path where practical.

- Build HRX with the HIP binding enabled so it produces `libamdhip64.so.7`.
  - The runtime loader path must put HRX's build output before ROCm's
    production `libamdhip64.so`.
  - The proof should verify the loaded runtime with loader diagnostics or symbol
    inspection before running AMReX.

- Verify that the following HIP symbols are exported by the HRX runtime:
  - `hipStreamAddCallback`
  - `hipLaunchKernel`
  - `hipMalloc`
  - `hipFree`
  - `hipMemcpyAsync`
  - `hipStreamCreate`
  - `hipStreamSynchronize`

`hipDeviceGetUuid` is not required for the first single-process proof. It must
be implemented or faked before attempting MPI-enabled AMReX, because AMReX uses
it to reason about rank-to-device placement.

## AMReX Build

Build AMReX as a minimal HIP configuration:

```bash
cmake -S /path/to/amrex -B /path/to/amrex-build-hrx-poc \
  -DAMReX_GPU_BACKEND=HIP \
  -DAMReX_AMD_ARCH=<target gfx arch> \
  -DCMAKE_CXX_COMPILER="$(which clang++)" \
  -DAMReX_MPI=OFF \
  -DAMReX_FORTRAN=OFF \
  -DAMReX_AMRLEVEL=OFF \
  -DAMReX_EB=OFF \
  -DAMReX_LINEAR_SOLVERS=OFF \
  -DAMReX_PARTICLES=OFF \
  -DAMReX_GPU_RDC=OFF \
  -DAMReX_SPACEDIM=1

cmake --build /path/to/amrex-build-hrx-poc
```

AMReX HIP builds still link `hiprand`, `rocrand`, and `rocprim`. The toy
executable must avoid AMReX random-number, reduction, scan, and solver paths
until those dependencies are validated separately against HRX.

## Toy Executable

The first AMReX executable is checked in at `examples/amrex_poc`. It should
avoid `MultiFab`, managed memory, random numbers, async arena allocations, and
MPI. It uses raw AMReX arena memory and a one-dimensional `ParallelFor`.

```cpp
#include <AMReX.H>
#include <AMReX_Arena.H>
#include <AMReX_Gpu.H>
#include <AMReX_GpuDevice.H>
#include <AMReX_GpuLaunch.H>
#include <AMReX_Print.H>
#include <AMReX_REAL.H>
#include <AMReX_Vector.H>

#include <cmath>

int main(int argc, char* argv[])
{
    amrex::Initialize(argc, argv);

    int const N = 1024;
    amrex::Vector<amrex::Real> h(N, amrex::Real(1.0));
    amrex::Vector<amrex::Real> out(N, amrex::Real(0.0));

    auto* d = static_cast<amrex::Real*>(
        amrex::The_Arena()->alloc(N * sizeof(amrex::Real)));

    amrex::Gpu::htod_memcpy_async(d, h.data(), N * sizeof(amrex::Real));

    amrex::ParallelFor(N, [=] AMREX_GPU_DEVICE (int i) noexcept {
        d[i] = amrex::Real(2.0) * d[i] + amrex::Real(1.0);
    });

    amrex::Gpu::dtoh_memcpy(out.data(), d, N * sizeof(amrex::Real));
    amrex::The_Arena()->free(d);

    bool ok = true;
    for (int i = 0; i < N; ++i) {
        ok = ok && std::abs(out[i] - amrex::Real(3.0)) < amrex::Real(1.0e-12);
    }

    amrex::Print() << (ok ? "AMReX HRX POC passed\n"
                          : "AMReX HRX POC failed\n");

    amrex::Finalize();
    return ok ? 0 : 1;
}
```

Configure HRX with the opt-in POC target after AMReX has been built or
installed:

```bash
cmake -S /path/to/hrx -B /path/to/hrx-build \
  -DHRX_BUILD_AMREX_POC=ON \
  -DCMAKE_PREFIX_PATH=/path/to/amrex-build-or-install

cmake --build /path/to/hrx-build --target amdhip64 amrex_hrx_poc
```

Run with conservative AMReX arena settings:

```bash
LD_LIBRARY_PATH=/path/to/hrx-build/lib:$LD_LIBRARY_PATH \
  ./amrex_hrx_poc \
  amrex.the_arena_is_managed=0 \
  amrex.the_arena_init_size=0 \
  amrex.the_device_arena_init_size=0 \
  amrex.the_pinned_arena_init_size=0
```

Use the actual HRX build library directory in place of `/path/to/hrx-build/lib`.

## Bring-Up Sequence

1. Implement and export `hipStreamAddCallback`.
2. Build HRX and inspect the produced `libamdhip64.so.7` for required symbols.
3. Run a non-AMReX HIP hello-kernel against HRX to isolate fat-binary
   registration and kernel launch.
4. Build AMReX with the minimal HIP configuration above.
5. Build and run the raw-pointer AMReX `ParallelFor` toy.
6. Confirm that HRX's `libamdhip64.so` is the runtime library being loaded.
7. Only after the raw-pointer proof passes, try `FArrayBox`, then `MultiFab`,
   then MPI.

## Acceptance Criteria

- AMReX initializes successfully.
- HRX's HIP-compatible runtime is loaded instead of production HIP.
- The AMReX kernel launches through HRX.
- Host-side verification reports the expected values.
- The process exits cleanly without AMReX aborts or HIP errors.

## Known Follow-Ups

- Implement or fake `hipDeviceGetUuid` before MPI-enabled AMReX.
- Validate device global symbol support before using AMReX features that call
  `hipMemcpyToSymbolAsync` or `hipMemcpyFromSymbolAsync`.
- Validate `rocprim`, `hiprand`, and `rocsparse` separately before attempting
  reductions, random numbers, or linear solvers.
- Validate stream-ordered allocation behavior before relying on AMReX async
  arenas for temporary GPU memory.
