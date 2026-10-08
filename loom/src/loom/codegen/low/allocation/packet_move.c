// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/low/allocation/packet_move.h"

#include "loom/codegen/low/allocation/live_range.h"
#include "loom/codegen/low/allocation/move_topology.h"
#include "loom/codegen/low/allocation/storage.h"
#include "loom/ops/low/ops.h"

typedef enum loom_low_allocation_packet_transfer_kind_e {
  LOOM_LOW_ALLOCATION_PACKET_TRANSFER_DEAD = 0,
  LOOM_LOW_ALLOCATION_PACKET_TRANSFER_FORWARDED = 1,
  LOOM_LOW_ALLOCATION_PACKET_TRANSFER_MATERIALIZED = 2,
} loom_low_allocation_packet_transfer_kind_t;

typedef struct loom_low_allocation_packet_move_builder_t {
  // Immutable allocation facts used to construct packet-local moves.
  const loom_low_allocation_packet_move_context_t* context;
  // Plan receiving final move groups.
  loom_low_allocation_packet_move_plan_t plan;
  // Number of group records available in |plan.groups|.
  iree_host_size_t group_capacity;
  // Next counted group being populated with final moves.
  iree_host_size_t build_group_index;
  // Next forwarded transfer output within the current group.
  uint32_t forwarded_transfer_index;
  // Next materialized transfer output within the current group.
  uint32_t materialized_transfer_index;
  // Number of raw physical moves in the current group.
  iree_host_size_t raw_move_count;
  // Source-preorder position shared by the construction region walk.
  loom_low_allocation_move_cursor_t cursor;
} loom_low_allocation_packet_move_builder_t;

static const loom_low_allocation_assignment_t*
loom_low_allocation_packet_move_try_assignment(
    const loom_low_allocation_assignment_map_t* map,
    loom_value_ordinal_t value_ordinal) {
  const uint32_t assignment_index =
      map->assignment_indices_by_value_ordinal[value_ordinal];
  return assignment_index != UINT32_MAX ? &map->assignments[assignment_index]
                                        : NULL;
}

static loom_value_id_t loom_low_allocation_packet_move_result(
    const loom_op_t* op,
    loom_low_allocation_packet_move_op_kind_t packet_move_kind) {
  switch (packet_move_kind) {
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_COPY:
      return loom_low_copy_result(op);
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_MOVE:
      return loom_low_move_result(op);
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_SLICE:
      return loom_low_slice_result(op);
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_CONCAT:
      return loom_low_concat_result(op);
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_NONE:
      IREE_ASSERT_UNREACHABLE("packet-move operation must have a result");
      return LOOM_VALUE_ID_INVALID;
  }
  IREE_ASSERT_UNREACHABLE("unknown packet-move operation kind");
  return LOOM_VALUE_ID_INVALID;
}

static loom_low_placement_cause_t loom_low_allocation_packet_move_cause(
    loom_low_allocation_packet_move_op_kind_t packet_move_kind) {
  switch (packet_move_kind) {
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_COPY:
      return LOOM_LOW_PLACEMENT_CAUSE_LOW_COPY;
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_MOVE:
      return LOOM_LOW_PLACEMENT_CAUSE_LOW_MOVE;
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_SLICE:
      return LOOM_LOW_PLACEMENT_CAUSE_LOW_SLICE;
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_CONCAT:
      return LOOM_LOW_PLACEMENT_CAUSE_LOW_CONCAT;
    case LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_NONE:
      IREE_ASSERT_UNREACHABLE("packet-move group must have a placement cause");
      return LOOM_LOW_PLACEMENT_CAUSE_UNKNOWN;
  }
  IREE_ASSERT_UNREACHABLE("unknown packet-move operation kind");
  return LOOM_LOW_PLACEMENT_CAUSE_UNKNOWN;
}

typedef enum loom_low_allocation_packet_transfer_phase_e {
  LOOM_LOW_ALLOCATION_PACKET_TRANSFER_PHASE_COUNT = 0,
  LOOM_LOW_ALLOCATION_PACKET_TRANSFER_PHASE_BUILD = 1,
} loom_low_allocation_packet_transfer_phase_t;

