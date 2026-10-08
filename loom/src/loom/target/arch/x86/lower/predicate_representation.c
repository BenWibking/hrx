// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/x86/lower/predicate_representation.h"

#include "loom/ops/vector/ops.h"

typedef enum loom_x86_predicate_representation_action_e {
  LOOM_X86_PREDICATE_REPRESENTATION_ACTION_FLEXIBLE_RESULT = 0,
  LOOM_X86_PREDICATE_REPRESENTATION_ACTION_EXTRACT = 1,
  LOOM_X86_PREDICATE_REPRESENTATION_ACTION_INSERT = 2,
  LOOM_X86_PREDICATE_REPRESENTATION_ACTION_SELECT = 3,
  LOOM_X86_PREDICATE_REPRESENTATION_ACTION_COMPARE = 4,
} loom_x86_predicate_representation_action_t;

typedef enum loom_x86_predicate_profile_e {
  LOOM_X86_PREDICATE_PROFILE_AVX2 = 0,
  LOOM_X86_PREDICATE_PROFILE_AVX512 = 1,
} loom_x86_predicate_profile_t;

typedef struct loom_x86_predicate_representation_policy_t {
  loom_x86_predicate_profile_t profile;
  uint32_t maximum_lane_count;
} loom_x86_predicate_representation_policy_t;

static bool loom_x86_predicate_type(loom_type_t source_type,
                                    uint32_t maximum_lane_count,
                                    uint32_t* out_lane_count) {
  if (out_lane_count != NULL) {
    *out_lane_count = 0;
  }
  if (!loom_type_is_vector(source_type) || loom_type_rank(source_type) != 1 ||
      !loom_type_is_all_static(source_type) ||
      loom_type_element_type(source_type) != LOOM_SCALAR_TYPE_I1) {
    return false;
  }
  const int64_t lane_count = loom_type_dim_static_size_at(source_type, 0);
  if ((lane_count != 2 && lane_count != 4 && lane_count != 8 &&
       lane_count != 16 && lane_count != 32 && lane_count != 64) ||
      lane_count > maximum_lane_count) {
    return false;
  }
  if (out_lane_count != NULL) {
    *out_lane_count = (uint32_t)lane_count;
  }
  return true;
}

bool loom_x86_avx2_predicate_type(loom_type_t source_type,
                                  uint32_t* out_lane_count) {
  return loom_x86_predicate_type(source_type, 32, out_lane_count);
}

bool loom_x86_avx512_predicate_type(loom_type_t source_type,
                                    uint32_t* out_lane_count) {
  return loom_x86_predicate_type(source_type, 64, out_lane_count);
}

static bool loom_x86_avx2_predicate_representation_available(
    uint32_t lane_count, loom_low_representation_id_t representation) {
  switch (representation) {
    case LOOM_X86_PREDICATE_REPRESENTATION_XMM:
      return lane_count <= 16;
    case LOOM_X86_PREDICATE_REPRESENTATION_YMM:
      return lane_count >= 4;
    default:
      return false;
  }
}

bool loom_x86_avx2_predicate_register_class(
    loom_type_t source_type, loom_low_representation_id_t representation,
    loom_x86_register_class_t* out_register_class) {
  uint32_t lane_count = 0;
  if (!loom_x86_avx2_predicate_type(source_type, &lane_count)) {
    return false;
  }
  if (representation == LOOM_LOW_REPRESENTATION_ID_NONE) {
    representation = lane_count == 32 ? LOOM_X86_PREDICATE_REPRESENTATION_YMM
                                      : LOOM_X86_PREDICATE_REPRESENTATION_XMM;
  }
  if (!loom_x86_avx2_predicate_representation_available(lane_count,
                                                        representation)) {
    return false;
  }
  *out_register_class = representation == LOOM_X86_PREDICATE_REPRESENTATION_XMM
                            ? LOOM_X86_REGISTER_CLASS_XMM
                            : LOOM_X86_REGISTER_CLASS_YMM;
  return true;
}

