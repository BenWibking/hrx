// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Offline compiler qualification, independent of execution resources.

#ifndef LOOM_TOOLS_LOOM_CHECK_COMPILE_H_
#define LOOM_TOOLS_LOOM_CHECK_COMPILE_H_

#include "loom/tools/loom-check/execute.h"
#include "loomc/compile.h"
#include "loomc/sanitizer.h"
#include "loomc/target.h"

#ifdef __cplusplus
extern "C" {
#endif

// Creates the compiler target environment linked into the final runner.
typedef loomc_status_t (*loom_check_create_target_environment_fn_t)(
    loomc_allocator_t allocator,
    loomc_target_environment_t** out_target_environment);

// Imports one optional foreign source format through LoomC. Builtin Loom text
// and bytecode admission do not use this callback.
typedef iree_status_t (*loom_check_compile_import_fn_t)(
    void* user_data, iree_string_view_t format,
    iree_string_view_t input_options, loomc_context_t* context,
    loomc_workspace_t* workspace, const loomc_source_t* source,
    iree_arena_block_pool_t* block_pool, iree_allocator_t host_allocator,
    loomc_module_t** out_module, loomc_result_t** out_result);

// Compiler integrations supplied by the final loom-check binary. The generic
// runner does not choose a configured target set or optional source frontend.
typedef struct loom_check_compile_provider_t {
  // Factory for the runner's configured artifact-capable target environment.
  loom_check_create_target_environment_fn_t create_target_environment;
  // Optional foreign-source importer dispatch.
  loom_check_compile_import_fn_t import;
  // Opaque state forwarded to |import|.
  void* import_user_data;
} loom_check_compile_provider_t;

typedef struct loom_check_compile_options_t {
  // Public context shared by admitted modules and the prepared compiler.
  loomc_context_t* context;
  // Prepared public compiler, or NULL when qualification is disabled.
  loomc_compiler_t* compiler;
  // Reusable invocation workspace owned by the runner.
  loomc_workspace_t* workspace;
  // Selected compiler profile.
  loomc_target_profile_t* target_profile;
  // Compile-time bindings applied while preparing the artifact request.
  const loomc_config_options_t* config;
  // Instrumentation attached to the default target pass program, or NULL.
  const loomc_sanitizer_options_t* sanitizer;
  // Optional foreign-source importer selected by the final runner.
  loom_check_compile_import_fn_t import;
  // Opaque state forwarded to |import|.
  void* import_user_data;
} loom_check_compile_options_t;

// Admits and compiles one source case through LoomC's final artifact producer.
// Success requires a nonempty artifact or exactly
// matched diagnostic annotations. RUN goldens, XFAIL, and external execution
// requirements do not describe this independent compile outcome. No device is
// opened and no artifact is loaded or executed.
iree_status_t loom_check_execute_compile(
    const loom_test_case_t* test_case, iree_host_size_t case_index,
    loom_check_file_report_t* report, iree_string_view_t filename,
    const loom_input_request_t* input_request,
    const loom_check_compile_options_t* options,
    const loom_check_environment_t* environment,
    iree_arena_block_pool_t* block_pool, iree_allocator_t allocator,
    loom_check_result_t* result);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLS_LOOM_CHECK_COMPILE_H_
