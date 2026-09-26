// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Compiler-owned preparation for SPIR-V binary emission.

#ifndef LOOM_TOOLING_TARGET_SPIRV_PREPARE_H_
#define LOOM_TOOLING_TARGET_SPIRV_PREPARE_H_

#include "iree/base/api.h"
#include "iree/base/internal/arena.h"
#include "loom/codegen/low/descriptors.h"
#include "loom/error/emitter.h"
#include "loom/ir/function_version.h"
#include "loom/ir/ir.h"
#include "loom/target/emit/spirv/module_builder.h"
#include "loom/target/emit/spirv/program.h"
#include "loom/target/facts.h"

#ifdef __cplusplus
extern "C" {
#endif

// One compiler-selected SPIR-V entry and its retained target facts.
typedef struct loom_spirv_compile_entry_t {
  // Verified target-low function definition.
  loom_op_t* function_op;
  // Immutable target facts already selected for |function_op|, or NULL to
  // resolve them from the function version or authored target witness.
  const loom_target_facts_t* target_facts;
} loom_spirv_compile_entry_t;

typedef struct loom_spirv_compile_options_t {
  // Optional compiler-owned function versions participating in preparation.
  const loom_function_version_list_t* function_versions;
  // Optional selected entry table in emission order. NULL prepares every
  // SPIR-V Low function in source module order.
  const loom_spirv_compile_entry_t* entries;
  // Number of entries in |entries|. Zero selects from the module.
  iree_host_size_t entry_count;
} loom_spirv_compile_options_t;

void loom_spirv_compile_options_initialize(
    loom_spirv_compile_options_t* out_options);

// Resolves selected entries and projects their immutable target bindings into
// an arena-owned SPIR-V program. Structured semantic rejection returns OK with
// |out_accepted| false. Infrastructure failures return a status and leave the
// plan empty.
iree_status_t loom_spirv_program_plan_prepare(
    loom_module_t* module,
    const loom_low_descriptor_registry_t* descriptor_registry,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    const loom_spirv_compile_options_t* options, bool* out_accepted,
    loom_spirv_program_plan_t* out_plan);

// Prepares and emits one SPIR-V module through the production boundary.
// Structured semantic rejection returns OK with |out_emitted| false and no
// binary.
iree_status_t loom_spirv_compile_module_binary(
    loom_module_t* module,
    const loom_low_descriptor_registry_t* descriptor_registry,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    const loom_spirv_compile_options_t* options, iree_allocator_t allocator,
    bool* out_emitted, loom_spirv_module_binary_t* out_module);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLING_TARGET_SPIRV_PREPARE_H_
