// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// iree-test-loom binary with build-selected execution providers.

#include <stddef.h>
#include <stdio.h>

#include "loom/tooling/input/configured.h"
#include "loom/tools/iree-test-loom/main.h"
#include "loom/transforms/cleanup/configured.h"
#include "loomc/interop.h"
#include "loomc/iree.h"
#include "loomc/target/configured.h"

#ifndef IREE_TEST_LOOM_HAVE_AMDGPU
#define IREE_TEST_LOOM_HAVE_AMDGPU 0
#endif  // IREE_TEST_LOOM_HAVE_AMDGPU
#ifndef IREE_TEST_LOOM_HAVE_SPIRV
#define IREE_TEST_LOOM_HAVE_SPIRV 0
#endif  // IREE_TEST_LOOM_HAVE_SPIRV
#ifndef IREE_TEST_LOOM_HAVE_IMPORT_CXX
#define IREE_TEST_LOOM_HAVE_IMPORT_CXX 0
#endif  // IREE_TEST_LOOM_HAVE_IMPORT_CXX
#ifndef IREE_TEST_LOOM_HAVE_VM
#define IREE_TEST_LOOM_HAVE_VM 0
#endif  // IREE_TEST_LOOM_HAVE_VM
#ifndef IREE_TEST_LOOM_HAVE_WASM
#define IREE_TEST_LOOM_HAVE_WASM 0
#endif  // IREE_TEST_LOOM_HAVE_WASM

#ifndef IREE_TEST_LOOM_HAVE_TASK
#define IREE_TEST_LOOM_HAVE_TASK 0
#endif  // IREE_TEST_LOOM_HAVE_TASK

#define IREE_TEST_LOOM_HAVE_ANY_DEVICE_PROVIDER               \
  (IREE_TEST_LOOM_HAVE_AMDGPU || IREE_TEST_LOOM_HAVE_SPIRV || \
   IREE_TEST_LOOM_HAVE_TASK)

#if IREE_TEST_LOOM_HAVE_AMDGPU
#include "loom/tooling/target/amdgpu/device_provider.h"
#include "loom/tooling/target/amdgpu/testbench_requirements.h"
#endif  // IREE_TEST_LOOM_HAVE_AMDGPU
#if IREE_TEST_LOOM_HAVE_SPIRV
#include "loom/tooling/target/spirv/device_provider.h"
#include "loom/tooling/target/spirv/testbench_requirements.h"
#endif  // IREE_TEST_LOOM_HAVE_SPIRV
#if IREE_TEST_LOOM_HAVE_IMPORT_CXX
#include "loom/import/cxx/tooling/loomc_input.h"
#endif  // IREE_TEST_LOOM_HAVE_IMPORT_CXX
#if IREE_TEST_LOOM_HAVE_TASK
#include "loom/tooling/target/cpu/task_device.h"
#endif  // IREE_TEST_LOOM_HAVE_TASK
#if IREE_TEST_LOOM_HAVE_VM
#include "loom/tooling/target/vm/testbench.h"
#endif  // IREE_TEST_LOOM_HAVE_VM
#if IREE_TEST_LOOM_HAVE_WASM && defined(IREE_PLATFORM_WASM)
#include "loom/tooling/target/wasm/testbench.h"
#endif  // IREE_TEST_LOOM_HAVE_WASM && IREE_PLATFORM_WASM

#if IREE_TEST_LOOM_HAVE_AMDGPU || IREE_TEST_LOOM_HAVE_SPIRV
static iree_status_t iree_test_loom_append_requirement_provider(
    iree_host_size_t provider_capacity,
    loom_testbench_requirement_provider_t* providers,
    iree_host_size_t* inout_provider_count,
    loom_testbench_requirement_provider_t provider) {
  if (*inout_provider_count >= provider_capacity) {
    return iree_make_status(
        IREE_STATUS_RESOURCE_EXHAUSTED,
        "iree-test-loom requirement provider capacity exceeded");
  }
  providers[(*inout_provider_count)++] = provider;
  return iree_ok_status();
}
#endif  // IREE_TEST_LOOM_HAVE_AMDGPU || IREE_TEST_LOOM_HAVE_SPIRV

