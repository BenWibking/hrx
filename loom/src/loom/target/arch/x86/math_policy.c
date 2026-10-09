// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/x86/math_policy.h"

#include "loom/target/arch/x86/feature_bits.h"

static loom_target_math_policy_decision_t loom_x86_math_keep(
    iree_string_view_t constraint_key) {
  return (loom_target_math_policy_decision_t){
      .action = LOOM_TARGET_MATH_POLICY_ACTION_KEEP,
      .constraint_key = constraint_key,
  };
}

static loom_target_math_policy_decision_t loom_x86_math_reject(
    iree_string_view_t constraint_key) {
  return (loom_target_math_policy_decision_t){
      .action = LOOM_TARGET_MATH_POLICY_ACTION_REJECT,
      .constraint_key = constraint_key,
  };
}

static bool loom_x86_math_op_is_native_arithmetic(
    loom_target_math_op_t math_op) {
  return math_op == LOOM_TARGET_MATH_OP_ADDF ||
         math_op == LOOM_TARGET_MATH_OP_SUBF ||
         math_op == LOOM_TARGET_MATH_OP_MULF;
}

static void loom_x86_math_policy_query(
    const loom_target_math_policy_t* policy,
    const loom_target_math_query_t* query,
    loom_target_math_policy_decision_t* out_decision) {
  if (!loom_x86_math_op_is_native_arithmetic(query->math_op)) {
    *out_decision = loom_x86_math_reject(IREE_SV("math.op.supported"));
    return;
  }
  if (query->element_type != LOOM_SCALAR_TYPE_F32 &&
      query->element_type != LOOM_SCALAR_TYPE_F64) {
    *out_decision = loom_x86_math_reject(IREE_SV("math.element.f32_f64"));
    return;
  }

  *out_decision = loom_x86_math_keep(IREE_SV("math.op.native_f32_f64"));
}

static bool loom_x86_math_target_has_fp16(
    const loom_target_bundle_t* target_bundle) {
  return target_bundle != NULL && target_bundle->config != NULL &&
         iree_any_bit_set(target_bundle->config->contract_feature_bits,
                          LOOM_X86_FEATURE_AVX512_FP16);
}

static void loom_x86_avx512_features_math_policy_query(
    const loom_target_math_policy_t* policy,
    const loom_target_math_query_t* query,
    loom_target_math_policy_decision_t* out_decision) {
  if (query->element_type != LOOM_SCALAR_TYPE_F16) {
    loom_x86_math_policy_query(policy, query, out_decision);
    return;
  }
  if (!loom_x86_math_op_is_native_arithmetic(query->math_op)) {
    *out_decision = loom_x86_math_reject(IREE_SV("math.op.supported"));
    return;
  }
  if (!loom_x86_math_target_has_fp16(query->target_bundle)) {
    *out_decision = loom_x86_math_reject(IREE_SV("math.feature.avx512_fp16"));
    return;
  }
  *out_decision = loom_x86_math_keep(IREE_SV("math.op.native_f16"));
}

static bool loom_x86_math_prefer_scalar_fma(
    const loom_target_math_policy_t* policy,
    const loom_target_bundle_t* target_bundle, loom_type_t value_type,
    loom_target_math_fastmath_flags_t fastmath_flags) {
  (void)policy;
  (void)target_bundle;
  (void)fastmath_flags;
  if (!loom_type_is_scalar(value_type)) {
    return false;
  }
  const loom_scalar_type_t element_type = loom_type_element_type(value_type);
  return element_type == LOOM_SCALAR_TYPE_F32 ||
         element_type == LOOM_SCALAR_TYPE_F64;
}

