// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_ANALYSIS_CHANNEL_HANDOFF_H_
#define LOOM_ANALYSIS_CHANNEL_HANDOFF_H_

#include "loom/analysis/channel_plan.h"
#include "loom/util/cfg_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

// One candidate ordered transport from a locally produced FIFO. The enclosing
// communication plan establishes that this worker owns all local publication
// and acceptance, and that asynchronous source retirement gates slot reuse.
typedef struct loom_channel_handoff_t {
  // Protocol identity of the locally produced, accepted and copied records.
  loom_value_id_t source_channel;
  // Protocol identity receiving each current record's asynchronous copy.
  loom_value_id_t destination_channel;
  // Positive specialized source slot count, independent of physical addresses.
  uint64_t capacity;
} loom_channel_handoff_t;

typedef enum loom_channel_handoff_rejection_kind_e {
  LOOM_CHANNEL_HANDOFF_REJECTION_NONE = 0,
  // An action requires a different transport or an expanded callable body.
  LOOM_CHANNEL_HANDOFF_REJECTION_ACTION,
  // Local reservation/publication/acceptance is not an ordered handoff.
  LOOM_CHANNEL_HANDOFF_REJECTION_PUBLICATION,
  // An input FIFO needs more than one independently carried read cursor.
  LOOM_CHANNEL_HANDOFF_REJECTION_INPUT_OVERLAP,
  // The transfer destination needs independently carried producer reservations.
  LOOM_CHANNEL_HANDOFF_REJECTION_DESTINATION_OVERLAP,
  // Reusing the next slot would overwrite a still-owned local history read.
  LOOM_CHANNEL_HANDOFF_REJECTION_HISTORY_ALIAS,
  // Copy does not transfer the current local record to the selected endpoint.
  LOOM_CHANNEL_HANDOFF_REJECTION_TRANSFER,
  // Incoming control paths disagree about publication or retained records.
  LOOM_CHANNEL_HANDOFF_REJECTION_CONTROL_JOIN,
  // Local protocol state escapes the worker and needs a persistent carrier.
  LOOM_CHANNEL_HANDOFF_REJECTION_LIVE_EXIT,
} loom_channel_handoff_rejection_kind_t;

typedef struct loom_channel_handoff_rejection_t {
  // Why this realization cannot erase local protocol bookkeeping.
  loom_channel_handoff_rejection_kind_t kind;
  // Source action or control edge at which the requirement becomes visible.
  const loom_op_t* op;
  // Conflicting retained read, or INVALID when the requirement has no one
  // value.
  loom_value_id_t value_id;
} loom_channel_handoff_rejection_t;

// Proves eligibility for local publication/acceptance and history bookkeeping
// to disappear into a sequential worker with an independently credited ordered
// transport. The channel plan and CFG describe the same immutable region; the
// plan's value domain remains acquired. Source ownership is already verified.
//
// The proof retains exact record distances across CFG edges and loops. Fanout
// keeps distinct obligations; issuing a copy transfers only that obligation
// to transport. Other reads must end before their slots recur. Destination
// reservations are individually matched to the copy consuming them. The proof
// establishes neither DMA completion nor destination visibility: selected
// transport must enforce both, including a final drain at execution completion.
// Matching protocol states at joins are a requirement of this realization,
// not a validity rule for channels in general. Channel-aware calls must already
// be expanded; borrowed-view calls impose no channel protocol requirements.
//
// Status reports allocation failure. A rejection requests another realization;
// it does not diagnose invalid source or authorize extra synchronization.
iree_status_t loom_channel_handoff_analyze(
    const loom_channel_plan_t* plan, const loom_cfg_graph_t* graph,
    const loom_channel_handoff_t* handoff, iree_arena_allocator_t* arena,
    loom_channel_handoff_rejection_t* out_rejection);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_ANALYSIS_CHANNEL_HANDOFF_H_
