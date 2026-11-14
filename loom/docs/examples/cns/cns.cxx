// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Kernels of AMReX's GPU CNS test (Tests/GPU/CNS, Sod problem, no viscosity),
// ported to Loom C++ with no AMReX dependency. The host driver (cns_loom.cpp)
// launches them one launch per box per kernel, in the order AMReX's RK2 time
// step does, either with hrx_stream_dispatch or as a recorded graph.
//
// Data layout, for a cubic box of `bs` cells per side:
//   state S:  (bs+4)^3 cells x 7 components (2 ghost cells), per box
//   prim  Q:  (bs+4)^3 cells x 8 components
//   slope D:  (bs+2)^3 cells x 5 components (1 ghost cell)
//   flux  F:  nodal in one direction: (bs+1) x bs x bs cells x 7 components
//   dSdt:     bs^3 cells x 7 components (no ghost cells)
// Every array is x-fastest, then y, z, then component (AMReX FArrayBox order).
// Pointer arguments address one box; the host binds each box's subrange.
// Only fill_ghosts, rk_update, rk2_update_2 and estdt span all boxes.

#include <loomcxx/kernel.h>

// Conserved components.
#define URHO 0
#define UMX 1
#define UMY 2
#define UMZ 3
#define UEDEN 4
#define UEINT 5
#define UTEMP 6
#define NCONS 7
// Hyperbolic equations (fluxes for UEINT and UTEMP are zero).
#define NEQNS 5
// Primitive components.
#define QRHO 0
#define QU 1
#define QV 2
#define QW 3
#define QPRES 4
#define QCS 5
#define QEINT 6
#define QTEMP 7
#define NPRIM 8

// One work-item per element, 256 per workgroup, as AMReX's ParallelFor<256>.
static loom::kernel::configuration configure(unsigned elements) {
  return {{(elements + 255) / 256, 1, 1}, {256, 1, 1}};
}

[[loom::inline]] static unsigned element_index() {
  return loom::kernel::workgroup::id.x * 256u + loom::kernel::workitem::id.x;
}

[[loom::inline]] static double dmax(double a, double b) { return a < b ? b : a; }
[[loom::inline]] static double dmin(double a, double b) { return b < a ? b : a; }

// Index of local cell (i, j, k) relative to the array's first stored cell.
[[loom::inline]] static int at(int i, int j, int k, int n, int nx, int ny,
                                int nz) {
  return ((n * nz + k) * ny + j) * nx + i;
}

//===----------------------------------------------------------------------===//
// Initial data and temperature
//===----------------------------------------------------------------------===//

// Sod shock tube along x (cns_initdata) over the box's valid cells.
[[loom::kernel(configure)]]
void cns_init(unsigned elements, int bs, int lox, double dx, double gamma,
              double p_l, double p_r, double rho_l, double rho_r, double u_l,
              double u_r, [[loom::noalias]] double* s) {
  const unsigned e = element_index();
  if (e >= elements) return;
  const int i = e % bs, j = (e / bs) % bs, k = e / (bs * bs);
  const int n = bs + 4;
  const double x = ((lox + i) + 0.5) * dx;
  double pt, rhot, uxt;
  if (x < 0.5) {
    pt = p_l;
    rhot = rho_l;
    uxt = u_l;
  } else {
    pt = p_r;
    rhot = rho_r;
    uxt = u_r;
  }
  const double et = pt / (gamma - 1.0);
  s[at(i + 2, j + 2, k + 2, URHO, n, n, n)] = rhot;
  s[at(i + 2, j + 2, k + 2, UMX, n, n, n)] = rhot * uxt;
  s[at(i + 2, j + 2, k + 2, UMY, n, n, n)] = 0.0;
  s[at(i + 2, j + 2, k + 2, UMZ, n, n, n)] = 0.0;
  s[at(i + 2, j + 2, k + 2, UEINT, n, n, n)] = et;
  s[at(i + 2, j + 2, k + 2, UEDEN, n, n, n)] = et + 0.5 * rhot * uxt * uxt;
  s[at(i + 2, j + 2, k + 2, UTEMP, n, n, n)] = 0.0;
}

