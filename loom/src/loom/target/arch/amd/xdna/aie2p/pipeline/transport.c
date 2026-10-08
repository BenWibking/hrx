// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/analysis/storage_geometry.h"
#include "loom/ops/channel/ops.h"
#include "loom/target/arch/amd/xdna/aie2p/array/binding.h"
#include "loom/target/arch/amd/xdna/aie2p/pipeline/native.h"
#include "loom/target/arch/amd/xdna/array/registers.h"

// DMA queue admission consumes already established completion edges. It never
// inserts a wait or changes the author's grouping to hide resource exhaustion.
typedef struct loom_aie2p_native_queue_t {
  // Selected engine pair, allocated at first use.
  loom_aie2p_native_dma_path_t* path;
  // Retained completion sites for tasks not yet retired in this block.
  const loom_op_t** completions;
  // Number of retained completion sites.
  uint8_t count;
  // Joint queue capacity of the two endpoints.
  uint8_t capacity;
  // Block whose straight-line issue interval is being admitted.
  const loom_block_t* block;
} loom_aie2p_native_queue_t;

typedef struct loom_aie2p_native_borrow_t {
  // Protocol instance associated with the acquired view.
  const loom_aie2p_native_channel_t* channel;
  // Owned read/write that becomes the slot's physical pointer.
  loom_value_id_t record;
  // The direction's invocation can advance beyond the initial physical slot.
  bool record_dynamic;
} loom_aie2p_native_borrow_t;

static bool loom_aie2p_native_descriptor(
    const loom_aie2p_native_context_t* context,
    const loom_pipeline_worker_t* worker,
    const loom_movement_endpoint_t* endpoint, const loom_xdna_dma_facts_t* dma,
    uint32_t bytes, bool shim, uint32_t* words, iree_string_view_t* reason) {
  loom_storage_geometry_t geometry;
  if (!loom_storage_geometry_query(&worker->facts.context, context->code.module,
                                   endpoint->type, &geometry)) {
    *reason = IREE_SV("specialized DMA endpoint shape and strides");
    return false;
  }
  loom_aie2p_array_binding_plan_t layout = {0};
  if (!loom_aie2p_array_resolve_binding_transfer(
          &geometry, geometry.rank, context->family, 0, false, 0, bytes, 1, dma,
          &layout, reason)) {
    return false;
  }
  words[0] =
      bytes / dma->transfer_length_granularity - dma->transfer_length_offset;
  if (shim) {
    words[4] = loom_xdna_register_field_encode_admitted(
        LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD4_BURST_LENGTH, 3);
    words[5] = loom_xdna_register_field_encode_admitted(
        LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD5_AXI_CACHE, 2);
    words[7] = loom_xdna_register_field_encode_admitted(
        LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD7_VALID_BD, 1);
    const loom_xdna_register_field_id_t step[] = {
        LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD3_D0_STEP_SIZE,
        LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD4_D1_STEP_SIZE,
        LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD5_D2_STEP_SIZE};
    const loom_xdna_register_field_id_t wrap[] = {
        LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD3_D0_WRAP,
        LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD4_D1_WRAP};
    for (uint8_t i = 0; i < layout.dma_dimension_count; ++i) {
      words[3 + i] |= loom_xdna_register_field_encode_admitted(
          step[i], layout.dma_dimensions[i].step_size - 1);
      if (i + 1 < layout.dma_dimension_count) {
        words[3 + i] |= loom_xdna_register_field_encode_admitted(
            wrap[i],
            layout.dma_dimensions[i].wrap & ((1u << dma->wrap_bits) - 1));
      }
    }
  } else {
    words[5] = loom_xdna_register_field_encode_admitted(
        LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD5_VALID_BD, 1);
    const loom_xdna_register_field_id_t step[] = {
        LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD2_D0_STEP_SIZE,
        LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD2_D1_STEP_SIZE,
        LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD3_D2_STEP_SIZE};
    const loom_xdna_register_field_id_t wrap[] = {
        LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD3_D0_WRAP,
        LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD3_D1_WRAP};
    for (uint8_t i = 0; i < layout.dma_dimension_count; ++i) {
      words[i < 2 ? 2 : 3] |= loom_xdna_register_field_encode_admitted(
          step[i], layout.dma_dimensions[i].step_size - 1);
      if (i + 1 < layout.dma_dimension_count) {
        words[3] |= loom_xdna_register_field_encode_admitted(
            wrap[i],
            layout.dma_dimensions[i].wrap & ((1u << dma->wrap_bits) - 1));
      }
    }
  }
  return true;
}

