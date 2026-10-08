// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Generic arity-changing construction for LoopLike operations.

#ifndef LOOM_REWRITE_LOOP_LIKE_H_
#define LOOM_REWRITE_LOOP_LIKE_H_

#include "iree/base/api.h"
#include "iree/base/internal/arena.h"
#include "loom/ops/op_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// State endpoints used to build a replacement for an existing LoopLike op.
typedef struct loom_loop_like_replacement_state_t {
  // New values entering the loop header.
  //
  // For counted loops these also correspond positionally to |result_types|.
  // For condition loops they define the independent before-region tuple.
  loom_value_slice_t initial_values;

  // New condition-region entry types.
  //
  // Required for non-empty condition-loop header state and unused for counted
  // loops. Types may reference sibling replacement header IDs. Such callers
  // reserve header identities followed by result identities before invoking
  // loom_loop_like_build_replacement or loom_loop_like_prepare_replacement.
  const loom_type_t* header_types;

  // Prefix offsets from each source header-state ordinal to its replacement
  // range. The array contains source iter_args count + 1 monotonically
  // increasing entries, begins at zero, and ends at |initial_values.count|.
  // A source value remains one-to-one when adjacent offsets differ by one. May
  // be NULL when both source and replacement header state are empty.
  const uint16_t* source_header_offsets;

  // New result types. These also define the body-region entry tuple.
  //
  // Types may reference sibling replacement result IDs. Callers constructing
  // such a scheme reserve the complete identity sequence before invoking
  // loom_loop_like_build_replacement or loom_loop_like_prepare_replacement,
  // matching generated loop builder semantics. Counted loops reserve only
  // results. Condition loops reserve header identities first and result
  // identities second.
  const loom_type_t* result_types;

  // Number of new results and body-region entry values.
  uint16_t result_count;

  // Prefix offsets from each source result/body-state ordinal to its
  // replacement range. The array contains source result count + 1
  // monotonically increasing entries, begins at zero, and ends at
  // |result_count|. May be NULL when both source and replacement result state
  // are empty.
  const uint16_t* source_result_offsets;
} loom_loop_like_replacement_state_t;

// Endpoints of a newly built LoopLike operation.
typedef struct loom_loop_like_replacement_t {
  // Newly built operation and its LoopLike interface metadata.
  loom_loop_like_t loop;

  // Entry block of the condition region, or NULL for counted loops.
  loom_block_t* condition_entry;

  // Complete condition-region state tuple, or an empty slice for counted loops.
  loom_value_slice_t condition_state;

  // Entry block of the primary body region.
  loom_block_t* body_entry;

  // Complete body-region state tuple, excluding a counted induction argument.
  loom_value_slice_t body_state;

  // Replacement result tuple.
  loom_value_slice_t results;
} loom_loop_like_replacement_t;

// Builds an empty-region replacement for |source| using |state|.
//
// The replacement has the same concrete operation kind. All non-state operand
// fields, per-instance operand segmentation, attributes, ownership ties,
// semantic and presentation flags, location, comments, and region/block
// presentation are preserved through the LoopLike interface. Counted loops use
// one positional state tuple. Condition loops independently map their initial
// and before-region header state and their body-region and result state.
// One-to-one source results and region arguments retain their display names.
//
// The returned body and condition blocks are empty. The caller interprets the
// source terminators, populates replacement region operations, and completes
// replacement of source result uses. |scratch_arena| retains no output state
// and may be reset after this call.
//
// Equivalent to loom_loop_like_prepare_replacement followed immediately by
// loom_loop_like_complete_replacement with |state->initial_values|.
iree_status_t loom_loop_like_build_replacement(
    loom_builder_t* builder, loom_loop_like_t source,
    const loom_loop_like_replacement_state_t* state,
    iree_arena_allocator_t* scratch_arena,
    loom_loop_like_replacement_t* out_replacement);

// Staged replacement construction
//
// Rewrites of nested or chained loops can need a replacement's endpoints
// before its initial values exist: an inner loop may start from an outer
// replacement's body argument while the outer backedge yields the inner
// replacement's result. Staging separates endpoint definition from
// publication so a caller can prepare every replacement in a batch, consume
// their endpoints in source recipes, and then complete each one.
//
// Preparation produces a detached operation (see
// loom_builder_allocate_detached_op) whose regions, entry blocks, region
// arguments, and results all exist with final types and definitions: result
// values name the detached op and argument values name their entry block.
// Its initial state operands are unset, it is linked into no block, its
// operands have no registered uses, and no builder callback has run. Any
// builder value reservation consumed by the header and result identities is
// released, so several prepared replacements may be outstanding at once.
//
// Between preparation and completion, other operations may reference the
// prepared endpoints and the caller may populate the replacement's regions.
// Such references are transitional: an endpoint defined in a detached
// region does not dominate a use outside it until the caller moves the user
// into that region. The caller owns that interval. It must complete every
// prepared replacement and restore ordinary ownership and dominance before
// any verifier, analysis, or other pass observes the module. Builder
// finalization callbacks still run for operations published during the
// interval; callers whose callbacks inspect operand definitions (for example,
// a rewriter with an attached fact table) must not reference prepared
// endpoints from operations they publish before completion.

// Prepares a detached replacement for |source| using |state|.
//
// Produces the same operation, endpoints, names, comments, and presentation as
// loom_loop_like_build_replacement, except that only |state->initial_values|
// .count is read: the initial state operands are supplied at completion. A
// prepared replacement that is never completed remains detached arena storage
// and is never observable as program IR.
iree_status_t loom_loop_like_prepare_replacement(
    loom_builder_t* builder, loom_loop_like_t source,
    const loom_loop_like_replacement_state_t* state,
    iree_arena_allocator_t* scratch_arena,
    loom_loop_like_replacement_t* out_replacement);

// Completes a prepared replacement.
//
// Installs |initial_values| as the replacement's initial state operands, links
// the operation at |builder|'s insertion point, and finalizes it: operand uses
// are registered and the builder callback runs exactly once. |initial_values|
// must have the header count supplied at preparation. Region population may
// occur before or after completion.
iree_status_t loom_loop_like_complete_replacement(
    loom_builder_t* builder, const loom_loop_like_replacement_t* replacement,
    loom_value_slice_t initial_values);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_REWRITE_LOOP_LIKE_H_
