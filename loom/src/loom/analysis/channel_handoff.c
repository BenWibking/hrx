// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/analysis/channel_handoff.h"

#include <stdlib.h>
#include <string.h>

#include "loom/ops/channel/ops.h"
#include "loom/ops/type_registry.h"

typedef enum loom_channel_handoff_publication_e {
  LOOM_CHANNEL_HANDOFF_AVAILABLE = 0,
  LOOM_CHANNEL_HANDOFF_WRITING,
  LOOM_CHANNEL_HANDOFF_PUBLISHED,
  LOOM_CHANNEL_HANDOFF_ACCEPTED,
} loom_channel_handoff_publication_t;

typedef struct loom_channel_handoff_read_t {
  // Particular owned obligation, distinct even for the same record.
  loom_value_id_t value;
  // Number of copied records since this local read was accepted; zero for an
  // unrelated input read, whose cursor is independently gated by release.
  uint64_t distance;
} loom_channel_handoff_read_t;

typedef struct loom_channel_handoff_state_t {
  // Progress of the one current local record.
  loom_channel_handoff_publication_t publication;
  // Outstanding producer obligation at the ordered transfer destination.
  loom_value_id_t destination_write;
  // Live owned reads; snapshots are sorted by value ID for exact joins.
  loom_channel_handoff_read_t* reads;
  // Number of live reads, not the source value-domain size.
  iree_host_size_t read_count;
} loom_channel_handoff_state_t;

typedef struct loom_channel_handoff_block_t {
  // First retained source action in the block.
  iree_host_size_t action_offset;
  // Number of retained source actions in the block.
  iree_host_size_t action_count;
  // Exact first incoming protocol state, retained for subsequent edge checks.
  loom_channel_handoff_state_t input;
  // Whether an entry-reachable predecessor has established the input state.
  bool reached;
} loom_channel_handoff_block_t;

static bool loom_channel_handoff_reject(
    loom_channel_handoff_rejection_kind_t kind, const loom_op_t* op,
    loom_value_id_t value, loom_channel_handoff_rejection_t* rejection) {
  *rejection = (loom_channel_handoff_rejection_t){kind, op, value};
  return false;
}

static iree_host_size_t loom_channel_handoff_find_read(
    const loom_channel_handoff_state_t* state, loom_value_id_t value) {
  for (iree_host_size_t i = 0; i < state->read_count; ++i) {
    if (state->reads[i].value == value) {
      return i;
    }
  }
  return IREE_HOST_SIZE_MAX;
}

static void loom_channel_handoff_remove_read(
    loom_channel_handoff_state_t* state, iree_host_size_t index) {
  state->reads[index] = state->reads[--state->read_count];
}

