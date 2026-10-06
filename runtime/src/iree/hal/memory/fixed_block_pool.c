// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/memory/fixed_block_pool.h"

#include "iree/async/frontier.h"
#include "iree/async/frontier_tracker.h"
#include "iree/async/notification.h"
#include "iree/base/internal/math.h"
#include "iree/base/threading/mutex.h"
#include "iree/hal/memory/buffer_range.h"
#include "iree/hal/memory/tracing.h"

enum {
  IREE_HAL_FIXED_BLOCK_POOL_INLINE_TRANSACTION_CAPACITY = 8,
  IREE_HAL_FIXED_BLOCK_POOL_INLINE_FRONTIER_CAPACITY = 8,
};

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

typedef struct iree_hal_fixed_block_pool_t {
  // Base pool resource for vtable dispatch and ref counting.
  iree_hal_pool_t base;

  // Provider backing the single fixed-block slab.
  iree_hal_slab_provider_t* slab_provider;

  // Retained finite backing; empty for a provider-backed pool.
  iree_hal_pool_buffer_range_t source_range;

  // Lock-free offset allocator for fixed-size blocks within |slab|.
  iree_hal_memory_fixed_block_allocator_t* block_allocator;

  // Serializes candidate snapshots and whole-batch claims. Frontier
  // eligibility, allocation, advice and tracing run outside; release never
  // takes this lock.
  iree_slim_mutex_t acquisition_mutex;

  // Maximum number of entries copied into a candidate frontier snapshot.
  uint8_t frontier_capacity;

  // Physical memory backing all fixed blocks.
  iree_hal_slab_t slab;

  // Prepared facts shared by views while this slab allocation is owned.
  iree_hal_slab_buffer_backing_t buffer_backing;

  // Host allocator used for pool metadata.
  iree_allocator_t host_allocator;

  // Stable named-memory stream for logical reservations from this pool.
  iree_hal_memory_trace_t trace;

  // Immutable memory properties provided by |slab_provider|.
  iree_hal_slab_provider_properties_t slab_properties;

  // User-visible byte capacity of each fixed block.
  iree_device_size_t user_block_size;

  // Backing byte size of every block in |block_allocator|.
  iree_device_size_t backing_block_size;

  // Number of blocks managed by |block_allocator|.
  uint32_t block_count;

  // ASAN policy used to shape hidden backing ranges.
  iree_hal_asan_pool_options_t asan_options;

  // ASAN layout for each live block. NULL when ASAN is disabled.
  iree_hal_asan_allocation_layout_t* asan_block_layouts;

  // Logical byte budget for live reservations. 0 means unlimited.
  iree_device_size_t budget_limit;

  // Approximate live reservation bytes for lock-free stats queries.
  iree_atomic_int64_t bytes_reserved;

  // Approximate live reservation count for lock-free stats queries.
  iree_atomic_int32_t reservation_count;

  // Total reservations committed by successful transactions.
  iree_atomic_int64_t reserve_count;

  // Total reservations returned by release transactions.
  iree_atomic_int64_t release_count;

  // Reserves that hit frontier-dominated reuse.
  iree_atomic_int64_t reuse_count;

  // Reserves where dominance check failed.
  iree_atomic_int64_t reuse_miss_count;

  // Reserves from fresh (never-used) blocks.
  iree_atomic_int64_t fresh_count;

  // Reserves that returned EXHAUSTED.
  iree_atomic_int64_t exhausted_count;

  // Reserves that returned OVER_BUDGET.
  iree_atomic_int64_t over_budget_count;

  // Reserves that returned NEEDS_WAIT.
  iree_atomic_int64_t wait_count;
} iree_hal_fixed_block_pool_t;

typedef struct iree_hal_fixed_block_pool_materialize_state_t
    iree_hal_fixed_block_pool_materialize_state_t;

// Per-buffer element in an owning materialization transaction.
typedef struct iree_hal_fixed_block_pool_materialize_element_t {
  // Shared transaction state controlling the ownership commit.
  iree_hal_fixed_block_pool_materialize_state_t* state;

  // Reservation released when the committed buffer is destroyed.
  iree_hal_pool_reservation_t reservation;

  // Materialized buffer staged until the complete transaction succeeds.
  iree_hal_buffer_t* buffer;
} iree_hal_fixed_block_pool_materialize_element_t;

// Shared state for an owning materialization transaction.
struct iree_hal_fixed_block_pool_materialize_state_t {
  // Borrowed from the wrapped buffer's creator. Pool owners must keep the pool
  // alive until all buffers sourced from it are destroyed.
  iree_hal_pool_t* pool;

  // Host allocator used for this state object.
  iree_allocator_t host_allocator;

  // Number of materialized buffers still referencing this transaction.
  iree_atomic_int32_t reference_count;

  // True after every buffer was materialized and reservation ownership moved.
  bool ownership_committed;

  // Per-buffer transaction elements.
  iree_hal_fixed_block_pool_materialize_element_t elements[];
};

// Staged result for one reservation acquisition. Transactions use staging so
// public output arrays remain untouched unless the operation succeeds.
typedef struct iree_hal_fixed_block_pool_acquire_element_t {
  // Candidate with a copied frontier until commit, then the acquired block.
  iree_hal_memory_fixed_block_allocator_allocation_t allocation;

  // Eligibility result established outside the acquisition mutex.
  iree_hal_pool_acquire_result_t result;

  // Request layout prepared before any block is claimed.
  iree_hal_asan_allocation_layout_t asan_layout;
} iree_hal_fixed_block_pool_acquire_element_t;

static const iree_hal_pool_vtable_t iree_hal_fixed_block_pool_vtable;
static void iree_hal_fixed_block_pool_destroy(iree_hal_pool_t* base_pool);

static const char* IREE_HAL_FIXED_BLOCK_POOL_TRACE_ID =
    "iree-hal-fixed-block-pool";

static void iree_hal_fixed_block_pool_advise_asan(
    iree_hal_fixed_block_pool_t* pool, iree_device_size_t offset,
    iree_hal_asan_range_advice_flags_t flags,
    const iree_hal_asan_allocation_layout_t* layout) {
  if (pool->source_range.buffer) {
    iree_hal_pool_buffer_range_advise_asan(&pool->source_range, offset, flags,
                                           layout);
  } else {
    iree_hal_slab_provider_advise_asan_range(pool->slab_provider, &pool->slab,
                                             offset, flags, layout);
  }
}

static bool iree_hal_fixed_block_pool_query_completed_epoch(
    void* user_data, iree_async_axis_t axis, uint64_t epoch) {
  return iree_async_frontier_tracker_query_epoch(user_data, axis, epoch);
}

//===----------------------------------------------------------------------===//
// Frontier helpers
//===----------------------------------------------------------------------===//

