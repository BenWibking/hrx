// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Selected rule programs retained independently of value-analysis scratch.
//
// Rule selection resolves descriptor rows, attributes, source origins, and
// result carriers while source facts are available. Emission consumes these
// concrete choices directly.

#ifndef LOOM_CODEGEN_LOW_LOWER_RULE_PLAN_H_
#define LOOM_CODEGEN_LOW_LOWER_RULE_PLAN_H_

#include "iree/base/internal/math.h"
#include "loom/codegen/low/lower/rules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_low_lower_selected_plan_t loom_low_lower_selected_plan_t;

typedef struct loom_low_lower_resolved_emit_t {
  // Static emit-program row selected by planning.
  const loom_low_lower_emit_t* emit;
  // Descriptor row referenced by emit and resolved during planning.
  loom_low_lower_resolved_descriptor_t descriptor;
  // Source access semantics for this memory packet; zero for address setup.
  loom_memory_access_flags_t access_flags;
  // Result carriers resolved from source values, type patterns, or descriptors.
  // Unset results derive their type from an earlier rule-local temporary or
  // from an explicit register-slice width.
  uint8_t result_type_mask;
  // Fact-derived source references retained after attributes and result types.
  // Bits address operand refs; set bits have packed value IDs.
  uint16_t source_value_mask;
  // Byte offset from this row to attributes, canonical result type IDs, and
  // fact-derived source values in that order. Zero for an empty payload. All
  // rows and their aligned payloads share one function-arena allocation. Elided
  // recipes retain descriptor identity without executable payloads.
  uint32_t data_offset;
} loom_low_lower_resolved_emit_t;

// Returns the attributes selected for this emit row, in generated table order.
static inline loom_named_attr_slice_t loom_low_lower_resolved_emit_attributes(
    const loom_low_lower_resolved_emit_t* resolved) {
  return loom_make_named_attr_slice(
      resolved->data_offset
          ? (const loom_named_attr_t*)((const uint8_t*)resolved +
                                       resolved->data_offset)
          : NULL,
      resolved->emit->attr_copy_count);
}

// Returns the canonical carrier ID for a result fixed by planning.
static inline loom_type_id_t loom_low_lower_resolved_emit_result_type_id(
    const loom_low_lower_resolved_emit_t* resolved, uint16_t result_ordinal) {
  const loom_named_attr_t* attributes =
      (const loom_named_attr_t*)((const uint8_t*)resolved +
                                 resolved->data_offset);
  const loom_type_id_t* result_types =
      (const loom_type_id_t*)(attributes + resolved->emit->attr_copy_count);
  const uint32_t preceding_mask = (UINT32_C(1) << result_ordinal) - 1u;
  return result_types[iree_math_count_ones_u32(resolved->result_type_mask &
                                               preceding_mask)];
}

// Returns the retained source for a fact-derived operand reference.
// The selected recipe establishes that the corresponding mask bit is set.
static inline loom_value_id_t loom_low_lower_resolved_emit_source_value(
    const loom_low_lower_resolved_emit_t* resolved,
    uint16_t reference_ordinal) {
  const loom_named_attr_t* attributes =
      (const loom_named_attr_t*)((const uint8_t*)resolved +
                                 resolved->data_offset);
  const loom_type_id_t* result_types =
      (const loom_type_id_t*)(attributes + resolved->emit->attr_copy_count);
  const loom_value_id_t* source_values =
      (const loom_value_id_t*)(result_types + iree_math_count_ones_u32(
                                                  resolved->result_type_mask));
  const uint32_t preceding_mask = (UINT32_C(1) << reference_ordinal) - 1u;
  return source_values[iree_math_count_ones_u32(resolved->source_value_mask &
                                                preceding_mask)];
}

// Finalizes a selected rule's descriptors, source access semantics, attributes,
// result carriers, and fact-derived operands. Source graphs may be borrowed
// during this call; payloads reference only module storage or the retained
// program allocation. Ordinary source operands remain direct IR field reads.
// Returned rows belong to the function arena and outlive construction scratch.
iree_status_t loom_low_lower_rule_plan_finalize(
    loom_low_lower_context_t* context,
    loom_low_lower_selected_plan_t* selected_plan);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_CODEGEN_LOW_LOWER_RULE_PLAN_H_
