// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/analysis/pipeline_resources.h"

#include <stdlib.h>

#include "loom/error/error_catalog.h"
#include "loom/ops/buffer/ops.h"
#include "loom/ops/pipeline/ops.h"

static iree_status_t loom_pipeline_resources_reject(
    const loom_op_t* op, iree_string_view_t requirement,
    iree_diagnostic_emitter_t diagnostic_emitter) {
  const loom_diagnostic_param_t params[] = {loom_param_string(requirement)};
  const loom_diagnostic_emission_t emission = {
      .op = op,
      .error = LOOM_ERR_LOWERING_066,
      .params = params,
      .param_count = IREE_ARRAYSIZE(params),
  };
  return iree_diagnostic_emit(diagnostic_emitter, &emission);
}

static int loom_pipeline_resources_compare_root(const void* lhs,
                                                const void* rhs) {
  const loom_value_id_t lhs_root =
      ((const loom_pipeline_resource_allocation_t*)lhs)->root_value_id;
  const loom_value_id_t rhs_root =
      ((const loom_pipeline_resource_allocation_t*)rhs)->root_value_id;
  return (lhs_root > rhs_root) - (lhs_root < rhs_root);
}

iree_status_t loom_pipeline_resources_build(
    loom_func_like_t pipeline, const loom_value_fact_table_t* facts,
    const loom_pipeline_resource_pool_t* pools, iree_host_size_t pool_count,
    const loom_pipeline_resource_pool_binding_t* bindings,
    iree_host_size_t binding_count,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    loom_pipeline_resources_t* out_resources, bool* out_valid) {
  *out_resources = (loom_pipeline_resources_t){0};
  *out_valid = false;
  const loom_region_t* body = loom_pipeline_def_body(pipeline.op);
  if (body->block_count != 1) {
    return loom_pipeline_resources_reject(
        pipeline.op,
        IREE_SV("specialized straight-line allocation construction"),
        diagnostic_emitter);
  }

  loom_pipeline_resources_t resources = {
      .pools = pools,
      .pool_count = pool_count,
  };
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, pool_count,
                                                 sizeof(*resources.packings),
                                                 (void**)&resources.packings));
  for (iree_host_size_t i = 0; i < pool_count; ++i) {
    IREE_RETURN_IF_ERROR(loom_source_storage_packing_create(
        (loom_source_storage_packing_interference_callback_t){0},
        pools[i].reserved_ranges, pools[i].reserved_range_count, arena,
        &resources.packings[i]));
  }

  loom_pipeline_resource_allocation_t* allocations = NULL;
  iree_host_size_t allocation_capacity = 0;
  const loom_block_t* entry = loom_region_const_entry_block(body);
  for (const loom_op_t* op = entry->first_op; op; op = op->next_op) {
    if (loom_pipeline_strand_isa(op)) {
      continue;
    }
    if (op->region_count || loom_pipeline_compose_isa(op)) {
      return loom_pipeline_resources_reject(
          op, IREE_SV("composed and specialized allocation construction"),
          diagnostic_emitter);
    }
    if (!loom_buffer_alloca_isa(op)) {
      continue;
    }

    const loom_value_id_t pool_value_id = loom_buffer_alloca_pool(op);
    iree_host_size_t binding = 0;
    for (; binding < binding_count; ++binding) {
      if (bindings[binding].value_id == pool_value_id) {
        break;
      }
    }
    if (binding == binding_count) {
      return loom_pipeline_resources_reject(
          op, IREE_SV("an explicit admitted binding for the allocation pool"),
          diagnostic_emitter);
    }
    const uint32_t pool_index = bindings[binding].pool_index;
    if (loom_buffer_alloca_memory_space(op) != pools[pool_index].memory_space) {
      return loom_pipeline_resources_reject(
          op, IREE_SV("an allocation memory space matching its selected pool"),
          diagnostic_emitter);
    }
    const loom_value_id_t root_value_id = loom_buffer_alloca_result(op);
    loom_value_fact_buffer_reference_t reference = {0};
    int64_t byte_length = 0;
    if (!loom_value_facts_query_buffer_reference(
            &facts->context, loom_value_fact_table_lookup(facts, root_value_id),
            &reference) ||
        !loom_value_facts_as_exact_i64(reference.maximum_byte_extent,
                                       &byte_length) ||
        byte_length < 0) {
      return loom_pipeline_resources_reject(
          op, IREE_SV("a finite specialized allocation extent"),
          diagnostic_emitter);
    }
    if (resources.allocation_count == allocation_capacity) {
      IREE_RETURN_IF_ERROR(iree_arena_grow_array(
          arena, resources.allocation_count, resources.allocation_count + 1,
          sizeof(*allocations), &allocation_capacity, (void**)&allocations));
    }
    loom_pipeline_resource_allocation_t allocation = {
        .root_value_id = root_value_id,
        .pool_value_id = pool_value_id,
        .pool_index = pool_index,
        .byte_length = (uint64_t)byte_length,
        .byte_alignment = reference.minimum_alignment,
    };
    IREE_RETURN_IF_ERROR(loom_source_storage_packing_append(
        resources.packings[pool_index], root_value_id, allocation.byte_length,
        allocation.byte_alignment, &allocation.byte_offset));
    allocations[resources.allocation_count++] = allocation;
  }
  if (resources.allocation_count > 1) {
    qsort(allocations, resources.allocation_count, sizeof(*allocations),
          loom_pipeline_resources_compare_root);
  }
  resources.allocations = allocations;
  *out_resources = resources;
  *out_valid = true;
  return iree_ok_status();
}

const loom_pipeline_resource_allocation_t*
loom_pipeline_resources_lookup_allocation(
    const loom_pipeline_resources_t* resources, loom_value_id_t root_value_id) {
  iree_host_size_t begin = 0;
  iree_host_size_t end = resources->allocation_count;
  while (begin < end) {
    const iree_host_size_t middle = begin + (end - begin) / 2;
    const loom_pipeline_resource_allocation_t* allocation =
        &resources->allocations[middle];
    if (allocation->root_value_id == root_value_id) {
      return allocation;
    }
    if (allocation->root_value_id < root_value_id) {
      begin = middle + 1;
    } else {
      end = middle;
    }
  }
  return NULL;
}

iree_status_t loom_pipeline_resources_check_capacity(
    const loom_pipeline_resources_t* resources, const loom_op_t* entry,
    iree_diagnostic_emitter_t diagnostic_emitter, bool* out_valid) {
  *out_valid = true;
  for (iree_host_size_t i = 0; i < resources->pool_count; ++i) {
    const uint64_t required =
        loom_source_storage_packing_requirement(resources->packings[i])
            .byte_length;
    if (required <= resources->pools[i].byte_capacity) {
      continue;
    }
    *out_valid = false;
    const loom_diagnostic_param_t params[] = {
        loom_param_u64(i),
        loom_param_u64(required),
        loom_param_u64(resources->pools[i].byte_capacity),
    };
    const loom_diagnostic_emission_t emission = {
        .op = entry,
        .error = LOOM_ERR_LOWERING_067,
        .params = params,
        .param_count = IREE_ARRAYSIZE(params),
    };
    IREE_RETURN_IF_ERROR(iree_diagnostic_emit(diagnostic_emitter, &emission));
  }
  return iree_ok_status();
}