static bool loom_channel_handoff_apply(
    const loom_channel_plan_t* plan, const loom_channel_handoff_t* handoff,
    const loom_channel_plan_action_t* action,
    loom_channel_handoff_state_t* state,
    loom_channel_handoff_rejection_t* rejection) {
  const loom_op_t* op = action->op;
  if (action->callable) {
    return loom_channel_handoff_reject(LOOM_CHANNEL_HANDOFF_REJECTION_ACTION,
                                       op, LOOM_VALUE_ID_INVALID, rejection);
  }
  const loom_value_id_t value = loom_op_operands(op)[0];
  const iree_host_size_t read = loom_channel_handoff_find_read(state, value);
  if (action->channel_value_id != handoff->source_channel) {
    if (loom_channel_acquire_isa(op)) {
      for (iree_host_size_t i = 0; i < state->read_count; ++i) {
        if (loom_channel_plan_channel(plan, state->reads[i].value) ==
            action->channel_value_id) {
          return loom_channel_handoff_reject(
              LOOM_CHANNEL_HANDOFF_REJECTION_INPUT_OVERLAP, op,
              state->reads[i].value, rejection);
        }
      }
      state->reads[state->read_count++] =
          (loom_channel_handoff_read_t){loom_op_results(op)[0], 0};
    } else if (loom_channel_release_isa(op) && read != IREE_HOST_SIZE_MAX) {
      loom_channel_handoff_remove_read(state, read);
    } else if (loom_channel_reserve_isa(op) &&
               action->channel_value_id == handoff->destination_channel) {
      if (state->destination_write != LOOM_VALUE_ID_INVALID) {
        return loom_channel_handoff_reject(
            LOOM_CHANNEL_HANDOFF_REJECTION_DESTINATION_OVERLAP, op,
            state->destination_write, rejection);
      }
      state->destination_write = loom_channel_reserve_write(op);
    } else {
      return loom_channel_handoff_reject(LOOM_CHANNEL_HANDOFF_REJECTION_ACTION,
                                         op, value, rejection);
    }
    return true;
  }

  if (loom_channel_reserve_isa(op)) {
    if (state->publication != LOOM_CHANNEL_HANDOFF_AVAILABLE) {
      return loom_channel_handoff_reject(
          LOOM_CHANNEL_HANDOFF_REJECTION_PUBLICATION, op, value, rejection);
    }
    for (iree_host_size_t i = 0; i < state->read_count; ++i) {
      if (loom_channel_plan_channel(plan, state->reads[i].value) ==
              handoff->source_channel &&
          state->reads[i].distance % handoff->capacity == 0) {
        return loom_channel_handoff_reject(
            LOOM_CHANNEL_HANDOFF_REJECTION_HISTORY_ALIAS, op,
            state->reads[i].value, rejection);
      }
    }
    state->publication = LOOM_CHANNEL_HANDOFF_WRITING;
  } else if (loom_channel_publish_isa(op)) {
    if (state->publication != LOOM_CHANNEL_HANDOFF_WRITING) {
      return loom_channel_handoff_reject(
          LOOM_CHANNEL_HANDOFF_REJECTION_PUBLICATION, op, value, rejection);
    }
    state->publication = LOOM_CHANNEL_HANDOFF_PUBLISHED;
  } else if (loom_channel_accept_isa(op)) {
    if (state->publication != LOOM_CHANNEL_HANDOFF_PUBLISHED) {
      return loom_channel_handoff_reject(
          LOOM_CHANNEL_HANDOFF_REJECTION_PUBLICATION, op, value, rejection);
    }
    state->publication = LOOM_CHANNEL_HANDOFF_ACCEPTED;
    state->reads[state->read_count++] =
        (loom_channel_handoff_read_t){loom_op_results(op)[0], 0};
  } else if (loom_channel_fanout_isa(op) && read != IREE_HOST_SIZE_MAX) {
    const uint64_t distance = state->reads[read].distance;
    loom_channel_handoff_remove_read(state, read);
    for (uint16_t i = 0; i < op->result_count; ++i) {
      state->reads[state->read_count++] =
          (loom_channel_handoff_read_t){loom_op_results(op)[i], distance};
    }
  } else if (loom_channel_copy_isa(op)) {
    if (action->destination_channel_value_id != handoff->destination_channel ||
        loom_channel_copy_destination(op) != state->destination_write ||
        state->publication != LOOM_CHANNEL_HANDOFF_ACCEPTED ||
        read == IREE_HOST_SIZE_MAX || state->reads[read].distance != 0) {
      return loom_channel_handoff_reject(
          LOOM_CHANNEL_HANDOFF_REJECTION_TRANSFER, op, value, rejection);
    }
    state->destination_write = LOOM_VALUE_ID_INVALID;
    loom_channel_handoff_remove_read(state, read);
    for (iree_host_size_t i = 0; i < state->read_count; ++i) {
      if (loom_channel_plan_channel(plan, state->reads[i].value) ==
          handoff->source_channel) {
        ++state->reads[i].distance;
      }
    }
    state->publication = LOOM_CHANNEL_HANDOFF_AVAILABLE;
  } else if (loom_channel_wait_isa(op) && read != IREE_HOST_SIZE_MAX) {
    // This read's dominating local publication establishes readiness.
  } else if (loom_channel_release_isa(op) && read != IREE_HOST_SIZE_MAX) {
    loom_channel_handoff_remove_read(state, read);
  } else {
    return loom_channel_handoff_reject(LOOM_CHANNEL_HANDOFF_REJECTION_ACTION,
                                       op, value, rejection);
  }
  return true;
}

static int loom_channel_handoff_compare_reads(const void* lhs,
                                              const void* rhs) {
  const loom_value_id_t a = ((const loom_channel_handoff_read_t*)lhs)->value;
  const loom_value_id_t b = ((const loom_channel_handoff_read_t*)rhs)->value;
  return (a > b) - (a < b);
}

static bool loom_channel_handoff_states_equal(
    const loom_channel_handoff_state_t* lhs,
    const loom_channel_handoff_state_t* rhs) {
  if (lhs->publication != rhs->publication ||
      lhs->destination_write != rhs->destination_write ||
      lhs->read_count != rhs->read_count) {
    return false;
  }
  for (iree_host_size_t i = 0; i < lhs->read_count; ++i) {
    if (lhs->reads[i].value != rhs->reads[i].value ||
        lhs->reads[i].distance != rhs->reads[i].distance) {
      return false;
    }
  }
  return true;
}

