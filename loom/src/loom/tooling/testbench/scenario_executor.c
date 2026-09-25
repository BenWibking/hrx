// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/testbench/scenario_executor.h"

#include <string.h>

static void loom_testbench_prepared_product_deinitialize(
    loom_testbench_prepared_product_t* product) {
  if (product->destroy != NULL) {
    product->destroy(product->user_data);
  }
  *product = (loom_testbench_prepared_product_t){0};
}

void loom_testbench_scenario_execution_options_initialize(
    loom_testbench_scenario_execution_options_t* out_options) {
  *out_options = (loom_testbench_scenario_execution_options_t){
      .host_allocator = iree_allocator_system(),
  };
}

static iree_status_t loom_testbench_prepare_scenario_product(
    const loom_testbench_execution_profile_t* profile,
    const loom_testbench_invocation_plan_t* invocation,
    const loom_testbench_value_table_t* configuration,
    iree_allocator_t host_allocator,
    loom_testbench_prepared_product_t* out_product) {
  *out_product = (loom_testbench_prepared_product_t){0};
  if (profile->prepare == NULL) {
    return iree_make_status(IREE_STATUS_UNAVAILABLE,
                            "scenario execution profile is not configured");
  }
  iree_status_t status =
      profile->prepare(profile->user_data, invocation, configuration,
                       host_allocator, out_product);
  if (!iree_status_is_ok(status)) {
    loom_testbench_prepared_product_deinitialize(out_product);
    return status;
  }
  if (out_product->execute == NULL) {
    loom_testbench_prepared_product_deinitialize(out_product);
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "scenario execution profile '%.*s' prepared no execution callback",
        (int)profile->name.size, profile->name.data);
  }
  out_product->profile = profile->name;
  out_product->invocation = invocation;
  return iree_ok_status();
}

iree_status_t loom_testbench_prepare_scenario_configuration(
    const loom_testbench_scenario_execution_options_t* options,
    const loom_testbench_scenario_configuration_values_t* configuration,
    loom_testbench_prepared_scenario_configuration_t* out_prepared) {
  *out_prepared = (loom_testbench_prepared_scenario_configuration_t){0};
  if (!iree_any_bit_set(configuration->flags,
                        LOOM_TESTBENCH_SCENARIO_VALUE_FLAG_MATERIALIZED)) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "scenario configuration is not materialized");
  }
  const loom_testbench_scenario_plan_t* scenario = configuration->scenario_plan;
  if (scenario->issue_count != 0) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "scenario '%.*s' has %zu planning issues",
                            (int)scenario->name.size, scenario->name.data,
                            scenario->issue_count);
  }

  const iree_allocator_t host_allocator =
      iree_allocator_is_null(options->host_allocator) ? iree_allocator_system()
                                                      : options->host_allocator;
  *out_prepared = (loom_testbench_prepared_scenario_configuration_t){
      .configuration = configuration,
      .host_allocator = host_allocator,
  };
  iree_status_t status = iree_ok_status();
  if (scenario->trial_count != 0) {
    status = iree_allocator_malloc_array(host_allocator, scenario->trial_count,
                                         sizeof(*out_prepared->trials),
                                         (void**)&out_prepared->trials);
    if (iree_status_is_ok(status)) {
      memset(out_prepared->trials, 0,
             scenario->trial_count * sizeof(*out_prepared->trials));
      out_prepared->trial_count = scenario->trial_count;
    }
  }

  for (iree_host_size_t trial_index = 0;
       iree_status_is_ok(status) && trial_index < scenario->trial_count;
       ++trial_index) {
    const loom_testbench_trial_plan_t* trial = &scenario->trials[trial_index];
    loom_testbench_prepared_scenario_trial_t* prepared_trial =
        &out_prepared->trials[trial_index];
    prepared_trial->trial_plan = trial;
    if (trial->issue_count != 0 ||
        trial->action.kind == LOOM_TESTBENCH_SCENARIO_ACTION_NONE) {
      status = iree_make_status(
          IREE_STATUS_FAILED_PRECONDITION,
          "scenario '%.*s' trial %zu has no executable action",
          (int)scenario->name.size, scenario->name.data, trial_index);
      break;
    }
    status = loom_testbench_prepare_scenario_product(
        &options->target, &trial->action.target, &configuration->values,
        host_allocator, &prepared_trial->target);
    if (iree_status_is_ok(status) &&
        trial->action.kind == LOOM_TESTBENCH_SCENARIO_ACTION_COMPARE) {
      status = loom_testbench_prepare_scenario_product(
          &options->oracle, &trial->action.oracle, &configuration->values,
          host_allocator, &prepared_trial->oracle);
    }
  }
  if (!iree_status_is_ok(status)) {
    loom_testbench_prepared_scenario_configuration_deinitialize(out_prepared);
  }
  return status;
}