static bool iree_hal_fixed_block_pool_frontier_is_satisfied(
    const iree_hal_fixed_block_pool_t* pool,
    const iree_async_frontier_t* requester_frontier,
    const iree_async_frontier_t* death_frontier,
    iree_hal_memory_fixed_block_allocator_block_flags_t block_flags) {
  if (!death_frontier) {
    return block_flags == IREE_HAL_MEMORY_FIXED_BLOCK_ALLOCATOR_BLOCK_FLAG_NONE;
  }
  if (block_flags & IREE_HAL_MEMORY_FIXED_BLOCK_ALLOCATOR_BLOCK_FLAG_TAINTED) {
    return false;
  }
  if (requester_frontier) {
    const iree_async_frontier_comparison_t comparison =
        iree_async_frontier_compare(requester_frontier, death_frontier);
    if (comparison == IREE_ASYNC_FRONTIER_AFTER ||
        comparison == IREE_ASYNC_FRONTIER_EQUAL) {
      return true;
    }
  }
  if (!pool->base.epoch_query.fn) {
    return false;
  }

  iree_host_size_t requester_index = 0;
  for (uint8_t i = 0; i < death_frontier->entry_count; ++i) {
    const iree_async_axis_t axis = death_frontier->entries[i].axis;
    const uint64_t epoch = death_frontier->entries[i].epoch;
    while (requester_frontier &&
           requester_index < requester_frontier->entry_count &&
           requester_frontier->entries[requester_index].axis < axis) {
      ++requester_index;
    }
    if (requester_frontier &&
        requester_index < requester_frontier->entry_count &&
        requester_frontier->entries[requester_index].axis == axis &&
        requester_frontier->entries[requester_index].epoch >= epoch) {
      continue;
    }
    if (!pool->base.epoch_query.fn(pool->base.epoch_query.user_data, axis,
                                   epoch)) {
      return false;
    }
  }
  return true;
}

// Acquisitions hold the metadata lock. Concurrent releases only reduce the
// charge, so an addition checked against this snapshot cannot exceed the limit.
static bool iree_hal_fixed_block_pool_try_charge_transaction(
    iree_hal_fixed_block_pool_t* pool, iree_device_size_t charged_length) {
  if (pool->budget_limit != 0) {
    const iree_device_size_t current = (iree_device_size_t)iree_atomic_load(
        &pool->bytes_reserved, iree_memory_order_relaxed);
    if (current > pool->budget_limit ||
        charged_length > pool->budget_limit - current) {
      return false;
    }
  }
  iree_atomic_fetch_add(&pool->bytes_reserved, (int64_t)charged_length,
                        iree_memory_order_relaxed);
  return true;
}

static void iree_hal_fixed_block_pool_uncharge_reservation(
    iree_hal_fixed_block_pool_t* pool, iree_device_size_t charged_length) {
  iree_atomic_fetch_add(&pool->bytes_reserved, -(int64_t)charged_length,
                        iree_memory_order_relaxed);
}

static bool iree_hal_fixed_block_pool_can_wait_for_allocation(
    iree_hal_pool_reserve_flags_t flags,
    const iree_hal_memory_fixed_block_allocator_allocation_t* allocation) {
  return iree_all_bits_set(flags,
                           IREE_HAL_POOL_RESERVE_FLAG_ALLOW_WAIT_FRONTIER) &&
         allocation->death_frontier &&
         !iree_all_bits_set(
             allocation->block_flags,
             IREE_HAL_MEMORY_FIXED_BLOCK_ALLOCATOR_BLOCK_FLAG_TAINTED);
}

static iree_device_size_t iree_hal_fixed_block_pool_max_user_alignment(
    iree_device_size_t user_block_size) {
  IREE_ASSERT(user_block_size > 0);
  return (iree_device_size_t)1
         << iree_math_count_trailing_zeros_u64(user_block_size);
}

static iree_status_t iree_hal_fixed_block_pool_calculate_asan_layout(
    const iree_hal_fixed_block_pool_t* pool, iree_device_size_t user_length,
    iree_device_size_t user_alignment,
    iree_hal_asan_allocation_layout_t* out_layout) {
  IREE_RETURN_IF_ERROR(iree_hal_asan_calculate_allocation_layout(
      &pool->asan_options, user_length, user_alignment, out_layout));
  return iree_hal_asan_extend_allocation_layout(pool->backing_block_size,
                                                out_layout);
}

static void iree_hal_fixed_block_pool_return_allocation(
    iree_hal_fixed_block_pool_t* pool,
    const iree_hal_memory_fixed_block_allocator_allocation_t* allocation,
    iree_device_size_t byte_length,
    const iree_hal_asan_allocation_layout_t* asan_layout,
    iree_hal_pool_acquire_result_t result,
    iree_hal_pool_reservation_t* out_reservation,
    iree_hal_pool_acquire_info_t* out_info) {
  const bool asan_enabled =
      iree_hal_asan_pool_options_is_enabled(&pool->asan_options);
  memset(out_reservation, 0, sizeof(*out_reservation));
  out_reservation->offset =
      allocation->offset + (asan_enabled ? asan_layout->user_offset : 0);
  out_reservation->byte_length = byte_length;
  out_reservation->block_handle = allocation->block_index;
  out_reservation->slab_index = 0;

  // Tainted blocks never reach this helper: frontier_is_satisfied rejects
  // them (so they never become OK/OK_FRESH) and can_wait_for_allocation
  // excludes them (so they never become OK_NEEDS_WAIT). Preserve the exact
  // prerequisite even when this requester already covers it.
  memset(out_info, 0, sizeof(*out_info));
  out_info->reuse_frontier = allocation->death_frontier;
  out_info->result = result;

  iree_atomic_fetch_add(&pool->reservation_count, 1, iree_memory_order_relaxed);
  iree_atomic_fetch_add(&pool->reserve_count, 1, iree_memory_order_relaxed);
  switch (result) {
    case IREE_HAL_POOL_ACQUIRE_OK:
      iree_atomic_fetch_add(&pool->reuse_count, 1, iree_memory_order_relaxed);
      break;
    case IREE_HAL_POOL_ACQUIRE_OK_FRESH:
      iree_atomic_fetch_add(&pool->fresh_count, 1, iree_memory_order_relaxed);
      break;
    case IREE_HAL_POOL_ACQUIRE_OK_NEEDS_WAIT:
      iree_atomic_fetch_add(&pool->wait_count, 1, iree_memory_order_relaxed);
      break;
    default:
      IREE_ASSERT(false, "invalid successful fixed-block pool result: %u",
                  result);
      break;
  }
  if (asan_enabled) {
    pool->asan_block_layouts[allocation->block_index] = *asan_layout;
  }
  iree_hal_memory_trace_alloc(
      &pool->trace,
      (void*)((uintptr_t)pool->slab.base_ptr + out_reservation->offset),
      out_reservation->byte_length);
}