bool loom_x86_avx512_predicate_register_class(
    loom_type_t source_type, loom_low_representation_id_t representation,
    loom_x86_register_class_t* out_register_class) {
  uint32_t lane_count = 0;
  if (!loom_x86_avx512_predicate_type(source_type, &lane_count)) {
    return false;
  }
  if (representation == LOOM_LOW_REPRESENTATION_ID_NONE) {
    representation = LOOM_X86_AVX512_PREDICATE_REPRESENTATION_CARRIER;
  }
  if (representation == LOOM_X86_AVX512_PREDICATE_REPRESENTATION_MASK) {
    *out_register_class = LOOM_X86_REGISTER_CLASS_K;
    return true;
  }
  if (representation != LOOM_X86_AVX512_PREDICATE_REPRESENTATION_CARRIER) {
    return false;
  }
  *out_register_class = lane_count <= 16   ? LOOM_X86_REGISTER_CLASS_XMM
                        : lane_count == 32 ? LOOM_X86_REGISTER_CLASS_YMM
                                           : LOOM_X86_REGISTER_CLASS_ZMM;
  return true;
}

static loom_low_representation_cost_t loom_x86_avx2_predicate_conversion_cost(
    loom_low_representation_id_t source,
    loom_low_representation_id_t destination) {
  if (source == destination) {
    return (loom_low_representation_cost_t){0};
  }
  if (source == LOOM_X86_PREDICATE_REPRESENTATION_XMM) {
    return (loom_low_representation_cost_t){.runtime = 1, .code_size = 5};
  }
  return (loom_low_representation_cost_t){.runtime = 3, .code_size = 15};
}

static loom_low_representation_cost_t loom_x86_predicate_conversion_cost(
    const loom_x86_predicate_representation_policy_t* policy,
    loom_low_representation_id_t source,
    loom_low_representation_id_t destination) {
  if (policy->profile == LOOM_X86_PREDICATE_PROFILE_AVX2) {
    return loom_x86_avx2_predicate_conversion_cost(source, destination);
  }
  return source == destination
             ? (loom_low_representation_cost_t){0}
             : (loom_low_representation_cost_t){.runtime = 1, .code_size = 6};
}

static bool loom_x86_predicate_representation_available(
    const loom_x86_predicate_representation_policy_t* policy,
    uint32_t lane_count, loom_low_representation_id_t representation) {
  if (policy->profile == LOOM_X86_PREDICATE_PROFILE_AVX2) {
    return loom_x86_avx2_predicate_representation_available(lane_count,
                                                            representation);
  }
  return representation == LOOM_X86_AVX512_PREDICATE_REPRESENTATION_MASK ||
         representation == LOOM_X86_AVX512_PREDICATE_REPRESENTATION_CARRIER;
}

static loom_low_representation_id_t loom_x86_predicate_callable_representation(
    const loom_x86_predicate_representation_policy_t* policy,
    uint32_t lane_count) {
  if (policy->profile == LOOM_X86_PREDICATE_PROFILE_AVX512) {
    return LOOM_X86_AVX512_PREDICATE_REPRESENTATION_CARRIER;
  }
  return lane_count == 32 ? LOOM_X86_PREDICATE_REPRESENTATION_YMM
                          : LOOM_X86_PREDICATE_REPRESENTATION_XMM;
}

static loom_low_representation_id_t loom_x86_predicate_native_representation(
    const loom_x86_predicate_representation_policy_t* policy,
    uint32_t vector_bit_width) {
  if (policy->profile == LOOM_X86_PREDICATE_PROFILE_AVX512) {
    return LOOM_X86_AVX512_PREDICATE_REPRESENTATION_MASK;
  }
  if (vector_bit_width == 128) {
    return LOOM_X86_PREDICATE_REPRESENTATION_XMM;
  }
  if (vector_bit_width == 256) {
    return LOOM_X86_PREDICATE_REPRESENTATION_YMM;
  }
  return LOOM_LOW_REPRESENTATION_ID_NONE;
}