iree_status_t loom_channel_handoff_analyze(
    const loom_channel_plan_t* plan, const loom_cfg_graph_t* graph,
    const loom_channel_handoff_t* handoff, iree_arena_allocator_t* arena,
    loom_channel_handoff_rejection_t* out_rejection) {
  *out_rejection = (loom_channel_handoff_rejection_t){
      .value_id = LOOM_VALUE_ID_INVALID,
  };
  const loom_local_value_domain_t* domain = plan->value_domain;
  const loom_block_t* entry = loom_region_const_entry_block(domain->region);
  for (uint16_t i = 0; i < entry->arg_count; ++i) {
    const loom_type_t type =
        loom_module_value_type(domain->module, entry->arg_ids[i]);
    if (loom_read_type_isa(type) || loom_write_type_isa(type)) {
      loom_channel_handoff_reject(LOOM_CHANNEL_HANDOFF_REJECTION_ACTION,
                                  entry->first_op->parent_op, entry->arg_ids[i],
                                  out_rejection);
      return iree_ok_status();
    }
  }
  iree_host_size_t read_capacity = 0;
  for (loom_value_ordinal_t i = 0; i < plan->value_count; ++i) {
    read_capacity += loom_read_type_isa(
        loom_module_value_type(domain->module, domain->value_ids[i]));
  }
  loom_channel_handoff_read_t* current_reads = NULL;
  loom_channel_handoff_read_t* edge_reads = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, read_capacity, sizeof(*current_reads), (void**)&current_reads));
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, read_capacity, sizeof(*edge_reads), (void**)&edge_reads));
  loom_channel_handoff_block_t* blocks = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, graph->block_count, sizeof(*blocks), (void**)&blocks));
  uint16_t* worklist = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, graph->block_count, sizeof(*worklist), (void**)&worklist));
  iree_host_size_t action_index = 0;
  for (iree_host_size_t b = 0; b < graph->block_count; ++b) {
    blocks[b] = (loom_channel_handoff_block_t){
        .action_offset = action_index,
        .input = {.destination_write = LOOM_VALUE_ID_INVALID},
    };
    while (action_index < plan->action_count &&
           plan->actions[action_index].op->parent_block ==
               graph->blocks[b].block) {
      ++action_index;
    }
    blocks[b].action_count = action_index - blocks[b].action_offset;
  }
  iree_host_size_t read_position = 0;
  iree_host_size_t write_position = 1;
  worklist[0] = 0;
  blocks[0].reached = true;
  while (read_position < write_position) {
    const uint16_t block_index = worklist[read_position++];
    const loom_channel_handoff_block_t* block = &blocks[block_index];
    loom_channel_handoff_state_t current = block->input;
    current.reads = current_reads;
    if (current.read_count) {
      memcpy(current_reads, block->input.reads,
             current.read_count * sizeof(*current_reads));
    }
    for (iree_host_size_t i = 0; i < block->action_count; ++i) {
      if (!loom_channel_handoff_apply(plan, handoff,
                                      &plan->actions[block->action_offset + i],
                                      &current, out_rejection)) {
        return iree_ok_status();
      }
    }
    const loom_cfg_edge_index_span_t edges =
        loom_cfg_graph_successor_edges(graph, block_index);
    if (!edges.count &&
        (current.publication != LOOM_CHANNEL_HANDOFF_AVAILABLE ||
         current.destination_write != LOOM_VALUE_ID_INVALID ||
         current.read_count)) {
      loom_channel_handoff_reject(LOOM_CHANNEL_HANDOFF_REJECTION_LIVE_EXIT,
                                  graph->blocks[block_index].block->last_op,
                                  current.read_count
                                      ? current.reads[0].value
                                      : current.destination_write,
                                  out_rejection);
      return iree_ok_status();
    }
    for (iree_host_size_t i = 0; i < edges.count; ++i) {
      const loom_cfg_edge_info_t* edge = &graph->edges[edges.values[i]];
      const loom_block_t* destination =
          graph->blocks[edge->target_block_index].block;
      loom_channel_handoff_state_t next = current;
      next.reads = edge_reads;
      if (next.read_count) {
        memcpy(edge_reads, current_reads,
               next.read_count * sizeof(*edge_reads));
      }
      const loom_value_id_t* arguments = NULL;
      uint16_t argument_count = 0;
      if (destination->arg_count &&
          !loom_cfg_terminator_payload_for_successor(
              edge->terminator, destination, &arguments, &argument_count)) {
        loom_channel_handoff_reject(LOOM_CHANNEL_HANDOFF_REJECTION_ACTION,
                                    edge->terminator, LOOM_VALUE_ID_INVALID,
                                    out_rejection);
        return iree_ok_status();
      }
      // Edge operands transfer simultaneously. Look up original source names
      // even when a destination is another transfer's source (loop rotation).
      for (uint16_t a = 0; a < argument_count; ++a) {
        if (arguments[a] == current.destination_write) {
          next.destination_write = destination->arg_ids[a];
        }
        const iree_host_size_t read =
            loom_channel_handoff_find_read(&current, arguments[a]);
        if (read != IREE_HOST_SIZE_MAX) {
          edge_reads[read].value = destination->arg_ids[a];
        }
      }
      if (next.read_count > 1) {
        qsort(edge_reads, next.read_count, sizeof(*edge_reads),
              loom_channel_handoff_compare_reads);
      }
      loom_channel_handoff_block_t* target = &blocks[edge->target_block_index];
      if (target->reached) {
        if (!loom_channel_handoff_states_equal(&target->input, &next)) {
          loom_channel_handoff_reject(
              LOOM_CHANNEL_HANDOFF_REJECTION_CONTROL_JOIN, edge->terminator,
              LOOM_VALUE_ID_INVALID, out_rejection);
          return iree_ok_status();
        }
      } else {
        target->input = next;
        IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
            arena, next.read_count, sizeof(*edge_reads),
            (void**)&target->input.reads));
        if (next.read_count) {
          memcpy(target->input.reads, edge_reads,
                 next.read_count * sizeof(*edge_reads));
        }
        target->reached = true;
        worklist[write_position++] = edge->target_block_index;
      }
    }
  }
  return iree_ok_status();
}
