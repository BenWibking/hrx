// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tools/loom-check/compile.h"

#include "loom/testing/test_file.h"
#include "loom/tooling/io/source_path.h"
#include "loom/tools/loom-check/compile_diagnostics.h"
#include "loom/tools/loom-check/diagnostics.h"
#include "loomc/artifact.h"
#include "loomc/interop.h"
#include "loomc/iree.h"

void loom_check_compile_session_deinitialize(
    loom_check_compile_session_t* session) {
  for (iree_host_size_t i = 0;
       i < IREE_ARRAYSIZE(session->artifact_pass_programs); ++i) {
    loomc_pass_program_release(session->artifact_pass_programs[i]);
  }
  loomc_compiler_release(session->compiler);
  loomc_workspace_release(session->workspace);
  loomc_context_release(session->context);
  loomc_target_environment_release(session->target_environment);
  *session = (loom_check_compile_session_t){0};
}

static iree_status_t loom_check_compile_session_prepare(
    loom_check_compile_session_t* session) {
  if (session->compiler != NULL) {
    return iree_ok_status();
  }
  if (session->native_target_environment == NULL) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "loom-check runner has no compiler target environment");
  }

  const loom_check_compile_provider_t* provider = session->provider;
  const loom_target_environment_t* native_target_environment =
      session->native_target_environment;
  const iree_allocator_t host_allocator = session->host_allocator;
  const loomc_allocator_t allocator = loomc_allocator_from_iree(host_allocator);
  iree_status_t status =
      iree_status_from_loomc(loomc_target_environment_create_from_native(
          native_target_environment, allocator, &session->target_environment));
  if (iree_status_is_ok(status)) {
    const loomc_context_target_options_t target_options = {
        .type = LOOMC_STRUCTURE_TYPE_CONTEXT_TARGET_OPTIONS,
        .structure_size = sizeof(target_options),
        .target_environment = session->target_environment,
    };
    const loomc_context_options_t context_options = {
        .type = LOOMC_STRUCTURE_TYPE_CONTEXT_OPTIONS,
        .structure_size = sizeof(context_options),
        .next = &target_options,
    };
    status = iree_status_from_loomc(
        loomc_context_create(&context_options, allocator, &session->context));
  }
  if (iree_status_is_ok(status)) {
    status = iree_status_from_loomc(loomc_workspace_create(
        /*options=*/NULL, allocator, &session->workspace));
  }
  if (iree_status_is_ok(status)) {
    status = iree_status_from_loomc(loomc_compiler_create(
        session->context, /*options=*/NULL, allocator, &session->compiler));
  }
  if (!iree_status_is_ok(status)) {
    loom_check_compile_session_deinitialize(session);
    session->provider = provider;
    session->native_target_environment = native_target_environment;
    session->host_allocator = host_allocator;
  }
  return status;
}

iree_status_t loom_check_compile_session_select_target_profile(
    loom_check_compile_session_t* session, iree_string_view_t specification,
    loomc_target_profile_t** out_target_profile) {
  *out_target_profile = NULL;
  IREE_RETURN_IF_ERROR(loom_check_compile_session_prepare(session));
  return iree_status_from_loomc(loomc_target_profile_select(
      session->target_environment, loomc_string_view_from_iree(specification),
      loomc_allocator_from_iree(session->host_allocator), out_target_profile));
}

static iree_status_t loom_check_compile_append_result(
    loom_check_diagnostic_collector_t* collector, const loomc_result_t* result,
    bool* out_succeeded) {
  *out_succeeded = loomc_result_succeeded(result);
  return loom_check_compile_append_result_diagnostics(collector, result);
}

