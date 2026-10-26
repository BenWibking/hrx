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

    // Estrin evaluation of the same degree-12 polynomial: p = 1 + (t + t^2 Q(t)).
    // Half the dependency depth of Horner; <= 1 ulp from the Horner result.
    const double t2 = t * t;
    const double t4 = t2 * t2;
    const double t8 = t4 * t4;
    const double q0 = fma(t, 0x1.5555555555511p-3, 0x1.000000000000bp-1);
    const double q1 = fma(t, 0x1.1111111122322p-7, 0x1.55555555502a1p-5);
    const double q2 = fma(t, 0x1.a01a014761f6ep-13, 0x1.6c16c1852b7b0p-10);
    const double q3 = fma(t, 0x1.71dee623fde64p-19, 0x1.a01997c89e6b0p-16);
    const double q4 = fma(t, 0x1.ade156a5dcb37p-26, 0x1.28af3fca7ab0cp-22);
    const double r0 = fma(t2, q1, q0);
    const double r1 = fma(t2, q3, q2);
    const double s0 = fma(t4, r1, r0);
    const double q = fma(t8, q4, s0);
    const double p = 1.0 + fma(t2, q, t);

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
    // Work on the high word with shifts and VOP2-encodable literals: the
    // 64-bit masks and select-between-constants of the direct transcription
    // become SGPR constants that spill and reload on every call.
    const u64 bits = __builtin_bit_cast(u64, a);
    const unsigned hi = (unsigned)(bits >> 32);
    const unsigned exponent_field = (hi << 1) >> 21;

    // m = frexp_mant(a) in [0.5, 1) and its exponent. Signed, zero, and
    // non-finite inputs produce unused values that the selects below replace.
    const bool subnormal = exponent_field == 0u;
    const double scaled_subnormal = a * 0x1p54;
    const double normalized_input = subnormal ? scaled_subnormal : a;
    const u64 normalized_bits = __builtin_bit_cast(u64, normalized_input);
    const unsigned normalized_hi = (unsigned)(normalized_bits >> 32);
    const unsigned normalized_lo = (unsigned)normalized_bits;
    const int biased_exponent = (int)((normalized_hi << 1) >> 21);
    const int subnormal_shift = subnormal ? 54 : 0;
    const int frexp_exponent = (biased_exponent - 1022) - subnormal_shift;
    const unsigned half_mantissa_hi = ((normalized_hi << 12) >> 12) | 0x3fe00000u;
    const u64 half_mantissa_bits = ((u64)half_mantissa_hi << 32) | (u64)normalized_lo;
    const double frexp_mantissa = __builtin_bit_cast(double, half_mantissa_bits);

    // b = m < 2/3; m = ldexp(m, b); e = frexp_exp(a) - b.
    // fdlibm-style reduction: m in [sqrt(1/2), sqrt(2)), s = f / (2 + f),
    // single-precision-free polynomial in s^2 (< 1 ulp; not bitwise vs the
    // double-double recipe, which it replaces for speed).
    const bool below = frexp_mantissa < 0x1.6a09e667f3bcdp-1;
    const double m = below ? frexp_mantissa * 2.0 : frexp_mantissa;
    const int e = frexp_exponent - (below ? 1 : 0);
    const double f = m - 1.0;
    const double hfsq = 0.5 * f * f;
    const double s = f / (2.0 + f);
    const double dk = (double)e;
    const double z = s * s;
    const double w = z * z;
    const double t1 = w * fma(w, fma(w, 1.531383769920937332e-01,
        2.222219843214978396e-01), 3.999999999940941908e-01);
    const double t2 = z * fma(w, fma(w, fma(w, 1.479819860511658591e-01,
        1.818357216161805012e-01), 2.857142874366239149e-01),
        6.666666666666735130e-01);
    const double R = t2 + t1;
    const double result = dk * 6.93147180369123816490e-01 -
        ((hfsq - (s * (hfsq + R) + dk * 1.90821492927058770002e-10)) - f);

    const bool is_nan = a != a;
    const bool is_infinite = (exponent_field == 2047u) & !is_nan;
    const bool negative = a < 0.0;
    const bool is_zero = a == 0.0;
    const double negative_infinity =
        __builtin_bit_cast(double, 0xfff0000000000000ULL);
    const double canonical_nan =
        __builtin_bit_cast(double, 0x7ff8000000000000ULL);
    const unsigned quiet_hi = hi | 0x00080000u;
    const double quiet_nan = __builtin_bit_cast(
        double, ((u64)quiet_hi << 32) | (u64)(unsigned)bits);
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
