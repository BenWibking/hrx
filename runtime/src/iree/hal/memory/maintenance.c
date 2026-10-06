// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/memory/maintenance.h"

void iree_hal_memory_maintenance_initialize(
    const iree_hal_memory_maintenance_vtable_t* vtable,
    iree_hal_memory_maintenance_t* out_maintenance) {
  memset(out_maintenance, 0, sizeof(*out_maintenance));
  iree_hal_resource_initialize(vtable, &out_maintenance->resource);
  iree_slim_mutex_initialize(&out_maintenance->mutex);
}

void iree_hal_memory_maintenance_deinitialize(
    iree_hal_memory_maintenance_t* maintenance) {
  IREE_ASSERT(!maintenance->head, "maintenance work must be joined");
  iree_slim_mutex_deinitialize(&maintenance->mutex);
}

void iree_hal_memory_maintenance_retain(
    iree_hal_memory_maintenance_t* maintenance) {
  iree_hal_resource_retain(maintenance);
}

void iree_hal_memory_maintenance_release(
    iree_hal_memory_maintenance_t* maintenance) {
  iree_hal_resource_release(maintenance);
}

void iree_hal_memory_maintenance_enqueue(
    iree_hal_memory_maintenance_t* maintenance,
    iree_hal_memory_maintenance_entry_t* entry) {
  entry->next = NULL;
  iree_slim_mutex_lock(&maintenance->mutex);
  const bool needs_wake = maintenance->head == NULL;
  if (maintenance->tail) {
    maintenance->tail->next = entry;
  } else {
    maintenance->head = entry;
  }
  maintenance->tail = entry;
  iree_slim_mutex_unlock(&maintenance->mutex);
  if (needs_wake) {
    const iree_hal_memory_maintenance_vtable_t* vtable =
        (const iree_hal_memory_maintenance_vtable_t*)
            maintenance->resource.vtable;
    vtable->wake(maintenance);
  }
}

bool iree_hal_memory_maintenance_has_work(
    iree_hal_memory_maintenance_t* maintenance) {
  iree_slim_mutex_lock(&maintenance->mutex);
  const bool has_work = maintenance->head != NULL;
  iree_slim_mutex_unlock(&maintenance->mutex);
  return has_work;
}

bool iree_hal_memory_maintenance_run_one(
    iree_hal_memory_maintenance_t* maintenance) {
  iree_slim_mutex_lock(&maintenance->mutex);
  iree_hal_memory_maintenance_entry_t* entry = maintenance->head;
  if (entry) {
    maintenance->head = entry->next;
    if (!maintenance->head) {
      maintenance->tail = NULL;
    }
  }
  iree_slim_mutex_unlock(&maintenance->mutex);
  if (!entry) {
    return false;
  }
  entry->fn(entry);
  return true;
}