//===----------------------------------------------------------------------===//
// Create / Destroy
//===----------------------------------------------------------------------===//

static iree_status_t iree_hal_fixed_block_pool_create_impl(
    iree_hal_fixed_block_pool_options_t options,
    iree_hal_pool_buffer_range_t source_range,
    iree_hal_slab_provider_t* slab_provider,
    iree_async_notification_t* notification,
    iree_async_frontier_tracker_t* frontier_tracker,
    iree_hal_pool_epoch_query_t epoch_query, iree_allocator_t host_allocator,
    iree_hal_pool_t** out_pool) {
  IREE_ASSERT_ARGUMENT(notification);
  IREE_ASSERT_ARGUMENT(out_pool);
  IREE_TRACE_ZONE_BEGIN(z0);

  if (slab_provider) {
    IREE_RETURN_AND_END_ZONE_IF_ERROR(
        z0, iree_hal_slab_provider_validate_asan_options(slab_provider,
                                                         &options.asan));
  }

  if (!iree_device_size_is_valid_alignment(options.alignment)) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "block alignment must be a power of two");
  }
  iree_device_size_t block_alignment =
      options.alignment ? options.alignment : 1;
  if (slab_provider) {
    iree_hal_slab_provider_properties_t properties;
    iree_hal_slab_provider_query_properties(slab_provider, &properties);
    if (block_alignment > properties.allocation_alignment) {
      IREE_TRACE_ZONE_END(z0);
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "block alignment exceeds native backing guarantee");
    }
  } else {
    block_alignment = iree_max(
        block_alignment, source_range.memory.backing->maintenance_alignment);
  }

  iree_hal_memory_fixed_block_allocator_options_t block_allocator_options =
      options.block_allocator_options;
  if (block_allocator_options.frontier_capacity == 0) {
    block_allocator_options.frontier_capacity =
        IREE_HAL_MEMORY_FIXED_BLOCK_ALLOCATOR_DEFAULT_FRONTIER_CAPACITY;
  }
  iree_device_size_t user_block_size = block_allocator_options.block_size;
  iree_device_size_t backing_block_size = user_block_size;
  if (iree_hal_asan_pool_options_is_enabled(&options.asan)) {
    if (user_block_size == 0) {
      IREE_TRACE_ZONE_END(z0);
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "block_size must be > 0");
    }
    const iree_device_size_t max_user_alignment =
        iree_hal_fixed_block_pool_max_user_alignment(user_block_size);
    iree_hal_asan_allocation_layout_t block_layout;
    IREE_RETURN_AND_END_ZONE_IF_ERROR(
        z0,
        iree_hal_asan_calculate_allocation_layout(
            &options.asan, user_block_size, max_user_alignment, &block_layout));
    if (!iree_device_size_checked_align(block_layout.backing_length,
                                        block_layout.backing_offset_alignment,
                                        &backing_block_size)) {
      IREE_TRACE_ZONE_END(z0);
      return iree_make_status(
          IREE_STATUS_OUT_OF_RANGE,
          "fixed-block ASAN backing block size overflows aligning %" PRIu64
          " bytes to %" PRIu64,
          (uint64_t)block_layout.backing_length,
          (uint64_t)block_layout.backing_offset_alignment);
    }
    IREE_RETURN_AND_END_ZONE_IF_ERROR(
        z0, iree_hal_asan_extend_allocation_layout(backing_block_size,
                                                   &block_layout));
    block_allocator_options.block_size = backing_block_size;
  }

  if (!iree_device_size_checked_align(backing_block_size, block_alignment,
                                      &backing_block_size)) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "fixed block alignment overflows");
  }
  block_allocator_options.block_size = backing_block_size;
  if (source_range.buffer) {
    const iree_device_size_t block_count =
        source_range.length / backing_block_size;
    if (block_count == 0 ||
        block_count > IREE_HAL_MEMORY_FIXED_BLOCK_ALLOCATOR_MAX_BLOCKS) {
      IREE_TRACE_ZONE_END(z0);
      return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                              "buffer range has an unsupported block count");
    }
    block_allocator_options.block_count = (uint32_t)block_count;
  }

  iree_device_size_t slab_length = 0;
  if (!iree_device_size_checked_mul(block_allocator_options.block_count,
                                    block_allocator_options.block_size,
                                    &slab_length)) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "fixed-block pool slab length overflows: block_count=%u "
        "block_size=%" PRIdsz,
        (unsigned)block_allocator_options.block_count,
        block_allocator_options.block_size);
  }

  iree_hal_fixed_block_pool_t* pool = NULL;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_allocator_malloc(host_allocator, sizeof(*pool), (void**)&pool));
  memset(pool, 0, sizeof(*pool));
  iree_hal_pool_initialize(&iree_hal_fixed_block_pool_vtable, notification,
                           frontier_tracker, &pool->base);
  pool->host_allocator = host_allocator;
  iree_slim_mutex_initialize(&pool->acquisition_mutex);
  pool->frontier_capacity =
      (uint8_t)iree_min(block_allocator_options.frontier_capacity, UINT8_MAX);
  pool->source_range = source_range;
  iree_hal_buffer_retain(source_range.buffer);
  pool->base.epoch_query = epoch_query;
  pool->base.maintenance =
      source_range.buffer ? source_range.memory.backing->maintenance : NULL;
  pool->user_block_size = user_block_size;
  pool->backing_block_size = backing_block_size;
  pool->block_count = block_allocator_options.block_count;
  pool->asan_options = options.asan;
  pool->base.asan_enabled =
      iree_hal_asan_pool_options_is_enabled(&options.asan);
  pool->budget_limit = options.budget_limit;

  iree_hal_slab_provider_retain(slab_provider);
  pool->slab_provider = slab_provider;
  if (slab_provider) {
    iree_hal_slab_provider_query_properties(slab_provider,
                                            &pool->slab_properties);
  } else {
    pool->slab_properties = (iree_hal_slab_provider_properties_t){
        .memory_type = iree_hal_buffer_memory_type(source_range.buffer),
        .supported_usage = iree_hal_buffer_allowed_usage(source_range.buffer),
        .queue_family_affinity =
            iree_hal_buffer_allocation_placement(source_range.buffer)
                .queue_family_affinity,
        .allocation_alignment =
            source_range.memory.backing->allocation_alignment,
        .maintenance_alignment =
            source_range.memory.backing->maintenance_alignment,
        .atomic_operations = source_range.memory.backing->atomic_operations,
    };
  }

  iree_status_t status = iree_hal_memory_trace_initialize_pool(
      options.trace_name, IREE_HAL_FIXED_BLOCK_POOL_TRACE_ID, host_allocator,
      &pool->trace);
  if (iree_status_is_ok(status)) {
    status = iree_hal_memory_fixed_block_allocator_allocate(
        block_allocator_options, pool->host_allocator, &pool->block_allocator);
  }
  if (iree_status_is_ok(status) &&
      iree_hal_asan_pool_options_is_enabled(&pool->asan_options)) {
    status = iree_allocator_malloc_array(
        pool->host_allocator, pool->block_count,
        sizeof(*pool->asan_block_layouts), (void**)&pool->asan_block_layouts);
  }
  if (iree_status_is_ok(status) && source_range.buffer) {
    // The pointer is an opaque trace identity, never native storage to access.
    pool->slab.base_ptr = (uint8_t*)source_range.buffer;
    pool->slab.length = source_range.length;
  } else if (iree_status_is_ok(status)) {
    status = iree_hal_slab_provider_acquire_slab(pool->slab_provider,
                                                 slab_length, &pool->slab);
    if (iree_status_is_ok(status)) {
      iree_hal_slab_buffer_backing_initialize(
          pool->slab_provider, &pool->slab, pool->base.notification,
          pool->base.frontier_tracker, NULL, &pool->buffer_backing);
    }
  }
  if (!iree_status_is_ok(status)) {
    iree_hal_fixed_block_pool_destroy((iree_hal_pool_t*)pool);
    IREE_TRACE_ZONE_END(z0);
    return status;
  }

  *out_pool = (iree_hal_pool_t*)pool;
  IREE_TRACE_ZONE_END(z0);
  return iree_ok_status();
}

