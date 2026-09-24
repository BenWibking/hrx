// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The argument reduction, polynomial coefficients, and rational approximation
// below are adapted from OpenLibm src/e_exp.c (derived from fdlibm).
// Copyright (C) 2004 by Sun Microsystems, Inc. All rights reserved.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.

#include <stdint.h>

#include "loom/ir/attribute.h"
#include "loom/ops/scalar/ops.h"
#include "loom/ops/scf/ops.h"
#include "loom/transforms/math/patterns.h"

static iree_status_t loom_math_legalize_build_exp_f64(
    loom_builder_t* builder, loom_value_id_t input, uint8_t fastmath_flags,
    loom_location_id_t location, loom_value_id_t* out_value) {
  const loom_type_t i32 = loom_type_scalar(LOOM_SCALAR_TYPE_I32);
  const loom_type_t i64 = loom_type_scalar(LOOM_SCALAR_TYPE_I64);
  const loom_type_t f64 = loom_type_scalar(LOOM_SCALAR_TYPE_F64);

#define EXP_OP(name, build_call)    \
  loom_op_t* name##_op = NULL;      \
  IREE_RETURN_IF_ERROR(build_call); \
  const loom_value_id_t name = loom_op_results(name##_op)[0]
#define EXP_I32(name, value)                                                  \
  EXP_OP(name, loom_scalar_constant_build(builder, loom_attr_i64(value), i32, \
                                          location, &name##_op))
#define EXP_I64(name, value)                                                  \
  EXP_OP(name, loom_scalar_constant_build(builder, loom_attr_i64(value), i64, \
                                          location, &name##_op))
#define EXP_F64(name, value)                                                  \
  EXP_OP(name, loom_scalar_constant_build(builder, loom_attr_f64(value), f64, \
                                          location, &name##_op))
#define EXP_ADD(name, lhs, rhs)                                               \
  EXP_OP(name, loom_scalar_addf_build(builder, fastmath_flags, lhs, rhs, f64, \
                                      location, &name##_op))
#define EXP_SUB(name, lhs, rhs)                                               \
  EXP_OP(name, loom_scalar_subf_build(builder, fastmath_flags, lhs, rhs, f64, \
                                      location, &name##_op))
#define EXP_MUL(name, lhs, rhs)                                               \
  EXP_OP(name, loom_scalar_mulf_build(builder, fastmath_flags, lhs, rhs, f64, \
                                      location, &name##_op))
#define EXP_SELECT(name, predicate, on_true, on_false, type)                \
  EXP_OP(name, loom_scf_select_build(builder, predicate, on_true, on_false, \
                                     type, location, &name##_op))

  EXP_OP(bits, loom_scalar_bitcast_build(builder, input, f64, i64, location,
                                         &bits_op));
  EXP_I64(magnitude_mask, INT64_MAX);
  EXP_OP(magnitude, loom_scalar_andi_build(builder, bits, magnitude_mask, i64,
                                           location, &magnitude_op));
  EXP_I64(half_ln2_bits, INT64_C(0x3fd62e4200000000));
  EXP_I64(one_half_ln2_bits, INT64_C(0x3ff0a2b200000000));
  EXP_OP(
      needs_reduction,
      loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_UGT, magnitude,
                             half_ln2_bits, location, &needs_reduction_op));
  EXP_OP(
      large_reduction,
      loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_UGE, magnitude,
                             one_half_ln2_bits, location, &large_reduction_op));
  EXP_I64(sign_mask, INT64_MIN);
  EXP_OP(sign_bits, loom_scalar_andi_build(builder, bits, sign_mask, i64,
                                           location, &sign_bits_op));
  EXP_I64(zero64, 0);
  EXP_OP(negative,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_NE,
                                sign_bits, zero64, location, &negative_op));

  EXP_F64(ln2_hi, 0.693147180369123816490);
  EXP_F64(negative_ln2_hi, -0.693147180369123816490);
  EXP_F64(ln2_lo, 1.90821492927058770002e-10);
  EXP_F64(negative_ln2_lo, -1.90821492927058770002e-10);
  EXP_SELECT(medium_ln2_hi, negative, negative_ln2_hi, ln2_hi, f64);
  EXP_SELECT(medium_ln2_lo, negative, negative_ln2_lo, ln2_lo, f64);
  EXP_SUB(medium_hi, input, medium_ln2_hi);
  EXP_I32(negative_one, -1);
  EXP_I32(one32, 1);
  EXP_I32(zero32, 0);
  EXP_SELECT(medium_k, negative, negative_one, one32, i32);

  // The low 32 bits of the integer-valued shifted double encode the signed
  // reduction count. This avoids a target-unsupported f64-to-i32 conversion.
  EXP_F64(inv_ln2, 1.44269504088896338700);
  EXP_F64(shift, 0x1.8p52);
  EXP_MUL(scaled_input, inv_ln2, input);
  EXP_ADD(shifted, scaled_input, shift);
  EXP_OP(shifted_bits, loom_scalar_bitcast_build(builder, shifted, f64, i64,
                                                 location, &shifted_bits_op));
  EXP_OP(large_k, loom_scalar_trunci_build(builder, shifted_bits, i64, i32,
                                           location, &large_k_op));
  EXP_SUB(dk, shifted, shift);
  EXP_MUL(large_hi_offset, dk, ln2_hi);
  EXP_SUB(large_hi, input, large_hi_offset);
  EXP_MUL(large_lo, dk, ln2_lo);
  EXP_SELECT(reduced_k, large_reduction, large_k, medium_k, i32);
  EXP_SELECT(k, needs_reduction, reduced_k, zero32, i32);
  EXP_SELECT(reduced_hi, large_reduction, large_hi, medium_hi, f64);
  EXP_SELECT(hi, needs_reduction, reduced_hi, input, f64);
  EXP_SELECT(reduced_lo, large_reduction, large_lo, medium_ln2_lo, f64);
  EXP_F64(zero, 0.0);
  EXP_SELECT(lo, needs_reduction, reduced_lo, zero, f64);

  EXP_SUB(r, hi, lo);
  EXP_MUL(t, r, r);
  EXP_F64(p1, 1.66666666666666019037e-01);
  EXP_F64(p2, -2.77777777770155933842e-03);
  EXP_F64(p3, 6.61375632143793436117e-05);
  EXP_F64(p4, -1.65339022054652515390e-06);
  EXP_F64(p5, 4.13813679705723846039e-08);
  EXP_MUL(poly5, t, p5);
  EXP_ADD(poly4, p4, poly5);
  EXP_MUL(poly4_t, t, poly4);
  EXP_ADD(poly3, p3, poly4_t);
  EXP_MUL(poly3_t, t, poly3);
  EXP_ADD(poly2, p2, poly3_t);
  EXP_MUL(poly2_t, t, poly2);
  EXP_ADD(poly1, p1, poly2_t);
  EXP_MUL(poly, t, poly1);
  EXP_SUB(c, r, poly);
  EXP_MUL(rc, r, c);
  EXP_F64(one, 1.0);
  EXP_F64(two, 2.0);
  EXP_SUB(c_minus_two, c, two);
  EXP_OP(ratio_zero,
         loom_scalar_divf_build(builder, fastmath_flags, rc, c_minus_two, f64,
                                location, &ratio_zero_op));
  EXP_SUB(corrected_zero, ratio_zero, r);
  EXP_SUB(y_zero, one, corrected_zero);
  EXP_SUB(two_minus_c, two, c);
  EXP_OP(ratio_nonzero,
         loom_scalar_divf_build(builder, fastmath_flags, rc, two_minus_c, f64,
                                location, &ratio_nonzero_op));
  EXP_SUB(corrected_lo, lo, ratio_nonzero);
  EXP_SUB(corrected_hi, corrected_lo, hi);
  EXP_SUB(y_nonzero, one, corrected_hi);
  EXP_OP(k_is_zero,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_EQ, k,
                                zero32, location, &k_is_zero_op));
  EXP_SELECT(y, k_is_zero, y_zero, y_nonzero, f64);

  EXP_I32(bias, 1023);
  EXP_OP(normal_exponent,
         loom_scalar_addi_build(builder, 0, k, bias, i32, location,
                                &normal_exponent_op));
  EXP_OP(normal_exponent64,
         loom_scalar_extsi_build(builder, normal_exponent, i32, i64, location,
                                 &normal_exponent64_op));
  EXP_I64(shift52, 52);
  EXP_OP(normal_bits,
         loom_scalar_shli_build(builder, 0, normal_exponent64, shift52, i64,
                                location, &normal_bits_op));
  EXP_OP(normal_scale, loom_scalar_bitcast_build(builder, normal_bits, i64, f64,
                                                 location, &normal_scale_op));
  EXP_MUL(normal_result, y, normal_scale);

  EXP_I32(subnormal_bias, 2023);
  EXP_OP(subnormal_exponent,
         loom_scalar_addi_build(builder, 0, k, subnormal_bias, i32, location,
                                &subnormal_exponent_op));
  EXP_OP(subnormal_exponent64,
         loom_scalar_extsi_build(builder, subnormal_exponent, i32, i64,
                                 location, &subnormal_exponent64_op));
  EXP_OP(subnormal_bits,
         loom_scalar_shli_build(builder, 0, subnormal_exponent64, shift52, i64,
                                location, &subnormal_bits_op));
  EXP_OP(subnormal_scale,
         loom_scalar_bitcast_build(builder, subnormal_bits, i64, f64, location,
                                   &subnormal_scale_op));
  EXP_MUL(subnormal_scaled, y, subnormal_scale);
  EXP_F64(tiny_scale, 0x1p-1000);
  EXP_MUL(subnormal_result, subnormal_scaled, tiny_scale);
  EXP_I32(normal_minimum, -1021);
  EXP_OP(is_normal,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_SGE, k,
                                normal_minimum, location, &is_normal_op));
  EXP_SELECT(scaled, is_normal, normal_result, subnormal_result, f64);
  EXP_I32(highest_k, 1024);
  EXP_OP(is_highest_k,
         loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_EQ, k,
                                highest_k, location, &is_highest_k_op));
  EXP_MUL(doubled_y, y, two);
  EXP_F64(max_power_of_two, 0x1p1023);
  EXP_MUL(highest_result, doubled_y, max_power_of_two);
  EXP_SELECT(finite_result, is_highest_k, highest_result, scaled, f64);
  EXP_SELECT(unscaled_result, k_is_zero, y_zero, finite_result, f64);

  EXP_F64(overflow_limit, 709.782712893383973096);
  EXP_F64(underflow_limit, -745.133219101941108420);
  EXP_OP(overflows, loom_scalar_cmpf_build(
                        builder, fastmath_flags, LOOM_SCALAR_CMPF_PREDICATE_OGT,
                        input, overflow_limit, location, &overflows_op));
  EXP_OP(underflows,
         loom_scalar_cmpf_build(builder, fastmath_flags,
                                LOOM_SCALAR_CMPF_PREDICATE_OLT, input,
                                underflow_limit, location, &underflows_op));
  EXP_OP(is_one, loom_scalar_cmpf_build(builder, fastmath_flags,
                                        LOOM_SCALAR_CMPF_PREDICATE_OEQ, input,
                                        one, location, &is_one_op));
  EXP_F64(exp_one, 2.718281828459045235360);
  EXP_I64(infinity_bits, INT64_C(0x7ff0000000000000));
  EXP_OP(infinity, loom_scalar_bitcast_build(builder, infinity_bits, i64, f64,
                                             location, &infinity_op));
  EXP_SELECT(with_one, is_one, exp_one, unscaled_result, f64);
  EXP_SELECT(with_underflow, underflows, zero, with_one, f64);
  EXP_SELECT(with_overflow, overflows, infinity, with_underflow, f64);

  EXP_OP(is_nan, loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_UGT,
                                        magnitude, infinity_bits, location,
                                        &is_nan_op));
  EXP_I64(quiet_nan_bit, INT64_C(0x0008000000000000));
  EXP_OP(quiet_bits, loom_scalar_ori_build(builder, bits, quiet_nan_bit, i64,
                                           location, &quiet_bits_op));
  EXP_OP(quiet_nan, loom_scalar_bitcast_build(builder, quiet_bits, i64, f64,
                                              location, &quiet_nan_op));
  EXP_SELECT(result, is_nan, quiet_nan, with_overflow, f64);
  *out_value = result;

#undef EXP_SELECT
#undef EXP_MUL
#undef EXP_SUB
#undef EXP_ADD
#undef EXP_F64
#undef EXP_I64
#undef EXP_I32
#undef EXP_OP
  return iree_ok_status();
}

iree_status_t loom_math_legalize_rewrite_exp_recipe(
    const loom_math_legalize_recipe_context_t* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_rewritten) {
  *out_rewritten = false;
  if (context->decision.recipe != LOOM_TARGET_MATH_RECIPE_EXP_RATIONAL_F64) {
    return iree_ok_status();
  }
  IREE_ASSERT(loom_scalar_expf_isa(op));
  IREE_ASSERT(context->query.element_type == LOOM_SCALAR_TYPE_F64);
  IREE_ASSERT(context->query.lane_domain ==
              LOOM_TARGET_MATH_LANE_DOMAIN_SCALAR);
  loom_builder_set_before(&rewriter->builder, op);
  const loom_value_id_t value_checkpoint =
      loom_rewriter_value_checkpoint(rewriter);
  loom_value_id_t replacement = LOOM_VALUE_ID_INVALID;
  IREE_RETURN_IF_ERROR(loom_math_legalize_build_exp_f64(
      &rewriter->builder, loom_scalar_expf_input(op), op->instance_flags,
      op->location, &replacement));
  IREE_RETURN_IF_ERROR(loom_rewriter_preserve_result_names_on_new_values(
      rewriter, op, &replacement, 1, value_checkpoint));
  IREE_RETURN_IF_ERROR(
      loom_rewriter_replace_all_uses_and_erase(rewriter, op, &replacement, 1));
  *out_rewritten = true;
  return iree_ok_status();
}
