// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once

// Strict binary64 exp, log, and cbrt for the Loom translation. The upstream
// AMDGPU lowering supports native f64 arithmetic but not scalar.expf, logf, or
// cbrtf on f64. These are statement-for-statement transcriptions of the Loom
// math recipes (loom/src/loom/transforms/math/patterns_{exp,log,cbrt}.c on
// feat/amdgpu-f64-math-recipes) and use only integer, bitcast, conversion,
// comparison, and f64 add/sub/mul/div operations. One operation per statement
// keeps the recipe's evaluation order and prevents contraction. Every
// conditional selects between values that are already computed.
// Remove this file and bind math::exp/log/cbrt back to the scalar operations
// once those recipes land upstream.
//
// The exp and log argument reductions, polynomial coefficients, and rational
// approximations are adapted from OpenLibm src/e_exp.c and src/e_log.c
// (derived from fdlibm).
// Copyright (C) 2004 by Sun Microsystems, Inc. All rights reserved.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.

namespace chemistry {
namespace f64_math {
using u32 = unsigned int;
using u64 = unsigned long long;

DEVICE double exp(double x) {
    const u64 bits = __builtin_bit_cast(u64, x);
    const u64 magnitude = bits & 0x7fffffffffffffffULL;
    const bool needs_reduction = magnitude > 0x3fd62e4200000000ULL;
    const bool large_reduction = magnitude >= 0x3ff0a2b200000000ULL;
    const bool negative = (bits & 0x8000000000000000ULL) != 0;

    const double ln2_hi = 0.693147180369123816490;
    const double negative_ln2_hi = -0.693147180369123816490;
    const double ln2_lo = 1.90821492927058770002e-10;
    const double negative_ln2_lo = -1.90821492927058770002e-10;
    const double medium_ln2_hi = negative ? negative_ln2_hi : ln2_hi;
    const double medium_ln2_lo = negative ? negative_ln2_lo : ln2_lo;
    const double medium_hi = x - medium_ln2_hi;
    const int medium_k = negative ? -1 : 1;

    // The low 32 bits of the integer-valued shifted double encode the signed
    // reduction count. This avoids a target-unsupported f64-to-i32 conversion.
    const double shift = 0x1.8p52;
    const double scaled_input = 1.44269504088896338700 * x;
    const double shifted = scaled_input + shift;
    const int large_k = (int)__builtin_bit_cast(u64, shifted);
    const double dk = shifted - shift;
    const double large_hi_offset = dk * ln2_hi;
    const double large_hi = x - large_hi_offset;
    const double large_lo = dk * ln2_lo;
    const int reduced_k = large_reduction ? large_k : medium_k;
    const int k = needs_reduction ? reduced_k : 0;
    const double reduced_hi = large_reduction ? large_hi : medium_hi;
    const double hi = needs_reduction ? reduced_hi : x;
    const double reduced_lo = large_reduction ? large_lo : medium_ln2_lo;
    const double lo = needs_reduction ? reduced_lo : 0.0;

    const double r = hi - lo;
    const double t = r * r;
    const double poly5 = t * 4.13813679705723846039e-08;
    const double poly4 = -1.65339022054652515390e-06 + poly5;
    const double poly4_t = t * poly4;
    const double poly3 = 6.61375632143793436117e-05 + poly4_t;
    const double poly3_t = t * poly3;
    const double poly2 = -2.77777777770155933842e-03 + poly3_t;
    const double poly2_t = t * poly2;
    const double poly1 = 1.66666666666666019037e-01 + poly2_t;
    const double poly = t * poly1;
    const double c = r - poly;
    const double rc = r * c;
    const double c_minus_two = c - 2.0;
    const double ratio_zero = rc / c_minus_two;
    const double corrected_zero = ratio_zero - r;
    const double y_zero = 1.0 - corrected_zero;
    const double two_minus_c = 2.0 - c;
    const double ratio_nonzero = rc / two_minus_c;
    const double corrected_lo = lo - ratio_nonzero;
    const double corrected_hi = corrected_lo - hi;
    const double y_nonzero = 1.0 - corrected_hi;
    const bool k_is_zero = k == 0;
    const double y = k_is_zero ? y_zero : y_nonzero;

    const double normal_scale =
        __builtin_bit_cast(double, (u64)(k + 1023) << 52);
    const double normal_result = y * normal_scale;
    const double subnormal_scale =
        __builtin_bit_cast(double, (u64)(k + 2023) << 52);
    const double subnormal_scaled = y * subnormal_scale;
    const double subnormal_result = subnormal_scaled * 0x1p-1000;
    const bool is_normal = k >= -1021;
    const double scaled = is_normal ? normal_result : subnormal_result;
    const bool is_highest_k = k == 1024;
    const double doubled_y = y * 2.0;
    const double highest_result = doubled_y * 0x1p1023;
    const double finite_result = is_highest_k ? highest_result : scaled;
    const double unscaled_result = k_is_zero ? y_zero : finite_result;

    const bool overflows = x > 709.782712893383973096;
    const bool underflows = x < -745.133219101941108420;
    const bool is_one = x == 1.0;
    const double infinity = __builtin_bit_cast(double, 0x7ff0000000000000ULL);
    const double with_one = is_one ? 2.718281828459045235360 : unscaled_result;
    const double with_underflow = underflows ? 0.0 : with_one;
    const double with_overflow = overflows ? infinity : with_underflow;
    const bool is_nan = magnitude > 0x7ff0000000000000ULL;
    const double quiet_nan =
        __builtin_bit_cast(double, bits | 0x0008000000000000ULL);
    return is_nan ? quiet_nan : with_overflow;
}

DEVICE double log(double x) {
    const u64 bits = __builtin_bit_cast(u64, x);
    const u64 magnitude = bits & 0x7fffffffffffffffULL;
    const bool subnormal = magnitude < 0x0010000000000000ULL;
    const double scaled_subnormal = x * 0x1p54;
    const double normalized_input = subnormal ? scaled_subnormal : x;
    const u64 normalized_bits = __builtin_bit_cast(u64, normalized_input);

    const u32 high_word = (u32)(normalized_bits >> 32);
    const u32 exponent = high_word >> 20;
    const int unbiased_exponent = (int)exponent - 1023;
    const int offset = subnormal ? -54 : 0;
    const int initial_k = unbiased_exponent + offset;
    const u32 hx = high_word & 0x000fffffu;
    const u32 rounded_hx = hx + 0x95f64u;
    const u32 i = rounded_hx & 0x100000u;
    const u32 normalized_exponent = i ^ 0x3ff00000u;
    const u32 normalized_high = hx | normalized_exponent;
    const u64 high_bits = (u64)normalized_high << 32;
    const u64 low_bits = normalized_bits & 0xffffffffULL;
    const double reduced = __builtin_bit_cast(double, high_bits | low_bits);
    const u32 exponent_adjustment = i >> 20;
    const int k = initial_k + (int)exponent_adjustment;
    const double dk = (double)k;

    const double f = reduced - 1.0;
    const double two_plus_f = 2.0 + f;
    const double s = f / two_plus_f;
    const double z = s * s;
    const double w = z * z;
    const double even6 = w * 1.531383769920937332e-01;
    const double even4 = 2.222219843214978396e-01 + even6;
    const double even4w = w * even4;
    const double even2 = 3.999999999940941908e-01 + even4w;
    const double t1 = w * even2;
    const double odd7 = w * 1.479819860511658591e-01;
    const double odd5 = 1.818357216161805012e-01 + odd7;
    const double odd5w = w * odd5;
    const double odd3 = 2.857142874366239149e-01 + odd5w;
    const double odd3w = w * odd3;
    const double odd1 = 6.666666666666735130e-01 + odd3w;
    const double t2 = z * odd1;
    const double r = t1 + t2;
    const double hi = dk * 6.93147180369123816490e-01;
    const double lo = dk * 1.90821492927058770002e-10;
    const double half_f = 0.5 * f;
    const double hfsq = half_f * f;
    const double hfsq_plus_r = hfsq + r;
    const double s_hfsq_plus_r = s * hfsq_plus_r;
    const double hfsq_correction = hfsq - s_hfsq_plus_r;
    const double hfsq_zero = f - hfsq_correction;
    const double hfsq_plus_lo = s_hfsq_plus_r + lo;
    const double hfsq_nonzero_correction = hfsq - hfsq_plus_lo;
    const double hfsq_nonzero_adjusted = hfsq_nonzero_correction - f;
    const double hfsq_nonzero = hi - hfsq_nonzero_adjusted;
    const double f_minus_r = f - r;
    const double s_f_minus_r = s * f_minus_r;
    const double direct_zero = f - s_f_minus_r;
    const double direct_nonzero_correction = s_f_minus_r - lo;
    const double direct_nonzero_adjusted = direct_nonzero_correction - f;
    const double direct_nonzero = hi - direct_nonzero_adjusted;
    const bool k_is_zero = k == 0;
    const double hfsq_result = k_is_zero ? hfsq_zero : hfsq_nonzero;
    const double direct_result = k_is_zero ? direct_zero : direct_nonzero;
    const bool below_lower = hx < 0x6147au;
    const bool above_upper = hx > 0x6b851u;
    const bool use_hfsq = below_lower ? below_lower : above_upper;
    const double finite_result = use_hfsq ? hfsq_result : direct_result;

    const bool is_zero = magnitude == 0;
    const bool is_infinite = magnitude == 0x7ff0000000000000ULL;
    const bool is_nan = magnitude > 0x7ff0000000000000ULL;
    const bool negative = (bits & 0x8000000000000000ULL) != 0;
    const double negative_infinity =
        __builtin_bit_cast(double, 0xfff0000000000000ULL);
    const double canonical_nan =
        __builtin_bit_cast(double, 0x7ff8000000000000ULL);
    const double quiet_nan =
        __builtin_bit_cast(double, bits | 0x0008000000000000ULL);
    const double with_infinity = is_infinite ? x : finite_result;
    const double with_negative = negative ? canonical_nan : with_infinity;
    const double with_zero = is_zero ? negative_infinity : with_negative;
    return is_nan ? quiet_nan : with_zero;
}

// Normalize |x| to a mantissa in [1, 8), estimate its cube root by a line,
// and refine six times in f64. The exponent split uses an exact reciprocal
// multiply for n in [0, 2097], avoiding integer division. Keeping the exponent
// scale separate makes the Newton divisions safe for subnormals and values
// near DBL_MAX. Zeros and infinities retain their bit patterns; NaNs retain
// their payloads and have their quiet bit set.
DEVICE double cbrt(double x) {
    const u64 bits = __builtin_bit_cast(u64, x);
    const u64 magnitude = bits & 0x7fffffffffffffffULL;
    const double positive = __builtin_bit_cast(double, magnitude);
    const bool subnormal = magnitude < 0x0010000000000000ULL;
    const double scaled_subnormal = positive * 0x1p54;
    const double normalized_value = subnormal ? scaled_subnormal : positive;
    const u64 normalized = __builtin_bit_cast(u64, normalized_value);
    const u64 shifted_exponent = normalized >> 52;
    const u64 exponent_bits = shifted_exponent & 2047;
    const int biased_exponent = (int)exponent_bits;
    const int unbiased_exponent = biased_exponent - 1023;
    const int exponent_shift = subnormal ? 54 : 0;
    const int exponent = unbiased_exponent - exponent_shift;
    const int n = exponent + 1074;
    const int reciprocal_product = n * 43691;
    const int q_positive = reciprocal_product >> 17;
    const int three_q = 3 * q_positive;
    const int remainder = n - three_q;

    const u64 mantissa_bits = normalized & 0x000fffffffffffffULL;
    const u64 unit_mantissa_bits = mantissa_bits | 0x3ff0000000000000ULL;
    const double mantissa = __builtin_bit_cast(double, unit_mantissa_bits);
    const int remainder_scale = 1 << remainder;
    const double remainder_scale_f64 = (double)remainder_scale;
    const double t = mantissa * remainder_scale_f64;
    const double seed_scaled = 0.25 * t;
    const double y0 = 0.75 + seed_scaled;
    const double twice_y0 = 2.0 * y0;
    const double y0_squared = y0 * y0;
    const double y1 = (twice_y0 + t / y0_squared) / 3.0;
    const double twice_y1 = 2.0 * y1;
    const double y1_squared = y1 * y1;
    const double y2 = (twice_y1 + t / y1_squared) / 3.0;
    const double twice_y2 = 2.0 * y2;
    const double y2_squared = y2 * y2;
    const double y3 = (twice_y2 + t / y2_squared) / 3.0;
    const double twice_y3 = 2.0 * y3;
    const double y3_squared = y3 * y3;
    const double y4 = (twice_y3 + t / y3_squared) / 3.0;
    const double twice_y4 = 2.0 * y4;
    const double y4_squared = y4 * y4;
    const double y5 = (twice_y4 + t / y4_squared) / 3.0;
    const double twice_y5 = 2.0 * y5;
    const double y5_squared = y5 * y5;
    const double y = (twice_y5 + t / y5_squared) / 3.0;

    const int scale_exponent = q_positive + 665;
    const u64 scale_bits = (u64)scale_exponent << 52;
    const double scale = __builtin_bit_cast(double, scale_bits);
    const double scaled_result = y * scale;
    const u64 unsigned_result_bits = __builtin_bit_cast(u64, scaled_result);
    const u64 sign_bits = bits & 0x8000000000000000ULL;
    const double signed_result =
        __builtin_bit_cast(double, unsigned_result_bits | sign_bits);

    const bool is_zero = magnitude == 0;
    const bool is_nonfinite = magnitude >= 0x7ff0000000000000ULL;
    const bool is_special = is_zero ? is_zero : is_nonfinite;
    const bool is_nan = magnitude > 0x7ff0000000000000ULL;
    const u64 nan_quiet_mask = is_nan ? 0x0008000000000000ULL : 0;
    const double special_result =
        __builtin_bit_cast(double, bits | nan_quiet_mask);
    return is_special ? special_result : signed_result;
}
} // namespace f64_math
} // namespace chemistry
