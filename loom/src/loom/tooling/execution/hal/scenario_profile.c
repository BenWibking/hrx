// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/execution/hal/scenario_profile.h"

#include <string.h>

#include "loom/tooling/execution/hal/testbench_staging.h"

typedef struct loom_run_hal_testbench_scenario_product_t {
  // Host allocator owning this product.
  iree_allocator_t host_allocator;
  // Eagerly compiled ordinary HAL kernel product.
  loom_run_hal_testbench_actual_provider_t provider;
} loom_run_hal_testbench_scenario_product_t;

typedef struct loom_run_hal_testbench_scenario_batch_t {
  // Host allocator owning all batch arrays.
  iree_allocator_t host_allocator;
  // Per-call retained binding lists.
  loom_run_hal_binding_list_t* binding_lists;
  // Number of initialized entries in |binding_lists|.
  iree_host_size_t initialized_binding_list_count;
  // Per-call dispatch recording descriptors.
  loom_run_hal_dispatch_sequence_step_t* steps;
  // Flat binding byte lengths in call and ABI order.
  iree_device_size_t* binding_lengths;
  // Flat staged binding table in call and ABI order.
  iree_hal_buffer_binding_t* binding_table;
  // Maximum number of entries in the flat binding arrays.
  iree_host_size_t binding_capacity;
  // Prepared command sequence containing every call.
  loom_run_hal_dispatch_sequence_t sequence;
  // Alias-preserving host-to-device staging for the flat binding table.
  loom_run_hal_testbench_staging_t staging;
} loom_run_hal_testbench_scenario_batch_t;

static void loom_run_hal_testbench_scenario_batch_deinitialize(
    loom_run_hal_testbench_scenario_batch_t* batch) {
  loom_run_hal_testbench_staging_deinitialize(&batch->staging);
  loom_run_hal_dispatch_sequence_deinitialize(&batch->sequence);
  for (iree_host_size_t i = batch->initialized_binding_list_count; i > 0; --i) {
    loom_run_hal_binding_list_deinitialize(&batch->binding_lists[i - 1]);
  }
  iree_allocator_free(batch->host_allocator, batch->binding_table);
  iree_allocator_free(batch->host_allocator, batch->binding_lengths);
  iree_allocator_free(batch->host_allocator, batch->steps);
  iree_allocator_free(batch->host_allocator, batch->binding_lists);
  *batch = (loom_run_hal_testbench_scenario_batch_t){0};
}

static iree_status_t loom_run_hal_testbench_scenario_allocate_array(
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

static iree_status_t loom_run_hal_testbench_scenario_batch_initialize(
    iree_host_size_t call_count, iree_host_size_t input_count,
    iree_allocator_t host_allocator,
    loom_run_hal_testbench_scenario_batch_t* out_batch) {
  *out_batch = (loom_run_hal_testbench_scenario_batch_t){
      .host_allocator = host_allocator,
  };
  if (!iree_host_size_checked_mul(call_count, input_count,
                                  &out_batch->binding_capacity)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "HAL scenario batch binding capacity overflowed");
  }

  iree_status_t status = loom_run_hal_testbench_scenario_allocate_array(
      host_allocator, call_count, sizeof(*out_batch->binding_lists),
      (void**)&out_batch->binding_lists);
  if (iree_status_is_ok(status)) {
    status = loom_run_hal_testbench_scenario_allocate_array(
        host_allocator, call_count, sizeof(*out_batch->steps),
        (void**)&out_batch->steps);
  }
  if (iree_status_is_ok(status)) {
    status = loom_run_hal_testbench_scenario_allocate_array(
        host_allocator, out_batch->binding_capacity,
        sizeof(*out_batch->binding_lengths),
        (void**)&out_batch->binding_lengths);
  }
  if (iree_status_is_ok(status)) {
    status = loom_run_hal_testbench_scenario_allocate_array(
        host_allocator, out_batch->binding_capacity,
        sizeof(*out_batch->binding_table), (void**)&out_batch->binding_table);
  }
  if (!iree_status_is_ok(status)) {
    loom_run_hal_testbench_scenario_batch_deinitialize(out_batch);
  }
  return status;
}

static void loom_run_hal_testbench_scenario_product_destroy(void* user_data) {
  loom_run_hal_testbench_scenario_product_t* product =
      (loom_run_hal_testbench_scenario_product_t*)user_data;
  const iree_allocator_t host_allocator = product->host_allocator;
  loom_run_hal_testbench_actual_provider_deinitialize(&product->provider);
  iree_allocator_free(host_allocator, product);
}

