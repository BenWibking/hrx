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
        if (n_step > s.max_steps[0]) {
            s.t[0] = x;
            s.dt[0] = h;
            result = TOO_MANY_STEPS; done = true;
        }
        else if (0.1 * math::abs(h) <= math::abs(x) * s.uround[0]) {
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
                    // Vector updates: all loads, then all stores (one drain
                    // per loop instead of two per element).
                    if (stage == 0) {
                        Real yv[15];
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            yv[i] = s.y[i];
                        }
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            s.ynew[i] = yv[i];
                        }
                    } else if (stage == 1) {
                        Real yv[15];
                        Real kv[15];
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            yv[i] = s.y[i] + C::a21 * s.ak1[i];
                            kv[i] = (C::c21 / h) * s.ak1[i];
                        }
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            s.ynew[i] = yv[i];
                            s.ak2[i] = kv[i];
                        }
                    } else {
                        Real yv[15];
                        Real kv[15];
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            yv[i] = s.y[i] + C::a31 * s.ak1[i] + C::a32 * s.ak2[i];
                            kv[i] = (C::c31 * s.ak1[i] + C::c32 * s.ak2[i]) / h;
                        }
                        [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                            s.ynew[i] = yv[i];
                            s.work[i] = kv[i];
                        }
                    }
                    CHEM_RHS(x, s.ynew, s.dy, s);
                    {
                        Real tv[15];
                        if (stage == 0) {
                            [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                                tv[i] = s.dy[i];
                            }
                        } else {
                            [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                                tv[i] = target[i] + s.dy[i];
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
                Real err;
                {
                    Real ev[15];
                    Real yo[15];
                    [[loom::unroll]] for (unsigned i = 0; i < 15u; ++i) {
                        const Real ak3i = s.work[i];
                        yo[i] = s.y[i];
                        yacc[i] = yo[i] + C::b1 * s.ak1[i] + C::b2 * s.ak2[i] + C::b3 * ak3i;
                        ev[i] = C::e1 * s.ak1[i] + C::e2 * s.ak2[i] + C::e3 * ak3i;
                    }
                    const Real t0 = ev[0] / (s.atol_vec[0] + s.rtol_vec[0] * math::max(math::abs(yo[0]), math::abs(yacc[0])));
                    const Real t1 = ev[1] / (s.atol_vec[1] + s.rtol_vec[1] * math::max(math::abs(yo[1]), math::abs(yacc[1])));
                    const Real t2 = ev[2] / (s.atol_vec[2] + s.rtol_vec[2] * math::max(math::abs(yo[2]), math::abs(yacc[2])));
                    const Real t3 = ev[3] / (s.atol_vec[3] + s.rtol_vec[3] * math::max(math::abs(yo[3]), math::abs(yacc[3])));
                    const Real t4 = ev[4] / (s.atol_vec[4] + s.rtol_vec[4] * math::max(math::abs(yo[4]), math::abs(yacc[4])));
                    const Real t5 = ev[5] / (s.atol_vec[5] + s.rtol_vec[5] * math::max(math::abs(yo[5]), math::abs(yacc[5])));
                    const Real t6 = ev[6] / (s.atol_vec[6] + s.rtol_vec[6] * math::max(math::abs(yo[6]), math::abs(yacc[6])));
                    const Real t7 = ev[7] / (s.atol_vec[7] + s.rtol_vec[7] * math::max(math::abs(yo[7]), math::abs(yacc[7])));
                    const Real t8 = ev[8] / (s.atol_vec[8] + s.rtol_vec[8] * math::max(math::abs(yo[8]), math::abs(yacc[8])));
                    const Real t9 = ev[9] / (s.atol_vec[9] + s.rtol_vec[9] * math::max(math::abs(yo[9]), math::abs(yacc[9])));
                    const Real t10 = ev[10] / (s.atol_vec[10] + s.rtol_vec[10] * math::max(math::abs(yo[10]), math::abs(yacc[10])));
                    const Real t11 = ev[11] / (s.atol_vec[11] + s.rtol_vec[11] * math::max(math::abs(yo[11]), math::abs(yacc[11])));
                    const Real t12 = ev[12] / (s.atol_vec[12] + s.rtol_vec[12] * math::max(math::abs(yo[12]), math::abs(yacc[12])));
                    const Real t13 = ev[13] / (s.atol_vec[13] + s.rtol_vec[13] * math::max(math::abs(yo[13]), math::abs(yacc[13])));
                    const Real t14 = ev[14] / (s.atol_vec[14] + s.rtol_vec[14] * math::max(math::abs(yo[14]), math::abs(yacc[14])));
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
                    err = math::sqrt(q14 / static_cast<Real>(N));
                }
                s.n_rhs[0] += 3;
                n_step += 1;
                s.n_step[0] = n_step;

                const Real raw_fac = math::cbrt(err) / s.safe[0];
                const Real lower_fac = 1.0 / s.fac_max[0];
                const Real upper_fac = 1.0 / s.fac_min[0];
                const Real fac_step = math::max(lower_fac, math::min(upper_fac, raw_fac));
                Real hnew = h / fac_step;
                if (err <= 1.0) {
                    n_accept += 1;
                    s.n_accept[0] = n_accept;
                    if (n_accept > 1) {
                        const Real facgus = math::max(1.0 / s.fac_max[0],
                            math::min(1.0 / s.fac_min[0],
                            (hacc / h) *
                            math::cbrt((err * err) / erracc) /
                            s.safe[0]));
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
                    if (n_accept >= 1) {
                        s.n_reject[0] += 1;
                    }
                }
            }
        }
    }
    return result;

}
#undef CHEM_EVAL_JACOBIAN
#undef CHEM_RHS
