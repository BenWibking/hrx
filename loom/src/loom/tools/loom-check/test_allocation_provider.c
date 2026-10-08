// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tools/loom-check/test_allocation_provider.h"

#include <inttypes.h>

#include "loom/codegen/low/allocation.h"
#include "loom/tools/loom-check/diagnostics.h"
#include "loom/tools/loom-check/low_emit.h"

enum loom_check_test_allocation_option_flag_bits_e {
  LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_HAS_STRATEGY = 1u << 0,
  LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_HAS_DIAGNOSTICS = 1u << 1,
  LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_ASSIGNMENT_COUNT = 1u << 2,
  LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_SPILL_COUNT = 1u << 3,
  LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_COPY_COUNT = 1u << 4,
};
typedef uint32_t loom_check_test_allocation_option_flags_t;

typedef struct loom_check_test_allocation_options_t {
  // Module-local target-low function symbol selected by the RUN line.
  iree_string_view_t function_symbol_name;
  // Comma-separated authored values whose locations are requested.
  iree_string_view_t value_selectors;
  // Comma-separated copy result values whose decisions are requested.
  iree_string_view_t copy_result_selectors;
  // Comma-separated spilled values whose plans are requested.
  iree_string_view_t spill_plan_selectors;
  // Candidate selection strategy used by Low schedule construction.
  loom_low_schedule_strategy_t schedule_strategy;
  // Structured allocator diagnostics requested by the RUN line.
  loom_low_allocation_diagnostic_flags_t allocation_diagnostic_flags;
  // Low allocation budget overrides parsed from the RUN line.
  loom_low_allocation_budget_t
      allocation_budgets[LOOM_CHECK_LOW_EMIT_MAX_ALLOCATION_BUDGETS];
  // Number of entries in |allocation_budgets|.
  iree_host_size_t allocation_budget_count;
  // Fixed low allocation requests parsed from the RUN line.
  loom_check_low_emit_fixed_value_spec_list_t allocation_fixed_values;
  // Presence of unique options and requested aggregate observations.
  loom_check_test_allocation_option_flags_t flags;
} loom_check_test_allocation_options_t;

static bool loom_check_test_allocation_matches(
    const loom_check_emit_provider_t* provider,
    iree_string_view_t target_name) {
  (void)provider;
  return iree_string_view_equal(target_name, IREE_SV("low-allocation-query"));
}

static bool loom_check_test_allocation_consume_selector(
    iree_string_view_t* list, iree_string_view_t* out_selector) {
  if (iree_string_view_is_empty(*list)) {
    return false;
  }
  iree_string_view_split(*list, ',', out_selector, list);
  *out_selector = iree_string_view_trim(*out_selector);
  return true;
}

static iree_status_t loom_check_test_allocation_validate_selector_list(
    iree_string_view_t option_name, iree_string_view_t value) {
  if (iree_string_view_is_empty(value)) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "low-allocation-query option '%.*s' requires an SSA value list",
        (int)option_name.size, option_name.data);
  }
  iree_string_view_t remaining = value;
  while (!iree_string_view_is_empty(remaining)) {
    iree_string_view_t selector = iree_string_view_empty();
    loom_check_test_allocation_consume_selector(&remaining, &selector);
    if (!iree_string_view_starts_with(selector, IREE_SV("%")) ||
        selector.size == 1) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "low-allocation-query option '%.*s' expected comma-separated SSA "
          "values, got '%.*s'",
          (int)option_name.size, option_name.data, (int)value.size, value.data);
    }
  }
  return iree_ok_status();
}

static iree_status_t loom_check_test_allocation_parse_unique_selector_option(
    iree_string_view_t name, iree_string_view_t value,
    iree_string_view_t* out_selectors) {
  if (!iree_string_view_is_empty(*out_selectors)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "duplicate low-allocation-query option '%.*s'",
                            (int)name.size, name.data);
  }
  IREE_RETURN_IF_ERROR(
      loom_check_test_allocation_validate_selector_list(name, value));
  *out_selectors = value;
  return iree_ok_status();
}

