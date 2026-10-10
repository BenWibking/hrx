// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// AMDGPU lowering for target-low spill traffic.
//
// Generic target-low allocation materialization represents spills with
// structural low.spill/low.reload ops and function-local storage handles. This
// target-owned layer lowers that representation into the concrete AMDGPU
// private-segment scratch packets selected from the active descriptor set.

#ifndef LOOM_TARGET_EMIT_NATIVE_AMDGPU_SPILL_LOWERING_H_
#define LOOM_TARGET_EMIT_NATIVE_AMDGPU_SPILL_LOWERING_H_

#include "iree/base/api.h"
#include "iree/base/internal/arena.h"
#include "loom/codegen/low/descriptors.h"
#include "loom/error/emitter.h"
#include "loom/ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_amdgpu_spill_lowering_result_t {
  // Number of user-facing diagnostics emitted while lowering spill traffic.
  uint32_t error_count;
  // Scratch-arena-owned value IDs created or newly exposed by spill lowering
  // that must remain in registers during subsequent allocation rounds.
  const loom_value_id_t* required_register_value_ids;
  // Number of entries in |required_register_value_ids|.
  iree_host_size_t required_register_value_count;
} loom_amdgpu_spill_lowering_result_t;

// Rewrites low.spill/low.reload in |function_op| into descriptor-backed AMDGPU
// scratch load/store packets. The descriptor set must be the already-selected
// target-low descriptor set for the function target.
// Direction of a scratch packet.
typedef enum loom_amdgpu_scratch_access_kind_e {
  // The packet does not access scratch through a spill-lowering form.
  LOOM_AMDGPU_SCRATCH_ACCESS_NONE = 0,
  // The packet loads from scratch.
  LOOM_AMDGPU_SCRATCH_ACCESS_LOAD = 1,
  // The packet stores to scratch.
  LOOM_AMDGPU_SCRATCH_ACCESS_STORE = 2,
} loom_amdgpu_scratch_access_kind_t;

// Classifies |descriptor| as one of the scratch load or store forms this
// lowering emits. Private-memory lowering shares these forms, so a match
// identifies scratch traffic, not necessarily a spill.
loom_amdgpu_scratch_access_kind_t
loom_amdgpu_spill_lowering_classify_descriptor(
    const loom_low_descriptor_set_t* descriptor_set,
    const loom_low_descriptor_t* descriptor);

iree_status_t loom_amdgpu_lower_spill_traffic(
    loom_module_t* module, loom_op_t* function_op,
    const loom_low_descriptor_set_t* descriptor_set,
    iree_diagnostic_emitter_t emitter,
    loom_amdgpu_spill_lowering_result_t* out_result,
    iree_arena_allocator_t* scratch_arena);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_EMIT_NATIVE_AMDGPU_SPILL_LOWERING_H_
