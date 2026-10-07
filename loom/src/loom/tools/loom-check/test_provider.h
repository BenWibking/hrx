// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Synthetic loom-check provider used by checked-in .loom-test files.

#ifndef LOOM_TOOLS_LOOM_CHECK_TEST_PROVIDER_H_
#define LOOM_TOOLS_LOOM_CHECK_TEST_PROVIDER_H_

#include "loom/tools/loom-check/provider.h"
#include "loomc/target.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const loom_check_provider_t loom_check_test_provider;

// Creates the configured compiler environment with the synthetic target
// provider used by loom-check-test.
loomc_status_t loom_check_test_create_target_environment(
    loomc_allocator_t allocator,
    loomc_target_environment_t** out_target_environment);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLS_LOOM_CHECK_TEST_PROVIDER_H_
