// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_SLAB_POOL_H_
#define IREE_HAL_SLAB_POOL_H_

#include "iree/hal/memory_scope.h"
#include "iree/hal/pool.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct iree_hal_slab_pool_options_t {
  // Physical backing placement, independent of the participating-family order.
  iree_hal_pool_placement_t placement;
  // Optional construction preferences never weaken required scope entries.
  struct {
    // Additional public host access when the selected memory supports it.
    iree_hal_pool_host_access_t host;
  } preferences;
  // Copied diagnostic name; empty selects the implementation's standard name.
  iree_string_view_t trace_name;
} iree_hal_slab_pool_options_t;

// Selects automatic placement and no optional public mapping grants.
IREE_API_EXPORT void iree_hal_slab_pool_options_initialize(
    iree_hal_slab_pool_options_t* out_options);

// Qualifies all requested families together before publishing a native source.
// Construction captures metadata and native owners; it acquires no payload,
// hidden cache, replica, default pool, or new execution queue. The group must
// outlive the pool and all derived buffers and operations. Failure leaves
// |out_pool| NULL. Ordinary caches and suballocators retain this pool as
// backing.
IREE_API_EXPORT iree_status_t iree_hal_slab_pool_create(
    iree_hal_device_group_t* group, iree_hal_pool_scope_t scope,
    const iree_hal_slab_pool_options_t* options,
    iree_allocator_t host_allocator, iree_hal_pool_t** out_pool);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_SLAB_POOL_H_
