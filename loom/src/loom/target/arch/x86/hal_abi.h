// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// x86 task entry parameter and invocation-state contracts.

#ifndef LOOM_TARGET_ARCH_X86_HAL_ABI_H_
#define LOOM_TARGET_ARCH_X86_HAL_ABI_H_

#include "iree/base/internal/arena.h"
#include "iree/hal/drivers/task/executable/library/abi.h"
#include "loom/ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

// Resident readonly library object returned by the compiled query function.
#define LOOM_X86_HAL_LIBRARY_SYMBOL "iree_hal_executable_library_v0"

typedef enum loom_x86_hal_builtin_e {
  LOOM_X86_HAL_BUILTIN_WORKGROUP_ID_X = 0,
  LOOM_X86_HAL_BUILTIN_WORKGROUP_ID_Y,
  LOOM_X86_HAL_BUILTIN_WORKGROUP_ID_Z,
  LOOM_X86_HAL_BUILTIN_WORKGROUP_COUNT_X,
  LOOM_X86_HAL_BUILTIN_WORKGROUP_COUNT_Y,
  LOOM_X86_HAL_BUILTIN_WORKGROUP_COUNT_Z,
  LOOM_X86_HAL_BUILTIN_COUNT_,
} loom_x86_hal_builtin_t;

typedef struct loom_x86_hal_builtin_info_t {
  // Qualified Low live-in source name for this invocation-constant value.
  iree_string_view_t name;
  // Physical state-pointer argument: one for dispatch, two for workgroup.
  uint8_t state_argument;
  // Byte offset in the x86-64 state structure.
  uint8_t offset;
  // Unsigned field width in bytes, before widening to the index carrier.
  uint8_t size;
} loom_x86_hal_builtin_info_t;

// Dense semantic builtin identities shared by source and authored Low paths.
extern const loom_x86_hal_builtin_info_t
    loom_x86_hal_builtins[LOOM_X86_HAL_BUILTIN_COUNT_];

typedef struct loom_x86_hal_abi_t {
  // Dispatch requirements represented by the versioned executable schema.
  iree_hal_executable_dispatch_attrs_v0_t attributes;
  // Arena-owned parameter records in logical declaration order.
  const iree_hal_executable_dispatch_parameter_v0_t* parameters;
} loom_x86_hal_abi_t;

// Parses the logical kernel interface independently of its physical signature.
// `signature` supplies source types and `offsets` supplies one binding ordinal
// or constant byte offset per parameter. Unused parameters retain their slots.
// Both entry materialization and native library emission consume this contract.
iree_status_t loom_x86_hal_abi_parse(const loom_module_t* module,
                                     loom_named_attr_slice_t layout,
                                     iree_arena_allocator_t* arena,
                                     loom_x86_hal_abi_t* out_abi);

// Builds the canonical x86 task parameter layout from a logical signature.
// Bindings are dense in declaration order; constants are aligned to their
// scalar element size, capped at eight bytes. Boolean lanes occupy one byte.
// The returned dictionary and its signature/offsets are module-owned.
iree_status_t loom_x86_hal_abi_layout_build(
    loom_module_t* module, const loom_type_t* types, uint16_t count,
    iree_arena_allocator_t* scratch_arena, loom_named_attr_slice_t* out_layout);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_ARCH_X86_HAL_ABI_H_
