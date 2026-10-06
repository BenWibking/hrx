// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/emit/native/x86/hal_library.h"

#include "iree/base/alignment.h"
#include "loom/ir/module.h"
#include "loom/ops/low/ops.h"
#include "loom/target/emit/native/x86/encoding.h"

// A signature carries logical types in source order; offsets explicitly place
// each value in either the binding table or the byte-addressed constant table.
// This contract remains meaningful after dead parameter uses disappear.
iree_status_t loom_x86_hal_library_entry_parse(
    const loom_module_t* module, const loom_target_entry_t* entry,
    iree_host_size_t symbol_index, iree_arena_allocator_t* arena,
    loom_x86_hal_library_entry_t* out_entry) {
  *out_entry = (loom_x86_hal_library_entry_t){
      .name = entry->func_name,
      .symbol_index = symbol_index,
      .attributes = {.workgroup_size_x = 1,
                     .workgroup_size_y = 1,
                     .workgroup_size_z = 1},
  };
  if (!loom_low_func_def_isa(entry->func.op)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "x86 HAL entries require a physical dispatch "
                            "function and its logical parameter ABI");
  }
  const loom_named_attr_slice_t layout =
      loom_low_func_def_abi_layout(entry->func.op);
  const loom_func_type_data_t* signature = NULL;
  const loom_attribute_t* offsets = NULL;
  for (uint16_t i = 0; i < layout.count; ++i) {
    const loom_named_attr_t* field = &layout.entries[i];
    const iree_string_view_t key =
        loom_string_table_get(&module->strings, field->name_id);
    if (iree_string_view_equal(key, IREE_SV("signature")) &&
        field->value.kind == LOOM_ATTR_TYPE) {
      const loom_type_t type =
          loom_type_table_get(&module->types, field->value.type_id);
      if (loom_type_is_function(type)) {
        signature = loom_type_func_data(type);
      }
    } else if (iree_string_view_equal(key, IREE_SV("offsets")) &&
               field->value.kind == LOOM_ATTR_I64_ARRAY) {
      offsets = &field->value;
    } else {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "invalid x86 HAL ABI field '%.*s'", (int)key.size,
                              key.data);
    }
  }
  if (!signature || signature->result_count || !offsets ||
      offsets->count != signature->arg_count) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "x86 HAL ABI requires a void logical signature and one offset per "
        "parameter");
  }
  iree_hal_executable_dispatch_parameter_v0_t* rows = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, signature->arg_count,
                                                 sizeof(*rows), (void**)&rows));
  out_entry->parameters = rows;
  out_entry->attributes.parameter_count = signature->arg_count;
  iree_status_t status = iree_ok_status();
  for (uint16_t i = 0; i < signature->arg_count && iree_status_is_ok(status);
       ++i) {
    const loom_type_t type = signature->types[i];
    const int64_t offset = offsets->i64_array[i];
    rows[i] = (iree_hal_executable_dispatch_parameter_v0_t){.name = UINT16_MAX};
    if (loom_type_is_buffer(type)) {
      if (offset < 0 || offset >= IREE_HAL_EXECUTABLE_MAX_BINDING_COUNT) {
        status = iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                  "x86 HAL binding ordinal is out of range");
      } else {
        rows[i].type = IREE_HAL_EXECUTABLE_DISPATCH_PARAM_TYPE_V0_BINDING;
        rows[i].offset = (uint16_t)offset;
        out_entry->attributes.binding_count =
            (uint8_t)iree_max(out_entry->attributes.binding_count, offset + 1);
      }
    } else {
      const loom_scalar_type_t element_type = loom_type_element_type(type);
      const uint32_t element_bits =
          element_type == LOOM_SCALAR_TYPE_INDEX ||
                  element_type == LOOM_SCALAR_TYPE_OFFSET
              ? 64
              : loom_scalar_type_bitwidth(element_type);
      uint64_t element_count = 1;
      const bool valid_shape =
          loom_type_is_scalar(type) ||
          (loom_type_is_vector(type) &&
           loom_type_static_element_count(type, &element_count));
      // This constant ABI uses one byte per boolean lane. Vector constants
      // retain lane order without padding between elements.
      const uint64_t element_bytes = (element_bits + 7) / 8;
      if (!valid_shape || !element_bytes || !element_count ||
          element_count > UINT8_MAX / element_bytes || offset < 0 ||
          offset > (int64_t)IREE_HAL_EXECUTABLE_MAX_CONSTANT_BYTE_LENGTH -
                       (int64_t)(element_count * element_bytes)) {
        status = iree_make_status(
            IREE_STATUS_INVALID_ARGUMENT,
            "x86 HAL constants require byte-addressable scalars or static "
            "vectors fitting the dispatch constant segment");
      } else {
        rows[i].type = IREE_HAL_EXECUTABLE_DISPATCH_PARAM_TYPE_V0_CONSTANT;
        rows[i].size = (uint8_t)(element_count * element_bytes);
        rows[i].offset = (uint16_t)offset;
        out_entry->attributes.constant_byte_length =
            iree_max(out_entry->attributes.constant_byte_length,
                     (uint32_t)rows[i].offset + rows[i].size);
      }
    }
  }
  return status;
}

