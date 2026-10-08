// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tools/loom-check/compile_diagnostics.h"

#include <string.h>

#include "loom/error/error_defs.h"
#include "loom/util/json.h"
#include "loom/util/stream.h"
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
  iree_string_view_t domain_name = iree_string_view_empty();
  iree_string_view_t code_text = iree_string_view_empty();
  if (iree_string_view_split(iree_string_view_from_loomc(code), '/',
                             &domain_name, &code_text) < 0 ||
      !loom_error_domain_from_name(domain_name, out_domain)) {
    *out_domain = LOOM_ERROR_DOMAIN_COUNT_;
    return NULL;
  }
  uint32_t code_value = 0;
  if (!iree_string_view_atoi_uint32_base(code_text, 10, &code_value) ||
      code_value > UINT16_MAX) {
    *out_domain = LOOM_ERROR_DOMAIN_COUNT_;
    return NULL;
  }
  *out_code = (uint16_t)code_value;
  return loom_error_def_lookup(*out_domain, *out_code);
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

static iree_status_t loom_check_compile_json_write_source_range(
    loom_json_object_writer_t* object, iree_string_view_t field_name,
    const loomc_source_range_t* range, iree_string_view_t filename) {
  if (range->source == NULL && range->start_line == 0 && range->start == 0 &&
      range->end == 0) {
    return iree_ok_status();
  }
  IREE_RETURN_IF_ERROR(loom_json_object_begin_field(object, field_name));
  loom_json_object_writer_t range_object;
  IREE_RETURN_IF_ERROR(loom_json_object_begin(object->stream, &range_object));
  IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
      &range_object, IREE_SV("filename"), filename));
  IREE_RETURN_IF_ERROR(loom_json_object_write_uint32_field(
      &range_object, IREE_SV("start_line"), range->start_line));
  IREE_RETURN_IF_ERROR(loom_json_object_write_uint32_field(
      &range_object, IREE_SV("start_column"), range->start_column));
  IREE_RETURN_IF_ERROR(loom_json_object_write_uint32_field(
      &range_object, IREE_SV("end_line"), range->end_line));
  IREE_RETURN_IF_ERROR(loom_json_object_write_uint32_field(
      &range_object, IREE_SV("end_column"), range->end_column));
  IREE_RETURN_IF_ERROR(loom_json_object_write_host_size_field(
      &range_object, IREE_SV("start_byte"), range->start));
  IREE_RETURN_IF_ERROR(loom_json_object_write_host_size_field(
      &range_object, IREE_SV("end_byte"), range->end));
  return loom_json_object_end(&range_object);
}

static iree_status_t loom_check_compile_append_diagnostic_json(
    loom_check_diagnostic_collector_t* collector,
    const loomc_diagnostic_t* diagnostic, const loom_error_def_t* error,
    loom_error_domain_t domain, uint16_t code, iree_string_view_t filename) {
  loom_output_stream_t stream;
  IREE_RETURN_IF_ERROR(loom_json_value_list_begin_value(
      &collector->result->diagnostics, &stream));
  loom_json_object_writer_t object;
  IREE_RETURN_IF_ERROR(loom_json_object_begin(&stream, &object));
  IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
      &object, IREE_SV("severity"),
      iree_make_cstring_view(loom_diagnostic_severity_name(
          loom_check_compile_diagnostic_severity(diagnostic->severity)))));
  if (error != NULL) {
    IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
        &object, IREE_SV("error_id"),
        iree_make_cstring_view(loom_error_def_id(error))));
    IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
        &object, IREE_SV("domain"),
        iree_make_cstring_view(loom_error_domain_name(domain))));
    IREE_RETURN_IF_ERROR(
        loom_json_object_write_uint32_field(&object, IREE_SV("code"), code));
    IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
        &object, IREE_SV("summary"),
        iree_make_cstring_view(loom_error_def_summary(error))));
  } else {
    IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
        &object, IREE_SV("diagnostic_code"),
        iree_string_view_from_loomc(diagnostic->code)));
  }
  IREE_RETURN_IF_ERROR(loom_check_compile_json_write_source_range(
      &object, IREE_SV("origin"), &diagnostic->range, filename));
  IREE_RETURN_IF_ERROR(loom_check_compile_json_write_source_range(
      &object, IREE_SV("source_location"), &diagnostic->range, filename));
  IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
      &object, IREE_SV("message"),
      iree_string_view_from_loomc(diagnostic->message)));
  if (!loomc_string_view_is_empty(diagnostic->formatted_text)) {
    IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
        &object, IREE_SV("formatted_text"),
        iree_string_view_from_loomc(diagnostic->formatted_text)));
  }
  if (diagnostic->parameter_count != 0) {
    IREE_RETURN_IF_ERROR(
        loom_json_object_begin_field(&object, IREE_SV("params")));
    loom_json_object_writer_t params;
    IREE_RETURN_IF_ERROR(loom_json_object_begin(&stream, &params));
    for (loomc_host_size_t i = 0; i < diagnostic->parameter_count; ++i) {
      IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
          &params, iree_string_view_from_loomc(diagnostic->parameters[i].name),
          iree_string_view_from_loomc(diagnostic->parameters[i].value)));
    }
    IREE_RETURN_IF_ERROR(loom_json_object_end(&params));
  }
  if (diagnostic->related_location_count != 0) {
    IREE_RETURN_IF_ERROR(
        loom_json_object_begin_field(&object, IREE_SV("related_locations")));
    loom_json_array_writer_t related;
    IREE_RETURN_IF_ERROR(loom_json_array_begin(&stream, &related));
    for (loomc_host_size_t i = 0; i < diagnostic->related_location_count; ++i) {
      IREE_RETURN_IF_ERROR(loom_json_array_begin_element(&related));
      loom_json_object_writer_t related_object;
      IREE_RETURN_IF_ERROR(loom_json_object_begin(&stream, &related_object));
      IREE_RETURN_IF_ERROR(loom_json_object_write_string_field(
          &related_object, IREE_SV("label"),
          iree_string_view_from_loomc(diagnostic->related_locations[i].label)));
      iree_string_view_t related_filename = iree_string_view_empty();
      IREE_RETURN_IF_ERROR(loom_check_compile_source_filename(
          &diagnostic->related_locations[i].range, collector->arena,
          &related_filename));
      IREE_RETURN_IF_ERROR(loom_check_compile_json_write_source_range(
          &related_object, IREE_SV("source_location"),
          &diagnostic->related_locations[i].range, related_filename));
      IREE_RETURN_IF_ERROR(loom_json_object_end(&related_object));
    }
    IREE_RETURN_IF_ERROR(loom_json_array_end(&related));
  }
  if (diagnostic->related_location_omitted_count != 0) {
    IREE_RETURN_IF_ERROR(loom_json_object_write_host_size_field(
        &object, IREE_SV("related_location_omitted_count"),
        diagnostic->related_location_omitted_count));
  }
  return loom_json_object_end(&object);
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
    const loom_error_def_t* error = loom_check_compile_diagnostic_error(
        source->code, &target.domain, &target.code);
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
    IREE_RETURN_IF_ERROR(loom_check_compile_append_diagnostic_json(
        collector, source, error, target.domain, target.code,
        target.origin.filename));
    IREE_RETURN_IF_ERROR(
        loom_check_diagnostic_collector_append(collector, &target));
  }
  return iree_ok_status();
}
