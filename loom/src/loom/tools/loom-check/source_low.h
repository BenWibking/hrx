// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Source-to-low compilation and textual artifacts for loom-check providers.

#ifndef LOOM_TOOLS_LOOM_CHECK_SOURCE_LOW_H_
#define LOOM_TOOLS_LOOM_CHECK_SOURCE_LOW_H_

#include "loom/error/source.h"
#include "loom/sanitizer/options.h"
#include "loom/target/selection.h"
#include "loom/target/specialization.h"
#include "loom/tools/loom-check/artifact.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum loom_check_emit_source_low_output_e {
  LOOM_CHECK_EMIT_SOURCE_LOW_OUTPUT_MODULE = 0,
  LOOM_CHECK_EMIT_SOURCE_LOW_OUTPUT_LOW = 1,
  LOOM_CHECK_EMIT_SOURCE_LOW_OUTPUT_PIPELINE = 2,
  LOOM_CHECK_EMIT_SOURCE_LOW_OUTPUT_PREPARED_PIPELINE = 3,
  LOOM_CHECK_EMIT_SOURCE_LOW_OUTPUT_NONE = 4,
} loom_check_emit_source_low_output_t;

typedef enum loom_check_source_low_option_bits_e {
  LOOM_CHECK_SOURCE_LOW_OPTION_OUTPUT = 1u << 0,
  LOOM_CHECK_SOURCE_LOW_OPTION_DIAGNOSTICS = 1u << 1,
  LOOM_CHECK_SOURCE_LOW_OPTION_CONTROL_FLOW = 1u << 2,
  LOOM_CHECK_SOURCE_LOW_OPTION_SANITIZER = 1u << 3,
  LOOM_CHECK_SOURCE_LOW_OPTION_SANITIZER_REPORTING = 1u << 4,
  LOOM_CHECK_SOURCE_LOW_OPTION_TARGET = 1u << 5,
} loom_check_source_low_option_bits_t;
typedef uint32_t loom_check_source_low_options_t;

// Parsed source-low request. Strings borrow the RUN line for the case lifetime.
typedef struct loom_check_source_low_request_t {
  // Textual product to compare, or NONE to check diagnostics only.
  loom_check_emit_source_low_output_t output;
  // Explicitly supplied options, used to reject duplicates and incompatible
  // modes.
  loom_check_source_low_options_t options;
  // Requested source-to-low legality diagnostics.
  loom_target_low_legality_diagnostic_flags_t diagnostic_flags;
  // Control-flow shape for the selected lowering pipeline.
  loom_target_control_flow_lowering_t control_flow_lowering;
  // Requested sanitizer instrumentation and reporting policy.
  loom_sanitizer_options_t sanitizer;
  // Optional source function to specialize. With TARGET but no name, select
  // the sole definition or the unique public entry among private helpers.
  iree_string_view_t function_name;
  // Parsed target profile specification for the selected function.
  loom_target_specification_t target;
} loom_check_source_low_request_t;

// Parses optional @function and source-low options. An explicit function
// requires target=family:selector; pipeline-text modes accept neither.
iree_status_t loom_check_source_low_parse(
    iree_string_view_t text, loom_check_source_low_request_t* request);

// Resolves an explicit compile target for a source entry. An empty function
// name selects the sole definition or the unique public entry among private
// helpers. The returned name borrows from the request or module; the selected
// profile belongs to the environment. Authored IR is unchanged.
iree_status_t loom_check_resolve_source_target(
    const loom_module_t* module, const loom_target_environment_t* environment,
    iree_string_view_t function_name,
    const loom_target_specification_t* specification,
    loom_target_specialization_request_t* out_request);

// Source-to-target-low preparation options for emit providers.
typedef struct loom_check_prepare_source_low_options_t {
  // Source-to-low legality diagnostics emitted while selecting target-low.
  loom_target_low_legality_diagnostic_flags_t source_low_diagnostic_flags;
  // Control-flow lowering shape used when building the default pipeline.
  loom_target_control_flow_lowering_t control_flow_lowering;
  // Sanitizer instrumentation checks enabled while preparing source-low IR.
  loom_sanitizer_options_t sanitizer;
  // Borrowed per-function specialization requests consumed by the pipeline.
  loom_target_specialization_request_list_t target_specializations;
  // Optional caller-owned structured compile report populated by the pipeline.
  loom_target_compile_report_t* report;
} loom_check_prepare_source_low_options_t;

// Verifies |module| as source IR, lowers it through the source-to-low pipeline,
// and verifies the resulting target-low module.
//
// Infrastructure failures return a non-OK status. User IR failures are emitted
// into |diagnostic_collector| and return OK so loom-check can match structured
// diagnostics in the usual way.
iree_status_t loom_check_prepare_source_low_module(
    loom_module_t* module,
    const loom_check_prepare_source_low_options_t* options,
    const loom_check_environment_t* environment,
    loom_source_resolver_t source_resolver,
    loom_check_diagnostic_collector_t* diagnostic_collector,
    iree_arena_block_pool_t* block_pool);

// Prepares source IR and invokes the target emitter named by
// |public_artifact_format| while retained compiler function versions remain
// live. The caller releases |out_artifact| when |out_emitted| is true.
iree_status_t loom_check_emit_source_low_artifact(
    const loom_check_emit_provider_request_t* request,
    const loom_check_prepare_source_low_options_t* options,
    iree_string_view_t public_artifact_format, bool* out_emitted,
    loom_target_emit_artifact_t* out_artifact);

// Runs source lowering and renders the selected artifact. User IR diagnostics
// accumulate in the collector; infrastructure and malformed-request failures
// return a status. The module is owned by the caller and may be transformed.
iree_status_t loom_check_source_low_emit(
    loom_module_t* module, const loom_check_source_low_request_t* request,
    const loom_check_environment_t* environment,
    loom_source_resolver_t source_resolver,
    loom_check_diagnostic_collector_t* diagnostic_collector,
    iree_arena_block_pool_t* block_pool, loom_check_result_t* result);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLS_LOOM_CHECK_SOURCE_LOW_H_