void loom_testbench_prepared_scenario_configuration_deinitialize(
    loom_testbench_prepared_scenario_configuration_t* prepared) {
  if (prepared == NULL) {
    return;
  }
  for (iree_host_size_t trial_index = prepared->trial_count; trial_index > 0;
       --trial_index) {
    loom_testbench_prepared_scenario_trial_t* trial =
        &prepared->trials[trial_index - 1];
    loom_testbench_prepared_product_deinitialize(&trial->oracle);
    loom_testbench_prepared_product_deinitialize(&trial->target);
  }
  iree_allocator_free(prepared->host_allocator, prepared->trials);
  *prepared = (loom_testbench_prepared_scenario_configuration_t){0};
}

static iree_status_t loom_testbench_scenario_allocate_array(
    iree_allocator_t allocator, iree_host_size_t count,
    iree_host_size_t element_size, void** out_ptr) {
  *out_ptr = NULL;
  if (count == 0) {
    return iree_ok_status();
  }
  IREE_RETURN_IF_ERROR(
      iree_allocator_malloc_array(allocator, count, element_size, out_ptr));
  memset(*out_ptr, 0, count * element_size);
  return iree_ok_status();
}

static iree_status_t loom_testbench_scenario_allocate_value_matrix(
    iree_allocator_t allocator, iree_host_size_t row_count,
    iree_host_size_t column_count, loom_testbench_value_t** out_values) {
  iree_host_size_t value_count = 0;
  if (!iree_host_size_checked_mul(row_count, column_count, &value_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "scenario batch value count overflowed");
  }
  return loom_testbench_scenario_allocate_array(
      allocator, value_count, sizeof(**out_values), (void**)out_values);
}

static void loom_testbench_scenario_reset_values(
    loom_testbench_value_t* values, iree_host_size_t row_count,
    iree_host_size_t column_count) {
  if (values == NULL) {
    return;
  }
  const iree_host_size_t value_count = row_count * column_count;
  for (iree_host_size_t value_index = 0; value_index < value_count;
       ++value_index) {
    loom_testbench_value_deinitialize(&values[value_index]);
  }
}

static loom_testbench_value_t* loom_testbench_scenario_value_matrix_row(
    loom_testbench_value_t* values, iree_host_size_t row_index,
    iree_host_size_t column_count) {
  return column_count == 0 ? NULL : values + row_index * column_count;
}

static void loom_testbench_scenario_trial_executor_reset_calls(
    loom_testbench_scenario_trial_executor_t* executor,
    iree_host_size_t call_count) {
  const loom_testbench_invocation_plan_t* target =
      executor->prepared_trial->target.invocation;
  loom_testbench_scenario_reset_values(executor->target_call_parameters,
                                       call_count, target->workload_count);
  loom_testbench_scenario_reset_values(executor->target_arguments, call_count,
                                       target->input_count);
  loom_testbench_scenario_reset_values(executor->target_results, call_count,
                                       target->result_count);
  const loom_testbench_invocation_plan_t* oracle =
      executor->prepared_trial->oracle.invocation;
  if (oracle != NULL) {
    loom_testbench_scenario_reset_values(executor->oracle_call_parameters,
                                         call_count, oracle->workload_count);
    loom_testbench_scenario_reset_values(executor->oracle_arguments, call_count,
                                         oracle->input_count);
    loom_testbench_scenario_reset_values(executor->oracle_results, call_count,
                                         oracle->result_count);
  }
}

