// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/transforms/pipeline/channel_materialization.h"

#include <string.h>

#include "loom/ops/cfg/ops.h"
#include "loom/ops/func/ops.h"

// One source block's retained exit and private protocol state.
typedef struct loom_channel_materialization_block_t {
  // Original source block, stable while forwarding blocks are appended.
  loom_block_t* block;
  // Original control exit; action emission leaves the CFG intact.
  loom_op_t* terminator;
  // Mutable state after the most recently materialized action in this block.
  loom_value_id_t* state;
} loom_channel_materialization_block_t;

static bool loom_channel_materialization_erases_value(
    const loom_channel_plan_t* plan,
    const loom_channel_materialization_options_t* options,
    loom_value_id_t value) {
  if (!options->erased_values) {
    return false;
  }
  const loom_value_ordinal_t ordinal =
      loom_local_value_domain_try_ordinal(plan->value_domain, value);
  return ordinal != LOOM_VALUE_ORDINAL_INVALID && ordinal < plan->value_count &&
         options->erased_values[ordinal];
}

static iree_status_t loom_channel_materialization_forward_state(
    loom_rewriter_t* rewriter, loom_region_t* region,
    const loom_channel_plan_t* plan,
    const loom_channel_materialization_options_t* options,
    const loom_channel_materialization_block_t* block,
    iree_host_size_t state_count) {
  loom_op_t* terminator = block->terminator;
  if (loom_cfg_br_isa(terminator) || loom_func_return_isa(terminator)) {
    const bool is_branch = loom_cfg_br_isa(terminator);
    const iree_host_size_t appended_count = is_branch ? state_count : 0;
    loom_value_id_t* arguments = NULL;
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
        rewriter->arena, terminator->operand_count + appended_count,
        sizeof(*arguments), (void**)&arguments));
    iree_host_size_t argument_count = 0;
    for (uint16_t i = 0; i < terminator->operand_count; ++i) {
      const loom_value_id_t value = loom_op_operands(terminator)[i];
      if (!loom_channel_materialization_erases_value(plan, options, value)) {
        arguments[argument_count++] = value;
      }
    }
    if (appended_count) {
      memcpy(arguments + argument_count, block->state,
             appended_count * sizeof(*arguments));
    }
    if (!appended_count && argument_count == terminator->operand_count) {
      return iree_ok_status();
    }
    loom_builder_set_before(&rewriter->builder, terminator);
    loom_op_t* branch = NULL;
    if (is_branch) {
      IREE_RETURN_IF_ERROR(loom_cfg_br_build(
          &rewriter->builder, loom_cfg_br_dest(terminator), arguments,
          argument_count + appended_count, terminator->location, &branch));
    } else {
      IREE_RETURN_IF_ERROR(
          loom_func_return_build(&rewriter->builder, arguments, argument_count,
                                 terminator->location, &branch));
    }
  } else {
    if (!state_count || !terminator->successor_count) {
      return iree_ok_status();
    }
    // Flat source execution uses cfg.br and cfg.cond_br. The latter carries no
    // argument payload, so each edge forwards its state through a plain branch.
    IREE_ASSERT(loom_cfg_cond_br_isa(terminator));
    loom_block_t* edges[2];
    for (uint8_t i = 0; i < 2; ++i) {
      loom_block_t* destination = loom_op_successors(terminator)[i];
      IREE_RETURN_IF_ERROR(
          loom_region_append_block(rewriter->module, region, &edges[i]));
      loom_builder_set_block(&rewriter->builder, edges[i]);
      rewriter->builder.ip.parent_op = terminator->parent_op;
      loom_op_t* branch = NULL;
      IREE_RETURN_IF_ERROR(loom_cfg_br_build(&rewriter->builder, destination,
                                             block->state, state_count,
                                             terminator->location, &branch));
    }
    loom_builder_set_before(&rewriter->builder, terminator);
    loom_op_t* branch = NULL;
    IREE_RETURN_IF_ERROR(loom_cfg_cond_br_build(
        &rewriter->builder, loom_cfg_cond_br_condition(terminator), edges[0],
        edges[1], terminator->location, &branch));
  }
  return loom_rewriter_erase(rewriter, terminator);
}

