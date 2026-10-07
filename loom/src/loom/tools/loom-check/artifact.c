// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tools/loom-check/artifact.h"

#include "loom/tools/loom-check/diagnostics.h"

iree_status_t loom_check_emit_target_artifact(
    const loom_check_emit_provider_request_t* request,
    iree_string_view_t public_artifact_format,
    const loom_function_version_list_t* function_versions, bool* out_emitted,
    loom_target_emit_artifact_t* out_artifact) {
  *out_emitted = false;
  *out_artifact = (loom_target_emit_artifact_t){0};
  const loom_target_emitter_t* emitter = loom_target_environment_lookup_emitter(
      request->environment->target_environment, public_artifact_format);
  if (emitter == NULL) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "target emitter for '%.*s' is not linked",
                            (int)public_artifact_format.size,
                            public_artifact_format.data);
  }

  loom_check_diagnostic_emitter_capture_t capture = {
      .diagnostic_collector = request->diagnostic_collector,
      .module = request->module,
      .source_resolver = request->source_resolver,
      .emitter = LOOM_EMITTER_PASS,
  };
  const loom_target_emit_request_t emit_request = {
      .target_environment = request->environment->target_environment,
      .low_descriptor_registry = &request->low_registry->registry,
      .module = request->module,
      .function_versions = function_versions,
      .identifier = emitter->default_identifier,
      .diagnostic_emitter = {.fn = loom_check_diagnostic_emitter_capture_emit,
                             .user_data = &capture},
      .scratch_arena = request->case_arena,
      .allocator = request->host_allocator,
  };
  return emitter->emit(&emit_request, out_emitted, out_artifact);
}

iree_status_t loom_check_target_artifact_borrow_or_clone_contents(
    const loom_target_emit_artifact_t* artifact, iree_allocator_t allocator,
    iree_const_byte_span_t* out_contents,
    iree_byte_span_t* out_owned_contents) {
  *out_contents = iree_const_byte_span_empty();
  *out_owned_contents = iree_byte_span_empty();
  if (iree_byte_sequence_try_get_contiguous_span(artifact->contents,
                                                 out_contents)) {
    return iree_ok_status();
  }
  IREE_RETURN_IF_ERROR(iree_byte_sequence_clone(artifact->contents, allocator,
                                                out_owned_contents));
  *out_contents = iree_make_const_byte_span(out_owned_contents->data,
                                            out_owned_contents->data_length);
  return iree_ok_status();
}
