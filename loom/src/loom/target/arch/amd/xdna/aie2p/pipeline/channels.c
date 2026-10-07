// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/ops/buffer/ops.h"
#include "loom/ops/channel/ops.h"
#include "loom/ops/index/ops.h"
#include "loom/ops/type_registry.h"
#include "loom/target/arch/amd/xdna/aie2p/pipeline/native.h"
#include "loom/transforms/pipeline/channel_materialization.h"

typedef struct loom_aie2p_native_endpoint_t {
  // Physical channel access and lock selectors from this worker's tile.
  const loom_aie2p_native_channel_access_t* access;
  // Initial physical slot address helper.
  loom_symbol_ref_t base;
  // First slot's address, defined in the worker entry block.
  loom_value_id_t initial;
  // Successor slot helper, or null when this worker advances no cursor.
  loom_symbol_ref_t next;
  // Mutable state indices for directions owned by this worker. One-slot
  // channels use initial directly and require no threaded state.
  struct {
    // Current read address; used only by an owned multi-slot acquire.
    iree_host_size_t reader;
    // Current write address; used only by an owned multi-slot reserve.
    iree_host_size_t writer;
  } cursor;
  // Native consuming-credit acquire helper, created on first use.
  loom_symbol_ref_t acquire;
  // Native producing-credit acquire helper, created on first use.
  loom_symbol_ref_t reserve;
  // Native ready-credit release helper, created on first use.
  loom_symbol_ref_t publish;
  // Native free-credit release helper, created on first use.
  loom_symbol_ref_t release;
  // Alignment common to every physical record address.
  uint64_t alignment;
} loom_aie2p_native_endpoint_t;

typedef struct loom_aie2p_native_channel_emitter_t {
  // Physical selection and typed helper emitter.
  loom_aie2p_native_context_t* context;
  // Canonical construction for identity lookup.
  const loom_pipeline_resources_t* resources;
  // Endpoint index by construction channel index; unused entries are
  // UINT32_MAX.
  const uint32_t* indices;
  // Bound endpoints in the worker's retained channel-access order.
  loom_aie2p_native_endpoint_t* endpoints;
  // Worker-owned source actions and completion deltas, retained before
  // rewriting.
  const loom_pipeline_worker_t* worker;
  // Zero displacement for views borrowed from the current slot address.
  loom_value_id_t origin;
  // Final completion publication followed by a quiescent stream wait.
  loom_symbol_ref_t complete;
} loom_aie2p_native_channel_emitter_t;

static iree_status_t loom_aie2p_native_invoke(loom_builder_t* builder,
                                              loom_symbol_ref_t callee,
                                              const loom_value_id_t* arguments,
                                              iree_host_size_t argument_count,
                                              const loom_type_t* result_type,
                                              loom_value_id_t* out_result) {
  loom_op_t* op = NULL;
  IREE_RETURN_IF_ERROR(loom_low_invoke_build(
      builder, 0, 0, 0, callee, arguments, argument_count, result_type,
      result_type ? 1 : 0, NULL, 0, LOOM_LOCATION_UNKNOWN, &op));
  if (out_result) {
    *out_result = loom_op_results(op)[0];
  }
  return iree_ok_status();
}

static iree_status_t loom_aie2p_native_address(
    loom_aie2p_worker_builder_t* context, loom_builder_t* builder,
    uint32_t address, loom_value_id_t* out_value) {
  const loom_named_attr_t attribute = {.name_id = context->integer_name,
                                       .value = loom_attr_i64(address)};
  return loom_aie2p_worker_op(
      context, builder, AIE2P_CORE_DESCRIPTOR_REF_MATERIALIZE_LOCAL_ADDRESS_I32,
      NULL, 0, loom_make_named_attr_slice(&attribute, 1),
      &context->address_type, out_value);
}

static iree_status_t loom_aie2p_native_lock_helper(
    loom_aie2p_worker_builder_t* context, uint32_t descriptor,
    uint16_t selector, int32_t delta, loom_symbol_ref_t* out_symbol) {
  loom_builder_t builder;
  loom_op_t* function;
  IREE_RETURN_IF_ERROR(loom_aie2p_worker_helper(
      context, NULL, 0, NULL, 0, &builder, out_symbol, &function));
  IREE_RETURN_IF_ERROR(
      loom_aie2p_worker_lock(context, &builder, descriptor, selector, delta));
  return loom_aie2p_worker_return(&builder, NULL, 0);
}

