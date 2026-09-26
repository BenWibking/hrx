// SPDX-License-Identifier: BSD-3-Clause
// Native differential validation, not a GPU runtime or atomic-ordering test.
#define PRIMORDIAL_ROS2S_NO_MAIN
#define __HIP_DEVICE_COMPILE__ 1
#include "reference.cpp"
#undef __HIP_DEVICE_COMPILE__
#include "reproducer.cpp"
#include "reference_kernels.inc"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lc = chemistry;
static_assert(sizeof(lc::size_type) == sizeof(std::size_t));
static_assert(sizeof(lc::u64) == sizeof(std::uint64_t));
static_assert(sizeof(lc::Real) == 8);
// Five diagonal poles force all singular retries, including the fifth-failure
// exit. Both original and rewritten integrators consume the SAME Jacobian.
struct SingularProblem {
    static constexpr std::size_t neqs = 15;
    using state_type = std::array<double, 15>;
    using jacobian_type = std::array<std::array<double, 15>, 15>;
    static void rhs(double, const state_type&, state_type&) { std::abort(); }
    static void jacobian(double, const state_type&, jacobian_type& jac) {
        double h = 1.;
        for (int n = 0; n < 15; ++n) {
            jac[n].fill(0.);
            if (n < 5) { jac[n][n] = 1. / (h * integrators::detail::ROS2SCoefficients::gamma); h *= .5; }
        }
    }
};
namespace singular_case {
using namespace chemistry;
inline void eval_jacobian(ScratchView s, Real) {
    SingularProblem::jacobian_type jac{};
    SingularProblem::state_type y{};
    SingularProblem::jacobian(0., y, jac);
    for (int n = 0; n < 15; ++n) for (int m = 0; m < 15; ++m) s.fjac[n * 15 + m] = jac[n][m];
    s.n_jac[0] += 1;
}
inline void rhs(Real, const Real*, Real*, ScratchView) { std::abort(); }
#define CHEM_EVAL_JACOBIAN singular_case::eval_jacobian
#define CHEM_RHS singular_case::rhs
#include "integrate.inc"
}
static unsigned long checks = 0;
static void equal(double a, double b, const char* label) {
    ++checks;
    if (std::bit_cast<std::uint64_t>(a) != std::bit_cast<std::uint64_t>(b)) {
        std::fprintf(stderr, "%s: %.17g != %.17g (%a != %a)\n", label, a, b, a, b);
        std::exit(1);
    }
}
static void integer(long long a, long long b, const char* label) {
    ++checks;
    if (a != b) {
        std::fprintf(stderr, "%s: %lld != %lld\n", label, a, b);
        std::exit(1);
    }
}
static lc::CellRecord pack(const CollapseState& c) {
    lc::CellRecord record{};
    record.current.rho = c.current.rho;
    record.current.T = c.current.T;
    record.current.e = c.current.e;
    for (int n = 0; n < 14; ++n) record.current.xn[n] = c.current.xn[n];
    record.time = c.time;
    record.density_driver = c.density_driver;
    record.completed_steps = c.completed_steps;
    record.stats.internal_steps = c.stats.internal_steps;
    record.stats.rhs_calls = c.stats.rhs_calls;
    record.stats.jacobian_calls = c.stats.jacobian_calls;
    record.stats.decompositions = c.stats.decompositions;
    record.stats.linear_solves = c.stats.linear_solves;
    record.stats.accepted_steps = c.stats.accepted_steps;
    record.stats.rejected_steps = c.stats.rejected_steps;
    return record;
}
static void check_burn(const pc::burn_t& c, const lc::BurnRecord& b) {
    equal(c.rho, b.rho, "rho"); equal(c.T, b.T, "T"); equal(c.e, b.e, "e");
    for (int n = 0; n < 14; ++n) equal(c.xn[n], b.xn[n], "species");
}
static void check_cell(const CollapseState& c, const lc::CellRecord& record) {
    check_burn(c.current, record.current);
    equal(c.time, record.time, "time"); equal(c.density_driver, record.density_driver, "density_driver");
    integer(c.completed_steps, record.completed_steps, "completed_steps");
    integer(c.stats.internal_steps, record.stats.internal_steps, "internal_steps");
    integer(c.stats.rhs_calls, record.stats.rhs_calls, "rhs_calls");
    integer(c.stats.jacobian_calls, record.stats.jacobian_calls, "jacobian_calls");
    integer(c.stats.decompositions, record.stats.decompositions, "decompositions");
    integer(c.stats.linear_solves, record.stats.linear_solves, "linear_solves");
    integer(c.stats.accepted_steps, record.stats.accepted_steps, "accepted_steps");
    integer(c.stats.rejected_steps, record.stats.rejected_steps, "rejected_steps");
}

