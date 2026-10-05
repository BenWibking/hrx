// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_TRANSFORMS_PIPELINE_CHANNEL_MATERIALIZATION_H_
#define LOOM_TRANSFORMS_PIPELINE_CHANNEL_MATERIALIZATION_H_

#include "loom/analysis/channel_plan.h"
#include "loom/rewrite/rewriter.h"

#ifdef __cplusplus
extern "C" {
#endif

// Emission mechanics for one already selected channel realization. Selection
// proves legality and retains resource bindings before this consuming rewrite.
typedef struct loom_channel_materialization_callback_t {
  // Emits immediately before action->op. Its operands already contain the
  // physical carriers substituted by previous actions. The retained action
  // supplies protocol identities independently of those carriers' addresses.
  // Writes one replacement per materialized result to results (erased result
  // slots are ignored) and updates state with successor cursor/control values.
  // The callback inserts ordinary operations but leaves the source operation
  // and containing CFG intact;
  // this common owner replaces results, erases actions and threads CFG state.
  // Status carries allocation or emission failure, not realization selection.
  iree_status_t (*fn)(void* user_data, loom_rewriter_t* rewriter,
                      const loom_channel_plan_action_t* action,
                      loom_value_id_t* state, iree_host_size_t state_count,
                      loom_value_id_t* results);
  // Optional execution-exit mechanics emitted before an original func.return,
  // after all actions in its block. Receives the path's final protocol state.
  // The realization owner selects this for an execution boundary that owns
  // outstanding transfers; an ordinary helper return does not imply a drain.
  // Leaves the return and CFG intact. NULL emits no completion operation.
  iree_status_t (*exit)(void* user_data, loom_rewriter_t* rewriter,
                        const loom_op_t* terminator,
                        const loom_value_id_t* state,
                        iree_host_size_t state_count);
  // Borrowed selected bindings, helper symbols and target emission context.
  void* user_data;
} loom_channel_materialization_callback_t;

typedef struct loom_channel_materialization_options_t {
  // Carrier type by original value ordinal, or a null type when unchanged.
  // Block arguments change here; operation results are replaced by emission.
  // The boundary owner also realizes function result types and all callsites.
  const loom_type_t* carrier_types;
  // Optional byte per original value ordinal selecting zero-bit carriers.
  // Every use must be a retained action or canonical CFG transport. Selected
  // emission cannot use these values; their ownership effects become code or
  // are discharged by the realization proof, never by a fabricated SSA value.
  const uint8_t* erased_values;
  // Initial private protocol state, available before the region's first action.
  // These are actual cursor/control values, not synthetic dependency tokens.
  const loom_value_id_t* initial_state;
  // Number of independently threaded SSA state values; zero needs no CFG edit.
  iree_host_size_t state_count;
  // Mechanics selected for every retained action, including channel-aware
  // calls.
  loom_channel_materialization_callback_t emit;
} loom_channel_materialization_options_t;

// Consumes a fixed-channel plan into ordinary executable IR. The plan's local
// value domain stays acquired; queries are only for original source values.
// Other source analyses are invalidated before this batch. Intermediate source
// operations can still refer to carriers whose types have already changed.
// Canonical CFG branches carry updated state through loops and reconvergence.
// Conditional edges receive ordinary forwarding blocks, subsequently removable
// by shared CFG simplification. Exit mechanics see the same path-specific
// state. The common owner imposes no synchronization or global drain.
//
// Source ownership and realization legality have already been established.
// The caller owns callable signature conversion and dead channel-argument
// pruning after all consumers have been realized. No fabricated channel value
// is introduced to keep an erased protocol argument alive.
iree_status_t loom_channel_materialize(
    loom_rewriter_t* rewriter, const loom_channel_plan_t* plan,
    const loom_channel_materialization_options_t* options);

#ifdef __cplusplus
}
#endif

#endif  // LOOM_TRANSFORMS_PIPELINE_CHANNEL_MATERIALIZATION_H_
