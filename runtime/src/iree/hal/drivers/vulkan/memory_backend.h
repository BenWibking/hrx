// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_VULKAN_MEMORY_BACKEND_H_
#define IREE_HAL_DRIVERS_VULKAN_MEMORY_BACKEND_H_

#include "iree/hal/drivers/vulkan/allocator.h"
#include "iree/hal/memory_backend.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Borrowed native owners established before the device group is published.
// The containing device and sealed group outlive all constructed pools.
typedef struct iree_hal_vulkan_memory_backend_t {
  // Generic native schema and shared Vulkan construction factory.
  iree_hal_memory_backend_t base;
  // Native allocation owner used for buffer placement metadata.
  iree_hal_device_t* device;
  // Existing whole-buffer allocation and sparse-binding services.
  iree_hal_vulkan_allocator_t* allocator;
  // Device-owned native dispatch table.
  const iree_hal_vulkan_device_syms_t* syms;
  // Native resource and memory namespace.
  VkDevice logical_device;
  // Device-owned immutable native property snapshot used during construction.
  const iree_hal_vulkan_physical_device_snapshot_t* physical_device;
  // Features actually enabled on this native owner.
  iree_hal_vulkan_features_t enabled_features;
  // Existing placement-local capacity notification.
  iree_async_notification_t* notification;
  // Existing independent cold allocation and retirement owner.
  iree_hal_memory_maintenance_t* maintenance;
  // Existing completion probe with a group-lifetime borrowed context.
  iree_hal_pool_epoch_query_t epoch_query;
} iree_hal_vulkan_memory_backend_t;

// Publishes the factory header after the caller fills the borrowed native and
// progress owners. Creates no native objects, payload, or allocation policy.
void iree_hal_vulkan_memory_backend_initialize(
    iree_hal_vulkan_memory_backend_t* backend);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DRIVERS_VULKAN_MEMORY_BACKEND_H_
