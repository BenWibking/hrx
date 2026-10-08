// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amdgpu/lower/control_operands.h"

#include "loom/ir/module.h"
#include "loom/target/arch/amdgpu/lower/emit.h"
#include "loom/target/arch/amdgpu/lower/materializers.h"
#include "loom/target/arch/amdgpu/lower/topology.h"
#include "loom/target/arch/amdgpu/lower/types.h"
#include "loom/target/arch/amdgpu/refs/target_refs.h"

iree_status_t loom_amdgpu_prepare_control_operand(
    void* user_data, loom_low_lower_context_t* context,
    const loom_op_t* source_terminator, loom_value_id_t source_value,
    loom_type_t required_type, const void** out_plan) {
  (void)user_data;
  (void)source_terminator;
  *out_plan = NULL;
  const loom_type_t source_type = loom_module_value_type(
      loom_low_lower_context_module(context), source_value);
  const bool requires_sgpr = loom_amdgpu_low_type_is_register_class(
      context, required_type, LOOM_AMDGPU_REG_CLASS_ID_SGPR);
  const bool requires_vgpr = loom_amdgpu_low_type_is_register_class(
      context, required_type, LOOM_AMDGPU_REG_CLASS_ID_VGPR);
  const loom_type_t actual_type =
      loom_low_lower_value_binding_type(context, source_value);
  if (requires_sgpr && loom_type_is_scalar(source_type) &&
      !loom_amdgpu_type_is_i1(source_type) &&
      loom_amdgpu_low_type_is_register_class(context, actual_type,
                                             LOOM_AMDGPU_REG_CLASS_ID_VGPR)) {
    // Scalar destinations are selected from canonical distribution facts.
    // A producer may nevertheless use VGPR instructions (for example, a
    // collective); selecting one lane is valid only with that shared proof.
    IREE_ASSERT(
        loom_value_facts_is_subgroup_uniform(loom_value_fact_table_lookup(
            loom_low_lower_context_fact_table(context), source_value)));
  }
  if (requires_sgpr && loom_amdgpu_type_is_i1(source_type) &&
      loom_low_register_type_unit_count(required_type) == 2) {
    return loom_amdgpu_prepare_native_i1_mask(context, source_value, out_plan);
  }
  if (requires_vgpr && (loom_amdgpu_type_is_i32(source_type) ||
                        loom_amdgpu_type_is_f32(source_type))) {
    return loom_amdgpu_prepare_vgpr_literal(context, source_value, out_plan);
  }
  return iree_ok_status();
}

// Materializes a subgroup-uniform scalar stored in one or two VGPRs as an
// SGPR branch payload without changing its canonical source mapping.
static iree_status_t loom_amdgpu_materialize_uniform_vgpr_scalar_as_sgpr(
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_value_id_t low_value_id, loom_type_t required_low_type,
    loom_value_id_t* out_low_value_id) {
  *out_low_value_id = LOOM_VALUE_ID_INVALID;
  const loom_module_t* module = loom_low_lower_context_module(context);
  const loom_type_t actual_type = loom_module_value_type(module, low_value_id);
  IREE_ASSERT(loom_amdgpu_low_type_is_register_class(
      context, actual_type, LOOM_AMDGPU_REG_CLASS_ID_VGPR));
  IREE_ASSERT(loom_amdgpu_low_type_is_register_class(
      context, required_low_type, LOOM_AMDGPU_REG_CLASS_ID_SGPR));

  const uint32_t register_count =
      loom_low_register_type_unit_count(actual_type);
  IREE_ASSERT_GE(register_count, 1);
  IREE_ASSERT_LE(register_count, 2);
  const loom_type_t vgpr_type =
      loom_low_register_carrier_type_with_unit_count(actual_type, 1);
  const loom_type_t sgpr_type =
      loom_low_register_carrier_type_with_unit_count(required_low_type, 1);
  // Scalar carriers contain at most two 32-bit register units.
  loom_value_id_t sgpr_registers[2] = {
      LOOM_VALUE_ID_INVALID,
      LOOM_VALUE_ID_INVALID,
  };
  for (uint32_t i = 0; i < register_count; ++i) {
    loom_value_id_t vgpr_register = low_value_id;
    if (register_count != 1) {
      IREE_RETURN_IF_ERROR(loom_amdgpu_extract_low_register_unit(
          context, source_op, low_value_id, register_count, i, vgpr_type,
          &vgpr_register));
    }
    const loom_value_id_t operands[] = {vgpr_register};
    loom_op_t* readfirstlane_op = NULL;
    IREE_RETURN_IF_ERROR(loom_amdgpu_emit_low_op(
        context, source_op, LOOM_AMDGPU_DESCRIPTOR_REF_V_READFIRSTLANE_B32,
        operands, IREE_ARRAYSIZE(operands), loom_make_named_attr_slice(NULL, 0),
        &sgpr_type, 1, &readfirstlane_op));
    sgpr_registers[i] =
        loom_value_slice_get(loom_low_op_results(readfirstlane_op), 0);
  }

  const loom_type_t result_type =
      loom_low_register_carrier_type_with_unit_count(required_low_type,
                                                     register_count);
  return loom_amdgpu_build_low_register_range(context, source_op,
                                              sgpr_registers, register_count,
                                              result_type, out_low_value_id);
}