#if IREE_TEST_LOOM_HAVE_IMPORT_CXX
static iree_status_t iree_test_loom_import_source(
    void* user_data, iree_string_view_t format,
    iree_string_view_t input_options,
    const loom_tooling_source_path_options_t* source_path_options,
    loomc_context_t* context, loomc_workspace_t* workspace,
    const loomc_source_t* source, iree_arena_block_pool_t* block_pool,
    iree_allocator_t host_allocator, loomc_module_t** out_module,
    loomc_result_t** out_result) {
  (void)user_data;
  if (iree_string_view_equal(format, IREE_SV("cxx"))) {
    return loom_cxx_input_import_loomc(
        context, workspace, source, input_options, source_path_options,
        block_pool, host_allocator, out_module, out_result);
  }
  return iree_make_status(
      IREE_STATUS_UNIMPLEMENTED,
      "input format '%.*s' has no LoomC importer linked into this runner",
      (int)format.size, format.data);
}
#endif  // IREE_TEST_LOOM_HAVE_IMPORT_CXX

static iree_status_t iree_test_loom_populate_requirement_providers(
    void* user_data, loom_run_hal_testbench_context_t* hal_context,
    iree_host_size_t provider_capacity,
    loom_testbench_requirement_provider_t* providers,
    iree_host_size_t* inout_provider_count) {
  (void)user_data;
#if IREE_TEST_LOOM_HAVE_AMDGPU
  loom_testbench_requirement_provider_t amdgpu_provider = {0};
  loom_amdgpu_hal_testbench_requirement_provider_initialize(hal_context,
                                                            &amdgpu_provider);
  IREE_RETURN_IF_ERROR(iree_test_loom_append_requirement_provider(
      provider_capacity, providers, inout_provider_count, amdgpu_provider));
#endif  // IREE_TEST_LOOM_HAVE_AMDGPU
#if IREE_TEST_LOOM_HAVE_SPIRV
  loom_testbench_requirement_provider_t vulkan_feature_provider = {0};
  loom_spirv_vulkan_feature_testbench_requirement_provider_initialize(
      hal_context, &vulkan_feature_provider);
  IREE_RETURN_IF_ERROR(iree_test_loom_append_requirement_provider(
      provider_capacity, providers, inout_provider_count,
      vulkan_feature_provider));
  loom_testbench_requirement_provider_t cooperative_matrix_provider = {0};
  loom_spirv_vulkan_cooperative_matrix_testbench_requirement_provider_initialize(
      hal_context, &cooperative_matrix_provider);
  IREE_RETURN_IF_ERROR(iree_test_loom_append_requirement_provider(
      provider_capacity, providers, inout_provider_count,
      cooperative_matrix_provider));
#endif  // IREE_TEST_LOOM_HAVE_SPIRV
#if !IREE_TEST_LOOM_HAVE_AMDGPU && !IREE_TEST_LOOM_HAVE_SPIRV
  (void)hal_context;
  (void)provider_capacity;
  (void)providers;
  (void)inout_provider_count;
#endif  // !IREE_TEST_LOOM_HAVE_AMDGPU && !IREE_TEST_LOOM_HAVE_SPIRV
  return iree_ok_status();
}

