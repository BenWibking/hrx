// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Native x86 task HAL library data contributions.

#ifndef LOOM_TARGET_EMIT_NATIVE_X86_HAL_LIBRARY_H_
#define LOOM_TARGET_EMIT_NATIVE_X86_HAL_LIBRARY_H_

#include "iree/base/internal/arena.h"
#include "iree/hal/drivers/task/executable/library/abi.h"
#include "loom/target/emit/native/object.h"
#include "loom/target/entry_selection.h"

#ifdef __cplusplus
extern "C" {
#endif

// Immutable library symbol addressed by the ordinary compiled query function.
#define LOOM_X86_HAL_LIBRARY_SYMBOL "iree_hal_executable_library_v0"

typedef struct loom_x86_hal_library_entry_t {
  // Export name, borrowed until the contribution has been built.
  iree_string_view_t name;
  // Native symbol index of the prepared dispatch function.
  iree_host_size_t symbol_index;
  // Dispatch requirements computed by entry preparation.
  iree_hal_executable_dispatch_attrs_v0_t attributes;
  // Logical parameters, with attributes.parameter_count entries.
  const iree_hal_executable_dispatch_parameter_v0_t* parameters;
} loom_x86_hal_library_entry_t;

typedef struct loom_x86_hal_library_data_t {
  // Arena-owned readonly data, independent of the input records.
  loom_native_section_contribution_t section;
  // Arena-owned pointer fixups referencing the caller's native symbol table.
  loom_native_object_fixup_t* fixups;
  // Number of populated pointer fixups.
  iree_host_size_t fixup_count;
} loom_x86_hal_library_data_t;

// Reads an authored HAL entry's logical parameter ABI. Parameters are declared
// in source order as binding ordinals or byte ranges of the constant segment;
// they remain independent of the three physical dispatch-state arguments.
// This is the input boundary for native library metadata, including reparsed
// Low. The returned records borrow names and own parameter storage in |arena|.
iree_status_t loom_x86_hal_library_entry_parse(
    const loom_module_t* module, const loom_target_entry_t* entry,
    iree_host_size_t symbol_index, iree_arena_allocator_t* arena,
    loom_x86_hal_library_entry_t* out_entry);

// Serializes the versioned x86-64 library layout. Every pointer is represented
// by a native fixup; no compiler-host address is copied into the image.
// Entry preparation owns signature, count, and export-name validation. The
// caller supplies the symbol/section indices reserved for the library data.
iree_status_t loom_x86_hal_library_build(
    iree_string_view_t name, const loom_x86_hal_library_entry_t* entries,
    uint16_t entry_count, iree_host_size_t library_symbol_index,
    iree_host_size_t section_index, iree_arena_allocator_t* arena,
    loom_x86_hal_library_data_t* out_data);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_EMIT_NATIVE_X86_HAL_LIBRARY_H_
