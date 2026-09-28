// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_ANALYSIS_PIPELINE_RESOURCES_H_
#define LOOM_ANALYSIS_PIPELINE_RESOURCES_H_

#include "loom/analysis/source_storage_packing.h"
#include "loom/error/emitter.h"
#include "loom/util/fact_table.h"

#ifdef __cplusplus
extern "C" {
#endif

// One backing store selected by the admitting caller. The caller supplies one
// row per distinct resource instance, not per target, memory space, coordinate,
// or source pool value. Several pool values may bind to the same row. Target
// placement retains its execution binding and owner alongside this table;
// this analysis neither chooses a device nor interprets physical addresses.
typedef struct loom_pipeline_resource_pool_t {
  // Addressing scope requested by allocations using this pool.
  loom_value_fact_memory_space_t memory_space;
  // Available extent relative to this backing store's origin.
  uint64_t byte_capacity;
  // Fixed ranges contributed by the transport or resource owner before packing.
  const loom_source_storage_packing_range_t* reserved_ranges;
  // Number of entries in reserved_ranges.
  iree_host_size_t reserved_range_count;
} loom_pipeline_resource_pool_t;

typedef struct loom_pipeline_resource_pool_binding_t {
  // Source pool value explicitly resolved by the admitting caller.
  loom_value_id_t value_id;
  // Canonical backing row in the caller's resource-pool table.
  uint32_t pool_index;
} loom_pipeline_resource_pool_binding_t;

typedef struct loom_pipeline_resource_allocation_t {
  // Fresh source allocation identity, independent of pool and channel identity.
  loom_value_id_t root_value_id;
  // Source pool value selecting the backing store.
  loom_value_id_t pool_value_id;
  // Canonical backing row selected by that value's explicit binding.
  uint32_t pool_index;
  // Immutable byte offset within the backing store.
  uint64_t byte_offset;
  // Complete allocation extent in bytes.
  uint64_t byte_length;
  // Alignment required by this allocation.
  uint64_t byte_alignment;
} loom_pipeline_resource_allocation_t;

typedef struct loom_pipeline_resources_t {
  // Borrowed canonical backing rows; the admitting plan outlives this result.
  const loom_pipeline_resource_pool_t* pools;
  // Arena-owned packings indexed by canonical backing row. Generated service
  // and compiled worker requirements reserve storage here before final capacity
  // admission. Earlier source offsets remain stable when requirements append.
  loom_source_storage_packing_t** packings;
  // Number of pools and packings.
  iree_host_size_t pool_count;
  // Arena-owned source placements, sorted by root identity for indexed lookup.
  const loom_pipeline_resource_allocation_t* allocations;
  // Number of source placements.
  iree_host_size_t allocation_count;
} loom_pipeline_resources_t;

// Resolves invocation-owned allocations in a specialized pipeline construction.
// The caller has composed child construction and supplied canonical pool
// bindings. This owner visits construction once, consumes retained buffer
// facts, and keeps every allocation live throughout the invocation.
// Strand-local storage remains with worker compilation and joins the same
// packings using source_storage_packing_reserve before capacity admission.
//
// Allocation construction must be straight-line with finite specialized sizes;
// executable control flow inside strands is unrestricted by this analysis.
// Unresolved authored bindings, extents, or construction are diagnostics with
// out_valid false. Statuses carry allocation and layout-arithmetic failures.
// No IR mutation occurs. Source mutation invalidates this result; a consuming
// rewrite must retain or translate its source correspondence before erasure.
iree_status_t loom_pipeline_resources_build(
    loom_func_like_t pipeline, const loom_value_fact_table_t* facts,
    const loom_pipeline_resource_pool_t* pools, iree_host_size_t pool_count,
    const loom_pipeline_resource_pool_binding_t* bindings,
    iree_host_size_t binding_count,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    loom_pipeline_resources_t* out_resources, bool* out_valid);

// Returns the retained placement for a source allocation root, or NULL when
// the root is external to this construction. This is an indexed table lookup;
// channel/view consumers obtain root identity from their retained value facts.
const loom_pipeline_resource_allocation_t*
loom_pipeline_resources_lookup_allocation(
    const loom_pipeline_resources_t* resources, loom_value_id_t root_value_id);

// Checks the complete pool budgets after source, transport and worker storage
// have joined the packings. Failure names the canonical pool, required extent,
// and available extent. The caller does not materialize addresses or publish
// an executable until this admission succeeds.
iree_status_t loom_pipeline_resources_check_capacity(
    const loom_pipeline_resources_t* resources, const loom_op_t* entry,
    iree_diagnostic_emitter_t diagnostic_emitter, bool* out_valid);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_ANALYSIS_PIPELINE_RESOURCES_H_
