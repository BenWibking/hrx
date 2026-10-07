// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_TARGET_ARCH_AMDGPU_PLANNING_SGPR_READ_HAZARD_H_
#define LOOM_TARGET_ARCH_AMDGPU_PLANNING_SGPR_READ_HAZARD_H_

#include "iree/base/api.h"
#include "iree/base/bitfield.h"
#include "iree/base/internal/arena.h"
#include "loom/codegen/low/allocation.h"
#include "loom/target/arch/amdgpu/refs/target_refs.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef enum loom_amdgpu_sgpr_read_hazard_alu_flag_bits_e {
  // Packet executes on the scalar ALU.
  LOOM_AMDGPU_SGPR_READ_HAZARD_ALU_FLAG_SCALAR = 1u << 0,
  // Packet executes on the vector ALU.
  LOOM_AMDGPU_SGPR_READ_HAZARD_ALU_FLAG_VECTOR = 1u << 1,
} loom_amdgpu_sgpr_read_hazard_alu_flag_bits_t;
typedef uint8_t loom_amdgpu_sgpr_read_hazard_alu_flags_t;

typedef enum loom_amdgpu_sgpr_read_hazard_register_flag_bits_e {
  LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_TRACKED = 1u << 0,
  LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_SCALAR_WRITE = 1u << 1,
  LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_VECTOR_WRITE = 1u << 2,
} loom_amdgpu_sgpr_read_hazard_register_flag_bits_t;
typedef uint8_t loom_amdgpu_sgpr_read_hazard_register_flags_t;

// State for one physical SGPR.
typedef struct loom_amdgpu_sgpr_read_hazard_register_t {
  // Tracking and active dependency bits for this physical SGPR.
  loom_amdgpu_sgpr_read_hazard_register_flags_t flags;
  // Padding retained so every register occupies eight bytes.
  uint8_t reserved[3];
  // ALU packet origin whose SGPR write created the active dependency.
  uint32_t origin;
} loom_amdgpu_sgpr_read_hazard_register_t;

static_assert(sizeof(loom_amdgpu_sgpr_read_hazard_register_t) == 8,
              "SGPR read-hazard registers must remain compact");

// GFX12 scalar-register dependencies created after VALU reads SGPR pairs.
typedef struct loom_amdgpu_sgpr_read_hazard_t {
  // Private state densely indexing allocated and ABI-fixed physical SGPRs.
  loom_amdgpu_sgpr_read_hazard_register_t* registers;
  // Number of entries in |registers|.
  iree_host_size_t state_count;
  // One-past-last SGPR in the ordinary allocated prefix.
  uint32_t allocated_register_count;
  // Even-sized state prefix reserved for ordinary allocated SGPRs.
  uint32_t allocated_state_count;
  // Even-aligned physical base represented by the fixed-state suffix.
  uint32_t fixed_state_base;
} loom_amdgpu_sgpr_read_hazard_t;

// Initializes |out_hazard| for an allocated SGPR prefix and a disjoint
// ABI-fixed SGPR range. Either range may be empty.
iree_status_t loom_amdgpu_sgpr_read_hazard_initialize(
    uint32_t allocated_register_count, uint32_t fixed_register_base,
    uint32_t fixed_register_count, iree_arena_allocator_t* arena,
    loom_amdgpu_sgpr_read_hazard_t* out_hazard);

// Returns true when storage was initialized for at least one physical SGPR.
static inline bool loom_amdgpu_sgpr_read_hazard_is_initialized(
    const loom_amdgpu_sgpr_read_hazard_t* hazard) {
  return hazard->registers != NULL && hazard->state_count != 0;
}

// Starts a new basic block with no tracked reads or outstanding hazards.
void loom_amdgpu_sgpr_read_hazard_begin_block(
    loom_amdgpu_sgpr_read_hazard_t* hazard);

// Clears outstanding dependencies while preserving SGPR pairs tracked by VALU
// reads in the current basic block.
void loom_amdgpu_sgpr_read_hazard_clear_dependencies(
    loom_amdgpu_sgpr_read_hazard_t* hazard);

// Tracks every SGPR pair overlapping a physical-SGPR read |assignment|.
static inline void loom_amdgpu_sgpr_read_hazard_track_read(
    loom_amdgpu_sgpr_read_hazard_t* hazard,
    const loom_low_allocation_assignment_t* assignment) {
  if (!loom_amdgpu_sgpr_read_hazard_is_initialized(hazard) ||
      assignment == NULL ||
      assignment->location_kind !=
          LOOM_LOW_ALLOCATION_LOCATION_PHYSICAL_REGISTER ||
      assignment->descriptor_reg_class_id != LOOM_AMDGPU_REG_CLASS_ID_SGPR) {
    return;
  }
  const uint64_t assignment_end =
      (uint64_t)assignment->location_base + assignment->location_count;
  loom_amdgpu_sgpr_read_hazard_register_t* registers = hazard->registers;
  uint32_t register_base = 0;
  if (assignment_end > hazard->allocated_register_count) {
    registers += hazard->allocated_state_count;
    register_base = hazard->fixed_state_base;
  }
  const uint32_t first_pair = (assignment->location_base - register_base) & ~1u;
  const uint32_t last_register =
      assignment->location_base + assignment->location_count - 1;
  const uint32_t end_pair = ((last_register - register_base) & ~1u) + 2;
  for (uint32_t pair = first_pair; pair < end_pair; pair += 2) {
    registers[pair].flags |= LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_TRACKED;
    registers[pair + 1].flags |=
        LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_TRACKED;
  }
}