// These are the x86-64 ABI offsets, independent of the compiler host's pointer
// size, alignment, and byte order. The standalone runtime schema checks the
// adapter wherever the host uses the same data model.
enum {
  LOOM_X86_HAL_LIBRARY_SIZE = 112,
  LOOM_X86_HAL_LIBRARY_EXPORTS = 8,
  LOOM_X86_HAL_HEADER_SIZE = 24,
  LOOM_X86_HAL_HEADER_NAME = 8,
  LOOM_X86_HAL_EXPORT_POINTERS = 8,
  LOOM_X86_HAL_EXPORT_ATTRIBUTES = 16,
  LOOM_X86_HAL_EXPORT_PARAMETERS = 24,
  LOOM_X86_HAL_EXPORT_NAMES = 40,
  LOOM_X86_HAL_ATTRIBUTES_SIZE = 64,
  LOOM_X86_HAL_PARAMETER_SIZE = 8,
};

#if defined(IREE_PTR_SIZE_64)
static_assert(sizeof(iree_hal_executable_library_v0_t) ==
                  LOOM_X86_HAL_LIBRARY_SIZE,
              "update the x86-64 library layout adapter");
static_assert(offsetof(iree_hal_executable_library_v0_t, exports) ==
                  LOOM_X86_HAL_LIBRARY_EXPORTS,
              "update the x86-64 export table offset");
static_assert(sizeof(iree_hal_executable_library_header_t) ==
                  LOOM_X86_HAL_HEADER_SIZE,
              "update the x86-64 library header adapter");
static_assert(offsetof(iree_hal_executable_library_header_t, name) ==
                  LOOM_X86_HAL_HEADER_NAME,
              "update the x86-64 library name offset");
static_assert(offsetof(iree_hal_executable_export_table_v0_t, ptrs) ==
                      LOOM_X86_HAL_EXPORT_POINTERS &&
                  offsetof(iree_hal_executable_export_table_v0_t, attrs) ==
                      LOOM_X86_HAL_EXPORT_ATTRIBUTES &&
                  offsetof(iree_hal_executable_export_table_v0_t, params) ==
                      LOOM_X86_HAL_EXPORT_PARAMETERS &&
                  offsetof(iree_hal_executable_export_table_v0_t, names) ==
                      LOOM_X86_HAL_EXPORT_NAMES,
              "update the x86-64 export table adapter");
static_assert(sizeof(iree_hal_executable_dispatch_attrs_v0_t) ==
                  LOOM_X86_HAL_ATTRIBUTES_SIZE,
              "update the dispatch attribute adapter");
#endif  // IREE_PTR_SIZE_64
static_assert(sizeof(iree_hal_executable_dispatch_parameter_v0_t) ==
                  LOOM_X86_HAL_PARAMETER_SIZE,
              "update the parameter adapter");

static void loom_x86_hal_library_pointer(loom_x86_hal_library_data_t* data,
                                         iree_host_size_t section_index,
                                         iree_host_size_t offset,
                                         iree_host_size_t symbol_index,
                                         uint64_t addend) {
  data->fixups[data->fixup_count++] = (loom_native_object_fixup_t){
      .section_contribution_index = section_index,
      .section_offset = offset,
      .relocation_kind = LOOM_X86_RELOCATION_POINTER,
      .target_symbol_index = symbol_index,
      .addend = (int64_t)addend,
  };
}

static void loom_x86_hal_library_attributes(
    const iree_hal_executable_dispatch_attrs_v0_t* attributes, uint8_t* data) {
  iree_unaligned_store_le_u64(data, attributes->flags);
  iree_unaligned_store_le_u16(data + 8, attributes->local_memory_pages);
  data[10] = attributes->binding_count;
  iree_unaligned_store_le_u32(data + 12, attributes->workgroup_size_x);
  iree_unaligned_store_le_u32(data + 16, attributes->workgroup_size_y);
  iree_unaligned_store_le_u16(data + 20, attributes->workgroup_size_z);
  iree_unaligned_store_le_u16(data + 22, attributes->parameter_count);
  iree_unaligned_store_le_u32(data + 24, attributes->constant_byte_length);
}