static iree_status_t loom_check_test_allocation_parse_option(
    iree_string_view_t token, loom_check_test_allocation_options_t* options) {
  if (iree_string_view_equal(token, IREE_SV("assignment-count"))) {
    if (iree_any_bit_set(
            options->flags,
            LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_ASSIGNMENT_COUNT)) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "duplicate low-allocation-query option 'assignment-count'");
    }
    options->flags |= LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_ASSIGNMENT_COUNT;
    return iree_ok_status();
  }
  if (iree_string_view_equal(token, IREE_SV("spill-count"))) {
    if (iree_any_bit_set(options->flags,
                         LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_SPILL_COUNT)) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "duplicate low-allocation-query option 'spill-count'");
    }
    options->flags |= LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_SPILL_COUNT;
    return iree_ok_status();
  }
  if (iree_string_view_equal(token, IREE_SV("copy-count"))) {
    if (iree_any_bit_set(options->flags,
                         LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_COPY_COUNT)) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "duplicate low-allocation-query option 'copy-count'");
    }
    options->flags |= LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_COPY_COUNT;
    return iree_ok_status();
  }

  iree_string_view_t name = iree_string_view_empty();
  iree_string_view_t value = iree_string_view_empty();
  iree_string_view_split(token, '=', &name, &value);
  name = iree_string_view_trim(name);
  value = iree_string_view_trim(value);
  if (iree_string_view_equal(name, IREE_SV("values"))) {
    return loom_check_test_allocation_parse_unique_selector_option(
        name, value, &options->value_selectors);
  }
  if (iree_string_view_equal(name, IREE_SV("copies"))) {
    return loom_check_test_allocation_parse_unique_selector_option(
        name, value, &options->copy_result_selectors);
  }
  if (iree_string_view_equal(name, IREE_SV("spill-plans"))) {
    return loom_check_test_allocation_parse_unique_selector_option(
        name, value, &options->spill_plan_selectors);
  }
  if (iree_string_view_equal(name, IREE_SV("strategy"))) {
    if (iree_any_bit_set(options->flags,
                         LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_HAS_STRATEGY)) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "duplicate low-allocation-query option 'strategy'");
    }
    IREE_RETURN_IF_ERROR(loom_check_low_emit_parse_schedule_strategy(
        value, IREE_SV("low-allocation-query"), &options->schedule_strategy));
    options->flags |= LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_HAS_STRATEGY;
    return iree_ok_status();
  }
  if (iree_string_view_equal(name, IREE_SV("diagnostics"))) {
    if (iree_any_bit_set(
            options->flags,
            LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_HAS_DIAGNOSTICS)) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "duplicate low-allocation-query option 'diagnostics'");
    }
    IREE_RETURN_IF_ERROR(loom_check_low_emit_parse_allocation_diagnostics(
        value, IREE_SV("low-allocation-query"),
        &options->allocation_diagnostic_flags));
    options->flags |= LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_HAS_DIAGNOSTICS;
    return iree_ok_status();
  }
  return loom_check_low_emit_parse_allocation_option(
      token, IREE_SV("low-allocation-query"), options->allocation_budgets,
      IREE_ARRAYSIZE(options->allocation_budgets),
      &options->allocation_budget_count, &options->allocation_fixed_values);
}

static iree_status_t loom_check_test_allocation_parse_options(
    const loom_check_emit_provider_request_t* request,
    loom_check_test_allocation_options_t* out_options) {
  *out_options = (loom_check_test_allocation_options_t){
      .schedule_strategy = LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY,
  };
  IREE_RETURN_IF_ERROR(loom_check_low_emit_fixed_value_spec_list_initialize(
      request->target_options, request->case_arena,
      &out_options->allocation_fixed_values));

  iree_string_view_t symbol_name = iree_string_view_empty();
  iree_string_view_t option_text = iree_string_view_empty();
  iree_string_view_split(request->target_options, ' ', &symbol_name,
                         &option_text);
  symbol_name = iree_string_view_trim(symbol_name);
  option_text = iree_string_view_trim(option_text);
  if (!iree_string_view_starts_with(symbol_name, IREE_SV("@")) ||
      symbol_name.size == 1) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "low-allocation-query requires a low function symbol name");
  }
  out_options->function_symbol_name =
      iree_string_view_substr(symbol_name, 1, IREE_HOST_SIZE_MAX);

  while (!iree_string_view_is_empty(option_text)) {
    iree_string_view_t token = iree_string_view_empty();
    iree_string_view_split(option_text, ' ', &token, &option_text);
    token = iree_string_view_trim(token);
    if (!iree_string_view_is_empty(token)) {
      IREE_RETURN_IF_ERROR(
          loom_check_test_allocation_parse_option(token, out_options));
    }
    option_text = iree_string_view_trim(option_text);
  }
  return iree_ok_status();
}

