// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Cleanup pass environment capability.

#ifndef LOOM_TRANSFORMS_CLEANUP_PASS_ENVIRONMENT_H_
#define LOOM_TRANSFORMS_CLEANUP_PASS_ENVIRONMENT_H_

#include "loom/pass/environment.h"
#include "loom/pass/types.h"
#include "loom/transforms/cleanup/patterns.h"

#ifdef __cplusplus
extern "C" {
#endif

// Capability type for loom_cleanup_pass_capability_t.
extern const loom_pass_environment_capability_type_t
    loom_cleanup_pass_capability_type;

typedef struct loom_cleanup_pass_capability_t {
  // Base capability header. Must remain the first field.
  loom_pass_environment_capability_t base;
  // Cleanup pattern registries prepared for this compiler invocation.
  const loom_cleanup_pattern_registry_t* pattern_registry;
} loom_cleanup_pass_capability_t;

// Creates a borrowed cleanup pass capability.
loom_cleanup_pass_capability_t loom_cleanup_pass_capability_make(
    const loom_cleanup_pattern_registry_t* pattern_registry);

// Looks up the cleanup capability from |environment|. Returns NULL when absent.
const loom_cleanup_pass_capability_t*
loom_cleanup_pass_capability_from_environment(
    const loom_pass_environment_t* environment);

// Looks up the cleanup capability from |pass->environment|. Returns NULL when
// absent.
const loom_cleanup_pass_capability_t* loom_cleanup_pass_capability_from_pass(
    const loom_pass_t* pass);

// Returns the pattern registries selected by |capability|, or NULL.
const loom_cleanup_pattern_registry_t*
loom_cleanup_pass_capability_pattern_registry(
    const loom_cleanup_pass_capability_t* capability);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TRANSFORMS_CLEANUP_PASS_ENVIRONMENT_H_