// cns_compute_temperature over the box's valid cells.
[[loom::kernel(configure)]]
void cns_compute_temp(unsigned elements, int bs, double cv,
                      [[loom::noalias]] double* s) {
  const unsigned e = element_index();
  if (e >= elements) return;
  const int n = bs + 4;
  const int i = e % bs + 2, j = (e / bs) % bs + 2, k = e / (bs * bs) + 2;
  const double rhoinv = 1.0 / s[at(i, j, k, URHO, n, n, n)];
  const double mx = s[at(i, j, k, UMX, n, n, n)];
  const double my = s[at(i, j, k, UMY, n, n, n)];
  const double mz = s[at(i, j, k, UMZ, n, n, n)];
  const double eint = s[at(i, j, k, UEDEN, n, n, n)] -
                      0.5 * rhoinv * (mx * mx + my * my + mz * mz);
  s[at(i, j, k, UEINT, n, n, n)] = eint;
  s[at(i, j, k, UTEMP, n, n, n)] = rhoinv * eint * (1.0 / cv);
}

//===----------------------------------------------------------------------===//
// Ghost cells
//===----------------------------------------------------------------------===//

// Fills the two ghost layers of box `box` in the all-box state array: from
// neighboring boxes inside the domain, periodic in y and z, and first-order
// extrapolation (foextrap) at the x boundaries. This matches AMReX's
// FillBoundary followed by CNS's physical boundary fill for lo_bc = hi_bc = 2.
[[loom::kernel(configure)]]
void cns_fill_ghosts(unsigned elements, int bs, int boxes_per_side, int box,
                     [[loom::noalias]] double* s) {
  const unsigned e = element_index();
  if (e >= elements) return;
  const int n = bs + 4;
  const int il = e % n, jl = (e / n) % n, kl = e / (n * n);
  if (il >= 2 && il < bs + 2 && jl >= 2 && jl < bs + 2 && kl >= 2 &&
      kl < bs + 2) {
    return;
  }
  const int domain = bs * boxes_per_side;
  const int bx = box % boxes_per_side;
  const int by = (box / boxes_per_side) % boxes_per_side;
  const int bz = box / (boxes_per_side * boxes_per_side);
  int gx = bx * bs + il - 2;
  int gy = by * bs + jl - 2;
  int gz = bz * bs + kl - 2;
  gx = gx < 0 ? 0 : (gx >= domain ? domain - 1 : gx);
  gy = gy < 0 ? gy + domain : (gy >= domain ? gy - domain : gy);
  gz = gz < 0 ? gz + domain : (gz >= domain ? gz - domain : gz);
  const int sx = gx / bs, sy = gy / bs, sz = gz / bs;
  const int source = (sz * boxes_per_side + sy) * boxes_per_side + sx;
  const int box_elements = n * n * n * NCONS;
  const int si = gx - sx * bs + 2, sj = gy - sy * bs + 2, sk = gz - sz * bs + 2;
  for (int c = 0; c < NCONS; ++c) {
    s[box * box_elements + at(il, jl, kl, c, n, n, n)] =
        s[source * box_elements + at(si, sj, sk, c, n, n, n)];
  }
}

//===----------------------------------------------------------------------===//
// compute_dSdt
//===----------------------------------------------------------------------===//

