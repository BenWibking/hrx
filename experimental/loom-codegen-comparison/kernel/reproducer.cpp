// SPDX-License-Identifier: BSD-3-Clause
// Seeded by generate.py from reference.cpp; edited by autoresearch campaign adv-mi210.
// Source SHA256: 6c7ca23933b211980e831e8d2bfc4328245ab0e3c75f6b07a5e0ed40307843d0
#include "support.h"
namespace chemistry {
constexpr int NumSpec = 14;
constexpr int neqs = NumSpec + 1;
constexpr int net_ienuc = NumSpec + 1;
constexpr Real default_redshift = 30.0;
constexpr Real grav_constant = 6.674e-8;
constexpr Real pi = 3.141592653589793238462643383279502884;
constexpr Real tff_reduc = 1.0e-1;
constexpr Real rtol_spec = 1.0e-4;
constexpr Real atol_spec = 1.0e-4;
constexpr Real rtol_energy = 1.0e-6;
constexpr Real atol_energy = 1.0e-6;
constexpr int perturbation_interval = 20;
constexpr Real perturbation_amplitude = 0.2;
constexpr size_type N = 15;
// The burn state abundances used by the RHS and Jacobian live in workgroup
// memory, element-interleaved across the 128 lanes (xn[k] at k * XS + lane).
constexpr unsigned XS = 128;
constexpr int SUCCESS = 1;
constexpr int BAD_INPUTS = -1;
constexpr int DT_UNDERFLOW = -2;
constexpr int TOO_MANY_STEPS = -4;
constexpr int TOO_MUCH_ACCURACY_REQUESTED = -5;
constexpr int LU_DECOMPOSITION_ERROR = -7;
struct ROS2SCoefficients {
    static constexpr Real gamma = 0.292893218813452;
    static constexpr Real ct2 = 0.585786437626905;
    static constexpr Real a21 = 2.0000000000000036;
    static constexpr Real a31 = 6.828427124746214;
    static constexpr Real a32 = 3.4142135623731007;
    static constexpr Real c21 = -6.828427124746214;
    static constexpr Real c31 = -10.949747468305889;
    static constexpr Real c32 = -7.535533905932761;
    static constexpr Real b1 = 6.828427124746214;
    static constexpr Real b2 = 3.414213562373101;
    static constexpr Real b3 = 1.0;
    static constexpr Real e1 = -0.23570226039551292;
    static constexpr Real e2 = -0.23570226039551567;
    static constexpr Real e3 = -0.13807118745769906;
};


using C = ROS2SCoefficients;
struct EosSums { Real sum_Abarinv; Real sum_gammasinv; Real gasconstant; };
struct IntegratorStats { u64 internal_steps; u64 rhs_calls; u64 jacobian_calls; u64 decompositions; u64 linear_solves; u64 accepted_steps; u64 rejected_steps; };
struct BurnRecord { Real rho; Real T; Real e; Real xn[14]; };
struct CellRecord { BurnRecord current; Real time; Real density_driver; int completed_steps; IntegratorStats stats; };
static_assert(sizeof(CellRecord) == 216);
struct ScratchRecord {
  Real t = 0.0;
  Real tout = 0.0;
  Real dt = 0.0;
  Real y[15] = {};
  Real rtol_vec[15] = {};
  Real atol_vec[15] = {};
  Real uround = 1.e-16;
  Real fac_min = 0.2;
  Real fac_max = 6.0;
  Real safe = 0.9;
  Real ynew[15] = {};
  Real ak1[15] = {};
  Real ak2[15] = {};
  Real work[15] = {};
  Real fjac[15][15] = {};
  Real e[15][15] = {};
  Real dy[15] = {};
  BurnRecord burn = {};
  int n_step = 0;
  int n_rhs = 0;
  int n_jac = 0;
  int n_accept = 0;
  int n_reject = 0;
  int n_decomp = 0;
  int n_solve = 0;
  int max_steps = 100000;
  int ip[15] = {};
};
static_assert(sizeof(ScratchRecord) == 4848);
struct BurnView { Real* rho; Real* T; Real* e; Real* xn; };
struct ScratchView {
  Real* t;
  Real* tout;
  Real* dt;
  Real* y;
  Real* rtol_vec;
  Real* atol_vec;
  Real* uround;
  Real* fac_min;
  Real* fac_max;
  Real* safe;
  Real* ynew;
  Real* ak1;
  Real* ak2;
  Real* work;
  Real* fjac;
  Real* e;
  Real* dy;
  BurnView burn;
  int* n_step;
  int* n_rhs;
  int* n_jac;
  int* n_accept;
  int* n_reject;
  int* n_decomp;
  int* n_solve;
  int* max_steps;
  int* ip;
};
DEVICE ScratchView scratch_view(ScratchRecord* s) {
  return {&s->t, &s->tout, &s->dt, s->y, s->rtol_vec, s->atol_vec, &s->uround, &s->fac_min, &s->fac_max, &s->safe, s->ynew, s->ak1, s->ak2, s->work, &s->fjac[0][0], &s->e[0][0], s->dy, {&s->burn.rho, &s->burn.T, &s->burn.e, s->burn.xn}, &s->n_step, &s->n_rhs, &s->n_jac, &s->n_accept, &s->n_reject, &s->n_decomp, &s->n_solve, &s->max_steps, s->ip};
}
DEVICE size_type matrix_index(size_type row, size_type column) {
  size_type index = row * 15 + column;
  __builtin_assume(index < 225);
  return index;
}
DEVICE Real small_number_density_floor() {
 return 1.0e-100;
}
DEVICE Real species_mass(int n) {

    return n == 0 ? 9.10938188e-28 :
           n == 1 ? 1.67262158e-24 :
           n == 2 ? 1.67353251819e-24 :
           n == 3 ? 1.67444345638e-24 :
           n == 4 ? 3.34512158e-24 :
           n == 5 ? 3.34603251819e-24 :
           n == 6 ? 3.34615409819e-24 :
           n == 7 ? 3.34694345638e-24 :
           n == 8 ? 3.34706503638e-24 :
           n == 9 ? 5.01865409819e-24 :
           n == 10 ? 5.01956503638e-24 :
           n == 11 ? 6.69024316e-24 :
           n == 12 ? 6.69115409819e-24 :
           6.69206503638e-24;

}
DEVICE Real species_gamma(int n) {

    return (n == 6 || n == 8 || n == 9 || n == 10) ? 1.4 : 5.0 / 3.0;

}
DEVICE Real density(const Real* xn) {

    Real rho = 0.0;
    for (size_type n = 0; n < NumSpec; ++n) {
        rho += xn[n] * species_mass(static_cast<int>(n));
    }
    return rho;

}
DEVICE EosSums eos_sums_from_number_densities(const Real* xn) {

    const Real gasconstant = 6.02214076e+23 * 1.3806490000000002e-16;
    const Real protonmass = 1.67262192595e-24;
    Real sum_abarinv = 0.0;
    Real sum_gammasinv = 0.0;
    Real rhotot = 0.0;

    [[loom::unroll]] for (unsigned n = 0; n < 14u; ++n) {
        rhotot += xn[n] * species_mass(static_cast<int>(n));
    }

    const Real rrhotot = 1.0 / rhotot;
    [[loom::unroll]] for (unsigned n = 0; n < 14u; ++n) {
        sum_abarinv += xn[n];
        sum_gammasinv += (xn[n] * protonmass * rrhotot) * (1.0 / (species_gamma(static_cast<int>(n)) - 1.0));
    }

    sum_abarinv *= protonmass * rrhotot;
    sum_gammasinv /= sum_abarinv;
    return {sum_abarinv, sum_gammasinv, gasconstant};

}
DEVICE EosSums eos_sums_strided(const Real* xn) {

    const Real gasconstant = 6.02214076e+23 * 1.3806490000000002e-16;
    const Real protonmass = 1.67262192595e-24;
    Real sum_abarinv = 0.0;
    Real sum_gammasinv = 0.0;
    Real rhotot = 0.0;

    [[loom::unroll]] for (unsigned n = 0; n < 14u; ++n) {
        rhotot += xn[n * XS] * species_mass(static_cast<int>(n));
    }

    const Real rrhotot = 1.0 / rhotot;
    [[loom::unroll]] for (unsigned n = 0; n < 14u; ++n) {
        sum_abarinv += xn[n * XS];
        sum_gammasinv += (xn[n * XS] * protonmass * rrhotot) * (1.0 / (species_gamma(static_cast<int>(n)) - 1.0));
    }

    sum_abarinv *= protonmass * rrhotot;
    sum_gammasinv /= sum_abarinv;
    return {sum_abarinv, sum_gammasinv, gasconstant};

}
DEVICE void eos_rt(BurnRecord* b) {

    const EosSums sums = eos_sums_from_number_densities(b->xn);
    b->e = sums.sum_gammasinv * sums.sum_Abarinv * sums.gasconstant * b->T;

}
DEVICE void eos_re(BurnRecord* b) {

    const EosSums sums = eos_sums_from_number_densities(b->xn);
    b->T = b->e / (sums.sum_gammasinv * sums.gasconstant * sums.sum_Abarinv);

}
DEVICE void eos_re_view(BurnView b) {

    const EosSums sums = eos_sums_strided(b.xn);
    b.T[0] = b.e[0] / (sums.sum_gammasinv * sums.gasconstant * sums.sum_Abarinv);

}
DEVICE void balance_charge(BurnRecord* b) {

    b->xn[0] = -b->xn[3] - b->xn[7] + b->xn[1] + b->xn[12] +
                  b->xn[6] + b->xn[4] + b->xn[9] + 2.0 * b->xn[11];

}
DEVICE void normalize_number_densities_to_density(BurnRecord* b, Real* mass_fractions) {


    // The mass fractions are recomputed from the not-yet-updated xn[n] in the
    // second loop (identical operations) instead of round-tripping through a
    // private array, which Loom promotes and then indexes via switch chains.
    (void)mass_fractions;
    Real sum = 0.0;
    for (size_type n = 0; n < NumSpec; ++n) {
        const Real mf = species_mass(static_cast<int>(n)) * b->xn[n] / b->rho;
        sum += mf;
    }
    for (size_type n = 0; n < NumSpec; ++n) {
        Real mf = species_mass(static_cast<int>(n)) * b->xn[n] / b->rho;
        mf /= sum;
        b->xn[n] = mf * b->rho / species_mass(static_cast<int>(n));
    }

}
DEVICE void floor_and_normalize_number_densities(BurnRecord* b, Real* mass) {

    for (int n = 0; n < NumSpec; ++n) {
        b->xn[n] = math::max(b->xn[n], small_number_density_floor());
    }
    normalize_number_densities_to_density(b, mass);

}
DEVICE auto rhs_specie_choice_0(Real T, Real x3, bool x4) {
return ((x4) ? (
   -5.5279999999999998e-28*((T)*(T)*(T)*(T)*(T)) + 3.3467999999999999e-23*((((T)*(T)))*(((T)*(T)))) - 7.5474000000000004e-19*((T)*(T)*(T)) - 2.3088e-11*T + 7.3427999999999993e-15*x3 + 4.2277999999999996e-8
)
: (
   0
));
}
DEVICE auto rhs_specie_choice_1(Real T, Real x10, Real x11, Real x12, Real x13, Real x14, Real x15, Real x8, Real x9) {
return ((T <= 1160.0) ? (
   1.4643482606109061e-16*math::exp((1.78186)*math::log(math::abs(T)))
)
: (
   3.3178155742407614e-14*math::exp(((1.1394493358416311)*math::log(math::abs(T))) + (-10.993097527150175*x10 + 14.449862906216714*x11 + 58.228375789703179*x12 - 162.59852239006702*x13 + 144.55426734953477*x14 - 44.454280878123605*x15 - 12.447178055372778*x8 + 6.9391784778399117*x9))
));
}
DEVICE auto rhs_specie_choice_2(Real T, Real x32, Real x33, Real x34) {
return ((x33 <= 9280.0) ? (
   x32
)
: (
   1250086.112245841*(math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T))*(1.5400000000000001e-9 + 4.6200000000000001e-10*math::exp(-93988.701501924661*x34))*math::exp(-469943.50750964211*x34) + x32
));
}
DEVICE auto rhs_specie_choice_3(Real T, Real x10, Real x11, Real x12, Real x13, Real x14, Real x15, Real x32, Real x33, Real x8, Real x9) {
return ((x33 <= 5500.0) ? (
   x32
)
: (
   3.2867337024382687e-10*math::exp(((-0.72411256578268512)*math::log(math::abs(T))) + (-2.4649195146505534*x10 - 1.020773727011937*x11 + 3.3530579587656564*x12 + 3.6203127646377791*x13 - 1.0930705283186732*x14 - 1.6921001126637107*x15 - 1.774686809424741*x8 - 1.951835616513679*x9))
));
}
DEVICE auto rhs_specie_choice_4(Real T, Real x28, Real x29) {
return ((T >= 10.0 & T <= 100000.0) ? (
   -7.7700000000000002e-13*T + 2.5000000000000002e-10*x28 + 2.96e-6*x29 - 1.73e-9
)
: (
   0
));
}
DEVICE auto rhs_specie_choice_5(Real T, Real x46, Real x52, Real x53, Real x57) {
return ((T < 30) ? (
   3.4977396723747635e-20*math::exp((-0.14999999999999999)*math::log(math::abs(T)))
)
: (
   math::exp((-3.194*x46 + 1.786*x52*x53 - 0.2072*x57 - 18.199999999999999)*math::log(math::abs(10)))
));
}
DEVICE auto rhs_specie_choice_6(Real T, Real x34, Real x52, Real x56, Real x6, Real x60, Real x61) {
    const auto ca = (-1.4491368e-7*x52 + 3.4172804999999998e-8*x56 + 3.5311931999999998e-13*((x6)*(x6)*(x6)*(x6)*(x6)*(x6)*(x6)) - 1.8171411000000001e-11*math::powi<6>(math::abs(x6)) + 3.3735381999999997e-7*x6 - 4.7813727999999997e-9*x60 + 3.9731542e-10*x61 - 3.3232183000000002e-7)*math::exp(-21237.150000000001*x34);
    const auto cb = (
   0
);
    return (T >= 100.0 & T <= 30000.0) ? ca : cb;
}
DEVICE auto rhs_specie_choice_7(Real T, Real x0, Real x34, bool x4) {
return ((x4) ? (
   1.26e-9*x0*math::exp(-127500.0*x34)
)
: (
   4.0000000000000003e-37*math::exp((4.7400000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto rhs_specie_choice_8(Real T, Real x34, bool x73) {
    const auto ca = 2.0000000000000001e-10*math::exp(((0.40200000000000002)*math::log(math::abs(T))) + (-37.100000000000001*x34)) - 3.3099999999999998e-17*math::exp((1.48)*math::log(math::abs(T)));
    const auto cb = (
   0
);
    return (x73) ? ca : cb;
}
DEVICE auto rhs_specie_choice_9(Real T, Real x34, bool x73) {
    const auto ca = 2.0299999999999998e-9*math::exp((-0.33200000000000002)*math::log(math::abs(T))) + 2.0600000000000001e-10*math::exp(((0.39600000000000002)*math::log(math::abs(T))) + (-33.0*x34));
    const auto cb = (
   0
);
    return (x73) ? ca : cb;
}
DEVICE auto rhs_specie_choice_10(Real T, Real x34, Real x44, Real x46, Real x54, Real x57, Real x60, Real x61) {
return ((T <= 1167.4796423742259) ? (
   math::exp((5.8888600000000002*x46 + 7.1969200000000004*x54 + 2.2506900000000001*x57 - 56.473700000000001 - 2.1690299999999998*x60/((((x44)*(x44)))*(((x44)*(x44)))) + 0.31788699999999998*x61/((x44)*(x44)*(x44)*(x44)*(x44)))*math::log(math::abs(10)))
)
: (
   3.1699999999999999e-10*math::exp(-5207.0*x34)
));
}
DEVICE auto rhs_specie_choice_11(Real T, Real x3, Real x34) {
return ((T > 200.0) ? (
   5.25e-11*math::exp(-4430.0*x34 + 173900.0/x3)
)
: (
   0
));
}
DEVICE void rhs_specie(Real temperature, Real* out, const Real* xn, Real z) {


    const Real g84_0 = xn[1 * XS];
    const Real g84_1_t1 = xn[10 * XS];
    const Real g84_1 = g84_0 + xn[10 * XS];
    const Real g84_2_t1 = xn[2 * XS];
    const Real g84_2 = g84_1 + xn[2 * XS];
    const Real g84_3_t1 = xn[3 * XS];
    const Real g84_3 = g84_2 + xn[3 * XS];
    const Real g84_4_t1 = 2.0 * xn[6 * XS];
    const Real g84_4 = g84_3 + 2.0 * xn[6 * XS];
    const Real g84_5_t1 = 2.0 * xn[8 * XS];
    const Real g84_5 = g84_4 + 2.0 * xn[8 * XS];
    const Real g84_6_t1 = xn[9 * XS];
    const Real g84_6 = g84_5 + xn[9 * XS];
    const Real x83 = g84_6;
    const Real T = temperature;
    const Real x0 = (math::exp((0.25)*math::log(math::abs(T)))*(1.0/T));
    const Real x2 = 1.3300135414628029e-18*math::exp(((0.94999999999999996)*math::log(math::abs(T))) + (-0.00010729613733905579*T))*xn[0 * XS]*xn[5 * XS];
    const Real acc0_0 = -(x2);
    const Real acc5_0 = -(x2);
    const Real acc7_0 = x2;
    const Real x1 = 2.5950363272655348e-10*xn[0 * XS]*xn[4 * XS]*x0;
    const Real acc0_1 = acc0_0 - x1;
    const Real acc4_0 = -(x1);
    const Real acc5_1 = acc5_0 + x1;
    const Real x3 = ((T)*(T));
    const Real x4 = T <= 10000.0;
    const Real x6 = math::log(math::abs(T));
    const Real x5 = xn[0 * XS]*xn[6 * XS]*rhs_specie_choice_0(T, x3, x4);
    const Real acc0_2 = acc0_1 - x5;
    const Real acc2_0 = 2 * x5;
    const Real acc6_0 = -(x5);
    const Real x7_t0 = 0.1068473250987532 * x6;
    const Real x7_t1 = 1;
    const Real x7 = 0.10684732509875319*x6 - 1;
    const Real x8 = ((x7)*(x7));
    const Real x9 = ((x7)*(x7)*(x7));
    const Real x10 = ((((x7)*(x7)))*(((x7)*(x7))));
    const Real x11 = ((x7)*(x7)*(x7)*(x7)*(x7));
    const Real x12 = math::powi<6>(math::abs(x7));
    const Real x13 = ((x7)*(x7)*(x7)*(x7)*(x7)*(x7)*(x7));
    const Real x15 = ((x7)*(x7)*(x7)*(x7)*(x7)*(x7)*(x7)*(x7)*(x7));
    const Real x14 = math::powi<8>(math::abs(x7));
    const Real x16 = xn[2 * XS]*xn[3 * XS];
    const Real x18 = xn[1 * XS]*xn[3 * XS];
    const Real x17 = x16*rhs_specie_choice_1(T, x10, x11, x12, x13, x14, x15, x8, x9);
    const Real acc0_3 = acc0_2 + x17;
    const Real g109_0 = x17;
    const Real x19 = 1.0e-8*math::exp((-0.40000000000000002)*math::log(math::abs(T)))*x18;
    const Real acc0_4 = acc0_3 + x19;
    const Real acc3_0 = -(x19);
    const Real x20 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));
    const Real x21 = xn[3 * XS]*xn[5 * XS];
    const Real x22 = x20*x21;
    const Real acc0_5 = acc0_4 + x22;
    const Real acc3_1 = acc3_0 - x22;
    const Real x23 = xn[0 * XS]*xn[2 * XS];
    const Real x25 = xn[0 * XS]*xn[12 * XS];
    const Real x24 = 1.4000000000000001e-18*math::exp(((0.92800000000000005)*math::log(math::abs(T))) + (-6.1728395061728397e-5*T))*x23;
    const Real acc0_6 = acc0_5 - x24;
    const Real g109_1 = g109_0 - x24;
    const Real x26 = 3.8571873359681582e-209*math::exp(((43.933476326349997)*math::log(math::abs(T))) + (-5902.1601240760483*x10 + 5825.9326359379538*x11 - 3578.1439181805954*x12 + 1242.7294446825149*x13 - 186.35635455381879*x14 - 1618.789587733125*x8 + 3854.4033653120223*x9))*x25;
    const Real acc0_7 = acc0_6 + x26;
    const Real x27 = 3.7903999274394518e-18*math::exp(((2.360852208681)*math::log(math::abs(T))) + (-258.18559308467115*x10 + 846.15238706523724*x11 - 1113.0879095147111*x12 + 671.95094388835207*x13 - 154.90262957142161*x14 - 24.766609674457612*x8 + 13.307984239358756*x9))*xn[0 * XS]*xn[3 * XS];
    const Real acc0_8 = acc0_7 + x27;
    const Real g109_2 = g109_1 + x27;
    const Real x28 = math::sqrt(T);
    const Real x29 = 1.0/x28;
    const Real x30 = xn[0 * XS]*x29;
    const Real x31 = 5.7884371785482823e-10*xn[11 * XS]*x30*math::exp(((-1.7524)*math::log(math::abs(0.00060040841663220993*x28 + 1.0))) + ((-0.24759999999999999)*math::log(math::abs(0.32668576019240059*x28 + 1.0))));
    const Real acc0_9 = acc0_8 - x31;
    const Real x32 = 1.4981088130721367e-10*(math::exp((0.36470000000000002)*math::log(math::abs(T)))*(1.0/T));
    const Real x34 = 1.0*(1.0/T);
    const Real x33 = 8.6173430000000006e-5*T;
    const Real x35_t0 = -4.352407911476755e-117 * math::exp((23.91596563 * math::log(math::abs(T))) + (-4361.9927099007555 * x10 + 4879.734514626049 * x11 - 3366.463969882694 * x12 + 1300.3028484326148 * x13 - 214.82451513312137 * x14 - 941.91483008145 * x8 + 2506.98665290609 * x9)) * xn[0 * XS] * xn[13 * XS];
    const Real x35_t1 = x25 * rhs_specie_choice_2(T, x32, x33, x34);
    const Real x35 = -4.3524079114767552e-117*math::exp(((23.915965629999999)*math::log(math::abs(T))) + (-4361.9927099007555*x10 + 4879.7345146260486*x11 - 3366.4639698826941*x12 + 1300.3028484326148*x13 - 214.82451513312137*x14 - 941.91483008144996*x8 + 2506.9866529060901*x9))*xn[0 * XS]*xn[13 * XS] + x25*rhs_specie_choice_2(T, x32, x33, x34);
    const Real acc0_10 = acc0_9 - x35;
    const Real x36_t0 = -5.908243863726507e-70 * math::exp((13.536556 * math::log(math::abs(T))) + (-2207.464350125769 * x10 + 2500.8077583366976 * x11 - 1768.8867461266502 * x12 + 704.1992662950037 * x13 - 120.0438480494693 * x14 - 502.72883252679094 * x8 + 1281.477767828706 * x9)) * x23;
    const Real x36_t1 = xn[0 * XS] * xn[1 * XS] * rhs_specie_choice_3(T, x10, x11, x12, x13, x14, x15, x32, x33, x8, x9);
    const Real x36 = -5.9082438637265071e-70*math::exp(((13.536555999999999)*math::log(math::abs(T))) + (-2207.4643501257692*x10 + 2500.8077583366976*x11 - 1768.8867461266502*x12 + 704.19926629500367*x13 - 120.0438480494693*x14 - 502.72883252679094*x8 + 1281.477767828706*x9))*x23 + xn[0 * XS]*xn[1 * XS]*rhs_specie_choice_3(T, x10, x11, x12, x13, x14, x15, x32, x33, x8, x9);
    const Real acc0_11 = acc0_10 - x36;
    const Real g79_0 = x36;
    const Real x40 = x16*(1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T))))/(0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0);
    const Real g44_0 = -(x40);
    const Real x37 = 7.1999999999999996e-8*xn[9 * XS]*x30;
    const Real g44_1 = g44_0 + x37;
    const Real x38 = xn[2 * XS]*xn[7 * XS];
    const Real x39 = x20*x38;
    const Real acc7_1 = acc7_0 - x39;
    const Real acc10_0 = x39;
    const Real g44_2 = g44_1 - x39;
    const Real x41 = xn[0 * XS]*xn[8 * XS];
    const Real x42 = 35.5*math::exp(((-2.2799999999999998)*math::log(math::abs(T))) + (-46707.0*x34))*x41;
    const Real g44_3 = g44_2 + x42;
    const Real x43 = g44_3;
    const Real acc0_12 = acc0_11 - x43;
    const Real acc2_1 = acc2_0 + x43;
    const Real x44 = M_LN10;
    const Real x45 = 1.0/x44;
    const Real x46 = x45*x6;
    const Real x47 = math::exp((-0.12690000000000001*math::powi<-3>(math::abs(x44))*((x6)*(x6)*(x6)) + 1.1180000000000001*math::powi<-2>(math::abs(x44))*((x6)*(x6)) - 1.5229999999999999*x46 - 19.379999999999999)*math::log(math::abs(10.0)));
    const Real x48 = xn[1 * XS]*xn[5 * XS];
    const Real x49 = x47*x48;
    const Real acc1_0 = -(x49);
    const Real x50 = x18*rhs_specie_choice_4(T, x28, x29);
    const Real acc1_1 = acc1_0 - x50;
    const Real acc2_2 = acc2_1 + 2 * x50;
    const Real acc3_2 = acc3_1 - x50;
    const Real x51 = 1.0000000000000001e-9*xn[1 * XS]*xn[10 * XS]*math::exp(-457.0*x34);
    const Real acc1_2 = acc1_1 - x51;
    const Real acc8_0 = x51;
    const Real x52 = ((x6)*(x6));
    const Real x53 = math::powi<-2>(math::abs(x44));
    const Real x54 = x52*x53;
    const Real x56 = ((x6)*(x6)*(x6));
    const Real x55_t0 = 8.460000000000001e-10 * x46;
    const Real x55_t1 = 1.3700000000000002e-10 * x54;
    const Real x55_t2 = 4.17e-10;
    const Real x55 = 8.4600000000000008e-10*x46 - 1.3700000000000002e-10*x54 + 4.1700000000000001e-10;
    const Real acc1_3 = acc1_2 + xn[4 * XS] * xn[8 * XS] * x55;
    const Real x57 = x56/((x44)*(x44)*(x44));
    const Real x58 = xn[1 * XS]*xn[2 * XS]*rhs_specie_choice_5(T, x46, x52, x53, x57);
    const Real acc2_3 = acc2_2 - x58;
    const Real x59 = 6.0e-10*xn[2 * XS]*xn[6 * XS];
    const Real acc8_1 = acc8_0 + x59;
    const Real x60 = ((((x6)*(x6)))*(((x6)*(x6))));
    const Real x61 = ((x6)*(x6)*(x6)*(x6)*(x6));
    const Real x62 = xn[1 * XS]*xn[8 * XS]*rhs_specie_choice_6(T, x34, x52, x56, x6, x60, x61);
    const Real acc8_2 = acc8_1 - x62;
    const Real x63 = -x59 + x62;
    const Real acc2_4 = acc2_3 + x63;
    const Real x64 = x19 + x58 + x63;
    const Real acc1_4 = acc1_3 - x64;
    const Real acc6_1 = acc6_0 + x64;
    const Real x65 = 6.3999999999999996e-10*xn[2 * XS]*xn[9 * XS];
    const Real acc10_1 = acc10_0 + x65;
    const Real x66 = -x65;
    const Real acc9_0 = x66;
    const Real g79_1 = g79_0 + x66;
    const Real x67 = math::exp((-0.5)*math::log(math::abs(T)));
    const Real acc5_2 = acc5_1 + 1.9745379206285203e-06 * xn[4 * XS] * xn[7 * XS] * x67;
    const Real x68 = xn[7 * XS]*x67;
    const Real x69 = 7.9674337148168363e-7*xn[1 * XS]*x68;
    const Real acc5_3 = acc5_2 + x69;
    const Real acc7_2 = acc7_1 - x69;
    const Real g79_2 = g79_1 + x69;
    const Real x70 = xn[1 * XS]*xn[13 * XS]*rhs_specie_choice_7(T, x0, x34, x4);
    const Real g79_3 = g79_2 + x70;
    const Real x71 = math::exp((0.25)*math::log(math::abs(T)));
    const Real x72 = 2.8833736969617052e-16*xn[12 * XS]*xn[2 * XS]*x71;
    const Real g79_4 = g79_3 - x72;
    const Real x73 = T >= 50.0;
    const Real x75 = xn[2 * XS]*xn[4 * XS];
    const Real x74 = x48*rhs_specie_choice_8(T, x34, x73);
    const Real acc4_1 = acc4_0 + x74;
    const Real x76 = x75*rhs_specie_choice_9(T, x34, x73);
    const Real acc4_2 = acc4_1 - x76;
    const Real x77 = x74 - x76;
    const Real acc5_4 = acc5_3 - x77;
    const Real g79_5 = g79_4 + x77;
    const Real x78 = g79_5;
    const Real acc1_5 = acc1_4 - x78;
    const Real acc2_5 = acc2_4 + x78;
    const Real x79 = 4.9999999999999996e-6*xn[3 * XS]*xn[6 * XS]*x29;
    const Real acc2_6 = acc2_5 + x79;
    const Real acc6_2 = acc6_1 - x79;
    const Real x80 = ((xn[2 * XS])*(xn[2 * XS])*(xn[2 * XS]));
    const Real x81 = 1.0/x71;
    const Real acc2_7 = acc2_6 + x80 * (-6.0000000000000005e-31 * x67 - 1.8e-31 * x81);
    const Real x82 = 1.0e-25*xn[2 * XS]*xn[5 * XS];
    const Real acc2_8 = acc2_7 - x82;
    const Real x85 = math::exp(-0.0022727272727272726*T);
    const Real x84_t0 = -133.8283 * x34;
    const Real x84_t1 = 4.890915 * x45 * x6;
    const Real x84_t2 = 0.474903 * x54;
    const Real x84 = -133.82830000000001*x34 - 4.8909149999999997*x45*x6 + 0.47490300000000002*x54;
    const Real x86 = math::exp(-0.00054054054054054055*T);
    const Real x87_t0 = -2.056313 * x85;
    const Real x87_t1 = 0.5864073 * x86;
    const Real x87_t2 = 0.8227443;
    const Real x87 = -2.0563129999999998*x85 + 0.58640729999999996*x86 + 0.82274429999999998;
    const Real x88_t0 = -69.70086 * x45 * math::log(40870.38 * x34 + 1.0);
    const Real x88_t1 = 4.633167 * x57;
    const Real x88 = -69.700860000000006*x45*math::log(40870.379999999997*x34 + 1.0) + 4.6331670000000003*x57;
    const Real x89 = math::exp((-23705.700000000001*x34 - 2080.4099999999999*x34/(math::exp((x87)*math::log(math::abs(math::exp((-x84 - 13.656822)*math::log(math::abs(10.0)))*x83))) + 1.0) - 68.422430000000006*x46 + 43.20243*x52*x53 - x88 - 178.4239 - (19.734269999999999*x45*math::log(16780.950000000001*x34 + 1.0) - 14.509090000000008*x46 + 37.886913*x52*x53 - x88 - 307.31920000000002)/(math::exp((x87)*math::log(math::abs(math::exp((-x84 - 14.82123)*math::log(math::abs(10.0)))*x83))) + 1.0))*math::log(math::abs(10.0)));
    const Real x90_t0 = 743.06 * x34;
    const Real x90_t1 = 2.464009 * x45 * x6;
    const Real x90_t2 = 0.1985955 * x54;
    const Real x90 = 743.05999999999995*x34 - 2.4640089999999999*x45*x6 + 0.19859550000000001*x54;
    const Real x91_t0 = 2.937507 * x85;
    const Real x91_t1 = 0.2358848 * x86;
    const Real x91_t2 = 0.7502286;
    const Real x91 = 2.9375070000000001*x85 + 0.23588480000000001*x86 + 0.75022860000000002;
    const Real x92_t0 = -21.36094 * x45 * math::log(27535.31 * x34 + 1.0);
    const Real x92_t1 = 0.2582097 * x57;
    const Real x92 = -21.360939999999999*x45*math::log(27535.310000000001*x34 + 1.0) + 0.25820969999999999*x57;
    const Real x93 = math::exp((-21467.790000000001*x34 - 1657.4099999999999*x34/(math::exp((x91)*math::log(math::abs(math::exp((-x90 - 8.1313220000000008)*math::log(math::abs(10.0)))*x83))) + 1.0) + 42.707410000000003*x45*x6 - 2.0273650000000001*x54 - x92 - 142.7664 - (70.138370000000009*x45*x6 + 11.28215*x45*math::log(14254.549999999999*x34 + 1.0) - 4.7035149999999994*x54 - x92 - 203.11568)/(math::exp((x91)*math::log(math::abs(math::exp((-x90 - 9.3055640000000004)*math::log(math::abs(10.0)))*x83))) + 1.0))*math::log(math::abs(10.0)));
    const Real x95_t0 = 5e-32 * x67;
    const Real x95_t1 = 1.5e-32 * x81;
    const Real x95 = 5.0000000000000004e-32*x67 + 1.5e-32*x81;
    const Real x94 = xn[2 * XS]*xn[8 * XS];
    const Real acc2_9 = acc2_8 + x94 * (3 * x89 + 3 * x93);
    const Real x96 = ((xn[2 * XS])*(xn[2 * XS]))*xn[8 * XS];
    const Real acc2_10 = acc2_9 - x95 * x96;
    const Real acc8_3 = acc8_2 + x95 * x96;
    const Real acc8_4 = acc8_3 + x96 * (-2.5e-32 * x67 - 7.5e-33 * x81);
    const Real x97 = x47*x75;
    const Real acc2_11 = acc2_10 - x97;
    const Real acc4_3 = acc4_2 - x97;
    const Real acc9_1 = acc9_0 + x97;
    const Real x98 = 1.0/(math::exp((1.3*x45*(x6 - 9.2103403719761836) - 137.42519902360013*x53*((0.10857362047581294*x6 - 1)*(0.10857362047581294*x6 - 1)) - 4.8449999999999998)*math::log(math::abs(10.0)))*x83 + 1.0);
    const Real x100 = math::exp(((0.34999999999999998)*math::log(math::abs(T))) + (-102000.0*x34))*x41;
    const Real acc2_12 = acc2_11 + 8.76e-10 * x100;
    const Real acc8_5 = acc8_4 + -4.38e-10 * x100;
    const Real x101 = xn[5 * XS]*xn[8 * XS]*rhs_specie_choice_10(T, x34, x44, x46, x54, x57, x60, x61);
    const Real acc8_6 = acc8_5 - x101;
    const Real x99 = ((xn[8 * XS])*(xn[8 * XS]))*math::exp(((x98)*math::log(math::abs(1.1800000000000001e-10*math::exp(-69500.0*x34)))) + ((1.0 - x98)*math::log(math::abs(8.1250000000000003e-8*x67*(1.0 - math::exp(-6000.0*x34))*math::exp(-52000.0*x34)))));
    const Real acc2_13 = acc2_12 + 2 * x99;
    const Real acc8_7 = acc8_6 - x99;
    const Real x102 = xn[10 * XS]*xn[2 * XS]*rhs_specie_choice_11(T, x3, x34);
    const Real acc8_8 = acc8_7 + x102;
    const Real x103 = x101 - x102;
    const Real acc2_14 = acc2_13 + x103;
    const Real x104 = 6.1739095063118665e-10*math::exp((0.40999999999999998)*math::log(math::abs(T)));
    const Real x105 = x104*x21;
    const Real acc7_3 = acc7_2 + x105;
    const Real x106 = x104*x38;
    const Real acc7_4 = acc7_3 - x106;
    const Real x107 = x105 - x106;
    const Real acc5_5 = acc5_4 - x107;
    const Real g109_3 = g109_2 + x107;
    const Real x108 = g109_3;
    const Real acc2_15 = acc2_14 + x108;
    const Real acc3_3 = acc3_2 - x108;
    const Real x109_t0 = x80 * (2e-31 * x67 + 6e-32 * x81);
    const Real x109_t1 = x94 * (-x89 - x93);
    const Real x109 = x80*(2.0000000000000002e-31*x67 + 6.0000000000000001e-32*x81) + x94*(-x89 - x93);
    const Real acc2_16 = acc2_15 + x109;
    const Real acc8_9 = acc8_8 + x109;
    const Real x110 = x40 - x42 + x79;
    const Real acc3_4 = acc3_3 - x110;
    const Real acc8_10 = acc8_9 + x110;
    const Real x111 = 9.8726896031426014e-7*xn[4 * XS]*x68;
    const Real acc4_4 = acc4_3 - x111;
    const Real acc7_5 = acc7_4 - x111;
    const Real x112 = -x51;
    const Real acc4_5 = acc4_4 - x112;
    const Real acc10_2 = acc10_1 + x112;
    const Real x113 = -x55;
    const Real acc4_6 = acc4_5 + xn[4 * XS] * xn[8 * XS] * x113;
    const Real x114 = -x37 + x49;
    const Real acc5_6 = acc5_5 - x114;
    const Real acc9_2 = acc9_1 + x114;
    const Real x115 = x103 + x22 + x82;
    const Real acc5_7 = acc5_6 - x115;
    const Real acc10_3 = acc10_2 + x115;
    const Real x116 = xn[4 * XS]*xn[8 * XS];
    const Real acc8_11 = acc8_10 + x113 * x116;
    const Real acc10_4 = acc10_3 + x116 * x55;
    const Real x117 = x26 - x31;
    const Real acc11_0 = x117;
    const Real acc12_0 = -(x117);
    const Real x118 = x35 - x70 + x72;
    const Real acc12_1 = acc12_0 - x118;
    const Real acc13_0 = x118;
    out[0] = acc0_12;
    out[1] = acc1_5;
    out[2] = acc2_16;
    out[3] = acc3_4;
    out[4] = acc4_6;
    out[5] = acc5_7;
    out[6] = acc6_2;
    out[7] = acc7_5;
    out[8] = acc8_11;
    out[9] = acc9_2;
    out[10] = acc10_4;
    out[11] = acc11_0;
    out[12] = acc12_1;
    out[13] = acc13_0;




}
DEVICE auto rhs_eint_choice_0(Real x4, bool x6) {
return ((x6) ? (
   1.0/10.0
)
: (
   x4
));
}
DEVICE auto rhs_eint_choice_1(bool x6, Real x9) {
return ((x6) ? (
   math::sqrt(10)
)
: (
   x9
));
}
DEVICE auto rhs_eint_choice_2(Real T, bool x6) {
    const auto ca = 0.63095734448019325;
    const auto cb = (
   math::exp((-0.20000000000000001)*math::log(math::abs(T)))
);
    return (x6) ? ca : cb;
}
DEVICE auto rhs_eint_choice_3(Real T, bool x6) {
    const auto ca = 5.011872336272722;
    const auto cb = (
   math::exp((0.69999999999999996)*math::log(math::abs(T)))
);
    return (x6) ? ca : cb;
}
DEVICE auto rhs_eint_choice_4(Real T, bool x6) {
    const auto ca = 0.67810976749343443;
    const auto cb = (
   math::exp((-0.16869999999999999)*math::log(math::abs(T)))
);
    return (x6) ? ca : cb;
}
DEVICE auto rhs_eint_choice_5(Real T) {
return ((T >= 10000.0) ? (
   10000.0
)
: (
   T
));
}
DEVICE auto rhs_eint_choice_6(bool x46, const Real* xn) {
return ((x46) ? (
   false
)
: (
   xn[2 * XS] >= 10000000000.0
));
}
DEVICE auto rhs_eint_choice_7(bool x46, const Real* xn) {
return ((x46) ? (
      0.01
   )
   : (
      xn[2 * XS]
   ));
}
DEVICE auto rhs_eint_choice_8(bool x46, const Real* xn) {
    const auto ca = 10000000000.0;
    const auto cb = (
   rhs_eint_choice_7(x46, xn)
);
    return (rhs_eint_choice_6(x46, xn)) ? ca : cb;
}
DEVICE auto rhs_eint_choice_9(Real x0, bool x55) {
return ((x55) ? (
      0.5
   )
   : (
      x0
   ));
}
DEVICE auto rhs_eint_choice_10(Real x0, bool x55, Real x56, bool x57) {
return ((x57) ? (
   math::exp((x56*math::log(rhs_eint_choice_9(x0, x55)) + 2.1498900000000001)*math::log(math::abs(10.0)))
)
: (
   0.0
));
}
DEVICE auto rhs_eint_choice_11(Real x87, bool x88) {
return ((x88) ? (
      300.0
   )
   : (
      x87
   ));
}
DEVICE auto rhs_eint_choice_12(Real x84, bool x85, Real x87, bool x88) {
return ((x85) ? (
   x84
)
: (
   5.5313336794064847e-19/(math::exp(rhs_eint_choice_11(x87, x88)) + 1.0)
));
}
DEVICE auto rhs_eint_choice_13(Real x72, bool x73, Real x84, bool x85, Real x87, bool x88) {
return ((x73) ? (
   x72
)
: rhs_eint_choice_12(x84, x85, x87, x88));
}
DEVICE auto rhs_eint_choice_14(Real x117, bool x118) {
return ((x118) ? (
   x117
)
: (
   0
));
}
DEVICE auto rhs_eint_choice_15(Real x113, bool x116, Real x117, bool x118) {
return ((x116) ? (
   x113
)
: rhs_eint_choice_14(x117, x118));
}
DEVICE auto rhs_eint_choice_16(Real x102, Real x103, bool x98) {
return ((x98) ? (
   x102
)
: (
   x103
));
}
DEVICE auto rhs_eint_choice_17(Real x101, Real x96, bool x98) {
return ((x98) ? (
   x96
)
: (
   x101
));
}
DEVICE auto rhs_eint_choice_18(Real x108, bool x109, Real x111) {
return ((x109) ? (
   x108
)
: (
   x111
));
}
DEVICE auto rhs_eint_choice_19(Real x106, bool x107, Real x108, bool x109, Real x111) {
return ((x107) ? (
   x106
)
: rhs_eint_choice_18(x108, x109, x111));
}
DEVICE auto rhs_eint_choice_20(Real x104, bool x105, Real x106, bool x107, Real x108, bool x109, Real x111) {
    const auto ca = x104;
    const auto cb = rhs_eint_choice_19(x106, x107, x108, x109, x111);
    return (x105) ? ca : cb;
}
DEVICE auto rhs_eint_choice_21(bool x57, Real x64, Real x69) {
return ((x57) ? (
                                 2.1648287161311648e-38/((x64*x69 + 1.471335691176954e-59)*(x64*x69 + 1.471335691176954e-59)) >= 1.0
                              )
                              : (
                                 true
                              ));
}
DEVICE auto rhs_eint_choice_22(bool x55, bool x57, Real x63, Real x64, Real x69) {
    const auto ca = 2.1648287161311648e-38/((x63*x69 + 1.471335691176954e-59)*(x63*x69 + 1.471335691176954e-59)) >= 1.0;
    const auto cb = (
                              rhs_eint_choice_21(x57, x64, x69)
                           );
    return (x55 & x57) ? ca : cb;
}
DEVICE auto rhs_eint_choice_23(bool x55, bool x57, bool x61, Real x63, Real x64, Real x69) {
    const auto ca = true;
    const auto cb = (
                           rhs_eint_choice_22(x55, x57, x63, x64, x69)
                        );
    return (x61) ? ca : cb;
}
DEVICE auto rhs_eint_choice_24(bool x55, bool x57, bool x61, Real x63, Real x64, Real x67, Real x69) {
    const auto ca = 4.833962023629485e-72/((x64*x67 + 2.1986273043946045e-76)*(x64*x67 + 2.1986273043946045e-76)) >= 1.0;
    const auto cb = (
                        rhs_eint_choice_23(x55, x57, x61, x63, x64, x69)
                     );
    return (x57 & x61) ? ca : cb;
}
DEVICE auto rhs_eint_choice_25(bool x55, bool x57, bool x61, Real x63, Real x64, Real x67, Real x69) {
    const auto ca = 4.833962023629485e-72/((x63*x67 + 2.1986273043946045e-76)*(x63*x67 + 2.1986273043946045e-76)) >= 1.0;
    const auto cb = (
                     rhs_eint_choice_24(x55, x57, x61, x63, x64, x67, x69)
                  );
    return (x55 & x57 & x61) ? ca : cb;
}
DEVICE auto rhs_eint_choice_26(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x67, Real x69) {
    const auto ca = true;
    const auto cb = (
                  rhs_eint_choice_25(x55, x57, x61, x63, x64, x67, x69)
               );
    return (x60) ? ca : cb;
}
DEVICE auto rhs_eint_choice_27(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
    const auto ca = 216.48287161311649/((x64*x66 + 1.471335691176954e-39)*(x64*x66 + 1.471335691176954e-39)) >= 1.0;
    const auto cb = (
               rhs_eint_choice_26(x55, x57, x60, x61, x63, x64, x67, x69)
            );
    return (x57 & x60) ? ca : cb;
}
DEVICE auto rhs_eint_choice_28(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
    const auto ca = 216.48287161311649/((x63*x66 + 1.471335691176954e-39)*(x63*x66 + 1.471335691176954e-39)) >= 1.0;
    const auto cb = (
            rhs_eint_choice_27(x55, x57, x60, x61, x63, x64, x66, x67, x69)
         );
    return (x55 & x57 & x60) ? ca : cb;
}
DEVICE auto rhs_eint_choice_29(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
    const auto ca = true;
    const auto cb = (
         rhs_eint_choice_28(x55, x57, x60, x61, x63, x64, x66, x67, x69)
      );
    return (x60 & x61) ? ca : cb;
}
DEVICE auto rhs_eint_choice_30(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
    const auto ca = 4.8339620236294848e-32/((x64 + 2.1986273043946046e-56)*(x64 + 2.1986273043946046e-56)) >= 1.0;
    const auto cb = (
      rhs_eint_choice_29(x55, x57, x60, x61, x63, x64, x66, x67, x69)
   );
    return (x57 & x60 & x61) ? ca : cb;
}
DEVICE auto rhs_eint_choice_31(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
    const auto ca = 4.8339620236294848e-32/((x63 + 2.1986273043946046e-56)*(x63 + 2.1986273043946046e-56)) >= 1.0;
    const auto cb = (
   rhs_eint_choice_30(x55, x57, x60, x61, x63, x64, x66, x67, x69)
);
    return (x55 & x57 & x60 & x61) ? ca : cb;
}
DEVICE auto rhs_eint_choice_32(Real x59, bool x60) {
return ((x60) ? (
      9.9999999999999993e-41
   )
   : (
      x59
   ));
}
DEVICE auto rhs_eint_choice_33(bool x61, Real x68) {
return ((x61) ? (
      1.0e+80
   )
   : (
      2.232953576238777e+46*x68
   ));
}
DEVICE auto rhs_eint_choice_34(Real x59, bool x60) {
return ((x60) ? (
      9.9999999999999993e-41
   )
   : (
      x59
   ));
}
DEVICE auto rhs_eint_choice_35(bool x61, Real x68) {
return ((x61) ? (
      1.0e+80
   )
   : (
      2.232953576238777e+46*x68
   ));
}
DEVICE auto rhs_eint_choice_36(bool x55, bool x57, Real x58, Real x59, bool x60, bool x61, Real x62, Real x63, Real x64, Real x66, Real x67, Real x68, Real x69) {
return ((rhs_eint_choice_31(x55, x57, x60, x61, x63, x64, x66, x67, x69)) ? (
   1.0
)
: (
   483396202.36294854/((x58*x62*math::sqrt(rhs_eint_choice_32(x59, x60)*rhs_eint_choice_33(x61, x68)) + 2.1986273043946046e-36)*(x58*x62*math::sqrt(rhs_eint_choice_34(x59, x60)*rhs_eint_choice_35(x61, x68)) + 2.1986273043946046e-36))
));
}
DEVICE auto rhs_eint_choice_37(Real T, bool x6) {
    const auto ca = 0.4008667176273028;
    const auto cb = (
   math::exp((-0.39700000000000002)*math::log(math::abs(T)))
);
    return (x6) ? ca : cb;
}
DEVICE auto rhs_eint_choice_38(Real T, bool x6) {
    const auto ca = 0.031622776601683791;
    const auto cb = (
   (math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T))
);
    return (x6) ? ca : cb;
}
DEVICE auto rhs_eint_choice_39(Real T, bool x6) {
    const auto ca = 2.3157944032250755;
    const auto cb = (
   math::exp((0.36470000000000002)*math::log(math::abs(T)))
);
    return (x6) ? ca : cb;
}
DEVICE auto rhs_eint_choice_40(bool x70, Real x71) {
return ((x70) ? (
      true
   )
   : (
      x71 >= 1.0
   ));
}
DEVICE auto rhs_eint_choice_41(bool x70, Real x71) {
return ((x70) ? (
         1.000000000000001e+18
      )
      : (
         x71
      ));
}
DEVICE auto rhs_eint_choice_42(bool x70, Real x71) {
return ((rhs_eint_choice_40(x70, x71)) ? (
      1.0
   )
   : (
      rhs_eint_choice_41(x70, x71)
   ));
}
DEVICE auto rhs_eint_choice_43(bool x109, bool x118, bool x150, bool x151, bool x161) {
return ((x109 & x118) ? (
                                                                                                                                                                           x150 & x161
                                                                                                                                                                        )
                                                                                                                                                                        : (
                                                                                                                                                                           x151 & x161
                                                                                                                                                                        ));
}
DEVICE auto rhs_eint_choice_44(bool x107, bool x109, bool x118, bool x149, bool x150, bool x151, bool x161) {
return ((x107 & x118) ? (
                                                                                                                                                                        x149 & x161
                                                                                                                                                                     )
                                                                                                                                                                     : (
                                                                                                                                                                        rhs_eint_choice_43(x109, x118, x150, x151, x161)
                                                                                                                                                                     ));
}
DEVICE auto rhs_eint_choice_45(bool x105, bool x107, bool x109, bool x118, bool x148, bool x149, bool x150, bool x151, bool x161) {
return ((x105 & x118) ? (
                                                                                                                                                                     x148 & x161
                                                                                                                                                                  )
                                                                                                                                                                  : (
                                                                                                                                                                     rhs_eint_choice_44(x107, x109, x118, x149, x150, x151, x161)
                                                                                                                                                                  ));
}
DEVICE auto rhs_eint_choice_46(bool x105, bool x107, bool x109, bool x116, bool x118, bool x142, bool x148, bool x149, bool x150, bool x151, bool x161) {
return ((x116) ? (
                                                                                                                                                                  x142 & x161
                                                                                                                                                               )
                                                                                                                                                               : (
                                                                                                                                                                  rhs_eint_choice_45(x105, x107, x109, x118, x148, x149, x150, x151, x161)
                                                                                                                                                               ));
}
DEVICE auto rhs_eint_choice_47(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x161) {
return ((x109 & x114 & x115) ? (
                                                                                                                                                               x140 & x161
                                                                                                                                                            )
                                                                                                                                                            : (
                                                                                                                                                               rhs_eint_choice_46(x105, x107, x109, x116, x118, x142, x148, x149, x150, x151, x161)
                                                                                                                                                            ));
}
DEVICE auto rhs_eint_choice_48(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x161) {
return ((x107 & x114 & x115) ? (
                                                                                                                                                            x138 & x161
                                                                                                                                                         )
                                                                                                                                                         : (
                                                                                                                                                            rhs_eint_choice_47(x105, x107, x109, x114, x115, x116, x118, x140, x142, x148, x149, x150, x151, x161)
                                                                                                                                                         ));
}
DEVICE auto rhs_eint_choice_49(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x161) {
return ((x105 & x114 & x115) ? (
                                                                                                                                                         x135 & x161
                                                                                                                                                      )
                                                                                                                                                      : (
                                                                                                                                                         rhs_eint_choice_48(x105, x107, x109, x114, x115, x116, x118, x138, x140, x142, x148, x149, x150, x151, x161)
                                                                                                                                                      ));
}
DEVICE auto rhs_eint_choice_50(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x161, bool x88) {
return ((x88 & (x105 | x88) & (x107 | x88) & (x109 | x88) & (x114 | x88) & (x115 | x88) & (x118 | x88) & (x105 | x107 | x88) & (x105 | x109 | x88) & (x105 | x114 | x88) & (x105 | x115 | x88) & (x105 | x118 | x88) & (x107 | x109 | x88) & (x107 | x114 | x88) & (x107 | x115 | x88) & (x107 | x118 | x88) & (x109 | x114 | x88) & (x109 | x115 | x88) & (x109 | x118 | x88) & (x114 | x115 | x88) & (x115 | x118 | x88) & (x105 | x107 | x109 | x88) & (x105 | x107 | x114 | x88) & (x105 | x107 | x115 | x88) & (x105 | x107 | x118 | x88) & (x105 | x109 | x114 | x88) & (x105 | x109 | x115 | x88) & (x105 | x109 | x118 | x88) & (x105 | x114 | x115 | x88) & (x105 | x115 | x118 | x88) & (x107 | x109 | x114 | x88) & (x107 | x109 | x115 | x88) & (x107 | x109 | x118 | x88) & (x107 | x114 | x115 | x88) & (x107 | x115 | x118 | x88) & (x109 | x114 | x115 | x88) & (x109 | x115 | x118 | x88) & (x105 | x107 | x109 | x114 | x88) & (x105 | x107 | x109 | x115 | x88) & (x105 | x107 | x109 | x118 | x88) & (x105 | x107 | x114 | x115 | x88) & (x105 | x107 | x115 | x118 | x88) & (x105 | x109 | x114 | x115 | x88) & (x105 | x109 | x115 | x118 | x88) & (x107 | x109 | x114 | x115 | x88) & (x107 | x109 | x115 | x118 | x88) & (x105 | x107 | x109 | x114 | x115 | x88) & (x105 | x107 | x109 | x115 | x118 | x88)) ? (
                                                                                                                                                      false
                                                                                                                                                   )
                                                                                                                                                   : (
                                                                                                                                                      rhs_eint_choice_49(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x161)
                                                                                                                                                   ));
}
DEVICE auto rhs_eint_choice_51(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x159, bool x160, bool x161, bool x85, bool x88) {
return ((x85) ? (
                                                                                                                                                   x159 & x160
                                                                                                                                                )
                                                                                                                                                : (
                                                                                                                                                   rhs_eint_choice_50(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x161, x88)
                                                                                                                                                ));
}
DEVICE auto rhs_eint_choice_52(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88) {
return ((x109 & x85) ? (
                                                                                                                                                x158 & x160
                                                                                                                                             )
                                                                                                                                             : (
                                                                                                                                                rhs_eint_choice_51(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x159, x160, x161, x85, x88)
                                                                                                                                             ));
}
DEVICE auto rhs_eint_choice_53(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88) {
return ((x107 & x85) ? (
                                                                                                                                             x157 & x160
                                                                                                                                          )
                                                                                                                                          : (
                                                                                                                                             rhs_eint_choice_52(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x158, x159, x160, x161, x85, x88)
                                                                                                                                          ));
}
DEVICE auto rhs_eint_choice_54(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88) {
return ((x105 & x85) ? (
                                                                                                                                          x156 & x160
                                                                                                                                       )
                                                                                                                                       : (
                                                                                                                                          rhs_eint_choice_53(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x157, x158, x159, x160, x161, x85, x88)
                                                                                                                                       ));
}
DEVICE auto rhs_eint_choice_55(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x98) {
return ((x98) ? (
                                                                                                                                       x155 & x160
                                                                                                                                    )
                                                                                                                                    : (
                                                                                                                                       rhs_eint_choice_54(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x156, x157, x158, x159, x160, x161, x85, x88)
                                                                                                                                    ));
}
DEVICE auto rhs_eint_choice_56(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x85 & x97) ? (
                                                                                                                                    x154 & x160
                                                                                                                                 )
                                                                                                                                 : (
                                                                                                                                    rhs_eint_choice_55(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x155, x156, x157, x158, x159, x160, x161, x85, x88, x98)
                                                                                                                                 ));
}
DEVICE auto rhs_eint_choice_57(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x85 & x97) ? (
                                                                                                                                 x153 & x160
                                                                                                                              )
                                                                                                                              : (
                                                                                                                                 rhs_eint_choice_56(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                                              ));
}
DEVICE auto rhs_eint_choice_58(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x85 & x97) ? (
                                                                                                                              x152 & x160
                                                                                                                           )
                                                                                                                           : (
                                                                                                                              rhs_eint_choice_57(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                                           ));
}
DEVICE auto rhs_eint_choice_59(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x118 & x85) ? (
                                                                                                                           x151 & x160
                                                                                                                        )
                                                                                                                        : (
                                                                                                                           rhs_eint_choice_58(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                                        ));
}
DEVICE auto rhs_eint_choice_60(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x118 & x85) ? (
                                                                                                                        x150 & x160
                                                                                                                     )
                                                                                                                     : (
                                                                                                                        rhs_eint_choice_59(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                                     ));
}
DEVICE auto rhs_eint_choice_61(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x118 & x85) ? (
                                                                                                                     x149 & x160
                                                                                                                  )
                                                                                                                  : (
                                                                                                                     rhs_eint_choice_60(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                                  ));
}
DEVICE auto rhs_eint_choice_62(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x118 & x85) ? (
                                                                                                                  x148 & x160
                                                                                                               )
                                                                                                               : (
                                                                                                                  rhs_eint_choice_61(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                               ));
}
DEVICE auto rhs_eint_choice_63(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x118 & x85 & x97) ? (
                                                                                                               x147 & x160
                                                                                                            )
                                                                                                            : (
                                                                                                               rhs_eint_choice_62(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                            ));
}
DEVICE auto rhs_eint_choice_64(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x118 & x85 & x97) ? (
                                                                                                            x146 & x160
                                                                                                         )
                                                                                                         : (
                                                                                                            rhs_eint_choice_63(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                         ));
}
DEVICE auto rhs_eint_choice_65(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x118 & x85 & x97) ? (
                                                                                                         x145 & x160
                                                                                                      )
                                                                                                      : (
                                                                                                         rhs_eint_choice_64(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                      ));
}
DEVICE auto rhs_eint_choice_66(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x118 & x85 & x97) ? (
                                                                                                      x144 & x160
                                                                                                   )
                                                                                                   : (
                                                                                                      rhs_eint_choice_65(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                   ));
}
DEVICE auto rhs_eint_choice_67(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x114 & x115 & x85) ? (
                                                                                                   x142 & x160
                                                                                                )
                                                                                                : (
                                                                                                   rhs_eint_choice_66(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                                ));
}
DEVICE auto rhs_eint_choice_68(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x114 & x115 & x85) ? (
                                                                                                x140 & x160
                                                                                             )
                                                                                             : (
                                                                                                rhs_eint_choice_67(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                             ));
}
DEVICE auto rhs_eint_choice_69(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x114 & x115 & x85) ? (
                                                                                             x138 & x160
                                                                                          )
                                                                                          : (
                                                                                             rhs_eint_choice_68(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                          ));
}
DEVICE auto rhs_eint_choice_70(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x114 & x115 & x85) ? (
                                                                                          x135 & x160
                                                                                       )
                                                                                       : (
                                                                                          rhs_eint_choice_69(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                       ));
}
DEVICE auto rhs_eint_choice_71(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x114 & x115 & x85 & x97) ? (
                                                                                       x132 & x160
                                                                                    )
                                                                                    : (
                                                                                       rhs_eint_choice_70(x105, x107, x109, x114, x115, x116, x118, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                    ));
}
DEVICE auto rhs_eint_choice_72(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x114 & x115 & x85 & x97) ? (
                                                                                    x130 & x160
                                                                                 )
                                                                                 : (
                                                                                    rhs_eint_choice_71(x105, x107, x109, x114, x115, x116, x118, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                                 ));
}
DEVICE auto rhs_eint_choice_73(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x114 & x115 & x85 & x97) ? (
                                                                                 x128 & x160
                                                                              )
                                                                              : (
                                                                                 rhs_eint_choice_72(x105, x107, x109, x114, x115, x116, x118, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                              ));
}
DEVICE auto rhs_eint_choice_74(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x114 & x115 & x85 & x97) ? (
                                                                              x125 & x160
                                                                           )
                                                                           : (
                                                                              rhs_eint_choice_73(x105, x107, x109, x114, x115, x116, x118, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                           ));
}
DEVICE auto rhs_eint_choice_75(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x73) ? (
                                                                           x120 & x159
                                                                        )
                                                                        : (
                                                                           rhs_eint_choice_74(x105, x107, x109, x114, x115, x116, x118, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x85, x88, x97, x98)
                                                                        ));
}
DEVICE auto rhs_eint_choice_76(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x73) ? (
                                                                        x120 & x158
                                                                     )
                                                                     : (
                                                                        rhs_eint_choice_75(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                                     ));
}
DEVICE auto rhs_eint_choice_77(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x73) ? (
                                                                     x120 & x157
                                                                  )
                                                                  : (
                                                                     rhs_eint_choice_76(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                                  ));
}
DEVICE auto rhs_eint_choice_78(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x73) ? (
                                                                  x120 & x156
                                                               )
                                                               : (
                                                                  rhs_eint_choice_77(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                               ));
}
DEVICE auto rhs_eint_choice_79(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x73 & x85 & x97) ? (
                                                               x120 & x155
                                                            )
                                                            : (
                                                               rhs_eint_choice_78(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                            ));
}
DEVICE auto rhs_eint_choice_80(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x73 & x85 & x97) ? (
                                                            x120 & x154
                                                         )
                                                         : (
                                                            rhs_eint_choice_79(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                         ));
}
DEVICE auto rhs_eint_choice_81(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x73 & x85 & x97) ? (
                                                         x120 & x153
                                                      )
                                                      : (
                                                         rhs_eint_choice_80(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                      ));
}
DEVICE auto rhs_eint_choice_82(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x73 & x85 & x97) ? (
                                                      x120 & x152
                                                   )
                                                   : (
                                                      rhs_eint_choice_81(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                   ));
}
DEVICE auto rhs_eint_choice_83(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x151;
    const auto cb = (
                                                   rhs_eint_choice_82(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                );
    return (x118 & x73) ? ca : cb;
}
DEVICE auto rhs_eint_choice_84(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x150;
    const auto cb = (
                                                rhs_eint_choice_83(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                             );
    return (x109 & x118 & x73) ? ca : cb;
}
DEVICE auto rhs_eint_choice_85(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x149;
    const auto cb = (
                                             rhs_eint_choice_84(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                          );
    return (x107 & x118 & x73) ? ca : cb;
}
DEVICE auto rhs_eint_choice_86(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x148;
    const auto cb = (
                                          rhs_eint_choice_85(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                       );
    return (x105 & x118 & x73) ? ca : cb;
}
DEVICE auto rhs_eint_choice_87(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x147;
    const auto cb = (
                                       rhs_eint_choice_86(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                    );
    return (x118 & x73 & x85 & x97) ? ca : cb;
}
DEVICE auto rhs_eint_choice_88(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x146;
    const auto cb = (
                                    rhs_eint_choice_87(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                 );
    return (x109 & x118 & x73 & x85 & x97) ? ca : cb;
}
DEVICE auto rhs_eint_choice_89(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x118 & x73 & x85 & x97) ? (
                                 x120 & x145
                              )
                              : (
                                 rhs_eint_choice_88(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                              ));
}
DEVICE auto rhs_eint_choice_90(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x144;
    const auto cb = (
                              rhs_eint_choice_89(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                           );
    return (x105 & x118 & x73 & x85 & x97) ? ca : cb;
}
DEVICE auto rhs_eint_choice_91(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x142;
    const auto cb = (
                           rhs_eint_choice_90(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                        );
    return (x114 & x115 & x73) ? ca : cb;
}
DEVICE auto rhs_eint_choice_92(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x140;
    const auto cb = (
                        rhs_eint_choice_91(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                     );
    return (x109 & x114 & x115 & x73) ? ca : cb;
}
DEVICE auto rhs_eint_choice_93(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x138;
    const auto cb = (
                     rhs_eint_choice_92(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                  );
    return (x107 & x114 & x115 & x73) ? ca : cb;
}
DEVICE auto rhs_eint_choice_94(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x135;
    const auto cb = (
                  rhs_eint_choice_93(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
               );
    return (x105 & x114 & x115 & x73) ? ca : cb;
}
DEVICE auto rhs_eint_choice_95(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x132;
    const auto cb = (
               rhs_eint_choice_94(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
            );
    return (x114 & x115 & x73 & x85 & x97) ? ca : cb;
}
DEVICE auto rhs_eint_choice_96(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x130;
    const auto cb = (
            rhs_eint_choice_95(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
         );
    return (x109 & x114 & x115 & x73 & x85 & x97) ? ca : cb;
}
DEVICE auto rhs_eint_choice_97(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x114 & x115 & x73 & x85 & x97) ? (
         x120 & x128
      )
      : (
         rhs_eint_choice_96(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
      ));
}
DEVICE auto rhs_eint_choice_98(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
    const auto ca = x120 & x125;
    const auto cb = (
      rhs_eint_choice_97(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
   );
    return (x105 & x114 & x115 & x73 & x85 & x97) ? ca : cb;
}
DEVICE auto rhs_eint_choice_99(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, Real x119, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, Real x89, bool x97, bool x98) {
return ((rhs_eint_choice_98(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)) ? (
      x119*x89/(x119 + x89)
   )
   : (
      0
   ));
}
DEVICE auto rhs_eint_choice_100(Real T, bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, Real x119, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x70, Real x71, bool x73, bool x85, bool x88, Real x89, bool x97, bool x98, const Real* xn) {
    const auto ca = 0;
    const auto cb = (
   xn[8 * XS]*rhs_eint_choice_42(x70, x71)*rhs_eint_choice_99(x105, x107, x109, x114, x115, x116, x118, x119, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x89, x97, x98)
);
    return (T < 2.0) ? ca : cb;
}
DEVICE auto rhs_eint_choice_101(Real T, Real x25, Real x26, Real x35, Real x41, Real x42, Real x43, Real x44, Real x45, Real x47, Real x48, Real x49, Real x5, Real x50, Real x51, Real x52, Real x53, Real x54, const Real* xn) {
    const auto ca = math::exp((21.93385*x25*x41 + 0.92432999999999998*x25*x47 + 0.77951999999999999*x26*x41*x47 - 10.19097*x26*x45 + 0.54962*x26*x50 - 1.06447*x35*x41*x50 + 2.1990599999999998*x35*x44 - 0.54262999999999995*x35*x45*x47 - 0.076759999999999995*x35*x49 + 0.11864*x41*x42*x49 - 0.0036600000000000001*x41*x48*x51 - 0.17333999999999999*x42*x43 + 0.11711000000000001*x42*x44*x47 + 0.62343000000000004*x42*x45*x50 + 0.0027499999999999998*x42*x48 - 0.0083499999999999998*x43*x47*x51 + 6.1920000000000003e-5*x43*x48*x52 - 0.001482*x43*x49*x53 + 0.0106*x43*x50*x54 - 0.00066631000000000004*x44*x48*x53 + 0.017590000000000001*x44*x49*x54 - 0.13768*x44*x50*x51 + 0.0025140000000000002*x45*x48*x54 - 0.073660000000000003*x45*x49*x51 - 42.567880000000002)*math::log(math::abs(10.0)))*xn[10 * XS];
    const auto cb = (
   0
);
    return (T >= x5) ? ca : cb;
}
// ratefit e_r9_b5e4: R9: Piecewise((sqrt(10), T <= 10), (sqrt(T), True))*exp(-157809.10000000001*Piecewise((0.1, T <= 10), (1.0/T, True)))/(0.0031622776601683794*Piecewise((sqrt(10), T <= 10), (sqrt(T), True)) + 1.0)
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 9.49e-05 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r9_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r9_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = -222.8249991518313;
    const Real c1 = 1051.482825773355;
    const Real c2 = -2414.4889860195017;
    const Real c3 = 3705.6188742572135;
    const Real c4 = -4266.584117082097;
    const Real c5 = 3930.4140064303087;
    const Real c6 = -3016.485449319375;
    const Real c7 = 1980.6529386069274;
    const Real c8 = -1140.727422738757;
    const Real c9 = 593.1227648389493;
    const Real c10 = -272.2954756652405;
    const Real c11 = 101.65158926957893;
    const Real c12 = -39.628319436329235;
    const Real c13 = 22.2679197884391;
    const Real c14 = -7.148148150445711;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max(((((((((((((((c14 * t + c13) * t + c12) * t + c11) * t + c10) * t + c9) * t + c8) * t + c7) * t + c6) * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? ((((((((((((((14.0 * c14 * t + 13.0 * c13) * t + 12.0 * c12) * t + 11.0 * c11) * t + 10.0 * c10) * t + 9.0 * c9) * t + 8.0 * c8) * t + 7.0 * c7) * t + 6.0 * c6) * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

// ratefit e_r10_b5e4: R10: Piecewise((sqrt(10), T <= 10), (sqrt(T), True))*exp(-285335.40000000002*Piecewise((0.1, T <= 10), (1.0/T, True)))/(0.0031622776601683794*Piecewise((sqrt(10), T <= 10), (sqrt(T), True)) + 1.0)
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 0.000167 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r10_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r10_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = -406.8065450323319;
    const Real c1 = 1898.7507792628546;
    const Real c2 = -4365.393837856743;
    const Real c3 = 6700.307878582065;
    const Real c4 = -7714.374569064654;
    const Real c5 = 7106.601012893757;
    const Real c6 = -5454.135133843697;
    const Real c7 = 3581.2200932156115;
    const Real c8 = -2062.556321390013;
    const Real c9 = 1072.429546408289;
    const Real c10 = -492.3374468843878;
    const Real c11 = 183.79698997822703;
    const Real c12 = -71.65238837500853;
    const Real c13 = 40.26263249879602;
    const Real c14 = -12.924597351094965;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max(((((((((((((((c14 * t + c13) * t + c12) * t + c11) * t + c10) * t + c9) * t + c8) * t + c7) * t + c6) * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? ((((((((((((((14.0 * c14 * t + 13.0 * c13) * t + 12.0 * c12) * t + 11.0 * c11) * t + 10.0 * c10) * t + 9.0 * c9) * t + 8.0 * c8) * t + 7.0 * c7) * t + 6.0 * c6) * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

// ratefit e_r11_b5e4: R11: Piecewise((sqrt(10), T <= 10), (sqrt(T), True))*exp(-631515.0*Piecewise((0.1, T <= 10), (1.0/T, True)))/(0.0031622776601683794*Piecewise((sqrt(10), T <= 10), (sqrt(T), True)) + 1.0)
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 5.31e-05 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r11_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r11_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = -906.2380972713319;
    const Real c1 = 4198.7146113832405;
    const Real c2 = -9661.269757153945;
    const Real c3 = 14829.907097840982;
    const Real c4 = -17073.657474251762;
    const Real c5 = 15725.423176400845;
    const Real c6 = -12071.316003392702;
    const Real c7 = 7941.188081803724;
    const Real c8 = -4564.929239077215;
    const Real c9 = 2336.6084522490833;
    const Real c10 = -1089.6576982673646;
    const Real c11 = 455.13791874155345;
    const Real c12 = -158.58409872408623;
    const Real c13 = 56.876517594047854;
    const Real c14 = -28.605197165738517;
    const Real c15 = 8.59575926756749;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max((((((((((((((((c15 * t + c14) * t + c13) * t + c12) * t + c11) * t + c10) * t + c9) * t + c8) * t + c7) * t + c6) * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? (((((((((((((((15.0 * c15 * t + 14.0 * c14) * t + 13.0 * c13) * t + 12.0 * c12) * t + 11.0 * c11) * t + 10.0 * c10) * t + 9.0 * c9) * t + 8.0 * c8) * t + 7.0 * c7) * t + 6.0 * c6) * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

// ratefit e_r15_b5e4: R15: (1.0 + 0.29999999999999999*exp(-94000.0*Piecewise((0.1, T <= 10), (1.0/T, True))))*Piecewise((0.031622776601683791, T <= 10), (T**(-1.5), True))*exp(-470000.0*Piecewise((0.1, T <= 10), (1.0/T, True)))
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 0.000159 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r15_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r15_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = -693.0152193420815;
    const Real c1 = 3112.647523653809;
    const Real c2 = -7190.095728101195;
    const Real c3 = 11037.140091618963;
    const Real c4 = -12706.746101217766;
    const Real c5 = 11704.14020130267;
    const Real c6 = -8984.729499800203;
    const Real c7 = 5906.818823118263;
    const Real c8 = -3396.015569878372;
    const Real c9 = 1747.3922146372286;
    const Real c10 = -811.2274152579629;
    const Real c11 = 328.9506805855059;
    const Real c12 = -118.99225325687891;
    const Real c13 = 47.702798215676125;
    const Real c14 = -20.770614686177073;
    const Real c15 = 5.264963424071986;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max((((((((((((((((c15 * t + c14) * t + c13) * t + c12) * t + c11) * t + c10) * t + c9) * t + c8) * t + c7) * t + c6) * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? (((((((((((((((15.0 * c15 * t + 14.0 * c14) * t + 13.0 * c13) * t + 12.0 * c12) * t + 11.0 * c11) * t + 10.0 * c10) * t + 9.0 * c9) * t + 8.0 * c8) * t + 7.0 * c7) * t + 6.0 * c6) * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

// ratefit e_r16_b5e4: R16: Piecewise((0.4008667176273028, T <= 10), (T**(-0.39700000000000002), True))*exp(-473638.0*Piecewise((0.1, T <= 10), (1.0/T, True)))/(0.0031622776601683794*Piecewise((sqrt(10), T <= 10), (sqrt(T), True)) + 1.0)
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 4.06e-05 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r16_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r16_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = -687.4090590181403;
    const Real c1 = 3143.843029941156;
    const Real c2 = -7246.058108313751;
    const Real c3 = 11122.42179722205;
    const Real c4 = -12805.308018586675;
    const Real c5 = 11794.11085064059;
    const Real c6 = -9053.516386830695;
    const Real c7 = 5955.915557003138;
    const Real c8 = -3423.7091061600345;
    const Real c9 = 1752.462424090049;
    const Real c10 = -817.2467031344459;
    const Real c11 = 341.3545661313376;
    const Real c12 = -118.93844187403184;
    const Real c13 = 42.65761347325992;
    const Real c14 = -21.45397837899692;
    const Real c15 = 6.446833584739234;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max((((((((((((((((c15 * t + c14) * t + c13) * t + c12) * t + c11) * t + c10) * t + c9) * t + c8) * t + c7) * t + c6) * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? (((((((((((((((15.0 * c15 * t + 14.0 * c14) * t + 13.0 * c13) * t + 12.0 * c12) * t + 11.0 * c11) * t + 10.0 * c10) * t + 9.0 * c9) * t + 8.0 * c8) * t + 7.0 * c7) * t + 6.0 * c6) * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

// ratefit e_r17_b5e4: R17: Piecewise((0.67810976749343443, T <= 10), (T**(-0.16869999999999999), True))*exp(-13179.0*Piecewise((0.1, T <= 10), (1.0/T, True)))/(0.0031622776601683794*Piecewise((sqrt(10), T <= 10), (sqrt(T), True)) + 1.0)
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 5.87e-05 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r17_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r17_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = -20.83208062679335;
    const Real c1 = 86.13672947739808;
    const Real c2 = -201.922052546411;
    const Real c3 = 309.2828230709332;
    const Real c4 = -356.4910116911419;
    const Real c5 = 328.23747999134633;
    const Real c6 = -251.2131375153582;
    const Real c7 = 165.41806056765634;
    const Real c8 = -97.22181421728084;
    const Real c9 = 49.53146053658634;
    const Real c10 = -19.86863887506302;
    const Real c11 = 8.488865043580288;
    const Real c12 = -5.398559122431083;
    const Real c13 = 1.8597600683794897;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max((((((((((((((c13 * t + c12) * t + c11) * t + c10) * t + c9) * t + c8) * t + c7) * t + c6) * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? (((((((((((((13.0 * c13 * t + 12.0 * c12) * t + 11.0 * c11) * t + 10.0 * c10) * t + 9.0 * c9) * t + 8.0 * c8) * t + 7.0 * c7) * t + 6.0 * c6) * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

// ratefit e_r18_b5e4: R18: Piecewise((0.67810976749343443, T <= 10), (T**(-0.16869999999999999), True))*exp(-55338.0*Piecewise((0.1, T <= 10), (1.0/T, True)))/(0.0031622776601683794*Piecewise((sqrt(10), T <= 10), (sqrt(T), True)) + 1.0)
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 0.000228 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r18_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r18_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = -81.65488910219389;
    const Real c1 = 366.23557693995946;
    const Real c2 = -846.8500739346576;
    const Real c3 = 1299.2989509858946;
    const Real c4 = -1496.663750172834;
    const Real c5 = 1378.2551469792638;
    const Real c6 = -1054.8841829706198;
    const Real c7 = 694.5505663382365;
    const Real c8 = -408.2358178080109;
    const Real c9 = 207.98579195797328;
    const Real c10 = -83.42231057028914;
    const Real c11 = 35.64536490005466;
    const Real c12 = -22.669156238779255;
    const Real c13 = 7.808643501998336;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max((((((((((((((c13 * t + c12) * t + c11) * t + c10) * t + c9) * t + c8) * t + c7) * t + c6) * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? (((((((((((((13.0 * c13 * t + 12.0 * c12) * t + 11.0 * c11) * t + 10.0 * c10) * t + 9.0 * c9) * t + 8.0 * c8) * t + 7.0 * c7) * t + 6.0 * c6) * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

// ratefit e_r20_b5e4: R20: exp(-118348.0*Piecewise((0.1, T <= 10), (1.0/T, True)))/(0.0031622776601683794*Piecewise((sqrt(10), T <= 10), (sqrt(T), True)) + 1.0)
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 7.2e-05 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r20_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r20_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = -170.87756256867868;
    const Real c1 = 785.9865427458483;
    const Real c2 = -1810.8107529894064;
    const Real c3 = 2778.957297244505;
    const Real c4 = -3199.7170958931124;
    const Real c5 = 2947.5905960390514;
    const Real c6 = -2262.1913314093567;
    const Real c7 = 1485.3814491559685;
    const Real c8 = -855.4813021785585;
    const Real c9 = 444.8084732961911;
    const Real c10 = -204.20678931175465;
    const Real c11 = 76.23292761019856;
    const Real c12 = -29.718951894086853;
    const Real c13 = 16.69972595245165;
    const Real c14 = -5.360712601292671;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max(((((((((((((((c14 * t + c13) * t + c12) * t + c11) * t + c10) * t + c9) * t + c8) * t + c7) * t + c6) * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? ((((((((((((((14.0 * c14 * t + 13.0 * c13) * t + 12.0 * c12) * t + 11.0 * c11) * t + 10.0 * c10) * t + 9.0 * c9) * t + 8.0 * c8) * t + 7.0 * c7) * t + 6.0 * c6) * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

// ratefit e_r21_b5e4: R21: Piecewise((0.63095734448019325, T <= 10), (T**(-0.20000000000000001), True))*Piecewise((sqrt(10), T <= 10), (sqrt(T), True))/(6.3095734448019361e-5*Piecewise((5.011872336272722, T <= 10), (T**0.69999999999999996, True)) + 1.0)
// Fit of log2 k as a polynomial in t = (lnT - m) * s, T in [10, 100000] K;
// max |k_fit/k - 1| = 2.33e-05 over 1000008 points incl. segment boundaries (bound 0.0005);
// where the source is < 1e-200 the fit only has to be <= 2e-200 (practically zero).
// k = 2^n * q(f), n = rint(g2), f = g2 - n; q ~ 2^f (degree 4, rel err 3.3e-06, C1 at f = +-1/2).
// e_r21_b5e4_kd returns k and dk/dT from the SAME arithmetic (Jacobian-consistent).
// lnT must be math::log(math::abs(T)); rT = 1/T. Outside [tmin, tmax] the fit is NOT validated.
DEVICE Real e_r21_b5e4_kd(Real T, Real lnT, Real rT, Real& dkdT) {
    const Real m = 6.907755278982137;
    const Real s = 0.21714724095162588;
    const Real t = (lnT - m) * s;
    const Real c0 = 2.978306971223347;
    const Real c1 = 1.9563165251369596;
    const Real c2 = -0.058201662395405175;
    const Real c3 = -0.060151884923787174;
    const Real c4 = -0.05010603678941659;
    const Real c5 = -0.03400212809112832;
    const Real c6 = -0.011742393148110977;
    // Below 2^-1000 the rate is zero for all practical purposes (user rule): clamp, no subnormals.
    const Real g2 = math::max(((((((c6 * t + c5) * t + c4) * t + c3) * t + c2) * t + c1) * t + c0), -1000.0);
    const Real dg2 = g2 > -1000.0 ? ((((((6.0 * c6 * t + 5.0 * c5) * t + 4.0 * c4) * t + 3.0 * c3) * t + 2.0 * c2) * t + 1.0 * c1)) * s : 0.0;
    const Real sh = g2 + 0x1.8p52;
    const Real dn = sh - 0x1.8p52;
    const Real f = g2 - dn;
    const Real p2n = __builtin_bit_cast(Real, (u64)((int)__builtin_bit_cast(u64, sh) + 1023) << 52);
    const Real q = ((((0.009545028545081992 * f + 0.055959288841372606) * f + 0.2402561484606705) * f + 0.693116521568605) * f + 0.9999989142691869);
    const Real dq = (((0.03818011418032797 * f + 0.16787786652411782) * f + 0.480512296921341) * f + 0.693116521568605);
    const Real k = q * p2n;
    dkdT = dq * p2n * dg2 * rT;
    return k;
}

DEVICE Real rhs_eint(Real temperature, const Real* xn, Real z) {


    const Real g1_0 = 9.10938188e-28 * xn[0 * XS];
    const Real g1_1 = g1_0 + 1.67262158e-24 * xn[1 * XS];
    const Real g1_2 = g1_1 + 5.01956503638e-24 * xn[10 * XS];
    const Real g1_3 = g1_2 + 6.69024316e-24 * xn[11 * XS];
    const Real g1_4 = g1_3 + 6.69115409819e-24 * xn[12 * XS];
    const Real g1_5 = g1_4 + 6.69206503638e-24 * xn[13 * XS];
    const Real g1_6 = g1_5 + 1.67353251819e-24 * xn[2 * XS];
    const Real g1_7 = g1_6 + 1.67444345638e-24 * xn[3 * XS];
    const Real g1_8 = g1_7 + 3.34512158e-24 * xn[4 * XS];
    const Real g1_9 = g1_8 + 3.34603251819e-24 * xn[5 * XS];
    const Real g1_10 = g1_9 + 3.34615409819e-24 * xn[6 * XS];
    const Real g1_11 = g1_10 + 3.34694345638e-24 * xn[7 * XS];
    const Real g1_12 = g1_11 + 3.34706503638e-24 * xn[8 * XS];
    const Real g1_13 = g1_12 + 5.01865409819e-24 * xn[9 * XS];
    const Real x0 = g1_13;
    const Real g27_0 = xn[10 * XS];
    const Real g27_1 = g27_0 + xn[2 * XS];
    const Real g27_2 = g27_1 + xn[3 * XS];
    const Real g27_3 = g27_2 + xn[9 * XS];
    const Real x16 = g27_3;
    const Real g28_0 = xn[1 * XS];
    const Real g28_1 = g28_0 + 2.0 * xn[6 * XS];
    const Real g28_2 = g28_1 + 2.0 * xn[8 * XS];
    const Real g28_3 = g28_2 + x16;
    const Real x17 = g28_3;
    const Real g82_0 = xn[0 * XS];
    const Real g82_1 = g82_0 + xn[11 * XS];
    const Real g82_2 = g82_1 + xn[13 * XS];
    const Real g82_3 = g82_2 + xn[5 * XS];
    const Real g82_4 = g82_3 + xn[6 * XS];
    const Real g82_5 = g82_4 + xn[7 * XS];
    const Real g82_6 = g82_5 + xn[8 * XS];
    const Real g82_7 = g82_6 + x16;
    const Real g88_0 = 0.0001361221361489879 * xn[0 * XS];
    const Real g88_1 = g88_0 + 0.24994102282436673 * xn[1 * XS];
    const Real g88_2 = g88_1 + 0.7500771449608146 * xn[10 * XS];
    const Real g88_3 = g88_2 + 0.9997277557271044 * xn[11 * XS];
    const Real g88_4 = g88_3 + 0.9998638778635521 * xn[12 * XS];
    const Real g88_5 = g88_4 + xn[13 * XS];
    const Real g88_6 = g88_5 + 0.25007714496081457 * xn[2 * XS];
    const Real g88_7 = g88_6 + 0.25021326709726244 * xn[3 * XS];
    const Real g88_8 = g88_7 + 0.4998638778635522 * xn[4 * XS];
    const Real g88_9 = g88_8 + 0.5 * xn[5 * XS];
    const Real g88_10 = g88_9 + 0.5000181677851813 * xn[6 * XS];
    const Real g88_11 = g88_10 + 0.5001361221364479 * xn[7 * XS];
    const Real g88_12 = g88_11 + 0.5001542899216291 * xn[8 * XS];
    const Real g88_13 = g88_12 + 0.7499410228243667 * xn[9 * XS];
    const Real x65 = g88_13;
    const Real T = temperature;
    const Real lnTf = math::log(math::abs(T));
    const Real rTf = 1.0 / T;
    Real dkf = 0.0;
    const Real fr9 = e_r9_b5e4_kd(T, lnTf, rTf, dkf);
    const Real fr10 = e_r10_b5e4_kd(T, lnTf, rTf, dkf);
    const Real fr11 = e_r11_b5e4_kd(T, lnTf, rTf, dkf);
    const Real fr15 = e_r15_b5e4_kd(T, lnTf, rTf, dkf);
    const Real fr16 = e_r16_b5e4_kd(T, lnTf, rTf, dkf);
    const Real fr17 = e_r17_b5e4_kd(T, lnTf, rTf, dkf);
    const Real fr18 = e_r18_b5e4_kd(T, lnTf, rTf, dkf);
    const Real fr20 = e_r20_b5e4_kd(T, lnTf, rTf, dkf);
    const Real fr21 = e_r21_b5e4_kd(T, lnTf, rTf, dkf);
    const Real g96_0 = 4.985670872372847e-33 * math::exp((3.76 * math::log(math::abs(T))) + (-2197000.0 / (T * T * T))) / (6.014246803527264e-08 * math::exp(2.1 * math::log(math::abs(T))) + 1.0);
    const Real x1 = 1.0/x0;
    const Real x2 = math::sqrt(T);
    const Real x3 = xn[1 * XS] + xn[12 * XS] + xn[4 * XS];
    const Real g82_8 = g82_7 + x3;
    const Real x59 = g82_8;
    const Real acc0_0 = -(2.13e-27 * xn[0 * XS] * x2 * (4.0 * xn[11 * XS] + x3));
    const Real x4 = 1.0*(1.0/T);
    const Real g96_1 = g96_0 + 1.6e-18 * math::exp(-11700.0 * x4);
    const Real g96_2 = g96_1 + 6.7e-19 * math::exp(-5860.0 * x4);
    const Real g96_3 = g96_2 + 3e-24 * math::exp(-510.0 * x4);
    const Real x72 = g96_3;
    const Real acc0_1 = acc0_0 + -3.1438547368704e-21 * math::exp((0.35 * math::log(math::abs(T))) + (-102000.0 * x4)) * xn[0 * XS] * xn[8 * XS];
    const Real x5 = 2.73*z + 2.73;
    const Real acc0_2 = acc0_1 - 5.65e-36 * xn[0 * XS] * (T - x5) * ((z + 1.0) * (z + 1.0) * ((z + 1.0) * (z + 1.0)));
    const Real x6 = T <= 10;
    const Real x7 = xn[0 * XS]*xn[12 * XS];
    const Real acc0_3 = acc0_2 - 1.55e-26 * x7 * rhs_eint_choice_39(T, x6);
    const Real x8 = rhs_eint_choice_0(x4, x6);
    const Real acc0_4 = acc0_3 - 1.24e-13 * x7 * fr15;
    const Real x9 = math::sqrt(T);
    const Real x10 = rhs_eint_choice_1(x6, x9);
    const Real x11 = 1.0/(0.0031622776601683794*x10 + 1.0);
    const Real x12 = xn[0 * XS]*x11;
    const Real acc0_5 = acc0_4 - 9.38e-22 * xn[13 * XS] * xn[0 * XS] * fr10;
    const Real x13 = xn[2 * XS]*x12;
    const Real acc0_6 = acc0_5 - 1.27e-21 * xn[2 * XS] * xn[0 * XS] * fr9;
    const Real acc0_7 = acc0_6 - 7.5e-19 * xn[2 * XS] * xn[0 * XS] * fr20;
    const Real x14 = xn[0 * XS]*fr21;
    const Real acc0_8 = acc0_7 - 3.4635323838154264e-26 * xn[1 * XS] * x14;
    const Real acc0_9 = acc0_8 - 1.3854129535261706e-25 * xn[11 * XS] * x14;
    const Real x15 = 1.0/x2;
    const Real x18 = 1.0/(1000000.0*x15/(x17*(1.6000000000000001*xn[2 * XS]*math::exp(-160000.0/((T)*(T))) + 1.3999999999999999*xn[8 * XS]*math::exp(-12000.0/(T + 1200.0)))) + 1.0);
    const Real acc0_10 = acc0_9 + 5.6556829037999995e-12 * xn[2 * XS] * xn[3 * XS] * x18 * (1.35e-09 * math::exp(0.098493 * math::log(math::abs(T))) + 4.43502e-10 * math::exp(0.5561 * math::log(math::abs(T))) + 3.7408500000000004e-16 * math::exp(2.1826 * math::log(math::abs(T)))) / (0.006191 * math::exp(1.0461 * math::log(math::abs(T))) + 8.9712e-11 * math::exp(3.0424 * math::log(math::abs(T))) + 3.2576e-14 * math::exp(3.7741 * math::log(math::abs(T))) + 1.0);
    const Real acc0_11 = acc0_10 + 1.75918975308e-21 * xn[2 * XS] * xn[6 * XS] * x18;
    const Real x19 = x11*x7;
    const Real acc0_12 = acc0_11 - 4.95e-22 * x7 * fr11;
    const Real acc0_13 = acc0_12 - 5.54e-17 * x7 * fr16;
    const Real x20 = ((xn[0 * XS])*(xn[0 * XS]))*xn[12 * XS];
    const Real acc0_14 = acc0_13 - 5.01e-27 * x20 * fr18;
    const Real acc0_15 = acc0_14 - 9.1e-27 * x20 * fr17;
    const Real x21 = math::exp((-0.25)*math::log(math::abs(T)));
    const Real acc0_16 = acc0_15 + 7.1777505408e-12 * (xn[2 * XS] * xn[2 * XS] * xn[2 * XS]) * x18 * (2e-31 * x15 + 6e-32 * x21);
    const Real acc0_17 = acc0_16 + 7.1777505408e-12 * (xn[2 * XS] * xn[2 * XS]) * xn[8 * XS] * x18 * (2.5e-32 * x15 + 7.5e-33 * x21);
    const Real x22 = math::sqrt(M_PI);
    const Real acc0_18 = acc0_17 + 0.0008437377159599618 * T * (1.380648e-16 * xn[0 * XS] + 1.380648e-16 * xn[1 * XS] + 1.380648e-16 * xn[10 * XS] + 1.380648e-16 * xn[11 * XS] + 1.380648e-16 * xn[12 * XS] + 1.380648e-16 * xn[13 * XS] + 1.380648e-16 * xn[2 * XS] + 1.380648e-16 * xn[3 * XS] + 1.380648e-16 * xn[4 * XS] + 1.380648e-16 * xn[5 * XS] + 1.380648e-16 * xn[6 * XS] + 1.380648e-16 * xn[7 * XS] + 1.380648e-16 * xn[8 * XS] + 1.380648e-16 * xn[9 * XS]) / (math::sqrt(x1) * x22);
    const Real x23 = math::log(math::abs(T));
    const Real x24 = M_LN10;
    const Real x25 = 1.0/x24;
    const Real x26 = math::powi<-2>(math::abs(x24));
    const Real x27 = 1.0/(math::exp((1.3*x25*(x23 - 9.2103403719761836) - 137.42519902360013*x26*((0.10857362047581294*x23 - 1)*(0.10857362047581294*x23 - 1)) - 4.8449999999999998)*math::log(math::abs(10.0)))*x17 + 1.0);
    const Real acc0_19 = acc0_18 - 7.1777505408e-12 * (xn[8 * XS] * xn[8 * XS]) * math::exp((x27 * math::log(math::abs(1.18e-10 * math::exp(-69500.0 * x4)))) + ((1.0 - x27) * math::log(math::abs(8.125e-08 * x15 * (1.0 - math::exp(-6000.0 * x4)) * math::exp(-52000.0 * x4)))));
    const Real x28 = ((x23)*(x23));
    const Real x29 = x26*x28;
    const Real x30 = -4.8909149999999997*x23*x25 + 0.47490300000000002*x29 - 133.82830000000001*x4;
    const Real x31 = math::exp(-0.0022727272727272726*T);
    const Real x32 = math::exp(-0.00054054054054054055*T);
    const Real x33 = -2.0563129999999998*x31 + 0.58640729999999996*x32 + 0.82274429999999998;
    const Real x34 = x23*x25;
    const Real x35 = math::powi<-3>(x24);
    const Real x36 = ((x23)*(x23)*(x23))*x35;
    const Real x37 = -69.700860000000006*x25*math::log(40870.379999999997*x4 + 1.0) + 4.6331670000000003*x36;
    const Real x38 = -2.4640089999999999*x23*x25 + 0.19859550000000001*x29 + 743.05999999999995*x4;
    const Real x39 = 2.9375070000000001*x31 + 0.23588480000000001*x32 + 0.75022860000000002;
    const Real x40 = -21.360939999999999*x25*math::log(27535.310000000001*x4 + 1.0) + 0.25820969999999999*x36;
    const Real acc0_20 = acc0_19 - 7.1777505408e-12 * xn[2 * XS] * xn[8 * XS] * (math::exp((42.70741 * x23 * x25 - 2.027365 * x29 - 21467.79 * x4 - 1657.4099999999999 * x4 / (math::exp(x39 * math::log(math::abs(math::exp((-x38 - 8.131322) * math::log(math::abs(10.0))) * x17))) + 1.0) - x40 - 142.7664 - (70.13837000000001 * x23 * x25 + 11.28215 * x25 * math::log(14254.55 * x4 + 1.0) - 4.7035149999999994 * x29 - x40 - 203.11568) / (math::exp(x39 * math::log(math::abs(math::exp((-x38 - 9.305564) * math::log(math::abs(10.0))) * x17))) + 1.0)) * math::log(math::abs(10.0))) + math::exp((43.20243 * x26 * x28 - 68.42243 * x34 - x37 - 23705.7 * x4 - 2080.41 * x4 / (math::exp(x33 * math::log(math::abs(math::exp((-x30 - 13.656822) * math::log(math::abs(10.0))) * x17))) + 1.0) - 178.4239 - (19.73427 * x25 * math::log(16780.95 * x4 + 1.0) + 37.886913 * x26 * x28 - 14.509090000000008 * x34 - x37 - 307.3192) / (math::exp(x33 * math::log(math::abs(math::exp((-x30 - 14.82123) * math::log(math::abs(10.0))) * x17))) + 1.0)) * math::log(math::abs(10.0))));
    const Real x41 = math::log(rhs_eint_choice_5(T));
    const Real x42 = math::powi<-4>(math::abs(x24));
    const Real x43 = ((((x41)*(x41)))*(((x41)*(x41))));
    const Real x44 = ((x41)*(x41)*(x41));
    const Real x45 = ((x41)*(x41));
    const Real x46 = xn[2 * XS] <= 0.01;
    const Real x47 = math::log(rhs_eint_choice_8(x46, xn));
    const Real x48 = ((((x47)*(x47)))*(((x47)*(x47))));
    const Real x49 = ((x47)*(x47)*(x47));
    const Real x50 = ((x47)*(x47));
    const Real x51 = math::powi<-5>(x24);
    const Real x52 = math::powi<-8>(math::abs(x24));
    const Real x53 = math::powi<-7>(x24);
    const Real x54 = math::powi<-6>(math::abs(x24));
    const Real acc0_21 = acc0_20 - rhs_eint_choice_101(T, x25, x26, x35, x41, x42, x43, x44, x45, x47, x48, x49, x5, x50, x51, x52, x53, x54, xn);
    const Real x55 = x0 >= 0.5;
    const Real x56 = 1.0000420000000001*x25;
    const Real x57 = x0 >= 9.9999999999999998e-13;
    const Real x58 = rhs_eint_choice_10(x0, x55, x56, x57);
    const Real x60 = x59 <= 9.9999999999999993e-41;
    const Real x61 = x0 <= 9.9999999999999993e-41;
    const Real x62 = x0*x22*x9;
    const Real x63 = math::exp((2.1498900000000001 - 0.69317629274152892*x25)*math::log(math::abs(10.0)))*x62;
    const Real x64 = math::exp((x56*math::log(x0) + 2.1498900000000001)*math::log(math::abs(10.0)))*x62;
    const Real x66 = 1.0/math::fabs(x65);
    const Real x67 = math::sqrt(x59);
    const Real x68 = math::powi<-2>(math::abs(x65));
    const Real x69 = math::sqrt(x59*x68);
    const Real acc0_22 = acc0_21 - 0.00022681492 * (T * T * (T * T)) * x0 * x58 * rhs_eint_choice_36(x55, x57, x58, x59, x60, x61, x62, x63, x64, x66, x67, x68, x69);
    const Real x70 = 1.2500000000000001e-10*xn[0 * XS] + 1.2500000000000001e-10*xn[1 * XS] + 1.2500000000000001e-10*xn[10 * XS] + 1.2500000000000001e-10*xn[11 * XS] + 1.2500000000000001e-10*xn[12 * XS] + 1.2500000000000001e-10*xn[13 * XS] + 1.2500000000000001e-10*xn[2 * XS] + 1.2500000000000001e-10*xn[3 * XS] + 1.2500000000000001e-10*xn[4 * XS] + 1.2500000000000001e-10*xn[5 * XS] + 1.2500000000000001e-10*xn[6 * XS] + 1.2500000000000001e-10*xn[7 * XS] + 1.2500000000000001e-10*xn[8 * XS] + 1.2500000000000001e-10*xn[9 * XS] <= 9.9999999999999993e-41;
    const Real x71 = 28601.610899577994*math::exp((-0.45000000000000001)*math::log(math::abs(x59)));
    const Real x73 = T < 2000.0;
    const Real x74 = x23 - 6.9077552789821368;
    const Real x75 = 0.14476482730108395*x23 - 1;
    const Real x76 = x26*((x75)*(x75));
    const Real x77 = ((x75)*(x75)*(x75));
    const Real x78 = x35*x77;
    const Real x79 = ((((x75)*(x75)))*(((x75)*(x75))));
    const Real x80 = ((x75)*(x75)*(x75)*(x75)*(x75));
    const Real x81 = x54*math::powi<6>(math::abs(x75));
    const Real x82 = x53*((x75)*(x75)*(x75)*(x75)*(x75)*(x75)*(x75));
    const Real x83 = math::powi<8>(math::abs(x75));
    const Real x84 = math::exp((5.0194035000000001*x25*x74 + 5627.2167698544854*x42*x79 + 86051.290034608537*x51*x80 + 9415777.8988952208*x52*x83 - 75.100986441619156*x76 - 1554.3387057364687*x78 - 428804.85473346239*x81 - 1662263.0320406025*x82 - 20.584225)*math::log(math::abs(10.0)));
    const Real x85 = T <= 10000.0;
    const Real x86 = 0.00020000000000000001*T;
    const Real x87 = x86 - 6.0;
    const Real x88 = x87 >= 300.0;
    const Real x89 = rhs_eint_choice_13(x72, x73, x84, x85, x87, x88);
    const Real x90 = math::exp((25.0*x25)*math::log(math::abs(T)));
    const Real x91 = math::exp((-200.0 + 20000.0/((10.0 + 2.3538526683701997e+17/x90)*(1.6889118802245084e-48*x90 + 10.0)))*math::log(math::abs(10.0)));
    const Real x92 = x42*x79;
    const Real x93 = x51*x80;
    const Real x94 = math::exp((2.0943374000000001*x25*x74 + 144.02112655888752*x35*x77 - 36.814414747418546*x76 - 339.5619991617852*x92 - 529.07725573213918*x93 - 23.962112000000001)*math::log(math::abs(10.0)))*xn[8 * XS]*x91;
    const Real g143_0 = x94;
    const Real x95 = x25*x74;
    const Real x96 = math::exp((-38.89917505778142*x76 + 95.70878894783884*x78 - 377.88183430702219*x92 + 3018.4974183098116*x93 + 2.1892372*x95 - 23.689236999999999)*math::log(math::abs(10.0)))*xn[13 * XS];
    const Real x97 = T > 10.0;
    const Real x98 = x85 && x97;
    const Real x99 = math::exp((16.666666666666664*x25)*math::log(math::abs(T)));
    const Real x100 = math::exp((-200.0 + 20000.0/((10.0 + 785.77199422741614/x99)*(5.0592917094448065e-34*x99 + 10.0)))*math::log(math::abs(10.0)));
    const Real x101 = 1.002560385050777e-22*xn[13 * XS]*x100;
    const Real g143_1 = g143_0 + rhs_eint_choice_17(x101, x96, x98);
    const Real x102 = math::exp((0.73442154540113413*x76 - 77.855706084264682*x78 - 1161.2797752309887*x92 + 5059.6285287169567*x93 + 1.5714710999999999*x95 - 22.089523)*math::log(math::abs(10.0)))*xn[1 * XS];
    const Real x103 = 1.1825091393820599e-21*xn[1 * XS]*x100;
    const Real g143_2 = g143_1 + rhs_eint_choice_16(x102, x103, x98);
    const Real x104 = math::exp((2774.5177117396752*x76 + 16037.924047681272*x78 + 45902.322591745004*x92 + 60522.293708798054*x93 + 37.383713*x95 - 16.818342000000001)*math::log(math::abs(10.0)))*xn[2 * XS];
    const Real x105 = T <= 100.0;
    const Real x106 = math::exp((3.5692468000000002*x25*x74 - 540.77102118284597*x76 - 9179.8864335208946*x78 - 48562.751069188118*x92 - 66875.646562351845*x93 - 24.311209000000002)*math::log(math::abs(10.0)))*xn[2 * XS];
    const Real x107 = T <= 1000.0;
    const Real x108 = math::exp((-177.55453097873294*x76 + 1956.911370108365*x78 - 12547.661945180447*x92 + 24439.250555499191*x93 + 4.6450521*x95 - 24.311209000000002)*math::log(math::abs(10.0)))*xn[2 * XS];
    const Real x109 = T <= 6000.0;
    const Real x110 = math::exp((17.997580222853362*x25)*math::log(math::abs(T)));
    const Real x111 = 1.8623144679125181e-22*math::exp((-200.0 + 20000.0/((10.0 + 2973.7534532281375/x110)*(1.3368457736780898e-34*x110 + 10.0)))*math::log(math::abs(10.0)))*xn[2 * XS];
    const Real g143_3 = g143_2 + rhs_eint_choice_20(x104, x105, x106, x107, x108, x109, x111);
    const Real x112 = x52*x83;
    const Real x113 = math::exp((366063607.58415633*x112 + 4616.3011562659685*x76 + 113122.17137872758*x78 + 87115306.05744876*x81 + 273295393.17143697*x82 + 1672890.7229183144*x92 + 15471651.937466398*x93 + 16.815729999999999*x95 - 21.928795999999998)*math::log(math::abs(10.0)))*xn[0 * XS];
    const Real x114 = T <= 500.0;
    const Real x115 = T > 100;
    const Real x116 = x114 && x115;
    const Real x117 = math::exp((-33025002.640084207*x112 + 44.525106942242758*x76 + 1331.8748828877385*x78 + 968783.44101153011*x81 + 4831859.3594864924*x82 - 10763.919849753534*x92 - 138531.11016116844*x93 + 1.6802758*x95 - 22.921188999999998)*math::log(math::abs(10.0)))*xn[0 * XS];
    const Real x118 = T > 500.0;
    const Real g143_4 = g143_3 + x91 * rhs_eint_choice_15(x113, x116, x117, x118);
    const Real x119 = g143_4;
    const Real x120 = x72 >= 1.0e-99;
    const Real x121 = x113*x91;
    const Real x122 = x104 + x94;
    const Real x123 = x102 + x96;
    const Real x124 = x122 + x123;
    const Real x125 = x121 + x124 >= 1.0e-99;
    const Real x126 = x123 + x94;
    const Real x127 = x106 + x126;
    const Real x128 = x121 + x127 >= 1.0e-99;
    const Real x129 = x108 + x126;
    const Real x130 = x121 + x129 >= 1.0e-99;
    const Real x131 = x111 + x126;
    const Real x132 = x121 + x131 >= 1.0e-99;
    const Real x133 = x101 + x103;
    const Real x134 = x122 + x133;
    const Real x135 = x121 + x134 >= 1.0e-99;
    const Real x136 = x133 + x94;
    const Real x137 = x106 + x136;
    const Real x138 = x121 + x137 >= 1.0e-99;
    const Real x139 = x108 + x136;
    const Real x140 = x121 + x139 >= 1.0e-99;
    const Real x141 = x111 + x136;
    const Real x142 = x121 + x141 >= 1.0e-99;
    const Real x143 = x117*x91;
    const Real x144 = x124 + x143 >= 1.0e-99;
    const Real x145 = x127 + x143 >= 1.0e-99;
    const Real x146 = x129 + x143 >= 1.0e-99;
    const Real x147 = x131 + x143 >= 1.0e-99;
    const Real x148 = x134 + x143 >= 1.0e-99;
    const Real x149 = x137 + x143 >= 1.0e-99;
    const Real x150 = x139 + x143 >= 1.0e-99;
    const Real x151 = x141 + x143 >= 1.0e-99;
    const Real x152 = x124 >= 1.0e-99;
    const Real x153 = x127 >= 1.0e-99;
    const Real x154 = x129 >= 1.0e-99;
    const Real x155 = x131 >= 1.0e-99;
    const Real x156 = x134 >= 1.0e-99;
    const Real x157 = x137 >= 1.0e-99;
    const Real x158 = x139 >= 1.0e-99;
    const Real x159 = x141 >= 1.0e-99;
    const Real x160 = x84 >= 1.0e-99;
    const Real x161 = 5.5313336794064847e-19/(0.0024787521766663585*math::exp(x86) + 1.0) >= 1.0e-99;
    const Real acc0_23 = acc0_22 - rhs_eint_choice_100(T, x105, x107, x109, x114, x115, x116, x118, x119, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x70, x71, x73, x85, x88, x89, x97, x98, xn);
    return x1*acc0_23;

}
DEVICE auto jac_nuc_choice_0(Real T, Real x23, Real x24, Real x25, bool x26) {
return ((x26) ? (
   -5.5279999999999998e-28*((T)*(T)*(T)*(T)*(T)) - 2.3088e-11*T + 7.3427999999999993e-15*x23 - 7.5474000000000004e-19*x24 + 3.3467999999999999e-23*x25 + 4.2277999999999996e-8
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_1(Real T, Real x28, bool x30, Real x31) {
return ((x30) ? (
   x28
)
: (
   1250086.112245841*(math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T))*x31 + x28
));
}
DEVICE auto jac_nuc_choice_2(Real x28, bool x49, Real x50, Real x52) {
return ((x49) ? (
   x28
)
: (
   3.2867337024382687e-10*x50*x52
));
}
DEVICE auto jac_nuc_choice_3(Real T, bool x61, Real x63) {
return ((x61) ? (
   1.4643482606109061e-16*math::exp((1.78186)*math::log(math::abs(T)))
)
: (
   x63
));
}
DEVICE auto jac_nuc_choice_4(Real T, Real x12, Real x35, Real x37, Real x38, Real x39, bool x49, Real x50, Real x52, Real x82, Real x84, Real x85, Real x87, Real x88) {
return ((x49) ? (
   x82
)
: (
   -2.3799651743169991e-10*(math::exp((-0.7241125657826851)*math::log(math::abs(T)))*(1.0/T))*x52 + 3.2867337024382687e-10*x50*x52*(-0.0071425856320495021*x12*x35 - 7.1075145702705346e-5*x12*x37 + 2.9934653521797078e-5*x12*x38 + 4.0289298963030308e-6*x12*x39 - 0.04052089463969382*x84 - 0.0012850420852755183*x85 - 1.4854136318202087e-7*x87 - 2.7640217188769353e-8*x88)
));
}
DEVICE auto jac_nuc_choice_5(Real T, Real x23, Real x24, Real x25, bool x26) {
return ((x26) ? (
   1.4685599999999999e-14*T - 2.2642200000000001e-18*x23 + 1.3387199999999999e-22*x24 - 2.7639999999999999e-27*x25 - 2.3088e-11
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_6(Real T, Real x12, Real x35, Real x37, Real x38, Real x39, Real x40, bool x61, Real x62, Real x63, Real x84, Real x85, Real x88) {
return ((x61) ? (
   2.6092635916521491e-16*(math::exp((1.78186)*math::log(math::abs(T)))*(1.0/T))
)
: (
   3.7804827525136553e-14*(math::exp((1.139449335841631)*math::log(math::abs(T)))*(1.0/T))*x62 + x63*(0.025393366159889998*x12*x35 + 0.0010061251423955*x12*x37 + 0.00051983779458540007*x12*x38 - 0.00018095067761848*x12*x39 + 1.9644009576313599e-5*x12*x40 - 0.28420270431082961*x84 - 0.0057310564851968003*x85 - 7.2615442150620009e-7*x88)
));
}
DEVICE auto jac_nuc_choice_7(Real T, Real x12, bool x30, Real x31, Real x82, Real x83) {
return ((x30) ? (
   x82
)
: (
   -1875129.1683687614*math::exp((-2.5)*math::log(math::abs(T)))*x31 + 587469852277.90271*x31*x83 + x82 + 54.282214350476039*x83*math::exp(-563932.20901156683*x12)
));
}
DEVICE auto jac_nuc_choice_8(Real T, bool x12, Real x13, Real x14) {
return ((x12) ? (
   1.4981088130721367e-10*(math::exp((0.36470000000000002)*math::log(math::abs(T)))*(1.0/T))
)
: (
   3.2867337024382687e-10*x13*x14
));
}
DEVICE auto jac_nuc_choice_9(Real T, Real x21, Real x22, bool x23) {
return ((x23) ? (
   1.26e-9*x21*x22
)
: (
   4.0000000000000003e-37*math::exp((4.7400000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_10(Real T, Real x25, bool x26) {
return ((x26) ? (
   2.0000000000000001e-10*math::exp((0.40200000000000002)*math::log(math::abs(T)))*x25 - 3.3099999999999998e-17*math::exp((1.48)*math::log(math::abs(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_11(Real T, Real x28, Real x29, bool x30) {
return ((x30) ? (
   -7.7700000000000002e-13*T + 2.5000000000000002e-10*x28 + 2.96e-6*x29 - 1.73e-9
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_12(Real T, bool x41, Real x46) {
return ((x41) ? (
   3.4977396723747635e-20*math::exp((-0.14999999999999999)*math::log(math::abs(T)))
)
: (
   x46
));
}
DEVICE auto jac_nuc_choice_13(Real x52, bool x53) {
return ((x53) ? (
   x52
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_14(Real T, bool x26, Real x56) {
return ((x26) ? (
   2.0299999999999998e-9*math::exp((-0.33200000000000002)*math::log(math::abs(T))) + 2.0600000000000001e-10*math::exp((0.39600000000000002)*math::log(math::abs(T)))*x56
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_15(Real T, bool x12, Real x13, Real x14, Real x18, Real x2, Real x4, Real x5, Real x6, Real x66, Real x67, Real x68, Real x8) {
return ((x12) ? (
   -9.5174852894472843e-11*math::exp((-1.6353)*math::log(math::abs(T)))
)
: (
   -2.3799651743169991e-10*(math::exp((-0.7241125657826851)*math::log(math::abs(T)))*(1.0/T))*x14 + 3.2867337024382687e-10*x13*x14*(-0.0071425856320495021*x18*x2 - 7.1075145702705346e-5*x18*x4 + 2.9934653521797078e-5*x18*x5 + 4.0289298963030308e-6*x18*x6 - 2.7640217188769353e-8*x18*x8 - 0.04052089463969382*x66 - 0.0012850420852755183*x67 - 1.4854136318202087e-7*x68)
));
}
DEVICE auto jac_nuc_choice_16(Real T, Real x22, bool x23) {
return ((x23) ? (
   0.00016065*math::exp((-2.75)*math::log(math::abs(T)))*x22 - 9.4499999999999994e-10*math::exp((-1.75)*math::log(math::abs(T)))*x22
)
: (
   1.8960000000000001e-36*(math::exp((4.7400000000000002)*math::log(math::abs(T)))*(1.0/T))
));
}
DEVICE auto jac_nuc_choice_17(Real T, Real x18, Real x32, Real x33, bool x41, Real x42, Real x44, Real x46, Real x64, Real x65) {
return ((x41) ? (
   -5.2466095085621454e-21*(math::exp((-0.14999999999999999)*math::log(math::abs(T)))*(1.0/T))
)
: (
   x33*x46*(3.5720000000000001*x18*x32*x44 - 0.62159999999999993*x42*x65 - 3.194*x64)
));
}
DEVICE auto jac_nuc_choice_18(Real T, Real x29, bool x30) {
return ((x30) ? (
   1.2500000000000001e-10*x29 - 7.7700000000000002e-13 - 1.48e-6/math::exp((3.0/2.0)*math::log(math::abs(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_19(Real T, Real x25, bool x26) {
return ((x26) ? (
   7.4200000000000004e-9*math::exp((-1.5979999999999999)*math::log(math::abs(T)))*x25 + 8.0400000000000002e-11*(math::exp((0.40200000000000002)*math::log(math::abs(T)))*(1.0/T))*x25 - 4.8987999999999998e-17*(math::exp((1.48)*math::log(math::abs(T)))*(1.0/T))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_20(Real x18, Real x43, Real x48, Real x49, Real x50, Real x51, Real x52, bool x53, Real x62, Real x63, Real x65) {
return ((x53) ? (
   x48*(-1.9125491199999999e-8*x18*x43 + 1.9865770999999999e-9*x18*x49 - 1.09028466e-10*x18*x50 + 2.4718352399999997e-12*x18*x51 + 3.3735381999999997e-7*x18 - 2.8982736e-7*x63 + 1.0251841499999999e-7*x65) + 21237.150000000001*x52*x62
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_21(Real T, bool x26, Real x56) {
return ((x26) ? (
   6.7980000000000007e-9*math::exp((-1.6040000000000001)*math::log(math::abs(T)))*x56 - 6.7396000000000002e-10*(math::exp((-0.33200000000000002)*math::log(math::abs(T)))*(1.0/T)) + 8.1576000000000009e-11*(math::exp((0.39600000000000002)*math::log(math::abs(T)))*(1.0/T))*x56
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_22(Real T, Real x14, Real x15, Real x16, bool x17) {
return ((x17) ? (
   -5.5279999999999998e-28*((T)*(T)*(T)*(T)*(T)) - 2.3088e-11*T + 7.3427999999999993e-15*x14 - 7.5474000000000004e-19*x15 + 3.3467999999999999e-23*x16 + 4.2277999999999996e-8
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_23(Real T, bool x36, Real x37, Real x39) {
return ((x36) ? (
   1.4981088130721367e-10*(math::exp((0.36470000000000002)*math::log(math::abs(T)))*(1.0/T))
)
: (
   3.2867337024382687e-10*x37*x39
));
}
DEVICE auto jac_nuc_choice_24(Real T, bool x17, Real x41, Real x42) {
return ((x17) ? (
   1.26e-9*x41*x42
)
: (
   4.0000000000000003e-37*math::exp((4.7400000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_25(Real T, Real x44, bool x45) {
return ((x45) ? (
   2.0000000000000001e-10*math::exp((0.40200000000000002)*math::log(math::abs(T)))*x44 - 3.3099999999999998e-17*math::exp((1.48)*math::log(math::abs(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_26(Real x54, bool x55) {
return ((x55) ? (
   x54
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_27(Real T, bool x57, Real x64) {
return ((x57) ? (
   3.4977396723747635e-20*math::exp((-0.14999999999999999)*math::log(math::abs(T)))
)
: (
   x64
));
}
DEVICE auto jac_nuc_choice_28(Real T, Real x0, Real x1, bool x66) {
return ((x66) ? (
   -7.7700000000000002e-13*T + 2.5000000000000002e-10*x0 + 2.96e-6*x1 - 1.73e-9
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_29(Real x170, bool x171) {
return ((x171) ? (
   x170
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_30(Real T, Real x173, bool x45) {
return ((x45) ? (
   2.0299999999999998e-9*math::exp((-0.33200000000000002)*math::log(math::abs(T))) + 2.0600000000000001e-10*math::exp((0.39600000000000002)*math::log(math::abs(T)))*x173
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_31(Real T, bool x184, Real x186) {
return ((x184) ? (
   1.4643482606109061e-16*math::exp((1.78186)*math::log(math::abs(T)))
)
: (
   x186
));
}
DEVICE auto jac_nuc_choice_32(Real x195, bool x196, Real x197) {
return ((x196) ? (
   x195
)
: (
   3.1699999999999999e-10*x197
));
}
DEVICE auto jac_nuc_choice_33(Real T, Real x21, Real x222, Real x223, Real x225, Real x227, Real x23, Real x24, Real x25, bool x36, Real x37, Real x39, Real x7) {
return ((x36) ? (
   -9.5174852894472843e-11*math::exp((-1.6353)*math::log(math::abs(T)))
)
: (
   -2.3799651743169991e-10*(math::exp((-0.7241125657826851)*math::log(math::abs(T)))*(1.0/T))*x39 + 3.2867337024382687e-10*x37*x39*(-0.0071425856320495021*x21*x7 - 0.04052089463969382*x222 - 0.0012850420852755183*x223 - 1.4854136318202087e-7*x225 - 2.7640217188769353e-8*x227 - 7.1075145702705346e-5*x23*x7 + 2.9934653521797078e-5*x24*x7 + 4.0289298963030308e-6*x25*x7)
));
}
DEVICE auto jac_nuc_choice_34(Real T, Real x14, Real x15, Real x16, bool x17) {
return ((x17) ? (
   1.4685599999999999e-14*T - 2.2642200000000001e-18*x14 + 1.3387199999999999e-22*x15 - 2.7639999999999999e-27*x16 - 2.3088e-11
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_35(Real T, bool x17, Real x42) {
return ((x17) ? (
   0.00016065*math::exp((-2.75)*math::log(math::abs(T)))*x42 - 9.4499999999999994e-10*math::exp((-1.75)*math::log(math::abs(T)))*x42
)
: (
   1.8960000000000001e-36*(math::exp((4.7400000000000002)*math::log(math::abs(T)))*(1.0/T))
));
}
DEVICE auto jac_nuc_choice_36(Real T, Real x216, Real x218, Real x48, bool x57, Real x58, Real x63, Real x64, Real x7) {
return ((x57) ? (
   -5.2466095085621454e-21*(math::exp((-0.14999999999999999)*math::log(math::abs(T)))*(1.0/T))
)
: (
   x58*x64*(-3.194*x216 - 0.62159999999999993*x218 + 3.5720000000000001*x48*x63*x7)
));
}
DEVICE auto jac_nuc_choice_37(Real x1, Real x209, bool x66) {
return ((x66) ? (
   1.2500000000000001e-10*x1 - 1.48e-6*x209 - 7.7700000000000002e-13
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_38(Real T, Real x44, bool x45) {
return ((x45) ? (
   7.4200000000000004e-9*math::exp((-1.5979999999999999)*math::log(math::abs(T)))*x44 + 8.0400000000000002e-11*(math::exp((0.40200000000000002)*math::log(math::abs(T)))*(1.0/T))*x44 - 4.8987999999999998e-17*(math::exp((1.48)*math::log(math::abs(T)))*(1.0/T))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_39(Real x169, Real x217, Real x219, Real x220, Real x221, Real x47, Real x52, Real x53, Real x54, bool x55, Real x7) {
return ((x55) ? (
   21237.150000000001*x169*x54 + x47*(1.0251841499999999e-7*x217 - 2.8982736e-7*x219 - 1.9125491199999999e-8*x220 + 1.9865770999999999e-9*x221 - 1.09028466e-10*x52*x7 + 2.4718352399999997e-12*x53*x7 + 3.3735381999999997e-7*x7)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_40(Real x15, Real x169, Real x170, bool x171) {
return ((x171) ? (
   x170*(4430.0*x169 - 347800.0/x15)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_41(Real T, Real x173, bool x45) {
return ((x45) ? (
   6.7980000000000007e-9*math::exp((-1.6040000000000001)*math::log(math::abs(T)))*x173 - 6.7396000000000002e-10*(math::exp((-0.33200000000000002)*math::log(math::abs(T)))*(1.0/T)) + 8.1576000000000009e-11*(math::exp((0.39600000000000002)*math::log(math::abs(T)))*(1.0/T))*x173
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_42(Real x169, Real x193, Real x194, Real x195, bool x196, Real x197, Real x216, Real x218, Real x220, Real x221, Real x226, Real x58) {
return ((x196) ? (
   x195*x58*(1.5894349999999999*x193*x221 - 8.6761199999999992*x194*x220 + 5.8888600000000002*x216 + 6.7520699999999998*x218 + 14.393840000000001*x226)
)
: (
   1.650619e-6*x169*x197
));
}
DEVICE auto jac_nuc_choice_43(Real T, bool x184, Real x185, Real x186, Real x21, Real x222, Real x223, Real x227, Real x23, Real x24, Real x25, Real x26, Real x7) {
return ((x184) ? (
   2.6092635916521491e-16*(math::exp((1.78186)*math::log(math::abs(T)))*(1.0/T))
)
: (
   3.7804827525136553e-14*(math::exp((1.139449335841631)*math::log(math::abs(T)))*(1.0/T))*x185 + x186*(0.025393366159889998*x21*x7 - 0.28420270431082961*x222 - 0.0057310564851968003*x223 - 7.2615442150620009e-7*x227 + 0.0010061251423955*x23*x7 + 0.00051983779458540007*x24*x7 - 0.00018095067761848*x25*x7 + 1.9644009576313599e-5*x26*x7)
));
}
DEVICE auto jac_nuc_choice_44(Real T, Real x21, Real x22, bool x23) {
return ((x23) ? (
   -7.7700000000000002e-13*T + 2.5000000000000002e-10*x21 + 2.96e-6*x22 - 1.73e-9
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_45(Real T, bool x30, Real x32) {
return ((x30) ? (
   1.4643482606109061e-16*math::exp((1.78186)*math::log(math::abs(T)))
)
: (
   x32
));
}
DEVICE auto jac_nuc_choice_46(Real x22, bool x23, Real x40) {
return ((x23) ? (
   1.2500000000000001e-10*x22 - 1.48e-6*x40 - 7.7700000000000002e-13
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_47(Real T, Real x11, Real x12, Real x13, Real x14, Real x15, bool x30, Real x31, Real x32, Real x4, Real x42, Real x43, Real x9) {
return ((x30) ? (
   2.6092635916521491e-16*(math::exp((1.78186)*math::log(math::abs(T)))*(1.0/T))
)
: (
   3.7804827525136553e-14*(math::exp((1.139449335841631)*math::log(math::abs(T)))*(1.0/T))*x31 + x32*(0.0010061251423955*x11*x4 + 0.00051983779458540007*x12*x4 - 0.00018095067761848*x13*x4 + 1.9644009576313599e-5*x14*x4 - 7.2615442150620009e-7*x15*x4 + 0.025393366159889998*x4*x9 - 0.28420270431082961*x42 - 0.0057310564851968003*x43)
));
}
DEVICE auto jac_nuc_choice_48(Real T, Real x4, bool x5) {
return ((x5) ? (
   2.0000000000000001e-10*math::exp((0.40200000000000002)*math::log(math::abs(T)))*x4 - 3.3099999999999998e-17*math::exp((1.48)*math::log(math::abs(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_49(Real T, bool x5, Real x7) {
return ((x5) ? (
   2.0299999999999998e-9*math::exp((-0.33200000000000002)*math::log(math::abs(T))) + 2.0600000000000001e-10*math::exp((0.39600000000000002)*math::log(math::abs(T)))*x7
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_50(Real T, Real x4, bool x5) {
return ((x5) ? (
   7.4200000000000004e-9*math::exp((-1.5979999999999999)*math::log(math::abs(T)))*x4 + 8.0400000000000002e-11*(math::exp((0.40200000000000002)*math::log(math::abs(T)))*(1.0/T))*x4 - 4.8987999999999998e-17*(math::exp((1.48)*math::log(math::abs(T)))*(1.0/T))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_51(Real T, bool x5, Real x7) {
return ((x5) ? (
   6.7980000000000007e-9*math::exp((-1.6040000000000001)*math::log(math::abs(T)))*x7 - 6.7396000000000002e-10*(math::exp((-0.33200000000000002)*math::log(math::abs(T)))*(1.0/T)) + 8.1576000000000009e-11*(math::exp((0.39600000000000002)*math::log(math::abs(T)))*(1.0/T))*x7
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_52(Real T, Real x8, bool x9) {
return ((x9) ? (
   2.0000000000000001e-10*math::exp((0.40200000000000002)*math::log(math::abs(T)))*x8 - 3.3099999999999998e-17*math::exp((1.48)*math::log(math::abs(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_53(Real x22, bool x23) {
return ((x23) ? (
   x22
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_54(Real T, Real x25, bool x9) {
return ((x9) ? (
   2.0299999999999998e-9*math::exp((-0.33200000000000002)*math::log(math::abs(T))) + 2.0600000000000001e-10*math::exp((0.39600000000000002)*math::log(math::abs(T)))*x25
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_55(Real x35, bool x36, Real x37) {
return ((x36) ? (
   x35
)
: (
   3.1699999999999999e-10*x37
));
}
DEVICE auto jac_nuc_choice_56(Real T, Real x8, bool x9) {
return ((x9) ? (
   7.4200000000000004e-9*math::exp((-1.5979999999999999)*math::log(math::abs(T)))*x8 + 8.0400000000000002e-11*(math::exp((0.40200000000000002)*math::log(math::abs(T)))*(1.0/T))*x8 - 4.8987999999999998e-17*(math::exp((1.48)*math::log(math::abs(T)))*(1.0/T))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_57(Real T, Real x21, Real x22, bool x23) {
return ((x23) ? (
   x22*(4430.0*x21 - 347800.0/((T)*(T)*(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_58(Real T, Real x25, bool x9) {
return ((x9) ? (
   6.7980000000000007e-9*math::exp((-1.6040000000000001)*math::log(math::abs(T)))*x25 - 6.7396000000000002e-10*(math::exp((-0.33200000000000002)*math::log(math::abs(T)))*(1.0/T)) + 8.1576000000000009e-11*(math::exp((0.39600000000000002)*math::log(math::abs(T)))*(1.0/T))*x25
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_59(Real x11, Real x13, Real x21, Real x28, Real x29, Real x30, Real x31, Real x32, Real x33, Real x34, Real x35, bool x36, Real x37, Real x41, Real x7) {
return ((x36) ? (
   x11*x35*(14.393840000000001*x13*x33*x7 + 1.5894349999999999*x28*x30*x7 - 8.6761199999999992*x29*x32*x7 + 6.7520699999999998*x31*x34*x7 + 5.8888600000000002*x41)
)
: (
   1.650619e-6*x21*x37
));
}
DEVICE auto jac_nuc_choice_60(Real T, Real x0, Real x1, Real x2, bool x3) {
return ((x3) ? (
   -5.5279999999999998e-28*((T)*(T)*(T)*(T)*(T)) - 2.3088e-11*T + 7.3427999999999993e-15*x0 - 7.5474000000000004e-19*x1 + 3.3467999999999999e-23*x2 + 4.2277999999999996e-8
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_61(Real T, Real x14, bool x6) {
return ((x6) ? (
   3.4977396723747635e-20*math::exp((-0.14999999999999999)*math::log(math::abs(T)))
)
: (
   x14
));
}
DEVICE auto jac_nuc_choice_62(Real x21, bool x22) {
return ((x22) ? (
   x21
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_63(Real T, Real x0, Real x1, Real x2, bool x3) {
return ((x3) ? (
   1.4685599999999999e-14*T - 2.2642200000000001e-18*x0 + 1.3387199999999999e-22*x1 - 2.7639999999999999e-27*x2 - 2.3088e-11
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_64(Real T, Real x10, Real x12, Real x14, Real x16, Real x25, bool x6, Real x7, Real x8, Real x9) {
return ((x6) ? (
   -5.2466095085621454e-21*(math::exp((-0.14999999999999999)*math::log(math::abs(T)))*(1.0/T))
)
: (
   x14*x8*(-0.62159999999999993*x10*x25 + 3.5720000000000001*x12*x16*x7 - x16*x9)
));
}
DEVICE auto jac_nuc_choice_65(Real x0, Real x11, Real x16, Real x17, Real x18, Real x19, Real x20, Real x21, bool x22, Real x25, Real x7) {
return ((x22) ? (
   x17*(-1.9125491199999999e-8*x11*x16 + 1.9865770999999999e-9*x16*x18 - 1.09028466e-10*x16*x19 + 2.4718352399999997e-12*x16*x20 - 2.8982736e-7*x16*x7 + 3.3735381999999997e-7*x16 + 1.0251841499999999e-7*x25) + 21237.150000000001*x21/x0
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_66(Real x14, bool x15) {
return ((x15) ? (
   x14
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_67(Real x116, bool x117) {
return ((x117) ? (
   x116
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_68(Real x133, bool x134, Real x135) {
return ((x134) ? (
   x133
)
: (
   3.1699999999999999e-10*x135
));
}
DEVICE auto jac_nuc_choice_69(Real x0, Real x115, Real x12, Real x13, Real x14, Real x149, bool x15, Real x152, Real x153, Real x154, Real x7) {
return ((x15) ? (
   21237.150000000001*x115*x14 + x7*(-1.09028466e-10*x0*x12 + 2.4718352399999997e-12*x0*x13 + 3.3735381999999997e-7*x0 - 2.8982736e-7*x149 + 1.0251841499999999e-7*x152 - 1.9125491199999999e-8*x153 + 1.9865770999999999e-9*x154)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_70(Real T, Real x115, Real x116, bool x117) {
return ((x117) ? (
   x116*(4430.0*x115 - 347800.0/((T)*(T)*(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_71(Real x115, Real x131, Real x132, Real x133, bool x134, Real x135, Real x148, Real x150, Real x153, Real x154, Real x155, Real x23) {
return ((x134) ? (
   x133*x23*(1.5894349999999999*x131*x154 - 8.6761199999999992*x132*x153 + 5.8888600000000002*x148 + 14.393840000000001*x150 + 6.7520699999999998*x155)
)
: (
   1.650619e-6*x115*x135
));
}
DEVICE auto jac_nuc_choice_72(Real x5, bool x6) {
return ((x6) ? (
   x5
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_73(Real x21, bool x22, Real x23) {
return ((x22) ? (
   x21
)
: (
   3.1699999999999999e-10*x23
));
}
DEVICE auto jac_nuc_choice_74(Real T, Real x4, Real x5, bool x6) {
return ((x6) ? (
   x5*(4430.0*x4 - 347800.0/((T)*(T)*(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_75(Real x0, Real x13, Real x16, Real x17, Real x18, Real x19, Real x20, Real x21, bool x22, Real x23, Real x26, Real x4, Real x8, Real x9) {
return ((x22) ? (
   x21*x8*(6.7520699999999998*x0*x13*x19 + 1.5894349999999999*x0*x16*x18 - 8.6761199999999992*x0*x17*x20 + 5.8888600000000002*x0*x9 + 14.393840000000001*x26)
)
: (
   1.650619e-6*x23*x4
));
}
DEVICE auto jac_nuc_choice_76(Real T, bool x10, Real x12, Real x8) {
return ((x10) ? (
   x8
)
: (
   1250086.112245841*(math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T))*x12 + x8
));
}
DEVICE auto jac_nuc_choice_77(Real T, Real x27, Real x28, bool x29) {
return ((x29) ? (
   1.26e-9*x27*x28
)
: (
   4.0000000000000003e-37*math::exp((4.7400000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_78(Real T, Real x28, bool x29) {
return ((x29) ? (
   0.00016065*math::exp((-2.75)*math::log(math::abs(T)))*x28 - 9.4499999999999994e-10*math::exp((-1.75)*math::log(math::abs(T)))*x28
)
: (
   1.8960000000000001e-36*(math::exp((4.7400000000000002)*math::log(math::abs(T)))*(1.0/T))
));
}
DEVICE auto jac_nuc_choice_79(Real T, bool x10, Real x11, Real x12, Real x34, Real x35) {
return ((x10) ? (
   x34
)
: (
   -1875129.1683687614*math::exp((-2.5)*math::log(math::abs(T)))*x12 + 587469852277.90271*x12*x35 + x34 + 54.282214350476039*x35*math::exp(-563932.20901156683*x11)
));
}
DEVICE auto jac_nuc_choice_80(Real T, Real x0, bool x2, Real x4) {
return ((x2) ? (
   x0
)
: (
   1250086.112245841*(math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T))*x4 + x0
));
}
DEVICE auto jac_nuc_choice_81(Real T, Real x17, Real x18, bool x19) {
return ((x19) ? (
   1.26e-9*x17*x18
)
: (
   4.0000000000000003e-37*math::exp((4.7400000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_82(Real T, bool x2, Real x22, Real x23, Real x3, Real x4) {
return ((x2) ? (
   x22
)
: (
   -1875129.1683687614*math::exp((-2.5)*math::log(math::abs(T)))*x4 + x22 + 587469852277.90271*x23*x4 + 54.282214350476039*x23*math::exp(-563932.20901156683*x3)
));
}
DEVICE auto jac_nuc_choice_83(Real T, Real x18, bool x19) {
return ((x19) ? (
   0.00016065*math::exp((-2.75)*math::log(math::abs(T)))*x18 - 9.4499999999999994e-10*math::exp((-1.75)*math::log(math::abs(T)))*x18
)
: (
   1.8960000000000001e-36*(math::exp((4.7400000000000002)*math::log(math::abs(T)))*(1.0/T))
));
}
DEVICE auto jac_nuc_choice_84(Real T, bool x16) {
    const auto ca = 2.3157944032250755;
    const auto cb = (
   math::exp((0.36470000000000002)*math::log(math::abs(T)))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_85(bool x16, Real x19) {
return ((x16) ? (
   math::sqrt(10)
)
: (
   x19
));
}
DEVICE auto jac_nuc_choice_86(bool x16, Real x8) {
return ((x16) ? (
   1.0/10.0
)
: (
   x8
));
}
DEVICE auto jac_nuc_choice_87(Real T, bool x16) {
    const auto ca = 5.011872336272722;
    const auto cb = (
   math::exp((0.69999999999999996)*math::log(math::abs(T)))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_88(Real T, bool x16) {
    const auto ca = 0.63095734448019325;
    const auto cb = (
   math::exp((-0.20000000000000001)*math::log(math::abs(T)))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_89(bool x16, Real x38) {
return ((x16) ? (
   0.031622776601683791
)
: (
   x38
));
}
DEVICE auto jac_nuc_choice_90(Real T, bool x16) {
    const auto ca = 0.4008667176273028;
    const auto cb = (
   math::exp((-0.39700000000000002)*math::log(math::abs(T)))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_91(Real T, bool x16) {
    const auto ca = 0.67810976749343443;
    const auto cb = (
   math::exp((-0.16869999999999999)*math::log(math::abs(T)))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_92(Real T, bool x167) {
return ((x167) ? (
   10000.0
)
: (
   T
));
}
DEVICE auto jac_nuc_choice_93(bool x173, const Real* xn) {
return ((x173) ? (
   false
)
: (
   xn[2 * XS] >= 10000000000.0
));
}
DEVICE auto jac_nuc_choice_94(bool x173, const Real* xn) {
return ((x173) ? (
      0.01
   )
   : (
      xn[2 * XS]
   ));
}
DEVICE auto jac_nuc_choice_95(bool x173, bool x174, const Real* xn) {
    const auto ca = 10000000000.0;
    const auto cb = (
   jac_nuc_choice_94(x173, xn)
);
    return (x174) ? ca : cb;
}
DEVICE auto jac_nuc_choice_96(bool x205, Real x212, Real x215) {
return ((x205) ? (
                                 2.1648287161311648e-38/((x212*x215 + 1.471335691176954e-59)*(x212*x215 + 1.471335691176954e-59)) >= 1.0
                              )
                              : (
                                 true
                              ));
}
DEVICE auto jac_nuc_choice_97(bool x205, bool x206, Real x210, Real x212, Real x215) {
    const auto ca = 2.1648287161311648e-38/((x210*x215 + 1.471335691176954e-59)*(x210*x215 + 1.471335691176954e-59)) >= 1.0;
    const auto cb = (
                              jac_nuc_choice_96(x205, x212, x215)
                           );
    return (x205 & x206) ? ca : cb;
}
DEVICE auto jac_nuc_choice_98(bool x205, bool x206, bool x207, Real x210, Real x212, Real x215) {
    const auto ca = true;
    const auto cb = (
                           jac_nuc_choice_97(x205, x206, x210, x212, x215)
                        );
    return (x207) ? ca : cb;
}
DEVICE auto jac_nuc_choice_99(bool x205, bool x206, bool x207, Real x210, Real x212, Real x214, Real x215) {
    const auto ca = 4.833962023629485e-72/((x212*x214 + 2.1986273043946045e-76)*(x212*x214 + 2.1986273043946045e-76)) >= 1.0;
    const auto cb = (
                        jac_nuc_choice_98(x205, x206, x207, x210, x212, x215)
                     );
    return (x205 & x207) ? ca : cb;
}
DEVICE auto jac_nuc_choice_100(bool x205, bool x206, bool x207, Real x210, Real x212, Real x214, Real x215) {
    const auto ca = 4.833962023629485e-72/((x210*x214 + 2.1986273043946045e-76)*(x210*x214 + 2.1986273043946045e-76)) >= 1.0;
    const auto cb = (
                     jac_nuc_choice_99(x205, x206, x207, x210, x212, x214, x215)
                  );
    return (x205 & x206 & x207) ? ca : cb;
}
DEVICE auto jac_nuc_choice_101(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x214, Real x215) {
    const auto ca = true;
    const auto cb = (
                  jac_nuc_choice_100(x205, x206, x207, x210, x212, x214, x215)
               );
    return (x204) ? ca : cb;
}
DEVICE auto jac_nuc_choice_102(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
    const auto ca = 216.48287161311649/((x212*x213 + 1.471335691176954e-39)*(x212*x213 + 1.471335691176954e-39)) >= 1.0;
    const auto cb = (
               jac_nuc_choice_101(x204, x205, x206, x207, x210, x212, x214, x215)
            );
    return (x204 & x205) ? ca : cb;
}
DEVICE auto jac_nuc_choice_103(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
    const auto ca = 216.48287161311649/((x210*x213 + 1.471335691176954e-39)*(x210*x213 + 1.471335691176954e-39)) >= 1.0;
    const auto cb = (
            jac_nuc_choice_102(x204, x205, x206, x207, x210, x212, x213, x214, x215)
         );
    return (x204 & x205 & x206) ? ca : cb;
}
DEVICE auto jac_nuc_choice_104(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
    const auto ca = true;
    const auto cb = (
         jac_nuc_choice_103(x204, x205, x206, x207, x210, x212, x213, x214, x215)
      );
    return (x204 & x207) ? ca : cb;
}
DEVICE auto jac_nuc_choice_105(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
    const auto ca = 4.8339620236294848e-32/((x212 + 2.1986273043946046e-56)*(x212 + 2.1986273043946046e-56)) >= 1.0;
    const auto cb = (
      jac_nuc_choice_104(x204, x205, x206, x207, x210, x212, x213, x214, x215)
   );
    return (x204 & x205 & x207) ? ca : cb;
}
DEVICE auto jac_nuc_choice_106(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
    const auto ca = 4.8339620236294848e-32/((x210 + 2.1986273043946046e-56)*(x210 + 2.1986273043946046e-56)) >= 1.0;
    const auto cb = (
   jac_nuc_choice_105(x204, x205, x206, x207, x210, x212, x213, x214, x215)
);
    return (x204 & x205 & x206 & x207) ? ca : cb;
}
DEVICE auto jac_nuc_choice_107(bool x206, Real x94) {
return ((x206) ? (
   0.5
)
: (
   x94
));
}
DEVICE auto jac_nuc_choice_108(bool x205, Real x217) {
return ((x205) ? (
   x217
)
: (
   0.0
));
}
DEVICE auto jac_nuc_choice_109(Real x203, bool x204) {
return ((x204) ? (
   9.9999999999999993e-41
)
: (
   x203
));
}
DEVICE auto jac_nuc_choice_110(Real x2, bool x207) {
return ((x207) ? (
   1.0e+80
)
: (
   2.232953576238777e+46*x2
));
}
DEVICE auto jac_nuc_choice_111(bool x216, Real x224) {
return ((x216) ? (
   1.0
)
: (
   483396202.36294854/((x224)*(x224))
));
}
DEVICE auto jac_nuc_choice_112(bool x231, Real x232) {
return ((x231) ? (
   true
)
: (
   x232 >= 1.0
));
}
DEVICE auto jac_nuc_choice_113(bool x231, Real x232) {
return ((x231) ? (
      1.000000000000001e+18
   )
   : (
      x232
   ));
}
DEVICE auto jac_nuc_choice_114(bool x231, Real x232, bool x233) {
return ((x233) ? (
   1.0
)
: (
   jac_nuc_choice_113(x231, x232)
));
}
DEVICE auto jac_nuc_choice_115(Real x296, bool x297) {
return ((x297) ? (
   x296
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_116(Real x291, bool x294, Real x296, bool x297) {
return ((x294) ? (
   x291
)
: jac_nuc_choice_115(x296, x297));
}
DEVICE auto jac_nuc_choice_117(Real x254, bool x257, Real x264) {
return ((x257) ? (
   x254
)
: (
   x264
));
}
DEVICE auto jac_nuc_choice_118(bool x257, Real x266, Real x268) {
return ((x257) ? (
   x266
)
: (
   x268
));
}
DEVICE auto jac_nuc_choice_119(Real x276, bool x277, Real x283) {
return ((x277) ? (
   x276
)
: (
   x283
));
}
DEVICE auto jac_nuc_choice_120(Real x273, bool x274, Real x276, bool x277, Real x283) {
return ((x274) ? (
   x273
)
: jac_nuc_choice_119(x276, x277, x283));
}
DEVICE auto jac_nuc_choice_121(Real x270, bool x271, Real x273, bool x274, Real x276, bool x277, Real x283) {
    const auto ca = x270;
    const auto cb = jac_nuc_choice_120(x273, x274, x276, x277, x283);
    return (x271) ? ca : cb;
}
DEVICE auto jac_nuc_choice_122(Real x312, bool x313) {
return ((x313) ? (
   300.0
)
: (
   x312
));
}
DEVICE auto jac_nuc_choice_123(bool x255, Real x310, Real x315) {
return ((x255) ? (
   x310
)
: (
   5.5313336794064847e-19/x315
));
}
DEVICE auto jac_nuc_choice_124(bool x255, Real x308, bool x309, Real x310, Real x315) {
return ((x309) ? (
   x308
)
: jac_nuc_choice_123(x255, x310, x315));
}
DEVICE auto jac_nuc_choice_125(bool x277, bool x297, bool x350, bool x351, bool x361) {
return ((x277 & x297) ? (
                                                                                                                                                                        x350 & x361
                                                                                                                                                                     )
                                                                                                                                                                     : (
                                                                                                                                                                        x351 & x361
                                                                                                                                                                     ));
}
DEVICE auto jac_nuc_choice_126(bool x274, bool x277, bool x297, bool x349, bool x350, bool x351, bool x361) {
return ((x274 & x297) ? (
                                                                                                                                                                     x349 & x361
                                                                                                                                                                  )
                                                                                                                                                                  : (
                                                                                                                                                                     jac_nuc_choice_125(x277, x297, x350, x351, x361)
                                                                                                                                                                  ));
}
DEVICE auto jac_nuc_choice_127(bool x271, bool x274, bool x277, bool x297, bool x348, bool x349, bool x350, bool x351, bool x361) {
return ((x271 & x297) ? (
                                                                                                                                                                  x348 & x361
                                                                                                                                                               )
                                                                                                                                                               : (
                                                                                                                                                                  jac_nuc_choice_126(x274, x277, x297, x349, x350, x351, x361)
                                                                                                                                                               ));
}
DEVICE auto jac_nuc_choice_128(bool x271, bool x274, bool x277, bool x294, bool x297, bool x342, bool x348, bool x349, bool x350, bool x351, bool x361) {
return ((x294) ? (
                                                                                                                                                               x342 & x361
                                                                                                                                                            )
                                                                                                                                                            : (
                                                                                                                                                               jac_nuc_choice_127(x271, x274, x277, x297, x348, x349, x350, x351, x361)
                                                                                                                                                            ));
}
DEVICE auto jac_nuc_choice_129(bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x361) {
return ((x277 & x292 & x293) ? (
                                                                                                                                                            x340 & x361
                                                                                                                                                         )
                                                                                                                                                         : (
                                                                                                                                                            jac_nuc_choice_128(x271, x274, x277, x294, x297, x342, x348, x349, x350, x351, x361)
                                                                                                                                                         ));
}
DEVICE auto jac_nuc_choice_130(bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x361) {
return ((x274 & x292 & x293) ? (
                                                                                                                                                         x338 & x361
                                                                                                                                                      )
                                                                                                                                                      : (
                                                                                                                                                         jac_nuc_choice_129(x271, x274, x277, x292, x293, x294, x297, x340, x342, x348, x349, x350, x351, x361)
                                                                                                                                                      ));
}
DEVICE auto jac_nuc_choice_131(bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x361) {
return ((x271 & x292 & x293) ? (
                                                                                                                                                      x335 & x361
                                                                                                                                                   )
                                                                                                                                                   : (
                                                                                                                                                      jac_nuc_choice_130(x271, x274, x277, x292, x293, x294, x297, x338, x340, x342, x348, x349, x350, x351, x361)
                                                                                                                                                   ));
}
DEVICE auto jac_nuc_choice_132(bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x361) {
return ((x313 & (x271 | x313) & (x274 | x313) & (x277 | x313) & (x292 | x313) & (x293 | x313) & (x297 | x313) & (x271 | x274 | x313) & (x271 | x277 | x313) & (x271 | x292 | x313) & (x271 | x293 | x313) & (x271 | x297 | x313) & (x274 | x277 | x313) & (x274 | x292 | x313) & (x274 | x293 | x313) & (x274 | x297 | x313) & (x277 | x292 | x313) & (x277 | x293 | x313) & (x277 | x297 | x313) & (x292 | x293 | x313) & (x293 | x297 | x313) & (x271 | x274 | x277 | x313) & (x271 | x274 | x292 | x313) & (x271 | x274 | x293 | x313) & (x271 | x274 | x297 | x313) & (x271 | x277 | x292 | x313) & (x271 | x277 | x293 | x313) & (x271 | x277 | x297 | x313) & (x271 | x292 | x293 | x313) & (x271 | x293 | x297 | x313) & (x274 | x277 | x292 | x313) & (x274 | x277 | x293 | x313) & (x274 | x277 | x297 | x313) & (x274 | x292 | x293 | x313) & (x274 | x293 | x297 | x313) & (x277 | x292 | x293 | x313) & (x277 | x293 | x297 | x313) & (x271 | x274 | x277 | x292 | x313) & (x271 | x274 | x277 | x293 | x313) & (x271 | x274 | x277 | x297 | x313) & (x271 | x274 | x292 | x293 | x313) & (x271 | x274 | x293 | x297 | x313) & (x271 | x277 | x292 | x293 | x313) & (x271 | x277 | x293 | x297 | x313) & (x274 | x277 | x292 | x293 | x313) & (x274 | x277 | x293 | x297 | x313) & (x271 | x274 | x277 | x292 | x293 | x313) & (x271 | x274 | x277 | x293 | x297 | x313)) ? (
                                                                                                                                                   false
                                                                                                                                                )
                                                                                                                                                : (
                                                                                                                                                   jac_nuc_choice_131(x271, x274, x277, x292, x293, x294, x297, x335, x338, x340, x342, x348, x349, x350, x351, x361)
                                                                                                                                                ));
}
DEVICE auto jac_nuc_choice_133(bool x255, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x359, bool x360, bool x361) {
return ((x255) ? (
                                                                                                                                                x359 & x360
                                                                                                                                             )
                                                                                                                                             : (
                                                                                                                                                jac_nuc_choice_132(x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x361)
                                                                                                                                             ));
}
DEVICE auto jac_nuc_choice_134(bool x255, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x277) ? (
                                                                                                                                             x358 & x360
                                                                                                                                          )
                                                                                                                                          : (
                                                                                                                                             jac_nuc_choice_133(x255, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x359, x360, x361)
                                                                                                                                          ));
}
DEVICE auto jac_nuc_choice_135(bool x255, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x274) ? (
                                                                                                                                          x357 & x360
                                                                                                                                       )
                                                                                                                                       : (
                                                                                                                                          jac_nuc_choice_134(x255, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x358, x359, x360, x361)
                                                                                                                                       ));
}
DEVICE auto jac_nuc_choice_136(bool x255, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x271) ? (
                                                                                                                                       x356 & x360
                                                                                                                                    )
                                                                                                                                    : (
                                                                                                                                       jac_nuc_choice_135(x255, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x357, x358, x359, x360, x361)
                                                                                                                                    ));
}
DEVICE auto jac_nuc_choice_137(bool x255, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x257) ? (
                                                                                                                                    x355 & x360
                                                                                                                                 )
                                                                                                                                 : (
                                                                                                                                    jac_nuc_choice_136(x255, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x356, x357, x358, x359, x360, x361)
                                                                                                                                 ));
}
DEVICE auto jac_nuc_choice_138(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x277) ? (
                                                                                                                                 x354 & x360
                                                                                                                              )
                                                                                                                              : (
                                                                                                                                 jac_nuc_choice_137(x255, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x355, x356, x357, x358, x359, x360, x361)
                                                                                                                              ));
}
DEVICE auto jac_nuc_choice_139(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x274) ? (
                                                                                                                              x353 & x360
                                                                                                                           )
                                                                                                                           : (
                                                                                                                              jac_nuc_choice_138(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                                           ));
}
DEVICE auto jac_nuc_choice_140(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x271) ? (
                                                                                                                           x352 & x360
                                                                                                                        )
                                                                                                                        : (
                                                                                                                           jac_nuc_choice_139(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                                        ));
}
DEVICE auto jac_nuc_choice_141(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x297) ? (
                                                                                                                        x351 & x360
                                                                                                                     )
                                                                                                                     : (
                                                                                                                        jac_nuc_choice_140(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                                     ));
}
DEVICE auto jac_nuc_choice_142(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x277 & x297) ? (
                                                                                                                     x350 & x360
                                                                                                                  )
                                                                                                                  : (
                                                                                                                     jac_nuc_choice_141(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                                  ));
}
DEVICE auto jac_nuc_choice_143(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x274 & x297) ? (
                                                                                                                  x349 & x360
                                                                                                               )
                                                                                                               : (
                                                                                                                  jac_nuc_choice_142(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                               ));
}
DEVICE auto jac_nuc_choice_144(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x271 & x297) ? (
                                                                                                               x348 & x360
                                                                                                            )
                                                                                                            : (
                                                                                                               jac_nuc_choice_143(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                            ));
}
DEVICE auto jac_nuc_choice_145(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x297) ? (
                                                                                                            x347 & x360
                                                                                                         )
                                                                                                         : (
                                                                                                            jac_nuc_choice_144(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                         ));
}
DEVICE auto jac_nuc_choice_146(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x277 & x297) ? (
                                                                                                         x346 & x360
                                                                                                      )
                                                                                                      : (
                                                                                                         jac_nuc_choice_145(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                      ));
}
DEVICE auto jac_nuc_choice_147(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x274 & x297) ? (
                                                                                                      x345 & x360
                                                                                                   )
                                                                                                   : (
                                                                                                      jac_nuc_choice_146(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                   ));
}
DEVICE auto jac_nuc_choice_148(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x271 & x297) ? (
                                                                                                   x344 & x360
                                                                                                )
                                                                                                : (
                                                                                                   jac_nuc_choice_147(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                                ));
}
DEVICE auto jac_nuc_choice_149(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x292 & x293) ? (
                                                                                                x342 & x360
                                                                                             )
                                                                                             : (
                                                                                                jac_nuc_choice_148(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                             ));
}
DEVICE auto jac_nuc_choice_150(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x277 & x292 & x293) ? (
                                                                                             x340 & x360
                                                                                          )
                                                                                          : (
                                                                                             jac_nuc_choice_149(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                          ));
}
DEVICE auto jac_nuc_choice_151(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x274 & x292 & x293) ? (
                                                                                          x338 & x360
                                                                                       )
                                                                                       : (
                                                                                          jac_nuc_choice_150(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                       ));
}
DEVICE auto jac_nuc_choice_152(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x271 & x292 & x293) ? (
                                                                                       x335 & x360
                                                                                    )
                                                                                    : (
                                                                                       jac_nuc_choice_151(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                    ));
}
DEVICE auto jac_nuc_choice_153(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x292 & x293) ? (
                                                                                    x332 & x360
                                                                                 )
                                                                                 : (
                                                                                    jac_nuc_choice_152(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                                 ));
}
DEVICE auto jac_nuc_choice_154(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x277 & x292 & x293) ? (
                                                                                 x330 & x360
                                                                              )
                                                                              : (
                                                                                 jac_nuc_choice_153(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                              ));
}
DEVICE auto jac_nuc_choice_155(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x274 & x292 & x293) ? (
                                                                              x328 & x360
                                                                           )
                                                                           : (
                                                                              jac_nuc_choice_154(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                           ));
}
DEVICE auto jac_nuc_choice_156(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x313, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x271 & x292 & x293) ? (
                                                                           x325 & x360
                                                                        )
                                                                        : (
                                                                           jac_nuc_choice_155(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                        ));
}
DEVICE auto jac_nuc_choice_157(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x309) ? (
                                                                        x320 & x359
                                                                     )
                                                                     : (
                                                                        jac_nuc_choice_156(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x313, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                     ));
}
DEVICE auto jac_nuc_choice_158(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x277 & x309) ? (
                                                                     x320 & x358
                                                                  )
                                                                  : (
                                                                     jac_nuc_choice_157(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                                  ));
}
DEVICE auto jac_nuc_choice_159(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x274 & x309) ? (
                                                                  x320 & x357
                                                               )
                                                               : (
                                                                  jac_nuc_choice_158(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                               ));
}
DEVICE auto jac_nuc_choice_160(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x271 & x309) ? (
                                                               x320 & x356
                                                            )
                                                            : (
                                                               jac_nuc_choice_159(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                            ));
}
DEVICE auto jac_nuc_choice_161(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x309) ? (
                                                            x320 & x355
                                                         )
                                                         : (
                                                            jac_nuc_choice_160(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                         ));
}
DEVICE auto jac_nuc_choice_162(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x277 & x309) ? (
                                                         x320 & x354
                                                      )
                                                      : (
                                                         jac_nuc_choice_161(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                      ));
}
DEVICE auto jac_nuc_choice_163(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x274 & x309) ? (
                                                      x320 & x353
                                                   )
                                                   : (
                                                      jac_nuc_choice_162(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                   ));
}
DEVICE auto jac_nuc_choice_164(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x271 & x309) ? (
                                                   x320 & x352
                                                )
                                                : (
                                                   jac_nuc_choice_163(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                                ));
}
DEVICE auto jac_nuc_choice_165(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x351;
    const auto cb = (
                                                jac_nuc_choice_164(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                             );
    return (x297 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_166(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x350;
    const auto cb = (
                                             jac_nuc_choice_165(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                          );
    return (x277 & x297 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_167(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x349;
    const auto cb = (
                                          jac_nuc_choice_166(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                       );
    return (x274 & x297 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_168(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x348;
    const auto cb = (
                                       jac_nuc_choice_167(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                    );
    return (x271 & x297 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_169(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x347;
    const auto cb = (
                                    jac_nuc_choice_168(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                 );
    return (x255 & x256 & x297 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_170(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x346;
    const auto cb = (
                                 jac_nuc_choice_169(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                              );
    return (x255 & x256 & x277 & x297 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_171(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x274 & x297 & x309) ? (
                              x320 & x345
                           )
                           : (
                              jac_nuc_choice_170(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                           ));
}
DEVICE auto jac_nuc_choice_172(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x344;
    const auto cb = (
                           jac_nuc_choice_171(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                        );
    return (x255 & x256 & x271 & x297 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_173(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x342;
    const auto cb = (
                        jac_nuc_choice_172(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                     );
    return (x292 & x293 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_174(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x340;
    const auto cb = (
                     jac_nuc_choice_173(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                  );
    return (x277 & x292 & x293 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_175(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x338;
    const auto cb = (
                  jac_nuc_choice_174(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
               );
    return (x274 & x292 & x293 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_176(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x335;
    const auto cb = (
               jac_nuc_choice_175(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
            );
    return (x271 & x292 & x293 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_177(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x332;
    const auto cb = (
            jac_nuc_choice_176(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
         );
    return (x255 & x256 & x292 & x293 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_178(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x330;
    const auto cb = (
         jac_nuc_choice_177(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
      );
    return (x255 & x256 & x277 & x292 & x293 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_179(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x274 & x292 & x293 & x309) ? (
      x320 & x328
   )
   : (
      jac_nuc_choice_178(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
   ));
}
DEVICE auto jac_nuc_choice_180(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
    const auto ca = x320 & x325;
    const auto cb = (
   jac_nuc_choice_179(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
);
    return (x255 & x256 & x271 & x292 & x293 & x309) ? ca : cb;
}
DEVICE auto jac_nuc_choice_181(Real x299, Real x319, bool x362) {
return ((x362) ? (
   x299*x319
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_182(bool x230, Real x364, const Real* xn) {
return ((x230) ? (
   0
)
: (
   xn[8 * XS]*x364
));
}
DEVICE auto jac_nuc_choice_183(Real x200, bool x201) {
return ((x201) ? (
   x200
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_184(bool x206, Real x95) {
return ((x206) ? (
   2.0
)
: (
   x95
));
}
DEVICE auto jac_nuc_choice_185(bool x206) {
return ((x206) ? (
      0
   )
   : (
      9.1093818800000008e-28
   ));
}
DEVICE auto jac_nuc_choice_186(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_185(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_187(bool x204) {
return ((x204) ? (
   0
)
: (
   1
));
}
DEVICE auto jac_nuc_choice_188(Real x1, bool x207) {
return ((x207) ? (
   9.9999999999999996e-81
)
: (
   4.4783734451139649e-47*x1
));
}
DEVICE auto jac_nuc_choice_189(Real x203, bool x204) {
return ((x204) ? (
   1.0e+40
)
: (
   1.0/x203
));
}
DEVICE auto jac_nuc_choice_190(Real x203, bool x231) {
return ((x231) ? (
      0
   )
   : (
      -12870.724904810098*math::exp((-1.45)*math::log(math::abs(x203)))
   ));
}
DEVICE auto jac_nuc_choice_191(Real x203, bool x231, bool x233) {
return ((x233) ? (
   0
)
: (
   jac_nuc_choice_190(x203, x231)
));
}
DEVICE auto jac_nuc_choice_192(Real x295, bool x297) {
return ((x297) ? (
   x295
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_193(Real x290, bool x294, Real x295, bool x297) {
return ((x294) ? (
   x290
)
: jac_nuc_choice_192(x295, x297));
}
DEVICE auto jac_nuc_choice_194(bool x206) {
return ((x206) ? (
      0
   )
   : (
      1.6726215800000001e-24
   ));
}
DEVICE auto jac_nuc_choice_195(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_194(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_196(bool x257, Real x265, Real x267) {
return ((x257) ? (
   x265
)
: (
   x267
));
}
DEVICE auto jac_nuc_choice_197(Real x275, bool x277, Real x282) {
return ((x277) ? (
   x275
)
: (
   x282
));
}
DEVICE auto jac_nuc_choice_198(Real x272, bool x274, Real x275, bool x277, Real x282) {
return ((x274) ? (
   x272
)
: jac_nuc_choice_197(x275, x277, x282));
}
DEVICE auto jac_nuc_choice_199(Real x269, bool x271, Real x272, bool x274, Real x275, bool x277, Real x282) {
    const auto ca = x269;
    const auto cb = jac_nuc_choice_198(x272, x274, x275, x277, x282);
    return (x271) ? ca : cb;
}
DEVICE auto jac_nuc_choice_200(bool x173) {
return ((x173) ? (
      0
   )
   : (
      1
   ));
}
DEVICE auto jac_nuc_choice_201(bool x173, bool x174) {
    const auto ca = 0;
    const auto cb = (
   jac_nuc_choice_200(x173)
);
    return (x174) ? ca : cb;
}
DEVICE auto jac_nuc_choice_202(bool x173, const Real* xn) {
return ((x173) ? (
      100.0
   )
   : (
      1.0/xn[2 * XS]
   ));
}
DEVICE auto jac_nuc_choice_203(bool x173, bool x174, const Real* xn) {
    const auto ca = 1.0e-10;
    const auto cb = (
   jac_nuc_choice_202(x173, xn)
);
    return (x174) ? ca : cb;
}
DEVICE auto jac_nuc_choice_204(bool x206) {
return ((x206) ? (
      0
   )
   : (
      1.6735325181900001e-24
   ));
}
DEVICE auto jac_nuc_choice_205(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_204(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_206(bool x206) {
return ((x206) ? (
      0
   )
   : (
      1.6744434563800001e-24
   ));
}
DEVICE auto jac_nuc_choice_207(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_206(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_208(bool x230, Real x380) {
return ((x230) ? (
   0
)
: (
   x380
));
}
DEVICE auto jac_nuc_choice_209(bool x206) {
return ((x206) ? (
      0
   )
   : (
      3.3451215800000003e-24
   ));
}
DEVICE auto jac_nuc_choice_210(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_209(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_211(bool x206) {
return ((x206) ? (
      0
   )
   : (
      3.3460325181899999e-24
   ));
}
DEVICE auto jac_nuc_choice_212(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_211(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_213(bool x206) {
return ((x206) ? (
      0
   )
   : (
      3.3461540981899999e-24
   ));
}
DEVICE auto jac_nuc_choice_214(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_213(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_215(bool x206) {
return ((x206) ? (
      0
   )
   : (
      3.3469434563800003e-24
   ));
}
DEVICE auto jac_nuc_choice_216(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_215(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_217(bool x206) {
return ((x206) ? (
      0
   )
   : (
      3.3470650363800003e-24
   ));
}
DEVICE auto jac_nuc_choice_218(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_217(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_219(bool x206) {
return ((x206) ? (
      0
   )
   : (
      5.0186540981899997e-24
   ));
}
DEVICE auto jac_nuc_choice_220(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_219(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_221(bool x206) {
return ((x206) ? (
      0
   )
   : (
      5.01956503638e-24
   ));
}
DEVICE auto jac_nuc_choice_222(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_221(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_223(bool x206) {
return ((x206) ? (
      0
   )
   : (
      6.6902431600000005e-24
   ));
}
DEVICE auto jac_nuc_choice_224(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_223(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_225(bool x206) {
return ((x206) ? (
      0
   )
   : (
      6.6911540981899994e-24
   ));
}
DEVICE auto jac_nuc_choice_226(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_225(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_227(bool x206) {
return ((x206) ? (
      0
   )
   : (
      6.6920650363799998e-24
   ));
}
DEVICE auto jac_nuc_choice_228(bool x205, bool x206, Real x369) {
return ((x205) ? (
   x369*jac_nuc_choice_227(x206)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_229(Real x253, bool x257, Real x263) {
return ((x257) ? (
   x253
)
: (
   x263
));
}
DEVICE auto jac_nuc_choice_230(bool x16, Real x48) {
return ((x16) ? (
   0
)
: (
   -x48
));
}
DEVICE auto jac_nuc_choice_231(bool x16, Real x481) {
return ((x16) ? (
   0
)
: (
   (1.0/2.0)*x481
));
}
DEVICE auto jac_nuc_choice_232(Real T, bool x16) {
    const auto ca = 0;
    const auto cb = (
   -0.20000000000000001*(math::exp((-0.20000000000000001)*math::log(math::abs(T)))*(1.0/T))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_233(Real T, bool x16) {
    const auto ca = 0;
    const auto cb = (
   -0.16869999999999999*(math::exp((-0.16869999999999999)*math::log(math::abs(T)))*(1.0/T))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_234(Real T, bool x16) {
    const auto ca = 0;
    const auto cb = (
   0.69999999999999996*(math::exp((0.69999999999999996)*math::log(math::abs(T)))*(1.0/T))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_235(bool x167) {
return ((x167) ? (
   0
)
: (
   1
));
}
DEVICE auto jac_nuc_choice_236(bool x167, Real x8) {
return ((x167) ? (
   0.0001
)
: (
   x8
));
}
DEVICE auto jac_nuc_choice_237(bool x313) {
return ((x313) ? (
      0
   )
   : (
      0.00020000000000000001
   ));
}
DEVICE auto jac_nuc_choice_238(bool x255, Real x310, bool x313, Real x314, Real x315, Real x512, Real x523, Real x524, Real x525, Real x526, Real x527, Real x528, Real x529) {
return ((x255) ? (
   x310*(11.55760367482214*x512 - 7.2479875549080308*x523 + 33.455408266588918*x524 - 35.698903039375494*x525 - 54.526167351293466*x526 + 62.988078688261993*x527 + 22.762583481781927*x528 - 32.574051224421225*x529)
)
: (
   -5.5313336794064847e-19*x314*jac_nuc_choice_237(x313)/((x315)*(x315))
));
}
DEVICE auto jac_nuc_choice_239(Real T, bool x255, Real x300, Real x301, Real x302, Real x303, Real x306, Real x307, bool x309, Real x310, bool x313, Real x314, Real x315, Real x48, Real x512, Real x523, Real x524, Real x525, Real x526, Real x527, Real x528, Real x529) {
return ((x309) ? (
   3.2860556719809434e-26*math::exp((-0.24000000000000021)*math::log(math::abs(T)))*x307 + 1.8746122480121903e-32*(math::exp((3.7599999999999998)*math::log(math::abs(T)))*(1.0/T))*x307 - 6.2968615725975507e-40*math::exp((4.8599999999999994)*math::log(math::abs(T)))*x306/((x303)*(x303)) + 1.8719999999999998e-14*x300*x48 + 3.9261999999999998e-15*x301*x48 + 1.53e-21*x302*x48
)
: jac_nuc_choice_238(x255, x310, x313, x314, x315, x512, x523, x524, x525, x526, x527, x528, x529));
}
DEVICE auto jac_nuc_choice_240(Real x296, bool x297, Real x512, Real x523, Real x524, Real x525, Real x526, Real x527, Real x528, Real x529) {
return ((x297) ? (
   x296*(3.8689780091986448*x512 + 4.297112944704045*x523 - 117.34186576967058*x524 + 103.76942484394611*x525 + 123.18901581604325*x526 - 101.40241318979159*x527 - 43.540996231705549*x528 + 27.91190909651122*x529)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_241(Real x291, bool x294, Real x296, bool x297, Real x512, Real x523, Real x524, Real x525, Real x526, Real x527, Real x528, Real x529) {
return ((x294) ? (
   x291*(38.719649225812766*x512 + 445.51869310442481*x523 + 1300.6686834345674*x524 + 5869.3152370465659*x525 + 11077.448644792414*x526 + 11324.985706577943*x527 + 6766.9891340104432*x528 + 2370.6849681533818*x529)
)
: jac_nuc_choice_240(x296, x297, x512, x523, x524, x525, x526, x527, x528, x529));
}
DEVICE auto jac_nuc_choice_242(Real x254, bool x257, Real x264, Real x512, Real x523, Real x527, Real x528, Real x529, Real x532) {
return ((x257) ? (
   x254*(5.0409049417480247*x512 - 3.7541549062629067*x523 + 2.2094886994529301*x527 - 1.528565035159452*x528 + 2.0057552335975877*x529)
)
: (
   x264*x532
));
}
DEVICE auto jac_nuc_choice_243(bool x257, Real x266, Real x268, Real x512, Real x523, Real x527, Real x528, Real x529, Real x532) {
return ((x257) ? (
   x266*(3.6184459289309552*x512 + 0.0708789387907936*x523 + 3.7035619079275177*x527 - 4.6974781513675152*x528 - 1.6316107607322889*x529)
)
: (
   x268*x532
));
}
DEVICE auto jac_nuc_choice_244(Real x101, Real x276, bool x277, Real x278, Real x279, Real x280, Real x281, Real x283, Real x512, Real x523, Real x527, Real x528, Real x529, Real x8) {
return ((x277) ? (
   x276*(10.695627721640689*x512 - 17.135767342440825*x523 + 17.889115159724135*x527 - 50.756388852554174*x528 + 41.010708268606813*x529)
)
: (
   x283*(278.71147075841589*x101*x279*x281*x8/((x279 + 0.003362753556164708)*(x279 + 0.003362753556164708)) - 1.1080034428212589e-30*x278*x280*x512/((1.3368457736780897e-35*x278 + 1)*(1.3368457736780897e-35*x278 + 1)))
));
}
DEVICE auto jac_nuc_choice_245(Real x101, Real x273, bool x274, Real x276, bool x277, Real x278, Real x279, Real x280, Real x281, Real x283, Real x512, Real x523, Real x527, Real x528, Real x529, Real x8) {
return ((x274) ? (
   x273*(8.2184944748967013*x101*x8 - 52.189748993977005*x523 - 48.951834264235494*x527 - 196.44057098336626*x528 - 192.38155095558542*x529)
)
: jac_nuc_choice_244(x101, x276, x277, x278, x279, x280, x281, x283, x512, x523, x527, x528, x529, x8));
}
DEVICE auto jac_nuc_choice_246(Real x101, Real x270, bool x271, Real x273, bool x274, Real x276, bool x277, Real x278, Real x279, Real x280, Real x281, Real x283, Real x512, Real x523, Real x527, Real x528, Real x529, Real x8) {
    const auto ca = x270*(86.079180274567719*x512 + 267.76838492252847*x523 + 44.301288185112313*x527 + 185.67890535151699*x528 + 336.10445235294861*x529);
    const auto cb = jac_nuc_choice_245(x101, x273, x274, x276, x277, x278, x279, x280, x281, x283, x512, x523, x527, x528, x529, x8);
    return (x271) ? ca : cb;
}
DEVICE auto jac_nuc_choice_247(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -6.0790882143828848e+42*x375
   ));
}
DEVICE auto jac_nuc_choice_248(bool x207, bool x216, Real x370, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378) {
return ((x216) ? (
   0
)
: (
   x378*(-x370*x373 - 1.8218763760000002e-27*x372 - x377*(x374 + x376*jac_nuc_choice_247(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_249(Real x239, Real x316, Real x318, bool x362, Real x381, Real x382) {
return ((x362) ? (
      x239*x316*x318*x381 - x239*x381*x382
   )
   : (
      0
   ));
}
DEVICE auto jac_nuc_choice_250(bool x230, Real x239, Real x316, Real x318, bool x362, Real x380, Real x381, Real x382, Real x383) {
    const auto ca = 0;
    const auto cb = (
   x380 + x383*jac_nuc_choice_249(x239, x316, x318, x362, x381, x382)
);
    return (x230) ? ca : cb;
}
DEVICE auto jac_nuc_choice_251(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -1.116213401528895e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_252(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x385) {
return ((x216) ? (
   0
)
: (
   x378*(-3.3452431600000003e-24*x372 - x373*x385 - x377*(x374 + x376*jac_nuc_choice_251(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_253(Real x316, Real x318, bool x362, Real x382, Real x386) {
return ((x362) ? (
      x316*x318*x386 - x382*x386
   )
   : (
      0
   ));
}
DEVICE auto jac_nuc_choice_254(bool x230, Real x316, Real x318, bool x362, Real x380, Real x382, Real x383, Real x386) {
    const auto ca = 0;
    const auto cb = (
   x380 + x383*jac_nuc_choice_253(x316, x318, x362, x382, x386)
);
    return (x230) ? ca : cb;
}
DEVICE auto jac_nuc_choice_255(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -1.1168213103516679e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_256(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x440) {
return ((x216) ? (
   0
)
: (
   x378*(-3.3470650363800003e-24*x372 - x373*x440 - x377*(x374 + x376*jac_nuc_choice_255(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_257(Real x316, Real x318, bool x362, Real x382, Real x419) {
return ((x362) ? (
      x316*x318*x419 - x382*x419
   )
   : (
      0
   ));
}
DEVICE auto jac_nuc_choice_258(bool x230, Real x316, Real x318, bool x362, Real x380, Real x382, Real x383, Real x419) {
    const auto ca = 0;
    const auto cb = (
   x380 + x383*jac_nuc_choice_257(x316, x318, x362, x382, x419)
);
    return (x230) ? ca : cb;
}
DEVICE auto jac_nuc_choice_259(Real x101, Real x179, Real x182, Real x183, Real x184, Real x185, Real x186, Real x188, Real x190, Real x191, Real x193, Real x194, Real x195, Real x196, Real x197, Real x198, Real x200, bool x201, Real x420, Real x421, Real x422, Real x423, Real x424, Real x425, Real x426, Real x427, Real x428) {
return ((x201) ? (
   x200*(2.1283484790071863*x101*x420 + 1.7949111316907187*x179*x420 - 0.019226585526500282*x182*x420 + 0.025328436022934504*x183*x420 + 0.26965574024053274*x184*x420 - 0.53023929521466884*x185*x420 - 1.249451749011359*x186*x420 + 0.00057030427583276537*x188*x426 - 0.006136941893251451*x190*x426 - 0.010237293323451529*x191*x427 + 0.023154795695148129*x193*x426 + 0.12150741535729581*x194*x427 + 0.048814803971473773*x195*x424 - 0.033709845761432836*x196*x422 - 0.63403983120684049*x197*x424 + 0.81953608629844088*x198*x422 + 2.5310936376227748*x420*x421 - 4.9020655078787438*x422*x423 + 2.8710012490505563*x424*x425 - 0.50882525384982424*x427*x428)
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_260(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -1.117429219174441e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_261(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x448) {
return ((x216) ? (
   0
)
: (
   x378*(-3.3488869127600003e-24*x372 - x373*x448 - x377*(x374 + x376*jac_nuc_choice_260(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_262(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -2.2323456674160042e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_263(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x457) {
return ((x216) ? (
   0
)
: (
   x378*(-6.6902431600000005e-24*x372 - x373*x457 - x377*(x374 + x376*jac_nuc_choice_262(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_264(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -2.232953576238777e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_265(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x460) {
return ((x216) ? (
   0
)
: (
   x378*(-6.6920650363799998e-24*x372 - x373*x460 - x377*(x374 + x376*jac_nuc_choice_264(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_266(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -2.2330347118805627e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_267(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x462) {
return ((x216) ? (
   0
)
: (
   x378*(-6.6923081963799998e-24*x372 - x373*x462 - x377*(x374 + x376*jac_nuc_choice_266(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_268(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -2.23356148506155e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_269(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x465) {
return ((x216) ? (
   0
)
: (
   x378*(-6.6938869127600005e-24*x372 - x373*x465 - x377*(x374 + x376*jac_nuc_choice_268(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_270(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -2.2336426207033358e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_271(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x467) {
return ((x216) ? (
   0
)
: (
   x378*(-6.6941300727600005e-24*x372 - x373*x467 - x377*(x374 + x376*jac_nuc_choice_270(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_272(Real x239, Real x248, Real x249, Real x316, Real x318, bool x362, Real x382) {
return ((x362) ? (
      x239*x248*x316*x318 - x249*x382
   )
   : (
      0
   ));
}
DEVICE auto jac_nuc_choice_273(bool x230, Real x239, Real x248, Real x249, Real x316, Real x318, bool x362, Real x364, Real x380, Real x382, Real x383) {
    const auto ca = 0;
    const auto cb = (
   x364 + x380 + x383*jac_nuc_choice_272(x239, x248, x249, x316, x318, x362, x382)
);
    return (x230) ? ca : cb;
}
DEVICE auto jac_nuc_choice_274(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -3.3491669777676719e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_275(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x468) {
return ((x216) ? (
   0
)
: (
   x378*(-1.0037308196379999e-23*x372 - x373*x468 - x377*(x374 + x376*jac_nuc_choice_274(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_276(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -3.3497748865904447e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_277(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x469) {
return ((x216) ? (
   0
)
: (
   x378*(-1.003913007276e-23*x372 - x373*x469 - x377*(x374 + x376*jac_nuc_choice_276(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_278(Real x199, bool x201) {
return ((x201) ? (
   x199
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_279(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -4.4646913348320083e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_280(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x470) {
return ((x216) ? (
   0
)
: (
   x378*(-1.3380486320000001e-23*x372 - x373*x470 - x377*(x374 + x376*jac_nuc_choice_279(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_281(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -4.4652992436547811e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_282(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x471) {
return ((x216) ? (
   0
)
: (
   x378*(-1.3382308196379999e-23*x372 - x373*x471 - x377*(x374 + x376*jac_nuc_choice_281(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_283(bool x207, Real x375) {
return ((x207) ? (
      0
   )
   : (
      -4.4659071524775539e+46*x375
   ));
}
DEVICE auto jac_nuc_choice_284(bool x207, bool x216, Real x372, Real x373, Real x374, Real x375, Real x376, Real x377, Real x378, Real x473) {
return ((x216) ? (
   0
)
: (
   x378*(-1.338413007276e-23*x372 - x373*x473 - x377*(x374 + x376*jac_nuc_choice_283(x207, x375)))
));
}
DEVICE auto jac_nuc_choice_285(Real x316, Real x318, bool x362, Real x382, Real x474) {
return ((x362) ? (
      x316*x318*x474 - x382*x474
   )
   : (
      0
   ));
}
DEVICE auto jac_nuc_choice_286(bool x230, Real x316, Real x318, bool x362, Real x380, Real x382, Real x383, Real x474) {
    const auto ca = 0;
    const auto cb = (
   x380 + x383*jac_nuc_choice_285(x316, x318, x362, x382, x474)
);
    return (x230) ? ca : cb;
}
DEVICE auto jac_nuc_choice_287(Real T, bool x16) {
    const auto ca = 0;
    const auto cb = (
   -0.39700000000000002*(math::exp((-0.39700000000000002)*math::log(math::abs(T)))*(1.0/T))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_288(bool x16, Real x488) {
return ((x16) ? (
   0
)
: (
   -1.5*x488
));
}
DEVICE auto jac_nuc_choice_289(Real T, bool x16) {
    const auto ca = 0;
    const auto cb = (
   0.36470000000000002*(math::exp((0.36470000000000002)*math::log(math::abs(T)))*(1.0/T))
);
    return (x16) ? ca : cb;
}
DEVICE auto jac_nuc_choice_290(bool x216, Real x222, Real x378, Real x481, Real x91, Real x94) {
return ((x216) ? (
   0
)
: (
   -x222*x378*x481*x91*x94
));
}
DEVICE auto jac_nuc_choice_291(Real x299, Real x318, Real x319, bool x362, Real x382, Real x530, Real x533) {
return ((x362) ? (
      x299*x318*x530 + x319*x533 + x382*(-x530 - x533)
   )
   : (
      0
   ));
}
DEVICE auto jac_nuc_choice_292(bool x230, Real x299, Real x318, Real x319, bool x362, Real x382, Real x383, Real x530, Real x533) {
    const auto ca = 0;
    const auto cb = (
   x383*jac_nuc_choice_291(x299, x318, x319, x362, x382, x530, x533)
);
    return (x230) ? ca : cb;
}
DEVICE auto jac_nuc_choice_293(Real x101, Real x168, Real x171, Real x172, Real x179, Real x181, Real x183, Real x184, Real x185, Real x186, Real x187, Real x189, Real x190, Real x192, Real x193, Real x194, Real x196, Real x197, Real x198, Real x200, bool x201, Real x421, Real x423, Real x425, Real x428, Real x506, Real x507, Real x508, Real x509, Real x510, Real x511) {
return ((x201) ? (
   x200*(50.504556041967454*x101*x506 + 0.011577397847574064*x168*x192*x508 + 0.00057030427583276537*x171*x187*x508 - 0.0046027064199385889*x172*x189*x508 - 46.931151210299063*x179*x506 - 0.0084274614403582089*x181*x506 + 0.27317869543281359*x183*x506 - 1.5965204000783517*x184*x506 - 2.4510327539393719*x185*x506 + 15.190568323798459*x186*x506 - 0.013649724431268705*x190*x510 + 0.12150741535729581*x193*x510 + 0.097629607942947547*x194*x511 - 0.33921683589988288*x196*x507 - 0.076906342106001127*x197*x509 + 2.8710012490505563*x198*x507 + 1.7949111316907187*x421*x506 - 2.4989034980227181*x423*x507 + 0.80896722072159821*x425*x509 - 0.95105974681026062*x428*x511)
)
: (
   0
));
}
DEVICE auto jacd_rhs_specie_choice_0_0(Real T, Real x3, bool x4, Real dx3) {
return ((x4) ? (
   ((((((-5.528e-28)*((((((((1.0*T) + (T*1.0))*T) + ((T * T)*1.0))*T) + (((T * T) * T)*1.0))*T) + ((((T * T) * T) * T)*1.0))) + (3.3468e-23*((((1.0*T) + (T*1.0))*(T * T)) + ((T * T)*((1.0*T) + (T*1.0)))))) - (7.5474e-19*((((1.0*T) + (T*1.0))*T) + ((T * T)*1.0)))) - (2.3088e-11*1.0)) + (7.3428e-15*dx3))
)
: (
   0.0
));
}
DEVICE auto jacd_rhs_specie_choice_1_1(Real T, Real x10, Real x11, Real x12, Real x13, Real x14, Real x15, Real x8, Real x9, Real dx10, Real dx11, Real dx12, Real dx13, Real dx14, Real dx15, Real dx8, Real dx9) {
return ((T <= 1160.0) ? (
   (1.464348260610906e-16*((1.78186*math::exp((1.78186 * math::log(math::abs(T)))))*(1.0/T)))
)
: (
   (((3.3178155742407614e-14*((1.139449335841631*math::exp((1.139449335841631 * math::log(math::abs(T)))))*(1.0/T)))*math::exp((((((((((-10.993097527150175) * x10) + (14.449862906216714 * x11)) + (58.22837578970318 * x12)) - (162.59852239006702 * x13)) + (144.55426734953477 * x14)) - (44.454280878123605 * x15)) - (12.447178055372778 * x8)) + (6.939178477839912 * x9)))) + ((3.3178155742407614e-14 * math::exp((1.139449335841631 * math::log(math::abs(T)))))*(math::exp((((((((((-10.993097527150175) * x10) + (14.449862906216714 * x11)) + (58.22837578970318 * x12)) - (162.59852239006702 * x13)) + (144.55426734953477 * x14)) - (44.454280878123605 * x15)) - (12.447178055372778 * x8)) + (6.939178477839912 * x9)))*(((((((((-10.993097527150175)*dx10) + (14.449862906216714*dx11)) + (58.22837578970318*dx12)) - (162.59852239006702*dx13)) + (144.55426734953477*dx14)) - (44.454280878123605*dx15)) - (12.447178055372778*dx8)) + (6.939178477839912*dx9)))))
));
}
DEVICE auto jacd_rhs_specie_choice_2_2(Real T, Real x32, Real x33, Real x34, Real dx32, Real dx33, Real dx34) {
return ((x33 <= 9280.0) ? (
   dx32
)
: (
   ((((((1250086.112245841*((-1.5*(math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T)))*(1.0/T)))*(1.54e-09 + (4.62e-10 * math::exp(((-93988.70150192466) * x34))))) + ((1250086.112245841 * (math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T)))*(4.62e-10*(math::exp(((-93988.70150192466) * x34))*((-93988.70150192466)*dx34)))))*math::exp(((-469943.5075096421) * x34))) + (((1250086.112245841 * (math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T))) * (1.54e-09 + (4.62e-10 * math::exp(((-93988.70150192466) * x34)))))*(math::exp(((-469943.5075096421) * x34))*((-469943.5075096421)*dx34)))) + dx32)
));
}
DEVICE auto jacd_rhs_specie_choice_3_3(Real T, Real x10, Real x11, Real x12, Real x13, Real x14, Real x15, Real x32, Real x33, Real x8, Real x9, Real dx10, Real dx11, Real dx12, Real dx13, Real dx14, Real dx15, Real dx32, Real dx33, Real dx8, Real dx9) {
return ((x33 <= 5500.0) ? (
   dx32
)
: (
   (((3.2867337024382687e-10*((-0.7241125657826851*math::exp(((-0.7241125657826851) * math::log(math::abs(T)))))*(1.0/T)))*math::exp((((((((((-2.4649195146505534) * x10) - (1.020773727011937 * x11)) + (3.3530579587656564 * x12)) + (3.620312764637779 * x13)) - (1.0930705283186732 * x14)) - (1.6921001126637107 * x15)) - (1.774686809424741 * x8)) - (1.951835616513679 * x9)))) + ((3.2867337024382687e-10 * math::exp(((-0.7241125657826851) * math::log(math::abs(T)))))*(math::exp((((((((((-2.4649195146505534) * x10) - (1.020773727011937 * x11)) + (3.3530579587656564 * x12)) + (3.620312764637779 * x13)) - (1.0930705283186732 * x14)) - (1.6921001126637107 * x15)) - (1.774686809424741 * x8)) - (1.951835616513679 * x9)))*(((((((((-2.4649195146505534)*dx10) - (1.020773727011937*dx11)) + (3.3530579587656564*dx12)) + (3.620312764637779*dx13)) - (1.0930705283186732*dx14)) - (1.6921001126637107*dx15)) - (1.774686809424741*dx8)) - (1.951835616513679*dx9)))))
));
}
DEVICE auto jacd_rhs_specie_choice_4_4(Real T, Real x28, Real x29, Real dx28, Real dx29) {
return ((T >= 10.0 & T <= 100000.0) ? (
   ((((-7.77e-13)*1.0) + (2.5e-10*dx28)) + (2.96e-06*dx29))
)
: (
   0.0
));
}
DEVICE auto jacd_rhs_specie_choice_5_5(Real T, Real x46, Real x52, Real x53, Real x57, Real dx46, Real dx52, Real dx57) {
return ((T < 30) ? (
   (3.4977396723747635e-20*((-0.15*math::exp(((-0.15) * math::log(math::abs(T)))))*(1.0/T)))
)
: (
   (math::exp(((((((-3.194) * x46) + ((1.786 * x52) * x53)) - (0.2072 * x57)) - 18.2) * math::log(math::abs(10.0))))*(((((-3.194)*dx46) + ((1.786*dx52)*x53)) - (0.2072*dx57))*math::log(math::abs(10.0))))
));
}
DEVICE auto jacd_rhs_specie_choice_6_6(Real T, Real x34, Real x52, Real x56, Real x6, Real x60, Real x61, Real dx34, Real dx52, Real dx56, Real dx6, Real dx60, Real dx61) {
    const auto ca = ((((((((((-1.4491368e-07)*dx52) + (3.4172805e-08*dx56)) + (3.5311932e-13*((((((((((((dx6*x6) + (x6*dx6))*x6) + ((x6 * x6)*dx6))*x6) + (((x6 * x6) * x6)*dx6))*x6) + ((((x6 * x6) * x6) * x6)*dx6))*x6) + (((((x6 * x6) * x6) * x6) * x6)*dx6))*x6) + ((((((x6 * x6) * x6) * x6) * x6) * x6)*dx6)))) - (1.8171411e-11*(6.0*(x6*x6*x6*x6*x6)*dx6))) + (3.3735382e-07*dx6)) - (4.7813728e-09*dx60)) + (3.9731542e-10*dx61))*math::exp(((-21237.15) * x34))) + ((((((((((-1.4491368e-07) * x52) + (3.4172805e-08 * x56)) + (3.5311932e-13 * ((((((x6 * x6) * x6) * x6) * x6) * x6) * x6))) - (1.8171411e-11 * math::powi<6>(math::abs(x6)))) + (3.3735382e-07 * x6)) - (4.7813728e-09 * x60)) + (3.9731542e-10 * x61)) - 3.3232183e-07)*(math::exp(((-21237.15) * x34))*((-21237.15)*dx34))));
    const auto cb = (
   0.0
);
    return (T >= 100.0 & T <= 30000.0) ? ca : cb;
}
DEVICE auto jacd_rhs_specie_choice_7_7(Real T, Real x0, Real x34, bool x4, Real dx0, Real dx34) {
return ((x4) ? (
   (((1.26e-09*dx0)*math::exp(((-127500.0) * x34))) + ((1.26e-09 * x0)*(math::exp(((-127500.0) * x34))*((-127500.0)*dx34))))
)
: (
   (4e-37*((4.74*math::exp((4.74 * math::log(math::abs(T)))))*(1.0/T)))
));
}
DEVICE auto jacd_rhs_specie_choice_8_8(Real T, Real x34, bool x73, Real dx34) {
    const auto ca = ((((2e-10*((0.402*math::exp((0.402 * math::log(math::abs(T)))))*(1.0/T)))*math::exp(((-37.1) * x34))) + ((2e-10 * math::exp((0.402 * math::log(math::abs(T)))))*(math::exp(((-37.1) * x34))*((-37.1)*dx34)))) - (3.31e-17*((1.48*math::exp((1.48 * math::log(math::abs(T)))))*(1.0/T))));
    const auto cb = (
   0.0
);
    return (x73) ? ca : cb;
}
DEVICE auto jacd_rhs_specie_choice_9_9(Real T, Real x34, bool x73, Real dx34) {
    const auto ca = ((2.03e-09*((-0.332*math::exp(((-0.332) * math::log(math::abs(T)))))*(1.0/T))) + (((2.06e-10*((0.396*math::exp((0.396 * math::log(math::abs(T)))))*(1.0/T)))*math::exp(((-33.0) * x34))) + ((2.06e-10 * math::exp((0.396 * math::log(math::abs(T)))))*(math::exp(((-33.0) * x34))*((-33.0)*dx34)))));
    const auto cb = (
   0.0
);
    return (x73) ? ca : cb;
}
DEVICE auto jacd_rhs_specie_choice_10_10(Real T, Real x34, Real x44, Real x46, Real x54, Real x57, Real x60, Real x61, Real dx34, Real dx46, Real dx54, Real dx57, Real dx60, Real dx61) {
return ((T <= 1167.4796423742259) ? (
   (math::exp((((((((5.88886 * x46) + (7.19692 * x54)) + (2.25069 * x57)) - 56.4737) - ((2.16903 * x60) / ((x44 * x44) * (x44 * x44)))) + ((0.317887 * x61) / ((((x44 * x44) * x44) * x44) * x44))) * math::log(math::abs(10.0))))*((((((5.88886*dx46) + (7.19692*dx54)) + (2.25069*dx57)) - ((2.16903*dx60)/((x44 * x44) * (x44 * x44)))) + ((0.317887*dx61)/((((x44 * x44) * x44) * x44) * x44)))*math::log(math::abs(10.0))))
)
: (
   (3.17e-10*(math::exp(((-5207.0) * x34))*((-5207.0)*dx34)))
));
}
DEVICE auto jacd_rhs_specie_choice_11_11(Real T, Real x3, Real x34, Real dx3, Real dx34) {
return ((T > 200.0) ? (
   (5.25e-11*(math::exp((((-4430.0) * x34) + (173900.0 / x3)))*(((-4430.0)*dx34) + (-(((173900.0 / x3)*dx3)/x3)))))
)
: (
   0.0
));
}
// Structurally zero Jacobian entries: jac_nuc never changes them, so they are
// written once per cell before the integrator instead of on every Jacobian call.
DEVICE void jac_nuc_structural_zeros(Real* out) {
    out[matrix_index(0, 10)] = 0;
    out[matrix_index(1, 11)] = 0;
    out[matrix_index(2, 11)] = 0;
    out[matrix_index(3, 4)] = 0;
    out[matrix_index(3, 9)] = 0;
    out[matrix_index(3, 10)] = 0;
    out[matrix_index(3, 11)] = 0;
    out[matrix_index(3, 12)] = 0;
    out[matrix_index(3, 13)] = 0;
    out[matrix_index(4, 3)] = 0;
    out[matrix_index(4, 6)] = 0;
    out[matrix_index(4, 9)] = 0;
    out[matrix_index(4, 11)] = 0;
    out[matrix_index(4, 12)] = 0;
    out[matrix_index(4, 13)] = 0;
    out[matrix_index(5, 6)] = 0;
    out[matrix_index(5, 11)] = 0;
    out[matrix_index(5, 12)] = 0;
    out[matrix_index(5, 13)] = 0;
    out[matrix_index(6, 4)] = 0;
    out[matrix_index(6, 5)] = 0;
    out[matrix_index(6, 7)] = 0;
    out[matrix_index(6, 9)] = 0;
    out[matrix_index(6, 10)] = 0;
    out[matrix_index(6, 11)] = 0;
    out[matrix_index(6, 12)] = 0;
    out[matrix_index(6, 13)] = 0;
    out[matrix_index(7, 6)] = 0;
    out[matrix_index(7, 8)] = 0;
    out[matrix_index(7, 9)] = 0;
    out[matrix_index(7, 10)] = 0;
    out[matrix_index(7, 11)] = 0;
    out[matrix_index(7, 12)] = 0;
    out[matrix_index(7, 13)] = 0;
    out[matrix_index(8, 7)] = 0;
    out[matrix_index(8, 11)] = 0;
    out[matrix_index(8, 12)] = 0;
    out[matrix_index(8, 13)] = 0;
    out[matrix_index(9, 3)] = 0;
    out[matrix_index(9, 6)] = 0;
    out[matrix_index(9, 7)] = 0;
    out[matrix_index(9, 8)] = 0;
    out[matrix_index(9, 10)] = 0;
    out[matrix_index(9, 11)] = 0;
    out[matrix_index(9, 12)] = 0;
    out[matrix_index(9, 13)] = 0;
    out[matrix_index(10, 0)] = 0;
    out[matrix_index(10, 6)] = 0;
    out[matrix_index(10, 11)] = 0;
    out[matrix_index(10, 12)] = 0;
    out[matrix_index(10, 13)] = 0;
    out[matrix_index(11, 1)] = 0;
    out[matrix_index(11, 2)] = 0;
    out[matrix_index(11, 3)] = 0;
    out[matrix_index(11, 4)] = 0;
    out[matrix_index(11, 5)] = 0;
    out[matrix_index(11, 6)] = 0;
    out[matrix_index(11, 7)] = 0;
    out[matrix_index(11, 8)] = 0;
    out[matrix_index(11, 9)] = 0;
    out[matrix_index(11, 10)] = 0;
    out[matrix_index(11, 13)] = 0;
    out[matrix_index(12, 3)] = 0;
    out[matrix_index(12, 4)] = 0;
    out[matrix_index(12, 5)] = 0;
    out[matrix_index(12, 6)] = 0;
    out[matrix_index(12, 7)] = 0;
    out[matrix_index(12, 8)] = 0;
    out[matrix_index(12, 9)] = 0;
    out[matrix_index(12, 10)] = 0;
    out[matrix_index(13, 3)] = 0;
    out[matrix_index(13, 4)] = 0;
    out[matrix_index(13, 5)] = 0;
    out[matrix_index(13, 6)] = 0;
    out[matrix_index(13, 7)] = 0;
    out[matrix_index(13, 8)] = 0;
    out[matrix_index(13, 9)] = 0;
    out[matrix_index(13, 10)] = 0;
    out[matrix_index(13, 11)] = 0;
}
DEVICE void jac_nuc(Real temperature, Real* out, const Real* xn, Real z) {
    Real T = temperature;
    const Real rT = 1.0/T;
    // rows 0..13 by forward differentiation of rhs_specie (genjac.py)
    const Real rv1192 = 1.0/(9.1093818800000008e-28*xn[0 * XS] + 1.6726215800000001e-24*xn[1 * XS] + 5.01956503638e-24*xn[10 * XS] + 6.6902431600000005e-24*xn[11 * XS] + 6.6911540981899994e-24*xn[12 * XS] + 6.6920650363799998e-24*xn[13 * XS] + 1.6735325181900001e-24*xn[2 * XS] + 1.6744434563800001e-24*xn[3 * XS] + 3.3451215800000003e-24*xn[4 * XS] + 3.3460325181899999e-24*xn[5 * XS] + 3.3461540981899999e-24*xn[6 * XS] + 3.3469434563800003e-24*xn[7 * XS] + 3.3470650363800003e-24*xn[8 * XS] + 5.0186540981899997e-24*xn[9 * XS]);
    const Real rv1193 = 2.0860422997526066e-16*rv1192;
    const Real rv1194 = 3.4767371836380304e-16*rv1192;
    const Real dTde = 1.0/(xn[0 * XS]*rv1193 + xn[1 * XS]*rv1193 + xn[10 * XS]*rv1194 + xn[11 * XS]*rv1193 + xn[12 * XS]*rv1193 + xn[13 * XS]*rv1193 + xn[2 * XS]*rv1193 + xn[3 * XS]*rv1193 + xn[4 * XS]*rv1193 + xn[5 * XS]*rv1193 + xn[6 * XS]*rv1194 + xn[7 * XS]*rv1193 + xn[8 * XS]*rv1194 + xn[9 * XS]*rv1194);
    const Real sx0 = (math::exp((0.25)*math::log(math::abs(T)))*(1.0/T));
    const Real sdT_x0 = ((-0.75*(math::exp((0.25)*math::log(math::abs(T)))*(1.0/T)))*rT);
    const Real sx2 = 1.3300135414628029e-18*math::exp((0.94999999999999996)*math::log(math::abs(T)))*xn[0 * XS]*xn[5 * XS]*math::exp(-0.00010729613733905579*T);
    const Real sdT_x2 = (((((1.3300135414628029e-18*((0.95*math::exp((0.95 * math::log(math::abs(T)))))*rT))*xn[0 * XS])*xn[5 * XS])*math::exp(((-0.00010729613733905579) * T))) + ((((1.3300135414628029e-18 * math::exp((0.95 * math::log(math::abs(T))))) * xn[0 * XS]) * xn[5 * XS])*(math::exp(((-0.00010729613733905579) * T))*((-0.00010729613733905579)*1.0))));
    const Real sdn0_x2 = ((((1.3300135414628029e-18 * math::exp((0.95 * math::log(math::abs(T)))))*1.0)*xn[5 * XS])*math::exp(((-0.00010729613733905579) * T)));
    const Real sdn5_x2 = ((((1.3300135414628029e-18 * math::exp((0.95 * math::log(math::abs(T))))) * xn[0 * XS])*1.0)*math::exp(((-0.00010729613733905579) * T)));
    const Real a0_T_0 = -sdT_x2;
    const Real a0_n0_0 = -sdn0_x2;
    const Real a0_n5_0 = -sdn5_x2;
    const Real a5_T_0 = -sdT_x2;
    const Real a5_n0_0 = -sdn0_x2;
    const Real a5_n5_0 = -sdn5_x2;
    const Real a7_T_0 = sdT_x2;
    const Real a7_n0_0 = sdn0_x2;
    const Real a7_n5_0 = sdn5_x2;
    out[matrix_index(7, 0)] = a7_n0_0;
    const Real sx1 = 2.5950363272655348e-10*xn[0 * XS]*xn[4 * XS]*sx0;
    const Real sdT_x1 = (((2.595036327265535e-10 * xn[0 * XS]) * xn[4 * XS])*sdT_x0);
    const Real sdn0_x1 = (((2.595036327265535e-10*1.0)*xn[4 * XS])*sx0);
    const Real sdn4_x1 = (((2.595036327265535e-10 * xn[0 * XS])*1.0)*sx0);
    const Real a0_T_1 = a0_T_0 - sdT_x1;
    const Real a0_n0_1 = a0_n0_0 - sdn0_x1;
    const Real a0_n4_0 = -sdn4_x1;
    const Real a4_T_0 = -sdT_x1;
    const Real a4_n0_0 = -sdn0_x1;
    const Real a4_n4_0 = -sdn4_x1;
    const Real a5_T_1 = a5_T_0 + sdT_x1;
    const Real a5_n0_1 = a5_n0_0 + sdn0_x1;
    const Real a5_n4_0 = sdn4_x1;
    out[matrix_index(0, 4)] = a0_n4_0;
    out[matrix_index(4, 0)] = a4_n0_0;
    const Real sx3 = ((T)*(T));
    const Real sdT_x3 = ((1.0*T) + (T*1.0));
    const Real sx4 = T <= 10000.0;
    const Real sx6 = math::log(math::abs(T));
    const Real sdT_x6 = (1.0*rT);
    const Real sx5 = xn[0 * XS]*xn[6 * XS]*rhs_specie_choice_0(T, sx3, sx4);
    const Real sdT_x5 = ((xn[0 * XS] * xn[6 * XS])*jacd_rhs_specie_choice_0_0(T, sx3, sx4, sdT_x3));
    const Real sdn0_x5 = ((1.0*xn[6 * XS])*rhs_specie_choice_0(T, sx3, sx4));
    const Real sdn6_x5 = ((xn[0 * XS]*1.0)*rhs_specie_choice_0(T, sx3, sx4));
    const Real a0_T_2 = a0_T_1 - sdT_x5;
    const Real a0_n0_2 = a0_n0_1 - sdn0_x5;
    const Real a0_n6_0 = -sdn6_x5;
    const Real a2_T_0 = (2.0*sdT_x5);
    const Real a2_n0_0 = (2.0*sdn0_x5);
    const Real a2_n6_0 = (2.0*sdn6_x5);
    const Real a6_T_0 = -sdT_x5;
    const Real a6_n0_0 = -sdn0_x5;
    const Real a6_n6_0 = -sdn6_x5;
    out[matrix_index(0, 6)] = a0_n6_0;
    out[matrix_index(6, 0)] = a6_n0_0;
    const Real sx7 = 0.10684732509875319*sx6 - 1;
    const Real sdT_x7 = (0.1068473250987532*sdT_x6);
    const Real sx8 = ((sx7)*(sx7));
    const Real sdT_x8 = ((sdT_x7*sx7) + (sx7*sdT_x7));
    const Real sx9 = ((sx7)*(sx7)*(sx7));
    const Real sdT_x9 = ((((sdT_x7*sx7) + (sx7*sdT_x7))*sx7) + ((sx7 * sx7)*sdT_x7));
    const Real sx10 = ((((sx7)*(sx7)))*(((sx7)*(sx7))));
    const Real sdT_x10 = ((((sdT_x7*sx7) + (sx7*sdT_x7))*(sx7 * sx7)) + ((sx7 * sx7)*((sdT_x7*sx7) + (sx7*sdT_x7))));
    const Real sx11 = ((sx7)*(sx7)*(sx7)*(sx7)*(sx7));
    const Real sdT_x11 = ((((((((sdT_x7*sx7) + (sx7*sdT_x7))*sx7) + ((sx7 * sx7)*sdT_x7))*sx7) + (((sx7 * sx7) * sx7)*sdT_x7))*sx7) + ((((sx7 * sx7) * sx7) * sx7)*sdT_x7));
    const Real sx12 = math::powi<6>(math::abs(sx7));
    const Real sdT_x12 = (6.0*(sx7*sx7*sx7*sx7*sx7)*sdT_x7);
    const Real sx13 = ((sx7)*(sx7)*(sx7)*(sx7)*(sx7)*(sx7)*(sx7));
    const Real sdT_x13 = ((((((((((((sdT_x7*sx7) + (sx7*sdT_x7))*sx7) + ((sx7 * sx7)*sdT_x7))*sx7) + (((sx7 * sx7) * sx7)*sdT_x7))*sx7) + ((((sx7 * sx7) * sx7) * sx7)*sdT_x7))*sx7) + (((((sx7 * sx7) * sx7) * sx7) * sx7)*sdT_x7))*sx7) + ((((((sx7 * sx7) * sx7) * sx7) * sx7) * sx7)*sdT_x7));
    const Real sx15 = ((sx7)*(sx7)*(sx7)*(sx7)*(sx7)*(sx7)*(sx7)*(sx7)*(sx7));
    const Real sdT_x15 = ((((((((((((((((sdT_x7*sx7) + (sx7*sdT_x7))*sx7) + ((sx7 * sx7)*sdT_x7))*sx7) + (((sx7 * sx7) * sx7)*sdT_x7))*sx7) + ((((sx7 * sx7) * sx7) * sx7)*sdT_x7))*sx7) + (((((sx7 * sx7) * sx7) * sx7) * sx7)*sdT_x7))*sx7) + ((((((sx7 * sx7) * sx7) * sx7) * sx7) * sx7)*sdT_x7))*sx7) + (((((((sx7 * sx7) * sx7) * sx7) * sx7) * sx7) * sx7)*sdT_x7))*sx7) + ((((((((sx7 * sx7) * sx7) * sx7) * sx7) * sx7) * sx7) * sx7)*sdT_x7));
    const Real sx14 = math::powi<8>(math::abs(sx7));
    const Real sdT_x14 = (8.0*(sx7*sx7*sx7*sx7*sx7*sx7*sx7)*sdT_x7);
    const Real sx16 = xn[2 * XS]*xn[3 * XS];
    const Real sdn2_x16 = (1.0*xn[3 * XS]);
    const Real sdn3_x16 = (xn[2 * XS]*1.0);
    const Real sx18 = xn[1 * XS]*xn[3 * XS];
    const Real sdn1_x18 = (1.0*xn[3 * XS]);
    const Real sdn3_x18 = (xn[1 * XS]*1.0);
    const Real sx17 = sx16*rhs_specie_choice_1(T, sx10, sx11, sx12, sx13, sx14, sx15, sx8, sx9);
    const Real sdT_x17 = (sx16*jacd_rhs_specie_choice_1_1(T, sx10, sx11, sx12, sx13, sx14, sx15, sx8, sx9, sdT_x10, sdT_x11, sdT_x12, sdT_x13, sdT_x14, sdT_x15, sdT_x8, sdT_x9));
    const Real sdn2_x17 = (sdn2_x16*rhs_specie_choice_1(T, sx10, sx11, sx12, sx13, sx14, sx15, sx8, sx9));
    const Real sdn3_x17 = (sdn3_x16*rhs_specie_choice_1(T, sx10, sx11, sx12, sx13, sx14, sx15, sx8, sx9));
    const Real a0_T_3 = a0_T_2 + sdT_x17;
    const Real a0_n2_0 = sdn2_x17;
    const Real a0_n3_0 = sdn3_x17;
    const Real sx19 = 1.0e-8*math::exp((-0.40000000000000002)*math::log(math::abs(T)))*sx18;
    const Real sdT_x19 = ((1e-08*((-0.4*math::exp(((-0.4) * math::log(math::abs(T)))))*rT))*sx18);
    const Real sdn1_x19 = ((1e-08 * math::exp(((-0.4) * math::log(math::abs(T)))))*sdn1_x18);
    const Real sdn3_x19 = ((1e-08 * math::exp(((-0.4) * math::log(math::abs(T)))))*sdn3_x18);
    const Real a0_T_4 = a0_T_3 + sdT_x19;
    const Real a0_n1_0 = sdn1_x19;
    const Real a0_n3_1 = a0_n3_0 + sdn3_x19;
    const Real a3_T_0 = -sdT_x19;
    const Real a3_n1_0 = -sdn1_x19;
    const Real a3_n3_0 = -sdn3_x19;
    const Real sx20 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));
    const Real sdT_x20 = (2.6534040307116387e-09*((-0.1*math::exp(((-0.1) * math::log(math::abs(T)))))*rT));
    const Real sx21 = xn[3 * XS]*xn[5 * XS];
    const Real sdn3_x21 = (1.0*xn[5 * XS]);
    const Real sdn5_x21 = (xn[3 * XS]*1.0);
    const Real sx22 = sx20*sx21;
    const Real sdT_x22 = (sdT_x20*sx21);
    const Real sdn3_x22 = (sx20*sdn3_x21);
    const Real sdn5_x22 = (sx20*sdn5_x21);
    const Real a0_T_5 = a0_T_4 + sdT_x22;
    const Real a0_n3_2 = a0_n3_1 + sdn3_x22;
    const Real a0_n5_1 = a0_n5_0 + sdn5_x22;
    const Real a3_T_1 = a3_T_0 - sdT_x22;
    const Real a3_n3_1 = a3_n3_0 - sdn3_x22;
    const Real a3_n5_0 = -sdn5_x22;
    out[matrix_index(0, 5)] = a0_n5_1;
    const Real sx23 = xn[0 * XS]*xn[2 * XS];
    const Real sdn0_x23 = (1.0*xn[2 * XS]);
    const Real sdn2_x23 = (xn[0 * XS]*1.0);
    const Real sx25 = xn[0 * XS]*xn[12 * XS];
    const Real sdn0_x25 = (1.0*xn[12 * XS]);
    const Real sdn12_x25 = (xn[0 * XS]*1.0);
    const Real sx24 = 1.4000000000000001e-18*math::exp((0.92800000000000005)*math::log(math::abs(T)))*sx23*math::exp(-6.1728395061728397e-5*T);
    const Real sdT_x24 = ((((1.4e-18*((0.928*math::exp((0.928 * math::log(math::abs(T)))))*rT))*sx23)*math::exp(((-6.17283950617284e-05) * T))) + (((1.4e-18 * math::exp((0.928 * math::log(math::abs(T))))) * sx23)*(math::exp(((-6.17283950617284e-05) * T))*((-6.17283950617284e-05)*1.0))));
    const Real sdn0_x24 = (((1.4e-18 * math::exp((0.928 * math::log(math::abs(T)))))*sdn0_x23)*math::exp(((-6.17283950617284e-05) * T)));
    const Real sdn2_x24 = (((1.4e-18 * math::exp((0.928 * math::log(math::abs(T)))))*sdn2_x23)*math::exp(((-6.17283950617284e-05) * T)));
    const Real a0_T_6 = a0_T_5 - sdT_x24;
    const Real a0_n0_3 = a0_n0_2 - sdn0_x24;
    const Real a0_n2_1 = a0_n2_0 - sdn2_x24;
    const Real sx26 = 3.8571873359681582e-209*math::exp((43.933476326349997)*math::log(math::abs(T)))*sx25*math::exp(-5902.1601240760483*sx10 + 5825.9326359379538*sx11 - 3578.1439181805954*sx12 + 1242.7294446825149*sx13 - 186.35635455381879*sx14 - 1618.789587733125*sx8 + 3854.4033653120223*sx9);
    const Real sdT_x26 = ((((3.857187335968158e-209*((43.93347632635*math::exp((43.93347632635 * math::log(math::abs(T)))))*rT))*sx25)*math::exp(((((((((-5902.160124076048) * sx10) + (5825.932635937954 * sx11)) - (3578.1439181805954 * sx12)) + (1242.729444682515 * sx13)) - (186.3563545538188 * sx14)) - (1618.789587733125 * sx8)) + (3854.4033653120223 * sx9)))) + (((3.857187335968158e-209 * math::exp((43.93347632635 * math::log(math::abs(T))))) * sx25)*(math::exp(((((((((-5902.160124076048) * sx10) + (5825.932635937954 * sx11)) - (3578.1439181805954 * sx12)) + (1242.729444682515 * sx13)) - (186.3563545538188 * sx14)) - (1618.789587733125 * sx8)) + (3854.4033653120223 * sx9)))*((((((((-5902.160124076048)*sdT_x10) + (5825.932635937954*sdT_x11)) - (3578.1439181805954*sdT_x12)) + (1242.729444682515*sdT_x13)) - (186.3563545538188*sdT_x14)) - (1618.789587733125*sdT_x8)) + (3854.4033653120223*sdT_x9)))));
    const Real sdn0_x26 = (((3.857187335968158e-209 * math::exp((43.93347632635 * math::log(math::abs(T)))))*sdn0_x25)*math::exp(((((((((-5902.160124076048) * sx10) + (5825.932635937954 * sx11)) - (3578.1439181805954 * sx12)) + (1242.729444682515 * sx13)) - (186.3563545538188 * sx14)) - (1618.789587733125 * sx8)) + (3854.4033653120223 * sx9))));
    const Real sdn12_x26 = (((3.857187335968158e-209 * math::exp((43.93347632635 * math::log(math::abs(T)))))*sdn12_x25)*math::exp(((((((((-5902.160124076048) * sx10) + (5825.932635937954 * sx11)) - (3578.1439181805954 * sx12)) + (1242.729444682515 * sx13)) - (186.3563545538188 * sx14)) - (1618.789587733125 * sx8)) + (3854.4033653120223 * sx9))));
    const Real a0_T_7 = a0_T_6 + sdT_x26;
    const Real a0_n0_4 = a0_n0_3 + sdn0_x26;
    const Real a0_n12_0 = sdn12_x26;
    const Real sx27 = 3.7903999274394518e-18*math::exp((2.360852208681)*math::log(math::abs(T)))*xn[0 * XS]*xn[3 * XS]*math::exp(-258.18559308467115*sx10 + 846.15238706523724*sx11 - 1113.0879095147111*sx12 + 671.95094388835207*sx13 - 154.90262957142161*sx14 - 24.766609674457612*sx8 + 13.307984239358756*sx9);
    const Real sdT_x27 = (((((3.790399927439452e-18*((2.360852208681*math::exp((2.360852208681 * math::log(math::abs(T)))))*rT))*xn[0 * XS])*xn[3 * XS])*math::exp(((((((((-258.18559308467115) * sx10) + (846.1523870652372 * sx11)) - (1113.087909514711 * sx12)) + (671.9509438883521 * sx13)) - (154.9026295714216 * sx14)) - (24.766609674457612 * sx8)) + (13.307984239358756 * sx9)))) + ((((3.790399927439452e-18 * math::exp((2.360852208681 * math::log(math::abs(T))))) * xn[0 * XS]) * xn[3 * XS])*(math::exp(((((((((-258.18559308467115) * sx10) + (846.1523870652372 * sx11)) - (1113.087909514711 * sx12)) + (671.9509438883521 * sx13)) - (154.9026295714216 * sx14)) - (24.766609674457612 * sx8)) + (13.307984239358756 * sx9)))*((((((((-258.18559308467115)*sdT_x10) + (846.1523870652372*sdT_x11)) - (1113.087909514711*sdT_x12)) + (671.9509438883521*sdT_x13)) - (154.9026295714216*sdT_x14)) - (24.766609674457612*sdT_x8)) + (13.307984239358756*sdT_x9)))));
    const Real sdn0_x27 = ((((3.790399927439452e-18 * math::exp((2.360852208681 * math::log(math::abs(T)))))*1.0)*xn[3 * XS])*math::exp(((((((((-258.18559308467115) * sx10) + (846.1523870652372 * sx11)) - (1113.087909514711 * sx12)) + (671.9509438883521 * sx13)) - (154.9026295714216 * sx14)) - (24.766609674457612 * sx8)) + (13.307984239358756 * sx9))));
    const Real sdn3_x27 = ((((3.790399927439452e-18 * math::exp((2.360852208681 * math::log(math::abs(T))))) * xn[0 * XS])*1.0)*math::exp(((((((((-258.18559308467115) * sx10) + (846.1523870652372 * sx11)) - (1113.087909514711 * sx12)) + (671.9509438883521 * sx13)) - (154.9026295714216 * sx14)) - (24.766609674457612 * sx8)) + (13.307984239358756 * sx9))));
    const Real a0_T_8 = a0_T_7 + sdT_x27;
    const Real a0_n0_5 = a0_n0_4 + sdn0_x27;
    const Real a0_n3_3 = a0_n3_2 + sdn3_x27;
    const Real sx28 = math::sqrt(T);
    const Real sdT_x28 = (1.0/(2.0*math::sqrt(T)));
    const Real sx29 = 1.0/sx28;
    const Real sdT_x29 = (-(((1.0 / sx28)*sdT_x28)/sx28));
    const Real sx30 = xn[0 * XS]*sx29;
    const Real sdT_x30 = (xn[0 * XS]*sdT_x29);
    const Real sdn0_x30 = (1.0*sx29);
    const Real sx31 = 5.7884371785482823e-10*xn[11 * XS]*sx30*math::exp((-1.7524)*math::log(math::abs(0.00060040841663220993*sx28 + 1.0)))*math::exp((-0.24759999999999999)*math::log(math::abs(0.32668576019240059*sx28 + 1.0)));
    const Real sdT_x31 = ((((((5.788437178548282e-10 * xn[11 * XS])*sdT_x30)*math::exp(((-1.7524) * math::log(math::abs(((0.0006004084166322099 * sx28) + 1.0)))))) + (((5.788437178548282e-10 * xn[11 * XS]) * sx30)*(((-1.7524*math::exp(((-1.7524) * math::log(math::abs(((0.0006004084166322099 * sx28) + 1.0))))))*(0.0006004084166322099*sdT_x28))/((0.0006004084166322099 * sx28) + 1.0))))*math::exp(((-0.2476) * math::log(math::abs(((0.3266857601924006 * sx28) + 1.0)))))) + ((((5.788437178548282e-10 * xn[11 * XS]) * sx30) * math::exp(((-1.7524) * math::log(math::abs(((0.0006004084166322099 * sx28) + 1.0))))))*(((-0.2476*math::exp(((-0.2476) * math::log(math::abs(((0.3266857601924006 * sx28) + 1.0))))))*(0.3266857601924006*sdT_x28))/((0.3266857601924006 * sx28) + 1.0))));
    const Real sdn0_x31 = ((((5.788437178548282e-10 * xn[11 * XS])*sdn0_x30)*math::exp(((-1.7524) * math::log(math::abs(((0.0006004084166322099 * sx28) + 1.0))))))*math::exp(((-0.2476) * math::log(math::abs(((0.3266857601924006 * sx28) + 1.0))))));
    const Real sdn11_x31 = ((((5.788437178548282e-10*1.0)*sx30)*math::exp(((-1.7524) * math::log(math::abs(((0.0006004084166322099 * sx28) + 1.0))))))*math::exp(((-0.2476) * math::log(math::abs(((0.3266857601924006 * sx28) + 1.0))))));
    const Real a0_T_9 = a0_T_8 - sdT_x31;
    const Real a0_n0_6 = a0_n0_5 - sdn0_x31;
    const Real a0_n11_0 = -sdn11_x31;
    out[matrix_index(0, 11)] = a0_n11_0;
    const Real sx32 = 1.4981088130721367e-10*(math::exp((0.36470000000000002)*math::log(math::abs(T)))*(1.0/T));
    const Real sdT_x32 = (1.4981088130721367e-10*((-0.6353*(math::exp((0.36470000000000002)*math::log(math::abs(T)))*(1.0/T)))*rT));
    const Real sx34 = 1.0/T;
    const Real sdT_x34 = (-(((1.0 / T)*1.0)*rT));
    const Real sx33 = 8.6173430000000006e-5*T;
    const Real sdT_x33 = (8.617343e-05*1.0);
    const Real sx35 = -4.3524079114767552e-117*math::exp((23.915965629999999)*math::log(math::abs(T)))*xn[0 * XS]*xn[13 * XS]*math::exp(-4361.9927099007555*sx10 + 4879.7345146260486*sx11 - 3366.4639698826941*sx12 + 1300.3028484326148*sx13 - 214.82451513312137*sx14 - 941.91483008144996*sx8 + 2506.9866529060901*sx9) + sx25*rhs_specie_choice_2(T, sx32, sx33, sx34);
    const Real sdT_x35 = (((((((-4.352407911476755e-117)*((23.91596563*math::exp((23.91596563 * math::log(math::abs(T)))))*rT))*xn[0 * XS])*xn[13 * XS])*math::exp(((((((((-4361.9927099007555) * sx10) + (4879.734514626049 * sx11)) - (3366.463969882694 * sx12)) + (1300.3028484326148 * sx13)) - (214.82451513312137 * sx14)) - (941.91483008145 * sx8)) + (2506.98665290609 * sx9)))) + (((((-4.352407911476755e-117) * math::exp((23.91596563 * math::log(math::abs(T))))) * xn[0 * XS]) * xn[13 * XS])*(math::exp(((((((((-4361.9927099007555) * sx10) + (4879.734514626049 * sx11)) - (3366.463969882694 * sx12)) + (1300.3028484326148 * sx13)) - (214.82451513312137 * sx14)) - (941.91483008145 * sx8)) + (2506.98665290609 * sx9)))*((((((((-4361.9927099007555)*sdT_x10) + (4879.734514626049*sdT_x11)) - (3366.463969882694*sdT_x12)) + (1300.3028484326148*sdT_x13)) - (214.82451513312137*sdT_x14)) - (941.91483008145*sdT_x8)) + (2506.98665290609*sdT_x9))))) + (sx25*jacd_rhs_specie_choice_2_2(T, sx32, sx33, sx34, sdT_x32, sdT_x33, sdT_x34)));
    const Real sdn0_x35 = ((((((-4.352407911476755e-117) * math::exp((23.91596563 * math::log(math::abs(T)))))*1.0)*xn[13 * XS])*math::exp(((((((((-4361.9927099007555) * sx10) + (4879.734514626049 * sx11)) - (3366.463969882694 * sx12)) + (1300.3028484326148 * sx13)) - (214.82451513312137 * sx14)) - (941.91483008145 * sx8)) + (2506.98665290609 * sx9)))) + (sdn0_x25*rhs_specie_choice_2(T, sx32, sx33, sx34)));
    const Real sdn12_x35 = (sdn12_x25*rhs_specie_choice_2(T, sx32, sx33, sx34));
    const Real sdn13_x35 = (((((-4.352407911476755e-117) * math::exp((23.91596563 * math::log(math::abs(T))))) * xn[0 * XS])*1.0)*math::exp(((((((((-4361.9927099007555) * sx10) + (4879.734514626049 * sx11)) - (3366.463969882694 * sx12)) + (1300.3028484326148 * sx13)) - (214.82451513312137 * sx14)) - (941.91483008145 * sx8)) + (2506.98665290609 * sx9))));
    const Real a0_T_10 = a0_T_9 - sdT_x35;
    const Real a0_n0_7 = a0_n0_6 - sdn0_x35;
    const Real a0_n12_1 = a0_n12_0 - sdn12_x35;
    const Real a0_n13_0 = -sdn13_x35;
    out[matrix_index(0, 12)] = a0_n12_1;
    out[matrix_index(0, 13)] = a0_n13_0;
    const Real sx36 = -5.9082438637265071e-70*math::exp((13.536555999999999)*math::log(math::abs(T)))*sx23*math::exp(-2207.4643501257692*sx10 + 2500.8077583366976*sx11 - 1768.8867461266502*sx12 + 704.19926629500367*sx13 - 120.0438480494693*sx14 - 502.72883252679094*sx8 + 1281.477767828706*sx9) + xn[0 * XS]*xn[1 * XS]*rhs_specie_choice_3(T, sx10, sx11, sx12, sx13, sx14, sx15, sx32, sx33, sx8, sx9);
    const Real sdT_x36 = ((((((-5.908243863726507e-70)*((13.536556*math::exp((13.536556 * math::log(math::abs(T)))))*rT))*sx23)*math::exp(((((((((-2207.464350125769) * sx10) + (2500.8077583366976 * sx11)) - (1768.8867461266502 * sx12)) + (704.1992662950037 * sx13)) - (120.0438480494693 * sx14)) - (502.72883252679094 * sx8)) + (1281.477767828706 * sx9)))) + ((((-5.908243863726507e-70) * math::exp((13.536556 * math::log(math::abs(T))))) * sx23)*(math::exp(((((((((-2207.464350125769) * sx10) + (2500.8077583366976 * sx11)) - (1768.8867461266502 * sx12)) + (704.1992662950037 * sx13)) - (120.0438480494693 * sx14)) - (502.72883252679094 * sx8)) + (1281.477767828706 * sx9)))*((((((((-2207.464350125769)*sdT_x10) + (2500.8077583366976*sdT_x11)) - (1768.8867461266502*sdT_x12)) + (704.1992662950037*sdT_x13)) - (120.0438480494693*sdT_x14)) - (502.72883252679094*sdT_x8)) + (1281.477767828706*sdT_x9))))) + ((xn[0 * XS] * xn[1 * XS])*jacd_rhs_specie_choice_3_3(T, sx10, sx11, sx12, sx13, sx14, sx15, sx32, sx33, sx8, sx9, sdT_x10, sdT_x11, sdT_x12, sdT_x13, sdT_x14, sdT_x15, sdT_x32, sdT_x33, sdT_x8, sdT_x9)));
    const Real sdn0_x36 = (((((-5.908243863726507e-70) * math::exp((13.536556 * math::log(math::abs(T)))))*sdn0_x23)*math::exp(((((((((-2207.464350125769) * sx10) + (2500.8077583366976 * sx11)) - (1768.8867461266502 * sx12)) + (704.1992662950037 * sx13)) - (120.0438480494693 * sx14)) - (502.72883252679094 * sx8)) + (1281.477767828706 * sx9)))) + ((1.0*xn[1 * XS])*rhs_specie_choice_3(T, sx10, sx11, sx12, sx13, sx14, sx15, sx32, sx33, sx8, sx9)));
    const Real sdn1_x36 = ((xn[0 * XS]*1.0)*rhs_specie_choice_3(T, sx10, sx11, sx12, sx13, sx14, sx15, sx32, sx33, sx8, sx9));
    const Real sdn2_x36 = ((((-5.908243863726507e-70) * math::exp((13.536556 * math::log(math::abs(T)))))*sdn2_x23)*math::exp(((((((((-2207.464350125769) * sx10) + (2500.8077583366976 * sx11)) - (1768.8867461266502 * sx12)) + (704.1992662950037 * sx13)) - (120.0438480494693 * sx14)) - (502.72883252679094 * sx8)) + (1281.477767828706 * sx9))));
    const Real a0_T_11 = a0_T_10 - sdT_x36;
    const Real a0_n0_8 = a0_n0_7 - sdn0_x36;
    const Real a0_n1_1 = a0_n1_0 - sdn1_x36;
    const Real a0_n2_2 = a0_n2_1 - sdn2_x36;
    out[matrix_index(0, 1)] = a0_n1_1;
    const Real sx40 = sx16*(1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T))))/(0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0);
    const Real sdT_x40 = (((sx16*(((1.35e-09*((0.098493*math::exp((0.098493 * math::log(math::abs(T)))))*rT)) + (4.43502e-10*((0.5561*math::exp((0.5561 * math::log(math::abs(T)))))*rT))) + (3.7408500000000004e-16*((2.1826*math::exp((2.1826 * math::log(math::abs(T)))))*rT))))/((((0.006191 * math::exp((1.0461 * math::log(math::abs(T))))) + (8.9712e-11 * math::exp((3.0424 * math::log(math::abs(T)))))) + (3.2576e-14 * math::exp((3.7741 * math::log(math::abs(T)))))) + 1.0)) - ((((sx16 * (((1.35e-09 * math::exp((0.098493 * math::log(math::abs(T))))) + (4.43502e-10 * math::exp((0.5561 * math::log(math::abs(T)))))) + (3.7408500000000004e-16 * math::exp((2.1826 * math::log(math::abs(T))))))) / ((((0.006191 * math::exp((1.0461 * math::log(math::abs(T))))) + (8.9712e-11 * math::exp((3.0424 * math::log(math::abs(T)))))) + (3.2576e-14 * math::exp((3.7741 * math::log(math::abs(T)))))) + 1.0))*(((0.006191*((1.0461*math::exp((1.0461 * math::log(math::abs(T)))))*rT)) + (8.9712e-11*((3.0424*math::exp((3.0424 * math::log(math::abs(T)))))*rT))) + (3.2576e-14*((3.7741*math::exp((3.7741 * math::log(math::abs(T)))))*rT))))/((((0.006191 * math::exp((1.0461 * math::log(math::abs(T))))) + (8.9712e-11 * math::exp((3.0424 * math::log(math::abs(T)))))) + (3.2576e-14 * math::exp((3.7741 * math::log(math::abs(T)))))) + 1.0)));
    const Real sdn2_x40 = ((sdn2_x16*(((1.35e-09 * math::exp((0.098493 * math::log(math::abs(T))))) + (4.43502e-10 * math::exp((0.5561 * math::log(math::abs(T)))))) + (3.7408500000000004e-16 * math::exp((2.1826 * math::log(math::abs(T)))))))/((((0.006191 * math::exp((1.0461 * math::log(math::abs(T))))) + (8.9712e-11 * math::exp((3.0424 * math::log(math::abs(T)))))) + (3.2576e-14 * math::exp((3.7741 * math::log(math::abs(T)))))) + 1.0));
    const Real sdn3_x40 = ((sdn3_x16*(((1.35e-09 * math::exp((0.098493 * math::log(math::abs(T))))) + (4.43502e-10 * math::exp((0.5561 * math::log(math::abs(T)))))) + (3.7408500000000004e-16 * math::exp((2.1826 * math::log(math::abs(T)))))))/((((0.006191 * math::exp((1.0461 * math::log(math::abs(T))))) + (8.9712e-11 * math::exp((3.0424 * math::log(math::abs(T)))))) + (3.2576e-14 * math::exp((3.7741 * math::log(math::abs(T)))))) + 1.0));
    const Real sx37 = 7.1999999999999996e-8*xn[9 * XS]*sx30;
    const Real sdT_x37 = ((7.2e-08 * xn[9 * XS])*sdT_x30);
    const Real sdn0_x37 = ((7.2e-08 * xn[9 * XS])*sdn0_x30);
    const Real sdn9_x37 = ((7.2e-08*1.0)*sx30);
    const Real sx38 = xn[2 * XS]*xn[7 * XS];
    const Real sdn2_x38 = (1.0*xn[7 * XS]);
    const Real sdn7_x38 = (xn[2 * XS]*1.0);
    const Real sx39 = sx20*sx38;
    const Real sdT_x39 = (sdT_x20*sx38);
    const Real sdn2_x39 = (sx20*sdn2_x38);
    const Real sdn7_x39 = (sx20*sdn7_x38);
    const Real a7_T_1 = a7_T_0 - sdT_x39;
    const Real a7_n2_0 = -sdn2_x39;
    const Real a7_n7_0 = -sdn7_x39;
    const Real a10_T_0 = sdT_x39;
    const Real a10_n2_0 = sdn2_x39;
    const Real a10_n7_0 = sdn7_x39;
    out[matrix_index(10, 7)] = a10_n7_0;
    const Real sx41 = xn[0 * XS]*xn[8 * XS];
    const Real sdn0_x41 = (1.0*xn[8 * XS]);
    const Real sdn8_x41 = (xn[0 * XS]*1.0);
    const Real sx42 = 35.5*math::exp((-2.2799999999999998)*math::log(math::abs(T)))*sx41*math::exp(-46707.0*sx34);
    const Real sdT_x42 = ((((35.5*((-2.28*math::exp(((-2.28) * math::log(math::abs(T)))))*rT))*sx41)*math::exp(((-46707.0) * sx34))) + (((35.5 * math::exp(((-2.28) * math::log(math::abs(T))))) * sx41)*(math::exp(((-46707.0) * sx34))*((-46707.0)*sdT_x34))));
    const Real sdn0_x42 = (((35.5 * math::exp(((-2.28) * math::log(math::abs(T)))))*sdn0_x41)*math::exp(((-46707.0) * sx34)));
    const Real sdn8_x42 = (((35.5 * math::exp(((-2.28) * math::log(math::abs(T)))))*sdn8_x41)*math::exp(((-46707.0) * sx34)));
    const Real sx43 = sx37 - sx39 - sx40 + sx42;
    const Real sdT_x43 = (((sdT_x37 - sdT_x39) - sdT_x40) + sdT_x42);
    const Real sdn0_x43 = (sdn0_x37 + sdn0_x42);
    const Real sdn2_x43 = ((-sdn2_x39) - sdn2_x40);
    const Real sdn3_x43 = (-sdn3_x40);
    const Real sdn7_x43 = (-sdn7_x39);
    const Real sdn8_x43 = sdn8_x42;
    const Real sdn9_x43 = sdn9_x37;
    const Real a0_T_12 = a0_T_11 - sdT_x43;
    const Real a0_n0_9 = a0_n0_8 - sdn0_x43;
    const Real a0_n2_3 = a0_n2_2 - sdn2_x43;
    const Real a0_n3_4 = a0_n3_3 - sdn3_x43;
    const Real a0_n7_0 = -sdn7_x43;
    const Real a0_n8_0 = -sdn8_x43;
    const Real a0_n9_0 = -sdn9_x43;
    const Real a2_T_1 = a2_T_0 + sdT_x43;
    const Real a2_n0_1 = a2_n0_0 + sdn0_x43;
    const Real a2_n2_0 = sdn2_x43;
    const Real a2_n3_0 = sdn3_x43;
    const Real a2_n7_0 = sdn7_x43;
    const Real a2_n8_0 = sdn8_x43;
    const Real a2_n9_0 = sdn9_x43;
    out[matrix_index(0, 14)] = a0_T_12*dTde;
    out[matrix_index(0, 0)] = a0_n0_9;
    out[matrix_index(0, 2)] = a0_n2_3;
    out[matrix_index(0, 3)] = a0_n3_4;
    out[matrix_index(0, 7)] = a0_n7_0;
    out[matrix_index(0, 8)] = a0_n8_0;
    out[matrix_index(0, 9)] = a0_n9_0;
    const Real sx44 = M_LN10;
    const Real sx45 = 1.0/sx44;
    const Real sx46 = sx45*sx6;
    const Real sdT_x46 = (sx45*sdT_x6);
    const Real sx47 = math::exp((-0.12690000000000001*math::powi<-3>(math::abs(sx44))*((sx6)*(sx6)*(sx6)) + 1.1180000000000001*math::powi<-2>(math::abs(sx44))*((sx6)*(sx6)) - 1.5229999999999999*sx46 - 19.379999999999999)*math::log(math::abs(10.0)));
    const Real sdT_x47 = (math::exp((((((((-0.1269) * math::exp(((-3.0) * math::log(math::abs(sx44))))) * ((sx6 * sx6) * sx6)) + ((1.118 * math::exp(((-2.0) * math::log(math::abs(sx44))))) * (sx6 * sx6))) - (1.523 * sx46)) - 19.38) * math::log(math::abs(10.0))))*((((((-0.1269) * math::exp(((-3.0) * math::log(math::abs(sx44)))))*((((sdT_x6*sx6) + (sx6*sdT_x6))*sx6) + ((sx6 * sx6)*sdT_x6))) + ((1.118 * math::exp(((-2.0) * math::log(math::abs(sx44)))))*((sdT_x6*sx6) + (sx6*sdT_x6)))) - (1.523*sdT_x46))*math::log(math::abs(10.0))));
    const Real sx48 = xn[1 * XS]*xn[5 * XS];
    const Real sdn1_x48 = (1.0*xn[5 * XS]);
    const Real sdn5_x48 = (xn[1 * XS]*1.0);
    const Real sx49 = sx47*sx48;
    const Real sdT_x49 = (sdT_x47*sx48);
    const Real sdn1_x49 = (sx47*sdn1_x48);
    const Real sdn5_x49 = (sx47*sdn5_x48);
    const Real a1_T_0 = -sdT_x49;
    const Real a1_n1_0 = -sdn1_x49;
    const Real a1_n5_0 = -sdn5_x49;
    const Real sx50 = sx18*rhs_specie_choice_4(T, sx28, sx29);
    const Real sdT_x50 = (sx18*jacd_rhs_specie_choice_4_4(T, sx28, sx29, sdT_x28, sdT_x29));
    const Real sdn1_x50 = (sdn1_x18*rhs_specie_choice_4(T, sx28, sx29));
    const Real sdn3_x50 = (sdn3_x18*rhs_specie_choice_4(T, sx28, sx29));
    const Real a1_T_1 = a1_T_0 - sdT_x50;
    const Real a1_n1_1 = a1_n1_0 - sdn1_x50;
    const Real a1_n3_0 = -sdn3_x50;
    const Real a2_T_2 = a2_T_1 + (2.0*sdT_x50);
    const Real a2_n1_0 = (2.0*sdn1_x50);
    const Real a2_n3_1 = a2_n3_0 + (2.0*sdn3_x50);
    const Real a3_T_2 = a3_T_1 - sdT_x50;
    const Real a3_n1_1 = a3_n1_0 - sdn1_x50;
    const Real a3_n3_2 = a3_n3_1 - sdn3_x50;
    out[matrix_index(3, 1)] = a3_n1_1;
    const Real sx51 = 1.0000000000000001e-9*xn[1 * XS]*xn[10 * XS]*math::exp(-457.0*sx34);
    const Real sdT_x51 = (((1e-09 * xn[1 * XS]) * xn[10 * XS])*(math::exp(((-457.0) * sx34))*((-457.0)*sdT_x34)));
    const Real sdn1_x51 = (((1e-09*1.0)*xn[10 * XS])*math::exp(((-457.0) * sx34)));
    const Real sdn10_x51 = (((1e-09 * xn[1 * XS])*1.0)*math::exp(((-457.0) * sx34)));
    const Real a1_T_2 = a1_T_1 - sdT_x51;
    const Real a1_n1_2 = a1_n1_1 - sdn1_x51;
    const Real a1_n10_0 = -sdn10_x51;
    const Real a8_T_0 = sdT_x51;
    const Real a8_n1_0 = sdn1_x51;
    const Real a8_n10_0 = sdn10_x51;
    out[matrix_index(1, 10)] = a1_n10_0;
    const Real sx52 = ((sx6)*(sx6));
    const Real sdT_x52 = ((sdT_x6*sx6) + (sx6*sdT_x6));
    const Real sx53 = math::powi<-2>(math::abs(sx44));
    const Real sx54 = sx52*sx53;
    const Real sdT_x54 = (sdT_x52*sx53);
    const Real sx56 = ((sx6)*(sx6)*(sx6));
    const Real sdT_x56 = ((((sdT_x6*sx6) + (sx6*sdT_x6))*sx6) + ((sx6 * sx6)*sdT_x6));
    const Real sx55 = 8.4600000000000008e-10*sx46 - 1.3700000000000002e-10*sx54 + 4.1700000000000001e-10;
    const Real sdT_x55 = ((8.460000000000001e-10*sdT_x46) - (1.3700000000000002e-10*sdT_x54));
    const Real a1_T_3 = a1_T_2 + ((xn[4 * XS] * xn[8 * XS])*sdT_x55);
    const Real a1_n4_0 = ((1.0*xn[8 * XS])*sx55);
    const Real a1_n8_0 = ((xn[4 * XS]*1.0)*sx55);
    const Real sx57 = sx56/((sx44)*(sx44)*(sx44));
    const Real sdT_x57 = (sdT_x56/((sx44 * sx44) * sx44));
    const Real sx58 = xn[1 * XS]*xn[2 * XS]*rhs_specie_choice_5(T, sx46, sx52, sx53, sx57);
    const Real sdT_x58 = ((xn[1 * XS] * xn[2 * XS])*jacd_rhs_specie_choice_5_5(T, sx46, sx52, sx53, sx57, sdT_x46, sdT_x52, sdT_x57));
    const Real sdn1_x58 = ((1.0*xn[2 * XS])*rhs_specie_choice_5(T, sx46, sx52, sx53, sx57));
    const Real sdn2_x58 = ((xn[1 * XS]*1.0)*rhs_specie_choice_5(T, sx46, sx52, sx53, sx57));
    const Real a2_T_3 = a2_T_2 - sdT_x58;
    const Real a2_n1_1 = a2_n1_0 - sdn1_x58;
    const Real a2_n2_1 = a2_n2_0 - sdn2_x58;
    const Real sx59 = 6.0e-10*xn[2 * XS]*xn[6 * XS];
    const Real sdn2_x59 = ((6e-10*1.0)*xn[6 * XS]);
    const Real sdn6_x59 = ((6e-10 * xn[2 * XS])*1.0);
    const Real a8_n2_0 = sdn2_x59;
    const Real a8_n6_0 = sdn6_x59;
    const Real sx60 = ((((sx6)*(sx6)))*(((sx6)*(sx6))));
    const Real sdT_x60 = ((((sdT_x6*sx6) + (sx6*sdT_x6))*(sx6 * sx6)) + ((sx6 * sx6)*((sdT_x6*sx6) + (sx6*sdT_x6))));
    const Real sx61 = ((sx6)*(sx6)*(sx6)*(sx6)*(sx6));
    const Real sdT_x61 = ((((((((sdT_x6*sx6) + (sx6*sdT_x6))*sx6) + ((sx6 * sx6)*sdT_x6))*sx6) + (((sx6 * sx6) * sx6)*sdT_x6))*sx6) + ((((sx6 * sx6) * sx6) * sx6)*sdT_x6));
    const Real sx62 = xn[1 * XS]*xn[8 * XS]*rhs_specie_choice_6(T, sx34, sx52, sx56, sx6, sx60, sx61);
    const Real sdT_x62 = ((xn[1 * XS] * xn[8 * XS])*jacd_rhs_specie_choice_6_6(T, sx34, sx52, sx56, sx6, sx60, sx61, sdT_x34, sdT_x52, sdT_x56, sdT_x6, sdT_x60, sdT_x61));
    const Real sdn1_x62 = ((1.0*xn[8 * XS])*rhs_specie_choice_6(T, sx34, sx52, sx56, sx6, sx60, sx61));
    const Real sdn8_x62 = ((xn[1 * XS]*1.0)*rhs_specie_choice_6(T, sx34, sx52, sx56, sx6, sx60, sx61));
    const Real a8_T_1 = a8_T_0 - sdT_x62;
    const Real a8_n1_1 = a8_n1_0 - sdn1_x62;
    const Real a8_n8_0 = -sdn8_x62;
    const Real sx63 = -sx59 + sx62;
    const Real sdT_x63 = sdT_x62;
    const Real sdn1_x63 = sdn1_x62;
    const Real sdn2_x63 = (-sdn2_x59);
    const Real sdn6_x63 = (-sdn6_x59);
    const Real sdn8_x63 = sdn8_x62;
    const Real a2_T_4 = a2_T_3 + sdT_x63;
    const Real a2_n1_2 = a2_n1_1 + sdn1_x63;
    const Real a2_n2_2 = a2_n2_1 + sdn2_x63;
    const Real a2_n6_1 = a2_n6_0 + sdn6_x63;
    const Real a2_n8_1 = a2_n8_0 + sdn8_x63;
    const Real sx64 = sx19 + sx58 + sx63;
    const Real sdT_x64 = ((sdT_x19 + sdT_x58) + sdT_x63);
    const Real sdn1_x64 = ((sdn1_x19 + sdn1_x58) + sdn1_x63);
    const Real sdn2_x64 = (sdn2_x58 + sdn2_x63);
    const Real sdn3_x64 = sdn3_x19;
    const Real sdn6_x64 = sdn6_x63;
    const Real sdn8_x64 = sdn8_x63;
    const Real a1_T_4 = a1_T_3 - sdT_x64;
    const Real a1_n1_3 = a1_n1_2 - sdn1_x64;
    const Real a1_n2_0 = -sdn2_x64;
    const Real a1_n3_1 = a1_n3_0 - sdn3_x64;
    const Real a1_n6_0 = -sdn6_x64;
    const Real a1_n8_1 = a1_n8_0 - sdn8_x64;
    const Real a6_T_1 = a6_T_0 + sdT_x64;
    const Real a6_n1_0 = sdn1_x64;
    const Real a6_n2_0 = sdn2_x64;
    const Real a6_n3_0 = sdn3_x64;
    const Real a6_n6_1 = a6_n6_0 + sdn6_x64;
    const Real a6_n8_0 = sdn8_x64;
    out[matrix_index(1, 8)] = a1_n8_1;
    out[matrix_index(1, 3)] = a1_n3_1;
    out[matrix_index(1, 6)] = a1_n6_0;
    out[matrix_index(6, 1)] = a6_n1_0;
    out[matrix_index(6, 2)] = a6_n2_0;
    out[matrix_index(6, 8)] = a6_n8_0;
    const Real sx65 = 6.3999999999999996e-10*xn[2 * XS]*xn[9 * XS];
    const Real sdn2_x65 = ((6.4e-10*1.0)*xn[9 * XS]);
    const Real sdn9_x65 = ((6.4e-10 * xn[2 * XS])*1.0);
    const Real a10_n2_1 = a10_n2_0 + sdn2_x65;
    const Real a10_n9_0 = sdn9_x65;
    out[matrix_index(10, 9)] = a10_n9_0;
    const Real sx66 = -sx65;
    const Real sdn2_x66 = (-sdn2_x65);
    const Real sdn9_x66 = (-sdn9_x65);
    const Real a9_n2_0 = sdn2_x66;
    const Real a9_n9_0 = sdn9_x66;
    const Real sx67 = math::exp((-0.5)*math::log(math::abs(T)));
    const Real sdT_x67 = ((-0.5*math::exp(((-0.5) * math::log(math::abs(T)))))*rT);
    const Real a5_T_2 = a5_T_1 + (((1.9745379206285203e-06 * xn[4 * XS]) * xn[7 * XS])*sdT_x67);
    const Real a5_n4_1 = a5_n4_0 + (((1.9745379206285203e-06*1.0)*xn[7 * XS])*sx67);
    const Real a5_n7_0 = (((1.9745379206285203e-06 * xn[4 * XS])*1.0)*sx67);
    const Real sx68 = xn[7 * XS]*sx67;
    const Real sdT_x68 = (xn[7 * XS]*sdT_x67);
    const Real sdn7_x68 = (1.0*sx67);
    const Real sx69 = 7.9674337148168363e-7*xn[1 * XS]*sx68;
    const Real sdT_x69 = ((7.967433714816836e-07 * xn[1 * XS])*sdT_x68);
    const Real sdn1_x69 = ((7.967433714816836e-07*1.0)*sx68);
    const Real sdn7_x69 = ((7.967433714816836e-07 * xn[1 * XS])*sdn7_x68);
    const Real a5_T_3 = a5_T_2 + sdT_x69;
    const Real a5_n1_0 = sdn1_x69;
    const Real a5_n7_1 = a5_n7_0 + sdn7_x69;
    const Real a7_T_2 = a7_T_1 - sdT_x69;
    const Real a7_n1_0 = -sdn1_x69;
    const Real a7_n7_1 = a7_n7_0 - sdn7_x69;
    out[matrix_index(7, 1)] = a7_n1_0;
    const Real sx70 = xn[1 * XS]*xn[13 * XS]*rhs_specie_choice_7(T, sx0, sx34, sx4);
    const Real sdT_x70 = ((xn[1 * XS] * xn[13 * XS])*jacd_rhs_specie_choice_7_7(T, sx0, sx34, sx4, sdT_x0, sdT_x34));
    const Real sdn1_x70 = ((1.0*xn[13 * XS])*rhs_specie_choice_7(T, sx0, sx34, sx4));
    const Real sdn13_x70 = ((xn[1 * XS]*1.0)*rhs_specie_choice_7(T, sx0, sx34, sx4));
    const Real sx71 = math::exp((0.25)*math::log(math::abs(T)));
    const Real sdT_x71 = ((0.25*math::exp((0.25 * math::log(math::abs(T)))))*rT);
    const Real sx72 = 2.8833736969617052e-16*xn[12 * XS]*xn[2 * XS]*sx71;
    const Real sdT_x72 = (((2.883373696961705e-16 * xn[12 * XS]) * xn[2 * XS])*sdT_x71);
    const Real sdn2_x72 = (((2.883373696961705e-16 * xn[12 * XS])*1.0)*sx71);
    const Real sdn12_x72 = (((2.883373696961705e-16*1.0)*xn[2 * XS])*sx71);
    const Real sx73 = T >= 50.0;
    const Real sx75 = xn[2 * XS]*xn[4 * XS];
    const Real sdn2_x75 = (1.0*xn[4 * XS]);
    const Real sdn4_x75 = (xn[2 * XS]*1.0);
    const Real sx74 = sx48*rhs_specie_choice_8(T, sx34, sx73);
    const Real sdT_x74 = (sx48*jacd_rhs_specie_choice_8_8(T, sx34, sx73, sdT_x34));
    const Real sdn1_x74 = (sdn1_x48*rhs_specie_choice_8(T, sx34, sx73));
    const Real sdn5_x74 = (sdn5_x48*rhs_specie_choice_8(T, sx34, sx73));
    const Real a4_T_1 = a4_T_0 + sdT_x74;
    const Real a4_n1_0 = sdn1_x74;
    const Real a4_n5_0 = sdn5_x74;
    out[matrix_index(4, 5)] = a4_n5_0;
    const Real sx76 = sx75*rhs_specie_choice_9(T, sx34, sx73);
    const Real sdT_x76 = (sx75*jacd_rhs_specie_choice_9_9(T, sx34, sx73, sdT_x34));
    const Real sdn2_x76 = (sdn2_x75*rhs_specie_choice_9(T, sx34, sx73));
    const Real sdn4_x76 = (sdn4_x75*rhs_specie_choice_9(T, sx34, sx73));
    const Real a4_T_2 = a4_T_1 - sdT_x76;
    const Real a4_n2_0 = -sdn2_x76;
    const Real a4_n4_1 = a4_n4_0 - sdn4_x76;
    const Real sx77 = sx74 - sx76;
    const Real sdT_x77 = (sdT_x74 - sdT_x76);
    const Real sdn1_x77 = sdn1_x74;
    const Real sdn2_x77 = (-sdn2_x76);
    const Real sdn4_x77 = (-sdn4_x76);
    const Real sdn5_x77 = sdn5_x74;
    const Real a5_T_4 = a5_T_3 - sdT_x77;
    const Real a5_n1_1 = a5_n1_0 - sdn1_x77;
    const Real a5_n2_0 = -sdn2_x77;
    const Real a5_n4_2 = a5_n4_1 - sdn4_x77;
    const Real a5_n5_1 = a5_n5_0 - sdn5_x77;
    out[matrix_index(5, 4)] = a5_n4_2;
    const Real sx78 = sx36 + sx66 + sx69 + sx70 - sx72 + sx77;
    const Real sdT_x78 = ((((sdT_x36 + sdT_x69) + sdT_x70) - sdT_x72) + sdT_x77);
    const Real sdn0_x78 = sdn0_x36;
    const Real sdn1_x78 = (((sdn1_x36 + sdn1_x69) + sdn1_x70) + sdn1_x77);
    const Real sdn2_x78 = (((sdn2_x36 + sdn2_x66) - sdn2_x72) + sdn2_x77);
    const Real sdn4_x78 = sdn4_x77;
    const Real sdn5_x78 = sdn5_x77;
    const Real sdn7_x78 = sdn7_x69;
    const Real sdn9_x78 = sdn9_x66;
    const Real sdn12_x78 = (-sdn12_x72);
    const Real sdn13_x78 = sdn13_x70;
    const Real a1_T_5 = a1_T_4 - sdT_x78;
    const Real a1_n0_0 = -sdn0_x78;
    const Real a1_n1_4 = a1_n1_3 - sdn1_x78;
    const Real a1_n2_1 = a1_n2_0 - sdn2_x78;
    const Real a1_n4_1 = a1_n4_0 - sdn4_x78;
    const Real a1_n5_1 = a1_n5_0 - sdn5_x78;
    const Real a1_n7_0 = -sdn7_x78;
    const Real a1_n9_0 = -sdn9_x78;
    const Real a1_n12_0 = -sdn12_x78;
    const Real a1_n13_0 = -sdn13_x78;
    const Real a2_T_5 = a2_T_4 + sdT_x78;
    const Real a2_n0_2 = a2_n0_1 + sdn0_x78;
    const Real a2_n1_3 = a2_n1_2 + sdn1_x78;
    const Real a2_n2_3 = a2_n2_2 + sdn2_x78;
    const Real a2_n4_0 = sdn4_x78;
    const Real a2_n5_0 = sdn5_x78;
    const Real a2_n7_1 = a2_n7_0 + sdn7_x78;
    const Real a2_n9_1 = a2_n9_0 + sdn9_x78;
    const Real a2_n12_0 = sdn12_x78;
    const Real a2_n13_0 = sdn13_x78;
    out[matrix_index(1, 14)] = a1_T_5*dTde;
    out[matrix_index(1, 4)] = a1_n4_1;
    out[matrix_index(1, 1)] = a1_n1_4;
    out[matrix_index(1, 5)] = a1_n5_1;
    out[matrix_index(1, 2)] = a1_n2_1;
    out[matrix_index(1, 0)] = a1_n0_0;
    out[matrix_index(1, 7)] = a1_n7_0;
    out[matrix_index(1, 9)] = a1_n9_0;
    out[matrix_index(1, 12)] = a1_n12_0;
    out[matrix_index(1, 13)] = a1_n13_0;
    out[matrix_index(2, 12)] = a2_n12_0;
    out[matrix_index(2, 13)] = a2_n13_0;
    const Real sx79 = 4.9999999999999996e-6*xn[3 * XS]*xn[6 * XS]*sx29;
    const Real sdT_x79 = (((4.9999999999999996e-06 * xn[3 * XS]) * xn[6 * XS])*sdT_x29);
    const Real sdn3_x79 = (((4.9999999999999996e-06*1.0)*xn[6 * XS])*sx29);
    const Real sdn6_x79 = (((4.9999999999999996e-06 * xn[3 * XS])*1.0)*sx29);
    const Real a2_T_6 = a2_T_5 + sdT_x79;
    const Real a2_n3_2 = a2_n3_1 + sdn3_x79;
    const Real a2_n6_2 = a2_n6_1 + sdn6_x79;
    const Real a6_T_2 = a6_T_1 - sdT_x79;
    const Real a6_n3_1 = a6_n3_0 - sdn3_x79;
    const Real a6_n6_2 = a6_n6_1 - sdn6_x79;
    out[matrix_index(6, 14)] = a6_T_2*dTde;
    out[matrix_index(6, 6)] = a6_n6_2;
    out[matrix_index(6, 3)] = a6_n3_1;
    const Real sx80 = ((xn[2 * XS])*(xn[2 * XS])*(xn[2 * XS]));
    const Real sdn2_x80 = ((((1.0*xn[2 * XS]) + (xn[2 * XS]*1.0))*xn[2 * XS]) + ((xn[2 * XS] * xn[2 * XS])*1.0));
    const Real sx81 = 1.0/sx71;
    const Real sdT_x81 = (-(((1.0 / sx71)*sdT_x71)/sx71));
    const Real a2_T_7 = a2_T_6 + (sx80*(((-6.0000000000000005e-31)*sdT_x67) - (1.8e-31*sdT_x81)));
    const Real a2_n2_4 = a2_n2_3 + (sdn2_x80*(((-6.0000000000000005e-31) * sx67) - (1.8e-31 * sx81)));
    const Real sx82 = 1.0e-25*xn[2 * XS]*xn[5 * XS];
    const Real sdn2_x82 = ((1e-25*1.0)*xn[5 * XS]);
    const Real sdn5_x82 = ((1e-25 * xn[2 * XS])*1.0);
    const Real a2_n2_5 = a2_n2_4 - sdn2_x82;
    const Real a2_n5_1 = a2_n5_0 - sdn5_x82;
    const Real sx83 = xn[1 * XS] + xn[10 * XS] + xn[2 * XS] + xn[3 * XS] + 2.0*xn[6 * XS] + 2.0*xn[8 * XS] + xn[9 * XS];
    const Real sdn1_x83 = 1.0;
    const Real sdn2_x83 = 1.0;
    const Real sdn3_x83 = 1.0;
    const Real sdn6_x83 = (2.0*1.0);
    const Real sdn8_x83 = (2.0*1.0);
    const Real sdn9_x83 = 1.0;
    const Real sdn10_x83 = 1.0;
    const Real sx85 = math::exp(-0.0022727272727272726*T);
    const Real sdT_x85 = (math::exp(((-0.0022727272727272726) * T))*((-0.0022727272727272726)*1.0));
    const Real sx84 = -133.82830000000001*sx34 - 4.8909149999999997*sx45*sx6 + 0.47490300000000002*sx54;
    const Real sdT_x84 = ((((-133.8283)*sdT_x34) - ((4.890915 * sx45)*sdT_x6)) + (0.474903*sdT_x54));
    const Real sx86 = math::exp(-0.00054054054054054055*T);
    const Real sdT_x86 = (math::exp(((-0.0005405405405405405) * T))*((-0.0005405405405405405)*1.0));
    const Real sx87 = -2.0563129999999998*sx85 + 0.58640729999999996*sx86 + 0.82274429999999998;
    const Real sdT_x87 = (((-2.056313)*sdT_x85) + (0.5864073*sdT_x86));
    const Real sx88 = -69.700860000000006*sx45*math::log(40870.379999999997*sx34 + 1.0) + 4.6331670000000003*sx57;
    const Real sdT_x88 = ((((-69.70086) * sx45)*((40870.38*sdT_x34)/((40870.38 * sx34) + 1.0))) + (4.633167*sdT_x57));
    const Real sx89 = math::exp((-23705.700000000001*sx34 - 2080.4099999999999*sx34/(math::exp((sx87)*math::log(math::abs(math::exp((-sx84 - 13.656822)*math::log(math::abs(10.0)))*sx83))) + 1.0) - 68.422430000000006*sx46 + 43.20243*sx52*sx53 - sx88 - 178.4239 - (19.734269999999999*sx45*math::log(16780.950000000001*sx34 + 1.0) - 14.509090000000008*sx46 + 37.886913*sx52*sx53 - sx88 - 307.31920000000002)/(math::exp((sx87)*math::log(math::abs(math::exp((-sx84 - 14.82123)*math::log(math::abs(10.0)))*sx83))) + 1.0))*math::log(math::abs(10.0)));
    const Real sdT_x89 = (math::exp((((((((((-23705.7) * sx34) - ((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) - (68.42243 * sx46)) + ((43.20243 * sx52) * sx53)) - sx88) - 178.4239) - (((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*((((((((-23705.7)*sdT_x34) - (((2080.41*sdT_x34)/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)) - ((((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))*((sdT_x87*math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))) + (sx87*(((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0))))*((-sdT_x84)*math::log(math::abs(10.0))))*sx83)/(math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (68.42243*sdT_x46)) + ((43.20243*sdT_x52)*sx53)) - sdT_x88) - (((((((19.73427 * sx45)*((16780.95*sdT_x34)/((16780.95 * sx34) + 1.0))) - (14.509090000000008*sdT_x46)) + ((37.886913*sdT_x52)*sx53)) - sdT_x88)/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)) - (((((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))*((sdT_x87*math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))) + (sx87*(((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0))))*((-sdT_x84)*math::log(math::abs(10.0))))*sx83)/(math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn1_x89 = (math::exp((((((((((-23705.7) * sx34) - ((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) - (68.42243 * sx46)) + ((43.20243 * sx52) * sx53)) - sx88) - 178.4239) - (((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0))))*sdn1_x83)/(math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0))))*sdn1_x83)/(math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn2_x89 = (math::exp((((((((((-23705.7) * sx34) - ((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) - (68.42243 * sx46)) + ((43.20243 * sx52) * sx53)) - sx88) - 178.4239) - (((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0))))*sdn2_x83)/(math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0))))*sdn2_x83)/(math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn3_x89 = (math::exp((((((((((-23705.7) * sx34) - ((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) - (68.42243 * sx46)) + ((43.20243 * sx52) * sx53)) - sx88) - 178.4239) - (((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0))))*sdn3_x83)/(math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0))))*sdn3_x83)/(math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn6_x89 = (math::exp((((((((((-23705.7) * sx34) - ((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) - (68.42243 * sx46)) + ((43.20243 * sx52) * sx53)) - sx88) - 178.4239) - (((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0))))*sdn6_x83)/(math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0))))*sdn6_x83)/(math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn8_x89 = (math::exp((((((((((-23705.7) * sx34) - ((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) - (68.42243 * sx46)) + ((43.20243 * sx52) * sx53)) - sx88) - 178.4239) - (((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0))))*sdn8_x83)/(math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0))))*sdn8_x83)/(math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn9_x89 = (math::exp((((((((((-23705.7) * sx34) - ((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) - (68.42243 * sx46)) + ((43.20243 * sx52) * sx53)) - sx88) - 178.4239) - (((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0))))*sdn9_x83)/(math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0))))*sdn9_x83)/(math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn10_x89 = (math::exp((((((((((-23705.7) * sx34) - ((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) - (68.42243 * sx46)) + ((43.20243 * sx52) * sx53)) - sx88) - 178.4239) - (((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((2080.41 * sx34) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0))))*sdn10_x83)/(math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 13.656822) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((19.73427 * sx45) * math::log(((16780.95 * sx34) + 1.0))) - (14.509090000000008 * sx46)) + ((37.886913 * sx52) * sx53)) - sx88) - 307.3192) / (math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))*(sx87*((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0))))*sdn10_x83)/(math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx87 * math::log(math::abs((math::exp((((-sx84) - 14.82123) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sx90 = 743.05999999999995*sx34 - 2.4640089999999999*sx45*sx6 + 0.19859550000000001*sx54;
    const Real sdT_x90 = (((743.06*sdT_x34) - ((2.464009 * sx45)*sdT_x6)) + (0.1985955*sdT_x54));
    const Real sx91 = 2.9375070000000001*sx85 + 0.23588480000000001*sx86 + 0.75022860000000002;
    const Real sdT_x91 = ((2.937507*sdT_x85) + (0.2358848*sdT_x86));
    const Real sx92 = -21.360939999999999*sx45*math::log(27535.310000000001*sx34 + 1.0) + 0.25820969999999999*sx57;
    const Real sdT_x92 = ((((-21.36094) * sx45)*((27535.31*sdT_x34)/((27535.31 * sx34) + 1.0))) + (0.2582097*sdT_x57));
    const Real sx93 = math::exp((-21467.790000000001*sx34 - 1657.4099999999999*sx34/(math::exp((sx91)*math::log(math::abs(math::exp((-sx90 - 8.1313220000000008)*math::log(math::abs(10.0)))*sx83))) + 1.0) + 42.707410000000003*sx45*sx6 - 2.0273650000000001*sx54 - sx92 - 142.7664 - (70.138370000000009*sx45*sx6 + 11.28215*sx45*math::log(14254.549999999999*sx34 + 1.0) - 4.7035149999999994*sx54 - sx92 - 203.11568)/(math::exp((sx91)*math::log(math::abs(math::exp((-sx90 - 9.3055640000000004)*math::log(math::abs(10.0)))*sx83))) + 1.0))*math::log(math::abs(10.0)));
    const Real sdT_x93 = (math::exp((((((((((-21467.79) * sx34) - ((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) + ((42.70741 * sx45) * sx6)) - (2.027365 * sx54)) - sx92) - 142.7664) - (((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*((((((((-21467.79)*sdT_x34) - (((1657.4099999999999*sdT_x34)/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)) - ((((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))*((sdT_x91*math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))) + (sx91*(((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0))))*((-sdT_x90)*math::log(math::abs(10.0))))*sx83)/(math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) + ((42.70741 * sx45)*sdT_x6)) - (2.027365*sdT_x54)) - sdT_x92) - (((((((70.13837000000001 * sx45)*sdT_x6) + ((11.28215 * sx45)*((14254.55*sdT_x34)/((14254.55 * sx34) + 1.0)))) - (4.7035149999999994*sdT_x54)) - sdT_x92)/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)) - (((((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))*((sdT_x91*math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))) + (sx91*(((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0))))*((-sdT_x90)*math::log(math::abs(10.0))))*sx83)/(math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn1_x93 = (math::exp((((((((((-21467.79) * sx34) - ((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) + ((42.70741 * sx45) * sx6)) - (2.027365 * sx54)) - sx92) - 142.7664) - (((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0))))*sdn1_x83)/(math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0))))*sdn1_x83)/(math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn2_x93 = (math::exp((((((((((-21467.79) * sx34) - ((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) + ((42.70741 * sx45) * sx6)) - (2.027365 * sx54)) - sx92) - 142.7664) - (((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0))))*sdn2_x83)/(math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0))))*sdn2_x83)/(math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn3_x93 = (math::exp((((((((((-21467.79) * sx34) - ((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) + ((42.70741 * sx45) * sx6)) - (2.027365 * sx54)) - sx92) - 142.7664) - (((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0))))*sdn3_x83)/(math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0))))*sdn3_x83)/(math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn6_x93 = (math::exp((((((((((-21467.79) * sx34) - ((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) + ((42.70741 * sx45) * sx6)) - (2.027365 * sx54)) - sx92) - 142.7664) - (((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0))))*sdn6_x83)/(math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0))))*sdn6_x83)/(math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn8_x93 = (math::exp((((((((((-21467.79) * sx34) - ((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) + ((42.70741 * sx45) * sx6)) - (2.027365 * sx54)) - sx92) - 142.7664) - (((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0))))*sdn8_x83)/(math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0))))*sdn8_x83)/(math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn9_x93 = (math::exp((((((((((-21467.79) * sx34) - ((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) + ((42.70741 * sx45) * sx6)) - (2.027365 * sx54)) - sx92) - 142.7664) - (((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0))))*sdn9_x83)/(math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0))))*sdn9_x83)/(math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sdn10_x93 = (math::exp((((((((((-21467.79) * sx34) - ((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) + ((42.70741 * sx45) * sx6)) - (2.027365 * sx54)) - sx92) - 142.7664) - (((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))) * math::log(math::abs(10.0))))*(((-(-((((1657.4099999999999 * sx34) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0))))*sdn10_x83)/(math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 8.131322) * math::log(math::abs(10.0)))) * sx83))))) + 1.0)))) - (-(((((((((70.13837000000001 * sx45) * sx6) + ((11.28215 * sx45) * math::log(((14254.55 * sx34) + 1.0)))) - (4.7035149999999994 * sx54)) - sx92) - 203.11568) / (math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))*(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))*(sx91*((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0))))*sdn10_x83)/(math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83)))))/(math::exp((sx91 * math::log(math::abs((math::exp((((-sx90) - 9.305564) * math::log(math::abs(10.0)))) * sx83))))) + 1.0))))*math::log(math::abs(10.0))));
    const Real sx95 = 5.0000000000000004e-32*sx67 + 1.5e-32*sx81;
    const Real sdT_x95 = ((5e-32*sdT_x67) + (1.5e-32*sdT_x81));
    const Real sx94 = xn[2 * XS]*xn[8 * XS];
    const Real sdn2_x94 = (1.0*xn[8 * XS]);
    const Real sdn8_x94 = (xn[2 * XS]*1.0);
    const Real a2_T_8 = a2_T_7 + (sx94*((3.0*sdT_x89) + (3.0*sdT_x93)));
    const Real a2_n1_4 = a2_n1_3 + (sx94*((3.0*sdn1_x89) + (3.0*sdn1_x93)));
    const Real a2_n2_6 = a2_n2_5 + ((sdn2_x94*((3.0 * sx89) + (3.0 * sx93))) + (sx94*((3.0*sdn2_x89) + (3.0*sdn2_x93))));
    const Real a2_n3_3 = a2_n3_2 + (sx94*((3.0*sdn3_x89) + (3.0*sdn3_x93)));
    const Real a2_n6_3 = a2_n6_2 + (sx94*((3.0*sdn6_x89) + (3.0*sdn6_x93)));
    const Real a2_n8_2 = a2_n8_1 + ((sdn8_x94*((3.0 * sx89) + (3.0 * sx93))) + (sx94*((3.0*sdn8_x89) + (3.0*sdn8_x93))));
    const Real a2_n9_2 = a2_n9_1 + (sx94*((3.0*sdn9_x89) + (3.0*sdn9_x93)));
    const Real a2_n10_0 = (sx94*((3.0*sdn10_x89) + (3.0*sdn10_x93)));
    const Real sx96 = ((xn[2 * XS])*(xn[2 * XS]))*xn[8 * XS];
    const Real sdn2_x96 = (((1.0*xn[2 * XS]) + (xn[2 * XS]*1.0))*xn[8 * XS]);
    const Real sdn8_x96 = ((xn[2 * XS] * xn[2 * XS])*1.0);
    const Real a2_T_9 = a2_T_8 - (sdT_x95*sx96);
    const Real a2_n2_7 = a2_n2_6 - (sx95*sdn2_x96);
    const Real a2_n8_3 = a2_n8_2 - (sx95*sdn8_x96);
    const Real a8_T_2 = a8_T_1 + (sdT_x95*sx96);
    const Real a8_n2_1 = a8_n2_0 + (sx95*sdn2_x96);
    const Real a8_n8_1 = a8_n8_0 + (sx95*sdn8_x96);
    const Real a8_T_3 = a8_T_2 + (sx96*(((-2.5e-32)*sdT_x67) - (7.5e-33*sdT_x81)));
    const Real a8_n2_2 = a8_n2_1 + (sdn2_x96*(((-2.5e-32) * sx67) - (7.5e-33 * sx81)));
    const Real a8_n8_2 = a8_n8_1 + (sdn8_x96*(((-2.5e-32) * sx67) - (7.5e-33 * sx81)));
    const Real sx97 = sx47*sx75;
    const Real sdT_x97 = (sdT_x47*sx75);
    const Real sdn2_x97 = (sx47*sdn2_x75);
    const Real sdn4_x97 = (sx47*sdn4_x75);
    const Real a2_T_10 = a2_T_9 - sdT_x97;
    const Real a2_n2_8 = a2_n2_7 - sdn2_x97;
    const Real a2_n4_1 = a2_n4_0 - sdn4_x97;
    const Real a4_T_3 = a4_T_2 - sdT_x97;
    const Real a4_n2_1 = a4_n2_0 - sdn2_x97;
    const Real a4_n4_2 = a4_n4_1 - sdn4_x97;
    const Real a9_T_0 = sdT_x97;
    const Real a9_n2_1 = a9_n2_0 + sdn2_x97;
    const Real a9_n4_0 = sdn4_x97;
    out[matrix_index(2, 4)] = a2_n4_1;
    out[matrix_index(4, 2)] = a4_n2_1;
    out[matrix_index(9, 2)] = a9_n2_1;
    out[matrix_index(9, 4)] = a9_n4_0;
    const Real sx98 = 1.0/(math::exp((1.3*sx45*(sx6 - 9.2103403719761836) - 137.42519902360013*sx53*((0.10857362047581294*sx6 - 1)*(0.10857362047581294*sx6 - 1)) - 4.8449999999999998)*math::log(math::abs(10.0)))*sx83 + 1.0);
    const Real sdT_x98 = (-(((1.0 / ((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0))*((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0))))*((((1.3 * sx45)*sdT_x6) - ((137.42519902360013 * sx53)*(((0.10857362047581294*sdT_x6)*((0.10857362047581294 * sx6) - 1.0)) + (((0.10857362047581294 * sx6) - 1.0)*(0.10857362047581294*sdT_x6)))))*math::log(math::abs(10.0))))*sx83))/((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0)));
    const Real sdn1_x98 = (-(((1.0 / ((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0))*(math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0))))*sdn1_x83))/((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0)));
    const Real sdn2_x98 = (-(((1.0 / ((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0))*(math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0))))*sdn2_x83))/((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0)));
    const Real sdn3_x98 = (-(((1.0 / ((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0))*(math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0))))*sdn3_x83))/((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0)));
    const Real sdn6_x98 = (-(((1.0 / ((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0))*(math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0))))*sdn6_x83))/((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0)));
    const Real sdn8_x98 = (-(((1.0 / ((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0))*(math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0))))*sdn8_x83))/((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0)));
    const Real sdn9_x98 = (-(((1.0 / ((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0))*(math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0))))*sdn9_x83))/((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0)));
    const Real sdn10_x98 = (-(((1.0 / ((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0))*(math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0))))*sdn10_x83))/((math::exp((((((1.3 * sx45) * (sx6 - 9.210340371976184)) - ((137.42519902360013 * sx53) * (((0.10857362047581294 * sx6) - 1.0) * ((0.10857362047581294 * sx6) - 1.0)))) - 4.845) * math::log(math::abs(10.0)))) * sx83) + 1.0)));
    const Real sx100 = math::exp((0.34999999999999998)*math::log(math::abs(T)))*sx41*math::exp(-102000.0*sx34);
    const Real sdT_x100 = (((((0.35*math::exp((0.35 * math::log(math::abs(T)))))*rT)*sx41)*math::exp(((-102000.0) * sx34))) + ((math::exp((0.35 * math::log(math::abs(T)))) * sx41)*(math::exp(((-102000.0) * sx34))*((-102000.0)*sdT_x34))));
    const Real sdn0_x100 = ((math::exp((0.35 * math::log(math::abs(T))))*sdn0_x41)*math::exp(((-102000.0) * sx34)));
    const Real sdn8_x100 = ((math::exp((0.35 * math::log(math::abs(T))))*sdn8_x41)*math::exp(((-102000.0) * sx34)));
    const Real a2_T_11 = a2_T_10 + (8.76e-10*sdT_x100);
    const Real a2_n0_3 = a2_n0_2 + (8.76e-10*sdn0_x100);
    const Real a2_n8_4 = a2_n8_3 + (8.76e-10*sdn8_x100);
    const Real a8_T_4 = a8_T_3 + ((-4.38e-10)*sdT_x100);
    const Real a8_n0_0 = ((-4.38e-10)*sdn0_x100);
    const Real a8_n8_3 = a8_n8_2 + ((-4.38e-10)*sdn8_x100);
    const Real sx101 = xn[5 * XS]*xn[8 * XS]*rhs_specie_choice_10(T, sx34, sx44, sx46, sx54, sx57, sx60, sx61);
    const Real sdT_x101 = ((xn[5 * XS] * xn[8 * XS])*jacd_rhs_specie_choice_10_10(T, sx34, sx44, sx46, sx54, sx57, sx60, sx61, sdT_x34, sdT_x46, sdT_x54, sdT_x57, sdT_x60, sdT_x61));
    const Real sdn5_x101 = ((1.0*xn[8 * XS])*rhs_specie_choice_10(T, sx34, sx44, sx46, sx54, sx57, sx60, sx61));
    const Real sdn8_x101 = ((xn[5 * XS]*1.0)*rhs_specie_choice_10(T, sx34, sx44, sx46, sx54, sx57, sx60, sx61));
    const Real a8_T_5 = a8_T_4 - sdT_x101;
    const Real a8_n5_0 = -sdn5_x101;
    const Real a8_n8_4 = a8_n8_3 - sdn8_x101;
    out[matrix_index(8, 5)] = a8_n5_0;
    const Real sx99 = ((xn[8 * XS])*(xn[8 * XS]))*math::exp((sx98)*math::log(math::abs(1.1800000000000001e-10*math::exp(-69500.0*sx34))))*math::exp((1.0 - sx98)*math::log(math::abs(8.1250000000000003e-8*sx67*(1.0 - math::exp(-6000.0*sx34))*math::exp(-52000.0*sx34))));
    const Real sdT_x99 = ((((xn[8 * XS] * xn[8 * XS])*(math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))*((sdT_x98*math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))) + (sx98*((1.18e-10*(math::exp(((-69500.0) * sx34))*((-69500.0)*sdT_x34)))/(1.18e-10 * math::exp(((-69500.0) * sx34))))))))*math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))) + (((xn[8 * XS] * xn[8 * XS]) * math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*(math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))*(((-sdT_x98)*math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))) + ((1.0 - sx98)*((((((8.125e-08*sdT_x67)*(1.0 - math::exp(((-6000.0) * sx34)))) + ((8.125e-08 * sx67)*(-(math::exp(((-6000.0) * sx34))*((-6000.0)*sdT_x34)))))*math::exp(((-52000.0) * sx34))) + (((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34))))*(math::exp(((-52000.0) * sx34))*((-52000.0)*sdT_x34))))/(((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))));
    const Real sdn1_x99 = ((((xn[8 * XS] * xn[8 * XS])*(math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))*(sdn1_x98*math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))) + (((xn[8 * XS] * xn[8 * XS]) * math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*(math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))*((-sdn1_x98)*math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))));
    const Real sdn2_x99 = ((((xn[8 * XS] * xn[8 * XS])*(math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))*(sdn2_x98*math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))) + (((xn[8 * XS] * xn[8 * XS]) * math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*(math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))*((-sdn2_x98)*math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))));
    const Real sdn3_x99 = ((((xn[8 * XS] * xn[8 * XS])*(math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))*(sdn3_x98*math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))) + (((xn[8 * XS] * xn[8 * XS]) * math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*(math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))*((-sdn3_x98)*math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))));
    const Real sdn6_x99 = ((((xn[8 * XS] * xn[8 * XS])*(math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))*(sdn6_x98*math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))) + (((xn[8 * XS] * xn[8 * XS]) * math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*(math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))*((-sdn6_x98)*math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))));
    const Real sdn8_x99 = ((((((1.0*xn[8 * XS]) + (xn[8 * XS]*1.0))*math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))) + ((xn[8 * XS] * xn[8 * XS])*(math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))*(sdn8_x98*math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))))*math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))) + (((xn[8 * XS] * xn[8 * XS]) * math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*(math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))*((-sdn8_x98)*math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))));
    const Real sdn9_x99 = ((((xn[8 * XS] * xn[8 * XS])*(math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))*(sdn9_x98*math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))) + (((xn[8 * XS] * xn[8 * XS]) * math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*(math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))*((-sdn9_x98)*math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))));
    const Real sdn10_x99 = ((((xn[8 * XS] * xn[8 * XS])*(math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34)))))))*(sdn10_x98*math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))) + (((xn[8 * XS] * xn[8 * XS]) * math::exp((sx98 * math::log(math::abs((1.18e-10 * math::exp(((-69500.0) * sx34))))))))*(math::exp(((1.0 - sx98) * math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))*((-sdn10_x98)*math::log(math::abs((((8.125e-08 * sx67) * (1.0 - math::exp(((-6000.0) * sx34)))) * math::exp(((-52000.0) * sx34)))))))));
    const Real a2_T_12 = a2_T_11 + (2.0*sdT_x99);
    const Real a2_n1_5 = a2_n1_4 + (2.0*sdn1_x99);
    const Real a2_n2_9 = a2_n2_8 + (2.0*sdn2_x99);
    const Real a2_n3_4 = a2_n3_3 + (2.0*sdn3_x99);
    const Real a2_n6_4 = a2_n6_3 + (2.0*sdn6_x99);
    const Real a2_n8_5 = a2_n8_4 + (2.0*sdn8_x99);
    const Real a2_n9_3 = a2_n9_2 + (2.0*sdn9_x99);
    const Real a2_n10_1 = a2_n10_0 + (2.0*sdn10_x99);
    const Real a8_T_6 = a8_T_5 - sdT_x99;
    const Real a8_n1_2 = a8_n1_1 - sdn1_x99;
    const Real a8_n2_3 = a8_n2_2 - sdn2_x99;
    const Real a8_n3_0 = -sdn3_x99;
    const Real a8_n6_1 = a8_n6_0 - sdn6_x99;
    const Real a8_n8_5 = a8_n8_4 - sdn8_x99;
    const Real a8_n9_0 = -sdn9_x99;
    const Real a8_n10_1 = a8_n10_0 - sdn10_x99;
    const Real sx102 = xn[10 * XS]*xn[2 * XS]*rhs_specie_choice_11(T, sx3, sx34);
    const Real sdT_x102 = ((xn[10 * XS] * xn[2 * XS])*jacd_rhs_specie_choice_11_11(T, sx3, sx34, sdT_x3, sdT_x34));
    const Real sdn2_x102 = ((xn[10 * XS]*1.0)*rhs_specie_choice_11(T, sx3, sx34));
    const Real sdn10_x102 = ((1.0*xn[2 * XS])*rhs_specie_choice_11(T, sx3, sx34));
    const Real a8_T_7 = a8_T_6 + sdT_x102;
    const Real a8_n2_4 = a8_n2_3 + sdn2_x102;
    const Real a8_n10_2 = a8_n10_1 + sdn10_x102;
    const Real sx103 = sx101 - sx102;
    const Real sdT_x103 = (sdT_x101 - sdT_x102);
    const Real sdn2_x103 = (-sdn2_x102);
    const Real sdn5_x103 = sdn5_x101;
    const Real sdn8_x103 = sdn8_x101;
    const Real sdn10_x103 = (-sdn10_x102);
    const Real a2_T_13 = a2_T_12 + sdT_x103;
    const Real a2_n2_10 = a2_n2_9 + sdn2_x103;
    const Real a2_n5_2 = a2_n5_1 + sdn5_x103;
    const Real a2_n8_6 = a2_n8_5 + sdn8_x103;
    const Real a2_n10_2 = a2_n10_1 + sdn10_x103;
    const Real sx104 = 6.1739095063118665e-10*math::exp((0.40999999999999998)*math::log(math::abs(T)));
    const Real sdT_x104 = (6.173909506311866e-10*((0.41*math::exp((0.41 * math::log(math::abs(T)))))*rT));
    const Real sx105 = sx104*sx21;
    const Real sdT_x105 = (sdT_x104*sx21);
    const Real sdn3_x105 = (sx104*sdn3_x21);
    const Real sdn5_x105 = (sx104*sdn5_x21);
    const Real a7_T_3 = a7_T_2 + sdT_x105;
    const Real a7_n3_0 = sdn3_x105;
    const Real a7_n5_1 = a7_n5_0 + sdn5_x105;
    out[matrix_index(7, 3)] = a7_n3_0;
    out[matrix_index(7, 5)] = a7_n5_1;
    const Real sx106 = sx104*sx38;
    const Real sdT_x106 = (sdT_x104*sx38);
    const Real sdn2_x106 = (sx104*sdn2_x38);
    const Real sdn7_x106 = (sx104*sdn7_x38);
    const Real a7_T_4 = a7_T_3 - sdT_x106;
    const Real a7_n2_1 = a7_n2_0 - sdn2_x106;
    const Real a7_n7_2 = a7_n7_1 - sdn7_x106;
    out[matrix_index(7, 2)] = a7_n2_1;
    const Real sx107 = sx105 - sx106;
    const Real sdT_x107 = (sdT_x105 - sdT_x106);
    const Real sdn2_x107 = (-sdn2_x106);
    const Real sdn3_x107 = sdn3_x105;
    const Real sdn5_x107 = sdn5_x105;
    const Real sdn7_x107 = (-sdn7_x106);
    const Real a5_T_5 = a5_T_4 - sdT_x107;
    const Real a5_n2_1 = a5_n2_0 - sdn2_x107;
    const Real a5_n3_0 = -sdn3_x107;
    const Real a5_n5_2 = a5_n5_1 - sdn5_x107;
    const Real a5_n7_2 = a5_n7_1 - sdn7_x107;
    out[matrix_index(5, 7)] = a5_n7_2;
    const Real sx108 = sx107 + sx17 - sx24 + sx27;
    const Real sdT_x108 = (((sdT_x107 + sdT_x17) - sdT_x24) + sdT_x27);
    const Real sdn0_x108 = ((-sdn0_x24) + sdn0_x27);
    const Real sdn2_x108 = ((sdn2_x107 + sdn2_x17) - sdn2_x24);
    const Real sdn3_x108 = ((sdn3_x107 + sdn3_x17) + sdn3_x27);
    const Real sdn5_x108 = sdn5_x107;
    const Real sdn7_x108 = sdn7_x107;
    const Real a2_T_14 = a2_T_13 + sdT_x108;
    const Real a2_n0_4 = a2_n0_3 + sdn0_x108;
    const Real a2_n2_11 = a2_n2_10 + sdn2_x108;
    const Real a2_n3_5 = a2_n3_4 + sdn3_x108;
    const Real a2_n5_3 = a2_n5_2 + sdn5_x108;
    const Real a2_n7_2 = a2_n7_1 + sdn7_x108;
    const Real a3_T_3 = a3_T_2 - sdT_x108;
    const Real a3_n0_0 = -sdn0_x108;
    const Real a3_n2_0 = -sdn2_x108;
    const Real a3_n3_3 = a3_n3_2 - sdn3_x108;
    const Real a3_n5_1 = a3_n5_0 - sdn5_x108;
    const Real a3_n7_0 = -sdn7_x108;
    out[matrix_index(2, 0)] = a2_n0_4;
    out[matrix_index(2, 5)] = a2_n5_3;
    out[matrix_index(2, 7)] = a2_n7_2;
    out[matrix_index(3, 5)] = a3_n5_1;
    out[matrix_index(3, 7)] = a3_n7_0;
    const Real sx109 = sx80*(2.0000000000000002e-31*sx67 + 6.0000000000000001e-32*sx81) + sx94*(-sx89 - sx93);
    const Real sdT_x109 = ((sx80*((2e-31*sdT_x67) + (6e-32*sdT_x81))) + (sx94*((-sdT_x89) - sdT_x93)));
    const Real sdn1_x109 = (sx94*((-sdn1_x89) - sdn1_x93));
    const Real sdn2_x109 = ((sdn2_x80*((2e-31 * sx67) + (6e-32 * sx81))) + ((sdn2_x94*((-sx89) - sx93)) + (sx94*((-sdn2_x89) - sdn2_x93))));
    const Real sdn3_x109 = (sx94*((-sdn3_x89) - sdn3_x93));
    const Real sdn6_x109 = (sx94*((-sdn6_x89) - sdn6_x93));
    const Real sdn8_x109 = ((sdn8_x94*((-sx89) - sx93)) + (sx94*((-sdn8_x89) - sdn8_x93)));
    const Real sdn9_x109 = (sx94*((-sdn9_x89) - sdn9_x93));
    const Real sdn10_x109 = (sx94*((-sdn10_x89) - sdn10_x93));
    const Real a2_T_15 = a2_T_14 + sdT_x109;
    const Real a2_n1_6 = a2_n1_5 + sdn1_x109;
    const Real a2_n2_12 = a2_n2_11 + sdn2_x109;
    const Real a2_n3_6 = a2_n3_5 + sdn3_x109;
    const Real a2_n6_5 = a2_n6_4 + sdn6_x109;
    const Real a2_n8_7 = a2_n8_6 + sdn8_x109;
    const Real a2_n9_4 = a2_n9_3 + sdn9_x109;
    const Real a2_n10_3 = a2_n10_2 + sdn10_x109;
    const Real a8_T_8 = a8_T_7 + sdT_x109;
    const Real a8_n1_3 = a8_n1_2 + sdn1_x109;
    const Real a8_n2_5 = a8_n2_4 + sdn2_x109;
    const Real a8_n3_1 = a8_n3_0 + sdn3_x109;
    const Real a8_n6_2 = a8_n6_1 + sdn6_x109;
    const Real a8_n8_6 = a8_n8_5 + sdn8_x109;
    const Real a8_n9_1 = a8_n9_0 + sdn9_x109;
    const Real a8_n10_3 = a8_n10_2 + sdn10_x109;
    out[matrix_index(2, 14)] = a2_T_15*dTde;
    out[matrix_index(2, 8)] = a2_n8_7;
    out[matrix_index(2, 2)] = a2_n2_12;
    out[matrix_index(2, 10)] = a2_n10_3;
    out[matrix_index(2, 3)] = a2_n3_6;
    out[matrix_index(2, 1)] = a2_n1_6;
    out[matrix_index(2, 6)] = a2_n6_5;
    out[matrix_index(2, 9)] = a2_n9_4;
    out[matrix_index(8, 10)] = a8_n10_3;
    out[matrix_index(8, 1)] = a8_n1_3;
    out[matrix_index(8, 9)] = a8_n9_1;
    const Real sx110 = sx40 - sx42 + sx79;
    const Real sdT_x110 = ((sdT_x40 - sdT_x42) + sdT_x79);
    const Real sdn0_x110 = (-sdn0_x42);
    const Real sdn2_x110 = sdn2_x40;
    const Real sdn3_x110 = (sdn3_x40 + sdn3_x79);
    const Real sdn6_x110 = sdn6_x79;
    const Real sdn8_x110 = (-sdn8_x42);
    const Real a3_T_4 = a3_T_3 - sdT_x110;
    const Real a3_n0_1 = a3_n0_0 - sdn0_x110;
    const Real a3_n2_1 = a3_n2_0 - sdn2_x110;
    const Real a3_n3_4 = a3_n3_3 - sdn3_x110;
    const Real a3_n6_0 = -sdn6_x110;
    const Real a3_n8_0 = -sdn8_x110;
    const Real a8_T_9 = a8_T_8 + sdT_x110;
    const Real a8_n0_1 = a8_n0_0 + sdn0_x110;
    const Real a8_n2_6 = a8_n2_5 + sdn2_x110;
    const Real a8_n3_2 = a8_n3_1 + sdn3_x110;
    const Real a8_n6_3 = a8_n6_2 + sdn6_x110;
    const Real a8_n8_7 = a8_n8_6 + sdn8_x110;
    out[matrix_index(3, 14)] = a3_T_4*dTde;
    out[matrix_index(3, 0)] = a3_n0_1;
    out[matrix_index(3, 2)] = a3_n2_1;
    out[matrix_index(3, 3)] = a3_n3_4;
    out[matrix_index(3, 6)] = a3_n6_0;
    out[matrix_index(3, 8)] = a3_n8_0;
    out[matrix_index(8, 0)] = a8_n0_1;
    out[matrix_index(8, 2)] = a8_n2_6;
    out[matrix_index(8, 3)] = a8_n3_2;
    out[matrix_index(8, 6)] = a8_n6_3;
    const Real sx111 = 9.8726896031426014e-7*xn[4 * XS]*sx68;
    const Real sdT_x111 = ((9.872689603142601e-07 * xn[4 * XS])*sdT_x68);
    const Real sdn4_x111 = ((9.872689603142601e-07*1.0)*sx68);
    const Real sdn7_x111 = ((9.872689603142601e-07 * xn[4 * XS])*sdn7_x68);
    const Real a4_T_4 = a4_T_3 - sdT_x111;
    const Real a4_n4_3 = a4_n4_2 - sdn4_x111;
    const Real a4_n7_0 = -sdn7_x111;
    const Real a7_T_5 = a7_T_4 - sdT_x111;
    const Real a7_n4_0 = -sdn4_x111;
    const Real a7_n7_3 = a7_n7_2 - sdn7_x111;
    out[matrix_index(4, 7)] = a4_n7_0;
    out[matrix_index(7, 14)] = a7_T_5*dTde;
    out[matrix_index(7, 7)] = a7_n7_3;
    out[matrix_index(7, 4)] = a7_n4_0;
    const Real sx112 = -sx51;
    const Real sdT_x112 = (-sdT_x51);
    const Real sdn1_x112 = (-sdn1_x51);
    const Real sdn10_x112 = (-sdn10_x51);
    const Real a4_T_5 = a4_T_4 - sdT_x112;
    const Real a4_n1_1 = a4_n1_0 - sdn1_x112;
    const Real a4_n10_0 = -sdn10_x112;
    const Real a10_T_1 = a10_T_0 + sdT_x112;
    const Real a10_n1_0 = sdn1_x112;
    const Real a10_n10_0 = sdn10_x112;
    out[matrix_index(4, 1)] = a4_n1_1;
    out[matrix_index(4, 10)] = a4_n10_0;
    out[matrix_index(10, 1)] = a10_n1_0;
    const Real sx113 = -sx55;
    const Real sdT_x113 = (-sdT_x55);
    const Real a4_T_6 = a4_T_5 + ((xn[4 * XS] * xn[8 * XS])*sdT_x113);
    const Real a4_n4_4 = a4_n4_3 + ((1.0*xn[8 * XS])*sx113);
    const Real a4_n8_0 = ((xn[4 * XS]*1.0)*sx113);
    out[matrix_index(4, 14)] = a4_T_6*dTde;
    out[matrix_index(4, 4)] = a4_n4_4;
    out[matrix_index(4, 8)] = a4_n8_0;
    const Real sx114 = -sx37 + sx49;
    const Real sdT_x114 = ((-sdT_x37) + sdT_x49);
    const Real sdn0_x114 = (-sdn0_x37);
    const Real sdn1_x114 = sdn1_x49;
    const Real sdn5_x114 = sdn5_x49;
    const Real sdn9_x114 = (-sdn9_x37);
    const Real a5_T_6 = a5_T_5 - sdT_x114;
    const Real a5_n0_2 = a5_n0_1 - sdn0_x114;
    const Real a5_n1_2 = a5_n1_1 - sdn1_x114;
    const Real a5_n5_3 = a5_n5_2 - sdn5_x114;
    const Real a5_n9_0 = -sdn9_x114;
    const Real a9_T_1 = a9_T_0 + sdT_x114;
    const Real a9_n0_0 = sdn0_x114;
    const Real a9_n1_0 = sdn1_x114;
    const Real a9_n5_0 = sdn5_x114;
    const Real a9_n9_1 = a9_n9_0 + sdn9_x114;
    out[matrix_index(5, 0)] = a5_n0_2;
    out[matrix_index(5, 1)] = a5_n1_2;
    out[matrix_index(5, 9)] = a5_n9_0;
    out[matrix_index(9, 14)] = a9_T_1*dTde;
    out[matrix_index(9, 0)] = a9_n0_0;
    out[matrix_index(9, 1)] = a9_n1_0;
    out[matrix_index(9, 5)] = a9_n5_0;
    out[matrix_index(9, 9)] = a9_n9_1;
    const Real sx115 = sx103 + sx22 + sx82;
    const Real sdT_x115 = (sdT_x103 + sdT_x22);
    const Real sdn2_x115 = (sdn2_x103 + sdn2_x82);
    const Real sdn3_x115 = sdn3_x22;
    const Real sdn5_x115 = ((sdn5_x103 + sdn5_x22) + sdn5_x82);
    const Real sdn8_x115 = sdn8_x103;
    const Real sdn10_x115 = sdn10_x103;
    const Real a5_T_7 = a5_T_6 - sdT_x115;
    const Real a5_n2_2 = a5_n2_1 - sdn2_x115;
    const Real a5_n3_1 = a5_n3_0 - sdn3_x115;
    const Real a5_n5_4 = a5_n5_3 - sdn5_x115;
    const Real a5_n8_0 = -sdn8_x115;
    const Real a5_n10_0 = -sdn10_x115;
    const Real a10_T_2 = a10_T_1 + sdT_x115;
    const Real a10_n2_2 = a10_n2_1 + sdn2_x115;
    const Real a10_n3_0 = sdn3_x115;
    const Real a10_n5_0 = sdn5_x115;
    const Real a10_n8_0 = sdn8_x115;
    const Real a10_n10_1 = a10_n10_0 + sdn10_x115;
    out[matrix_index(5, 14)] = a5_T_7*dTde;
    out[matrix_index(5, 2)] = a5_n2_2;
    out[matrix_index(5, 3)] = a5_n3_1;
    out[matrix_index(5, 5)] = a5_n5_4;
    out[matrix_index(5, 8)] = a5_n8_0;
    out[matrix_index(5, 10)] = a5_n10_0;
    out[matrix_index(10, 10)] = a10_n10_1;
    out[matrix_index(10, 2)] = a10_n2_2;
    out[matrix_index(10, 3)] = a10_n3_0;
    out[matrix_index(10, 5)] = a10_n5_0;
    const Real sx116 = xn[4 * XS]*xn[8 * XS];
    const Real sdn4_x116 = (1.0*xn[8 * XS]);
    const Real sdn8_x116 = (xn[4 * XS]*1.0);
    const Real a8_T_10 = a8_T_9 + (sdT_x113*sx116);
    const Real a8_n4_0 = (sx113*sdn4_x116);
    const Real a8_n8_8 = a8_n8_7 + (sx113*sdn8_x116);
    const Real a10_T_3 = a10_T_2 + (sx116*sdT_x55);
    const Real a10_n4_0 = (sdn4_x116*sx55);
    const Real a10_n8_1 = a10_n8_0 + (sdn8_x116*sx55);
    out[matrix_index(8, 14)] = a8_T_10*dTde;
    out[matrix_index(8, 8)] = a8_n8_8;
    out[matrix_index(8, 4)] = a8_n4_0;
    out[matrix_index(10, 14)] = a10_T_3*dTde;
    out[matrix_index(10, 8)] = a10_n8_1;
    out[matrix_index(10, 4)] = a10_n4_0;
    const Real sx117 = sx26 - sx31;
    const Real sdT_x117 = (sdT_x26 - sdT_x31);
    const Real sdn0_x117 = (sdn0_x26 - sdn0_x31);
    const Real sdn11_x117 = (-sdn11_x31);
    const Real sdn12_x117 = sdn12_x26;
    const Real a11_T_0 = sdT_x117;
    const Real a11_n0_0 = sdn0_x117;
    const Real a11_n11_0 = sdn11_x117;
    const Real a11_n12_0 = sdn12_x117;
    const Real a12_T_0 = -sdT_x117;
    const Real a12_n0_0 = -sdn0_x117;
    const Real a12_n11_0 = -sdn11_x117;
    const Real a12_n12_0 = -sdn12_x117;
    out[matrix_index(11, 14)] = a11_T_0*dTde;
    out[matrix_index(11, 0)] = a11_n0_0;
    out[matrix_index(11, 11)] = a11_n11_0;
    out[matrix_index(11, 12)] = a11_n12_0;
    out[matrix_index(12, 11)] = a12_n11_0;
    const Real sx118 = sx35 - sx70 + sx72;
    const Real sdT_x118 = ((sdT_x35 - sdT_x70) + sdT_x72);
    const Real sdn0_x118 = sdn0_x35;
    const Real sdn1_x118 = (-sdn1_x70);
    const Real sdn2_x118 = sdn2_x72;
    const Real sdn12_x118 = (sdn12_x35 + sdn12_x72);
    const Real sdn13_x118 = (sdn13_x35 - sdn13_x70);
    const Real a12_T_1 = a12_T_0 - sdT_x118;
    const Real a12_n0_1 = a12_n0_0 - sdn0_x118;
    const Real a12_n1_0 = -sdn1_x118;
    const Real a12_n2_0 = -sdn2_x118;
    const Real a12_n12_1 = a12_n12_0 - sdn12_x118;
    const Real a12_n13_0 = -sdn13_x118;
    const Real a13_T_0 = sdT_x118;
    const Real a13_n0_0 = sdn0_x118;
    const Real a13_n1_0 = sdn1_x118;
    const Real a13_n2_0 = sdn2_x118;
    const Real a13_n12_0 = sdn12_x118;
    const Real a13_n13_0 = sdn13_x118;
    out[matrix_index(12, 14)] = a12_T_1*dTde;
    out[matrix_index(12, 0)] = a12_n0_1;
    out[matrix_index(12, 12)] = a12_n12_1;
    out[matrix_index(12, 1)] = a12_n1_0;
    out[matrix_index(12, 2)] = a12_n2_0;
    out[matrix_index(12, 13)] = a12_n13_0;
    out[matrix_index(13, 14)] = a13_T_0*dTde;
    out[matrix_index(13, 0)] = a13_n0_0;
    out[matrix_index(13, 1)] = a13_n1_0;
    out[matrix_index(13, 2)] = a13_n2_0;
    out[matrix_index(13, 12)] = a13_n12_0;
    out[matrix_index(13, 13)] = a13_n13_0;
    // Energy-row contributions of the fitted energy-only terms, from k_fit and dk_fit/dT only.
    const Real elnT = math::log(math::abs(T));
    const Real erT = 1.0 / T;
    Real ekd9 = 0.0; const Real ek9 = e_r9_b5e4_kd(T, elnT, erT, ekd9);
    Real ekd10 = 0.0; const Real ek10 = e_r10_b5e4_kd(T, elnT, erT, ekd10);
    Real ekd11 = 0.0; const Real ek11 = e_r11_b5e4_kd(T, elnT, erT, ekd11);
    Real ekd15 = 0.0; const Real ek15 = e_r15_b5e4_kd(T, elnT, erT, ekd15);
    Real ekd16 = 0.0; const Real ek16 = e_r16_b5e4_kd(T, elnT, erT, ekd16);
    Real ekd17 = 0.0; const Real ek17 = e_r17_b5e4_kd(T, elnT, erT, ekd17);
    Real ekd18 = 0.0; const Real ek18 = e_r18_b5e4_kd(T, elnT, erT, ekd18);
    Real ekd20 = 0.0; const Real ek20 = e_r20_b5e4_kd(T, elnT, erT, ekd20);
    Real ekd21 = 0.0; const Real ek21 = e_r21_b5e4_kd(T, elnT, erT, ekd21);
    const Real e_L = (-1.24e-13) * xn[0 * XS]*xn[12 * XS] * ek15 + (-9.38e-22) * xn[13 * XS]*xn[0 * XS] * ek10 + (-1.27e-21) * xn[2 * XS]*xn[0 * XS] * ek9 + (-7.5e-19) * xn[2 * XS]*xn[0 * XS] * ek20 + (-3.4635323838154264e-26) * xn[1 * XS]*xn[0 * XS] * ek21 + (-1.3854129535261706e-25) * xn[11 * XS]*xn[0 * XS] * ek21 + (-4.95e-22) * xn[0 * XS]*xn[12 * XS] * ek11 + (-5.54e-17) * xn[0 * XS]*xn[12 * XS] * ek16 + (-5.01e-27) * xn[0 * XS]*xn[0 * XS]*xn[12 * XS] * ek18 + (-9.1e-27) * xn[0 * XS]*xn[0 * XS]*xn[12 * XS] * ek17;
    const Real e_B = (-1.24e-13) * xn[0 * XS]*xn[12 * XS] * ekd15 + (-9.38e-22) * xn[13 * XS]*xn[0 * XS] * ekd10 + (-1.27e-21) * xn[2 * XS]*xn[0 * XS] * ekd9 + (-7.5e-19) * xn[2 * XS]*xn[0 * XS] * ekd20 + (-3.4635323838154264e-26) * xn[1 * XS]*xn[0 * XS] * ekd21 + (-1.3854129535261706e-25) * xn[11 * XS]*xn[0 * XS] * ekd21 + (-4.95e-22) * xn[0 * XS]*xn[12 * XS] * ekd11 + (-5.54e-17) * xn[0 * XS]*xn[12 * XS] * ekd16 + (-5.01e-27) * xn[0 * XS]*xn[0 * XS]*xn[12 * XS] * ekd18 + (-9.1e-27) * xn[0 * XS]*xn[0 * XS]*xn[12 * XS] * ekd17;
    const Real e_A0 = (-1.24e-13) * 1.0 * xn[12 * XS] * ek15 + (-9.38e-22) * 1.0 * xn[13 * XS] * ek10 + (-1.27e-21) * 1.0 * xn[2 * XS] * ek9 + (-7.5e-19) * 1.0 * xn[2 * XS] * ek20 + (-3.4635323838154264e-26) * 1.0 * xn[1 * XS] * ek21 + (-1.3854129535261706e-25) * 1.0 * xn[11 * XS] * ek21 + (-4.95e-22) * 1.0 * xn[12 * XS] * ek11 + (-5.54e-17) * 1.0 * xn[12 * XS] * ek16 + (-5.01e-27) * 2.0 * xn[0 * XS]*xn[12 * XS] * ek18 + (-9.1e-27) * 2.0 * xn[0 * XS]*xn[12 * XS] * ek17;
    const Real e_A1 = (-3.4635323838154264e-26) * 1.0 * xn[0 * XS] * ek21;
    const Real e_A2 = (-1.27e-21) * 1.0 * xn[0 * XS] * ek9 + (-7.5e-19) * 1.0 * xn[0 * XS] * ek20;
    const Real e_A11 = (-1.3854129535261706e-25) * 1.0 * xn[0 * XS] * ek21;
    const Real e_A12 = (-1.24e-13) * 1.0 * xn[0 * XS] * ek15 + (-4.95e-22) * 1.0 * xn[0 * XS] * ek11 + (-5.54e-17) * 1.0 * xn[0 * XS] * ek16 + (-5.01e-27) * 1.0 * xn[0 * XS]*xn[0 * XS] * ek18 + (-9.1e-27) * 1.0 * xn[0 * XS]*xn[0 * XS] * ek17;
    const Real e_A13 = (-9.38e-22) * 1.0 * xn[0 * XS] * ek10;
    const Real v1641 = xn[0 * XS]*xn[12 * XS];
    const Real v1844 = math::log(0.001*T);
    const Real v1850 = ((v1844)*(v1844));
    const Real v1849 = ((v1844)*(v1844)*(v1844));
    const Real v1845 = ((v1844)*(v1844)*(v1844)*(v1844)*(v1844));
    const Real v1704 = M_LN10;
    const Real v1705 = 1.0/v1704;
    const Real v1773 = math::powi<-4>(math::abs(v1704));
    const Real v1706 = math::powi<-2>(math::abs(v1704));
    const Real v1784 = math::powi<-5>(v1704);
    const Real v1846 = v1784*v1845;
    const Real v1738 = math::powi<-3>(v1704);
    const Real v1851 = v1706*v1850;
    const Real v1862 = math::exp((16.666666666666664*v1705)*math::log(math::abs(T)));
    const Real v1847 = ((((v1844)*(v1844)))*(((v1844)*(v1844))));
    const Real v1865 = 1.0/(5.0592917094448065e-34*v1862 + 10.0);
    const Real v1839 = math::exp((25.0*v1705)*math::log(math::abs(T)));
    const Real v1840 = 1.0/v1839;
    const Real v1863 = 1.0/v1862;
    const Real v1864 = 1.0/(785.77199422741614*v1863 + 10.0);
    const Real v1848 = v1773*v1847;
    const Real v1841 = 1.0/(2.3538526683701997e+17*v1840 + 10.0);
    const Real v1842 = 1.0/(1.6889118802245084e-48*v1839 + 10.0);
    const Real v1852 = math::exp((2.0943374000000001*v1705*v1844 + 0.43693353000000001*v1738*v1849 - 0.033638326000000003*v1846 - 0.14913216000000001*v1848 - 0.77151435999999995*v1851 - 23.962112000000001)*math::log(math::abs(10.0)));
    const Real v1866 = math::exp((20000.0*v1864*v1865 - 200.0)*math::log(math::abs(10.0)));
    const Real v1867 = 1.002560385050777e-22*v1866;
    const Real v1843 = math::exp((20000.0*v1841*v1842 - 200.0)*math::log(math::abs(10.0)));
    const Real v1871 = 1.1825091393820599e-21*v1866;
    const Real v1853 = v1843*v1852;
    const Real v1854 = xn[8 * XS]*v1853;
    const Real v1872 = xn[1 * XS]*v1871;
    const Real v1868 = xn[13 * XS]*v1867;
    const Real v1937 = v1868 + v1872;
    const Real v1940 = v1854 + v1937;
    const Real v1882 = math::exp((17.997580222853362*v1705)*math::log(math::abs(T)));
    const Real v1883 = 1.0/v1882;
    const Real v1885 = 1.0/(1.3368457736780898e-34*v1882 + 10.0);
    const Real v1884 = 1.0/(2973.7534532281375*v1883 + 10.0);
    const Real v1886 = 1.8623144679125181e-22*math::exp((20000.0*v1884*v1885 - 200.0)*math::log(math::abs(10.0)));
    const Real v1887 = xn[2 * XS]*v1886;
    const Real v1888 = math::powi<8>(math::abs(v1844));
    const Real v1945 = v1887 + v1940;
    const Real v1791 = math::powi<-8>(math::abs(v1704));
    const Real v1889 = v1791*v1888;
    const Real v1793 = math::powi<-7>(v1704);
    const Real v1890 = ((v1844)*(v1844)*(v1844)*(v1844)*(v1844)*(v1844)*(v1844));
    const Real v1891 = v1793*v1890;
    const Real v1892 = math::powi<6>(math::abs(v1844));
    const Real v1796 = math::powi<-6>(math::abs(v1704));
    const Real v1856 = v1738*v1849;
    const Real v1893 = v1796*v1892;
    const Real v1855 = v1705*v1844;
    const Real v1894 = math::exp((983.67575999999997*v1846 + 734.71650999999997*v1848 + 96.743155000000002*v1851 + 16.815729999999999*v1855 + 343.1918*v1856 + 70.609154000000004*v1889 + 364.14445999999998*v1891 + 801.81246999999996*v1893 - 21.928795999999998)*math::log(math::abs(10.0)));
    const Real v1895 = xn[0 * XS]*v1894;
    const Real v1925 = v1843*v1895;
    const Real v1946 = v1925 + v1945 >= 1.0e-99;
    const Real v1899 = math::exp((-8.8077017000000009*v1846 - 4.7274035999999997*v1848 + 0.93310621999999999*v1851 + 1.6802758*v1855 + 4.0406627000000004*v1856 - 6.3701156000000001*v1889 + 6.4380698000000001*v1891 + 8.9167182999999994*v1893 - 22.921188999999998)*math::log(math::abs(10.0)));
    const Real v1900 = xn[0 * XS]*v1899;
    const Real v1947 = v1843*v1900;
    const Real v1873 = math::exp((3.8479610000000002*v1846 + 20.159831000000001*v1848 + 58.145166000000003*v1851 + 37.383713*v1855 + 48.656103000000002*v1856 - 16.818342000000001)*math::log(math::abs(10.0)));
    const Real v1955 = v1945 + v1947 >= 1.0e-99;
    const Real v1874 = xn[2 * XS]*v1873;
    const Real v1926 = v1854 + v1874;
    const Real v1938 = v1926 + v1937;
    const Real v1876 = math::exp((3.5692468000000002*v1705*v1844 - 4.2519023000000002*v1846 - 21.328264000000001*v1848 - 11.33286*v1851 - 27.850082*v1856 - 24.311209000000002)*math::log(math::abs(10.0)));
    const Real v1939 = v1925 + v1938 >= 1.0e-99;
    const Real v1877 = xn[2 * XS]*v1876;
    const Real v1941 = v1877 + v1940;
    const Real v1942 = v1925 + v1941 >= 1.0e-99;
    const Real v1879 = math::exp((1.5538288*v1846 - 5.5108049000000001*v1848 - 3.7209846*v1851 + 4.6450521*v1855 + 5.9369081000000001*v1856 - 24.311209000000002)*math::log(math::abs(10.0)));
    const Real v1880 = xn[2 * XS]*v1879;
    const Real v1943 = v1880 + v1940;
    const Real v1944 = v1925 + v1943 >= 1.0e-99;
    const Real v1952 = v1938 + v1947 >= 1.0e-99;
    const Real v1953 = v1941 + v1947 >= 1.0e-99;
    const Real v1954 = v1943 + v1947 >= 1.0e-99;
    const Real v1857 = math::exp((0.19191374999999999*v1846 - 0.16596184*v1848 - 0.81520437999999995*v1851 + 2.1892372*v1855 + 0.29036281000000003*v1856 - 23.689236999999999)*math::log(math::abs(10.0)));
    const Real v1858 = xn[13 * XS]*v1857;
    const Real v1869 = math::exp((0.32168730000000001*v1846 - 0.51002221000000003*v1848 + 0.015391166*v1851 + 1.5714710999999999*v1855 - 0.23619984999999999*v1856 - 22.089523)*math::log(math::abs(10.0)));
    const Real v1870 = xn[1 * XS]*v1869;
    const Real v1927 = v1858 + v1870;
    const Real v1930 = v1854 + v1927;
    const Real v1935 = v1887 + v1930;
    const Real v1936 = v1925 + v1935 >= 1.0e-99;
    const Real v1928 = v1926 + v1927;
    const Real v1929 = v1925 + v1928 >= 1.0e-99;
    const Real v1951 = v1935 + v1947 >= 1.0e-99;
    const Real v1931 = v1877 + v1930;
    const Real v1932 = v1925 + v1931 >= 1.0e-99;
    const Real v1933 = v1880 + v1930;
    const Real v1934 = v1925 + v1933 >= 1.0e-99;
    const Real v1950 = v1933 + v1947 >= 1.0e-99;
    const Real v1948 = v1928 + v1947 >= 1.0e-99;
    const Real v1949 = v1931 + v1947 >= 1.0e-99;
    const Real v1963 = v1945 >= 1.0e-99;
    const Real v1960 = v1938 >= 1.0e-99;
    const Real v1962 = v1943 >= 1.0e-99;
    const Real v1959 = v1935 >= 1.0e-99;
    const Real v1957 = v1931 >= 1.0e-99;
    const Real v1956 = v1928 >= 1.0e-99;
    const Real v1958 = v1933 >= 1.0e-99;
    const Real v1914 = math::exp((5.0194035000000001*v1705*v1844 + 2.4714160999999999*v1773*v1847 + 5.4710749999999999*v1784*v1845 + 1.8161874*v1791*v1888 - 1.5738805*v1851 - 4.7155769000000003*v1856 - 2.2148338000000001*v1891 - 3.9467355999999998*v1893 - 20.584225)*math::log(math::abs(10.0)));
    const Real v1964 = v1914 >= 1.0e-99;
    const Real v1908 = ((T)*(T)*(T));
    const Real v1909 = 1.0/v1908;
    const Real v1907 = 6.0142468035272636e-8*math::exp((2.1000000000000001)*math::log(math::abs(T))) + 1.0;
    const Real v1910 = math::exp(-2197000.0*v1909);
    const Real v1911 = v1910/v1907;
    const Real v1612 = 1.0/T;
    const Real v1906 = math::exp(-510.0*v1612);
    const Real v1904 = math::exp(-11700.0*v1612);
    const Real v1905 = math::exp(-5860.0*v1612);
    const Real v1912 = 4.985670872372847e-33*math::exp((3.7599999999999998)*math::log(math::abs(T)))*v1911 + 1.6e-18*v1904 + 6.7e-19*v1905 + 3.0e-24*v1906;
    const Real v1924 = v1912 >= 1.0e-99;
    const Real v1859 = T <= 10000.0;
    const Real v1860 = T > 10.0;
    const Real v1861 = v1859 && v1860;
    const Real v1896 = T <= 500.0;
    const Real v1897 = T > 100;
    const Real v1898 = v1896 && v1897;
    const Real v1915 = 0.00020000000000000001*T;
    const Real v1916 = v1915 - 6.0;
    const Real v1917 = v1916 >= 300.0;
    const Real v1965 = 5.5313336794064847e-19/(0.0024787521766663585*math::exp(v1915) + 1.0) >= 1.0e-99;
    const Real v1875 = T <= 100.0;
    const Real v1878 = T <= 1000.0;
    const Real v1961 = v1941 >= 1.0e-99;
    const Real v1881 = T <= 6000.0;
    const Real v1901 = T > 500.0;
    const Real v1913 = T < 2000.0;
    const Real v1966 = jac_nuc_choice_180(v1859, v1860, v1861, v1875, v1878, v1881, v1896, v1897, v1898, v1901, v1913, v1917, v1924, v1929, v1932, v1934, v1936, v1939, v1942, v1944, v1946, v1948, v1949, v1950, v1951, v1952, v1953, v1954, v1955, v1956, v1957, v1958, v1959, v1960, v1961, v1962, v1963, v1964, v1965);
    const Real v1918 = math::exp(jac_nuc_choice_122(v1916, v1917));
    const Real v1919 = v1918 + 1.0;
    const Real v1920 = jac_nuc_choice_124(v1859, v1912, v1913, v1914, v1919);
    const Real v1902 = v1843*jac_nuc_choice_116(v1895, v1898, v1900, v1901);
    const Real v1903 = v1854 + v1902 + jac_nuc_choice_117(v1858, v1861, v1868) + jac_nuc_choice_118(v1861, v1870, v1872) + jac_nuc_choice_121(v1874, v1875, v1877, v1878, v1880, v1881, v1887);
    const Real v1921 = v1903 + v1920;
    const Real v1922 = 1.0/v1921;
    const Real v1923 = v1920*v1922;
    const Real v1986 = v1903*v1920/((v1921)*(v1921));
    const Real v2108 = v1706*v1612;
    const Real v2127 = v1844*v2108;
    const Real v2118 = v1738*v1612;
    const Real v2133 = v1850*v2118;
    const Real v2116 = v1705*v1612;
    const Real v2135 = 4.8910985889961177e-12*v1705*v1840*v1842*v1612/((v1840 + 4.2483542552915895e-17)*(v1840 + 4.2483542552915895e-17)) - 1.9444316593927493e-44*v1839*v1841*v2116/((1.6889118802245085e-49*v1839 + 1)*(1.6889118802245085e-49*v1839 + 1));
    const Real v2136 = 976.7825399351309*v1705*v1863*v1865*v1612/((v1863 + 0.012726338013398102)*(v1863 + 0.012726338013398102)) - 3.8831498904253243e-30*v1862*v1864*v2116/((5.0592917094448061e-35*v1862 + 1)*(5.0592917094448061e-35*v1862 + 1));
    const Real v2128 = v1791*v1890*v1612;
    const Real v2129 = v1793*v1892*v1612;
    const Real v2130 = v1796*v1845*v1612;
    const Real v2131 = v1784*v1847*v1612;
    const Real v2132 = v1773*v1849*v1612;
    const Real v2137 = v1843*jac_nuc_choice_241(v1895, v1898, v1900, v1901, v2116, v2127, v2128, v2129, v2130, v2131, v2132, v2133) + v1854*v2135 + v1854*(4.8223900769399082*v1705*v1612 + 3.0182298984217999*v1738*v1850*v1612 - 3.5529549287336835*v2127 - 0.3872755400043702*v2131 - 1.3735579540080118*v2132) + v1902*v2135 + jac_nuc_choice_242(v1858, v1861, v1868, v2116, v2127, v2131, v2132, v2133, v2136) + jac_nuc_choice_243(v1861, v1870, v1872, v2116, v2127, v2131, v2132, v2133, v2136) + jac_nuc_choice_246(v1705, v1874, v1875, v1877, v1878, v1880, v1881, v1882, v1883, v1884, v1885, v1887, v2116, v2127, v2131, v2132, v2133, v1612);
    const Real v1652 = math::powi<-2>(math::abs(T));
    const Real v2134 = jac_nuc_choice_239(T, v1859, v1904, v1905, v1906, v1907, v1910, v1911, v1913, v1914, v1917, v1918, v1919, v1652, v2116, v2127, v2128, v2129, v2130, v2131, v2132, v2133);
    const Real v1777 = xn[2 * XS] <= 0.01;
    const Real v1778 = jac_nuc_choice_93(v1777, xn);
    const Real v1779 = math::log(jac_nuc_choice_95(v1777, v1778, xn));
    const Real v1781 = ((v1779)*(v1779)*(v1779));
    const Real v1800 = v1781*v1784;
    const Real v1782 = ((v1779)*(v1779));
    const Real v1802 = v1773*v1782;
    const Real v1780 = ((((v1779)*(v1779)))*(((v1779)*(v1779))));
    const Real v1785 = v1780*v1784;
    const Real v1787 = v1773*v1781;
    const Real v1789 = v1738*v1782;
    const Real v1771 = T >= 10000.0;
    const Real v1772 = math::log(jac_nuc_choice_92(T, v1771));
    const Real v1774 = ((((v1772)*(v1772)))*(((v1772)*(v1772))));
    const Real v1792 = v1774*v1791;
    const Real v1775 = ((v1772)*(v1772)*(v1772));
    const Real v1794 = v1775*v1793;
    const Real v1795 = v1774*v1793;
    const Real v1776 = ((v1772)*(v1772));
    const Real v1797 = v1776*v1796;
    const Real v1798 = v1775*v1796;
    const Real v1799 = v1774*v1796;
    const Real v1801 = v1775*v1784;
    const Real v1786 = v1774*v1784;
    const Real v1790 = v1738*v1776;
    const Real v1788 = v1773*v1775;
    const Real v1783 = v1706*v1772;
    const Real v1803 = math::exp((21.93385*v1705*v1772 + 0.92432999999999998*v1705*v1779 - 10.19097*v1706*v1776 + 0.54962*v1706*v1782 + 2.1990599999999998*v1738*v1775 - 0.076759999999999995*v1738*v1781 - 0.0036600000000000001*v1772*v1785 + 0.11864*v1772*v1787 - 1.06447*v1772*v1789 - 0.17333999999999999*v1773*v1774 + 0.0027499999999999998*v1773*v1780 - 0.073660000000000003*v1776*v1800 + 0.62343000000000004*v1776*v1802 + 0.77951999999999999*v1779*v1783 - 0.0083499999999999998*v1779*v1786 + 0.11711000000000001*v1779*v1788 - 0.54262999999999995*v1779*v1790 + 6.1920000000000003e-5*v1780*v1792 - 0.00066631000000000004*v1780*v1794 + 0.0025140000000000002*v1780*v1797 - 0.001482*v1781*v1795 + 0.017590000000000001*v1781*v1798 + 0.0106*v1782*v1799 - 0.13768*v1782*v1801 - 42.567880000000002)*math::log(math::abs(10.0)));
    const Real v1804 = xn[10 * XS]*v1803;
    const Real v1604 = 0.00013612213614898791*xn[0 * XS] + 0.24994102282436673*xn[1 * XS] + 0.75007714496081457*xn[10 * XS] + 0.99972775572710437*xn[11 * XS] + 0.99986387786355213*xn[12 * XS] + xn[13 * XS] + 0.25007714496081457*xn[2 * XS] + 0.25021326709726244*xn[3 * XS] + 0.49986387786355219*xn[4 * XS] + 0.5*xn[5 * XS] + 0.50001816778518127*xn[6 * XS] + 0.50013612213644787*xn[7 * XS] + 0.50015428992162914*xn[8 * XS] + 0.7499410228243667*xn[9 * XS];
    const Real v1605 = ((v1604)*(v1604));
    const Real v1698 = 9.1093818800000008e-28*xn[0 * XS] + 1.6726215800000001e-24*xn[1 * XS] + 5.01956503638e-24*xn[10 * XS] + 6.6902431600000005e-24*xn[11 * XS] + 6.6911540981899994e-24*xn[12 * XS] + 6.6920650363799998e-24*xn[13 * XS] + 1.6735325181900001e-24*xn[2 * XS] + 1.6744434563800001e-24*xn[3 * XS] + 3.3451215800000003e-24*xn[4 * XS] + 3.3460325181899999e-24*xn[5 * XS] + 3.3461540981899999e-24*xn[6 * XS] + 3.3469434563800003e-24*xn[7 * XS] + 3.3470650363800003e-24*xn[8 * XS] + 5.0186540981899997e-24*xn[9 * XS];
    const Real v1606 = 1.0/v1605;
    const Real v1811 = v1698 <= 9.9999999999999993e-41;
    const Real v1824 = jac_nuc_choice_110(v1606, v1811);
    const Real v1648 = xn[10 * XS] + xn[2 * XS] + xn[3 * XS] + xn[9 * XS];
    const Real v1607 = xn[1 * XS] + xn[12 * XS] + xn[4 * XS];
    const Real v1806 = xn[6 * XS] + xn[8 * XS];
    const Real v1807 = xn[0 * XS] + xn[11 * XS] + xn[13 * XS] + xn[5 * XS] + xn[7 * XS] + v1806 + v1607 + v1648;
    const Real v1808 = v1807 <= 9.9999999999999993e-41;
    const Real v1823 = jac_nuc_choice_109(v1807, v1808);
    const Real v1825 = math::sqrt(v1823*v1824);
    const Real v1810 = v1698 >= 0.5;
    const Real v1815 = 1.0000420000000001*v1705;
    const Real v1821 = math::exp((v1815*math::log(jac_nuc_choice_107(v1810, v1698)) + 2.1498900000000001)*math::log(math::abs(10.0)));
    const Real v1809 = v1698 >= 9.9999999999999998e-13;
    const Real v1822 = jac_nuc_choice_108(v1809, v1821);
    const Real v1695 = math::sqrt(M_PI);
    const Real v1826 = v1822*v1825;
    const Real v1623 = math::sqrt(T);
    const Real v1812 = v1623*v1695;
    const Real v1813 = v1812*v1698;
    const Real v1827 = v1813*v1826;
    const Real v1814 = math::exp((2.1498900000000001 - 0.69317629274152892*v1705)*math::log(math::abs(10.0)))*v1813;
    const Real v1828 = v1827 + 2.1986273043946046e-36;
    const Real v1817 = 1.0/math::fabs(v1604);
    const Real v1816 = math::exp((v1815*math::log(v1698) + 2.1498900000000001)*math::log(math::abs(10.0)))*v1813;
    const Real v1818 = math::sqrt(v1807);
    const Real v1819 = math::sqrt(v1606*v1807);
    const Real v1820 = jac_nuc_choice_106(v1808, v1809, v1810, v1811, v1814, v1816, v1817, v1818, v1819);
    const Real v1829 = jac_nuc_choice_111(v1820, v1828);
    const Real acc14_0 = -(0.00090725968 * v1822 * v1829 * v1908 * v1698);
    const Real v1723 = math::log(math::abs(T));
    const Real v1724 = ((v1723)*(v1723));
    const Real v1725 = v1706*v1724;
    const Real v1726 = -4.8909149999999997*v1705*v1723 + 0.47490300000000002*v1725 - 133.82830000000001*v1612;
    const Real v1727 = math::exp((v1726 + 14.82123)*math::log(math::abs(10.0)));
    const Real v1649 = xn[1 * XS] + 2.0*xn[6 * XS] + 2.0*xn[8 * XS] + v1648;
    const Real v1728 = 1.0/v1727;
    const Real v1729 = v1728*v1649;
    const Real v1730 = math::exp(-0.0022727272727272726*T);
    const Real v1731 = math::exp(-0.00054054054054054055*T);
    const Real v1732 = -2.0563129999999998*v1730 + 0.58640729999999996*v1731 + 0.82274429999999998;
    const Real v1733 = math::exp((v1732)*math::log(math::abs(v1729)));
    const Real v1734 = v1733 + 1.0;
    const Real v1735 = 1.0/v1734;
    const Real v1743 = math::exp((v1726 + 13.656822)*math::log(math::abs(10.0)));
    const Real v1744 = 1.0/v1743;
    const Real v1745 = v1744*v1649;
    const Real v1746 = math::exp((v1732)*math::log(math::abs(v1745)));
    const Real v1747 = v1746 + 1.0;
    const Real v1748 = 1.0/v1747;
    const Real v1739 = ((v1723)*(v1723)*(v1723))*v1738;
    const Real v1740 = 40870.379999999997*v1612 + 1.0;
    const Real v1741 = -69.700860000000006*v1705*math::log(v1740) + 4.6331670000000003*v1739;
    const Real v1736 = v1705*v1723;
    const Real v1737 = 16780.950000000001*v1612 + 1.0;
    const Real v1742 = 19.734269999999999*v1705*math::log(v1737) + 37.886913*v1706*v1724 - 14.509090000000008*v1736 - v1741 - 307.31920000000002;
    const Real v1749 = math::exp((43.20243*v1706*v1724 - v1735*v1742 - 68.422430000000006*v1736 - v1741 - 2080.4099999999999*v1748*v1612 - 23705.700000000001*v1612 - 178.4239)*math::log(math::abs(10.0)));
    const Real v1750 = -2.4640089999999999*v1705*v1723 + 0.19859550000000001*v1725 + 743.05999999999995*v1612;
    const Real v1762 = math::exp((v1750 + 8.1313220000000008)*math::log(math::abs(10.0)));
    const Real v1763 = 1.0/v1762;
    const Real v1764 = v1763*v1649;
    const Real v1754 = 2.9375070000000001*v1730 + 0.23588480000000001*v1731 + 0.75022860000000002;
    const Real v1765 = math::exp((v1754)*math::log(math::abs(v1764)));
    const Real v1766 = v1765 + 1.0;
    const Real v1767 = 1.0/v1766;
    const Real v1751 = math::exp((v1750 + 9.3055640000000004)*math::log(math::abs(10.0)));
    const Real v1752 = 1.0/v1751;
    const Real v1753 = v1752*v1649;
    const Real v1755 = math::exp((v1754)*math::log(math::abs(v1753)));
    const Real v1756 = v1755 + 1.0;
    const Real v1757 = 1.0/v1756;
    const Real v1759 = 27535.310000000001*v1612 + 1.0;
    const Real v1758 = 14254.549999999999*v1612 + 1.0;
    const Real v1760 = -21.360939999999999*v1705*math::log(v1759) + 0.25820969999999999*v1739;
    const Real v1761 = 70.138370000000009*v1705*v1723 + 11.28215*v1705*math::log(v1758) - 4.7035149999999994*v1725 - v1760 - 203.11568;
    const Real v1768 = math::exp((42.707410000000003*v1705*v1723 - 2.0273650000000001*v1725 - v1757*v1761 - v1760 - 1657.4099999999999*v1767*v1612 - 21467.790000000001*v1612 - 142.7664)*math::log(math::abs(10.0)));
    const Real v1714 = 52000.0*v1612;
    const Real v1982 = 483396202.36294854/((v1828)*(v1828)*(v1828));
    const Real v1715 = math::exp(-v1714);
    const Real v1713 = 1.0 - math::exp(-6000.0*v1612);
    const Real v1716 = v1713*v1715;
    const Real v1609 = math::sqrt(T);
    const Real acc11_0 = -(8.52e-27 * xn[0 * XS] * v1609);
    const Real v1651 = 1.0/v1609;
    const Real v1717 = 8.1250000000000003e-8*v1716*v1651;
    const Real v1703 = math::log(0.0001*T);
    const Real v1707 = math::exp((1.3*v1705*v1703 - 1.6200000000000001*v1706*((v1703)*(v1703)) - 4.8449999999999998)*math::log(math::abs(10.0)));
    const Real v1708 = v1707*v1649;
    const Real v1709 = v1708 + 1.0;
    const Real v1710 = 1.0/v1709;
    const Real v1711 = 1.0*v1710;
    const Real v1718 = 1.0 - v1711;
    const Real v1719 = math::exp((v1718)*math::log(math::abs(v1717)));
    const Real v1702 = 1.1800000000000001e-10*math::exp(-69500.0*v1612);
    const Real v1712 = math::exp((v1711)*math::log(math::abs(v1702)));
    const Real v1720 = v1712*v1719;
    const Real v1701 = ((xn[8 * XS])*(xn[8 * XS]));
    const Real v1721 = v1720*v1701;
    const Real v1722 = 7.1777505408000004e-12*v1721;
    const Real v1996 = v1733*v1742/((v1734)*(v1734));
    const Real v2005 = v1755*v1761/((v1756)*(v1756));
    const Real v1997 = 2.3025850929940459*v1996;
    const Real v2006 = 2.3025850929940459*v2005;
    const Real v1991 = v1746*v1612/((v1747)*(v1747));
    const Real v1992 = 4790.3210533157426*v1991;
    const Real v2000 = v1765*v1612/((v1766)*(v1766));
    const Real v1655 = math::exp(-12000.0/(T + 1200.0));
    const Real v1656 = xn[8 * XS]*v1655;
    const Real v1653 = math::exp(-160000.0*v1652);
    const Real v1654 = xn[2 * XS]*v1653;
    const Real v1657 = 1.0/(1.6000000000000001*v1654 + 1.3999999999999999*v1656);
    const Real v1658 = v1651*v1657;
    const Real v1650 = 1.0/v1649;
    const Real v1659 = v1650*v1658;
    const Real v2018 = math::exp((-2)*math::log(math::abs(v1659 + 9.9999999999999995e-7)));
    const Real v2035 = v1650*v1651/((v1654 + 0.87499999999999989*v1656)*(v1654 + 0.87499999999999989*v1656));
    const Real v1642 = (math::exp((-0.5)*math::log(math::abs(T)))*(1.0/T));
    const Real v2107 = v2018*(500000.0*v1642*v1650*v1657 - 390624.99999999994*v2035*(-512000.0*v1909*v1654 - 0.011666666666666665*v1656/((0.00083333333333333339*T + 1)*(0.00083333333333333339*T + 1))));
    const Real v2012 = math::powi<-2>(math::abs(v1709));
    const Real v1660 = 1.0/(1000000.0*v1659 + 1.0);
    const Real v2013 = v2012*math::log(v1717);
    const Real v2043 = 5.6556829037999995e-12*v1660;
    const Real v1693 = 0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0;
    const Real v1694 = 1.0/v1693;
    const Real v2051 = xn[2 * XS]*v2043*v1694;
    const Real acc14_1 = acc14_0 + xn[3 * XS] * v2051 * (1.3296555e-10 * (math::exp((0.098492999999999997)*math::log(math::abs(T)))*(1.0/T)) + 2.466314622e-10 * (math::exp((0.55610000000000004)*math::log(math::abs(T)))*(1.0/T)) + 8.16477921e-16 * (math::exp((2.1825999999999999)*math::log(math::abs(T)))*(1.0/T)));
    const Real v2016 = math::log(v1702);
    const Real acc8_0 = 1.43555010816e-11 * v1707 * v1712 * v1719 * v2012 * v2016 * v1701;
    const Real v2049 = v2012*v2016;
    const Real v2117 = v1723*v2108;
    const Real v2126 = v1754*(1710.9588792001557*v1652 + 5.6735903924031659*v2116 - 0.91456607567139814*v2117);
    const Real v2123 = v1732*(11.261747970100974*v1705*v1612 - 308.15104860073512*v1652 - 2.1870091368363029*v2117);
    const Real v1830 = ((((T)*(T)))*(((T)*(T))));
    const Real v1831 = v1822*v1830;
    const Real v1833 = 0.00022681492*v1698;
    const Real v1983 = v1831*v1833;
    const Real v1620 = T <= 10;
    const Real acc14_2 = acc14_1 - 1.55e-26 * v1641 * jac_nuc_choice_289(T, v1620);
    const Real v1624 = jac_nuc_choice_85(v1620, v1623);
    const Real v1625 = 0.0031622776601683794*v1624 + 1.0;
    const Real v1626 = 1.0/v1625;
    const Real v1671 = xn[12 * XS]*v1626;
    const Real v1670 = ((xn[0 * XS])*(xn[0 * XS]));
    const Real v1672 = v1670*v1671;
    const Real v2082 = jac_nuc_choice_230(v1620, v1652);
    const Real v2103 = v2082*v1672;
    const Real v2109 = 1.0*v1708*(2.9933606208922598*v1705*v1612 - 7.460375701300709*v2108*v1703);
    const Real acc14_3 = acc14_2 - v1722 * (69500.0 * v1710 * v1652 - v2049 * v2109);
    const Real v1993 = v1743*v1744;
    const Real v1998 = v1727*v1728;
    const Real v2002 = v1762*v1763;
    const Real v2007 = v1751*v1752;
    const Real v1632 = 6.3095734448019361e-5*jac_nuc_choice_87(T, v1620) + 1.0;
    const Real v1633 = 1.0/v1632;
    const Real v1634 = jac_nuc_choice_88(T, v1620);
    const Real v1635 = v1633*v1634;
    const Real v2085 = 1.0/v1623;
    const Real acc14_4 = acc14_3 - v1983 * jac_nuc_choice_290(v1820, v1826, v1982, v2085, v1695, v1698);
    const Real v2086 = jac_nuc_choice_231(v1620, v2085);
    const Real v2087 = xn[0 * XS]*v2086;
    const Real v2088 = v1635*v2087;
    const Real v1661 = xn[0 * XS]*v1626;
    const Real v1679 = v1624*v1661;
    const Real v2104 = v2082*v1679;
    const Real v2093 = v2086*v1661;
    const Real v1627 = xn[2 * XS]*v1626;
    const Real v2097 = v1627*v2082;
    const Real v2098 = v1672*jac_nuc_choice_233(T, v1620);
    const Real v2095 = v2086/((v1625)*(v1625));
    const Real v2101 = v1641*v2095;
    const Real v2102 = xn[12 * XS]*v2095*v1670;
    const Real v1835 = 1.2500000000000001e-10*xn[0 * XS] + 1.2500000000000001e-10*xn[1 * XS] + 1.2500000000000001e-10*xn[10 * XS] + 1.2500000000000001e-10*xn[11 * XS] + 1.2500000000000001e-10*xn[12 * XS] + 1.2500000000000001e-10*xn[13 * XS] + 1.2500000000000001e-10*xn[2 * XS] + 1.2500000000000001e-10*xn[3 * XS] + 1.2500000000000001e-10*xn[4 * XS] + 1.2500000000000001e-10*xn[5 * XS] + 1.2500000000000001e-10*xn[6 * XS] + 1.2500000000000001e-10*xn[7 * XS] + 1.2500000000000001e-10*xn[8 * XS] + 1.2500000000000001e-10*xn[9 * XS] <= 9.9999999999999993e-41;
    const Real v1836 = 28601.610899577994*math::exp((-0.45000000000000001)*math::log(math::abs(v1807)));
    const Real v1837 = jac_nuc_choice_112(v1835, v1836);
    const Real v1838 = jac_nuc_choice_114(v1835, v1836, v1837);
    const Real v1987 = xn[8 * XS]*v1838;
    const Real v2096 = xn[2 * XS]*v2095;
    const Real v1628 = jac_nuc_choice_86(v1620, v1612);
    const Real v1683 = math::exp(-157809.10000000001*v1628);
    const Real acc14_5 = acc14_4;
    const Real v1684 = v1624*v1683;
    const Real v2106 = xn[0 * XS]*v1684;
    const Real acc14_6 = acc14_5;
    const Real acc14_7 = acc14_6;
    const Real v2110 = jac_nuc_choice_235(v1771)*jac_nuc_choice_236(v1771, v1612);
    const Real v2112 = v1780*v2110;
    const Real v2114 = v1781*v2110;
    const Real v2115 = v1782*v2110;
    const Real v1643 = jac_nuc_choice_89(v1620, v1642);
    const Real v2083 = v1641*v1643*v2082;
    const Real acc14_8 = acc14_7;
    const Real v2090 = xn[0 * XS]*v1624;
    const Real v2091 = v1633*v2090*jac_nuc_choice_232(T, v1620);
    const Real v2099 = v1634*v2090*jac_nuc_choice_234(T, v1620)/((v1632)*(v1632));
    const Real acc14_9 = acc14_8;
    const Real acc14_10 = acc14_9;
    const Real v2113 = v1779*v2110;
    const Real v2119 = v1724*v2118;
    const Real v2120 = v1705*v1652;
    const Real v2121 = v2120/v1740;
    const Real v2124 = v2120/v1759;
    const Real v1970 = xn[12 * XS]*v1661;
    const Real v1644 = 1.0 + 0.29999999999999999*math::exp(-94000.0*v1628);
    const Real v1645 = math::exp(-470000.0*v1628);
    const Real acc14_11 = acc14_10;
    const Real v1666 = jac_nuc_choice_91(T, v1620);
    const Real v1646 = 1.24e-13*v1644*v1645;
    const Real v1667 = math::exp(-55338.0*v1628);
    const Real acc14_12 = acc14_11;
    const Real v1668 = v1666*v1667;
    const Real acc14_13 = acc14_12;
    const Real acc14_14 = acc14_13;
    const Real acc0_0 = 0.0;
    const Real v1673 = math::exp(-13179.0*v1628);
    const Real acc14_15 = acc14_14;
    const Real v1674 = v1666*v1673;
    const Real acc14_16 = acc14_15;
    const Real acc14_17 = acc14_16;
    const Real acc0_1 = acc0_0;
    const Real v1676 = math::exp(-631515.0*v1628);
    const Real acc14_18 = acc14_17;
    const Real acc14_19 = acc14_18;
    const Real v1677 = 4.9500000000000001e-22*v1676;
    const Real acc12_0 = 0.0;
    const Real v1680 = math::exp(-285335.40000000002*v1628);
    const Real v1678 = xn[12 * XS]*v1677;
    const Real acc14_20 = acc14_19;
    const Real v1681 = 9.3799999999999993e-22*v1680;
    const Real acc13_0 = 0.0;
    const Real v1682 = xn[13 * XS]*v1681;
    const Real acc14_21 = acc14_20;
    const Real v1699 = 1.0/v1698;
    const Real v1700 = math::exp((-1.0/2.0)*math::log(math::abs(v1699)));
    const Real v1696 = 1.0/v1695;
    const Real v2020 = v1696*v1700;
    const Real v2001 = 3816.3275589792611*v2000;
    const Real v2025 = v1706*v1779;
    const Real v2029 = v1773*v1776;
    const Real v2027 = v1738*v1779;
    const Real v2032 = v1776*v1784;
    const Real v1688 = math::exp((-0.25)*math::log(math::abs(T)));
    const Real v1689 = 2.0000000000000002e-31*v1651 + 6.0000000000000001e-32*v1688;
    const Real v1687 = ((xn[2 * XS])*(xn[2 * XS])*(xn[2 * XS]));
    const Real v2038 = v1687*v1689;
    const Real acc14_22 = acc14_21 + 7.1777505408e-24 * v2038 * v2107;
    const Real v1691 = 2.5000000000000002e-32*v1651 + 7.5000000000000001e-33*v1688;
    const Real v1690 = ((xn[2 * XS])*(xn[2 * XS]));
    const Real acc2_0 = 2.15332516224e-11 * v1660 * v1689 * v1690;
    const Real acc8_1 = acc8_0 + 7.1777505408e-12 * v1660 * v1690 * v1691;
    const Real v2045 = xn[8 * XS]*v1690*v1691;
    const Real acc14_23 = acc14_22 + 7.1777505408e-24 * v2045 * v2107;
    const Real v1692 = 1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T)));
    const Real acc14_24 = acc14_23 + xn[2 * XS] * xn[3 * XS] * v2043 * v1692 * (-0.006476405100000001 * (math::exp((1.0461)*math::log(math::abs(T)))*(1.0/T)) - 2.729397888e-10 * (math::exp((3.0424000000000002)*math::log(math::abs(T)))*(1.0/T)) - 1.229450816e-13 * (math::exp((3.7740999999999998)*math::log(math::abs(T)))*(1.0/T))) / (v1693 * v1693);
    const Real acc3_0 = v2051 * v1692;
    const Real v2042 = xn[3 * XS]*v1692*v1694;
    const Real acc2_1 = acc2_0 + v2042 * v2043;
    const Real v2046 = xn[2 * XS]*v2042;
    const Real v1629 = math::exp(-118348.0*v1628);
    const Real v2047 = 5.6556829037999991e-24*v2046;
    const Real acc14_25 = acc14_24 + v2047 * v2107;
    const Real v2094 = xn[0 * XS]*v1629;
    const Real acc14_26 = acc14_25;
    const Real acc14_27 = acc14_26;
    const Real v2105 = xn[13 * XS]*v1680;
    const Real acc14_28 = acc14_27;
    const Real acc14_29 = acc14_28;
    const Real v2111 = v1772*v2110;
    const Real v1662 = math::exp(-473638.0*v1628);
    const Real acc14_30 = acc14_29;
    const Real v1663 = v1662*jac_nuc_choice_90(T, v1620);
    const Real acc14_31 = acc14_30;
    const Real acc14_32 = acc14_31;
    const Real v1613 = math::exp(-102000.0*v1612);
    const Real v2021 = T*v2020;
    const Real v1614 = xn[8 * XS]*v1613;
    const Real v2081 = xn[0 * XS]*v1614;
    const Real acc14_33 = acc14_32 + -3.206731831607808e-16 * math::exp(-1.65 * math::log(math::abs(T))) * v2081;
    const Real acc14_34 = acc14_33 - 1.10034915790464e-21 * (math::exp((0.34999999999999998)*math::log(math::abs(T)))*(1.0/T)) * v2081;
    const Real v2079 = 2.0860422997526066e-16*v1699;
    const Real v2080 = 3.4767371836380304e-16*v1699;
    const Real v2122 = 0.0046734386363636356*v1730 - 0.00031697691891891889*v1731;
    const Real v2125 = -0.0066761522727272725*v1730 - 0.0001275052972972973*v1731;
    const Real v1608 = 4.0*xn[11 * XS] + v1607;
    const Real acc14_35 = acc14_34 - 1.065e-27 * xn[0 * XS] * v1608 * v1651;
    const Real v1617 = 2.73*z + 2.73;
    const Real v1805 = T >= v1617;
    const Real acc14_36 = acc14_35 - jac_nuc_choice_293(v1705, v1772, v1775, v1776, v1783, v1785, v1787, v1788, v1789, v1790, v1791, v1793, v1794, v1796, v1797, v1798, v1800, v1801, v1802, v1804, v1805, v2025, v2027, v2029, v2032, v2110, v2111, v2112, v2113, v2114, v2115);
    const Real acc10_0 = -(jac_nuc_choice_278(v1803, v1805));
    const Real v2009 = 7.1777505408000004e-12*xn[8 * XS];
    const Real v2010 = xn[2 * XS]*v2009;
    const Real acc14_37 = acc14_36 - v2010 * (v1749 * (-2.302585092994046 * v1735 * (75.773826 * v1706 * v1723 * v1612 - 14.509090000000008 * v2116 - 13.899501 * v2119 - 2848700.6345268 * v2121 - 331159.7981565 * v2120 / v1737) + 4790.321053315743 * v1748 * v1652 + v1992 * (v1993 * v2123 + v2122 * math::log(v1745)) + v1997 * (v1998 * v2123 + v2122 * math::log(v1729)) + 54584.391438988954 * v1652 - 157.54846734442862 * v2116 + 198.9545425982375 * v2117 - 32.00478380265584 * v2119 - 6559375.615464089 * v2121) + v1768 * (-2.302585092994046 * v1757 * (70.13837000000001 * v1705 * v1612 - 9.407029999999999 * v2117 - 0.7746291 * v2119 - 588180.1047914 * v2124 - 160821.9712825 * v2120 / v1758) + 3816.327558979261 * v1767 * v1652 + v2001 * (v2002 * v2126 + v2125 * math::log(v1764)) + v2006 * (v2007 * v2126 + v2125 * math::log(v1753)) + 49431.41323352665 * v1652 + 98.33744562638485 * v2116 - 9.336360854115748 * v2117 - 1.783649418259394 * v2119 - 1354334.7412883535 * v2124));
    const Real v2040 = xn[2 * XS]*xn[6 * XS];
    const Real v2041 = 1.7591897530800001e-33*v2040;
    const Real acc14_38 = acc14_37 + v2041 * v2107;
    const Real v2084 = 3.4635323838154264e-26*xn[1 * XS];
    const Real acc14_39 = acc14_38;
    const Real acc14_40 = acc14_39;
    const Real v2089 = 1.3854129535261706e-25*xn[11 * XS];
    const Real acc14_41 = acc14_40;
    const Real acc14_42 = acc14_41;
    const Real v2092 = math::exp((-2.5)*math::log(math::abs(T)));
    const Real acc14_43 = acc14_42 - v1722 * (v2013 * v2109 + 12307692.307692308 * v1718 * v1609 * (0.0042250000000000005 * v1713 * v1715 * v2092 - 4.0625e-08 * v1716 * v1642 - 0.00048750000000000003 * v2092 * math::exp(-58000.0 * v1612)) * math::exp(v1714) / v1713);
    const Real acc14_44 = acc14_43;
    const Real v2100 = (math::exp((-0.25)*math::log(math::abs(T)))*(1.0/T));
    const Real acc14_45 = acc14_44 + v2009 * v1660 * v1690 * (-1.25e-32 * v1642 - 1.875e-33 * v2100);
    const Real acc14_46 = acc14_45 + 7.1777505408e-12 * v1660 * v1687 * (-1e-31 * v1642 - 1.5e-32 * v2100);
    const Real v1618 = 5.6500000000000001e-36*((((z + 1.0)*(z + 1.0)))*(((z + 1.0)*(z + 1.0))));
    const Real acc14_47 = acc14_46 - xn[0 * XS] * v1618;
    const Real v1697 = 1.3806479999999999e-16*xn[0 * XS] + 1.3806479999999999e-16*xn[1 * XS] + 1.3806479999999999e-16*xn[10 * XS] + 1.3806479999999999e-16*xn[11 * XS] + 1.3806479999999999e-16*xn[12 * XS] + 1.3806479999999999e-16*xn[13 * XS] + 1.3806479999999999e-16*xn[2 * XS] + 1.3806479999999999e-16*xn[3 * XS] + 1.3806479999999999e-16*xn[4 * XS] + 1.3806479999999999e-16*xn[5 * XS] + 1.3806479999999999e-16*xn[6 * XS] + 1.3806479999999999e-16*xn[7 * XS] + 1.3806479999999999e-16*xn[8 * XS] + 1.3806479999999999e-16*xn[9 * XS];
    const Real acc14_48 = acc14_47 + 0.0008437377159599618 * v2020 * v1697;
    const Real acc0_2 = acc0_1 + 8581161392004762.0 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc1_0 = 1.5756322344156688e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc4_0 = 3.151149938820873e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc5_0 = 3.1520080549619573e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc7_0 = 3.1528661711030424e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc8_2 = acc8_1 + 3.1529807011135066e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc10_1 = acc10_0 + 4.72849840551871e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc11_1 = acc11_0 + 6.302299877641746e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc12_1 = acc12_0 + 6.30315799378283e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real acc13_1 = acc13_0 + 6.3040161099239145e+19 * T * v1606 * v1696 * v1697 * v1698 * v1700;
    const Real v1834 = T < 2.0;
    const Real acc14_49 = acc14_48 - jac_nuc_choice_292(v1834, v1903, v1922, v1923, v1966, v1986, v1987, v2134, v2137);
    out[matrix_index(14, 14)] = v1699*(acc14_49 + e_B)/(xn[0 * XS] * v2079 + xn[1 * XS] * v2079 + xn[10 * XS] * v2080 + xn[11 * XS] * v2079 + xn[12 * XS] * v2079 + xn[13 * XS] * v2079 + xn[2 * XS] * v2079 + xn[3 * XS] * v2079 + xn[4 * XS] * v2079 + xn[5 * XS] * v2079 + xn[6 * XS] * v2080 + xn[7 * XS] * v2079 + xn[8 * XS] * v2080 + xn[9 * XS] * v2080);
    const Real v1636 = v1624*v1635;
    const Real v1637 = 3.4635323838154264e-26*v1636;
    const Real acc1_1 = acc1_0;
    const Real v1638 = xn[1 * XS]*v1637;
    const Real acc0_3 = acc0_2;
    const Real v1639 = 1.3854129535261706e-25*v1636;
    const Real acc11_2 = acc11_1;
    const Real v1640 = xn[11 * XS]*v1639;
    const Real acc0_4 = acc0_3;
    const Real v1664 = 5.5399999999999998e-17*v1663;
    const Real acc0_5 = acc0_4;
    const Real v1665 = v1661*v1664;
    const Real acc12_2 = acc12_1;
    const Real v1685 = 1.2700000000000001e-21*v1684;
    const Real acc2_2 = acc2_1;
    const Real v1967 = jac_nuc_choice_181(v1903, v1923, v1966);
    const Real v1968 = v1838*v1967;
    const Real v1610 = 2.1299999999999999e-27*v1609;
    const Real v1615 = 3.1438547368704001e-21*math::exp((0.34999999999999998)*math::log(math::abs(T)));
    const Real acc8_3 = acc8_2 - xn[0 * XS] * v1615 * v1613;
    const Real v1611 = v1608*v1610;
    const Real acc0_6 = acc0_5 - v1611;
    const Real v1616 = v1614*v1615;
    const Real acc0_7 = acc0_6 - v1616;
    const Real v1621 = 1.5499999999999999e-26*jac_nuc_choice_84(T, v1620);
    const Real acc12_3 = acc12_2 - xn[0 * XS] * v1621;
    const Real v1622 = xn[12 * XS]*v1621;
    const Real acc0_8 = acc0_7 - v1622;
    const Real v1630 = 7.4999999999999996e-19*v1629;
    const Real acc2_3 = acc2_2;
    const Real v1631 = v1627*v1630;
    const Real acc0_9 = acc0_8;
    const Real v1769 = 7.1777505408000004e-12*v1749 + 7.1777505408000004e-12*v1768;
    const Real acc8_4 = acc8_3 - xn[2 * XS] * v1769;
    const Real v1770 = xn[8 * XS]*v1769;
    const Real acc2_4 = acc2_3 - v1770;
    const Real v1669 = 5.0099999999999997e-27*v1668;
    const Real v1675 = 9.1000000000000001e-27*v1674;
    const Real v1686 = v1627*v1685;
    const Real acc0_10 = acc0_9;
    const Real v1832 = v1829*v1831;
    const Real acc0_11 = acc0_10 - 2.06614372236165e-31 * v1832;
    const Real acc1_2 = acc1_1 - 3.793755298579736e-28 * v1832;
    const Real acc2_5 = acc2_4 - 3.7958214423066343e-28 * v1832;
    const Real acc3_1 = acc3_0 + -3.797887586033532e-28 * v1832;
    const Real acc4_1 = acc4_0 - 7.587234835579737e-28 * v1832;
    const Real acc5_1 = acc5_0 - 7.589300979306633e-28 * v1832;
    const Real acc6_0 = -(7.5895767408863695e-28 * v1832);
    const Real acc7_1 = acc7_0 - 7.5913671230335325e-28 * v1832;
    const Real acc8_5 = acc8_4 - 7.591642884613269e-28 * v1832;
    const Real acc9_0 = -1.138305627788637e-27 * v1832;
    const Real acc10_2 = acc10_1 - 1.1385122421613269e-27 * v1832;
    const Real acc11_3 = acc11_2 - 1.5174469671159474e-27 * v1832;
    const Real acc12_4 = acc12_3 - 1.5176535814886368e-27 * v1832;
    const Real acc13_2 = acc13_1 - 1.5178601958613267e-27 * v1832;
    const Real v1647 = v1643*v1646;
    const Real acc0_12 = acc0_11;
    const Real acc12_5 = acc12_4;
    const Real v1619 = v1618*(T - v1617);
    const Real acc0_13 = acc0_12 - v1619;
    const Real v2067 = 1.4355501081600001e-11*v1707*v1721;
    const Real acc6_1 = acc6_0 + v2049 * v2067;
    const Real v1969 = v1606*(0.00084373771595996178*T*v1696*v1697*v1700 - xn[0 * XS]*v1616 - xn[0 * XS]*v1619 - xn[0 * XS]*v1622 - xn[0 * XS]*v1611 + 5.6556829037999995e-12*xn[2 * XS]*xn[3 * XS]*v1660*v1692*v1694 + 1.75918975308e-21*xn[2 * XS]*xn[6 * XS]*v1660 - xn[2 * XS]*v1770 + 7.1777505408000004e-12*xn[8 * XS]*v1660*v1690*v1691 - v1722 - v1832*v1833 + 7.1777505408000004e-12*v1660*v1687*v1689 - jac_nuc_choice_182(v1834, v1968, xn) - jac_nuc_choice_183(v1804, v1805) + e_L);
    const Real v1994 = v1732*v1650;
    const Real v1995 = v1993*v1994;
    const Real v2003 = v1754*v1650;
    const Real v1999 = v1994*v1998;
    const Real v2004 = v2002*v2003;
    const Real v2008 = v2003*v2007;
    const Real v2011 = v2010*(v1749*(v1992*v1995 + v1997*v1999) + v1768*(v2001*v2004 + v2006*v2008));
    const Real v2014 = v1707*v1722;
    const Real v2015 = v2013*v2014;
    const Real v2050 = -v2011 + v2014*v2049 - v2015;
    const Real acc2_6 = acc2_5 + v2050;
    const Real v2017 = math::exp((-2)*math::log(math::abs(0.5*xn[1 * XS] + 0.5*xn[10 * XS] + 0.5*xn[2 * XS] + 0.5*xn[3 * XS] + 0.5*xn[9 * XS] + v1806)));
    const Real v2034 = v2017*v1658;
    const Real v2036 = v2018*(250000.0*v2034 + 624999.99999999988*v2035*v1653);
    const Real acc2_7 = acc2_6 + v2036 * v2041;
    const Real acc2_8 = acc2_7 + v2036 * v2047;
    const Real v2037 = 7.1777505407999997e-24*v2036;
    const Real acc2_9 = acc2_8 + v2037 * v2038;
    const Real acc2_10 = acc2_9 + v2037 * v2045;
    const Real v2048 = v1606*v2021*v1697*v1698;
    const Real acc2_11 = acc2_10 + 1.5764903505567533e+19 * v2048;
    const Real acc3_2 = acc3_1 + 1.5773484666978378e+19 * v2048;
    const Real acc6_2 = acc6_1 + 3.1521225849724215e+19 * v2048;
    const Real acc9_1 = acc9_0 + 4.727640289377626e+19 * v2048;
    const Real v1984 = xn[8 * XS]*v1967*jac_nuc_choice_191(v1807, v1835, v1837);
    const Real acc8_6 = acc8_5 - jac_nuc_choice_273(v1834, v1843, v1852, v1853, v1920, v1922, v1966, v1968, v1984, v1986, v1987);
    const Real v2022 = 1.1649047900646892e-19*v2021;
    const Real acc2_12 = acc2_11 + v2022;
    const Real v2024 = jac_nuc_choice_201(v1777, v1778)*jac_nuc_choice_203(v1777, v1778, xn);
    const Real v2026 = v1772*v2024;
    const Real v2030 = v1781*v2024;
    const Real v2028 = v1779*v2024;
    const Real v2031 = v1782*v2024;
    const Real acc2_13 = acc2_12 - jac_nuc_choice_259(v1705, v1783, v1786, v1787, v1788, v1789, v1790, v1792, v1794, v1795, v1797, v1798, v1799, v1800, v1801, v1802, v1804, v1805, v2024, v2025, v2026, v2027, v2028, v2029, v2030, v2031, v2032);
    const Real v1973 = 2.3026818015679518*v1705*v1821*jac_nuc_choice_184(v1810, v1699);
    const Real v2044 = jac_nuc_choice_205(v1809, v1810, v1973);
    const Real v1975 = v1829*v1830*v1833;
    const Real acc2_14 = acc2_13 - v1975 * v2044;
    const Real v1976 = v1812*v1826;
    const Real v1977 = 2*v1813*v1825;
    const Real v1978 = (1.0/2.0)*v1824*jac_nuc_choice_187(v1808);
    const Real v1980 = (1.0/2.0)*v1823;
    const Real v1979 = math::powi<-3>(v1604);
    const Real v1981 = 2*v1827*jac_nuc_choice_188(v1605, v1811)*jac_nuc_choice_189(v1807, v1808);
    const Real acc2_15 = acc2_14 - v1983 * jac_nuc_choice_256(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2044);
    const Real v2023 = jac_nuc_choice_199(v1873, v1875, v1876, v1878, v1879, v1881, v1886);
    const Real acc2_16 = acc2_15 - jac_nuc_choice_258(v1834, v1920, v1922, v1966, v1984, v1986, v1987, v2023);
    const Real v2033 = 1.75918975308e-21*v1660;
    const Real acc2_17 = acc2_16 + xn[6 * XS] * v2033;
    const Real acc6_3 = acc6_2 + xn[2 * XS] * v2033;
    const Real v2039 = 1.4355501081600001e-11*xn[8 * XS];
    const Real acc2_18 = acc2_17 + xn[2 * XS] * v2039 * v1660 * v1691;
    out[matrix_index(14, 2)] = -3.7369204214442467e+22 * v1969 + v1699 * (acc2_18 + e_A2);
    const Real acc8_7 = acc8_6 - v1720 * v2039;
    const Real v2068 = v2013*v2067;
    const Real acc6_4 = acc6_3 - v2068;
    const Real acc8_8 = acc8_7 - v2068;
    const Real v2070 = 500000.0*v2034 + 546874.99999999988*v2035*v1655;
    const Real acc8_9 = acc8_8 + 5.655682903799999e-24 * xn[2 * XS] * xn[3 * XS] * v2018 * v2070 * v1692 * v1694;
    const Real acc8_10 = acc8_9 + 1.75918975308e-33 * xn[2 * XS] * xn[6 * XS] * v2018 * v2070;
    const Real acc8_11 = acc8_10 + 7.1777505408e-24 * xn[8 * XS] * v2018 * v2070 * v1690 * v1691;
    const Real acc8_12 = acc8_11 + 7.1777505408e-24 * v2018 * v2070 * v1687 * v1689;
    const Real v2065 = v2010*(v1749*(9580.6421066314851*v1991*v1995 + 4.6051701859880918*v1996*v1999) + v1768*(7632.6551179585222*v2000*v2004 + 4.6051701859880918*v2005*v2008));
    const Real acc6_5 = acc6_4 - v2065;
    const Real acc8_13 = acc8_12 - v2065;
    const Real v2071 = jac_nuc_choice_218(v1809, v1810, v1973);
    const Real acc8_14 = acc8_13 - v1975 * v2071;
    const Real acc8_15 = acc8_14 - v1983 * jac_nuc_choice_271(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2071);
    const Real v1971 = -1.1649047900646892e-19*T*v1696*v1700;
    const Real acc0_14 = acc0_13 - v1971;
    const Real acc1_3 = acc1_2 - v1971;
    const Real acc8_16 = acc8_15 - v1971;
    out[matrix_index(14, 8)] = -7.473840842888493e+22 * v1969 + v1699 * acc8_16;
    const Real acc13_3 = acc13_2 - v1971;
    const Real v1974 = jac_nuc_choice_186(v1809, v1810, v1973);
    const Real acc0_15 = acc0_14 - v1974 * v1975;
    const Real acc0_16 = acc0_15 - v1983 * jac_nuc_choice_248(v1811, v1820, v1974, v1976, v1977, v1978, v1979, v1980, v1981, v1982);
    const Real v1985 = jac_nuc_choice_193(v1894, v1898, v1899, v1901);
    const Real acc0_17 = acc0_16 - jac_nuc_choice_250(v1834, v1843, v1920, v1922, v1966, v1984, v1985, v1986, v1987);
    const Real v1972 = v1624*v1626;
    const Real acc0_18 = acc0_17;
    const Real acc0_19 = acc0_18;
    out[matrix_index(14, 0)] = -2.0340826846270714e+19 * v1969 + v1699 * (acc0_19 + e_A0);
    const Real v1988 = xn[0 * XS]*v1610;
    const Real acc1_4 = acc1_3 - v1988;
    const Real v1989 = jac_nuc_choice_195(v1809, v1810, v1973);
    const Real acc1_5 = acc1_4 - v1975 * v1989;
    const Real acc1_6 = acc1_5 - v1983 * jac_nuc_choice_252(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v1989);
    const Real v1990 = jac_nuc_choice_196(v1861, v1869, v1871);
    const Real acc1_7 = acc1_6 - jac_nuc_choice_254(v1834, v1920, v1922, v1966, v1984, v1986, v1987, v1990);
    const Real v2019 = -1.4139207259499998e-18*xn[2 * XS]*xn[3 * XS]*v2017*v2018*v1651*v1657*v1692*v1694 - 4.3979743826999997e-28*xn[2 * XS]*xn[6 * XS]*v2017*v2018*v1651*v1657 - 1.7944376352000002e-18*xn[8 * XS]*v2017*v2018*v1651*v1657*v1690*v1691 - 7.1777505408000004e-12*v1707*v1712*v1719*v2012*v2016*v1701 + v2011 + v2015 - 1.7944376352000002e-18*v2017*v2018*v1651*v1657*v1687*v1689;
    const Real acc1_8 = acc1_7 - v2019;
    out[matrix_index(14, 1)] = -3.734886338755154e+22 * v1969 + v1699 * (acc1_8 + e_A1);
    const Real acc10_3 = acc10_2 - v2019;
    const Real v2078 = jac_nuc_choice_229(v1857, v1861, v1867);
    const Real acc13_4 = acc13_3 - jac_nuc_choice_286(v1834, v1920, v1922, v1966, v1984, v1986, v1987, v2078);
    const Real v2077 = jac_nuc_choice_228(v1809, v1810, v1973);
    const Real acc13_5 = acc13_4 - v1975 * v2077;
    const Real acc13_6 = acc13_5 - v1983 * jac_nuc_choice_284(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2077);
    out[matrix_index(14, 13)] = -1.4943070555407201e+23 * v1969 + v1699 * (acc13_6 + e_A13);
    const Real v2058 = jac_nuc_choice_208(v1834, v1984);
    const Real v2062 = v1971 + v2058;
    const Real acc5_2 = acc5_1 - v2062;
    const Real acc7_2 = acc7_1 - v2062;
    const Real acc10_4 = acc10_3 - v2062;
    const Real acc11_4 = acc11_3 - v2062;
    const Real v2063 = v1988 + v2062;
    const Real acc4_2 = acc4_1 - v2063;
    const Real acc12_6 = acc12_5 - v2063;
    const Real v2075 = jac_nuc_choice_226(v1809, v1810, v1973);
    const Real acc12_7 = acc12_6 - v1975 * v2075;
    const Real acc12_8 = acc12_7 - v1983 * jac_nuc_choice_282(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2075);
    const Real v2076 = v1626*v1670;
    const Real acc12_9 = acc12_8;
    const Real acc12_10 = acc12_9;
    out[matrix_index(14, 12)] = -1.4941036472718107e+23 * v1969 + v1699 * (acc12_10 + e_A12);
    const Real v2053 = v2018*v2034;
    const Real v2054 = v2038*v2053;
    const Real acc6_6 = acc6_5 + 3.5888752704e-18 * v2054;
    const Real v2055 = v2040*v2053;
    const Real acc6_7 = acc6_6 + 8.7959487654e-28 * v2055;
    const Real v2056 = v2045*v2053;
    const Real acc6_8 = acc6_7 + 3.5888752704e-18 * v2056;
    const Real v2057 = v2046*v2053;
    const Real acc6_9 = acc6_8 + 2.8278414518999996e-18 * v2057;
    const Real v2059 = v2022 - v2058;
    const Real acc6_10 = acc6_9 + v2059;
    const Real v2066 = jac_nuc_choice_214(v1809, v1810, v1973);
    const Real acc6_11 = acc6_10 - v1975 * v2066;
    const Real acc6_12 = acc6_11 - v1983 * jac_nuc_choice_267(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2066);
    out[matrix_index(14, 6)] = -7.4718067601994e+22 * v1969 + v1699 * acc6_12;
    const Real v2052 = jac_nuc_choice_207(v1809, v1810, v1973);
    const Real acc3_3 = acc3_2 - v1975 * v2052;
    const Real acc3_4 = acc3_3 - v1983 * jac_nuc_choice_261(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2052);
    const Real v2060 = v2050 + 1.7944376352000002e-18*v2054 + 4.3979743826999997e-28*v2055 + 1.7944376352000002e-18*v2056 + 1.4139207259499998e-18*v2057 + v2059;
    const Real acc3_5 = acc3_4 + v2060;
    out[matrix_index(14, 3)] = -3.73895450413334e+22 * v1969 + v1699 * acc3_5;
    const Real acc9_2 = acc9_1 + v2060;
    const Real v2073 = jac_nuc_choice_222(v1809, v1810, v1973);
    const Real acc10_5 = acc10_4 - v1975 * v2073;
    const Real acc10_6 = acc10_5 - v1983 * jac_nuc_choice_277(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2073);
    out[matrix_index(14, 10)] = -1.1208455699147847e+23 * v1969 + v1699 * acc10_6;
    const Real v2072 = jac_nuc_choice_220(v1809, v1810, v1973);
    const Real acc9_3 = acc9_2 - v1975 * v2072;
    const Real acc9_4 = acc9_3 - v1983 * jac_nuc_choice_275(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2072);
    out[matrix_index(14, 9)] = -1.1206421616458753e+23 * v1969 + v1699 * acc9_4;
    const Real v2074 = jac_nuc_choice_224(v1809, v1810, v1973);
    const Real acc11_5 = acc11_4 - v1975 * v2074;
    const Real acc11_6 = acc11_5 - v1983 * jac_nuc_choice_280(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2074);
    out[matrix_index(14, 11)] = -1.4939002390029017e+23 * v1969 + v1699 * (acc11_6 + e_A11);
    const Real v2061 = jac_nuc_choice_210(v1809, v1810, v1973);
    const Real acc4_3 = acc4_2 - v1975 * v2061;
    const Real acc4_4 = acc4_3 - v1983 * jac_nuc_choice_263(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2061);
    out[matrix_index(14, 4)] = -7.4695011950145084e+22 * v1969 + v1699 * acc4_4;
    const Real v2069 = jac_nuc_choice_216(v1809, v1810, v1973);
    const Real acc7_3 = acc7_2 - v1975 * v2069;
    const Real acc7_4 = acc7_3 - v1983 * jac_nuc_choice_269(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2069);
    out[matrix_index(14, 7)] = -7.473569360392695e+22 * v1969 + v1699 * acc7_4;
    const Real v2064 = jac_nuc_choice_212(v1809, v1810, v1973);
    const Real acc5_3 = acc5_2 - v1975 * v2064;
    const Real acc5_4 = acc5_3 - v1983 * jac_nuc_choice_265(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2064);
    out[matrix_index(14, 5)] = -7.4715352777036004e+22 * v1969 + v1699 * acc5_4;
}
DEVICE void burn_state_from_y(const Real* y, BurnRecord* b) {

    b->rho = 0.0;
    for (int n = 0; n < NumSpec; ++n) b->xn[n] = math::max(y[n], small_number_density_floor());
    b->e = y[NumSpec];
    eos_re(b);

}
DEVICE void burn_state_from_y_view(const Real* y, BurnView b) {

    b.rho[0] = 0.0;
    [[loom::unroll]] for (unsigned n = 0; n < 14u; ++n) {
        b.xn[n * XS] = math::max(y[n], small_number_density_floor());
    }
    b.e[0] = y[NumSpec];
    eos_re_view(b);

}
DEVICE void rhs(Real t, const Real* y, Real* out, ScratchView s) {

    BurnView b = s.burn;
    burn_state_from_y_view(y, b);
    // rhs_eint first: only edot (not the 14 species outputs) is live across
    // the other half of the RHS.
    rhs_specie(b.T[0], out, b.xn, default_redshift);
    out[14] = rhs_eint(b.T[0], b.xn, default_redshift);

}
DEVICE void eval_jacobian(ScratchView s, Real x) {

    // jac_nuc writes the 146 structurally nonzero entries; the 79 zeros are
    // written once in burn_ros2s (jac_nuc_structural_zeros).
    BurnView b = s.burn;
    burn_state_from_y_view(s.y, b);
    jac_nuc(b.T[0], s.fjac, b.xn, default_redshift);

}
DEVICE unsigned midx(unsigned row, unsigned column) {
  unsigned index = row * 15u + column;
  __builtin_assume(index < 225u);
  return index;
}
// One elimination step of the reference LU with a compile-time column K.
// Same arithmetic per element as the reference; the pivot row's trailing
// values stay in registers and each trailing row is scaled then updated
// (i outer, j inner; every element sees the identical operation sequence).
// Reference partial-pivot search over column K (first maximum of |A[i][K]|).
template <unsigned K>
DEVICE unsigned lu_pivot_search(const Real* A) {
    unsigned pivot_row = K;
    Real max_val = math::abs(A[midx(K, K)]);
    for (unsigned i = K + 1; i < 15u; ++i) {
        const Real val = math::abs(A[midx(i, K)]);
        if (val > max_val) {
            max_val = val;
            pivot_row = i;
        }
    }
    return pivot_row;
}
// The pivot for column K arrives precomputed: the previous step's row update
// tracks the running first maximum of |A[i][K]| over i = K..14 as it writes
// those values (same comparisons in the same order as the search).
template <unsigned K>
DEVICE int lu_step(Real* A, int* ipvt, int info, unsigned pivot_row) {
    unsigned next_pivot = K + 1;
    ipvt[K] = static_cast<int>(pivot_row);
    __builtin_assume(pivot_row < 15u);
    // K == 13: the final A(14,14), kept from registers for the singularity check.
    Real last_diag = 0.0;

    if (A[midx(pivot_row, K)] != 0.0) {
        if (pivot_row != K) {
            const Real t = A[midx(pivot_row, K)];
            A[midx(pivot_row, K)] = A[midx(K, K)];
            A[midx(K, K)] = t;
            Real prow[14 - K];
            Real krow[14 - K];
            // Full-row interchange (LAPACK form): the multipliers in columns
            // 0..K-1 move with their rows, so the solve can apply the whole
            // permutation up front and run the forward sweep with constant
            // indices. The multipliers are only data here (not used again by
            // the elimination), so the factorization arithmetic is unchanged.
            Real plft[K > 0 ? K : 1];
            Real klft[K > 0 ? K : 1];
            [[loom::unroll]] for (unsigned j = 0; j < K; ++j) {
                plft[j] = A[midx(pivot_row, j)];
                klft[j] = A[midx(K, j)];
            }
            [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
                prow[j - K - 1] = A[midx(pivot_row, j)];
                krow[j - K - 1] = A[midx(K, j)];
            }
            [[loom::unroll]] for (unsigned j = 0; j < K; ++j) {
                A[midx(pivot_row, j)] = klft[j];
                A[midx(K, j)] = plft[j];
            }
            [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
                A[midx(pivot_row, j)] = krow[j - K - 1];
                A[midx(K, j)] = prow[j - K - 1];
            }
        }

        const Real multiplier = -1.0 / A[midx(K, K)];
        // The diagonal of U is stored as its reciprocal (only the solve reads it).
        A[midx(K, K)] = -multiplier;
        Real row[14 - K];
        [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
            row[j - K - 1] = A[midx(K, j)];
        }
        Real next_max = 0.0;
        // Software-pipelined rows: row i+1 is loaded before row i is
        // updated and stored, so its load latency overlaps row i's work.
        // Same per-element operations as the plain row loop.
        Real nxt[15 - K];
        [[loom::unroll]] for (unsigned j = K; j < 15u; ++j) {
            nxt[j - K] = A[midx(K + 1, j)];
        }
        for (unsigned i = K + 1; i < 15u; ++i) {
            Real cur[15 - K];
            [[loom::unroll]] for (unsigned j = K; j < 15u; ++j) {
                cur[j - K] = nxt[j - K];
            }
            if (i + 1 < 15u) {
                const unsigned i1 = i + 1;
                __builtin_assume(i1 < 15u);
                [[loom::unroll]] for (unsigned j = K; j < 15u; ++j) {
                    nxt[j - K] = A[midx(i1, j)];
                }
            }
            const Real m = cur[0] * multiplier;
            [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
                cur[j - K] = cur[j - K] + row[j - K - 1] * m;
            }
            if constexpr (K + 1 < 14u) {
                const Real val = math::abs(cur[1]);
                const bool take = (i == K + 1) | (val > next_max);
                next_max = take ? val : next_max;
                next_pivot = take ? i : next_pivot;
            }
            A[midx(i, K)] = m;
            [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
                A[midx(i, j)] = cur[j - K];
            }
            if constexpr (K == 13u) last_diag = cur[1];
        }
    } else {
        info = static_cast<int>(K + 1);
        if constexpr (K + 1 < 14u) next_pivot = lu_pivot_search<K + 1>(A);
        if constexpr (K == 13u) last_diag = A[midx(14u, 14u)];
    }
    if constexpr (K + 1 < 14u) {
        return lu_step<K + 1>(A, ipvt, info, next_pivot);
    } else {
        // Reference: a zero final diagonal reports info = 15 (overriding).
        if (last_diag == 0.0) info = 15;
        A[midx(14u, 14u)] = 1.0 / last_diag;
        return info;
    }
}
// Iteration-matrix element E(i,j) = -J(i,j) (+ fac on the diagonal), exactly as
// decompose used to store it into e.
DEVICE Real iter_elem(const Real* J, unsigned i, unsigned j, Real fac) {
    const Real nj = -J[midx(i, j)];
    return (i == j) ? nj + fac : nj;
}
// Step K = 0 of lu_step with the iteration matrix formed on the fly from fjac:
// identical values and operation order to "e = -fjac + fac*I; lu_step<0>(e)",
// but the loads come from fjac (never stored here), so the per-row loads do not
// wait on the preceding stores to e. Every element of e is written.
DEVICE int lu_step0_from_jac(const Real* J, Real* A, int* ipvt, Real fac) {
    // Column-0 pivot search with all loads issued together (fjac is not
    // stored here); same comparisons in the same order (first maximum).
    Real col[15];
    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
        col[i] = math::abs(iter_elem(J, i, 0u, fac));
    }
    unsigned pivot_row = 0;
    Real max_val = col[0];
    [[loom::unroll]] for (unsigned i = 1; i < 15u; ++i) {
        const bool take = col[i] > max_val;
        max_val = take ? col[i] : max_val;
        pivot_row = take ? i : pivot_row;
    }
    __builtin_assume(pivot_row < 15u);
    ipvt[0] = static_cast<int>(pivot_row);
    unsigned next_pivot = 1;
    int info = 0;
    const Real pivot = iter_elem(J, pivot_row, 0u, fac);
    if (pivot != 0.0) {
        Real row[14];
        [[loom::unroll]] for (unsigned j = 1; j < 15u; ++j) {
            row[j - 1] = iter_elem(J, pivot_row, j, fac);
        }
        const Real multiplier = -1.0 / pivot;
        A[midx(0u, 0u)] = -multiplier;
        [[loom::unroll]] for (unsigned j = 1; j < 15u; ++j) {
            A[midx(0u, j)] = row[j - 1];
        }
        Real next_max = 0.0;
        // Software-pipelined rows (see lu_step): row i+1 is loaded first.
        Real nxt[15];
        {
            const unsigned src1 = (1u == pivot_row) ? 0u : 1u;
            [[loom::unroll]] for (unsigned j = 0; j < 15u; ++j) {
                nxt[j] = iter_elem(J, src1, j, fac);
            }
        }
        for (unsigned i = 1; i < 15u; ++i) {
            Real cur[15];
            [[loom::unroll]] for (unsigned j = 0; j < 15u; ++j) {
                cur[j] = nxt[j];
            }
            if (i + 1 < 15u) {
                const unsigned i1 = i + 1;
                const unsigned src = (i1 == pivot_row) ? 0u : i1;
                __builtin_assume(src < 15u);
                [[loom::unroll]] for (unsigned j = 0; j < 15u; ++j) {
                    nxt[j] = iter_elem(J, src, j, fac);
                }
            }
            const Real m = cur[0] * multiplier;
            [[loom::unroll]] for (unsigned j = 1; j < 15u; ++j) {
                cur[j] = cur[j] + row[j - 1] * m;
            }
            const Real val = math::abs(cur[1]);
            const bool take = (i == 1u) | (val > next_max);
            next_max = take ? val : next_max;
            next_pivot = take ? i : next_pivot;
            A[midx(i, 0u)] = m;
            [[loom::unroll]] for (unsigned j = 1; j < 15u; ++j) {
                A[midx(i, j)] = cur[j];
            }
        }
    } else {
        for (unsigned i = 0; i < 15u; ++i) {
            Real cur[15];
            [[loom::unroll]] for (unsigned j = 0; j < 15u; ++j) {
                cur[j] = iter_elem(J, i, j, fac);
            }
            [[loom::unroll]] for (unsigned j = 0; j < 15u; ++j) {
                A[midx(i, j)] = cur[j];
            }
        }
        info = 1;
        next_pivot = lu_pivot_search<1>(A);
    }
    return lu_step<1>(A, ipvt, info, next_pivot);
}
DEVICE int lu_decomposition(Real* A, int* ipvt) {

    int info = lu_step<0>(A, ipvt, 0, lu_pivot_search<0>(A));

    ipvt[14] = 14;
    if (A[midx(14u, 14u)] == 0.0) {
        info = 15;
    }

    return info;

}
// Reference solve with compile-time columns; element updates are grouped in
// chunks of 7: load the chunk's x and LU values, then store the chunk, so a
// store separates loads only between chunks. Same operations per element.
template <unsigned K, unsigned J>
DEVICE void lu_solve_fwd_chunk(const Real* LU, Real* x, Real t) {
    constexpr unsigned E = (J + 7 < 15u) ? J + 7 : 15u;
    Real xv[7];
    Real lv[7];
    [[loom::unroll]] for (unsigned j = J; j < E; ++j) {
        xv[j - J] = x[j];
        lv[j - J] = LU[midx(j, K)];
    }
    [[loom::unroll]] for (unsigned j = J; j < E; ++j) {
        x[j] = xv[j - J] + t * lv[j - J];
    }
    if constexpr (E < 15u) lu_solve_fwd_chunk<K, E>(LU, x, t);
}
// Returns the updated x[K - 1] (kept in a register for the next step).
template <unsigned K, unsigned J>
DEVICE Real lu_solve_bwd_chunk(const Real* LU, Real* x, Real t) {
    constexpr unsigned E = (J + 7 < K) ? J + 7 : K;
    Real xv[7];
    Real lv[7];
    [[loom::unroll]] for (unsigned j = J; j < E; ++j) {
        xv[j - J] = x[j];
        lv[j - J] = LU[midx(j, K)];
    }
    [[loom::unroll]] for (unsigned j = J; j < E; ++j) {
        xv[j - J] = xv[j - J] + t * lv[j - J];
        x[j] = xv[j - J];
    }
    if constexpr (E < K) {
        return lu_solve_bwd_chunk<K, E>(LU, x, t);
    } else {
        return xv[E - 1 - J];
    }
}
template <unsigned K>
DEVICE void lu_solve_forward(const Real* LU, const int* ipvt, Real* x) {
    const unsigned pivot_row = static_cast<unsigned>(ipvt[K]);
    __builtin_assume(pivot_row < 15u);
    const Real t = x[pivot_row];
    if (pivot_row != K) {
        x[pivot_row] = x[K];
        x[K] = t;
    }
    lu_solve_fwd_chunk<K, K + 1>(LU, x, t);
    if constexpr (K + 1 < 14u) lu_solve_forward<K + 1>(LU, ipvt, x);
}
// xk is the current x[K] (carried from the previous step's update), so
// x[K] is not reloaded right after it was stored.
template <unsigned K>
DEVICE void lu_solve_backward(const Real* LU, Real* x, Real xk) {
    const Real xq = xk / LU[midx(K, K)];
    x[K] = xq;
    const Real t = -xq;
    if constexpr (K > 0) {
        const Real next = lu_solve_bwd_chunk<K, 0>(LU, x, t);
        lu_solve_backward<K - 1>(LU, x, next);
    }
}
// Batched solve steps: every load of a step (x entries, LU column, and the
// pivot entries) is issued before any of its stores, so each step waits on
// memory once. The forward pivot swap is an ordered overwrite store:
// x[j] (j > K) gets its update, then x[p] = old x[K] + t*L(p,K) (for p == K
// it is overwritten by x[K] = t). Same per-element operations as lu_solve.
template <unsigned K>
DEVICE void lu_fwd_batched(const Real* LU, const int* ipvt, Real* x) {
    const unsigned pivot_row = static_cast<unsigned>(ipvt[K]);
    __builtin_assume(pivot_row < 15u);
    const Real t = x[pivot_row];
    const Real a = x[K];
    const Real lp = LU[midx(pivot_row, K)];
    Real v[14 - K];
    [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
        v[j - K - 1] = x[j] + t * LU[midx(j, K)];
    }
    [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
        x[j] = v[j - K - 1];
    }
    x[pivot_row] = a + t * lp;
    x[K] = t;
    if constexpr (K + 1 < 14u) lu_fwd_batched<K + 1>(LU, ipvt, x);
}
template <unsigned K>
DEVICE void lu_bwd_batched(const Real* LU, Real* x, Real xk) {
    const Real xq = xk / LU[midx(K, K)];
    if constexpr (K > 0) {
        Real v[K];
        [[loom::unroll]] for (unsigned j = 0; j < K; ++j) {
            v[j] = x[j];
        }
        Real u[K];
        [[loom::unroll]] for (unsigned j = 0; j < K; ++j) {
            u[j] = LU[midx(j, K)];
        }
        x[K] = xq;
        const Real t = -xq;
        [[loom::unroll]] for (unsigned j = 0; j < K; ++j) {
            v[j] = v[j] + t * u[j];
        }
        [[loom::unroll]] for (unsigned j = 0; j < K; ++j) {
            x[j] = v[j];
        }
        lu_bwd_batched<K - 1>(LU, x, v[K - 1]);
    } else {
        x[K] = xq;
    }
}
// Back substitution in registers: it has no pivoting, so every index is a
// compile-time constant. Same operations per element as lu_bwd_batched.
template <unsigned K>
DEVICE void lu_bwd_regs(const Real* LU, Real* v) {
    v[K] = v[K] * LU[midx(K, K)];
    if constexpr (K > 0) {
        const Real t = -v[K];
        [[loom::unroll]] for (unsigned j = 0; j < K; ++j) {
            v[j] = v[j] + t * LU[midx(j, K)];
        }
        lu_bwd_regs<K - 1>(LU, v);
    }
}
// Forward sweep in registers for the LAPACK-form factorization (rows of L
// interchanged with the pivots): x[j] += x[K] * L(j,K), constant indices.
// Each value gets the same operations as in the interleaved LINPACK sweep.
template <unsigned K>
DEVICE void lu_fwd_regs(const Real* LU, Real* v) {
    const Real t = v[K];
    [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
        v[j] = v[j] + t * LU[midx(j, K)];
    }
    if constexpr (K + 1 < 14u) lu_fwd_regs<K + 1>(LU, v);
}
// Composite row permutation of the pivot sequence: applying the swaps
// (K, ipvt[K]) for K = 0..13 to x gives x[perm[i]] at position i.
template <unsigned K>
DEVICE void perm_step(const int* pv, unsigned* pm) {
    const unsigned p = static_cast<unsigned>(pv[K]);
    const unsigned a = pm[K];
    unsigned b = a;
    [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
        b = (p == j) ? pm[j] : b;
    }
    [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
        pm[j] = (p == j) ? a : pm[j];
    }
    pm[K] = b;
    if constexpr (K + 1 < 14u) perm_step<K + 1>(pv, pm);
}
DEVICE void store_permutation(int* ipvt) {
    int pv[14];
    [[loom::unroll]] for (unsigned k = 0; k < 14u; ++k) {
        pv[k] = ipvt[k];
    }
    unsigned pm[15];
    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
        pm[i] = i;
    }
    perm_step<0>(pv, pm);
    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
        ipvt[i] = static_cast<int>(pm[i]);
    }
}
// Solve with the permutation applied up front (one gather), then both sweeps
// in registers; x is stored once.
DEVICE void lu_solve_perm(const Real* LU, const int* perm, Real* x) {
    Real v[15];
    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
        const unsigned src = static_cast<unsigned>(perm[i]);
        __builtin_assume(src < 15u);
        v[i] = x[src];
    }
    lu_fwd_regs<0>(LU, v);
    lu_bwd_regs<14>(LU, v);
    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
        x[i] = v[i];
    }
}
DEVICE void lu_solve_batched(const Real* LU, const int* ipvt, Real* x) {

    // All pivot rows are loaded up front (independent of the x updates), so
    // each forward step waits on one memory round trip instead of two.
    int pv[14];
    [[loom::unroll]] for (unsigned k = 0; k < 14u; ++k) {
        pv[k] = ipvt[k];
    }
    lu_fwd_batched<0>(LU, pv, x);
    Real v[15];
    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
        v[i] = x[i];
    }
    lu_bwd_regs<14>(LU, v);
    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
        x[i] = v[i];
    }

}
DEVICE void lu_solve(const Real* LU, const int* ipvt, Real* x) {

    lu_solve_forward<0>(LU, ipvt, x);
    lu_solve_backward<14>(LU, x, x[14]);

}
DEVICE int decompose(ScratchView s, Real fac) {

        Real* A = s.e;
        // ip[14] is never read by the solve; the A(14,14) == 0 check is done
        // in lu_step<13> on the register value.
        int info = lu_step0_from_jac(s.fjac, A, s.ip, fac);
        // ip now holds the composite row permutation (all 15 entries).
        store_permutation(s.ip);
        return info;

}
DEVICE void solve(ScratchView s, Real* ak) {

        lu_solve_perm(s.e, s.ip, ak);

}
DEVICE Real error_norm(ScratchView s) {

        Real err = 0.0;
        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
            const Real sk = s.atol_vec[i] + s.rtol_vec[i] * math::max(math::abs(s.y[i]), math::abs(s.ynew[i]));
            const Real term = s.work[i] / sk;
            err += term * term;
        }
        return math::sqrt(err / static_cast<Real>(N));

}
// Final ROS2S combination with the error norm formed in registers: yacc gets
// the new state; same operations and summation order as the combination loop
// followed by error_norm.
// The tolerances are the compile-time values configure_ros2s stores into
// atol_vec/rtol_vec (never changed afterwards), used directly here.
DEVICE Real combine_and_error_norm(ScratchView s, Real* yacc) {
    Real ev[15];
    Real yo[15];
    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
        const Real ak3i = s.work[i];
        yo[i] = s.y[i];
        yacc[i] = yo[i] + C::b1 * s.ak1[i] + C::b2 * s.ak2[i] + C::b3 * ak3i;
        ev[i] = C::e1 * s.ak1[i] + C::e2 * s.ak2[i] + C::e3 * ak3i;
    }
    const Real t0 = ev[0] / (atol_spec + rtol_spec * math::max(math::abs(yo[0]), math::abs(yacc[0])));
    const Real t1 = ev[1] / (atol_spec + rtol_spec * math::max(math::abs(yo[1]), math::abs(yacc[1])));
    const Real t2 = ev[2] / (atol_spec + rtol_spec * math::max(math::abs(yo[2]), math::abs(yacc[2])));
    const Real t3 = ev[3] / (atol_spec + rtol_spec * math::max(math::abs(yo[3]), math::abs(yacc[3])));
    const Real t4 = ev[4] / (atol_spec + rtol_spec * math::max(math::abs(yo[4]), math::abs(yacc[4])));
    const Real t5 = ev[5] / (atol_spec + rtol_spec * math::max(math::abs(yo[5]), math::abs(yacc[5])));
    const Real t6 = ev[6] / (atol_spec + rtol_spec * math::max(math::abs(yo[6]), math::abs(yacc[6])));
    const Real t7 = ev[7] / (atol_spec + rtol_spec * math::max(math::abs(yo[7]), math::abs(yacc[7])));
    const Real t8 = ev[8] / (atol_spec + rtol_spec * math::max(math::abs(yo[8]), math::abs(yacc[8])));
    const Real t9 = ev[9] / (atol_spec + rtol_spec * math::max(math::abs(yo[9]), math::abs(yacc[9])));
    const Real t10 = ev[10] / (atol_spec + rtol_spec * math::max(math::abs(yo[10]), math::abs(yacc[10])));
    const Real t11 = ev[11] / (atol_spec + rtol_spec * math::max(math::abs(yo[11]), math::abs(yacc[11])));
    const Real t12 = ev[12] / (atol_spec + rtol_spec * math::max(math::abs(yo[12]), math::abs(yacc[12])));
    const Real t13 = ev[13] / (atol_spec + rtol_spec * math::max(math::abs(yo[13]), math::abs(yacc[13])));
    const Real t14 = ev[14] / (atol_energy + rtol_energy * math::max(math::abs(yo[14]), math::abs(yacc[14])));
    const Real q0 = 0.0 + t0 * t0;
    const Real q1 = q0 + t1 * t1;
    const Real q2 = q1 + t2 * t2;
    const Real q3 = q2 + t3 * t3;
    const Real q4 = q3 + t4 * t4;
    const Real q5 = q4 + t5 * t5;
    const Real q6 = q5 + t6 * t6;
    const Real q7 = q6 + t7 * t7;
    const Real q8 = q7 + t8 * t8;
    const Real q9 = q8 + t9 * t9;
    const Real q10 = q9 + t10 * t10;
    const Real q11 = q10 + t11 * t11;
    const Real q12 = q11 + t12 * t12;
    const Real q13 = q12 + t13 * t13;
    const Real q14 = q13 + t14 * t14;
    return math::sqrt(q14 / static_cast<Real>(N));
}
#include "integrate.inc"
DEVICE void configure_ros2s(ScratchView s) {

    for (int n = 0; n < NumSpec; ++n) {
        s.rtol_vec[static_cast<size_type>(n)] = rtol_spec;
        s.atol_vec[static_cast<size_type>(n)] = atol_spec;
    }
    s.rtol_vec[NumSpec] = rtol_energy;
    s.atol_vec[NumSpec] = atol_energy;
    s.max_steps[0] = 10000000;

}
DEVICE int burn_ros2s(BurnRecord* b, Real duration, IntegratorStats* stats, Real* xnlane) {

    eos_rt(b);
    // Work arrays are fully written before they are read; no zero fill.

    Real t = 0.0;
    Real tout = 0.0;
    Real dt = 0.0;
    Real y[15];
    Real rtol_vec[15];
    Real atol_vec[15];
    Real uround = 1.e-16;
    Real fac_min = 0.2;
    Real fac_max = 6.0;
    Real safe = 0.9;
    Real ynew[15];
    Real kst[45];
    Real* ak1 = kst;
    Real* ak2 = kst + 15;
    Real* work = kst + 30;
    Real fjac[225];
    Real e[225];
    Real dy[15];
    Real burn_rho = 0.0;
    Real burn_T = 0.0;
    Real burn_e = 0.0;
    int n_step = 0;
    int n_rhs = 0;
    int n_jac = 0;
    int n_accept = 0;
    int n_reject = 0;
    int n_decomp = 0;
    int n_solve = 0;
    int max_steps = 100000;
    int ip[15];
    ScratchView s = {&t, &tout, &dt, y, rtol_vec, atol_vec, &uround, &fac_min, &fac_max, &safe, ynew, ak1, ak2, work, fjac, e, dy, {&burn_rho, &burn_T, &burn_e, xnlane}, &n_step, &n_rhs, &n_jac, &n_accept, &n_reject, &n_decomp, &n_solve, &max_steps, ip};
    configure_ros2s(s);

    s.t[0] = 0.0;
    s.tout[0] = duration;
    s.dt[0] = duration;
    for (int n = 0; n < NumSpec; ++n) {
        s.y[static_cast<size_type>(n)] = b->xn[static_cast<size_type>(n)];
    }
    s.y[NumSpec] = b->e;

    jac_nuc_structural_zeros(s.fjac);
    const auto result = integrate(s);

    stats->internal_steps += static_cast<u64>(math::max(0, s.n_step[0]));
    stats->rhs_calls += static_cast<u64>(math::max(0, 3 * s.n_step[0]));
    stats->jacobian_calls += static_cast<u64>(math::max(0, s.n_accept[0] + (result == LU_DECOMPOSITION_ERROR ? 1 : 0)));
    stats->decompositions += static_cast<u64>(math::max(0, s.n_step[0]));
    stats->linear_solves += static_cast<u64>(math::max(0, 3 * s.n_step[0]));
    stats->accepted_steps += static_cast<u64>(math::max(0, s.n_accept[0]));
    stats->rejected_steps += static_cast<u64>(math::max(0, s.n_accept[0] >= 1 ? s.n_step[0] - s.n_accept[0] - s.n_reject[0] + 1 : 0));

    if (result != SUCCESS) {
        return result;
    }

    for (int n = 0; n < NumSpec; ++n) {
        b->xn[static_cast<size_type>(n)] = s.y[static_cast<size_type>(n)];
    }
    b->e = s.y[NumSpec];
    return result;

}
DEVICE u64 splitmix64(u64 value) {

    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);

}
DEVICE Real perturbation_factor(int cell, int step) {

    const auto seed = (static_cast<u64>(static_cast<unsigned int>(cell)) << 32U) ^
                      (static_cast<u64>(static_cast<unsigned int>(step)) << 16U);
    const auto bits = splitmix64(seed) >> 11U;
    const auto unit = static_cast<Real>(bits) *
                      (1.0 / static_cast<Real>(1ULL << 53U));
    return 1.0 + perturbation_amplitude * (2.0 * unit - 1.0);

}
DEVICE bool valid_positive(Real value) {

    return value > 0.0 && math::isfinite(value);

}
DEVICE void apply_perturbation(CellRecord* record, int cell, int step, bool enabled, Real* mass) {
BurnRecord* b = &record->current;

    if (!enabled || step == 0 || static_cast<unsigned>(step) % static_cast<unsigned>(perturbation_interval) != 0) {
        return;
    }

    const Real factor = perturbation_factor(cell, step);
    record->density_driver *= factor;
    b->rho *= factor;
    for (int n = 0; n < NumSpec; ++n) {
        b->xn[n] *= factor;
    }

    floor_and_normalize_number_densities(b, mass);
    balance_charge(b);
    floor_and_normalize_number_densities(b, mass);
    eos_re(b);

}
DEVICE Real collapse_timestep(const BurnRecord* b) {

    const Real rho = density(b->xn);
    if (!valid_positive(rho)) {
        return -1.0;
    }
    const Real tff = math::sqrt(pi * 3.0 / (32.0 * rho * grav_constant));
    if (!valid_positive(tff)) {
        return -1.0;
    }
    const Real dt = tff_reduc * tff;
    return valid_positive(dt) ? dt : -1.0;

}
KERNEL void prepare_grid_timestep_kernel(CellRecord* cells, int num_cells, int completed_global_steps, Real grid_time, int step, bool perturb, Real* dt_candidates, int* failure_code CELL_PARAMETER) {


    const int cell = CELL_INDEX;

    if (cell >= num_cells) {
        return;
    }
    dt_candidates[cell] = MAX_DOUBLE;
    if (*failure_code != static_cast<int>(SUCCESS)) {
        return;
    }

    CellRecord* record = cells + cell;
BurnRecord* b = &record->current;
Real mass[14];
    if (record->completed_steps < completed_global_steps) {
        return;
    }
    if (record->completed_steps != completed_global_steps || record->time != grid_time) {
        atomic_cas(failure_code, static_cast<int>(SUCCESS),
                  static_cast<int>(BAD_INPUTS));
        return;
    }

    apply_perturbation(record, cell, step, perturb, mass);
    const Real dt = collapse_timestep(b);
    if (!valid_positive(dt) || !valid_positive(record->density_driver)) {
        atomic_cas(failure_code, static_cast<int>(SUCCESS),
                  static_cast<int>(BAD_INPUTS));
        return;
    }
    if (dt < 10.0) {
        const Real tff = dt / tff_reduc;
        record->density_driver += dt * (record->density_driver / tff);
        return;
    }
    dt_candidates[cell] = dt;

}
KERNEL void advance_collapse_gridwide_kernel(CellRecord* cells, int num_cells, int completed_global_steps, Real next_grid_time, Real dt_grid, int* integrated_count, int* failure_code CELL_PARAMETER) {
    WORKGROUP_ARRAY(Real, xn_lds, 14 * 128);


    const int cell = CELL_INDEX;

    if (cell >= num_cells ||
        *failure_code != static_cast<int>(SUCCESS)) {
        return;
    }

    CellRecord* record = cells + cell;
BurnRecord* b = &record->current;
Real mass[14];
    if (record->completed_steps != completed_global_steps) {
        return;
    }

    const Real local_dt = collapse_timestep(b);
    if (local_dt < 10.0) {
        return;
    }
    const Real old_density = record->density_driver;
    const Real tff = local_dt / tff_reduc;
    record->density_driver += dt_grid * (record->density_driver / tff);
    if (!valid_positive(record->density_driver) || record->density_driver > 2.0e-6) {
        return;
    }

    const Real density_ratio = record->density_driver / old_density;
    for (int n = 0; n < NumSpec; ++n) {
        b->xn[n] *= density_ratio;
    }
    b->rho *= density_ratio;

    auto failure = SUCCESS;
    const auto result = burn_ros2s(b, dt_grid, &record->stats, xn_lds + LANE_INDEX);
    if (result != SUCCESS) {
        failure = result;
    } else {
        floor_and_normalize_number_densities(b, mass);
        balance_charge(b);
        floor_and_normalize_number_densities(b, mass);
        eos_re(b);
        record->time = next_grid_time;
        record->completed_steps = completed_global_steps + 1;
        atomic_add(integrated_count, 1);
    }
    if (failure != SUCCESS) {
        atomic_cas(failure_code, static_cast<int>(SUCCESS),
                  static_cast<int>(failure));
    }

}
} // namespace chemistry
