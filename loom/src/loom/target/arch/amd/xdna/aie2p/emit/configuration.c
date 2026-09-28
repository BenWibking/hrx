// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amd/xdna/aie2p/emit/configuration.h"

#include <inttypes.h>
#include <string.h>

#include "loom/codegen/low/function_model.h"
#include "loom/codegen/low/packet.h"
#include "loom/codegen/low/schedule/run.h"
#include "loom/ops/low/ops.h"
#include "loom/target/arch/amd/xdna/aie2p/descriptors/configuration_descriptors.h"
#include "loom/target/arch/amd/xdna/aie2p/emit/leaf_compile.h"
#include "loom/target/arch/amd/xdna/error_catalog.h"
#include "loom/target/reporting/low.h"

typedef enum loom_aie2p_configuration_phase_e {
  LOOM_AIE2P_CONFIGURATION_ENTRY,
  LOOM_AIE2P_CONFIGURATION_INITIALIZE,
  LOOM_AIE2P_CONFIGURATION_INVOKE,
} loom_aie2p_configuration_phase_t;

// Statically evaluated operands indexed by the shared Low value domain. The
// descriptor's register class determines which member an operand consumes.
typedef union loom_aie2p_configuration_value_t {
  // Exact nonnegative configuration scalar.
  uint64_t scalar;
  // Explicit external binding ordinal.
  uint32_t binding;
  // Byte span within an external binding.
  struct {
    // External binding providing the runtime base address.
    uint32_t binding;
    // Byte offset relative to that runtime base.
    uint64_t offset;
    // Complete byte span reachable by the command.
    uint64_t length;
  } range;
} loom_aie2p_configuration_value_t;

typedef struct loom_aie2p_configuration_worker_t {
  // Detached native object compiled once per referenced complete function.
  loom_aie2p_leaf_contribution_t contribution;
  // Code linked against the compute-tile instruction-memory contract.
  loom_aie2p_linked_tile_t linked;
} loom_aie2p_configuration_worker_t;

typedef struct loom_aie2p_configuration_emitter_t {
  // Enclosing immutable compilation request.
  const loom_aie2p_xdna_artifact_request_t* request;
  // Device facts governing physical coordinates and DMA address encoding.
  const loom_xdna_array_family_t* family;
  // Product entry populated by the three explicit phases.
  loom_aie2p_xdna_entry_t* entry;
  // Physical actions and runtime relocations retained for native serialization.
  loom_aie2p_array_program_t* program;
  // Compiled complete workers indexed by source symbol ID.
  loom_aie2p_configuration_worker_t** workers;
  // Number of authored-input diagnostics; no product is published on failure.
  uint32_t error_count;
  // Entry-selected initialization function.
  loom_symbol_ref_t initialize;
  // Entry-selected invocation function.
  loom_symbol_ref_t invoke;
} loom_aie2p_configuration_emitter_t;

static iree_status_t loom_aie2p_configuration_schedule(
    loom_aie2p_configuration_emitter_t* emitter, const loom_op_t* function,
    loom_low_schedule_table_t* out_schedule) {
  const loom_aie2p_xdna_artifact_request_t* request = emitter->request;
  const loom_target_function_version_t* version =
      loom_target_function_version_list_find(
          request->function_versions,
          loom_func_like_const_cast(request->module, function));
  loom_low_function_model_t model = {0};
  iree_status_t status = loom_low_function_model_initialize(
      request->module, function,
      version ? version->function_target_facts : NULL,
      request->low_descriptor_registry, request->diagnostic_emitter, 0,
      request->scratch_arena, &model);
  emitter->error_count += model.error_count;
  if (iree_status_is_ok(status) && !emitter->error_count) {
    const loom_low_schedule_options_t options = {
        .emitter = request->diagnostic_emitter,
        .strategy = LOOM_LOW_SCHEDULE_STRATEGY_SOURCE_PRIORITY,
    };
    status = loom_low_schedule_function(&model, &options,
                                        request->scratch_arena, out_schedule);
  }
  loom_low_function_model_deinitialize(&model);
  emitter->error_count += out_schedule->error_count;
  return status;
}

