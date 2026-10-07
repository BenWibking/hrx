// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/target/spirv/check/loom_check.h"

#include <stdint.h>

#include "loom/target/entry_selection.h"
#include "loom/target/tool/spirv.h"
#include "loom/tools/loom-check/artifact.h"
#include "loom/tools/loom-check/diagnostics.h"
#include "loom/tools/loom-check/requirements.h"
#include "loom/tools/loom-check/source_low.h"
#include "loom/verify/verify.h"

typedef enum loom_spirv_loom_check_input_e {
  LOOM_SPIRV_LOOM_CHECK_INPUT_LOW = 0,
  LOOM_SPIRV_LOOM_CHECK_INPUT_SOURCE_LOW = 1,
} loom_spirv_loom_check_input_t;

typedef enum loom_spirv_loom_check_emit_flag_bits_e {
  LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_NONE = 0u,
  LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_VALIDATE = 1u << 0,
  LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_TARGET = 1u << 1,
} loom_spirv_loom_check_emit_flag_bits_t;
typedef uint32_t loom_spirv_loom_check_emit_flags_t;

typedef struct loom_spirv_loom_check_emit_request_t {
  // Input form consumed by SPIR-V emission.
  loom_spirv_loom_check_input_t input;
  // Source-to-low control-flow shape when |input| is source-low.
  loom_target_control_flow_lowering_t control_flow_lowering;
  // Additional emit behavior requested by the RUN line.
  loom_spirv_loom_check_emit_flags_t flags;
  // Optional source function selected for target specialization.
  iree_string_view_t function_name;
  // Invocation-selected profile when the TARGET flag is present.
  loom_target_specification_t target;
} loom_spirv_loom_check_emit_request_t;

static bool loom_spirv_loom_check_emit_provider_matches(
    const loom_check_emit_provider_t* provider,
    iree_string_view_t target_name) {
  return iree_string_view_equal(target_name, IREE_SV("spirv-dis"));
}

static iree_status_t loom_spirv_loom_check_consume_token(
    iree_string_view_t* remaining, iree_string_view_t* out_token) {
  iree_string_view_t text = iree_string_view_trim(*remaining);
  iree_string_view_t token = iree_string_view_empty();
  iree_string_view_t rest = iree_string_view_empty();
  iree_string_view_split(text, ' ', &token, &rest);
  token = iree_string_view_trim(token);
  *remaining = iree_string_view_trim(rest);
  *out_token = token;
  return iree_ok_status();
}