static iree_host_size_t loom_x86_predicate_candidates(
    const loom_x86_predicate_representation_policy_t* policy,
    uint32_t lane_count, loom_low_representation_id_t native_representation,
    loom_low_representation_candidate_t candidates[2]) {
  iree_host_size_t candidate_count = 0;
  for (loom_low_representation_id_t representation = 0; representation <= 1;
       ++representation) {
    if (!loom_x86_predicate_representation_available(policy, lane_count,
                                                     representation)) {
      continue;
    }
    candidates[candidate_count++] = (loom_low_representation_candidate_t){
        .representation = representation,
        .cost = native_representation == LOOM_LOW_REPRESENTATION_ID_NONE
                    ? (loom_low_representation_cost_t){0}
                    : loom_x86_predicate_conversion_cost(
                          policy, native_representation, representation),
    };
  }
  return candidate_count;
}

static void loom_x86_predicate_constrain_flexible(
    const loom_x86_predicate_representation_policy_t* policy,
    const loom_module_t* module, loom_value_id_t value_id,
    loom_low_lower_representation_recorder_t* recorder) {
  uint32_t lane_count = 0;
  if (!loom_x86_predicate_type(loom_module_value_type(module, value_id),
                               policy->maximum_lane_count, &lane_count)) {
    return;
  }
  loom_low_representation_candidate_t candidates[2];
  const loom_low_representation_id_t native_representation =
      policy->profile == LOOM_X86_PREDICATE_PROFILE_AVX512
          ? LOOM_X86_AVX512_PREDICATE_REPRESENTATION_MASK
          : LOOM_LOW_REPRESENTATION_ID_NONE;
  const iree_host_size_t candidate_count = loom_x86_predicate_candidates(
      policy, lane_count, native_representation, candidates);
  loom_low_lower_representation_record_candidates(recorder, value_id,
                                                  candidates, candidate_count);
}

static void loom_x86_predicate_constrain_callable_value(
    const loom_x86_predicate_representation_policy_t* policy,
    const loom_module_t* module, loom_value_id_t value_id,
    loom_low_lower_representation_recorder_t* recorder) {
  uint32_t lane_count = 0;
  if (!loom_x86_predicate_type(loom_module_value_type(module, value_id),
                               policy->maximum_lane_count, &lane_count)) {
    return;
  }
  const loom_low_representation_candidate_t candidate = {
      .representation =
          loom_x86_predicate_callable_representation(policy, lane_count),
  };
  loom_low_lower_representation_record_candidates(recorder, value_id,
                                                  &candidate, 1);
}

static void loom_x86_predicate_constrain_callable_values(
    const loom_x86_predicate_representation_policy_t* policy,
    const loom_module_t* module, const loom_value_id_t* value_ids,
    iree_host_size_t value_count,
    loom_low_lower_representation_recorder_t* recorder) {
  for (iree_host_size_t i = 0; i < value_count; ++i) {
    loom_x86_predicate_constrain_callable_value(policy, module, value_ids[i],
                                                recorder);
  }
}

static bool loom_x86_predicate_representation_relation(
    void* user_data, loom_low_lower_context_t* context,
    const loom_op_t* source_op, const loom_value_relation_t* relation,
    loom_low_lower_representation_recorder_t* recorder) {
  const loom_x86_predicate_representation_policy_t* policy = user_data;
  (void)source_op;
  (void)recorder;
  if (iree_any_bit_set(relation->flags, LOOM_VALUE_RELATION_FLAG_TYPE_CHANGE)) {
    return false;
  }
  const loom_module_t* module = loom_low_lower_context_module(context);
  const loom_type_t source_type =
      loom_module_value_type(module, relation->source_value_id);
  return loom_type_equal(
             source_type,
             loom_module_value_type(module, relation->destination_value_id)) &&
         loom_x86_predicate_type(source_type, policy->maximum_lane_count, NULL);
}

