// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <stdint.h>

#include "loom/ir/attribute.h"
#include "loom/ops/scalar/ops.h"
#include "loom/ops/scf/ops.h"
#include "loom/transforms/math/patterns.h"

// Normalize |x| to a mantissa in [1, 8), estimate its cube root by a line,
// and refine six times in f64. The exponent split uses an exact reciprocal
// multiply for n in [0, 2097], avoiding a target integer division. Keeping
// the exponent scale separate makes the Newton divisions safe for subnormals
// and values near DBL_MAX. Zeros and infinities retain their bit patterns;
// NaNs retain their payloads and have their quiet bit set. No approximate-math
// flags are introduced.
static iree_status_t loom_math_legalize_build_cbrt_f64(
    loom_builder_t* builder, loom_value_id_t input, uint8_t fastmath_flags,
    loom_location_id_t location, loom_value_id_t* out_value) {
  const loom_type_t i1 = loom_type_scalar(LOOM_SCALAR_TYPE_I1);
  const loom_type_t i32 = loom_type_scalar(LOOM_SCALAR_TYPE_I32);
  const loom_type_t i64 = loom_type_scalar(LOOM_SCALAR_TYPE_I64);
  const loom_type_t f64 = loom_type_scalar(LOOM_SCALAR_TYPE_F64);

#define CBRT_OP(name, build_call)   \
  loom_op_t* name##_op = NULL;      \
  IREE_RETURN_IF_ERROR(build_call); \
  const loom_value_id_t name = loom_op_results(name##_op)[0]
#define CBRT_I32(name, value)                                                  \
  CBRT_OP(name, loom_scalar_constant_build(builder, loom_attr_i64(value), i32, \
                                           location, &name##_op))
#define CBRT_I64(name, value)                                                  \
  CBRT_OP(name, loom_scalar_constant_build(builder, loom_attr_i64(value), i64, \
                                           location, &name##_op))
#define CBRT_F64(name, value)                                                  \
  CBRT_OP(name, loom_scalar_constant_build(builder, loom_attr_f64(value), f64, \
                                           location, &name##_op))

  CBRT_OP(bits, loom_scalar_bitcast_build(builder, input, f64, i64, location,
                                          &bits_op));
  CBRT_I64(magnitude_mask, INT64_MAX);
  CBRT_OP(magnitude, loom_scalar_andi_build(builder, bits, magnitude_mask, i64,
                                            location, &magnitude_op));
  CBRT_OP(positive, loom_scalar_bitcast_build(builder, magnitude, i64, f64,
                                              location, &positive_op));
  CBRT_I64(min_normal_bits, INT64_C(0x0010000000000000));
  CBRT_OP(subnormal, loom_scalar_cmpi_build(
                         builder, LOOM_SCALAR_CMPI_PREDICATE_ULT, magnitude,
                         min_normal_bits, location, &subnormal_op));
  CBRT_F64(subnormal_scale, 0x1p54);
  CBRT_OP(
      scaled_subnormal,
      loom_scalar_mulf_build(builder, fastmath_flags, positive, subnormal_scale,
                             f64, location, &scaled_subnormal_op));
  CBRT_OP(normalized_value,
          loom_scf_select_build(builder, subnormal, scaled_subnormal, positive,
                                f64, location, &normalized_value_op));
  CBRT_OP(normalized, loom_scalar_bitcast_build(builder, normalized_value, f64,
                                                i64, location, &normalized_op));
  CBRT_I64(shift52, 52);
  CBRT_OP(shifted_exponent,
          loom_scalar_shrui_build(builder, normalized, shift52, i64, location,
                                  &shifted_exponent_op));
  CBRT_I64(exponent_mask, 2047);
  CBRT_OP(exponent_bits,
          loom_scalar_andi_build(builder, shifted_exponent, exponent_mask, i64,
                                 location, &exponent_bits_op));
  CBRT_OP(biased_exponent,
          loom_scalar_trunci_build(builder, exponent_bits, i64, i32, location,
                                   &biased_exponent_op));
  CBRT_I32(exponent_bias, 1023);
  CBRT_OP(unbiased_exponent,
          loom_scalar_subi_build(builder, 0, biased_exponent, exponent_bias,
                                 i32, location, &unbiased_exponent_op));
  CBRT_I32(subnormal_exponent_shift, 54);
  CBRT_I32(zero32, 0);
  CBRT_OP(exponent_shift,
          loom_scf_select_build(builder, subnormal, subnormal_exponent_shift,
                                zero32, i32, location, &exponent_shift_op));
  CBRT_OP(exponent,
          loom_scalar_subi_build(builder, 0, unbiased_exponent, exponent_shift,
                                 i32, location, &exponent_op));
  CBRT_I32(min_exponent_offset, 1074);
  CBRT_OP(n, loom_scalar_addi_build(builder, 0, exponent, min_exponent_offset,
                                    i32, location, &n_op));
  CBRT_I32(reciprocal_three, 43691);
  CBRT_OP(reciprocal_product,
          loom_scalar_muli_build(builder, 0, n, reciprocal_three, i32, location,
                                 &reciprocal_product_op));
  CBRT_I32(shift17, 17);
  CBRT_OP(q_positive,
          loom_scalar_shrsi_build(builder, reciprocal_product, shift17, i32,
                                  location, &q_positive_op));
  CBRT_I32(three32, 3);
  CBRT_OP(three_q, loom_scalar_muli_build(builder, 0, three32, q_positive, i32,
                                          location, &three_q_op));
  CBRT_OP(remainder, loom_scalar_subi_build(builder, 0, n, three_q, i32,
                                            location, &remainder_op));

  CBRT_I64(mantissa_mask, INT64_C(0x000fffffffffffff));
  CBRT_OP(mantissa_bits,
          loom_scalar_andi_build(builder, normalized, mantissa_mask, i64,
                                 location, &mantissa_bits_op));
  CBRT_I64(one_bits, INT64_C(0x3ff0000000000000));
  CBRT_OP(unit_mantissa_bits,
          loom_scalar_ori_build(builder, mantissa_bits, one_bits, i64, location,
                                &unit_mantissa_bits_op));
  CBRT_OP(mantissa, loom_scalar_bitcast_build(builder, unit_mantissa_bits, i64,
                                              f64, location, &mantissa_op));
  CBRT_I32(one32, 1);
  CBRT_OP(remainder_scale,
          loom_scalar_shli_build(builder, 0, one32, remainder, i32, location,
                                 &remainder_scale_op));
  CBRT_OP(remainder_scale_f64,
          loom_scalar_sitofp_build(builder, remainder_scale, i32, f64, location,
                                   &remainder_scale_f64_op));
  CBRT_OP(t, loom_scalar_mulf_build(builder, fastmath_flags, mantissa,
                                    remainder_scale_f64, f64, location, &t_op));
  CBRT_F64(seed_offset, 0.75);
  CBRT_F64(seed_slope, 0.25);
  CBRT_OP(seed_scaled,
          loom_scalar_mulf_build(builder, fastmath_flags, seed_slope, t, f64,
                                 location, &seed_scaled_op));
  CBRT_OP(seed, loom_scalar_addf_build(builder, fastmath_flags, seed_offset,
                                       seed_scaled, f64, location, &seed_op));

  CBRT_F64(two, 2.0);
  CBRT_F64(three, 3.0);
  loom_value_id_t y = seed;
  for (int iteration = 0; iteration < 6; ++iteration) {
    CBRT_OP(twice_y, loom_scalar_mulf_build(builder, fastmath_flags, two, y,
                                            f64, location, &twice_y_op));
    CBRT_OP(y_squared, loom_scalar_mulf_build(builder, fastmath_flags, y, y,
                                              f64, location, &y_squared_op));
    CBRT_OP(quotient,
            loom_scalar_divf_build(builder, fastmath_flags, t, y_squared, f64,
                                   location, &quotient_op));
    CBRT_OP(numerator,
            loom_scalar_addf_build(builder, fastmath_flags, twice_y, quotient,
                                   f64, location, &numerator_op));
    CBRT_OP(next_y, loom_scalar_divf_build(builder, fastmath_flags, numerator,
                                           three, f64, location, &next_y_op));
    y = next_y;
  }

  CBRT_I32(scale_exponent_offset, 665);
  CBRT_OP(scale_exponent,
          loom_scalar_addi_build(builder, 0, q_positive, scale_exponent_offset,
                                 i32, location, &scale_exponent_op));
  CBRT_OP(scale_exponent_i64,
          loom_scalar_extsi_build(builder, scale_exponent, i32, i64, location,
                                  &scale_exponent_i64_op));
  CBRT_OP(scale_bits,
          loom_scalar_shli_build(builder, 0, scale_exponent_i64, shift52, i64,
                                 location, &scale_bits_op));
  CBRT_OP(scale, loom_scalar_bitcast_build(builder, scale_bits, i64, f64,
                                           location, &scale_op));
  CBRT_OP(scaled_result,
          loom_scalar_mulf_build(builder, fastmath_flags, y, scale, f64,
                                 location, &scaled_result_op));
  CBRT_OP(unsigned_result_bits,
          loom_scalar_bitcast_build(builder, scaled_result, f64, i64, location,
                                    &unsigned_result_bits_op));
  CBRT_I64(sign_mask, INT64_MIN);
  CBRT_OP(sign_bits, loom_scalar_andi_build(builder, bits, sign_mask, i64,
                                            location, &sign_bits_op));
  CBRT_OP(result_bits,
          loom_scalar_ori_build(builder, unsigned_result_bits, sign_bits, i64,
                                location, &result_bits_op));
  CBRT_OP(signed_result,
          loom_scalar_bitcast_build(builder, result_bits, i64, f64, location,
                                    &signed_result_op));

  CBRT_I64(zero64, 0);
  CBRT_OP(is_zero,
          loom_scalar_cmpi_build(builder, LOOM_SCALAR_CMPI_PREDICATE_EQ,
                                 magnitude, zero64, location, &is_zero_op));
  CBRT_I64(infinity_bits, INT64_C(0x7ff0000000000000));
  CBRT_OP(is_nonfinite, loom_scalar_cmpi_build(
                            builder, LOOM_SCALAR_CMPI_PREDICATE_UGE, magnitude,
                            infinity_bits, location, &is_nonfinite_op));
  CBRT_OP(is_special,
          loom_scf_select_build(builder, is_zero, is_zero, is_nonfinite, i1,
                                location, &is_special_op));
  CBRT_OP(is_nan, loom_scalar_cmpi_build(
                      builder, LOOM_SCALAR_CMPI_PREDICATE_UGT, magnitude,
                      infinity_bits, location, &is_nan_op));
  CBRT_I64(quiet_nan_bit, INT64_C(0x0008000000000000));
  CBRT_OP(nan_quiet_mask,
          loom_scf_select_build(builder, is_nan, quiet_nan_bit, zero64, i64,
                                location, &nan_quiet_mask_op));
  CBRT_OP(special_bits, loom_scalar_ori_build(builder, bits, nan_quiet_mask,
                                              i64, location, &special_bits_op));
  CBRT_OP(special_result,
          loom_scalar_bitcast_build(builder, special_bits, i64, f64, location,
                                    &special_result_op));
  CBRT_OP(result,
          loom_scf_select_build(builder, is_special, special_result,
                                signed_result, f64, location, &result_op));
  *out_value = result;

#undef CBRT_F64
#undef CBRT_I64
#undef CBRT_I32
#undef CBRT_OP
  return iree_ok_status();
}

iree_status_t loom_math_legalize_rewrite_cbrt_recipe(
    const loom_math_legalize_recipe_context_t* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_rewritten) {
  *out_rewritten = false;
  if (context->decision.recipe != LOOM_TARGET_MATH_RECIPE_CBRT_NEWTON_F64) {
    return iree_ok_status();
  }
  IREE_ASSERT(loom_scalar_cbrtf_isa(op));
  IREE_ASSERT(context->query.element_type == LOOM_SCALAR_TYPE_F64);
  IREE_ASSERT(context->query.lane_domain ==
              LOOM_TARGET_MATH_LANE_DOMAIN_SCALAR);
  loom_builder_set_before(&rewriter->builder, op);
  const loom_value_id_t value_checkpoint =
      loom_rewriter_value_checkpoint(rewriter);
  loom_value_id_t replacement = LOOM_VALUE_ID_INVALID;
  IREE_RETURN_IF_ERROR(loom_math_legalize_build_cbrt_f64(
      &rewriter->builder, loom_scalar_cbrtf_input(op), op->instance_flags,
      op->location, &replacement));
  IREE_RETURN_IF_ERROR(loom_rewriter_preserve_result_names_on_new_values(
      rewriter, op, &replacement, 1, value_checkpoint));
  IREE_RETURN_IF_ERROR(
      loom_rewriter_replace_all_uses_and_erase(rewriter, op, &replacement, 1));
  *out_rewritten = true;
  return iree_ok_status();
}
