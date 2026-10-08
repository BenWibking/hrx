// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/low/lower/bindings.h"

#include "loom/codegen/low/lower/context.h"
#include "loom/ir/module.h"

iree_status_t loom_low_lower_plan_value_type(loom_low_lower_context_t* context,
                                             loom_value_id_t source_value_id,
                                             loom_type_t type) {
  loom_type_id_t type_id;
  IREE_RETURN_IF_ERROR(
      loom_module_intern_type_id(context->module, type, &type_id));
  const loom_value_ordinal_t ordinal = loom_low_lowering_frame_value_ordinal(
      &context->lowering, source_value_id);
  context->lowering.value_bindings[ordinal].type = type_id;
  return iree_ok_status();
}

loom_type_t loom_low_lower_value_binding_type(
    const loom_low_lower_context_t* context, loom_value_id_t source_value_id) {
  const loom_value_ordinal_t ordinal = loom_low_lowering_frame_value_ordinal(
      &context->lowering, source_value_id);
  const loom_low_lower_value_binding_t binding =
      context->lowering.value_bindings[ordinal];
  if (iree_any_bit_set(context->lowering.source_plan.value_flags[ordinal],
                       LOOM_LOW_LOWER_VALUE_MATERIALIZED)) {
    return loom_module_value_type(context->module, binding.value);
  }
  IREE_ASSERT_NE(binding.type, LOOM_TYPE_ID_INVALID,
                 "native carrier must be published by the selected producer");
  return loom_type_table_get(&context->module->types, binding.type);
}

iree_status_t loom_low_lower_lookup_value(loom_low_lower_context_t* context,
                                          loom_value_id_t source_value_id,
                                          loom_value_id_t* out_low_value_id) {
  const loom_value_ordinal_t ordinal = loom_low_lowering_frame_value_ordinal(
      &context->lowering, source_value_id);
  IREE_ASSERT(
      iree_any_bit_set(context->lowering.source_plan.value_flags[ordinal],
                       LOOM_LOW_LOWER_VALUE_MATERIALIZED),
      "SSA lookup must follow producer materialization");
  const loom_value_id_t low_value =
      context->lowering.value_bindings[ordinal].value;
  IREE_ASSERT_NE(low_value, LOOM_LOW_LOWER_VALUE_ID_ELIDED,
                 "source-to-low requested elided source value");
  *out_low_value_id = low_value;
  return iree_ok_status();
}

bool loom_low_lower_source_value_has_low_mapping(
    const loom_low_lower_context_t* context, loom_value_id_t source_value_id) {
  const loom_value_ordinal_t ordinal = loom_low_lowering_frame_value_ordinal(
      &context->lowering, source_value_id);
  return iree_any_bit_set(context->lowering.source_plan.value_flags[ordinal],
                          LOOM_LOW_LOWER_VALUE_MATERIALIZED) &&
         context->lowering.value_bindings[ordinal].value !=
             LOOM_LOW_LOWER_VALUE_ID_ELIDED;
}

iree_status_t loom_low_lower_bind_value(loom_low_lower_context_t* context,
                                        loom_value_id_t source_value_id,
                                        loom_value_id_t low_value_id) {
  const loom_value_ordinal_t ordinal = loom_low_lowering_frame_value_ordinal(
      &context->lowering, source_value_id);
  loom_low_lower_value_binding_t* binding =
      &context->lowering.value_bindings[ordinal];
  if (iree_any_bit_set(context->lowering.source_plan.value_flags[ordinal],
                       LOOM_LOW_LOWER_VALUE_MATERIALIZED)) {
    IREE_ASSERT_EQ(binding->value, low_value_id);
  } else if (binding->type != LOOM_TYPE_ID_INVALID) {
    IREE_ASSERT(loom_type_equal(
                    loom_low_lower_value_binding_type(context, source_value_id),
                    loom_module_value_type(context->module, low_value_id)),
                "materialization must preserve the selected producer carrier");
  }
  binding->value = low_value_id;
  context->lowering.source_plan.value_flags[ordinal] |=
      LOOM_LOW_LOWER_VALUE_MATERIALIZED;
  return loom_low_lower_copy_value_name(context, source_value_id, low_value_id);
}

iree_status_t loom_low_lower_replace_value_binding(
    loom_low_lower_context_t* context, loom_value_id_t source_value_id,
    loom_value_id_t low_value_id) {
  const loom_value_ordinal_t ordinal = loom_low_lowering_frame_value_ordinal(
      &context->lowering, source_value_id);
  IREE_ASSERT(
      loom_low_lower_source_value_has_low_mapping(context, source_value_id));
  context->lowering.value_bindings[ordinal].value = low_value_id;
  return loom_low_lower_copy_value_name(context, source_value_id, low_value_id);
}

iree_status_t loom_low_lower_bind_value_alias(loom_low_lower_context_t* context,
                                              loom_value_id_t source_value_id,
                                              loom_value_id_t result_value_id) {
  loom_value_id_t low_value_id;
  IREE_RETURN_IF_ERROR(
      loom_low_lower_lookup_value(context, source_value_id, &low_value_id));
  return loom_low_lower_bind_value(context, result_value_id, low_value_id);
}

iree_status_t loom_low_lower_elide_value(loom_low_lower_context_t* context,
                                         loom_value_id_t source_value_id) {
  const loom_value_ordinal_t ordinal = loom_low_lowering_frame_value_ordinal(
      &context->lowering, source_value_id);
  context->lowering.value_bindings[ordinal].value =
      LOOM_LOW_LOWER_VALUE_ID_ELIDED;
  context->lowering.source_plan.value_flags[ordinal] |=
      LOOM_LOW_LOWER_VALUE_MATERIALIZED;
  return iree_ok_status();
}