// Labels only newly appended edges; prefix sharing belongs to a single route
// source. Packet routes use distinct sources so filters remain unambiguous.
static void loom_aie2p_native_route_label(loom_aie2p_native_context_t* context,
                                          iree_host_size_t first,
                                          uint8_t packet) {
  for (iree_host_size_t i = first; i < context->routing.route_count; ++i) {
    context->routes[i] = (loom_aie2p_native_route_t){
        .edge = &context->routing.routes[i], .packet = packet};
  }
}

static bool loom_aie2p_native_packet_route(
    loom_aie2p_native_context_t* context, uint32_t* next_source,
    loom_xdna_tile_coordinate_t source, loom_xdna_stream_port_t source_port,
    loom_xdna_tile_coordinate_t destination,
    loom_xdna_stream_port_t destination_port, uint8_t packet) {
  const iree_host_size_t first = context->routing.route_count;
  const uint32_t index = (*next_source)++;
  if (!loom_aie2p_array_route_stream(&context->routing, index, index, source,
                                     source_port, 0, destination,
                                     destination_port, 0)) {
    return false;
  }
  loom_aie2p_native_route_label(context, first, packet);
  return true;
}

typedef struct loom_aie2p_native_switch_t {
  // Packet arbiter/selection pair, UINT8_MAX if free, UINT8_MAX - 1 if circuit.
  uint8_t* masters;
  // Packet-filter rule count per source slave, or UINT8_MAX if circuit.
  uint8_t* slaves;
  // Next unique arbiter/selection pair, from the architectural 8 x 4 set.
  uint8_t next_selection;
} loom_aie2p_native_switch_t;

