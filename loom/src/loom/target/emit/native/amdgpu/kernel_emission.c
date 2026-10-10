// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/emit/native/amdgpu/kernel_emission.h"

#include <stdint.h>
#include <stdlib.h>

#include "loom/codegen/low/packet.h"
#include "loom/error/source.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"
#include "loom/ops/low/ops.h"
#include "loom/target/arch/amdgpu/planning/occupancy.h"
#include "loom/target/arch/amdgpu/planning/packet_plan.h"
#include "loom/target/arch/amdgpu/planning/wait_counters.h"
#include "loom/target/arch/amdgpu/refs/target_refs.h"
#include "loom/target/emit/native/amdgpu/encoding.h"
#include "loom/target/emit/native/amdgpu/kernel_assembly.h"
#include "loom/target/emit/native/amdgpu/kernel_entry.h"
#include "loom/target/emit/native/amdgpu/kernel_record.h"
#include "loom/target/emit/native/amdgpu/spill_lowering.h"
#include "loom/target/reporting/low_mix.h"
#include "loom/target/reporting/low_names.h"

static void loom_amdgpu_kernel_emission_accumulate_wait_action(
    loom_target_compile_report_wait_plan_t* summary,
    const loom_amdgpu_wait_plan_action_t* action) {
  ++summary->action_count;
  switch (action->kind) {
    case LOOM_AMDGPU_WAIT_PLAN_ACTION_EXPLICIT:
      ++summary->explicit_action_count;
      break;
    case LOOM_AMDGPU_WAIT_PLAN_ACTION_PLANNED:
      ++summary->planned_action_count;
      break;
    case LOOM_AMDGPU_WAIT_PLAN_ACTION_UNKNOWN:
    default:
      IREE_ASSERT(false, "wait plan action kind must be known");
      break;
  }
  if (action->target_count == 0) {
    ++summary->full_drain_count;
    summary->max_full_drain_outstanding_before =
        iree_max(summary->max_full_drain_outstanding_before,
                 (uint64_t)action->outstanding_before);
  } else {
    ++summary->partial_wait_count;
  }
  const uint64_t drained_count =
      action->outstanding_before > action->target_count
          ? (uint64_t)action->outstanding_before - action->target_count
          : 0;
  summary->drained_count += drained_count;
  summary->max_drained_count =
      iree_max(summary->max_drained_count, drained_count);
  summary->max_outstanding_before = iree_max(
      summary->max_outstanding_before, (uint64_t)action->outstanding_before);
}

static iree_string_view_t loom_amdgpu_kernel_emission_wait_action_name(
    loom_amdgpu_wait_plan_action_kind_t kind) {
  switch (kind) {
    case LOOM_AMDGPU_WAIT_PLAN_ACTION_EXPLICIT:
      return IREE_SV("explicit");
    case LOOM_AMDGPU_WAIT_PLAN_ACTION_PLANNED:
      return IREE_SV("planned");
    case LOOM_AMDGPU_WAIT_PLAN_ACTION_UNKNOWN:
    default:
      return IREE_SV("unknown");
  }
}

typedef struct loom_amdgpu_kernel_emission_wait_endpoint_t {
  // Node index in the schedule table, or UINT32_MAX.
  uint32_t node_index;
  // Scheduled ordinal for |node_index|, or UINT32_MAX.
  uint32_t scheduled_ordinal;
  // Operation mnemonic for |node_index|, or empty.
  iree_string_view_t operation_name;
  // Descriptor key for |node_index|, or empty.
  iree_string_view_t descriptor_key;
  // Descriptor semantic tag for |node_index|, or empty.
  iree_string_view_t semantic_tag;
} loom_amdgpu_kernel_emission_wait_endpoint_t;

static loom_amdgpu_kernel_emission_wait_endpoint_t
loom_amdgpu_kernel_emission_wait_endpoint(
    const loom_amdgpu_wait_plan_t* wait_plan, uint32_t node_index) {
  loom_amdgpu_kernel_emission_wait_endpoint_t endpoint = {
      .node_index = UINT32_MAX,
      .scheduled_ordinal = UINT32_MAX,
      .operation_name = iree_string_view_empty(),
      .descriptor_key = iree_string_view_empty(),
      .semantic_tag = iree_string_view_empty(),
  };
  const loom_low_schedule_table_t* schedule = wait_plan->schedule;
  if (node_index == LOOM_LOW_SCHEDULE_NODE_NONE ||
      node_index >= schedule->node_count) {
    return endpoint;
  }

  const loom_low_schedule_node_t* node = &schedule->nodes[node_index];
  endpoint.node_index = node_index;
  endpoint.scheduled_ordinal = node->scheduled_ordinal;
  if (node->op != NULL) {
    endpoint.operation_name = loom_op_name(schedule->module, node->op);
  }
  if (node->descriptor != NULL) {
    endpoint.descriptor_key = loom_low_descriptor_set_string(
        schedule->target.descriptor_set, node->descriptor->key_string_ref);
    if (node->descriptor->semantic_tag_string_ref != LOOM_STRING_REF_NONE) {
      endpoint.semantic_tag = loom_low_descriptor_set_string(
          schedule->target.descriptor_set,
          node->descriptor->semantic_tag_string_ref);
    }
  }
  return endpoint;
}