static iree_status_t loom_aie2p_native_endpoint(
    loom_aie2p_native_context_t* context,
    const loom_aie2p_native_channel_access_t* access,
    loom_aie2p_native_endpoint_t* endpoint) {
  const loom_aie2p_native_channel_t* channel = access->channel;
  *endpoint = (loom_aie2p_native_endpoint_t){.access = access,
                                             .next = loom_symbol_ref_null(),
                                             .acquire = loom_symbol_ref_null(),
                                             .reserve = loom_symbol_ref_null(),
                                             .publish = loom_symbol_ref_null(),
                                             .release = loom_symbol_ref_null(),
                                             .alignment = access->alignment};
  const uint32_t address = access->address;
  loom_aie2p_worker_builder_t* code = &context->code;
  loom_builder_t builder;
  loom_op_t* function;
  IREE_RETURN_IF_ERROR(
      loom_aie2p_worker_helper(code, NULL, 0, &code->address_type, 1, &builder,
                               &endpoint->base, &function));
  loom_value_id_t base;
  IREE_RETURN_IF_ERROR(
      loom_aie2p_native_address(code, &builder, address, &base));
  IREE_RETURN_IF_ERROR(loom_aie2p_worker_return(&builder, &base, 1));
  if (channel->source->capacity > 1 &&
      (channel->cursor.reader == code->worker_index ||
       channel->cursor.writer == code->worker_index)) {
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_helper(
        code, &code->address_type, 1, &code->address_type, 1, &builder,
        &endpoint->next, &function));
    loom_region_t* body = loom_low_func_def_body(function);
    const loom_value_id_t pointer = loom_region_entry_block(body)->arg_ids[0];
    loom_value_id_t current, stride, next, limit, wrap;
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_op(
        code, &builder, AIE2P_CORE_DESCRIPTOR_REF_MOVE_LOCAL_ADDRESS_TO_SCALAR,
        &pointer, 1, loom_named_attr_slice_empty(), &code->scalar_type,
        &current));
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_constant(
        code, &builder, channel->byte_stride, &stride));
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_binary(
        code, &builder, AIE2P_CORE_DESCRIPTOR_REF_ADD_I32, current, stride,
        &next));
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_constant(
        code, &builder,
        address + channel->byte_stride * channel->source->capacity, &limit));
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_binary(
        code, &builder, AIE2P_CORE_DESCRIPTOR_REF_CMP_EQ_I32, next, limit,
        &wrap));
    loom_block_t *wrap_block, *next_block;
    IREE_RETURN_IF_ERROR(
        loom_region_append_block(code->module, body, &wrap_block));
    IREE_RETURN_IF_ERROR(
        loom_region_append_block(code->module, body, &next_block));
    loom_op_t* branch;
    IREE_RETURN_IF_ERROR(
        loom_low_cond_br_build(&builder, wrap, wrap_block, next_block,
                               LOOM_LOCATION_UNKNOWN, &branch));
    loom_builder_set_block(&builder, wrap_block);
    IREE_RETURN_IF_ERROR(
        loom_aie2p_native_address(code, &builder, address, &base));
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_return(&builder, &base, 1));
    loom_builder_set_block(&builder, next_block);
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_op(
        code, &builder, AIE2P_CORE_DESCRIPTOR_REF_MOVE_SCALAR_TO_LOCAL_ADDRESS,
        &next, 1, loom_named_attr_slice_empty(), &code->address_type, &base));
    IREE_RETURN_IF_ERROR(loom_aie2p_worker_return(&builder, &base, 1));
  }
  return iree_ok_status();
}

