// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Selected rule programs retained independently of value-analysis scratch.
//
// Rule selection resolves descriptor rows and attribute recipes while source
// facts are available. Emission consumes these concrete choices directly.

#ifndef LOOM_CODEGEN_LOW_LOWER_RULE_PLAN_H_
#define LOOM_CODEGEN_LOW_LOWER_RULE_PLAN_H_

#include "loom/codegen/low/lower/rules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_low_lower_resolved_emit_t {
  // Static emit-program row selected by planning.
  const loom_low_lower_emit_t* emit;
  // Descriptor row referenced by emit and resolved during planning.
  loom_low_lower_resolved_descriptor_t descriptor;
  // Source access semantics for this memory packet; zero for address setup.
  loom_memory_access_flags_t access_flags;
  // Byte offset from this row to its retained attributes. Zero for no
  // attributes. Rows and their attribute payloads share one function-arena
  // allocation.
  uint32_t attributes_offset;
} loom_low_lower_resolved_emit_t;

// Returns the attributes selected for this emit row, in generated table order.
static inline loom_named_attr_slice_t loom_low_lower_resolved_emit_attributes(
    const loom_low_lower_resolved_emit_t* resolved) {
  return loom_make_named_attr_slice(
      resolved->attributes_offset
          ? (const loom_named_attr_t*)((const uint8_t*)resolved +
                                       resolved->attributes_offset)
          : NULL,
      resolved->emit->attr_copy_count);
}

// Resolves a selected rule's descriptors, source access semantics, and complete
// attribute recipes. Source graphs may be borrowed during this call; attribute
// payloads reference only module storage or the retained program allocation.
// Returned rows belong to the function arena and outlive construction scratch.
iree_status_t loom_low_lower_rule_set_resolve_emit_program(
    loom_low_lower_context_t* context, uint16_t rule_set_index,
    const loom_low_lower_rule_set_t* rule_set,
    const loom_low_lower_rule_t* rule, const loom_op_t* source_op,
    const loom_op_t* const* source_nodes, uint8_t source_node_count,
    const loom_low_source_memory_access_plan_t* source_memory_access,
    const loom_low_lower_resolved_emit_t** out_resolved_emits);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_CODEGEN_LOW_LOWER_RULE_PLAN_H_