static void loom_x86_predicate_observe_compare(
    const loom_x86_predicate_representation_policy_t* policy,
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_low_lower_representation_recorder_t* recorder) {
  const bool is_integer = loom_vector_cmpi_isa(source_op);
  const loom_value_id_t lhs = is_integer ? loom_vector_cmpi_lhs(source_op)
                                         : loom_vector_cmpf_lhs(source_op);
  const loom_value_id_t result = is_integer
                                     ? loom_vector_cmpi_result(source_op)
                                     : loom_vector_cmpf_result(source_op);
  const loom_module_t* module = loom_low_lower_context_module(context);
  uint32_t lane_count = 0;
  if (!loom_x86_predicate_type(loom_module_value_type(module, result),
                               policy->maximum_lane_count, &lane_count)) {
    return;
  }
  const loom_type_t operand_type = loom_module_value_type(module, lhs);
  if (!loom_type_is_vector(operand_type) || loom_type_rank(operand_type) != 1 ||
      !loom_type_is_all_static(operand_type)) {
    return;
  }
  const int32_t element_bit_width =
      loom_scalar_type_bitwidth(loom_type_element_type(operand_type));
  if (element_bit_width <= 0) {
    return;
  }
  const uint32_t vector_bit_width = lane_count * (uint32_t)element_bit_width;
  const loom_low_representation_id_t native_representation =
      loom_x86_predicate_native_representation(policy, vector_bit_width);
  if (native_representation == LOOM_LOW_REPRESENTATION_ID_NONE) {
    return;
  }
  loom_low_representation_candidate_t candidates[2];
  const iree_host_size_t candidate_count = loom_x86_predicate_candidates(
      policy, lane_count, native_representation, candidates);
  loom_low_lower_representation_record_candidates(recorder, result, candidates,
                                                  candidate_count);
}

static void loom_x86_predicate_observe_select(
    const loom_x86_predicate_representation_policy_t* policy,
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_low_lower_representation_recorder_t* recorder) {
  const loom_module_t* module = loom_low_lower_context_module(context);
  const loom_value_id_t condition = loom_vector_select_condition(source_op);
  uint32_t lane_count = 0;
  if (!loom_x86_predicate_type(loom_module_value_type(module, condition),
                               policy->maximum_lane_count, &lane_count)) {
    return;
  }
  const loom_type_t value_type =
      loom_module_value_type(module, loom_vector_select_true_value(source_op));
  if (!loom_type_is_vector(value_type) || loom_type_rank(value_type) != 1 ||
      !loom_type_is_all_static(value_type)) {
    return;
  }
  const int32_t element_bit_width =
      loom_scalar_type_bitwidth(loom_type_element_type(value_type));
  if (element_bit_width <= 0) {
    return;
  }
  const uint32_t vector_bit_width = lane_count * (uint32_t)element_bit_width;
  const loom_low_representation_id_t native_representation =
      loom_x86_predicate_native_representation(policy, vector_bit_width);
  if (native_representation == LOOM_LOW_REPRESENTATION_ID_NONE) {
    return;
  }
  loom_low_representation_candidate_t candidates[2];
  const iree_host_size_t candidate_count = loom_x86_predicate_candidates(
      policy, lane_count, native_representation, candidates);
  loom_low_lower_representation_record_costs(recorder, condition, candidates,
                                             candidate_count);
}

static void loom_x86_predicate_observe_lane_access(
    const loom_x86_predicate_representation_policy_t* policy,
    loom_low_lower_context_t* context, loom_value_id_t value_id,
    loom_low_lower_representation_recorder_t* recorder) {
  const loom_module_t* module = loom_low_lower_context_module(context);
  uint32_t lane_count = 0;
  if (!loom_x86_predicate_type(loom_module_value_type(module, value_id),
                               policy->maximum_lane_count, &lane_count)) {
    return;
  }
  loom_low_representation_candidate_t candidates[2];
  iree_host_size_t candidate_count = 0;
  if (policy->profile == LOOM_X86_PREDICATE_PROFILE_AVX512) {
    // XMM/YMM carriers have direct lane operations. A K lane access requires
    // moving the mask through a GPR and isolating the bit. The 64-lane carrier
    // has no direct operation until the ZMM lane family is introduced, so its
    // K representation remains cheaper.
    candidates[candidate_count++] = (loom_low_representation_candidate_t){
        .representation = LOOM_X86_AVX512_PREDICATE_REPRESENTATION_MASK,
        .cost = lane_count == 64
                    ? (loom_low_representation_cost_t){0}
                    : (loom_low_representation_cost_t){.runtime = 3,
                                                       .code_size = 12},
    };
    candidates[candidate_count++] = (loom_low_representation_candidate_t){
        .representation = LOOM_X86_AVX512_PREDICATE_REPRESENTATION_CARRIER,
        .cost =
            lane_count == 64
                ? (loom_low_representation_cost_t){.runtime = 1, .code_size = 6}
                : (loom_low_representation_cost_t){0},
    };
  } else {
    candidate_count = loom_x86_predicate_candidates(
        policy, lane_count, LOOM_X86_PREDICATE_REPRESENTATION_XMM, candidates);
  }
  loom_low_lower_representation_record_costs(recorder, value_id, candidates,
                                             candidate_count);
}