static void test_math_and_chemistry() {
    for (double temperature : {10., 100., 1000., 10000., 1.e5, 1.e6}) {
        for (double scale : {1.e-3, 1., 1.e3}) {
            auto c = make_collapse_state();
            c.current.T = temperature;
            for (auto& x : c.current.xn) x *= scale;
            c.current.rho = pc::density(c.current.xn);
            pc::eos_rt(c.current);
            auto record = pack(c);
            auto* b = &record.current;
            lc::eos_rt(b); check_burn(c.current, *b);
            pc::eos_re(c.current); lc::eos_re(b); check_burn(c.current, *b);
            pc::floor_and_normalize_number_densities(c.current);
            double mass[14]; lc::floor_and_normalize_number_densities(b, mass);
            pc::balance_charge(c.current); lc::balance_charge(b);
            check_burn(c.current, *b);
            pc::Array1D<double, 0, 13> X;
            pc::Array1D<double, 1, 15> out;
            for (int n = 0; n < 14; ++n) X(n) = c.current.xn[n];
            double actual[15] = {};
            pc::rhs_specie(c.current, out, X, 30.);
            lc::rhs_specie(b->T, actual, b->xn, 30.);
            for (int n = 0; n < 14; ++n) equal(out(n+1), actual[n], "rhs species");
            equal(pc::rhs_eint(c.current, X, 30.), lc::rhs_eint(b->T, b->xn, 30.), "rhs energy");
            pc::PrimordialChem::jacobian_type jac{};
            pc::JacobianAdapter adapter{jac};
            double actual_jac[225] = {};
            pc::jac_nuc(c.current, adapter, X, 30.);
            lc::jac_nuc(b->T, actual_jac, b->xn, 30.);
            for (int i = 0; i < 15; ++i)
                for (int j = 0; j < 15; ++j) equal(jac[i][j], actual_jac[i * 15 + j], "jacobian");
        }
    }
    // Preserve min/max first-operand semantics for NaNs and signed zeros.
    for (double a : {0., -0., 1., -1., std::numeric_limits<double>::quiet_NaN()})
        for (double b : {0., -0., 1., -1., std::numeric_limits<double>::quiet_NaN()}) {
            equal(std::min(a,b), lc::math::min(a,b), "min");
            equal(std::max(a,b), lc::math::max(a,b), "max");
        }
}

static void test_lu() {
    for (int variant = 0; variant < 3; ++variant) {
        std::array<std::array<double, 15>, 15> a{};
        double matrix[15][15] = {};
        std::array<int, 15> ip{}; int fp[15] = {};
        for (int i = 0; i < 15; ++i)
            for (int j = 0; j < 15; ++j) {
                double x = (i == j ? 7.0 : 0.01*(i+1)*(j+1));
                if (variant == 1 && j == 0) x = (i == 14 ? 100. : 0.);
                if (variant == 2) x = 0.;
                a[i][j] = matrix[i][j] = x;
            }
        int info = integrators::linalg::lu_decomposition<15>(a, ip);
        integer(info, lc::lu_decomposition(&matrix[0][0], fp), "LU info");
        for (int i = 0; i < 15; ++i) {
            integer(ip[i], fp[i], "pivot");
            for (int j = 0; j < 15; ++j) equal(a[i][j], matrix[i][j], "LU matrix");
        }
        if (!info) {
            std::array<double, 15> x{}; double fx[15];
            for (int n = 0; n < 15; ++n) x[n] = fx[n] = n+1.;
            integrators::linalg::lu_solve<15>(a, ip, x);
            lc::lu_solve(&matrix[0][0], fp, fx);
            for (int n = 0; n < 15; ++n) equal(x[n], fx[n], "LU solution");
        }
    }
}

