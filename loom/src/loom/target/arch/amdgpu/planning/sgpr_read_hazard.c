// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amdgpu/planning/sgpr_read_hazard.h"

#include <string.h>

iree_status_t loom_amdgpu_sgpr_read_hazard_initialize(
    uint32_t allocated_register_count, uint32_t fixed_register_base,
    uint32_t fixed_register_count, iree_arena_allocator_t* arena,
    loom_amdgpu_sgpr_read_hazard_t* out_hazard) {
  *out_hazard = (loom_amdgpu_sgpr_read_hazard_t){0};
  IREE_ASSERT(fixed_register_count == 0 ||
              allocated_register_count <= fixed_register_base);
  const uint32_t allocated_state_count =
      iree_host_align(allocated_register_count, 2);
  const uint32_t fixed_state_base = fixed_register_base & ~1u;
  const uint32_t fixed_state_count =
      fixed_register_count == 0
          ? 0
          : iree_host_align(
                fixed_register_base - fixed_state_base + fixed_register_count,
                2);
  const iree_host_size_t state_count =
      (iree_host_size_t)allocated_state_count + fixed_state_count;
  if (state_count == 0) {
    return iree_ok_status();
  }
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, state_count, sizeof(*out_hazard->registers),
      (void**)&out_hazard->registers));
  memset(out_hazard->registers, 0,
         state_count * sizeof(*out_hazard->registers));
  out_hazard->state_count = state_count;
  out_hazard->allocated_register_count = allocated_register_count;
  out_hazard->allocated_state_count = allocated_state_count;
  out_hazard->fixed_state_base = fixed_state_base;
  return iree_ok_status();
}

void loom_amdgpu_sgpr_read_hazard_begin_block(
    loom_amdgpu_sgpr_read_hazard_t* hazard) {
  if (!loom_amdgpu_sgpr_read_hazard_is_initialized(hazard)) {
    return;
  }
  memset(hazard->registers, 0,
         hazard->state_count * sizeof(*hazard->registers));
}

void loom_amdgpu_sgpr_read_hazard_clear_dependencies(
    loom_amdgpu_sgpr_read_hazard_t* hazard) {
  if (!loom_amdgpu_sgpr_read_hazard_is_initialized(hazard)) {
    return;
  }
  for (iree_host_size_t i = 0; i < hazard->state_count; ++i) {
    hazard->registers[i].flags &=
        ~(LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_SCALAR_WRITE |
          LOOM_AMDGPU_SGPR_READ_HAZARD_REGISTER_FLAG_VECTOR_WRITE);
    hazard->registers[i].origin = UINT32_MAX;
  }
}