static void loom_x86_predicate_observe_insert(
    const loom_x86_predicate_representation_policy_t* policy,
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_low_lower_representation_recorder_t* recorder) {
  const loom_value_id_t destination = loom_vector_insert_dest(source_op);
  const loom_value_id_t result = loom_vector_insert_result(source_op);
  const loom_module_t* module = loom_low_lower_context_module(context);
  if (!loom_x86_predicate_type(loom_module_value_type(module, destination),
                               policy->maximum_lane_count, NULL)) {
    return;
  }
  loom_low_lower_representation_record_union(recorder, destination, result);
  loom_x86_predicate_observe_lane_access(policy, context, destination,
                                         recorder);
}

static void loom_x86_predicate_observe_boundary(
    void* user_data, uint8_t action,
    loom_low_lower_representation_boundary_flags_t flags,
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_low_lower_representation_recorder_t* recorder) {
  const loom_x86_predicate_representation_policy_t* policy = user_data;
  (void)flags;
  switch ((loom_x86_predicate_representation_action_t)action) {
    case LOOM_X86_PREDICATE_REPRESENTATION_ACTION_FLEXIBLE_RESULT:
      loom_x86_predicate_constrain_flexible(
          policy, loom_low_lower_context_module(context),
          loom_op_const_results(source_op)[0], recorder);
      return;
    case LOOM_X86_PREDICATE_REPRESENTATION_ACTION_EXTRACT:
      loom_x86_predicate_observe_lane_access(
          policy, context, loom_vector_extract_source(source_op), recorder);
      loom_low_lower_representation_claim_operand(recorder, 0);
      return;
    case LOOM_X86_PREDICATE_REPRESENTATION_ACTION_INSERT:
      loom_x86_predicate_observe_insert(policy, context, source_op, recorder);
      loom_low_lower_representation_claim_operand(recorder, 0);
      return;
    case LOOM_X86_PREDICATE_REPRESENTATION_ACTION_SELECT:
      loom_x86_predicate_observe_select(policy, context, source_op, recorder);
      loom_low_lower_representation_claim_operand(recorder, 0);
      return;
    case LOOM_X86_PREDICATE_REPRESENTATION_ACTION_COMPARE:
      loom_x86_predicate_observe_compare(policy, context, source_op, recorder);
      return;
  }
  IREE_ASSERT_UNREACHABLE("unknown x86 predicate representation action");
}

