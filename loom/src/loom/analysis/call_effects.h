// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Retained access and escape effects at synchronous callable boundaries.

#ifndef LOOM_ANALYSIS_CALL_EFFECTS_H_
#define LOOM_ANALYSIS_CALL_EFFECTS_H_

#include "iree/base/api.h"
#include "iree/base/internal/arena.h"
#include "loom/ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

enum loom_call_argument_effect_bits_e {
  // The callee may read bytes reached through this argument.
  LOOM_CALL_ARGUMENT_READ = 1u << 0,
  // The callee may write bytes reached through this argument.
  LOOM_CALL_ARGUMENT_WRITE = 1u << 1,
  // Access may outlive the call, or reference transport is not fully modeled.
  // This includes returned aliases, publication, and asynchronous accesses.
  LOOM_CALL_ARGUMENT_ESCAPE = 1u << 2,
};
typedef uint8_t loom_call_argument_effects_t;

typedef struct loom_call_effect_summary_t {
  // Effects in formal argument order; scalar arguments have no effects.
  const loom_call_argument_effects_t* arguments;
  // Number of entries in |arguments|.
  uint16_t argument_count;
  // The body may access memory without an identified reference operand.
  bool has_unknown_memory_access;
} loom_call_effect_summary_t;

typedef struct loom_call_effects_t loom_call_effects_t;

// Builds callable argument summaries before source bodies are rewritten.
//
// The analysis owns one reference-use traversal and a monotone effect graph.
// Argument/alias edges propagate effects to a fixed point, including recursive
// calls. Unmodeled uses remain escaping; a synchronous call alone does not
// establish a no-capture contract. No source facts or IR pointers survive in
// the result. The module ordinal scratch must be unacquired during
// construction.
//
// The summaries belong to |arena| and remain valid across source-to-Low body
// replacement preserving symbol identity and callable semantics. Other body,
// signature, or symbol-table changes invalidate them. Modules containing only
// kernels or native functions require no graph and return NULL.
iree_status_t loom_call_effects_analyze_module(
    loom_module_t* module, iree_arena_allocator_t* arena,
    loom_call_effects_t** out_effects);

// Returns the retained summary, or NULL for an opaque/bodyless callable.
// A NULL analysis conservatively supplies no callable proof.
const loom_call_effect_summary_t* loom_call_effects_lookup(
    const loom_call_effects_t* effects, loom_symbol_ref_t callee);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_ANALYSIS_CALL_EFFECTS_H_