static iree_status_t loom_aie2p_native_admit_routes(
    loom_aie2p_native_context_t* context, const loom_op_t* entry,
    bool* out_valid) {
  const iree_host_size_t tile_count =
      context->family->column_count * context->family->row_count;
  loom_aie2p_native_switch_t* switches;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      context->pass->arena, tile_count, sizeof(*switches), (void**)&switches));
  memset(switches, 0, tile_count * sizeof(*switches));
  for (iree_host_size_t i = 0; i < context->routing.route_count; ++i) {
    loom_aie2p_native_route_t* selected = &context->routes[i];
    const loom_aie2p_array_route_plan_t* edge = selected->edge;
    if (edge->switch_kind == LOOM_AIE2P_ARRAY_SWITCH_KIND_SHIM_MUX) {
      continue;
    }
    const iree_host_size_t tile_index =
        edge->coordinate.column * context->family->row_count +
        edge->coordinate.row;
    const loom_xdna_tile_kind_t kind = context->tiles[tile_index].facts->kind;
    loom_aie2p_native_switch_t* state = &switches[tile_index];
    if (!state->masters) {
      const loom_xdna_register_field_id_t master_fields[] = {
          LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_STREAM_MASTER_CONFIG_ENABLE,
          LOOM_XDNA_REGISTER_FIELD_MEMORY_TILE_STREAM_MASTER_CONFIG_ENABLE,
          LOOM_XDNA_REGISTER_FIELD_CORE_STREAM_MASTER_CONFIG_ENABLE};
      const loom_xdna_register_field_id_t slave_fields[] = {
          LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_STREAM_SLAVE_CONFIG_ENABLE,
          LOOM_XDNA_REGISTER_FIELD_MEMORY_TILE_STREAM_SLAVE_CONFIG_ENABLE,
          LOOM_XDNA_REGISTER_FIELD_CORE_STREAM_SLAVE_CONFIG_ENABLE};
      loom_xdna_register_dimension_info_t masters, slaves;
      IREE_RETURN_IF_ERROR(loom_xdna_register_field_dimension(
          master_fields[kind - 1], 0, &masters));
      IREE_RETURN_IF_ERROR(loom_xdna_register_field_dimension(
          slave_fields[kind - 1], 0, &slaves));
      IREE_RETURN_IF_ERROR(iree_arena_allocate(
          context->pass->arena, masters.count, (void**)&state->masters));
      IREE_RETURN_IF_ERROR(iree_arena_allocate(
          context->pass->arena, slaves.count, (void**)&state->slaves));
      memset(state->masters, 0xff, masters.count);
      memset(state->slaves, 0, slaves.count);
    }
    const uint16_t master =
        loom_xdna_array_stream_port_range(context->family, kind,
                                          LOOM_XDNA_STREAM_DIRECTION_MASTER,
                                          edge->destination_port)
            ->ordinal +
        edge->destination_channel;
    const uint16_t slave =
        loom_xdna_array_stream_port_range(context->family, kind,
                                          LOOM_XDNA_STREAM_DIRECTION_SLAVE,
                                          edge->source_port)
            ->ordinal +
        edge->source_channel;
    if (selected->packet == UINT8_MAX) {
      // A circuit source may fan out, but each destination master has exactly
      // one source. Packet filtering and circuit mode cannot share a port.
      if (state->masters[master] != UINT8_MAX ||
          (state->slaves[slave] != 0 && state->slaves[slave] != UINT8_MAX)) {
        return loom_aie2p_native_reject(
            context, entry,
            IREE_SV("exclusive circuit destinations and compatible stream-port "
                    "modes"));
      }
      state->masters[master] = UINT8_MAX - 1;
      state->slaves[slave] = UINT8_MAX;
      continue;
    }
    if (state->masters[master] == UINT8_MAX - 1 ||
        state->slaves[slave] == UINT8_MAX) {
      return loom_aie2p_native_reject(
          context, entry,
          IREE_SV("separate circuit and packet stream-switch ports"));
    }
    if (state->slaves[slave] == 4 ||
        (state->masters[master] == UINT8_MAX && state->next_selection == 32)) {
      return loom_aie2p_native_reject(
          context, entry,
          IREE_SV("available packet filters and unicast master selections"));
    }
    if (state->masters[master] == UINT8_MAX) {
      state->masters[master] = state->next_selection++;
    }
    selected->selection = state->masters[master];
    selected->slot = state->slaves[slave]++;
  }
  *out_valid = true;
  return iree_ok_status();
}