static void loom_x86_predicate_observe_callable_boundary(
    void* user_data,
    loom_low_lower_representation_callable_boundary_kind_t kind,
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_low_lower_representation_recorder_t* recorder) {
  const loom_x86_predicate_representation_policy_t* policy = user_data;
  const loom_module_t* module = loom_low_lower_context_module(context);
  switch (kind) {
    case LOOM_LOW_LOWER_REPRESENTATION_CALLABLE_DEFINITION: {
      const loom_func_like_t function =
          loom_func_like_const_cast(module, source_op);
      uint16_t argument_count = 0;
      const loom_value_id_t* argument_ids =
          loom_func_like_arg_ids(function, &argument_count);
      loom_x86_predicate_constrain_callable_values(policy, module, argument_ids,
                                                   argument_count, recorder);
      return;
    }
    case LOOM_LOW_LOWER_REPRESENTATION_CALLABLE_CALL: {
      const loom_call_like_t call =
          loom_call_like_const_cast(module, source_op);
      const loom_value_slice_t operands = loom_call_like_operands(call);
      const loom_value_slice_t results = loom_call_like_results(call);
      loom_x86_predicate_constrain_callable_values(
          policy, module, operands.values, operands.count, recorder);
      loom_x86_predicate_constrain_callable_values(
          policy, module, results.values, results.count, recorder);
      return;
    }
    case LOOM_LOW_LOWER_REPRESENTATION_CALLABLE_EXIT:
      loom_x86_predicate_constrain_callable_values(
          policy, module, loom_op_const_operands(source_op),
          source_op->operand_count, recorder);
      return;
  }
  IREE_ASSERT_UNREACHABLE("unknown callable representation boundary kind");
}

static void loom_x86_predicate_observe_unclaimed_operand(
    void* user_data, loom_low_lower_context_t* context,
    const loom_op_t* source_op, uint16_t source_operand_index,
    loom_low_lower_representation_recorder_t* recorder) {
  const loom_x86_predicate_representation_policy_t* policy = user_data;
  loom_x86_predicate_constrain_callable_value(
      policy, loom_low_lower_context_module(context),
      loom_op_const_operands(source_op)[source_operand_index], recorder);
}

static const loom_low_lower_representation_boundary_t
    kX86PredicateRepresentationBoundaries[] = {
        {LOOM_OP_VECTOR_CONSTANT,
         LOOM_X86_PREDICATE_REPRESENTATION_ACTION_FLEXIBLE_RESULT,
         LOOM_LOW_LOWER_REPRESENTATION_BOUNDARY_FLAG_RESULTS},
        {LOOM_OP_VECTOR_SPLAT,
         LOOM_X86_PREDICATE_REPRESENTATION_ACTION_FLEXIBLE_RESULT,
         LOOM_LOW_LOWER_REPRESENTATION_BOUNDARY_FLAG_RESULTS},
        {LOOM_OP_VECTOR_EXTRACT,
         LOOM_X86_PREDICATE_REPRESENTATION_ACTION_EXTRACT,
         LOOM_LOW_LOWER_REPRESENTATION_BOUNDARY_FLAG_OPERANDS},
        {LOOM_OP_VECTOR_INSERT, LOOM_X86_PREDICATE_REPRESENTATION_ACTION_INSERT,
         LOOM_LOW_LOWER_REPRESENTATION_BOUNDARY_FLAG_OPERANDS |
             LOOM_LOW_LOWER_REPRESENTATION_BOUNDARY_FLAG_RESULTS},
        {LOOM_OP_VECTOR_SELECT, LOOM_X86_PREDICATE_REPRESENTATION_ACTION_SELECT,
         LOOM_LOW_LOWER_REPRESENTATION_BOUNDARY_FLAG_OPERANDS},
        {LOOM_OP_VECTOR_CMPI, LOOM_X86_PREDICATE_REPRESENTATION_ACTION_COMPARE,
         LOOM_LOW_LOWER_REPRESENTATION_BOUNDARY_FLAG_RESULTS},
        {LOOM_OP_VECTOR_CMPF, LOOM_X86_PREDICATE_REPRESENTATION_ACTION_COMPARE,
         LOOM_LOW_LOWER_REPRESENTATION_BOUNDARY_FLAG_RESULTS},
};
static const loom_low_lower_representation_boundary_span_t
    kX86PredicateRepresentationBoundarySpans[] = {
        {0, IREE_ARRAYSIZE(kX86PredicateRepresentationBoundaries)},
};
static_assert((loom_op_kind_t)LOOM_OP_VECTOR_CONSTANT <
                      (loom_op_kind_t)LOOM_OP_VECTOR_SPLAT &&
                  (loom_op_kind_t)LOOM_OP_VECTOR_SPLAT <
                      (loom_op_kind_t)LOOM_OP_VECTOR_EXTRACT &&
                  (loom_op_kind_t)LOOM_OP_VECTOR_EXTRACT <
                      (loom_op_kind_t)LOOM_OP_VECTOR_INSERT &&
                  (loom_op_kind_t)LOOM_OP_VECTOR_INSERT <
                      (loom_op_kind_t)LOOM_OP_VECTOR_SELECT &&
                  (loom_op_kind_t)LOOM_OP_VECTOR_SELECT <
                      (loom_op_kind_t)LOOM_OP_VECTOR_CMPI &&
                  (loom_op_kind_t)LOOM_OP_VECTOR_CMPI <
                      (loom_op_kind_t)LOOM_OP_VECTOR_CMPF,
              "x86 predicate representation boundaries must remain ordered");

