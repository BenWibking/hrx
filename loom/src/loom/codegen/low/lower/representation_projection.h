// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Projection of authored Low representations into selected target contracts.

#ifndef LOOM_CODEGEN_LOW_LOWER_REPRESENTATION_PROJECTION_H_
#define LOOM_CODEGEN_LOW_LOWER_REPRESENTATION_PROJECTION_H_

#include "iree/base/api.h"
#include "iree/base/internal/arena.h"
#include "loom/codegen/low/descriptors.h"
#include "loom/error/emitter.h"
#include "loom/ir/ir.h"
#include "loom/ops/op_defs.h"
#include "loom/target/facts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_low_representation_projection_plan_t
    loom_low_representation_projection_plan_t;

// Plans register types and descriptor packets for |target_facts|' exact
// representation without changing the authored function. The retained plan
// belongs to |arena|; construction scratch is released before returning. User
// contract failures emit diagnostics and return a NULL plan. Status reports
// infrastructure failures. The function must remain unchanged until execution.
iree_status_t loom_low_plan_function_representation(
    loom_module_t* module, loom_func_like_t function,
    const loom_target_facts_t* target_facts,
    const loom_low_descriptor_registry_t* descriptor_registry,
    iree_diagnostic_emitter_t emitter, iree_arena_allocator_t* arena,
    const loom_low_representation_projection_plan_t** out_plan);

// Applies the complete projection, including the function's representation
// attribute and descriptor effect summaries. No compatibility or type mapping
// decisions remain; only allocation failure can interrupt publication.
iree_status_t loom_low_apply_function_representation(
    loom_module_t* module,
    const loom_low_representation_projection_plan_t* plan, bool* out_changed);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_CODEGEN_LOW_LOWER_REPRESENTATION_PROJECTION_H_