iree_status_t loom_channel_materialize(
    loom_rewriter_t* rewriter, const loom_channel_plan_t* plan,
    const loom_channel_materialization_options_t* options) {
  loom_module_t* module = rewriter->module;
  loom_region_t* region = (loom_region_t*)plan->value_domain->region;
  const uint16_t block_count = region->block_count;
  const iree_host_size_t state_count = options->state_count;
  loom_channel_materialization_block_t* blocks = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      rewriter->arena, block_count, sizeof(*blocks), (void**)&blocks));
  loom_value_id_t* states = NULL;
  if (state_count) {
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
        rewriter->arena, block_count, state_count * sizeof(*states),
        (void**)&states));
    memcpy(states, options->initial_state, state_count * sizeof(*states));
  }
  for (uint16_t b = 0; b < block_count; ++b) {
    loom_block_t* block = region->blocks[b];
    blocks[b] = (loom_channel_materialization_block_t){
        .block = block,
        .terminator = block->last_op,
        .state = state_count ? states + b * state_count : NULL,
    };
    for (uint16_t i = 0; i < block->arg_count; ++i) {
      const loom_value_id_t value = block->arg_ids[i];
      const loom_type_t carrier =
          options->carrier_types[loom_local_value_domain_ordinal(
              plan->value_domain, value)];
      if (loom_type_kind(carrier) != LOOM_TYPE_NONE) {
        IREE_RETURN_IF_ERROR(
            loom_rewriter_set_value_type(rewriter, value, carrier));
      }
    }
    if (b == 0) {
      continue;
    }
    for (iree_host_size_t i = 0; i < state_count; ++i) {
      const loom_type_t type =
          loom_module_value_type(module, options->initial_state[i]);
      loom_value_id_t argument = LOOM_VALUE_ID_INVALID;
      IREE_RETURN_IF_ERROR(loom_module_define_value(module, type, &argument));
      IREE_RETURN_IF_ERROR(loom_block_add_arg(module, block, argument));
      blocks[b].state[i] = argument;
    }
  }
  uint16_t maximum_results = 0;
  for (iree_host_size_t i = 0; i < plan->action_count; ++i) {
    maximum_results =
        iree_max(maximum_results, plan->actions[i].op->result_count);
  }
  loom_value_id_t* results = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      rewriter->arena, maximum_results, sizeof(*results), (void**)&results));
  loom_op_t** consumed_ops = NULL;
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate_array(rewriter->arena, plan->action_count,
                                sizeof(*consumed_ops), (void**)&consumed_ops));
  iree_host_size_t action_index = 0;
  for (uint16_t b = 0; b < block_count; ++b) {
    while (action_index < plan->action_count &&
           plan->actions[action_index].op->parent_block == blocks[b].block) {
      const loom_channel_plan_action_t* action = &plan->actions[action_index++];
      loom_builder_set_before(&rewriter->builder, action->op);
      const loom_value_id_t checkpoint =
          loom_rewriter_value_checkpoint(rewriter);
      for (uint16_t i = 0; i < action->op->result_count; ++i) {
        results[i] = LOOM_VALUE_ID_INVALID;
      }
      IREE_RETURN_IF_ERROR(options->emit.fn(options->emit.user_data, rewriter,
                                            action, blocks[b].state,
                                            state_count, results));
      IREE_RETURN_IF_ERROR(loom_rewriter_preserve_result_names_on_new_values(
          rewriter, action->op, results, action->op->result_count, checkpoint));
      for (uint16_t i = 0; i < action->op->result_count; ++i) {
        const loom_value_id_t value = loom_op_results(action->op)[i];
        if (!loom_channel_materialization_erases_value(plan, options, value)) {
          IREE_RETURN_IF_ERROR(
              loom_rewriter_replace_all_uses_with(rewriter, value, results[i]));
        }
      }
      consumed_ops[action_index - 1] = action->op;
    }
    if (options->emit.exit && loom_func_return_isa(blocks[b].terminator)) {
      loom_builder_set_before(&rewriter->builder, blocks[b].terminator);
      IREE_RETURN_IF_ERROR(options->emit.exit(options->emit.user_data, rewriter,
                                              blocks[b].terminator,
                                              blocks[b].state, state_count));
    }
    IREE_RETURN_IF_ERROR(loom_channel_materialization_forward_state(
        rewriter, region, plan, options, &blocks[b], state_count));
  }
  IREE_ASSERT_EQ(action_index, plan->action_count);
  IREE_RETURN_IF_ERROR(loom_rewriter_erase_closed_set(rewriter, consumed_ops,
                                                      plan->action_count));
  for (uint16_t b = 1; b < block_count; ++b) {
    loom_block_t* block = blocks[b].block;
    for (uint16_t i = block->arg_count; i > 0; --i) {
      if (loom_channel_materialization_erases_value(plan, options,
                                                    block->arg_ids[i - 1])) {
        IREE_RETURN_IF_ERROR(loom_block_remove_arg(module, block, i - 1));
      }
    }
  }
  return iree_ok_status();
}