static iree_status_t loom_aie2p_native_channel_action(
    void* user_data, loom_rewriter_t* rewriter,
    const loom_channel_plan_action_t* action, loom_value_id_t* state,
    iree_host_size_t state_count, loom_value_id_t* results) {
  (void)state_count;
  loom_aie2p_native_channel_emitter_t* emitter = user_data;
  const loom_pipeline_resource_channel_t* channel =
      loom_pipeline_resources_lookup_channel(emitter->resources,
                                             action->channel->value_id);
  const uint32_t index =
      emitter->indices[channel - emitter->resources->channels];
  loom_aie2p_native_endpoint_t* endpoint = &emitter->endpoints[index];
  loom_builder_t* builder = &rewriter->builder;
  if (loom_channel_acquire_isa(action->op) ||
      loom_channel_reserve_isa(action->op) ||
      loom_channel_wait_isa(action->op)) {
    loom_value_id_t record;
    uint16_t view_result = 0;
    if (loom_channel_wait_isa(action->op)) {
      record = loom_channel_wait_read(action->op);
    } else {
      const bool write = loom_channel_reserve_isa(action->op);
      loom_symbol_ref_t* acquire =
          write ? &endpoint->reserve : &endpoint->acquire;
      if (!loom_symbol_ref_is_valid(*acquire)) {
        IREE_RETURN_IF_ERROR(loom_aie2p_native_lock_helper(
            &emitter->context->code,
            AIE2P_CORE_DESCRIPTOR_REF_LOCK_ACQUIRE_IMMEDIATE,
            write ? endpoint->access->locks.free
                  : endpoint->access->locks.ready,
            -1, acquire));
      }
      IREE_RETURN_IF_ERROR(
          loom_aie2p_native_invoke(builder, *acquire, NULL, 0, NULL, NULL));
      record = endpoint->initial;
      if (loom_symbol_ref_is_valid(endpoint->next)) {
        const iree_host_size_t cursor =
            write ? endpoint->cursor.writer : endpoint->cursor.reader;
        record = state[cursor];
        const loom_type_t buffer_type = loom_type_buffer();
        IREE_RETURN_IF_ERROR(
            loom_aie2p_native_invoke(builder, endpoint->next, &state[cursor], 1,
                                     &buffer_type, &state[cursor]));
      }
      results[0] = record;
      view_result = 1;
    }
    const loom_type_t buffer_type = loom_type_buffer();
    loom_op_t* aligned;
    IREE_RETURN_IF_ERROR(loom_buffer_assume_alignment_build(
        builder, &record, 1, endpoint->alignment, &buffer_type, 1,
        action->op->location, &aligned));
    loom_op_t* view;
    IREE_RETURN_IF_ERROR(loom_buffer_view_build(
        builder, loom_op_results(aligned)[0], emitter->origin,
        loom_module_value_type(rewriter->module,
                               loom_op_results(action->op)[view_result]),
        action->op->location, &view));
    results[view_result] = loom_buffer_view_result(view);
    return iree_ok_status();
  }
  const uint32_t credits =
      emitter->worker->completion
          .credits[action - emitter->worker->channels.actions];
  if (!credits) {
    return iree_ok_status();
  }
  const bool publish = loom_channel_publish_isa(action->op);
  loom_symbol_ref_t additional = loom_symbol_ref_null();
  loom_symbol_ref_t* completion =
      publish ? &endpoint->publish : &endpoint->release;
  if (credits != 1) {
    completion = &additional;
  }
  if (!loom_symbol_ref_is_valid(*completion)) {
    IREE_RETURN_IF_ERROR(loom_aie2p_native_lock_helper(
        &emitter->context->code,
        AIE2P_CORE_DESCRIPTOR_REF_LOCK_RELEASE_IMMEDIATE,
        publish ? endpoint->access->locks.ready : endpoint->access->locks.free,
        (int32_t)credits, completion));
  }
  return loom_aie2p_native_invoke(builder, *completion, NULL, 0, NULL, NULL);
}

static iree_status_t loom_aie2p_native_channel_exit(
    void* user_data, loom_rewriter_t* rewriter, const loom_op_t* terminator,
    const loom_value_id_t* state, iree_host_size_t state_count) {
  (void)terminator;
  (void)state;
  (void)state_count;
  loom_aie2p_native_channel_emitter_t* emitter = user_data;
  return loom_aie2p_native_invoke(&rewriter->builder, emitter->complete, NULL,
                                  0, NULL, NULL);
}

