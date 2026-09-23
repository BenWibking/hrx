// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Cleanup rewrite pattern composition.
//
// Cleanup has three ordering-sensitive pattern phases. Universal pre-fold
// patterns run before constant folding, universal post-type patterns run after
// type propagation, and source-combine patterns run after structural
// canonicalization only while source representations remain legal.

#ifndef LOOM_TRANSFORMS_CLEANUP_PATTERNS_H_
#define LOOM_TRANSFORMS_CLEANUP_PATTERNS_H_

#include "iree/base/api.h"
#include "loom/rewrite/pattern_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_symbolic_expr_context_t loom_symbolic_expr_context_t;

// Invocation-local state available to cleanup rewrite patterns.
typedef struct loom_cleanup_pattern_context_t {
  // Symbolic expression context maintained by the canonicalizer driver.
  loom_symbolic_expr_context_t* symbolic_expression_context;
} loom_cleanup_pattern_context_t;

// Ordered provider lists selected by one compiler composition.
typedef struct loom_cleanup_pattern_provider_set_t {
  // Universal patterns applied before fact-backed constant folding.
  loom_rewrite_pattern_provider_list_t universal_pre_fold;
  // Universal patterns applied after type propagation.
  loom_rewrite_pattern_provider_list_t universal_post_type;
  // Source representation combines disabled after target legalization.
  loom_rewrite_pattern_provider_list_t source_combine;
} loom_cleanup_pattern_provider_set_t;

// Indexed pattern registries prepared for one compiler invocation.
typedef struct loom_cleanup_pattern_registry_t {
  // Universal patterns applied before fact-backed constant folding.
  const loom_rewrite_pattern_registry_t* universal_pre_fold;
  // Universal patterns applied after type propagation.
  const loom_rewrite_pattern_registry_t* universal_post_type;
  // Source representation combines disabled after target legalization.
  const loom_rewrite_pattern_registry_t* source_combine;
} loom_cleanup_pattern_registry_t;

// Owned storage for the three indexed cleanup pattern registries. Provider
// descriptors and callback data remain borrowed.
typedef struct loom_cleanup_pattern_registry_storage_t {
  // Storage for the universal pre-fold registry.
  loom_rewrite_pattern_registry_storage_t universal_pre_fold_storage;
  // Storage for the universal post-type registry.
  loom_rewrite_pattern_registry_storage_t universal_post_type_storage;
  // Storage for the source-combine registry.
  loom_rewrite_pattern_registry_storage_t source_combine_storage;
  // Borrowed registry view over the three storage fields.
  loom_cleanup_pattern_registry_t registry;
} loom_cleanup_pattern_registry_storage_t;

// Composes each cleanup phase into an indexed registry while preserving the
// provider and pattern order selected by |provider_set|.
iree_status_t loom_cleanup_pattern_registry_storage_initialize(
    const loom_cleanup_pattern_provider_set_t* provider_set,
    iree_allocator_t allocator,
    loom_cleanup_pattern_registry_storage_t* out_storage);

// Releases registry storage. Borrowed provider data is never released.
void loom_cleanup_pattern_registry_storage_deinitialize(
    loom_cleanup_pattern_registry_storage_t* storage);

// Returns the immutable registry view owned by |storage|.
const loom_cleanup_pattern_registry_t*
loom_cleanup_pattern_registry_storage_registry(
    const loom_cleanup_pattern_registry_storage_t* storage);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TRANSFORMS_CLEANUP_PATTERNS_H_