iree_status_t loom_testbench_scenario_trial_executor_initialize(
    const loom_testbench_prepared_scenario_configuration_t* prepared,
    iree_host_size_t trial_index,
    const loom_testbench_value_materializer_options_t* materializer_options,
    iree_host_size_t batch_capacity,
    loom_testbench_scenario_trial_executor_t* out_executor) {
  *out_executor = (loom_testbench_scenario_trial_executor_t){0};
  if (trial_index >= prepared->trial_count) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "scenario trial index %zu exceeds count %zu",
                            trial_index, prepared->trial_count);
  }
  if (batch_capacity == 0) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "scenario batch capacity must be positive");
  }

  const loom_testbench_prepared_scenario_trial_t* prepared_trial =
      &prepared->trials[trial_index];
  const loom_testbench_invocation_plan_t* target =
      prepared_trial->target.invocation;
  const loom_testbench_invocation_plan_t* oracle =
      prepared_trial->oracle.invocation;
  *out_executor = (loom_testbench_scenario_trial_executor_t){
      .prepared_trial = prepared_trial,
      .configuration = prepared->configuration,
      .materializer_options = *materializer_options,
      .host_allocator = prepared->host_allocator,
      .batch_capacity = batch_capacity,
  };
  iree_status_t status = loom_testbench_scenario_allocate_array(
      out_executor->host_allocator, batch_capacity,
      sizeof(*out_executor->trial_values), (void**)&out_executor->trial_values);
  if (iree_status_is_ok(status)) {
    status = loom_testbench_scenario_allocate_array(
        out_executor->host_allocator, batch_capacity,
        sizeof(*out_executor->results), (void**)&out_executor->results);
  }
  if (iree_status_is_ok(status)) {
    status = loom_testbench_scenario_allocate_array(
        out_executor->host_allocator, batch_capacity,
        sizeof(*out_executor->expectation_reports),
        (void**)&out_executor->expectation_reports);
  }
  if (iree_status_is_ok(status)) {
    status = loom_testbench_scenario_allocate_array(
        out_executor->host_allocator, batch_capacity,
        sizeof(*out_executor->target_calls),
        (void**)&out_executor->target_calls);
  }
  if (iree_status_is_ok(status) && oracle != NULL) {
    status = loom_testbench_scenario_allocate_array(
        out_executor->host_allocator, batch_capacity,
        sizeof(*out_executor->oracle_calls),
        (void**)&out_executor->oracle_calls);
  }
  if (iree_status_is_ok(status)) {
    status = loom_testbench_scenario_allocate_value_matrix(
        out_executor->host_allocator, batch_capacity, target->workload_count,
        &out_executor->target_call_parameters);
  }
  if (iree_status_is_ok(status)) {
    status = loom_testbench_scenario_allocate_value_matrix(
        out_executor->host_allocator, batch_capacity, target->input_count,
        &out_executor->target_arguments);
  }
  if (iree_status_is_ok(status)) {
    status = loom_testbench_scenario_allocate_value_matrix(
        out_executor->host_allocator, batch_capacity, target->result_count,
        &out_executor->target_results);
  }
  if (iree_status_is_ok(status) && oracle != NULL) {
    status = loom_testbench_scenario_allocate_value_matrix(
        out_executor->host_allocator, batch_capacity, oracle->workload_count,
        &out_executor->oracle_call_parameters);
  }
  if (iree_status_is_ok(status) && oracle != NULL) {
    status = loom_testbench_scenario_allocate_value_matrix(
        out_executor->host_allocator, batch_capacity, oracle->input_count,
        &out_executor->oracle_arguments);
  }
  if (iree_status_is_ok(status) && oracle != NULL) {
    status = loom_testbench_scenario_allocate_value_matrix(
        out_executor->host_allocator, batch_capacity, oracle->result_count,
        &out_executor->oracle_results);
  }

  const loom_module_t* module = prepared->configuration->values.module;
  const loom_testbench_trial_plan_t* trial = prepared_trial->trial_plan;
  for (iree_host_size_t call_index = 0;
       iree_status_is_ok(status) && call_index < batch_capacity; ++call_index) {
    status = loom_testbench_scenario_trial_values_initialize(
        module, prepared->configuration->scenario_plan, trial_index,
        out_executor->host_allocator, &out_executor->trial_values[call_index]);
    if (iree_status_is_ok(status)) {
      out_executor->initialized_slot_count = call_index + 1;
    }
    if (iree_status_is_ok(status) && oracle != NULL) {
      status = loom_testbench_expectation_report_initialize(
          trial->action.expectation_count, out_executor->host_allocator,
          &out_executor->expectation_reports[call_index]);
    }
    if (iree_status_is_ok(status)) {
      out_executor->target_calls[call_index] = (loom_testbench_product_call_t){
          .identity = &out_executor->trial_values[call_index].identity,
          .call_parameters = loom_testbench_scenario_value_matrix_row(
              out_executor->target_call_parameters, call_index,
              target->workload_count),
          .arguments = loom_testbench_scenario_value_matrix_row(
              out_executor->target_arguments, call_index, target->input_count),
          .results = loom_testbench_scenario_value_matrix_row(
              out_executor->target_results, call_index, target->result_count),
      };
    }
    if (iree_status_is_ok(status) && oracle != NULL) {
      out_executor->oracle_calls[call_index] = (loom_testbench_product_call_t){
          .identity = &out_executor->trial_values[call_index].identity,
          .call_parameters = loom_testbench_scenario_value_matrix_row(
              out_executor->oracle_call_parameters, call_index,
              oracle->workload_count),
          .arguments = loom_testbench_scenario_value_matrix_row(
              out_executor->oracle_arguments, call_index, oracle->input_count),
          .results = loom_testbench_scenario_value_matrix_row(
              out_executor->oracle_results, call_index, oracle->result_count),
      };
    }
  }
  if (!iree_status_is_ok(status)) {
    loom_testbench_scenario_trial_executor_deinitialize(out_executor);
  }
  return status;
}