// cns_ctoprim over the box grown by 2 (all stored cells of S and Q).
[[loom::kernel(configure)]]
void cns_ctoprim(unsigned elements, int bs, double gamma, double smallr,
                 double smallp, double cv, [[loom::noalias]] const double* u,
                 [[loom::noalias]] double* q) {
  const unsigned e = element_index();
  if (e >= elements) return;
  const int n = bs + 4;
  const int i = e % n, j = (e / n) % n, k = e / (n * n);
  const double rho = dmax(u[at(i, j, k, URHO, n, n, n)], smallr);
  const double rhoinv = 1.0 / rho;
  const double ux = u[at(i, j, k, UMX, n, n, n)] * rhoinv;
  const double uy = u[at(i, j, k, UMY, n, n, n)] * rhoinv;
  const double uz = u[at(i, j, k, UMZ, n, n, n)] * rhoinv;
  const double kineng = 0.5 * rho * (ux * ux + uy * uy + uz * uz);
  double ei = u[at(i, j, k, UEDEN, n, n, n)] - kineng;
  if (ei <= 0.0) ei = u[at(i, j, k, UEINT, n, n, n)];
  const double p = dmax((gamma - 1.0) * ei, smallp);
  ei *= rhoinv;
  q[at(i, j, k, QRHO, n, n, n)] = rho;
  q[at(i, j, k, QU, n, n, n)] = ux;
  q[at(i, j, k, QV, n, n, n)] = uy;
  q[at(i, j, k, QW, n, n, n)] = uz;
  q[at(i, j, k, QEINT, n, n, n)] = ei;
  q[at(i, j, k, QPRES, n, n, n)] = p;
  q[at(i, j, k, QCS, n, n, n)] = __builtin_sqrt(gamma * p * rhoinv);
  q[at(i, j, k, QTEMP, n, n, n)] = ei / cv;
}

[[loom::inline]] static double cns_limiter(double dlft, double drgt) {
  const double dcen = 0.5 * (dlft + drgt);
  const double dsgn = __builtin_copysign(1.0, dcen);
  const double slop = 2.0 * dmin(__builtin_fabs(dlft), __builtin_fabs(drgt));
  const double dlim = (dlft * drgt >= 0.0) ? slop : 0.0;
  return dsgn * dmin(dlim, __builtin_fabs(dcen));
}

// Characteristic slopes along one direction (cns_slope_x/y/z). `dir` selects
// the normal velocity; the two tangential velocities follow in CNS's order.
// The region is the box grown by one cell along `dir`.
[[loom::inline]] static void cns_slope(int dir, unsigned e, int bs,
                                       const double* q, double* dq) {
  const int lx = dir == 0 ? bs + 2 : bs, ly = dir == 1 ? bs + 2 : bs;
  int i = e % lx, j = (e / lx) % ly, k = e / (lx * ly);
  i += dir == 0 ? 1 : 2;
  j += dir == 1 ? 1 : 2;
  k += dir == 2 ? 1 : 2;
  const int dx = dir == 0, dy = dir == 1, dz = dir == 2;
  const int qn = bs + 4;
  const int vn = dir == 0 ? QU : (dir == 1 ? QV : QW);
  const int t1 = dir == 0 ? QV : QU;
  const int t2 = dir == 2 ? QV : QW;
#define QA(di, c) q[at(i + (di) * dx, j + (di) * dy, k + (di) * dz, c, qn, qn, qn)]
  const double cs = QA(0, QCS);
  double dlft = 0.5 * (QA(0, QPRES) - QA(-1, QPRES)) / cs -
                0.5 * QA(0, QRHO) * (QA(0, vn) - QA(-1, vn));
  double drgt = 0.5 * (QA(1, QPRES) - QA(0, QPRES)) / cs -
                0.5 * QA(0, QRHO) * (QA(1, vn) - QA(0, vn));
  const double d0 = cns_limiter(dlft, drgt);
  const double cs2 = cs * cs;
  dlft = (QA(0, QRHO) - QA(-1, QRHO)) - (QA(0, QPRES) - QA(-1, QPRES)) / cs2;
  drgt = (QA(1, QRHO) - QA(0, QRHO)) - (QA(1, QPRES) - QA(0, QPRES)) / cs2;
  const double d1 = cns_limiter(dlft, drgt);
  dlft = 0.5 * (QA(0, QPRES) - QA(-1, QPRES)) / cs +
         0.5 * QA(0, QRHO) * (QA(0, vn) - QA(-1, vn));
  drgt = 0.5 * (QA(1, QPRES) - QA(0, QPRES)) / cs +
         0.5 * QA(0, QRHO) * (QA(1, vn) - QA(0, vn));
  const double d2 = cns_limiter(dlft, drgt);
  dlft = QA(0, t1) - QA(-1, t1);
  drgt = QA(1, t1) - QA(0, t1);
  const double d3 = cns_limiter(dlft, drgt);
  dlft = QA(0, t2) - QA(-1, t2);
  drgt = QA(1, t2) - QA(0, t2);
  const double d4 = cns_limiter(dlft, drgt);
#undef QA
  // dq is stored on the box grown by one cell.
  const int dn = bs + 2;
  dq[at(i - 1, j - 1, k - 1, 0, dn, dn, dn)] = d0;
  dq[at(i - 1, j - 1, k - 1, 1, dn, dn, dn)] = d1;
  dq[at(i - 1, j - 1, k - 1, 2, dn, dn, dn)] = d2;
  dq[at(i - 1, j - 1, k - 1, 3, dn, dn, dn)] = d3;
  dq[at(i - 1, j - 1, k - 1, 4, dn, dn, dn)] = d4;
}