int main(int argc, char** argv) {
  loomc_target_environment_t* target_environment = NULL;
  iree_status_t status =
      iree_status_from_loomc(loomc_target_environment_create_configured(
          loomc_allocator_system(), &target_environment));
  if (!iree_status_is_ok(status)) {
    iree_status_fprint(stderr, status);
    iree_status_free(status);
    return 1;
  }
  const loom_target_environment_t* native_target_environment =
      loomc_target_environment_get_interop_view(target_environment);

#if IREE_TEST_LOOM_HAVE_TASK
  loom_task_device_provider_t task_provider;
  loom_task_device_provider_initialize(native_target_environment,
                                       &task_provider);
#endif  // IREE_TEST_LOOM_HAVE_TASK
#if IREE_TEST_LOOM_HAVE_ANY_DEVICE_PROVIDER
  const loom_device_provider_t* device_providers[] = {
#if IREE_TEST_LOOM_HAVE_AMDGPU
      &loom_amdgpu_device_provider,
#endif  // IREE_TEST_LOOM_HAVE_AMDGPU
#if IREE_TEST_LOOM_HAVE_SPIRV
      &loom_spirv_vulkan_device_provider,
#endif  // IREE_TEST_LOOM_HAVE_SPIRV
#if IREE_TEST_LOOM_HAVE_TASK
      &task_provider.base,
#endif  // IREE_TEST_LOOM_HAVE_TASK
  };
#endif  // IREE_TEST_LOOM_HAVE_ANY_DEVICE_PROVIDER
  const loom_device_provider_registry_t device_registry = {
#if IREE_TEST_LOOM_HAVE_ANY_DEVICE_PROVIDER
      .providers = device_providers,
      .provider_count = IREE_ARRAYSIZE(device_providers),
#else
      .providers = NULL,
      .provider_count = 0,
#endif  // IREE_TEST_LOOM_HAVE_ANY_DEVICE_PROVIDER
  };
  iree_test_loom_configuration_t configuration = {
      .input_providers = loom_configured_input_providers(),
      .tool_name = "iree-test-loom",
      .target_environment = target_environment,
#if IREE_TEST_LOOM_HAVE_IMPORT_CXX
      .import = iree_test_loom_import_source,
#endif  // IREE_TEST_LOOM_HAVE_IMPORT_CXX
      .cleanup_pattern_provider_set =
          loom_cleanup_configured_pattern_provider_set(),
      .device_provider_registry = &device_registry,
      .populate_requirement_providers =
          {
              .fn = iree_test_loom_populate_requirement_providers,
          },
  };
#if IREE_TEST_LOOM_HAVE_VM
  loom_vm_testbench_t vm_testbench;
  status = loom_vm_testbench_initialize(target_environment,
                                        iree_allocator_system(), &vm_testbench);
  if (!iree_status_is_ok(status)) {
    iree_status_fprint(stderr, status);
    iree_status_free(status);
    loom_vm_testbench_deinitialize(&vm_testbench);
    loomc_target_environment_release(target_environment);
    return 1;
  }
  configuration.function_call_provider.fn =
      loom_vm_testbench_invocation_provider;
  configuration.function_call_provider.user_data = &vm_testbench;
  configuration.scenario_target_profile.fn =
      loom_vm_testbench_execution_profile;
  configuration.scenario_target_profile.user_data = &vm_testbench;
  configuration.scenario_oracle_profile.fn =
      loom_vm_testbench_execution_profile;
  configuration.scenario_oracle_profile.user_data = &vm_testbench;
#endif  // IREE_TEST_LOOM_HAVE_VM
#if IREE_TEST_LOOM_HAVE_WASM && defined(IREE_PLATFORM_WASM)
  loom_wasm_testbench_t wasm_testbench;
  loom_wasm_testbench_initialize(native_target_environment,
                                 configuration.cleanup_pattern_provider_set,
                                 iree_allocator_system(), &wasm_testbench);
  configuration.scenario_target_profile.fn =
      loom_wasm_testbench_execution_profile;
  configuration.scenario_target_profile.user_data = &wasm_testbench;
#endif  // IREE_TEST_LOOM_HAVE_WASM && IREE_PLATFORM_WASM
  int exit_code = iree_test_loom_main(argc, argv, &configuration);
#if IREE_TEST_LOOM_HAVE_VM
  loom_vm_testbench_deinitialize(&vm_testbench);
#endif  // IREE_TEST_LOOM_HAVE_VM
  loomc_target_environment_release(target_environment);
  return exit_code;
}