static iree_status_t loom_spirv_loom_check_parse_emit_request(
    iree_string_view_t target_options,
    loom_spirv_loom_check_emit_request_t* out_request) {
  *out_request = (loom_spirv_loom_check_emit_request_t){
      .input = LOOM_SPIRV_LOOM_CHECK_INPUT_LOW,
      .control_flow_lowering = LOOM_TARGET_CONTROL_FLOW_LOWERING_CFG,
      .flags = LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_NONE,
  };

  enum {
    LOOM_SPIRV_LOOM_CHECK_PARSE_OPTION_INPUT = 1u << 0,
    LOOM_SPIRV_LOOM_CHECK_PARSE_OPTION_CONTROL_FLOW = 1u << 1,
  };
  uint32_t parse_options = 0;
  iree_string_view_t token = iree_string_view_empty();
  while (!iree_string_view_is_empty(target_options)) {
    IREE_RETURN_IF_ERROR(
        loom_spirv_loom_check_consume_token(&target_options, &token));
    if (iree_string_view_is_empty(token)) {
      continue;
    }
    if (iree_string_view_equal(token, IREE_SV("validate"))) {
      out_request->flags |= LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_VALIDATE;
      continue;
    }
    if (iree_string_view_starts_with(token, IREE_SV("@"))) {
      if (token.size == 1 ||
          !iree_string_view_is_empty(out_request->function_name)) {
        return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                "spirv-dis expects at most one @function");
      }
      out_request->function_name = token;
      continue;
    }
    iree_string_view_t option_name = iree_string_view_empty();
    iree_string_view_t option_value = iree_string_view_empty();
    iree_string_view_split(token, '=', &option_name, &option_value);
    option_name = iree_string_view_trim(option_name);
    option_value = iree_string_view_trim(option_value);
    if (iree_string_view_equal(option_name, IREE_SV("target"))) {
      if (iree_any_bit_set(out_request->flags,
                           LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_TARGET)) {
        return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                "duplicate spirv-dis option 'target'");
      }
      IREE_RETURN_IF_ERROR(
          loom_target_specification_parse(option_value, &out_request->target));
      out_request->flags |= LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_TARGET;
      continue;
    }
    if (iree_string_view_equal(option_name, IREE_SV("input"))) {
      if (iree_any_bit_set(parse_options,
                           LOOM_SPIRV_LOOM_CHECK_PARSE_OPTION_INPUT)) {
        return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                "duplicate spirv-dis option 'input'");
      }
      if (iree_string_view_equal(option_value, IREE_SV("low"))) {
        out_request->input = LOOM_SPIRV_LOOM_CHECK_INPUT_LOW;
      } else if (iree_string_view_equal(option_value, IREE_SV("source-low"))) {
        out_request->input = LOOM_SPIRV_LOOM_CHECK_INPUT_SOURCE_LOW;
      } else {
        return iree_make_status(
            IREE_STATUS_INVALID_ARGUMENT,
            "spirv-dis option 'input' expected 'low' or 'source-low', got "
            "'%.*s'",
            (int)option_value.size, option_value.data);
      }
      parse_options |= LOOM_SPIRV_LOOM_CHECK_PARSE_OPTION_INPUT;
      continue;
    }
    if (iree_string_view_equal(option_name, IREE_SV("control-flow"))) {
      if (iree_any_bit_set(parse_options,
                           LOOM_SPIRV_LOOM_CHECK_PARSE_OPTION_CONTROL_FLOW)) {
        return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                "duplicate spirv-dis option 'control-flow'");
      }
      if (iree_string_view_equal(option_value, IREE_SV("cfg"))) {
        out_request->control_flow_lowering =
            LOOM_TARGET_CONTROL_FLOW_LOWERING_CFG;
      } else if (iree_string_view_equal(option_value,
                                        IREE_SV("structured-low"))) {
        out_request->control_flow_lowering =
            LOOM_TARGET_CONTROL_FLOW_LOWERING_STRUCTURED_LOW;
      } else {
        return iree_make_status(
            IREE_STATUS_INVALID_ARGUMENT,
            "spirv-dis option 'control-flow' expected 'cfg' or "
            "'structured-low', got '%.*s'",
            (int)option_value.size, option_value.data);
      }
      parse_options |= LOOM_SPIRV_LOOM_CHECK_PARSE_OPTION_CONTROL_FLOW;
      continue;
    }
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "unknown spirv-dis option '%.*s'", (int)token.size,
                            token.data);
  }
  if (!iree_string_view_is_empty(out_request->function_name) &&
      !iree_any_bit_set(out_request->flags,
                        LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_TARGET)) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "spirv-dis @function requires target=family:selector");
  }
  if (out_request->input == LOOM_SPIRV_LOOM_CHECK_INPUT_LOW &&
      (out_request->control_flow_lowering !=
           LOOM_TARGET_CONTROL_FLOW_LOWERING_CFG ||
       iree_any_bit_set(out_request->flags,
                        LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_TARGET))) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "spirv-dis target and control-flow options require input=source-low");
  }
  return iree_ok_status();
}

static iree_status_t loom_spirv_loom_check_emit_provider_check_requirements(
    const loom_check_emit_provider_t* provider,
    const loom_test_case_t* test_case, loom_check_result_t* result,
    bool* out_continue_execution) {
  iree_string_view_t emit_target =
      iree_string_view_trim(test_case->emit_target);
  iree_string_view_t target_name = iree_string_view_empty();
  iree_string_view_t target_options = iree_string_view_empty();
  iree_string_view_split(emit_target, ' ', &target_name, &target_options);
  target_name = iree_string_view_trim(target_name);
  target_options = iree_string_view_trim(target_options);

  if (!iree_string_view_equal(target_name, IREE_SV("spirv-dis"))) {
    return iree_ok_status();
  }
  IREE_RETURN_IF_ERROR(loom_check_require_declared_requirement(
      test_case, IREE_SV("spirv-dis"), result, out_continue_execution));
  if (!*out_continue_execution) {
    return iree_ok_status();
  }

  loom_spirv_loom_check_emit_request_t request = {0};
  IREE_RETURN_IF_ERROR(
      loom_spirv_loom_check_parse_emit_request(target_options, &request));
  if (iree_any_bit_set(request.flags,
                       LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_VALIDATE)) {
    IREE_RETURN_IF_ERROR(loom_check_require_declared_requirement(
        test_case, IREE_SV("spirv-val"), result, out_continue_execution));
  }
  return iree_ok_status();
}

