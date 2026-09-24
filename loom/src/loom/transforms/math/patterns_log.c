// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The argument reduction and polynomial below are adapted from OpenLibm
// src/e_log.c (derived from fdlibm).
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.

#include <stdint.h>

#include "loom/ir/attribute.h"
#include "loom/ops/scalar/ops.h"
#include "loom/ops/scf/ops.h"
#include "loom/transforms/math/patterns.h"

static iree_status_t loom_math_legalize_build_log_f64(
    loom_builder_t* builder, loom_value_id_t input, uint8_t fastmath_flags,
    loom_location_id_t location, loom_value_id_t* out_value) {
  const loom_type_t i1 = loom_type_scalar(LOOM_SCALAR_TYPE_I1);
  const loom_type_t i32 = loom_type_scalar(LOOM_SCALAR_TYPE_I32);
  const loom_type_t i64 = loom_type_scalar(LOOM_SCALAR_TYPE_I64);
  const loom_type_t f64 = loom_type_scalar(LOOM_SCALAR_TYPE_F64);

#define LOG_OP(name, build_call)    \
  loom_op_t* name##_op = NULL;      \
  IREE_RETURN_IF_ERROR(build_call); \
  const loom_value_id_t name = loom_op_results(name##_op)[0]
#define LOG_I32(name, value)                                                  \
  LOG_OP(name, loom_scalar_constant_build(builder, loom_attr_i64(value), i32, \
                                          location, &name##_op))
#define LOG_I64(name, value)                                                  \
  LOG_OP(name, loom_scalar_constant_build(builder, loom_attr_i64(value), i64, \
                                          location, &name##_op))
#define LOG_F64(name, value)                                                  \
  LOG_OP(name, loom_scalar_constant_build(builder, loom_attr_f64(value), f64, \
                                          location, &name##_op))
#define LOG_ADD(name, lhs, rhs)                                               \
  LOG_OP(name, loom_scalar_addf_build(builder, fastmath_flags, lhs, rhs, f64, \
                                      location, &name##_op))
#define LOG_SUB(name, lhs, rhs)                                               \
  LOG_OP(name, loom_scalar_subf_build(builder, fastmath_flags, lhs, rhs, f64, \
                                      location, &name##_op))
#define LOG_MUL(name, lhs, rhs)                                               \
  LOG_OP(name, loom_scalar_mulf_build(builder, fastmath_flags, lhs, rhs, f64, \
                                      location, &name##_op))
#define LOG_SELECT(name, predicate, on_true, on_false, type)                \
  LOG_OP(name, loom_scf_select_build(builder, predicate, on_true, on_false, \
                                     type, location, &name##_op))

  LOG_OP(bits, loom_scalar_bitcast_build(builder, input, f64, i64, location,
                                         &bits_op));
  LOG_I64(magnitude_mask, INT64_MAX);
  LOG_OP(magnitude, loom_scalar_andi_build(builder, bits, magnitude_mask, i64,
                                           location, &magnitude_op));
  LOG_I64(min_normal_bits, INT64_C(0x0010000000000000));
  LOG_OP(subnormal, loom_scalar_cmpi_build(
                        builder, LOOM_SCALAR_CMPI_PREDICATE_ULT, magnitude,
                        min_normal_bits, location, &subnormal_op));
  LOG_F64(subnormal_scale, 0x1p54);
  LOG_MUL(scaled_subnormal, input, subnormal_scale);
  LOG_SELECT(normalized_input, subnormal, scaled_subnormal, input, f64);
  LOG_OP(normalized_bits,
         loom_scalar_bitcast_build(builder, normalized_input, f64, i64,
                                   location, &normalized_bits_op));

  LOG_I64(shift32, 32);
  LOG_OP(high_word64, loom_scalar_shrui_build(builder, normalized_bits, shift32,
                                              i64, location, &high_word64_op));
  LOG_OP(high_word, loom_scalar_trunci_build(builder, high_word64, i64, i32,
                                             location, &high_word_op));
  LOG_I32(shift20, 20);
  LOG_OP(exponent, loom_scalar_shrui_build(builder, high_word, shift20, i32,
                                           location, &exponent_op));
  LOG_I32(bias, 1023);
  LOG_OP(unbiased_exponent,
         loom_scalar_subi_build(builder, 0, exponent, bias, i32, location,
                                &unbiased_exponent_op));
  LOG_I32(subnormal_offset, -54);
  LOG_I32(zero32, 0);
  LOG_SELECT(offset, subnormal, subnormal_offset, zero32, i32);
  LOG_OP(initial_k,
         loom_scalar_addi_build(builder, 0, unbiased_exponent, offset, i32,
                                location, &initial_k_op));
  LOG_I32(mantissa_mask32, INT32_C(0x000fffff));
  LOG_OP(hx, loom_scalar_andi_build(builder, high_word, mantissa_mask32, i32,
                                    location, &hx_op));
  LOG_I32(rounding_offset, INT32_C(0x95f64));
  LOG_OP(rounded_hx, loom_scalar_addi_build(builder, 0, hx, rounding_offset,
                                            i32, location, &rounded_hx_op));
  LOG_I32(normalize_bit, INT32_C(0x100000));
  LOG_OP(i, loom_scalar_andi_build(builder, rounded_hx, normalize_bit, i32,
                                   location, &i_op));
  LOG_I32(one_exponent, INT32_C(0x3ff00000));
  LOG_OP(normalized_exponent,
         loom_scalar_xori_build(builder, i, one_exponent, i32, location,
                                &normalized_exponent_op));
  LOG_OP(normalized_high,
         loom_scalar_ori_build(builder, hx, normalized_exponent, i32, location,
                               &normalized_high_op));
  LOG_OP(normalized_high64,
         loom_scalar_extui_build(builder, normalized_high, i32, i64, location,
                                 &normalized_high64_op));
  LOG_OP(high_bits,
         loom_scalar_shli_build(builder, 0, normalized_high64, shift32, i64,
                                location, &high_bits_op));
  LOG_I64(low_word_mask, INT64_C(0xffffffff));
  LOG_OP(low_bits,
         loom_scalar_andi_build(builder, normalized_bits, low_word_mask, i64,
                                location, &low_bits_op));
  LOG_OP(reduced_bits, loom_scalar_ori_build(builder, high_bits, low_bits, i64,
                                             location, &reduced_bits_op));
  LOG_OP(reduced, loom_scalar_bitcast_build(builder, reduced_bits, i64, f64,
                                            location, &reduced_op));
  LOG_OP(exponent_adjustment,
         loom_scalar_shrui_build(builder, i, shift20, i32, location,
                                 &exponent_adjustment_op));
  LOG_OP(k, loom_scalar_addi_build(builder, 0, initial_k, exponent_adjustment,
                                   i32, location, &k_op));
  LOG_OP(dk, loom_scalar_sitofp_build(builder, k, i32, f64, location, &dk_op));

  LOG_F64(one, 1.0);
  LOG_F64(two, 2.0);
  LOG_SUB(f, reduced, one);
  LOG_ADD(two_plus_f, two, f);
  LOG_OP(s, loom_scalar_divf_build(builder, fastmath_flags, f, two_plus_f, f64,
                                   location, &s_op));
  LOG_MUL(z, s, s);
  LOG_MUL(w, z, z);
  LOG_F64(lg1, 6.666666666666735130e-01);
  LOG_F64(lg2, 3.999999999940941908e-01);
  LOG_F64(lg3, 2.857142874366239149e-01);
  LOG_F64(lg4, 2.222219843214978396e-01);
  LOG_F64(lg5, 1.818357216161805012e-01);
  LOG_F64(lg6, 1.531383769920937332e-01);
  LOG_F64(lg7, 1.479819860511658591e-01);
  LOG_MUL(even6, w, lg6);
  LOG_ADD(even4, lg4, even6);
  LOG_MUL(even4w, w, even4);
  LOG_ADD(even2, lg2, even4w);
  LOG_MUL(t1, w, even2);
  LOG_MUL(odd7, w, lg7);
  LOG_ADD(odd5, lg5, odd7);
  LOG_MUL(odd5w, w, odd5);
  LOG_ADD(odd3, lg3, odd5w);
  LOG_MUL(odd3w, w, odd3);
  LOG_ADD(odd1, lg1, odd3w);
  LOG_MUL(t2, z, odd1);
  LOG_ADD(r, t1, t2);
  LOG_F64(ln2_hi, 6.93147180369123816490e-01);
  LOG_F64(ln2_lo, 1.90821492927058770002e-10);
  LOG_MUL(hi, dk, ln2_hi);
  LOG_MUL(lo, dk, ln2_lo);
  LOG_F64(half, 0.5);
  LOG_MUL(half_f, half, f);
  LOG_MUL(hfsq, half_f, f);
  LOG_ADD(hfsq_plus_r, hfsq, r);
  LOG_MUL(s_hfsq_plus_r, s, hfsq_plus_r);
  LOG_SUB(hfsq_correction, hfsq, s_hfsq_plus_r);
  LOG_SUB(hfsq_zero, f, hfsq_correction);
  LOG_ADD(hfsq_plus_lo, s_hfsq_plus_r, lo);
  LOG_SUB(hfsq_nonzero_correction, hfsq, hfsq_plus_lo);
  LOG_SUB(hfsq_nonzero_adjusted, hfsq_nonzero_correction, f);
  LOG_SUB(hfsq_nonzero, hi, hfsq_nonzero_adjusted);
  LOG_SUB(f_minus_r, f, r);
  LOG_MUL(s_f_minus_r, s, f_minus_r);
  LOG_SUB(direct_zero, f, s_f_minus_r);
  LOG_SUB(direct_nonzero_correction, s_f_minus_r, lo);
  LOG_SUB(direct_nonzero_adjusted, direct_nonzero_correction, f);
  LOG_SUB(direct_nonzero, hi, direct_nonzero_adjusted);
  LOG_OP(k_is_zero,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_EQ, k,
                                zero32, location, &k_is_zero_op));
  LOG_SELECT(hfsq_result, k_is_zero, hfsq_zero, hfsq_nonzero, f64);
  LOG_SELECT(direct_result, k_is_zero, direct_zero, direct_nonzero, f64);
  LOG_I32(hx_lower, INT32_C(0x6147a));
  LOG_I32(hx_upper, INT32_C(0x6b851));
  LOG_OP(below_lower,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_ULT, hx,
                                hx_lower, location, &below_lower_op));
  LOG_OP(above_upper,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_UGT, hx,
                                hx_upper, location, &above_upper_op));
  LOG_SELECT(use_hfsq, below_lower, below_lower, above_upper, i1);
  LOG_SELECT(finite_result, use_hfsq, hfsq_result, direct_result, f64);

  LOG_I64(infinity_bits, INT64_C(0x7ff0000000000000));
  LOG_I64(zero64, 0);
  LOG_OP(is_zero,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_EQ,
                                magnitude, zero64, location, &is_zero_op));
  LOG_OP(is_infinite, loom_scalar_cmpi_build(
                          builder, LOOM_SCALAR_CMPI_PREDICATE_EQ, magnitude,
                          infinity_bits, location, &is_infinite_op));
  LOG_OP(is_nan, loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_UGT,
                                        magnitude, infinity_bits, location,
                                        &is_nan_op));
  LOG_I64(sign_mask, INT64_MIN);
  LOG_OP(sign_bits, loom_scalar_andi_build(builder, bits, sign_mask, i64,
                                           location, &sign_bits_op));
  LOG_OP(negative,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_NE,
                                sign_bits, zero64, location, &negative_op));
  LOG_I64(negative_infinity_bits, INT64_MIN | INT64_C(0x7ff0000000000000));
  LOG_OP(negative_infinity,
         loom_scalar_bitcast_build(builder, negative_infinity_bits, i64, f64,
                                   location, &negative_infinity_op));
  LOG_I64(canonical_nan_bits, INT64_C(0x7ff8000000000000));
  LOG_OP(canonical_nan,
         loom_scalar_bitcast_build(builder, canonical_nan_bits, i64, f64,
                                   location, &canonical_nan_op));
  LOG_I64(quiet_nan_bit, INT64_C(0x0008000000000000));
  LOG_OP(quiet_bits, loom_scalar_ori_build(builder, bits, quiet_nan_bit, i64,
                                           location, &quiet_bits_op));
  LOG_OP(quiet_nan, loom_scalar_bitcast_build(builder, quiet_bits, i64, f64,
                                              location, &quiet_nan_op));
  LOG_SELECT(with_infinity, is_infinite, input, finite_result, f64);
  LOG_SELECT(with_negative, negative, canonical_nan, with_infinity, f64);
  LOG_SELECT(with_zero, is_zero, negative_infinity, with_negative, f64);
  LOG_SELECT(result, is_nan, quiet_nan, with_zero, f64);
  *out_value = result;

#undef LOG_SELECT
#undef LOG_MUL
#undef LOG_SUB
#undef LOG_ADD
#undef LOG_F64
#undef LOG_I64
#undef LOG_I32
#undef LOG_OP
  return iree_ok_status();
}

iree_status_t loom_math_legalize_rewrite_log_recipe(
    const loom_math_legalize_recipe_context_t* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_rewritten) {
  *out_rewritten = false;
  if (context->decision.recipe != LOOM_TARGET_MATH_RECIPE_LOG_RATIONAL_F64) {
    return iree_ok_status();
  }
  IREE_ASSERT(loom_scalar_logf_isa(op));
  IREE_ASSERT(context->query.element_type == LOOM_SCALAR_TYPE_F64);
  IREE_ASSERT(context->query.lane_domain ==
              LOOM_TARGET_MATH_LANE_DOMAIN_SCALAR);
  loom_builder_set_before(&rewriter->builder, op);
  const loom_value_id_t value_checkpoint =
      loom_rewriter_value_checkpoint(rewriter);
  loom_value_id_t replacement = LOOM_VALUE_ID_INVALID;
  IREE_RETURN_IF_ERROR(loom_math_legalize_build_log_f64(
      &rewriter->builder, loom_scalar_logf_input(op), op->instance_flags,
      op->location, &replacement));
  IREE_RETURN_IF_ERROR(loom_rewriter_preserve_result_names_on_new_values(
      rewriter, op, &replacement, 1, value_checkpoint));
  IREE_RETURN_IF_ERROR(
      loom_rewriter_replace_all_uses_and_erase(rewriter, op, &replacement, 1));
  *out_rewritten = true;
  return iree_ok_status();
}