IREE_API_EXPORT iree_status_t iree_hal_fixed_block_pool_create(
    iree_hal_fixed_block_pool_options_t options,
    iree_hal_slab_provider_t* slab_provider,
    iree_async_notification_t* notification,
    iree_async_frontier_tracker_t* frontier_tracker,
    iree_hal_pool_epoch_query_t epoch_query, iree_allocator_t host_allocator,
    iree_hal_pool_t** out_pool) {
  IREE_ASSERT_ARGUMENT(slab_provider);
  *out_pool = NULL;
  return iree_hal_fixed_block_pool_create_impl(
      options, (iree_hal_pool_buffer_range_t){0}, slab_provider, notification,
      frontier_tracker, epoch_query, host_allocator, out_pool);
}

IREE_API_EXPORT iree_status_t iree_hal_fixed_block_pool_create_from_buffer(
    iree_hal_buffer_t* buffer, iree_device_size_t offset,
    iree_device_size_t length,
    const iree_hal_fixed_block_pool_options_t* options,
    iree_allocator_t host_allocator, iree_hal_pool_t** out_pool) {
  *out_pool = NULL;
  if (options->block_allocator_options.block_count != 0 ||
      options->block_allocator_options.initial_frontier) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "finite block geometry and history come from its buffer");
  }
  if (options->block_allocator_options.block_size == 0) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "block_size must be > 0");
  }
  if (!iree_device_size_is_valid_alignment(options->alignment)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "block alignment must be a power of two");
  }
  iree_device_size_t alignment = options->alignment ? options->alignment : 1;
  if (iree_hal_asan_pool_options_is_enabled(&options->asan)) {
    iree_hal_asan_allocation_layout_t layout;
    IREE_RETURN_IF_ERROR(iree_hal_asan_calculate_allocation_layout(
        &options->asan, options->block_allocator_options.block_size,
        iree_hal_fixed_block_pool_max_user_alignment(
            options->block_allocator_options.block_size),
        &layout));
    alignment = iree_max(alignment, layout.backing_offset_alignment);
  }
  iree_hal_pool_buffer_range_t range;
  IREE_RETURN_IF_ERROR(iree_hal_pool_buffer_range_initialize(
      buffer, offset, length, alignment, &options->asan, &range));
  iree_hal_fixed_block_pool_options_t resolved = *options;
  resolved.block_allocator_options.initial_frontier =
      range.memory.reuse_frontier;
  iree_hal_pool_epoch_query_t epoch_query = {
      .fn = iree_hal_fixed_block_pool_query_completed_epoch,
      .user_data = range.memory.backing->tracker,
  };
  iree_status_t status = iree_hal_fixed_block_pool_create_impl(
      resolved, range, NULL, range.memory.backing->notification,
      range.memory.backing->tracker, epoch_query, host_allocator, out_pool);
  iree_hal_pool_buffer_range_deinitialize(&range);
  return status;
}

static void iree_hal_fixed_block_pool_destroy(iree_hal_pool_t* base_pool) {
  IREE_TRACE_ZONE_BEGIN(z0);
  iree_hal_fixed_block_pool_t* pool = (iree_hal_fixed_block_pool_t*)base_pool;
  iree_allocator_t host_allocator = pool->host_allocator;
  iree_hal_memory_fixed_block_allocator_free(pool->block_allocator);
  if (pool->slab_provider && pool->slab.length > 0) {
    iree_hal_slab_provider_release_slab(pool->slab_provider, &pool->slab);
  }
  iree_allocator_free(pool->host_allocator, pool->asan_block_layouts);
  iree_hal_memory_trace_deinitialize(&pool->trace);
  iree_hal_pool_deinitialize(base_pool);
  iree_hal_slab_provider_release(pool->slab_provider);
  iree_hal_pool_buffer_range_deinitialize(&pool->source_range);
  iree_slim_mutex_deinitialize(&pool->acquisition_mutex);
  iree_allocator_free(host_allocator, pool);
  IREE_TRACE_ZONE_END(z0);
}

//===----------------------------------------------------------------------===//
// Reserve / Release
//===----------------------------------------------------------------------===//

static iree_status_t iree_hal_fixed_block_pool_validate_reservation_request(
    const iree_hal_fixed_block_pool_t* pool,
    const iree_hal_pool_reservation_request_t* request,
    iree_hal_asan_allocation_layout_t* out_layout) {
  const iree_device_size_t size = request->allocation_size;
  const iree_device_size_t alignment =
      request->params.min_alignment ? request->params.min_alignment : 1;
  if (size == 0) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "reservation size must be > 0");
  }
  if (!iree_device_size_is_power_of_two(alignment)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "reservation alignment (%" PRIdsz
                            ") must be a power of two",
                            alignment);
  }
  if (size > pool->user_block_size) {
    return iree_status_from_code(IREE_STATUS_OUT_OF_RANGE);
  }
  if (alignment > pool->slab_properties.allocation_alignment ||
      (pool->source_range.buffer &&
       !iree_device_size_has_alignment(pool->source_range.memory.offset,
                                       alignment))) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "reservation alignment exceeds backing alignment");
  }
  if (pool->backing_block_size % alignment != 0) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "reservation alignment %" PRIdsz
                            " is incompatible with fixed block size %" PRIdsz,
                            alignment, pool->user_block_size);
  }
  if (iree_hal_asan_pool_options_is_enabled(&pool->asan_options)) {
    IREE_RETURN_IF_ERROR(iree_hal_fixed_block_pool_calculate_asan_layout(
        pool, size, alignment, out_layout));
  }
  return iree_ok_status();
}