static iree_status_t loom_check_test_allocation_resolve_selector(
    const loom_low_allocation_table_t* allocation, iree_string_view_t selector,
    loom_value_id_t* out_value_id) {
  const iree_string_view_t value_name =
      iree_string_view_substr(selector, 1, IREE_HOST_SIZE_MAX);
  loom_check_low_emit_value_resolution_t resolution;
  loom_check_low_emit_resolve_function_value(
      allocation->module, allocation->function_op, value_name, &resolution);
  if (resolution.kind == LOOM_CHECK_LOW_EMIT_VALUE_RESOLUTION_NOT_FOUND) {
    return iree_make_status(IREE_STATUS_NOT_FOUND,
                            "low-allocation-query value '%.*s' was not found",
                            (int)selector.size, selector.data);
  }
  if (resolution.kind == LOOM_CHECK_LOW_EMIT_VALUE_RESOLUTION_AMBIGUOUS) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "low-allocation-query value '%.*s' is ambiguous",
                            (int)selector.size, selector.data);
  }
  *out_value_id = resolution.value_id;
  return iree_ok_status();
}

static iree_status_t loom_check_test_allocation_append_value_reference(
    const loom_module_t* module, loom_value_id_t value_id,
    iree_string_builder_t* builder) {
  const iree_string_view_t value_name =
      loom_module_value_name(module, value_id);
  if (!iree_string_view_is_empty(value_name)) {
    return iree_string_builder_append_format(
        builder, "%%%.*s", (int)value_name.size, value_name.data);
  }
  return iree_string_builder_append_format(builder, "%%%" PRIu32, value_id);
}

static iree_status_t loom_check_test_allocation_append_value(
    const loom_low_allocation_table_t* allocation, iree_string_view_t selector,
    iree_string_builder_t* builder) {
  loom_value_id_t value_id = LOOM_VALUE_ID_INVALID;
  IREE_RETURN_IF_ERROR(loom_check_test_allocation_resolve_selector(
      allocation, selector, &value_id));
  const loom_low_allocation_assignment_t* assignment =
      loom_low_allocation_try_map_active_value_assignment(allocation, value_id,
                                                          NULL);
  if (!assignment) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "low-allocation-query value '%.*s' has no allocation assignment",
        (int)selector.size, selector.data);
  }
  iree_string_view_t register_class = iree_string_view_empty();
  IREE_RETURN_IF_ERROR(loom_low_allocation_assignment_register_class_name(
      allocation, assignment, &register_class));
  const iree_string_view_t location_kind =
      loom_low_allocation_location_kind_name(assignment->location_kind);
  return iree_string_builder_append_format(
      builder, "value %.*s: %.*s -> %.*s[%" PRIu32 ":%" PRIu32 "]\n",
      (int)selector.size, selector.data, (int)register_class.size,
      register_class.data, (int)location_kind.size, location_kind.data,
      assignment->location_base, assignment->location_count);
}

static iree_string_view_t loom_check_test_allocation_copy_kind_name(
    loom_low_allocation_copy_kind_t kind) {
  switch (kind) {
    case LOOM_LOW_ALLOCATION_COPY_COALESCED:
      return IREE_SV("coalesced");
    case LOOM_LOW_ALLOCATION_COPY_MATERIALIZED:
      return IREE_SV("materialized");
    default:
      return IREE_SV("unknown");
  }
}

