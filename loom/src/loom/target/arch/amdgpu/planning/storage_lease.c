// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amdgpu/planning/storage_lease.h"

#include "loom/target/arch/amdgpu/planning/structural_packet.h"
#include "loom/target/arch/amdgpu/planning/wait_counters.h"
#include "loom/target/arch/amdgpu/target_info_defs.h"

typedef struct loom_amdgpu_storage_lease_query_t {
  // The producer block's native exit guarantees XCNT completion.
  bool block_exit_drains_xcnt;
  // Downstream storage-lease event sink.
  loom_low_storage_lease_emit_fn_t emit;
  // Opaque context passed to |emit|.
  void* emit_user_data;
} loom_amdgpu_storage_lease_query_t;

static bool loom_amdgpu_storage_lease_block_exit_drains_xcnt(
    const loom_low_schedule_table_t* schedule,
    const loom_low_schedule_node_t* node) {
  const loom_low_schedule_block_t* block = &schedule->blocks[node->block_index];
  const loom_low_schedule_node_t* exit_node =
      &schedule->nodes[block->node_start + block->node_count - 1u];
  return loom_amdgpu_structural_packet_control_transfer_count(schedule,
                                                              exit_node) != 0;
}

static iree_status_t loom_amdgpu_storage_lease_emit(
    void* user_data, const loom_low_storage_lease_event_t* event) {
  loom_amdgpu_storage_lease_query_t* query =
      (loom_amdgpu_storage_lease_query_t*)user_data;
  loom_low_storage_lease_event_t projected_event = *event;
  if (event->kind == LOOM_LOW_STORAGE_LEASE_SOURCE_READ &&
      event->release_class_id == LOOM_AMDGPU_WAIT_COUNTER_X &&
      query->block_exit_drains_xcnt) {
    projected_event.flags &=
        (loom_low_storage_lease_flags_t)~LOOM_LOW_STORAGE_LEASE_FLAG_MAY_CARRY_ACROSS_BOUNDARY;
    projected_event.flags |=
        LOOM_LOW_STORAGE_LEASE_FLAG_RELEASE_BEFORE_BOUNDARY;
  }
  return query->emit(query->emit_user_data, &projected_event);
}

static iree_status_t loom_amdgpu_storage_lease_query(
    void* user_data, const loom_low_schedule_table_t* schedule,
    const loom_low_schedule_node_t* node, loom_low_storage_lease_emit_fn_t emit,
    void* emit_user_data) {
  if (schedule == NULL || node == NULL || node->descriptor == NULL ||
      node->descriptor->storage_lease_count == 0 ||
      schedule->target.descriptor_set == NULL) {
    return iree_ok_status();
  }
  const loom_low_descriptor_set_t* descriptor_set =
      schedule->target.descriptor_set;
  if (descriptor_set->target_stable_id != LOOM_AMDGPU_TARGET_STABLE_ID) {
    return iree_ok_status();
  }
  loom_amdgpu_storage_lease_query_t query = {
      .block_exit_drains_xcnt =
          loom_amdgpu_storage_lease_block_exit_drains_xcnt(schedule, node),
      .emit = emit,
      .emit_user_data = emit_user_data,
  };
  return loom_low_storage_lease_query_descriptor_rows(
      user_data, schedule, node, loom_amdgpu_storage_lease_emit, &query);
}

void loom_amdgpu_storage_lease_provider(
    loom_low_storage_lease_provider_t* out_provider) {
  IREE_ASSERT_ARGUMENT(out_provider);
  *out_provider = (loom_low_storage_lease_provider_t){
      .query = loom_amdgpu_storage_lease_query,
  };
}