// Copies a candidate frontier into transaction-owned storage. Acquisitions
// remain excluded while |source| borrows the allocator's metadata.
static void iree_hal_fixed_block_pool_copy_candidate(
    const iree_hal_memory_fixed_block_allocator_allocation_t* source,
    iree_async_frontier_t* frontier_storage,
    iree_hal_memory_fixed_block_allocator_allocation_t* out_candidate) {
  *out_candidate = *source;
  if (source->death_frontier) {
    memcpy(
        frontier_storage, source->death_frontier,
        sizeof(*frontier_storage) + source->death_frontier->entry_count *
                                        sizeof(frontier_storage->entries[0]));
    out_candidate->death_frontier = frontier_storage;
  }
}

// Scans each available block at most once. Ready candidates fill the prefix;
// pending fallbacks fill the suffix and are displaced by later ready
// candidates. No bitmap claims or budget charges are visible during selection.
static iree_host_size_t iree_hal_fixed_block_pool_select_candidates(
    iree_hal_fixed_block_pool_t* pool, iree_host_size_t request_count,
    const iree_async_frontier_t* requester_frontier,
    iree_hal_pool_reserve_flags_t flags, iree_host_size_t frontier_stride,
    uint8_t* frontier_storage,
    iree_hal_fixed_block_pool_acquire_element_t* elements) {
  iree_async_frontier_t* scratch_frontier =
      (iree_async_frontier_t*)(frontier_storage +
                               request_count * frontier_stride);
  iree_host_size_t ready_count = 0;
  iree_host_size_t pending_count = 0;
  uint32_t start_block_index = 0;
  while (ready_count < request_count) {
    iree_hal_memory_fixed_block_allocator_allocation_t candidate;
    iree_slim_mutex_lock(&pool->acquisition_mutex);
    const bool found = iree_hal_memory_fixed_block_allocator_query_candidate(
        pool->block_allocator, start_block_index, &candidate);
    if (found) {
      iree_hal_fixed_block_pool_copy_candidate(&candidate, scratch_frontier,
                                               &candidate);
    }
    iree_slim_mutex_unlock(&pool->acquisition_mutex);
    if (!found) {
      break;
    }
    start_block_index = candidate.block_index + 1;

    iree_host_size_t selected_index = 0;
    iree_hal_pool_acquire_result_t result;
    if (iree_hal_fixed_block_pool_frontier_is_satisfied(
            pool, requester_frontier, candidate.death_frontier,
            candidate.block_flags)) {
      if (ready_count + pending_count == request_count) {
        --pending_count;
      }
      selected_index = ready_count++;
      result = candidate.death_frontier ? IREE_HAL_POOL_ACQUIRE_OK
                                        : IREE_HAL_POOL_ACQUIRE_OK_FRESH;
    } else {
      iree_atomic_fetch_add(&pool->reuse_miss_count, 1,
                            iree_memory_order_relaxed);
      if (ready_count + pending_count == request_count ||
          !iree_hal_fixed_block_pool_can_wait_for_allocation(flags,
                                                             &candidate)) {
        continue;
      }
      selected_index = request_count - ++pending_count;
      result = IREE_HAL_POOL_ACQUIRE_OK_NEEDS_WAIT;
    }
    iree_hal_fixed_block_pool_copy_candidate(
        &candidate,
        (iree_async_frontier_t*)(frontier_storage +
                                 selected_index * frontier_stride),
        &elements[selected_index].allocation);
    elements[selected_index].result = result;
  }
  return ready_count + pending_count;
}

// Returns false only when a candidate changed during unlocked selection. A
// failed validation or budget check leaves every candidate available, so there
// is no rollback and no capacity event for a competing acquirer to observe.
static bool iree_hal_fixed_block_pool_commit_candidates(
    iree_hal_fixed_block_pool_t* pool, iree_host_size_t request_count,
    iree_device_size_t charged_length,
    iree_hal_fixed_block_pool_acquire_element_t* elements,
    iree_hal_pool_acquire_result_t* out_result) {
  bool current = true;
  iree_slim_mutex_lock(&pool->acquisition_mutex);
  for (iree_host_size_t i = 0; i < request_count && current; ++i) {
    current = iree_hal_memory_fixed_block_allocator_candidate_is_current(
        pool->block_allocator, &elements[i].allocation);
  }
  if (current) {
    if (iree_hal_fixed_block_pool_try_charge_transaction(pool,
                                                         charged_length)) {
      for (iree_host_size_t i = 0; i < request_count; ++i) {
        iree_hal_memory_fixed_block_allocator_acquire_candidate(
            pool->block_allocator, elements[i].allocation.block_index,
            &elements[i].allocation);
      }
      *out_result = IREE_HAL_POOL_ACQUIRE_OK_FRESH;
    } else {
      iree_atomic_fetch_add(&pool->over_budget_count, 1,
                            iree_memory_order_relaxed);
      *out_result = IREE_HAL_POOL_ACQUIRE_OVER_BUDGET;
    }
  }
  iree_slim_mutex_unlock(&pool->acquisition_mutex);
  return current;
}

