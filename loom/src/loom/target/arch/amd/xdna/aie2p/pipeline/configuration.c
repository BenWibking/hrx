// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amd/xdna/aie2p/descriptors/configuration_descriptors.h"
#include "loom/target/arch/amd/xdna/aie2p/ops/target.h"
#include "loom/target/arch/amd/xdna/aie2p/pipeline/native.h"
#include "loom/target/arch/amd/xdna/aie2p/records/target_records.h"
#include "loom/target/arch/amd/xdna/array/registers.h"
#include "loom/target/facts_builder.h"
#include "loom/target/function_version.h"

typedef struct loom_aie2p_native_configuration_t {
  // Native occurrence supplying physical selection.
  loom_aie2p_native_context_t* context;
  // Typed configuration instruction descriptors.
  const loom_low_descriptor_set_t* descriptors;
  // Builder in the currently emitted configuration function.
  loom_builder_t builder;
  // Configuration scalar type, independent of the core's machine word width.
  loom_type_t scalar_type;
  // Native invocation buffer identity.
  loom_type_t binding_type;
  // Relocated byte range in an invocation buffer.
  loom_type_t span_type;
  // Constant immediate name in configuration descriptors.
  loom_string_id_t value_name;
  // Explicit configuration representation contract.
  loom_string_id_t contract;
  // Exact deployment target declaration.
  loom_symbol_ref_t target;
} loom_aie2p_native_configuration_t;

static iree_status_t loom_aie2p_native_config_constant(
    loom_aie2p_native_configuration_t* config, uint64_t value,
    loom_value_id_t* out_value) {
  const loom_named_attr_t attr = {.name_id = config->value_name,
                                  .value = loom_attr_i64((int64_t)value)};
  loom_op_t* op;
  IREE_RETURN_IF_ERROR(loom_low_build_resolved_descriptor_const(
      &config->builder, config->descriptors,
      &config->descriptors->descriptors
           [AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_CONSTANT],
      loom_make_named_attr_slice(&attr, 1), config->scalar_type,
      LOOM_LOCATION_UNKNOWN, &op));
  *out_value = loom_low_const_result(op);
  return iree_ok_status();
}

static iree_status_t loom_aie2p_native_config_op(
    loom_aie2p_native_configuration_t* config, uint32_t descriptor,
    const uint64_t* values, iree_host_size_t value_count,
    loom_named_attr_slice_t attributes) {
  loom_value_id_t* operands;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(config->context->pass->arena,
                                                 value_count, sizeof(*operands),
                                                 (void**)&operands));
  for (iree_host_size_t i = 0; i < value_count; ++i) {
    IREE_RETURN_IF_ERROR(
        loom_aie2p_native_config_constant(config, values[i], &operands[i]));
  }
  loom_op_t* op;
  return loom_low_build_resolved_descriptor_op(
      &config->builder, config->descriptors,
      &config->descriptors->descriptors[descriptor], 0, operands, value_count,
      attributes, NULL, 0, NULL, 0, LOOM_LOCATION_UNKNOWN, &op);
}

static iree_status_t loom_aie2p_native_config_function(
    loom_aie2p_native_configuration_t* config, loom_symbol_ref_t symbol,
    uint8_t visibility, uint8_t retain, loom_op_t** out_function) {
  loom_builder_set_block(&config->builder,
                         loom_module_block(config->context->code.module));
  return loom_low_func_def_build(
      &config->builder,
      LOOM_LOW_FUNC_DEF_BUILD_FLAG_HAS_TARGET |
          (visibility ? LOOM_LOW_FUNC_DEF_BUILD_FLAG_HAS_VISIBILITY : 0) |
          (retain ? LOOM_LOW_FUNC_DEF_BUILD_FLAG_HAS_RETAIN : 0) |
          (visibility || retain ? LOOM_LOW_FUNC_DEF_BUILD_FLAG_HAS_ABI : 0),
      visibility, retain, 0, 0, 0, 0, 0, config->contract, config->target,
      LOOM_TARGET_ABI_ARRAY_PROGRAM, loom_named_attr_slice_empty(),
      loom_named_attr_slice_empty(), LOOM_STRING_ID_INVALID,
      loom_named_attr_slice_empty(), symbol, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
      LOOM_LOCATION_UNKNOWN, out_function);
}