static bool loom_x86_math_prefer_avx512_fma(
    const loom_target_math_policy_t* policy,
    const loom_target_bundle_t* target_bundle, loom_type_t value_type,
    loom_target_math_fastmath_flags_t fastmath_flags) {
  if (loom_x86_math_prefer_scalar_fma(policy, target_bundle, value_type,
                                      fastmath_flags)) {
    return true;
  }
  const loom_scalar_type_t element_type = loom_type_element_type(value_type);
  if (element_type == LOOM_SCALAR_TYPE_F16 &&
      loom_x86_math_target_has_fp16(target_bundle)) {
    if (loom_type_is_scalar(value_type)) {
      return true;
    }
    if (!loom_type_is_vector(value_type) || loom_type_rank(value_type) != 1 ||
        !loom_type_is_all_static(value_type)) {
      return false;
    }
    const int64_t bit_width = loom_type_dim_static_size_at(value_type, 0) * 16;
    return bit_width == 64 || bit_width == 128 || bit_width == 256 ||
           bit_width == 512;
  }
  return loom_type_is_vector(value_type) && loom_type_rank(value_type) == 1 &&
         loom_type_is_all_static(value_type) &&
         loom_type_dim_static_size_at(value_type, 0) == 16 &&
         element_type == LOOM_SCALAR_TYPE_F32;
}

static bool loom_x86_math_prefer_avx2_fma(
    const loom_target_math_policy_t* policy,
    const loom_target_bundle_t* target_bundle, loom_type_t value_type,
    loom_target_math_fastmath_flags_t fastmath_flags) {
  if (loom_x86_math_prefer_scalar_fma(policy, target_bundle, value_type,
                                      fastmath_flags)) {
    return true;
  }
  if (!loom_type_is_vector(value_type) || loom_type_rank(value_type) != 1 ||
      !loom_type_is_all_static(value_type)) {
    return false;
  }
  const loom_scalar_type_t element_type = loom_type_element_type(value_type);
  if (element_type != LOOM_SCALAR_TYPE_F32 &&
      element_type != LOOM_SCALAR_TYPE_F64) {
    return false;
  }
  const int64_t bit_width = loom_type_dim_static_size_at(value_type, 0) *
                            loom_scalar_type_bitwidth(element_type);
  return bit_width == 128 || bit_width == 256;
}

static const loom_target_math_policy_t kX86MathPolicy = {
    .name = IREE_SVL("x86-math"),
    .query = loom_x86_math_policy_query,
    .prefer_fma = loom_x86_math_prefer_avx2_fma,
};

static const loom_target_math_policy_t kX86Avx512MathPolicy = {
    .name = IREE_SVL("x86-math"),
    .query = loom_x86_math_policy_query,
    .prefer_fma = loom_x86_math_prefer_avx512_fma,
};

static const loom_target_math_policy_t kX86Avx512FeaturesMathPolicy = {
    .name = IREE_SVL("x86-math"),
    .query = loom_x86_avx512_features_math_policy_query,
    .prefer_fma = loom_x86_math_prefer_avx512_fma,
};

static const loom_target_math_policy_registry_entry_t kX86MathPolicyEntries[] =
    {
        {/*.contract_set_key=*/IREE_SVL("x86.avx512.core"),
         /*.policy=*/&kX86Avx512MathPolicy},
        {/*.contract_set_key=*/IREE_SVL("x86.avx2.core"),
         /*.policy=*/&kX86MathPolicy},
        {/*.contract_set_key=*/IREE_SVL("x86.avx2_features.core"),
         /*.policy=*/&kX86MathPolicy},
        {/*.contract_set_key=*/IREE_SVL("x86.packed_dot.core"),
         /*.policy=*/&kX86MathPolicy},
        {/*.contract_set_key=*/IREE_SVL("x86.avx512_features.core"),
         /*.policy=*/&kX86Avx512FeaturesMathPolicy},
};

void loom_x86_math_policy_registry_initialize(
    loom_target_math_policy_registry_t* out_registry) {
  loom_target_math_policy_registry_initialize_from_entries(
      out_registry, kX86MathPolicyEntries,
      IREE_ARRAYSIZE(kX86MathPolicyEntries));
}