static iree_status_t loom_check_compile_admit_module(
    const loom_test_case_t* test_case, iree_string_view_t filename,
    const loom_input_request_t* input_request,
    loom_check_compile_session_t* session,
    const loom_check_environment_t* environment,
    loom_check_diagnostic_collector_t* collector,
    iree_arena_block_pool_t* block_pool, iree_allocator_t host_allocator,
    loomc_module_t** out_module) {
  *out_module = NULL;

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

  loomc_module_t* module = NULL;
  loomc_result_t* result = NULL;
  loomc_source_t* source = NULL;
  char* source_identifier_storage = NULL;
  if (iree_status_is_ok(status) &&
      (provider == &loom_input_text_provider ||
       provider == &loom_input_bytecode_provider)) {
    status = loom_tooling_source_path_remap(
        source_identifier, &input_request->source_path_options, host_allocator,
        &source_identifier, &source_identifier_storage);
  }
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
          session->context, session->workspace, source, /*options=*/NULL,
          loomc_allocator_from_iree(host_allocator), &module, &result));
    } else if (session->provider != NULL && session->provider->import != NULL) {
      status = session->provider->import(
          session->provider->import_user_data, provider->name, input_options,
          &input_request->source_path_options, session->context,
          session->workspace, source, block_pool, host_allocator, &module,
          &result);
    } else {
      status = iree_make_status(
          IREE_STATUS_UNIMPLEMENTED,
          "input format '%.*s' has no LoomC importer linked into this runner",
          (int)provider->name.size, provider->name.data);
    }
  }
  bool admitted = false;
  if (iree_status_is_ok(status)) {
    status = loom_check_compile_append_result(collector, result, &admitted);
  }
  if (iree_status_is_ok(status) && admitted) {
    *out_module = module;
    module = NULL;
  }
  loomc_result_release(result);
  loomc_module_release(module);
  loomc_source_release(source);
  iree_allocator_free(host_allocator, source_identifier_storage);
  iree_string_builder_deinitialize(&stripped_source);
  return status;
}

static iree_status_t loom_check_compile_artifact_module(
    loom_check_compile_session_t* session,
    const loomc_pass_program_t* pass_program, loomc_module_t* module,
    const loomc_compile_artifact_options_t* options,
    loom_check_diagnostic_collector_t* collector, iree_allocator_t allocator,
    loomc_result_t** out_result) {
  *out_result = NULL;
  loomc_result_t* result = NULL;
  iree_status_t status = iree_status_from_loomc(loomc_compile_artifact(
      session->compiler, session->workspace, pass_program, module, options,
      loomc_allocator_from_iree(allocator), &result));
  bool compiled = false;
  if (iree_status_is_ok(status)) {
    status = loom_check_compile_append_result(collector, result, &compiled);
  }
  if (iree_status_is_ok(status) && compiled) {
    *out_result = result;
    result = NULL;
  }
  loomc_result_release(result);
  return status;
}

static iree_status_t loom_check_compile_get_artifact_pass_program(
    loom_check_compile_session_t* session,
    const loom_check_compile_artifact_options_t* options,
    loom_check_diagnostic_collector_t* collector,
    const loomc_pass_program_t** out_pass_program) {
  *out_pass_program = NULL;
  const loomc_allocator_t allocator =
      loomc_allocator_from_iree(session->host_allocator);
  const loomc_target_control_flow_lowering_t control_flow =
      options->control_flow_lowering;
  const iree_host_size_t pass_program_index =
      options->lower_source_to_low ? 1 + (iree_host_size_t)control_flow : 0;
  loomc_pass_program_t** pass_program =
      &session->artifact_pass_programs[pass_program_index];
  if (*pass_program == NULL && !options->lower_source_to_low) {
    IREE_RETURN_IF_ERROR(iree_status_from_loomc(loomc_pass_program_create_empty(
        session->context, /*options=*/NULL, allocator, pass_program)));
  }
  if (*pass_program == NULL) {
    const loomc_target_pipeline_options_t pipeline_options = {
        .type = LOOMC_STRUCTURE_TYPE_TARGET_PIPELINE_OPTIONS,
        .structure_size = sizeof(pipeline_options),
        .kind = LOOMC_TARGET_PIPELINE_KIND_SOURCE_LOW,
        .control_flow_lowering = control_flow,
    };
    loomc_result_t* operation_result = NULL;
    iree_status_t status =
        iree_status_from_loomc(loomc_pass_program_create_from_target_pipeline(
            session->context, &pipeline_options, allocator, pass_program,
            &operation_result));
    bool prepared = false;
    if (iree_status_is_ok(status)) {
      status = loom_check_compile_append_result(collector, operation_result,
                                                &prepared);
    }
    loomc_result_release(operation_result);
    if (!prepared) {
      loomc_pass_program_release(*pass_program);
      *pass_program = NULL;
    }
    IREE_RETURN_IF_ERROR(status);
  }
  *out_pass_program = *pass_program;
  return iree_ok_status();
}