static iree_status_t loom_aie2p_native_config_register(
    loom_aie2p_native_configuration_t* config, loom_xdna_tile_coordinate_t tile,
    loom_xdna_register_field_id_t field, const uint16_t* indices,
    iree_host_size_t index_count, int64_t value, uint32_t instruction) {
  uint64_t address;
  uint32_t bits;
  loom_xdna_register_field_info_t info;
  IREE_RETURN_IF_ERROR(loom_xdna_register_field_info(field, &info));
  IREE_RETURN_IF_ERROR(loom_xdna_register_field_address(
      config->context->family, field, tile, index_count, indices, &address));
  IREE_RETURN_IF_ERROR(loom_xdna_register_field_encode(field, value, &bits));
  const uint64_t mask = ((UINT64_C(1) << info.bit_width) - 1)
                        << info.least_significant_bit;
  const uint64_t operands[] = {address, mask, bits};
  return loom_aie2p_native_config_op(config, instruction, operands, 3,
                                     loom_named_attr_slice_empty());
}

static iree_status_t loom_aie2p_native_config_write(
    loom_aie2p_native_configuration_t* config, uint64_t address,
    uint32_t value) {
  const uint64_t operands[] = {address, value};
  return loom_aie2p_native_config_op(
      config, AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE32,
      operands, 2, loom_named_attr_slice_empty());
}

static iree_status_t loom_aie2p_native_config_routes(
    loom_aie2p_native_configuration_t* config) {
  const loom_aie2p_native_context_t* context = config->context;
  // The three tile kinds use the same switch word layout. These identifiers
  // select their distinct physical address patterns.
  static const loom_xdna_register_field_id_t fields[][3] = {
      {LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_STREAM_MASTER_CONFIG_ENABLE,
       LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_STREAM_SLAVE_CONFIG_ENABLE,
       LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_STREAM_SLAVE_SLOT_ENABLE},
      {LOOM_XDNA_REGISTER_FIELD_MEMORY_TILE_STREAM_MASTER_CONFIG_ENABLE,
       LOOM_XDNA_REGISTER_FIELD_MEMORY_TILE_STREAM_SLAVE_CONFIG_ENABLE,
       LOOM_XDNA_REGISTER_FIELD_MEMORY_TILE_STREAM_SLAVE_SLOT_ENABLE},
      {LOOM_XDNA_REGISTER_FIELD_CORE_STREAM_MASTER_CONFIG_ENABLE,
       LOOM_XDNA_REGISTER_FIELD_CORE_STREAM_SLAVE_CONFIG_ENABLE,
       LOOM_XDNA_REGISTER_FIELD_CORE_STREAM_SLAVE_SLOT_ENABLE},
  };
  static const loom_xdna_register_field_id_t mux_fields[8] = {
      0,
      0,
      LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_MUX_CONFIG_SOUTH2,
      LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_MUX_CONFIG_SOUTH3,
      0,
      0,
      LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_MUX_CONFIG_SOUTH6,
      LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_MUX_CONFIG_SOUTH7};
  static const loom_xdna_register_field_id_t demux_fields[6] = {
      0,
      0,
      LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DEMUX_CONFIG_SOUTH2,
      LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DEMUX_CONFIG_SOUTH3,
      LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DEMUX_CONFIG_SOUTH4,
      LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DEMUX_CONFIG_SOUTH5};
  for (iree_host_size_t i = 0; i < context->routing.route_count; ++i) {
    const loom_aie2p_native_route_t* selected = &context->routes[i];
    const loom_aie2p_array_route_plan_t* edge = selected->edge;
    if (edge->switch_kind == LOOM_AIE2P_ARRAY_SWITCH_KIND_SHIM_MUX) {
      const loom_xdna_register_field_id_t field =
          edge->source_port == LOOM_XDNA_STREAM_PORT_DMA
              ? mux_fields[edge->destination_channel]
              : demux_fields[edge->source_channel];
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
          config, edge->coordinate, field, NULL, 0, 1,
          AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
      continue;
    }
    const loom_xdna_tile_kind_t kind =
        context
            ->tiles[edge->coordinate.column * context->family->row_count +
                    edge->coordinate.row]
            .facts->kind;
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
    uint32_t master_bits = UINT32_C(1) << 31;
    uint32_t slave_bits = UINT32_C(1) << 31;
    if (selected->packet == UINT8_MAX) {
      master_bits |= slave;
    } else {
      const uint32_t arbiter = selected->selection & 7;
      const uint32_t master_select = selected->selection >> 3;
      // Control requests and one-word completion packets retain their header
      // at every hop, including TileControl and the receiving core.
      master_bits |=
          UINT32_C(1) << 30 | arbiter | (UINT32_C(1) << (master_select + 3));
      slave_bits |= UINT32_C(1) << 30;
      const uint16_t slot[] = {slave, selected->slot};
      const uint32_t slot_bits = (uint32_t)selected->packet << 24 |
                                 UINT32_C(31) << 16 | UINT32_C(1) << 8 |
                                 master_select << 4 | arbiter;
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_write(
          config,
          loom_xdna_register_field_address_admitted(
              context->family, fields[kind - 1][2], edge->coordinate, slot),
          slot_bits));
    }
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_write(
        config,
        loom_xdna_register_field_address_admitted(
            context->family, fields[kind - 1][0], edge->coordinate, &master),
        master_bits));
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_write(
        config,
        loom_xdna_register_field_address_admitted(
            context->family, fields[kind - 1][1], edge->coordinate, &slave),
        slave_bits));
  }
  return iree_ok_status();
}