static void test_integrator() {
    int total_rejects = 0;
    for (int variant = 0; variant < 10; ++variant) {
        Ros2sIntegrator::State old;
        configure_ros2s(old);
        auto initial = make_initial_state();
        if (variant >= 8) {
            initial.T = 10000.;
            pc::eos_rt(initial);
        }
        for (int n = 0; n < 14; ++n) old.y[n] = initial.xn[n];
        old.y[14] = initial.e;
        old.tout = old.dt = (variant == 1 ? 1.e10 : 1.e7);
        if (variant == 2) old.tout = 0.;
        if (variant == 3) old.atol_vec[7] = 0.;
        if (variant == 4) old.max_steps = -1;
        if (variant == 5) { old.t = 1.e20; old.tout = 2.e20; old.dt = 1.; }
        if (variant == 6) old.max_steps = 0;
        if (variant == 7) old.safe = 0.;
        if (variant >= 8) old.tout = old.dt = 1.e14;
        if (variant == 9) old.safe = .1;
        lc::ScratchRecord storage{};
        auto* s = &storage;
        lc::initialize_solver(lc::scratch_view(s));
        // Copy every original field, including matrices and all default values.
#define COPY_SCALAR(n) s->n = old.n
        COPY_SCALAR(t); COPY_SCALAR(tout); COPY_SCALAR(dt);
        COPY_SCALAR(uround); COPY_SCALAR(fac_min); COPY_SCALAR(fac_max); COPY_SCALAR(safe);
        COPY_SCALAR(n_step); COPY_SCALAR(n_rhs); COPY_SCALAR(n_jac); COPY_SCALAR(n_accept);
        COPY_SCALAR(n_reject); COPY_SCALAR(n_decomp); COPY_SCALAR(n_solve); COPY_SCALAR(max_steps);
#undef COPY_SCALAR
#define COPY_ARRAY(n) for (int k = 0; k < 15; ++k) s->n[k] = old.n[k]
        COPY_ARRAY(y); COPY_ARRAY(rtol_vec); COPY_ARRAY(atol_vec); COPY_ARRAY(ynew);
        COPY_ARRAY(ak1); COPY_ARRAY(ak2); COPY_ARRAY(work); COPY_ARRAY(dy); COPY_ARRAY(ip);
#undef COPY_ARRAY
        Ros2sIntegrator integrator;
        int result = static_cast<int>(integrator.integrate(old));
        integer(result, lc::integrate(lc::scratch_view(s)), "integrator result");
#define CHECK_SCALAR(n) equal(old.n, s->n, #n)
        CHECK_SCALAR(t); CHECK_SCALAR(tout); CHECK_SCALAR(dt); CHECK_SCALAR(uround);
        CHECK_SCALAR(fac_min); CHECK_SCALAR(fac_max); CHECK_SCALAR(safe);
        CHECK_SCALAR(n_step); CHECK_SCALAR(n_rhs); CHECK_SCALAR(n_jac); CHECK_SCALAR(n_accept);
        CHECK_SCALAR(n_reject); CHECK_SCALAR(n_decomp); CHECK_SCALAR(n_solve); CHECK_SCALAR(max_steps);
#undef CHECK_SCALAR
#define CHECK_ARRAY(n) for (int k = 0; k < 15; ++k) equal(old.n[k], s->n[k], #n)
        CHECK_ARRAY(y); CHECK_ARRAY(rtol_vec); CHECK_ARRAY(atol_vec); CHECK_ARRAY(ynew);
        CHECK_ARRAY(ak1); CHECK_ARRAY(ak2); CHECK_ARRAY(work); CHECK_ARRAY(dy); CHECK_ARRAY(ip);
#undef CHECK_ARRAY
        for (int i = 0; i < 15; ++i) for (int j = 0; j < 15; ++j) {
            equal(old.e[i][j], s->e[i][j], "solver LU");
            equal(old.fjac[i][j], s->fjac[i][j], "solver jacobian");
        }
        std::printf("integrator variant %d: result %d, accepted %d, rejected %d\n",
                    variant, result, old.n_accept, old.n_reject);
        total_rejects += old.n_reject;
    }
    if (!total_rejects) { std::fprintf(stderr, "missing rejected-step coverage\n"); std::exit(1); }
}