static iree_status_t loom_check_test_allocation_append_copy(
    const loom_low_allocation_table_t* allocation,
    iree_string_view_t result_selector, iree_string_builder_t* builder) {
  loom_value_id_t result_value_id = LOOM_VALUE_ID_INVALID;
  IREE_RETURN_IF_ERROR(loom_check_test_allocation_resolve_selector(
      allocation, result_selector, &result_value_id));
  const loom_low_allocation_copy_decision_t* selected = NULL;
  for (iree_host_size_t i = 0; i < allocation->copy_decision_count; ++i) {
    const loom_low_allocation_copy_decision_t* decision =
        &allocation->copy_decisions[i];
    if (decision->result_value_id != result_value_id) {
      continue;
    }
    if (selected) {
      return iree_make_status(
          IREE_STATUS_FAILED_PRECONDITION,
          "low-allocation-query copy result '%.*s' has multiple decisions",
          (int)result_selector.size, result_selector.data);
    }
    selected = decision;
  }
  if (!selected) {
    return iree_make_status(
        IREE_STATUS_NOT_FOUND,
        "low-allocation-query copy result '%.*s' has no decision",
        (int)result_selector.size, result_selector.data);
  }

  IREE_RETURN_IF_ERROR(iree_string_builder_append_cstring(builder, "copy "));
  IREE_RETURN_IF_ERROR(loom_check_test_allocation_append_value_reference(
      allocation->module, selected->source_value_id, builder));
  IREE_RETURN_IF_ERROR(iree_string_builder_append_cstring(builder, " -> "));
  IREE_RETURN_IF_ERROR(loom_check_test_allocation_append_value_reference(
      allocation->module, selected->result_value_id, builder));
  const iree_string_view_t kind =
      loom_check_test_allocation_copy_kind_name(selected->kind);
  return iree_string_builder_append_format(builder, ": %.*s\n", (int)kind.size,
                                           kind.data);
}

static iree_status_t loom_check_test_allocation_append_spill_plan(
    const loom_low_allocation_table_t* allocation, iree_string_view_t selector,
    iree_string_builder_t* builder) {
  loom_value_id_t value_id = LOOM_VALUE_ID_INVALID;
  IREE_RETURN_IF_ERROR(loom_check_test_allocation_resolve_selector(
      allocation, selector, &value_id));
  const loom_low_allocation_spill_plan_t* selected = NULL;
  for (iree_host_size_t i = 0; i < allocation->spill_plan_count; ++i) {
    const loom_low_allocation_spill_plan_t* spill_plan =
        &allocation->spill_plans[i];
    if (spill_plan->value_id != value_id) {
      continue;
    }
    if (selected) {
      return iree_make_status(
          IREE_STATUS_FAILED_PRECONDITION,
          "low-allocation-query value '%.*s' has multiple spill plans",
          (int)selector.size, selector.data);
    }
    selected = spill_plan;
  }
  if (!selected) {
    return iree_make_status(
        IREE_STATUS_NOT_FOUND,
        "low-allocation-query value '%.*s' has no spill plan",
        (int)selector.size, selector.data);
  }

  const iree_string_view_t slot_space =
      loom_low_spill_slot_space_name(selected->slot_space);
  return iree_string_builder_append_format(
      builder,
      "spill %.*s: %.*s[%" PRIu32 "] bytes=%" PRIu32 " align=%" PRIu32
      " stores=%" PRIu32 " reloads=%" PRIu32 "\n",
      (int)selector.size, selector.data, (int)slot_space.size, slot_space.data,
      selected->slot_index, selected->byte_size, selected->byte_alignment,
      selected->store_count, selected->reload_count);
}

static iree_status_t loom_check_test_allocation_append_selected_values(
    const loom_low_allocation_table_t* allocation, iree_string_view_t selectors,
    iree_string_builder_t* builder) {
  iree_string_view_t selector = iree_string_view_empty();
  while (loom_check_test_allocation_consume_selector(&selectors, &selector)) {
    IREE_RETURN_IF_ERROR(
        loom_check_test_allocation_append_value(allocation, selector, builder));
  }
  return iree_ok_status();
}

