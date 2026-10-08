// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tools/loom-check/compile_diagnostics.h"

#include <string.h>

#include "loom/error/error_defs.h"
#include "loom/tooling/cli/loomc_diagnostic_json.h"
#include "loomc/iree.h"

static iree_status_t loom_check_compile_copy_string(
    iree_arena_allocator_t* arena, iree_string_view_t source,
    iree_string_view_t* out_copy) {
  *out_copy = iree_string_view_empty();
  if (iree_string_view_is_empty(source)) {
    return iree_ok_status();
  }
  char* storage = NULL;
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate(arena, source.size, (void**)&storage));
  memcpy(storage, source.data, source.size);
  *out_copy = iree_make_string_view(storage, source.size);
  return iree_ok_status();
}

static loom_diagnostic_severity_t loom_check_compile_diagnostic_severity(
    loomc_diagnostic_severity_t severity) {
  switch (severity) {
    case LOOMC_DIAGNOSTIC_SEVERITY_NOTE:
      return LOOM_DIAGNOSTIC_REMARK;
    case LOOMC_DIAGNOSTIC_SEVERITY_WARNING:
      return LOOM_DIAGNOSTIC_WARNING;
    case LOOMC_DIAGNOSTIC_SEVERITY_ERROR:
      return LOOM_DIAGNOSTIC_ERROR;
  }
  return LOOM_DIAGNOSTIC_ERROR;
}

static const loom_error_def_t* loom_check_compile_diagnostic_error(
    loomc_string_view_t code, loom_error_domain_t* out_domain,
    uint16_t* out_code) {
  *out_domain = LOOM_ERROR_DOMAIN_COUNT_;
  *out_code = 0;
  loom_error_ref_t ref = LOOM_ERROR_REF_NONE;
  if (!loom_error_ref_parse(iree_string_view_from_loomc(code), &ref)) {
    return NULL;
  }
  *out_domain = loom_error_ref_domain(ref);
  *out_code = loom_error_ref_code(ref);
  return loom_error_def_lookup_ref(ref);
}

static iree_status_t loom_check_compile_source_filename(
    const loomc_source_range_t* range, iree_arena_allocator_t* arena,
    iree_string_view_t* out_filename) {
  *out_filename = iree_string_view_empty();
  if (range->source == NULL) {
    return iree_ok_status();
  }
  const iree_string_view_t identifier =
      iree_string_view_from_loomc(loomc_source_identifier(range->source));
  return loom_check_compile_copy_string(arena, identifier, out_filename);
}

iree_status_t loom_check_compile_append_result_diagnostics(
    loom_check_diagnostic_collector_t* collector,
    const loomc_result_t* source_result) {
  const loomc_host_size_t diagnostic_count =
      loomc_result_diagnostic_count(source_result);
  for (loomc_host_size_t i = 0; i < diagnostic_count; ++i) {
    const loomc_diagnostic_t* source =
        loomc_result_diagnostic_at(source_result, i);
    loom_check_collected_diagnostic_t target = {0};
    target.severity = loom_check_compile_diagnostic_severity(source->severity);
    loom_check_compile_diagnostic_error(source->code, &target.domain,
                                        &target.code);
    IREE_RETURN_IF_ERROR(loom_check_compile_source_filename(
        &source->range, collector->arena, &target.origin.filename));
    target.origin.line = source->range.start_line;
    IREE_RETURN_IF_ERROR(loom_check_compile_copy_string(
        collector->arena, iree_string_view_from_loomc(source->message),
        &target.message));
    if (source->parameter_count != 0) {
      IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
          collector->arena, source->parameter_count, sizeof(*target.params),
          (void**)&target.params));
      for (loomc_host_size_t j = 0; j < source->parameter_count; ++j) {
        IREE_RETURN_IF_ERROR(loom_check_compile_copy_string(
            collector->arena,
            iree_string_view_from_loomc(source->parameters[j].name),
            &target.params[j].name));
        IREE_RETURN_IF_ERROR(loom_check_compile_copy_string(
            collector->arena,
            iree_string_view_from_loomc(source->parameters[j].value),
            &target.params[j].value));
      }
      target.param_count = source->parameter_count;
    }
    const iree_string_view_t formatted =
        !loomc_string_view_is_empty(source->formatted_text)
            ? iree_string_view_from_loomc(source->formatted_text)
            : iree_string_view_from_loomc(source->message);
    IREE_RETURN_IF_ERROR(loom_check_compile_copy_string(
        collector->arena, formatted, &target.formatted_diagnostic));
    loom_output_stream_t diagnostic_stream;
    IREE_RETURN_IF_ERROR(loom_json_value_list_begin_value(
        &collector->result->diagnostics, &diagnostic_stream));
    IREE_RETURN_IF_ERROR(loom_tooling_cli_write_loomc_diagnostic_json(
        &diagnostic_stream, source));
    IREE_RETURN_IF_ERROR(
        loom_check_diagnostic_collector_append(collector, &target));
  }
  return iree_ok_status();
}
