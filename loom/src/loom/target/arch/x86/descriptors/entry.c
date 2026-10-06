// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/x86/descriptors/entry.h"

#include "loom/target/arch/x86/descriptors/avx2_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx512_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx512_packed_dot_descriptors.h"
#include "loom/target/arch/x86/descriptors/scalar_descriptors.h"
#include "loom/target/arch/x86/descriptors/simd128_descriptors.h"

#define LOOM_X86_ENTRY_DESCRIPTORS(prefix)                                    \
  [prefix##_DESCRIPTOR_SET_ORDINAL] = &(const loom_x86_entry_descriptors_t) { \
    .load_u8 = prefix##_DESCRIPTOR_REF_SCALAR_MOVZX_LOAD_U8_GPR32,            \
    .load_u16 = prefix##_DESCRIPTOR_REF_SCALAR_MOVZX_LOAD_U16_GPR32,          \
    .load_u32 = prefix##_DESCRIPTOR_REF_SCALAR_MOV_LOAD_GPR32,                \
    .load_u64 = prefix##_DESCRIPTOR_REF_SCALAR_MOV_LOAD_GPR64,                \
    .widen_u32 = prefix##_DESCRIPTOR_REF_SCALAR_MOVZX_GPR64_GPR32,            \
    .constant_u32 = prefix##_DESCRIPTOR_REF_SCALAR_MOVIMM_GPR32,              \
    .constant_u64 = prefix##_DESCRIPTOR_REF_SCALAR_MOVIMM_GPR64,              \
    .compare_uge_u32 = prefix##_DESCRIPTOR_REF_SCALAR_CMP_UGE_IMM_GPR32,      \
    .symbol_address = prefix##_DESCRIPTOR_REF_SCALAR_LEA_SYMBOL_GPR64,        \
  }

static const loom_x86_entry_descriptors_t* const kEntryDescriptors[] = {
    LOOM_X86_ENTRY_DESCRIPTORS(X86_SCALAR_CORE),
    LOOM_X86_ENTRY_DESCRIPTORS(X86_SIMD128_CORE),
    LOOM_X86_ENTRY_DESCRIPTORS(X86_AVX2_CORE),
    LOOM_X86_ENTRY_DESCRIPTORS(X86_AVX512_CORE),
    LOOM_X86_ENTRY_DESCRIPTORS(X86_AVX512_PACKED_DOT_CORE),
};
#undef LOOM_X86_ENTRY_DESCRIPTORS

const loom_x86_entry_descriptors_t* loom_x86_entry_descriptors(
    const loom_low_descriptor_set_t* descriptor_set) {
  return descriptor_set->descriptor_set_ordinal <
                 IREE_ARRAYSIZE(kEntryDescriptors)
             ? kEntryDescriptors[descriptor_set->descriptor_set_ordinal]
             : NULL;
}
