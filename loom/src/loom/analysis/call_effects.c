// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/analysis/call_effects.h"

#include <string.h>

#include "loom/analysis/movement.h"
#include "loom/analysis/value_relation.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"
#include "loom/ops/op_defs.h"
#include "loom/util/walk.h"

#define LOOM_CALL_EFFECT_NODE_NONE UINT32_MAX

typedef struct loom_call_effect_edge_t {
  // Node receiving the source node's access/escape effects.
  uint32_t target;
  // Next dependency of the same source node.
  struct loom_call_effect_edge_t* next;
} loom_call_effect_edge_t;

typedef struct loom_call_effect_node_t {
  // Reference value indexed by the module scratch, or INVALID for a function.
  loom_value_id_t value_id;
  // Union of all observed access and escape effects.
  loom_call_argument_effects_t effects;
  // True while this node is in the propagation queue.
  bool queued;
  // Next queued node, or NONE.
  uint32_t next_pending;
  // Alias origins and actual arguments that inherit this node's effects.
  loom_call_effect_edge_t* edges;
} loom_call_effect_node_t;

typedef struct loom_call_effect_function_t {
  // Source callable whose body is summarized, or empty for opaque symbols.
  loom_func_like_t function;
  // Synthetic graph node propagating unknown ambient memory effects.
  uint32_t ambient_node;
  // Reference argument nodes in formal order; nonreferences contain NONE.
  uint32_t* argument_nodes;
} loom_call_effect_function_t;

struct loom_call_effects_t {
  // Detached summaries indexed by the original module symbol ID.
  loom_call_effect_summary_t* functions;
  // Number of symbol-indexed entries.
  iree_host_size_t function_count;
};

typedef struct loom_call_effect_builder_t {
  // Verified module providing source values and reusable ordinal scratch.
  loom_module_t* module;
  // Transient graph storage, released after retaining argument summaries.
  iree_arena_allocator_t arena;
  // Result owning detached formal argument effects.
  loom_call_effects_t* result;
  // Source function records indexed by symbol ID.
  loom_call_effect_function_t* functions;
  // Compact graph of reference values and function ambient-effect nodes.
  loom_call_effect_node_t* nodes;
  // Number of initialized graph nodes.
  iree_host_size_t node_count;
  // Allocated node capacity.
  iree_host_size_t node_capacity;
  // First node awaiting propagation, or NONE.
  uint32_t pending_head;
  // Last node awaiting propagation, or NONE.
  uint32_t pending_tail;
  // Function owning the body currently traversed.
  loom_call_effect_function_t* current;
  // Reused per-operation operand effect classification.
  loom_call_argument_effects_t* operand_effects;
  // Allocated operand classification capacity.
  iree_host_size_t operand_capacity;
} loom_call_effect_builder_t;

static bool loom_call_effect_is_reference(const loom_module_t* module,
                                          loom_value_id_t value_id) {
  const loom_type_t type = loom_module_value_type(module, value_id);
  return loom_type_is_buffer(type) || loom_type_is_view(type);
}

static loom_region_t* loom_call_effect_source_body(loom_func_like_t function) {
  if (loom_func_like_is_kernel(function) ||
      loom_func_like_repr_contract(function) != LOOM_STRING_ID_INVALID) {
    return NULL;
  }
  return loom_func_like_body(function);
}

static iree_status_t loom_call_effect_add_node(
    loom_call_effect_builder_t* builder, loom_value_id_t value_id,
    uint32_t* out_node) {
  if (builder->node_count == UINT32_MAX) {
    return iree_make_status(IREE_STATUS_RESOURCE_EXHAUSTED,
                            "call effect graph exceeds node index range");
  }
  if (builder->node_count == builder->node_capacity) {
    IREE_RETURN_IF_ERROR(iree_arena_grow_array(
        &builder->arena, builder->node_count, builder->node_count + 1,
        sizeof(*builder->nodes), &builder->node_capacity,
        (void**)&builder->nodes));
  }
  *out_node = (uint32_t)builder->node_count++;
  builder->nodes[*out_node] = (loom_call_effect_node_t){
      .value_id = value_id, .next_pending = LOOM_CALL_EFFECT_NODE_NONE};
  return iree_ok_status();
}

static iree_status_t loom_call_effect_reference_node(
    loom_call_effect_builder_t* builder, loom_value_id_t value_id,
    uint32_t* out_node) {
  *out_node =
      loom_value_u32_scratch_load(&builder->module->scratch.values, value_id);
  if (*out_node == LOOM_CALL_EFFECT_NODE_NONE) {
    IREE_RETURN_IF_ERROR(
        loom_call_effect_add_node(builder, value_id, out_node));
    loom_value_u32_scratch_store(&builder->module->scratch.values, value_id,
                                 *out_node);
  }
  return iree_ok_status();
}