iree_status_t loom_x86_hal_library_build(
    iree_string_view_t name, const loom_x86_hal_library_entry_t* entries,
    uint16_t entry_count, iree_host_size_t library_symbol_index,
    iree_host_size_t section_index, iree_arena_allocator_t* arena,
    loom_x86_hal_library_data_t* out_data) {
  *out_data = (loom_x86_hal_library_data_t){0};
  const iree_host_size_t header_offset = LOOM_X86_HAL_LIBRARY_SIZE;
  const iree_host_size_t pointers_offset =
      header_offset + LOOM_X86_HAL_HEADER_SIZE;
  const iree_host_size_t attributes_offset = pointers_offset + entry_count * 8;
  const iree_host_size_t parameter_pointers_offset =
      attributes_offset + entry_count * LOOM_X86_HAL_ATTRIBUTES_SIZE;
  const iree_host_size_t names_offset =
      parameter_pointers_offset + entry_count * 8;
  const iree_host_size_t parameters_offset = names_offset + entry_count * 8;
  // Entry/parameter counts are each bounded by their 16-bit schema fields.
  // Aggregate strings and parameter records still need checked host extents.
  uint64_t parameter_count = 0;
  iree_host_size_t string_length = 0;
  bool fits = iree_host_size_checked_add(name.size, 1, &string_length);
  for (uint16_t i = 0; i < entry_count && fits; ++i) {
    parameter_count += entries[i].attributes.parameter_count;
    fits = iree_host_size_checked_add(string_length, entries[i].name.size,
                                      &string_length) &&
           iree_host_size_checked_add(string_length, 1, &string_length);
  }
  const uint64_t strings_offset =
      parameters_offset + parameter_count * LOOM_X86_HAL_PARAMETER_SIZE;
  iree_host_size_t contents_length = 0;
  if (!fits || strings_offset > IREE_HOST_SIZE_MAX ||
      !iree_host_size_checked_add((iree_host_size_t)strings_offset,
                                  string_length, &contents_length)) {
    return iree_make_status(IREE_STATUS_RESOURCE_EXHAUSTED,
                            "native HAL library data exceeds host size");
  }
  uint8_t* contents = NULL;
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate(arena, contents_length, (void**)&contents));
  memset(contents, 0, contents_length);
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, 6 + entry_count * 3,
                                                 sizeof(*out_data->fixups),
                                                 (void**)&out_data->fixups));
  out_data->section = (loom_native_section_contribution_t){
      .section_name = IREE_SV(".rodata"),
      .storage = LOOM_NATIVE_SECTION_STORAGE_CONTENTS,
      .access = LOOM_NATIVE_SECTION_ACCESS_READ,
      .contribution_alignment = 8,
      .contents = iree_make_const_byte_span(contents, contents_length),
  };
  loom_x86_hal_library_pointer(out_data, section_index, 0, library_symbol_index,
                               header_offset);
  iree_unaligned_store_le_u32(contents + header_offset,
                              IREE_HAL_EXECUTABLE_LIBRARY_VERSION_0_8);
  loom_x86_hal_library_pointer(out_data, section_index,
                               header_offset + LOOM_X86_HAL_HEADER_NAME,
                               library_symbol_index, strings_offset);
  memcpy(contents + strings_offset, name.data, name.size);
  iree_unaligned_store_le_u32(contents + LOOM_X86_HAL_LIBRARY_EXPORTS,
                              (uint32_t)entry_count);
  const iree_host_size_t table_fields[] = {
      LOOM_X86_HAL_EXPORT_POINTERS, LOOM_X86_HAL_EXPORT_ATTRIBUTES,
      LOOM_X86_HAL_EXPORT_PARAMETERS, LOOM_X86_HAL_EXPORT_NAMES};
  const iree_host_size_t table_offsets[] = {pointers_offset, attributes_offset,
                                            parameter_pointers_offset,
                                            names_offset};
  for (iree_host_size_t i = 0; i < IREE_ARRAYSIZE(table_fields); ++i) {
    loom_x86_hal_library_pointer(out_data, section_index,
                                 LOOM_X86_HAL_LIBRARY_EXPORTS + table_fields[i],
                                 library_symbol_index, table_offsets[i]);
  }
  iree_host_size_t parameter_offset = parameters_offset;
  iree_host_size_t string_offset = strings_offset + name.size + 1;
  for (iree_host_size_t i = 0; i < entry_count; ++i) {
    const loom_x86_hal_library_entry_t* entry = &entries[i];
    loom_x86_hal_library_pointer(out_data, section_index,
                                 pointers_offset + i * 8, entry->symbol_index,
                                 0);
    loom_x86_hal_library_pointer(out_data, section_index,
                                 parameter_pointers_offset + i * 8,
                                 library_symbol_index, parameter_offset);
    loom_x86_hal_library_pointer(out_data, section_index, names_offset + i * 8,
                                 library_symbol_index, string_offset);
    loom_x86_hal_library_attributes(
        &entry->attributes,
        contents + attributes_offset + i * LOOM_X86_HAL_ATTRIBUTES_SIZE);
    memcpy(contents + string_offset, entry->name.data, entry->name.size);
    string_offset += entry->name.size + 1;
    for (uint16_t p = 0; p < entry->attributes.parameter_count; ++p) {
      const iree_hal_executable_dispatch_parameter_v0_t* parameter =
          &entry->parameters[p];
      contents[parameter_offset] = parameter->type;
      contents[parameter_offset + 1] = parameter->size;
      iree_unaligned_store_le_u16(contents + parameter_offset + 2,
                                  parameter->flags);
      iree_unaligned_store_le_u16(contents + parameter_offset + 4,
                                  parameter->name);
      iree_unaligned_store_le_u16(contents + parameter_offset + 6,
                                  parameter->offset);
      parameter_offset += LOOM_X86_HAL_PARAMETER_SIZE;
    }
  }
  return iree_ok_status();
}
