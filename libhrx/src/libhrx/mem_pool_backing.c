// Copyright 2026 The HRX Authors
// SPDX-License-Identifier: Apache-2.0

#include "mem_pool_backing.h"

#include <stdlib.h>
#include <string.h>

#include "hrx_internal.h"
#include "iree/hal/memory/slab_cache.h"

// Default slab length for growable GPU allocation pools.
static const iree_device_size_t HRX_MEM_POOL_GPU_SLAB_LENGTH_DEFAULT =
    (iree_device_size_t)256 * 1024 * 1024;

// Default range length for CPU/local allocation pools.
static const iree_device_size_t HRX_MEM_POOL_CPU_RANGE_LENGTH_DEFAULT =
    (iree_device_size_t)64 * 1024 * 1024;

// Minimum byte alignment for HRX memory-pool reservations.
static const iree_device_size_t HRX_MEM_POOL_ALIGNMENT = 256;

static iree_status_t hrx_mem_pool_parse_range_length_env(
    const char* name, bool* out_found, iree_device_size_t* out_length) {
  *out_found = false;
  *out_length = 0;

  const char* value = getenv(name);
  if (!value || !value[0]) {
    return iree_ok_status();
  }

  char* end = NULL;
  unsigned long long parsed = strtoull(value, &end, 10);
  if (value[0] < '0' || value[0] > '9' || !end || *end != '\0' || parsed == 0 ||
      parsed > (unsigned long long)IREE_DEVICE_SIZE_MAX) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "%s must be a positive byte count, got '%s'", name,
                            value);
  }

  *out_found = true;
  *out_length = (iree_device_size_t)parsed;
  return iree_ok_status();
}

static iree_status_t hrx_mem_pool_query_range_length(
    hrx_device_t device, iree_device_size_t* out_range_length) {
  bool has_env_range_length = false;
  iree_device_size_t range_length = 0;
  IREE_RETURN_IF_ERROR(hrx_mem_pool_parse_range_length_env(
      "HRX_MEM_POOL_BYTES", &has_env_range_length, &range_length));
  if (!has_env_range_length) {
    IREE_RETURN_IF_ERROR(hrx_mem_pool_parse_range_length_env(
        "HRX_HIP_POOL_BYTES", &has_env_range_length, &range_length));
  }

  if (!has_env_range_length) {
    if (device->type == HRX_ACCELERATOR_GPU) {
      // TLSF grows by this range length one slab at a time. A slab is fully
      // committed on first use, so its size sets the growth and idle-retention
      // granularity rather than expressing a fraction of total device memory.
      range_length = HRX_MEM_POOL_GPU_SLAB_LENGTH_DEFAULT;
    } else {
      range_length = HRX_MEM_POOL_CPU_RANGE_LENGTH_DEFAULT;
    }
  }

  if (!iree_device_size_checked_align(range_length, HRX_MEM_POOL_ALIGNMENT,
                                      &range_length)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "memory pool range length overflows alignment");
  }
  *out_range_length = range_length;
  return iree_ok_status();
}