static iree_status_t loom_aie2p_native_config_transfers(
    loom_aie2p_native_configuration_t* config,
    const loom_pipeline_realization_t* realization) {
  const loom_aie2p_native_context_t* context = config->context;
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    const loom_aie2p_native_worker_t* worker = &context->workers[i];
    for (const loom_aie2p_native_dma_path_t* path = worker->paths; path;
         path = path->next) {
      const loom_xdna_register_field_id_t local_control =
          path->ingress
              ? LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_CHANNEL_S2MM_CONTROL_RESET
              : LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_CHANNEL_MM2S_CONTROL_RESET;
      const uint16_t local_engine = path->local_engine;
      const uint64_t local_address = loom_xdna_register_field_address_admitted(
          context->family, local_control, path->local->coordinate,
          &local_engine);
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_write(
          config, local_address,
          loom_xdna_register_field_encode_admitted(local_control, 1)));
      IREE_RETURN_IF_ERROR(
          loom_aie2p_native_config_write(config, local_address, 0));
      const loom_xdna_register_field_id_t shim_control =
          path->ingress
              ? LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_CHANNEL_MM2S_CONTROL_CONTROLLER_ID
              : LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_CHANNEL_S2MM_CONTROL_CONTROLLER_ID;
      const uint16_t shim_engine = path->shim_engine;
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_write(
          config,
          loom_xdna_register_field_address_admitted(
              context->family, shim_control, path->shim->coordinate,
              &shim_engine),
          loom_xdna_register_field_encode_admitted(shim_control,
                                                   path->completion_packet)));
    }
    for (const loom_aie2p_native_transfer_t* transfer = worker->transfers;
         transfer; transfer = transfer->next) {
      uint64_t local[7] = {loom_xdna_register_field_address_admitted(
          context->family,
          LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_DMA_BD_WORD0_BASE_ADDRESS,
          transfer->path->local->coordinate, &transfer->local_descriptor)};
      uint64_t shim[9] = {loom_xdna_register_field_address_admitted(
          context->family,
          LOOM_XDNA_REGISTER_FIELD_SHIM_NOC_DMA_BD_WORD0_BUFFER_LENGTH,
          transfer->path->shim->coordinate, &transfer->shim_descriptor)};
      for (unsigned j = 0; j < IREE_ARRAYSIZE(transfer->local_words); ++j) {
        local[j + 1] = transfer->local_words[j];
      }
      for (unsigned j = 0; j < IREE_ARRAYSIZE(transfer->shim_words); ++j) {
        shim[j + 1] = transfer->shim_words[j];
      }
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_op(
          config,
          AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_BLOCK32, local,
          IREE_ARRAYSIZE(local), loom_named_attr_slice_empty()));
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_op(
          config,
          AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_BLOCK32, shim,
          IREE_ARRAYSIZE(shim), loom_named_attr_slice_empty()));
    }
  }
  return iree_ok_status();
}

