// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/emit/native/x86/module.h"

#include "loom/target/emit/native/x86/abi.h"
#include "loom/target/emit/native/x86/function.h"

static iree_status_t loom_x86_module_check_declaration(
    const loom_target_emit_request_t* request, const loom_target_entry_t* entry,
    bool* out_accepted) {
  loom_x86_function_abi_t abi;
  return loom_x86_function_abi_prepare(
      request->module, entry, request->diagnostic_emitter, out_accepted, &abi);
}

static iree_status_t loom_x86_module_encode_function(
    const loom_target_emit_request_t* request, const loom_target_entry_t* entry,
    const uint32_t* symbol_indices, iree_host_size_t section_index,
    loom_native_module_fixups_t* fixups, iree_arena_allocator_t* function_arena,
    iree_io_stream_t* stream, bool* out_accepted) {
  loom_x86_function_abi_t abi;
  IREE_RETURN_IF_ERROR(loom_x86_function_abi_prepare(
      request->module, entry, request->diagnostic_emitter, out_accepted, &abi));
  if (!*out_accepted) {
    return iree_ok_status();
  }
  const loom_low_allocation_reserved_range_t stack_pointer = {
      .register_class = IREE_SV("x86.gpr64"),
      .location_kind = LOOM_LOW_ALLOCATION_LOCATION_PHYSICAL_REGISTER,
      .location_base = 4,
      .location_count = 1,
  };
  const loom_low_emission_frame_options_t frame_options = {
      .descriptor_registry = request->low_descriptor_registry,
      .function_target_facts = entry->target_facts,
      .memory_accesses = entry->function_version
                             ? entry->function_version->memory_accesses
                             : NULL,
      .schedule_strategy = LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY,
      .allocation_entry_locations = abi.entry_locations,
      .allocation_entry_location_count = abi.entry_location_count,
      .call_contracts = {.query = loom_x86_function_call_contract},
      .synchronous_storage_spaces = LOOM_LOW_STORAGE_SPACE_SET_STACK |
                                    LOOM_LOW_STORAGE_SPACE_SET_PRIVATE |
                                    LOOM_LOW_STORAGE_SPACE_SET_SCRATCH,
      .allocation_reserved_ranges = &stack_pointer,
      .allocation_reserved_range_count = 1,
      .emitter = request->diagnostic_emitter,
  };
  const loom_low_emission_frame_spill_free_options_t spill_options = {0};
  loom_low_emission_frame_t frame = {0};
  IREE_RETURN_IF_ERROR(loom_low_emission_frame_build_spill_free(
      request->module, (loom_op_t*)entry->func.op, &frame_options,
      &spill_options, function_arena, &frame, out_accepted));
  if (!*out_accepted) {
    return iree_ok_status();
  }
  IREE_RETURN_IF_ERROR(loom_native_module_check_calls(
      request, &frame.schedule, symbol_indices, out_accepted));
  if (!*out_accepted) {
    return iree_ok_status();
  }
  loom_x86_function_t function;
  IREE_RETURN_IF_ERROR(
      loom_x86_function_prepare(&frame, request->diagnostic_emitter,
                                function_arena, out_accepted, &function));
  if (!*out_accepted) {
    return iree_ok_status();
  }
  if (function.symbol_fixup_count) {
    IREE_RETURN_IF_ERROR(iree_arena_grow_array(
        request->scratch_arena, fixups->count,
        fixups->count + function.symbol_fixup_count, sizeof(*fixups->values),
        &fixups->capacity, (void**)&fixups->values));
  }
  IREE_RETURN_IF_ERROR(loom_x86_function_write(
      &function, symbol_indices, section_index,
      function.symbol_fixup_count ? fixups->values + fixups->count : NULL,
      stream, function_arena));
  fixups->count += function.symbol_fixup_count;
  return iree_ok_status();
}

iree_status_t loom_x86_module_emit(
    const loom_target_emit_request_t* request,
    const loom_target_fact_type_t* target_fact_type,
    loom_native_module_format_t format, bool* out_emitted,
    loom_target_emit_artifact_t* out_artifact) {
  static const loom_native_elf_relocation_t relocations[] = {
      [LOOM_X86_RELOCATION_CALL] =
          {
              .type = 4,  // R_X86_64_PLT32.
              .image_encoding = LOOM_NATIVE_ELF_FIXUP_PC_RELATIVE_32,
          },
      [LOOM_X86_RELOCATION_ADDRESS] =
          {
              .type = 2,  // R_X86_64_PC32.
              .image_encoding = LOOM_NATIVE_ELF_FIXUP_PC_RELATIVE_32,
          },
      [LOOM_X86_RELOCATION_POINTER] =
          {
              .type = 1,  // R_X86_64_64.
              .image_encoding = LOOM_NATIVE_ELF_FIXUP_ABSOLUTE_64,
          },
  };
  static const loom_native_module_elf_target_t target = {
      .check_declaration = loom_x86_module_check_declaration,
      .emit_function = loom_x86_module_encode_function,
      .function_alignment = 16,
      .pointer_relocation_kind = LOOM_X86_RELOCATION_POINTER,
      .image_options =
          {
              .machine = LOOM_NATIVE_ELF_MACHINE_X86_64,
              .page_alignment = 4096,
              .relative_relocation_type = 8,  // R_X86_64_RELATIVE.
              .relocations = relocations,
          },
  };
  return loom_native_module_emit_elf64le(request, target_fact_type, format,
                                         &target, out_emitted, out_artifact);
}
