// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/low/lower/structural_plan.h"

#include <string.h>

#include "loom/codegen/low/lower/context.h"
#include "loom/codegen/low/lower/source_call.h"
#include "loom/error/error_catalog.h"
#include "loom/ir/facts.h"
#include "loom/ir/module.h"
#include "loom/ops/cfg/ops.h"
#include "loom/ops/scf/ops.h"

struct loom_low_lower_structural_block_t {
  // Canonical native argument types in source argument order.
  loom_type_id_t* argument_types;
  // Zero for a dynamic condition, one for false, and two for true.
  uint8_t condition;
};

struct loom_low_lower_structural_types_t {
  // Next typed operation in the shared source traversal.
  struct loom_low_lower_structural_types_t* next;
  // Source operation whose result types precede any while header types in the
  // canonical type ID array immediately following this record.
  const loom_op_t* source_op;
};

static iree_status_t loom_low_lower_structural_require_blocks(
    loom_low_lower_context_t* context) {
  loom_low_lower_structural_plan_t* plan =
      &context->lowering.source_plan.structural;
  if (plan->blocks != NULL) {
    return iree_ok_status();
  }
  const uint16_t count =
      loom_func_like_body(context->source_function)->block_count;
  IREE_RETURN_IF_ERROR(loom_low_lower_allocate_function_array(
      context, count, sizeof(*plan->blocks), (void**)&plan->blocks));
  memset(plan->blocks, 0, count * sizeof(*plan->blocks));
  return iree_ok_status();
}

iree_status_t loom_low_lower_structural_plan_branch(
    loom_low_lower_context_t* context, const loom_op_t* source_op) {
  const loom_value_facts_t facts = loom_value_fact_table_lookup(
      context->lowering.fact_table, loom_cfg_cond_br_condition(source_op));
  bool condition = false;
  if (loom_value_facts_as_exact_bool(facts, &condition)) {
    IREE_RETURN_IF_ERROR(loom_low_lower_structural_require_blocks(context));
    context->lowering.source_plan.structural
        .blocks[source_op->parent_block->region_index]
        .condition = condition ? 2 : 1;
  }
  return iree_ok_status();
}

bool loom_low_lower_structural_branch_exact_bool(
    const loom_low_lower_context_t* context, const loom_op_t* source_op,
    bool* out_condition) {
  const loom_low_lower_structural_block_t* blocks =
      context->lowering.source_plan.structural.blocks;
  const uint8_t condition =
      blocks ? blocks[source_op->parent_block->region_index].condition : 0;
  if (out_condition != NULL) {
    *out_condition = condition == 2;
  }
  return condition != 0;
}

static iree_status_t loom_low_lower_structural_plan_types(
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    const loom_value_id_t* source_values, uint16_t count,
    loom_type_id_t* type_ids) {
  iree_status_t status = iree_ok_status();
  for (uint16_t i = 0; i < count && iree_status_is_ok(status) &&
                       context->result->error_count == 0;
       ++i) {
    loom_type_t type = loom_type_none();
    status =
        loom_low_lower_map_value(context, source_op, source_values[i], &type);
    if (iree_status_is_ok(status) && type_ids != NULL &&
        context->result->error_count == 0) {
      status = loom_module_intern_type_id(context->module, type, &type_ids[i]);
    }
  }
  return status;
}

iree_status_t loom_low_lower_structural_plan_block(
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    const loom_block_t* source_block) {
  loom_type_id_t* type_ids = NULL;
  if (source_block->arg_count != 0 &&
      source_block->parent_region ==
          loom_func_like_body(context->source_function)) {
    IREE_RETURN_IF_ERROR(loom_low_lower_structural_require_blocks(context));
    IREE_RETURN_IF_ERROR(loom_low_lower_allocate_function_array(
        context, source_block->arg_count, sizeof(*type_ids),
        (void**)&type_ids));
    context->lowering.source_plan.structural.blocks[source_block->region_index]
        .argument_types = type_ids;
  }
  return loom_low_lower_structural_plan_types(
      context, source_op, source_block->arg_ids, source_block->arg_count,
      type_ids);
}

loom_type_t loom_low_lower_structural_block_argument_type(
    const loom_low_lower_context_t* context, uint16_t block_index,
    uint16_t argument_index) {
  return loom_type_table_get(
      &context->module->types,
      context->lowering.source_plan.structural.blocks[block_index]
          .argument_types[argument_index]);
}

