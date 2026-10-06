// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/x86/hal_abi.h"

#include "iree/base/alignment.h"
#include "loom/ir/module.h"

const loom_x86_hal_builtin_info_t
    loom_x86_hal_builtins[LOOM_X86_HAL_BUILTIN_COUNT_] = {
        {IREE_SVL("x86.hal.workgroup_id_x"), 2, 0, 4},
        {IREE_SVL("x86.hal.workgroup_id_y"), 2, 4, 4},
        {IREE_SVL("x86.hal.workgroup_id_z"), 2, 8, 2},
        {IREE_SVL("x86.hal.workgroup_count_x"), 1, 12, 4},
        {IREE_SVL("x86.hal.workgroup_count_y"), 1, 16, 4},
        {IREE_SVL("x86.hal.workgroup_count_z"), 1, 20, 2},
};

static uint8_t loom_x86_hal_constant_size(loom_type_t type,
                                          uint32_t* out_alignment) {
  const loom_scalar_type_t element_type = loom_type_element_type(type);
  const uint32_t element_bits = element_type == LOOM_SCALAR_TYPE_INDEX ||
                                        element_type == LOOM_SCALAR_TYPE_OFFSET
                                    ? 64
                                    : loom_scalar_type_bitwidth(element_type);
  const uint32_t element_bytes = (element_bits + 7) / 8;
  uint64_t element_count = 1;
  if (!(loom_type_is_scalar(type) ||
        (loom_type_is_vector(type) &&
         loom_type_static_element_count(type, &element_count))) ||
      !element_bytes || !element_count ||
      element_count > UINT8_MAX / element_bytes) {
    return 0;
  }
  *out_alignment = iree_min(element_bytes, 8);
  return (uint8_t)(element_count * element_bytes);
}

// A signature carries logical types in source order; offsets explicitly place
// each value in either the binding table or the byte-addressed constant table.
// This contract remains meaningful after dead parameter uses disappear.
iree_status_t loom_x86_hal_abi_parse(const loom_module_t* module,
                                     loom_named_attr_slice_t layout,
                                     iree_arena_allocator_t* arena,
                                     loom_x86_hal_abi_t* out_abi) {
  *out_abi = (loom_x86_hal_abi_t){
      .attributes = {.workgroup_size_x = 1,
                     .workgroup_size_y = 1,
                     .workgroup_size_z = 1},
  };
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
  out_abi->parameters = rows;
  out_abi->attributes.parameter_count = signature->arg_count;
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
        out_abi->attributes.binding_count =
            (uint8_t)iree_max(out_abi->attributes.binding_count, offset + 1);
      }
    } else {
      uint32_t alignment = 0;
      const uint8_t size = loom_x86_hal_constant_size(type, &alignment);
      if (!size || offset < 0 ||
          offset >
              (int64_t)IREE_HAL_EXECUTABLE_MAX_CONSTANT_BYTE_LENGTH - size) {
        status = iree_make_status(
            IREE_STATUS_INVALID_ARGUMENT,
            "x86 HAL constants require byte-addressable scalars or static "
            "vectors fitting the dispatch constant segment");
      } else {
        rows[i].type = IREE_HAL_EXECUTABLE_DISPATCH_PARAM_TYPE_V0_CONSTANT;
        rows[i].size = size;
        rows[i].offset = (uint16_t)offset;
        out_abi->attributes.constant_byte_length =
            iree_max(out_abi->attributes.constant_byte_length,
                     (uint32_t)rows[i].offset + rows[i].size);
      }
    }
  }
  return status;
}

iree_status_t loom_x86_hal_abi_layout_build(
    loom_module_t* module, const loom_type_t* types, uint16_t count,
    iree_arena_allocator_t* scratch_arena,
    loom_named_attr_slice_t* out_layout) {
  *out_layout = loom_named_attr_slice_empty();
  int64_t* offsets = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      scratch_arena, count, sizeof(*offsets), (void**)&offsets));
  uint32_t binding_count = 0;
  uint32_t constant_bytes = 0;
  iree_status_t status = iree_ok_status();
  for (uint16_t i = 0; i < count && iree_status_is_ok(status); ++i) {
    if (loom_type_is_buffer(types[i])) {
      offsets[i] = binding_count++;
    } else {
      uint32_t alignment = 0;
      const uint8_t size = loom_x86_hal_constant_size(types[i], &alignment);
      if (!size) {
        status = iree_make_status(
            IREE_STATUS_INVALID_ARGUMENT,
            "x86 HAL parameter %u requires a buffer, scalar, or static vector",
            i);
      } else {
        constant_bytes = (uint32_t)iree_host_align(constant_bytes, alignment);
        offsets[i] = constant_bytes;
        constant_bytes += size;
      }
    }
  }
  IREE_RETURN_IF_ERROR(status);
  if (binding_count > IREE_HAL_EXECUTABLE_MAX_BINDING_COUNT ||
      constant_bytes > IREE_HAL_EXECUTABLE_MAX_CONSTANT_BYTE_LENGTH) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "x86 HAL parameter layout exceeds dispatch limits");
  }
  loom_type_t signature;
  IREE_RETURN_IF_ERROR(loom_module_intern_function_type(module, types, count,
                                                        NULL, 0, &signature));
  loom_named_attr_t fields[2];
  IREE_RETURN_IF_ERROR(loom_module_intern_string(module, IREE_SV("offsets"),
                                                 &fields[0].name_id));
  fields[0].value = loom_attr_i64_array(offsets, count);
  IREE_RETURN_IF_ERROR(loom_module_intern_string(module, IREE_SV("signature"),
                                                 &fields[1].name_id));
  fields[1].value =
      loom_attr_type(loom_module_lookup_type_id(module, signature));
  loom_attribute_t layout;
  IREE_RETURN_IF_ERROR(loom_module_make_canonical_attr_dict(
      module, loom_make_named_attr_slice(fields, 2), &layout));
  *out_layout = loom_attr_as_dict(layout);
  return iree_ok_status();
}