static bool loom_low_allocation_packet_move_has_relations(
    const loom_op_t* op,
    loom_low_allocation_packet_move_op_kind_t packet_move_kind) {
  return packet_move_kind != LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_NONE &&
         (packet_move_kind != LOOM_LOW_ALLOCATION_PACKET_MOVE_OP_CONCAT ||
          loom_low_concat_sources(op).count != 0);
}

static loom_low_allocation_packet_transfer_kind_t
loom_low_allocation_packet_move_classify_unit(
    const loom_low_allocation_packet_move_context_t* context,
    const loom_low_placement_relation_t* relation,
    const loom_low_allocation_assignment_t* source_assignment,
    const loom_low_allocation_assignment_t* destination_assignment,
    uint32_t relation_unit_offset) {
  const uint32_t destination_unit =
      relation->result_unit_offset + relation_unit_offset;
  const loom_low_allocation_unit_liveness_t* unit_liveness =
      context->move_plan->context.unit_liveness;
  const uint32_t start_point =
      loom_low_allocation_live_range_assignment_unit_start_point(
          unit_liveness->start_points, unit_liveness->point_count,
          destination_assignment, destination_unit);
  const uint32_t end_point =
      loom_low_allocation_live_range_assignment_unit_end_point(
          unit_liveness->end_points, unit_liveness->point_count,
          destination_assignment, destination_unit);
  if (start_point >= end_point) {
    return LOOM_LOW_ALLOCATION_PACKET_TRANSFER_DEAD;
  }
  IREE_ASSERT(source_assignment != NULL,
              "live packet transfer source must have an assignment");
  if (relation->cause == LOOM_LOW_PLACEMENT_CAUSE_LOW_CONCAT &&
      !iree_any_bit_set(relation->flags,
                        LOOM_LOW_PLACEMENT_RELATION_FLAG_MATERIALIZE_PART)) {
    return LOOM_LOW_ALLOCATION_PACKET_TRANSFER_FORWARDED;
  }
  return loom_low_allocation_storage_assignment_subranges_equal(
             context->move_plan->context.descriptor_set, source_assignment,
             relation->source_unit_offset + relation_unit_offset,
             destination_assignment, destination_unit, 1)
             ? LOOM_LOW_ALLOCATION_PACKET_TRANSFER_FORWARDED
             : LOOM_LOW_ALLOCATION_PACKET_TRANSFER_MATERIALIZED;
}

static iree_status_t loom_low_allocation_packet_move_record_transfer(
    loom_low_allocation_packet_move_builder_t* builder,
    loom_low_allocation_packet_move_group_t* group, uint32_t relation_index,
    const loom_low_placement_relation_t* relation,
    const loom_low_allocation_assignment_t* source_assignment,
    const loom_low_allocation_assignment_t* destination_assignment,
    loom_low_allocation_packet_transfer_kind_t kind,
    uint32_t relation_unit_offset, uint32_t unit_count,
    loom_low_allocation_packet_transfer_phase_t phase) {
  IREE_ASSERT(kind != LOOM_LOW_ALLOCATION_PACKET_TRANSFER_DEAD);
  IREE_ASSERT_NE(unit_count, 0u);
  if (phase == LOOM_LOW_ALLOCATION_PACKET_TRANSFER_PHASE_COUNT) {
    if (builder->plan.transfer_count == UINT32_MAX) {
      return iree_make_status(
          IREE_STATUS_RESOURCE_EXHAUSTED,
          "packet transfer run count exceeds the compact index domain");
    }
    uint32_t* const kind_count =
        kind == LOOM_LOW_ALLOCATION_PACKET_TRANSFER_FORWARDED
            ? &group->forwarded_transfer_count
            : &group->materialized_transfer_count;
    if (*kind_count == UINT32_MAX) {
      return iree_make_status(
          IREE_STATUS_RESOURCE_EXHAUSTED,
          "packet transfer group run count exceeds the compact index domain");
    }
    ++*kind_count;
    ++builder->plan.transfer_count;
    return iree_ok_status();
  }

  uint32_t transfer_index = 0;
  if (kind == LOOM_LOW_ALLOCATION_PACKET_TRANSFER_FORWARDED) {
    transfer_index = builder->forwarded_transfer_index++;
  } else {
    transfer_index = builder->materialized_transfer_index++;
  }
  IREE_ASSERT_LT(transfer_index, builder->plan.transfer_count);
  builder->plan.transfers[transfer_index] =
      (loom_low_allocation_packet_transfer_t){
          .relation_index = relation_index,
          .relation_unit_offset = relation_unit_offset,
          .unit_count = unit_count,
      };
  if (kind == LOOM_LOW_ALLOCATION_PACKET_TRANSFER_MATERIALIZED) {
    loom_low_allocation_move_plan_append_assignment(
        builder->context->move_plan, source_assignment,
        relation->source_unit_offset + relation_unit_offset,
        destination_assignment,
        relation->result_unit_offset + relation_unit_offset, unit_count,
        &builder->raw_move_count);
  }
  return iree_ok_status();
}