static iree_status_t loom_amdgpu_kernel_emission_record_wait_plan(
    loom_target_compile_report_t* report,
    const loom_amdgpu_packet_plan_t* packet_plan) {
  if (report == NULL || packet_plan->wait_plan.action_count == 0) {
    return iree_ok_status();
  }
  const loom_amdgpu_wait_plan_t* wait_plan = &packet_plan->wait_plan;
  loom_target_compile_report_wait_plan_t summary = {0};
  loom_target_compile_report_wait_plan_t
      counter_summaries[LOOM_AMDGPU_WAIT_COUNTER_SLOT_COUNT] = {0};
  loom_target_compile_report_wait_plan_t
      reason_summaries[LOOM_AMDGPU_WAIT_COUNTER_SLOT_COUNT]
                      [LOOM_AMDGPU_WAIT_PLAN_REASON_COUNT] = {0};
  for (iree_host_size_t i = 0; i < wait_plan->action_count; ++i) {
    const loom_amdgpu_wait_plan_action_t* action = &wait_plan->actions[i];
    IREE_ASSERT(loom_amdgpu_wait_counter_id_is_valid(action->counter_id),
                "wait plan action must name a concrete counter");
    IREE_ASSERT(action->reason < LOOM_AMDGPU_WAIT_PLAN_REASON_COUNT,
                "wait plan action must name a concrete reason");
    loom_amdgpu_kernel_emission_accumulate_wait_action(&summary, action);
    const uint32_t counter_index =
        loom_amdgpu_wait_counter_slot_from_id(action->counter_id);
    loom_amdgpu_kernel_emission_accumulate_wait_action(
        &counter_summaries[counter_index], action);
    loom_amdgpu_kernel_emission_accumulate_wait_action(
        &reason_summaries[counter_index][action->reason], action);
  }
  loom_target_compile_report_record_wait_plan(report, &summary);
  for (uint32_t i = 0; i < LOOM_AMDGPU_WAIT_COUNTER_SLOT_COUNT; ++i) {
    if (counter_summaries[i].action_count == 0) {
      continue;
    }
    const uint32_t counter_id = i + 1;
    const loom_target_compile_report_wait_counter_row_t row = {
        .function_name = report->function_name,
        .counter_name = loom_amdgpu_wait_counter_name(counter_id),
        .counter_id = counter_id,
        .summary = counter_summaries[i],
    };
    IREE_RETURN_IF_ERROR(
        loom_target_compile_report_record_wait_counter_row(report, &row));
  }
  for (uint32_t i = 0; i < LOOM_AMDGPU_WAIT_COUNTER_SLOT_COUNT; ++i) {
    const uint32_t counter_id = i + 1;
    const iree_string_view_t counter_name =
        loom_amdgpu_wait_counter_name(counter_id);
    for (uint32_t reason_id = 0; reason_id < LOOM_AMDGPU_WAIT_PLAN_REASON_COUNT;
         ++reason_id) {
      if (reason_summaries[i][reason_id].action_count == 0) {
        continue;
      }
      const loom_target_compile_report_wait_reason_summary_row_t row = {
          .function_name = report->function_name,
          .counter_name = counter_name,
          .reason_name = loom_amdgpu_wait_plan_reason_name(
              (loom_amdgpu_wait_plan_reason_t)reason_id),
          .counter_id = counter_id,
          .reason_id = reason_id,
          .summary = reason_summaries[i][reason_id],
      };
      IREE_RETURN_IF_ERROR(
          loom_target_compile_report_record_wait_reason_summary_row(report,
                                                                    &row));
    }
  }
  if (!loom_target_compile_report_wants_details(
          report, LOOM_TARGET_COMPILE_REPORT_DETAIL_WAIT_PLAN)) {
    return iree_ok_status();
  }
  for (iree_host_size_t i = 0; i < wait_plan->action_count; ++i) {
    const loom_amdgpu_wait_plan_action_t* action = &wait_plan->actions[i];
    const uint32_t outstanding_after = action->target_count;
    const uint32_t drained_count =
        action->outstanding_before > outstanding_after
            ? action->outstanding_before - outstanding_after
            : 0;
    const loom_amdgpu_kernel_emission_wait_endpoint_t producer =
        loom_amdgpu_kernel_emission_wait_endpoint(wait_plan,
                                                  action->producer_node);
    const loom_amdgpu_kernel_emission_wait_endpoint_t consumer =
        loom_amdgpu_kernel_emission_wait_endpoint(wait_plan,
                                                  action->consumer_node);
    const loom_target_compile_report_wait_action_row_t row = {
        .function_name = report->function_name,
        .counter_name = loom_amdgpu_wait_counter_name(action->counter_id),
        .action_name =
            loom_amdgpu_kernel_emission_wait_action_name(action->kind),
        .reason_name = loom_amdgpu_wait_plan_reason_name(action->reason),
        .counter_id = action->counter_id,
        .action_id = (uint32_t)action->kind,
        .reason_id = (uint32_t)action->reason,
        .block_index = action->block_index,
        .node_index = action->node_index,
        .scheduled_ordinal = action->scheduled_ordinal,
        .producer_node = producer.node_index,
        .producer_scheduled_ordinal = producer.scheduled_ordinal,
        .producer_operation_name = producer.operation_name,
        .producer_descriptor_key = producer.descriptor_key,
        .producer_semantic_tag = producer.semantic_tag,
        .consumer_node = consumer.node_index,
        .consumer_scheduled_ordinal = consumer.scheduled_ordinal,
        .consumer_operation_name = consumer.operation_name,
        .consumer_descriptor_key = consumer.descriptor_key,
        .consumer_semantic_tag = consumer.semantic_tag,
        .target_count = action->target_count,
        .outstanding_before = action->outstanding_before,
        .outstanding_after = outstanding_after,
        .drained_count = drained_count,
    };
    IREE_RETURN_IF_ERROR(
        loom_target_compile_report_record_wait_action_row(report, &row));
  }
  return iree_ok_status();
}