static iree_status_t loom_check_test_allocation_append_selected_copies(
    const loom_low_allocation_table_t* allocation, iree_string_view_t selectors,
    iree_string_builder_t* builder) {
  iree_string_view_t selector = iree_string_view_empty();
  while (loom_check_test_allocation_consume_selector(&selectors, &selector)) {
    IREE_RETURN_IF_ERROR(
        loom_check_test_allocation_append_copy(allocation, selector, builder));
  }
  return iree_ok_status();
}

static iree_status_t loom_check_test_allocation_append_selected_spill_plans(
    const loom_low_allocation_table_t* allocation, iree_string_view_t selectors,
    iree_string_builder_t* builder) {
  iree_string_view_t selector = iree_string_view_empty();
  while (loom_check_test_allocation_consume_selector(&selectors, &selector)) {
    IREE_RETURN_IF_ERROR(loom_check_test_allocation_append_spill_plan(
        allocation, selector, builder));
  }
  return iree_ok_status();
}

static iree_status_t loom_check_test_allocation_execute(
    const loom_check_emit_provider_t* provider,
    const loom_check_emit_provider_request_t* request) {
  (void)provider;
  loom_check_test_allocation_options_t options;
  IREE_RETURN_IF_ERROR(
      loom_check_test_allocation_parse_options(request, &options));

  const loom_low_emission_frame_options_t frame_options = {
      .schedule_strategy = options.schedule_strategy,
      .allocation_budgets = options.allocation_budgets,
      .allocation_budget_count = options.allocation_budget_count,
      .allocation_diagnostic_flags = options.allocation_diagnostic_flags,
  };
  loom_low_emission_frame_t frame = {0};
  bool accepted = false;
  IREE_RETURN_IF_ERROR(loom_check_low_emit_packetize_function(
      request, options.function_symbol_name, &frame_options,
      options.allocation_fixed_values.specs,
      options.allocation_fixed_values.count, NULL, &frame, &accepted));
  if (request->diagnostic_collector != NULL &&
      loom_check_diagnostic_collector_has_error(
          request->diagnostic_collector)) {
    return iree_ok_status();
  }
  if (!accepted) {
    return iree_ok_status();
  }

  const loom_low_allocation_table_t* allocation = &frame.allocation;
  iree_string_builder_t* output = &request->result->actual_output;
  if (iree_any_bit_set(
          options.flags,
          LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_ASSIGNMENT_COUNT)) {
    IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
        output, "assignments: %" PRIhsz "\n", allocation->assignment_count));
  }
  if (iree_any_bit_set(options.flags,
                       LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_SPILL_COUNT)) {
    IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
        output, "spills: %" PRIhsz "\n", allocation->spill_count));
  }
  if (iree_any_bit_set(options.flags,
                       LOOM_CHECK_TEST_ALLOCATION_OPTION_FLAG_COPY_COUNT)) {
    IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
        output, "copies: coalesced=%" PRIhsz " materialized=%" PRIhsz "\n",
        allocation->coalesced_copy_count, allocation->materialized_copy_count));
  }

  loom_low_allocation_value_scratch_t value_scratch = {0};
  iree_status_t status =
      loom_low_allocation_acquire_value_scratch(allocation, &value_scratch);
  if (iree_status_is_ok(status)) {
    status = loom_check_test_allocation_append_selected_values(
        allocation, options.value_selectors, output);
  }
  if (iree_status_is_ok(status)) {
    status = loom_check_test_allocation_append_selected_copies(
        allocation, options.copy_result_selectors, output);
  }
  if (iree_status_is_ok(status)) {
    status = loom_check_test_allocation_append_selected_spill_plans(
        allocation, options.spill_plan_selectors, output);
  }
  loom_low_allocation_release_value_scratch(&value_scratch);
  return status;
}

static iree_status_t loom_check_test_allocation_append_names(
    const loom_check_emit_provider_t* provider,
    iree_string_builder_t* builder) {
  (void)provider;
  return iree_string_builder_append_cstring(builder, "low-allocation-query");
}

const loom_check_emit_provider_t loom_check_test_allocation_provider = {
    .name = IREE_SVL("allocation-query"),
    .match = loom_check_test_allocation_matches,
    .execute = loom_check_test_allocation_execute,
    .append_names = loom_check_test_allocation_append_names,
};