[[loom::kernel(configure)]]
void cns_slope_x(unsigned elements, int bs, [[loom::noalias]] const double* q,
                 [[loom::noalias]] double* dq) {
  const unsigned e = element_index();
  if (e < elements) cns_slope(0, e, bs, q, dq);
}

[[loom::kernel(configure)]]
void cns_slope_y(unsigned elements, int bs, [[loom::noalias]] const double* q,
                 [[loom::noalias]] double* dq) {
  const unsigned e = element_index();
  if (e < elements) cns_slope(1, e, bs, q, dq);
}

[[loom::kernel(configure)]]
void cns_slope_z(unsigned elements, int bs, [[loom::noalias]] const double* q,
                 [[loom::noalias]] double* dq) {
  const unsigned e = element_index();
  if (e < elements) cns_slope(2, e, bs, q, dq);
}

// Two-shock approximate Riemann solver (riemann in CNS_hydro_K.H). The five
// fluxes are returned in normal-direction order: mass, normal momentum, first
// tangential momentum, second tangential momentum, energy.
[[loom::inline]] static void riemann(
    double gamma, double smallp, double rl, double ul, double pl, double ut1l,
    double ut2l, double rr, double ur, double pr, double ut1r, double ut2r,
    double* flx) {
  const double weakwv = 1.e-3;
  const double sml = 1.e-6;

  const double clsql = gamma * pl * rl;
  const double clsqr = gamma * pr * rr;
  double wl = __builtin_sqrt(clsql);
  double wr = __builtin_sqrt(clsqr);
  const double cleft = wl / rl;
  const double cright = wr / rr;
  const double ccsmall = sml * (cleft + cright);

  double pstar = (wl * pr + wr * pl - wr * wl * (ur - ul)) / (wl + wr);
  pstar = dmax(pstar, smallp);
  double pstnm1 = pstar;

  double wlsq = (0.5 * (gamma - 1.) * (pstar + pl) + pstar) * rl;
  double wrsq = (0.5 * (gamma - 1.) * (pstar + pr) + pstar) * rr;

  wl = __builtin_sqrt(wlsq);
  wr = __builtin_sqrt(wrsq);
  double ustarp = ul - (pstar - pl) / wl;
  double ustarm = ur + (pstar - pr) / wr;

  pstar = (wl * pr + wr * pl - wr * wl * (ur - ul)) / (wl + wr);
  pstar = dmax(pstar, smallp);

  double ustar;
  for (int iter = 0; iter < 3; ++iter) {
    wlsq = (0.5 * (gamma - 1.) * (pstar + pl) + pstar) * rl;
    wrsq = (0.5 * (gamma - 1.) * (pstar + pr) + pstar) * rr;

    wl = 1. / __builtin_sqrt(wlsq);
    wr = 1. / __builtin_sqrt(wrsq);

    const double ustnm1 = ustarm;
    const double ustnp1 = ustarp;

    ustarm = ur - (pr - pstar) * wr;
    ustarp = ul + (pl - pstar) * wl;

    const double dpditer = __builtin_fabs(pstnm1 - pstar);
    double zp = __builtin_fabs(ustarp - ustnp1);
    if (zp - weakwv * cleft < 0.0) {
      zp = dpditer * wl;
    }
    double zm = __builtin_fabs(ustarm - ustnm1);
    if (zm - weakwv * cright < 0.0) {
      zm = dpditer * wr;
    }

    const double zz = zp + zm;
    const double denom = dpditer / dmax(zz, ccsmall);
    pstnm1 = pstar;
    pstar = pstar - denom * (ustarm - ustarp);
    pstar = dmax(pstar, smallp);
    ustar = 0.5 * (ustarm + ustarp);
  }

  double ro, uo, po, sgnm, utrans1, utrans2;
  if (ustar > 0.) {
    ro = rl;
    uo = ul;
    po = pl;
    sgnm = 1.;
    utrans1 = ut1l;
    utrans2 = ut2l;
  } else if (ustar < 0.) {
    ro = rr;
    uo = ur;
    po = pr;
    sgnm = -1.;
    utrans1 = ut1r;
    utrans2 = ut2r;
  } else {
    uo = 0.5 * (ur + ul);
    po = 0.5 * (pr + pl);
    ro = 2. * (rl * rr) / (rl + rr);
    sgnm = 1.;
    utrans1 = 0.5 * (ut1l + ut1r);
    utrans2 = 0.5 * (ut2l + ut2r);
  }
  const double wosq = (0.5 * (gamma - 1.) * (pstar + po) + pstar) * ro;
  const double co = __builtin_sqrt(gamma * po / ro);
  const double wo = __builtin_sqrt(wosq);
  const double dpjmp = pstar - po;
  const double rstar = ro / (1. - ro * dpjmp / wosq);
  const double cstar = __builtin_sqrt(gamma * pstar / rstar);
  double spout = co - sgnm * uo;
  double spin = cstar - sgnm * uo;
  if (pstar >= po) {
    spin = wo / ro - sgnm * uo;
    spout = spin;
  }
  const double ss = dmax(spout - spin, spout + spin);
  const double frac = 0.5 * (1. + (spin + spout) / dmax(ss, ccsmall));

  double rgdnv, ugdnv, pgdnv;
  if (spout < 0.) {
    rgdnv = ro;
    ugdnv = uo;
    pgdnv = po;
  } else if (spin >= 0.) {
    rgdnv = rstar;
    ugdnv = ustar;
    pgdnv = pstar;
  } else {
    rgdnv = frac * rstar + (1. - frac) * ro;
    ugdnv = frac * ustar + (1. - frac) * uo;
    pgdnv = frac * pstar + (1. - frac) * po;
  }

  flx[0] = rgdnv * ugdnv;
  flx[1] = rgdnv * ugdnv * ugdnv + pgdnv;
  flx[2] = rgdnv * ugdnv * utrans1;
  flx[3] = rgdnv * ugdnv * utrans2;
  flx[4] = ugdnv * (0.5 * rgdnv * (ugdnv * ugdnv + utrans1 * utrans1 +
                                   utrans2 * utrans2) +
                    pgdnv / (gamma - 1.) + pgdnv);
}