void loom_testbench_scenario_trial_executor_deinitialize(
    loom_testbench_scenario_trial_executor_t* executor) {
  if (executor == NULL) {
    return;
  }
  if (executor->prepared_trial != NULL) {
    loom_testbench_scenario_trial_executor_reset_calls(
        executor, executor->batch_capacity);
  }
  for (iree_host_size_t call_index = executor->initialized_slot_count;
       call_index > 0; --call_index) {
    loom_testbench_expectation_report_deinitialize(
        &executor->expectation_reports[call_index - 1]);
    loom_testbench_scenario_trial_values_deinitialize(
        &executor->trial_values[call_index - 1]);
  }
  iree_allocator_free(executor->host_allocator, executor->oracle_results);
  iree_allocator_free(executor->host_allocator, executor->oracle_arguments);
  iree_allocator_free(executor->host_allocator,
                      executor->oracle_call_parameters);
  iree_allocator_free(executor->host_allocator, executor->target_results);
  iree_allocator_free(executor->host_allocator, executor->target_arguments);
  iree_allocator_free(executor->host_allocator,
                      executor->target_call_parameters);
  iree_allocator_free(executor->host_allocator, executor->oracle_calls);
  iree_allocator_free(executor->host_allocator, executor->target_calls);
  iree_allocator_free(executor->host_allocator, executor->expectation_reports);
  iree_allocator_free(executor->host_allocator, executor->results);
  iree_allocator_free(executor->host_allocator, executor->trial_values);
  *executor = (loom_testbench_scenario_trial_executor_t){0};
}