static iree_status_t loom_aie2p_native_config_bindings(
    loom_aie2p_native_configuration_t* config,
    const loom_pipeline_realization_t* realization) {
  const loom_aie2p_native_context_t* context = config->context;
  loom_value_id_t* bindings;
  IREE_RETURN_IF_ERROR(
      iree_arena_allocate_array(context->pass->arena, context->binding_count,
                                sizeof(*bindings), (void**)&bindings));
  for (iree_host_size_t i = 0; i < context->binding_count; ++i) {
    const loom_aie2p_native_binding_t* binding = &context->bindings[i];
    if (!binding->access) {
      const uint64_t ordinal = i;
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_op(
          config,
          AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_BINDING_UNUSED,
          &ordinal, 1, loom_named_attr_slice_empty()));
      continue;
    }
    const uint64_t values[] = {i, binding->access, binding->byte_length,
                               binding->byte_alignment};
    loom_value_id_t operands[4];
    for (unsigned j = 0; j < IREE_ARRAYSIZE(values); ++j) {
      IREE_RETURN_IF_ERROR(
          loom_aie2p_native_config_constant(config, values[j], &operands[j]));
    }
    loom_op_t* op;
    IREE_RETURN_IF_ERROR(loom_low_build_resolved_descriptor_op(
        &config->builder, config->descriptors,
        &config->descriptors->descriptors
             [AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_BINDING],
        0, operands, 4, loom_named_attr_slice_empty(), &config->binding_type, 1,
        NULL, 0, LOOM_LOCATION_UNKNOWN, &op));
    bindings[i] = loom_op_results(op)[0];
  }
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    const loom_aie2p_native_worker_t* worker = &context->workers[i];
    const uint64_t tile_address = (uint64_t)worker->tile->coordinate.column
                                      << context->family->column_shift |
                                  (uint64_t)worker->tile->coordinate.row
                                      << context->family->row_shift;
    for (const loom_aie2p_native_transfer_t* transfer = worker->transfers;
         transfer; transfer = transfer->next) {
      const loom_symbolic_expr_t* offset =
          &transfer->external_view->begin_byte_offset;
      const uint64_t begin =
          loom_symbolic_expr_is_constant(offset) ? offset->constant : 0;
      loom_value_id_t range[] = {bindings[transfer->binding],
                                 LOOM_VALUE_ID_INVALID, LOOM_VALUE_ID_INVALID};
      IREE_RETURN_IF_ERROR(
          loom_aie2p_native_config_constant(config, begin, &range[1]));
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_constant(
          config, context->bindings[transfer->binding].byte_length - begin,
          &range[2]));
      loom_op_t* op;
      IREE_RETURN_IF_ERROR(loom_low_build_resolved_descriptor_op(
          &config->builder, config->descriptors,
          &config->descriptors->descriptors
               [AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_RANGE],
          0, range, 3, loom_named_attr_slice_empty(), &config->span_type, 1,
          NULL, 0, LOOM_LOCATION_UNKNOWN, &op));
      loom_value_id_t operands[] = {LOOM_VALUE_ID_INVALID,
                                    loom_op_results(op)[0]};
      // Configuration writes address the memory owner's allocation space.
      // The worker's self-memory load aperture is a separate core address.
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_constant(
          config,
          tile_address + worker->tile->facts->memory.local_base +
              transfer->base_storage_offset,
          &operands[0]));
      IREE_RETURN_IF_ERROR(loom_low_build_resolved_descriptor_op(
          &config->builder, config->descriptors,
          &config->descriptors->descriptors
               [AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_ADDRESS],
          0, operands, 2, loom_named_attr_slice_empty(), NULL, 0, NULL, 0,
          LOOM_LOCATION_UNKNOWN, &op));
    }
  }
  return iree_ok_status();
}

