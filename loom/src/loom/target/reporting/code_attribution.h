// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Per-instruction code attribution rows.
//
// Each row describes one emitted native instruction: its byte range within the
// function's symbol, the scheduled packet whose expansion produced it, and the
// source location of that packet's operation. Inserted target instructions such
// as completion waits also name why they exist. Profilers join sampled program
// counters against these rows to attribute time to source, blocks, and waits.

#ifndef LOOM_TARGET_REPORTING_CODE_ATTRIBUTION_H_
#define LOOM_TARGET_REPORTING_CODE_ATTRIBUTION_H_

#include "iree/base/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_target_compile_report_code_attribution_row_t {
  // Target artifact function symbol containing this instruction.
  iree_string_view_t function_name;
  // Byte offset of the instruction from the start of the function symbol.
  uint64_t symbol_offset;
  // Encoded instruction size in bytes.
  uint32_t byte_size;
  // Why the instruction exists: "packet" for the expansion of a scheduled
  // packet, otherwise the inserted-instruction kind (such as "wait").
  iree_string_view_t instruction_kind;
  // Region block label containing the owning scheduled packet.
  iree_string_view_t block_name;
  // Region block ordinal containing the owning scheduled packet.
  uint32_t block_index;
  // Schedule node whose native expansion contains this instruction.
  uint32_t node_index;
  // Scheduled ordinal of |node_index| within |block_index|.
  uint32_t scheduled_ordinal;
  // Operation mnemonic of the owning scheduled node.
  iree_string_view_t operation_name;
  // Descriptor key of the owning node, or empty for structural nodes.
  iree_string_view_t descriptor_key;
  // Source filename of the owning operation, or empty when unknown.
  iree_string_view_t source_filename;
  // One-based source line, or zero when unknown.
  uint32_t source_line;
  // One-based source column, or zero when unknown.
  uint32_t source_column;
  // Target-specific reason for an inserted wait or delay, or empty.
  iree_string_view_t wait_reason;
  // Number of planned wait actions coalesced into this instruction.
  uint32_t wait_action_count;
  // Descriptor key (or structural operation mnemonic) of the first waited-on
  // producer, or empty.
  iree_string_view_t wait_producer_name;
  // Scratch access made by this instruction, or for an inserted wait, by the
  // instruction it waits on: "load", "store", or empty.
  iree_string_view_t scratch_access;
  // Name of the accessed storage reservation, or empty when unresolved.
  iree_string_view_t scratch_storage_name;
  // Where the accessed data comes from: the spilled value's definition for
  // spill slots, otherwise the storage reservation's declaration. Empty
  // filename and zero line when unknown.
  iree_string_view_t scratch_source_filename;
  // One-based line of the data source, or zero when unknown.
  uint32_t scratch_source_line;
  // One-based column of the data source, or zero when unknown.
  uint32_t scratch_source_column;
  // Name of the spilled SSA value when the storage is a compiler spill slot.
  iree_string_view_t spill_value_name;
  // Spill slot ordinal when the storage is a compiler spill slot, otherwise
  // UINT32_MAX.
  uint32_t spill_slot_index;
} loom_target_compile_report_code_attribution_row_t;

struct loom_target_compile_report_t;

// Appends one row when the report requested code attribution details.
iree_status_t loom_target_compile_report_record_code_attribution_row(
    struct loom_target_compile_report_t* report,
    const loom_target_compile_report_code_attribution_row_t* row);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_REPORTING_CODE_ATTRIBUTION_H_
