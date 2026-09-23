// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/transforms/vector/combine_patterns.h"

#include "loom/ops/vector/construction.h"
#include "loom/ops/vector/ops.h"
#include "loom/ops/vector/table.h"

static iree_status_t loom_vector_from_elements_combine_lanes_pattern(
    const loom_rewrite_pattern_t* pattern, void* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_changed) {
  (void)pattern;
  (void)context;
  return loom_vector_from_elements_combine_lanes(op, rewriter, out_changed);
}

static iree_status_t loom_vector_from_elements_to_table_lookup_pattern(
    const loom_rewrite_pattern_t* pattern, void* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_changed) {
  (void)pattern;
  (void)context;
  return loom_vector_from_elements_to_table_lookup(op, rewriter, out_changed);
}

static iree_status_t loom_vector_table_lookup_simplify_indices_pattern(
    const loom_rewrite_pattern_t* pattern, void* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_changed) {
  (void)pattern;
  (void)context;
  return loom_vector_table_lookup_simplify_indices(op, rewriter, out_changed);
}

static const loom_rewrite_pattern_t kVectorSourceCombinePatterns[] = {
    {
        .root_kind = LOOM_OP_VECTOR_FROM_ELEMENTS,
        .match_and_rewrite = loom_vector_from_elements_combine_lanes_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_FROM_ELEMENTS,
        .match_and_rewrite = loom_vector_from_elements_to_table_lookup_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_TABLE_LOOKUP,
        .match_and_rewrite = loom_vector_table_lookup_simplify_indices_pattern,
    },
};

const loom_rewrite_pattern_provider_t
    loom_vector_source_combine_pattern_provider = {
        .name = IREE_SVL("vector"),
        .patterns = kVectorSourceCombinePatterns,
        .pattern_count = IREE_ARRAYSIZE(kVectorSourceCombinePatterns),
};
