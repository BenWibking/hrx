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

    // safe, fac_min, fac_max, atol_vec, rtol_vec and uround are the compile-time
    // values configure_ros2s/burn_ros2s store (all checks pass for them).
    if (s.tout[0] <= s.t[0]) {
        return BAD_INPUTS;
    }
    const Real hmaxn = s.tout[0] - s.t[0];
    Real h = s.dt[0];
    if (math::abs(h) <= 10.0 * 1.e-16) {
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

    bool need_jac = true;
    // Stage 0's f(y) is reused from s.dy on retries (same y).
    bool have_f0 = false;
    // One loop for new steps and retries: a lane that needs a new Jacobian
    // evaluates it while retrying lanes wait, then all lanes run the
    // decomposition and stages together. Same per-lane sequence of operations.
    while (!done) {
      if (need_jac) {
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
            need_jac = false;
            have_f0 = false;
        }
      }
        if (!done) {
                // Stage 0's RHS (at y, independent of h) runs first, then the
                // Jacobian (new steps) and the decomposition, so stage 0's solve
                // reads the factor right after it was written (still cached).
                // A lane with a singular matrix runs the stages on a discarded
                // factor and is handled after the stage loop.
                bool singular = false;
                bool noperm = true;
                const Real fac = 1.0 / (h * C::gamma);
                for (int stage = 0; stage < 3; ++stage) {
                    Real* target = s.ak1 + 15 * stage;
                    Real yv[15];
                    if (stage == 0) {
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            yv[i] = s.y[i];
                        }
                    } else if (stage == 1) {
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            yv[i] = s.y[i] + C::a21 * s.ak1[i];
                        }
                    } else {
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            yv[i] = s.y[i] + C::a31 * s.ak1[i] + C::a32 * s.ak2[i];
                        }
                    }
                    Real dyv[15];
                    const bool reuse = (stage == 0) & have_f0;
                    if (!reuse) {
                        CHEM_RHS(x, yv, dyv, s);
                    } else {
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            dyv[i] = s.dy[i];
                        }
                    }
                    if ((stage == 0) & !have_f0) {
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            s.dy[i] = dyv[i];
                        }
                        have_f0 = true;
                    }
                    if (stage == 0) {
                        singular = decompose(s, fac, &noperm) != 0;
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
                        solve_regs(s, tv, target, noperm);
                    }
                }
                if (singular) {
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
                    need_jac = true;
                } else {

                    reject = true;
                    last = false;
                    h = hnew;
                    // Rejections after the first accept are counted at exit from
                    // n_step, n_accept and the step of the first accept.
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