static iree_status_t loom_amdgpu_kernel_emission_record_native_insertions(
    loom_target_compile_report_t* report,
    const loom_low_emission_frame_t* frame,
    const loom_amdgpu_encoded_instruction_stream_t* stream) {
  if (report == NULL || stream->native_insertion_count == 0) {
    return iree_ok_status();
  }

  const loom_low_descriptor_set_t* descriptor_set =
      frame->schedule.target.descriptor_set;
  loom_target_compile_report_low_dynamic_context_t dynamic_context = {0};
  iree_status_t status =
      loom_target_compile_report_low_dynamic_context_initialize(
          frame, &dynamic_context);
  for (iree_host_size_t i = 0;
       i < stream->native_insertion_count && iree_status_is_ok(status); ++i) {
    const loom_amdgpu_native_insertion_t* insertion =
        &stream->native_insertions[i];
    const loom_low_schedule_node_t* node =
        &frame->schedule.nodes[insertion->node_index];
    const loom_low_schedule_block_t* block =
        &frame->schedule.blocks[insertion->block_index];
    loom_target_compile_report_target_insertion_kind_t insertion_kind =
        LOOM_TARGET_COMPILE_REPORT_TARGET_INSERTION_NONE;
    iree_string_view_t packet_key = iree_string_view_empty();
    switch (insertion->kind) {
      case LOOM_AMDGPU_NATIVE_INSERTION_ADDRESS_STATE:
        insertion_kind = LOOM_TARGET_COMPILE_REPORT_TARGET_INSERTION_STATE;
        break;
      case LOOM_AMDGPU_NATIVE_INSERTION_WAIT:
        insertion_kind = LOOM_TARGET_COMPILE_REPORT_TARGET_INSERTION_WAIT;
        break;
      case LOOM_AMDGPU_NATIVE_INSERTION_S_NOP:
        insertion_kind = LOOM_TARGET_COMPILE_REPORT_TARGET_INSERTION_DELAY;
        packet_key = IREE_SV("amdgpu.s_nop");
        break;
      case LOOM_AMDGPU_NATIVE_INSERTION_S_DELAY_ALU:
        insertion_kind = LOOM_TARGET_COMPILE_REPORT_TARGET_INSERTION_DELAY;
        break;
      case LOOM_AMDGPU_NATIVE_INSERTION_V_NOP:
        insertion_kind = LOOM_TARGET_COMPILE_REPORT_TARGET_INSERTION_DELAY;
        packet_key = IREE_SV("amdgpu.v_nop");
        break;
      case LOOM_AMDGPU_NATIVE_INSERTION_BRANCH_ISLAND_SKIP:
      case LOOM_AMDGPU_NATIVE_INSERTION_BRANCH_ISLAND_HOP:
        insertion_kind = LOOM_TARGET_COMPILE_REPORT_TARGET_INSERTION_OTHER;
        packet_key = IREE_SV("amdgpu.s_branch");
        break;
      case LOOM_AMDGPU_NATIVE_INSERTION_NONE:
      default:
        IREE_ASSERT_UNREACHABLE(
            "native encoder produced an invalid insertion kind");
        IREE_BUILTIN_UNREACHABLE();
    }
    if (insertion->descriptor_ref != LOOM_AMDGPU_DESCRIPTOR_REF_NONE) {
      const loom_low_descriptor_t* packet_descriptor =
          loom_amdgpu_descriptor_ref_descriptor(descriptor_set,
                                                insertion->descriptor_ref);
      IREE_ASSERT(packet_descriptor != NULL);
      packet_key = loom_low_descriptor_set_string(
          descriptor_set, packet_descriptor->key_string_ref);
    }
    iree_string_view_t boundary_descriptor_key = iree_string_view_empty();
    if (node->descriptor != NULL) {
      boundary_descriptor_key = loom_low_descriptor_set_string(
          descriptor_set, node->descriptor->key_string_ref);
    }
    loom_target_compile_report_target_insertion_row_t row = {
        .function_name = report->function_name,
        .insertion_kind = insertion_kind,
        .packet_key = packet_key,
        .block_name =
            loom_target_compile_report_block_name(frame->module, block->block),
        .block_index = insertion->block_index,
        .node_index = insertion->node_index,
        .scheduled_ordinal = insertion->scheduled_ordinal,
        .boundary_operation_name = node->op != NULL
                                       ? loom_op_name(frame->module, node->op)
                                       : iree_string_view_empty(),
        .boundary_descriptor_key = boundary_descriptor_key,
        .static_packet_count = 1,
    };
    uint64_t execution_multiplier = 0;
    if (insertion->kind != LOOM_AMDGPU_NATIVE_INSERTION_BRANCH_ISLAND_HOP &&
        loom_target_compile_report_low_node_execution_multiplier(
            frame->module, &dynamic_context, node, &execution_multiplier)) {
      row.flags =
          LOOM_TARGET_COMPILE_REPORT_TARGET_INSERTION_FLAG_DYNAMIC_PACKET_COUNT;
      row.dynamic_packet_count = execution_multiplier;
    }
    status =
        loom_target_compile_report_record_target_insertion_row(report, &row);
  }
  loom_target_compile_report_low_dynamic_context_deinitialize(&dynamic_context);
  return status;
}