iree_status_t loom_check_compile_artifact(
    const loom_check_emit_provider_request_t* request,
    const loom_check_compile_artifact_options_t* options,
    loomc_source_t** out_artifact_source) {
  *out_artifact_source = NULL;
  loom_check_compile_session_t* session = request->environment->compile_session;
  IREE_RETURN_IF_ERROR(loom_check_compile_session_prepare(session));

  loomc_module_t* module = NULL;
  iree_status_t status = loom_check_compile_admit_module(
      request->test_case, request->filename, request->input_request, session,
      request->environment, request->diagnostic_collector, request->block_pool,
      request->host_allocator, &module);

  const loomc_pass_program_t* pass_program = NULL;
  if (iree_status_is_ok(status) && module != NULL) {
    status = loom_check_compile_get_artifact_pass_program(
        session, options, request->diagnostic_collector, &pass_program);
  }

  loomc_target_profile_t* target_profile = NULL;
  if (iree_status_is_ok(status) && pass_program != NULL &&
      !iree_string_view_is_empty(options->target)) {
    status = loom_check_compile_session_select_target_profile(
        session, options->target, &target_profile);
  }

  const loomc_emit_options_t emit_options = {
      .type = LOOMC_STRUCTURE_TYPE_EMIT_OPTIONS,
      .structure_size = sizeof(emit_options),
      .artifact_format = loomc_string_view_from_iree(options->artifact_format),
  };
  const loomc_string_view_t root = loomc_string_view_from_iree(options->root);
  const loomc_compile_artifact_options_t compile_options = {
      .type = LOOMC_STRUCTURE_TYPE_COMPILE_ARTIFACT_OPTIONS,
      .structure_size = sizeof(compile_options),
      .roots = iree_string_view_is_empty(options->root) ? NULL : &root,
      .root_count = iree_string_view_is_empty(options->root) ? 0 : 1,
      .target_profile = target_profile,
      .emit_options = &emit_options,
  };
  loomc_result_t* result = NULL;
  if (iree_status_is_ok(status) && pass_program != NULL) {
    status = loom_check_compile_artifact_module(
        session, pass_program, module, &compile_options,
        request->diagnostic_collector, request->host_allocator, &result);
  }
  if (iree_status_is_ok(status) && result != NULL) {
    const loomc_artifact_t* artifact = loomc_result_artifact_at(result, 0);
    status = iree_status_from_loomc(loomc_artifact_create_source(
        artifact, LOOMC_SOURCE_FORMAT_UNKNOWN,
        loomc_allocator_from_iree(request->host_allocator),
        out_artifact_source));
  }

  loomc_result_release(result);
  loomc_target_profile_release(target_profile);
  loomc_module_release(module);
  loomc_workspace_trim(session->workspace);
  return status;
}

iree_status_t loom_check_execute_compile(
    const loom_test_case_t* test_case, iree_host_size_t case_index,
    loom_check_file_report_t* report, iree_string_view_t filename,
    const loom_input_request_t* input_request,
    const loom_check_compile_options_t* options,
    const loom_check_environment_t* environment,
    iree_arena_block_pool_t* block_pool, iree_allocator_t allocator,
    loom_check_result_t* result) {
  IREE_RETURN_IF_ERROR(loom_check_compile_session_prepare(options->session));
  iree_arena_allocator_t arena;
  iree_arena_initialize(block_pool, &arena);
  loom_check_diagnostic_collector_t collector = {
      .arena = &arena,
      .host_allocator = allocator,
      .filename = filename,
      .result = result,
  };

  loomc_module_t* module = NULL;
  iree_status_t status = loom_check_compile_admit_module(
      test_case, filename, input_request, options->session, environment,
      &collector, block_pool, allocator, &module);

  loomc_result_t* operation_result = NULL;
  if (iree_status_is_ok(status) && module != NULL) {
    const loomc_compile_artifact_options_t compile_options = {
        .type = LOOMC_STRUCTURE_TYPE_COMPILE_ARTIFACT_OPTIONS,
        .structure_size = sizeof(compile_options),
        .next = options->sanitizer,
        .target_profile = options->target_profile,
        .config = options->config,
    };
    status = loom_check_compile_artifact_module(
        options->session, /*pass_program=*/NULL, module, &compile_options,
        &collector, allocator, &operation_result);
  }
  loomc_result_release(operation_result);
  loomc_module_release(module);
  loomc_workspace_trim(options->session->workspace);

  if (iree_status_is_ok(status)) {
    status = loom_check_diagnostic_collector_finish(
        &collector, test_case, case_index, report, allocator, result);
    result->final_outcome = result->raw_outcome;
  }
  iree_arena_deinitialize(&arena);
  return status;
}
