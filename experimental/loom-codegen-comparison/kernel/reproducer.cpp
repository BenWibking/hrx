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

    [[loom::unroll]] for (unsigned n = 0; n < 14u; ++n) {
        sum_abarinv += xn[n];
        sum_gammasinv += (xn[n] * protonmass / rhotot) * (1.0 / (species_gamma(static_cast<int>(n)) - 1.0));
    }

    sum_abarinv *= protonmass / rhotot;
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

    const EosSums sums = eos_sums_from_number_densities(b.xn);
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
   3.3178155742407614e-14*math::exp((1.1394493358416311)*math::log(math::abs(T)))*math::exp(-10.993097527150175*x10 + 14.449862906216714*x11 + 58.228375789703179*x12 - 162.59852239006702*x13 + 144.55426734953477*x14 - 44.454280878123605*x15 - 12.447178055372778*x8 + 6.9391784778399117*x9)
));
}
DEVICE auto rhs_specie_choice_2(Real T, Real x32, Real x33, Real x34) {
return ((x33 <= 9280.0) ? (
   x32
)
: (
   1250086.112245841*math::exp((-1.5)*math::log(math::abs(T)))*(1.5400000000000001e-9 + 4.6200000000000001e-10*math::exp(-93988.701501924661*x34))*math::exp(-469943.50750964211*x34) + x32
));
}
DEVICE auto rhs_specie_choice_3(Real T, Real x10, Real x11, Real x12, Real x13, Real x14, Real x15, Real x32, Real x33, Real x8, Real x9) {
return ((x33 <= 5500.0) ? (
   x32
)
: (
   3.2867337024382687e-10*math::exp((-0.72411256578268512)*math::log(math::abs(T)))*math::exp(-2.4649195146505534*x10 - 1.020773727011937*x11 + 3.3530579587656564*x12 + 3.6203127646377791*x13 - 1.0930705283186732*x14 - 1.6921001126637107*x15 - 1.774686809424741*x8 - 1.951835616513679*x9)
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
return ((T >= 100.0 & T <= 30000.0) ? (
   (-1.4491368e-7*x52 + 3.4172804999999998e-8*x56 + 3.5311931999999998e-13*((x6)*(x6)*(x6)*(x6)*(x6)*(x6)*(x6)) - 1.8171411000000001e-11*math::exp((6)*math::log(math::abs(x6))) + 3.3735381999999997e-7*x6 - 4.7813727999999997e-9*x60 + 3.9731542e-10*x61 - 3.3232183000000002e-7)*math::exp(-21237.150000000001*x34)
)
: (
   0
));
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
return ((x73) ? (
   2.0000000000000001e-10*math::exp((0.40200000000000002)*math::log(math::abs(T)))*math::exp(-37.100000000000001*x34) - 3.3099999999999998e-17*math::exp((1.48)*math::log(math::abs(T)))
)
: (
   0
));
}
DEVICE auto rhs_specie_choice_9(Real T, Real x34, bool x73) {
return ((x73) ? (
   2.0299999999999998e-9*math::exp((-0.33200000000000002)*math::log(math::abs(T))) + 2.0600000000000001e-10*math::exp((0.39600000000000002)*math::log(math::abs(T)))*math::exp(-33.0*x34)
)
: (
   0
));
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


    Real T = temperature;

    Real x0 = math::exp((-0.75)*math::log(math::abs(T)));

    Real x1 = 2.5950363272655348e-10*xn[0]*xn[4]*x0;

    Real x2 = 1.3300135414628029e-18*math::exp((0.94999999999999996)*math::log(math::abs(T)))*xn[0]*xn[5]*math::exp(-0.00010729613733905579*T);

    Real x3 = ((T)*(T));

    Real x4 = T <= 10000.0;

    Real x5 = xn[0]*xn[6]*rhs_specie_choice_0(T, x3, x4);

    Real x6 = math::log(T);

    Real x7 = 0.10684732509875319*x6 - 1;

    Real x8 = ((x7)*(x7));

    Real x9 = ((x7)*(x7)*(x7));

    Real x10 = ((((x7)*(x7)))*(((x7)*(x7))));

    Real x11 = ((x7)*(x7)*(x7)*(x7)*(x7));

    Real x12 = math::exp((6)*math::log(math::abs(x7)));

    Real x13 = ((x7)*(x7)*(x7)*(x7)*(x7)*(x7)*(x7));

    Real x14 = math::exp((8)*math::log(math::abs(x7)));

    Real x15 = ((x7)*(x7)*(x7)*(x7)*(x7)*(x7)*(x7)*(x7)*(x7));

    Real x16 = xn[2]*xn[3];

    Real x17 = x16*rhs_specie_choice_1(T, x10, x11, x12, x13, x14, x15, x8, x9);

    Real x18 = xn[1]*xn[3];

    Real x19 = 1.0e-8*math::exp((-0.40000000000000002)*math::log(math::abs(T)))*x18;

    Real x20 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));

    Real x21 = xn[3]*xn[5];

    Real x22 = x20*x21;

    Real x23 = xn[0]*xn[2];

    Real x24 = 1.4000000000000001e-18*math::exp((0.92800000000000005)*math::log(math::abs(T)))*x23*math::exp(-6.1728395061728397e-5*T);

    Real x25 = xn[0]*xn[12];

    Real x26 = 3.8571873359681582e-209*math::exp((43.933476326349997)*math::log(math::abs(T)))*x25*math::exp(-5902.1601240760483*x10 + 5825.9326359379538*x11 - 3578.1439181805954*x12 + 1242.7294446825149*x13 - 186.35635455381879*x14 - 1618.789587733125*x8 + 3854.4033653120223*x9);

    Real x27 = 3.7903999274394518e-18*math::exp((2.360852208681)*math::log(math::abs(T)))*xn[0]*xn[3]*math::exp(-258.18559308467115*x10 + 846.15238706523724*x11 - 1113.0879095147111*x12 + 671.95094388835207*x13 - 154.90262957142161*x14 - 24.766609674457612*x8 + 13.307984239358756*x9);

    Real x28 = math::sqrt(T);

    Real x29 = 1.0/x28;

    Real x30 = xn[0]*x29;

    Real x31 = 5.7884371785482823e-10*xn[11]*x30*math::exp((-1.7524)*math::log(math::abs(0.00060040841663220993*x28 + 1.0)))*math::exp((-0.24759999999999999)*math::log(math::abs(0.32668576019240059*x28 + 1.0)));

    Real x32 = 1.4981088130721367e-10*math::exp((-0.63529999999999998)*math::log(math::abs(T)));

    Real x33 = 8.6173430000000006e-5*T;

    Real x34 = 1.0/T;

    Real x35 = -4.3524079114767552e-117*math::exp((23.915965629999999)*math::log(math::abs(T)))*xn[0]*xn[13]*math::exp(-4361.9927099007555*x10 + 4879.7345146260486*x11 - 3366.4639698826941*x12 + 1300.3028484326148*x13 - 214.82451513312137*x14 - 941.91483008144996*x8 + 2506.9866529060901*x9) + x25*rhs_specie_choice_2(T, x32, x33, x34);

    Real x36 = -5.9082438637265071e-70*math::exp((13.536555999999999)*math::log(math::abs(T)))*x23*math::exp(-2207.4643501257692*x10 + 2500.8077583366976*x11 - 1768.8867461266502*x12 + 704.19926629500367*x13 - 120.0438480494693*x14 - 502.72883252679094*x8 + 1281.477767828706*x9) + xn[0]*xn[1]*rhs_specie_choice_3(T, x10, x11, x12, x13, x14, x15, x32, x33, x8, x9);

    Real x37 = 7.1999999999999996e-8*xn[9]*x30;

    Real x38 = xn[2]*xn[7];

    Real x39 = x20*x38;

    Real x40 = x16*(1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T))))/(0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0);

    Real x41 = xn[0]*xn[8];

    Real x42 = 35.5*math::exp((-2.2799999999999998)*math::log(math::abs(T)))*x41*math::exp(-46707.0*x34);

    Real x43 = x37 - x39 - x40 + x42;

    Real x44 = M_LN10;

    Real x45 = 1.0/x44;

    Real x46 = x45*x6;

    Real x47 = math::exp((-0.12690000000000001*math::exp((-3.0)*math::log(math::abs(x44)))*((x6)*(x6)*(x6)) + 1.1180000000000001*math::exp((-2.0)*math::log(math::abs(x44)))*((x6)*(x6)) - 1.5229999999999999*x46 - 19.379999999999999)*math::log(math::abs(10.0)));

    Real x48 = xn[1]*xn[5];

    Real x49 = x47*x48;

    Real x50 = x18*rhs_specie_choice_4(T, x28, x29);

    Real x51 = 1.0000000000000001e-9*xn[1]*xn[10]*math::exp(-457.0*x34);

    Real x52 = ((x6)*(x6));

    Real x53 = math::exp((-2)*math::log(math::abs(x44)));

    Real x54 = x52*x53;

    Real x55 = 8.4600000000000008e-10*x46 - 1.3700000000000002e-10*x54 + 4.1700000000000001e-10;

    Real x56 = ((x6)*(x6)*(x6));

    Real x57 = x56/((x44)*(x44)*(x44));

    Real x58 = xn[1]*xn[2]*rhs_specie_choice_5(T, x46, x52, x53, x57);

    Real x59 = 6.0e-10*xn[2]*xn[6];

    Real x60 = ((((x6)*(x6)))*(((x6)*(x6))));

    Real x61 = ((x6)*(x6)*(x6)*(x6)*(x6));

    Real x62 = xn[1]*xn[8]*rhs_specie_choice_6(T, x34, x52, x56, x6, x60, x61);

    Real x63 = -x59 + x62;

    Real x64 = x19 + x58 + x63;

    Real x65 = 6.3999999999999996e-10*xn[2]*xn[9];

    Real x66 = -x65;

    Real x67 = math::exp((-0.5)*math::log(math::abs(T)));

    Real x68 = xn[7]*x67;

    Real x69 = 7.9674337148168363e-7*xn[1]*x68;

    Real x70 = xn[1]*xn[13]*rhs_specie_choice_7(T, x0, x34, x4);

    Real x71 = math::exp((0.25)*math::log(math::abs(T)));

    Real x72 = 2.8833736969617052e-16*xn[12]*xn[2]*x71;

    Real x73 = T >= 50.0;

    Real x74 = x48*rhs_specie_choice_8(T, x34, x73);

    Real x75 = xn[2]*xn[4];

    Real x76 = x75*rhs_specie_choice_9(T, x34, x73);

    Real x77 = x74 - x76;

    Real x78 = x36 + x66 + x69 + x70 - x72 + x77;

    Real x79 = 4.9999999999999996e-6*xn[3]*xn[6]*x29;

    Real x80 = ((xn[2])*(xn[2])*(xn[2]));

    Real x81 = 1.0/x71;

    Real x82 = 1.0e-25*xn[2]*xn[5];

    Real x83 = xn[1] + xn[10] + xn[2] + xn[3] + 2.0*xn[6] + 2.0*xn[8] + xn[9];

    Real x84 = -133.82830000000001*x34 - 4.8909149999999997*x45*x6 + 0.47490300000000002*x54;

    Real x85 = math::exp(-0.0022727272727272726*T);

    Real x86 = math::exp(-0.00054054054054054055*T);

    Real x87 = -2.0563129999999998*x85 + 0.58640729999999996*x86 + 0.82274429999999998;

    Real x88 = -69.700860000000006*x45*math::log(40870.379999999997*x34 + 1.0) + 4.6331670000000003*x57;

    Real x89 = math::exp((-23705.700000000001*x34 - 2080.4099999999999*x34/(math::exp((x87)*math::log(math::abs(math::exp((-x84 - 13.656822)*math::log(math::abs(10.0)))*x83))) + 1.0) - 68.422430000000006*x46 + 43.20243*x52*x53 - x88 - 178.4239 - (19.734269999999999*x45*math::log(16780.950000000001*x34 + 1.0) - 14.509090000000008*x46 + 37.886913*x52*x53 - x88 - 307.31920000000002)/(math::exp((x87)*math::log(math::abs(math::exp((-x84 - 14.82123)*math::log(math::abs(10.0)))*x83))) + 1.0))*math::log(math::abs(10.0)));

    Real x90 = 743.05999999999995*x34 - 2.4640089999999999*x45*x6 + 0.19859550000000001*x54;

    Real x91 = 2.9375070000000001*x85 + 0.23588480000000001*x86 + 0.75022860000000002;

    Real x92 = -21.360939999999999*x45*math::log(27535.310000000001*x34 + 1.0) + 0.25820969999999999*x57;

    Real x93 = math::exp((-21467.790000000001*x34 - 1657.4099999999999*x34/(math::exp((x91)*math::log(math::abs(math::exp((-x90 - 8.1313220000000008)*math::log(math::abs(10.0)))*x83))) + 1.0) + 42.707410000000003*x45*x6 - 2.0273650000000001*x54 - x92 - 142.7664 - (70.138370000000009*x45*x6 + 11.28215*x45*math::log(14254.549999999999*x34 + 1.0) - 4.7035149999999994*x54 - x92 - 203.11568)/(math::exp((x91)*math::log(math::abs(math::exp((-x90 - 9.3055640000000004)*math::log(math::abs(10.0)))*x83))) + 1.0))*math::log(math::abs(10.0)));

    Real x94 = xn[2]*xn[8];

    Real x95 = 5.0000000000000004e-32*x67 + 1.5e-32*x81;

    Real x96 = ((xn[2])*(xn[2]))*xn[8];

    Real x97 = x47*x75;

    Real x98 = 1.0/(math::exp((1.3*x45*(x6 - 9.2103403719761836) - 137.42519902360013*x53*((0.10857362047581294*x6 - 1)*(0.10857362047581294*x6 - 1)) - 4.8449999999999998)*math::log(math::abs(10.0)))*x83 + 1.0);

    Real x99 = ((xn[8])*(xn[8]))*math::exp((x98)*math::log(math::abs(1.1800000000000001e-10*math::exp(-69500.0*x34))))*math::exp((1.0 - x98)*math::log(math::abs(8.1250000000000003e-8*x67*(1.0 - math::exp(-6000.0*x34))*math::exp(-52000.0*x34))));

    Real x100 = math::exp((0.34999999999999998)*math::log(math::abs(T)))*x41*math::exp(-102000.0*x34);

    Real x101 = xn[5]*xn[8]*rhs_specie_choice_10(T, x34, x44, x46, x54, x57, x60, x61);

    Real x102 = xn[10]*xn[2]*rhs_specie_choice_11(T, x3, x34);

    Real x103 = x101 - x102;

    Real x104 = 6.1739095063118665e-10*math::exp((0.40999999999999998)*math::log(math::abs(T)));

    Real x105 = x104*x21;

    Real x106 = x104*x38;

    Real x107 = x105 - x106;

    Real x108 = x107 + x17 - x24 + x27;

    Real x109 = x80*(2.0000000000000002e-31*x67 + 6.0000000000000001e-32*x81) + x94*(-x89 - x93);

    Real x110 = x40 - x42 + x79;

    Real x111 = 9.8726896031426014e-7*xn[4]*x68;

    Real x112 = -x51;

    Real x113 = -x55;

    Real x114 = -x37 + x49;

    Real x115 = x103 + x22 + x82;

    Real x116 = xn[4]*xn[8];

    Real x117 = x26 - x31;

    Real x118 = x35 - x70 + x72;

    out[0] = -x1 + x17 + x19 - x2 + x22 - x24 + x26 + x27 - x31 - x35 - x36 - x43 - x5;

    out[1] = xn[4]*xn[8]*x55 - x49 - x50 - x51 - x64 - x78;

    out[2] = 8.7599999999999997e-10*x100 + x103 + x108 + x109 + x43 + 2*x5 + 2*x50 - x58 + x63 + x78 + x79 + x80*(-6.0000000000000005e-31*x67 - 1.8e-31*x81) - x82 + x94*(3*x89 + 3*x93) - x95*x96 - x97 + 2*x99;

    out[3] = -x108 - x110 - x19 - x22 - x50;

    out[4] = xn[4]*xn[8]*x113 - x1 - x111 - x112 + x74 - x76 - x97;

    out[5] = 1.9745379206285203e-6*xn[4]*xn[7]*x67 + x1 - x107 - x114 - x115 - x2 + x69 - x77;

    out[6] = -x5 + x64 - x79;

    out[7] = x105 - x106 - x111 + x2 - x39 - x69;

    out[8] = -4.3799999999999999e-10*x100 - x101 + x102 + x109 + x110 + x113*x116 + x51 + x59 - x62 + x95*x96 + x96*(-2.5000000000000002e-32*x67 - 7.5000000000000001e-33*x81) - x99;

    out[9] = x114 + x66 + x97;

    out[10] = x112 + x115 + x116*x55 + x39 + x65;

    out[11] = x117;

    out[12] = -x117 - x118;

    out[13] = x118;




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
return ((x6) ? (
   0.63095734448019325
)
: (
   math::exp((-0.20000000000000001)*math::log(math::abs(T)))
));
}
DEVICE auto rhs_eint_choice_3(Real T, bool x6) {
return ((x6) ? (
   5.011872336272722
)
: (
   math::exp((0.69999999999999996)*math::log(math::abs(T)))
));
}
DEVICE auto rhs_eint_choice_4(Real T, bool x6) {
return ((x6) ? (
   0.67810976749343443
)
: (
   math::exp((-0.16869999999999999)*math::log(math::abs(T)))
));
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
   xn[2] >= 10000000000.0
));
}
DEVICE auto rhs_eint_choice_7(bool x46, const Real* xn) {
return ((x46) ? (
      0.01
   )
   : (
      xn[2]
   ));
}
DEVICE auto rhs_eint_choice_8(bool x46, const Real* xn) {
return ((rhs_eint_choice_6(x46, xn)) ? (
   10000000000.0
)
: (
   rhs_eint_choice_7(x46, xn)
));
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
return ((x105) ? (
   x104
)
: rhs_eint_choice_19(x106, x107, x108, x109, x111));
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
return ((x55 & x57) ? (
                              2.1648287161311648e-38/((x63*x69 + 1.471335691176954e-59)*(x63*x69 + 1.471335691176954e-59)) >= 1.0
                           )
                           : (
                              rhs_eint_choice_21(x57, x64, x69)
                           ));
}
DEVICE auto rhs_eint_choice_23(bool x55, bool x57, bool x61, Real x63, Real x64, Real x69) {
return ((x61) ? (
                           true
                        )
                        : (
                           rhs_eint_choice_22(x55, x57, x63, x64, x69)
                        ));
}
DEVICE auto rhs_eint_choice_24(bool x55, bool x57, bool x61, Real x63, Real x64, Real x67, Real x69) {
return ((x57 & x61) ? (
                        4.833962023629485e-72/((x64*x67 + 2.1986273043946045e-76)*(x64*x67 + 2.1986273043946045e-76)) >= 1.0
                     )
                     : (
                        rhs_eint_choice_23(x55, x57, x61, x63, x64, x69)
                     ));
}
DEVICE auto rhs_eint_choice_25(bool x55, bool x57, bool x61, Real x63, Real x64, Real x67, Real x69) {
return ((x55 & x57 & x61) ? (
                     4.833962023629485e-72/((x63*x67 + 2.1986273043946045e-76)*(x63*x67 + 2.1986273043946045e-76)) >= 1.0
                  )
                  : (
                     rhs_eint_choice_24(x55, x57, x61, x63, x64, x67, x69)
                  ));
}
DEVICE auto rhs_eint_choice_26(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x67, Real x69) {
return ((x60) ? (
                  true
               )
               : (
                  rhs_eint_choice_25(x55, x57, x61, x63, x64, x67, x69)
               ));
}
DEVICE auto rhs_eint_choice_27(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
return ((x57 & x60) ? (
               216.48287161311649/((x64*x66 + 1.471335691176954e-39)*(x64*x66 + 1.471335691176954e-39)) >= 1.0
            )
            : (
               rhs_eint_choice_26(x55, x57, x60, x61, x63, x64, x67, x69)
            ));
}
DEVICE auto rhs_eint_choice_28(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
return ((x55 & x57 & x60) ? (
            216.48287161311649/((x63*x66 + 1.471335691176954e-39)*(x63*x66 + 1.471335691176954e-39)) >= 1.0
         )
         : (
            rhs_eint_choice_27(x55, x57, x60, x61, x63, x64, x66, x67, x69)
         ));
}
DEVICE auto rhs_eint_choice_29(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
return ((x60 & x61) ? (
         true
      )
      : (
         rhs_eint_choice_28(x55, x57, x60, x61, x63, x64, x66, x67, x69)
      ));
}
DEVICE auto rhs_eint_choice_30(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
return ((x57 & x60 & x61) ? (
      4.8339620236294848e-32/((x64 + 2.1986273043946046e-56)*(x64 + 2.1986273043946046e-56)) >= 1.0
   )
   : (
      rhs_eint_choice_29(x55, x57, x60, x61, x63, x64, x66, x67, x69)
   ));
}
DEVICE auto rhs_eint_choice_31(bool x55, bool x57, bool x60, bool x61, Real x63, Real x64, Real x66, Real x67, Real x69) {
return ((x55 & x57 & x60 & x61) ? (
   4.8339620236294848e-32/((x63 + 2.1986273043946046e-56)*(x63 + 2.1986273043946046e-56)) >= 1.0
)
: (
   rhs_eint_choice_30(x55, x57, x60, x61, x63, x64, x66, x67, x69)
));
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
return ((x6) ? (
   0.4008667176273028
)
: (
   math::exp((-0.39700000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto rhs_eint_choice_38(Real T, bool x6) {
return ((x6) ? (
   0.031622776601683791
)
: (
   math::exp((-1.5)*math::log(math::abs(T)))
));
}
DEVICE auto rhs_eint_choice_39(Real T, bool x6) {
return ((x6) ? (
   2.3157944032250755
)
: (
   math::exp((0.36470000000000002)*math::log(math::abs(T)))
));
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
return ((x118 & x73) ? (
                                                   x120 & x151
                                                )
                                                : (
                                                   rhs_eint_choice_82(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                                ));
}
DEVICE auto rhs_eint_choice_84(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x118 & x73) ? (
                                                x120 & x150
                                             )
                                             : (
                                                rhs_eint_choice_83(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                             ));
}
DEVICE auto rhs_eint_choice_85(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x118 & x73) ? (
                                             x120 & x149
                                          )
                                          : (
                                             rhs_eint_choice_84(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                          ));
}
DEVICE auto rhs_eint_choice_86(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x118 & x73) ? (
                                          x120 & x148
                                       )
                                       : (
                                          rhs_eint_choice_85(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                       ));
}
DEVICE auto rhs_eint_choice_87(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x118 & x73 & x85 & x97) ? (
                                       x120 & x147
                                    )
                                    : (
                                       rhs_eint_choice_86(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                    ));
}
DEVICE auto rhs_eint_choice_88(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x118 & x73 & x85 & x97) ? (
                                    x120 & x146
                                 )
                                 : (
                                    rhs_eint_choice_87(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                                 ));
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
return ((x105 & x118 & x73 & x85 & x97) ? (
                              x120 & x144
                           )
                           : (
                              rhs_eint_choice_89(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                           ));
}
DEVICE auto rhs_eint_choice_91(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x114 & x115 & x73) ? (
                           x120 & x142
                        )
                        : (
                           rhs_eint_choice_90(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                        ));
}
DEVICE auto rhs_eint_choice_92(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x114 & x115 & x73) ? (
                        x120 & x140
                     )
                     : (
                        rhs_eint_choice_91(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                     ));
}
DEVICE auto rhs_eint_choice_93(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x107 & x114 & x115 & x73) ? (
                     x120 & x138
                  )
                  : (
                     rhs_eint_choice_92(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
                  ));
}
DEVICE auto rhs_eint_choice_94(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x105 & x114 & x115 & x73) ? (
                  x120 & x135
               )
               : (
                  rhs_eint_choice_93(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
               ));
}
DEVICE auto rhs_eint_choice_95(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x114 & x115 & x73 & x85 & x97) ? (
               x120 & x132
            )
            : (
               rhs_eint_choice_94(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
            ));
}
DEVICE auto rhs_eint_choice_96(bool x105, bool x107, bool x109, bool x114, bool x115, bool x116, bool x118, bool x120, bool x125, bool x128, bool x130, bool x132, bool x135, bool x138, bool x140, bool x142, bool x144, bool x145, bool x146, bool x147, bool x148, bool x149, bool x150, bool x151, bool x152, bool x153, bool x154, bool x155, bool x156, bool x157, bool x158, bool x159, bool x160, bool x161, bool x73, bool x85, bool x88, bool x97, bool x98) {
return ((x109 & x114 & x115 & x73 & x85 & x97) ? (
            x120 & x130
         )
         : (
            rhs_eint_choice_95(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
         ));
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
return ((x105 & x114 & x115 & x73 & x85 & x97) ? (
      x120 & x125
   )
   : (
      rhs_eint_choice_97(x105, x107, x109, x114, x115, x116, x118, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x97, x98)
   ));
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
return ((T < 2.0) ? (
   0
)
: (
   xn[8]*rhs_eint_choice_42(x70, x71)*rhs_eint_choice_99(x105, x107, x109, x114, x115, x116, x118, x119, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x73, x85, x88, x89, x97, x98)
));
}
DEVICE auto rhs_eint_choice_101(Real T, Real x25, Real x26, Real x35, Real x41, Real x42, Real x43, Real x44, Real x45, Real x47, Real x48, Real x49, Real x5, Real x50, Real x51, Real x52, Real x53, Real x54, const Real* xn) {
return ((T >= x5) ? (
   math::exp((21.93385*x25*x41 + 0.92432999999999998*x25*x47 + 0.77951999999999999*x26*x41*x47 - 10.19097*x26*x45 + 0.54962*x26*x50 - 1.06447*x35*x41*x50 + 2.1990599999999998*x35*x44 - 0.54262999999999995*x35*x45*x47 - 0.076759999999999995*x35*x49 + 0.11864*x41*x42*x49 - 0.0036600000000000001*x41*x48*x51 - 0.17333999999999999*x42*x43 + 0.11711000000000001*x42*x44*x47 + 0.62343000000000004*x42*x45*x50 + 0.0027499999999999998*x42*x48 - 0.0083499999999999998*x43*x47*x51 + 6.1920000000000003e-5*x43*x48*x52 - 0.001482*x43*x49*x53 + 0.0106*x43*x50*x54 - 0.00066631000000000004*x44*x48*x53 + 0.017590000000000001*x44*x49*x54 - 0.13768*x44*x50*x51 + 0.0025140000000000002*x45*x48*x54 - 0.073660000000000003*x45*x49*x51 - 42.567880000000002)*math::log(math::abs(10.0)))*xn[10]
)
: (
   0
));
}
DEVICE Real rhs_eint(Real temperature, const Real* xn, Real z) {


    Real T = temperature;
        Real x0 = 9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9];

    Real x1 = 1.0/x0;

    Real x2 = math::sqrt(T);

    Real x3 = xn[1] + xn[12] + xn[4];

    Real x4 = 1.0/T;

    Real x5 = 2.73*z + 2.73;

    Real x6 = T <= 10;

    Real x7 = xn[0]*xn[12];

    Real x8 = rhs_eint_choice_0(x4, x6);

    Real x9 = math::sqrt(T);

    Real x10 = rhs_eint_choice_1(x6, x9);

    Real x11 = 1.0/(0.0031622776601683794*x10 + 1.0);

    Real x12 = xn[0]*x11;

    Real x13 = xn[2]*x12;

    Real x14 = xn[0]*x10*rhs_eint_choice_2(T, x6)/(6.3095734448019361e-5*rhs_eint_choice_3(T, x6) + 1.0);

    Real x15 = 1.0/x2;

    Real x16 = xn[10] + xn[2] + xn[3] + xn[9];

    Real x17 = xn[1] + 2.0*xn[6] + 2.0*xn[8] + x16;

    Real x18 = 1.0/(1000000.0*x15/(x17*(1.6000000000000001*xn[2]*math::exp(-160000.0/((T)*(T))) + 1.3999999999999999*xn[8]*math::exp(-12000.0/(T + 1200.0)))) + 1.0);

    Real x19 = x11*x7;

    Real x20 = ((xn[0])*(xn[0]))*xn[12]*x11*rhs_eint_choice_4(T, x6);

    Real x21 = math::exp((-0.25)*math::log(math::abs(T)));

    Real x22 = math::sqrt(M_PI);

    Real x23 = math::log(T);

    Real x24 = M_LN10;

    Real x25 = 1.0/x24;

    Real x26 = math::exp((-2)*math::log(math::abs(x24)));

    Real x27 = 1.0/(math::exp((1.3*x25*(x23 - 9.2103403719761836) - 137.42519902360013*x26*((0.10857362047581294*x23 - 1)*(0.10857362047581294*x23 - 1)) - 4.8449999999999998)*math::log(math::abs(10.0)))*x17 + 1.0);

    Real x28 = ((x23)*(x23));

    Real x29 = x26*x28;

    Real x30 = -4.8909149999999997*x23*x25 + 0.47490300000000002*x29 - 133.82830000000001*x4;

    Real x31 = math::exp(-0.0022727272727272726*T);

    Real x32 = math::exp(-0.00054054054054054055*T);

    Real x33 = -2.0563129999999998*x31 + 0.58640729999999996*x32 + 0.82274429999999998;

    Real x34 = x23*x25;

    Real x35 = math::powi<-3>(x24);

    Real x36 = ((x23)*(x23)*(x23))*x35;

    Real x37 = -69.700860000000006*x25*math::log(40870.379999999997*x4 + 1.0) + 4.6331670000000003*x36;

    Real x38 = -2.4640089999999999*x23*x25 + 0.19859550000000001*x29 + 743.05999999999995*x4;

    Real x39 = 2.9375070000000001*x31 + 0.23588480000000001*x32 + 0.75022860000000002;

    Real x40 = -21.360939999999999*x25*math::log(27535.310000000001*x4 + 1.0) + 0.25820969999999999*x36;

    Real x41 = math::log(rhs_eint_choice_5(T));

    Real x42 = math::exp((-4)*math::log(math::abs(x24)));

    Real x43 = ((((x41)*(x41)))*(((x41)*(x41))));

    Real x44 = ((x41)*(x41)*(x41));

    Real x45 = ((x41)*(x41));

    Real x46 = xn[2] <= 0.01;

    Real x47 = math::log(rhs_eint_choice_8(x46, xn));

    Real x48 = ((((x47)*(x47)))*(((x47)*(x47))));

    Real x49 = ((x47)*(x47)*(x47));

    Real x50 = ((x47)*(x47));

    Real x51 = math::powi<-5>(x24);

    Real x52 = math::exp((-8)*math::log(math::abs(x24)));

    Real x53 = math::powi<-7>(x24);

    Real x54 = math::exp((-6)*math::log(math::abs(x24)));

    Real x55 = x0 >= 0.5;

    Real x56 = 1.0000420000000001*x25;

    Real x57 = x0 >= 9.9999999999999998e-13;

    Real x58 = rhs_eint_choice_10(x0, x55, x56, x57);

    Real x59 = xn[0] + xn[11] + xn[13] + xn[5] + xn[6] + xn[7] + xn[8] + x16 + x3;

    Real x60 = x59 <= 9.9999999999999993e-41;

    Real x61 = x0 <= 9.9999999999999993e-41;

    Real x62 = x0*x22*x9;

    Real x63 = math::exp((2.1498900000000001 - 0.69317629274152892*x25)*math::log(math::abs(10.0)))*x62;

    Real x64 = math::exp((x56*math::log(x0) + 2.1498900000000001)*math::log(math::abs(10.0)))*x62;

    Real x65 = 0.00013612213614898791*xn[0] + 0.24994102282436673*xn[1] + 0.75007714496081457*xn[10] + 0.99972775572710437*xn[11] + 0.99986387786355213*xn[12] + xn[13] + 0.25007714496081457*xn[2] + 0.25021326709726244*xn[3] + 0.49986387786355219*xn[4] + 0.5*xn[5] + 0.50001816778518127*xn[6] + 0.50013612213644787*xn[7] + 0.50015428992162914*xn[8] + 0.7499410228243667*xn[9];

    Real x66 = 1.0/math::fabs(x65);

    Real x67 = math::sqrt(x59);

    Real x68 = math::exp((-2)*math::log(math::abs(x65)));

    Real x69 = math::sqrt(x59*x68);

    Real x70 = 1.2500000000000001e-10*xn[0] + 1.2500000000000001e-10*xn[1] + 1.2500000000000001e-10*xn[10] + 1.2500000000000001e-10*xn[11] + 1.2500000000000001e-10*xn[12] + 1.2500000000000001e-10*xn[13] + 1.2500000000000001e-10*xn[2] + 1.2500000000000001e-10*xn[3] + 1.2500000000000001e-10*xn[4] + 1.2500000000000001e-10*xn[5] + 1.2500000000000001e-10*xn[6] + 1.2500000000000001e-10*xn[7] + 1.2500000000000001e-10*xn[8] + 1.2500000000000001e-10*xn[9] <= 9.9999999999999993e-41;

    Real x71 = 28601.610899577994*math::exp((-0.45000000000000001)*math::log(math::abs(x59)));

    Real x72 = 4.985670872372847e-33*math::exp((3.7599999999999998)*math::log(math::abs(T)))*math::exp(-2197000.0/((T)*(T)*(T)))/(6.0142468035272636e-8*math::exp((2.1000000000000001)*math::log(math::abs(T))) + 1.0) + 1.6e-18*math::exp(-11700.0*x4) + 6.7e-19*math::exp(-5860.0*x4) + 3.0e-24*math::exp(-510.0*x4);

    Real x73 = T < 2000.0;

    Real x74 = x23 - 6.9077552789821368;

    Real x75 = 0.14476482730108395*x23 - 1;

    Real x76 = x26*((x75)*(x75));

    Real x77 = ((x75)*(x75)*(x75));

    Real x78 = x35*x77;

    Real x79 = ((((x75)*(x75)))*(((x75)*(x75))));

    Real x80 = ((x75)*(x75)*(x75)*(x75)*(x75));

    Real x81 = x54*math::exp((6)*math::log(math::abs(x75)));

    Real x82 = x53*((x75)*(x75)*(x75)*(x75)*(x75)*(x75)*(x75));

    Real x83 = math::exp((8)*math::log(math::abs(x75)));

    Real x84 = math::exp((5.0194035000000001*x25*x74 + 5627.2167698544854*x42*x79 + 86051.290034608537*x51*x80 + 9415777.8988952208*x52*x83 - 75.100986441619156*x76 - 1554.3387057364687*x78 - 428804.85473346239*x81 - 1662263.0320406025*x82 - 20.584225)*math::log(math::abs(10.0)));

    Real x85 = T <= 10000.0;

    Real x86 = 0.00020000000000000001*T;

    Real x87 = x86 - 6.0;

    Real x88 = x87 >= 300.0;

    Real x89 = rhs_eint_choice_13(x72, x73, x84, x85, x87, x88);

    Real x90 = math::exp((25.0*x25)*math::log(math::abs(T)));

    Real x91 = math::exp((-200.0 + 20000.0/((10.0 + 2.3538526683701997e+17/x90)*(1.6889118802245084e-48*x90 + 10.0)))*math::log(math::abs(10.0)));

    Real x92 = x42*x79;

    Real x93 = x51*x80;

    Real x94 = math::exp((2.0943374000000001*x25*x74 + 144.02112655888752*x35*x77 - 36.814414747418546*x76 - 339.5619991617852*x92 - 529.07725573213918*x93 - 23.962112000000001)*math::log(math::abs(10.0)))*xn[8]*x91;

    Real x95 = x25*x74;

    Real x96 = math::exp((-38.89917505778142*x76 + 95.70878894783884*x78 - 377.88183430702219*x92 + 3018.4974183098116*x93 + 2.1892372*x95 - 23.689236999999999)*math::log(math::abs(10.0)))*xn[13];

    Real x97 = T > 10.0;

    Real x98 = x85 && x97;

    Real x99 = math::exp((16.666666666666664*x25)*math::log(math::abs(T)));

    Real x100 = math::exp((-200.0 + 20000.0/((10.0 + 785.77199422741614/x99)*(5.0592917094448065e-34*x99 + 10.0)))*math::log(math::abs(10.0)));

    Real x101 = 1.002560385050777e-22*xn[13]*x100;

    Real x102 = math::exp((0.73442154540113413*x76 - 77.855706084264682*x78 - 1161.2797752309887*x92 + 5059.6285287169567*x93 + 1.5714710999999999*x95 - 22.089523)*math::log(math::abs(10.0)))*xn[1];

    Real x103 = 1.1825091393820599e-21*xn[1]*x100;

    Real x104 = math::exp((2774.5177117396752*x76 + 16037.924047681272*x78 + 45902.322591745004*x92 + 60522.293708798054*x93 + 37.383713*x95 - 16.818342000000001)*math::log(math::abs(10.0)))*xn[2];

    Real x105 = T <= 100.0;

    Real x106 = math::exp((3.5692468000000002*x25*x74 - 540.77102118284597*x76 - 9179.8864335208946*x78 - 48562.751069188118*x92 - 66875.646562351845*x93 - 24.311209000000002)*math::log(math::abs(10.0)))*xn[2];

    Real x107 = T <= 1000.0;

    Real x108 = math::exp((-177.55453097873294*x76 + 1956.911370108365*x78 - 12547.661945180447*x92 + 24439.250555499191*x93 + 4.6450521*x95 - 24.311209000000002)*math::log(math::abs(10.0)))*xn[2];

    Real x109 = T <= 6000.0;

    Real x110 = math::exp((17.997580222853362*x25)*math::log(math::abs(T)));

    Real x111 = 1.8623144679125181e-22*math::exp((-200.0 + 20000.0/((10.0 + 2973.7534532281375/x110)*(1.3368457736780898e-34*x110 + 10.0)))*math::log(math::abs(10.0)))*xn[2];

    Real x112 = x52*x83;

    Real x113 = math::exp((366063607.58415633*x112 + 4616.3011562659685*x76 + 113122.17137872758*x78 + 87115306.05744876*x81 + 273295393.17143697*x82 + 1672890.7229183144*x92 + 15471651.937466398*x93 + 16.815729999999999*x95 - 21.928795999999998)*math::log(math::abs(10.0)))*xn[0];

    Real x114 = T <= 500.0;

    Real x115 = T > 100;

    Real x116 = x114 && x115;

    Real x117 = math::exp((-33025002.640084207*x112 + 44.525106942242758*x76 + 1331.8748828877385*x78 + 968783.44101153011*x81 + 4831859.3594864924*x82 - 10763.919849753534*x92 - 138531.11016116844*x93 + 1.6802758*x95 - 22.921188999999998)*math::log(math::abs(10.0)))*xn[0];

    Real x118 = T > 500.0;

    Real x119 = x91*rhs_eint_choice_15(x113, x116, x117, x118) + x94 + rhs_eint_choice_16(x102, x103, x98) + rhs_eint_choice_17(x101, x96, x98) + rhs_eint_choice_20(x104, x105, x106, x107, x108, x109, x111);

    Real x120 = x72 >= 1.0e-99;

    Real x121 = x113*x91;

    Real x122 = x104 + x94;

    Real x123 = x102 + x96;

    Real x124 = x122 + x123;

    Real x125 = x121 + x124 >= 1.0e-99;

    Real x126 = x123 + x94;

    Real x127 = x106 + x126;

    Real x128 = x121 + x127 >= 1.0e-99;

    Real x129 = x108 + x126;

    Real x130 = x121 + x129 >= 1.0e-99;

    Real x131 = x111 + x126;

    Real x132 = x121 + x131 >= 1.0e-99;

    Real x133 = x101 + x103;

    Real x134 = x122 + x133;

    Real x135 = x121 + x134 >= 1.0e-99;

    Real x136 = x133 + x94;

    Real x137 = x106 + x136;

    Real x138 = x121 + x137 >= 1.0e-99;

    Real x139 = x108 + x136;

    Real x140 = x121 + x139 >= 1.0e-99;

    Real x141 = x111 + x136;

    Real x142 = x121 + x141 >= 1.0e-99;

    Real x143 = x117*x91;

    Real x144 = x124 + x143 >= 1.0e-99;

    Real x145 = x127 + x143 >= 1.0e-99;

    Real x146 = x129 + x143 >= 1.0e-99;

    Real x147 = x131 + x143 >= 1.0e-99;

    Real x148 = x134 + x143 >= 1.0e-99;

    Real x149 = x137 + x143 >= 1.0e-99;

    Real x150 = x139 + x143 >= 1.0e-99;

    Real x151 = x141 + x143 >= 1.0e-99;

    Real x152 = x124 >= 1.0e-99;

    Real x153 = x127 >= 1.0e-99;

    Real x154 = x129 >= 1.0e-99;

    Real x155 = x131 >= 1.0e-99;

    Real x156 = x134 >= 1.0e-99;

    Real x157 = x137 >= 1.0e-99;

    Real x158 = x139 >= 1.0e-99;

    Real x159 = x141 >= 1.0e-99;

    Real x160 = x84 >= 1.0e-99;

    Real x161 = 5.5313336794064847e-19/(0.0024787521766663585*math::exp(x86) + 1.0) >= 1.0e-99;


    return (x1*(-3.1438547368704001e-21*math::exp((0.34999999999999998)*math::log(math::abs(T)))*xn[0]*xn[8]*math::exp(-102000.0*x4) - 0.00022681492*((((T)*(T)))*(((T)*(T))))*x0*x58*rhs_eint_choice_36(x55, x57, x58, x59, x60, x61, x62, x63, x64, x66, x67, x68, x69) + 0.00084373771595996178*T*(1.3806479999999999e-16*xn[0] + 1.3806479999999999e-16*xn[1] + 1.3806479999999999e-16*xn[10] + 1.3806479999999999e-16*xn[11] + 1.3806479999999999e-16*xn[12] + 1.3806479999999999e-16*xn[13] + 1.3806479999999999e-16*xn[2] + 1.3806479999999999e-16*xn[3] + 1.3806479999999999e-16*xn[4] + 1.3806479999999999e-16*xn[5] + 1.3806479999999999e-16*xn[6] + 1.3806479999999999e-16*xn[7] + 1.3806479999999999e-16*xn[8] + 1.3806479999999999e-16*xn[9])/(math::sqrt(x1)*x22) - 2.1299999999999999e-27*xn[0]*x2*(4.0*xn[11] + x3) - 5.6500000000000001e-36*xn[0]*(T - x5)*((((z + 1.0)*(z + 1.0)))*(((z + 1.0)*(z + 1.0)))) - 3.4635323838154264e-26*xn[1]*x14 - 1.3854129535261706e-25*xn[11]*x14 - 9.3799999999999993e-22*xn[13]*x10*x12*math::exp(-285335.40000000002*x8) + 7.1777505408000004e-12*((xn[2])*(xn[2])*(xn[2]))*x18*(2.0000000000000002e-31*x15 + 6.0000000000000001e-32*x21) + 7.1777505408000004e-12*((xn[2])*(xn[2]))*xn[8]*x18*(2.5000000000000002e-32*x15 + 7.5000000000000001e-33*x21) + 5.6556829037999995e-12*xn[2]*xn[3]*x18*(1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T))))/(0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0) + 1.75918975308e-21*xn[2]*xn[6]*x18 - 7.1777505408000004e-12*xn[2]*xn[8]*(math::exp((42.707410000000003*x23*x25 - 2.0273650000000001*x29 - 21467.790000000001*x4 - 1657.4099999999999*x4/(math::exp((x39)*math::log(math::abs(math::exp((-x38 - 8.1313220000000008)*math::log(math::abs(10.0)))*x17))) + 1.0) - x40 - 142.7664 - (70.138370000000009*x23*x25 + 11.28215*x25*math::log(14254.549999999999*x4 + 1.0) - 4.7035149999999994*x29 - x40 - 203.11568)/(math::exp((x39)*math::log(math::abs(math::exp((-x38 - 9.3055640000000004)*math::log(math::abs(10.0)))*x17))) + 1.0))*math::log(math::abs(10.0))) + math::exp((43.20243*x26*x28 - 68.422430000000006*x34 - x37 - 23705.700000000001*x4 - 2080.4099999999999*x4/(math::exp((x33)*math::log(math::abs(math::exp((-x30 - 13.656822)*math::log(math::abs(10.0)))*x17))) + 1.0) - 178.4239 - (19.734269999999999*x25*math::log(16780.950000000001*x4 + 1.0) + 37.886913*x26*x28 - 14.509090000000008*x34 - x37 - 307.31920000000002)/(math::exp((x33)*math::log(math::abs(math::exp((-x30 - 14.82123)*math::log(math::abs(10.0)))*x17))) + 1.0))*math::log(math::abs(10.0)))) - 7.1777505408000004e-12*((xn[8])*(xn[8]))*math::exp((x27)*math::log(math::abs(1.1800000000000001e-10*math::exp(-69500.0*x4))))*math::exp((1.0 - x27)*math::log(math::abs(8.1250000000000003e-8*x15*(1.0 - math::exp(-6000.0*x4))*math::exp(-52000.0*x4)))) - 1.2700000000000001e-21*x10*x13*math::exp(-157809.10000000001*x8) - 4.9500000000000001e-22*x10*x19*math::exp(-631515.0*x8) - 7.4999999999999996e-19*x13*math::exp(-118348.0*x8) - 5.5399999999999998e-17*x19*rhs_eint_choice_37(T, x6)*math::exp(-473638.0*x8) - 5.0099999999999997e-27*x20*math::exp(-55338.0*x8) - 9.1000000000000001e-27*x20*math::exp(-13179.0*x8) - 1.24e-13*x7*(1.0 + 0.29999999999999999*math::exp(-94000.0*x8))*rhs_eint_choice_38(T, x6)*math::exp(-470000.0*x8) - 1.5499999999999999e-26*x7*rhs_eint_choice_39(T, x6) - rhs_eint_choice_100(T, x105, x107, x109, x114, x115, x116, x118, x119, x120, x125, x128, x130, x132, x135, x138, x140, x142, x144, x145, x146, x147, x148, x149, x150, x151, x152, x153, x154, x155, x156, x157, x158, x159, x160, x161, x70, x71, x73, x85, x88, x89, x97, x98, xn) - rhs_eint_choice_101(T, x25, x26, x35, x41, x42, x43, x44, x45, x47, x48, x49, x5, x50, x51, x52, x53, x54, xn)));

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
   1250086.112245841*math::exp((-1.5)*math::log(math::abs(T)))*x31 + x28
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
   -2.3799651743169991e-10*math::exp((-1.724112565782685)*math::log(math::abs(T)))*x52 + 3.2867337024382687e-10*x50*x52*(-0.0071425856320495021*x12*x35 - 7.1075145702705346e-5*x12*x37 + 2.9934653521797078e-5*x12*x38 + 4.0289298963030308e-6*x12*x39 - 0.04052089463969382*x84 - 0.0012850420852755183*x85 - 1.4854136318202087e-7*x87 - 2.7640217188769353e-8*x88)
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
   2.6092635916521491e-16*math::exp((0.78186)*math::log(math::abs(T)))
)
: (
   3.7804827525136553e-14*math::exp((0.13944933584163111)*math::log(math::abs(T)))*x62 + x63*(0.025393366159889998*x12*x35 + 0.0010061251423955*x12*x37 + 0.00051983779458540007*x12*x38 - 0.00018095067761848*x12*x39 + 1.9644009576313599e-5*x12*x40 - 0.28420270431082961*x84 - 0.0057310564851968003*x85 - 7.2615442150620009e-7*x88)
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
   1.4981088130721367e-10*math::exp((-0.63529999999999998)*math::log(math::abs(T)))
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
   -2.3799651743169991e-10*math::exp((-1.724112565782685)*math::log(math::abs(T)))*x14 + 3.2867337024382687e-10*x13*x14*(-0.0071425856320495021*x18*x2 - 7.1075145702705346e-5*x18*x4 + 2.9934653521797078e-5*x18*x5 + 4.0289298963030308e-6*x18*x6 - 2.7640217188769353e-8*x18*x8 - 0.04052089463969382*x66 - 0.0012850420852755183*x67 - 1.4854136318202087e-7*x68)
));
}
DEVICE auto jac_nuc_choice_16(Real T, Real x22, bool x23) {
return ((x23) ? (
   0.00016065*math::exp((-2.75)*math::log(math::abs(T)))*x22 - 9.4499999999999994e-10*math::exp((-1.75)*math::log(math::abs(T)))*x22
)
: (
   1.8960000000000001e-36*math::exp((3.7400000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_17(Real T, Real x18, Real x32, Real x33, bool x41, Real x42, Real x44, Real x46, Real x64, Real x65) {
return ((x41) ? (
   -5.2466095085621454e-21*math::exp((-1.1499999999999999)*math::log(math::abs(T)))
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
   7.4200000000000004e-9*math::exp((-1.5979999999999999)*math::log(math::abs(T)))*x25 + 8.0400000000000002e-11*math::exp((-0.59799999999999998)*math::log(math::abs(T)))*x25 - 4.8987999999999998e-17*math::exp((0.47999999999999998)*math::log(math::abs(T)))
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
   6.7980000000000007e-9*math::exp((-1.6040000000000001)*math::log(math::abs(T)))*x56 - 6.7396000000000002e-10*math::exp((-1.3320000000000001)*math::log(math::abs(T))) + 8.1576000000000009e-11*math::exp((-0.60399999999999998)*math::log(math::abs(T)))*x56
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
   1.4981088130721367e-10*math::exp((-0.63529999999999998)*math::log(math::abs(T)))
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
   -2.3799651743169991e-10*math::exp((-1.724112565782685)*math::log(math::abs(T)))*x39 + 3.2867337024382687e-10*x37*x39*(-0.0071425856320495021*x21*x7 - 0.04052089463969382*x222 - 0.0012850420852755183*x223 - 1.4854136318202087e-7*x225 - 2.7640217188769353e-8*x227 - 7.1075145702705346e-5*x23*x7 + 2.9934653521797078e-5*x24*x7 + 4.0289298963030308e-6*x25*x7)
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
   1.8960000000000001e-36*math::exp((3.7400000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_36(Real T, Real x216, Real x218, Real x48, bool x57, Real x58, Real x63, Real x64, Real x7) {
return ((x57) ? (
   -5.2466095085621454e-21*math::exp((-1.1499999999999999)*math::log(math::abs(T)))
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
   7.4200000000000004e-9*math::exp((-1.5979999999999999)*math::log(math::abs(T)))*x44 + 8.0400000000000002e-11*math::exp((-0.59799999999999998)*math::log(math::abs(T)))*x44 - 4.8987999999999998e-17*math::exp((0.47999999999999998)*math::log(math::abs(T)))
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
   6.7980000000000007e-9*math::exp((-1.6040000000000001)*math::log(math::abs(T)))*x173 - 6.7396000000000002e-10*math::exp((-1.3320000000000001)*math::log(math::abs(T))) + 8.1576000000000009e-11*math::exp((-0.60399999999999998)*math::log(math::abs(T)))*x173
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
   2.6092635916521491e-16*math::exp((0.78186)*math::log(math::abs(T)))
)
: (
   3.7804827525136553e-14*math::exp((0.13944933584163111)*math::log(math::abs(T)))*x185 + x186*(0.025393366159889998*x21*x7 - 0.28420270431082961*x222 - 0.0057310564851968003*x223 - 7.2615442150620009e-7*x227 + 0.0010061251423955*x23*x7 + 0.00051983779458540007*x24*x7 - 0.00018095067761848*x25*x7 + 1.9644009576313599e-5*x26*x7)
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
   2.6092635916521491e-16*math::exp((0.78186)*math::log(math::abs(T)))
)
: (
   3.7804827525136553e-14*math::exp((0.13944933584163111)*math::log(math::abs(T)))*x31 + x32*(0.0010061251423955*x11*x4 + 0.00051983779458540007*x12*x4 - 0.00018095067761848*x13*x4 + 1.9644009576313599e-5*x14*x4 - 7.2615442150620009e-7*x15*x4 + 0.025393366159889998*x4*x9 - 0.28420270431082961*x42 - 0.0057310564851968003*x43)
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
   7.4200000000000004e-9*math::exp((-1.5979999999999999)*math::log(math::abs(T)))*x4 + 8.0400000000000002e-11*math::exp((-0.59799999999999998)*math::log(math::abs(T)))*x4 - 4.8987999999999998e-17*math::exp((0.47999999999999998)*math::log(math::abs(T)))
)
: (
   0
));
}
DEVICE auto jac_nuc_choice_51(Real T, bool x5, Real x7) {
return ((x5) ? (
   6.7980000000000007e-9*math::exp((-1.6040000000000001)*math::log(math::abs(T)))*x7 - 6.7396000000000002e-10*math::exp((-1.3320000000000001)*math::log(math::abs(T))) + 8.1576000000000009e-11*math::exp((-0.60399999999999998)*math::log(math::abs(T)))*x7
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
   7.4200000000000004e-9*math::exp((-1.5979999999999999)*math::log(math::abs(T)))*x8 + 8.0400000000000002e-11*math::exp((-0.59799999999999998)*math::log(math::abs(T)))*x8 - 4.8987999999999998e-17*math::exp((0.47999999999999998)*math::log(math::abs(T)))
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
   6.7980000000000007e-9*math::exp((-1.6040000000000001)*math::log(math::abs(T)))*x25 - 6.7396000000000002e-10*math::exp((-1.3320000000000001)*math::log(math::abs(T))) + 8.1576000000000009e-11*math::exp((-0.60399999999999998)*math::log(math::abs(T)))*x25
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
   -5.2466095085621454e-21*math::exp((-1.1499999999999999)*math::log(math::abs(T)))
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
   1250086.112245841*math::exp((-1.5)*math::log(math::abs(T)))*x12 + x8
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
   1.8960000000000001e-36*math::exp((3.7400000000000002)*math::log(math::abs(T)))
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
   1250086.112245841*math::exp((-1.5)*math::log(math::abs(T)))*x4 + x0
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
   1.8960000000000001e-36*math::exp((3.7400000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_84(Real T, bool x16) {
return ((x16) ? (
   2.3157944032250755
)
: (
   math::exp((0.36470000000000002)*math::log(math::abs(T)))
));
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
return ((x16) ? (
   5.011872336272722
)
: (
   math::exp((0.69999999999999996)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_88(Real T, bool x16) {
return ((x16) ? (
   0.63095734448019325
)
: (
   math::exp((-0.20000000000000001)*math::log(math::abs(T)))
));
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
return ((x16) ? (
   0.4008667176273028
)
: (
   math::exp((-0.39700000000000002)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_91(Real T, bool x16) {
return ((x16) ? (
   0.67810976749343443
)
: (
   math::exp((-0.16869999999999999)*math::log(math::abs(T)))
));
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
   xn[2] >= 10000000000.0
));
}
DEVICE auto jac_nuc_choice_94(bool x173, const Real* xn) {
return ((x173) ? (
      0.01
   )
   : (
      xn[2]
   ));
}
DEVICE auto jac_nuc_choice_95(bool x173, bool x174, const Real* xn) {
return ((x174) ? (
   10000000000.0
)
: (
   jac_nuc_choice_94(x173, xn)
));
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
return ((x205 & x206) ? (
                              2.1648287161311648e-38/((x210*x215 + 1.471335691176954e-59)*(x210*x215 + 1.471335691176954e-59)) >= 1.0
                           )
                           : (
                              jac_nuc_choice_96(x205, x212, x215)
                           ));
}
DEVICE auto jac_nuc_choice_98(bool x205, bool x206, bool x207, Real x210, Real x212, Real x215) {
return ((x207) ? (
                           true
                        )
                        : (
                           jac_nuc_choice_97(x205, x206, x210, x212, x215)
                        ));
}
DEVICE auto jac_nuc_choice_99(bool x205, bool x206, bool x207, Real x210, Real x212, Real x214, Real x215) {
return ((x205 & x207) ? (
                        4.833962023629485e-72/((x212*x214 + 2.1986273043946045e-76)*(x212*x214 + 2.1986273043946045e-76)) >= 1.0
                     )
                     : (
                        jac_nuc_choice_98(x205, x206, x207, x210, x212, x215)
                     ));
}
DEVICE auto jac_nuc_choice_100(bool x205, bool x206, bool x207, Real x210, Real x212, Real x214, Real x215) {
return ((x205 & x206 & x207) ? (
                     4.833962023629485e-72/((x210*x214 + 2.1986273043946045e-76)*(x210*x214 + 2.1986273043946045e-76)) >= 1.0
                  )
                  : (
                     jac_nuc_choice_99(x205, x206, x207, x210, x212, x214, x215)
                  ));
}
DEVICE auto jac_nuc_choice_101(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x214, Real x215) {
return ((x204) ? (
                  true
               )
               : (
                  jac_nuc_choice_100(x205, x206, x207, x210, x212, x214, x215)
               ));
}
DEVICE auto jac_nuc_choice_102(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
return ((x204 & x205) ? (
               216.48287161311649/((x212*x213 + 1.471335691176954e-39)*(x212*x213 + 1.471335691176954e-39)) >= 1.0
            )
            : (
               jac_nuc_choice_101(x204, x205, x206, x207, x210, x212, x214, x215)
            ));
}
DEVICE auto jac_nuc_choice_103(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
return ((x204 & x205 & x206) ? (
            216.48287161311649/((x210*x213 + 1.471335691176954e-39)*(x210*x213 + 1.471335691176954e-39)) >= 1.0
         )
         : (
            jac_nuc_choice_102(x204, x205, x206, x207, x210, x212, x213, x214, x215)
         ));
}
DEVICE auto jac_nuc_choice_104(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
return ((x204 & x207) ? (
         true
      )
      : (
         jac_nuc_choice_103(x204, x205, x206, x207, x210, x212, x213, x214, x215)
      ));
}
DEVICE auto jac_nuc_choice_105(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
return ((x204 & x205 & x207) ? (
      4.8339620236294848e-32/((x212 + 2.1986273043946046e-56)*(x212 + 2.1986273043946046e-56)) >= 1.0
   )
   : (
      jac_nuc_choice_104(x204, x205, x206, x207, x210, x212, x213, x214, x215)
   ));
}
DEVICE auto jac_nuc_choice_106(bool x204, bool x205, bool x206, bool x207, Real x210, Real x212, Real x213, Real x214, Real x215) {
return ((x204 & x205 & x206 & x207) ? (
   4.8339620236294848e-32/((x210 + 2.1986273043946046e-56)*(x210 + 2.1986273043946046e-56)) >= 1.0
)
: (
   jac_nuc_choice_105(x204, x205, x206, x207, x210, x212, x213, x214, x215)
));
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
return ((x271) ? (
   x270
)
: jac_nuc_choice_120(x273, x274, x276, x277, x283));
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
return ((x297 & x309) ? (
                                                x320 & x351
                                             )
                                             : (
                                                jac_nuc_choice_164(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                             ));
}
DEVICE auto jac_nuc_choice_166(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x277 & x297 & x309) ? (
                                             x320 & x350
                                          )
                                          : (
                                             jac_nuc_choice_165(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                          ));
}
DEVICE auto jac_nuc_choice_167(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x274 & x297 & x309) ? (
                                          x320 & x349
                                       )
                                       : (
                                          jac_nuc_choice_166(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                       ));
}
DEVICE auto jac_nuc_choice_168(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x271 & x297 & x309) ? (
                                       x320 & x348
                                    )
                                    : (
                                       jac_nuc_choice_167(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                    ));
}
DEVICE auto jac_nuc_choice_169(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x297 & x309) ? (
                                    x320 & x347
                                 )
                                 : (
                                    jac_nuc_choice_168(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                                 ));
}
DEVICE auto jac_nuc_choice_170(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x277 & x297 & x309) ? (
                                 x320 & x346
                              )
                              : (
                                 jac_nuc_choice_169(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                              ));
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
return ((x255 & x256 & x271 & x297 & x309) ? (
                           x320 & x344
                        )
                        : (
                           jac_nuc_choice_171(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                        ));
}
DEVICE auto jac_nuc_choice_173(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x292 & x293 & x309) ? (
                        x320 & x342
                     )
                     : (
                        jac_nuc_choice_172(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                     ));
}
DEVICE auto jac_nuc_choice_174(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x277 & x292 & x293 & x309) ? (
                     x320 & x340
                  )
                  : (
                     jac_nuc_choice_173(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
                  ));
}
DEVICE auto jac_nuc_choice_175(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x274 & x292 & x293 & x309) ? (
                  x320 & x338
               )
               : (
                  jac_nuc_choice_174(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
               ));
}
DEVICE auto jac_nuc_choice_176(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x271 & x292 & x293 & x309) ? (
               x320 & x335
            )
            : (
               jac_nuc_choice_175(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
            ));
}
DEVICE auto jac_nuc_choice_177(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x292 & x293 & x309) ? (
            x320 & x332
         )
         : (
            jac_nuc_choice_176(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
         ));
}
DEVICE auto jac_nuc_choice_178(bool x255, bool x256, bool x257, bool x271, bool x274, bool x277, bool x292, bool x293, bool x294, bool x297, bool x309, bool x313, bool x320, bool x325, bool x328, bool x330, bool x332, bool x335, bool x338, bool x340, bool x342, bool x344, bool x345, bool x346, bool x347, bool x348, bool x349, bool x350, bool x351, bool x352, bool x353, bool x354, bool x355, bool x356, bool x357, bool x358, bool x359, bool x360, bool x361) {
return ((x255 & x256 & x277 & x292 & x293 & x309) ? (
         x320 & x330
      )
      : (
         jac_nuc_choice_177(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
      ));
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
return ((x255 & x256 & x271 & x292 & x293 & x309) ? (
   x320 & x325
)
: (
   jac_nuc_choice_179(x255, x256, x257, x271, x274, x277, x292, x293, x294, x297, x309, x313, x320, x325, x328, x330, x332, x335, x338, x340, x342, x344, x345, x346, x347, x348, x349, x350, x351, x352, x353, x354, x355, x356, x357, x358, x359, x360, x361)
));
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
   xn[8]*x364
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
return ((x271) ? (
   x269
)
: jac_nuc_choice_198(x272, x274, x275, x277, x282));
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
return ((x174) ? (
   0
)
: (
   jac_nuc_choice_200(x173)
));
}
DEVICE auto jac_nuc_choice_202(bool x173, const Real* xn) {
return ((x173) ? (
      100.0
   )
   : (
      1.0/xn[2]
   ));
}
DEVICE auto jac_nuc_choice_203(bool x173, bool x174, const Real* xn) {
return ((x174) ? (
   1.0e-10
)
: (
   jac_nuc_choice_202(x173, xn)
));
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
return ((x16) ? (
   0
)
: (
   -0.20000000000000001*math::exp((-1.2)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_233(Real T, bool x16) {
return ((x16) ? (
   0
)
: (
   -0.16869999999999999*math::exp((-1.1687000000000001)*math::log(math::abs(T)))
));
}
DEVICE auto jac_nuc_choice_234(Real T, bool x16) {
return ((x16) ? (
   0
)
: (
   0.69999999999999996*math::exp((-0.30000000000000004)*math::log(math::abs(T)))
));
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
   3.2860556719809434e-26*math::exp((-0.24000000000000021)*math::log(math::abs(T)))*x307 + 1.8746122480121903e-32*math::exp((2.7599999999999998)*math::log(math::abs(T)))*x307 - 6.2968615725975507e-40*math::exp((4.8599999999999994)*math::log(math::abs(T)))*x306/((x303)*(x303)) + 1.8719999999999998e-14*x300*x48 + 3.9261999999999998e-15*x301*x48 + 1.53e-21*x302*x48
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
return ((x271) ? (
   x270*(86.079180274567719*x512 + 267.76838492252847*x523 + 44.301288185112313*x527 + 185.67890535151699*x528 + 336.10445235294861*x529)
)
: jac_nuc_choice_245(x101, x273, x274, x276, x277, x278, x279, x280, x281, x283, x512, x523, x527, x528, x529, x8));
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
return ((x230) ? (
   0
)
: (
   x380 + x383*jac_nuc_choice_249(x239, x316, x318, x362, x381, x382)
));
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
return ((x230) ? (
   0
)
: (
   x380 + x383*jac_nuc_choice_253(x316, x318, x362, x382, x386)
));
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
return ((x230) ? (
   0
)
: (
   x380 + x383*jac_nuc_choice_257(x316, x318, x362, x382, x419)
));
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
return ((x230) ? (
   0
)
: (
   x364 + x380 + x383*jac_nuc_choice_272(x239, x248, x249, x316, x318, x362, x382)
));
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
return ((x230) ? (
   0
)
: (
   x380 + x383*jac_nuc_choice_285(x316, x318, x362, x382, x474)
));
}
DEVICE auto jac_nuc_choice_287(Real T, bool x16) {
return ((x16) ? (
   0
)
: (
   -0.39700000000000002*math::exp((-1.397)*math::log(math::abs(T)))
));
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
return ((x16) ? (
   0
)
: (
   0.36470000000000002*math::exp((-0.63529999999999998)*math::log(math::abs(T)))
));
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
return ((x230) ? (
   0
)
: (
   x383*jac_nuc_choice_291(x299, x318, x319, x362, x382, x530, x533)
));
}
DEVICE auto jac_nuc_choice_293(Real x101, Real x168, Real x171, Real x172, Real x179, Real x181, Real x183, Real x184, Real x185, Real x186, Real x187, Real x189, Real x190, Real x192, Real x193, Real x194, Real x196, Real x197, Real x198, Real x200, bool x201, Real x421, Real x423, Real x425, Real x428, Real x506, Real x507, Real x508, Real x509, Real x510, Real x511) {
return ((x201) ? (
   x200*(50.504556041967454*x101*x506 + 0.011577397847574064*x168*x192*x508 + 0.00057030427583276537*x171*x187*x508 - 0.0046027064199385889*x172*x189*x508 - 46.931151210299063*x179*x506 - 0.0084274614403582089*x181*x506 + 0.27317869543281359*x183*x506 - 1.5965204000783517*x184*x506 - 2.4510327539393719*x185*x506 + 15.190568323798459*x186*x506 - 0.013649724431268705*x190*x510 + 0.12150741535729581*x193*x510 + 0.097629607942947547*x194*x511 - 0.33921683589988288*x196*x507 - 0.076906342106001127*x197*x509 + 2.8710012490505563*x198*x507 + 1.7949111316907187*x421*x506 - 2.4989034980227181*x423*x507 + 0.80896722072159821*x425*x509 - 0.95105974681026062*x428*x511)
)
: (
   0
));
}
DEVICE void jac_nuc(Real temperature, Real* out, const Real* xn, Real z) {
    Real T = temperature;
    Real kr_t0[737];
    volatile unsigned kzs_t0_v = 0u;
    const unsigned kzs_t0 = kzs_t0_v;
    __builtin_assume(kzs_t0 < 8u);
    const Real v1054 = 9.8726896031426014e-7*math::exp((-0.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 0u] = v1054;
    const Real v1002 = 6.1739095063118665e-10*math::exp((0.40999999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 1u] = v1002;
    const Real v1036 = 2.5950363272655348e-10*math::exp((-0.75)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 2u] = v1036;
    const Real v1583 = 2.8833736969617052e-16*math::exp((0.25)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 3u] = v1583;
    const Real v693 = 2.8833736969617052e-16*math::exp((0.25)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 4u] = v693;
    const Real v1077 = 7.1999999999999996e-8/math::sqrt(T);
    kr_t0[kzs_t0 + 5u] = v1077;
    const Real v1390 = 7.1999999999999996e-8/math::sqrt(T);
    kr_t0[kzs_t0 + 6u] = v1390;
    const Real v1535 = 2.8833736969617052e-16*math::exp((0.25)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 7u] = v1535;
    const Real v1187 = 6.1739095063118665e-10*math::exp((0.40999999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 8u] = v1187;
    const Real v654 = 7.9674337148168363e-7*math::exp((-0.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 9u] = v654;
    const Real v534 = 2.5950363272655348e-10*math::exp((-0.75)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 10u] = v534;
    const Real v1186 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 11u] = v1186;
    const Real v1184 = math::exp((-0.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 12u] = v1184;
    const Real v1423 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 13u] = v1423;
    const Real v589 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 14u] = v589;
    const Real v1011 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 15u] = v1011;
    const Real v1096 = 6.1739095063118665e-10*math::exp((0.40999999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 16u] = v1096;
    const Real v1103 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 17u] = v1103;
    const Real v1082 = math::exp((-0.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 18u] = v1082;
    const Real v1182 = math::exp((0.94999999999999996)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 22u] = v1182;
    const Real v1183 = 1.3300135414628029e-18*v1182;
    kr_t0[kzs_t0 + 20u] = v1183;
    const Real v1180 = math::exp(-0.00010729613733905579*T);
    kr_t0[kzs_t0 + 19u] = v1180;
    const Real v1190 = 2.5313028975878652e-10*math::exp((-0.59000000000000008)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 21u] = v1190;
    const Real v885 = math::exp((0.25)*math::log(math::abs(T)));
    const Real v886 = 2.8833736969617052e-16*v885;
    kr_t0[kzs_t0 + 23u] = v886;
    const Real v1160 = 4.9999999999999996e-6/math::sqrt(T);
    kr_t0[kzs_t0 + 25u] = v1160;
    const Real v1141 = 1.0e-8*math::exp((-0.40000000000000002)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 24u] = v1141;
    const Real v535 = math::sqrt(T);
    const Real v536 = 1.0/v535;
    const Real v537 = 7.1999999999999996e-8*v536;
    kr_t0[kzs_t0 + 26u] = v537;
    const Real v546 = 1.0/T;
    kr_t0[kzs_t0 + 297u] = v546;
    const Real v547 = math::exp(-46707.0*v546);
    kr_t0[kzs_t0 + 27u] = v547;
    const Real v549 = 35.5*math::exp((-2.2799999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 28u] = v549;
    const Real v1420 = 1.0/T;
    kr_t0[kzs_t0 + 37u] = v1420;
    const Real v1421 = math::exp(-457.0*v1420);
    kr_t0[kzs_t0 + 32u] = v1421;
    const Real v1422 = 1.0000000000000001e-9*v1421;
    kr_t0[kzs_t0 + 29u] = v1422;
    const Real v656 = 1.0/T;
    kr_t0[kzs_t0 + 243u] = v656;
    const Real v657 = math::exp(-457.0*v656);
    kr_t0[kzs_t0 + 253u] = v657;
    const Real v658 = 1.0000000000000001e-9*v657;
    kr_t0[kzs_t0 + 30u] = v658;
    const Real v1424 = math::exp((-2)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 33u] = v1424;
    const Real v1425 = 5.25e-11*math::exp(-4430.0*v1420 + 173900.0*v1424);
    kr_t0[kzs_t0 + 34u] = v1425;
    const Real v1426 = T > 200.0;
    kr_t0[kzs_t0 + 35u] = v1426;
    const Real v1427 = jac_nuc_choice_72(v1425, v1426);
    kr_t0[kzs_t0 + 31u] = v1427;
    const Real v1428 = M_LN10;
    kr_t0[kzs_t0 + 49u] = v1428;
    const Real v1432 = math::exp((-2)*math::log(math::abs(v1428)));
    const Real v1430 = math::log(T);
    const Real v1433 = ((v1430)*(v1430));
    kr_t0[kzs_t0 + 40u] = v1433;
    const Real v1434 = v1432*v1433;
    const Real v1429 = 1.0/v1428;
    kr_t0[kzs_t0 + 38u] = v1429;
    const Real v1431 = v1430*v1429;
    const Real v1440 = ((v1430)*(v1430)*(v1430));
    kr_t0[kzs_t0 + 45u] = v1440;
    const Real v1436 = math::powi<-5>(v1428);
    kr_t0[kzs_t0 + 41u] = v1436;
    const Real v1437 = math::exp((-4)*math::log(math::abs(v1428)));
    kr_t0[kzs_t0 + 42u] = v1437;
    const Real v1438 = ((((v1430)*(v1430)))*(((v1430)*(v1430))));
    kr_t0[kzs_t0 + 43u] = v1438;
    const Real v1439 = math::powi<-3>(v1428);
    kr_t0[kzs_t0 + 44u] = v1439;
    const Real v1441 = math::exp((0.31788699999999998*((v1430)*(v1430)*(v1430)*(v1430)*(v1430))*v1436 + 5.8888600000000002*v1431 + 7.1969200000000004*v1434 - 2.1690299999999998*v1437*v1438 + 2.2506900000000001*v1439*v1440 - 56.473700000000001)*math::log(math::abs(10)));
    kr_t0[kzs_t0 + 46u] = v1441;
    const Real v1446 = v1420*v1430*v1432;
    kr_t0[kzs_t0 + 39u] = v1446;
    const Real v1442 = T <= 1167.4796423742259;
    kr_t0[kzs_t0 + 47u] = v1442;
    const Real v1443 = math::exp(-5207.0*v1420);
    kr_t0[kzs_t0 + 48u] = v1443;
    const Real v1445 = 2.6534040307116389e-10*math::exp((-1.1000000000000001)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 36u] = v1445;
    const Real v1444 = jac_nuc_choice_73(v1441, v1442, v1443);
    kr_t0[kzs_t0 + 50u] = v1444;
    const Real v1435 = 8.4600000000000008e-10*v1431 - 1.3700000000000002e-10*v1434 + 4.1700000000000001e-10;
    kr_t0[kzs_t0 + 51u] = v1435;
    const Real v1392 = M_LN10;
    const Real v1393 = 1.0/v1392;
    const Real v1394 = math::exp((-3.0)*math::log(math::abs(v1392)));
    const Real v1395 = math::exp((-2.0)*math::log(math::abs(v1392)));
    const Real v1391 = math::log(T);
    const Real v1396 = ((v1391)*(v1391));
    const Real v1397 = math::exp((-1.5229999999999999*v1391*v1393 - 0.12690000000000001*((v1391)*(v1391)*(v1391))*v1394 + 1.1180000000000001*v1395*v1396 - 19.379999999999999)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 52u] = v1397;
    const Real v1400 = 1.0/T;
    const Real v1401 = 5.1485802679346868*math::exp((1.0)*math::log(math::abs(v1391)))*v1400*v1395 - 3.5068370966299316*v1400*v1393 - 0.87659414490283338*v1400*v1394*v1396;
    kr_t0[kzs_t0 + 53u] = v1401;
    const Real v981 = 1.0/T;
    kr_t0[kzs_t0 + 215u] = v981;
    const Real v982 = math::exp(-46707.0*v981);
    kr_t0[kzs_t0 + 54u] = v982;
    const Real v984 = 35.5*math::exp((-2.2799999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 55u] = v984;
    const Real v1037 = 1.0/T;
    kr_t0[kzs_t0 + 62u] = v1037;
    const Real v1038 = math::exp(-457.0*v1037);
    kr_t0[kzs_t0 + 69u] = v1038;
    const Real v1039 = 1.0000000000000001e-9*v1038;
    kr_t0[kzs_t0 + 56u] = v1039;
    const Real v1040 = math::exp(-37.100000000000001*v1037);
    kr_t0[kzs_t0 + 59u] = v1040;
    const Real v1041 = T >= 50.0;
    kr_t0[kzs_t0 + 60u] = v1041;
    const Real v1042 = jac_nuc_choice_48(T, v1040, v1041);
    kr_t0[kzs_t0 + 57u] = v1042;
    const Real v1045 = M_LN10;
    const Real v1046 = 1.0/v1045;
    const Real v1047 = math::log(T);
    kr_t0[kzs_t0 + 63u] = v1047;
    const Real v1048 = v1046*v1047;
    const Real v1049 = math::exp((-3.0)*math::log(math::abs(v1045)));
    kr_t0[kzs_t0 + 65u] = v1049;
    const Real v1050 = math::exp((-2.0)*math::log(math::abs(v1045)));
    kr_t0[kzs_t0 + 64u] = v1050;
    const Real v1051 = ((v1047)*(v1047));
    kr_t0[kzs_t0 + 66u] = v1051;
    const Real v1052 = math::exp((-0.12690000000000001*((v1047)*(v1047)*(v1047))*v1049 - 1.5229999999999999*v1048 + 1.1180000000000001*v1050*v1051 - 19.379999999999999)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 58u] = v1052;
    const Real v1057 = v1037*v1046;
    kr_t0[kzs_t0 + 67u] = v1057;
    const Real v1055 = math::exp((-2)*math::log(math::abs(v1045)));
    kr_t0[kzs_t0 + 68u] = v1055;
    const Real v1043 = math::exp(-33.0*v1037);
    kr_t0[kzs_t0 + 61u] = v1043;
    const Real v1056 = 1.3700000000000002e-10*((v1047)*(v1047))*v1055 - 8.4600000000000008e-10*v1048 - 4.1700000000000001e-10;
    kr_t0[kzs_t0 + 71u] = v1056;
    const Real v1044 = jac_nuc_choice_49(T, v1041, v1043);
    kr_t0[kzs_t0 + 70u] = v1044;
    const Real v998 = math::sqrt(T);
    const Real v999 = 1.0/v998;
    kr_t0[kzs_t0 + 222u] = v999;
    const Real v1012 = 4.9999999999999996e-6*v999;
    kr_t0[kzs_t0 + 72u] = v1012;
    const Real v1000 = T >= 10.0 && T <= 100000.0;
    kr_t0[kzs_t0 + 223u] = v1000;
    const Real v1001 = jac_nuc_choice_44(T, v998, v999, v1000);
    kr_t0[kzs_t0 + 74u] = v1001;
    const Real v997 = 1.0e-8*math::exp((-0.40000000000000002)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 73u] = v997;
    const Real v544 = math::exp((0.94999999999999996)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 320u] = v544;
    const Real v545 = 1.3300135414628029e-18*v544;
    kr_t0[kzs_t0 + 75u] = v545;
    const Real v542 = math::exp(-0.00010729613733905579*T);
    kr_t0[kzs_t0 + 76u] = v542;
    const Real v659 = math::exp((-0.75)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 280u] = v659;
    const Real v660 = math::exp(-127500.0*v656);
    kr_t0[kzs_t0 + 255u] = v660;
    const Real v661 = T <= 10000.0;
    kr_t0[kzs_t0 + 256u] = v661;
    const Real v662 = jac_nuc_choice_9(T, v659, v660, v661);
    kr_t0[kzs_t0 + 77u] = v662;
    const Real v1136 = ((T)*(T));
    kr_t0[kzs_t0 + 79u] = v1136;
    const Real v1137 = ((T)*(T)*(T));
    kr_t0[kzs_t0 + 80u] = v1137;
    const Real v1138 = ((((T)*(T)))*(((T)*(T))));
    kr_t0[kzs_t0 + 81u] = v1138;
    const Real v1139 = T <= 10000.0;
    kr_t0[kzs_t0 + 82u] = v1139;
    const Real v1140 = jac_nuc_choice_60(T, v1136, v1137, v1138, v1139);
    kr_t0[kzs_t0 + 78u] = v1140;
    const Real v1152 = 1.0/T;
    kr_t0[kzs_t0 + 86u] = v1152;
    const Real v1153 = math::exp(-21237.150000000001*v1152);
    kr_t0[kzs_t0 + 93u] = v1153;
    const Real v1143 = math::log(T);
    kr_t0[kzs_t0 + 89u] = v1143;
    const Real v1154 = ((((v1143)*(v1143)))*(((v1143)*(v1143))));
    kr_t0[kzs_t0 + 94u] = v1154;
    const Real v1155 = ((v1143)*(v1143)*(v1143)*(v1143)*(v1143));
    kr_t0[kzs_t0 + 95u] = v1155;
    const Real v1156 = math::exp((6)*math::log(math::abs(v1143)));
    kr_t0[kzs_t0 + 96u] = v1156;
    const Real v1147 = ((v1143)*(v1143)*(v1143));
    kr_t0[kzs_t0 + 92u] = v1147;
    const Real v1149 = ((v1143)*(v1143));
    const Real v1157 = v1153*(3.4172804999999998e-8*v1147 - 1.4491368e-7*v1149 - 4.7813727999999997e-9*v1154 + 3.9731542e-10*v1155 - 1.8171411000000001e-11*v1156 + 3.5311931999999998e-13*((v1143)*(v1143)*(v1143)*(v1143)*(v1143)*(v1143)*(v1143)) + 3.3735381999999997e-7*v1143 - 3.3232183000000002e-7);
    kr_t0[kzs_t0 + 97u] = v1157;
    const Real v1144 = M_LN10;
    kr_t0[kzs_t0 + 90u] = v1144;
    const Real v1145 = 3.194/v1144;
    kr_t0[kzs_t0 + 91u] = v1145;
    const Real v1146 = math::powi<-3>(v1144);
    kr_t0[kzs_t0 + 83u] = v1146;
    const Real v1148 = math::exp((-2)*math::log(math::abs(v1144)));
    kr_t0[kzs_t0 + 84u] = v1148;
    const Real v1150 = math::exp((-0.2072*v1146*v1147 + 1.786*v1148*v1149 - v1143*v1145 - 18.199999999999999)*math::log(math::abs(10)));
    kr_t0[kzs_t0 + 85u] = v1150;
    const Real v1161 = v1149*v1152;
    kr_t0[kzs_t0 + 87u] = v1161;
    const Real v1158 = T >= 100.0 && T <= 30000.0;
    kr_t0[kzs_t0 + 98u] = v1158;
    const Real v1142 = T < 30;
    kr_t0[kzs_t0 + 88u] = v1142;
    const Real v1159 = jac_nuc_choice_62(v1157, v1158);
    kr_t0[kzs_t0 + 100u] = v1159;
    const Real v1151 = jac_nuc_choice_61(T, v1150, v1142);
    kr_t0[kzs_t0 + 99u] = v1151;
    const Real v1080 = math::exp((0.94999999999999996)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 107u] = v1080;
    const Real v1081 = 1.3300135414628029e-18*v1080;
    kr_t0[kzs_t0 + 103u] = v1081;
    const Real v1078 = math::exp(-0.00010729613733905579*T);
    kr_t0[kzs_t0 + 101u] = v1078;
    const Real v1076 = 2.5950363272655348e-10*math::exp((-0.75)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 102u] = v1076;
    const Real v1083 = 1.0/T;
    kr_t0[kzs_t0 + 112u] = v1083;
    const Real v1101 = math::exp(-33.0*v1083);
    kr_t0[kzs_t0 + 119u] = v1101;
    const Real v1085 = T >= 50.0;
    kr_t0[kzs_t0 + 109u] = v1085;
    const Real v1102 = jac_nuc_choice_54(T, v1101, v1085);
    kr_t0[kzs_t0 + 104u] = v1102;
    const Real v1097 = math::exp((-2)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 116u] = v1097;
    const Real v1098 = 5.25e-11*math::exp(173900.0*v1097 - 4430.0*v1083);
    kr_t0[kzs_t0 + 117u] = v1098;
    const Real v1099 = T > 200.0;
    kr_t0[kzs_t0 + 118u] = v1099;
    const Real v1100 = jac_nuc_choice_53(v1098, v1099);
    kr_t0[kzs_t0 + 105u] = v1100;
    const Real v1087 = M_LN10;
    kr_t0[kzs_t0 + 121u] = v1087;
    const Real v1088 = 1.0/v1087;
    const Real v1089 = math::log(T);
    kr_t0[kzs_t0 + 110u] = v1089;
    const Real v1090 = v1088*v1089;
    const Real v1104 = math::powi<-5>(v1087);
    kr_t0[kzs_t0 + 122u] = v1104;
    const Real v1105 = math::exp((-4)*math::log(math::abs(v1087)));
    kr_t0[kzs_t0 + 123u] = v1105;
    const Real v1106 = ((((v1089)*(v1089)))*(((v1089)*(v1089))));
    kr_t0[kzs_t0 + 124u] = v1106;
    const Real v1107 = math::powi<-3>(v1087);
    kr_t0[kzs_t0 + 125u] = v1107;
    const Real v1108 = ((v1089)*(v1089)*(v1089));
    kr_t0[kzs_t0 + 126u] = v1108;
    const Real v1109 = math::exp((-2)*math::log(math::abs(v1087)));
    kr_t0[kzs_t0 + 127u] = v1109;
    const Real v1110 = ((v1089)*(v1089));
    kr_t0[kzs_t0 + 128u] = v1110;
    const Real v1111 = math::exp((0.31788699999999998*((v1089)*(v1089)*(v1089)*(v1089)*(v1089))*v1104 + 5.8888600000000002*v1090 - 2.1690299999999998*v1105*v1106 + 2.2506900000000001*v1107*v1108 + 7.1969200000000004*v1109*v1110 - 56.473700000000001)*math::log(math::abs(10)));
    kr_t0[kzs_t0 + 129u] = v1111;
    const Real v1091 = math::exp((-3.0)*math::log(math::abs(v1087)));
    kr_t0[kzs_t0 + 113u] = v1091;
    const Real v1092 = math::exp((-2.0)*math::log(math::abs(v1087)));
    kr_t0[kzs_t0 + 111u] = v1092;
    const Real v1093 = ((v1089)*(v1089));
    kr_t0[kzs_t0 + 114u] = v1093;
    const Real v1094 = math::exp((-0.12690000000000001*((v1089)*(v1089)*(v1089))*v1091 - 1.5229999999999999*v1090 + 1.1180000000000001*v1092*v1093 - 19.379999999999999)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 106u] = v1094;
    const Real v1117 = v1088*v1083;
    kr_t0[kzs_t0 + 115u] = v1117;
    const Real v1084 = math::exp(-37.100000000000001*v1083);
    kr_t0[kzs_t0 + 108u] = v1084;
    const Real v1112 = T <= 1167.4796423742259;
    kr_t0[kzs_t0 + 130u] = v1112;
    const Real v1113 = math::exp(-5207.0*v1083);
    kr_t0[kzs_t0 + 131u] = v1113;
    const Real v1116 = math::exp((-0.59000000000000008)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 120u] = v1116;
    const Real v1114 = jac_nuc_choice_55(v1111, v1112, v1113);
    kr_t0[kzs_t0 + 133u] = v1114;
    const Real v1086 = jac_nuc_choice_52(T, v1084, v1085);
    kr_t0[kzs_t0 + 132u] = v1086;
    const Real v790 = math::sqrt(T);
    kr_t0[kzs_t0 + 524u] = v790;
    const Real v791 = 1.0/v790;
    kr_t0[kzs_t0 + 134u] = v791;
    const Real v884 = 2.6534040307116387e-9*math::exp((-0.10000000000000001)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 135u] = v884;
    const Real v887 = 6.1739095063118665e-10*math::exp((0.40999999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 136u] = v887;
    const Real v1563 = 8.6173430000000006e-5*T;
    const Real v1564 = v1563 <= 9280.0;
    kr_t0[kzs_t0 + 140u] = v1564;
    const Real v1565 = 1.0/T;
    kr_t0[kzs_t0 + 143u] = v1565;
    const Real v1566 = (1.5400000000000001e-9 + 4.6200000000000001e-10*math::exp(-93988.701501924661*v1565))*math::exp(-469943.50750964211*v1565);
    kr_t0[kzs_t0 + 144u] = v1566;
    const Real v1562 = 1.4981088130721367e-10*math::exp((-0.63529999999999998)*math::log(math::abs(T)));
    const Real v1567 = jac_nuc_choice_80(T, v1562, v1564, v1566);
    kr_t0[kzs_t0 + 137u] = v1567;
    const Real v1568 = math::log(v1563);
    kr_t0[kzs_t0 + 148u] = v1568;
    const Real v1569 = ((v1568)*(v1568));
    kr_t0[kzs_t0 + 149u] = v1569;
    const Real v1570 = ((v1568)*(v1568)*(v1568));
    kr_t0[kzs_t0 + 150u] = v1570;
    const Real v1571 = ((((v1568)*(v1568)))*(((v1568)*(v1568))));
    kr_t0[kzs_t0 + 151u] = v1571;
    const Real v1572 = ((v1568)*(v1568)*(v1568)*(v1568)*(v1568));
    kr_t0[kzs_t0 + 145u] = v1572;
    const Real v1573 = math::exp((6)*math::log(math::abs(v1568)));
    kr_t0[kzs_t0 + 146u] = v1573;
    const Real v1574 = ((v1568)*(v1568)*(v1568)*(v1568)*(v1568)*(v1568)*(v1568));
    kr_t0[kzs_t0 + 147u] = v1574;
    const Real v1575 = math::exp(0.067953912300000002*v1572 - 0.0050090561*v1573 + 0.000206723616*v1574 - 3.6491614100000001e-6*math::exp((8)*math::log(math::abs(v1568))) - 10.753230200000001*v1569 + 3.0580387500000001*v1570 - 0.56851189000000002*v1571);
    kr_t0[kzs_t0 + 138u] = v1575;
    const Real v1577 = 4.3524079114767552e-117*math::exp((23.915965629999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 139u] = v1577;
    const Real v1579 = math::exp((-0.75)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 154u] = v1579;
    const Real v1580 = math::exp(-127500.0*v1565);
    kr_t0[kzs_t0 + 152u] = v1580;
    const Real v1581 = T <= 10000.0;
    kr_t0[kzs_t0 + 153u] = v1581;
    const Real v1584 = -9.5174852894472843e-11*math::exp((-1.6353)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 141u] = v1584;
    const Real v1585 = math::exp((-3.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 142u] = v1585;
    const Real v1582 = jac_nuc_choice_81(T, v1579, v1580, v1581);
    kr_t0[kzs_t0 + 155u] = v1582;
    const Real v550 = 0.00060040841663220993*v535 + 1.0;
    kr_t0[kzs_t0 + 314u] = v550;
    const Real v551 = math::exp((-1.7524)*math::log(math::abs(v550)));
    kr_t0[kzs_t0 + 156u] = v551;
    const Real v552 = 0.32668576019240059*v535 + 1.0;
    kr_t0[kzs_t0 + 318u] = v552;
    const Real v553 = math::exp((-0.24759999999999999)*math::log(math::abs(v552)));
    kr_t0[kzs_t0 + 157u] = v553;
    const Real v556 = 5.7884371785482823e-10*v536;
    kr_t0[kzs_t0 + 158u] = v556;
    const Real v666 = math::sqrt(T);
    const Real v667 = 1.0/v666;
    kr_t0[kzs_t0 + 265u] = v667;
    const Real v668 = T >= 10.0 && T <= 100000.0;
    kr_t0[kzs_t0 + 266u] = v668;
    const Real v669 = jac_nuc_choice_11(T, v666, v667, v668);
    kr_t0[kzs_t0 + 160u] = v669;
    const Real v655 = 1.0e-8*math::exp((-0.40000000000000002)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 159u] = v655;
    const Real v560 = T <= 10000.0;
    kr_t0[kzs_t0 + 313u] = v560;
    const Real v557 = ((T)*(T));
    kr_t0[kzs_t0 + 310u] = v557;
    const Real v558 = ((T)*(T)*(T));
    kr_t0[kzs_t0 + 311u] = v558;
    const Real v559 = ((((T)*(T)))*(((T)*(T))));
    kr_t0[kzs_t0 + 312u] = v559;
    const Real v561 = jac_nuc_choice_0(T, v557, v558, v559, v560);
    kr_t0[kzs_t0 + 161u] = v561;
    const Real v729 = 1.0/T;
    kr_t0[kzs_t0 + 427u] = v729;
    const Real v764 = math::exp(-127500.0*v729);
    kr_t0[kzs_t0 + 486u] = v764;
    const Real v739 = T <= 10000.0;
    kr_t0[kzs_t0 + 483u] = v739;
    const Real v763 = math::exp((-0.75)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 506u] = v763;
    const Real v765 = jac_nuc_choice_24(T, v739, v763, v764);
    kr_t0[kzs_t0 + 162u] = v765;
    const Real v1515 = 1.0/T;
    kr_t0[kzs_t0 + 169u] = v1515;
    const Real v1532 = math::exp(-127500.0*v1515);
    kr_t0[kzs_t0 + 178u] = v1532;
    const Real v1531 = math::exp((-0.75)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 181u] = v1531;
    const Real v1533 = T <= 10000.0;
    kr_t0[kzs_t0 + 179u] = v1533;
    const Real v1534 = jac_nuc_choice_77(T, v1531, v1532, v1533);
    kr_t0[kzs_t0 + 163u] = v1534;
    const Real v1513 = 8.6173430000000006e-5*T;
    const Real v1518 = math::log(v1513);
    const Real v1519 = ((v1518)*(v1518));
    kr_t0[kzs_t0 + 170u] = v1519;
    const Real v1520 = ((v1518)*(v1518)*(v1518));
    const Real v1521 = ((((v1518)*(v1518)))*(((v1518)*(v1518))));
    kr_t0[kzs_t0 + 171u] = v1521;
    const Real v1522 = ((v1518)*(v1518)*(v1518)*(v1518)*(v1518));
    const Real v1523 = math::exp((6)*math::log(math::abs(v1518)));
    kr_t0[kzs_t0 + 172u] = v1523;
    const Real v1524 = ((v1518)*(v1518)*(v1518)*(v1518)*(v1518)*(v1518)*(v1518));
    const Real v1525 = math::exp((8)*math::log(math::abs(v1518)));
    const Real v1529 = math::exp(-18.480669935680002*v1519 + 4.7016264867590021*v1520 - 0.76924663344919997*v1521 + 0.081130420973029999*v1522 - 0.005324020628287001*v1523 + 0.00019757053122209999*v1524 - 3.1655810656650001e-6*v1525);
    kr_t0[kzs_t0 + 167u] = v1529;
    const Real v1530 = 3.8571873359681582e-209*math::exp((43.933476326349997)*math::log(math::abs(T)))*v1529;
    kr_t0[kzs_t0 + 182u] = v1530;
    const Real v1526 = math::exp(-10.753230200000001*v1519 + 3.0580387500000001*v1520 - 0.56851189000000002*v1521 + 0.067953912300000002*v1522 - 0.0050090561*v1523 + 0.000206723616*v1524 - 3.6491614100000001e-6*v1525);
    kr_t0[kzs_t0 + 166u] = v1526;
    const Real v1504 = math::sqrt(T);
    const Real v1507 = 0.32668576019240059*v1504 + 1.0;
    kr_t0[kzs_t0 + 180u] = v1507;
    const Real v1508 = math::exp((-0.24759999999999999)*math::log(math::abs(v1507)));
    kr_t0[kzs_t0 + 164u] = v1508;
    const Real v1505 = 0.00060040841663220993*v1504 + 1.0;
    kr_t0[kzs_t0 + 177u] = v1505;
    const Real v1506 = math::exp((-1.7524)*math::log(math::abs(v1505)));
    kr_t0[kzs_t0 + 165u] = v1506;
    const Real v1541 = v1515*v1520;
    kr_t0[kzs_t0 + 174u] = v1541;
    const Real v1542 = v1515*v1522;
    kr_t0[kzs_t0 + 175u] = v1542;
    const Real v1543 = v1515*v1524;
    kr_t0[kzs_t0 + 176u] = v1543;
    const Real v1540 = v1515*v1518;
    kr_t0[kzs_t0 + 173u] = v1540;
    const Real v1514 = v1513 <= 9280.0;
    kr_t0[kzs_t0 + 183u] = v1514;
    const Real v1538 = -9.5174852894472843e-11*math::exp((-1.6353)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 185u] = v1538;
    const Real v1539 = math::exp((-3.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 186u] = v1539;
    const Real v1516 = (1.5400000000000001e-9 + 4.6200000000000001e-10*math::exp(-93988.701501924661*v1515))*math::exp(-469943.50750964211*v1515);
    kr_t0[kzs_t0 + 184u] = v1516;
    const Real v1527 = math::exp((23.915965629999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 168u] = v1527;
    const Real v1512 = 1.4981088130721367e-10*math::exp((-0.63529999999999998)*math::log(math::abs(T)));
    const Real v1517 = jac_nuc_choice_76(T, v1514, v1516, v1512);
    kr_t0[kzs_t0 + 187u] = v1517;
    const Real v1511 = 5.7884371785482823e-10/v1504;
    kr_t0[kzs_t0 + 189u] = v1511;
    const Real v1528 = 4.3524079114767552e-117*v1527;
    kr_t0[kzs_t0 + 188u] = v1528;
    const Real v1465 = math::sqrt(T);
    const Real v1466 = 0.00060040841663220993*v1465 + 1.0;
    kr_t0[kzs_t0 + 195u] = v1466;
    const Real v1467 = math::exp((-1.7524)*math::log(math::abs(v1466)));
    kr_t0[kzs_t0 + 190u] = v1467;
    const Real v1468 = 0.32668576019240059*v1465 + 1.0;
    kr_t0[kzs_t0 + 204u] = v1468;
    const Real v1469 = math::exp((-0.24759999999999999)*math::log(math::abs(v1468)));
    kr_t0[kzs_t0 + 191u] = v1469;
    const Real v1472 = 5.7884371785482823e-10/v1465;
    kr_t0[kzs_t0 + 192u] = v1472;
    const Real v1473 = math::log(8.6173430000000006e-5*T);
    kr_t0[kzs_t0 + 202u] = v1473;
    const Real v1474 = ((v1473)*(v1473));
    kr_t0[kzs_t0 + 203u] = v1474;
    const Real v1475 = ((v1473)*(v1473)*(v1473));
    kr_t0[kzs_t0 + 197u] = v1475;
    const Real v1476 = ((((v1473)*(v1473)))*(((v1473)*(v1473))));
    kr_t0[kzs_t0 + 198u] = v1476;
    const Real v1477 = ((v1473)*(v1473)*(v1473)*(v1473)*(v1473));
    kr_t0[kzs_t0 + 199u] = v1477;
    const Real v1478 = math::exp((6)*math::log(math::abs(v1473)));
    kr_t0[kzs_t0 + 200u] = v1478;
    const Real v1479 = ((v1473)*(v1473)*(v1473)*(v1473)*(v1473)*(v1473)*(v1473));
    kr_t0[kzs_t0 + 201u] = v1479;
    const Real v1480 = math::exp(4.7016264867590021*v1475 - 0.76924663344919997*v1476 + 0.081130420973029999*v1477 - 0.005324020628287001*v1478 + 0.00019757053122209999*v1479 - 3.1655810656650001e-6*math::exp((8)*math::log(math::abs(v1473))) - 18.480669935680002*v1474);
    kr_t0[kzs_t0 + 193u] = v1480;
    const Real v1482 = 3.8571873359681582e-209*math::exp((43.933476326349997)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 194u] = v1482;
    const Real v1485 = 1.0/T;
    kr_t0[kzs_t0 + 196u] = v1485;
    const Real v1233 = M_LN10;
    kr_t0[kzs_t0 + 391u] = v1233;
    const Real v1234 = 1.0/v1233;
    kr_t0[kzs_t0 + 357u] = v1234;
    const Real v1218 = math::log(T);
    kr_t0[kzs_t0 + 358u] = v1218;
    const Real v1271 = v1234*v1218;
    kr_t0[kzs_t0 + 352u] = v1271;
    const Real v1235 = math::exp((-2)*math::log(math::abs(v1233)));
    kr_t0[kzs_t0 + 349u] = v1235;
    const Real v1219 = ((v1218)*(v1218));
    kr_t0[kzs_t0 + 350u] = v1219;
    const Real v1340 = 1.3700000000000002e-10*v1235*v1219 - 8.4600000000000008e-10*v1271 - 4.1700000000000001e-10;
    kr_t0[kzs_t0 + 205u] = v1340;
    const Real v1210 = 1.0/T;
    kr_t0[kzs_t0 + 348u] = v1210;
    const Real v1211 = math::exp(-46707.0*v1210);
    kr_t0[kzs_t0 + 206u] = v1211;
    const Real v1214 = math::exp(-102000.0*v1210);
    kr_t0[kzs_t0 + 207u] = v1214;
    const Real v1216 = 4.3799999999999999e-10*math::exp((0.34999999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 209u] = v1216;
    const Real v1213 = 35.5*math::exp((-2.2799999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 208u] = v1213;
    const Real v985 = math::log(8.6173430000000006e-5*T);
    const Real v992 = math::exp((8)*math::log(math::abs(v985)));
    kr_t0[kzs_t0 + 227u] = v992;
    const Real v986 = ((v985)*(v985));
    kr_t0[kzs_t0 + 219u] = v986;
    const Real v987 = ((v985)*(v985)*(v985));
    const Real v988 = ((((v985)*(v985)))*(((v985)*(v985))));
    kr_t0[kzs_t0 + 214u] = v988;
    const Real v989 = ((v985)*(v985)*(v985)*(v985)*(v985));
    kr_t0[kzs_t0 + 216u] = v989;
    const Real v990 = math::exp((6)*math::log(math::abs(v985)));
    kr_t0[kzs_t0 + 217u] = v990;
    const Real v991 = ((v985)*(v985)*(v985)*(v985)*(v985)*(v985)*(v985));
    kr_t0[kzs_t0 + 218u] = v991;
    const Real v993 = math::exp(0.01623316639567*v987 - 0.033650120313629989*v988 + 0.01178329782711*v989 - 0.001656194699504*v990 + 0.0001068275202678*v991 - 2.6312858092069998e-6*v992 - 0.28274430617039997*v986);
    kr_t0[kzs_t0 + 210u] = v993;
    const Real v995 = 3.7903999274394518e-18*math::exp((2.360852208681)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 211u] = v995;
    const Real v1008 = math::exp(0.0084644553866299998*v987 - 0.0014327641212992001*v988 + 0.00020122502847909999*v989 + 8.6639632430900003e-5*v990 - 2.5850096802639999e-5*v991 + 2.4555011970391999e-6*v992 - 8.0683824611800006e-8*((v985)*(v985)*(v985)*(v985)*(v985)*(v985)*(v985)*(v985)*(v985)) - 0.14210135215541481*v986);
    kr_t0[kzs_t0 + 229u] = v1008;
    const Real v1009 = 3.3178155742407614e-14*math::exp((1.1394493358416311)*math::log(math::abs(T)))*v1008;
    kr_t0[kzs_t0 + 230u] = v1009;
    const Real v1020 = v987*v981;
    kr_t0[kzs_t0 + 221u] = v1020;
    const Real v977 = math::exp(-6.1728395061728397e-5*T);
    kr_t0[kzs_t0 + 212u] = v977;
    const Real v1003 = 0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0;
    kr_t0[kzs_t0 + 232u] = v1003;
    const Real v1004 = 1.0/v1003;
    kr_t0[kzs_t0 + 226u] = v1004;
    const Real v1019 = v981*v985;
    kr_t0[kzs_t0 + 220u] = v1019;
    const Real v979 = math::exp((0.92800000000000005)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 213u] = v979;
    const Real v1005 = 1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 231u] = v1005;
    const Real v1007 = T <= 1160.0;
    kr_t0[kzs_t0 + 228u] = v1007;
    const Real v1016 = math::exp((-0.59000000000000008)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 225u] = v1016;
    const Real v1017 = math::exp((-3.0/2.0)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 224u] = v1017;
    const Real v1006 = v1004*v1005;
    kr_t0[kzs_t0 + 233u] = v1006;
    const Real v1010 = jac_nuc_choice_45(T, v1007, v1009);
    kr_t0[kzs_t0 + 234u] = v1010;
    const Real v980 = 1.4000000000000001e-18*v979;
    kr_t0[kzs_t0 + 235u] = v980;
    const Real v671 = M_LN10;
    kr_t0[kzs_t0 + 258u] = v671;
    const Real v672 = 1.0/v671;
    kr_t0[kzs_t0 + 282u] = v672;
    const Real v670 = math::log(T);
    kr_t0[kzs_t0 + 257u] = v670;
    const Real v673 = v670*v672;
    const Real v682 = math::exp((-2)*math::log(math::abs(v671)));
    kr_t0[kzs_t0 + 261u] = v682;
    const Real v683 = ((v670)*(v670));
    const Real v696 = 8.4600000000000008e-10*v673 - 1.3700000000000002e-10*v682*v683 + 4.1700000000000001e-10;
    kr_t0[kzs_t0 + 237u] = v696;
    const Real v664 = T >= 50.0;
    kr_t0[kzs_t0 + 268u] = v664;
    const Real v694 = math::exp(-33.0*v656);
    kr_t0[kzs_t0 + 281u] = v694;
    const Real v695 = jac_nuc_choice_14(T, v664, v694);
    kr_t0[kzs_t0 + 236u] = v695;
    const Real v638 = 8.6173430000000006e-5*T;
    const Real v639 = math::log(v638);
    const Real v640 = ((v639)*(v639));
    kr_t0[kzs_t0 + 244u] = v640;
    const Real v641 = ((v639)*(v639)*(v639));
    const Real v642 = ((((v639)*(v639)))*(((v639)*(v639))));
    kr_t0[kzs_t0 + 245u] = v642;
    const Real v643 = ((v639)*(v639)*(v639)*(v639)*(v639));
    kr_t0[kzs_t0 + 246u] = v643;
    const Real v644 = math::exp((6)*math::log(math::abs(v639)));
    kr_t0[kzs_t0 + 247u] = v644;
    const Real v645 = ((v639)*(v639)*(v639)*(v639)*(v639)*(v639)*(v639));
    const Real v646 = math::exp((8)*math::log(math::abs(v639)));
    kr_t0[kzs_t0 + 251u] = v646;
    const Real v647 = math::exp(-5.7393287500000003*v640 + 1.56315498*v641 - 0.28770560000000001*v642 + 0.034825597700000002*v643 - 0.00263197617*v644 + 0.000111954395*v645 - 2.0391498499999999e-6*v646);
    kr_t0[kzs_t0 + 239u] = v647;
    const Real v652 = math::exp(-3.0711352431965949e-9*((v639)*(v639)*(v639)*(v639)*(v639)*(v639)*(v639)*(v639)*(v639)) - 0.02026044731984691*v640 - 0.002380861877349834*v641 - 0.00032126052131887958*v642 - 1.421502914054107e-5*v643 + 4.9891089202995129e-6*v644 + 5.7556141375757583e-7*v645 - 1.8567670397752609e-8*v646);
    kr_t0[kzs_t0 + 242u] = v652;
    const Real v681 = ((v670)*(v670)*(v670));
    kr_t0[kzs_t0 + 269u] = v681;
    const Real v686 = math::exp(-21237.150000000001*v656);
    kr_t0[kzs_t0 + 270u] = v686;
    const Real v687 = ((((v670)*(v670)))*(((v670)*(v670))));
    kr_t0[kzs_t0 + 271u] = v687;
    const Real v688 = ((v670)*(v670)*(v670)*(v670)*(v670));
    kr_t0[kzs_t0 + 272u] = v688;
    const Real v689 = math::exp((6)*math::log(math::abs(v670)));
    kr_t0[kzs_t0 + 273u] = v689;
    const Real v690 = v686*(3.5311931999999998e-13*((v670)*(v670)*(v670)*(v670)*(v670)*(v670)*(v670)) + 3.3735381999999997e-7*v670 + 3.4172804999999998e-8*v681 - 1.4491368e-7*v683 - 4.7813727999999997e-9*v687 + 3.9731542e-10*v688 - 1.8171411000000001e-11*v689 - 3.3232183000000002e-7);
    kr_t0[kzs_t0 + 274u] = v690;
    const Real v674 = math::exp((-3.0)*math::log(math::abs(v671)));
    kr_t0[kzs_t0 + 278u] = v674;
    const Real v675 = math::exp((-2.0)*math::log(math::abs(v671)));
    kr_t0[kzs_t0 + 277u] = v675;
    const Real v676 = ((v670)*(v670));
    kr_t0[kzs_t0 + 279u] = v676;
    const Real v677 = math::exp((-0.12690000000000001*((v670)*(v670)*(v670))*v674 - 1.5229999999999999*v673 + 1.1180000000000001*v675*v676 - 19.379999999999999)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 238u] = v677;
    const Real v705 = v656*v641;
    kr_t0[kzs_t0 + 249u] = v705;
    const Real v706 = v656*v645;
    kr_t0[kzs_t0 + 250u] = v706;
    const Real v680 = math::powi<-3>(v671);
    kr_t0[kzs_t0 + 260u] = v680;
    const Real v684 = math::exp((-3.194*v673 - 0.2072*v680*v681 + 1.786*v682*v683 - 18.199999999999999)*math::log(math::abs(10)));
    kr_t0[kzs_t0 + 262u] = v684;
    const Real v704 = v639*v656;
    kr_t0[kzs_t0 + 248u] = v704;
    const Real v650 = v638 <= 5500.0;
    kr_t0[kzs_t0 + 240u] = v650;
    const Real v648 = math::exp((13.536555999999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 252u] = v648;
    const Real v651 = math::exp((-0.72411256578268512)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 241u] = v651;
    const Real v663 = math::exp(-37.100000000000001*v656);
    kr_t0[kzs_t0 + 267u] = v663;
    const Real v679 = T < 30;
    kr_t0[kzs_t0 + 259u] = v679;
    const Real v691 = T >= 100.0 && T <= 30000.0;
    kr_t0[kzs_t0 + 275u] = v691;
    const Real v700 = math::exp((-2)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 254u] = v700;
    const Real v701 = v656*v670;
    kr_t0[kzs_t0 + 276u] = v701;
    const Real v702 = v656*v672;
    kr_t0[kzs_t0 + 263u] = v702;
    const Real v703 = v656*v683;
    kr_t0[kzs_t0 + 264u] = v703;
    const Real v653 = jac_nuc_choice_8(T, v650, v651, v652);
    kr_t0[kzs_t0 + 283u] = v653;
    const Real v685 = jac_nuc_choice_12(T, v679, v684);
    kr_t0[kzs_t0 + 284u] = v685;
    const Real v692 = jac_nuc_choice_13(v690, v691);
    kr_t0[kzs_t0 + 286u] = v692;
    const Real v665 = jac_nuc_choice_10(T, v663, v664);
    kr_t0[kzs_t0 + 285u] = v665;
    const Real v649 = 5.9082438637265071e-70*v648;
    kr_t0[kzs_t0 + 287u] = v649;
    const Real v780 = M_LN10;
    kr_t0[kzs_t0 + 488u] = v780;
    const Real v781 = 1.0/v780;
    kr_t0[kzs_t0 + 446u] = v781;
    const Real v770 = math::log(T);
    kr_t0[kzs_t0 + 438u] = v770;
    const Real v782 = v770*v781;
    kr_t0[kzs_t0 + 453u] = v782;
    const Real v897 = math::exp((-3.0)*math::log(math::abs(v780)));
    kr_t0[kzs_t0 + 508u] = v897;
    const Real v899 = ((v770)*(v770));
    kr_t0[kzs_t0 + 509u] = v899;
    const Real v898 = math::exp((-2.0)*math::log(math::abs(v780)));
    kr_t0[kzs_t0 + 510u] = v898;
    const Real v900 = math::exp((-0.12690000000000001*v897*((v770)*(v770)*(v770)) + 1.1180000000000001*v898*v899 - 1.5229999999999999*v782 - 19.379999999999999)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 289u] = v900;
    const Real v767 = T >= 50.0;
    kr_t0[kzs_t0 + 493u] = v767;
    const Real v895 = math::exp(-33.0*v729);
    kr_t0[kzs_t0 + 507u] = v895;
    const Real v896 = jac_nuc_choice_30(T, v895, v767);
    kr_t0[kzs_t0 + 288u] = v896;
    const Real v1273 = math::powi<-3>(v1233);
    const Real v1220 = ((v1218)*(v1218)*(v1218));
    const Real v1274 = v1220*v1273;
    const Real v1221 = ((((v1218)*(v1218)))*(((v1218)*(v1218))));
    const Real v1222 = ((v1218)*(v1218)*(v1218)*(v1218)*(v1218));
    kr_t0[kzs_t0 + 370u] = v1222;
    const Real v1260 = v1235*v1219;
    kr_t0[kzs_t0 + 359u] = v1260;
    const Real v1341 = math::powi<-5>(v1233);
    kr_t0[kzs_t0 + 385u] = v1341;
    const Real v1342 = math::exp((-4)*math::log(math::abs(v1233)));
    kr_t0[kzs_t0 + 386u] = v1342;
    const Real v1343 = math::exp((-2.1690299999999998*v1221*v1342 + 0.31788699999999998*v1222*v1341 + 7.1969200000000004*v1260 + 5.8888600000000002*v1271 + 2.2506900000000001*v1274 - 56.473700000000001)*math::log(math::abs(10)));
    kr_t0[kzs_t0 + 387u] = v1343;
    const Real v1344 = T <= 1167.4796423742259;
    kr_t0[kzs_t0 + 388u] = v1344;
    const Real v1345 = math::exp(-5207.0*v1210);
    kr_t0[kzs_t0 + 389u] = v1345;
    const Real v1346 = jac_nuc_choice_68(v1343, v1344, v1345);
    kr_t0[kzs_t0 + 290u] = v1346;
    const Real v563 = 8.6173430000000006e-5*T;
    const Real v568 = math::log(v563);
    const Real v569 = ((v568)*(v568));
    kr_t0[kzs_t0 + 298u] = v569;
    const Real v570 = ((v568)*(v568)*(v568));
    const Real v571 = ((((v568)*(v568)))*(((v568)*(v568))));
    kr_t0[kzs_t0 + 299u] = v571;
    const Real v572 = ((v568)*(v568)*(v568)*(v568)*(v568));
    kr_t0[kzs_t0 + 300u] = v572;
    const Real v573 = math::exp((6)*math::log(math::abs(v568)));
    kr_t0[kzs_t0 + 301u] = v573;
    const Real v574 = ((v568)*(v568)*(v568)*(v568)*(v568)*(v568)*(v568));
    kr_t0[kzs_t0 + 324u] = v574;
    const Real v575 = math::exp((8)*math::log(math::abs(v568)));
    const Real v580 = math::exp(-10.753230200000001*v569 + 3.0580387500000001*v570 - 0.56851189000000002*v571 + 0.067953912300000002*v572 - 0.0050090561*v573 + 0.000206723616*v574 - 3.6491614100000001e-6*v575);
    kr_t0[kzs_t0 + 291u] = v580;
    const Real v579 = math::exp((23.915965629999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 336u] = v579;
    const Real v602 = 4.3524079114767552e-117*v579;
    kr_t0[kzs_t0 + 292u] = v602;
    const Real v585 = ((v568)*(v568)*(v568)*(v568)*(v568)*(v568)*(v568)*(v568)*(v568));
    const Real v596 = math::exp(-0.14210135215541481*v569 + 0.0084644553866299998*v570 - 0.0014327641212992001*v571 + 0.00020122502847909999*v572 + 8.6639632430900003e-5*v573 - 2.5850096802639999e-5*v574 + 2.4555011970391999e-6*v575 - 8.0683824611800006e-8*v585);
    kr_t0[kzs_t0 + 326u] = v596;
    const Real v597 = 3.3178155742407614e-14*math::exp((1.1394493358416311)*math::log(math::abs(T)))*v596;
    kr_t0[kzs_t0 + 327u] = v597;
    const Real v581 = math::exp((43.933476326349997)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 335u] = v581;
    const Real v582 = math::exp(-18.480669935680002*v569 + 4.7016264867590021*v570 - 0.76924663344919997*v571 + 0.081130420973029999*v572 - 0.005324020628287001*v573 + 0.00019757053122209999*v574 - 3.1655810656650001e-6*v575);
    kr_t0[kzs_t0 + 296u] = v582;
    const Real v601 = 3.8571873359681582e-209*v581*v582;
    kr_t0[kzs_t0 + 323u] = v601;
    const Real v538 = math::exp(-6.1728395061728397e-5*T);
    kr_t0[kzs_t0 + 293u] = v538;
    const Real v586 = math::exp(-0.02026044731984691*v569 - 0.002380861877349834*v570 - 0.00032126052131887958*v571 - 1.421502914054107e-5*v572 + 4.9891089202995129e-6*v573 + 5.7556141375757583e-7*v574 - 1.8567670397752609e-8*v575 - 3.0711352431965949e-9*v585);
    kr_t0[kzs_t0 + 304u] = v586;
    const Real v590 = 0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0;
    kr_t0[kzs_t0 + 332u] = v590;
    const Real v591 = 1.0/v590;
    kr_t0[kzs_t0 + 321u] = v591;
    const Real v577 = math::exp((13.536555999999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 337u] = v577;
    const Real v594 = 5.9082438637265071e-70*v577;
    kr_t0[kzs_t0 + 315u] = v594;
    const Real v567 = math::exp((2.360852208681)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 338u] = v567;
    const Real v599 = 3.7903999274394518e-18*v567;
    kr_t0[kzs_t0 + 317u] = v599;
    const Real v576 = math::exp(-0.28274430617039997*v569 + 0.01623316639567*v570 - 0.033650120313629989*v571 + 0.01178329782711*v572 - 0.001656194699504*v573 + 0.0001068275202678*v574 - 2.6312858092069998e-6*v575);
    kr_t0[kzs_t0 + 294u] = v576;
    const Real v578 = math::exp(-5.7393287500000003*v569 + 1.56315498*v570 - 0.28770560000000001*v571 + 0.034825597700000002*v572 - 0.00263197617*v573 + 0.000111954395*v574 - 2.0391498499999999e-6*v575);
    kr_t0[kzs_t0 + 295u] = v578;
    const Real v540 = math::exp((0.92800000000000005)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 322u] = v540;
    const Real v564 = v563 <= 9280.0;
    kr_t0[kzs_t0 + 328u] = v564;
    const Real v565 = (1.5400000000000001e-9 + 4.6200000000000001e-10*math::exp(-93988.701501924661*v546))*math::exp(-469943.50750964211*v546);
    kr_t0[kzs_t0 + 329u] = v565;
    const Real v583 = v563 <= 5500.0;
    kr_t0[kzs_t0 + 302u] = v583;
    const Real v584 = math::exp((-0.72411256578268512)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 303u] = v584;
    const Real v592 = 1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 331u] = v592;
    const Real v595 = T <= 1160.0;
    kr_t0[kzs_t0 + 325u] = v595;
    const Real v606 = 2.6534040307116389e-10*math::exp((-1.1000000000000001)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 319u] = v606;
    const Real v616 = -9.5174852894472843e-11*math::exp((-1.6353)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 305u] = v616;
    const Real v617 = math::exp((-3.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 330u] = v617;
    const Real v618 = v546*v568;
    kr_t0[kzs_t0 + 306u] = v618;
    const Real v619 = v546*v570;
    kr_t0[kzs_t0 + 307u] = v619;
    const Real v620 = v546*v572;
    kr_t0[kzs_t0 + 316u] = v620;
    const Real v621 = v546*v574;
    kr_t0[kzs_t0 + 308u] = v621;
    const Real v622 = v546*v575;
    kr_t0[kzs_t0 + 309u] = v622;
    const Real v562 = 1.4981088130721367e-10*math::exp((-0.63529999999999998)*math::log(math::abs(T)));
    const Real v566 = jac_nuc_choice_1(T, v562, v564, v565);
    kr_t0[kzs_t0 + 334u] = v566;
    const Real v587 = jac_nuc_choice_2(v562, v583, v584, v586);
    kr_t0[kzs_t0 + 333u] = v587;
    const Real v541 = 1.4000000000000001e-18*v540;
    kr_t0[kzs_t0 + 339u] = v541;
    const Real v593 = v591*v592;
    kr_t0[kzs_t0 + 340u] = v593;
    const Real v598 = jac_nuc_choice_3(T, v595, v597);
    kr_t0[kzs_t0 + 341u] = v598;
    const Real v588 = 1.0e-8*math::exp((-0.40000000000000002)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 342u] = v588;
    const Real v772 = ((v770)*(v770)*(v770));
    const Real v783 = math::powi<-3>(v780);
    const Real v784 = v772*v783;
    const Real v785 = math::exp((-2)*math::log(math::abs(v780)));
    kr_t0[kzs_t0 + 439u] = v785;
    const Real v771 = ((v770)*(v770));
    kr_t0[kzs_t0 + 452u] = v771;
    const Real v793 = v771*v785;
    kr_t0[kzs_t0 + 456u] = v793;
    const Real v773 = ((((v770)*(v770)))*(((v770)*(v770))));
    const Real v774 = ((v770)*(v770)*(v770)*(v770)*(v770));
    kr_t0[kzs_t0 + 500u] = v774;
    const Real v915 = math::powi<-5>(v780);
    kr_t0[kzs_t0 + 512u] = v915;
    const Real v916 = math::exp((-4)*math::log(math::abs(v780)));
    kr_t0[kzs_t0 + 513u] = v916;
    const Real v917 = math::exp((0.31788699999999998*v915*v774 - 2.1690299999999998*v916*v773 + 5.8888600000000002*v782 + 2.2506900000000001*v784 + 7.1969200000000004*v793 - 56.473700000000001)*math::log(math::abs(10)));
    kr_t0[kzs_t0 + 514u] = v917;
    const Real v918 = T <= 1167.4796423742259;
    kr_t0[kzs_t0 + 515u] = v918;
    const Real v919 = math::exp(-5207.0*v729);
    kr_t0[kzs_t0 + 516u] = v919;
    const Real v920 = jac_nuc_choice_32(v917, v918, v919);
    kr_t0[kzs_t0 + 344u] = v920;
    const Real v766 = math::exp(-37.100000000000001*v729);
    kr_t0[kzs_t0 + 492u] = v766;
    const Real v768 = jac_nuc_choice_25(T, v766, v767);
    kr_t0[kzs_t0 + 343u] = v768;
    const Real v1261 = -133.82830000000001*v1210 - 4.8909149999999997*v1234*v1218 + 0.47490300000000002*v1260;
    const Real v1278 = math::exp((v1261 + 13.656822)*math::log(math::abs(10.0)));
    const Real v1279 = 1.0/v1278;
    kr_t0[kzs_t0 + 345u] = v1279;
    const Real v1265 = math::exp(-0.0022727272727272726*T);
    const Real v1266 = math::exp(-0.00054054054054054055*T);
    const Real v1267 = -2.0563129999999998*v1265 + 0.58640729999999996*v1266 + 0.82274429999999998;
    kr_t0[kzs_t0 + 346u] = v1267;
    const Real v1262 = math::exp((v1261 + 14.82123)*math::log(math::abs(10.0)));
    const Real v1263 = 1.0/v1262;
    kr_t0[kzs_t0 + 347u] = v1263;
    const Real v1275 = 40870.379999999997*v1210 + 1.0;
    const Real v1276 = -69.700860000000006*v1234*math::log(v1275) + 4.6331670000000003*v1274;
    kr_t0[kzs_t0 + 353u] = v1276;
    const Real v1272 = 16780.950000000001*v1210 + 1.0;
    kr_t0[kzs_t0 + 400u] = v1272;
    const Real v1277 = 19.734269999999999*v1234*math::log(v1272) + 37.886913*v1235*v1219 - 14.509090000000008*v1271 - v1276 - 307.31920000000002;
    kr_t0[kzs_t0 + 351u] = v1277;
    const Real v1295 = 743.05999999999995*v1210 - 2.4640089999999999*v1234*v1218 + 0.19859550000000001*v1260;
    const Real v1307 = math::exp((v1295 + 8.1313220000000008)*math::log(math::abs(10.0)));
    const Real v1308 = 1.0/v1307;
    kr_t0[kzs_t0 + 354u] = v1308;
    const Real v1299 = 2.9375070000000001*v1265 + 0.23588480000000001*v1266 + 0.75022860000000002;
    kr_t0[kzs_t0 + 355u] = v1299;
    const Real v1296 = math::exp((v1295 + 9.3055640000000004)*math::log(math::abs(10.0)));
    const Real v1297 = 1.0/v1296;
    kr_t0[kzs_t0 + 356u] = v1297;
    const Real v1304 = 27535.310000000001*v1210 + 1.0;
    const Real v1305 = -21.360939999999999*v1234*math::log(v1304) + 0.25820969999999999*v1274;
    kr_t0[kzs_t0 + 361u] = v1305;
    const Real v1303 = 14254.549999999999*v1210 + 1.0;
    kr_t0[kzs_t0 + 398u] = v1303;
    const Real v1306 = 70.138370000000009*v1234*v1218 + 11.28215*v1234*math::log(v1303) - 4.7035149999999994*v1260 - v1305 - 203.11568;
    kr_t0[kzs_t0 + 360u] = v1306;
    const Real v1249 = 52000.0*v1210;
    kr_t0[kzs_t0 + 411u] = v1249;
    const Real v1250 = math::exp(-v1249);
    kr_t0[kzs_t0 + 410u] = v1250;
    const Real v1248 = 1.0 - math::exp(-6000.0*v1210);
    kr_t0[kzs_t0 + 409u] = v1248;
    const Real v1251 = v1248*v1250;
    kr_t0[kzs_t0 + 407u] = v1251;
    const Real v1246 = math::sqrt(T);
    kr_t0[kzs_t0 + 406u] = v1246;
    const Real v1247 = 1.0/v1246;
    kr_t0[kzs_t0 + 420u] = v1247;
    const Real v1252 = 8.1250000000000003e-8*v1247*v1251;
    kr_t0[kzs_t0 + 363u] = v1252;
    const Real v1232 = math::log(0.0001*T);
    kr_t0[kzs_t0 + 367u] = v1232;
    const Real v1236 = math::exp((-1.6200000000000001*((v1232)*(v1232))*v1235 + 1.3*v1232*v1234 - 4.8449999999999998)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 362u] = v1236;
    const Real v1229 = 1.1800000000000001e-10*math::exp(-69500.0*v1210);
    kr_t0[kzs_t0 + 364u] = v1229;
    const Real v1258 = math::log(v1252);
    kr_t0[kzs_t0 + 365u] = v1258;
    const Real v1230 = math::log(v1229);
    kr_t0[kzs_t0 + 366u] = v1230;
    const Real v1359 = v1210*v1218;
    kr_t0[kzs_t0 + 373u] = v1359;
    const Real v1360 = v1359*v1235;
    kr_t0[kzs_t0 + 384u] = v1360;
    const Real v1325 = math::exp((-2)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 368u] = v1325;
    const Real v1358 = v1210*v1234;
    kr_t0[kzs_t0 + 383u] = v1358;
    const Real v1374 = v1299*(1710.9588792001557*v1325 + 5.6735903924031659*v1358 - 0.91456607567139814*v1360);
    kr_t0[kzs_t0 + 393u] = v1374;
    const Real v1371 = v1267*(11.261747970100974*v1210*v1234 - 308.15104860073512*v1325 - 2.1870091368363029*v1360);
    kr_t0[kzs_t0 + 402u] = v1371;
    const Real v1287 = v1278*v1279;
    kr_t0[kzs_t0 + 403u] = v1287;
    const Real v1293 = v1262*v1263;
    kr_t0[kzs_t0 + 404u] = v1293;
    const Real v1316 = v1307*v1308;
    kr_t0[kzs_t0 + 392u] = v1316;
    const Real v1321 = v1296*v1297;
    kr_t0[kzs_t0 + 395u] = v1321;
    const Real v1368 = v1325*v1234;
    kr_t0[kzs_t0 + 397u] = v1368;
    const Real v1369 = v1368/v1275;
    kr_t0[kzs_t0 + 399u] = v1369;
    const Real v1372 = v1368/v1304;
    kr_t0[kzs_t0 + 396u] = v1372;
    const Real v1217 = math::exp(-21237.150000000001*v1210);
    kr_t0[kzs_t0 + 378u] = v1217;
    const Real v1223 = math::exp((6)*math::log(math::abs(v1218)));
    kr_t0[kzs_t0 + 371u] = v1223;
    const Real v1224 = v1217*(3.4172804999999998e-8*v1220 - 4.7813727999999997e-9*v1221 + 3.9731542e-10*v1222 - 1.8171411000000001e-11*v1223 + 3.5311931999999998e-13*((v1218)*(v1218)*(v1218)*(v1218)*(v1218)*(v1218)*(v1218)) + 3.3735381999999997e-7*v1218 - 1.4491368e-7*v1219 - 3.3232183000000002e-7);
    kr_t0[kzs_t0 + 372u] = v1224;
    const Real v1370 = 0.0046734386363636356*v1265 - 0.00031697691891891889*v1266;
    kr_t0[kzs_t0 + 401u] = v1370;
    const Real v1373 = -0.0066761522727272725*v1265 - 0.0001275052972972973*v1266;
    kr_t0[kzs_t0 + 394u] = v1373;
    const Real v1326 = 5.25e-11*math::exp(-4430.0*v1210 + 173900.0*v1325);
    kr_t0[kzs_t0 + 379u] = v1326;
    const Real v1332 = 0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0;
    kr_t0[kzs_t0 + 413u] = v1332;
    const Real v1333 = 1.0/v1332;
    kr_t0[kzs_t0 + 405u] = v1333;
    const Real v1362 = v1210*v1219;
    kr_t0[kzs_t0 + 375u] = v1362;
    const Real v1365 = v1362*v1273;
    kr_t0[kzs_t0 + 390u] = v1365;
    const Real v1225 = T >= 100.0 && T <= 30000.0;
    kr_t0[kzs_t0 + 374u] = v1225;
    const Real v1227 = math::exp(-457.0*v1210);
    kr_t0[kzs_t0 + 369u] = v1227;
    const Real v1327 = T > 200.0;
    kr_t0[kzs_t0 + 380u] = v1327;
    const Real v1334 = 1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 412u] = v1334;
    const Real v1355 = math::exp((-1.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 381u] = v1355;
    const Real v1356 = math::exp((-1.25)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 382u] = v1356;
    const Real v1363 = v1210*v1220;
    kr_t0[kzs_t0 + 376u] = v1363;
    const Real v1364 = v1210*v1221;
    kr_t0[kzs_t0 + 377u] = v1364;
    const Real v1367 = math::exp((-2.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 408u] = v1367;
    const Real v1331 = math::exp((-0.25)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 419u] = v1331;
    const Real v1336 = 1.5e-32*v1331 + 5.0000000000000004e-32*v1247;
    kr_t0[kzs_t0 + 415u] = v1336;
    const Real v1338 = -7.5000000000000001e-33*v1331 - 2.5000000000000002e-32*v1247;
    kr_t0[kzs_t0 + 416u] = v1338;
    const Real v1226 = jac_nuc_choice_66(v1224, v1225);
    kr_t0[kzs_t0 + 414u] = v1226;
    const Real v1328 = jac_nuc_choice_67(v1326, v1327);
    kr_t0[kzs_t0 + 417u] = v1328;
    const Real v1335 = v1333*v1334;
    kr_t0[kzs_t0 + 418u] = v1335;
    const Real v1339 = 4.9999999999999996e-6/math::sqrt(T);
    kr_t0[kzs_t0 + 421u] = v1339;
    const Real v1228 = 1.0000000000000001e-9*v1227;
    kr_t0[kzs_t0 + 422u] = v1228;
    const Real v794 = -4.8909149999999997*v770*v781 - 133.82830000000001*v729 + 0.47490300000000002*v793;
    const Real v795 = math::exp((v794 + 14.82123)*math::log(math::abs(10.0)));
    const Real v796 = 1.0/v795;
    kr_t0[kzs_t0 + 423u] = v796;
    const Real v798 = math::exp(-0.0022727272727272726*T);
    const Real v799 = math::exp(-0.00054054054054054055*T);
    const Real v800 = -2.0563129999999998*v798 + 0.58640729999999996*v799 + 0.82274429999999998;
    kr_t0[kzs_t0 + 424u] = v800;
    const Real v805 = 40870.379999999997*v729 + 1.0;
    const Real v806 = -69.700860000000006*v781*math::log(v805) + 4.6331670000000003*v784;
    kr_t0[kzs_t0 + 454u] = v806;
    const Real v804 = 16780.950000000001*v729 + 1.0;
    kr_t0[kzs_t0 + 437u] = v804;
    const Real v807 = 37.886913*v771*v785 + 19.734269999999999*v781*math::log(v804) - 14.509090000000008*v782 - v806 - 307.31920000000002;
    kr_t0[kzs_t0 + 425u] = v807;
    const Real v808 = math::exp((v794 + 13.656822)*math::log(math::abs(10.0)));
    const Real v809 = 1.0/v808;
    kr_t0[kzs_t0 + 426u] = v809;
    const Real v941 = v770*v729;
    kr_t0[kzs_t0 + 496u] = v941;
    const Real v948 = v941*v785;
    kr_t0[kzs_t0 + 434u] = v948;
    const Real v736 = ((T)*(T));
    kr_t0[kzs_t0 + 480u] = v736;
    const Real v891 = 1.0/v736;
    kr_t0[kzs_t0 + 431u] = v891;
    const Real v956 = v800*(-308.15104860073512*v891 - 2.1870091368363029*v948 + 11.261747970100974*v781*v729);
    kr_t0[kzs_t0 + 429u] = v956;
    const Real v953 = v891*v781;
    kr_t0[kzs_t0 + 436u] = v953;
    const Real v954 = v953/v805;
    kr_t0[kzs_t0 + 435u] = v954;
    const Real v817 = v808*v809;
    kr_t0[kzs_t0 + 440u] = v817;
    const Real v823 = v795*v796;
    kr_t0[kzs_t0 + 428u] = v823;
    const Real v955 = 0.0046734386363636356*v798 - 0.00031697691891891889*v799;
    kr_t0[kzs_t0 + 430u] = v955;
    const Real v939 = v771*v729;
    kr_t0[kzs_t0 + 495u] = v939;
    const Real v940 = v939*v783;
    kr_t0[kzs_t0 + 433u] = v940;
    const Real v938 = v781*v729;
    kr_t0[kzs_t0 + 432u] = v938;
    const Real v826 = -2.4640089999999999*v770*v781 + 743.05999999999995*v729 + 0.19859550000000001*v793;
    const Real v827 = math::exp((v826 + 9.3055640000000004)*math::log(math::abs(10.0)));
    const Real v828 = 1.0/v827;
    kr_t0[kzs_t0 + 441u] = v828;
    const Real v830 = 2.9375070000000001*v798 + 0.23588480000000001*v799 + 0.75022860000000002;
    kr_t0[kzs_t0 + 442u] = v830;
    const Real v835 = 27535.310000000001*v729 + 1.0;
    const Real v836 = -21.360939999999999*v781*math::log(v835) + 0.25820969999999999*v784;
    kr_t0[kzs_t0 + 455u] = v836;
    const Real v834 = 14254.549999999999*v729 + 1.0;
    kr_t0[kzs_t0 + 447u] = v834;
    const Real v837 = -v836 + 70.138370000000009*v770*v781 + 11.28215*v781*math::log(v834) - 4.7035149999999994*v793 - 203.11568;
    kr_t0[kzs_t0 + 443u] = v837;
    const Real v838 = math::exp((v826 + 8.1313220000000008)*math::log(math::abs(10.0)));
    const Real v839 = 1.0/v838;
    kr_t0[kzs_t0 + 444u] = v839;
    const Real v847 = v838*v839;
    kr_t0[kzs_t0 + 448u] = v847;
    const Real v852 = v827*v828;
    kr_t0[kzs_t0 + 451u] = v852;
    const Real v958 = v953/v835;
    kr_t0[kzs_t0 + 445u] = v958;
    const Real v960 = v830*(1710.9588792001557*v891 + 5.6735903924031659*v938 - 0.91456607567139814*v948);
    kr_t0[kzs_t0 + 449u] = v960;
    const Real v959 = -0.0066761522727272725*v798 - 0.0001275052972972973*v799;
    kr_t0[kzs_t0 + 450u] = v959;
    const Real v858 = math::log(0.0001*T);
    kr_t0[kzs_t0 + 465u] = v858;
    const Real v859 = math::exp((-1.6200000000000001*((v858)*(v858))*v785 + 1.3*v858*v781 - 4.8449999999999998)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 457u] = v859;
    const Real v864 = 52000.0*v729;
    kr_t0[kzs_t0 + 529u] = v864;
    const Real v865 = math::exp(-v864);
    kr_t0[kzs_t0 + 526u] = v865;
    const Real v863 = 1.0 - math::exp(-6000.0*v729);
    kr_t0[kzs_t0 + 525u] = v863;
    const Real v866 = v863*v865;
    kr_t0[kzs_t0 + 528u] = v866;
    const Real v867 = 8.1250000000000003e-8*v866*v791;
    kr_t0[kzs_t0 + 458u] = v867;
    const Real v871 = 1.1800000000000001e-10*math::exp(-69500.0*v729);
    kr_t0[kzs_t0 + 459u] = v871;
    const Real v741 = 8.6173430000000006e-5*T;
    const Real v742 = math::log(v741);
    const Real v743 = ((v742)*(v742));
    kr_t0[kzs_t0 + 469u] = v743;
    const Real v744 = ((v742)*(v742)*(v742));
    const Real v745 = ((((v742)*(v742)))*(((v742)*(v742))));
    kr_t0[kzs_t0 + 474u] = v745;
    const Real v746 = ((v742)*(v742)*(v742)*(v742)*(v742));
    kr_t0[kzs_t0 + 475u] = v746;
    const Real v747 = math::exp((6)*math::log(math::abs(v742)));
    kr_t0[kzs_t0 + 476u] = v747;
    const Real v748 = ((v742)*(v742)*(v742)*(v742)*(v742)*(v742)*(v742));
    kr_t0[kzs_t0 + 523u] = v748;
    const Real v749 = math::exp((8)*math::log(math::abs(v742)));
    const Real v750 = math::exp(-0.28274430617039997*v743 + 0.01623316639567*v744 - 0.033650120313629989*v745 + 0.01178329782711*v746 - 0.001656194699504*v747 + 0.0001068275202678*v748 - 2.6312858092069998e-6*v749);
    kr_t0[kzs_t0 + 460u] = v750;
    const Real v752 = 3.7903999274394518e-18*math::exp((2.360852208681)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 461u] = v752;
    const Real v754 = math::exp(-5.7393287500000003*v743 + 1.56315498*v744 - 0.28770560000000001*v745 + 0.034825597700000002*v746 - 0.00263197617*v747 + 0.000111954395*v748 - 2.0391498499999999e-6*v749);
    kr_t0[kzs_t0 + 462u] = v754;
    const Real v756 = 5.9082438637265071e-70*math::exp((13.536555999999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 463u] = v756;
    const Real v868 = math::log(v867);
    kr_t0[kzs_t0 + 464u] = v868;
    const Real v760 = ((v742)*(v742)*(v742)*(v742)*(v742)*(v742)*(v742)*(v742)*(v742));
    const Real v907 = math::exp(-0.14210135215541481*v743 + 0.0084644553866299998*v744 - 0.0014327641212992001*v745 + 0.00020122502847909999*v746 + 8.6639632430900003e-5*v747 - 2.5850096802639999e-5*v748 + 2.4555011970391999e-6*v749 - 8.0683824611800006e-8*v760);
    kr_t0[kzs_t0 + 521u] = v907;
    const Real v908 = 3.3178155742407614e-14*math::exp((1.1394493358416311)*math::log(math::abs(T)))*v907;
    kr_t0[kzs_t0 + 522u] = v908;
    const Real v761 = math::exp(-0.02026044731984691*v743 - 0.002380861877349834*v744 - 0.00032126052131887958*v745 - 1.421502914054107e-5*v746 + 4.9891089202995129e-6*v747 + 5.7556141375757583e-7*v748 - 1.8567670397752609e-8*v749 - 3.0711352431965949e-9*v760);
    kr_t0[kzs_t0 + 479u] = v761;
    const Real v945 = v744*v729;
    kr_t0[kzs_t0 + 471u] = v945;
    const Real v946 = v746*v729;
    kr_t0[kzs_t0 + 485u] = v946;
    const Real v947 = v748*v729;
    kr_t0[kzs_t0 + 472u] = v947;
    const Real v949 = v749*v729;
    kr_t0[kzs_t0 + 473u] = v949;
    const Real v769 = math::exp(-21237.150000000001*v729);
    kr_t0[kzs_t0 + 499u] = v769;
    const Real v775 = math::exp((6)*math::log(math::abs(v770)));
    kr_t0[kzs_t0 + 501u] = v775;
    const Real v776 = v769*(3.5311931999999998e-13*((v770)*(v770)*(v770)*(v770)*(v770)*(v770)*(v770)) + 3.3735381999999997e-7*v770 - 1.4491368e-7*v771 + 3.4172804999999998e-8*v772 - 4.7813727999999997e-9*v773 + 3.9731542e-10*v774 - 1.8171411000000001e-11*v775 - 3.3232183000000002e-7);
    kr_t0[kzs_t0 + 502u] = v776;
    const Real v892 = 5.25e-11*math::exp(173900.0*v891 - 4430.0*v729);
    kr_t0[kzs_t0 + 504u] = v892;
    const Real v725 = math::exp(-6.1728395061728397e-5*T);
    kr_t0[kzs_t0 + 466u] = v725;
    const Real v730 = math::exp(-46707.0*v729);
    kr_t0[kzs_t0 + 467u] = v730;
    const Real v733 = math::exp(-102000.0*v729);
    kr_t0[kzs_t0 + 468u] = v733;
    const Real v944 = v742*v729;
    kr_t0[kzs_t0 + 470u] = v944;
    const Real v722 = math::sqrt(T);
    const Real v723 = 1.0/v722;
    kr_t0[kzs_t0 + 490u] = v723;
    const Real v758 = v741 <= 5500.0;
    kr_t0[kzs_t0 + 477u] = v758;
    const Real v902 = 0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0;
    kr_t0[kzs_t0 + 531u] = v902;
    const Real v903 = 1.0/v902;
    kr_t0[kzs_t0 + 518u] = v903;
    const Real v727 = math::exp((0.92800000000000005)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 519u] = v727;
    const Real v737 = ((T)*(T)*(T));
    kr_t0[kzs_t0 + 481u] = v737;
    const Real v738 = ((((T)*(T)))*(((T)*(T))));
    kr_t0[kzs_t0 + 482u] = v738;
    const Real v759 = math::exp((-0.72411256578268512)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 478u] = v759;
    const Real v777 = T >= 100.0 && T <= 30000.0;
    kr_t0[kzs_t0 + 503u] = v777;
    const Real v779 = T < 30;
    kr_t0[kzs_t0 + 487u] = v779;
    const Real v786 = math::exp((1.786*v771*v785 - 3.194*v782 - 0.2072*v784 - 18.199999999999999)*math::log(math::abs(10)));
    kr_t0[kzs_t0 + 489u] = v786;
    const Real v788 = T >= 10.0 && T <= 100000.0;
    kr_t0[kzs_t0 + 491u] = v788;
    const Real v893 = T > 200.0;
    kr_t0[kzs_t0 + 505u] = v893;
    const Real v904 = 1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 530u] = v904;
    const Real v906 = T <= 1160.0;
    kr_t0[kzs_t0 + 520u] = v906;
    const Real v928 = math::exp((-1.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 494u] = v928;
    const Real v930 = 2.5313028975878652e-10*math::exp((-0.59000000000000008)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 511u] = v930;
    const Real v931 = math::exp((-3.0/2.0)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 484u] = v931;
    const Real v936 = math::exp((-1.25)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 517u] = v936;
    const Real v942 = v772*v729;
    kr_t0[kzs_t0 + 497u] = v942;
    const Real v943 = v773*v729;
    kr_t0[kzs_t0 + 498u] = v943;
    const Real v952 = math::exp((-2.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 527u] = v952;
    const Real v888 = 1.0/v885;
    kr_t0[kzs_t0 + 538u] = v888;
    const Real v889 = -1.5e-32*v888 - 5.0000000000000004e-32*v791;
    kr_t0[kzs_t0 + 535u] = v889;
    const Real v905 = v903*v904;
    kr_t0[kzs_t0 + 536u] = v905;
    const Real v909 = jac_nuc_choice_31(T, v906, v908);
    kr_t0[kzs_t0 + 537u] = v909;
    const Real v787 = jac_nuc_choice_27(T, v779, v786);
    kr_t0[kzs_t0 + 533u] = v787;
    const Real v728 = 1.4000000000000001e-18*v727;
    kr_t0[kzs_t0 + 532u] = v728;
    const Real v894 = jac_nuc_choice_29(v892, v893);
    kr_t0[kzs_t0 + 534u] = v894;
    const Real v778 = jac_nuc_choice_26(v776, v777);
    kr_t0[kzs_t0 + 541u] = v778;
    const Real v732 = 35.5*math::exp((-2.2799999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 539u] = v732;
    const Real v735 = 8.7599999999999997e-10*math::exp((0.34999999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 540u] = v735;
    const Real v789 = 2*jac_nuc_choice_28(T, v722, v723, v788);
    kr_t0[kzs_t0 + 543u] = v789;
    const Real v762 = jac_nuc_choice_23(T, v758, v759, v761);
    kr_t0[kzs_t0 + 542u] = v762;
    const Real v740 = 2*jac_nuc_choice_22(T, v736, v737, v738, v739);
    kr_t0[kzs_t0 + 544u] = v740;
    const Real v724 = 7.1999999999999996e-8*v723;
    kr_t0[kzs_t0 + 545u] = v724;
    const Real v914 = 4.9999999999999996e-6*v723;
    kr_t0[kzs_t0 + 546u] = v914;
    const Real v1704 = M_LN10;
    const Real v1784 = math::powi<-5>(v1704);
    kr_t0[kzs_t0 + 593u] = v1784;
    const Real v1844 = math::log(0.001*T);
    const Real v1845 = ((v1844)*(v1844)*(v1844)*(v1844)*(v1844));
    const Real v1846 = v1784*v1845;
    const Real v1773 = math::exp((-4)*math::log(math::abs(v1704)));
    kr_t0[kzs_t0 + 594u] = v1773;
    const Real v1847 = ((((v1844)*(v1844)))*(((v1844)*(v1844))));
    const Real v1848 = v1773*v1847;
    const Real v1706 = math::exp((-2)*math::log(math::abs(v1704)));
    kr_t0[kzs_t0 + 596u] = v1706;
    const Real v1850 = ((v1844)*(v1844));
    kr_t0[kzs_t0 + 587u] = v1850;
    const Real v1851 = v1706*v1850;
    const Real v1705 = 1.0/v1704;
    kr_t0[kzs_t0 + 584u] = v1705;
    const Real v1738 = math::powi<-3>(v1704);
    kr_t0[kzs_t0 + 586u] = v1738;
    const Real v1849 = ((v1844)*(v1844)*(v1844));
    const Real v1852 = math::exp((2.0943374000000001*v1705*v1844 + 0.43693353000000001*v1738*v1849 - 0.033638326000000003*v1846 - 0.14913216000000001*v1848 - 0.77151435999999995*v1851 - 23.962112000000001)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 724u] = v1852;
    const Real v1839 = math::exp((25.0*v1705)*math::log(math::abs(T)));
    const Real v1840 = 1.0/v1839;
    const Real v1841 = 1.0/(2.3538526683701997e+17*v1840 + 10.0);
    const Real v1842 = 1.0/(1.6889118802245084e-48*v1839 + 10.0);
    const Real v1843 = math::exp((20000.0*v1841*v1842 - 200.0)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 552u] = v1843;
    const Real v1853 = v1843*v1852;
    kr_t0[kzs_t0 + 547u] = v1853;
    const Real v1862 = math::exp((16.666666666666664*v1705)*math::log(math::abs(T)));
    const Real v1863 = 1.0/v1862;
    const Real v1864 = 1.0/(785.77199422741614*v1863 + 10.0);
    const Real v1865 = 1.0/(5.0592917094448065e-34*v1862 + 10.0);
    const Real v1866 = math::exp((20000.0*v1864*v1865 - 200.0)*math::log(math::abs(10.0)));
    const Real v1871 = 1.1825091393820599e-21*v1866;
    kr_t0[kzs_t0 + 548u] = v1871;
    const Real v1867 = 1.002560385050777e-22*v1866;
    kr_t0[kzs_t0 + 549u] = v1867;
    const Real v1882 = math::exp((17.997580222853362*v1705)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 589u] = v1882;
    const Real v1883 = 1.0/v1882;
    kr_t0[kzs_t0 + 590u] = v1883;
    const Real v1884 = 1.0/(2973.7534532281375*v1883 + 10.0);
    kr_t0[kzs_t0 + 591u] = v1884;
    const Real v1885 = 1.0/(1.3368457736780898e-34*v1882 + 10.0);
    kr_t0[kzs_t0 + 592u] = v1885;
    const Real v1886 = 1.8623144679125181e-22*math::exp((20000.0*v1884*v1885 - 200.0)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 550u] = v1886;
    const Real v1888 = math::exp((8)*math::log(math::abs(v1844)));
    const Real v1791 = math::exp((-8)*math::log(math::abs(v1704)));
    kr_t0[kzs_t0 + 704u] = v1791;
    const Real v1889 = v1791*v1888;
    const Real v1793 = math::powi<-7>(v1704);
    kr_t0[kzs_t0 + 705u] = v1793;
    const Real v1890 = ((v1844)*(v1844)*(v1844)*(v1844)*(v1844)*(v1844)*(v1844));
    const Real v1891 = v1793*v1890;
    const Real v1892 = math::exp((6)*math::log(math::abs(v1844)));
    const Real v1796 = math::exp((-6)*math::log(math::abs(v1704)));
    kr_t0[kzs_t0 + 706u] = v1796;
    const Real v1893 = v1796*v1892;
    const Real v1856 = v1738*v1849;
    const Real v1855 = v1705*v1844;
    const Real v1894 = math::exp((983.67575999999997*v1846 + 734.71650999999997*v1848 + 96.743155000000002*v1851 + 16.815729999999999*v1855 + 343.1918*v1856 + 70.609154000000004*v1889 + 364.14445999999998*v1891 + 801.81246999999996*v1893 - 21.928795999999998)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 551u] = v1894;
    const Real v1899 = math::exp((-8.8077017000000009*v1846 - 4.7274035999999997*v1848 + 0.93310621999999999*v1851 + 1.6802758*v1855 + 4.0406627000000004*v1856 - 6.3701156000000001*v1889 + 6.4380698000000001*v1891 + 8.9167182999999994*v1893 - 22.921188999999998)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 553u] = v1899;
    const Real v1873 = math::exp((3.8479610000000002*v1846 + 20.159831000000001*v1848 + 58.145166000000003*v1851 + 37.383713*v1855 + 48.656103000000002*v1856 - 16.818342000000001)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 554u] = v1873;
    const Real v1876 = math::exp((3.5692468000000002*v1705*v1844 - 4.2519023000000002*v1846 - 21.328264000000001*v1848 - 11.33286*v1851 - 27.850082*v1856 - 24.311209000000002)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 555u] = v1876;
    const Real v1879 = math::exp((1.5538288*v1846 - 5.5108049000000001*v1848 - 3.7209846*v1851 + 4.6450521*v1855 + 5.9369081000000001*v1856 - 24.311209000000002)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 556u] = v1879;
    const Real v1857 = math::exp((0.19191374999999999*v1846 - 0.16596184*v1848 - 0.81520437999999995*v1851 + 2.1892372*v1855 + 0.29036281000000003*v1856 - 23.689236999999999)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 557u] = v1857;
    const Real v1869 = math::exp((0.32168730000000001*v1846 - 0.51002221000000003*v1848 + 0.015391166*v1851 + 1.5714710999999999*v1855 - 0.23619984999999999*v1856 - 22.089523)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 558u] = v1869;
    const Real v1914 = math::exp((5.0194035000000001*v1705*v1844 + 2.4714160999999999*v1773*v1847 + 5.4710749999999999*v1784*v1845 + 1.8161874*v1791*v1888 - 1.5738805*v1851 - 4.7155769000000003*v1856 - 2.2148338000000001*v1891 - 3.9467355999999998*v1893 - 20.584225)*math::log(math::abs(10.0)));
    const Real v1964 = v1914 >= 1.0e-99;
    kr_t0[kzs_t0 + 572u] = v1964;
    const Real v1908 = ((T)*(T)*(T));
    kr_t0[kzs_t0 + 672u] = v1908;
    const Real v1909 = 1.0/v1908;
    kr_t0[kzs_t0 + 634u] = v1909;
    const Real v1910 = math::exp(-2197000.0*v1909);
    const Real v1907 = 6.0142468035272636e-8*math::exp((2.1000000000000001)*math::log(math::abs(T))) + 1.0;
    const Real v1911 = v1910/v1907;
    const Real v1612 = 1.0/T;
    kr_t0[kzs_t0 + 585u] = v1612;
    const Real v1904 = math::exp(-11700.0*v1612);
    const Real v1905 = math::exp(-5860.0*v1612);
    const Real v1906 = math::exp(-510.0*v1612);
    const Real v1912 = 4.985670872372847e-33*math::exp((3.7599999999999998)*math::log(math::abs(T)))*v1911 + 1.6e-18*v1904 + 6.7e-19*v1905 + 3.0e-24*v1906;
    const Real v1924 = v1912 >= 1.0e-99;
    kr_t0[kzs_t0 + 571u] = v1924;
    const Real v1859 = T <= 10000.0;
    kr_t0[kzs_t0 + 559u] = v1859;
    const Real v1860 = T > 10.0;
    kr_t0[kzs_t0 + 560u] = v1860;
    const Real v1861 = v1859 && v1860;
    kr_t0[kzs_t0 + 561u] = v1861;
    const Real v1896 = T <= 500.0;
    kr_t0[kzs_t0 + 565u] = v1896;
    const Real v1897 = T > 100;
    kr_t0[kzs_t0 + 566u] = v1897;
    const Real v1898 = v1896 && v1897;
    kr_t0[kzs_t0 + 567u] = v1898;
    const Real v1915 = 0.00020000000000000001*T;
    const Real v1916 = v1915 - 6.0;
    const Real v1917 = v1916 >= 300.0;
    kr_t0[kzs_t0 + 570u] = v1917;
    const Real v1965 = 5.5313336794064847e-19/(0.0024787521766663585*math::exp(v1915) + 1.0) >= 1.0e-99;
    kr_t0[kzs_t0 + 573u] = v1965;
    const Real v1875 = T <= 100.0;
    kr_t0[kzs_t0 + 562u] = v1875;
    const Real v1878 = T <= 1000.0;
    kr_t0[kzs_t0 + 563u] = v1878;
    const Real v1881 = T <= 6000.0;
    kr_t0[kzs_t0 + 564u] = v1881;
    const Real v1901 = T > 500.0;
    kr_t0[kzs_t0 + 568u] = v1901;
    const Real v1913 = T < 2000.0;
    kr_t0[kzs_t0 + 569u] = v1913;
    const Real v1918 = math::exp(jac_nuc_choice_122(v1916, v1917));
    const Real v1919 = v1918 + 1.0;
    const Real v1920 = jac_nuc_choice_124(v1859, v1912, v1913, v1914, v1919);
    kr_t0[kzs_t0 + 574u] = v1920;
    const Real v2108 = v1706*v1612;
    kr_t0[kzs_t0 + 640u] = v2108;
    const Real v2127 = v1844*v2108;
    kr_t0[kzs_t0 + 576u] = v2127;
    const Real v2118 = v1738*v1612;
    const Real v2133 = v1850*v2118;
    kr_t0[kzs_t0 + 582u] = v2133;
    const Real v2116 = v1705*v1612;
    kr_t0[kzs_t0 + 575u] = v2116;
    const Real v2135 = 4.8910985889961177e-12*v1705*v1840*v1842*v1612/((v1840 + 4.2483542552915895e-17)*(v1840 + 4.2483542552915895e-17)) - 1.9444316593927493e-44*v1839*v1841*v2116/((1.6889118802245085e-49*v1839 + 1)*(1.6889118802245085e-49*v1839 + 1));
    kr_t0[kzs_t0 + 583u] = v2135;
    const Real v2136 = 976.7825399351309*v1705*v1863*v1865*v1612/((v1863 + 0.012726338013398102)*(v1863 + 0.012726338013398102)) - 3.8831498904253243e-30*v1862*v1864*v2116/((5.0592917094448061e-35*v1862 + 1)*(5.0592917094448061e-35*v1862 + 1));
    kr_t0[kzs_t0 + 588u] = v2136;
    const Real v2128 = v1791*v1890*v1612;
    kr_t0[kzs_t0 + 577u] = v2128;
    const Real v2129 = v1793*v1892*v1612;
    kr_t0[kzs_t0 + 578u] = v2129;
    const Real v2130 = v1796*v1845*v1612;
    kr_t0[kzs_t0 + 579u] = v2130;
    const Real v2131 = v1784*v1847*v1612;
    kr_t0[kzs_t0 + 580u] = v2131;
    const Real v2132 = v1773*v1849*v1612;
    kr_t0[kzs_t0 + 581u] = v2132;
    const Real v1652 = math::exp((-2)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 665u] = v1652;
    const Real v2134 = jac_nuc_choice_239(T, v1859, v1904, v1905, v1906, v1907, v1910, v1911, v1913, v1914, v1917, v1918, v1919, v1652, v2116, v2127, v2128, v2129, v2130, v2131, v2132, v2133);
    kr_t0[kzs_t0 + 703u] = v2134;
    const Real v1771 = T >= 10000.0;
    const Real v1772 = math::log(jac_nuc_choice_92(T, v1771));
    kr_t0[kzs_t0 + 595u] = v1772;
    const Real v1774 = ((((v1772)*(v1772)))*(((v1772)*(v1772))));
    kr_t0[kzs_t0 + 599u] = v1774;
    const Real v1792 = v1774*v1791;
    kr_t0[kzs_t0 + 604u] = v1792;
    const Real v1775 = ((v1772)*(v1772)*(v1772));
    kr_t0[kzs_t0 + 598u] = v1775;
    const Real v1794 = v1775*v1793;
    kr_t0[kzs_t0 + 605u] = v1794;
    const Real v1795 = v1774*v1793;
    kr_t0[kzs_t0 + 607u] = v1795;
    const Real v1776 = ((v1772)*(v1772));
    kr_t0[kzs_t0 + 597u] = v1776;
    const Real v1797 = v1776*v1796;
    kr_t0[kzs_t0 + 606u] = v1797;
    const Real v1798 = v1775*v1796;
    kr_t0[kzs_t0 + 608u] = v1798;
    const Real v1799 = v1774*v1796;
    kr_t0[kzs_t0 + 609u] = v1799;
    const Real v1801 = v1775*v1784;
    kr_t0[kzs_t0 + 610u] = v1801;
    const Real v1786 = v1774*v1784;
    kr_t0[kzs_t0 + 601u] = v1786;
    const Real v1788 = v1773*v1775;
    kr_t0[kzs_t0 + 602u] = v1788;
    const Real v1790 = v1738*v1776;
    kr_t0[kzs_t0 + 603u] = v1790;
    const Real v1783 = v1706*v1772;
    kr_t0[kzs_t0 + 600u] = v1783;
    const Real v1815 = 1.0000420000000001*v1705;
    kr_t0[kzs_t0 + 611u] = v1815;
    const Real v1695 = math::sqrt(M_PI);
    kr_t0[kzs_t0 + 678u] = v1695;
    const Real v1623 = math::sqrt(T);
    const Real v1812 = v1623*v1695;
    kr_t0[kzs_t0 + 612u] = v1812;
    const Real v1723 = math::log(T);
    kr_t0[kzs_t0 + 623u] = v1723;
    const Real v1724 = ((v1723)*(v1723));
    kr_t0[kzs_t0 + 616u] = v1724;
    const Real v1725 = v1706*v1724;
    kr_t0[kzs_t0 + 624u] = v1725;
    const Real v1726 = -4.8909149999999997*v1705*v1723 + 0.47490300000000002*v1725 - 133.82830000000001*v1612;
    const Real v1727 = math::exp((v1726 + 14.82123)*math::log(math::abs(10.0)));
    const Real v1728 = 1.0/v1727;
    kr_t0[kzs_t0 + 613u] = v1728;
    const Real v1730 = math::exp(-0.0022727272727272726*T);
    const Real v1731 = math::exp(-0.00054054054054054055*T);
    const Real v1732 = -2.0563129999999998*v1730 + 0.58640729999999996*v1731 + 0.82274429999999998;
    kr_t0[kzs_t0 + 614u] = v1732;
    const Real v1743 = math::exp((v1726 + 13.656822)*math::log(math::abs(10.0)));
    const Real v1744 = 1.0/v1743;
    kr_t0[kzs_t0 + 615u] = v1744;
    const Real v1739 = ((v1723)*(v1723)*(v1723))*v1738;
    const Real v1740 = 40870.379999999997*v1612 + 1.0;
    const Real v1741 = -69.700860000000006*v1705*math::log(v1740) + 4.6331670000000003*v1739;
    kr_t0[kzs_t0 + 619u] = v1741;
    const Real v1736 = v1705*v1723;
    kr_t0[kzs_t0 + 618u] = v1736;
    const Real v1737 = 16780.950000000001*v1612 + 1.0;
    kr_t0[kzs_t0 + 685u] = v1737;
    const Real v1742 = 19.734269999999999*v1705*math::log(v1737) + 37.886913*v1706*v1724 - 14.509090000000008*v1736 - v1741 - 307.31920000000002;
    kr_t0[kzs_t0 + 617u] = v1742;
    const Real v1750 = -2.4640089999999999*v1705*v1723 + 0.19859550000000001*v1725 + 743.05999999999995*v1612;
    const Real v1762 = math::exp((v1750 + 8.1313220000000008)*math::log(math::abs(10.0)));
    const Real v1763 = 1.0/v1762;
    kr_t0[kzs_t0 + 620u] = v1763;
    const Real v1754 = 2.9375070000000001*v1730 + 0.23588480000000001*v1731 + 0.75022860000000002;
    kr_t0[kzs_t0 + 621u] = v1754;
    const Real v1751 = math::exp((v1750 + 9.3055640000000004)*math::log(math::abs(10.0)));
    const Real v1752 = 1.0/v1751;
    kr_t0[kzs_t0 + 622u] = v1752;
    const Real v1759 = 27535.310000000001*v1612 + 1.0;
    const Real v1760 = -21.360939999999999*v1705*math::log(v1759) + 0.25820969999999999*v1739;
    kr_t0[kzs_t0 + 626u] = v1760;
    const Real v1758 = 14254.549999999999*v1612 + 1.0;
    kr_t0[kzs_t0 + 692u] = v1758;
    const Real v1761 = 70.138370000000009*v1705*v1723 + 11.28215*v1705*math::log(v1758) - 4.7035149999999994*v1725 - v1760 - 203.11568;
    kr_t0[kzs_t0 + 625u] = v1761;
    const Real v1714 = 52000.0*v1612;
    kr_t0[kzs_t0 + 671u] = v1714;
    const Real v1715 = math::exp(-v1714);
    kr_t0[kzs_t0 + 668u] = v1715;
    const Real v1713 = 1.0 - math::exp(-6000.0*v1612);
    kr_t0[kzs_t0 + 667u] = v1713;
    const Real v1716 = v1713*v1715;
    kr_t0[kzs_t0 + 670u] = v1716;
    const Real v1609 = math::sqrt(T);
    kr_t0[kzs_t0 + 666u] = v1609;
    const Real v1651 = 1.0/v1609;
    kr_t0[kzs_t0 + 632u] = v1651;
    const Real v1717 = 8.1250000000000003e-8*v1716*v1651;
    kr_t0[kzs_t0 + 628u] = v1717;
    const Real v1703 = math::log(0.0001*T);
    kr_t0[kzs_t0 + 641u] = v1703;
    const Real v1707 = math::exp((1.3*v1705*v1703 - 1.6200000000000001*v1706*((v1703)*(v1703)) - 4.8449999999999998)*math::log(math::abs(10.0)));
    kr_t0[kzs_t0 + 627u] = v1707;
    const Real v1702 = 1.1800000000000001e-10*math::exp(-69500.0*v1612);
    kr_t0[kzs_t0 + 629u] = v1702;
    const Real v1655 = math::exp(-12000.0/(T + 1200.0));
    kr_t0[kzs_t0 + 630u] = v1655;
    const Real v1653 = math::exp(-160000.0*v1652);
    kr_t0[kzs_t0 + 631u] = v1653;
    const Real v1642 = math::exp((-1.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 633u] = v1642;
    const Real v1693 = 0.0061910000000000003*math::exp((1.0461)*math::log(math::abs(T))) + 8.9711999999999997e-11*math::exp((3.0424000000000002)*math::log(math::abs(T))) + 3.2575999999999999e-14*math::exp((3.7740999999999998)*math::log(math::abs(T))) + 1.0;
    kr_t0[kzs_t0 + 664u] = v1693;
    const Real v1694 = 1.0/v1693;
    kr_t0[kzs_t0 + 635u] = v1694;
    const Real v2016 = math::log(v1702);
    kr_t0[kzs_t0 + 636u] = v2016;
    const Real v2117 = v1723*v2108;
    kr_t0[kzs_t0 + 690u] = v2117;
    const Real v2126 = v1754*(1710.9588792001557*v1652 + 5.6735903924031659*v2116 - 0.91456607567139814*v2117);
    kr_t0[kzs_t0 + 694u] = v2126;
    const Real v2123 = v1732*(11.261747970100974*v1705*v1612 - 308.15104860073512*v1652 - 2.1870091368363029*v2117);
    kr_t0[kzs_t0 + 687u] = v2123;
    const Real v1830 = ((((T)*(T)))*(((T)*(T))));
    kr_t0[kzs_t0 + 637u] = v1830;
    const Real v1620 = T <= 10;
    kr_t0[kzs_t0 + 645u] = v1620;
    const Real v1624 = jac_nuc_choice_85(v1620, v1623);
    kr_t0[kzs_t0 + 644u] = v1624;
    const Real v1625 = 0.0031622776601683794*v1624 + 1.0;
    const Real v1626 = 1.0/v1625;
    kr_t0[kzs_t0 + 638u] = v1626;
    const Real v2082 = jac_nuc_choice_230(v1620, v1652);
    kr_t0[kzs_t0 + 639u] = v2082;
    const Real v1993 = v1743*v1744;
    kr_t0[kzs_t0 + 686u] = v1993;
    const Real v1998 = v1727*v1728;
    kr_t0[kzs_t0 + 689u] = v1998;
    const Real v2002 = v1762*v1763;
    kr_t0[kzs_t0 + 693u] = v2002;
    const Real v2007 = v1751*v1752;
    kr_t0[kzs_t0 + 696u] = v2007;
    const Real v1632 = 6.3095734448019361e-5*jac_nuc_choice_87(T, v1620) + 1.0;
    kr_t0[kzs_t0 + 652u] = v1632;
    const Real v1633 = 1.0/v1632;
    kr_t0[kzs_t0 + 650u] = v1633;
    const Real v1634 = jac_nuc_choice_88(T, v1620);
    kr_t0[kzs_t0 + 651u] = v1634;
    const Real v1635 = v1633*v1634;
    kr_t0[kzs_t0 + 643u] = v1635;
    const Real v2085 = 1.0/v1623;
    kr_t0[kzs_t0 + 677u] = v2085;
    const Real v2086 = jac_nuc_choice_231(v1620, v2085);
    kr_t0[kzs_t0 + 642u] = v2086;
    const Real v2095 = v2086/((v1625)*(v1625));
    kr_t0[kzs_t0 + 646u] = v2095;
    const Real v1628 = jac_nuc_choice_86(v1620, v1612);
    kr_t0[kzs_t0 + 697u] = v1628;
    const Real v1683 = math::exp(-157809.10000000001*v1628);
    kr_t0[kzs_t0 + 673u] = v1683;
    const Real v1684 = v1624*v1683;
    kr_t0[kzs_t0 + 647u] = v1684;
    const Real v2110 = jac_nuc_choice_235(v1771)*jac_nuc_choice_236(v1771, v1612);
    kr_t0[kzs_t0 + 648u] = v2110;
    const Real v1643 = jac_nuc_choice_89(v1620, v1642);
    kr_t0[kzs_t0 + 649u] = v1643;
    const Real v2119 = v1724*v2118;
    kr_t0[kzs_t0 + 682u] = v2119;
    const Real v2120 = v1705*v1652;
    kr_t0[kzs_t0 + 684u] = v2120;
    const Real v2121 = v2120/v1740;
    kr_t0[kzs_t0 + 683u] = v2121;
    const Real v2124 = v2120/v1759;
    kr_t0[kzs_t0 + 691u] = v2124;
    const Real v1644 = 1.0 + 0.29999999999999999*math::exp(-94000.0*v1628);
    kr_t0[kzs_t0 + 679u] = v1644;
    const Real v1645 = math::exp(-470000.0*v1628);
    kr_t0[kzs_t0 + 680u] = v1645;
    const Real v1646 = 1.24e-13*v1644*v1645;
    kr_t0[kzs_t0 + 676u] = v1646;
    const Real v1666 = jac_nuc_choice_91(T, v1620);
    const Real v1667 = math::exp(-55338.0*v1628);
    kr_t0[kzs_t0 + 698u] = v1667;
    const Real v1668 = v1666*v1667;
    kr_t0[kzs_t0 + 700u] = v1668;
    const Real v1673 = math::exp(-13179.0*v1628);
    kr_t0[kzs_t0 + 699u] = v1673;
    const Real v1674 = v1666*v1673;
    kr_t0[kzs_t0 + 701u] = v1674;
    const Real v1676 = math::exp(-631515.0*v1628);
    kr_t0[kzs_t0 + 663u] = v1676;
    const Real v1677 = 4.9500000000000001e-22*v1676;
    kr_t0[kzs_t0 + 653u] = v1677;
    const Real v1680 = math::exp(-285335.40000000002*v1628);
    kr_t0[kzs_t0 + 660u] = v1680;
    const Real v1681 = 9.3799999999999993e-22*v1680;
    kr_t0[kzs_t0 + 654u] = v1681;
    const Real v1696 = 1.0/v1695;
    kr_t0[kzs_t0 + 655u] = v1696;
    const Real v2029 = v1773*v1776;
    kr_t0[kzs_t0 + 708u] = v2029;
    const Real v2032 = v1776*v1784;
    kr_t0[kzs_t0 + 709u] = v2032;
    const Real v1688 = math::exp((-0.25)*math::log(math::abs(T)));
    const Real v1689 = 2.0000000000000002e-31*v1651 + 6.0000000000000001e-32*v1688;
    kr_t0[kzs_t0 + 656u] = v1689;
    const Real v1691 = 2.5000000000000002e-32*v1651 + 7.5000000000000001e-33*v1688;
    kr_t0[kzs_t0 + 657u] = v1691;
    const Real v1692 = 1.3500000000000001e-9*math::exp((0.098492999999999997)*math::log(math::abs(T))) + 4.4350199999999998e-10*math::exp((0.55610000000000004)*math::log(math::abs(T))) + 3.7408500000000004e-16*math::exp((2.1825999999999999)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 658u] = v1692;
    const Real v1629 = math::exp(-118348.0*v1628);
    kr_t0[kzs_t0 + 659u] = v1629;
    const Real v2111 = v1772*v2110;
    kr_t0[kzs_t0 + 710u] = v2111;
    const Real v1662 = math::exp(-473638.0*v1628);
    kr_t0[kzs_t0 + 675u] = v1662;
    const Real v1663 = v1662*jac_nuc_choice_90(T, v1620);
    kr_t0[kzs_t0 + 674u] = v1663;
    const Real v1613 = math::exp(-102000.0*v1612);
    kr_t0[kzs_t0 + 661u] = v1613;
    const Real v2122 = 0.0046734386363636356*v1730 - 0.00031697691891891889*v1731;
    kr_t0[kzs_t0 + 688u] = v2122;
    const Real v2125 = -0.0066761522727272725*v1730 - 0.0001275052972972973*v1731;
    kr_t0[kzs_t0 + 695u] = v2125;
    const Real v1617 = 2.73*z + 2.73;
    const Real v1805 = T >= v1617;
    kr_t0[kzs_t0 + 707u] = v1805;
    const Real v2092 = math::exp((-2.5)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 669u] = v2092;
    const Real v2100 = math::exp((-1.25)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 681u] = v2100;
    const Real v1618 = 5.6500000000000001e-36*((((z + 1.0)*(z + 1.0)))*(((z + 1.0)*(z + 1.0))));
    kr_t0[kzs_t0 + 662u] = v1618;
    const Real v1834 = T < 2.0;
    kr_t0[kzs_t0 + 702u] = v1834;
    const Real v1636 = v1624*v1635;
    const Real v1637 = 3.4635323838154264e-26*v1636;
    kr_t0[kzs_t0 + 711u] = v1637;
    const Real v1639 = 1.3854129535261706e-25*v1636;
    kr_t0[kzs_t0 + 712u] = v1639;
    const Real v1664 = 5.5399999999999998e-17*v1663;
    kr_t0[kzs_t0 + 713u] = v1664;
    const Real v1685 = 1.2700000000000001e-21*v1684;
    kr_t0[kzs_t0 + 714u] = v1685;
    const Real v1610 = 2.1299999999999999e-27*v1609;
    kr_t0[kzs_t0 + 715u] = v1610;
    const Real v1615 = 3.1438547368704001e-21*math::exp((0.34999999999999998)*math::log(math::abs(T)));
    kr_t0[kzs_t0 + 716u] = v1615;
    const Real v1621 = 1.5499999999999999e-26*jac_nuc_choice_84(T, v1620);
    kr_t0[kzs_t0 + 717u] = v1621;
    const Real v1630 = 7.4999999999999996e-19*v1629;
    kr_t0[kzs_t0 + 718u] = v1630;
    const Real v1669 = 5.0099999999999997e-27*v1668;
    kr_t0[kzs_t0 + 721u] = v1669;
    const Real v1675 = 9.1000000000000001e-27*v1674;
    kr_t0[kzs_t0 + 722u] = v1675;
    const Real v1619 = v1618*(T - v1617);
    kr_t0[kzs_t0 + 719u] = v1619;
    const Real v1647 = v1643*v1646;
    kr_t0[kzs_t0 + 720u] = v1647;
    const Real v2023 = jac_nuc_choice_199(v1873, v1875, v1876, v1878, v1879, v1881, v1886);
    kr_t0[kzs_t0 + 723u] = v2023;
    const Real v1972 = v1624*v1626;
    kr_t0[kzs_t0 + 725u] = v1972;
    const Real v1985 = jac_nuc_choice_193(v1894, v1898, v1899, v1901);
    kr_t0[kzs_t0 + 726u] = v1985;
    const Real v1990 = jac_nuc_choice_196(v1861, v1869, v1871);
    kr_t0[kzs_t0 + 727u] = v1990;
    const Real v2078 = jac_nuc_choice_229(v1857, v1861, v1867);
    kr_t0[kzs_t0 + 728u] = v2078;
    volatile unsigned kzl_t0_v = 0u;
    const unsigned kzl_t0 = kzl_t0_v;
    __builtin_assume(kzl_t0 < 8u);
    out[matrix_index(13, 3)] = 0;
    out[matrix_index(9, 3)] = 0;
    out[matrix_index(9, 7)] = 0;
    out[matrix_index(12, 8)] = 0;
    out[matrix_index(5, 13)] = 0;
    out[matrix_index(11, 13)] = 0;
    out[matrix_index(12, 7)] = 0;
    out[matrix_index(11, 2)] = 0;
    out[matrix_index(3, 4)] = 0;
    out[matrix_index(7, 6)] = 0;
    out[matrix_index(0, 10)] = 0;
    out[matrix_index(9, 8)] = 0;
    out[matrix_index(9, 11)] = 0;
    out[matrix_index(12, 10)] = 0;
    out[matrix_index(7, 9)] = 0;
    out[matrix_index(10, 11)] = 0;
    out[matrix_index(3, 12)] = 0;
    out[matrix_index(4, 3)] = 0;
    out[matrix_index(10, 6)] = 0;
    out[matrix_index(13, 10)] = 0;
    out[matrix_index(11, 5)] = 0;
    out[matrix_index(10, 13)] = 0;
    out[matrix_index(12, 9)] = 0;
    out[matrix_index(11, 6)] = 0;
    out[matrix_index(6, 5)] = 0;
    out[matrix_index(2, 11)] = 0;
    out[matrix_index(13, 5)] = 0;
    out[matrix_index(13, 4)] = 0;
    out[matrix_index(4, 11)] = 0;
    out[matrix_index(13, 11)] = 0;
    out[matrix_index(6, 13)] = 0;
    out[matrix_index(6, 11)] = 0;
    out[matrix_index(11, 8)] = 0;
    out[matrix_index(4, 13)] = 0;
    out[matrix_index(6, 7)] = 0;
    out[matrix_index(12, 5)] = 0;
    out[matrix_index(5, 11)] = 0;
    out[matrix_index(12, 3)] = 0;
    out[matrix_index(11, 10)] = 0;
    out[matrix_index(9, 13)] = 0;
    out[matrix_index(1, 6)] = 6.0e-10*xn[2];
    out[matrix_index(3, 10)] = 0;
    out[matrix_index(3, 13)] = 0;
    out[matrix_index(1, 9)] = 6.3999999999999996e-10*xn[2];
    out[matrix_index(13, 8)] = 0;
    out[matrix_index(7, 8)] = 0;
    out[matrix_index(7, 11)] = 0;
    out[matrix_index(6, 9)] = 0;
    out[matrix_index(9, 10)] = 0;
    out[matrix_index(13, 6)] = 0;
    out[matrix_index(6, 4)] = 0;
    out[matrix_index(7, 10)] = 0;
    out[matrix_index(9, 12)] = 0;
    out[matrix_index(12, 6)] = 0;
    out[matrix_index(10, 9)] = 6.3999999999999996e-10*xn[2];
    out[matrix_index(6, 10)] = 0;
    out[matrix_index(3, 11)] = 0;
    out[matrix_index(10, 12)] = 0;
    out[matrix_index(10, 0)] = 0;
    out[matrix_index(7, 13)] = 0;
    out[matrix_index(11, 9)] = 0;
    out[matrix_index(8, 12)] = 0;
    out[matrix_index(4, 12)] = 0;
    out[matrix_index(12, 4)] = 0;
    out[matrix_index(8, 7)] = 0;
    out[matrix_index(6, 12)] = 0;
    out[matrix_index(3, 9)] = 0;
    out[matrix_index(7, 12)] = 0;
    out[matrix_index(4, 9)] = 0;
    out[matrix_index(8, 11)] = 0;
    out[matrix_index(13, 9)] = 0;
    out[matrix_index(13, 7)] = 0;
    out[matrix_index(11, 7)] = 0;
    out[matrix_index(1, 11)] = 0;
    out[matrix_index(11, 3)] = 0;
    out[matrix_index(9, 6)] = 0;
    out[matrix_index(4, 6)] = 0;
    out[matrix_index(11, 1)] = 0;
    out[matrix_index(5, 12)] = 0;
    out[matrix_index(8, 13)] = 0;
    out[matrix_index(11, 4)] = 0;
    out[matrix_index(5, 6)] = 0;
    const Real v1054_g0 = kr_t0[kzl_t0 + 0u];
    out[matrix_index(4, 7)] = -xn[4]*v1054_g0;
    const Real v1002_g0 = kr_t0[kzl_t0 + 1u];
    out[matrix_index(3, 7)] = xn[2]*v1002_g0;
    const Real v1036_g0 = kr_t0[kzl_t0 + 2u];
    out[matrix_index(4, 0)] = -xn[4]*v1036_g0;
    const Real v1583_g0 = kr_t0[kzl_t0 + 3u];
    out[matrix_index(13, 2)] = xn[12]*v1583_g0;
    const Real v693_g0 = kr_t0[kzl_t0 + 4u];
    out[matrix_index(1, 12)] = xn[2]*v693_g0;
    const Real v1077_g0 = kr_t0[kzl_t0 + 5u];
    out[matrix_index(5, 9)] = xn[0]*v1077_g0;
    const Real v1390_g0 = kr_t0[kzl_t0 + 6u];
    out[matrix_index(9, 0)] = -xn[9]*v1390_g0;
    out[matrix_index(9, 9)] = -xn[0]*v1390_g0 - 6.3999999999999996e-10*xn[2];
    const Real v1535_g0 = kr_t0[kzl_t0 + 7u];
    out[matrix_index(12, 2)] = -xn[12]*v1535_g0;
    const Real v1187_g0 = kr_t0[kzl_t0 + 8u];
    out[matrix_index(7, 3)] = xn[5]*v1187_g0;
    const Real v654_g0 = kr_t0[kzl_t0 + 9u];
    out[matrix_index(1, 7)] = -xn[1]*v654_g0;
    const Real v534_g0 = kr_t0[kzl_t0 + 10u];
    out[matrix_index(0, 4)] = -xn[0]*v534_g0;
    const Real v1186_g0 = kr_t0[kzl_t0 + 11u];
    out[matrix_index(7, 2)] = -xn[7]*v1186_g0 - xn[7]*v1187_g0;
    const Real v1184_g0 = kr_t0[kzl_t0 + 12u];
    out[matrix_index(7, 7)] = -7.9674337148168363e-7*xn[1]*v1184_g0 - xn[2]*v1186_g0 - xn[2]*v1187_g0 - 9.8726896031426014e-7*xn[4]*v1184_g0;
    const Real v1185 = xn[7]*v1184_g0;
    out[matrix_index(7, 1)] = -7.9674337148168363e-7*v1185;
    out[matrix_index(7, 4)] = -9.8726896031426014e-7*v1185;
    const Real v1423_g0 = kr_t0[kzl_t0 + 13u];
    out[matrix_index(10, 7)] = xn[2]*v1423_g0;
    out[matrix_index(10, 3)] = xn[5]*v1423_g0;
    const Real v589_g0 = kr_t0[kzl_t0 + 14u];
    out[matrix_index(0, 7)] = xn[2]*v589_g0;
    const Real v1011_g0 = kr_t0[kzl_t0 + 15u];
    out[matrix_index(3, 5)] = -xn[3]*v1002_g0 - xn[3]*v1011_g0;
    const Real v1096_g0 = kr_t0[kzl_t0 + 16u];
    const Real v1103_g0 = kr_t0[kzl_t0 + 17u];
    out[matrix_index(5, 3)] = -xn[5]*v1096_g0 - xn[5]*v1103_g0;
    const Real v1082_g0 = kr_t0[kzl_t0 + 18u];
    out[matrix_index(5, 7)] = 7.9674337148168363e-7*xn[1]*v1082_g0 + xn[2]*v1096_g0 + 1.9745379206285203e-6*xn[4]*v1082_g0;
    const Real v1180_g0 = kr_t0[kzl_t0 + 19u];
    const Real v1183_g0 = kr_t0[kzl_t0 + 20u];
    out[matrix_index(7, 5)] = xn[0]*v1180_g0*v1183_g0 + xn[3]*v1187_g0;
    const Real v1181 = xn[5]*v1180_g0;
    const Real v1191 = xn[0]*v1181;
    const Real v1192 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1193 = 2.0860422997526066e-16*v1192;
    const Real v1194 = 3.4767371836380304e-16*v1192;
    const Real v1188 = math::exp((-1.5)*math::log(math::abs(T)))*xn[7];
    const Real v1189 = xn[2]*xn[7];
    const Real v1190_g0 = kr_t0[kzl_t0 + 21u];
    const Real v1182_g0 = kr_t0[kzl_t0 + 22u];
    out[matrix_index(7, 14)] = (2.6534040307116389e-10*math::exp((-1.1000000000000001)*math::log(math::abs(T)))*v1189 + 1.2635128643896626e-18*math::exp((-0.050000000000000044)*math::log(math::abs(T)))*v1191 + 3.9837168574084181e-7*xn[1]*v1188 + xn[3]*xn[5]*v1190_g0 + 4.9363448015713007e-7*xn[4]*v1188 - v1190_g0*v1189 - 1.4270531560759686e-22*v1191*v1182_g0)/(xn[0]*v1193 + xn[1]*v1193 + xn[10]*v1194 + xn[11]*v1193 + xn[12]*v1193 + xn[13]*v1193 + xn[2]*v1193 + xn[3]*v1193 + xn[4]*v1193 + xn[5]*v1193 + xn[6]*v1194 + xn[7]*v1193 + xn[8]*v1194 + xn[9]*v1194);
    out[matrix_index(7, 0)] = v1181*v1183_g0;
    const Real v886_g0 = kr_t0[kzl_t0 + 23u];
    out[matrix_index(2, 12)] = -xn[2]*v886_g0;
    const Real v1141_g0 = kr_t0[kzl_t0 + 24u];
    const Real v1160_g0 = kr_t0[kzl_t0 + 25u];
    out[matrix_index(6, 3)] = xn[1]*v1141_g0 - xn[6]*v1160_g0;
    const Real v537_g0 = kr_t0[kzl_t0 + 26u];
    out[matrix_index(0, 9)] = -xn[0]*v537_g0;
    const Real v547_g0 = kr_t0[kzl_t0 + 27u];
    const Real v549_g0 = kr_t0[kzl_t0 + 28u];
    out[matrix_index(0, 8)] = -xn[0]*v547_g0*v549_g0;
    const Real v1422_g0 = kr_t0[kzl_t0 + 29u];
    out[matrix_index(10, 1)] = -xn[10]*v1422_g0;
    const Real v658_g0 = kr_t0[kzl_t0 + 30u];
    out[matrix_index(1, 10)] = -xn[1]*v658_g0;
    const Real v1427_g0 = kr_t0[kzl_t0 + 31u];
    out[matrix_index(10, 10)] = -xn[1]*v1422_g0 - xn[2]*v1427_g0;
    out[matrix_index(10, 2)] = -xn[10]*v1427_g0 + 1.0e-25*xn[5] + xn[7]*v1423_g0 + 6.3999999999999996e-10*xn[9];
    const Real v1447 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1448 = 2.0860422997526066e-16*v1447;
    const Real v1449 = 3.4767371836380304e-16*v1447;
    const Real v1421_g0 = kr_t0[kzl_t0 + 32u];
    const Real v1424_g0 = kr_t0[kzl_t0 + 33u];
    const Real v1425_g0 = kr_t0[kzl_t0 + 34u];
    const Real v1426_g0 = kr_t0[kzl_t0 + 35u];
    const Real v1445_g0 = kr_t0[kzl_t0 + 36u];
    const Real v1420_g0 = kr_t0[kzl_t0 + 37u];
    const Real v1429_g0 = kr_t0[kzl_t0 + 38u];
    const Real v1446_g0 = kr_t0[kzl_t0 + 39u];
    const Real v1433_g0 = kr_t0[kzl_t0 + 40u];
    const Real v1436_g0 = kr_t0[kzl_t0 + 41u];
    const Real v1437_g0 = kr_t0[kzl_t0 + 42u];
    const Real v1438_g0 = kr_t0[kzl_t0 + 43u];
    const Real v1439_g0 = kr_t0[kzl_t0 + 44u];
    const Real v1440_g0 = kr_t0[kzl_t0 + 45u];
    const Real v1441_g0 = kr_t0[kzl_t0 + 46u];
    const Real v1442_g0 = kr_t0[kzl_t0 + 47u];
    const Real v1443_g0 = kr_t0[kzl_t0 + 48u];
    const Real v1428_g0 = kr_t0[kzl_t0 + 49u];
    out[matrix_index(10, 14)] = (-4.5700000000000003e-7*xn[1]*xn[10]*v1421_g0*v1424_g0 - xn[10]*xn[2]*jac_nuc_choice_74(T, v1424_g0, v1425_g0, v1426_g0) - xn[2]*xn[7]*v1445_g0 - xn[3]*xn[5]*v1445_g0 + xn[4]*xn[8]*(8.4600000000000008e-10*v1420_g0*v1429_g0 - 2.7400000000000004e-10*v1446_g0) + xn[5]*xn[8]*jac_nuc_choice_75(v1420_g0, v1433_g0, v1436_g0, v1437_g0, v1438_g0, v1439_g0, v1440_g0, v1441_g0, v1442_g0, v1443_g0, v1446_g0, v1424_g0, v1428_g0, v1429_g0))/(xn[0]*v1448 + xn[1]*v1448 + xn[10]*v1449 + xn[11]*v1448 + xn[12]*v1448 + xn[13]*v1448 + xn[2]*v1448 + xn[3]*v1448 + xn[4]*v1448 + xn[5]*v1448 + xn[6]*v1449 + xn[7]*v1448 + xn[8]*v1449 + xn[9]*v1449);
    const Real v1444_g0 = kr_t0[kzl_t0 + 50u];
    out[matrix_index(10, 5)] = 1.0e-25*xn[2] + xn[3]*v1423_g0 + xn[8]*v1444_g0;
    const Real v1435_g0 = kr_t0[kzl_t0 + 51u];
    out[matrix_index(10, 8)] = xn[4]*v1435_g0 + xn[5]*v1444_g0;
    out[matrix_index(10, 4)] = xn[8]*v1435_g0;
    const Real v1397_g0 = kr_t0[kzl_t0 + 52u];
    const Real v1398 = xn[5]*v1397_g0;
    const Real v1399 = xn[4]*v1397_g0;
    const Real v1402 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1403 = 2.0860422997526066e-16*v1402;
    const Real v1404 = 3.4767371836380304e-16*v1402;
    const Real v1401_g0 = kr_t0[kzl_t0 + 53u];
    out[matrix_index(9, 14)] = (xn[1]*v1401_g0*v1398 + xn[2]*v1401_g0*v1399 + 3.5999999999999998e-8*xn[0]*xn[9]/math::exp((3.0/2.0)*math::log(math::abs(T))))/(xn[0]*v1403 + xn[1]*v1403 + xn[10]*v1404 + xn[11]*v1403 + xn[12]*v1403 + xn[13]*v1403 + xn[2]*v1403 + xn[3]*v1403 + xn[4]*v1403 + xn[5]*v1403 + xn[6]*v1404 + xn[7]*v1403 + xn[8]*v1404 + xn[9]*v1404);
    out[matrix_index(9, 1)] = v1398;
    out[matrix_index(9, 2)] = -6.3999999999999996e-10*xn[9] + v1399;
    out[matrix_index(9, 5)] = xn[1]*v1397_g0;
    out[matrix_index(9, 4)] = xn[2]*v1397_g0;
    const Real v982_g0 = kr_t0[kzl_t0 + 54u];
    const Real v984_g0 = kr_t0[kzl_t0 + 55u];
    out[matrix_index(3, 8)] = xn[0]*v982_g0*v984_g0;
    const Real v1039_g0 = kr_t0[kzl_t0 + 56u];
    out[matrix_index(4, 10)] = xn[1]*v1039_g0;
    const Real v1042_g0 = kr_t0[kzl_t0 + 57u];
    out[matrix_index(4, 1)] = xn[10]*v1039_g0 + xn[5]*v1042_g0;
    out[matrix_index(4, 5)] = xn[1]*v1042_g0;
    const Real v1052_g0 = kr_t0[kzl_t0 + 58u];
    const Real v1053 = xn[4]*v1052_g0;
    const Real v1058 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1059 = 2.0860422997526066e-16*v1058;
    const Real v1060 = 3.4767371836380304e-16*v1058;
    const Real v1040_g0 = kr_t0[kzl_t0 + 59u];
    const Real v1041_g0 = kr_t0[kzl_t0 + 60u];
    const Real v1043_g0 = kr_t0[kzl_t0 + 61u];
    const Real v1037_g0 = kr_t0[kzl_t0 + 62u];
    const Real v1047_g0 = kr_t0[kzl_t0 + 63u];
    const Real v1050_g0 = kr_t0[kzl_t0 + 64u];
    const Real v1049_g0 = kr_t0[kzl_t0 + 65u];
    const Real v1051_g0 = kr_t0[kzl_t0 + 66u];
    const Real v1057_g0 = kr_t0[kzl_t0 + 67u];
    const Real v1055_g0 = kr_t0[kzl_t0 + 68u];
    const Real v1038_g0 = kr_t0[kzl_t0 + 69u];
    out[matrix_index(4, 14)] = (1.9462772454491511e-10*math::exp((-1.75)*math::log(math::abs(T)))*xn[0]*xn[4] + 4.9363448015713007e-7*math::exp((-1.5)*math::log(math::abs(T)))*xn[4]*xn[7] + xn[1]*xn[5]*jac_nuc_choice_50(T, v1040_g0, v1041_g0) - xn[2]*xn[4]*jac_nuc_choice_51(T, v1041_g0, v1043_g0) - xn[2]*v1053*(5.1485802679346868*v1037_g0*math::exp((1.0)*math::log(math::abs(v1047_g0)))*v1050_g0 - 0.87659414490283338*v1037_g0*v1049_g0*v1051_g0 - 3.5068370966299316*v1057_g0) + xn[4]*xn[8]*(2.7400000000000004e-10*v1037_g0*v1047_g0*v1055_g0 - 8.4600000000000008e-10*v1057_g0) + 4.5700000000000003e-7*xn[1]*xn[10]*v1038_g0/((T)*(T)))/(xn[0]*v1059 + xn[1]*v1059 + xn[10]*v1060 + xn[11]*v1059 + xn[12]*v1059 + xn[13]*v1059 + xn[2]*v1059 + xn[3]*v1059 + xn[4]*v1059 + xn[5]*v1059 + xn[6]*v1060 + xn[7]*v1059 + xn[8]*v1060 + xn[9]*v1060);
    const Real v1036_g1 = kr_t0[kzl_t0 + 2u];
    const Real v1044_g0 = kr_t0[kzl_t0 + 70u];
    const Real v1054_g1 = kr_t0[kzl_t0 + 0u];
    const Real v1056_g0 = kr_t0[kzl_t0 + 71u];
    out[matrix_index(4, 4)] = -xn[0]*v1036_g1 - xn[2]*v1052_g0 - xn[2]*v1044_g0 - xn[7]*v1054_g1 + xn[8]*v1056_g0;
    out[matrix_index(4, 2)] = -xn[4]*v1044_g0 - v1053;
    out[matrix_index(4, 8)] = xn[4]*v1056_g0;
    const Real v1012_g0 = kr_t0[kzl_t0 + 72u];
    out[matrix_index(3, 6)] = -xn[3]*v1012_g0;
    const Real v997_g0 = kr_t0[kzl_t0 + 73u];
    const Real v1001_g0 = kr_t0[kzl_t0 + 74u];
    out[matrix_index(3, 1)] = -xn[3]*v997_g0 - xn[3]*v1001_g0;
    const Real v545_g0 = kr_t0[kzl_t0 + 75u];
    const Real v542_g0 = kr_t0[kzl_t0 + 76u];
    const Real v589_g1 = kr_t0[kzl_t0 + 14u];
    out[matrix_index(0, 5)] = -xn[0]*v545_g0*v542_g0 + xn[3]*v589_g1;
    const Real v662_g0 = kr_t0[kzl_t0 + 77u];
    out[matrix_index(1, 13)] = -xn[1]*v662_g0;
    const Real v1140_g0 = kr_t0[kzl_t0 + 78u];
    const Real v1160_g1 = kr_t0[kzl_t0 + 25u];
    out[matrix_index(6, 6)] = -xn[0]*v1140_g0 - 6.0e-10*xn[2] - xn[3]*v1160_g1;
    out[matrix_index(6, 0)] = -xn[6]*v1140_g0;
    const Real v1162 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1163 = 2.0860422997526066e-16*v1162;
    const Real v1164 = 3.4767371836380304e-16*v1162;
    const Real v1136_g0 = kr_t0[kzl_t0 + 79u];
    const Real v1137_g0 = kr_t0[kzl_t0 + 80u];
    const Real v1138_g0 = kr_t0[kzl_t0 + 81u];
    const Real v1139_g0 = kr_t0[kzl_t0 + 82u];
    const Real v1146_g0 = kr_t0[kzl_t0 + 83u];
    const Real v1148_g0 = kr_t0[kzl_t0 + 84u];
    const Real v1150_g0 = kr_t0[kzl_t0 + 85u];
    const Real v1152_g0 = kr_t0[kzl_t0 + 86u];
    const Real v1161_g0 = kr_t0[kzl_t0 + 87u];
    const Real v1142_g0 = kr_t0[kzl_t0 + 88u];
    const Real v1143_g0 = kr_t0[kzl_t0 + 89u];
    const Real v1144_g0 = kr_t0[kzl_t0 + 90u];
    const Real v1145_g0 = kr_t0[kzl_t0 + 91u];
    const Real v1147_g0 = kr_t0[kzl_t0 + 92u];
    const Real v1153_g0 = kr_t0[kzl_t0 + 93u];
    const Real v1154_g0 = kr_t0[kzl_t0 + 94u];
    const Real v1155_g0 = kr_t0[kzl_t0 + 95u];
    const Real v1156_g0 = kr_t0[kzl_t0 + 96u];
    const Real v1157_g0 = kr_t0[kzl_t0 + 97u];
    const Real v1158_g0 = kr_t0[kzl_t0 + 98u];
    out[matrix_index(6, 14)] = (-4.0000000000000002e-9*math::exp((-1.3999999999999999)*math::log(math::abs(T)))*xn[1]*xn[3] - xn[0]*xn[6]*jac_nuc_choice_63(T, v1136_g0, v1137_g0, v1138_g0, v1139_g0) + xn[1]*xn[2]*jac_nuc_choice_64(T, v1146_g0, v1148_g0, v1150_g0, v1152_g0, v1161_g0, v1142_g0, v1143_g0, v1144_g0, v1145_g0) + xn[1]*xn[8]*jac_nuc_choice_65(v1136_g0, v1147_g0, v1152_g0, v1153_g0, v1154_g0, v1155_g0, v1156_g0, v1157_g0, v1158_g0, v1161_g0, v1143_g0) + 2.4999999999999998e-6*xn[3]*xn[6]/math::exp((3.0/2.0)*math::log(math::abs(T))))/(xn[0]*v1163 + xn[1]*v1163 + xn[10]*v1164 + xn[11]*v1163 + xn[12]*v1163 + xn[13]*v1163 + xn[2]*v1163 + xn[3]*v1163 + xn[4]*v1163 + xn[5]*v1163 + xn[6]*v1164 + xn[7]*v1163 + xn[8]*v1164 + xn[9]*v1164);
    const Real v1151_g0 = kr_t0[kzl_t0 + 99u];
    const Real v1141_g1 = kr_t0[kzl_t0 + 24u];
    const Real v1159_g0 = kr_t0[kzl_t0 + 100u];
    out[matrix_index(6, 1)] = xn[2]*v1151_g0 + xn[3]*v1141_g1 + xn[8]*v1159_g0;
    out[matrix_index(6, 8)] = xn[1]*v1159_g0;
    out[matrix_index(6, 2)] = xn[1]*v1151_g0 - 6.0e-10*xn[6];
    const Real v1078_g0 = kr_t0[kzl_t0 + 101u];
    const Real v1079 = xn[5]*v1078_g0;
    const Real v1076_g0 = kr_t0[kzl_t0 + 102u];
    const Real v1077_g1 = kr_t0[kzl_t0 + 5u];
    const Real v1081_g0 = kr_t0[kzl_t0 + 103u];
    out[matrix_index(5, 0)] = xn[4]*v1076_g0 + xn[9]*v1077_g1 - v1079*v1081_g0;
    const Real v1102_g0 = kr_t0[kzl_t0 + 104u];
    const Real v1082_g1 = kr_t0[kzl_t0 + 18u];
    out[matrix_index(5, 4)] = xn[0]*v1076_g0 + xn[2]*v1102_g0 + 1.9745379206285203e-6*xn[7]*v1082_g1;
    const Real v1100_g0 = kr_t0[kzl_t0 + 105u];
    const Real v1096_g1 = kr_t0[kzl_t0 + 16u];
    out[matrix_index(5, 2)] = xn[10]*v1100_g0 + xn[4]*v1102_g0 - 1.0e-25*xn[5] + xn[7]*v1096_g1;
    out[matrix_index(5, 10)] = xn[2]*v1100_g0;
    const Real v1094_g0 = kr_t0[kzl_t0 + 106u];
    const Real v1095 = xn[5]*v1094_g0;
    const Real v1118 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1119 = 2.0860422997526066e-16*v1118;
    const Real v1120 = 3.4767371836380304e-16*v1118;
    const Real v1115 = math::exp((-1.5)*math::log(math::abs(T)))*xn[7];
    const Real v1080_g0 = kr_t0[kzl_t0 + 107u];
    const Real v1084_g0 = kr_t0[kzl_t0 + 108u];
    const Real v1085_g0 = kr_t0[kzl_t0 + 109u];
    const Real v1089_g0 = kr_t0[kzl_t0 + 110u];
    const Real v1092_g0 = kr_t0[kzl_t0 + 111u];
    const Real v1083_g0 = kr_t0[kzl_t0 + 112u];
    const Real v1091_g0 = kr_t0[kzl_t0 + 113u];
    const Real v1093_g0 = kr_t0[kzl_t0 + 114u];
    const Real v1117_g0 = kr_t0[kzl_t0 + 115u];
    const Real v1097_g0 = kr_t0[kzl_t0 + 116u];
    const Real v1098_g0 = kr_t0[kzl_t0 + 117u];
    const Real v1099_g0 = kr_t0[kzl_t0 + 118u];
    const Real v1101_g0 = kr_t0[kzl_t0 + 119u];
    const Real v1116_g0 = kr_t0[kzl_t0 + 120u];
    const Real v1087_g0 = kr_t0[kzl_t0 + 121u];
    const Real v1104_g0 = kr_t0[kzl_t0 + 122u];
    const Real v1105_g0 = kr_t0[kzl_t0 + 123u];
    const Real v1106_g0 = kr_t0[kzl_t0 + 124u];
    const Real v1107_g0 = kr_t0[kzl_t0 + 125u];
    const Real v1108_g0 = kr_t0[kzl_t0 + 126u];
    const Real v1109_g0 = kr_t0[kzl_t0 + 127u];
    const Real v1110_g0 = kr_t0[kzl_t0 + 128u];
    const Real v1111_g0 = kr_t0[kzl_t0 + 129u];
    const Real v1112_g0 = kr_t0[kzl_t0 + 130u];
    const Real v1113_g0 = kr_t0[kzl_t0 + 131u];
    out[matrix_index(5, 14)] = (-1.9462772454491511e-10*math::exp((-1.75)*math::log(math::abs(T)))*xn[0]*xn[4] + 2.6534040307116389e-10*math::exp((-1.1000000000000001)*math::log(math::abs(T)))*xn[3]*xn[5] - 1.2635128643896626e-18*math::exp((-0.050000000000000044)*math::log(math::abs(T)))*xn[0]*v1079 + 1.4270531560759686e-22*xn[0]*xn[5]*v1078_g0*v1080_g0 - xn[1]*xn[5]*jac_nuc_choice_56(T, v1084_g0, v1085_g0) - xn[1]*v1095*(5.1485802679346868*math::exp((1.0)*math::log(math::abs(v1089_g0)))*v1092_g0*v1083_g0 - 0.87659414490283338*v1091_g0*v1093_g0*v1083_g0 - 3.5068370966299316*v1117_g0) - 3.9837168574084181e-7*xn[1]*v1115 + xn[10]*xn[2]*jac_nuc_choice_57(T, v1097_g0, v1098_g0, v1099_g0) + xn[2]*xn[4]*jac_nuc_choice_58(T, v1101_g0, v1085_g0) + 2.5313028975878652e-10*xn[2]*xn[7]*v1116_g0 - 2.5313028975878652e-10*xn[3]*xn[5]*v1116_g0 - 9.8726896031426014e-7*xn[4]*v1115 - xn[5]*xn[8]*jac_nuc_choice_59(v1087_g0, v1089_g0, v1097_g0, v1104_g0, v1105_g0, v1106_g0, v1107_g0, v1108_g0, v1109_g0, v1110_g0, v1111_g0, v1112_g0, v1113_g0, v1117_g0, v1083_g0) - 3.5999999999999998e-8*xn[0]*xn[9]/math::exp((3.0/2.0)*math::log(math::abs(T))))/(xn[0]*v1119 + xn[1]*v1119 + xn[10]*v1120 + xn[11]*v1119 + xn[12]*v1119 + xn[13]*v1119 + xn[2]*v1119 + xn[3]*v1119 + xn[4]*v1119 + xn[5]*v1119 + xn[6]*v1120 + xn[7]*v1119 + xn[8]*v1120 + xn[9]*v1120);
    const Real v1086_g0 = kr_t0[kzl_t0 + 132u];
    const Real v1103_g1 = kr_t0[kzl_t0 + 17u];
    const Real v1114_g0 = kr_t0[kzl_t0 + 133u];
    out[matrix_index(5, 5)] = -xn[0]*v1078_g0*v1081_g0 - xn[1]*v1086_g0 - xn[1]*v1094_g0 - 1.0e-25*xn[2] - xn[3]*v1096_g1 - xn[3]*v1103_g1 - xn[8]*v1114_g0;
    out[matrix_index(5, 1)] = -xn[5]*v1086_g0 + 7.9674337148168363e-7*xn[7]*v1082_g1 - v1095;
    out[matrix_index(5, 8)] = -xn[5]*v1114_g0;
    const Real v791_g0 = kr_t0[kzl_t0 + 134u];
    const Real v884_g0 = kr_t0[kzl_t0 + 135u];
    const Real v887_g0 = kr_t0[kzl_t0 + 136u];
    out[matrix_index(2, 7)] = 7.9674337148168363e-7*xn[1]*v791_g0 - xn[2]*v884_g0 - xn[2]*v887_g0;
    const Real v1567_g0 = kr_t0[kzl_t0 + 137u];
    const Real v1583_g1 = kr_t0[kzl_t0 + 3u];
    out[matrix_index(13, 12)] = xn[0]*v1567_g0 + xn[2]*v1583_g1;
    const Real v1575_g0 = kr_t0[kzl_t0 + 138u];
    const Real v1576 = xn[13]*v1575_g0;
    const Real v1577_g0 = kr_t0[kzl_t0 + 139u];
    const Real v1578 = v1576*v1577_g0;
    const Real v1586 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1587 = 2.0860422997526066e-16*v1586;
    const Real v1588 = 3.4767371836380304e-16*v1586;
    const Real v1564_g0 = kr_t0[kzl_t0 + 140u];
    const Real v1584_g0 = kr_t0[kzl_t0 + 141u];
    const Real v1585_g0 = kr_t0[kzl_t0 + 142u];
    const Real v1565_g0 = kr_t0[kzl_t0 + 143u];
    const Real v1566_g0 = kr_t0[kzl_t0 + 144u];
    const Real v1572_g0 = kr_t0[kzl_t0 + 145u];
    const Real v1573_g0 = kr_t0[kzl_t0 + 146u];
    const Real v1574_g0 = kr_t0[kzl_t0 + 147u];
    const Real v1568_g0 = kr_t0[kzl_t0 + 148u];
    const Real v1569_g0 = kr_t0[kzl_t0 + 149u];
    const Real v1570_g0 = kr_t0[kzl_t0 + 150u];
    const Real v1571_g0 = kr_t0[kzl_t0 + 151u];
    const Real v1580_g0 = kr_t0[kzl_t0 + 152u];
    const Real v1581_g0 = kr_t0[kzl_t0 + 153u];
    const Real v1579_g0 = kr_t0[kzl_t0 + 154u];
    out[matrix_index(13, 14)] = (-1.0409203801861816e-115*math::exp((22.915965629999999)*math::log(math::abs(T)))*xn[0]*v1576 + xn[0]*xn[12]*jac_nuc_choice_82(T, v1564_g0, v1584_g0, v1585_g0, v1565_g0, v1566_g0) - xn[0]*v1578*(-0.030054336600000002*v1572_g0*v1565_g0 + 0.001447065312*v1573_g0*v1565_g0 - 2.9193291280000001e-5*v1574_g0*v1565_g0 - 21.506460400000002*v1565_g0*v1568_g0 + 9.1741162500000009*v1565_g0*v1569_g0 - 2.2740475600000001*v1565_g0*v1570_g0 + 0.33976956150000004*v1565_g0*v1571_g0) - xn[1]*xn[13]*jac_nuc_choice_83(T, v1580_g0, v1581_g0) + 7.2084342424042629e-17*xn[12]*xn[2]*v1579_g0)/(xn[0]*v1587 + xn[1]*v1587 + xn[10]*v1588 + xn[11]*v1587 + xn[12]*v1587 + xn[13]*v1587 + xn[2]*v1587 + xn[3]*v1587 + xn[4]*v1587 + xn[5]*v1587 + xn[6]*v1588 + xn[7]*v1587 + xn[8]*v1588 + xn[9]*v1588);
    const Real v1582_g0 = kr_t0[kzl_t0 + 155u];
    out[matrix_index(13, 13)] = -xn[0]*v1575_g0*v1577_g0 - xn[1]*v1582_g0;
    out[matrix_index(13, 0)] = xn[12]*v1567_g0 - v1578;
    out[matrix_index(13, 1)] = -xn[13]*v1582_g0;
    const Real v551_g0 = kr_t0[kzl_t0 + 156u];
    const Real v600 = xn[0]*v551_g0;
    const Real v553_g0 = kr_t0[kzl_t0 + 157u];
    const Real v556_g0 = kr_t0[kzl_t0 + 158u];
    out[matrix_index(0, 11)] = -v553_g0*v556_g0*v600;
    const Real v655_g0 = kr_t0[kzl_t0 + 159u];
    const Real v669_g0 = kr_t0[kzl_t0 + 160u];
    out[matrix_index(1, 3)] = -xn[1]*v655_g0 - xn[1]*v669_g0;
    const Real v561_g0 = kr_t0[kzl_t0 + 161u];
    out[matrix_index(0, 6)] = -xn[0]*v561_g0;
    const Real v765_g0 = kr_t0[kzl_t0 + 162u];
    out[matrix_index(2, 13)] = xn[1]*v765_g0;
    const Real v1534_g0 = kr_t0[kzl_t0 + 163u];
    out[matrix_index(12, 1)] = xn[13]*v1534_g0;
    const Real v1508_g0 = kr_t0[kzl_t0 + 164u];
    const Real v1509 = xn[11]*v1508_g0;
    const Real v1506_g0 = kr_t0[kzl_t0 + 165u];
    const Real v1510 = v1506_g0*v1509;
    const Real v1536 = xn[0]*v1506_g0;
    const Real v1544 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1545 = 2.0860422997526066e-16*v1544;
    const Real v1546 = 3.4767371836380304e-16*v1544;
    const Real v1537 = xn[0]*xn[12];
    const Real v1526_g0 = kr_t0[kzl_t0 + 166u];
    const Real v1529_g0 = kr_t0[kzl_t0 + 167u];
    const Real v1527_g0 = kr_t0[kzl_t0 + 168u];
    const Real v1515_g0 = kr_t0[kzl_t0 + 169u];
    const Real v1519_g0 = kr_t0[kzl_t0 + 170u];
    const Real v1521_g0 = kr_t0[kzl_t0 + 171u];
    const Real v1523_g0 = kr_t0[kzl_t0 + 172u];
    const Real v1540_g0 = kr_t0[kzl_t0 + 173u];
    const Real v1541_g0 = kr_t0[kzl_t0 + 174u];
    const Real v1542_g0 = kr_t0[kzl_t0 + 175u];
    const Real v1543_g0 = kr_t0[kzl_t0 + 176u];
    const Real v1505_g0 = kr_t0[kzl_t0 + 177u];
    const Real v1532_g0 = kr_t0[kzl_t0 + 178u];
    const Real v1533_g0 = kr_t0[kzl_t0 + 179u];
    const Real v1507_g0 = kr_t0[kzl_t0 + 180u];
    const Real v1531_g0 = kr_t0[kzl_t0 + 181u];
    const Real v1530_g0 = kr_t0[kzl_t0 + 182u];
    const Real v1514_g0 = kr_t0[kzl_t0 + 183u];
    const Real v1516_g0 = kr_t0[kzl_t0 + 184u];
    const Real v1538_g0 = kr_t0[kzl_t0 + 185u];
    const Real v1539_g0 = kr_t0[kzl_t0 + 186u];
    out[matrix_index(12, 14)] = (1.0409203801861816e-115*math::exp((22.915965629999999)*math::log(math::abs(T)))*xn[0]*xn[13]*v1526_g0 - 1.694596485110541e-207*math::exp((42.933476326349997)*math::log(math::abs(T)))*v1529_g0*v1537 + 4.3524079114767552e-117*xn[0]*xn[13]*v1526_g0*v1527_g0*(9.1741162500000009*v1515_g0*v1519_g0 + 0.33976956150000004*v1515_g0*v1521_g0 + 0.001447065312*v1515_g0*v1523_g0 - 21.506460400000002*v1540_g0 - 2.2740475600000001*v1541_g0 - 0.030054336600000002*v1542_g0 - 2.9193291280000001e-5*v1543_g0) - 3.0451686126851684e-13*xn[0]*math::exp((-2.7523999999999997)*math::log(math::abs(v1505_g0)))*v1515_g0*v1509 + xn[1]*xn[13]*jac_nuc_choice_78(T, v1532_g0, v1533_g0) - 2.3410580000000002e-11*xn[11]*v1515_g0*math::exp((-1.2476)*math::log(math::abs(v1507_g0)))*v1536 - 7.2084342424042629e-17*xn[12]*xn[2]*v1531_g0 - v1530_g0*v1537*(14.104879460277006*v1515_g0*v1519_g0 + 0.40565210486515002*v1515_g0*v1521_g0 + 0.0013829937185547*v1515_g0*v1523_g0 - 36.961339871360003*v1540_g0 - 3.0769865337967999*v1541_g0 - 0.031944123769722006*v1542_g0 - 2.5324648525320001e-5*v1543_g0) - v1537*jac_nuc_choice_79(T, v1514_g0, v1515_g0, v1516_g0, v1538_g0, v1539_g0) - 2.8942185892741411e-10*xn[0]*v1510/math::exp((3.0/2.0)*math::log(math::abs(T))))/(xn[0]*v1545 + xn[1]*v1545 + xn[10]*v1546 + xn[11]*v1545 + xn[12]*v1545 + xn[13]*v1545 + xn[2]*v1545 + xn[3]*v1545 + xn[4]*v1545 + xn[5]*v1545 + xn[6]*v1546 + xn[7]*v1545 + xn[8]*v1546 + xn[9]*v1546);
    const Real v1517_g0 = kr_t0[kzl_t0 + 187u];
    const Real v1528_g0 = kr_t0[kzl_t0 + 188u];
    const Real v1511_g0 = kr_t0[kzl_t0 + 189u];
    out[matrix_index(12, 0)] = -xn[12]*v1517_g0 - xn[12]*v1530_g0 + xn[13]*v1526_g0*v1528_g0 + v1510*v1511_g0;
    out[matrix_index(12, 13)] = xn[0]*v1526_g0*v1528_g0 + xn[1]*v1534_g0;
    const Real v1535_g1 = kr_t0[kzl_t0 + 7u];
    out[matrix_index(12, 12)] = -xn[0]*v1517_g0 - xn[0]*v1530_g0 - xn[2]*v1535_g1;
    out[matrix_index(12, 11)] = v1536*v1508_g0*v1511_g0;
    const Real v1467_g0 = kr_t0[kzl_t0 + 190u];
    const Real v1484 = xn[0]*v1467_g0;
    const Real v1469_g0 = kr_t0[kzl_t0 + 191u];
    const Real v1472_g0 = kr_t0[kzl_t0 + 192u];
    out[matrix_index(11, 11)] = -v1484*v1469_g0*v1472_g0;
    const Real v1480_g0 = kr_t0[kzl_t0 + 193u];
    const Real v1481 = xn[12]*v1480_g0;
    const Real v1482_g0 = kr_t0[kzl_t0 + 194u];
    const Real v1483 = v1481*v1482_g0;
    const Real v1486 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1487 = 2.0860422997526066e-16*v1486;
    const Real v1488 = 3.4767371836380304e-16*v1486;
    const Real v1470 = xn[11]*v1469_g0;
    const Real v1471 = v1467_g0*v1470;
    const Real v1466_g0 = kr_t0[kzl_t0 + 195u];
    const Real v1485_g0 = kr_t0[kzl_t0 + 196u];
    const Real v1475_g0 = kr_t0[kzl_t0 + 197u];
    const Real v1476_g0 = kr_t0[kzl_t0 + 198u];
    const Real v1477_g0 = kr_t0[kzl_t0 + 199u];
    const Real v1478_g0 = kr_t0[kzl_t0 + 200u];
    const Real v1479_g0 = kr_t0[kzl_t0 + 201u];
    const Real v1473_g0 = kr_t0[kzl_t0 + 202u];
    const Real v1474_g0 = kr_t0[kzl_t0 + 203u];
    const Real v1468_g0 = kr_t0[kzl_t0 + 204u];
    out[matrix_index(11, 14)] = (1.694596485110541e-207*math::exp((42.933476326349997)*math::log(math::abs(T)))*xn[0]*v1481 + 3.0451686126851684e-13*xn[0]*math::exp((-2.7523999999999997)*math::log(math::abs(v1466_g0)))*v1485_g0*v1470 + xn[0]*v1483*(-3.0769865337967999*v1475_g0*v1485_g0 + 0.40565210486515002*v1476_g0*v1485_g0 - 0.031944123769722006*v1477_g0*v1485_g0 + 0.0013829937185547*v1478_g0*v1485_g0 - 2.5324648525320001e-5*v1479_g0*v1485_g0 - 36.961339871360003*v1485_g0*v1473_g0 + 14.104879460277006*v1485_g0*v1474_g0) + 2.3410580000000002e-11*xn[11]*v1484*v1485_g0*math::exp((-1.2476)*math::log(math::abs(v1468_g0))) + 2.8942185892741411e-10*xn[0]*v1471/math::exp((3.0/2.0)*math::log(math::abs(T))))/(xn[0]*v1487 + xn[1]*v1487 + xn[10]*v1488 + xn[11]*v1487 + xn[12]*v1487 + xn[13]*v1487 + xn[2]*v1487 + xn[3]*v1487 + xn[4]*v1487 + xn[5]*v1487 + xn[6]*v1488 + xn[7]*v1487 + xn[8]*v1488 + xn[9]*v1488);
    out[matrix_index(11, 0)] = v1483 - v1471*v1472_g0;
    out[matrix_index(11, 12)] = xn[0]*v1480_g0*v1482_g0;
    const Real v1340_g0 = kr_t0[kzl_t0 + 205u];
    out[matrix_index(8, 4)] = xn[8]*v1340_g0;
    const Real v1211_g0 = kr_t0[kzl_t0 + 206u];
    const Real v1212 = xn[8]*v1211_g0;
    const Real v1214_g0 = kr_t0[kzl_t0 + 207u];
    const Real v1215 = xn[8]*v1214_g0;
    const Real v1213_g0 = kr_t0[kzl_t0 + 208u];
    const Real v1216_g0 = kr_t0[kzl_t0 + 209u];
    out[matrix_index(8, 0)] = -v1212*v1213_g0 - v1215*v1216_g0;
    const Real v993_g0 = kr_t0[kzl_t0 + 210u];
    const Real v994 = xn[3]*v993_g0;
    const Real v995_g0 = kr_t0[kzl_t0 + 211u];
    const Real v996 = v994*v995_g0;
    const Real v977_g0 = kr_t0[kzl_t0 + 212u];
    const Real v978 = xn[2]*v977_g0;
    const Real v1013 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1014 = 2.0860422997526066e-16*v1013;
    const Real v1015 = 3.4767371836380304e-16*v1013;
    const Real v982_g1 = kr_t0[kzl_t0 + 54u];
    const Real v983 = xn[8]*v982_g1;
    const Real v1018 = xn[2]*xn[3];
    const Real v979_g0 = kr_t0[kzl_t0 + 213u];
    const Real v988_g0 = kr_t0[kzl_t0 + 214u];
    const Real v981_g0 = kr_t0[kzl_t0 + 215u];
    const Real v989_g0 = kr_t0[kzl_t0 + 216u];
    const Real v990_g0 = kr_t0[kzl_t0 + 217u];
    const Real v991_g0 = kr_t0[kzl_t0 + 218u];
    const Real v986_g0 = kr_t0[kzl_t0 + 219u];
    const Real v1019_g0 = kr_t0[kzl_t0 + 220u];
    const Real v1020_g0 = kr_t0[kzl_t0 + 221u];
    const Real v999_g0 = kr_t0[kzl_t0 + 222u];
    const Real v1000_g0 = kr_t0[kzl_t0 + 223u];
    const Real v1017_g0 = kr_t0[kzl_t0 + 224u];
    const Real v1016_g0 = kr_t0[kzl_t0 + 225u];
    const Real v1004_g0 = kr_t0[kzl_t0 + 226u];
    const Real v992_g0 = kr_t0[kzl_t0 + 227u];
    const Real v1007_g0 = kr_t0[kzl_t0 + 228u];
    const Real v1008_g0 = kr_t0[kzl_t0 + 229u];
    const Real v1009_g0 = kr_t0[kzl_t0 + 230u];
    const Real v1005_g0 = kr_t0[kzl_t0 + 231u];
    const Real v1003_g0 = kr_t0[kzl_t0 + 232u];
    out[matrix_index(3, 14)] = (1658098.5*math::exp((-4.2799999999999994)*math::log(math::abs(T)))*xn[0]*xn[8]*v982_g1 - 80.939999999999998*math::exp((-3.2799999999999998)*math::log(math::abs(T)))*xn[0]*v983 + 4.0000000000000002e-9*math::exp((-1.3999999999999999)*math::log(math::abs(T)))*xn[1]*xn[3] + 2.6534040307116389e-10*math::exp((-1.1000000000000001)*math::log(math::abs(T)))*xn[3]*xn[5] + 1.2992000000000002e-18*math::exp((-0.071999999999999953)*math::log(math::abs(T)))*xn[0]*xn[2]*v977_g0 - 8.9485740404797324e-18*math::exp((1.360852208681)*math::log(math::abs(T)))*xn[0]*v994 - 8.6419753086419757e-23*xn[0]*v978*v979_g0 - xn[0]*v996*(0.058916489135550004*v988_g0*v981_g0 - 0.009937168197024001*v989_g0*v981_g0 + 0.00074779264187460007*v990_g0*v981_g0 - 2.1050286473655998e-5*v991_g0*v981_g0 + 0.048699499187009998*v981_g0*v986_g0 - 0.56548861234079995*v1019_g0 - 0.13460048125451995*v1020_g0) - xn[1]*xn[3]*jac_nuc_choice_46(v999_g0, v1000_g0, v1017_g0) + 2.5313028975878652e-10*xn[2]*xn[7]*v1016_g0 - 2.5313028975878652e-10*xn[3]*xn[5]*v1016_g0 + 2.4999999999999998e-6*xn[3]*xn[6]*v1017_g0 - v1004_g0*v1018*(1.3296555000000001e-10*math::exp((-0.90150700000000006)*math::log(math::abs(T))) + 2.466314622e-10*math::exp((-0.44389999999999996)*math::log(math::abs(T))) + 8.1647792100000001e-16*math::exp((1.1825999999999999)*math::log(math::abs(T)))) - v1018*jac_nuc_choice_47(T, v988_g0, v989_g0, v990_g0, v991_g0, v992_g0, v1007_g0, v1008_g0, v1009_g0, v981_g0, v1019_g0, v1020_g0, v986_g0) - v1005_g0*v1018*(-0.0064764051000000007*math::exp((0.04610000000000003)*math::log(math::abs(T))) - 2.7293978880000002e-10*math::exp((2.0424000000000002)*math::log(math::abs(T))) - 1.229450816e-13*math::exp((2.7740999999999998)*math::log(math::abs(T))))/((v1003_g0)*(v1003_g0)))/(xn[0]*v1014 + xn[1]*v1014 + xn[10]*v1015 + xn[11]*v1014 + xn[12]*v1014 + xn[13]*v1014 + xn[2]*v1014 + xn[3]*v1014 + xn[4]*v1014 + xn[5]*v1014 + xn[6]*v1015 + xn[7]*v1014 + xn[8]*v1015 + xn[9]*v1015);
    const Real v997_g1 = kr_t0[kzl_t0 + 73u];
    const Real v1001_g1 = kr_t0[kzl_t0 + 74u];
    const Real v1006_g0 = kr_t0[kzl_t0 + 233u];
    const Real v1010_g0 = kr_t0[kzl_t0 + 234u];
    const Real v1002_g1 = kr_t0[kzl_t0 + 1u];
    const Real v1011_g1 = kr_t0[kzl_t0 + 15u];
    const Real v1012_g1 = kr_t0[kzl_t0 + 72u];
    out[matrix_index(3, 3)] = -xn[0]*v993_g0*v995_g0 - xn[1]*v997_g1 - xn[1]*v1001_g1 - xn[2]*v1006_g0 - xn[2]*v1010_g0 - xn[5]*v1002_g1 - xn[5]*v1011_g1 - xn[6]*v1012_g1;
    const Real v980_g0 = kr_t0[kzl_t0 + 235u];
    out[matrix_index(3, 2)] = xn[0]*v977_g0*v980_g0 - xn[3]*v1006_g0 - xn[3]*v1010_g0 + xn[7]*v1002_g1;
    const Real v984_g1 = kr_t0[kzl_t0 + 55u];
    out[matrix_index(3, 0)] = v978*v980_g0 - v996 + v983*v984_g1;
    const Real v695_g0 = kr_t0[kzl_t0 + 236u];
    const Real v696_g0 = kr_t0[kzl_t0 + 237u];
    out[matrix_index(1, 4)] = xn[2]*v695_g0 + xn[8]*v696_g0;
    const Real v677_g0 = kr_t0[kzl_t0 + 238u];
    const Real v678 = xn[5]*v677_g0;
    const Real v697 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v698 = 2.0860422997526066e-16*v697;
    const Real v699 = 3.4767371836380304e-16*v697;
    const Real v647_g0 = kr_t0[kzl_t0 + 239u];
    const Real v650_g0 = kr_t0[kzl_t0 + 240u];
    const Real v651_g0 = kr_t0[kzl_t0 + 241u];
    const Real v652_g0 = kr_t0[kzl_t0 + 242u];
    const Real v656_g0 = kr_t0[kzl_t0 + 243u];
    const Real v640_g0 = kr_t0[kzl_t0 + 244u];
    const Real v642_g0 = kr_t0[kzl_t0 + 245u];
    const Real v643_g0 = kr_t0[kzl_t0 + 246u];
    const Real v644_g0 = kr_t0[kzl_t0 + 247u];
    const Real v704_g0 = kr_t0[kzl_t0 + 248u];
    const Real v705_g0 = kr_t0[kzl_t0 + 249u];
    const Real v706_g0 = kr_t0[kzl_t0 + 250u];
    const Real v646_g0 = kr_t0[kzl_t0 + 251u];
    const Real v648_g0 = kr_t0[kzl_t0 + 252u];
    const Real v657_g0 = kr_t0[kzl_t0 + 253u];
    const Real v700_g0 = kr_t0[kzl_t0 + 254u];
    const Real v660_g0 = kr_t0[kzl_t0 + 255u];
    const Real v661_g0 = kr_t0[kzl_t0 + 256u];
    const Real v670_g0 = kr_t0[kzl_t0 + 257u];
    const Real v671_g0 = kr_t0[kzl_t0 + 258u];
    const Real v679_g0 = kr_t0[kzl_t0 + 259u];
    const Real v680_g0 = kr_t0[kzl_t0 + 260u];
    const Real v682_g0 = kr_t0[kzl_t0 + 261u];
    const Real v684_g0 = kr_t0[kzl_t0 + 262u];
    const Real v702_g0 = kr_t0[kzl_t0 + 263u];
    const Real v703_g0 = kr_t0[kzl_t0 + 264u];
    const Real v667_g0 = kr_t0[kzl_t0 + 265u];
    const Real v668_g0 = kr_t0[kzl_t0 + 266u];
    const Real v663_g0 = kr_t0[kzl_t0 + 267u];
    const Real v664_g0 = kr_t0[kzl_t0 + 268u];
    const Real v681_g0 = kr_t0[kzl_t0 + 269u];
    const Real v686_g0 = kr_t0[kzl_t0 + 270u];
    const Real v687_g0 = kr_t0[kzl_t0 + 271u];
    const Real v688_g0 = kr_t0[kzl_t0 + 272u];
    const Real v689_g0 = kr_t0[kzl_t0 + 273u];
    const Real v690_g0 = kr_t0[kzl_t0 + 274u];
    const Real v691_g0 = kr_t0[kzl_t0 + 275u];
    const Real v701_g0 = kr_t0[kzl_t0 + 276u];
    const Real v675_g0 = kr_t0[kzl_t0 + 277u];
    const Real v674_g0 = kr_t0[kzl_t0 + 278u];
    const Real v676_g0 = kr_t0[kzl_t0 + 279u];
    const Real v659_g0 = kr_t0[kzl_t0 + 280u];
    const Real v694_g0 = kr_t0[kzl_t0 + 281u];
    const Real v672_g0 = kr_t0[kzl_t0 + 282u];
    out[matrix_index(1, 14)] = (3.9837168574084181e-7*math::exp((-1.5)*math::log(math::abs(T)))*xn[1]*xn[7] + 4.0000000000000002e-9*math::exp((-1.3999999999999999)*math::log(math::abs(T)))*xn[1]*xn[3] + 7.997727392299023e-69*math::exp((12.536555999999999)*math::log(math::abs(T)))*xn[0]*xn[2]*v647_g0 - xn[0]*xn[1]*jac_nuc_choice_15(T, v650_g0, v651_g0, v652_g0, v656_g0, v640_g0, v642_g0, v643_g0, v644_g0, v704_g0, v705_g0, v706_g0, v646_g0) + 5.9082438637265071e-70*xn[0]*xn[2]*v648_g0*v647_g0*(4.6894649399999997*v656_g0*v640_g0 + 0.1741279885*v656_g0*v642_g0 - 0.015791857020000001*v656_g0*v643_g0 + 0.00078368076500000001*v656_g0*v644_g0 - 11.478657500000001*v704_g0 - 1.1508224*v705_g0 - 1.6313198799999999e-5*v706_g0) - 4.5700000000000003e-7*xn[1]*xn[10]*v657_g0*v700_g0 - xn[1]*xn[13]*jac_nuc_choice_16(T, v660_g0, v661_g0) - xn[1]*xn[2]*jac_nuc_choice_17(T, v656_g0, v670_g0, v671_g0, v679_g0, v680_g0, v682_g0, v684_g0, v702_g0, v703_g0) - xn[1]*xn[3]*jac_nuc_choice_18(T, v667_g0, v668_g0) - xn[1]*xn[5]*jac_nuc_choice_19(T, v663_g0, v664_g0) - xn[1]*xn[8]*jac_nuc_choice_20(v656_g0, v681_g0, v686_g0, v687_g0, v688_g0, v689_g0, v690_g0, v691_g0, v700_g0, v701_g0, v703_g0) - xn[1]*v678*(5.1485802679346868*v656_g0*math::exp((1.0)*math::log(math::abs(v670_g0)))*v675_g0 - 0.87659414490283338*v656_g0*v674_g0*v676_g0 - 3.5068370966299316*v702_g0) + 7.2084342424042629e-17*xn[12]*xn[2]*v659_g0 + xn[2]*xn[4]*jac_nuc_choice_21(T, v664_g0, v694_g0) + xn[4]*xn[8]*(8.4600000000000008e-10*v656_g0*v672_g0 - 2.7400000000000004e-10*v682_g0*v701_g0))/(xn[0]*v698 + xn[1]*v698 + xn[10]*v699 + xn[11]*v698 + xn[12]*v698 + xn[13]*v698 + xn[2]*v698 + xn[3]*v698 + xn[4]*v698 + xn[5]*v698 + xn[6]*v699 + xn[7]*v698 + xn[8]*v699 + xn[9]*v699);
    const Real v653_g0 = kr_t0[kzl_t0 + 283u];
    const Real v658_g1 = kr_t0[kzl_t0 + 30u];
    const Real v662_g1 = kr_t0[kzl_t0 + 77u];
    const Real v685_g0 = kr_t0[kzl_t0 + 284u];
    const Real v655_g1 = kr_t0[kzl_t0 + 159u];
    const Real v669_g1 = kr_t0[kzl_t0 + 160u];
    const Real v665_g0 = kr_t0[kzl_t0 + 285u];
    const Real v654_g1 = kr_t0[kzl_t0 + 9u];
    const Real v692_g0 = kr_t0[kzl_t0 + 286u];
    out[matrix_index(1, 1)] = -xn[0]*v653_g0 - xn[10]*v658_g1 - xn[13]*v662_g1 - xn[2]*v685_g0 - xn[3]*v655_g1 - xn[3]*v669_g1 - xn[5]*v665_g0 - xn[7]*v654_g1 - xn[8]*v692_g0 - v678;
    const Real v649_g0 = kr_t0[kzl_t0 + 287u];
    const Real v693_g1 = kr_t0[kzl_t0 + 4u];
    out[matrix_index(1, 2)] = xn[0]*v649_g0*v647_g0 - xn[1]*v685_g0 + xn[12]*v693_g1 + xn[4]*v695_g0 + 6.0e-10*xn[6] + 6.3999999999999996e-10*xn[9];
    out[matrix_index(1, 0)] = -xn[1]*v653_g0 + xn[2]*v649_g0*v647_g0;
    out[matrix_index(1, 8)] = -xn[1]*v692_g0 + xn[4]*v696_g0;
    out[matrix_index(1, 5)] = -xn[1]*v665_g0 - xn[1]*v677_g0;
    const Real v896_g0 = kr_t0[kzl_t0 + 288u];
    const Real v900_g0 = kr_t0[kzl_t0 + 289u];
    out[matrix_index(2, 4)] = -xn[2]*v896_g0 - xn[2]*v900_g0;
    const Real v1346_g0 = kr_t0[kzl_t0 + 290u];
    out[matrix_index(8, 5)] = -xn[8]*v1346_g0;
    const Real v580_g0 = kr_t0[kzl_t0 + 291u];
    const Real v602_g0 = kr_t0[kzl_t0 + 292u];
    out[matrix_index(0, 13)] = xn[0]*v580_g0*v602_g0;
    const Real v538_g0 = kr_t0[kzl_t0 + 293u];
    const Real v539 = xn[2]*v538_g0;
    const Real v608 = xn[0]*v539;
    const Real v553_g1 = kr_t0[kzl_t0 + 157u];
    const Real v554 = xn[11]*v553_g1;
    const Real v551_g1 = kr_t0[kzl_t0 + 156u];
    const Real v555 = v551_g1*v554;
    const Real v603 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v604 = 2.0860422997526066e-16*v603;
    const Real v605 = 3.4767371836380304e-16*v603;
    const Real v542_g1 = kr_t0[kzl_t0 + 76u];
    const Real v543 = xn[5]*v542_g1;
    const Real v609 = xn[0]*v543;
    const Real v547_g1 = kr_t0[kzl_t0 + 27u];
    const Real v548 = xn[8]*v547_g1;
    const Real v610 = xn[0]*v548;
    const Real v576_g0 = kr_t0[kzl_t0 + 294u];
    const Real v612 = xn[3]*v576_g0;
    const Real v578_g0 = kr_t0[kzl_t0 + 295u];
    const Real v613 = xn[2]*v578_g0;
    const Real v607 = xn[0]/math::exp((3.0/2.0)*math::log(math::abs(T)));
    const Real v611 = xn[2]*xn[3];
    const Real v614 = xn[13]*v580_g0;
    const Real v615 = xn[0]*xn[12];
    const Real v582_g0 = kr_t0[kzl_t0 + 296u];
    const Real v546_g0 = kr_t0[kzl_t0 + 297u];
    const Real v569_g0 = kr_t0[kzl_t0 + 298u];
    const Real v571_g0 = kr_t0[kzl_t0 + 299u];
    const Real v572_g0 = kr_t0[kzl_t0 + 300u];
    const Real v573_g0 = kr_t0[kzl_t0 + 301u];
    const Real v583_g0 = kr_t0[kzl_t0 + 302u];
    const Real v584_g0 = kr_t0[kzl_t0 + 303u];
    const Real v586_g0 = kr_t0[kzl_t0 + 304u];
    const Real v616_g0 = kr_t0[kzl_t0 + 305u];
    const Real v618_g0 = kr_t0[kzl_t0 + 306u];
    const Real v619_g0 = kr_t0[kzl_t0 + 307u];
    const Real v621_g0 = kr_t0[kzl_t0 + 308u];
    const Real v622_g0 = kr_t0[kzl_t0 + 309u];
    const Real v557_g0 = kr_t0[kzl_t0 + 310u];
    const Real v558_g0 = kr_t0[kzl_t0 + 311u];
    const Real v559_g0 = kr_t0[kzl_t0 + 312u];
    const Real v560_g0 = kr_t0[kzl_t0 + 313u];
    const Real v550_g0 = kr_t0[kzl_t0 + 314u];
    const Real v594_g0 = kr_t0[kzl_t0 + 315u];
    const Real v620_g0 = kr_t0[kzl_t0 + 316u];
    const Real v599_g0 = kr_t0[kzl_t0 + 317u];
    const Real v552_g0 = kr_t0[kzl_t0 + 318u];
    const Real v606_g0 = kr_t0[kzl_t0 + 319u];
    const Real v544_g0 = kr_t0[kzl_t0 + 320u];
    const Real v591_g0 = kr_t0[kzl_t0 + 321u];
    const Real v540_g0 = kr_t0[kzl_t0 + 322u];
    const Real v601_g0 = kr_t0[kzl_t0 + 323u];
    const Real v574_g0 = kr_t0[kzl_t0 + 324u];
    const Real v595_g0 = kr_t0[kzl_t0 + 325u];
    const Real v596_g0 = kr_t0[kzl_t0 + 326u];
    const Real v597_g0 = kr_t0[kzl_t0 + 327u];
    const Real v564_g0 = kr_t0[kzl_t0 + 328u];
    const Real v565_g0 = kr_t0[kzl_t0 + 329u];
    const Real v617_g0 = kr_t0[kzl_t0 + 330u];
    const Real v592_g0 = kr_t0[kzl_t0 + 331u];
    const Real v590_g0 = kr_t0[kzl_t0 + 332u];
    out[matrix_index(0, 14)] = (-1658098.5*math::exp((-4.2799999999999994)*math::log(math::abs(T)))*v610 + 80.939999999999998*math::exp((-3.2799999999999998)*math::log(math::abs(T)))*v610 + 1.9462772454491511e-10*math::exp((-1.75)*math::log(math::abs(T)))*xn[0]*xn[4] - 4.0000000000000002e-9*math::exp((-1.3999999999999999)*math::log(math::abs(T)))*xn[1]*xn[3] - 1.2992000000000002e-18*math::exp((-0.071999999999999953)*math::log(math::abs(T)))*v608 - 1.2635128643896626e-18*math::exp((-0.050000000000000044)*math::log(math::abs(T)))*v609 + 8.9485740404797324e-18*math::exp((1.360852208681)*math::log(math::abs(T)))*xn[0]*v612 + 7.997727392299023e-69*math::exp((12.536555999999999)*math::log(math::abs(T)))*xn[0]*v613 + 1.0409203801861816e-115*math::exp((22.915965629999999)*math::log(math::abs(T)))*xn[0]*v614 + 1.694596485110541e-207*math::exp((42.933476326349997)*math::log(math::abs(T)))*v582_g0*v615 - xn[0]*xn[1]*jac_nuc_choice_4(T, v546_g0, v569_g0, v571_g0, v572_g0, v573_g0, v583_g0, v584_g0, v586_g0, v616_g0, v618_g0, v619_g0, v621_g0, v622_g0) - xn[0]*xn[6]*jac_nuc_choice_5(T, v557_g0, v558_g0, v559_g0, v560_g0) + 3.0451686126851684e-13*xn[0]*v546_g0*math::exp((-2.7523999999999997)*math::log(math::abs(v550_g0)))*v554 + xn[0]*v594_g0*v613*(4.6894649399999997*v546_g0*v569_g0 + 0.1741279885*v546_g0*v571_g0 + 0.00078368076500000001*v546_g0*v573_g0 - 11.478657500000001*v618_g0 - 1.1508224*v619_g0 - 0.015791857020000001*v620_g0 - 1.6313198799999999e-5*v621_g0) + xn[0]*v599_g0*v612*(0.048699499187009998*v546_g0*v569_g0 + 0.058916489135550004*v546_g0*v571_g0 + 0.00074779264187460007*v546_g0*v573_g0 - 0.56548861234079995*v618_g0 - 0.13460048125451995*v619_g0 - 0.009937168197024001*v620_g0 - 2.1050286473655998e-5*v621_g0) + xn[0]*v602_g0*v614*(9.1741162500000009*v546_g0*v569_g0 + 0.33976956150000004*v546_g0*v571_g0 + 0.001447065312*v546_g0*v573_g0 - 21.506460400000002*v618_g0 - 2.2740475600000001*v619_g0 - 0.030054336600000002*v620_g0 - 2.9193291280000001e-5*v621_g0) + 2.3410580000000002e-11*xn[11]*v546_g0*math::exp((-1.2476)*math::log(math::abs(v552_g0)))*v600 - xn[2]*xn[7]*v606_g0 - xn[3]*xn[5]*v606_g0 + 3.5999999999999998e-8*xn[9]*v607 + 1.4270531560759686e-22*v544_g0*v609 + 2.8942185892741411e-10*v555*v607 + v591_g0*v611*(1.3296555000000001e-10*math::exp((-0.90150700000000006)*math::log(math::abs(T))) + 2.466314622e-10*math::exp((-0.44389999999999996)*math::log(math::abs(T))) + 8.1647792100000001e-16*math::exp((1.1825999999999999)*math::log(math::abs(T)))) + 8.6419753086419757e-23*v540_g0*v608 + v601_g0*v615*(14.104879460277006*v546_g0*v569_g0 + 0.40565210486515002*v546_g0*v571_g0 + 0.0013829937185547*v546_g0*v573_g0 - 36.961339871360003*v618_g0 - 3.0769865337967999*v619_g0 - 0.031944123769722006*v620_g0 - 2.5324648525320001e-5*v621_g0) + v611*jac_nuc_choice_6(T, v546_g0, v569_g0, v571_g0, v572_g0, v573_g0, v574_g0, v595_g0, v596_g0, v597_g0, v618_g0, v619_g0, v622_g0) - v615*jac_nuc_choice_7(T, v546_g0, v564_g0, v565_g0, v616_g0, v617_g0) + v592_g0*v611*(-0.0064764051000000007*math::exp((0.04610000000000003)*math::log(math::abs(T))) - 2.7293978880000002e-10*math::exp((2.0424000000000002)*math::log(math::abs(T))) - 1.229450816e-13*math::exp((2.7740999999999998)*math::log(math::abs(T))))/((v590_g0)*(v590_g0)))/(xn[0]*v604 + xn[1]*v604 + xn[10]*v605 + xn[11]*v604 + xn[12]*v604 + xn[13]*v604 + xn[2]*v604 + xn[3]*v604 + xn[4]*v604 + xn[5]*v604 + xn[6]*v605 + xn[7]*v604 + xn[8]*v605 + xn[9]*v605);
    const Real v587_g0 = kr_t0[kzl_t0 + 333u];
    const Real v566_g0 = kr_t0[kzl_t0 + 334u];
    const Real v581_g0 = kr_t0[kzl_t0 + 335u];
    const Real v579_g0 = kr_t0[kzl_t0 + 336u];
    const Real v577_g0 = kr_t0[kzl_t0 + 337u];
    const Real v567_g0 = kr_t0[kzl_t0 + 338u];
    const Real v534_g1 = kr_t0[kzl_t0 + 10u];
    const Real v561_g1 = kr_t0[kzl_t0 + 161u];
    const Real v537_g1 = kr_t0[kzl_t0 + 26u];
    const Real v545_g1 = kr_t0[kzl_t0 + 75u];
    const Real v549_g1 = kr_t0[kzl_t0 + 28u];
    const Real v556_g1 = kr_t0[kzl_t0 + 158u];
    const Real v541_g0 = kr_t0[kzl_t0 + 339u];
    out[matrix_index(0, 0)] = -xn[1]*v587_g0 - xn[12]*v566_g0 + 3.8571873359681582e-209*xn[12]*v581_g0*v582_g0 + 4.3524079114767552e-117*xn[13]*v579_g0*v580_g0 + 5.9082438637265071e-70*xn[2]*v577_g0*v578_g0 + 3.7903999274394518e-18*xn[3]*v567_g0*v576_g0 - xn[4]*v534_g1 - xn[6]*v561_g1 - xn[9]*v537_g1 - v545_g1*v543 - v548*v549_g1 - v555*v556_g1 - v539*v541_g0;
    const Real v593_g0 = kr_t0[kzl_t0 + 340u];
    const Real v598_g0 = kr_t0[kzl_t0 + 341u];
    const Real v589_g2 = kr_t0[kzl_t0 + 14u];
    out[matrix_index(0, 2)] = -xn[0]*v538_g0*v541_g0 + xn[0]*v578_g0*v594_g0 + xn[3]*v593_g0 + xn[3]*v598_g0 + xn[7]*v589_g2;
    const Real v588_g0 = kr_t0[kzl_t0 + 342u];
    out[matrix_index(0, 3)] = xn[0]*v576_g0*v599_g0 + xn[1]*v588_g0 + xn[2]*v593_g0 + xn[2]*v598_g0 + xn[5]*v589_g2;
    out[matrix_index(0, 12)] = -xn[0]*v566_g0 + xn[0]*v601_g0;
    out[matrix_index(0, 1)] = -xn[0]*v587_g0 + xn[3]*v588_g0;
    const Real v768_g0 = kr_t0[kzl_t0 + 343u];
    const Real v887_g1 = kr_t0[kzl_t0 + 136u];
    const Real v920_g0 = kr_t0[kzl_t0 + 344u];
    out[matrix_index(2, 5)] = xn[1]*v768_g0 - 1.0e-25*xn[2] + xn[3]*v887_g1 + xn[8]*v920_g0;
    const Real v1231 = xn[1] + xn[10] + xn[2] + xn[3] + 2.0*xn[6] + 2.0*xn[8] + xn[9];
    const Real v1279_g0 = kr_t0[kzl_t0 + 345u];
    const Real v1280 = v1231*v1279_g0;
    const Real v1267_g0 = kr_t0[kzl_t0 + 346u];
    const Real v1281 = math::exp((v1267_g0)*math::log(math::abs(v1280)));
    const Real v1282 = v1281 + 1.0;
    const Real v1283 = 1.0/v1282;
    const Real v1263_g0 = kr_t0[kzl_t0 + 347u];
    const Real v1264 = v1231*v1263_g0;
    const Real v1268 = math::exp((v1267_g0)*math::log(math::abs(v1264)));
    const Real v1269 = v1268 + 1.0;
    const Real v1270 = 1.0/v1269;
    const Real v1210_g0 = kr_t0[kzl_t0 + 348u];
    const Real v1235_g0 = kr_t0[kzl_t0 + 349u];
    const Real v1219_g0 = kr_t0[kzl_t0 + 350u];
    const Real v1277_g0 = kr_t0[kzl_t0 + 351u];
    const Real v1271_g0 = kr_t0[kzl_t0 + 352u];
    const Real v1276_g0 = kr_t0[kzl_t0 + 353u];
    const Real v1284 = math::exp((-2080.4099999999999*v1210_g0*v1283 - 23705.700000000001*v1210_g0 + 43.20243*v1235_g0*v1219_g0 - v1270*v1277_g0 - 68.422430000000006*v1271_g0 - v1276_g0 - 178.4239)*math::log(math::abs(10.0)));
    const Real v1308_g0 = kr_t0[kzl_t0 + 354u];
    const Real v1309 = v1231*v1308_g0;
    const Real v1299_g0 = kr_t0[kzl_t0 + 355u];
    const Real v1310 = math::exp((v1299_g0)*math::log(math::abs(v1309)));
    const Real v1311 = v1310 + 1.0;
    const Real v1312 = 1.0/v1311;
    const Real v1297_g0 = kr_t0[kzl_t0 + 356u];
    const Real v1298 = v1231*v1297_g0;
    const Real v1300 = math::exp((v1299_g0)*math::log(math::abs(v1298)));
    const Real v1301 = v1300 + 1.0;
    const Real v1302 = 1.0/v1301;
    const Real v1234_g0 = kr_t0[kzl_t0 + 357u];
    const Real v1218_g0 = kr_t0[kzl_t0 + 358u];
    const Real v1260_g0 = kr_t0[kzl_t0 + 359u];
    const Real v1306_g0 = kr_t0[kzl_t0 + 360u];
    const Real v1305_g0 = kr_t0[kzl_t0 + 361u];
    const Real v1313 = math::exp((-1657.4099999999999*v1210_g0*v1312 - 21467.790000000001*v1210_g0 + 42.707410000000003*v1234_g0*v1218_g0 - 2.0273650000000001*v1260_g0 - v1302*v1306_g0 - v1305_g0 - 142.7664)*math::log(math::abs(10.0)));
    const Real v1236_g0 = kr_t0[kzl_t0 + 362u];
    const Real v1237 = v1231*v1236_g0;
    const Real v1238 = v1237 + 1.0;
    const Real v1243 = 1.0/v1238;
    const Real v1244 = 1.0*v1243;
    const Real v1253 = 1.0 - v1244;
    const Real v1252_g0 = kr_t0[kzl_t0 + 363u];
    const Real v1254 = math::exp((v1253)*math::log(math::abs(v1252_g0)));
    const Real v1229_g0 = kr_t0[kzl_t0 + 364u];
    const Real v1245 = math::exp((v1244)*math::log(math::abs(v1229_g0)));
    const Real v1255 = v1245*v1254;
    const Real v1242 = ((xn[8])*(xn[8]));
    const Real v1256 = v1242*v1255;
    const Real v1239 = math::exp((-2)*math::log(math::abs(v1238)));
    const Real v1240 = 1.0*v1239;
    const Real v1258_g0 = kr_t0[kzl_t0 + 365u];
    const Real v1259 = v1240*v1258_g0;
    const Real v1291 = v1268*v1277_g0/((v1269)*(v1269));
    const Real v1292 = 2.3025850929940459*v1291;
    const Real v1319 = v1300*v1306_g0/((v1301)*(v1301));
    const Real v1320 = 2.3025850929940459*v1319;
    const Real v1285 = v1210_g0*v1281/((v1282)*(v1282));
    const Real v1286 = 4790.3210533157426*v1285;
    const Real v1314 = v1210_g0*v1310/((v1311)*(v1311));
    const Real v1315 = 3816.3275589792611*v1314;
    const Real v1230_g0 = kr_t0[kzl_t0 + 366u];
    const Real v1241 = v1230_g0*v1240;
    const Real v1210_g1 = kr_t0[kzl_t0 + 348u];
    const Real v1232_g0 = kr_t0[kzl_t0 + 367u];
    const Real v1235_g1 = kr_t0[kzl_t0 + 349u];
    const Real v1366 = v1237*(-7.460375701300709*v1210_g1*v1232_g0*v1235_g1 + 2.9933606208922598*v1210_g1*v1234_g0);
    const Real v1350 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v1351 = 2.0860422997526066e-16*v1350;
    const Real v1352 = 3.4767371836380304e-16*v1350;
    const Real v1330 = ((xn[2])*(xn[2]));
    const Real v1357 = xn[8]*v1330;
    const Real v1323 = xn[2]*xn[8];
    const Real v1353 = xn[0]*v1212;
    const Real v1354 = xn[0]*v1215;
    const Real v1361 = xn[2]*xn[3];
    const Real v1325_g0 = kr_t0[kzl_t0 + 368u];
    const Real v1227_g0 = kr_t0[kzl_t0 + 369u];
    const Real v1222_g0 = kr_t0[kzl_t0 + 370u];
    const Real v1223_g0 = kr_t0[kzl_t0 + 371u];
    const Real v1224_g0 = kr_t0[kzl_t0 + 372u];
    const Real v1359_g0 = kr_t0[kzl_t0 + 373u];
    const Real v1225_g0 = kr_t0[kzl_t0 + 374u];
    const Real v1362_g0 = kr_t0[kzl_t0 + 375u];
    const Real v1363_g0 = kr_t0[kzl_t0 + 376u];
    const Real v1364_g0 = kr_t0[kzl_t0 + 377u];
    const Real v1217_g0 = kr_t0[kzl_t0 + 378u];
    const Real v1326_g0 = kr_t0[kzl_t0 + 379u];
    const Real v1327_g0 = kr_t0[kzl_t0 + 380u];
    const Real v1355_g0 = kr_t0[kzl_t0 + 381u];
    const Real v1356_g0 = kr_t0[kzl_t0 + 382u];
    const Real v1358_g0 = kr_t0[kzl_t0 + 383u];
    const Real v1360_g0 = kr_t0[kzl_t0 + 384u];
    const Real v1341_g0 = kr_t0[kzl_t0 + 385u];
    const Real v1342_g0 = kr_t0[kzl_t0 + 386u];
    const Real v1343_g0 = kr_t0[kzl_t0 + 387u];
    const Real v1344_g0 = kr_t0[kzl_t0 + 388u];
    const Real v1345_g0 = kr_t0[kzl_t0 + 389u];
    const Real v1365_g0 = kr_t0[kzl_t0 + 390u];
    const Real v1233_g0 = kr_t0[kzl_t0 + 391u];
    const Real v1316_g0 = kr_t0[kzl_t0 + 392u];
    const Real v1374_g0 = kr_t0[kzl_t0 + 393u];
    const Real v1373_g0 = kr_t0[kzl_t0 + 394u];
    const Real v1321_g0 = kr_t0[kzl_t0 + 395u];
    const Real v1372_g0 = kr_t0[kzl_t0 + 396u];
    const Real v1234_g1 = kr_t0[kzl_t0 + 357u];
    const Real v1368_g0 = kr_t0[kzl_t0 + 397u];
    const Real v1303_g0 = kr_t0[kzl_t0 + 398u];
    const Real v1369_g0 = kr_t0[kzl_t0 + 399u];
    const Real v1218_g1 = kr_t0[kzl_t0 + 358u];
    const Real v1272_g0 = kr_t0[kzl_t0 + 400u];
    const Real v1370_g0 = kr_t0[kzl_t0 + 401u];
    const Real v1371_g0 = kr_t0[kzl_t0 + 402u];
    const Real v1287_g0 = kr_t0[kzl_t0 + 403u];
    const Real v1293_g0 = kr_t0[kzl_t0 + 404u];
    const Real v1333_g0 = kr_t0[kzl_t0 + 405u];
    const Real v1246_g0 = kr_t0[kzl_t0 + 406u];
    const Real v1251_g0 = kr_t0[kzl_t0 + 407u];
    const Real v1367_g0 = kr_t0[kzl_t0 + 408u];
    const Real v1248_g0 = kr_t0[kzl_t0 + 409u];
    const Real v1250_g0 = kr_t0[kzl_t0 + 410u];
    const Real v1249_g0 = kr_t0[kzl_t0 + 411u];
    const Real v1334_g0 = kr_t0[kzl_t0 + 412u];
    const Real v1332_g0 = kr_t0[kzl_t0 + 413u];
    out[matrix_index(8, 14)] = (-1658098.5*math::exp((-4.2799999999999994)*math::log(math::abs(T)))*v1353 + 80.939999999999998*math::exp((-3.2799999999999998)*math::log(math::abs(T)))*v1353 - 4.4675999999999997e-5*math::exp((-1.6499999999999999)*math::log(math::abs(T)))*v1354 - 1.5329999999999998e-10*math::exp((-0.65000000000000002)*math::log(math::abs(T)))*v1354 + 4.5700000000000003e-7*xn[1]*xn[10]*v1325_g0*v1227_g0 - xn[1]*xn[8]*jac_nuc_choice_69(v1210_g1, v1325_g0, v1222_g0, v1223_g0, v1224_g0, v1359_g0, v1225_g0, v1362_g0, v1363_g0, v1364_g0, v1217_g0) + xn[10]*xn[2]*jac_nuc_choice_70(T, v1325_g0, v1326_g0, v1327_g0) + ((xn[2])*(xn[2])*(xn[2]))*(-1.0000000000000001e-31*v1355_g0 - 1.5e-32*v1356_g0) + xn[4]*xn[8]*(-8.4600000000000008e-10*v1358_g0 + 2.7400000000000004e-10*v1360_g0) - xn[5]*xn[8]*jac_nuc_choice_71(v1325_g0, v1341_g0, v1342_g0, v1343_g0, v1344_g0, v1345_g0, v1358_g0, v1360_g0, v1363_g0, v1364_g0, v1365_g0, v1233_g0) + v1323*(-v1313*(3816.3275589792611*v1312*v1325_g0 + v1315*(v1316_g0*v1374_g0 + v1373_g0*math::log(v1309)) + v1320*(v1321_g0*v1374_g0 + v1373_g0*math::log(v1298)) + 49431.413233526648*v1325_g0 + 98.337445626384849*v1358_g0 - 9.3363608541157479*v1360_g0 - 1.783649418259394*v1365_g0 - 1354334.7412883535*v1372_g0 - 2.3025850929940459*v1302*(70.138370000000009*v1210_g1*v1234_g1 - 9.4070299999999989*v1360_g0 - 0.77462909999999996*v1365_g0 - 160821.97128249999*v1368_g0/v1303_g0 - 588180.10479140002*v1372_g0)) - v1284*(4790.3210533157426*v1325_g0*v1283 + 54584.391438988954*v1325_g0 - 157.54846734442862*v1358_g0 + 198.95454259823751*v1360_g0 - 32.004783802655837*v1365_g0 - 6559375.6154640894*v1369_g0 - 2.3025850929940459*v1270*(75.773826*v1210_g1*v1235_g1*v1218_g1 - 14.509090000000008*v1358_g0 - 13.899501000000001*v1365_g0 - 331159.79815649998*v1368_g0/v1272_g0 - 2848700.6345267999*v1369_g0) + v1286*(v1370_g0*math::log(v1280) + v1371_g0*v1287_g0) + v1292*(v1370_g0*math::log(v1264) + v1371_g0*v1293_g0))) + v1333_g0*v1361*(1.3296555000000001e-10*math::exp((-0.90150700000000006)*math::log(math::abs(T))) + 2.466314622e-10*math::exp((-0.44389999999999996)*math::log(math::abs(T))) + 8.1647792100000001e-16*math::exp((1.1825999999999999)*math::log(math::abs(T)))) + v1357*(-2.5000000000000002e-32*v1355_g0 - 3.75e-33*v1356_g0) + v1357*(1.2500000000000001e-32*v1355_g0 + 1.875e-33*v1356_g0) - v1256*(69500.0*v1325_g0*v1243 - v1366*v1241) - v1256*(v1366*v1259 + 12307692.307692308*v1246_g0*v1253*(-4.0625000000000001e-8*v1355_g0*v1251_g0 + 0.0042250000000000005*v1367_g0*v1248_g0*v1250_g0 - 0.00048750000000000003*v1367_g0*math::exp(-58000.0*v1210_g1))*math::exp(v1249_g0)/v1248_g0) + v1334_g0*v1361*(-0.0064764051000000007*math::exp((0.04610000000000003)*math::log(math::abs(T))) - 2.7293978880000002e-10*math::exp((2.0424000000000002)*math::log(math::abs(T))) - 1.229450816e-13*math::exp((2.7740999999999998)*math::log(math::abs(T))))/((v1332_g0)*(v1332_g0)) - 2.4999999999999998e-6*xn[3]*xn[6]/math::exp((3.0/2.0)*math::log(math::abs(T))))/(xn[0]*v1351 + xn[1]*v1351 + xn[10]*v1352 + xn[11]*v1351 + xn[12]*v1351 + xn[13]*v1351 + xn[2]*v1351 + xn[3]*v1351 + xn[4]*v1351 + xn[5]*v1351 + xn[6]*v1352 + xn[7]*v1351 + xn[8]*v1352 + xn[9]*v1352);
    const Real v1288 = 1.0/v1231;
    const Real v1299_g1 = kr_t0[kzl_t0 + 355u];
    const Real v1317 = v1288*v1299_g1;
    const Real v1318 = v1316_g0*v1317;
    const Real v1322 = v1317*v1321_g0;
    const Real v1267_g1 = kr_t0[kzl_t0 + 346u];
    const Real v1289 = v1267_g1*v1288;
    const Real v1290 = v1287_g0*v1289;
    const Real v1294 = v1289*v1293_g0;
    const Real v1349 = -v1313*(7632.6551179585222*v1314*v1318 + 4.6051701859880918*v1319*v1322) - v1284*(9580.6421066314851*v1285*v1290 + 4.6051701859880918*v1291*v1294);
    const Real v1236_g1 = kr_t0[kzl_t0 + 362u];
    const Real v1257 = v1236_g1*v1256;
    const Real v1347 = 2.0*v1239*v1257;
    const Real v1258_g1 = kr_t0[kzl_t0 + 365u];
    const Real v1348 = v1347*v1258_g1;
    const Real v1329 = -v1313 - v1284;
    const Real v1211_g1 = kr_t0[kzl_t0 + 206u];
    const Real v1213_g1 = kr_t0[kzl_t0 + 208u];
    const Real v1214_g1 = kr_t0[kzl_t0 + 207u];
    const Real v1216_g1 = kr_t0[kzl_t0 + 209u];
    const Real v1226_g0 = kr_t0[kzl_t0 + 414u];
    const Real v1340_g1 = kr_t0[kzl_t0 + 205u];
    const Real v1346_g1 = kr_t0[kzl_t0 + 290u];
    const Real v1336_g0 = kr_t0[kzl_t0 + 415u];
    const Real v1338_g0 = kr_t0[kzl_t0 + 416u];
    out[matrix_index(8, 8)] = -xn[0]*v1211_g1*v1213_g1 - xn[0]*v1214_g1*v1216_g1 - xn[1]*v1226_g0 + xn[2]*xn[8]*v1349 + xn[2]*v1329 + xn[4]*v1340_g1 - xn[5]*v1346_g1 - 2*xn[8]*v1255 + v1330*v1336_g0 + v1330*v1338_g0 - v1348 + 2.0*v1230_g0*v1236_g1*v1239*v1242*v1245*v1254;
    const Real v1324 = v1323*(-v1313*(v1315*v1318 + v1320*v1322) - v1284*(v1286*v1290 + v1292*v1294)) + v1241*v1257 - v1257*v1259;
    const Real v1337 = 2*v1323;
    const Real v1328_g0 = kr_t0[kzl_t0 + 417u];
    const Real v1335_g0 = kr_t0[kzl_t0 + 418u];
    const Real v1331_g0 = kr_t0[kzl_t0 + 419u];
    const Real v1247_g0 = kr_t0[kzl_t0 + 420u];
    out[matrix_index(8, 2)] = xn[10]*v1328_g0 + xn[3]*v1335_g0 + 6.0e-10*xn[6] + xn[8]*v1329 + v1324 + 3*v1330*(6.0000000000000001e-32*v1331_g0 + 2.0000000000000002e-31*v1247_g0) + v1336_g0*v1337 + v1337*v1338_g0;
    const Real v1339_g0 = kr_t0[kzl_t0 + 421u];
    out[matrix_index(8, 6)] = 6.0e-10*xn[2] + xn[3]*v1339_g0 + v1323*v1349 + v1347*v1230_g0 - v1348;
    out[matrix_index(8, 3)] = xn[2]*v1335_g0 + xn[6]*v1339_g0 + v1324;
    const Real v1228_g0 = kr_t0[kzl_t0 + 422u];
    out[matrix_index(8, 10)] = xn[1]*v1228_g0 + xn[2]*v1328_g0 + v1324;
    out[matrix_index(8, 1)] = xn[10]*v1228_g0 - xn[8]*v1226_g0 + v1324;
    out[matrix_index(8, 9)] = v1324;
    const Real v792 = xn[1] + xn[10] + xn[2] + xn[3] + 2.0*xn[6] + 2.0*xn[8] + xn[9];
    const Real v796_g0 = kr_t0[kzl_t0 + 423u];
    const Real v797 = v792*v796_g0;
    const Real v800_g0 = kr_t0[kzl_t0 + 424u];
    const Real v801 = math::exp((v800_g0)*math::log(math::abs(v797)));
    const Real v802 = v801 + 1.0;
    const Real v807_g0 = kr_t0[kzl_t0 + 425u];
    const Real v821 = v801*v807_g0/((v802)*(v802));
    const Real v822 = 2.3025850929940459*v821;
    const Real v809_g0 = kr_t0[kzl_t0 + 426u];
    const Real v810 = v792*v809_g0;
    const Real v811 = math::exp((v800_g0)*math::log(math::abs(v810)));
    const Real v812 = v811 + 1.0;
    const Real v729_g0 = kr_t0[kzl_t0 + 427u];
    const Real v815 = v729_g0*v811/((v812)*(v812));
    const Real v816 = 4790.3210533157426*v815;
    const Real v803 = 1.0/v802;
    const Real v813 = 1.0/v812;
    const Real v823_g0 = kr_t0[kzl_t0 + 428u];
    const Real v956_g0 = kr_t0[kzl_t0 + 429u];
    const Real v955_g0 = kr_t0[kzl_t0 + 430u];
    const Real v891_g0 = kr_t0[kzl_t0 + 431u];
    const Real v938_g0 = kr_t0[kzl_t0 + 432u];
    const Real v940_g0 = kr_t0[kzl_t0 + 433u];
    const Real v948_g0 = kr_t0[kzl_t0 + 434u];
    const Real v954_g0 = kr_t0[kzl_t0 + 435u];
    const Real v953_g0 = kr_t0[kzl_t0 + 436u];
    const Real v804_g0 = kr_t0[kzl_t0 + 437u];
    const Real v770_g0 = kr_t0[kzl_t0 + 438u];
    const Real v785_g0 = kr_t0[kzl_t0 + 439u];
    const Real v817_g0 = kr_t0[kzl_t0 + 440u];
    const Real v957 = v822*(v823_g0*v956_g0 + v955_g0*math::log(v797)) + 4790.3210533157426*v891_g0*v813 + 54584.391438988954*v891_g0 - 157.54846734442862*v938_g0 - 32.004783802655837*v940_g0 + 198.95454259823751*v948_g0 - 6559375.6154640894*v954_g0 - 2.3025850929940459*v803*(-14.509090000000008*v938_g0 - 13.899501000000001*v940_g0 - 331159.79815649998*v953_g0/v804_g0 - 2848700.6345267999*v954_g0 + 75.773826*v770_g0*v785_g0*v729_g0) + v816*(v955_g0*math::log(v810) + v956_g0*v817_g0);
    const Real v828_g0 = kr_t0[kzl_t0 + 441u];
    const Real v829 = v828_g0*v792;
    const Real v830_g0 = kr_t0[kzl_t0 + 442u];
    const Real v831 = math::exp((v830_g0)*math::log(math::abs(v829)));
    const Real v832 = v831 + 1.0;
    const Real v837_g0 = kr_t0[kzl_t0 + 443u];
    const Real v850 = v831*v837_g0/((v832)*(v832));
    const Real v851 = 2.3025850929940459*v850;
    const Real v839_g0 = kr_t0[kzl_t0 + 444u];
    const Real v840 = v839_g0*v792;
    const Real v841 = math::exp((v830_g0)*math::log(math::abs(v840)));
    const Real v842 = v841 + 1.0;
    const Real v845 = v841*v729_g0/((v842)*(v842));
    const Real v846 = 3816.3275589792611*v845;
    const Real v833 = 1.0/v832;
    const Real v843 = 1.0/v842;
    const Real v958_g0 = kr_t0[kzl_t0 + 445u];
    const Real v781_g0 = kr_t0[kzl_t0 + 446u];
    const Real v834_g0 = kr_t0[kzl_t0 + 447u];
    const Real v847_g0 = kr_t0[kzl_t0 + 448u];
    const Real v960_g0 = kr_t0[kzl_t0 + 449u];
    const Real v959_g0 = kr_t0[kzl_t0 + 450u];
    const Real v852_g0 = kr_t0[kzl_t0 + 451u];
    const Real v961 = -2.3025850929940459*v833*(-0.77462909999999996*v940_g0 - 9.4070299999999989*v948_g0 - 588180.10479140002*v958_g0 + 70.138370000000009*v781_g0*v729_g0 - 160821.97128249999*v953_g0/v834_g0) + 3816.3275589792611*v843*v891_g0 + v846*(v847_g0*v960_g0 + v959_g0*math::log(v840)) + v851*(v852_g0*v960_g0 + v959_g0*math::log(v829)) + 49431.413233526648*v891_g0 + 98.337445626384849*v938_g0 - 1.783649418259394*v940_g0 - 9.3363608541157479*v948_g0 - 1354334.7412883535*v958_g0;
    const Real v771_g0 = kr_t0[kzl_t0 + 452u];
    const Real v782_g0 = kr_t0[kzl_t0 + 453u];
    const Real v806_g0 = kr_t0[kzl_t0 + 454u];
    const Real v814 = math::exp((43.20243*v771_g0*v785_g0 - 68.422430000000006*v782_g0 - 2080.4099999999999*v729_g0*v813 - 23705.700000000001*v729_g0 - v803*v807_g0 - v806_g0 - 178.4239)*math::log(math::abs(10.0)));
    const Real v911 = 3*v814;
    const Real v836_g0 = kr_t0[kzl_t0 + 455u];
    const Real v793_g0 = kr_t0[kzl_t0 + 456u];
    const Real v844 = math::exp((-v833*v837_g0 - v836_g0 - 1657.4099999999999*v843*v729_g0 + 42.707410000000003*v770_g0*v781_g0 - 21467.790000000001*v729_g0 - 2.0273650000000001*v793_g0 - 142.7664)*math::log(math::abs(10.0)));
    const Real v912 = 3*v844;
    const Real v859_g0 = kr_t0[kzl_t0 + 457u];
    const Real v860 = v859_g0*v792;
    const Real v861 = v860 + 1.0;
    const Real v872 = 1.0/v861;
    const Real v873 = 1.0*v872;
    const Real v875 = 1.0 - v873;
    const Real v867_g0 = kr_t0[kzl_t0 + 458u];
    const Real v876 = math::exp((v875)*math::log(math::abs(v867_g0)));
    const Real v871_g0 = kr_t0[kzl_t0 + 459u];
    const Real v874 = math::exp((v873)*math::log(math::abs(v871_g0)));
    const Real v877 = v874*v876;
    const Real v870 = ((xn[8])*(xn[8]));
    const Real v878 = v870*v877;
    const Real v951 = 2*v878;
    const Real v750_g0 = kr_t0[kzl_t0 + 460u];
    const Real v751 = xn[3]*v750_g0;
    const Real v752_g0 = kr_t0[kzl_t0 + 461u];
    const Real v753 = v751*v752_g0;
    const Real v754_g0 = kr_t0[kzl_t0 + 462u];
    const Real v755 = xn[2]*v754_g0;
    const Real v756_g0 = kr_t0[kzl_t0 + 463u];
    const Real v757 = v755*v756_g0;
    const Real v862 = math::exp((-2)*math::log(math::abs(v861)));
    const Real v868_g0 = kr_t0[kzl_t0 + 464u];
    const Real v869 = v862*v868_g0;
    const Real v881 = v862*math::log(v871_g0);
    const Real v858_g0 = kr_t0[kzl_t0 + 465u];
    const Real v785_g1 = kr_t0[kzl_t0 + 439u];
    const Real v729_g1 = kr_t0[kzl_t0 + 427u];
    const Real v950 = 1.0*v860*(-7.460375701300709*v858_g0*v785_g1*v729_g1 + 2.9933606208922598*v781_g0*v729_g1);
    const Real v725_g0 = kr_t0[kzl_t0 + 466u];
    const Real v726 = xn[2]*v725_g0;
    const Real v932 = xn[0]*v726;
    const Real v730_g0 = kr_t0[kzl_t0 + 467u];
    const Real v731 = xn[8]*v730_g0;
    const Real v933 = xn[0]*v731;
    const Real v733_g0 = kr_t0[kzl_t0 + 468u];
    const Real v734 = xn[8]*v733_g0;
    const Real v934 = xn[0]*v734;
    const Real v925 = 1.0/(9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9]);
    const Real v926 = 2.0860422997526066e-16*v925;
    const Real v927 = 3.4767371836380304e-16*v925;
    const Real v856 = xn[2]*xn[8];
    const Real v890 = ((xn[2])*(xn[2]));
    const Real v900_g1 = kr_t0[kzl_t0 + 289u];
    const Real v901 = xn[4]*v900_g1;
    const Real v929 = xn[2]*xn[7];
    const Real v935 = ((xn[2])*(xn[2])*(xn[2]));
    const Real v937 = xn[2]*xn[3];
    const Real v743_g0 = kr_t0[kzl_t0 + 469u];
    const Real v944_g0 = kr_t0[kzl_t0 + 470u];
    const Real v945_g0 = kr_t0[kzl_t0 + 471u];
    const Real v947_g0 = kr_t0[kzl_t0 + 472u];
    const Real v949_g0 = kr_t0[kzl_t0 + 473u];
    const Real v745_g0 = kr_t0[kzl_t0 + 474u];
    const Real v746_g0 = kr_t0[kzl_t0 + 475u];
    const Real v747_g0 = kr_t0[kzl_t0 + 476u];
    const Real v758_g0 = kr_t0[kzl_t0 + 477u];
    const Real v759_g0 = kr_t0[kzl_t0 + 478u];
    const Real v761_g0 = kr_t0[kzl_t0 + 479u];
    const Real v736_g0 = kr_t0[kzl_t0 + 480u];
    const Real v737_g0 = kr_t0[kzl_t0 + 481u];
    const Real v738_g0 = kr_t0[kzl_t0 + 482u];
    const Real v739_g0 = kr_t0[kzl_t0 + 483u];
    const Real v931_g0 = kr_t0[kzl_t0 + 484u];
    const Real v946_g0 = kr_t0[kzl_t0 + 485u];
    const Real v764_g0 = kr_t0[kzl_t0 + 486u];
    const Real v938_g1 = kr_t0[kzl_t0 + 432u];
    const Real v940_g1 = kr_t0[kzl_t0 + 433u];
    const Real v770_g1 = kr_t0[kzl_t0 + 438u];
    const Real v779_g0 = kr_t0[kzl_t0 + 487u];
    const Real v780_g0 = kr_t0[kzl_t0 + 488u];
    const Real v786_g0 = kr_t0[kzl_t0 + 489u];
    const Real v723_g0 = kr_t0[kzl_t0 + 490u];
    const Real v788_g0 = kr_t0[kzl_t0 + 491u];
    const Real v766_g0 = kr_t0[kzl_t0 + 492u];
    const Real v767_g0 = kr_t0[kzl_t0 + 493u];
    const Real v928_g0 = kr_t0[kzl_t0 + 494u];
    const Real v891_g1 = kr_t0[kzl_t0 + 431u];
    const Real v939_g0 = kr_t0[kzl_t0 + 495u];
    const Real v941_g0 = kr_t0[kzl_t0 + 496u];
    const Real v942_g0 = kr_t0[kzl_t0 + 497u];
    const Real v943_g0 = kr_t0[kzl_t0 + 498u];
    const Real v769_g0 = kr_t0[kzl_t0 + 499u];
    const Real v774_g0 = kr_t0[kzl_t0 + 500u];
    const Real v775_g0 = kr_t0[kzl_t0 + 501u];
    const Real v776_g0 = kr_t0[kzl_t0 + 502u];
    const Real v777_g0 = kr_t0[kzl_t0 + 503u];
    const Real v892_g0 = kr_t0[kzl_t0 + 504u];
    const Real v893_g0 = kr_t0[kzl_t0 + 505u];
    const Real v763_g0 = kr_t0[kzl_t0 + 506u];
    const Real v895_g0 = kr_t0[kzl_t0 + 507u];
    const Real v897_g0 = kr_t0[kzl_t0 + 508u];
    const Real v899_g0 = kr_t0[kzl_t0 + 509u];
    const Real v898_g0 = kr_t0[kzl_t0 + 510u];
    const Real v930_g0 = kr_t0[kzl_t0 + 511u];
    const Real v915_g0 = kr_t0[kzl_t0 + 512u];
    const Real v916_g0 = kr_t0[kzl_t0 + 513u];
    const Real v917_g0 = kr_t0[kzl_t0 + 514u];
    const Real v918_g0 = kr_t0[kzl_t0 + 515u];
    const Real v919_g0 = kr_t0[kzl_t0 + 516u];
    const Real v948_g1 = kr_t0[kzl_t0 + 434u];
    const Real v936_g0 = kr_t0[kzl_t0 + 517u];
    const Real v903_g0 = kr_t0[kzl_t0 + 518u];
    const Real v727_g0 = kr_t0[kzl_t0 + 519u];
    const Real v906_g0 = kr_t0[kzl_t0 + 520u];
    const Real v907_g0 = kr_t0[kzl_t0 + 521u];
    const Real v908_g0 = kr_t0[kzl_t0 + 522u];
    const Real v748_g0 = kr_t0[kzl_t0 + 523u];
    const Real v790_g0 = kr_t0[kzl_t0 + 524u];
    const Real v863_g0 = kr_t0[kzl_t0 + 525u];
    const Real v865_g0 = kr_t0[kzl_t0 + 526u];
    const Real v952_g0 = kr_t0[kzl_t0 + 527u];
    const Real v866_g0 = kr_t0[kzl_t0 + 528u];
    const Real v864_g0 = kr_t0[kzl_t0 + 529u];
    const Real v904_g0 = kr_t0[kzl_t0 + 530u];
    const Real v902_g0 = kr_t0[kzl_t0 + 531u];
    out[matrix_index(2, 14)] = (1658098.5*math::exp((-4.2799999999999994)*math::log(math::abs(T)))*v933 - 80.939999999999998*math::exp((-3.2799999999999998)*math::log(math::abs(T)))*v933 + 8.9351999999999994e-5*math::exp((-1.6499999999999999)*math::log(math::abs(T)))*v934 + 2.6534040307116389e-10*math::exp((-1.1000000000000001)*math::log(math::abs(T)))*v929 + 3.0659999999999995e-10*math::exp((-0.65000000000000002)*math::log(math::abs(T)))*v934 - 1.2992000000000002e-18*math::exp((-0.071999999999999953)*math::log(math::abs(T)))*v932 + 8.9485740404797324e-18*math::exp((1.360852208681)*math::log(math::abs(T)))*xn[0]*v751 - 7.997727392299023e-69*math::exp((12.536555999999999)*math::log(math::abs(T)))*xn[0]*v755 + xn[0]*xn[1]*jac_nuc_choice_33(T, v743_g0, v944_g0, v945_g0, v947_g0, v949_g0, v745_g0, v746_g0, v747_g0, v758_g0, v759_g0, v761_g0, v729_g1) + 2*xn[0]*xn[6]*jac_nuc_choice_34(T, v736_g0, v737_g0, v738_g0, v739_g0) - 3.5999999999999998e-8*xn[0]*xn[9]*v931_g0 + xn[0]*v753*(0.048699499187009998*v743_g0*v729_g1 - 0.56548861234079995*v944_g0 - 0.13460048125451995*v945_g0 - 0.009937168197024001*v946_g0 - 2.1050286473655998e-5*v947_g0 + 0.058916489135550004*v745_g0*v729_g1 + 0.00074779264187460007*v747_g0*v729_g1) - xn[0]*v757*(4.6894649399999997*v743_g0*v729_g1 - 11.478657500000001*v944_g0 - 1.1508224*v945_g0 - 0.015791857020000001*v946_g0 - 1.6313198799999999e-5*v947_g0 + 0.1741279885*v745_g0*v729_g1 + 0.00078368076500000001*v747_g0*v729_g1) + xn[1]*xn[13]*jac_nuc_choice_35(T, v739_g0, v764_g0) - xn[1]*xn[2]*jac_nuc_choice_36(T, v938_g1, v940_g1, v770_g1, v779_g0, v780_g0, v785_g1, v786_g0, v729_g1) + 2*xn[1]*xn[3]*jac_nuc_choice_37(v723_g0, v931_g0, v788_g0) + xn[1]*xn[5]*jac_nuc_choice_38(T, v766_g0, v767_g0) - 3.9837168574084181e-7*xn[1]*xn[7]*v928_g0 + xn[1]*xn[8]*jac_nuc_choice_39(v891_g1, v939_g0, v941_g0, v942_g0, v943_g0, v769_g0, v774_g0, v775_g0, v776_g0, v777_g0, v729_g1) - xn[10]*xn[2]*jac_nuc_choice_40(v737_g0, v891_g1, v892_g0, v893_g0) - 7.2084342424042629e-17*xn[12]*xn[2]*v763_g0 - xn[2]*xn[4]*jac_nuc_choice_41(T, v895_g0, v767_g0) - xn[2]*v901*(-0.87659414490283338*v897_g0*v899_g0*v729_g1 + 5.1485802679346868*v898_g0*math::exp((1.0)*math::log(math::abs(v770_g1)))*v729_g1 - 3.5068370966299316*v938_g1) + xn[3]*xn[5]*v930_g0 - 2.4999999999999998e-6*xn[3]*xn[6]*v931_g0 + xn[5]*xn[8]*jac_nuc_choice_42(v891_g1, v915_g0, v916_g0, v917_g0, v918_g0, v919_g0, v938_g1, v940_g1, v942_g0, v943_g0, v948_g1, v780_g0) + xn[8]*v890*(2.5000000000000002e-32*v928_g0 + 3.75e-33*v936_g0) + v856*(-v844*v961 - v957*v814) + v856*(v911*v957 + v912*v961) - v903_g0*v937*(1.3296555000000001e-10*math::exp((-0.90150700000000006)*math::log(math::abs(T))) + 2.466314622e-10*math::exp((-0.44389999999999996)*math::log(math::abs(T))) + 8.1647792100000001e-16*math::exp((1.1825999999999999)*math::log(math::abs(T)))) - v929*v930_g0 + 8.6419753086419757e-23*v932*v727_g0 + v935*(-1.0000000000000001e-31*v928_g0 - 1.5e-32*v936_g0) + v935*(3.0000000000000003e-31*v928_g0 + 4.5e-32*v936_g0) + v937*jac_nuc_choice_43(T, v906_g0, v907_g0, v908_g0, v743_g0, v944_g0, v945_g0, v949_g0, v745_g0, v746_g0, v747_g0, v748_g0, v729_g1) + v951*(v869*v950 + 12307692.307692308*v875*v790_g0*(0.0042250000000000005*v863_g0*v865_g0*v952_g0 - 4.0625000000000001e-8*v866_g0*v928_g0 - 0.00048750000000000003*v952_g0*math::exp(-58000.0*v729_g1))*math::exp(v864_g0)/v863_g0) + v951*(69500.0*v872*v891_g1 - v881*v950) - v904_g0*v937*(-0.0064764051000000007*math::exp((0.04610000000000003)*math::log(math::abs(T))) - 2.7293978880000002e-10*math::exp((2.0424000000000002)*math::log(math::abs(T))) - 1.229450816e-13*math::exp((2.7740999999999998)*math::log(math::abs(T))))/((v902_g0)*(v902_g0)))/(xn[0]*v926 + xn[1]*v926 + xn[10]*v927 + xn[11]*v926 + xn[12]*v926 + xn[13]*v926 + xn[2]*v926 + xn[3]*v926 + xn[4]*v926 + xn[5]*v926 + xn[6]*v927 + xn[7]*v926 + xn[8]*v927 + xn[9]*v927);
    const Real v818 = 1.0/v792;
    const Real v800_g1 = kr_t0[kzl_t0 + 424u];
    const Real v819 = v800_g1*v818;
    const Real v817_g1 = kr_t0[kzl_t0 + 440u];
    const Real v820 = v817_g1*v819;
    const Real v823_g1 = kr_t0[kzl_t0 + 428u];
    const Real v824 = v823_g1*v819;
    const Real v825 = v814*(v822*v824 + v816*v820);
    const Real v830_g1 = kr_t0[kzl_t0 + 442u];
    const Real v848 = v830_g1*v818;
    const Real v847_g1 = kr_t0[kzl_t0 + 448u];
    const Real v849 = v847_g1*v848;
    const Real v852_g1 = kr_t0[kzl_t0 + 451u];
    const Real v853 = v848*v852_g1;
    const Real v854 = v844*(v846*v849 + v851*v853);
    const Real v855 = -v825 - v854;
    const Real v857 = 3*v825 + 3*v854;
    const Real v859_g1 = kr_t0[kzl_t0 + 457u];
    const Real v879 = v859_g1*v878;
    const Real v880 = 2.0*v879;
    const Real v882 = v880*v881;
    const Real v910 = -v844 - v814;
    const Real v913 = v911 + v912;
    const Real v725_g1 = kr_t0[kzl_t0 + 466u];
    const Real v728_g0 = kr_t0[kzl_t0 + 532u];
    const Real v754_g1 = kr_t0[kzl_t0 + 462u];
    const Real v756_g1 = kr_t0[kzl_t0 + 463u];
    const Real v787_g0 = kr_t0[kzl_t0 + 533u];
    const Real v894_g0 = kr_t0[kzl_t0 + 534u];
    const Real v886_g1 = kr_t0[kzl_t0 + 23u];
    const Real v889_g0 = kr_t0[kzl_t0 + 535u];
    const Real v905_g0 = kr_t0[kzl_t0 + 536u];
    const Real v909_g0 = kr_t0[kzl_t0 + 537u];
    const Real v896_g1 = kr_t0[kzl_t0 + 288u];
    const Real v884_g1 = kr_t0[kzl_t0 + 135u];
    const Real v887_g2 = kr_t0[kzl_t0 + 136u];
    const Real v868_g1 = kr_t0[kzl_t0 + 464u];
    const Real v888_g0 = kr_t0[kzl_t0 + 538u];
    const Real v791_g1 = kr_t0[kzl_t0 + 134u];
    out[matrix_index(2, 2)] = -xn[0]*v725_g1*v728_g0 - xn[0]*v754_g1*v756_g1 - xn[1]*v787_g0 - xn[10]*v894_g0 - xn[12]*v886_g1 + xn[2]*xn[8]*v855 + xn[2]*xn[8]*v857 + 2*xn[2]*xn[8]*v889_g0 - xn[3]*v905_g0 + xn[3]*v909_g0 - xn[4]*v896_g1 - 1.0e-25*xn[5] - 6.0e-10*xn[6] - xn[7]*v884_g1 - xn[7]*v887_g2 + xn[8]*v910 + xn[8]*v913 - 6.3999999999999996e-10*xn[9] + 2.0*v859_g1*v862*v868_g1*v870*v874*v876 - v882 + 3*v890*(-1.8e-31*v888_g0 - 6.0000000000000005e-31*v791_g1) + 3*v890*(6.0000000000000001e-32*v888_g0 + 2.0000000000000002e-31*v791_g1) - v901;
    const Real v921 = 4.6051701859880918*v824*v821 + 9580.6421066314851*v815*v820;
    const Real v922 = 7632.6551179585222*v845*v849 + 4.6051701859880918*v850*v853;
    const Real v923 = 4.0*v879;
    const Real v924 = v856*(-v844*v922 - v921*v814) + v856*(v911*v921 + v912*v922) + v869*v923 - v881*v923;
    const Real v732_g0 = kr_t0[kzl_t0 + 539u];
    const Real v730_g1 = kr_t0[kzl_t0 + 467u];
    const Real v733_g1 = kr_t0[kzl_t0 + 468u];
    const Real v735_g0 = kr_t0[kzl_t0 + 540u];
    const Real v778_g0 = kr_t0[kzl_t0 + 541u];
    const Real v920_g1 = kr_t0[kzl_t0 + 344u];
    out[matrix_index(2, 8)] = xn[0]*v732_g0*v730_g1 + xn[0]*v733_g1*v735_g0 + xn[1]*v778_g0 + xn[2]*v910 + xn[2]*v913 + xn[5]*v920_g1 + 4*xn[8]*v877 + v889_g0*v890 + v924;
    const Real v883 = v855*v856 + v856*v857 + v869*v880 - v882;
    const Real v762_g0 = kr_t0[kzl_t0 + 542u];
    const Real v765_g1 = kr_t0[kzl_t0 + 162u];
    const Real v789_g0 = kr_t0[kzl_t0 + 543u];
    const Real v768_g1 = kr_t0[kzl_t0 + 343u];
    out[matrix_index(2, 1)] = xn[0]*v762_g0 + xn[13]*v765_g1 - xn[2]*v787_g0 + xn[3]*v789_g0 + xn[5]*v768_g1 + 7.9674337148168363e-7*xn[7]*v791_g1 + xn[8]*v778_g0 + v883;
    const Real v740_g0 = kr_t0[kzl_t0 + 544u];
    const Real v724_g0 = kr_t0[kzl_t0 + 545u];
    out[matrix_index(2, 0)] = xn[1]*v762_g0 + xn[6]*v740_g0 + xn[9]*v724_g0 + v732_g0*v731 + v734*v735_g0 + v753 - v757 - v726*v728_g0;
    const Real v750_g1 = kr_t0[kzl_t0 + 460u];
    const Real v752_g1 = kr_t0[kzl_t0 + 461u];
    const Real v914_g0 = kr_t0[kzl_t0 + 546u];
    out[matrix_index(2, 3)] = xn[0]*v750_g1*v752_g1 + xn[1]*v789_g0 - xn[2]*v905_g0 + xn[2]*v909_g0 + xn[5]*v887_g2 + xn[6]*v914_g0 + v883;
    out[matrix_index(2, 6)] = xn[0]*v740_g0 - 6.0e-10*xn[2] + xn[3]*v914_g0 + v924;
    out[matrix_index(2, 9)] = xn[0]*v724_g0 - 6.3999999999999996e-10*xn[2] + v883;
    out[matrix_index(2, 10)] = -xn[2]*v894_g0 + v883;
    const Real v1853_g0 = kr_t0[kzl_t0 + 547u];
    const Real v1854 = xn[8]*v1853_g0;
    const Real v1871_g0 = kr_t0[kzl_t0 + 548u];
    const Real v1872 = xn[1]*v1871_g0;
    const Real v1867_g0 = kr_t0[kzl_t0 + 549u];
    const Real v1868 = xn[13]*v1867_g0;
    const Real v1937 = v1868 + v1872;
    const Real v1940 = v1854 + v1937;
    const Real v1886_g0 = kr_t0[kzl_t0 + 550u];
    const Real v1887 = xn[2]*v1886_g0;
    const Real v1945 = v1887 + v1940;
    const Real v1894_g0 = kr_t0[kzl_t0 + 551u];
    const Real v1895 = xn[0]*v1894_g0;
    const Real v1843_g0 = kr_t0[kzl_t0 + 552u];
    const Real v1925 = v1843_g0*v1895;
    const Real v1946 = v1925 + v1945 >= 1.0e-99;
    const Real v1899_g0 = kr_t0[kzl_t0 + 553u];
    const Real v1900 = xn[0]*v1899_g0;
    const Real v1947 = v1843_g0*v1900;
    const Real v1955 = v1945 + v1947 >= 1.0e-99;
    const Real v1873_g0 = kr_t0[kzl_t0 + 554u];
    const Real v1874 = xn[2]*v1873_g0;
    const Real v1926 = v1854 + v1874;
    const Real v1938 = v1926 + v1937;
    const Real v1939 = v1925 + v1938 >= 1.0e-99;
    const Real v1876_g0 = kr_t0[kzl_t0 + 555u];
    const Real v1877 = xn[2]*v1876_g0;
    const Real v1941 = v1877 + v1940;
    const Real v1942 = v1925 + v1941 >= 1.0e-99;
    const Real v1879_g0 = kr_t0[kzl_t0 + 556u];
    const Real v1880 = xn[2]*v1879_g0;
    const Real v1943 = v1880 + v1940;
    const Real v1944 = v1925 + v1943 >= 1.0e-99;
    const Real v1952 = v1938 + v1947 >= 1.0e-99;
    const Real v1953 = v1941 + v1947 >= 1.0e-99;
    const Real v1954 = v1943 + v1947 >= 1.0e-99;
    const Real v1857_g0 = kr_t0[kzl_t0 + 557u];
    const Real v1858 = xn[13]*v1857_g0;
    const Real v1869_g0 = kr_t0[kzl_t0 + 558u];
    const Real v1870 = xn[1]*v1869_g0;
    const Real v1927 = v1858 + v1870;
    const Real v1930 = v1854 + v1927;
    const Real v1935 = v1887 + v1930;
    const Real v1936 = v1925 + v1935 >= 1.0e-99;
    const Real v1951 = v1935 + v1947 >= 1.0e-99;
    const Real v1928 = v1926 + v1927;
    const Real v1929 = v1925 + v1928 >= 1.0e-99;
    const Real v1931 = v1877 + v1930;
    const Real v1932 = v1925 + v1931 >= 1.0e-99;
    const Real v1933 = v1880 + v1930;
    const Real v1934 = v1925 + v1933 >= 1.0e-99;
    const Real v1948 = v1928 + v1947 >= 1.0e-99;
    const Real v1949 = v1931 + v1947 >= 1.0e-99;
    const Real v1950 = v1933 + v1947 >= 1.0e-99;
    const Real v1963 = v1945 >= 1.0e-99;
    const Real v1960 = v1938 >= 1.0e-99;
    const Real v1962 = v1943 >= 1.0e-99;
    const Real v1959 = v1935 >= 1.0e-99;
    const Real v1961 = v1941 >= 1.0e-99;
    const Real v1956 = v1928 >= 1.0e-99;
    const Real v1957 = v1931 >= 1.0e-99;
    const Real v1958 = v1933 >= 1.0e-99;
    const Real v1859_g0 = kr_t0[kzl_t0 + 559u];
    const Real v1860_g0 = kr_t0[kzl_t0 + 560u];
    const Real v1861_g0 = kr_t0[kzl_t0 + 561u];
    const Real v1875_g0 = kr_t0[kzl_t0 + 562u];
    const Real v1878_g0 = kr_t0[kzl_t0 + 563u];
    const Real v1881_g0 = kr_t0[kzl_t0 + 564u];
    const Real v1896_g0 = kr_t0[kzl_t0 + 565u];
    const Real v1897_g0 = kr_t0[kzl_t0 + 566u];
    const Real v1898_g0 = kr_t0[kzl_t0 + 567u];
    const Real v1901_g0 = kr_t0[kzl_t0 + 568u];
    const Real v1913_g0 = kr_t0[kzl_t0 + 569u];
    const Real v1917_g0 = kr_t0[kzl_t0 + 570u];
    const Real v1924_g0 = kr_t0[kzl_t0 + 571u];
    const Real v1964_g0 = kr_t0[kzl_t0 + 572u];
    const Real v1965_g0 = kr_t0[kzl_t0 + 573u];
    const Real v1966 = jac_nuc_choice_180(v1859_g0, v1860_g0, v1861_g0, v1875_g0, v1878_g0, v1881_g0, v1896_g0, v1897_g0, v1898_g0, v1901_g0, v1913_g0, v1917_g0, v1924_g0, v1929, v1932, v1934, v1936, v1939, v1942, v1944, v1946, v1948, v1949, v1950, v1951, v1952, v1953, v1954, v1955, v1956, v1957, v1958, v1959, v1960, v1961, v1962, v1963, v1964_g0, v1965_g0);
    const Real v1843_g1 = kr_t0[kzl_t0 + 552u];
    const Real v1902 = v1843_g1*jac_nuc_choice_116(v1895, v1898_g0, v1900, v1901_g0);
    const Real v1903 = v1854 + v1902 + jac_nuc_choice_117(v1858, v1861_g0, v1868) + jac_nuc_choice_118(v1861_g0, v1870, v1872) + jac_nuc_choice_121(v1874, v1875_g0, v1877, v1878_g0, v1880, v1881_g0, v1887);
    const Real v1920_g0 = kr_t0[kzl_t0 + 574u];
    const Real v1921 = v1903 + v1920_g0;
    const Real v1922 = 1.0/v1921;
    const Real v1923 = v1920_g0*v1922;
    const Real v1986 = v1903*v1920_g0/((v1921)*(v1921));
    const Real v2116_g0 = kr_t0[kzl_t0 + 575u];
    const Real v2127_g0 = kr_t0[kzl_t0 + 576u];
    const Real v2128_g0 = kr_t0[kzl_t0 + 577u];
    const Real v2129_g0 = kr_t0[kzl_t0 + 578u];
    const Real v2130_g0 = kr_t0[kzl_t0 + 579u];
    const Real v2131_g0 = kr_t0[kzl_t0 + 580u];
    const Real v2132_g0 = kr_t0[kzl_t0 + 581u];
    const Real v2133_g0 = kr_t0[kzl_t0 + 582u];
    const Real v2135_g0 = kr_t0[kzl_t0 + 583u];
    const Real v1705_g0 = kr_t0[kzl_t0 + 584u];
    const Real v1612_g0 = kr_t0[kzl_t0 + 585u];
    const Real v1738_g0 = kr_t0[kzl_t0 + 586u];
    const Real v1850_g0 = kr_t0[kzl_t0 + 587u];
    const Real v2136_g0 = kr_t0[kzl_t0 + 588u];
    const Real v1882_g0 = kr_t0[kzl_t0 + 589u];
    const Real v1883_g0 = kr_t0[kzl_t0 + 590u];
    const Real v1884_g0 = kr_t0[kzl_t0 + 591u];
    const Real v1885_g0 = kr_t0[kzl_t0 + 592u];
    const Real v2137 = v1843_g1*jac_nuc_choice_241(v1895, v1898_g0, v1900, v1901_g0, v2116_g0, v2127_g0, v2128_g0, v2129_g0, v2130_g0, v2131_g0, v2132_g0, v2133_g0) + v1854*v2135_g0 + v1854*(4.8223900769399082*v1705_g0*v1612_g0 + 3.0182298984217999*v1738_g0*v1850_g0*v1612_g0 - 3.5529549287336835*v2127_g0 - 0.3872755400043702*v2131_g0 - 1.3735579540080118*v2132_g0) + v1902*v2135_g0 + jac_nuc_choice_242(v1858, v1861_g0, v1868, v2116_g0, v2127_g0, v2131_g0, v2132_g0, v2133_g0, v2136_g0) + jac_nuc_choice_243(v1861_g0, v1870, v1872, v2116_g0, v2127_g0, v2131_g0, v2132_g0, v2133_g0, v2136_g0) + jac_nuc_choice_246(v1705_g0, v1874, v1875_g0, v1877, v1878_g0, v1880, v1881_g0, v1882_g0, v1883_g0, v1884_g0, v1885_g0, v1887, v2116_g0, v2127_g0, v2131_g0, v2132_g0, v2133_g0, v1612_g0);
    const Real v1777 = xn[2] <= 0.01;
    const Real v1778 = jac_nuc_choice_93(v1777, xn);
    const Real v1779 = math::log(jac_nuc_choice_95(v1777, v1778, xn));
    const Real v1781 = ((v1779)*(v1779)*(v1779));
    const Real v1784_g0 = kr_t0[kzl_t0 + 593u];
    const Real v1800 = v1781*v1784_g0;
    const Real v1782 = ((v1779)*(v1779));
    const Real v1773_g0 = kr_t0[kzl_t0 + 594u];
    const Real v1802 = v1773_g0*v1782;
    const Real v1780 = ((((v1779)*(v1779)))*(((v1779)*(v1779))));
    const Real v1785 = v1780*v1784_g0;
    const Real v1787 = v1773_g0*v1781;
    const Real v1789 = v1738_g0*v1782;
    const Real v1772_g0 = kr_t0[kzl_t0 + 595u];
    const Real v1706_g0 = kr_t0[kzl_t0 + 596u];
    const Real v1776_g0 = kr_t0[kzl_t0 + 597u];
    const Real v1775_g0 = kr_t0[kzl_t0 + 598u];
    const Real v1774_g0 = kr_t0[kzl_t0 + 599u];
    const Real v1783_g0 = kr_t0[kzl_t0 + 600u];
    const Real v1786_g0 = kr_t0[kzl_t0 + 601u];
    const Real v1788_g0 = kr_t0[kzl_t0 + 602u];
    const Real v1790_g0 = kr_t0[kzl_t0 + 603u];
    const Real v1792_g0 = kr_t0[kzl_t0 + 604u];
    const Real v1794_g0 = kr_t0[kzl_t0 + 605u];
    const Real v1797_g0 = kr_t0[kzl_t0 + 606u];
    const Real v1795_g0 = kr_t0[kzl_t0 + 607u];
    const Real v1798_g0 = kr_t0[kzl_t0 + 608u];
    const Real v1799_g0 = kr_t0[kzl_t0 + 609u];
    const Real v1801_g0 = kr_t0[kzl_t0 + 610u];
    const Real v1803 = math::exp((21.93385*v1705_g0*v1772_g0 + 0.92432999999999998*v1705_g0*v1779 - 10.19097*v1706_g0*v1776_g0 + 0.54962*v1706_g0*v1782 + 2.1990599999999998*v1738_g0*v1775_g0 - 0.076759999999999995*v1738_g0*v1781 - 0.0036600000000000001*v1772_g0*v1785 + 0.11864*v1772_g0*v1787 - 1.06447*v1772_g0*v1789 - 0.17333999999999999*v1773_g0*v1774_g0 + 0.0027499999999999998*v1773_g0*v1780 - 0.073660000000000003*v1776_g0*v1800 + 0.62343000000000004*v1776_g0*v1802 + 0.77951999999999999*v1779*v1783_g0 - 0.0083499999999999998*v1779*v1786_g0 + 0.11711000000000001*v1779*v1788_g0 - 0.54262999999999995*v1779*v1790_g0 + 6.1920000000000003e-5*v1780*v1792_g0 - 0.00066631000000000004*v1780*v1794_g0 + 0.0025140000000000002*v1780*v1797_g0 - 0.001482*v1781*v1795_g0 + 0.017590000000000001*v1781*v1798_g0 + 0.0106*v1782*v1799_g0 - 0.13768*v1782*v1801_g0 - 42.567880000000002)*math::log(math::abs(10.0)));
    const Real v1804 = xn[10]*v1803;
    const Real v1604 = 0.00013612213614898791*xn[0] + 0.24994102282436673*xn[1] + 0.75007714496081457*xn[10] + 0.99972775572710437*xn[11] + 0.99986387786355213*xn[12] + xn[13] + 0.25007714496081457*xn[2] + 0.25021326709726244*xn[3] + 0.49986387786355219*xn[4] + 0.5*xn[5] + 0.50001816778518127*xn[6] + 0.50013612213644787*xn[7] + 0.50015428992162914*xn[8] + 0.7499410228243667*xn[9];
    const Real v1605 = ((v1604)*(v1604));
    const Real v1606 = 1.0/v1605;
    const Real v1698 = 9.1093818800000008e-28*xn[0] + 1.6726215800000001e-24*xn[1] + 5.01956503638e-24*xn[10] + 6.6902431600000005e-24*xn[11] + 6.6911540981899994e-24*xn[12] + 6.6920650363799998e-24*xn[13] + 1.6735325181900001e-24*xn[2] + 1.6744434563800001e-24*xn[3] + 3.3451215800000003e-24*xn[4] + 3.3460325181899999e-24*xn[5] + 3.3461540981899999e-24*xn[6] + 3.3469434563800003e-24*xn[7] + 3.3470650363800003e-24*xn[8] + 5.0186540981899997e-24*xn[9];
    const Real v1811 = v1698 <= 9.9999999999999993e-41;
    const Real v1824 = jac_nuc_choice_110(v1606, v1811);
    const Real v1648 = xn[10] + xn[2] + xn[3] + xn[9];
    const Real v1806 = xn[6] + xn[8];
    const Real v1607 = xn[1] + xn[12] + xn[4];
    const Real v1807 = xn[0] + xn[11] + xn[13] + xn[5] + xn[7] + v1806 + v1607 + v1648;
    const Real v1808 = v1807 <= 9.9999999999999993e-41;
    const Real v1823 = jac_nuc_choice_109(v1807, v1808);
    const Real v1825 = math::sqrt(v1823*v1824);
    const Real v1810 = v1698 >= 0.5;
    const Real v1815_g0 = kr_t0[kzl_t0 + 611u];
    const Real v1821 = math::exp((v1815_g0*math::log(jac_nuc_choice_107(v1810, v1698)) + 2.1498900000000001)*math::log(math::abs(10.0)));
    const Real v1809 = v1698 >= 9.9999999999999998e-13;
    const Real v1822 = jac_nuc_choice_108(v1809, v1821);
    const Real v1826 = v1822*v1825;
    const Real v1812_g0 = kr_t0[kzl_t0 + 612u];
    const Real v1813 = v1812_g0*v1698;
    const Real v1827 = v1813*v1826;
    const Real v1828 = v1827 + 2.1986273043946046e-36;
    const Real v1705_g1 = kr_t0[kzl_t0 + 584u];
    const Real v1814 = math::exp((2.1498900000000001 - 0.69317629274152892*v1705_g1)*math::log(math::abs(10.0)))*v1813;
    const Real v1816 = math::exp((v1815_g0*math::log(v1698) + 2.1498900000000001)*math::log(math::abs(10.0)))*v1813;
    const Real v1817 = 1.0/math::fabs(v1604);
    const Real v1818 = math::sqrt(v1807);
    const Real v1819 = math::sqrt(v1606*v1807);
    const Real v1820 = jac_nuc_choice_106(v1808, v1809, v1810, v1811, v1814, v1816, v1817, v1818, v1819);
    const Real v1829 = jac_nuc_choice_111(v1820, v1828);
    const Real v1649 = xn[1] + 2.0*xn[6] + 2.0*xn[8] + v1648;
    const Real v1728_g0 = kr_t0[kzl_t0 + 613u];
    const Real v1729 = v1728_g0*v1649;
    const Real v1732_g0 = kr_t0[kzl_t0 + 614u];
    const Real v1733 = math::exp((v1732_g0)*math::log(math::abs(v1729)));
    const Real v1734 = v1733 + 1.0;
    const Real v1735 = 1.0/v1734;
    const Real v1744_g0 = kr_t0[kzl_t0 + 615u];
    const Real v1745 = v1744_g0*v1649;
    const Real v1746 = math::exp((v1732_g0)*math::log(math::abs(v1745)));
    const Real v1747 = v1746 + 1.0;
    const Real v1748 = 1.0/v1747;
    const Real v1706_g1 = kr_t0[kzl_t0 + 596u];
    const Real v1724_g0 = kr_t0[kzl_t0 + 616u];
    const Real v1742_g0 = kr_t0[kzl_t0 + 617u];
    const Real v1736_g0 = kr_t0[kzl_t0 + 618u];
    const Real v1741_g0 = kr_t0[kzl_t0 + 619u];
    const Real v1612_g1 = kr_t0[kzl_t0 + 585u];
    const Real v1749 = math::exp((43.20243*v1706_g1*v1724_g0 - v1735*v1742_g0 - 68.422430000000006*v1736_g0 - v1741_g0 - 2080.4099999999999*v1748*v1612_g1 - 23705.700000000001*v1612_g1 - 178.4239)*math::log(math::abs(10.0)));
    const Real v1763_g0 = kr_t0[kzl_t0 + 620u];
    const Real v1764 = v1763_g0*v1649;
    const Real v1754_g0 = kr_t0[kzl_t0 + 621u];
    const Real v1765 = math::exp((v1754_g0)*math::log(math::abs(v1764)));
    const Real v1766 = v1765 + 1.0;
    const Real v1767 = 1.0/v1766;
    const Real v1752_g0 = kr_t0[kzl_t0 + 622u];
    const Real v1753 = v1752_g0*v1649;
    const Real v1755 = math::exp((v1754_g0)*math::log(math::abs(v1753)));
    const Real v1756 = v1755 + 1.0;
    const Real v1757 = 1.0/v1756;
    const Real v1723_g0 = kr_t0[kzl_t0 + 623u];
    const Real v1725_g0 = kr_t0[kzl_t0 + 624u];
    const Real v1761_g0 = kr_t0[kzl_t0 + 625u];
    const Real v1760_g0 = kr_t0[kzl_t0 + 626u];
    const Real v1768 = math::exp((42.707410000000003*v1705_g1*v1723_g0 - 2.0273650000000001*v1725_g0 - v1757*v1761_g0 - v1760_g0 - 1657.4099999999999*v1767*v1612_g1 - 21467.790000000001*v1612_g1 - 142.7664)*math::log(math::abs(10.0)));
    const Real v1982 = 483396202.36294854/((v1828)*(v1828)*(v1828));
    const Real v1707_g0 = kr_t0[kzl_t0 + 627u];
    const Real v1708 = v1707_g0*v1649;
    const Real v1709 = v1708 + 1.0;
    const Real v1710 = 1.0/v1709;
    const Real v1711 = 1.0*v1710;
    const Real v1718 = 1.0 - v1711;
    const Real v1717_g0 = kr_t0[kzl_t0 + 628u];
    const Real v1719 = math::exp((v1718)*math::log(math::abs(v1717_g0)));
    const Real v1702_g0 = kr_t0[kzl_t0 + 629u];
    const Real v1712 = math::exp((v1711)*math::log(math::abs(v1702_g0)));
    const Real v1720 = v1712*v1719;
    const Real v1701 = ((xn[8])*(xn[8]));
    const Real v1721 = v1720*v1701;
    const Real v1722 = 7.1777505408000004e-12*v1721;
    const Real v1996 = v1733*v1742_g0/((v1734)*(v1734));
    const Real v1997 = 2.3025850929940459*v1996;
    const Real v2005 = v1755*v1761_g0/((v1756)*(v1756));
    const Real v2006 = 2.3025850929940459*v2005;
    const Real v1991 = v1746*v1612_g1/((v1747)*(v1747));
    const Real v1992 = 4790.3210533157426*v1991;
    const Real v2000 = v1765*v1612_g1/((v1766)*(v1766));
    const Real v2001 = 3816.3275589792611*v2000;
    const Real v1655_g0 = kr_t0[kzl_t0 + 630u];
    const Real v1656 = xn[8]*v1655_g0;
    const Real v1653_g0 = kr_t0[kzl_t0 + 631u];
    const Real v1654 = xn[2]*v1653_g0;
    const Real v1657 = 1.0/(1.6000000000000001*v1654 + 1.3999999999999999*v1656);
    const Real v1651_g0 = kr_t0[kzl_t0 + 632u];
    const Real v1658 = v1651_g0*v1657;
    const Real v1650 = 1.0/v1649;
    const Real v1659 = v1650*v1658;
    const Real v2018 = math::exp((-2)*math::log(math::abs(v1659 + 9.9999999999999995e-7)));
    const Real v2035 = v1650*v1651_g0/((v1654 + 0.87499999999999989*v1656)*(v1654 + 0.87499999999999989*v1656));
    const Real v1642_g0 = kr_t0[kzl_t0 + 633u];
    const Real v1909_g0 = kr_t0[kzl_t0 + 634u];
    const Real v2107 = v2018*(500000.0*v1642_g0*v1650*v1657 - 390624.99999999994*v2035*(-512000.0*v1909_g0*v1654 - 0.011666666666666665*v1656/((0.00083333333333333339*T + 1)*(0.00083333333333333339*T + 1))));
    const Real v2012 = math::exp((-2)*math::log(math::abs(v1709)));
    const Real v2013 = v2012*math::log(v1717_g0);
    const Real v1660 = 1.0/(1000000.0*v1659 + 1.0);
    const Real v2043 = 5.6556829037999995e-12*v1660;
    const Real v1694_g0 = kr_t0[kzl_t0 + 635u];
    const Real v2051 = xn[2]*v2043*v1694_g0;
    const Real v2016_g0 = kr_t0[kzl_t0 + 636u];
    const Real v2049 = v2012*v2016_g0;
    const Real v1830_g0 = kr_t0[kzl_t0 + 637u];
    const Real v1831 = v1822*v1830_g0;
    const Real v1833 = 0.00022681492*v1698;
    const Real v1983 = v1831*v1833;
    const Real v1626_g0 = kr_t0[kzl_t0 + 638u];
    const Real v1671 = xn[12]*v1626_g0;
    const Real v1670 = ((xn[0])*(xn[0]));
    const Real v1672 = v1670*v1671;
    const Real v2082_g0 = kr_t0[kzl_t0 + 639u];
    const Real v2103 = v2082_g0*v1672;
    const Real v1705_g2 = kr_t0[kzl_t0 + 584u];
    const Real v1612_g2 = kr_t0[kzl_t0 + 585u];
    const Real v2108_g0 = kr_t0[kzl_t0 + 640u];
    const Real v1703_g0 = kr_t0[kzl_t0 + 641u];
    const Real v2109 = 1.0*v1708*(2.9933606208922598*v1705_g2*v1612_g2 - 7.460375701300709*v2108_g0*v1703_g0);
    const Real v2086_g0 = kr_t0[kzl_t0 + 642u];
    const Real v2087 = xn[0]*v2086_g0;
    const Real v1635_g0 = kr_t0[kzl_t0 + 643u];
    const Real v2088 = v1635_g0*v2087;
    const Real v1661 = xn[0]*v1626_g0;
    const Real v1624_g0 = kr_t0[kzl_t0 + 644u];
    const Real v1679 = v1624_g0*v1661;
    const Real v2104 = v2082_g0*v1679;
    const Real v2093 = v2086_g0*v1661;
    const Real v1627 = xn[2]*v1626_g0;
    const Real v2097 = v1627*v2082_g0;
    const Real v1620_g0 = kr_t0[kzl_t0 + 645u];
    const Real v2098 = v1672*jac_nuc_choice_233(T, v1620_g0);
    const Real v1641 = xn[0]*xn[12];
    const Real v2095_g0 = kr_t0[kzl_t0 + 646u];
    const Real v2101 = v1641*v2095_g0;
    const Real v2102 = xn[12]*v2095_g0*v1670;
    const Real v1835 = 1.2500000000000001e-10*xn[0] + 1.2500000000000001e-10*xn[1] + 1.2500000000000001e-10*xn[10] + 1.2500000000000001e-10*xn[11] + 1.2500000000000001e-10*xn[12] + 1.2500000000000001e-10*xn[13] + 1.2500000000000001e-10*xn[2] + 1.2500000000000001e-10*xn[3] + 1.2500000000000001e-10*xn[4] + 1.2500000000000001e-10*xn[5] + 1.2500000000000001e-10*xn[6] + 1.2500000000000001e-10*xn[7] + 1.2500000000000001e-10*xn[8] + 1.2500000000000001e-10*xn[9] <= 9.9999999999999993e-41;
    const Real v1836 = 28601.610899577994*math::exp((-0.45000000000000001)*math::log(math::abs(v1807)));
    const Real v1837 = jac_nuc_choice_112(v1835, v1836);
    const Real v1838 = jac_nuc_choice_114(v1835, v1836, v1837);
    const Real v1987 = xn[8]*v1838;
    const Real v2096 = xn[2]*v2095_g0;
    const Real v1684_g0 = kr_t0[kzl_t0 + 647u];
    const Real v2106 = xn[0]*v1684_g0;
    const Real v2110_g0 = kr_t0[kzl_t0 + 648u];
    const Real v2112 = v1780*v2110_g0;
    const Real v2114 = v1781*v2110_g0;
    const Real v2115 = v1782*v2110_g0;
    const Real v1643_g0 = kr_t0[kzl_t0 + 649u];
    const Real v2083 = v1641*v1643_g0*v2082_g0;
    const Real v2090 = xn[0]*v1624_g0;
    const Real v1633_g0 = kr_t0[kzl_t0 + 650u];
    const Real v2091 = v1633_g0*v2090*jac_nuc_choice_232(T, v1620_g0);
    const Real v1634_g0 = kr_t0[kzl_t0 + 651u];
    const Real v1632_g0 = kr_t0[kzl_t0 + 652u];
    const Real v2099 = v1634_g0*v2090*jac_nuc_choice_234(T, v1620_g0)/((v1632_g0)*(v1632_g0));
    const Real v2113 = v1779*v2110_g0;
    const Real v1970 = xn[12]*v1661;
    const Real v1677_g0 = kr_t0[kzl_t0 + 653u];
    const Real v1678 = xn[12]*v1677_g0;
    const Real v1681_g0 = kr_t0[kzl_t0 + 654u];
    const Real v1682 = xn[13]*v1681_g0;
    const Real v1699 = 1.0/v1698;
    const Real v1700 = math::exp((-1.0/2.0)*math::log(math::abs(v1699)));
    const Real v1696_g0 = kr_t0[kzl_t0 + 655u];
    const Real v2020 = v1696_g0*v1700;
    const Real v1706_g2 = kr_t0[kzl_t0 + 596u];
    const Real v2025 = v1706_g2*v1779;
    const Real v1738_g1 = kr_t0[kzl_t0 + 586u];
    const Real v2027 = v1738_g1*v1779;
    const Real v1687 = ((xn[2])*(xn[2])*(xn[2]));
    const Real v1689_g0 = kr_t0[kzl_t0 + 656u];
    const Real v2038 = v1687*v1689_g0;
    const Real v1690 = ((xn[2])*(xn[2]));
    const Real v1691_g0 = kr_t0[kzl_t0 + 657u];
    const Real v2045 = xn[8]*v1690*v1691_g0;
    const Real v1692_g0 = kr_t0[kzl_t0 + 658u];
    const Real v1694_g1 = kr_t0[kzl_t0 + 635u];
    const Real v2042 = xn[3]*v1692_g0*v1694_g1;
    const Real v2046 = xn[2]*v2042;
    const Real v2047 = 5.6556829037999991e-24*v2046;
    const Real v1629_g0 = kr_t0[kzl_t0 + 659u];
    const Real v2094 = xn[0]*v1629_g0;
    const Real v1680_g0 = kr_t0[kzl_t0 + 660u];
    const Real v2105 = xn[13]*v1680_g0;
    const Real v1613_g0 = kr_t0[kzl_t0 + 661u];
    const Real v1614 = xn[8]*v1613_g0;
    const Real v2081 = xn[0]*v1614;
    const Real v2079 = 2.0860422997526066e-16*v1699;
    const Real v2080 = 3.4767371836380304e-16*v1699;
    const Real v1608 = 4.0*xn[11] + v1607;
    const Real v2009 = 7.1777505408000004e-12*xn[8];
    const Real v2010 = xn[2]*v2009;
    const Real v2040 = xn[2]*xn[6];
    const Real v2041 = 1.7591897530800001e-33*v2040;
    const Real v2084 = 3.4635323838154264e-26*xn[1];
    const Real v2089 = 1.3854129535261706e-25*xn[11];
    const Real v1697 = 1.3806479999999999e-16*xn[0] + 1.3806479999999999e-16*xn[1] + 1.3806479999999999e-16*xn[10] + 1.3806479999999999e-16*xn[11] + 1.3806479999999999e-16*xn[12] + 1.3806479999999999e-16*xn[13] + 1.3806479999999999e-16*xn[2] + 1.3806479999999999e-16*xn[3] + 1.3806479999999999e-16*xn[4] + 1.3806479999999999e-16*xn[5] + 1.3806479999999999e-16*xn[6] + 1.3806479999999999e-16*xn[7] + 1.3806479999999999e-16*xn[8] + 1.3806479999999999e-16*xn[9];
    const Real v1618_g0 = kr_t0[kzl_t0 + 662u];
    const Real v1651_g1 = kr_t0[kzl_t0 + 632u];
    const Real v1676_g0 = kr_t0[kzl_t0 + 663u];
    const Real v1693_g0 = kr_t0[kzl_t0 + 664u];
    const Real v1652_g0 = kr_t0[kzl_t0 + 665u];
    const Real v1609_g0 = kr_t0[kzl_t0 + 666u];
    const Real v1713_g0 = kr_t0[kzl_t0 + 667u];
    const Real v1715_g0 = kr_t0[kzl_t0 + 668u];
    const Real v2092_g0 = kr_t0[kzl_t0 + 669u];
    const Real v1716_g0 = kr_t0[kzl_t0 + 670u];
    const Real v1642_g1 = kr_t0[kzl_t0 + 633u];
    const Real v1612_g3 = kr_t0[kzl_t0 + 585u];
    const Real v1714_g0 = kr_t0[kzl_t0 + 671u];
    const Real v1624_g1 = kr_t0[kzl_t0 + 644u];
    const Real v1908_g0 = kr_t0[kzl_t0 + 672u];
    const Real v1683_g0 = kr_t0[kzl_t0 + 673u];
    const Real v2082_g1 = kr_t0[kzl_t0 + 639u];
    const Real v1663_g0 = kr_t0[kzl_t0 + 674u];
    const Real v1662_g0 = kr_t0[kzl_t0 + 675u];
    const Real v1620_g1 = kr_t0[kzl_t0 + 645u];
    const Real v1646_g0 = kr_t0[kzl_t0 + 676u];
    const Real v2085_g0 = kr_t0[kzl_t0 + 677u];
    const Real v1695_g0 = kr_t0[kzl_t0 + 678u];
    const Real v1644_g0 = kr_t0[kzl_t0 + 679u];
    const Real v1645_g0 = kr_t0[kzl_t0 + 680u];
    const Real v2100_g0 = kr_t0[kzl_t0 + 681u];
    const Real v1723_g1 = kr_t0[kzl_t0 + 623u];
    const Real v2116_g1 = kr_t0[kzl_t0 + 575u];
    const Real v2119_g0 = kr_t0[kzl_t0 + 682u];
    const Real v2121_g0 = kr_t0[kzl_t0 + 683u];
    const Real v2120_g0 = kr_t0[kzl_t0 + 684u];
    const Real v1737_g0 = kr_t0[kzl_t0 + 685u];
    const Real v1993_g0 = kr_t0[kzl_t0 + 686u];
    const Real v2123_g0 = kr_t0[kzl_t0 + 687u];
    const Real v2122_g0 = kr_t0[kzl_t0 + 688u];
    const Real v1998_g0 = kr_t0[kzl_t0 + 689u];
    const Real v2117_g0 = kr_t0[kzl_t0 + 690u];
    const Real v1705_g3 = kr_t0[kzl_t0 + 584u];
    const Real v2124_g0 = kr_t0[kzl_t0 + 691u];
    const Real v1758_g0 = kr_t0[kzl_t0 + 692u];
    const Real v2002_g0 = kr_t0[kzl_t0 + 693u];
    const Real v2126_g0 = kr_t0[kzl_t0 + 694u];
    const Real v2125_g0 = kr_t0[kzl_t0 + 695u];
    const Real v2007_g0 = kr_t0[kzl_t0 + 696u];
    const Real v1628_g0 = kr_t0[kzl_t0 + 697u];
    const Real v2095_g1 = kr_t0[kzl_t0 + 646u];
    const Real v1667_g0 = kr_t0[kzl_t0 + 698u];
    const Real v1673_g0 = kr_t0[kzl_t0 + 699u];
    const Real v1668_g0 = kr_t0[kzl_t0 + 700u];
    const Real v1674_g0 = kr_t0[kzl_t0 + 701u];
    const Real v1834_g0 = kr_t0[kzl_t0 + 702u];
    const Real v2134_g0 = kr_t0[kzl_t0 + 703u];
    const Real v1772_g1 = kr_t0[kzl_t0 + 595u];
    const Real v1775_g1 = kr_t0[kzl_t0 + 598u];
    const Real v1776_g1 = kr_t0[kzl_t0 + 597u];
    const Real v1783_g1 = kr_t0[kzl_t0 + 600u];
    const Real v1788_g1 = kr_t0[kzl_t0 + 602u];
    const Real v1790_g1 = kr_t0[kzl_t0 + 603u];
    const Real v1791_g0 = kr_t0[kzl_t0 + 704u];
    const Real v1793_g0 = kr_t0[kzl_t0 + 705u];
    const Real v1794_g1 = kr_t0[kzl_t0 + 605u];
    const Real v1796_g0 = kr_t0[kzl_t0 + 706u];
    const Real v1797_g1 = kr_t0[kzl_t0 + 606u];
    const Real v1798_g1 = kr_t0[kzl_t0 + 608u];
    const Real v1801_g1 = kr_t0[kzl_t0 + 610u];
    const Real v1805_g0 = kr_t0[kzl_t0 + 707u];
    const Real v2029_g0 = kr_t0[kzl_t0 + 708u];
    const Real v2032_g0 = kr_t0[kzl_t0 + 709u];
    const Real v2110_g1 = kr_t0[kzl_t0 + 648u];
    const Real v2111_g0 = kr_t0[kzl_t0 + 710u];
    out[matrix_index(14, 14)] = v1699*(-3.2067318316078082e-16*math::exp((-1.6499999999999999)*math::log(math::abs(T)))*v2081 - 1.10034915790464e-21*math::exp((-0.65000000000000002)*math::log(math::abs(T)))*v2081 - xn[0]*v1618_g0 - 1.0649999999999999e-27*xn[0]*v1608*v1651_g1 + 2.185341195413336e-30*xn[1]*v2099 + 8.741364781653344e-30*xn[11]*v2099 + 3.12599925e-16*xn[12]*v2104*v1676_g0 + xn[2]*xn[3]*v2043*v1692_g0*(-0.0064764051000000007*math::exp((0.04610000000000003)*math::log(math::abs(T))) - 2.7293978880000002e-10*math::exp((2.0424000000000002)*math::log(math::abs(T))) - 1.229450816e-13*math::exp((2.7740999999999998)*math::log(math::abs(T))))/((v1693_g0)*(v1693_g0)) + xn[3]*v2051*(1.3296555000000001e-10*math::exp((-0.90150700000000006)*math::log(math::abs(T))) + 2.466314622e-10*math::exp((-0.44389999999999996)*math::log(math::abs(T))) + 8.1647792100000001e-16*math::exp((1.1825999999999999)*math::log(math::abs(T)))) - v1722*(69500.0*v1710*v1652_g0 - v2049*v2109) - v1722*(v2013*v2109 + 12307692.307692308*v1718*v1609_g0*(0.0042250000000000005*v1713_g0*v1715_g0*v2092_g0 - 4.0625000000000001e-8*v1716_g0*v1642_g1 - 0.00048750000000000003*v2092_g0*math::exp(-58000.0*v1612_g3))*math::exp(v1714_g0)/v1713_g0) + 1.5653274417833479e-24*v1624_g1*v2101*v1676_g0 - 0.00090725967999999999*v1822*v1829*v1908_g0*v1698 - 1.2700000000000001e-21*v1627*v2087*v1683_g0 + 2.62395452e-11*v1970*v2082_g1*v1663_g0 - 5.5399999999999998e-17*v1970*v1662_g0*jac_nuc_choice_287(T, v1620_g1) - v1641*v1646_g0*jac_nuc_choice_288(v1620_g1, v2092_g0) - 1.5499999999999999e-26*v1641*jac_nuc_choice_289(T, v1620_g1) - v1983*jac_nuc_choice_290(v1820, v1826, v1982, v2085_g0, v1695_g0, v1698) + 5.8280000000000003e-8*v1644_g0*v1645_g0*v2083 + v2009*v1660*v1690*(-1.2500000000000001e-32*v1642_g1 - 1.875e-33*v2100_g0) - v2010*(v1749*(-2.3025850929940459*v1735*(75.773826*v1706_g2*v1723_g1*v1612_g3 - 14.509090000000008*v2116_g1 - 13.899501000000001*v2119_g0 - 2848700.6345267999*v2121_g0 - 331159.79815649998*v2120_g0/v1737_g0) + 4790.3210533157426*v1748*v1652_g0 + v1992*(v1993_g0*v2123_g0 + v2122_g0*math::log(v1745)) + v1997*(v1998_g0*v2123_g0 + v2122_g0*math::log(v1729)) + 54584.391438988954*v1652_g0 - 157.54846734442862*v2116_g1 + 198.95454259823751*v2117_g0 - 32.004783802655837*v2119_g0 - 6559375.6154640894*v2121_g0) + v1768*(-2.3025850929940459*v1757*(70.138370000000009*v1705_g3*v1612_g3 - 9.4070299999999989*v2117_g0 - 0.77462909999999996*v2119_g0 - 588180.10479140002*v2124_g0 - 160821.97128249999*v2120_g0/v1758_g0) + 3816.3275589792611*v1767*v1652_g0 + v2001*(v2002_g0*v2126_g0 + v2125_g0*math::log(v1764)) + v2006*(v2007_g0*v2126_g0 + v2125_g0*math::log(v1753)) + 49431.413233526648*v1652_g0 + 98.337445626384849*v2116_g1 - 9.3363608541157479*v2117_g0 - 1.783649418259394*v2119_g0 - 1354334.7412883535*v2124_g0)) + 0.00084373771595996178*v2020*v1697 + 7.1777505407999997e-24*v2038*v2107 + v2041*v2107 + 7.1777505407999997e-24*v2045*v2107 + v2047*v2107 + 3.4968000000000002e-9*v2083*math::exp(-564000.0*v1628_g0) - v2084*v2088 - v2084*v2091 - v2088*v2089 - v2089*v2091 + 2.9662164452379397e-24*v2090*v2095_g1*v2105 - v2093*v1678 - v2093*v1682 + 2.3717082451262844e-21*v2094*v2096 + 8.8760999999999989e-14*v2094*v2097 + 4.0160926284138423e-24*v2096*v2106 + 2.0041755700000002e-16*v2097*v2106 - 5.0099999999999997e-27*v2098*v1667_g0 - 9.1000000000000001e-27*v2098*v1673_g0 + 1.7519018237332822e-19*v2101*v1663_g0 + 1.5843011077443579e-29*v2102*v1668_g0 + 2.8776726707532255e-29*v2102*v1674_g0 + 2.7724337999999999e-22*v2103*v1668_g0 + 1.199289e-22*v2103*v1674_g0 + 2.6764460520000001e-16*v2104*v2105 + 7.1777505408000004e-12*v1660*v1687*(-1.0000000000000001e-31*v1642_g1 - 1.5e-32*v2100_g0) - jac_nuc_choice_292(v1834_g0, v1903, v1922, v1923, v1966, v1986, v1987, v2134_g0, v2137) - jac_nuc_choice_293(v1705_g3, v1772_g1, v1775_g1, v1776_g1, v1783_g1, v1785, v1787, v1788_g1, v1789, v1790_g1, v1791_g0, v1793_g0, v1794_g1, v1796_g0, v1797_g1, v1798_g1, v1800, v1801_g1, v1802, v1804, v1805_g0, v2025, v2027, v2029_g0, v2032_g0, v2110_g1, v2111_g0, v2112, v2113, v2114, v2115))/(xn[0]*v2079 + xn[1]*v2079 + xn[10]*v2080 + xn[11]*v2079 + xn[12]*v2079 + xn[13]*v2079 + xn[2]*v2079 + xn[3]*v2079 + xn[4]*v2079 + xn[5]*v2079 + xn[6]*v2080 + xn[7]*v2079 + xn[8]*v2080 + xn[9]*v2080);
    const Real v1637_g0 = kr_t0[kzl_t0 + 711u];
    const Real v1638 = xn[1]*v1637_g0;
    const Real v1639_g0 = kr_t0[kzl_t0 + 712u];
    const Real v1640 = xn[11]*v1639_g0;
    const Real v1664_g0 = kr_t0[kzl_t0 + 713u];
    const Real v1665 = v1661*v1664_g0;
    const Real v1685_g0 = kr_t0[kzl_t0 + 714u];
    const Real v1686 = v1627*v1685_g0;
    const Real v1967 = jac_nuc_choice_181(v1903, v1923, v1966);
    const Real v1968 = v1838*v1967;
    const Real v1610_g0 = kr_t0[kzl_t0 + 715u];
    const Real v1611 = v1608*v1610_g0;
    const Real v1615_g0 = kr_t0[kzl_t0 + 716u];
    const Real v1616 = v1614*v1615_g0;
    const Real v1621_g0 = kr_t0[kzl_t0 + 717u];
    const Real v1622 = xn[12]*v1621_g0;
    const Real v1630_g0 = kr_t0[kzl_t0 + 718u];
    const Real v1631 = v1627*v1630_g0;
    const Real v1769 = 7.1777505408000004e-12*v1749 + 7.1777505408000004e-12*v1768;
    const Real v1770 = xn[8]*v1769;
    const Real v1832 = v1829*v1831;
    const Real v1696_g1 = kr_t0[kzl_t0 + 655u];
    const Real v1619_g0 = kr_t0[kzl_t0 + 719u];
    const Real v1692_g1 = kr_t0[kzl_t0 + 658u];
    const Real v1694_g2 = kr_t0[kzl_t0 + 635u];
    const Real v1691_g1 = kr_t0[kzl_t0 + 657u];
    const Real v1647_g0 = kr_t0[kzl_t0 + 720u];
    const Real v1689_g1 = kr_t0[kzl_t0 + 656u];
    const Real v1669_g0 = kr_t0[kzl_t0 + 721u];
    const Real v1675_g0 = kr_t0[kzl_t0 + 722u];
    const Real v1969 = v1606*(0.00084373771595996178*T*v1696_g1*v1697*v1700 - xn[0]*v1616 - xn[0]*v1619_g0 - xn[0]*v1622 - xn[0]*v1631 - xn[0]*v1638 - xn[0]*v1640 - xn[0]*v1611 - xn[0]*v1686 - xn[12]*v1665 + 5.6556829037999995e-12*xn[2]*xn[3]*v1660*v1692_g1*v1694_g2 + 1.75918975308e-21*xn[2]*xn[6]*v1660 - xn[2]*v1770 + 7.1777505408000004e-12*xn[8]*v1660*v1690*v1691_g1 - v1722 - v1832*v1833 - v1641*v1647_g0 + 7.1777505408000004e-12*v1660*v1687*v1689_g1 - v1669_g0*v1672 - v1672*v1675_g0 - v1678*v1679 - v1679*v1682 - jac_nuc_choice_182(v1834_g0, v1968, xn) - jac_nuc_choice_183(v1804, v1805_g0));
    const Real v1732_g1 = kr_t0[kzl_t0 + 614u];
    const Real v1994 = v1732_g1*v1650;
    const Real v1995 = v1993_g0*v1994;
    const Real v1999 = v1994*v1998_g0;
    const Real v1754_g1 = kr_t0[kzl_t0 + 621u];
    const Real v2003 = v1754_g1*v1650;
    const Real v2004 = v2002_g0*v2003;
    const Real v2008 = v2003*v2007_g0;
    const Real v2011 = v2010*(v1749*(v1992*v1995 + v1997*v1999) + v1768*(v2001*v2004 + v2006*v2008));
    const Real v1707_g1 = kr_t0[kzl_t0 + 627u];
    const Real v2014 = v1707_g1*v1722;
    const Real v2015 = v2013*v2014;
    const Real v2050 = -v2011 + v2014*v2049 - v2015;
    const Real v2017 = math::exp((-2)*math::log(math::abs(0.5*xn[1] + 0.5*xn[10] + 0.5*xn[2] + 0.5*xn[3] + 0.5*xn[9] + v1806)));
    const Real v2034 = v2017*v1658;
    const Real v1653_g1 = kr_t0[kzl_t0 + 631u];
    const Real v2036 = v2018*(250000.0*v2034 + 624999.99999999988*v2035*v1653_g1);
    const Real v2037 = 7.1777505407999997e-24*v2036;
    const Real v2021 = T*v2020;
    const Real v2048 = v1606*v2021*v1697*v1698;
    const Real v1984 = xn[8]*v1967*jac_nuc_choice_191(v1807, v1835, v1837);
    const Real v2022 = 1.1649047900646892e-19*v2021;
    const Real v2024 = jac_nuc_choice_201(v1777, v1778)*jac_nuc_choice_203(v1777, v1778, xn);
    const Real v1772_g2 = kr_t0[kzl_t0 + 595u];
    const Real v2026 = v1772_g2*v2024;
    const Real v2028 = v1779*v2024;
    const Real v2030 = v1781*v2024;
    const Real v2031 = v1782*v2024;
    const Real v1705_g4 = kr_t0[kzl_t0 + 584u];
    const Real v1973 = 2.3026818015679518*v1705_g4*v1821*jac_nuc_choice_184(v1810, v1699);
    const Real v2044 = jac_nuc_choice_205(v1809, v1810, v1973);
    const Real v1830_g1 = kr_t0[kzl_t0 + 637u];
    const Real v1975 = v1829*v1830_g1*v1833;
    const Real v1812_g1 = kr_t0[kzl_t0 + 612u];
    const Real v1976 = v1812_g1*v1826;
    const Real v1977 = 2*v1813*v1825;
    const Real v1978 = (1.0/2.0)*v1824*jac_nuc_choice_187(v1808);
    const Real v1979 = math::powi<-3>(v1604);
    const Real v1980 = (1.0/2.0)*v1823;
    const Real v1981 = 2*v1827*jac_nuc_choice_188(v1605, v1811)*jac_nuc_choice_189(v1807, v1808);
    const Real v2033 = 1.75918975308e-21*v1660;
    const Real v2039 = 1.4355501081600001e-11*xn[8];
    const Real v1691_g2 = kr_t0[kzl_t0 + 657u];
    const Real v1630_g1 = kr_t0[kzl_t0 + 718u];
    const Real v1689_g2 = kr_t0[kzl_t0 + 656u];
    const Real v1685_g1 = kr_t0[kzl_t0 + 714u];
    const Real v1834_g1 = kr_t0[kzl_t0 + 702u];
    const Real v1920_g1 = kr_t0[kzl_t0 + 574u];
    const Real v2023_g0 = kr_t0[kzl_t0 + 723u];
    const Real v1783_g2 = kr_t0[kzl_t0 + 600u];
    const Real v1786_g1 = kr_t0[kzl_t0 + 601u];
    const Real v1788_g2 = kr_t0[kzl_t0 + 602u];
    const Real v1790_g2 = kr_t0[kzl_t0 + 603u];
    const Real v1792_g1 = kr_t0[kzl_t0 + 604u];
    const Real v1794_g2 = kr_t0[kzl_t0 + 605u];
    const Real v1795_g1 = kr_t0[kzl_t0 + 607u];
    const Real v1797_g2 = kr_t0[kzl_t0 + 606u];
    const Real v1798_g2 = kr_t0[kzl_t0 + 608u];
    const Real v1799_g1 = kr_t0[kzl_t0 + 609u];
    const Real v1801_g2 = kr_t0[kzl_t0 + 610u];
    const Real v1805_g1 = kr_t0[kzl_t0 + 707u];
    const Real v2029_g1 = kr_t0[kzl_t0 + 708u];
    const Real v2032_g1 = kr_t0[kzl_t0 + 709u];
    out[matrix_index(14, 2)] = -3.7369204214442467e+22*v1969 + v1699*(xn[2]*v2039*v1660*v1691_g2 + xn[6]*v2033 - v1770 - 3.7958214423066343e-28*v1832 - v1630_g1*v1661 - v1975*v2044 - v1983*jac_nuc_choice_256(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2044) + v2022 + v2036*v2041 + v2036*v2047 + v2037*v2038 + v2037*v2045 + v2042*v2043 + 1.5764903505567533e+19*v2048 + v2050 + 2.1533251622400001e-11*v1660*v1689_g2*v1690 - v1661*v1685_g1 - jac_nuc_choice_258(v1834_g1, v1920_g1, v1922, v1966, v1984, v1986, v1987, v2023_g0) - jac_nuc_choice_259(v1705_g4, v1783_g2, v1786_g1, v1787, v1788_g2, v1789, v1790_g2, v1792_g1, v1794_g2, v1795_g1, v1797_g2, v1798_g2, v1799_g1, v1800, v1801_g2, v1802, v1804, v1805_g1, v2024, v2025, v2026, v2027, v2028, v2029_g1, v2030, v2031, v2032_g1));
    const Real v2067 = 1.4355501081600001e-11*v1707_g1*v1721;
    const Real v2068 = v2013*v2067;
    const Real v2065 = v2010*(v1749*(9580.6421066314851*v1991*v1995 + 4.6051701859880918*v1996*v1999) + v1768*(7632.6551179585222*v2000*v2004 + 4.6051701859880918*v2005*v2008));
    const Real v1655_g1 = kr_t0[kzl_t0 + 630u];
    const Real v2070 = 500000.0*v2034 + 546874.99999999988*v2035*v1655_g1;
    const Real v2071 = jac_nuc_choice_218(v1809, v1810, v1973);
    const Real v1696_g2 = kr_t0[kzl_t0 + 655u];
    const Real v1971 = -1.1649047900646892e-19*T*v1696_g2*v1700;
    const Real v1615_g1 = kr_t0[kzl_t0 + 716u];
    const Real v1613_g1 = kr_t0[kzl_t0 + 661u];
    const Real v1692_g2 = kr_t0[kzl_t0 + 658u];
    const Real v1694_g3 = kr_t0[kzl_t0 + 635u];
    const Real v1707_g2 = kr_t0[kzl_t0 + 627u];
    const Real v2016_g1 = kr_t0[kzl_t0 + 636u];
    const Real v1843_g2 = kr_t0[kzl_t0 + 552u];
    const Real v1852_g0 = kr_t0[kzl_t0 + 724u];
    const Real v1853_g1 = kr_t0[kzl_t0 + 547u];
    out[matrix_index(14, 8)] = -7.4738408428884933e+22*v1969 + v1699*(3.1529807011135066e+19*T*v1606*v1696_g2*v1697*v1698*v1700 - xn[0]*v1615_g1*v1613_g1 + 5.6556829037999991e-24*xn[2]*xn[3]*v2018*v2070*v1692_g2*v1694_g3 + 1.7591897530800001e-33*xn[2]*xn[6]*v2018*v2070 - xn[2]*v1769 + 7.1777505407999997e-24*xn[8]*v2018*v2070*v1690*v1691_g2 + 1.4355501081600001e-11*v1707_g2*v1712*v1719*v2012*v2016_g1*v1701 - v1720*v2039 - 7.5916428846132686e-28*v1832 - v1971 - v1975*v2071 - v1983*jac_nuc_choice_271(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2071) + 7.1777505407999997e-24*v2018*v2070*v1687*v1689_g2 - v2065 - v2068 + 7.1777505408000004e-12*v1660*v1690*v1691_g2 - jac_nuc_choice_273(v1834_g1, v1843_g2, v1852_g0, v1853_g1, v1920_g1, v1922, v1966, v1968, v1984, v1986, v1987));
    const Real v1974 = jac_nuc_choice_186(v1809, v1810, v1973);
    const Real v1647_g1 = kr_t0[kzl_t0 + 720u];
    const Real v1619_g1 = kr_t0[kzl_t0 + 719u];
    const Real v1668_g1 = kr_t0[kzl_t0 + 700u];
    const Real v1674_g1 = kr_t0[kzl_t0 + 701u];
    const Real v1972_g0 = kr_t0[kzl_t0 + 725u];
    const Real v1664_g1 = kr_t0[kzl_t0 + 713u];
    const Real v1985_g0 = kr_t0[kzl_t0 + 726u];
    out[matrix_index(14, 0)] = -2.0340826846270714e+19*v1969 + v1699*(8581161392004762.0*T*v1606*v1696_g2*v1697*v1698*v1700 - xn[12]*v1647_g1 - v1616 - v1619_g1 - v1622 - 2.0661437223616499e-31*v1832 - v1631 - v1638 - v1640 - 1.0019999999999999e-26*v1970*v1668_g1 - 1.82e-26*v1970*v1674_g1 - v1971 - v1972_g0*v1678 - v1972_g0*v1682 - v1974*v1975 - v1983*jac_nuc_choice_248(v1811, v1820, v1974, v1976, v1977, v1978, v1979, v1980, v1981, v1982) - v1664_g1*v1671 - v1611 - v1686 - jac_nuc_choice_250(v1834_g1, v1843_g2, v1920_g1, v1922, v1966, v1984, v1985_g0, v1986, v1987));
    const Real v1610_g1 = kr_t0[kzl_t0 + 715u];
    const Real v1988 = xn[0]*v1610_g1;
    const Real v1989 = jac_nuc_choice_195(v1809, v1810, v1973);
    const Real v1651_g2 = kr_t0[kzl_t0 + 632u];
    const Real v2019 = -1.4139207259499998e-18*xn[2]*xn[3]*v2017*v2018*v1651_g2*v1657*v1692_g2*v1694_g3 - 4.3979743826999997e-28*xn[2]*xn[6]*v2017*v2018*v1651_g2*v1657 - 1.7944376352000002e-18*xn[8]*v2017*v2018*v1651_g2*v1657*v1690*v1691_g2 - 7.1777505408000004e-12*v1707_g2*v1712*v1719*v2012*v2016_g1*v1701 + v2011 + v2015 - 1.7944376352000002e-18*v2017*v2018*v1651_g2*v1657*v1687*v1689_g2;
    const Real v1637_g1 = kr_t0[kzl_t0 + 711u];
    const Real v1990_g0 = kr_t0[kzl_t0 + 727u];
    out[matrix_index(14, 1)] = -3.7348863387551538e+22*v1969 + v1699*(1.5756322344156688e+19*T*v1606*v1696_g2*v1697*v1698*v1700 - xn[0]*v1637_g1 - 3.7937552985797361e-28*v1832 - v1971 - v1975*v1989 - v1983*jac_nuc_choice_252(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v1989) - v1988 - v2019 - jac_nuc_choice_254(v1834_g1, v1920_g1, v1922, v1966, v1984, v1986, v1987, v1990_g0));
    const Real v2077 = jac_nuc_choice_228(v1809, v1810, v1973);
    const Real v1681_g1 = kr_t0[kzl_t0 + 654u];
    const Real v2078_g0 = kr_t0[kzl_t0 + 728u];
    out[matrix_index(14, 13)] = -1.4943070555407201e+23*v1969 + v1699*(6.3040161099239145e+19*T*v1606*v1696_g2*v1697*v1698*v1700 - 1.5178601958613267e-27*v1832 - v1971 - v1975*v2077 - v1983*jac_nuc_choice_284(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2077) - v1679*v1681_g1 - jac_nuc_choice_286(v1834_g1, v1920_g1, v1922, v1966, v1984, v1986, v1987, v2078_g0));
    const Real v2058 = jac_nuc_choice_208(v1834_g1, v1984);
    const Real v2062 = v1971 + v2058;
    const Real v2063 = v1988 + v2062;
    const Real v2075 = jac_nuc_choice_226(v1809, v1810, v1973);
    const Real v1626_g1 = kr_t0[kzl_t0 + 638u];
    const Real v2076 = v1626_g1*v1670;
    const Real v1621_g1 = kr_t0[kzl_t0 + 717u];
    const Real v1669_g1 = kr_t0[kzl_t0 + 721u];
    const Real v1675_g1 = kr_t0[kzl_t0 + 722u];
    const Real v1677_g1 = kr_t0[kzl_t0 + 653u];
    out[matrix_index(14, 12)] = -1.4941036472718107e+23*v1969 + v1699*(6.3031579937828299e+19*T*v1606*v1696_g2*v1697*v1698*v1700 - xn[0]*v1621_g1 - xn[0]*v1647_g1 - 1.5176535814886368e-27*v1832 - v1975*v2075 - v1983*jac_nuc_choice_282(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2075) - v2063 - v2076*v1669_g1 - v2076*v1675_g1 - v1665 - v1677_g1*v1679);
    const Real v2053 = v2018*v2034;
    const Real v2054 = v2038*v2053;
    const Real v2055 = v2040*v2053;
    const Real v2056 = v2045*v2053;
    const Real v2057 = v2046*v2053;
    const Real v2059 = v2022 - v2058;
    const Real v2066 = jac_nuc_choice_214(v1809, v1810, v1973);
    out[matrix_index(14, 6)] = -7.4718067601993997e+22*v1969 + v1699*(xn[2]*v2033 - 7.5895767408863695e-28*v1832 - v1975*v2066 - v1983*jac_nuc_choice_267(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2066) + 3.1521225849724215e+19*v2048 + v2049*v2067 + 3.5888752704000004e-18*v2054 + 8.7959487653999994e-28*v2055 + 3.5888752704000004e-18*v2056 + 2.8278414518999996e-18*v2057 + v2059 - v2065 - v2068);
    const Real v2052 = jac_nuc_choice_207(v1809, v1810, v1973);
    const Real v2060 = v2050 + 1.7944376352000002e-18*v2054 + 4.3979743826999997e-28*v2055 + 1.7944376352000002e-18*v2056 + 1.4139207259499998e-18*v2057 + v2059;
    out[matrix_index(14, 3)] = -3.7389545041333399e+22*v1969 + v1699*(-3.7978875860335321e-28*v1832 - v1975*v2052 - v1983*jac_nuc_choice_261(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2052) + 1.5773484666978378e+19*v2048 + v2051*v1692_g2 + v2060);
    const Real v2073 = jac_nuc_choice_222(v1809, v1810, v1973);
    const Real v1805_g2 = kr_t0[kzl_t0 + 707u];
    out[matrix_index(14, 10)] = -1.1208455699147847e+23*v1969 + v1699*(4.7284984055187104e+19*T*v1606*v1696_g2*v1697*v1698*v1700 - 1.1385122421613269e-27*v1832 - v1975*v2073 - v1983*jac_nuc_choice_277(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2073) - v2019 - v2062 - jac_nuc_choice_278(v1803, v1805_g2));
    const Real v2072 = jac_nuc_choice_220(v1809, v1810, v1973);
    out[matrix_index(14, 9)] = -1.1206421616458753e+23*v1969 + v1699*(-1.1383056277886369e-27*v1832 - v1975*v2072 - v1983*jac_nuc_choice_275(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2072) + 4.7276402893776257e+19*v2048 + v2060);
    const Real v2074 = jac_nuc_choice_224(v1809, v1810, v1973);
    const Real v1696_g3 = kr_t0[kzl_t0 + 655u];
    const Real v1639_g1 = kr_t0[kzl_t0 + 712u];
    const Real v1609_g1 = kr_t0[kzl_t0 + 666u];
    out[matrix_index(14, 11)] = -1.4939002390029017e+23*v1969 + v1699*(6.302299877641746e+19*T*v1606*v1696_g3*v1697*v1698*v1700 - xn[0]*v1639_g1 - 8.5199999999999994e-27*xn[0]*v1609_g1 - 1.5174469671159474e-27*v1832 - v1975*v2074 - v1983*jac_nuc_choice_280(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2074) - v2062);
    const Real v2061 = jac_nuc_choice_210(v1809, v1810, v1973);
    out[matrix_index(14, 4)] = -7.4695011950145084e+22*v1969 + v1699*(3.151149938820873e+19*T*v1606*v1696_g3*v1697*v1698*v1700 - 7.5872348355797369e-28*v1832 - v1975*v2061 - v1983*jac_nuc_choice_263(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2061) - v2063);
    const Real v2069 = jac_nuc_choice_216(v1809, v1810, v1973);
    out[matrix_index(14, 7)] = -7.4735693603926949e+22*v1969 + v1699*(3.1528661711030424e+19*T*v1606*v1696_g3*v1697*v1698*v1700 - 7.5913671230335325e-28*v1832 - v1975*v2069 - v1983*jac_nuc_choice_269(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2069) - v2062);
    const Real v2064 = jac_nuc_choice_212(v1809, v1810, v1973);
    out[matrix_index(14, 5)] = -7.4715352777036004e+22*v1969 + v1699*(3.1520080549619573e+19*T*v1606*v1696_g3*v1697*v1698*v1700 - 7.5893009793066334e-28*v1832 - v1975*v2064 - v1983*jac_nuc_choice_265(v1811, v1820, v1976, v1977, v1978, v1979, v1980, v1981, v1982, v2064) - v2062);

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
        b.xn[n] = math::max(y[n], small_number_density_floor());
    }
    b.e[0] = y[NumSpec];
    eos_re_view(b);

}
DEVICE void rhs(Real t, const Real* y, Real* out, ScratchView s) {

    BurnView b = s.burn;
    burn_state_from_y_view(y, b);
    rhs_specie(b.T[0], out, b.xn, default_redshift);
    Real edot = rhs_eint(b.T[0], b.xn, default_redshift);
    out[14] = edot;

}
DEVICE void eval_jacobian(ScratchView s, Real x) {

    // jac_nuc unconditionally writes all 225 entries; no pre-zeroing needed.
    BurnView b = s.burn;
    burn_state_from_y_view(s.y, b);
    jac_nuc(b.T[0], s.fjac, b.xn, default_redshift);
    s.n_jac[0] += 1;

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

    if (A[midx(pivot_row, K)] != 0.0) {
        if (pivot_row != K) {
            const Real t = A[midx(pivot_row, K)];
            A[midx(pivot_row, K)] = A[midx(K, K)];
            A[midx(K, K)] = t;
            Real prow[14 - K];
            Real krow[14 - K];
            [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
                prow[j - K - 1] = A[midx(pivot_row, j)];
                krow[j - K - 1] = A[midx(K, j)];
            }
            [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
                A[midx(pivot_row, j)] = krow[j - K - 1];
                A[midx(K, j)] = prow[j - K - 1];
            }
        }

        const Real multiplier = -1.0 / A[midx(K, K)];
        Real row[14 - K];
        [[loom::unroll]] for (unsigned j = K + 1; j < 15u; ++j) {
            row[j - K - 1] = A[midx(K, j)];
        }
        Real next_max = 0.0;
        for (unsigned i = K + 1; i < 15u; ++i) {
            // Load the whole trailing row first, then store it: no store
            // between this row's loads (one memory wait per row).
            Real cur[15 - K];
            [[loom::unroll]] for (unsigned j = K; j < 15u; ++j) {
                cur[j - K] = A[midx(i, j)];
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
        }
    } else {
        info = static_cast<int>(K + 1);
        if constexpr (K + 1 < 14u) next_pivot = lu_pivot_search<K + 1>(A);
    }
    if constexpr (K + 1 < 14u) {
        return lu_step<K + 1>(A, ipvt, info, next_pivot);
    } else {
        return info;
    }
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
template <unsigned K, unsigned J>
DEVICE void lu_solve_bwd_chunk(const Real* LU, Real* x, Real t) {
    constexpr unsigned E = (J + 7 < K) ? J + 7 : K;
    Real xv[7];
    Real lv[7];
    [[loom::unroll]] for (unsigned j = J; j < E; ++j) {
        xv[j - J] = x[j];
        lv[j - J] = LU[midx(j, K)];
    }
    [[loom::unroll]] for (unsigned j = J; j < E; ++j) {
        x[j] = xv[j - J] + t * lv[j - J];
    }
    if constexpr (E < K) lu_solve_bwd_chunk<K, E>(LU, x, t);
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
template <unsigned K>
DEVICE void lu_solve_backward(const Real* LU, Real* x) {
    x[K] /= LU[midx(K, K)];
    const Real t = -x[K];
    if constexpr (K > 0) {
        lu_solve_bwd_chunk<K, 0>(LU, x, t);
        lu_solve_backward<K - 1>(LU, x);
    }
}
DEVICE void lu_solve(const Real* LU, const int* ipvt, Real* x) {

    lu_solve_forward<0>(LU, ipvt, x);
    lu_solve_backward<14>(LU, x);

}
DEVICE int decompose(ScratchView s, Real fac) {

        for (unsigned i = 0; i < 15u; ++i) {
            [[loom::unroll]] for (unsigned j = 0; j < 15u; ++j) {
                s.e[midx(i, j)] = -s.fjac[midx(i, j)];
            }
            s.e[midx(i, i)] += fac;
        }
        const int info = lu_decomposition(s.e, s.ip);
        if (info == 0) {
            s.n_decomp[0] += 1;
        }
        return info;

}
DEVICE void solve(ScratchView s, Real* ak) {

        lu_solve(s.e, s.ip, ak);
        s.n_solve[0] += 1;

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
DEVICE int burn_ros2s(BurnRecord* b, Real duration, IntegratorStats* stats) {

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
    Real burn_xn[14];
    int n_step = 0;
    int n_rhs = 0;
    int n_jac = 0;
    int n_accept = 0;
    int n_reject = 0;
    int n_decomp = 0;
    int n_solve = 0;
    int max_steps = 100000;
    int ip[15];
    ScratchView s = {&t, &tout, &dt, y, rtol_vec, atol_vec, &uround, &fac_min, &fac_max, &safe, ynew, ak1, ak2, work, fjac, e, dy, {&burn_rho, &burn_T, &burn_e, burn_xn}, &n_step, &n_rhs, &n_jac, &n_accept, &n_reject, &n_decomp, &n_solve, &max_steps, ip};
    configure_ros2s(s);

    s.t[0] = 0.0;
    s.tout[0] = duration;
    s.dt[0] = duration;
    for (int n = 0; n < NumSpec; ++n) {
        s.y[static_cast<size_type>(n)] = b->xn[static_cast<size_type>(n)];
    }
    s.y[NumSpec] = b->e;

    const auto result = integrate(s);

    stats->internal_steps += static_cast<u64>(math::max(0, s.n_step[0]));
    stats->rhs_calls += static_cast<u64>(math::max(0, s.n_rhs[0]));
    stats->jacobian_calls += static_cast<u64>(math::max(0, s.n_jac[0]));
    stats->decompositions += static_cast<u64>(math::max(0, s.n_decomp[0]));
    stats->linear_solves += static_cast<u64>(math::max(0, s.n_solve[0]));
    stats->accepted_steps += static_cast<u64>(math::max(0, s.n_accept[0]));
    stats->rejected_steps += static_cast<u64>(math::max(0, s.n_reject[0]));

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
    const auto result = burn_ros2s(b, dt_grid, &record->stats);
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