static loom_target_compile_report_target_resources_t
loom_amdgpu_kernel_emission_target_resources(
    const loom_amdgpu_occupancy_target_resources_t* target_resources) {
  return (loom_target_compile_report_target_resources_t){
      .scalar_register_class = target_resources->scalar_register_class,
      .scalar_register_count = target_resources->scalar_register_count,
      .vector_register_class = target_resources->vector_register_class,
      .vector_register_count = target_resources->vector_register_count,
      .subgroup_size = target_resources->wave_size,
      .max_subgroups_per_simd = target_resources->max_waves_per_simd,
      .resident_subgroups_per_simd = target_resources->resident_waves_per_simd,
      .occupancy_percent = target_resources->occupancy_percent,
      .limiting_resource = target_resources->limiting_resource,
      .residency_summary = target_resources->residency_summary,
  };
}

static iree_status_t loom_amdgpu_kernel_emission_record_summary(
    loom_target_compile_report_t* report,
    const loom_amdgpu_kernel_record_t* record,
    const loom_amdgpu_encoded_instruction_stream_t* stream,
    const loom_amdgpu_kernel_entry_envelope_t* entry_envelope,
    const loom_amdgpu_packet_plan_t* packet_plan,
    iree_const_byte_span_t kernel_text, iree_arena_allocator_t* table_arena) {
  if (report == NULL) {
    return iree_ok_status();
  }

  uint64_t instruction_count = 0;
  if (!iree_checked_add_u64(stream->instruction_count,
                            entry_envelope->instruction_count,
                            &instruction_count)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "AMDGPU kernel entry instruction count overflowed");
  }
  const uint64_t coissued_instruction_count =
      (uint64_t)packet_plan->vopd_plan.pair_count;
  loom_target_compile_report_record_emission(report, instruction_count,
                                             kernel_text.data_length,
                                             kernel_text.data_length);
  const loom_target_compile_report_emission_breakdown_t emission_breakdown = {
      .body_instruction_count = stream->instruction_count,
      .entry_instruction_count = entry_envelope->instruction_count,
      .coissued_instruction_count = coissued_instruction_count,
      .coissued_component_count = coissued_instruction_count * 2u,
  };
  loom_target_compile_report_record_emission_breakdown(report,
                                                       &emission_breakdown);
  loom_target_compile_report_record_memory(
      report, record->metadata.private_segment_fixed_size,
      record->metadata.group_segment_fixed_size);

  loom_target_residency_constraint_list_t residency_constraints = {0};
  const bool capture_residency_constraints =
      iree_any_bit_set(report->requested_detail_flags,
                       LOOM_TARGET_COMPILE_REPORT_DETAIL_RESIDENCY_CONSTRAINTS);
  loom_amdgpu_occupancy_target_resources_t occupancy = {0};
  const uint32_t flat_workgroup_size =
      record->metadata.has_required_workgroup_size
          ? record->metadata.max_flat_workgroup_size
          : 0;
  IREE_RETURN_IF_ERROR(loom_amdgpu_occupancy_build_target_resources(
      record->processor, record->metadata.wavefront_size,
      record->metadata.sgpr_count, record->metadata.vgpr_count,
      flat_workgroup_size, record->metadata.group_segment_fixed_size,
      table_arena, &occupancy,
      capture_residency_constraints ? &residency_constraints : NULL));
  const loom_target_compile_report_target_resources_t target_resources =
      loom_amdgpu_kernel_emission_target_resources(&occupancy);
  loom_target_compile_report_record_target_resources(report, &target_resources);
  if (capture_residency_constraints) {
    IREE_RETURN_IF_ERROR(
        loom_target_compile_report_record_residency_constraints(
            report, &residency_constraints));
  }
  return iree_ok_status();
}

static iree_string_view_t loom_amdgpu_kernel_emission_instruction_kind_name(
    loom_amdgpu_native_insertion_kind_t kind) {
  switch (kind) {
    case LOOM_AMDGPU_NATIVE_INSERTION_NONE:
      return IREE_SV("packet");
    case LOOM_AMDGPU_NATIVE_INSERTION_ADDRESS_STATE:
      return IREE_SV("address_state");
    case LOOM_AMDGPU_NATIVE_INSERTION_WAIT:
      return IREE_SV("wait");
    case LOOM_AMDGPU_NATIVE_INSERTION_S_NOP:
      return IREE_SV("s_nop");
    case LOOM_AMDGPU_NATIVE_INSERTION_S_DELAY_ALU:
      return IREE_SV("s_delay_alu");
    case LOOM_AMDGPU_NATIVE_INSERTION_V_NOP:
      return IREE_SV("v_nop");
    case LOOM_AMDGPU_NATIVE_INSERTION_BRANCH_ISLAND_SKIP:
      return IREE_SV("branch_island_skip");
    case LOOM_AMDGPU_NATIVE_INSERTION_BRANCH_ISLAND_HOP:
      return IREE_SV("branch_island_hop");
    default:
      return IREE_SV("unknown");
  }
}

