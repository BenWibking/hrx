// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_ANALYSIS_PIPELINE_WORKERS_H_
#define LOOM_ANALYSIS_PIPELINE_WORKERS_H_

#include "loom/analysis/kernel_async_legality.h"
#include "loom/analysis/pipeline_resources.h"
#include "loom/util/cfg_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

// Actual channel correspondence at a construction or worker boundary. Storage
// and protocol identities remain owned by the construction that created them.
typedef struct loom_pipeline_channel_binding_t {
  // Value in the receiving source scope.
  loom_value_id_t value_id;
  // Construction occurrence owning the channel and its allocation placements.
  const loom_pipeline_resources_t* resources;
  // Binding in resources, independent of its storage address or source symbol.
  const loom_pipeline_resource_channel_t* channel;
} loom_pipeline_channel_binding_t;

// Specialized authored worker geometry. This is a domain of execution units,
// not a count of records and not a request to repeat the strand body per
// record.
typedef struct loom_pipeline_worker_axis_t {
  // First worker coordinate along this axis.
  uint64_t origin;
  // Number of workers along this axis.
  uint64_t count;
  // Coordinate increment between workers along this axis.
  uint64_t stride;
} loom_pipeline_worker_axis_t;

typedef struct loom_pipeline_worker_t {
  // Authored strand and its ordinary call, borrowed through consuming rewrite.
  const loom_pipeline_resource_strand_t* source;
  // Visible body bound by the call's actual captures. Shared definitions remain
  // immutable until the materialization owner has assigned distinct instances.
  loom_func_like_t function;
  // Arena-owned worker geometry in source axis order.
  const loom_pipeline_worker_axis_t* axes;
  // Number of worker axes.
  iree_host_size_t rank;
  // Arena-owned formal channel bindings, in formal argument order.
  const loom_pipeline_channel_binding_t* bindings;
  // Number of channel formals in bindings.
  iree_host_size_t binding_count;
  // Facts seeded from the actual captures, including storage projections.
  loom_value_fact_table_t facts;
  // Retained ordinal domain. Released after construction; restore before
  // querying ordinal-indexed plans or starting their consuming rewrite.
  loom_local_value_domain_t value_domain;
  // Communication membership and actions established in this occurrence.
  loom_channel_plan_t channels;
  // Borrowed asynchronous movement and its admitted group completion edges.
  loom_kernel_async_legality_result_t asynchronous;
  // CFG topology retained for scheduling and handoff analysis.
  const loom_cfg_graph_t* graph;
} loom_pipeline_worker_t;

// Binds every outlined strand in one construction occurrence. The construction
// facts and local resources are already computed. incoming is indexed in
// ascending value_id order. Incoming channels retain
// their originating construction instead of being reconstructed from addresses
// or payload types. callables supplies visible channel-aware helper summaries
// when such calls survive source inlining; ordinary calls need no summary.
//
// Each worker receives its own facts and communication plan, even when several
// strands call the same function. Target selection consumes these results and
// never rewalks source to recover captures, placement, or protocol membership.
// No source mutation occurs. All plans and their domains live in arena, while
// the module ordinal scratch is available on return. Rejected authored input
// emits a diagnostic and leaves out_valid false; status carries allocation or
// diagnostic emission failure. The caller admits all resources before mutation.
iree_status_t loom_pipeline_workers_build(
    loom_module_t* module, const loom_pipeline_resources_t* resources,
    const loom_value_fact_table_t* construction_facts,
    const loom_pipeline_channel_binding_t* incoming,
    iree_host_size_t incoming_count,
    const loom_channel_plan_callable_t* const* callables,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    loom_pipeline_worker_t** out_workers, bool* out_valid);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_ANALYSIS_PIPELINE_WORKERS_H_