// Interface states and fluxes on the faces normal to `dir`
// (cns_riemann_x/y/z), including the zero fluxes of UEINT and UTEMP.
[[loom::inline]] static void cns_riemann(int dir, unsigned e, int bs,
                                         double gamma, double smallp,
                                         double smallr, const double* dq,
                                         const double* q, double* f) {
  const int fx = dir == 0 ? bs + 1 : bs, fy = dir == 1 ? bs + 1 : bs;
  const int fz = dir == 2 ? bs + 1 : bs;
  const int i = e % fx, j = (e / fx) % fy, k = e / (fx * fy);
  const int dx = dir == 0, dy = dir == 1, dz = dir == 2;
  const int qn = bs + 4, dn = bs + 2;
  const int vn = dir == 0 ? QU : (dir == 1 ? QV : QW);
  const int t1 = dir == 0 ? QV : QU;
  const int t2 = dir == 2 ? QV : QW;
  // Left state at the cell before the face, right state at the cell after.
#define QL(c) q[at(i + 2 - dx, j + 2 - dy, k + 2 - dz, c, qn, qn, qn)]
#define QR(c) q[at(i + 2, j + 2, k + 2, c, qn, qn, qn)]
#define DL(c) dq[at(i + 1 - dx, j + 1 - dy, k + 1 - dz, c, dn, dn, dn)]
#define DR(c) dq[at(i + 1, j + 1, k + 1, c, dn, dn, dn)]
  double cspeed = QL(QCS);
  double rl = QL(QRHO) + 0.5 * ((DL(0) + DL(2)) / cspeed + DL(1));
  rl = dmax(rl, smallr);
  const double ul = QL(vn) + 0.5 * ((DL(2) - DL(0)) / QL(QRHO));
  double pl = QL(QPRES) + 0.5 * (DL(0) + DL(2)) * cspeed;
  pl = dmax(pl, smallp);
  const double ut1l = QL(t1) + 0.5 * DL(3);
  const double ut2l = QL(t2) + 0.5 * DL(4);

  cspeed = QR(QCS);
  double rr = QR(QRHO) - 0.5 * ((DR(0) + DR(2)) / cspeed + DR(1));
  rr = dmax(rr, smallr);
  const double ur = QR(vn) - 0.5 * ((DR(2) - DR(0)) / QR(QRHO));
  double pr = QR(QPRES) - 0.5 * (DR(0) + DR(2)) * cspeed;
  pr = dmax(pr, smallp);
  const double ut1r = QR(t1) - 0.5 * DR(3);
  const double ut2r = QR(t2) - 0.5 * DR(4);
#undef QL
#undef QR
#undef DL
#undef DR
  double flx[5];
  riemann(gamma, smallp, rl, ul, pl, ut1l, ut2l, rr, ur, pr, ut1r, ut2r, flx);
  // Normal momentum and the tangential momenta in CNS's component order.
  const int un = dir == 0 ? UMX : (dir == 1 ? UMY : UMZ);
  const int ut1 = dir == 0 ? UMY : UMX;
  const int ut2 = dir == 2 ? UMY : UMZ;
  f[at(i, j, k, URHO, fx, fy, fz)] = flx[0];
  f[at(i, j, k, un, fx, fy, fz)] = flx[1];
  f[at(i, j, k, ut1, fx, fy, fz)] = flx[2];
  f[at(i, j, k, ut2, fx, fy, fz)] = flx[3];
  f[at(i, j, k, UEDEN, fx, fy, fz)] = flx[4];
  for (int c = NEQNS; c < NCONS; ++c) f[at(i, j, k, c, fx, fy, fz)] = 0.0;
}