// HRX's ordinary device memory is usable on every family of its device. Host
// mapping is required for CPU storage and optional for GPU storage. Prefer the
// dispatch device's memory ahead of optional host mapping on system memory or
// another GPU.
static iree_status_t hrx_mem_pool_backing_create_source(
    hrx_device_t device, iree_hal_pool_t** out_pool) {
  const iree_hal_device_queue_spec_t* queues =
      iree_hal_device_spec_queues(iree_hal_device_spec(device->hal_device));
  iree_hal_pool_family_access_t* families = NULL;
  IREE_RETURN_IF_ERROR(iree_allocator_malloc(
      iree_allocator_system(), queues->family_count * sizeof(*families),
      (void**)&families));
  for (iree_host_size_t i = 0; i < queues->family_count; ++i) {
    families[i].family = iree_hal_device_queue_family(device->hal_device, i);
    families[i].usage =
        IREE_HAL_BUFFER_USAGE_TRANSFER | IREE_HAL_BUFFER_USAGE_DISPATCH;
  }
  const iree_hal_pool_host_access_t host = {
      .access = IREE_HAL_MEMORY_ACCESS_ALL,
      .modes = IREE_HAL_MAPPING_MODE_SCOPED | IREE_HAL_MAPPING_MODE_PERSISTENT,
  };
  iree_hal_pool_scope_t scope = {
      .family_count = queues->family_count,
      .families = families,
  };
  iree_hal_slab_pool_options_t options;
  iree_hal_slab_pool_options_initialize(&options);
  options.trace_name = iree_make_cstring_view("hrx-mem-pool-source");
  if (device->type == HRX_ACCELERATOR_CPU) {
    scope.host = host;
  } else {
    options.preferences.host = host;
    const iree_hal_device_topology_info_t* topology_info =
        iree_hal_device_topology_info(device->hal_device);
    const iree_hal_queue_family_spec_t* dispatch_family =
        iree_hal_queue_family_spec(
            iree_hal_queue_family(device->dispatch_queue));
    const iree_hal_topology_t* topology =
        iree_hal_device_group_topology(device->hal_device_group);
    for (iree_host_size_t i = 0; i < iree_hal_topology_node_count(topology);
         ++i) {
      const iree_hal_topology_node_t* node =
          iree_hal_topology_node_at(topology, i);
      if (node->kind == IREE_HAL_TOPOLOGY_NODE_KIND_PHYSICAL_DEVICE &&
          node->device_ordinal == topology_info->topology_index &&
          iree_any_bit_set(node->physical_device_affinity,
                           dispatch_family->physical_device_affinity)) {
        options.placement.mode = IREE_HAL_POOL_PLACEMENT_PREFERRED;
        options.placement.node = node->ordinal;
        break;
      }
    }
  }
  iree_status_t status =
      iree_hal_slab_pool_create(device->hal_device_group, scope, &options,
                                iree_allocator_system(), out_pool);
  iree_allocator_free(iree_allocator_system(), families);
  return status;
}

static iree_status_t hrx_mem_pool_backing_ensure_cache_locked(
    hrx_device_t device) {
  hrx_mem_pool_backing_t* backing = &device->mem_pool_backing;
  if (backing->cache) {
    return iree_ok_status();
  }

  iree_device_size_t range_length = 0;
  IREE_RETURN_IF_ERROR(hrx_mem_pool_query_range_length(device, &range_length));

  iree_hal_tlsf_pool_options_t options = {0};
  options.tlsf_options.range_length = range_length;
  options.tlsf_options.alignment = HRX_MEM_POOL_ALIGNMENT;
  options.tlsf_options.frontier_capacity =
      IREE_HAL_MEMORY_TLSF_DEFAULT_FRONTIER_CAPACITY;
  options.asan =
      iree_hal_device_spec_sanitizer(iree_hal_device_spec(device->hal_device))
          ->asan.pool_options;
  options.budget_limit = 0;
  options.trace_name = iree_make_cstring_view("hrx-mem-pool");

  iree_hal_pool_t* backing_pool = NULL;
  iree_status_t status =
      hrx_mem_pool_backing_create_source(device, &backing_pool);
  iree_hal_pool_t* backing_cache = NULL;
  if (iree_status_is_ok(status)) {
    iree_hal_slab_cache_options_t cache_options = {.max_count = UINT32_MAX};
    status = iree_hal_tlsf_pool_query_backing_request(backing_pool, &options,
                                                      &cache_options.slab);
    if (iree_status_is_ok(status)) {
      status =
          iree_hal_slab_cache_create(backing_pool, &cache_options,
                                     iree_allocator_system(), &backing_cache);
    }
  }
  iree_hal_pool_release(backing_pool);
  if (iree_status_is_ok(status)) {
    backing->cache = backing_cache;
    backing->pool_options = options;
  } else {
    iree_hal_pool_release(backing_cache);
  }
  return status;
}

void hrx_mem_pool_backing_initialize(hrx_mem_pool_backing_t* backing) {
  memset(backing, 0, sizeof(*backing));
  iree_slim_mutex_initialize(&backing->mutex);
}

void hrx_mem_pool_backing_deinitialize(hrx_mem_pool_backing_t* backing) {
  // Every attached pool retains the device containing this owner.
  IREE_ASSERT(!backing->retention_head);
  iree_hal_pool_release(backing->cache);
  iree_slim_mutex_deinitialize(&backing->mutex);
}