static iree_status_t loom_spirv_loom_check_strip_disassembly_comments(
    iree_string_view_t input, iree_string_builder_t* output) {
  iree_string_view_t remaining = input;
  while (!iree_string_view_is_empty(remaining)) {
    iree_string_view_t line = iree_string_view_empty();
    iree_string_view_split(remaining, '\n', &line, &remaining);
    iree_string_view_t trimmed = iree_string_view_trim(line);
    if (iree_string_view_starts_with(trimmed, IREE_SV(";"))) {
      continue;
    }
    IREE_RETURN_IF_ERROR(iree_string_builder_append_string(output, line));
    IREE_RETURN_IF_ERROR(iree_string_builder_append_cstring(output, "\n"));
  }
  return iree_ok_status();
}

static iree_status_t loom_spirv_loom_check_verify_low_module(
    const loom_check_emit_provider_request_t* request) {
  const loom_target_entry_options_t entry_options = {
      .diagnostic_sink = {.fn = loom_check_diagnostic_collector_sink,
                          .user_data = request->diagnostic_collector},
      .source_resolver = request->source_resolver,
      .max_errors = 20,
  };
  loom_verify_result_t verify_result = {0};
  IREE_RETURN_IF_ERROR(loom_target_entry_verify_module(
      request->module, &entry_options, 20, &verify_result));
  if (verify_result.error_count != 0 &&
      request->diagnostic_collector->count == 0) {
    return iree_make_status(
        IREE_STATUS_INTERNAL,
        "lowered module verifier reported errors without diagnostics");
  }
  if (verify_result.error_count != 0) {
    return iree_ok_status();
  }

  loom_target_entry_diagnostic_emitter_t verifier_emitter = {0};
  loom_target_entry_diagnostic_emitter_initialize(
      request->module, &entry_options, LOOM_EMITTER_VERIFIER,
      &verifier_emitter);
  loom_low_verify_result_t low_verify_result = {0};
  loom_low_verify_scratch_t low_verify_scratch =
      loom_low_verify_scratch_for_module(request->module);
  IREE_RETURN_IF_ERROR(loom_target_entry_verify_low_module(
      request->module, request->low_registry, &entry_options, &verifier_emitter,
      20,
      loom_target_environment_low_verify_provider_list(
          request->environment->target_environment),
      &low_verify_scratch, &low_verify_result));
  if (low_verify_result.error_count != 0 &&
      request->diagnostic_collector->count == 0) {
    return iree_make_status(
        IREE_STATUS_INTERNAL,
        "lowered low verifier reported errors without diagnostics");
  }
  return iree_ok_status();
}

static iree_status_t loom_spirv_loom_check_emit_provider_execute(
    const loom_check_emit_provider_t* provider,
    const loom_check_emit_provider_request_t* request) {
  loom_spirv_loom_check_emit_request_t emit_request = {0};
  IREE_RETURN_IF_ERROR(loom_spirv_loom_check_parse_emit_request(
      request->target_options, &emit_request));

  loom_target_emit_artifact_t artifact = {0};
  bool emitted = false;
  iree_status_t status = iree_ok_status();
  if (emit_request.input == LOOM_SPIRV_LOOM_CHECK_INPUT_SOURCE_LOW) {
    loom_check_prepare_source_low_options_t prepare_options = {0};
    prepare_options.control_flow_lowering = emit_request.control_flow_lowering;
    loom_target_specialization_request_t specialization = {0};
    if (iree_any_bit_set(emit_request.flags,
                         LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_TARGET)) {
      IREE_RETURN_IF_ERROR(loom_check_resolve_source_target(
          request->module, request->environment->target_environment,
          emit_request.function_name, &emit_request.target, &specialization));
      prepare_options.target_specializations =
          (loom_target_specialization_request_list_t){&specialization, 1};
    }
    status = loom_check_emit_source_low_artifact(
        request, &prepare_options, IREE_SV("spirv"), &emitted, &artifact);
  } else {
    status = loom_spirv_loom_check_verify_low_module(request);
    if (iree_status_is_ok(status) &&
        request->diagnostic_collector->count == 0) {
      const loom_function_version_list_t function_versions = {0};
      status = loom_check_emit_target_artifact(
          request, IREE_SV("spirv"), &function_versions, &emitted, &artifact);
    }
  }
  if (!iree_status_is_ok(status)) {
    loom_target_emit_artifact_release(&artifact);
    return status;
  }

  iree_const_byte_span_t contents = iree_const_byte_span_empty();
  iree_byte_span_t owned_contents = iree_byte_span_empty();
  if (iree_status_is_ok(status) && emitted) {
    status = loom_check_target_artifact_borrow_or_clone_contents(
        &artifact, request->host_allocator, &contents, &owned_contents);
  }

  loom_spirv_toolchain_t toolchain;
  loom_spirv_toolchain_initialize_from_environment(&toolchain);
  if (iree_status_is_ok(status) && emitted &&
      iree_any_bit_set(emit_request.flags,
                       LOOM_SPIRV_LOOM_CHECK_EMIT_FLAG_VALIDATE)) {
    status = loom_spirv_tool_validate_binary(&toolchain, contents,
                                             request->host_allocator);
  }

  loom_tool_output_t disassembly = {0};
  if (iree_status_is_ok(status) && emitted) {
    status = loom_spirv_tool_disassemble_binary(
        &toolchain, contents, request->host_allocator, &disassembly);
  }
  if (iree_status_is_ok(status) && emitted) {
    status = loom_spirv_loom_check_strip_disassembly_comments(
        iree_make_string_view(disassembly.data, disassembly.length),
        &request->result->actual_output);
  }

  loom_tool_output_deinitialize(&disassembly, request->host_allocator);
  iree_allocator_free(request->host_allocator, owned_contents.data);
  loom_target_emit_artifact_release(&artifact);
  return status;
}