iree_status_t loom_low_lower_structural_plan_op(
    loom_low_lower_context_t* context, const loom_op_t* source_op) {
  if (loom_scf_for_isa(source_op) &&
      loom_scf_for_pipeline_depth_is_present(source_op)) {
    const loom_diagnostic_param_t params[] = {
        loom_param_string(IREE_SV("pipeline")),
        loom_param_i64(0),
        loom_param_string(
            IREE_SV("consumed by pipeline-scf-for before source-to-low")),
    };
    return loom_low_lower_emit_target_context_error(
        context, source_op, LOOM_ERR_STRUCTURE_014, params,
        IREE_ARRAYSIZE(params));
  }
  const loom_block_t* header =
      loom_scf_while_isa(source_op)
          ? loom_region_const_entry_block(loom_scf_while_before(source_op))
          : NULL;
  const uint16_t header_count = header ? header->arg_count : 0;
  if (!loom_scf_if_isa(source_op) && header == NULL &&
      !loom_low_lower_source_call_is_structural(context->module, source_op)) {
    return iree_ok_status();
  }
  const iree_host_size_t count =
      source_op->result_count + (iree_host_size_t)header_count;
  if (count == 0) {
    return iree_ok_status();
  }
  loom_low_lower_structural_types_t* record = NULL;
  IREE_RETURN_IF_ERROR(loom_low_lower_allocate_plan_data(
      context, sizeof(*record) + count * sizeof(loom_type_id_t),
      (void**)&record));
  record->source_op = source_op;
  record->next = NULL;
  loom_type_id_t* type_ids = (loom_type_id_t*)(record + 1);
  IREE_RETURN_IF_ERROR(loom_low_lower_structural_plan_types(
      context, source_op, loom_op_const_results(source_op),
      source_op->result_count, type_ids));
  if (header != NULL) {
    IREE_RETURN_IF_ERROR(loom_low_lower_structural_plan_types(
        context, source_op, header->arg_ids, header_count,
        type_ids + source_op->result_count));
  }
  loom_low_lower_structural_plan_t* plan =
      &context->lowering.source_plan.structural;
  if (plan->last != NULL) {
    plan->last->next = record;
  } else {
    plan->cursor = record;
  }
  plan->last = record;
  return iree_ok_status();
}

static iree_status_t loom_low_lower_structural_expand_types(
    loom_low_lower_context_t* context, const loom_type_id_t* type_ids,
    uint16_t count, loom_type_t** out_types) {
  IREE_RETURN_IF_ERROR(loom_low_lower_allocate_emission_array(
      context, count, sizeof(**out_types), (void**)out_types));
  for (uint16_t i = 0; i < count; ++i) {
    (*out_types)[i] = loom_type_table_get(&context->module->types, type_ids[i]);
  }
  return iree_ok_status();
}

iree_status_t loom_low_lower_structural_take_types(
    loom_low_lower_context_t* context, const loom_op_t* source_op,
    loom_type_t** out_result_types, loom_type_t** out_header_types) {
  *out_result_types = NULL;
  const uint16_t header_count =
      loom_scf_while_isa(source_op)
          ? loom_region_const_entry_block(loom_scf_while_before(source_op))
                ->arg_count
          : 0;
  if (out_header_types != NULL) {
    *out_header_types = NULL;
  }
  if (source_op->result_count == 0 && header_count == 0) {
    return iree_ok_status();
  }
  loom_low_lower_structural_plan_t* plan =
      &context->lowering.source_plan.structural;
  const loom_low_lower_structural_types_t* record = plan->cursor;
  IREE_ASSERT(record != NULL && record->source_op == source_op,
              "structural emission must consume the planned source order");
  plan->cursor = record->next;
  const loom_type_id_t* type_ids = (const loom_type_id_t*)(record + 1);
  IREE_RETURN_IF_ERROR(loom_low_lower_structural_expand_types(
      context, type_ids, source_op->result_count, out_result_types));
  if (header_count != 0) {
    IREE_RETURN_IF_ERROR(loom_low_lower_structural_expand_types(
        context, type_ids + source_op->result_count, header_count,
        out_header_types));
  }
  return iree_ok_status();
}