static iree_status_t loom_testbench_scenario_load_values(
    const loom_testbench_value_table_t* table, const loom_value_id_t* value_ids,
    iree_host_size_t value_count, loom_testbench_value_t* values) {
  for (iree_host_size_t value_index = 0; value_index < value_count;
       ++value_index) {
    IREE_RETURN_IF_ERROR(loom_testbench_value_table_lookup_retain(
        table, value_ids[value_index], &values[value_index]));
  }
  return iree_ok_status();
}

static iree_status_t loom_testbench_scenario_load_call(
    const loom_testbench_invocation_plan_t* invocation,
    const loom_testbench_value_table_t* table,
    loom_testbench_product_call_t* call) {
  IREE_RETURN_IF_ERROR(loom_testbench_scenario_load_values(
      table, invocation->workload_value_ids, invocation->workload_count,
      call->call_parameters));
  return loom_testbench_scenario_load_values(table, invocation->input_value_ids,
                                             invocation->input_count,
                                             call->arguments);
}

static iree_status_t loom_testbench_scenario_store_call_results(
    const loom_testbench_invocation_plan_t* invocation,
    loom_testbench_product_call_t* call, loom_testbench_value_table_t* table) {
  for (iree_host_size_t result_index = 0;
       result_index < invocation->result_count; ++result_index) {
    if (call->results[result_index].kind == LOOM_TESTBENCH_VALUE_KIND_NONE) {
      return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                              "scenario product result %zu was not assigned",
                              result_index);
    }
  }
  for (iree_host_size_t result_index = 0;
       result_index < invocation->result_count; ++result_index) {
    IREE_RETURN_IF_ERROR(loom_testbench_value_table_assign_move(
        table, invocation->result_value_ids[result_index],
        &call->results[result_index]));
  }
  return iree_ok_status();
}

static iree_status_t loom_testbench_scenario_load_batch_calls(
    const loom_testbench_invocation_plan_t* invocation,
    loom_testbench_scenario_trial_values_t* trial_values, bool use_oracle,
    iree_host_size_t call_count, loom_testbench_product_call_t* calls) {
  for (iree_host_size_t call_index = 0; call_index < call_count; ++call_index) {
    loom_testbench_value_table_t* table =
        use_oracle ? &trial_values[call_index].oracle
                   : &trial_values[call_index].target;
    IREE_RETURN_IF_ERROR(loom_testbench_scenario_load_call(invocation, table,
                                                           &calls[call_index]));
  }
  return iree_ok_status();
}

static iree_status_t loom_testbench_scenario_store_batch_results(
    const loom_testbench_invocation_plan_t* invocation,
    loom_testbench_scenario_trial_values_t* trial_values, bool use_oracle,
    iree_host_size_t call_count, loom_testbench_product_call_t* calls) {
  for (iree_host_size_t call_index = 0; call_index < call_count; ++call_index) {
    loom_testbench_value_table_t* table =
        use_oracle ? &trial_values[call_index].oracle
                   : &trial_values[call_index].target;
    IREE_RETURN_IF_ERROR(loom_testbench_scenario_store_call_results(
        invocation, &calls[call_index], table));
  }
  return iree_ok_status();
}

static iree_status_t loom_testbench_scenario_execute_product(
    const loom_testbench_prepared_product_t* product,
    loom_testbench_scenario_trial_values_t* trial_values, bool use_oracle,
    iree_host_size_t call_count, loom_testbench_product_call_t* calls) {
  IREE_RETURN_IF_ERROR(loom_testbench_scenario_load_batch_calls(
      product->invocation, trial_values, use_oracle, call_count, calls));
  IREE_RETURN_IF_ERROR(product->execute(product->user_data, product->invocation,
                                        call_count, calls));
  return loom_testbench_scenario_store_batch_results(
      product->invocation, trial_values, use_oracle, call_count, calls);
}