[[loom::kernel(configure)]]
void cns_riemann_x(unsigned elements, int bs, double gamma, double smallp,
                   double smallr, [[loom::noalias]] const double* dq,
                   [[loom::noalias]] const double* q,
                   [[loom::noalias]] double* f) {
  const unsigned e = element_index();
  if (e < elements) cns_riemann(0, e, bs, gamma, smallp, smallr, dq, q, f);
}

[[loom::kernel(configure)]]
void cns_riemann_y(unsigned elements, int bs, double gamma, double smallp,
                   double smallr, [[loom::noalias]] const double* dq,
                   [[loom::noalias]] const double* q,
                   [[loom::noalias]] double* f) {
  const unsigned e = element_index();
  if (e < elements) cns_riemann(1, e, bs, gamma, smallp, smallr, dq, q, f);
}

[[loom::kernel(configure)]]
void cns_riemann_z(unsigned elements, int bs, double gamma, double smallp,
                   double smallr, [[loom::noalias]] const double* dq,
                   [[loom::noalias]] const double* q,
                   [[loom::noalias]] double* f) {
  const unsigned e = element_index();
  if (e < elements) cns_riemann(2, e, bs, gamma, smallp, smallr, dq, q, f);
}

// cns_flux_to_dudt over the valid cells, one work-item per cell and component.
[[loom::kernel(configure)]]
void cns_flux_to_dudt(unsigned elements, int bs, double dxinv0, double dxinv1,
                      double dxinv2, [[loom::noalias]] const double* fx,
                      [[loom::noalias]] const double* fy,
                      [[loom::noalias]] const double* fz,
                      [[loom::noalias]] double* dudt) {
  const unsigned e = element_index();
  if (e >= elements) return;
  const int cells = bs * bs * bs;
  const int n = e / cells;
  const int c = e % cells;
  const int i = c % bs, j = (c / bs) % bs, k = c / (bs * bs);
  const int b1 = bs + 1;
  dudt[at(i, j, k, n, bs, bs, bs)] =
      dxinv0 * (fx[at(i, j, k, n, b1, bs, bs)] -
                fx[at(i + 1, j, k, n, b1, bs, bs)]) +
      dxinv1 * (fy[at(i, j, k, n, bs, b1, bs)] -
                fy[at(i, j + 1, k, n, bs, b1, bs)]) +
      dxinv2 * (fz[at(i, j, k, n, bs, bs, b1)] -
                fz[at(i, j, k + 1, n, bs, bs, b1)]);
}