static void loom_call_effect_include(loom_call_effect_builder_t* builder,
                                     uint32_t node_index,
                                     loom_call_argument_effects_t effects) {
  loom_call_effect_node_t* node = &builder->nodes[node_index];
  if ((node->effects | effects) == node->effects) {
    return;
  }
  node->effects |= effects;
  if (node->queued) {
    return;
  }
  node->queued = true;
  node->next_pending = LOOM_CALL_EFFECT_NODE_NONE;
  if (builder->pending_tail == LOOM_CALL_EFFECT_NODE_NONE) {
    builder->pending_head = node_index;
  } else {
    builder->nodes[builder->pending_tail].next_pending = node_index;
  }
  builder->pending_tail = node_index;
}

static iree_status_t loom_call_effect_add_edge(
    loom_call_effect_builder_t* builder, uint32_t source, uint32_t target) {
  if (source == target) {
    return iree_ok_status();
  }
  loom_call_effect_edge_t* edge = NULL;
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate(&builder->arena, sizeof(*edge), (void**)&edge));
  *edge = (loom_call_effect_edge_t){.target = target,
                                    .next = builder->nodes[source].edges};
  builder->nodes[source].edges = edge;
  return iree_ok_status();
}

static iree_status_t loom_call_effect_add_alias(
    loom_call_effect_builder_t* builder, loom_value_id_t source,
    loom_value_id_t alias) {
  uint32_t source_node = 0;
  uint32_t alias_node = 0;
  IREE_RETURN_IF_ERROR(
      loom_call_effect_reference_node(builder, source, &source_node));
  IREE_RETURN_IF_ERROR(
      loom_call_effect_reference_node(builder, alias, &alias_node));
  return loom_call_effect_add_edge(builder, alias_node, source_node);
}