static iree_status_t loom_amdgpu_materialize_branch_address(
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_value_id_t low_value_id, loom_type_t required_low_type,
    loom_value_id_t* out_low_value_id) {
  *out_low_value_id = low_value_id;

  const loom_module_t* module = loom_low_lower_context_module(context);
  loom_type_t actual_type = loom_module_value_type(module, *out_low_value_id);
  if (loom_type_equal(actual_type, required_low_type)) {
    return iree_ok_status();
  }

  const bool requires_sgpr = loom_amdgpu_low_type_is_register_class(
      context, required_low_type, LOOM_AMDGPU_REG_CLASS_ID_SGPR);
  const bool requires_vgpr = loom_amdgpu_low_type_is_register_class(
      context, required_low_type, LOOM_AMDGPU_REG_CLASS_ID_VGPR);
  if (!requires_sgpr && !requires_vgpr) {
    IREE_ASSERT_UNREACHABLE(
        "AMDGPU address branch payload selected non-register low type");
    IREE_BUILTIN_UNREACHABLE();
  }

  const bool actual_is_sgpr = loom_amdgpu_low_type_is_register_class(
      context, actual_type, LOOM_AMDGPU_REG_CLASS_ID_SGPR);
  const bool actual_is_vgpr = loom_amdgpu_low_type_is_register_class(
      context, actual_type, LOOM_AMDGPU_REG_CLASS_ID_VGPR);
  if (requires_vgpr && actual_is_sgpr) {
    IREE_RETURN_IF_ERROR(loom_amdgpu_materialize_low_vgpr_b32_registers(
        context, source_op, *out_low_value_id, out_low_value_id));
    actual_type = loom_module_value_type(module, *out_low_value_id);
  } else if (requires_sgpr && actual_is_vgpr) {
    IREE_RETURN_IF_ERROR(loom_amdgpu_materialize_uniform_vgpr_scalar_as_sgpr(
        context, source_op, *out_low_value_id, required_low_type,
        out_low_value_id));
    actual_type = loom_module_value_type(module, *out_low_value_id);
  }

  if (loom_type_equal(actual_type, required_low_type)) {
    return iree_ok_status();
  }

  const bool actual_matches_required_class =
      loom_amdgpu_low_type_is_register_class(
          context, actual_type,
          requires_vgpr ? LOOM_AMDGPU_REG_CLASS_ID_VGPR
                        : LOOM_AMDGPU_REG_CLASS_ID_SGPR);
  if (!actual_matches_required_class) {
    IREE_ASSERT_UNREACHABLE(
        "AMDGPU address branch payload materialized wrong register class");
    IREE_BUILTIN_UNREACHABLE();
  }

  const uint32_t actual_unit_count =
      loom_low_register_type_unit_count(actual_type);
  const uint32_t required_unit_count =
      loom_low_register_type_unit_count(required_low_type);
  if (actual_unit_count == 2 && required_unit_count == 1) {
    // Entry ABI values remain 64-bit even when their retained range proves
    // that a destination block can use one address register.
    return loom_amdgpu_emit_low_slice(context, source_op, *out_low_value_id,
                                      /*offset=*/0, required_low_type,
                                      out_low_value_id);
  }
  if (actual_unit_count != 1 || required_unit_count != 2) {
    IREE_ASSERT_UNREACHABLE(
        "AMDGPU address branch payload materialized wrong register shape");
    IREE_BUILTIN_UNREACHABLE();
  }

  loom_type_t lane_type =
      loom_low_register_carrier_type_with_unit_count(required_low_type, 1);
  loom_value_id_t high_zero = LOOM_VALUE_ID_INVALID;
  const uint16_t zero_descriptor = requires_vgpr
                                       ? LOOM_AMDGPU_DESCRIPTOR_REF_V_MOV_B32
                                       : LOOM_AMDGPU_DESCRIPTOR_REF_S_MOV_B32;
  IREE_RETURN_IF_ERROR(loom_amdgpu_emit_const_u32(
      context, source_op, zero_descriptor, 0, lane_type, &high_zero));

  const loom_value_id_t lanes[] = {*out_low_value_id, high_zero};
  return loom_amdgpu_build_low_register_range(
      context, source_op, lanes, IREE_ARRAYSIZE(lanes), required_low_type,
      out_low_value_id);
}

