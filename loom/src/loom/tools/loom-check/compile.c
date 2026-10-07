// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tools/loom-check/compile.h"

#include "loom/testing/test_file.h"
#include "loom/tools/loom-check/compile_diagnostics.h"
#include "loom/tools/loom-check/diagnostics.h"
#include "loomc/iree.h"

static iree_status_t loom_check_compile_admit_module(
    const loom_test_case_t* test_case, iree_string_view_t filename,
    const loom_input_request_t* input_request,
    const loom_check_compile_options_t* options,
    const loom_check_environment_t* environment,
    iree_arena_block_pool_t* block_pool, iree_allocator_t host_allocator,
    loomc_module_t** out_module, loomc_result_t** out_result) {
  *out_module = NULL;
  *out_result = NULL;

  iree_string_view_t format = input_request->format;
  iree_string_view_t input_options = iree_string_view_empty();
  if (!iree_string_view_is_empty(test_case->input_options.format)) {
    format = test_case->input_options.format;
    input_options = test_case->input_options.arguments;
  }
  const loom_input_provider_t* provider = NULL;
  IREE_RETURN_IF_ERROR(loom_input_provider_select(
      environment->input_providers, format, input_request->path, &provider));

  loomc_source_format_t source_format = LOOMC_SOURCE_FORMAT_UNKNOWN;
  iree_string_view_t source_identifier = input_request->path;
  iree_string_view_t source_contents = test_case->input;
  iree_string_builder_t stripped_source;
  iree_string_builder_initialize(host_allocator, &stripped_source);
  iree_status_t status = iree_ok_status();
  if (provider == &loom_input_text_provider) {
    if (!iree_string_view_is_empty(input_options)) {
      status =
          iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                           "Loom text input does not accept input options");
    }
    if (iree_status_is_ok(status)) {
      status = loom_test_file_strip_comments(source_contents, &stripped_source);
    }
    source_format = LOOMC_SOURCE_FORMAT_TEXT;
    source_identifier = filename;
    source_contents = iree_string_builder_view(&stripped_source);
  } else if (provider == &loom_input_bytecode_provider) {
    if (!iree_string_view_is_empty(input_options)) {
      status =
          iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                           "Loom bytecode input does not accept input options");
    }
    source_format = LOOMC_SOURCE_FORMAT_BYTECODE;
    source_identifier = filename;
  }

  loomc_source_t* source = NULL;
  if (iree_status_is_ok(status)) {
    const loomc_source_options_t source_options = {
        .type = LOOMC_STRUCTURE_TYPE_SOURCE_OPTIONS,
        .structure_size = sizeof(source_options),
        .format = source_format,
        .identifier = loomc_string_view_from_iree(source_identifier),
        .contents = loomc_byte_span_from_iree(iree_make_const_byte_span(
            source_contents.data, source_contents.size)),
        .storage = LOOMC_SOURCE_STORAGE_BORROWED,
    };
    status = iree_status_from_loomc(loomc_source_create(
        &source_options, loomc_allocator_from_iree(host_allocator), &source));
  }
  if (iree_status_is_ok(status)) {
    if (provider == &loom_input_text_provider ||
        provider == &loom_input_bytecode_provider) {
      status = iree_status_from_loomc(loomc_module_deserialize_from_source(
          options->context, options->workspace, source, /*options=*/NULL,
          loomc_allocator_from_iree(host_allocator), out_module, out_result));
    } else if (options->import != NULL) {
      status = options->import(options->import_user_data, provider->name,
                               input_options, options->context,
                               options->workspace, source, block_pool,
                               host_allocator, out_module, out_result);
    } else {
      status = iree_make_status(
          IREE_STATUS_UNIMPLEMENTED,
          "input format '%.*s' has no LoomC importer linked into this runner",
          (int)provider->name.size, provider->name.data);
    }
  }
  loomc_source_release(source);
  iree_string_builder_deinitialize(&stripped_source);
  return status;
}

static iree_status_t loom_check_compile_append_result(
    loom_check_diagnostic_collector_t* collector,
    const loom_input_request_t* input_request, const loomc_result_t* result,
    bool* out_succeeded) {
  *out_succeeded = loomc_result_succeeded(result);
  return loom_check_compile_append_result_diagnostics(
      collector, result, &input_request->source_path_options);
}

static bool loom_check_compile_result_has_artifact(
    const loomc_result_t* result) {
  const loomc_host_size_t artifact_count = loomc_result_artifact_count(result);
  for (loomc_host_size_t i = 0; i < artifact_count; ++i) {
    const loomc_artifact_t* artifact = loomc_result_artifact_at(result, i);
    if (artifact != NULL &&
        loomc_byte_sequence_length(artifact->contents) != 0) {
      return true;
    }
  }
  return false;
}

iree_status_t loom_check_execute_compile(
    const loom_test_case_t* test_case, iree_host_size_t case_index,
    loom_check_file_report_t* report, iree_string_view_t filename,
    const loom_input_request_t* input_request,
    const loom_check_compile_options_t* options,
    const loom_check_environment_t* environment,
    iree_arena_block_pool_t* block_pool, iree_allocator_t allocator,
    loom_check_result_t* result) {
  iree_arena_allocator_t arena;
  iree_arena_initialize(block_pool, &arena);
  loom_check_diagnostic_collector_t collector = {
      .arena = &arena,
      .host_allocator = allocator,
      .filename = filename,
      .result = result,
  };

  loomc_module_t* module = NULL;
  loomc_result_t* operation_result = NULL;
  iree_status_t status = loom_check_compile_admit_module(
      test_case, filename, input_request, options, environment, block_pool,
      allocator, &module, &operation_result);
  bool admitted = false;
  if (iree_status_is_ok(status)) {
    status = loom_check_compile_append_result(&collector, input_request,
                                              operation_result, &admitted);
  }
  loomc_result_release(operation_result);
  operation_result = NULL;

  if (iree_status_is_ok(status) && admitted) {
    const loomc_compile_artifact_options_t compile_options = {
        .type = LOOMC_STRUCTURE_TYPE_COMPILE_ARTIFACT_OPTIONS,
        .structure_size = sizeof(compile_options),
        .next = options->sanitizer,
        .target_profile = options->target_profile,
        .config = options->config,
    };
    status = iree_status_from_loomc(loomc_compile_artifact(
        options->compiler, options->workspace, /*pass_program=*/NULL, module,
        &compile_options, loomc_allocator_from_iree(allocator),
        &operation_result));
  }
  bool compiled = false;
  if (iree_status_is_ok(status) && admitted) {
    status = loom_check_compile_append_result(&collector, input_request,
                                              operation_result, &compiled);
  }
  if (iree_status_is_ok(status) && compiled &&
      !loom_check_compile_result_has_artifact(operation_result)) {
    status = iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "compiler produced neither a nonempty artifact nor an error");
  }
  loomc_result_release(operation_result);
  loomc_module_release(module);
  loomc_workspace_trim(options->workspace);

  if (iree_status_is_ok(status)) {
    status = loom_check_diagnostic_collector_finish(
        &collector, test_case, case_index, report, allocator, result);
    result->final_outcome = result->raw_outcome;
  }
  iree_arena_deinitialize(&arena);
  return status;
}