//===----------------------------------------------------------------------===//
// Runge-Kutta updates and time step (all boxes in one launch, as AMReX's
// MultiFab ParallelFor)
//===----------------------------------------------------------------------===//

// Index of element `e` of a [box][component][valid cell] iteration in the
// state array (with ghosts) and in dSdt (without).
[[loom::inline]] static void rk_indices(unsigned e, int bs, int* s_index,
                                        int* d_index) {
  const int cells = bs * bs * bs;
  const int c = e % cells;
  const int n = (e / cells) % NCONS;
  const int box = e / (cells * NCONS);
  const int i = c % bs, j = (c / bs) % bs, k = c / (bs * bs);
  const int sn = bs + 4;
  *s_index = box * sn * sn * sn * NCONS + at(i + 2, j + 2, k + 2, n, sn, sn, sn);
  *d_index = box * cells * NCONS + at(i, j, k, n, bs, bs, bs);
}

// Stage 1: snew = sold + dt * dsdt. The time step is read from device memory
// so that a recorded graph can be replayed with a new time step.
[[loom::kernel(configure)]]
void cns_rk_update(unsigned elements, int bs,
                   [[loom::noalias]] const double* time_step,
                   [[loom::noalias]] const double* sold,
                   [[loom::noalias]] const double* dsdt,
                   [[loom::noalias]] double* snew) {
  const unsigned e = element_index();
  if (e >= elements) return;
  const double dt = time_step[0];
  int si, di;
  rk_indices(e, bs, &si, &di);
  snew[si] = sold[si] + dt * dsdt[di];
}

// Stage 2: snew = 0.5 * (snew + sold + dsdt * dt).
[[loom::kernel(configure)]]
void cns_rk2_update_2(unsigned elements, int bs,
                      [[loom::noalias]] const double* time_step,
                      [[loom::noalias]] const double* sold,
                      [[loom::noalias]] const double* dsdt,
                      [[loom::noalias]] double* snew) {
  const unsigned e = element_index();
  if (e >= elements) return;
  const double dt = time_step[0];
  int si, di;
  rk_indices(e, bs, &si, &di);
  snew[si] = 0.5 * (snew[si] + sold[si] + dsdt[di] * dt);
}

