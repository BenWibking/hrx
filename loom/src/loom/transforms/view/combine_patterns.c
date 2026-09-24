// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/transforms/view/combine_patterns.h"

#include "loom/ops/view/ops.h"
#include "loom/transforms/cleanup/patterns.h"
#include "loom/transforms/view/load_coalescing.h"

static iree_status_t loom_view_load_coalescing_pattern(
    const loom_rewrite_pattern_t* pattern, void* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_changed) {
  (void)pattern;
  loom_cleanup_pattern_context_t* cleanup_context =
      (loom_cleanup_pattern_context_t*)context;
  return loom_view_load_coalescing_rewrite(
      rewriter, cleanup_context->symbolic_expression_context, op, out_changed);
}

static const loom_rewrite_pattern_t kViewSourceCombinePatterns[] = {
    {
        .root_kind = LOOM_OP_VIEW_LOAD,
        .match_and_rewrite = loom_view_load_coalescing_pattern,
    },
};

const loom_rewrite_pattern_provider_t
    loom_view_source_combine_pattern_provider = {
        .name = IREE_SVL("view"),
        .patterns = kViewSourceCombinePatterns,
        .pattern_count = IREE_ARRAYSIZE(kViewSourceCombinePatterns),
};
