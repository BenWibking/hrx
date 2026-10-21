// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once

// Binary64 exp, log, and cbrt for the Loom translation. The upstream AMDGPU
// lowering does not support scalar.expf, logf, or cbrtf on f64. These are
// statement-for-statement transcriptions of AMD's OCML (ROCm-Device-Libs
// ocml/src/expD_base.h, logD_base.h with ep.h, and cbrtD.cl at llvm-project
// amd/device-libs d366fa84f3fd). They use f64 add/sub/mul/div, explicit fma,
// f32 approximate log2/exp2, Loom's frexp and ldexp, conversions, and integer
// and bitcast operations. Loom has no f64 rint or fast reciprocal, so the
// transcription substitutes:
//   rint(v)      (v + 0x1.8p52) - 0x1.8p52, exact for |v| < 2^51.
//   MATH_FAST_RCP and MATH_FAST_DIV
//                IEEE f64 division.
// One operation per statement keeps OCML's evaluation order and prevents
// contraction. Every conditional selects between values that are already
// computed. Remove this file and bind math::exp/log/cbrt back to the scalar
// operations once Loom lowers them for f64.
//
// The algorithms and coefficients are adapted from ROCm-Device-Libs, which
// carries the following license.
//
// University of Illinois/NCSA Open Source License
//
// Copyright (c) 2014-2016, Advanced Micro Devices, Inc.
// All rights reserved.
//
// Developed by:
//
//     AMD Research and AMD HSA Software Development
//
//     Advanced Micro Devices, Inc.
//
//     www.amd.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// with the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimers.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimers in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the names of the LLVM Team, University of Illinois at
//       Urbana-Champaign, nor the names of its contributors may be used to
//       endorse or promote products derived from this Software without
//       specific prior written permission.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH
// THE SOFTWARE.

#include <loomcxx/scalar.h>

