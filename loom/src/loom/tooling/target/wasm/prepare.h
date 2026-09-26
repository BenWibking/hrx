// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Compiler-owned preparation for WebAssembly binary emission.

#ifndef LOOM_TOOLING_TARGET_WASM_PREPARE_H_
#define LOOM_TOOLING_TARGET_WASM_PREPARE_H_

#include "iree/base/api.h"
#include "iree/base/internal/arena.h"
#include "loom/codegen/low/descriptors.h"
#include "loom/error/emitter.h"
#include "loom/ir/ir.h"
#include "loom/target/emit/wasm/module_binary.h"
#include "loom/target/emit/wasm/program.h"

#ifdef __cplusplus
extern "C" {
#endif

// Allocates structured Low functions and projects their final physical facts
// into an arena-owned WebAssembly program plan. Semantic allocation rejection
// returns OK with |out_accepted| false. Infrastructure failures return a
// status and also leave the plan empty.
iree_status_t loom_wasm_program_plan_prepare(
    loom_module_t* module,
    const loom_low_descriptor_registry_t* descriptor_registry,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    bool* out_accepted, loom_wasm_program_plan_t* out_plan);

// Prepares and emits one WebAssembly module through the production boundary.
// Semantic rejection returns OK with |out_emitted| false and no binary.
iree_status_t loom_wasm_compile_module_binary(
    loom_module_t* module,
    const loom_low_descriptor_registry_t* descriptor_registry,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    iree_allocator_t allocator, bool* out_emitted,
    loom_wasm_module_binary_t* out_module);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLING_TARGET_WASM_PREPARE_H_
