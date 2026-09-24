// SPDX-License-Identifier: BSD-3-Clause
// Generated exact control rewrite; also tested with a singular-Jacobian fixture.
#ifndef CHEM_EVAL_JACOBIAN
#define CHEM_EVAL_JACOBIAN eval_jacobian
#endif
#ifndef CHEM_RHS
#define CHEM_RHS rhs
#endif
DEVICE int integrate(ScratchRecord* s) {

    int result = SUCCESS;
    bool done = false;

    if (s->tout <= s->t || s->safe <= 0.001 || s->safe >= 1.0 ||
        s->fac_min <= 0.0 || s->fac_max < 1.0) {
        return BAD_INPUTS;
    }
    for (size_type i = 0; i < N && !done; ++i) {
        if (s->atol_vec[i] <= 0.0 || s->rtol_vec[i] <= 10.0 * s->uround) {
            result = TOO_MUCH_ACCURACY_REQUESTED;
            done = true;
        }
    }

    if (done) return result;
    const Real hmaxn = s->tout - s->t;
    Real h = s->dt;
    if (math::abs(h) <= 10.0 * s->uround) {
        h = 1.e-6;
    }
    h = math::min(math::abs(h), hmaxn);

    bool reject = false;
    bool last = false;
    int nsing = 0;
    Real hacc = 0.0;
    Real erracc = 1.0;
    Real hopt = h;
    Real x = s->t;
    int n_step = 0;
    int n_accept = 0;

    while (!done) {
        if (n_step > s->max_steps) {
            s->t = x;
            s->dt = h;
            result = TOO_MANY_STEPS; done = true;
        }
        else if (0.1 * math::abs(h) <= math::abs(x) * s->uround) {
            s->t = x;
            s->dt = h;
            result = DT_UNDERFLOW; done = true;
        }
        else if (last) {
            s->t = x;
            s->dt = hopt;
            result = SUCCESS; done = true;
        }

        if (!done) {
            hopt = h;
            if (x + h * 1.0001 >= s->tout) {
                h = s->tout - x;
                last = true;
            }

            CHEM_EVAL_JACOBIAN(s, x);
            bool retry = true;

            while (retry && !done) {
                const Real fac = 1.0 / (h * C::gamma);
                if (decompose(s, fac) != 0) {
                    nsing += 1;
                    if (nsing >= 5) {
                        s->t = x;
                        s->dt = h;
                        result = LU_DECOMPOSITION_ERROR; done = true;
                        continue;
                    }
                    h *= 0.5;
                    reject = true;
                    last = false;
                    continue;
                }

                CHEM_RHS(x, s->y, s->ak1, s);
                solve(s, s->ak1);

                for (size_type i = 0; i < N; ++i) {
                    s->ynew[i] = s->y[i] + C::a21 * s->ak1[i];
                    s->ak2[i] = (C::c21 / h) * s->ak1[i];
                }
                CHEM_RHS(x + C::ct2 * h, s->ynew, s->dy, s);
                for (size_type i = 0; i < N; ++i) {
                    s->ak2[i] += s->dy[i];
                }
                solve(s, s->ak2);

                for (size_type i = 0; i < N; ++i) {
                    s->ynew[i] = s->y[i] + C::a31 * s->ak1[i] + C::a32 * s->ak2[i];
                    s->work[i] = (C::c31 * s->ak1[i] + C::c32 * s->ak2[i]) / h;
                }
                CHEM_RHS(x + h, s->ynew, s->dy, s);
                for (size_type i = 0; i < N; ++i) {
                    s->work[i] += s->dy[i];
                }
                solve(s, s->work);

                for (size_type i = 0; i < N; ++i) {
                    const Real ak3i = s->work[i];
                    s->ynew[i] = s->y[i] + C::b1 * s->ak1[i] + C::b2 * s->ak2[i] + C::b3 * ak3i;
                    s->work[i] = C::e1 * s->ak1[i] + C::e2 * s->ak2[i] + C::e3 * ak3i;
                }
                s->n_rhs += 3;
                n_step += 1;
                s->n_step = n_step;

                const Real err = error_norm(s);
                const Real raw_fac = math::cbrt(err) / s->safe;
                const Real lower_fac = 1.0 / s->fac_max;
                const Real upper_fac = 1.0 / s->fac_min;
                const Real fac_step = math::max(lower_fac, math::min(upper_fac, raw_fac));
                Real hnew = h / fac_step;
                if (err <= 1.0) {
                    n_accept += 1;
                    s->n_accept = n_accept;
                    if (n_accept > 1) {
                        const Real facgus = math::max(1.0 / s->fac_max,
                            math::min(1.0 / s->fac_min,
                            (hacc / h) *
                            math::cbrt((err * err) / erracc) /
                            s->safe));
                        hnew = h / math::max(fac_step, facgus);
                    }
                    hacc = h;
                    erracc = math::max(1.e-2, err);
                    for (size_type copy = 0; copy < N; ++copy) s->y[copy] = s->ynew[copy];
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
                        s->n_reject += 1;
                    }
                }
            }
        }
    }
    return result;

}
#undef CHEM_EVAL_JACOBIAN
#undef CHEM_RHS