namespace chemistry {
namespace f64_math {
using u64 = unsigned long long;

// MATH_MAD: a * b + c with a single rounding.
DEVICE double fma(double a, double b, double c) {
    return loom::scalar::fmaf(a, b, c);
}

// BUILTIN_AMDGPU_LOG2_F32 and BUILTIN_AMDGPU_EXP2_F32 (v_log_f32, v_exp_f32).
DEVICE float amdgpu_log2(float x) {
    return loom::scalar::approximate::log2f(x);
}
DEVICE float amdgpu_exp2(float x) {
    return loom::scalar::approximate::exp2f(x);
}

// BUILTIN_FREXP_MANT_F64/BUILTIN_FREXP_EXP_F64 and BUILTIN_FLDEXP_F64
// (v_frexp_mant_f64, v_frexp_exp_i32_f64, v_ldexp_f64).
DEVICE loom::scalar::frexpf_result<double> frexp(double x) {
    return loom::scalar::frexpf(x);
}
DEVICE double ldexp(double x, int n) {
    return loom::scalar::ldexpf(x, n);
}

// expD_base.h
DEVICE double exp(double x) {
    // dn = rint(x * log2(e)). The low 32 bits of the integer-valued shifted
    // double encode (int)dn. This avoids a target-unsupported f64-to-i32
    // conversion.
    const double shift = 0x1.8p52;
    const double scaled_input = x * 0x1.71547652b82fep+0;
    const double shifted = scaled_input + shift;
    const double dn = shifted - shift;
    const int n = (int)__builtin_bit_cast(u64, shifted);

    const double negative_dn = -dn;
    const double t_hi = fma(negative_dn, 0x1.62e42fefa39efp-1, x);
    const double t = fma(negative_dn, 0x1.abc9e3b39803fp-56, t_hi);

    const double p10 = fma(t, 0x1.ade156a5dcb37p-26, 0x1.28af3fca7ab0cp-22);
    const double p9 = fma(t, p10, 0x1.71dee623fde64p-19);
    const double p8 = fma(t, p9, 0x1.a01997c89e6b0p-16);
    const double p7 = fma(t, p8, 0x1.a01a014761f6ep-13);
    const double p6 = fma(t, p7, 0x1.6c16c1852b7b0p-10);
    const double p5 = fma(t, p6, 0x1.1111111122322p-7);
    const double p4 = fma(t, p5, 0x1.55555555502a1p-5);
    const double p3 = fma(t, p4, 0x1.5555555555511p-3);
    const double p2 = fma(t, p3, 0x1.000000000000bp-1);
    const double p1 = fma(t, p2, 1.0);
    const double p = fma(t, p1, 1.0);

    const double z = ldexp(p, n);

    // NaN inputs propagate through the arithmetic above.
    const double infinity = __builtin_bit_cast(double, 0x7ff0000000000000ULL);
    const bool overflows = x > 1024.0;
    const bool underflows = x < -1075.0;
    const double with_overflow = overflows ? infinity : z;
    return underflows ? 0.0 : with_overflow;
}

// logD_base.h, with the ep.h double-double helpers expanded in place.
DEVICE double log(double a) {
    const u64 bits = __builtin_bit_cast(u64, a);
    const u64 magnitude = bits & 0x7fffffffffffffffULL;

    // Signed, zero, and non-finite inputs produce unused values that the
    // selects below replace.
    const auto parts = frexp(a);

    // b = m < 2/3; m = ldexp(m, b); e = frexp_exp(a) - b.
    const bool below_two_thirds = parts.fraction < 0x1.5555555555555p-1;
    const int b = below_two_thirds ? 1 : 0;
    const double m = ldexp(parts.fraction, b);
    const int e = parts.exponent - b;

    // x = div(m - 1.0, fadd(1.0, m))
    const double numerator = m - 1.0;
    const double denominator_hi = 1.0 + m;
    const double denominator_hi_minus_one = denominator_hi - 1.0;
    const double denominator_lo = m - denominator_hi_minus_one;
    const double reciprocal = 1.0 / denominator_hi;
    const double q_hi = numerator * reciprocal;
    const double product_hi = q_hi * denominator_hi;
    const double negative_product_hi = -product_hi;
    const double product_lo_hi = fma(q_hi, denominator_hi, negative_product_hi);
    const double product_lo = fma(q_hi, denominator_lo, product_lo_hi);
    const double normalized_product_hi = product_hi + product_lo;
    const double product_hi_change = normalized_product_hi - product_hi;
    const double normalized_product_lo = product_lo - product_hi_change;
    const double difference_hi = numerator - normalized_product_hi;
    const double numerator_minus_difference = numerator - difference_hi;
    const double difference_lo_hi =
        numerator_minus_difference - normalized_product_hi;
    const double difference_lo = difference_lo_hi - normalized_product_lo;
    const double difference = difference_hi + difference_lo;
    const double q_lo = difference * reciprocal;
    const double x_hi = q_hi + q_lo;
    const double x_hi_change = x_hi - q_hi;
    const double x_lo = q_lo - x_hi_change;

    const double s = x_hi * x_hi;
    const double p5 = fma(s, 0x1.3ab76bf559e2bp-3, 0x1.385386b47b09ap-3);
    const double p4 = fma(s, p5, 0x1.7474dd7f4df2ep-3);
    const double p3 = fma(s, p4, 0x1.c71c016291751p-3);
    const double p2 = fma(s, p3, 0x1.249249b27acf1p-2);
    const double p1 = fma(s, p2, 0x1.99999998ef7b6p-2);
    const double p = fma(s, p1, 0x1.5555555555780p-1);

    // r = fadd(ldx(x, 1), s * x.hi * p)
    const double twice_x_hi = x_hi * 2.0;
    const double twice_x_lo = x_lo * 2.0;
    const double s_x_hi = s * x_hi;
    const double tail = s_x_hi * p;
    const double sum_hi = twice_x_hi + tail;
    const double sum_hi_change = sum_hi - twice_x_hi;
    const double sum_lo_hi = tail - sum_hi_change;
    const double sum_lo = sum_lo_hi + twice_x_lo;
    const double r_hi = sum_hi + sum_lo;
    const double r_hi_change = r_hi - sum_hi;
    const double r_lo = sum_lo - r_hi_change;

    // r = add(mul(con(ln2_hi, ln2_lo), (double)e), r)
    const double ln2_hi = 0x1.62e42fefa39efp-1;
    const double ln2_lo = 0x1.abc9e3b39803fp-56;
    const double de = (double)e;
    const double scaled_hi = ln2_hi * de;
    const double negative_scaled_hi = -scaled_hi;
    const double scaled_lo_hi = fma(ln2_hi, de, negative_scaled_hi);
    const double scaled_lo = fma(ln2_lo, de, scaled_lo_hi);
    const double l_hi = scaled_hi + scaled_lo;
    const double l_hi_change = l_hi - scaled_hi;
    const double l_lo = scaled_lo - l_hi_change;

    const double high_sum = l_hi + r_hi;
    const double high_r_part = high_sum - l_hi;
    const double high_l_part = high_sum - high_r_part;
    const double high_l_error = l_hi - high_l_part;
    const double high_r_error = r_hi - high_r_part;
    const double high_error = high_l_error + high_r_error;
    const double low_sum = l_lo + r_lo;
    const double low_r_part = low_sum - l_lo;
    const double low_l_part = low_sum - low_r_part;
    const double low_l_error = l_lo - low_l_part;
    const double low_r_error = r_lo - low_r_part;
    const double low_error = low_l_error + low_r_error;
    const double total_lo_hi = high_error + low_sum;
    const double total_hi = high_sum + total_lo_hi;
    const double total_hi_change = total_hi - high_sum;
    const double total_lo_lo = total_lo_hi - total_hi_change;
    const double total_lo = total_lo_lo + low_error;
    const double result = total_hi + total_lo;

    // OCML's NaN result comes from propagation through frexp. Setting the
    // quiet bit produces the same value without depending on the propagated
    // payload.
    const bool is_infinite = magnitude == 0x7ff0000000000000ULL;
    const bool is_nan = magnitude > 0x7ff0000000000000ULL;
    const bool negative = a < 0.0;
    const bool is_zero = a == 0.0;
    const double negative_infinity =
        __builtin_bit_cast(double, 0xfff0000000000000ULL);
    const double canonical_nan =
        __builtin_bit_cast(double, 0x7ff8000000000000ULL);
    const double quiet_nan =
        __builtin_bit_cast(double, bits | 0x0008000000000000ULL);
    const double with_infinity = is_infinite ? a : result;
    const double with_negative = negative ? canonical_nan : with_infinity;
    const double with_zero = is_zero ? negative_infinity : with_negative;
    return is_nan ? quiet_nan : with_zero;
}

// cbrtD.cl
DEVICE double cbrt(double x) {
    const u64 bits = __builtin_bit_cast(u64, x);
    const u64 magnitude = bits & 0x7fffffffffffffffULL;
    const double a = __builtin_bit_cast(double, magnitude);

    // Zeros, infinities, and NaNs produce unused values that the final select
    // replaces.
    const int e3 = frexp(a).exponent;

    // e = rint(e3 / 3). e3 / 3 is never a tie, so this is
    // floor((e3 + 1) / 3). Adding 3 * 359 keeps the dividend in [2, 2102],
    // where (n * 43691) >> 17 is exactly floor(n / 3).
    const int biased_dividend = e3 + 1078;
    const int reciprocal_product = biased_dividend * 43691;
    const int biased_quotient = reciprocal_product >> 17;
    const int e = biased_quotient - 359;

    // a = ldexp(a, -3 * e)
    const int three_e = 3 * e;
    const int negative_three_e = -three_e;
    const double reduced = ldexp(a, negative_three_e);

    const float reduced_f32 = (float)reduced;
    const float log2_reduced = amdgpu_log2(reduced_f32);
    const float third_log2 = 0x1.555556p-2f * log2_reduced;
    const float seed = amdgpu_exp2(third_log2);
    const double c = (double)seed;
    const double c2 = c * c;
    const double negative_c = -c;
    const double residual = fma(negative_c, c2, reduced);
    const double twice_c = c + c;
    const double denominator = fma(twice_c, c2, reduced);
    const double correction = residual / denominator;
    const double root = fma(c, correction, c);

    // c = ldexp(c, e)
    const double scaled_root = ldexp(root, e);

    // Zeros, infinities, and NaNs return x unchanged.
    const bool is_nonzero = magnitude != 0;
    const bool is_finite = magnitude < 0x7ff0000000000000ULL;
    const bool is_regular = is_nonzero ? is_finite : is_nonzero;
    const double unsigned_result = is_regular ? scaled_root : x;
    const u64 unsigned_bits = __builtin_bit_cast(u64, unsigned_result);
    const u64 result_magnitude = unsigned_bits & 0x7fffffffffffffffULL;
    const u64 sign_bits = bits & 0x8000000000000000ULL;
    const u64 result_bits = result_magnitude | sign_bits;
    return __builtin_bit_cast(double, result_bits);
}
} // namespace f64_math
} // namespace chemistry
