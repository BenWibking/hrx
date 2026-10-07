// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amd/xdna/aie2p/check/artifact.h"

#include <inttypes.h>

#include "iree/schemas/xdna_executable.h"
#include "loom/target/reporting/format.h"
#include "loom/tools/loom-check/source_low.h"

static bool loom_aie2p_artifact_check_matches(
    const loom_check_emit_provider_t* provider, iree_string_view_t name) {
  (void)provider;
  return iree_string_view_equal(name, IREE_SV("aie2p-xdna"));
}

static iree_status_t loom_aie2p_artifact_check_print(
    iree_const_byte_span_t bytes, iree_string_builder_t* output) {
  // The production ELF writer owns layout validity. Read its public wire
  // records independently, so checks cover serialized requirements and fixups.
  const uint32_t header_offset = iree_unaligned_load_le_u32(bytes.data + 28);
  const uint32_t metadata_offset =
      iree_unaligned_load_le_u32(bytes.data + header_offset + 4);
  const uint8_t* metadata = bytes.data + metadata_offset;
  const iree_xdna_elf_header_record_t header =
      iree_xdna_elf_decode_header(metadata);
  const uint32_t entry_offset =
      IREE_XDNA_ELF_HEADER_RECORD_SIZE +
      header.allocation_count * IREE_XDNA_ELF_ALLOCATION_RECORD_SIZE +
      header.allocation_use_count * sizeof(uint32_t);
  const uint32_t binding_offset =
      entry_offset + header.entry_count * IREE_XDNA_ELF_ENTRY_RECORD_SIZE;
  const uint32_t relocation_offset =
      binding_offset + header.binding_count * IREE_XDNA_ELF_BINDING_RECORD_SIZE;
  const uint32_t invocation_offset =
      relocation_offset +
      header.relocation_count * IREE_XDNA_ELF_RELOCATION_RECORD_SIZE;
  IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
      output, "partition: %u columns, %u rows\nentries: %u\n",
      header.column_count, header.row_count, header.entry_count));
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    const iree_xdna_elf_entry_record_t entry = iree_xdna_elf_decode_entry(
        metadata + entry_offset + i * IREE_XDNA_ELF_ENTRY_RECORD_SIZE);
    IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
        output, "entry %u: %u bindings, %u dynamic relocations\n", i,
        entry.binding_count, entry.dynamic_relocation_count));
    for (uint32_t j = 0; j < entry.binding_count; ++j) {
      const iree_xdna_elf_binding_record_t binding =
          iree_xdna_elf_decode_binding(metadata + binding_offset +
                                       (entry.first_binding + j) *
                                           IREE_XDNA_ELF_BINDING_RECORD_SIZE);
      IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
          output,
          "  binding %u: access=%u bytes=%" PRIu64 " alignment=%" PRIu64
          " offset=[%" PRIu64 ",%" PRIu64 "]\n",
          j, binding.access, binding.minimum_byte_length,
          binding.minimum_alignment, binding.minimum_byte_offset,
          binding.maximum_byte_offset));
    }
    for (uint32_t j = 0; j < entry.dynamic_relocation_count; ++j) {
      const iree_xdna_elf_relocation_record_t relocation =
          iree_xdna_elf_decode_relocation(
              metadata + relocation_offset +
              (entry.first_dynamic_relocation + j) *
                  IREE_XDNA_ELF_RELOCATION_RECORD_SIZE);
      IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
          output,
          "  relocation %u: binding=%u addend=%" PRId64 " maximum=%" PRIu64
          " alignment=%" PRIu64 "\n",
          j, relocation.source_ordinal, relocation.addend,
          relocation.maximum_value, relocation.alignment));
    }
    for (uint32_t j = 0; j < entry.invocation_count; ++j) {
      const iree_xdna_elf_invocation_record_t invocation =
          iree_xdna_elf_decode_invocation(
              metadata + invocation_offset +
              (entry.first_invocation + j) *
                  IREE_XDNA_ELF_INVOCATION_RECORD_SIZE);
      IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
          output, "  invocation %u: bytes=%u next=%u\n", j,
          invocation.byte_length, invocation.next_invocation));
    }
  }
  return iree_ok_status();
}

// Read final ELF sections, including relocated code and uninitialized data.
// This is check-provider output, independent of the compiler's placement plan.
static iree_status_t loom_aie2p_artifact_check_print_sections(
    iree_const_byte_span_t bytes, iree_string_builder_t* output) {
  const uint32_t table_offset = iree_unaligned_load_le_u32(bytes.data + 32);
  const uint16_t record_size = iree_unaligned_load_le_u16(bytes.data + 46);
  const uint16_t record_count = iree_unaligned_load_le_u16(bytes.data + 48);
  const uint16_t names_index = iree_unaligned_load_le_u16(bytes.data + 50);
  const uint8_t* names_header =
      bytes.data + table_offset + names_index * record_size;
  const char* names =
      (const char*)bytes.data + iree_unaligned_load_le_u32(names_header + 16);
  for (uint16_t i = 0; i < record_count; ++i) {
    const uint8_t* header = bytes.data + table_offset + i * record_size;
    const uint32_t flags = iree_unaligned_load_le_u32(header + 8);
    if (!(flags & 2)) {
      continue;  // Only SHF_ALLOC sections occupy native worker memory.
    }
    const uint32_t type = iree_unaligned_load_le_u32(header + 4);
    const uint32_t address = iree_unaligned_load_le_u32(header + 12);
    const uint32_t length = iree_unaligned_load_le_u32(header + 20);
    IREE_RETURN_IF_ERROR(iree_string_builder_append_format(
        output, "%s: address=0x%08x bytes=%u %s\n",
        names + iree_unaligned_load_le_u32(header), address, length,
        type == 8 ? "reserved" : "initialized"));
    if (type == 8) {
      continue;
    }
    const uint8_t* contents =
        bytes.data + iree_unaligned_load_le_u32(header + 16);
    IREE_RETURN_IF_ERROR(iree_string_builder_append_cstring(output, "  "));
    for (uint32_t j = 0; j < length; ++j) {
      IREE_RETURN_IF_ERROR(
          iree_string_builder_append_format(output, "%02x", contents[j]));
    }
    IREE_RETURN_IF_ERROR(iree_string_builder_append_cstring(output, "\n"));
  }
  return iree_ok_status();
}

