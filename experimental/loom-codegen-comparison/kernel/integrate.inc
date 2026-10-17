// SPDX-License-Identifier: BSD-3-Clause
// Generated exact control rewrite; also tested with a singular-Jacobian fixture.
#ifndef CHEM_EVAL_JACOBIAN
#define CHEM_EVAL_JACOBIAN eval_jacobian
#endif
#ifndef CHEM_RHS
#define CHEM_RHS rhs
#endif
DEVICE int integrate(ScratchView s) {

    int result = SUCCESS;
    bool done = false;

    if (s.tout[0] <= s.t[0] || s.safe[0] <= 0.001 || s.safe[0] >= 1.0 ||
        s.fac_min[0] <= 0.0 || s.fac_max[0] < 1.0) {
        return BAD_INPUTS;
    }
    for (size_type i = 0; i < N && !done; ++i) {
        __builtin_assume(i < N);
        if (s.atol_vec[i] <= 0.0 || s.rtol_vec[i] <= 10.0 * s.uround[0]) {
            result = TOO_MUCH_ACCURACY_REQUESTED;
            done = true;
        }
    }

    if (done) return result;
    const Real hmaxn = s.tout[0] - s.t[0];
    Real h = s.dt[0];
    if (math::abs(h) <= 10.0 * s.uround[0]) {
        h = 1.e-6;
    }
    h = math::min(math::abs(h), hmaxn);

    // Controller settings: the compile-time values burn_ros2s and
    // configure_ros2s store (nothing changes them), used directly in the loop.
    constexpr Real uround_c = 1.e-16;
    constexpr Real safe_c = 0.9;
    constexpr Real fac_min_c = 0.2;
    constexpr Real fac_max_c = 6.0;
    constexpr int max_steps_c = 10000000;
    bool reject = false;
    bool last = false;
    int nsing = 0;
    Real hacc = 0.0;
    Real erracc = 1.0;
    Real hopt = h;
    Real x = s.t[0];
    int n_step = 0;
    int n_accept = 0;

    while (!done) {
        if (n_step > max_steps_c) {
            s.t[0] = x;
            s.dt[0] = h;
            result = TOO_MANY_STEPS; done = true;
        }
        else if (0.1 * math::abs(h) <= math::abs(x) * uround_c) {
            s.t[0] = x;
            s.dt[0] = h;
            result = DT_UNDERFLOW; done = true;
        }
        else if (last) {
            s.t[0] = x;
            s.dt[0] = hopt;
            result = SUCCESS; done = true;
        }

        if (!done) {
            hopt = h;
            if (x + h * 1.0001 >= s.tout[0]) {
                h = s.tout[0] - x;
                last = true;
            }

            CHEM_EVAL_JACOBIAN(s, x);
            bool retry = true;

            while (retry && !done) {
                const Real fac = 1.0 / (h * C::gamma);
                if (decompose(s, fac) != 0) {
                    nsing += 1;
                    if (nsing >= 5) {
                        s.t[0] = x;
                        s.dt[0] = h;
                        result = LU_DECOMPOSITION_ERROR; done = true;
                        continue;
                    }
                    h *= 0.5;
                    reject = true;
                    last = false;
                    continue;
                }

                // One RHS/solve call site for all three stages (ak1, ak2, work
                // are contiguous: stage k writes ak1 + 15*k).
                for (int stage = 0; stage < 3; ++stage) {
                    Real* target = s.ak1 + 15 * stage;
                    // The stage state goes to the RHS in registers (ynew is only
                    // rewritten after the stages), and the k-vector term is formed
                    // after the RHS from ak1/ak2, so nothing is stored and then
                    // reloaded from the same array before the RHS.
                    Real yv[15];
                    if (stage == 1) {
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            yv[i] = s.y[i] + C::a21 * s.ak1[i];
                        }
                    } else if (stage == 2) {
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            yv[i] = s.y[i] + C::a31 * s.ak1[i] + C::a32 * s.ak2[i];
                        }
                    }
                    Real dyv[15];
                    if (stage == 0) {
                        // Stage-0 RHS at (x, y): evaluated with the Jacobian.
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            dyv[i] = s.dy[i];
                        }
                    } else {
                        CHEM_RHS(x, yv, dyv, s);
                    }
                    {
                        Real tv[15];
                        if (stage == 0) {
                            [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                                tv[i] = dyv[i];
                            }
                        } else if (stage == 1) {
                            [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                                tv[i] = (C::c21 / h) * s.ak1[i] + dyv[i];
                            }
                        } else {
                            [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                                tv[i] = (C::c31 * s.ak1[i] + C::c32 * s.ak2[i]) / h + dyv[i];
                            }
                        }
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            target[i] = tv[i];
                        }
                    }
                    solve(s, target);
                }

                // Final combination with the error norm formed in registers: the
                // new state and error vector are not stored and reloaded (ynew and
                // work are only read here and by the accept copy). Same operations
                // and order as the combination loop + error_norm.
                Real yacc[15];
                const Real err = combine_and_error_norm(s, yacc);
                // rhs calls, decompositions and solves are 3*n_step, n_step and
                // 3*n_step; burn_ros2s derives them from n_step.
                n_step += 1;

                const Real raw_fac = math::cbrt(err) / safe_c;
                const Real lower_fac = 1.0 / fac_max_c;
                const Real upper_fac = 1.0 / fac_min_c;
                const Real fac_step = math::max(lower_fac, math::min(upper_fac, raw_fac));
                Real hnew = h / fac_step;
                if (err <= 1.0) {
                    n_accept += 1;
                    if (n_accept == 1) {
                        // n_reject holds the attempt number of the first accept.
                        s.n_reject[0] = n_step;
                    }
                    if (n_accept > 1) {
                        const Real facgus = math::max(1.0 / fac_max_c,
                            math::min(1.0 / fac_min_c,
                            (hacc / h) *
                            math::cbrt((err * err) / erracc) /
                            safe_c));
                        hnew = h / math::max(fac_step, facgus);
                    }
                    hacc = h;
                    erracc = math::max(1.e-2, err);
                    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                        s.y[i] = yacc[i];
                    }
                    x += h;
                    if (math::abs(hnew) > hmaxn) {
                        hnew = hmaxn;
                    }
                    if (reject) {
                        hnew = math::min(math::abs(hnew), math::abs(h));
                    }
                    reject = false;
                    h = hnew;
                    retry = false;
                } else {

                    reject = true;
                    last = false;
                    h = hnew;
                    // Rejections after the first accept are counted at exit from
                    // n_step, n_accept and the step of the first accept.
                }
            }
        }
    }
    // The step counters are only read after integrate returns.
    s.n_step[0] = n_step;
    s.n_accept[0] = n_accept;
    return result;

}
#undef CHEM_EVAL_JACOBIAN
#undef CHEM_RHS