static iree_status_t loom_call_effect_visit(void* user_data, loom_op_t* op,
                                            const loom_walk_context_t* context,
                                            loom_walk_result_t* out_result) {
  (void)context;
  *out_result = LOOM_WALK_CONTINUE;
  loom_call_effect_builder_t* builder = user_data;
  const loom_module_t* module = builder->module;
  const loom_op_vtable_t* vtable = loom_op_vtable(module, op);
  const loom_value_id_t* operands = loom_op_const_operands(op);
  if (op->operand_count > builder->operand_capacity) {
    IREE_RETURN_IF_ERROR(iree_arena_grow_array(
        &builder->arena, 0, op->operand_count,
        sizeof(*builder->operand_effects), &builder->operand_capacity,
        (void**)&builder->operand_effects));
  }
  if (op->operand_count) {
    memset(builder->operand_effects, LOOM_CALL_ARGUMENT_ESCAPE,
           op->operand_count * sizeof(*builder->operand_effects));
  }

  const loom_call_like_t call = loom_call_like_cast(module, op);
  const bool semantic_call = loom_call_like_is_direct_semantic(call);
  const loom_symbol_ref_t callee = loom_call_like_callee(call);
  loom_call_effect_function_t* target = NULL;
  if (semantic_call && callee.module_id == 0 &&
      callee.symbol_id < builder->result->function_count) {
    target = &builder->functions[callee.symbol_id];
    if (!target->function.op) {
      target = NULL;
    }
  }
  if (target) {
    IREE_RETURN_IF_ERROR(loom_call_effect_add_edge(
        builder, target->ambient_node, builder->current->ambient_node));
    for (uint16_t i = 0; i < op->operand_count; ++i) {
      if (target->argument_nodes[i] == LOOM_CALL_EFFECT_NODE_NONE) {
        continue;
      }
      uint32_t actual_node = 0;
      IREE_RETURN_IF_ERROR(
          loom_call_effect_reference_node(builder, operands[i], &actual_node));
      IREE_RETURN_IF_ERROR(loom_call_effect_add_edge(
          builder, target->argument_nodes[i], actual_node));
      builder->operand_effects[i] = 0;
    }
  } else if (loom_call_like_isa(call)) {
    // Low escape hatches and absent source bodies provide no bounded access
    // contract, even if the call's result is declared pure.
    loom_call_effect_include(builder, builder->current->ambient_node,
                             LOOM_CALL_ARGUMENT_ESCAPE);
  } else {
    const loom_trait_flags_t traits = loom_op_effective_traits(module, op);
    bool described_read = false;
    bool described_write = false;
    const bool asynchronous = loom_movement_op_kind_is_async(op->kind);
    for (uint16_t i = 0; i < op->operand_count; ++i) {
      const loom_operand_descriptor_t* descriptor = NULL;
      if (!loom_op_operand_descriptor_at(vtable, op, i, &descriptor, NULL,
                                         NULL)) {
        continue;
      }
      loom_call_argument_effects_t effects = 0;
      if (iree_any_bit_set(descriptor->flags, LOOM_OPERAND_READS)) {
        effects |= LOOM_CALL_ARGUMENT_READ;
        described_read = true;
      }
      if (iree_any_bit_set(descriptor->flags, LOOM_OPERAND_WRITES)) {
        effects |= LOOM_CALL_ARGUMENT_WRITE;
        described_write = true;
      }
      if (effects) {
        builder->operand_effects[i] =
            effects | (asynchronous ? LOOM_CALL_ARGUMENT_ESCAPE : 0);
      }
    }
    if (iree_any_bit_set(traits, LOOM_TRAIT_UNKNOWN_EFFECTS) ||
        (iree_any_bit_set(traits, LOOM_TRAIT_READS_MEMORY) &&
         !described_read) ||
        (iree_any_bit_set(traits, LOOM_TRAIT_WRITES_MEMORY) &&
         !described_write)) {
      loom_call_effect_include(builder, builder->current->ambient_node,
                               LOOM_CALL_ARGUMENT_ESCAPE);
    }

    const loom_value_relation_mask_t mask =
        LOOM_VALUE_RELATION_MASK_ALL &
        ~LOOM_VALUE_RELATION_MASK(LOOM_VALUE_RELATION_ELEMENTWISE);
    loom_value_relation_iterator_t iterator;
    loom_value_relation_iterator_initialize(module, op, mask, &iterator);
    loom_value_relation_t relation;
    while (loom_value_relation_iterator_next(&iterator, &relation)) {
      if (!loom_call_effect_is_reference(module, relation.source_value_id) ||
          !loom_call_effect_is_reference(module,
                                         relation.destination_value_id)) {
        continue;
      }
      IREE_RETURN_IF_ERROR(loom_call_effect_add_alias(
          builder, relation.source_value_id, relation.destination_value_id));
      if (relation.source_operand_index !=
          LOOM_VALUE_RELATION_OPERAND_INDEX_NONE) {
        builder->operand_effects[relation.source_operand_index] &=
            ~LOOM_CALL_ARGUMENT_ESCAPE;
      }
    }
  }
  for (uint16_t i = 0; i < op->operand_count; ++i) {
    if (!loom_call_effect_is_reference(module, operands[i])) {
      continue;
    }
    uint32_t node = 0;
    IREE_RETURN_IF_ERROR(
        loom_call_effect_reference_node(builder, operands[i], &node));
    loom_call_effect_include(builder, node, builder->operand_effects[i]);
  }
  return iree_ok_status();
}

static void loom_call_effect_solve(loom_call_effect_builder_t* builder) {
  while (builder->pending_head != LOOM_CALL_EFFECT_NODE_NONE) {
    const uint32_t index = builder->pending_head;
    loom_call_effect_node_t* node = &builder->nodes[index];
    builder->pending_head = node->next_pending;
    if (builder->pending_head == LOOM_CALL_EFFECT_NODE_NONE) {
      builder->pending_tail = LOOM_CALL_EFFECT_NODE_NONE;
    }
    node->queued = false;
    for (const loom_call_effect_edge_t* edge = node->edges; edge;
         edge = edge->next) {
      loom_call_effect_include(builder, edge->target, node->effects);
    }
  }
}

