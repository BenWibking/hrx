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
    iree_string_view_t input_options,
    const loom_tooling_source_path_options_t* source_path_options,
    loomc_context_t* context, loomc_workspace_t* workspace,
    const loomc_source_t* source, iree_arena_block_pool_t* block_pool,
    iree_allocator_t host_allocator, loomc_module_t** out_module,
    loomc_result_t** out_result);

// Compiler integrations supplied by the final loom-check binary. The generic
// runner does not choose a configured target set or optional source frontend.
typedef struct loom_check_compile_provider_t {
  // Factory for the runner's artifact-capable target environment.
  loom_check_create_target_environment_fn_t create_target_environment;
  // Optional foreign-source importer dispatch.
  loom_check_compile_import_fn_t import;
  // Opaque state forwarded to |import|.
  void* import_user_data;
} loom_check_compile_provider_t;

// Lazily prepared public compiler state shared by checker cases. Initializing a
// session does not construct a target environment, context, workspace, or
// compiler; the first public compile operation prepares all four together.
typedef struct loom_check_compile_session_t {
  // Binary-owned compiler integrations used when the session is prepared.
  const loom_check_compile_provider_t* provider;
  // Host allocator used for every session-owned public handle.
  iree_allocator_t host_allocator;
  // Public target environment, or NULL before the first compile operation.
  loomc_target_environment_t* target_environment;
  // Public context, or NULL before the first compile operation.
  loomc_context_t* context;
  // Reusable invocation workspace, or NULL before the first compile operation.
  loomc_workspace_t* workspace;
  // Prepared compiler, or NULL before the first compile operation.
  loomc_compiler_t* compiler;
  // Cached empty, source-to-Low CFG, and source-to-Low structured programs.
  loomc_pass_program_t* artifact_pass_programs[3];
} loom_check_compile_session_t;

typedef struct loom_check_compile_options_t {
  // Lazily prepared public compiler state owned by the runner.
  loom_check_compile_session_t* session;
  // Selected compiler profile.
  loomc_target_profile_t* target_profile;
  // Compile-time bindings applied while preparing the artifact request.
  const loomc_config_options_t* config;
  // Instrumentation attached to the default target pass program, or NULL.
  const loomc_sanitizer_options_t* sanitizer;
} loom_check_compile_options_t;

// Public compile request used by source-consuming emit providers.
typedef struct loom_check_compile_artifact_options_t {
  // Public artifact format selecting the target emitter.
  iree_string_view_t artifact_format;
  // Optional explicit root function symbol.
  iree_string_view_t root;
  // Optional complete family:selector target profile specification.
  iree_string_view_t target;
  // Whether to lower source IR to Low before target emission.
  bool lower_source_to_low;
  // Source-to-Low control-flow shape when lowering source IR.
  loomc_target_control_flow_lowering_t control_flow_lowering;
} loom_check_compile_artifact_options_t;

// Releases all public compiler state prepared by |session|.
void loom_check_compile_session_deinitialize(
    loom_check_compile_session_t* session);

// Selects a complete target profile from the prepared session environment.
iree_status_t loom_check_compile_session_select_target_profile(
    loom_check_compile_session_t* session, iree_string_view_t specification,
    loomc_target_profile_t** out_target_profile);

// Admits one check case and compiles it to one target artifact. Public
// diagnostics are appended to |request->diagnostic_collector|. A successful
// primary artifact is returned as an immutable source owned by the caller.
iree_status_t loom_check_compile_artifact(
    const loom_check_emit_provider_request_t* request,
    const loom_check_compile_artifact_options_t* options,
    loomc_source_t** out_artifact_source);

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