static void test_grid(int limit) {
    constexpr int count = 4;
    std::vector<CollapseState> old(count);
    std::vector<lc::CellRecord> cells(count);
    for (int n = 0; n < count; ++n) {
        old[n] = make_collapse_state();
        old[n].current.T *= 1. + n*.25;
        pc::eos_rt(old[n].current);
        cells[n] = pack(old[n]);
    }
    double time = 0.; int completed = 0;
    for (int step = 0; step < limit; ++step) {
        double candidates[count], expected[count];
        int failure = 1, original_failure = 1;
        for (int lane = 0; lane < count+3; ++lane) {
            reference_prepare_grid_timestep_kernel(old.data(), count, completed, time, step,
                true, expected, &original_failure, lane);
            lc::prepare_grid_timestep_kernel(cells.data(),
                count, completed, time, step, true, candidates, &failure, lane);
        }
        integer(original_failure, failure, "prepare failure");
        double dt = MAX_DOUBLE;
        for (int n = 0; n < count; ++n) {
            equal(expected[n], candidates[n], "candidate");
            check_cell(old[n], cells[n]);
            if (expected[n] < dt) dt = expected[n];
        }
        if (failure != 1 || dt == MAX_DOUBLE) break;
        double next = time + dt;
        int integrated = 0, original_integrated = 0;
        for (int lane = 0; lane < count+3; ++lane) {
            reference_advance_collapse_gridwide_kernel(old.data(), count, completed, next,
                dt, &original_integrated, &original_failure, lane);
            lc::advance_collapse_gridwide_kernel(cells.data(),
                count, completed, next, dt, &integrated, &failure, lane);
        }
        integer(original_failure, failure, "advance failure");
        integer(original_integrated, integrated, "integrated");
        for (int n = 0; n < count; ++n)
            check_cell(old[n], cells[n]);
        if (failure != 1 || integrated == 0) break;
        time = next; ++completed;
    }
    if (limit == 25) integer(25, completed, "grid completed 25 steps including perturbation step 20");
    else integer(true, completed >= 25, "full grid reaches perturbation step 20");
    std::printf("grid: %d cells, %d steps, final time %.17g\n", count, completed, time);
}

static void test_singular_exit() {
    integrators::RODASState<15> old;
    configure_ros2s(old);
    old.dt = 1.; old.tout = 10.;
    lc::ScratchRecord storage{};
    auto* s = &storage;
    lc::initialize_solver(lc::scratch_view(s)); lc::configure_ros2s(lc::scratch_view(s));
    s->dt = 1.; s->tout = 10.;
    integrators::RODAS<SingularProblem> integrator;
    int result = static_cast<int>(integrator.integrate(old));
    integer(-7, result, "singular fixture triggers fifth failure");
    integer(result, singular_case::integrate(lc::scratch_view(s)), "singular result");
    equal(old.t, s->t, "singular t"); equal(old.dt, s->dt, "singular dt");
    integer(old.n_jac, s->n_jac, "singular Jacobian count");
    integer(old.n_decomp, s->n_decomp, "singular decomposition count");
    integer(old.n_rhs, s->n_rhs, "singular RHS count");
    for (int n = 0; n < 15; ++n) {
        integer(old.ip[n], s->ip[n], "singular pivot");
        for (int m = 0; m < 15; ++m) equal(old.e[n][m], s->e[n][m], "singular LU");
    }
}

static void test_kernel_guards() {
    for (int variant = 0; variant < 7; ++variant) {
        auto c = make_collapse_state();
        if (variant == 0) c.completed_steps = -1;
        if (variant == 1) c.completed_steps = 1;
        if (variant == 2) c.time = 1.;
        if (variant == 3) c.current.xn.fill(0.);
        if (variant == 4) c.density_driver = -1.;
        if (variant == 5) {
            for (auto& x : c.current.xn) x *= 1.e30;
            c.current.rho = pc::density(c.current.xn);
            c.density_driver = c.current.rho;
        }
        auto record = pack(c);
        int f = variant == 6 ? -7 : 1, old_f = f;
        double dt = -99., old_dt = dt;
        reference_prepare_grid_timestep_kernel(&c, 1, 0, 0., 1, false, &old_dt, &old_f, 0);
        lc::prepare_grid_timestep_kernel(&record,
                                         1, 0, 0., 1, false, &dt, &f, 0);
        integer(old_f, f, "guard failure"); equal(old_dt, dt, "guard candidate");
        check_cell(c, record);
        int count = 0, old_count = 0;
        reference_advance_collapse_gridwide_kernel(&c, 1, 0, 100., 100., &old_count, &old_f, 0);
        lc::advance_collapse_gridwide_kernel(&record,
                                             1, 0, 100., 100., &count, &f, 0);
        integer(old_f, f, "advance guard failure"); integer(old_count, count, "advance guard count");
        check_cell(c, record);
    }
}

int main(int argc, char** argv) {
    test_math_and_chemistry(); test_lu(); test_integrator(); test_singular_exit();
    test_grid(argc == 2 && std::strcmp(argv[1], "--full-grid") == 0 ? 1000 : 25);
    test_kernel_guards();
    std::printf("PASS: %lu exact field comparisons\n", checks);
}
