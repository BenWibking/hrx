// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Native x86 register and stack transport selection.

#ifndef LOOM_TARGET_EMIT_NATIVE_X86_TRANSPORT_H_
#define LOOM_TARGET_EMIT_NATIVE_X86_TRANSPORT_H_

#include "loom/target/emit/native/x86/encoding.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum loom_x86_storage_transfer_e {
  LOOM_X86_STORAGE_TRANSFER_LOAD = 0,
  LOOM_X86_STORAGE_TRANSFER_STORE = 1,
} loom_x86_storage_transfer_t;

// One selected physical transfer. A zero encoding format denotes a coalesced
// register move and must not be appended to the prepared function.
typedef struct loom_x86_transport_instruction_t {
  // Fully allocated registers and stack displacement.
  loom_x86_encoding_operands_t operands;
  // Native scalar form or packed vector recipe.
  uint16_t encoding_format_id;
  // Immutable opcode and prefix fields.
  uint16_t encoding_id;
  // Architectural GPRs written by the transfer.
  uint16_t gpr_writes;
} loom_x86_transport_instruction_t;

// Maps an allocation location to its architectural register number. K values
// rotate abstract locations 0--6 to K1--K7 and location 7 to K0 so explicit
// writemask constraints can exclude K0 without shrinking K-only capacity.
uint8_t loom_x86_transport_register(uint16_t descriptor_reg_class_id,
                                    uint32_t location);

// Returns the byte width of one allocation unit in the register class.
uint32_t loom_x86_transport_byte_length(uint16_t descriptor_reg_class_id);

// Selects one register transfer from both retained endpoint classes. Returns
// false only for a representation change whose semantics are absent from a
// physical move row, currently K/SIMD conversions. Exact matches at the same
// architectural register produce a zero encoding format.
bool loom_x86_transport_select_register(
    uint16_t destination_reg_class_id, uint32_t destination_location,
    uint16_t source_reg_class_id, uint32_t source_location,
    loom_x86_transport_instruction_t* out_instruction);

// Selects one load or store between a register and base-plus-displacement
// memory. All current allocatable x86 register classes have a storage form.
void loom_x86_transport_select_storage(
    loom_x86_storage_transfer_t transfer, uint16_t descriptor_reg_class_id,
    uint32_t register_location, uint8_t base_register, int32_t displacement,
    loom_x86_transport_instruction_t* out_instruction);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_EMIT_NATIVE_X86_TRANSPORT_H_
