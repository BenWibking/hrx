// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_MEMORY_TLSF_POOL_H_
#define IREE_HAL_MEMORY_TLSF_POOL_H_

#include "iree/async/notification.h"
#include "iree/base/api.h"
#include "iree/hal/memory/tlsf.h"
#include "iree/hal/pool.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

//===----------------------------------------------------------------------===//
// iree_hal_tlsf_pool_t
//===----------------------------------------------------------------------===//

// Options for creating a HAL pool that wraps iree_hal_memory_tlsf_t.
typedef struct iree_hal_tlsf_pool_options_t {
  // Raw TLSF allocator configuration for each slab. The range length is the
  // fixed slab size and maximum single reservation served by this pool.
  // Live-pressure exhaustion grows by acquiring another slab of at least this
  // size, rounded to the required alignment. Backing reservations
  // supply history; initial_frontier must be NULL for the backing constructor.
  // Alignment requires the same support from the backing pool.
  iree_hal_memory_tlsf_options_t tlsf_options;

  // ASAN policy used to shape hidden backing ranges for reservations.
  iree_hal_asan_pool_options_t asan;

  // Logical byte budget for live reservations in this pool. 0 means unlimited.
  iree_device_size_t budget_limit;

  // Optional named-memory trace identifier for logical reservations returned by
  // this pool. Empty uses a generic process-stable identifier.
  iree_string_view_t trace_name;
} iree_hal_tlsf_pool_options_t;

// Creates an initially empty TLSF allocator retaining |backing_pool| once.
// Growth obtains ordinary reservations and borrowed prepared buffer ranges.
// Every byte inherits the backing reservation's exact reuse prerequisite.
// Failed preparation returns that original prerequisite without losing history.
//
// Release publishes reservation metadata without native work. Acquisition and
// trim drain the pending releases under a short metadata mutex. Backing-pool
// calls, host allocation/free, materialization and native advice run outside
// that mutex. Trim returns whole unused ranges with their merged history; it
// never trims the backing pool itself. Pending history may be returned for
// queue-owned waiting when ALLOW_WAIT_FRONTIER is set.
//
// Captures the backing pool's immutable capabilities and group progress owner.
// The device group outlives both pools and all operations using them.
IREE_API_EXPORT iree_status_t iree_hal_tlsf_pool_create(
    iree_hal_pool_t* backing_pool, const iree_hal_tlsf_pool_options_t* options,
    iree_allocator_t host_allocator, iree_hal_pool_t** out_pool);

// Creates a finite pool retaining the supplied prepared buffer range. Offsets
// are relative to the source view; WHOLE_BUFFER uses its remaining extent.
// Alignment rounds the range inward. The pool never grows or releases its
// backing before destruction. Explicit allocation epochs remain caller-owned.
// tlsf_options.range_length and initial_frontier must be zero/NULL: capacity
// and initial history are derived from the buffer range. Every untouched byte
// inherits the source's exact reuse prerequisite.
IREE_API_EXPORT iree_status_t iree_hal_tlsf_pool_create_from_buffer(
    iree_hal_buffer_t* buffer, iree_device_size_t offset,
    iree_device_size_t length, const iree_hal_tlsf_pool_options_t* options,
    iree_allocator_t host_allocator, iree_hal_pool_t** out_pool);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_MEMORY_TLSF_POOL_H_