static iree_status_t loom_aie2p_native_select_worker_transfers(
    loom_aie2p_native_context_t* context,
    const loom_pipeline_realization_t* realization, iree_host_size_t i,
    uint32_t* next_source, uint8_t* next_controller, bool* out_valid) {
  *out_valid = false;
  iree_arena_allocator_t* arena = context->pass->arena;
  loom_module_t* module = context->code.module;
  const iree_host_size_t tile_count =
      context->family->column_count * context->family->row_count;
  const loom_block_t* entry =
      loom_region_entry_block(loom_func_like_body(realization->function));
  const loom_pipeline_worker_t* source = &realization->workers[i];
  loom_aie2p_native_worker_t* worker = &context->workers[i];
  const bool configuration =
      worker->execution == LOOM_AIE2P_NATIVE_EXECUTION_CONFIGURATION;
  loom_aie2p_native_queue_t* queues;
  uint8_t* controls;
  loom_aie2p_native_borrow_t* borrows;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, tile_count * 2, sizeof(*queues), (void**)&queues));
  memset(queues, 0, tile_count * 2 * sizeof(*queues));
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate(arena, tile_count, (void**)&controls));
  memset(controls, 0xff, tile_count);
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate_array(arena, source->value_domain.value_count,
                                sizeof(*borrows), (void**)&borrows));
  memset(borrows, 0, source->value_domain.value_count * sizeof(*borrows));
  for (iree_host_size_t j = 0; j < source->channels.action_count; ++j) {
    const loom_channel_plan_action_t* action = &source->channels.actions[j];
    if (!loom_channel_acquire_isa(action->op) &&
        !loom_channel_reserve_isa(action->op) &&
        !loom_channel_wait_isa(action->op)) {
      continue;
    }
    const loom_pipeline_resource_channel_t* bound =
        loom_pipeline_resources_lookup_channel(&realization->resources,
                                               action->channel->value_id);
    const loom_value_id_t* results = loom_op_const_results(action->op);
    const bool wait = loom_channel_wait_isa(action->op);
    const loom_value_ordinal_t ordinal = loom_local_value_domain_ordinal(
        &source->value_domain, results[wait ? 0 : 1]);
    borrows[ordinal] = (loom_aie2p_native_borrow_t){
        .channel = &context->channels[bound - realization->resources.channels],
        .record = wait ? loom_channel_wait_read(action->op) : results[0],
        .record_dynamic = bound->capacity > 1 &&
                          source->completion.actions[j].maximum_admissions > 1};
  }
  uint8_t next_control = 0;
  bool has_egress = false;
  iree_host_size_t transport_step = 0;
  loom_aie2p_native_transfer_t** tail = &worker->transfers;
  iree_host_size_t stream_index = 0;
  for (const loom_kernel_async_stream_t* stream =
           configuration ? source->transport.streams[0]
                         : source->asynchronous.streams;
       stream;
       stream = configuration ? (++stream_index < source->transport.stream_count
                                     ? source->transport.streams[stream_index]
                                     : NULL)
                              : stream->next) {
    for (iree_host_size_t j = 0; j < stream->transfer_count; ++j) {
      const loom_kernel_async_transfer_t* transfer = &stream->transfers[j];
      const loom_movement_request_t* request = &transfer->request;
      const bool ingress =
          request->source.memory_space == LOOM_VALUE_FACT_MEMORY_SPACE_GLOBAL;
      const loom_movement_endpoint_t* external =
          ingress ? &request->source : &request->dest;
      const loom_movement_endpoint_t* local =
          ingress ? &request->dest : &request->source;
      if (request->kind != LOOM_MOVEMENT_KIND_KERNEL_ASYNC_COPY ||
          external->memory_space != LOOM_VALUE_FACT_MEMORY_SPACE_GLOBAL ||
          local->memory_space != LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP) {
        return loom_aie2p_native_reject(
            context, request->op,
            IREE_SV("a dense or strided borrowed copy between caller memory "
                    "and tile memory"));
      }
      const loom_view_region_t *external_view = NULL, *local_view = NULL;
      loom_view_region_table_try_lookup(
          &source->asynchronous.movement.view_regions, external->value_id,
          &external_view);
      loom_view_region_table_try_lookup(
          &source->asynchronous.movement.view_regions, local->value_id,
          &local_view);
      const loom_aie2p_native_borrow_t borrow =
          borrows[loom_local_value_domain_ordinal(
              &source->value_domain, local_view->base_view_value_id)];
      const loom_value_t* root =
          loom_module_value(module, external->root_value_id);
      if (!borrow.channel || !loom_value_is_block_arg(root) ||
          loom_value_def_block(root) != entry ||
          !loom_symbolic_expr_is_linear(&external_view->begin_byte_offset) ||
          !loom_symbolic_expr_is_linear(&local_view->projection_byte_offset) ||
          external->begin_byte_offset.facts.range_lo < 0 ||
          external->end_byte_offset.facts.range_hi > UINT32_MAX ||
          local_view->projection_byte_offset.facts.range_lo < 0 ||
          local_view->projection_byte_offset.facts.range_hi > UINT32_MAX) {
        return loom_aie2p_native_reject(
            context, request->op,
            IREE_SV("a caller-buffer root and a borrowed channel record with "
                    "bounded native offsets"));
      }
      loom_aie2p_native_tile_t* local_tile =
          context->pool_tiles[borrow.channel->pool_index];
      loom_aie2p_native_tile_t* shim =
          &context->tiles[worker->tile->coordinate.column *
                          context->family->row_count];
      if (local_tile->next_descriptor ==
              local_tile->facts->dma.buffer_descriptor_count ||
          shim->next_descriptor == shim->facts->dma.buffer_descriptor_count ||
          (ingress && local_tile->next_lock == local_tile->facts->lock_count) ||
          loom_movement_endpoint_minimum_byte_alignment(external) <
              shim->facts->dma.address_alignment ||
          loom_movement_endpoint_minimum_byte_alignment(local) <
              local_tile->facts->dma.address_alignment) {
        return loom_aie2p_native_reject(
            context, request->op,
            IREE_SV("aligned endpoints and available native descriptors and "
                    "completion semaphores"));
      }
      const iree_host_size_t local_index = local_tile - context->tiles;
      loom_aie2p_native_queue_t* queue =
          &queues[local_index * 2 + (ingress ? 0 : 1)];
      if (!queue->path) {
        uint8_t* local_engine =
            ingress ? &local_tile->next_s2mm : &local_tile->next_mm2s;
        uint8_t* shim_engine = ingress ? &shim->next_mm2s : &shim->next_s2mm;
        if (*local_engine ==
                local_tile->facts->dma.channel_count_per_direction ||
            *shim_engine == shim->facts->dma.channel_count_per_direction ||
            (!ingress && has_egress && !configuration)) {
          return loom_aie2p_native_reject(
              context, request->op,
              IREE_SV("an independent DMA engine pair and one external "
                      "completion stream per worker"));
        }
        IREE_RETURN_IF_ERROR(iree_arena_allocate(arena, sizeof(*queue->path),
                                                 (void**)&queue->path));
        *queue->path =
            (loom_aie2p_native_dma_path_t){.next = worker->paths,
                                           .local = local_tile,
                                           .shim = shim,
                                           .local_engine = (*local_engine)++,
                                           .shim_engine = (*shim_engine)++,
                                           .ingress = ingress};
        worker->paths = queue->path;
        queue->capacity = iree_min(local_tile->facts->dma.task_queue_depth,
                                   shim->facts->dma.task_queue_depth);
        IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
            arena, queue->capacity, sizeof(*queue->completions),
            (void**)&queue->completions));
        const iree_host_size_t route_first = context->routing.route_count;
        const uint32_t route = (*next_source)++;
        const bool routed =
            ingress ? loom_aie2p_array_route_ingress(
                          &context->routing, route, route, shim->coordinate,
                          queue->path->shim_engine, local_tile->coordinate,
                          queue->path->local_engine)
                    : loom_aie2p_array_route_egress(
                          &context->routing, route, route,
                          local_tile->coordinate, queue->path->local_engine,
                          shim->coordinate, queue->path->shim_engine);
        if (!routed) {
          return loom_aie2p_native_reject(
              context, request->op, IREE_SV("available payload stream links"));
        }
        loom_aie2p_native_route_label(context, route_first, UINT8_MAX);
        loom_aie2p_native_tile_t* destinations[] = {local_tile, shim};
        uint8_t* packets[] = {&queue->path->control.local,
                              &queue->path->control.shim};
        for (unsigned k = 0; !configuration && k < 2; ++k) {
          const iree_host_size_t tile_index = destinations[k] - context->tiles;
          if (controls[tile_index] == UINT8_MAX) {
            if (next_control == 4 ||
                !loom_aie2p_native_packet_route(
                    context, next_source, worker->tile->coordinate,
                    LOOM_XDNA_STREAM_PORT_CORE, destinations[k]->coordinate,
                    LOOM_XDNA_STREAM_PORT_TILE_CONTROL, next_control)) {
              return loom_aie2p_native_reject(
                  context, request->op,
                  IREE_SV("available worker control-packet routes"));
            }
            controls[tile_index] = next_control++;
          }
          *packets[k] = controls[tile_index];
        }
        if (!ingress) {
          has_egress = true;
          queue->path->completion_packet =
              configuration ? 0 : next_controller[shim->coordinate.column]++;
          if (!loom_aie2p_native_packet_route(
                  context, next_source, shim->coordinate,
                  LOOM_XDNA_STREAM_PORT_TILE_CONTROL,
                  configuration ? shim->coordinate : worker->tile->coordinate,
                  configuration ? LOOM_XDNA_STREAM_PORT_SOUTH
                                : LOOM_XDNA_STREAM_PORT_CORE,
                  queue->path->completion_packet)) {
            return loom_aie2p_native_reject(
                context, request->op,
                IREE_SV("an independent external-completion return route"));
          }
        }
      }
      if (queue->block != stream->block) {
        queue->count = 0;
        queue->block = stream->block;
      }
      uint8_t pending = 0;
      for (uint8_t k = 0; k < queue->count; ++k) {
        if (queue->completions[k]->block_ordinal > request->op->block_ordinal) {
          queue->completions[pending++] = queue->completions[k];
        }
      }
      queue->count = pending;
      if (pending == queue->capacity) {
        return loom_aie2p_native_reject(
            context, request->op,
            IREE_SV("DMA group retirement before the native task queues fill"));
      }
      const loom_op_t* completion =
          stream->groups[transfer->group_index].completion;
      queue->completions[queue->count++] = completion;
      loom_aie2p_native_transfer_t* selected;
      IREE_RETURN_IF_ERROR(
          iree_arena_allocate(arena, sizeof(*selected), (void**)&selected));
      *selected = (loom_aie2p_native_transfer_t){
          .source = transfer,
          .completion = completion,
          .path = queue->path,
          .binding = loom_value_def_index(root),
          .local_descriptor = local_tile->next_descriptor++,
          .shim_descriptor = shim->next_descriptor++,
          .external_view = external_view,
          .local_view = local_view,
          .local_channel = borrow.channel,
          .local_record = borrow.record,
          .record_dynamic = borrow.record_dynamic};
      *tail = selected;
      tail = &selected->next;
      iree_string_view_t reason;
      if (!loom_aie2p_native_descriptor(
              context, source, external, &shim->facts->dma,
              (uint32_t)request->transferred_byte_count, true,
              selected->shim_words, &reason) ||
          !loom_aie2p_native_descriptor(
              context, source, local, &local_tile->facts->dma,
              (uint32_t)request->transferred_byte_count, false,
              selected->local_words, &reason)) {
        return loom_aie2p_native_reject(context, request->op, reason);
      }
      // Each transfer site owns its descriptors. A fixed slot and projection
      // need no per-issue address patch, even when the site repeats in a loop.
      if (!borrow.record_dynamic &&
          loom_symbolic_expr_is_constant(&local_view->projection_byte_offset)) {
        const uint32_t address =
            borrow.channel->byte_offset +
            (uint32_t)local_view->projection_byte_offset.constant;
        selected->local_words[0] |= loom_xdna_register_field_encode_admitted(
            LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD0_BASE_ADDRESS,
            address >> local_tile->facts->dma.address_encoding_shift);
      }
      if (ingress) {
        selected->completion_lock = local_tile->next_lock++;
        if (!configuration) {
          IREE_RETURN_IF_ERROR(loom_xdna_array_form_lock_selector(
              context->family, worker->tile->coordinate, local_tile->coordinate,
              selected->completion_lock, &selected->completion_selector));
        }
        selected->local_words[5] |=
            loom_xdna_register_field_encode_admitted(
                LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD5_LOCK_RELEASE_ID,
                selected->completion_lock) |
            loom_xdna_register_field_encode_admitted(
                LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD5_LOCK_RELEASE_VALUE,
                1);
      }
      if (configuration) {
        while (source->transport.steps[transport_step].op != request->op) {
          ++transport_step;
        }
        const loom_op_t* preceding =
            transport_step ? source->transport.steps[transport_step - 1].op
                           : NULL;
        if (!ingress && preceding && loom_channel_acquire_isa(preceding) &&
            transport_step + 1 < source->transport.count &&
            source->transport.steps[transport_step + 1].op == completion &&
            loom_op_const_results(preceding)[0] == borrow.record) {
          selected->admission = preceding;
          selected->local_words[5] |=
              loom_xdna_register_field_encode_admitted(
                  LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD5_LOCK_ACQUIRE_ID,
                  borrow.channel->ready_lock) |
              loom_xdna_register_field_encode_admitted(
                  LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD5_LOCK_ACQUIRE_VALUE,
                  -1) |
              loom_xdna_register_field_encode_admitted(
                  LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD5_LOCK_ACQUIRE_ENABLE,
                  1);
        }
      }
      if (!loom_symbolic_expr_is_constant(&external_view->begin_byte_offset)) {
        IREE_RETURN_IF_ERROR(loom_source_storage_packing_reserve(
            context->inventory.pools[worker->tile->pool_index].packing, 8, 8,
            NULL, 0, &selected->base_storage_offset));
        IREE_RETURN_IF_ERROR(loom_xdna_array_form_load_address(
            context->family, worker->tile->coordinate,
            LOOM_XDNA_MEMORY_SPACE_DATA, worker->tile->coordinate,
            selected->base_storage_offset, 8, &selected->base_load_address));
      }
      loom_aie2p_native_binding_t* binding =
          &context->bindings[selected->binding];
      binding->byte_length =
          iree_max(binding->byte_length,
                   (uint64_t)external->end_byte_offset.facts.range_hi);
      binding->byte_alignment =
          iree_max(binding->byte_alignment, external->root_minimum_alignment);
      binding->access |= ingress ? LOOM_AIE2P_ARRAY_BINDING_ACCESS_READ
                                 : LOOM_AIE2P_ARRAY_BINDING_ACCESS_WRITE;
    }
  }
  *out_valid = true;
  return iree_ok_status();
}