iree_status_t loom_testbench_run_scenario_trial_batch(
    loom_testbench_scenario_trial_executor_t* executor,
    iree_host_size_t first_trial_ordinal, iree_host_size_t trial_count,
    loom_testbench_scenario_trial_result_list_t* out_results) {
  *out_results = (loom_testbench_scenario_trial_result_list_t){0};
  const loom_testbench_trial_plan_t* trial =
      executor->prepared_trial->trial_plan;
  if (trial_count > executor->batch_capacity) {
    return iree_make_status(
        IREE_STATUS_OUT_OF_RANGE,
        "scenario trial batch count %zu exceeds capacity %zu", trial_count,
        executor->batch_capacity);
  }
  if (first_trial_ordinal > trial->trial_count ||
      trial_count > trial->trial_count - first_trial_ordinal) {
    return iree_make_status(
        IREE_STATUS_OUT_OF_RANGE,
        "scenario trial range [%zu, %zu) exceeds domain count %zu",
        first_trial_ordinal, first_trial_ordinal + trial_count,
        trial->trial_count);
  }
  loom_testbench_scenario_trial_executor_reset_calls(executor, trial_count);

  iree_status_t status = iree_ok_status();
  for (iree_host_size_t call_index = 0;
       iree_status_is_ok(status) && call_index < trial_count; ++call_index) {
    if (trial->action.kind == LOOM_TESTBENCH_SCENARIO_ACTION_COMPARE) {
      loom_testbench_expectation_report_reset(
          &executor->expectation_reports[call_index]);
    }
    status = loom_testbench_scenario_trial_values_materialize(
        &executor->materializer_options, executor->configuration,
        first_trial_ordinal + call_index, &executor->trial_values[call_index]);
  }
  if (iree_status_is_ok(status) && trial_count != 0) {
    status = loom_testbench_scenario_execute_product(
        &executor->prepared_trial->target, executor->trial_values,
        /*use_oracle=*/false, trial_count, executor->target_calls);
    if (!iree_status_is_ok(status)) {
      status = iree_status_annotate_f(
          status, "executing target profile '%.*s'",
          (int)executor->prepared_trial->target.profile.size,
          executor->prepared_trial->target.profile.data);
    }
  }
  if (iree_status_is_ok(status) && trial_count != 0 &&
      trial->action.kind == LOOM_TESTBENCH_SCENARIO_ACTION_COMPARE) {
    status = loom_testbench_scenario_execute_product(
        &executor->prepared_trial->oracle, executor->trial_values,
        /*use_oracle=*/true, trial_count, executor->oracle_calls);
    if (!iree_status_is_ok(status)) {
      status = iree_status_annotate_f(
          status, "executing oracle profile '%.*s'",
          (int)executor->prepared_trial->oracle.profile.size,
          executor->prepared_trial->oracle.profile.data);
    }
  }

  for (iree_host_size_t call_index = 0;
       iree_status_is_ok(status) && call_index < trial_count; ++call_index) {
    loom_testbench_scenario_trial_result_t* result =
        &executor->results[call_index];
    *result = (loom_testbench_scenario_trial_result_t){
        .identity = executor->trial_values[call_index].identity,
        .trial_plan = trial,
        .passed = true,
    };
    if (trial->action.kind == LOOM_TESTBENCH_SCENARIO_ACTION_COMPARE) {
      loom_testbench_expectation_report_t* report =
          &executor->expectation_reports[call_index];
      status = loom_testbench_evaluate_scenario_action_expectations(
          &trial->action, &executor->trial_values[call_index].target,
          &executor->trial_values[call_index].oracle,
          /*observations=*/NULL, report);
      result->passed = report->failure_count == 0;
      result->expectation_report = report;
    }
  }

  if (!iree_status_is_ok(status)) {
    loom_testbench_scenario_trial_executor_reset_calls(executor, trial_count);
    return iree_status_annotate_f(
        status, "executing check.scenario '@%.*s' trial domain",
        (int)executor->configuration->scenario_plan->name.size,
        executor->configuration->scenario_plan->name.data);
  }
  loom_testbench_scenario_trial_executor_reset_calls(executor, trial_count);
  out_results->values = executor->results;
  out_results->count = trial_count;
  return iree_ok_status();
}