static iree_status_t loom_low_allocation_packet_move_scan_relation(
    loom_low_allocation_packet_move_builder_t* builder,
    loom_low_allocation_packet_move_group_t* group, uint32_t relation_index,
    const loom_low_placement_relation_t* relation,
    const loom_low_allocation_assignment_t* source_assignment,
    const loom_low_allocation_assignment_t* destination_assignment,
    loom_low_allocation_packet_transfer_phase_t phase) {
  if (relation->unit_count == 0) {
    return iree_ok_status();
  }
  loom_low_allocation_packet_transfer_kind_t run_kind =
      loom_low_allocation_packet_move_classify_unit(builder->context, relation,
                                                    source_assignment,
                                                    destination_assignment, 0);
  uint32_t run_start = 0;
  for (uint32_t unit = 1; unit < relation->unit_count; ++unit) {
    const loom_low_allocation_packet_transfer_kind_t kind =
        loom_low_allocation_packet_move_classify_unit(
            builder->context, relation, source_assignment,
            destination_assignment, unit);
    if (kind == run_kind) {
      continue;
    }
    if (run_kind != LOOM_LOW_ALLOCATION_PACKET_TRANSFER_DEAD) {
      IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_record_transfer(
          builder, group, relation_index, relation, source_assignment,
          destination_assignment, run_kind, run_start, unit - run_start,
          phase));
    }
    run_kind = kind;
    run_start = unit;
  }
  if (run_kind != LOOM_LOW_ALLOCATION_PACKET_TRANSFER_DEAD) {
    IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_record_transfer(
        builder, group, relation_index, relation, source_assignment,
        destination_assignment, run_kind, run_start,
        relation->unit_count - run_start, phase));
  }
  return iree_ok_status();
}

static iree_status_t loom_low_allocation_packet_move_scan_group(
    loom_low_allocation_packet_move_builder_t* builder,
    loom_low_allocation_packet_move_group_t* group, const loom_op_t* op,
    loom_low_allocation_packet_move_op_kind_t packet_move_kind,
    loom_low_allocation_packet_transfer_phase_t phase) {
  const loom_low_allocation_packet_move_context_t* context = builder->context;
  loom_value_ordinal_t result_ordinal = LOOM_VALUE_ORDINAL_INVALID;
  const bool result_found =
      loom_low_allocation_assignment_map_value_ordinal_for_value(
          &context->move_plan->context.assignment_map,
          loom_low_allocation_packet_move_result(op, packet_move_kind),
          &result_ordinal);
  IREE_ASSERT(result_found,
              "packet-move result must belong to allocation liveness");
  const loom_low_placement_relation_range_t range =
      loom_low_placement_relation_range_for_value_ordinal(context->placement,
                                                          result_ordinal);
  const loom_low_allocation_assignment_map_t* assignment_map =
      &context->move_plan->context.assignment_map;
  for (uint32_t i = 0; i < range.count; ++i) {
    const uint32_t relation_index = range.start + i;
    const loom_low_placement_relation_t* relation =
        &context->placement->relations[relation_index];
    if (relation->op != op || relation->cause != group->cause) {
      continue;
    }
    const loom_low_allocation_assignment_t* destination_assignment =
        loom_low_allocation_packet_move_try_assignment(
            assignment_map, relation->result_ordinal);
    if (destination_assignment == NULL) {
      continue;
    }
    const loom_low_allocation_assignment_t* source_assignment =
        loom_low_allocation_packet_move_try_assignment(
            assignment_map, relation->source_ordinal);
    IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_scan_relation(
        builder, group, relation_index, relation, source_assignment,
        destination_assignment, phase));
  }
  return iree_ok_status();
}