// Names a waited-on producer by its descriptor key, or by its operation
// mnemonic for structural nodes.
static iree_string_view_t loom_amdgpu_kernel_emission_producer_name(
    const loom_low_emission_frame_t* frame, uint32_t producer_node) {
  if (producer_node >= frame->schedule.node_count) {
    return iree_string_view_empty();
  }
  const loom_low_schedule_node_t* producer =
      &frame->schedule.nodes[producer_node];
  if (producer->descriptor != NULL) {
    return loom_low_descriptor_set_string(frame->schedule.target.descriptor_set,
                                          producer->descriptor->key_string_ref);
  }
  return producer->op != NULL ? loom_op_name(frame->module, producer->op)
                              : iree_string_view_empty();
}

// Fills the wait fields of |row| from the wait packet or wait state that
// produced |entry|. Coalesced counter waits name their first planned action.
static void loom_amdgpu_kernel_emission_attribute_wait(
    const loom_low_emission_frame_t* frame,
    const loom_amdgpu_packet_plan_t* packet_plan,
    const loom_amdgpu_instruction_map_entry_t* entry,
    loom_target_compile_report_code_attribution_row_t* row) {
  if (entry->wait_packet_index != UINT32_MAX &&
      entry->wait_packet_index < packet_plan->wait_packets.packet_count) {
    const loom_amdgpu_wait_packet_t* wait_packet =
        &packet_plan->wait_packets.packets[entry->wait_packet_index];
    row->wait_action_count = (uint32_t)wait_packet->source_action_count;
    if (wait_packet->source_action_count != 0 &&
        wait_packet->source_action_start <
            packet_plan->wait_plan.action_count) {
      const loom_amdgpu_wait_plan_action_t* action =
          &packet_plan->wait_plan.actions[wait_packet->source_action_start];
      row->wait_reason = loom_amdgpu_wait_plan_reason_name(action->reason);
      row->wait_producer_name = loom_amdgpu_kernel_emission_producer_name(
          frame, action->producer_node);
    }
  } else if (entry->wait_state_index != UINT32_MAX &&
             entry->wait_state_index < packet_plan->wait_states.state_count) {
    const loom_amdgpu_wait_state_t* wait_state =
        &packet_plan->wait_states.states[entry->wait_state_index];
    row->wait_action_count = 1;
    row->wait_reason = loom_amdgpu_wait_state_reason_name(wait_state->reason);
    row->wait_producer_name = loom_amdgpu_kernel_emission_producer_name(
        frame, wait_state->producer_node);
  }
}

// One private-segment reservation, ordered by byte offset.
typedef struct loom_amdgpu_kernel_emission_segment_range_t {
  // First byte of the reservation in the private segment.
  uint64_t byte_offset;
  // Reservation size in bytes.
  uint64_t byte_size;
  // low.storage.reserve result owning the range.
  loom_value_id_t storage_value_id;
} loom_amdgpu_kernel_emission_segment_range_t;

// Links scratch packets back to the spills they serve.
typedef struct loom_amdgpu_kernel_emission_spill_index_t {
  // Private-segment reservations sorted by |byte_offset|.
  loom_amdgpu_kernel_emission_segment_range_t* ranges;
  // Number of entries in |ranges|.
  iree_host_size_t range_count;
  // Materialized spills sorted by storage value.
  const loom_low_allocation_materialized_spill_t** spills;
  // Number of entries in |spills|.
  iree_host_size_t spill_count;
  // Interned "offset" immediate name, or LOOM_STRING_ID_INVALID.
  loom_string_id_t offset_name_id;
} loom_amdgpu_kernel_emission_spill_index_t;

static int loom_amdgpu_kernel_emission_compare_ranges(const void* lhs,
                                                      const void* rhs) {
  const loom_amdgpu_kernel_emission_segment_range_t* a = lhs;
  const loom_amdgpu_kernel_emission_segment_range_t* b = rhs;
  return a->byte_offset < b->byte_offset ? -1
                                         : (a->byte_offset > b->byte_offset);
}

static int loom_amdgpu_kernel_emission_compare_spills(const void* lhs,
                                                      const void* rhs) {
  const loom_value_id_t a =
      (*(const loom_low_allocation_materialized_spill_t* const*)lhs)
          ->storage_value_id;
  const loom_value_id_t b =
      (*(const loom_low_allocation_materialized_spill_t* const*)rhs)
          ->storage_value_id;
  return a < b ? -1 : (a > b);
}