static iree_status_t loom_aie2p_configuration_diagnose(
    loom_aie2p_configuration_emitter_t* emitter, const loom_op_t* op,
    const loom_error_def_t* error, const loom_diagnostic_param_t* params,
    iree_host_size_t param_count) {
  ++emitter->error_count;
  const loom_diagnostic_emission_t emission = {
      .op = op,
      .error = error,
      .params = params,
      .param_count = param_count,
  };
  return iree_diagnostic_emit(emitter->request->diagnostic_emitter, &emission);
}

static iree_status_t loom_aie2p_configuration_check_scalar(
    loom_aie2p_configuration_emitter_t* emitter, const loom_op_t* op,
    iree_string_view_t operand_name, uint64_t value, uint64_t minimum,
    uint64_t maximum) {
  if (value >= minimum && value <= maximum) {
    return iree_ok_status();
  }
  const loom_diagnostic_param_t params[] = {
      loom_param_string(operand_name),
      loom_param_u64(value),
      loom_param_u64(minimum),
      loom_param_u64(maximum),
  };
  return loom_aie2p_configuration_diagnose(emitter, op, LOOM_ERR_XDNA_041,
                                           params, IREE_ARRAYSIZE(params));
}

static iree_status_t loom_aie2p_configuration_check_alignment(
    loom_aie2p_configuration_emitter_t* emitter, const loom_op_t* op,
    iree_string_view_t operand_name, uint64_t value, uint64_t alignment) {
  if (value % alignment == 0) {
    return iree_ok_status();
  }
  const loom_diagnostic_param_t params[] = {
      loom_param_string(operand_name),
      loom_param_u64(value),
      loom_param_u64(alignment),
  };
  return loom_aie2p_configuration_diagnose(emitter, op, LOOM_ERR_XDNA_042,
                                           params, IREE_ARRAYSIZE(params));
}

static iree_status_t loom_aie2p_configuration_load(
    loom_aie2p_configuration_emitter_t* emitter, const loom_op_t* op,
    loom_symbol_ref_t symbol, uint64_t column, uint64_t row,
    loom_aie2p_xdna_tile_t* out_tile) {
  const loom_aie2p_xdna_artifact_request_t* request = emitter->request;
  const loom_xdna_tile_facts_t* tile = NULL;
  if (column < emitter->entry->column_count &&
      row < emitter->family->row_count) {
    tile = loom_xdna_array_tile_facts(
        emitter->family,
        (loom_xdna_tile_coordinate_t){(uint16_t)column, (uint16_t)row});
  }
  if (!tile || tile->kind != LOOM_XDNA_TILE_KIND_COMPUTE) {
    const loom_diagnostic_param_t params[] = {
        loom_param_u64(column),
        loom_param_u64(row),
        loom_param_u32(emitter->entry->column_count),
    };
    return loom_aie2p_configuration_diagnose(emitter, op, LOOM_ERR_XDNA_046,
                                             params, IREE_ARRAYSIZE(params));
  }
  loom_aie2p_configuration_worker_t* worker =
      emitter->workers[symbol.symbol_id];
  if (!worker) {
    IREE_RETURN_IF_ERROR(iree_arena_allocate(request->scratch_arena,
                                             sizeof(*worker), (void**)&worker));
    *worker = (loom_aie2p_configuration_worker_t){0};
    loom_op_t* function =
        request->module->symbols.entries[symbol.symbol_id].defining_op;
    const loom_target_function_version_t* version =
        loom_target_function_version_list_find(
            request->function_versions,
            loom_func_like_const_cast(request->module, function));
    loom_target_compile_report_t report;
    loom_target_compile_report_t* report_ptr = NULL;
    if (request->compile_report) {
      loom_target_compile_report_initialize(&report,
                                            request->compile_report->allocator);
      report.requested_detail_flags =
          request->compile_report->requested_detail_flags;
      report_ptr = &report;
    }
    const loom_aie2p_leaf_compile_options_t options = {
        .descriptor_registry = request->low_descriptor_registry,
        .function_target_facts =
            version ? version->function_target_facts : NULL,
        .diagnostic_emitter = request->diagnostic_emitter,
        .compile_report = report_ptr,
    };
    bool compiled = false;
    iree_status_t status = loom_aie2p_leaf_compile(
        request->module, function, &options, request->scratch_arena, &compiled,
        &worker->contribution);
    if (report_ptr) {
      status = iree_status_join(status,
                                loom_target_compile_report_record_entry_report(
                                    request->compile_report, report_ptr));
      loom_target_compile_report_deinitialize(report_ptr);
    }
    IREE_RETURN_IF_ERROR(status);
    if (!compiled) {
      ++emitter->error_count;
      return iree_ok_status();
    }
    const loom_aie2p_tile_link_layout_t layout = {
        .program_address = tile->memory.program_base,
        .program_byte_capacity = tile->memory.program_capacity,
    };
    const iree_string_view_t name = loom_string_table_get(
        &request->module->strings,
        request->module->symbols.entries[symbol.symbol_id].name_id);
    IREE_RETURN_IF_ERROR(
        loom_aie2p_tile_link(&worker->contribution, &layout,
                             request->scratch_arena, &worker->linked),
        "worker '@%.*s': code=%" PRIu64 " bytes, tile capacity=%u bytes",
        (int)name.size, name.data,
        worker->contribution.realization.code.byte_length,
        tile->memory.program_capacity);
    emitter->workers[symbol.symbol_id] = worker;
  }
  *out_tile = (loom_aie2p_xdna_tile_t){
      .coordinate = {(uint16_t)column, (uint16_t)row},
      .contribution = &worker->contribution,
      .linked_tile = &worker->linked,
  };
  return iree_ok_status();
}

