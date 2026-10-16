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
                const Real err = combine_and_error_norm(s, yacc);
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
