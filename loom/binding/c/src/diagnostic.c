// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "diagnostic.h"

#include "iree/base/api.h"
#include "loom/error/error_defs.h"
#include "loom/error/renderer.h"
#include "loom/error/source.h"
#include "loom/util/stream.h"
#include "loom/verify/verify.h"
#include "loomc/iree.h"

static loomc_diagnostic_severity_t loomc_diagnostic_severity_from_loom(
    loom_diagnostic_severity_t severity) {
  switch (severity) {
    case LOOM_DIAGNOSTIC_ERROR:
      return LOOMC_DIAGNOSTIC_SEVERITY_ERROR;
    case LOOM_DIAGNOSTIC_WARNING:
      return LOOMC_DIAGNOSTIC_SEVERITY_WARNING;
    case LOOM_DIAGNOSTIC_REMARK:
      return LOOMC_DIAGNOSTIC_SEVERITY_NOTE;
    case LOOM_DIAGNOSTIC_COUNT_:
      break;
  }
  return LOOMC_DIAGNOSTIC_SEVERITY_ERROR;
}

static loomc_status_t loomc_format_loom_diagnostic_code(
    const loom_diagnostic_t* diagnostic, iree_string_builder_t* builder) {
  const char* domain =
      loom_error_domain_name(loom_error_def_domain(diagnostic->error));
  return loomc_status_from_iree(iree_string_builder_append_format(
      builder, "%s/%03u", domain, loom_error_def_code(diagnostic->error)));
}

static loomc_status_t loomc_render_loom_diagnostic_message(
    const loom_diagnostic_t* diagnostic, iree_string_builder_t* builder) {
  loom_output_stream_t stream;
  loom_output_stream_for_builder(builder, &stream);
  loom_type_formatter_t formatter = {loom_type_format_minimal, NULL};
  return loomc_status_from_iree(loom_diagnostic_render_message(
      diagnostic->error, diagnostic->params, diagnostic->param_count, formatter,
      &stream));
}

// Retains the matching input owner or copies a diagnostic's borrowed identity
// and optional spelling before frontend/module storage is released.
static loomc_status_t loomc_source_from_loom_diagnostic(
    const loomc_source_t* source, const loom_diagnostic_t* diagnostic,
    loomc_allocator_t allocator, loomc_source_t** out_source) {
  *out_source = NULL;
  const loom_source_range_t* range = &diagnostic->source_location;
  loomc_byte_span_t contents =
      loomc_make_byte_span(range->source.data, range->source.size);
  loomc_source_format_t format = LOOMC_SOURCE_FORMAT_UNKNOWN;
  if (source != NULL) {
    const loomc_byte_span_t input_contents = loomc_source_contents(source);
    // Reader offsets identify the bytecode input itself. Later compiler
    // locations identify original source, even when the input was bytecode.
    if (diagnostic->emitter == LOOM_EMITTER_BYTECODE_READER) {
      contents = input_contents;
      format = LOOMC_SOURCE_FORMAT_BYTECODE;
    }
    if (iree_string_view_equal(
            iree_string_view_from_loomc(loomc_source_identifier(source)),
            range->filename) &&
        contents.data == input_contents.data &&
        contents.data_length == input_contents.data_length) {
      *out_source = (loomc_source_t*)source;
      loomc_source_retain(*out_source);
      return loomc_ok_status();
    }
  }
  if (range->filename.size == 0 && contents.data_length == 0) {
    return loomc_ok_status();
  }
  const loomc_source_options_t options = {
      .format = format,
      .identifier = loomc_string_view_from_iree(range->filename),
      .contents = contents,
      .storage = LOOMC_SOURCE_STORAGE_COPY,
  };
  return loomc_source_create(&options, allocator, out_source);
}

loomc_status_t loomc_result_add_loom_diagnostic(
    loomc_result_t* result, const loomc_source_t* source,
    const loom_diagnostic_t* diagnostic) {
  if (result == NULL || diagnostic == NULL || diagnostic->error == NULL) {
    return loomc_make_status(LOOMC_STATUS_INVALID_ARGUMENT,
                             "result and diagnostic must not be NULL");
  }
  iree_allocator_t allocator =
      iree_allocator_from_loomc(loomc_result_allocator(result));
  iree_string_builder_t code_builder;
  iree_string_builder_initialize(allocator, &code_builder);
  iree_string_builder_t message_builder;
  iree_string_builder_initialize(allocator, &message_builder);

  loomc_source_t* diagnostic_source = NULL;
  loomc_status_t status =
      loomc_format_loom_diagnostic_code(diagnostic, &code_builder);
  if (loomc_status_is_ok(status)) {
    status = loomc_render_loom_diagnostic_message(diagnostic, &message_builder);
  }
  if (loomc_status_is_ok(status)) {
    status = loomc_source_from_loom_diagnostic(
        source, diagnostic, loomc_result_allocator(result), &diagnostic_source);
  }
  if (loomc_status_is_ok(status)) {
    const loom_source_range_t* range = &diagnostic->source_location;
    loomc_diagnostic_t public_diagnostic = {
        .severity = loomc_diagnostic_severity_from_loom(diagnostic->severity),
        .code = loomc_string_view_from_iree(
            iree_string_builder_view(&code_builder)),
        .message = loomc_string_view_from_iree(
            iree_string_builder_view(&message_builder)),
        .range =
            {
                .source = diagnostic_source,
                .start = range->start,
                .end = range->end,
                .start_line = range->start_line,
                .start_column = range->start_column,
                .end_line = range->end_line,
                .end_column = range->end_column,
            },
    };
    status = loomc_result_add_diagnostic(result, &public_diagnostic);
  }

  loomc_source_release(diagnostic_source);
  iree_string_builder_deinitialize(&message_builder);
  iree_string_builder_deinitialize(&code_builder);
  return status;
}