// Physical writes stay within one tile aperture in the declared partition.
// Register meanings remain explicit in the authored configuration program.
static iree_status_t loom_aie2p_configuration_check_address(
    loom_aie2p_configuration_emitter_t* emitter, const loom_op_t* op,
    uint32_t address, iree_host_size_t word_count) {
  const loom_xdna_array_family_t* family = emitter->family;
  const uint64_t aperture = UINT64_C(1) << family->row_shift;
  const uint32_t row_mask =
      (UINT32_C(1) << (family->column_shift - family->row_shift)) - 1;
  IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_alignment(
      emitter, op, IREE_SV("address"), address, 4));
  if ((address >> family->column_shift) >= emitter->entry->column_count ||
      ((address >> family->row_shift) & row_mask) >= family->row_count ||
      word_count > (aperture - (address & (aperture - 1))) / 4) {
    const loom_diagnostic_param_t params[] = {
        loom_param_u32(address),
        loom_param_u64(word_count),
        loom_param_u32(emitter->entry->column_count),
    };
    return loom_aie2p_configuration_diagnose(emitter, op, LOOM_ERR_XDNA_045,
                                             params, IREE_ARRAYSIZE(params));
  }
  return iree_ok_status();
}

static iree_status_t loom_aie2p_configuration_phase_emit(
    loom_aie2p_configuration_emitter_t* emitter, const loom_op_t* function,
    loom_aie2p_configuration_phase_t phase) {
  const loom_aie2p_xdna_artifact_request_t* request = emitter->request;
  iree_arena_allocator_t* arena = request->scratch_arena;
  loom_low_schedule_table_t schedule = {0};
  IREE_RETURN_IF_ERROR(
      loom_aie2p_configuration_schedule(emitter, function, &schedule));
  if (emitter->error_count) {
    return iree_ok_status();
  }
  loom_aie2p_configuration_value_t* values = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, schedule.value_count, sizeof(*values), (void**)&values));
  loom_aie2p_program_record_t* records = NULL;
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      arena, schedule.node_count, sizeof(*records), (void**)&records));
  iree_host_size_t record_count = 0;
  iree_xdna_elf_binding_record_t* bindings = NULL;
  loom_aie2p_program_relocation_t* relocations = NULL;
  loom_aie2p_xdna_tile_t* tiles = NULL;
  if (phase == LOOM_AIE2P_CONFIGURATION_INITIALIZE) {
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
        arena, schedule.node_count, sizeof(*tiles), (void**)&tiles));
  } else if (phase == LOOM_AIE2P_CONFIGURATION_INVOKE) {
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
        arena, schedule.node_count, sizeof(*bindings), (void**)&bindings));
    IREE_RETURN_IF_ERROR(iree_arena_allocate_array(arena, schedule.node_count,
                                                   sizeof(*relocations),
                                                   (void**)&relocations));
  }
  // The device profile owns the shim address contract. Every address-bearing
  // command below uses that same contract for its typed relocation.
  const loom_xdna_tile_facts_t* shim_tile = loom_xdna_array_tile_facts(
      emitter->family, (loom_xdna_tile_coordinate_t){0, 0});
  const loom_xdna_dma_facts_t* shim = &shim_tile->dma;
  iree_status_t status = iree_ok_status();
  for (iree_host_size_t i = 0;
       i < schedule.node_count && iree_status_is_ok(status) &&
       !emitter->error_count;
       ++i) {
    const loom_low_schedule_node_t* node =
        &schedule.nodes[schedule.scheduled_node_indices[i]];
    const loom_op_t* op = node->op;
    const loom_value_ordinal_t* operands =
        loom_low_schedule_node_const_operand_ordinals(node);
    const loom_value_ordinal_t* results =
        loom_low_schedule_node_const_result_ordinals(node);
    if (loom_low_return_isa(op)) {
      continue;
    }
    if (loom_low_assume_isa(op)) {
      for (uint16_t j = 0; j < node->result_count; ++j) {
        values[results[j]] = values[operands[j]];
      }
      continue;
    }
    const loom_named_attr_slice_t attrs = loom_low_const_isa(op)
                                              ? loom_low_const_attrs(op)
                                              : loom_low_op_attrs(op);
#define SCALAR(index) (values[operands[index]].scalar)
#define WORD(index) ((uint32_t)SCALAR(index))
    const uint32_t opcode = node->source_descriptor_ordinal;
    // Non-range commands consume physical register words and coordinates.
    // Constants, binding extents and range arithmetic retain their 64-bit
    // width.
    if (opcode != AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_CONSTANT &&
        opcode != AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_BINDING &&
        opcode != AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_RANGE) {
      for (uint16_t j = 0; j < node->operand_count; ++j) {
        if (j == 1 &&
            (opcode ==
                 AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_ADDRESS ||
             opcode ==
                 AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_SHIM_DESCRIPTOR)) {
          continue;
        }
        const loom_low_operand_t* operand =
            &schedule.target.descriptor_set
                 ->operands[node->descriptor->operand_start +
                            iree_min(j, node->descriptor->operand_count - 1)];
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op,
            loom_low_descriptor_set_string(schedule.target.descriptor_set,
                                           operand->field_name_string_ref),
            SCALAR(j), 0, UINT32_MAX));
      }
    }
    if (emitter->error_count) {
      break;
    }
    loom_aie2p_program_record_t record = {0};
    switch (opcode) {
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_CONSTANT:
        values[results[0]].scalar =
            loom_aie2p_configuration_configuration_constant_value(attrs).i64;
        continue;
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_BINDING: {
        if (SCALAR(0) != emitter->entry->binding_count) {
          const loom_diagnostic_param_t params[] = {
              loom_param_u64(SCALAR(0)),
              loom_param_u64(emitter->entry->binding_count),
          };
          status = loom_aie2p_configuration_diagnose(
              emitter, op, LOOM_ERR_XDNA_043, params, IREE_ARRAYSIZE(params));
          continue;
        }
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("access"), SCALAR(1), 1, 3));
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("length"), SCALAR(2), 1, INT64_MAX));
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("alignment"), SCALAR(3), 1, UINT32_MAX));
        if (SCALAR(3) & (SCALAR(3) - 1)) {
          const loom_diagnostic_param_t params[] = {loom_param_u64(SCALAR(3))};
          status = loom_aie2p_configuration_diagnose(
              emitter, op, LOOM_ERR_XDNA_048, params, IREE_ARRAYSIZE(params));
        }
        if (emitter->error_count) {
          continue;
        }
        values[results[0]].binding = WORD(0);
        bindings[emitter->entry->binding_count++] =
            (iree_xdna_elf_binding_record_t){
                .kind = IREE_XDNA_ELF_BINDING_KIND_BUFFER,
                .address_space = IREE_XDNA_ELF_BINDING_ADDRESS_SPACE_GLOBAL,
                .access = WORD(1),
                .usage = IREE_XDNA_ELF_BINDING_USAGE_DEVICE_VISIBLE |
                         IREE_XDNA_ELF_BINDING_USAGE_COHERENT,
                .minimum_byte_length = SCALAR(2),
                .minimum_alignment = WORD(3),
                .maximum_byte_offset = UINT64_MAX,
            };
        continue;
      }
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_RANGE: {
        const uint32_t binding = values[operands[0]].binding;
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("length"), SCALAR(2), 1, INT64_MAX));
        if (SCALAR(1) > bindings[binding].minimum_byte_length ||
            SCALAR(2) > bindings[binding].minimum_byte_length - SCALAR(1)) {
          const loom_diagnostic_param_t params[] = {
              loom_param_u32(binding),
              loom_param_u64(bindings[binding].minimum_byte_length),
              loom_param_u64(SCALAR(1)),
              loom_param_u64(SCALAR(2)),
          };
          status = loom_aie2p_configuration_diagnose(
              emitter, op, LOOM_ERR_XDNA_044, params, IREE_ARRAYSIZE(params));
          continue;
        }
        values[results[0]].range.binding = binding;
        values[results[0]].range.offset = SCALAR(1);
        values[results[0]].range.length = SCALAR(2);
        continue;
      }
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_ENTRY: {
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("columns"), SCALAR(0), 1,
            emitter->family->column_count));
        if (emitter->error_count) {
          continue;
        }
        emitter->entry->column_count = (uint16_t)SCALAR(0);
        emitter->initialize =
            loom_aie2p_configuration_configuration_entry_initialize(attrs)
                .symbol;
        emitter->invoke =
            loom_aie2p_configuration_configuration_entry_invoke(attrs).symbol;
        continue;
      }
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE32:
        record.type = LOOM_AIE2P_PROGRAM_RECORD_REGISTER_WRITE32;
        record.value.register_write32 =
            (loom_aie2p_program_register_write32_t){WORD(0), WORD(1)};
        break;
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_MASK32:
        record.type = LOOM_AIE2P_PROGRAM_RECORD_REGISTER_MASK_WRITE32;
        record.value.register_mask_write32 =
            (loom_aie2p_program_register_mask_write32_t){WORD(0), WORD(1),
                                                         WORD(2)};
        break;
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_BLOCK32: {
        uint32_t* words = NULL;
        const iree_host_size_t count = node->operand_count - 1;
        if (!count) {
          continue;
        }
        IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
            arena, count, sizeof(*words), (void**)&words));
        for (iree_host_size_t j = 0; j < count; ++j) {
          words[j] = WORD(j + 1);
        }
        record.type = LOOM_AIE2P_PROGRAM_RECORD_REGISTER_BLOCK_WRITE32;
        record.value.register_block_write32 =
            (loom_aie2p_program_register_block_write32_t){WORD(0), words,
                                                          count};
        break;
      }
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_WRITE_ADDRESS:
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_SHIM_DESCRIPTOR: {
        const loom_aie2p_configuration_value_t span = values[operands[1]];
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("range length"), span.range.length, 1,
            shim->address_maximum));
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_alignment(
            emitter, op, IREE_SV("range offset"), span.range.offset,
            shim->address_alignment));
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("binding alignment"),
            bindings[span.range.binding].minimum_alignment,
            shim->address_alignment, UINT32_MAX));
        if (emitter->error_count) {
          continue;
        }
        const bool descriptor =
            opcode ==
            AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_SHIM_DESCRIPTOR;
        const iree_host_size_t count = descriptor ? 8 : 2;
        uint32_t* words = NULL;
        IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
            arena, count, sizeof(*words), (void**)&words));
        memset(words, 0, count * sizeof(*words));
        if (descriptor) {
          words[0] = WORD(2);
          for (iree_host_size_t j = 2; j < 8; ++j) {
            words[j] = WORD(j + 1);
          }
          if (words[2] & 0xffff) {
            const loom_diagnostic_param_t params[] = {loom_param_u32(words[2])};
            status = loom_aie2p_configuration_diagnose(
                emitter, op, LOOM_ERR_XDNA_047, params, IREE_ARRAYSIZE(params));
            continue;
          }
        }
        record.type = LOOM_AIE2P_PROGRAM_RECORD_REGISTER_BLOCK_WRITE32;
        record.value.register_block_write32 =
            (loom_aie2p_program_register_block_write32_t){WORD(0), words,
                                                          count};
        relocations[emitter->program->relocation_count++] =
            (loom_aie2p_program_relocation_t){
                .target_record_index = (uint32_t)record_count,
                .target_word_index = descriptor ? 1 : 0,
                .binding_ordinal = span.range.binding,
                .addend = (int64_t)span.range.offset,
                .maximum_value = shim->address_maximum - span.range.length,
                .required_alignment = shim->address_alignment,
            };
        break;
      }
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_PROGRAM_LOAD: {
        const loom_symbol_ref_t symbol =
            loom_aie2p_configuration_configuration_program_load_program(attrs)
                .symbol;
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_load(
            emitter, op, symbol, SCALAR(0), SCALAR(1),
            &tiles[emitter->entry->tile_count]));
        if (emitter->error_count) {
          continue;
        }
        record.type = LOOM_AIE2P_PROGRAM_RECORD_TILE_PROGRAM_LOAD;
        record.value.tile_program_load.tile_program_index =
            (uint32_t)emitter->entry->tile_count++;
        break;
      }
      case AIE2P_CONFIGURATION_DESCRIPTOR_REF_CONFIGURATION_DMA_WAIT: {
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("column"), SCALAR(0), 0,
            emitter->entry->column_count - 1));
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("row"), SCALAR(1), 0,
            emitter->family->row_count - 1));
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("direction"), SCALAR(2),
            LOOM_XDNA_DMA_DIRECTION_MEMORY_TO_STREAM,
            LOOM_XDNA_DMA_DIRECTION_STREAM_TO_MEMORY));
        if (emitter->error_count) {
          continue;
        }
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("columns"), SCALAR(4), 1,
            emitter->entry->column_count - SCALAR(0)));
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
            emitter, op, IREE_SV("rows"), SCALAR(5), 1,
            emitter->family->row_count - SCALAR(1)));
        if (emitter->error_count) {
          continue;
        }
        const loom_xdna_tile_coordinate_t coordinate = {(uint16_t)SCALAR(0),
                                                        (uint16_t)SCALAR(1)};
        for (uint8_t j = 0; j < emitter->family->tile_count; ++j) {
          const loom_xdna_tile_facts_t* covered = &emitter->family->tiles[j];
          if (covered->first_row < SCALAR(1) + SCALAR(5) &&
              covered->first_row + covered->row_count > SCALAR(1) &&
              SCALAR(3) >= covered->dma.channel_count_per_direction) {
            IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_scalar(
                emitter, op, IREE_SV("channel"), SCALAR(3), 0,
                covered->dma.channel_count_per_direction - 1));
          }
        }
        record.type = LOOM_AIE2P_PROGRAM_RECORD_DMA_TASK_WAIT;
        record.value.dma_task_wait = (loom_aie2p_program_dma_task_wait_t){
            coordinate, (loom_xdna_dma_direction_t)SCALAR(2),
            (uint8_t)SCALAR(3), (uint8_t)SCALAR(4), (uint8_t)SCALAR(5)};
        break;
      }
      default:
        IREE_ASSERT_UNREACHABLE("configuration descriptor coverage");
    }
