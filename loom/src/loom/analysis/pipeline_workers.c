// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/analysis/pipeline_workers.h"

#include "loom/error/error_catalog.h"
#include "loom/ops/func/ops.h"
#include "loom/ops/pipeline/ops.h"
#include "loom/ops/type_registry.h"
#include "loom/util/fact_cfg.h"
#include "loom/util/fact_extensions.h"

static iree_status_t loom_pipeline_workers_reject(
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

static bool loom_pipeline_worker_coordinate(
    int64_t literal, loom_value_slice_t values, uint16_t* dynamic_index,
    const loom_value_fact_table_t* facts, uint64_t* out_value) {
  int64_t value = literal;
  if (literal == INT64_MIN && !loom_value_facts_as_exact_i64(
                                  loom_value_fact_table_lookup(
                                      facts, values.values[(*dynamic_index)++]),
                                  &value)) {
    return false;
  }
  if (value < 0) {
    return false;
  }
  *out_value = (uint64_t)value;
  return true;
}

static const loom_pipeline_channel_binding_t*
loom_pipeline_worker_lookup_incoming(
    const loom_pipeline_channel_binding_t* incoming,
    iree_host_size_t incoming_count, loom_value_id_t value_id) {
  iree_host_size_t begin = 0, end = incoming_count;
  while (begin < end) {
    const iree_host_size_t middle = begin + (end - begin) / 2;
    const loom_pipeline_channel_binding_t* binding = &incoming[middle];
    if (binding->value_id == value_id) {
      return binding;
    }
    if (binding->value_id < value_id) {
      begin = middle + 1;
    } else {
      end = middle;
    }
  }
  return NULL;
}

static iree_status_t loom_pipeline_worker_build(
    loom_module_t* module, const loom_pipeline_resources_t* resources,
    const loom_value_fact_table_t* construction_facts,
    const loom_pipeline_channel_binding_t* incoming,
    iree_host_size_t incoming_count,
    const loom_channel_plan_callable_t* const* callables,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    loom_pipeline_worker_t* worker, bool* out_valid) {
  *out_valid = false;
  const loom_op_t* declaration = worker->source->declaration;
  const loom_attribute_t origins =
      loom_pipeline_strand_static_origins(declaration);
  const loom_attribute_t counts =
      loom_pipeline_strand_static_counts(declaration);
  const loom_attribute_t strides =
      loom_pipeline_strand_static_strides(declaration);
  loom_pipeline_worker_axis_t* axes = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, origins.count,
                                                 sizeof(*axes), (void**)&axes));
  uint16_t origin_index = 0, count_index = 0, stride_index = 0;
  for (uint32_t i = 0; i < origins.count; ++i) {
    if (!loom_pipeline_worker_coordinate(
            origins.i64_array[i], loom_pipeline_strand_origins(declaration),
            &origin_index, construction_facts, &axes[i].origin) ||
        !loom_pipeline_worker_coordinate(
            counts.i64_array[i], loom_pipeline_strand_counts(declaration),
            &count_index, construction_facts, &axes[i].count) ||
        !loom_pipeline_worker_coordinate(
            strides.i64_array[i], loom_pipeline_strand_strides(declaration),
            &stride_index, construction_facts, &axes[i].stride) ||
        axes[i].stride == 0) {
      return loom_pipeline_workers_reject(
          declaration, IREE_SV("specialized finite worker geometry"),
          diagnostic_emitter);
    }
  }
  worker->axes = axes;
  worker->rank = origins.count;
  const loom_symbol_ref_t callee = loom_func_call_callee(worker->source->call);
  worker->function = loom_func_like_cast(
      module, module->symbols.entries[callee.symbol_id].defining_op);
  loom_region_t* body = loom_func_like_body(worker->function);
  if (!body) {
    return loom_pipeline_workers_reject(worker->source->call,
                                        IREE_SV("a visible strand body"),
                                        diagnostic_emitter);
  }
  const loom_value_slice_t actuals =
      loom_func_call_operands(worker->source->call);
  uint16_t formal_count = 0;
  const loom_value_id_t* formals =
      loom_func_like_arg_ids(worker->function, &formal_count);
  loom_pipeline_channel_binding_t* bindings = NULL;
  loom_channel_plan_binding_t* channel_bindings = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, formal_count, sizeof(*bindings), (void**)&bindings));
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, formal_count,
                                                 sizeof(*channel_bindings),
                                                 (void**)&channel_bindings));
  IREE_RETURN_IF_ERROR(
      loom_value_fact_table_initialize(&worker->facts, arena, 0));
  worker->facts.context.resolve_type_domain =
      construction_facts->context.resolve_type_domain;
  worker->facts.context.resolve_region_target =
      construction_facts->context.resolve_region_target;
  worker->facts.context.target_facts = loom_value_fact_table_block_target_facts(
      construction_facts,
      loom_region_const_entry_block(loom_pipeline_strand_body(declaration)));
  for (uint16_t i = 0; i < formal_count; ++i) {
    const loom_type_t type = loom_module_value_type(module, formals[i]);
    loom_value_facts_t facts;
    IREE_RETURN_IF_ERROR(loom_value_fact_table_clone_fact_for_type(
        &worker->facts, construction_facts, module, type,
        loom_value_fact_table_lookup(construction_facts, actuals.values[i]),
        &facts));
    IREE_RETURN_IF_ERROR(
        loom_value_fact_table_define(&worker->facts, formals[i], facts));
    if (!loom_channel_type_isa(type)) {
      continue;
    }
    loom_pipeline_channel_binding_t binding = {
        .value_id = formals[i],
        .resources = resources,
        .channel = loom_pipeline_resources_lookup_channel(resources,
                                                          actuals.values[i]),
    };
    if (!binding.channel) {
      const loom_pipeline_channel_binding_t* parent =
          loom_pipeline_worker_lookup_incoming(incoming, incoming_count,
                                               actuals.values[i]);
      if (parent) {
        binding.resources = parent->resources;
        binding.channel = parent->channel;
      }
    }
    if (!binding.channel) {
      return loom_pipeline_workers_reject(
          worker->source->call,
          IREE_SV("an incoming channel construction binding"),
          diagnostic_emitter);
    }
    bindings[worker->binding_count] = binding;
    channel_bindings[worker->binding_count++] = (loom_channel_plan_binding_t){
        .value_id = formals[i], .channel = &binding.channel->identity};
  }
  worker->bindings = bindings;
  IREE_RETURN_IF_ERROR(
      loom_value_fact_table_compute(&worker->facts, module, worker->function));
  const loom_value_fact_cfg_region_t* structure = NULL;
  IREE_RETURN_IF_ERROR(loom_value_fact_table_get_or_build_cfg_region(
      &worker->facts, module, body, &structure));
  worker->graph = &structure->graph;
  IREE_RETURN_IF_ERROR(loom_local_value_domain_acquire_for_region(
      module, body, arena, &worker->value_domain));
  loom_channel_plan_rejection_t rejection;
  iree_status_t status = loom_channel_plan_build(
      &worker->value_domain, channel_bindings, worker->binding_count, callables,
      arena, &worker->channels, &rejection);
  if (iree_status_is_ok(status) &&
      rejection.kind == LOOM_CHANNEL_PLAN_REJECTION_NONE) {
    status = loom_channel_completion_analyze(&worker->channels, worker->graph,
                                             arena, &worker->completion);
  }
  if (iree_status_is_ok(status) &&
      rejection.kind == LOOM_CHANNEL_PLAN_REJECTION_NONE) {
    const loom_kernel_async_legality_options_t options = {
        .value_domain = &worker->value_domain,
        .fact_table = &worker->facts,
        .emitter = diagnostic_emitter,
        .phase_name = IREE_SV("pipeline-worker"),
    };
    status = loom_kernel_async_legality_analyze_function(
        module, worker->function, &options, arena, &worker->asynchronous);
  }
  loom_local_value_domain_release(&worker->value_domain);
  if (!iree_status_is_ok(status)) {
    return status;
  }
  if (rejection.kind != LOOM_CHANNEL_PLAN_REJECTION_NONE) {
    return loom_pipeline_workers_reject(
        rejection.op ? rejection.op : declaration,
        IREE_SV("statically bound channels and visible channel-aware calls"),
        diagnostic_emitter);
  }
  *out_valid = worker->asynchronous.error_count == 0;
  return iree_ok_status();
}

iree_status_t loom_pipeline_workers_build(
    loom_module_t* module, const loom_pipeline_resources_t* resources,
    const loom_value_fact_table_t* construction_facts,
    const loom_pipeline_channel_binding_t* incoming,
    iree_host_size_t incoming_count,
    const loom_channel_plan_callable_t* const* callables,
    iree_diagnostic_emitter_t diagnostic_emitter, iree_arena_allocator_t* arena,
    loom_pipeline_worker_t** out_workers, bool* out_valid) {
  *out_workers = NULL;
  *out_valid = false;
  loom_pipeline_worker_t* workers = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, resources->strand_count, sizeof(*workers), (void**)&workers));
  for (iree_host_size_t i = 0; i < resources->strand_count; ++i) {
    workers[i] = (loom_pipeline_worker_t){.source = &resources->strands[i]};
    bool valid = false;
    IREE_RETURN_IF_ERROR(loom_pipeline_worker_build(
        module, resources, construction_facts, incoming, incoming_count,
        callables, diagnostic_emitter, arena, &workers[i], &valid));
    if (!valid) {
      return iree_ok_status();
    }
  }
  *out_workers = workers;
  *out_valid = true;
  return iree_ok_status();
}