iree_status_t loom_aie2p_native_emit_configuration(
    void* user_data, loom_rewriter_t* rewriter,
    const loom_pipeline_realization_t* realization) {
  loom_aie2p_native_context_t* context = user_data;
  loom_module_t* module = rewriter->module;
  loom_aie2p_native_configuration_t config = {
      .context = context,
      .descriptors = loom_aie2p_configuration_descriptor_set()};
  loom_builder_initialize(module, &module->arena, loom_module_block(module),
                          &config.builder);
  IREE_RETURN_IF_ERROR(loom_module_intern_string(
      module, IREE_SV("amd.xdna.aie2p.configuration"), &config.contract));
  IREE_RETURN_IF_ERROR(
      loom_module_intern_string(module, IREE_SV("value"), &config.value_name));
  IREE_RETURN_IF_ERROR(loom_low_build_typed_register_type(
      module, config.descriptors,
      AIE2P_CONFIGURATION_REG_CLASS_ID_AIE2P_CONFIG_SCALAR, 1,
      loom_type_scalar(LOOM_SCALAR_TYPE_I64), &config.scalar_type));
  IREE_RETURN_IF_ERROR(loom_low_build_register_type(
      config.descriptors, AIE2P_CONFIGURATION_REG_CLASS_ID_AIE2P_CONFIG_BINDING,
      1, &config.binding_type));
  IREE_RETURN_IF_ERROR(loom_low_build_register_type(
      config.descriptors, AIE2P_CONFIGURATION_REG_CLASS_ID_AIE2P_CONFIG_SPAN, 1,
      &config.span_type));
  IREE_RETURN_IF_ERROR(
      loom_aie2p_worker_symbol(&context->code, &config.target));
  loom_target_facts_t* facts;
  IREE_RETURN_IF_ERROR(loom_target_facts_builder_clone(&context->target->base,
                                                       &module->arena, &facts));
  loom_target_facts_builder_set_worker_contract(
      LOOM_AIE2P_TARGET_KIND_CONFIGURATION,
      &loom_aie2p_configuration_target_bundle, facts);
  loom_target_function_version_t* version =
      loom_target_function_version_cast(context->pass->function_version);
  const loom_resolved_target_t resolved = {
      .provider = version->resolved_target.provider, .facts = facts};
  IREE_RETURN_IF_ERROR(loom_aie2p_target_materialize_definition(
      &config.builder, &resolved, config.target, LOOM_LOCATION_UNKNOWN));
  loom_symbol_ref_t initialize_symbol, invoke_symbol;
  IREE_RETURN_IF_ERROR(
      loom_aie2p_worker_symbol(&context->code, &initialize_symbol));
  IREE_RETURN_IF_ERROR(
      loom_aie2p_worker_symbol(&context->code, &invoke_symbol));
  const loom_symbol_ref_t entry_symbol =
      loom_func_like_callee(realization->function);
  const uint8_t visibility = loom_func_like_visibility(realization->function);
  const uint8_t retain =
      iree_any_bit_set(module->symbols.entries[entry_symbol.symbol_id].flags,
                       LOOM_SYMBOL_FLAG_RETAIN)
          ? LOOM_LOW_RETAIN_RETAIN
          : 0;
  uint64_t columns = 1;
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    const uint64_t limit = context->workers[i].tile->coordinate.column + 1;
    if (limit > columns) {
      columns = limit;
    }
  }
  loom_op_t* initialize;
  IREE_RETURN_IF_ERROR(loom_aie2p_native_config_function(
      &config, initialize_symbol, 0, 0, &initialize));
  loom_builder_enter_region(&config.builder, initialize,
                            loom_low_func_def_body(initialize));
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    const loom_aie2p_native_worker_t* worker = &context->workers[i];
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, worker->tile->coordinate,
        LOOM_XDNA_REGISTER_FIELD_CORE_CONTROL_ENABLE, NULL, 0, 0,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, worker->tile->coordinate,
        LOOM_XDNA_REGISTER_FIELD_CORE_CONTROL_RESET, NULL, 0, 1,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
    const uint64_t position[] = {worker->tile->coordinate.column,
                                 worker->tile->coordinate.row};
    loom_string_id_t program_name;
    IREE_RETURN_IF_ERROR(
        loom_module_intern_string(module, IREE_SV("program"), &program_name));
    const loom_named_attr_t program = {
        .name_id = program_name,
        .value = loom_attr_symbol(
            loom_func_like_callee(realization->workers[i].function))};
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_op(
        &config, AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_PROGRAM_LOAD,
        position, 2, loom_make_named_attr_slice(&program, 1)));
  }
  for (iree_host_size_t i = 0; i < context->inventory.pool_count; ++i) {
    const uint64_t length = loom_source_storage_packing_requirement(
                                context->inventory.pools[i].packing)
                                .byte_length;
    if (!length) {
      continue;
    }
    const loom_xdna_tile_coordinate_t tile = context->pool_tiles[i]->coordinate;
    if ((uint64_t)tile.column + 1 > columns) {
      columns = tile.column + 1;
    }
    const uint64_t range[] = {tile.column, tile.row, 0, length};
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_op(
        &config, AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_DATA_RESERVE,
        range, 4, loom_named_attr_slice_empty()));
  }
  IREE_RETURN_IF_ERROR(loom_aie2p_native_config_routes(&config));
  IREE_RETURN_IF_ERROR(
      loom_aie2p_native_config_transfers(&config, realization));
  IREE_RETURN_IF_ERROR(loom_aie2p_worker_return(&config.builder, NULL, 0));

  loom_op_t* invoke;
  IREE_RETURN_IF_ERROR(
      loom_aie2p_native_config_function(&config, invoke_symbol, 0, 0, &invoke));
  loom_builder_enter_region(&config.builder, invoke,
                            loom_low_func_def_body(invoke));
  IREE_RETURN_IF_ERROR(loom_aie2p_native_config_bindings(&config, realization));
  for (iree_host_size_t i = 0; i < realization->resources.channel_count; ++i) {
    const loom_aie2p_native_channel_t* channel = &context->channels[i];
    const loom_xdna_tile_coordinate_t tile =
        context->pool_tiles[channel->pool_index]->coordinate;
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, tile, LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_LOCK_VALUE_VALUE,
        &channel->free_lock, 1, channel->source->capacity,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, tile, LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_LOCK_VALUE_VALUE,
        &channel->ready_lock, 1, 0,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
  }
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    const loom_aie2p_native_worker_t* worker = &context->workers[i];
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, worker->tile->coordinate,
        LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_LOCK_VALUE_VALUE,
        &worker->completion_lock, 1, 0,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
    for (const loom_aie2p_native_transfer_t* transfer = worker->transfers;
         transfer; transfer = transfer->next) {
      if (!transfer->path->ingress) {
        continue;
      }
      IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
          &config, transfer->path->local->coordinate,
          LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_LOCK_VALUE_VALUE,
          &transfer->completion_lock, 1, 0,
          AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
    }
  }
  // Every semaphore is initialized before any worker can produce a credit.
  // Ingress completions can belong to a neighboring worker's memory tile.
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    const loom_xdna_tile_coordinate_t tile =
        context->workers[i].tile->coordinate;
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, tile, LOOM_XDNA_REGISTER_FIELD_CORE_CONTROL_RESET, NULL, 0, 0,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, tile, LOOM_XDNA_REGISTER_FIELD_CORE_CONTROL_ENABLE, NULL, 0, 1,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
  }
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    const loom_aie2p_native_worker_t* worker = &context->workers[i];
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, worker->tile->coordinate,
        LOOM_XDNA_REGISTER_FIELD_COMPUTE_MEMORY_LOCK_VALUE_VALUE,
        &worker->completion_lock, 1, 1,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WAIT_MASK32));
  }
  for (iree_host_size_t i = 0; i < realization->resources.strand_count; ++i) {
    const loom_xdna_tile_coordinate_t tile =
        context->workers[i].tile->coordinate;
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, tile, LOOM_XDNA_REGISTER_FIELD_CORE_CONTROL_ENABLE, NULL, 0, 0,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
    IREE_RETURN_IF_ERROR(loom_aie2p_native_config_register(
        &config, tile, LOOM_XDNA_REGISTER_FIELD_CORE_CONTROL_RESET, NULL, 0, 1,
        AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32));
  }
  IREE_RETURN_IF_ERROR(loom_aie2p_worker_return(&config.builder, NULL, 0));

  IREE_RETURN_IF_ERROR(loom_rewriter_erase(rewriter, realization->function.op));
  loom_op_t* entry;
  IREE_RETURN_IF_ERROR(loom_aie2p_native_config_function(
      &config, entry_symbol, visibility, retain, &entry));
  loom_builder_enter_region(&config.builder, entry,
                            loom_low_func_def_body(entry));
  loom_string_id_t initialize_name, invoke_name;
  IREE_RETURN_IF_ERROR(loom_module_intern_string(module, IREE_SV("initialize"),
                                                 &initialize_name));
  IREE_RETURN_IF_ERROR(
      loom_module_intern_string(module, IREE_SV("invoke"), &invoke_name));
  const loom_named_attr_t entries[] = {
      {.name_id = initialize_name,
       .value = loom_attr_symbol(initialize_symbol)},
      {.name_id = invoke_name, .value = loom_attr_symbol(invoke_symbol)}};
  IREE_RETURN_IF_ERROR(loom_aie2p_native_config_op(
      &config, AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_ENTRY, &columns,
      1, loom_make_named_attr_slice(entries, 2)));
  IREE_RETURN_IF_ERROR(loom_aie2p_worker_return(&config.builder, NULL, 0));
  loom_function_version_update(&version->base,
                               loom_func_like_cast(module, entry));
  version->resolved_target = resolved;
  version->function_target_facts = facts;
  version->target_requirement_facts = facts;
  version->authored_target_is_exact = true;
  version->target_context_ordinal = realization->entry_target_context;
  return iree_ok_status();
}