iree_status_t loom_aie2p_native_emit_worker(
    void* user_data, loom_rewriter_t* rewriter,
    const loom_pipeline_realization_t* realization,
    iree_host_size_t worker_index) {
  loom_aie2p_native_context_t* context = user_data;
  context->code.realization = realization;
  context->code.worker_index = worker_index;
  context->code.diagnostic_emitter = context->pass->diagnostic_emitter;
  const loom_pipeline_worker_t* source = &realization->workers[worker_index];
  const loom_aie2p_native_worker_t* worker = &context->workers[worker_index];
  loom_aie2p_native_channel_emitter_t emitter = {
      .context = context,
      .resources = &realization->resources,
      .worker = source,
      .indices = worker->channels.indices};
  const iree_host_size_t endpoint_count = worker->channels.count;
  iree_arena_allocator_t* arena = context->pass->arena;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, endpoint_count,
                                                 sizeof(*emitter.endpoints),
                                                 (void**)&emitter.endpoints));
  for (iree_host_size_t i = 0; i < endpoint_count; ++i) {
    IREE_RETURN_IF_ERROR(loom_aie2p_native_endpoint(
        context, &worker->channels.accesses[i], &emitter.endpoints[i]));
  }
  loom_aie2p_worker_builder_t* code = &context->code;
  loom_builder_t builder;
  loom_op_t* complete;
  IREE_RETURN_IF_ERROR(loom_aie2p_worker_helper(
      code, NULL, 0, NULL, 0, &builder, &emitter.complete, &complete));
  IREE_RETURN_IF_ERROR(loom_aie2p_worker_lock(
      code, &builder, AIE2P_CORE_DESCRIPTOR_REF_LOCK_RELEASE_IMMEDIATE,
      worker->completion_selector, 1));
  loom_block_t* park;
  IREE_RETURN_IF_ERROR(loom_region_append_block(
      code->module, loom_low_func_def_body(complete), &park));
  loom_op_t* branch;
  IREE_RETURN_IF_ERROR(loom_low_br_build(&builder, park, NULL, 0,
                                         LOOM_LOCATION_UNKNOWN, &branch));
  loom_builder_set_block(&builder, park);
  loom_value_id_t unused;
  IREE_RETURN_IF_ERROR(loom_aie2p_worker_op(
      code, &builder, AIE2P_CORE_DESCRIPTOR_REF_STREAM_READ_I32, NULL, 0,
      loom_named_attr_slice_empty(), &code->scalar_type, &unused));
  IREE_RETURN_IF_ERROR(loom_low_br_build(&builder, park, NULL, 0,
                                         LOOM_LOCATION_UNKNOWN, &branch));

  IREE_RETURN_IF_ERROR(loom_aie2p_native_emit_transfers(
      context, rewriter, realization, worker_index));
  loom_block_t* entry =
      loom_region_entry_block(loom_func_like_body(source->function));
  loom_builder_set_before(&rewriter->builder, entry->first_op);
  loom_op_t* origin;
  IREE_RETURN_IF_ERROR(
      loom_index_constant_build(&rewriter->builder, loom_attr_i64(0),
                                loom_type_scalar(LOOM_SCALAR_TYPE_OFFSET),
                                LOOM_LOCATION_UNKNOWN, &origin));
  emitter.origin = loom_index_constant_result(origin);
  loom_value_id_t* state = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, endpoint_count * 2, sizeof(*state), (void**)&state));
  iree_host_size_t state_count = 0;
  const loom_type_t buffer_type = loom_type_buffer();
  for (iree_host_size_t i = 0; i < endpoint_count; ++i) {
    loom_aie2p_native_endpoint_t* endpoint = &emitter.endpoints[i];
    IREE_RETURN_IF_ERROR(
        loom_aie2p_native_invoke(&rewriter->builder, endpoint->base, NULL, 0,
                                 &buffer_type, &endpoint->initial));
    if (loom_symbol_ref_is_valid(endpoint->next)) {
      const loom_aie2p_native_channel_t* channel = endpoint->access->channel;
      if (channel->cursor.reader == worker_index) {
        endpoint->cursor.reader = state_count;
        state[state_count++] = endpoint->initial;
      }
      if (channel->cursor.writer == worker_index) {
        endpoint->cursor.writer = state_count;
        state[state_count++] = endpoint->initial;
      }
    }
  }
  loom_type_t* carriers = NULL;
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate_array(arena, source->channels.value_count,
                                sizeof(*carriers), (void**)&carriers));
  memset(carriers, 0, source->channels.value_count * sizeof(*carriers));
  for (loom_value_ordinal_t i = 0; i < source->channels.value_count; ++i) {
    const loom_type_t type =
        loom_module_value_type(code->module, source->value_domain.value_ids[i]);
    if (loom_read_type_isa(type) || loom_write_type_isa(type)) {
      carriers[i] = buffer_type;
    }
  }
  const loom_channel_materialization_options_t options = {
      .carrier_types = carriers,
      .initial_state = state,
      .state_count = state_count,
      .emit = {.fn = loom_aie2p_native_channel_action,
               .exit = loom_aie2p_native_channel_exit,
               .user_data = &emitter}};
  return loom_channel_materialize(rewriter, &source->channels, &options);
}