static iree_status_t loom_spirv_loom_check_emit_provider_append_names(
    const loom_check_emit_provider_t* provider,
    iree_string_builder_t* builder) {
  return iree_string_builder_append_cstring(builder, "spirv-dis");
}

static bool loom_spirv_loom_check_requirement_provider_matches(
    const loom_check_requirement_provider_t* provider,
    iree_string_view_t requirement) {
  return iree_string_view_equal(requirement, IREE_SV("spirv-as")) ||
         iree_string_view_equal(requirement, IREE_SV("spirv-dis")) ||
         iree_string_view_equal(requirement, IREE_SV("spirv-val"));
}

static iree_status_t loom_spirv_loom_check_query_spirv_tool(
    loom_spirv_tool_kind_t tool_kind, iree_allocator_t allocator) {
  loom_spirv_toolchain_t toolchain;
  loom_spirv_toolchain_initialize_from_environment(&toolchain);
  loom_tool_output_t version_text = {0};
  iree_status_t status = loom_spirv_tool_query_version(
      &toolchain, tool_kind, allocator, &version_text);
  loom_tool_output_deinitialize(&version_text, allocator);
  return status;
}

static iree_status_t loom_spirv_loom_check_requirement_provider_query(
    const loom_check_requirement_provider_t* provider,
    const loom_check_environment_t* environment, iree_string_view_t requirement,
    iree_allocator_t allocator) {
  if (iree_string_view_equal(requirement, IREE_SV("spirv-as"))) {
    return loom_spirv_loom_check_query_spirv_tool(LOOM_SPIRV_TOOL_SPIRV_AS,
                                                  allocator);
  }
  if (iree_string_view_equal(requirement, IREE_SV("spirv-dis"))) {
    return loom_spirv_loom_check_query_spirv_tool(LOOM_SPIRV_TOOL_SPIRV_DIS,
                                                  allocator);
  }
  if (iree_string_view_equal(requirement, IREE_SV("spirv-val"))) {
    return loom_spirv_loom_check_query_spirv_tool(LOOM_SPIRV_TOOL_SPIRV_VAL,
                                                  allocator);
  }
  return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                          "unknown SPIR-V loom-check requirement '%.*s'",
                          (int)requirement.size, requirement.data);
}

static iree_status_t loom_spirv_loom_check_requirement_provider_append_names(
    const loom_check_requirement_provider_t* provider,
    iree_string_builder_t* builder) {
  return iree_string_builder_append_cstring(builder,
                                            "spirv-as, spirv-dis, spirv-val");
}

const loom_check_emit_provider_t loom_spirv_loom_check_emit_provider = {
    .name = IREE_SVL("spirv"),
    .match = loom_spirv_loom_check_emit_provider_matches,
    .check_requirements =
        loom_spirv_loom_check_emit_provider_check_requirements,
    .execute = loom_spirv_loom_check_emit_provider_execute,
    .append_names = loom_spirv_loom_check_emit_provider_append_names,
};

const loom_check_requirement_provider_t
    loom_spirv_loom_check_requirement_provider = {
        .name = IREE_SVL("spirv"),
        .match = loom_spirv_loom_check_requirement_provider_matches,
        .query = loom_spirv_loom_check_requirement_provider_query,
        .append_names = loom_spirv_loom_check_requirement_provider_append_names,
};