iree_status_t loom_amdgpu_emit_control_operand(
    void* user_data, loom_low_lower_context_t* context,
    const loom_op_t* source_terminator, loom_value_id_t source_value_id,
    loom_value_id_t low_value_id, loom_type_t required_low_type,
    const void* plan, loom_value_id_t* out_low_value_id) {
  (void)user_data;
  *out_low_value_id = low_value_id;
  const loom_type_t source_type = loom_module_value_type(
      loom_low_lower_context_module(context), source_value_id);
  const bool requires_vgpr = loom_amdgpu_low_type_is_register_class(
      context, required_low_type, LOOM_AMDGPU_REG_CLASS_ID_VGPR);
  const bool requires_sgpr = loom_amdgpu_low_type_is_register_class(
      context, required_low_type, LOOM_AMDGPU_REG_CLASS_ID_SGPR);
  if ((requires_sgpr || requires_vgpr) &&
      loom_amdgpu_type_is_address_scalar(source_type)) {
    return loom_amdgpu_materialize_branch_address(
        context, source_terminator, low_value_id, required_low_type,
        out_low_value_id);
  }
  if (requires_vgpr) {
    if (loom_amdgpu_type_is_i32(source_type) ||
        loom_amdgpu_vector_i32_register_count(source_type) != 0 ||
        loom_amdgpu_type_is_f32(source_type) ||
        loom_amdgpu_vector_f32_register_count(source_type) != 0) {
      return loom_amdgpu_emit_prepared_vgpr_value(
          context, source_terminator, source_value_id, plan, out_low_value_id);
    }
    return loom_amdgpu_materialize_low_vgpr_b32_registers(
        context, source_terminator, low_value_id, out_low_value_id);
  }

  if (requires_sgpr && loom_amdgpu_type_is_i1(source_type)) {
    if (loom_low_register_type_unit_count(required_low_type) == 2) {
      return loom_amdgpu_emit_prepared_native_i1_mask(
          context, source_terminator, source_value_id, plan, out_low_value_id);
    }
    // A scalar destination has a uniformity proof. Its source can still use a
    // native mask (for example, a Boolean select), or transient SCC state.
    // Capture the truth value without changing the source's canonical mapping.
    loom_value_id_t low_condition = low_value_id;
    const loom_type_t actual_type = loom_module_value_type(
        loom_low_lower_context_module(context), low_value_id);
    if (loom_low_register_type_unit_count(actual_type) == 2) {
      const uint32_t wavefront_size = loom_amdgpu_target_wavefront_size(
          loom_low_lower_context_bundle(context));
      IREE_RETURN_IF_ERROR(loom_amdgpu_emit_lane_mask_nonzero_scc(
          context, source_terminator, low_value_id, wavefront_size,
          &low_condition));
    }
    loom_value_id_t low_false = LOOM_VALUE_ID_INVALID;
    IREE_RETURN_IF_ERROR(loom_amdgpu_emit_const_u32(
        context, source_terminator, LOOM_AMDGPU_DESCRIPTOR_REF_S_MOV_B32, 0,
        required_low_type, &low_false));
    loom_value_id_t low_true = LOOM_VALUE_ID_INVALID;
    IREE_RETURN_IF_ERROR(loom_amdgpu_emit_const_u32(
        context, source_terminator, LOOM_AMDGPU_DESCRIPTOR_REF_S_MOV_B32, 1,
        required_low_type, &low_true));
    const loom_value_id_t operands[] = {low_true, low_false, low_condition};
    loom_op_t* select_op = NULL;
    IREE_RETURN_IF_ERROR(loom_amdgpu_emit_low_op(
        context, source_terminator, LOOM_AMDGPU_DESCRIPTOR_REF_S_CSELECT_B32,
        operands, IREE_ARRAYSIZE(operands), loom_named_attr_slice_empty(),
        &required_low_type, 1, &select_op));
    *out_low_value_id = loom_value_slice_get(loom_low_op_results(select_op), 0);
    return iree_ok_status();
  }

  if (requires_sgpr && loom_type_is_scalar(source_type)) {
    // Preparation proved that selecting one active lane preserves this value.
    return loom_amdgpu_materialize_uniform_vgpr_scalar_as_sgpr(
        context, source_terminator, low_value_id, required_low_type,
        out_low_value_id);
  }

  IREE_ASSERT_UNREACHABLE(
      "AMDGPU branch argument materializer selected unsupported type");
  IREE_BUILTIN_UNREACHABLE();
}