static const loom_x86_predicate_representation_policy_t
    kX86Avx2PredicateRepresentationPolicy = {
        .profile = LOOM_X86_PREDICATE_PROFILE_AVX2,
        .maximum_lane_count = 32,
};

static const loom_x86_predicate_representation_policy_t
    kX86Avx512PredicateRepresentationPolicy = {
        .profile = LOOM_X86_PREDICATE_PROFILE_AVX512,
        .maximum_lane_count = 64,
};

static const loom_low_lower_representation_provider_t
    kX86Avx2PredicateRepresentationProvider = {
        .relation = loom_x86_predicate_representation_relation,
        .observe_boundary = loom_x86_predicate_observe_boundary,
        .observe_callable_boundary =
            loom_x86_predicate_observe_callable_boundary,
        .observe_unclaimed_operand =
            loom_x86_predicate_observe_unclaimed_operand,
        .boundaries = kX86PredicateRepresentationBoundaries,
        .boundary_spans = kX86PredicateRepresentationBoundarySpans,
        .boundary_count = IREE_ARRAYSIZE(kX86PredicateRepresentationBoundaries),
        .boundary_dialect_base_id = LOOM_DIALECT_VECTOR,
        .boundary_dialect_count =
            IREE_ARRAYSIZE(kX86PredicateRepresentationBoundarySpans),
        .relation_mask = LOOM_VALUE_RELATION_MASK_ALL,
        .user_data = (void*)&kX86Avx2PredicateRepresentationPolicy,
};

static const loom_low_lower_representation_provider_t
    kX86Avx512PredicateRepresentationProvider = {
        .relation = loom_x86_predicate_representation_relation,
        .observe_boundary = loom_x86_predicate_observe_boundary,
        .observe_callable_boundary =
            loom_x86_predicate_observe_callable_boundary,
        .observe_unclaimed_operand =
            loom_x86_predicate_observe_unclaimed_operand,
        .boundaries = kX86PredicateRepresentationBoundaries,
        .boundary_spans = kX86PredicateRepresentationBoundarySpans,
        .boundary_count = IREE_ARRAYSIZE(kX86PredicateRepresentationBoundaries),
        .boundary_dialect_base_id = LOOM_DIALECT_VECTOR,
        .boundary_dialect_count =
            IREE_ARRAYSIZE(kX86PredicateRepresentationBoundarySpans),
        .relation_mask = LOOM_VALUE_RELATION_MASK_ALL,
        .user_data = (void*)&kX86Avx512PredicateRepresentationPolicy,
};

const loom_low_lower_source_plan_observer_t
    loom_x86_avx2_predicate_representation_observer = {
        .begin = loom_low_lower_representation_observer_begin,
        .observe = loom_low_lower_representation_observer_observe,
        .end = loom_low_lower_representation_observer_end,
        .user_data = (void*)&kX86Avx2PredicateRepresentationProvider,
};

const loom_low_lower_source_plan_observer_t
    loom_x86_avx512_predicate_representation_observer = {
        .begin = loom_low_lower_representation_observer_begin,
        .observe = loom_low_lower_representation_observer_observe,
        .end = loom_low_lower_representation_observer_end,
        .user_data = (void*)&kX86Avx512PredicateRepresentationProvider,
};
