// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/target/cpu/task_device.h"

#include "iree/hal/drivers/task/device_spec.h"
#include "loom/target/selection.h"
#include "loom/tooling/execution/hal/runtime.h"

static iree_status_t loom_task_device_select_target(
    const loom_device_provider_t* base_provider,
    const loom_run_hal_runtime_t* runtime,
    const loom_target_facts_t* requirement,
    const loom_target_profile_t* requested_profile,
    loom_device_target_t* out_target) {
  const loom_task_device_provider_t* provider =
      (const loom_task_device_provider_t*)base_provider;
  const iree_hal_device_spec_t* device_spec =
      iree_hal_device_spec(runtime->device);
  if (!device_spec) {
    return iree_make_status(IREE_STATUS_UNAVAILABLE,
                            "task device does not publish device facts");
  }
  const iree_hal_device_spec_facet_t* facet =
      iree_hal_cpu_device_spec_find_facet(device_spec);
  if (!facet) {
    return iree_make_status(IREE_STATUS_UNAVAILABLE,
                            "task device does not publish CPU capabilities");
  }
  iree_hal_cpu_device_spec_t cpu_spec;
  IREE_RETURN_IF_ERROR(iree_hal_cpu_device_spec_decode_facet(facet, &cpu_spec));
  const loom_target_profile_t* profile = NULL;
  IREE_RETURN_IF_ERROR(loom_target_environment_select_cpu_profile(
      provider->target_environment, &cpu_spec.cpu_data, requirement,
      requested_profile, &profile));
  const loom_target_emitter_t* emitter =
      loom_target_environment_lookup_canonical_kernel_emitter(
          provider->target_environment, profile->type->fact_type);
  if (!emitter) {
    return iree_make_status(IREE_STATUS_UNAVAILABLE,
                            "native CPU profile '%.*s' has no kernel emitter",
                            (int)profile->target_bundle->name.size,
                            profile->target_bundle->name.data);
  }

  const iree_hal_queue_family_ordinal_t family_ordinal =
      iree_hal_queue_family_ordinal(
          iree_hal_queue_family(runtime->dispatch_queue));
  const iree_hal_device_queue_spec_t* queues =
      iree_hal_device_spec_queues(device_spec);
  const iree_hal_executable_target_selection_t selection = {
      .family = IREE_SV("cpu"),
      .target_key = iree_cpu_architecture_name(cpu_spec.cpu_data.architecture),
      .kind_flags = IREE_HAL_EXECUTABLE_TARGET_KIND_FLAG_GENERIC,
      .physical_device_affinity =
          queues->families[family_ordinal].physical_device_affinity,
  };
  const iree_hal_executable_target_selection_result_t result =
      iree_hal_device_spec_select_executable_target(device_spec, &selection);
  if (result.outcome == IREE_HAL_EXECUTABLE_TARGET_SELECTION_OUTCOME_NO_MATCH) {
    return iree_make_status(IREE_STATUS_UNAVAILABLE,
                            "task dispatch queue has no compatible CPU loader");
  }
  if (result.outcome ==
      IREE_HAL_EXECUTABLE_TARGET_SELECTION_OUTCOME_AMBIGUOUS) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "task dispatch queue has ambiguous CPU targets");
  }
  *out_target = (loom_device_target_t){
      .executable_target = result.target,
      .target_profile = profile,
      .target_emitter = emitter,
  };
  return iree_ok_status();
}

static iree_status_t loom_task_device_select_compatible_target(
    const loom_device_provider_t* provider,
    const loom_run_hal_runtime_t* runtime,
    const loom_target_facts_t* requirement, iree_allocator_t allocator,
    loom_device_target_t* out_target) {
  (void)allocator;
  return loom_task_device_select_target(provider, runtime, requirement, NULL,
                                        out_target);
}

static iree_status_t loom_task_device_select_profile_target(
    const loom_device_provider_t* provider,
    const loom_run_hal_runtime_t* runtime, const loom_target_profile_t* profile,
    loom_device_target_t* out_target) {
  return loom_task_device_select_target(provider, runtime, NULL, profile,
                                        out_target);
}

void loom_task_device_provider_initialize(
    const loom_target_environment_t* target_environment,
    loom_task_device_provider_t* out_provider) {
  *out_provider = (loom_task_device_provider_t){
      .base =
          {
              .name = IREE_SVL("task-hal"),
              .driver_name = IREE_SVL("task"),
              .select_compatible_target =
                  loom_task_device_select_compatible_target,
              .select_profile_target = loom_task_device_select_profile_target,
          },
      .target_environment = target_environment,
  };
}
