// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/transforms/cleanup/configured.h"

#include "loom/transforms/scf/combine_patterns.h"
#include "loom/transforms/vector/combine_patterns.h"
#include "loom/transforms/view/combine_patterns.h"

static const loom_rewrite_pattern_provider_t* const
    kConfiguredSourceCombinePatternProviders[] = {
        &loom_scf_source_combine_pattern_provider,
        &loom_view_source_combine_pattern_provider,
        &loom_vector_source_combine_pattern_provider,
};

static const loom_cleanup_pattern_provider_set_t kConfiguredPatternProviders = {
    .source_combine =
        {
            .count = IREE_ARRAYSIZE(kConfiguredSourceCombinePatternProviders),
            .values = kConfiguredSourceCombinePatternProviders,
        },
};

const loom_cleanup_pattern_provider_set_t*
loom_cleanup_configured_pattern_provider_set(void) {
  return &kConfiguredPatternProviders;
}