#undef WORD
#undef SCALAR
    switch (record.type) {
      case LOOM_AIE2P_PROGRAM_RECORD_REGISTER_WRITE32: {
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_address(
            emitter, op, record.value.register_write32.address, 1));
        break;
      }
      case LOOM_AIE2P_PROGRAM_RECORD_REGISTER_MASK_WRITE32: {
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_address(
            emitter, op, record.value.register_mask_write32.address, 1));
        break;
      }
      case LOOM_AIE2P_PROGRAM_RECORD_REGISTER_BLOCK_WRITE32: {
        IREE_RETURN_IF_ERROR(loom_aie2p_configuration_check_address(
            emitter, op, record.value.register_block_write32.address,
            record.value.register_block_write32.word_count));
        break;
      }
      default:
        break;
    }
    if (!emitter->error_count) {
      records[record_count++] = record;
    }
  }
  if (!iree_status_is_ok(status) || emitter->error_count) {
    return status;
  }
  if (phase == LOOM_AIE2P_CONFIGURATION_INITIALIZE) {
    emitter->program->array_records = records;
    emitter->program->array_record_count = record_count;
    emitter->entry->tiles = tiles;
  } else if (phase == LOOM_AIE2P_CONFIGURATION_INVOKE) {
    emitter->program->control_records = records;
    emitter->program->control_record_count = record_count;
    emitter->program->relocations = relocations;
    emitter->entry->bindings = bindings;
  }
  return status;
}

