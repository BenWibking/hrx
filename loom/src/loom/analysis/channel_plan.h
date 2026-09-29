// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Channel membership retained independently of storage and record ownership.

#ifndef LOOM_ANALYSIS_CHANNEL_PLAN_H_
#define LOOM_ANALYSIS_CHANNEL_PLAN_H_

#include "loom/ir/local_value_domain.h"

#ifdef __cplusplus
extern "C" {
#endif

// A source argument or capture bound by the enclosing communication plan.
// This correspondence comes from callable/capture substitution, never from
// equal addresses, storage roots, argument names, or payload types.
typedef struct loom_channel_plan_binding_t {
  // Channel or owned access in the analyzed region's value domain.
  loom_value_id_t value_id;
  // Channel identity in the enclosing plan. Several arguments may select it.
  loom_value_id_t channel_value_id;
} loom_channel_plan_binding_t;

// A visible callable's channel membership in formal argument coordinates.
// The enclosing plan retains its body for physical specialization. This is not
// an ownership or effect summary: returning a read from an argument's channel
// does not imply returning that argument's particular record or obligation.
typedef struct loom_channel_plan_callable_t {
  // Same-module definition whose source body remains available.
  loom_op_t* function;
  // Formal argument supplying each returned channel/read/write's membership;
  // UINT16_MAX for an ordinary result. Indexed by function result ordinal.
  const uint16_t* result_arguments;
} loom_channel_plan_callable_t;

// One operation's channel identities, established before carrier rewriting.
typedef struct loom_channel_plan_action_t {
  // Source operation, borrowed until the consuming rewrite erases it.
  loom_op_t* op;
  // Channel of the first operand, or the result for channel.bind; INVALID for
  // a call, whose actual arguments have independent entries in the value table.
  loom_value_id_t channel_value_id;
  // Destination channel for channel.copy; INVALID for other operations.
  loom_value_id_t destination_channel_value_id;
  // Retained formal summary for a channel-aware call; NULL for channel ops.
  const loom_channel_plan_callable_t* callable;
} loom_channel_plan_action_t;

typedef enum loom_channel_plan_rejection_kind_e {
  LOOM_CHANNEL_PLAN_REJECTION_NONE = 0,
  // A call or other operation still needs a channel-aware representation.
  LOOM_CHANNEL_PLAN_REJECTION_OPAQUE_USE = 1,
  // Control chooses between different channels and needs a dynamic carrier.
  LOOM_CHANNEL_PLAN_REJECTION_DYNAMIC_CHANNEL = 2,
  // An incoming access lacks its enclosing plan's channel correspondence.
  LOOM_CHANNEL_PLAN_REJECTION_UNBOUND_ACCESS = 3,
} loom_channel_plan_rejection_kind_t;

typedef struct loom_channel_plan_rejection_t {
  // Requirement that prevented this fixed-channel realization.
  loom_channel_plan_rejection_kind_t kind;
  // Authored operation requiring the unsupported behavior, if present.
  const loom_op_t* op;
  // Channel or access whose binding could not be retained.
  loom_value_id_t value_id;
} loom_channel_plan_rejection_t;

typedef struct loom_channel_plan_t {
  // Borrowed domain retained by the owner through materialization.
  const loom_local_value_domain_t* value_domain;
  // Direct channel identity by value ordinal; INVALID for unrelated values.
  // Equal entries mean membership only: records and consuming obligations
  // remain distinct SSA values with their original lifetime requirements.
  const loom_value_id_t* channel_value_ids;
  // Original domain extent, excluding values created during materialization.
  loom_value_ordinal_t value_count;
  // Arena-owned channel actions in block/source order.
  const loom_channel_plan_action_t* actions;
  // Number of retained actions.
  iree_host_size_t action_count;
  // Function exits collected with the source operations.
  loom_op_t* const* returns;
  // Number of retained exits.
  iree_host_size_t return_count;
} loom_channel_plan_t;

// Builds fixed-channel membership for one verified flat execution region.
// SCF policies and control lowering precede this boundary. The enclosing plan
// supplies the identity of every incoming channel/read/write argument; a local
// channel.bind creates a fresh identity independent of its backing allocation.
//
// A single collection owns the channel actions and forwarding edges. The
// resulting direct table includes loop-carried accesses and fanout results.
// |callables| is an optional symbol-indexed table of previously summarized
// definitions, owned by the enclosing call-graph plan. Calls carrying channels
// or accesses require an entry there; ordinary borrowed-view calls are opaque
// to this analysis. Dynamic choices between channels are reported explicitly,
// rather than merged because their storage or payload shapes happen to match.
//
// Status reports allocation failures. A non-NONE rejection describes authored
// behavior needing another realization; no partial plan is published. The
// plan is invalidated by source mutation, except its consuming rewrite may use
// the retained identities/actions while replacing their physical carriers.
iree_status_t loom_channel_plan_build(
    const loom_local_value_domain_t* value_domain,
    const loom_channel_plan_binding_t* bindings, iree_host_size_t binding_count,
    const loom_channel_plan_callable_t* const* callables,
    iree_arena_allocator_t* arena, loom_channel_plan_t* out_plan,
    loom_channel_plan_rejection_t* out_rejection);

// Summarizes an already analyzed callable without walking its body again.
// Build the formal plan with each incoming channel/read/write bound to its own
// argument ID. All returned communication values must belong to a formal
// argument's channel; a dynamically chosen or locally created escaping channel
// needs a runtime channel carrier and produces DYNAMIC_CHANNEL. Ordinary
// returned values need no membership. The summary outlives the local domain.
iree_status_t loom_channel_plan_summarize(
    const loom_channel_plan_t* plan, loom_op_t* function,
    iree_arena_allocator_t* arena, loom_channel_plan_callable_t* out_callable,
    loom_channel_plan_rejection_t* out_rejection);

// Direct membership query for an original channel/read/write value.
static inline loom_value_id_t loom_channel_plan_channel(
    const loom_channel_plan_t* plan, loom_value_id_t value_id) {
  return plan->channel_value_ids[loom_local_value_domain_ordinal(
      plan->value_domain, value_id)];
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_ANALYSIS_CHANNEL_PLAN_H_