static iree_status_t loom_aie2p_artifact_check_execute(
    const loom_check_emit_provider_t* provider,
    const loom_check_emit_provider_request_t* request) {
  (void)provider;
  iree_string_view_t options = iree_string_view_trim(request->target_options);
  enum {
    OUTPUT_ARTIFACT,
    OUTPUT_SECTIONS,
    OUTPUT_PIPELINE_REPORT,
  } output = OUTPUT_ARTIFACT;
  bool source_low = false;
  while (!iree_string_view_is_empty(options)) {
    iree_string_view_t option;
    iree_string_view_split(options, ' ', &option, &options);
    options = iree_string_view_trim(options);
    if (iree_string_view_equal(option, IREE_SV("output=sections")) &&
        output == OUTPUT_ARTIFACT) {
      output = OUTPUT_SECTIONS;
    } else if (iree_string_view_equal(option,
                                      IREE_SV("output=pipeline-report")) &&
               output == OUTPUT_ARTIFACT) {
      output = OUTPUT_PIPELINE_REPORT;
    } else if (iree_string_view_equal(option, IREE_SV("input=source-low")) &&
               !source_low) {
      source_low = true;
    } else {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "invalid or duplicate aie2p-xdna option '%.*s'",
                              (int)option.size, option.data);
    }
  }
  loom_target_compile_report_t report;
  loom_target_compile_report_initialize(&report, request->host_allocator);
  report.requested_detail_flags =
      LOOM_TARGET_COMPILE_REPORT_DETAIL_PIPELINE_PLAN_ROWS;
  loom_target_compile_report_t* report_ptr =
      output == OUTPUT_PIPELINE_REPORT ? &report : NULL;
  loom_target_emit_artifact_t artifact = {0};
  bool emitted = false;
  iree_status_t status = iree_ok_status();
  if (source_low) {
    const loom_check_prepare_source_low_options_t prepare_options = {
        .report = report_ptr,
    };
    status = loom_check_emit_source_low_artifact(
        request, &prepare_options, IREE_SV("xdna"), &emitted, &artifact);
  } else {
    status = loom_check_emit_target_artifact(request, IREE_SV("xdna"), NULL,
                                             report_ptr, &emitted, &artifact);
  }
  iree_const_byte_span_t bytes = iree_const_byte_span_empty();
  iree_byte_span_t owned_contents = iree_byte_span_empty();
  if (iree_status_is_ok(status) && emitted &&
      output != OUTPUT_PIPELINE_REPORT) {
    status = loom_check_target_artifact_borrow_or_clone_contents(
        &artifact, request->host_allocator, &bytes, &owned_contents);
  }
  if (iree_status_is_ok(status) && emitted &&
      output == OUTPUT_PIPELINE_REPORT) {
    const loom_target_compile_report_format_options_t format_options = {
        .mode = LOOM_TARGET_COMPILE_REPORT_FORMAT_MODE_DETAILS,
    };
    iree_string_builder_t formatted;
    iree_string_builder_initialize(request->host_allocator, &formatted);
    status = loom_target_compile_report_format_text(&report, &format_options,
                                                    &formatted);
    // Select the canonical inventory from the public report. Per-function
    // scheduling and compiler allocation statistics are separate contracts.
    iree_string_view_t remaining = iree_string_builder_view(&formatted);
    while (iree_status_is_ok(status) && !iree_string_view_is_empty(remaining)) {
      iree_string_view_t line;
      iree_string_view_split(remaining, '\n', &line, &remaining);
      if (iree_string_view_starts_with(line,
                                       IREE_SV("COMPILE-REPORT: pipeline"))) {
        status = iree_string_builder_append_format(
            &request->result->actual_output, "%.*s\n", (int)line.size,
            line.data);
      }
    }
    iree_string_builder_deinitialize(&formatted);
  } else if (iree_status_is_ok(status) && emitted &&
             output == OUTPUT_SECTIONS) {
    status = loom_aie2p_artifact_check_print_sections(
        bytes, &request->result->actual_output);
  } else if (iree_status_is_ok(status) && emitted) {
    status =
        loom_aie2p_artifact_check_print(bytes, &request->result->actual_output);
  }
  iree_allocator_free(request->host_allocator, owned_contents.data);
  loom_target_emit_artifact_release(&artifact);
  loom_target_compile_report_deinitialize(&report);
  return status;
}

static iree_status_t loom_aie2p_artifact_check_append_names(
    const loom_check_emit_provider_t* provider,
    iree_string_builder_t* builder) {
  (void)provider;
  return iree_string_builder_append_cstring(builder, "aie2p-xdna\n");
}

const loom_check_emit_provider_t loom_aie2p_artifact_check_emit_provider = {
    .name = IREE_SVL("aie2p-xdna"),
    .match = loom_aie2p_artifact_check_matches,
    .execute = loom_aie2p_artifact_check_execute,
    .append_names = loom_aie2p_artifact_check_append_names,
};
