// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_MEMORY_MAINTENANCE_THREAD_H_
#define IREE_HAL_MEMORY_MAINTENANCE_THREAD_H_

#include "iree/base/threading/affinity.h"
#include "iree/hal/memory/maintenance.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Creates a cold memory owner independent of native execution-queue lifetime.
// The thread establishes |affinity| before running. All pools over the native
// memory domain share this owner; individual pool policies need no threads.
iree_status_t iree_hal_memory_maintenance_thread_create(
    iree_thread_affinity_t affinity, iree_allocator_t host_allocator,
    iree_hal_memory_maintenance_t** out_maintenance);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_MEMORY_MAINTENANCE_THREAD_H_
