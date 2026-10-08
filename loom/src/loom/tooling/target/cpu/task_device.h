// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Native CPU target projection for task HAL execution.

#ifndef LOOM_TOOLING_TARGET_CPU_TASK_DEVICE_H_
#define LOOM_TOOLING_TARGET_CPU_TASK_DEVICE_H_

#include "loom/tooling/execution/hal/device_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_task_device_provider_t {
  // Device adapter registered alongside other HAL drivers.
  loom_device_provider_t base;
  // Borrowed core capabilities; must outlive this adapter and its targets.
  const loom_target_environment_t* target_environment;
} loom_task_device_provider_t;

// Initializes a task adapter using |target_environment|'s native CPU profiles
// and canonical kernel emitters. Selection reads the active task device's
// published CPU facts and queue affinity; it never probes the compiler host.
// The adapter and selected targets own no storage and require no teardown.
void loom_task_device_provider_initialize(
    const loom_target_environment_t* target_environment,
    loom_task_device_provider_t* out_provider);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLING_TARGET_CPU_TASK_DEVICE_H_