iree_status_t hrx_mem_pool_backing_create_pool(hrx_device_t device,
                                               iree_hal_pool_t** out_pool) {
  *out_pool = NULL;
  hrx_mem_pool_backing_t* backing = &device->mem_pool_backing;
  iree_slim_mutex_lock(&backing->mutex);
  iree_status_t status = hrx_mem_pool_backing_ensure_cache_locked(device);
  iree_slim_mutex_unlock(&backing->mutex);
  if (!iree_status_is_ok(status)) {
    return status;
  }
  // The device retains the cache and immutable options throughout this call.
  return iree_hal_tlsf_pool_create(backing->cache, &backing->pool_options,
                                   iree_allocator_system(), out_pool);
}

// Retention is changed only by cold policy operations. Allocation and steady
// free completion use the cached maximum without traversing other pools.
static void hrx_mem_pool_backing_recompute_retention(
    hrx_mem_pool_backing_t* backing) {
  uint64_t min_bytes_to_keep = 0;
  for (hrx_mem_pool_retention_t* entry = backing->retention_head; entry;
       entry = entry->next) {
    min_bytes_to_keep = iree_max(min_bytes_to_keep, entry->min_bytes_to_keep);
  }
  backing->min_bytes_to_keep = min_bytes_to_keep;
}

static void hrx_mem_pool_backing_set_retention_locked(
    hrx_mem_pool_backing_t* backing, hrx_mem_pool_retention_t* retention,
    uint64_t min_bytes_to_keep) {
  const uint64_t previous = retention->min_bytes_to_keep;
  retention->min_bytes_to_keep = min_bytes_to_keep;
  if (min_bytes_to_keep > backing->min_bytes_to_keep) {
    backing->min_bytes_to_keep = min_bytes_to_keep;
  } else if (min_bytes_to_keep < previous &&
             previous == backing->min_bytes_to_keep) {
    hrx_mem_pool_backing_recompute_retention(backing);
  }
}

void hrx_mem_pool_backing_attach(hrx_mem_pool_backing_t* backing,
                                 hrx_mem_pool_retention_t* retention) {
  iree_slim_mutex_lock(&backing->mutex);
  *retention = (hrx_mem_pool_retention_t){.next = backing->retention_head};
  if (retention->next) {
    retention->next->previous = retention;
  }
  backing->retention_head = retention;
  iree_slim_mutex_unlock(&backing->mutex);
}

void hrx_mem_pool_backing_detach(hrx_mem_pool_backing_t* backing,
                                 hrx_mem_pool_retention_t* retention) {
  iree_slim_mutex_lock(&backing->mutex);
  if (retention->previous) {
    retention->previous->next = retention->next;
  } else {
    backing->retention_head = retention->next;
  }
  if (retention->next) {
    retention->next->previous = retention->previous;
  }
  if (retention->min_bytes_to_keep == backing->min_bytes_to_keep) {
    hrx_mem_pool_backing_recompute_retention(backing);
  }
  iree_slim_mutex_unlock(&backing->mutex);
}

void hrx_mem_pool_backing_set_retention(hrx_mem_pool_backing_t* backing,
                                        hrx_mem_pool_retention_t* retention,
                                        uint64_t min_bytes_to_keep) {
  iree_slim_mutex_lock(&backing->mutex);
  hrx_mem_pool_backing_set_retention_locked(backing, retention,
                                            min_bytes_to_keep);
  iree_slim_mutex_unlock(&backing->mutex);
}

void hrx_mem_pool_backing_trim(hrx_mem_pool_backing_t* backing,
                               hrx_mem_pool_retention_t* retention,
                               uint64_t min_bytes_to_keep) {
  iree_slim_mutex_lock(&backing->mutex);
  hrx_mem_pool_backing_set_retention_locked(backing, retention,
                                            min_bytes_to_keep);
  if (backing->cache) {
    iree_hal_pool_trim(backing->cache, IREE_HAL_POOL_TRIM_FLAG_EXCESS,
                       (iree_device_size_t)iree_min(backing->min_bytes_to_keep,
                                                    IREE_DEVICE_SIZE_MAX));
  }
  iree_slim_mutex_unlock(&backing->mutex);
}