// Records an ALU write to a physical-SGPR |assignment| when its pair has been
// tracked. The caller-supplied |origin| identifies the writing packet.
static inline void loom_amdgpu_sgpr_read_hazard_record_write(
    loom_amdgpu_sgpr_read_hazard_t* hazard,
    const loom_low_allocation_assignment_t* assignment,
    loom_amdgpu_sgpr_read_hazard_alu_flags_t alu_flags, uint32_t origin) {
  if (!loom_amdgpu_sgpr_read_hazard_is_initialized(hazard) ||
      assignment == NULL ||
      assignment->location_kind !=
          LOOM_LOW_ALLOCATION_LOCATION_PHYSICAL_REGISTER ||
      assignment->descriptor_reg_class_id != LOOM_AMDGPU_REG_CLASS_ID_SGPR ||
      alu_flags == 0) {
    return;
  }
  const uint64_t assignment_end =
      (uint64_t)assignment->location_base + assignment->location_count;
  loom_amdgpu_sgpr_read_hazard_register_t* registers = hazard->registers;
  uint32_t register_base = 0;
  if (assignment_end > hazard->allocated_register_count) {
    registers += hazard->allocated_state_count;
    register_base = hazard->fixed_state_base;
  }
  const loom_amdgpu_sgpr_read_hazard_register_flags_t dependency_flag =
      iree_any_bit_set(alu_flags, LOOM_AMDGPU_SGPR_READ_HAZARD_ALU_FLAG_VECTOR)
          ? LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_VECTOR_WRITE
          : LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_SCALAR_WRITE;
  for (uint32_t i = 0; i < assignment->location_count; ++i) {
    const uint32_t register_index =
        assignment->location_base + i - register_base;
    const uint32_t pair_base = register_index & ~1u;
    if (!iree_any_bit_set(
            registers[pair_base].flags | registers[pair_base + 1].flags,
            LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_TRACKED)) {
      continue;
    }
    loom_amdgpu_sgpr_read_hazard_register_t* register_state =
        &registers[register_index];
    register_state->flags =
        (register_state->flags &
         ~(LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_SCALAR_WRITE |
           LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_VECTOR_WRITE)) |
        dependency_flag;
    register_state->origin = origin;
  }
}

// Returns true when an ALU read of |assignment| must wait and stores the
// writing packet origin in |out_origin|.
static inline bool loom_amdgpu_sgpr_read_hazard_query_read(
    const loom_amdgpu_sgpr_read_hazard_t* hazard,
    const loom_low_allocation_assignment_t* assignment,
    loom_amdgpu_sgpr_read_hazard_alu_flags_t alu_flags, uint32_t* out_origin) {
  *out_origin = UINT32_MAX;
  if (!loom_amdgpu_sgpr_read_hazard_is_initialized(hazard) ||
      assignment == NULL ||
      assignment->location_kind !=
          LOOM_LOW_ALLOCATION_LOCATION_PHYSICAL_REGISTER ||
      assignment->descriptor_reg_class_id != LOOM_AMDGPU_REG_CLASS_ID_SGPR ||
      alu_flags == 0) {
    return false;
  }
  const uint64_t assignment_end =
      (uint64_t)assignment->location_base + assignment->location_count;
  const loom_amdgpu_sgpr_read_hazard_register_t* registers = hazard->registers;
  uint32_t register_base = 0;
  if (assignment_end > hazard->allocated_register_count) {
    registers += hazard->allocated_state_count;
    register_base = hazard->fixed_state_base;
  }
  const bool is_vector_alu =
      iree_any_bit_set(alu_flags, LOOM_AMDGPU_SGPR_READ_HAZARD_ALU_FLAG_VECTOR);
  for (uint32_t i = 0; i < assignment->location_count; ++i) {
    const loom_amdgpu_sgpr_read_hazard_register_t* register_state =
        &registers[assignment->location_base + i - register_base];
    const bool waits_for_scalar_write = iree_any_bit_set(
        register_state->flags,
        LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_SCALAR_WRITE);
    const bool waits_for_vector_write =
        is_vector_alu &&
        iree_any_bit_set(
            register_state->flags,
            LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_VECTOR_WRITE);
    if (waits_for_scalar_write || waits_for_vector_write) {
      *out_origin = register_state->origin;
      return true;
    }
  }
  return false;
}

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // LOOM_TARGET_ARCH_AMDGPU_PLANNING_SGPR_READ_HAZARD_H_