static iree_status_t iree_hal_fixed_block_pool_acquire_reservations(
    iree_hal_pool_t* base_pool, iree_host_size_t request_count,
    const iree_hal_pool_reservation_request_t* requests,
    const iree_async_frontier_t* requester_frontier,
    iree_hal_pool_reserve_flags_t flags,
    iree_hal_pool_reservation_t* out_reservations,
    iree_hal_pool_acquire_info_t* out_infos,
    iree_hal_pool_acquire_result_t* out_result) {
  iree_hal_fixed_block_pool_t* pool = (iree_hal_fixed_block_pool_t*)base_pool;
  iree_hal_fixed_block_pool_acquire_element_t
      inline_elements[IREE_HAL_FIXED_BLOCK_POOL_INLINE_TRANSACTION_CAPACITY];
  iree_alignas(IREE_ASYNC_FRONTIER_ALIGNMENT) uint8_t
      inline_frontiers[(IREE_HAL_FIXED_BLOCK_POOL_INLINE_TRANSACTION_CAPACITY +
                        1) *
                       (sizeof(iree_async_frontier_t) +
                        IREE_HAL_FIXED_BLOCK_POOL_INLINE_FRONTIER_CAPACITY *
                            sizeof(iree_async_frontier_entry_t))];
  const iree_host_size_t frontier_stride =
      sizeof(iree_async_frontier_t) +
      pool->frontier_capacity * sizeof(iree_async_frontier_entry_t);
  const bool needs_storage =
      request_count > IREE_ARRAYSIZE(inline_elements) ||
      pool->frontier_capacity >
          IREE_HAL_FIXED_BLOCK_POOL_INLINE_FRONTIER_CAPACITY;
  if (needs_storage &&
      iree_any_bit_set(flags, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH)) {
    for (iree_host_size_t i = 0; i < request_count; ++i) {
      iree_hal_asan_allocation_layout_t layout;
      IREE_RETURN_IF_ERROR(
          iree_hal_fixed_block_pool_validate_reservation_request(
              pool, &requests[i], &layout));
    }
    iree_atomic_fetch_add(&pool->exhausted_count, 1, iree_memory_order_relaxed);
    for (iree_host_size_t i = 0; i < request_count; ++i) {
      out_infos[i] = (iree_hal_pool_acquire_info_t){
          .result = IREE_HAL_POOL_ACQUIRE_EXHAUSTED,
          .flags = IREE_HAL_POOL_ACQUIRE_FLAG_GROWTH_REQUIRED,
      };
    }
    *out_result = IREE_HAL_POOL_ACQUIRE_EXHAUSTED;
    return iree_ok_status();
  }

  iree_hal_fixed_block_pool_acquire_element_t* elements = inline_elements;
  uint8_t* frontier_storage = inline_frontiers;
  void* storage = NULL;
  if (needs_storage) {
    iree_host_size_t total_size = 0;
    iree_host_size_t frontier_offset = 0;
    // The extra frontier is a scratch snapshot reused during candidate scans.
    // Express it separately so request_count + 1 cannot overflow.
    IREE_RETURN_IF_ERROR(IREE_STRUCT_LAYOUT(
        0, &total_size,
        IREE_STRUCT_FIELD(request_count,
                          iree_hal_fixed_block_pool_acquire_element_t, NULL),
        IREE_STRUCT_ARRAY_FIELD_ALIGNED(request_count, frontier_stride, uint8_t,
                                        IREE_ASYNC_FRONTIER_ALIGNMENT,
                                        &frontier_offset),
        IREE_STRUCT_FIELD(frontier_stride, uint8_t, NULL)));
    IREE_RETURN_IF_ERROR(
        iree_allocator_malloc(pool->host_allocator, total_size, &storage));
    elements = (iree_hal_fixed_block_pool_acquire_element_t*)storage;
    frontier_storage = (uint8_t*)storage + frontier_offset;
  }

  iree_status_t status = iree_ok_status();
  for (iree_host_size_t i = 0; i < request_count && iree_status_is_ok(status);
       ++i) {
    status = iree_hal_fixed_block_pool_validate_reservation_request(
        pool, &requests[i], &elements[i].asan_layout);
  }
  iree_device_size_t charged_length = 0;
  if (iree_status_is_ok(status) &&
      !iree_device_size_checked_mul(request_count, pool->backing_block_size,
                                    &charged_length)) {
    status = iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                              "fixed-block transaction charge overflows");
  }

  if (iree_status_is_ok(status)) {
    iree_hal_pool_acquire_result_t result = IREE_HAL_POOL_ACQUIRE_NONE;
    iree_host_size_t selected_count = 0;
    do {
      selected_count = iree_hal_fixed_block_pool_select_candidates(
          pool, request_count, requester_frontier, flags, frontier_stride,
          frontier_storage, elements);
      if (selected_count != request_count) {
        iree_atomic_fetch_add(&pool->exhausted_count, 1,
                              iree_memory_order_relaxed);
        result = IREE_HAL_POOL_ACQUIRE_EXHAUSTED;
        break;
      }
    } while (!iree_hal_fixed_block_pool_commit_candidates(
        pool, request_count, charged_length, elements, &result));

    if (result == IREE_HAL_POOL_ACQUIRE_EXHAUSTED ||
        result == IREE_HAL_POOL_ACQUIRE_OVER_BUDGET) {
      memset(out_infos, 0, request_count * sizeof(*out_infos));
      out_infos[result == IREE_HAL_POOL_ACQUIRE_EXHAUSTED ? selected_count : 0]
          .result = result;
    } else {
      for (iree_host_size_t i = 0; i < request_count; ++i) {
        const iree_hal_pool_acquire_result_t item_result = elements[i].result;
        iree_hal_fixed_block_pool_return_allocation(
            pool, &elements[i].allocation, requests[i].allocation_size,
            &elements[i].asan_layout, elements[i].result, &out_reservations[i],
            &out_infos[i]);
        if (item_result == IREE_HAL_POOL_ACQUIRE_OK_NEEDS_WAIT ||
            (item_result == IREE_HAL_POOL_ACQUIRE_OK &&
             result == IREE_HAL_POOL_ACQUIRE_OK_FRESH)) {
          result = item_result;
        }
      }
    }
    *out_result = result;
  }
  iree_allocator_free(pool->host_allocator, storage);
  return status;
}

static void iree_hal_fixed_block_pool_release_one_reservation(
    iree_hal_pool_t* base_pool, const iree_hal_pool_reservation_t* reservation,
    const iree_async_frontier_t* death_frontier) {
  iree_hal_fixed_block_pool_t* pool = (iree_hal_fixed_block_pool_t*)base_pool;

  iree_hal_memory_trace_free(
      &pool->trace,
      (void*)((uintptr_t)pool->slab.base_ptr + reservation->offset));

  const uint32_t block_index = (uint32_t)reservation->block_handle;
  iree_hal_memory_fixed_block_allocator_release(pool->block_allocator,
                                                block_index, death_frontier);

  iree_hal_fixed_block_pool_uncharge_reservation(pool,
                                                 pool->backing_block_size);
  iree_atomic_fetch_add(&pool->reservation_count, -1,
                        iree_memory_order_relaxed);
  iree_atomic_fetch_add(&pool->release_count, 1, iree_memory_order_relaxed);
}

static void iree_hal_fixed_block_pool_release_reservations(
    iree_hal_pool_t* base_pool, iree_host_size_t reservation_count,
    const iree_hal_pool_reservation_t* reservations,
    const iree_async_frontier_t* death_frontier) {
  iree_hal_fixed_block_pool_t* pool = (iree_hal_fixed_block_pool_t*)base_pool;
  for (iree_host_size_t i = 0; i < reservation_count; ++i) {
    iree_hal_fixed_block_pool_release_one_reservation(
        base_pool, &reservations[i], death_frontier);
  }
  iree_async_notification_signal_if_observed(pool->base.notification,
                                             INT32_MAX);
}

//===----------------------------------------------------------------------===//
// Wrap / Query / Trim / Notification
//===----------------------------------------------------------------------===//