static iree_status_t loom_run_hal_testbench_scenario_product_execute(
    void* user_data, const loom_testbench_invocation_plan_t* invocation,
    iree_host_size_t call_count, loom_testbench_product_call_t* calls) {
  loom_run_hal_testbench_scenario_product_t* product =
      (loom_run_hal_testbench_scenario_product_t*)user_data;
  loom_run_hal_testbench_actual_provider_t* provider = &product->provider;
  if (invocation != provider->kernel_launch) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "HAL scenario product received an unexpected invocation");
  }
  if (call_count == 0) {
    return iree_ok_status();
  }

  loom_run_hal_testbench_scenario_batch_t batch = {0};
  iree_status_t status = loom_run_hal_testbench_scenario_batch_initialize(
      call_count, invocation->input_count, product->host_allocator, &batch);
  iree_host_size_t binding_count = 0;
  for (iree_host_size_t call_index = 0;
       iree_status_is_ok(status) && call_index < call_count; ++call_index) {
    loom_testbench_product_call_t* call = &calls[call_index];
    loom_run_hal_binding_list_t* bindings = &batch.binding_lists[call_index];
    batch.initialized_binding_list_count = call_index + 1;
    loom_run_hal_invocation_options_t invocation_options = {0};
    status = loom_run_hal_testbench_actual_provider_materialize_invocation(
        provider, invocation->workload_count, call->call_parameters,
        invocation->input_count, call->arguments, &invocation_options,
        bindings);
    if (!iree_status_is_ok(status)) {
      status = iree_status_annotate_f(
          status,
          "materializing HAL scenario trial configuration %zu domain %zu "
          "ordinal %zu",
          call->identity->configuration_ordinal, call->identity->trial_index,
          call->identity->trial_ordinal);
      break;
    }
    IREE_ASSERT(binding_count + bindings->count <= batch.binding_capacity);
    const iree_host_size_t first_binding = binding_count;
    for (iree_host_size_t binding_index = 0; binding_index < bindings->count;
         ++binding_index) {
      const iree_tooling_buffer_binding_t* binding =
          &bindings->values[binding_index];
      batch.binding_lengths[binding_count] = binding->byte_length;
      batch.binding_table[binding_count] = (iree_hal_buffer_binding_t){
          .buffer = binding->buffer,
          .offset = binding->byte_offset,
          .length = binding->byte_length,
      };
      ++binding_count;
    }
    batch.steps[call_index] = (loom_run_hal_dispatch_sequence_step_t){
        .candidate = &provider->prepared_candidate,
        .execution_epoch = 0,
        .options = invocation_options,
        .binding_lengths =
            bindings->count == 0 ? NULL : &batch.binding_lengths[first_binding],
        .binding_count = bindings->count,
    };
  }

  if (iree_status_is_ok(status)) {
    status = loom_run_hal_dispatch_sequence_prepare(
        &provider->context->runtime, call_count, batch.steps, &batch.sequence);
  }
  if (iree_status_is_ok(status)) {
    status = loom_run_hal_testbench_staging_initialize(
        &provider->context->runtime, binding_count, batch.binding_table,
        product->host_allocator, &batch.staging);
  }
  if (iree_status_is_ok(status)) {
    status = loom_run_hal_dispatch_sequence_execute(
        &provider->context->runtime, &batch.sequence,
        (iree_hal_buffer_binding_table_t){
            .count = binding_count,
            .bindings = batch.binding_table,
        });
  }
  if (iree_status_is_ok(status)) {
    status = loom_run_hal_testbench_staging_readback(
        &provider->context->runtime, &batch.staging);
  }
  loom_run_hal_testbench_scenario_batch_deinitialize(&batch);
  return status;
}

static iree_status_t loom_run_hal_testbench_scenario_product_prepare(
    void* user_data, const loom_testbench_invocation_plan_t* invocation,
    const loom_testbench_value_table_t* configuration,
    iree_allocator_t host_allocator,
    loom_testbench_prepared_product_t* out_product) {
  (void)configuration;
  *out_product = (loom_testbench_prepared_product_t){0};
  if (invocation->kind != LOOM_TESTBENCH_INVOCATION_KERNEL_LAUNCH) {
    return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                            "HAL scenario profile requires a kernel subject");
  }
  if (invocation->result_count != 0) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "HAL scenario kernel subject cannot return invocation results");
  }

  loom_run_hal_testbench_scenario_profile_t* profile =
      (loom_run_hal_testbench_scenario_profile_t*)user_data;
  if (profile->provider_options.run_module == NULL ||
      profile->provider_options.run_module->module != invocation->module) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "HAL scenario profile is not bound to the invocation module");
  }

  loom_run_hal_testbench_scenario_product_t* product = NULL;
  IREE_RETURN_IF_ERROR(iree_allocator_malloc(host_allocator, sizeof(*product),
                                             (void**)&product));
  *product = (loom_run_hal_testbench_scenario_product_t){
      .host_allocator = host_allocator,
  };
  loom_run_hal_testbench_actual_provider_options_t provider_options =
      profile->provider_options;
  provider_options.kernel_launch = invocation;
  loom_run_hal_testbench_actual_provider_initialize(&provider_options,
                                                    &product->provider);
  iree_status_t status =
      loom_run_hal_testbench_actual_provider_compile(&product->provider);
  if (iree_status_is_ok(status) && product->provider.compile_rejected) {
    status = iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "HAL scenario product compilation rejected at '%.*s' with '%.*s'",
        (int)product->provider.compile_failure_stage.size,
        product->provider.compile_failure_stage.data,
        (int)product->provider.compile_failure_kind.size,
        product->provider.compile_failure_kind.data);
  }
  if (iree_status_is_ok(status)) {
    *out_product = (loom_testbench_prepared_product_t){
        .execute = loom_run_hal_testbench_scenario_product_execute,
        .destroy = loom_run_hal_testbench_scenario_product_destroy,
        .user_data = product,
    };
  } else {
    loom_run_hal_testbench_scenario_product_destroy(product);
  }
  return status;
}

void loom_run_hal_testbench_scenario_profile_initialize(
    iree_string_view_t name,
    const loom_run_hal_testbench_actual_provider_options_t* provider_options,
    loom_run_hal_testbench_scenario_profile_t* out_profile) {
  *out_profile = (loom_run_hal_testbench_scenario_profile_t){
      .name = name,
      .provider_options = *provider_options,
  };
  out_profile->provider_options.kernel_launch = NULL;
}

loom_testbench_execution_profile_t
loom_run_hal_testbench_scenario_execution_profile(
    loom_run_hal_testbench_scenario_profile_t* profile) {
  return (loom_testbench_execution_profile_t){
      .name = profile->name,
      .prepare = loom_run_hal_testbench_scenario_product_prepare,
      .user_data = profile,
  };
}