static iree_status_t loom_low_allocation_packet_move_count_group(
    loom_low_allocation_packet_move_builder_t* builder, const loom_op_t* op,
    uint32_t source_ordinal,
    loom_low_allocation_packet_move_op_kind_t packet_move_kind) {
  IREE_ASSERT_LT(builder->plan.group_count, builder->group_capacity);
  IREE_ASSERT_LE(builder->plan.transfer_count, UINT32_MAX);
  loom_low_allocation_packet_move_group_t* group =
      &builder->plan.groups[builder->plan.group_count++];
  *group = (loom_low_allocation_packet_move_group_t){
      .source_ordinal = source_ordinal,
      .cause = loom_low_allocation_packet_move_cause(packet_move_kind),
      .transfer_start = (uint32_t)builder->plan.transfer_count,
  };
  return loom_low_allocation_packet_move_scan_group(
      builder, group, op, packet_move_kind,
      LOOM_LOW_ALLOCATION_PACKET_TRANSFER_PHASE_COUNT);
}

static iree_status_t loom_low_allocation_packet_move_count_region(
    loom_low_allocation_packet_move_builder_t* builder,
    const loom_region_t* region, uint32_t* inout_source_ordinal) {
  const loom_liveness_analysis_t* liveness =
      builder->context->move_plan->context.assignment_map.liveness;
  const loom_block_t* block = NULL;
  loom_region_for_each_block(region, block) {
    const loom_op_t* op = NULL;
    loom_block_for_each_op(block, op) {
      const loom_low_allocation_packet_move_op_kind_t packet_move_kind =
          loom_low_allocation_move_topology_packet_move_op_kind(op);
      if (loom_low_allocation_packet_move_has_relations(op, packet_move_kind)) {
        IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_count_group(
            builder, op, *inout_source_ordinal, packet_move_kind));
      }
      ++*inout_source_ordinal;
      if (!loom_liveness_analysis_includes_region_tree(liveness)) {
        continue;
      }
      loom_region_t* const* regions = loom_op_regions(op);
      for (uint8_t i = 0; i < op->region_count; ++i) {
        if (regions[i] == NULL) {
          continue;
        }
        IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_count_region(
            builder, regions[i], inout_source_ordinal));
      }
    }
  }
  return iree_ok_status();
}

static iree_status_t loom_low_allocation_packet_move_build_group(
    loom_low_allocation_packet_move_builder_t* builder,
    const loom_liveness_operation_point_t* operation_point,
    uint32_t source_ordinal,
    loom_low_allocation_packet_move_op_kind_t packet_move_kind) {
  IREE_ASSERT_LT(builder->build_group_index, builder->plan.group_count);
  loom_low_allocation_packet_move_group_t* group =
      &builder->plan.groups[builder->build_group_index++];
  IREE_ASSERT_EQ(group->source_ordinal, source_ordinal);
  IREE_ASSERT_EQ(group->cause,
                 loom_low_allocation_packet_move_cause(packet_move_kind));
  builder->forwarded_transfer_index = group->transfer_start;
  builder->materialized_transfer_index =
      group->transfer_start + group->forwarded_transfer_count;
  builder->raw_move_count = 0;
  IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_scan_group(
      builder, group, operation_point->op, packet_move_kind,
      LOOM_LOW_ALLOCATION_PACKET_TRANSFER_PHASE_BUILD));
  IREE_ASSERT_EQ(builder->forwarded_transfer_index,
                 group->transfer_start + group->forwarded_transfer_count);
  IREE_ASSERT_EQ(builder->materialized_transfer_index,
                 group->transfer_start + group->forwarded_transfer_count +
                     group->materialized_transfer_count);
  IREE_RETURN_IF_ERROR(loom_low_allocation_move_plan_append_group(
      builder->context->move_plan, operation_point->op,
      operation_point->start_point, operation_point->end_point,
      builder->raw_move_count, &group->move_group));
  builder->plan.move_count += group->move_group.moves.count;
  return iree_ok_status();
}

