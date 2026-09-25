// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_TRANSFORMS_VECTOR_TABLE_LEGALIZATION_H_
#define LOOM_TRANSFORMS_VECTOR_TABLE_LEGALIZATION_H_

#include "loom/target/legalization.h"

#ifdef __cplusplus
extern "C" {
#endif

// Native comparison and selection carriers used for ordered quantization.
typedef struct loom_vector_table_quantize_policy_t {
  // Maximum packet payload in bits for comparisons and ordinal conversion.
  uint16_t packet_bit_count;
  // Input element type or an exact floating-point extension supported natively.
  loom_scalar_type_t comparison_element_type;
  // Native integer type used while selecting ordinal codes.
  loom_scalar_type_t ordinal_element_type;
} loom_vector_table_quantize_policy_t;

// Expands a verified vector.table.quantize into lane-parallel comparisons and
// ordinal selections. Static rank-one input and threshold packets slice the
// original SSA values; their memory observations are preserved. Each input lane
// counts the ordered thresholds it passes. Ordinals are converted to the
// declared unsigned result width, and result packets concatenate in lane order.
// Predicate results directly use the comparison's mask. Returns false through
// |out_rewritten| for dynamic shapes, higher-rank inputs, or empty input
// vectors.
iree_status_t loom_vector_table_quantize_rewrite(
    loom_target_legalization_context_t* context, loom_op_t* op,
    const loom_vector_table_quantize_policy_t* policy, bool* out_rewritten);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TRANSFORMS_VECTOR_TABLE_LEGALIZATION_H_