iree_status_t loom_aie2p_native_select_transfers(
    loom_aie2p_native_context_t* context,
    const loom_pipeline_realization_t* realization, bool* out_valid) {
  *out_valid = false;
  iree_arena_allocator_t* arena = context->pass->arena;
  iree_host_size_t transfer_count = 0;
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    for (const loom_kernel_async_stream_t* stream =
             realization->workers[i].asynchronous.streams;
         stream; stream = stream->next) {
      transfer_count += stream->transfer_count;
    }
  }
  const iree_host_size_t source_count = transfer_count * 4;
  const iree_host_size_t route_capacity =
      source_count *
      (context->family->column_count + context->family->row_count + 2);
  IREE_RETURN_IF_ERROR(loom_aie2p_array_route_builder_initialize(
      context->family, source_count, route_capacity, arena, &context->routing));
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, route_capacity,
                                                 sizeof(*context->routes),
                                                 (void**)&context->routes));
  uint32_t next_source = 0;
  uint8_t* next_controller;
  IREE_RETURN_IF_ERROR(iree_arena_allocate(arena, context->family->column_count,
                                           (void**)&next_controller));
  memset(next_controller, 1, context->family->column_count);
  iree_status_t status = iree_ok_status();
  bool valid = true;
  for (iree_host_size_t i = 0; iree_status_is_ok(status) && valid &&
                               i < realization->resources.strand_count;
       ++i) {
    loom_pipeline_worker_t* worker = &realization->workers[i];
    loom_local_value_domain_restore(&worker->value_domain);
    status = loom_aie2p_native_select_worker_transfers(
        context, realization, i, &next_source, next_controller, &valid);
    loom_local_value_domain_release(&worker->value_domain);
  }
  if (iree_status_is_ok(status) && valid) {
    status = loom_aie2p_native_admit_routes(context, realization->function.op,
                                            out_valid);
  }
  return status;
}