static iree_status_t loom_low_allocation_packet_move_build_region(
    loom_low_allocation_packet_move_builder_t* builder,
    const loom_region_t* region, uint32_t* inout_source_ordinal) {
  const loom_low_allocation_move_plan_context_t* move_context =
      &builder->context->move_plan->context;
  const loom_block_t* block = NULL;
  loom_region_for_each_block(region, block) {
    const loom_op_t* op = NULL;
    loom_block_for_each_op(block, op) {
      const loom_liveness_operation_point_t* operation_point =
          loom_low_allocation_move_plan_next_operation(
              builder->context->move_plan, op, &builder->cursor);
      const loom_low_allocation_packet_move_op_kind_t packet_move_kind =
          loom_low_allocation_move_topology_packet_move_op_kind(op);
      if (loom_low_allocation_packet_move_has_relations(op, packet_move_kind)) {
        IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_build_group(
            builder, operation_point, *inout_source_ordinal, packet_move_kind));
        if (move_context->target_constraints->error_count != 0) {
          return iree_ok_status();
        }
      }
      ++*inout_source_ordinal;
      if (!loom_liveness_analysis_includes_region_tree(
              move_context->assignment_map.liveness)) {
        continue;
      }
      loom_region_t* const* regions = loom_op_regions(op);
      for (uint8_t i = 0; i < op->region_count; ++i) {
        if (regions[i] == NULL) {
          continue;
        }
        IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_build_region(
            builder, regions[i], inout_source_ordinal));
        if (move_context->target_constraints->error_count != 0) {
          return iree_ok_status();
        }
      }
    }
  }
  return iree_ok_status();
}

iree_status_t loom_low_allocation_packet_move_plan_build(
    const loom_low_allocation_packet_move_context_t* context,
    iree_arena_allocator_t* arena,
    loom_low_allocation_packet_move_plan_t* out_plan) {
  *out_plan = (loom_low_allocation_packet_move_plan_t){0};
  const iree_host_size_t group_capacity =
      context->placement->packet_move_group_count;
  if (group_capacity == 0) {
    return iree_ok_status();
  }
  loom_low_allocation_packet_move_builder_t builder = {
      .context = context,
      .group_capacity = group_capacity,
  };
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, group_capacity,
                                                 sizeof(*builder.plan.groups),
                                                 (void**)&builder.plan.groups));
  uint32_t source_ordinal = 0;
  IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_count_region(
      &builder, context->move_plan->context.assignment_map.liveness->region,
      &source_ordinal));
  IREE_ASSERT_EQ(builder.plan.group_count, group_capacity);
  if (builder.plan.transfer_count != 0) {
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
        arena, builder.plan.transfer_count, sizeof(*builder.plan.transfers),
        (void**)&builder.plan.transfers));
  }
  source_ordinal = 0;
  IREE_RETURN_IF_ERROR(loom_low_allocation_packet_move_build_region(
      &builder, context->move_plan->context.assignment_map.liveness->region,
      &source_ordinal));
  if (context->move_plan->context.target_constraints->error_count == 0) {
    IREE_ASSERT_EQ(builder.build_group_index, group_capacity);
  }
  *out_plan = builder.plan;
  return iree_ok_status();
}