static iree_status_t loom_amdgpu_kernel_emission_build_spill_index(
    const loom_low_emission_frame_t* frame,
    const loom_amdgpu_storage_layout_t* storage_layout,
    iree_arena_allocator_t* arena,
    loom_amdgpu_kernel_emission_spill_index_t* out_index) {
  *out_index = (loom_amdgpu_kernel_emission_spill_index_t){
      .offset_name_id =
          loom_module_lookup_string(frame->module, IREE_SV("offset")),
  };
  if (storage_layout->record_count != 0) {
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
        arena, storage_layout->record_count, sizeof(*out_index->ranges),
        (void**)&out_index->ranges));
  }
  for (iree_host_size_t i = 0; i < storage_layout->record_count; ++i) {
    const loom_amdgpu_storage_layout_record_t* record =
        &storage_layout->records[i];
    if (record->reservation.space != LOOM_STORAGE_SPACE_PRIVATE &&
        record->reservation.space != LOOM_STORAGE_SPACE_SCRATCH) {
      continue;
    }
    out_index->ranges[out_index->range_count++] =
        (loom_amdgpu_kernel_emission_segment_range_t){
            .byte_offset = record->reservation.byte_offset,
            .byte_size = record->reservation.byte_size,
            .storage_value_id = record->storage_value_id,
        };
  }
  qsort(out_index->ranges, out_index->range_count, sizeof(*out_index->ranges),
        loom_amdgpu_kernel_emission_compare_ranges);

  const iree_host_size_t spill_count = frame->materialized_spills.record_count;
  if (spill_count != 0) {
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, spill_count,
                                                   sizeof(*out_index->spills),
                                                   (void**)&out_index->spills));
  }
  for (const loom_low_allocation_materialized_spill_vec_t* vec =
           frame->materialized_spills.head;
       vec != NULL; vec = vec->next) {
    for (iree_host_size_t i = 0; i < vec->record_count; ++i) {
      out_index->spills[out_index->spill_count++] = &vec->records[i];
    }
  }
  qsort(out_index->spills, out_index->spill_count, sizeof(*out_index->spills),
        loom_amdgpu_kernel_emission_compare_spills);
  return iree_ok_status();
}