iree_status_t loom_aie2p_configuration_emit(
    const loom_aie2p_xdna_artifact_request_t* request,
    const loom_op_t* entry_op, const loom_xdna_device_profile_t* device_profile,
    loom_aie2p_xdna_entry_t* out_entry, bool* out_valid) {
  *out_valid = false;
  *out_entry = (loom_aie2p_xdna_entry_t){0};
  const loom_func_like_t function =
      loom_func_like_const_cast(request->module, entry_op);
  loom_aie2p_xdna_entry_t entry = {0};
  entry.name = loom_string_table_get(
      &request->module->strings,
      request->module->symbols
          .entries[loom_func_like_callee(function).symbol_id]
          .name_id);
  loom_aie2p_configuration_emitter_t emitter = {
      .request = request,
      .family = loom_xdna_device_profile_array_family(device_profile),
      .entry = &entry,
      .initialize = loom_symbol_ref_null(),
      .invoke = loom_symbol_ref_null(),
  };
  IREE_RETURN_IF_ERROR(iree_arena_allocate(request->scratch_arena,
                                           sizeof(*emitter.program),
                                           (void**)&emitter.program));
  *emitter.program = (loom_aie2p_array_program_t){0};
  IREE_RETURN_IF_ERROR(iree_arena_allocate_array(
      request->scratch_arena, request->module->symbols.count,
      sizeof(*emitter.workers), (void**)&emitter.workers));
  memset(emitter.workers, 0,
         request->module->symbols.count * sizeof(*emitter.workers));
  IREE_RETURN_IF_ERROR(loom_aie2p_configuration_phase_emit(
      &emitter, entry_op, LOOM_AIE2P_CONFIGURATION_ENTRY));
  if (emitter.error_count) {
    return iree_ok_status();
  }
  IREE_RETURN_IF_ERROR(loom_aie2p_configuration_phase_emit(
      &emitter,
      request->module->symbols.entries[emitter.initialize.symbol_id]
          .defining_op,
      LOOM_AIE2P_CONFIGURATION_INITIALIZE));
  if (emitter.error_count) {
    return iree_ok_status();
  }
  IREE_RETURN_IF_ERROR(loom_aie2p_configuration_phase_emit(
      &emitter,
      request->module->symbols.entries[emitter.invoke.symbol_id].defining_op,
      LOOM_AIE2P_CONFIGURATION_INVOKE));
  if (emitter.error_count) {
    return iree_ok_status();
  }
  entry.array_program = emitter.program;
  *out_entry = entry;
  *out_valid = true;
  return iree_ok_status();
}