static iree_status_t loom_call_effect_build(
    loom_call_effect_builder_t* builder, iree_arena_allocator_t* result_arena) {
  loom_module_t* module = builder->module;
  const iree_host_size_t count = builder->result->function_count;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(&builder->arena, count,
                                                 sizeof(*builder->functions),
                                                 (void**)&builder->functions));
  memset(builder->functions, 0, count * sizeof(*builder->functions));
  for (iree_host_size_t i = 0; i < count; ++i) {
    loom_func_like_t function =
        loom_func_like_cast(module, module->symbols.entries[i].defining_op);
    loom_region_t* body = loom_call_effect_source_body(function);
    if (!body) {
      continue;
    }
    loom_call_effect_function_t* record = &builder->functions[i];
    record->function = function;
    IREE_RETURN_IF_ERROR(loom_call_effect_add_node(
        builder, LOOM_VALUE_ID_INVALID, &record->ambient_node));
    const loom_block_t* entry = loom_region_entry_block(body);
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
        &builder->arena, entry->arg_count, sizeof(*record->argument_nodes),
        (void**)&record->argument_nodes));
    for (uint16_t j = 0; j < entry->arg_count; ++j) {
      record->argument_nodes[j] = LOOM_CALL_EFFECT_NODE_NONE;
      const loom_value_id_t value_id = loom_block_arg_id(entry, j);
      if (loom_call_effect_is_reference(module, value_id)) {
        IREE_RETURN_IF_ERROR(loom_call_effect_reference_node(
            builder, value_id, &record->argument_nodes[j]));
      }
    }
    // A non-NULL argument array also identifies a zero-argument definition.
    loom_call_argument_effects_t* arguments = NULL;
    IREE_RETURN_IF_ERROR(
        iree_arena_allocate_array(result_arena, iree_max(entry->arg_count, 1),
                                  sizeof(*arguments), (void**)&arguments));
    memset(arguments, 0, entry->arg_count * sizeof(*arguments));
    builder->result->functions[i] = (loom_call_effect_summary_t){
        .arguments = arguments, .argument_count = entry->arg_count};
  }
  for (iree_host_size_t i = 0; i < count; ++i) {
    if (!builder->functions[i].function.op) {
      continue;
    }
    builder->current = &builder->functions[i];
    loom_walk_result_t walk_result;
    IREE_RETURN_IF_ERROR(loom_walk_region(
        module, loom_func_like_body(builder->current->function),
        LOOM_WALK_PRE_ORDER,
        (loom_walk_callback_t){.fn = loom_call_effect_visit,
                               .user_data = builder},
        &builder->arena, &walk_result));
  }
  loom_call_effect_solve(builder);
  for (iree_host_size_t i = 0; i < count; ++i) {
    const loom_call_effect_function_t* record = &builder->functions[i];
    if (!record->function.op) {
      continue;
    }
    loom_call_effect_summary_t* summary = &builder->result->functions[i];
    summary->has_unknown_memory_access =
        builder->nodes[record->ambient_node].effects != 0;
    loom_call_argument_effects_t* arguments =
        (loom_call_argument_effects_t*)summary->arguments;
    for (uint16_t j = 0; j < summary->argument_count; ++j) {
      if (record->argument_nodes[j] != LOOM_CALL_EFFECT_NODE_NONE) {
        arguments[j] = builder->nodes[record->argument_nodes[j]].effects;
      }
    }
  }
  return iree_ok_status();
}

iree_status_t loom_call_effects_analyze_module(
    loom_module_t* module, iree_arena_allocator_t* arena,
    loom_call_effects_t** out_effects) {
  *out_effects = NULL;
  bool has_callable = false;
  for (iree_host_size_t i = 0; i < module->symbols.count; ++i) {
    const loom_func_like_t function =
        loom_func_like_cast(module, module->symbols.entries[i].defining_op);
    if (loom_call_effect_source_body(function)) {
      has_callable = true;
      break;
    }
  }
  if (!has_callable) {
    return iree_ok_status();
  }
  loom_call_effects_t* result = NULL;
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate(arena, sizeof(*result), (void**)&result));
  *result = (loom_call_effects_t){.function_count = module->symbols.count};
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, result->function_count,
                                                 sizeof(*result->functions),
                                                 (void**)&result->functions));
  memset(result->functions, 0,
         result->function_count * sizeof(*result->functions));
  loom_call_effect_builder_t builder = {
      .module = module,
      .result = result,
      .pending_head = LOOM_CALL_EFFECT_NODE_NONE,
      .pending_tail = LOOM_CALL_EFFECT_NODE_NONE,
  };
  iree_arena_initialize(arena->block_pool, &builder.arena);
  loom_module_value_ordinal_scratch_acquire(module);
  iree_status_t status = loom_call_effect_build(&builder, arena);
  for (iree_host_size_t i = 0; i < builder.node_count; ++i) {
    const loom_value_id_t value_id = builder.nodes[i].value_id;
    if (value_id != LOOM_VALUE_ID_INVALID) {
      loom_value_u32_scratch_store(&module->scratch.values, value_id,
                                   LOOM_VALUE_ORDINAL_INVALID);
    }
  }
  loom_module_value_ordinal_scratch_release(module);
  iree_arena_deinitialize(&builder.arena);
  if (iree_status_is_ok(status)) {
    *out_effects = result;
  }
  return status;
}

const loom_call_effect_summary_t* loom_call_effects_lookup(
    const loom_call_effects_t* effects, loom_symbol_ref_t callee) {
  if (!effects || callee.module_id != 0 ||
      callee.symbol_id >= effects->function_count) {
    return NULL;
  }
  const loom_call_effect_summary_t* summary =
      &effects->functions[callee.symbol_id];
  return summary->arguments ? summary : NULL;
}
