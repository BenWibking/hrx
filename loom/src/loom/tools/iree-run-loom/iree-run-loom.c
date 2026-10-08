// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// iree-run-loom binary with build-selected compiler integrations.

#include <stddef.h>

#include "loom/tooling/input/configured.h"
#include "loom/tools/iree-run-loom/main.h"
#include "loomc/target/configured.h"

#ifndef IREE_RUN_LOOM_HAVE_AMDGPU
#define IREE_RUN_LOOM_HAVE_AMDGPU 0
#endif  // IREE_RUN_LOOM_HAVE_AMDGPU
#ifndef IREE_RUN_LOOM_HAVE_IMPORT_CXX
#define IREE_RUN_LOOM_HAVE_IMPORT_CXX 0
#endif  // IREE_RUN_LOOM_HAVE_IMPORT_CXX
#ifndef IREE_RUN_LOOM_HAVE_SPIRV
#define IREE_RUN_LOOM_HAVE_SPIRV 0
#endif  // IREE_RUN_LOOM_HAVE_SPIRV

#if IREE_RUN_LOOM_HAVE_AMDGPU
#include "loomc/target/amdgpu/iree_hal.h"
#endif  // IREE_RUN_LOOM_HAVE_AMDGPU
#if IREE_RUN_LOOM_HAVE_IMPORT_CXX
#include "loom/import/cxx/tooling/loomc_input.h"
#endif  // IREE_RUN_LOOM_HAVE_IMPORT_CXX
#if IREE_RUN_LOOM_HAVE_SPIRV
#include "loomc/target/spirv/iree_hal.h"
#endif  // IREE_RUN_LOOM_HAVE_SPIRV

#if IREE_RUN_LOOM_HAVE_AMDGPU || IREE_RUN_LOOM_HAVE_SPIRV
static const loomc_iree_hal_target_provider_t* kIreeRunLoomTargetProviders[2];
#endif  // IREE_RUN_LOOM_HAVE_AMDGPU || IREE_RUN_LOOM_HAVE_SPIRV

#if IREE_RUN_LOOM_HAVE_IMPORT_CXX
static iree_status_t iree_run_loom_import_source(
    void* user_data, iree_string_view_t format,
    iree_string_view_t input_options, loomc_context_t* context,
    loomc_workspace_t* workspace, const loomc_source_t* source,
    iree_arena_block_pool_t* block_pool, iree_allocator_t host_allocator,
    loomc_module_t** out_module, loomc_result_t** out_result) {
  (void)user_data;
  if (iree_string_view_equal(format, IREE_SV("cxx"))) {
    return loom_cxx_input_import_loomc(context, workspace, source,
                                       input_options, block_pool,
                                       host_allocator, out_module, out_result);
  }
  return iree_make_status(
      IREE_STATUS_UNIMPLEMENTED,
      "input format '%.*s' has no LoomC importer linked into this runner",
      (int)format.size, format.data);
}
#endif  // IREE_RUN_LOOM_HAVE_IMPORT_CXX

int main(int argc, char** argv) {
  iree_host_size_t target_provider_count = 0;
#if IREE_RUN_LOOM_HAVE_AMDGPU
  kIreeRunLoomTargetProviders[target_provider_count++] =
      loomc_amdgpu_iree_hal_target_provider();
#endif  // IREE_RUN_LOOM_HAVE_AMDGPU
#if IREE_RUN_LOOM_HAVE_SPIRV
  kIreeRunLoomTargetProviders[target_provider_count++] =
      loomc_spirv_iree_hal_target_provider();
#endif  // IREE_RUN_LOOM_HAVE_SPIRV

  const iree_run_loom_configuration_t configuration = {
      .input_providers = loom_configured_input_providers(),
      .tool_name = "iree-run-loom",
      .create_target_environment = loomc_target_environment_create_configured,
#if IREE_RUN_LOOM_HAVE_IMPORT_CXX
      .import = iree_run_loom_import_source,
#endif  // IREE_RUN_LOOM_HAVE_IMPORT_CXX
#if IREE_RUN_LOOM_HAVE_AMDGPU || IREE_RUN_LOOM_HAVE_SPIRV
      .hal_target_providers = kIreeRunLoomTargetProviders,
#endif  // IREE_RUN_LOOM_HAVE_AMDGPU || IREE_RUN_LOOM_HAVE_SPIRV
      .hal_target_provider_count = target_provider_count,
  };
  return iree_run_loom_main(argc, argv, &configuration);
}
