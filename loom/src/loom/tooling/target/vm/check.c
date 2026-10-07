// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/target/vm/check.h"

#include "iree/vm/bytecode/disassembler.h"
#include "loom/target/arch/vm/provider.h"
#include "loom/tools/loom-check/compile.h"
#include "loom/tools/loom-check/execute.h"
#include "loom/tools/loom-check/source_low.h"

static bool loom_vm_check_match(const loom_check_emit_provider_t* provider,
                                iree_string_view_t target_name) {
  return iree_string_view_equal(target_name, IREE_SV("vm-dis"));
}

static iree_status_t loom_vm_check_write(void* user_data,
                                         iree_string_view_t fragment) {
  return iree_string_builder_append_string(user_data, fragment);
}

static iree_status_t loom_vm_check_emit(
    const loom_check_emit_provider_t* provider,
    const loom_check_emit_provider_request_t* request) {
  loom_check_source_low_request_t source_request;
  IREE_RETURN_IF_ERROR(
      loom_check_source_low_parse(request->target_options, &source_request));
  if (source_request.options & ~LOOM_CHECK_SOURCE_LOW_OPTION_TARGET) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "vm-dis accepts only @function and target options");
  }
  const loom_check_compile_artifact_options_t compile_options = {
      .artifact_format = IREE_SV("vm"),
      .root = source_request.function_name,
      .target = source_request.target,
      .lower_source_to_low = true,
      .control_flow_lowering = LOOMC_TARGET_CONTROL_FLOW_LOWERING_CFG,
  };
  loomc_source_t* artifact_source = NULL;
  iree_status_t status =
      loom_check_compile_artifact(request, &compile_options, &artifact_source);
  const loomc_byte_span_t source_contents =
      loomc_source_contents(artifact_source);
  const iree_const_byte_span_t contents = iree_make_const_byte_span(
      source_contents.data, source_contents.data_length);
  if (iree_status_is_ok(status) && artifact_source != NULL) {
    status = iree_vm_bytecode_disassemble_module(
        contents,
        (iree_vm_bytecode_disassembler_write_callback_t){
            .fn = loom_vm_check_write,
            .user_data = &request->result->actual_output},
        request->host_allocator);
  }
  loomc_source_release(artifact_source);
  return status;
}

static iree_status_t loom_vm_check_append_names(
    const loom_check_emit_provider_t* provider,
    iree_string_builder_t* builder) {
  return iree_string_builder_append_cstring(builder, "vm-dis");
}

static const loom_check_emit_provider_t loom_vm_check_emit_provider = {
    .name = IREE_SVL("vm"),
    .consumes_source = true,
    .match = loom_vm_check_match,
    .execute = loom_vm_check_emit,
    .append_names = loom_vm_check_append_names,
};

static const loom_check_emit_provider_t* const loom_vm_check_emit_providers[] =
    {
        &loom_vm_check_emit_provider,
};

const loom_check_provider_t loom_vm_check_provider = {
    .name = IREE_SVL("vm"),
    .target_provider = &loom_vm_target_provider,
    .emit_providers = loom_vm_check_emit_providers,
    .emit_provider_count = IREE_ARRAYSIZE(loom_vm_check_emit_providers),
};