static void iree_hal_fixed_block_pool_advise_asan_reservations(
    iree_hal_pool_t* base_pool, iree_host_size_t reservation_count,
    const iree_hal_pool_reservation_t* reservations,
    iree_hal_asan_range_advice_flags_t flags) {
  iree_hal_fixed_block_pool_t* pool = (iree_hal_fixed_block_pool_t*)base_pool;
  for (iree_host_size_t i = 0; i < reservation_count; ++i) {
    const uint32_t block_index = (uint32_t)reservations[i].block_handle;
    iree_hal_fixed_block_pool_advise_asan(
        pool, (iree_device_size_t)block_index * pool->backing_block_size, flags,
        &pool->asan_block_layouts[block_index]);
  }
}

static void iree_hal_fixed_block_pool_buffer_release(
    void* user_data, iree_hal_buffer_t* buffer) {
  (void)buffer;
  iree_hal_fixed_block_pool_materialize_element_t* element =
      (iree_hal_fixed_block_pool_materialize_element_t*)user_data;
  iree_hal_fixed_block_pool_materialize_state_t* state = element->state;
  if (state->ownership_committed) {
    iree_hal_pool_advise_asan_reservations(
        state->pool, 1, &element->reservation,
        IREE_HAL_ASAN_RANGE_ADVICE_FLAG_RELEASED);
    iree_hal_pool_release_reservations(state->pool, 1, &element->reservation,
                                       NULL);
  }
  const int32_t previous_count = iree_atomic_fetch_sub(
      &state->reference_count, 1, iree_memory_order_acq_rel);
  IREE_ASSERT(previous_count > 0);
  if (previous_count == 1) {
    iree_allocator_free(state->host_allocator, state);
  }
}

static iree_status_t iree_hal_fixed_block_pool_materialize_reservations(
    iree_hal_pool_t* base_pool, iree_host_size_t reservation_count,
    const iree_hal_pool_reservation_request_t* requests,
    const iree_hal_pool_reservation_t* reservations,
    iree_hal_pool_materialize_flags_t flags, iree_hal_buffer_t** out_buffers) {
  iree_hal_fixed_block_pool_t* pool = (iree_hal_fixed_block_pool_t*)base_pool;

  for (iree_host_size_t i = 0; i < reservation_count; ++i) {
    if (reservations[i].byte_length < requests[i].allocation_size) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "reservation %" PRIhsz " has %" PRIdsz
          " bytes but its allocation request requires %" PRIdsz,
          i, reservations[i].byte_length, requests[i].allocation_size);
    }
  }

  const bool transfer_ownership = iree_all_bits_set(
      flags, IREE_HAL_POOL_MATERIALIZE_FLAG_TRANSFER_RESERVATION_OWNERSHIP);
  iree_hal_fixed_block_pool_materialize_state_t* state = NULL;
  iree_hal_buffer_t*
      inline_buffers[IREE_HAL_FIXED_BLOCK_POOL_INLINE_TRANSACTION_CAPACITY] = {
          0};
  iree_hal_buffer_t** staged_buffers = inline_buffers;
  bool staged_buffers_allocated = false;
  iree_status_t status = iree_ok_status();
  if (transfer_ownership) {
    if (reservation_count > INT32_MAX) {
      return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                              "materialization count exceeds INT32_MAX");
    }
    iree_host_size_t state_size = 0;
    if (!iree_host_size_checked_mul_add(
            reservation_count,
            sizeof(iree_hal_fixed_block_pool_materialize_element_t),
            sizeof(*state), &state_size)) {
      return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                              "materialization state size overflow");
    }
    status =
        iree_allocator_malloc(pool->host_allocator, state_size, (void**)&state);
    if (!iree_status_is_ok(status)) {
      return status;
    }
    memset(state, 0, state_size);
    state->pool = base_pool;
    state->host_allocator = pool->host_allocator;
    iree_atomic_store(&state->reference_count, (int32_t)reservation_count,
                      iree_memory_order_relaxed);
    for (iree_host_size_t i = 0; i < reservation_count; ++i) {
      state->elements[i].state = state;
      state->elements[i].reservation = reservations[i];
    }
  } else if (reservation_count > IREE_ARRAYSIZE(inline_buffers)) {
    status = iree_allocator_malloc_array(
        pool->host_allocator, reservation_count, sizeof(*staged_buffers),
        (void**)&staged_buffers);
    if (!iree_status_is_ok(status)) {
      return status;
    }
    staged_buffers_allocated = true;
    memset(staged_buffers, 0, reservation_count * sizeof(*staged_buffers));
  }

  iree_host_size_t materialized_count = 0;
  while (materialized_count < reservation_count && iree_status_is_ok(status)) {
    iree_hal_buffer_release_callback_t release_callback =
        iree_hal_buffer_release_callback_null();
    iree_hal_buffer_t** staged_buffer = &staged_buffers[materialized_count];
    if (state) {
      iree_hal_fixed_block_pool_materialize_element_t* element =
          &state->elements[materialized_count];
      release_callback.fn = iree_hal_fixed_block_pool_buffer_release;
      release_callback.user_data = element;
      staged_buffer = &element->buffer;
    }
    if (pool->source_range.buffer) {
      status = iree_hal_pool_buffer_range_materialize(
          &pool->source_range, reservations[materialized_count].offset,
          reservations[materialized_count].byte_length,
          requests[materialized_count].params,
          iree_hal_memory_fixed_block_allocator_block_death_frontier(
              pool->block_allocator,
              (uint32_t)reservations[materialized_count].block_handle),
          release_callback, pool->host_allocator, staged_buffer);
    } else {
      status = iree_hal_slab_provider_wrap_buffer(
          pool->slab_provider, &pool->slab,
          reservations[materialized_count].offset,
          reservations[materialized_count].byte_length,
          requests[materialized_count].params, release_callback, staged_buffer);
    }
    if (iree_status_is_ok(status)) {
      if (pool->slab_provider) {
        (*staged_buffer)->memory = (iree_hal_buffer_memory_view_t){
            .backing = &pool->buffer_backing.facts,
            .offset = reservations[materialized_count].offset,
            .reuse_frontier =
                iree_hal_memory_fixed_block_allocator_block_death_frontier(
                    pool->block_allocator,
                    (uint32_t)reservations[materialized_count].block_handle),
        };
      }
      if ((*staged_buffer)->memory.reuse_frontier &&
          (*staged_buffer)->memory.reuse_frontier->entry_count == 0) {
        (*staged_buffer)->memory.reuse_frontier = NULL;
      }
      ++materialized_count;
    }
  }
  if (iree_status_is_ok(status)) {
    if (state) {
      state->ownership_committed = true;
    }
    for (iree_host_size_t i = 0; i < reservation_count; ++i) {
      out_buffers[i] = state ? state->elements[i].buffer : staged_buffers[i];
    }
  } else {
    if (state) {
      iree_atomic_store(&state->reference_count, (int32_t)materialized_count,
                        iree_memory_order_relaxed);
    }
    for (iree_host_size_t i = 0; i < materialized_count; ++i) {
      iree_hal_buffer_release(state ? state->elements[i].buffer
                                    : staged_buffers[i]);
    }
    if (state && materialized_count == 0) {
      iree_allocator_free(pool->host_allocator, state);
    }
  }
  if (staged_buffers_allocated) {
    iree_allocator_free(pool->host_allocator, staged_buffers);
  }
  return status;
}