loomc_status_t loomc_result_add_loom_diagnostic_emission(
    loomc_result_t* result, const loom_module_t* module, loom_emitter_t emitter,
    const loom_diagnostic_emission_t* emission) {
  if (emission == NULL || emission->error == NULL) {
    return loomc_make_status(LOOMC_STATUS_INVALID_ARGUMENT,
                             "diagnostic emission must not be NULL");
  }
  loom_diagnostic_t diagnostic = {
      .severity = loom_error_def_severity(emission->error),
      .error = emission->error,
      .params = emission->params,
      .param_count = emission->param_count,
      .emitter = emitter,
  };
  module = emission->module ? emission->module : module;
  if (emission->op) {
    loom_source_resolve((loom_source_resolver_t){0}, module,
                        emission->op->location, &diagnostic.source_location);
    diagnostic.origin = diagnostic.source_location;
  }
  return loomc_result_add_loom_diagnostic(result, NULL, &diagnostic);
}

static iree_status_t loomc_result_verify_capture_diagnostic(
    void* user_data, const loom_diagnostic_t* diagnostic) {
  return iree_status_from_loomc(loomc_result_add_loom_diagnostic(
      (loomc_result_t*)user_data, NULL, diagnostic));
}

loomc_status_t loomc_result_verify_loom_module(const loom_module_t* module,
                                               loomc_result_t* result) {
  if (module == NULL || result == NULL) {
    return loomc_make_status(LOOMC_STATUS_INVALID_ARGUMENT,
                             "module and result must not be NULL");
  }
  loom_verify_options_t verify_options = {
      .sink =
          {
              .fn = loomc_result_verify_capture_diagnostic,
              .user_data = result,
          },
      .max_errors = 20,
  };
  loom_verify_result_t verify_result = {0};
  LOOMC_RETURN_IF_ERROR(loomc_status_from_iree(
      loom_verify_module(module, &verify_options, &verify_result)));
  if (verify_result.error_count != 0) {
    return loomc_result_set_state(result, LOOMC_RESULT_STATE_FAILED);
  }
  return loomc_ok_status();
}

loomc_status_t loomc_result_add_status_diagnostic(
    loomc_result_t* result, const loomc_source_t* source,
    loomc_diagnostic_severity_t severity, loomc_string_view_t code,
    loomc_status_t status) {
  if (result == NULL) {
    return loomc_make_status(LOOMC_STATUS_INVALID_ARGUMENT,
                             "result must not be NULL");
  }
  loomc_string_view_t message = loomc_status_message(status);
  if (loomc_string_view_is_empty(message)) {
    message = loomc_make_cstring_view(
        loomc_status_code_string(loomc_status_code(status)));
  }
  loomc_diagnostic_t diagnostic = {
      .severity = severity,
      .code = code,
      .message = message,
      .range =
          {
              .source = source,
          },
  };
  return loomc_result_add_diagnostic(result, &diagnostic);
}

bool loomc_status_is_result_diagnostic(loomc_status_t status) {
  switch (loomc_status_code(status)) {
    case LOOMC_STATUS_INVALID_ARGUMENT:
    case LOOMC_STATUS_NOT_FOUND:
    case LOOMC_STATUS_FAILED_PRECONDITION:
    case LOOMC_STATUS_OUT_OF_RANGE:
    case LOOMC_STATUS_UNIMPLEMENTED:
    case LOOMC_STATUS_INCOMPATIBLE:
      return true;
    default:
      return false;
  }
}

loomc_status_t loomc_result_fail_status_diagnostic(
    loomc_result_t* result, const loomc_source_t* source,
    loomc_diagnostic_severity_t severity, loomc_string_view_t code,
    loomc_status_t status) {
  LOOMC_RETURN_IF_ERROR(loomc_result_add_status_diagnostic(
      result, source, severity, code, status));
  return loomc_result_set_state(result, LOOMC_RESULT_STATE_FAILED);
}

loomc_status_t loomc_result_fail_status_diagnostic_consume(
    loomc_result_t* result, const loomc_source_t* source,
    loomc_diagnostic_severity_t severity, loomc_string_view_t code,
    loomc_status_t status) {
  loomc_status_t add_status = loomc_result_fail_status_diagnostic(
      result, source, severity, code, status);
  loomc_status_free(status);
  return add_status;
}