// Per-workgroup minimum of the stable time step over valid cells (cns_estdt).
// The host takes the minimum of the partial results and applies the CFL number.
[[loom::kernel(configure)]]
void cns_estdt(unsigned elements, int bs, double dx, double gamma,
               double smallr, double smallp, [[loom::noalias]] const double* s,
               [[loom::noalias]] double* partial) {
  const unsigned e = element_index();
  double dt = 1.0e300;
  if (e < elements) {
    const int cells = bs * bs * bs;
    const int c = e % cells;
    const int box = e / cells;
    const int sn = bs + 4;
    const int i = c % bs + 2, j = (c / bs) % bs + 2, k = c / (bs * bs) + 2;
    const int base = box * sn * sn * sn * NCONS;
    const double rho = s[base + at(i, j, k, URHO, sn, sn, sn)];
    const double rhoinv = 1.0 / dmax(rho, smallr);
    const double vx = s[base + at(i, j, k, UMX, sn, sn, sn)] * rhoinv;
    const double vy = s[base + at(i, j, k, UMY, sn, sn, sn)] * rhoinv;
    const double vz = s[base + at(i, j, k, UMZ, sn, sn, sn)] * rhoinv;
    const double ei = s[base + at(i, j, k, UEINT, sn, sn, sn)];
    const double p = dmax((gamma - 1.0) * ei, smallp);
    const double cs = __builtin_sqrt(gamma * p * rhoinv);
    const double dtx = dx / (__builtin_fabs(vx) + cs);
    const double dty = dx / (__builtin_fabs(vy) + cs);
    const double dtz = dx / (__builtin_fabs(vz) + cs);
    dt = dmin(dtx, dmin(dty, dtz));
  }
  const double group_min = loom::kernel::workgroup::reduce::minnumf(dt);
  if (loom::kernel::workitem::id.x == 0) {
    partial[loom::kernel::workgroup::id.x] = group_min;
  }
}

// Per-workgroup sums of the first five conserved components over valid cells
// (CNS::printTotal). partial[c * workgroups + group] receives component c; the
// host adds the partial sums. Each reduction is written out separately: a
// workgroup reduction in a loop would reuse its workgroup scratch slot.
[[loom::kernel(configure)]]
void cns_sums(unsigned elements, int bs, int workgroups,
              [[loom::noalias]] const double* s,
              [[loom::noalias]] double* partial) {
  const unsigned e = element_index();
  double rho = 0.0, mx = 0.0, my = 0.0, mz = 0.0, eden = 0.0;
  if (e < elements) {
    const int cells = bs * bs * bs;
    const int c = e % cells;
    const int box = e / cells;
    const int sn = bs + 4;
    const int i = c % bs + 2, j = (c / bs) % bs + 2, k = c / (bs * bs) + 2;
    const int base = box * sn * sn * sn * NCONS;
    rho = s[base + at(i, j, k, URHO, sn, sn, sn)];
    mx = s[base + at(i, j, k, UMX, sn, sn, sn)];
    my = s[base + at(i, j, k, UMY, sn, sn, sn)];
    mz = s[base + at(i, j, k, UMZ, sn, sn, sn)];
    eden = s[base + at(i, j, k, UEDEN, sn, sn, sn)];
  }
  const double sum_rho = loom::kernel::workgroup::reduce::addf(rho);
  const double sum_mx = loom::kernel::workgroup::reduce::addf(mx);
  const double sum_my = loom::kernel::workgroup::reduce::addf(my);
  const double sum_mz = loom::kernel::workgroup::reduce::addf(mz);
  const double sum_eden = loom::kernel::workgroup::reduce::addf(eden);
  if (loom::kernel::workitem::id.x == 0) {
    const unsigned group = loom::kernel::workgroup::id.x;
    partial[0 * workgroups + group] = sum_rho;
    partial[1 * workgroups + group] = sum_mx;
    partial[2 * workgroups + group] = sum_my;
    partial[3 * workgroups + group] = sum_mz;
    partial[4 * workgroups + group] = sum_eden;
  }
}
