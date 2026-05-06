// SPDX-License-Identifier: Apache-2.0

#include <AMReX.H>
#include <AMReX_Arena.H>
#include <AMReX_Gpu.H>
#include <AMReX_GpuDevice.H>
#include <AMReX_GpuLaunch.H>
#include <AMReX_Print.H>
#include <AMReX_REAL.H>
#include <AMReX_Vector.H>

#include <cmath>

int main(int argc, char *argv[]) {
  amrex::Initialize(argc, argv);

  constexpr int n = 1024;
  amrex::Vector<amrex::Real> h(n, amrex::Real(1.0));
  amrex::Vector<amrex::Real> out(n, amrex::Real(0.0));

  auto *d = static_cast<amrex::Real *>(
      amrex::The_Arena()->alloc(n * sizeof(amrex::Real)));

  amrex::Gpu::htod_memcpy_async(d, h.data(), n * sizeof(amrex::Real));

  amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
    d[i] = amrex::Real(2.0) * d[i] + amrex::Real(1.0);
  });

  amrex::Gpu::dtoh_memcpy(out.data(), d, n * sizeof(amrex::Real));
  amrex::The_Arena()->free(d);

  bool ok = true;
  for (int i = 0; i < n; ++i) {
    ok = ok && std::abs(out[i] - amrex::Real(3.0)) < amrex::Real(1.0e-12);
  }

  amrex::Print() << (ok ? "AMReX HRX POC passed\n"
                        : "AMReX HRX POC failed\n");

  amrex::Finalize();
  return ok ? 0 : 1;
}