static iree_status_t loom_amdgpu_materialize_full_low_vgpr_b32_registers(
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_value_id_t low_value, loom_value_id_t* out_low_value) {
  *out_low_value = low_value;
  const loom_module_t* module = loom_low_lower_context_module(context);
  const loom_type_t low_type = loom_module_value_type(module, low_value);
  const uint32_t unit_count = loom_low_register_type_unit_count(low_type);
  const bool is_vgpr = loom_amdgpu_low_type_is_register_class(
      context, low_type, LOOM_AMDGPU_REG_CLASS_ID_VGPR);
  if (is_vgpr && unit_count == 1) {
    return loom_amdgpu_materialize_full_low_vgpr_b32(context, source_op,
                                                     low_value, out_low_value);
  }
  return loom_amdgpu_materialize_low_vgpr_b32_registers(
      context, source_op, low_value, out_low_value);
}

iree_status_t loom_amdgpu_materialize_structural_operand(
    void* user_data, loom_low_lower_context_t* context,
    const loom_op_t* source_op, iree_host_size_t operand_index,
    loom_value_id_t source_value_id, loom_value_id_t low_value_id,
    loom_type_t required_low_type, loom_value_id_t* out_low_value_id) {
  (void)user_data;
  (void)operand_index;
  *out_low_value_id = low_value_id;

  const bool requires_vgpr = loom_amdgpu_low_type_is_register_class(
      context, required_low_type, LOOM_AMDGPU_REG_CLASS_ID_VGPR);
  if (!requires_vgpr) {
    return iree_ok_status();
  }
  return loom_amdgpu_materialize_full_low_vgpr_b32_registers(
      context, source_op, *out_low_value_id, out_low_value_id);
}