// Returns the reservation containing private-segment byte |offset|.
static loom_value_id_t loom_amdgpu_kernel_emission_storage_at_offset(
    const loom_amdgpu_kernel_emission_spill_index_t* index, uint64_t offset) {
  iree_host_size_t low = 0;
  iree_host_size_t high = index->range_count;
  while (low < high) {
    const iree_host_size_t mid = low + (high - low) / 2;
    if (index->ranges[mid].byte_offset <= offset) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  if (low == 0) {
    return LOOM_VALUE_ID_INVALID;
  }
  const loom_amdgpu_kernel_emission_segment_range_t* range =
      &index->ranges[low - 1];
  return offset - range->byte_offset < range->byte_size
             ? range->storage_value_id
             : LOOM_VALUE_ID_INVALID;
}

static const loom_low_allocation_materialized_spill_t*
loom_amdgpu_kernel_emission_spill_for_storage(
    const loom_amdgpu_kernel_emission_spill_index_t* index,
    loom_value_id_t storage_value_id) {
  iree_host_size_t low = 0;
  iree_host_size_t high = index->spill_count;
  while (low < high) {
    const iree_host_size_t mid = low + (high - low) / 2;
    if (index->spills[mid]->storage_value_id < storage_value_id) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low < index->spill_count &&
                 index->spills[low]->storage_value_id == storage_value_id
             ? index->spills[low]
             : NULL;
}

// Returns the storage a lowered scratch packet accesses: the storage operand
// of its address computation when it has one, otherwise the reservation that
// contains its immediate private-segment offset.
static loom_value_id_t loom_amdgpu_kernel_emission_scratch_storage(
    const loom_low_emission_frame_t* frame,
    const loom_amdgpu_kernel_emission_spill_index_t* index,
    uint32_t node_index) {
  const loom_low_schedule_node_t* node = &frame->schedule.nodes[node_index];
  if (node->op->operand_count != 0) {
    const loom_value_t* address =
        loom_module_value(frame->module, loom_op_const_operands(node->op)[0]);
    const loom_op_t* address_op = loom_value_def_op(address);
    if (address_op != NULL && loom_low_storage_address_isa(address_op)) {
      return loom_low_storage_address_storage(address_op);
    }
  }
  if (index->offset_name_id == LOOM_STRING_ID_INVALID) {
    return LOOM_VALUE_ID_INVALID;
  }
  const loom_low_packet_view_t packet =
      loom_low_packet_at_node(&frame->schedule, node_index);
  const loom_named_attr_slice_t attrs = loom_low_packet_attrs(&packet);
  for (iree_host_size_t i = 0; i < attrs.count; ++i) {
    if (attrs.entries[i].name_id == index->offset_name_id) {
      const int64_t offset = loom_attr_as_i64(attrs.entries[i].value);
      return offset >= 0 ? loom_amdgpu_kernel_emission_storage_at_offset(
                               index, (uint64_t)offset)
                         : LOOM_VALUE_ID_INVALID;
    }
  }
  return LOOM_VALUE_ID_INVALID;
}

// Resolves |location| to a single source file position.
static void loom_amdgpu_kernel_emission_resolve_source(
    const loom_module_t* module, loom_location_id_t location,
    iree_string_view_t* out_filename, uint32_t* out_line,
    uint32_t* out_column) {
  loom_source_range_t range;
  if (loom_source_resolve((loom_source_resolver_t){0}, module, location,
                          &range)) {
    *out_filename = range.filename;
    *out_line = range.start_line;
    *out_column = range.start_column;
  }
}

// Fills the scratch fields of |row| when |node_index| accesses scratch,
// linking spill slots to the spilled value.
static void loom_amdgpu_kernel_emission_attribute_scratch(
    const loom_low_emission_frame_t* frame,
    const loom_amdgpu_kernel_emission_spill_index_t* index, uint32_t node_index,
    loom_target_compile_report_code_attribution_row_t* row) {
  if (node_index >= frame->schedule.node_count) {
    return;
  }
  const loom_low_schedule_node_t* node = &frame->schedule.nodes[node_index];
  const loom_amdgpu_scratch_access_kind_t kind =
      loom_amdgpu_spill_lowering_classify_descriptor(
          frame->schedule.target.descriptor_set, node->descriptor);
  if (kind == LOOM_AMDGPU_SCRATCH_ACCESS_NONE || node->op == NULL) {
    return;
  }
  row->scratch_access = kind == LOOM_AMDGPU_SCRATCH_ACCESS_LOAD
                            ? IREE_SV("load")
                            : IREE_SV("store");
  const loom_value_id_t storage_value_id =
      loom_amdgpu_kernel_emission_scratch_storage(frame, index, node_index);
  if (storage_value_id == LOOM_VALUE_ID_INVALID) {
    return;
  }
  row->scratch_storage_name =
      loom_target_compile_report_value_name(frame->module, storage_value_id);
  const loom_low_allocation_materialized_spill_t* spill =
      loom_amdgpu_kernel_emission_spill_for_storage(index, storage_value_id);
  const loom_value_id_t source_value_id =
      spill != NULL ? spill->value_id : storage_value_id;
  if (spill != NULL) {
    row->spill_value_name =
        loom_target_compile_report_value_name(frame->module, spill->value_id);
    row->spill_slot_index = spill->slot_index;
  }
  const loom_op_t* source_op =
      loom_value_def_op(loom_module_value(frame->module, source_value_id));
  if (source_op != NULL) {
    loom_amdgpu_kernel_emission_resolve_source(
        frame->module, source_op->location, &row->scratch_source_filename,
        &row->scratch_source_line, &row->scratch_source_column);
  }
}

// Returns the waited-on producer node of |entry|, or UINT32_MAX.
static uint32_t loom_amdgpu_kernel_emission_wait_producer(
    const loom_amdgpu_packet_plan_t* packet_plan,
    const loom_amdgpu_instruction_map_entry_t* entry) {
  if (entry->wait_packet_index != UINT32_MAX &&
      entry->wait_packet_index < packet_plan->wait_packets.packet_count) {
    const loom_amdgpu_wait_packet_t* wait_packet =
        &packet_plan->wait_packets.packets[entry->wait_packet_index];
    if (wait_packet->source_action_count != 0 &&
        wait_packet->source_action_start <
            packet_plan->wait_plan.action_count) {
      return packet_plan->wait_plan.actions[wait_packet->source_action_start]
          .producer_node;
    }
  } else if (entry->wait_state_index != UINT32_MAX &&
             entry->wait_state_index < packet_plan->wait_states.state_count) {
    return packet_plan->wait_states.states[entry->wait_state_index]
        .producer_node;
  }
  return UINT32_MAX;
}

static iree_status_t loom_amdgpu_kernel_emission_record_code_attribution(
    loom_target_compile_report_t* report,
    const loom_low_emission_frame_t* frame,
    const loom_amdgpu_packet_plan_t* packet_plan,
    const loom_amdgpu_kernel_entry_envelope_t* entry_envelope,
    const loom_amdgpu_storage_layout_t* storage_layout,
    const loom_amdgpu_encoded_instruction_stream_t* stream,
    iree_arena_allocator_t* arena) {
  if (report == NULL || stream->instruction_map_count == 0) {
    return iree_ok_status();
  }
  loom_amdgpu_kernel_emission_spill_index_t spill_index;
  IREE_RETURN_IF_ERROR(loom_amdgpu_kernel_emission_build_spill_index(
      frame, storage_layout, arena, &spill_index));
  const loom_low_descriptor_set_t* descriptor_set =
      frame->schedule.target.descriptor_set;
  // The function symbol begins with the target entry envelope, if any.
  const uint64_t body_offset = entry_envelope->text.data_length;
  iree_status_t status = iree_ok_status();
  for (iree_host_size_t i = 0;
       i < stream->instruction_map_count && iree_status_is_ok(status); ++i) {
    const loom_amdgpu_instruction_map_entry_t* entry =
        &stream->instruction_map[i];
    const loom_low_schedule_node_t* node =
        &frame->schedule.nodes[entry->node_index];
    const loom_low_schedule_block_t* block =
        &frame->schedule.blocks[node->block_index];
    loom_target_compile_report_code_attribution_row_t row = {
        .function_name = report->function_name,
        .symbol_offset = body_offset + entry->byte_offset,
        .byte_size = entry->byte_size,
        .instruction_kind = loom_amdgpu_kernel_emission_instruction_kind_name(
            entry->insertion_kind),
        .block_name =
            loom_target_compile_report_block_name(frame->module, block->block),
        .block_index = node->block_index,
        .node_index = entry->node_index,
        .scheduled_ordinal = node->scheduled_ordinal,
        .spill_slot_index = UINT32_MAX,
    };
    if (node->descriptor != NULL) {
      row.descriptor_key = loom_low_descriptor_set_string(
          descriptor_set, node->descriptor->key_string_ref);
    }
    if (node->op != NULL) {
      row.operation_name = loom_op_name(frame->module, node->op);
      loom_source_range_t range;
      if (loom_source_resolve((loom_source_resolver_t){0}, frame->module,
                              node->op->location, &range)) {
        row.source_filename = range.filename;
        row.source_line = range.start_line;
        row.source_column = range.start_column;
      }
    }
    loom_amdgpu_kernel_emission_attribute_wait(frame, packet_plan, entry, &row);
    // A packet describes its own scratch access; a wait describes the access
    // it waits on.
    loom_amdgpu_kernel_emission_attribute_scratch(
        frame, &spill_index,
        row.wait_action_count != 0
            ? loom_amdgpu_kernel_emission_wait_producer(packet_plan, entry)
            : (entry->insertion_kind == LOOM_AMDGPU_NATIVE_INSERTION_NONE
                   ? entry->node_index
                   : UINT32_MAX),
        &row);
    status =
        loom_target_compile_report_record_code_attribution_row(report, &row);
  }
  return status;
}

iree_status_t loom_amdgpu_kernel_emission_build(
    const loom_low_emission_frame_t* frame,
    const loom_amdgpu_hal_kernel_abi_layout_t* abi_layout,
    const loom_amdgpu_hal_kernel_abi_verify_result_t* abi_verify,
    const loom_amdgpu_native_preflight_t* preflight,
    iree_string_builder_t* target_listing, loom_target_compile_report_t* report,
    loom_amdgpu_hsaco_kernel_t* out_kernel,
    iree_arena_allocator_t* table_arena) {
  *out_kernel = (loom_amdgpu_hsaco_kernel_t){0};
  loom_amdgpu_packet_plan_t packet_plan = {0};
  IREE_RETURN_IF_ERROR(loom_amdgpu_packet_plan_build(
      &frame->schedule, &frame->allocation, table_arena, &packet_plan));
  IREE_RETURN_IF_ERROR(
      loom_amdgpu_kernel_emission_record_wait_plan(report, &packet_plan));

  loom_amdgpu_kernel_record_t record = {0};
  const loom_amdgpu_kernel_record_options_t record_options = {
      .abi_layout = abi_layout,
      .abi_verify = abi_verify,
      .preflight = preflight,
  };
  IREE_RETURN_IF_ERROR(
      loom_amdgpu_kernel_record_build(&frame->schedule, &frame->allocation,
                                      &record_options, &record, table_arena));

  loom_amdgpu_encoded_instruction_stream_t stream = {0};
  const loom_amdgpu_encode_instruction_stream_options_t encode_options = {
      .packet_plan = &packet_plan,
      .storage_layout = &record.storage_layout,
      .flags =
          (report != NULL
               ? LOOM_AMDGPU_ENCODE_INSTRUCTION_STREAM_FLAG_CAPTURE_NATIVE_INSERTIONS
               : LOOM_AMDGPU_ENCODE_INSTRUCTION_STREAM_FLAG_NONE) |
          (report != NULL &&
                   loom_target_compile_report_wants_details(
                       report,
                       LOOM_TARGET_COMPILE_REPORT_DETAIL_CODE_ATTRIBUTION_ROWS)
               ? LOOM_AMDGPU_ENCODE_INSTRUCTION_STREAM_FLAG_CAPTURE_INSTRUCTION_MAP
               : LOOM_AMDGPU_ENCODE_INSTRUCTION_STREAM_FLAG_NONE),
  };
  IREE_RETURN_IF_ERROR(
      loom_amdgpu_encode_instruction_stream_result_with_options(
          &frame->schedule, &frame->allocation, &encode_options, &stream,
          table_arena));

  const loom_amdgpu_kernel_entry_envelope_t* entry_envelope =
      loom_amdgpu_kernel_entry_envelope_for_properties(
          &record.processor->properties);
  iree_const_byte_span_t kernel_text = iree_const_byte_span_empty();
  const loom_amdgpu_hsaco_text_fixup_t* kernel_text_fixups = NULL;
  IREE_RETURN_IF_ERROR(loom_amdgpu_kernel_entry_prepend_text(
      entry_envelope, stream.text, stream.text_fixups, stream.text_fixup_count,
      &kernel_text, &kernel_text_fixups, table_arena));
  *out_kernel = (loom_amdgpu_hsaco_kernel_t){
      .metadata = record.metadata,
      .descriptor_options =
          {
              .flags = record.descriptor_flags,
              .minimum_user_sgpr_count = record.user_sgpr_count,
          },
      .text = kernel_text,
      .text_fixups = kernel_text_fixups,
      .text_fixup_count = stream.text_fixup_count,
  };

  if (target_listing != NULL) {
    if (iree_string_builder_size(target_listing) != 0) {
      IREE_RETURN_IF_ERROR(
          iree_string_builder_append_cstring(target_listing, "\n\n"));
    }
    IREE_RETURN_IF_ERROR(loom_amdgpu_emit_kernel_assembly(
        &frame->schedule, &frame->allocation, &record, &packet_plan,
        &stream.layout, target_listing, table_arena));
  }

  IREE_RETURN_IF_ERROR(loom_amdgpu_kernel_emission_record_native_insertions(
      report, frame, &stream));
  IREE_RETURN_IF_ERROR(loom_amdgpu_kernel_emission_record_code_attribution(
      report, frame, &packet_plan, entry_envelope, &record.storage_layout,
      &stream, table_arena));
  return loom_amdgpu_kernel_emission_record_summary(
      report, &record, &stream, entry_envelope, &packet_plan, kernel_text,
      table_arena);
}