static void iree_hal_fixed_block_pool_query_capabilities(
    const iree_hal_pool_t* base_pool,
    iree_hal_pool_capabilities_t* out_capabilities) {
  const iree_hal_fixed_block_pool_t* pool =
      (const iree_hal_fixed_block_pool_t*)base_pool;
  out_capabilities->memory_type = pool->slab_properties.memory_type;
  out_capabilities->allowed_access =
      pool->source_range.buffer
          ? iree_hal_buffer_allowed_access(pool->source_range.buffer)
          : IREE_HAL_MEMORY_ACCESS_ALL;
  out_capabilities->supported_usage = pool->slab_properties.supported_usage;
  out_capabilities->queue_family_affinity =
      pool->slab_properties.queue_family_affinity;
  out_capabilities->atomic_operations = pool->slab_properties.atomic_operations;
  out_capabilities->min_allocation_size = 1;
  out_capabilities->max_allocation_size = pool->user_block_size;
  iree_device_size_t alignment = iree_min(
      iree_hal_fixed_block_pool_max_user_alignment(pool->backing_block_size),
      pool->slab_properties.allocation_alignment);
  if (pool->source_range.buffer && pool->source_range.memory.offset) {
    alignment =
        iree_min(alignment, iree_hal_fixed_block_pool_max_user_alignment(
                                pool->source_range.memory.offset));
  }
  if (iree_hal_asan_pool_options_is_enabled(&pool->asan_options)) {
    alignment = iree_min(
        alignment,
        iree_hal_fixed_block_pool_max_user_alignment(pool->user_block_size));
  }
  out_capabilities->max_allocation_alignment = alignment;
  out_capabilities->maintenance_alignment =
      pool->slab_properties.maintenance_alignment;
}

static iree_status_t iree_hal_fixed_block_pool_validate_asan(
    const iree_hal_pool_t* base_pool,
    const iree_hal_asan_pool_options_t* options) {
  const iree_hal_fixed_block_pool_t* pool =
      (const iree_hal_fixed_block_pool_t*)base_pool;
  if (pool->slab_provider) {
    return iree_hal_slab_provider_validate_asan_options(pool->slab_provider,
                                                        options);
  }
  const iree_hal_buffer_range_advice_t* advice =
      pool->source_range.memory.backing->advice;
  if (!advice) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "buffer has no native ASAN range advice");
  }
  return advice->validate_asan(advice->user_data, options);
}

static void iree_hal_fixed_block_pool_query_stats(
    const iree_hal_pool_t* base_pool, iree_hal_pool_stats_t* out_stats) {
  const iree_hal_fixed_block_pool_t* pool =
      (const iree_hal_fixed_block_pool_t*)base_pool;
  iree_hal_memory_fixed_block_allocator_stats_t block_stats;
  iree_hal_memory_fixed_block_allocator_query_stats(pool->block_allocator,
                                                    &block_stats);
  out_stats->bytes_reserved = (iree_device_size_t)iree_atomic_load(
      &pool->bytes_reserved, iree_memory_order_relaxed);
  const iree_device_size_t managed_bytes =
      (iree_device_size_t)block_stats.block_count * pool->backing_block_size;
  out_stats->bytes_free = managed_bytes - out_stats->bytes_reserved;
  out_stats->bytes_committed = pool->slab.length;
  out_stats->budget_limit = pool->budget_limit;
  out_stats->reservation_count = (uint32_t)iree_atomic_load(
      &pool->reservation_count, iree_memory_order_relaxed);
  out_stats->slab_count = 1;
  out_stats->reserve_count = (uint64_t)iree_atomic_load(
      &pool->reserve_count, iree_memory_order_relaxed);
  out_stats->release_count = (uint64_t)iree_atomic_load(
      &pool->release_count, iree_memory_order_relaxed);
  out_stats->reuse_count =
      (uint64_t)iree_atomic_load(&pool->reuse_count, iree_memory_order_relaxed);
  out_stats->reuse_miss_count = (uint64_t)iree_atomic_load(
      &pool->reuse_miss_count, iree_memory_order_relaxed);
  out_stats->fresh_count =
      (uint64_t)iree_atomic_load(&pool->fresh_count, iree_memory_order_relaxed);
  out_stats->exhausted_count = (uint64_t)iree_atomic_load(
      &pool->exhausted_count, iree_memory_order_relaxed);
  out_stats->over_budget_count = (uint64_t)iree_atomic_load(
      &pool->over_budget_count, iree_memory_order_relaxed);
  out_stats->wait_count =
      (uint64_t)iree_atomic_load(&pool->wait_count, iree_memory_order_relaxed);
}

static void iree_hal_fixed_block_pool_trim(
    iree_hal_pool_t* base_pool, iree_hal_pool_trim_flags_t flags,
    iree_device_size_t min_bytes_to_keep) {
  iree_hal_fixed_block_pool_t* pool = (iree_hal_fixed_block_pool_t*)base_pool;
  // The single slab supplies the pool's fixed capacity for its entire lifetime.
  (void)min_bytes_to_keep;
  if (pool->slab_provider) {
    iree_hal_slab_provider_trim(pool->slab_provider, flags);
  }
}

//===----------------------------------------------------------------------===//
// Vtable
//===----------------------------------------------------------------------===//

static const iree_hal_pool_vtable_t iree_hal_fixed_block_pool_vtable = {
    .destroy = iree_hal_fixed_block_pool_destroy,
    .acquire_reservations = iree_hal_fixed_block_pool_acquire_reservations,
    .release_reservations = iree_hal_fixed_block_pool_release_reservations,
    .materialize_reservations =
        iree_hal_fixed_block_pool_materialize_reservations,
    .query_capabilities = iree_hal_fixed_block_pool_query_capabilities,
    .validate_asan = iree_hal_fixed_block_pool_validate_asan,
    .query_stats = iree_hal_fixed_block_pool_query_stats,
    .trim = iree_hal_fixed_block_pool_trim,
    .advise_asan_reservations =
        iree_hal_fixed_block_pool_advise_asan_reservations,
};
