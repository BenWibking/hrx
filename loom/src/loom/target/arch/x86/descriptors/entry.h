// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Instruction identities used by x86 entry-point materialization. Descriptor
// ordinals belong to a representation view, even when views share storage.

#ifndef LOOM_TARGET_ARCH_X86_DESCRIPTORS_ENTRY_H_
#define LOOM_TARGET_ARCH_X86_DESCRIPTORS_ENTRY_H_

#include "loom/codegen/low/descriptors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_x86_entry_descriptors_t {
  // Unsigned byte load to a GPR32 carrier.
  uint32_t load_u8;
  // Unsigned halfword load to a GPR32 carrier.
  uint32_t load_u16;
  // Word load to a GPR32 carrier.
  uint32_t load_u32;
  // Pointer or doubleword load to a GPR64 carrier.
  uint32_t load_u64;
  // Zero extension of a word to a pointer-sized carrier.
  uint32_t widen_u32;
  // Word immediate materialization.
  uint32_t constant_u32;
  // Pointer-sized immediate materialization.
  uint32_t constant_u64;
  // Unsigned word comparison with an immediate lower bound.
  uint32_t compare_uge_u32;
  // RIP-relative address of a native symbol.
  uint32_t symbol_address;
} loom_x86_entry_descriptors_t;

// Returns the entry vocabulary of an x86 representation contract. Feature-only
// fragments without scalar control/addressing instructions return NULL.
const loom_x86_entry_descriptors_t* loom_x86_entry_descriptors(
    const loom_low_descriptor_set_t* descriptor_set);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_ARCH_X86_DESCRIPTORS_ENTRY_H_
