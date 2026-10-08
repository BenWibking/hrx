// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_CTS_UTIL_POOL_TEST_UTIL_H_
#define IREE_HAL_CTS_UTIL_POOL_TEST_UTIL_H_

#include "iree/hal/device.h"
#include "iree/hal/memory/fixed_block_pool.h"

namespace iree::hal::cts {

// Allocates one real native range and uses it as a finite block arena. The
// requested blocks_per_slab determines its initial capacity. The caller owns
// both outputs and releases the finite pool before its native source.
iree_status_t CreateFiniteBlockPool(
    const iree_hal_queue_pool_backend_t& backend,
    iree_hal_fixed_block_pool_options_t options,
    iree_allocator_t host_allocator, iree_hal_pool_t** out_backing_pool,
    iree_hal_pool_t** out_pool);

}  // namespace iree::hal::cts

#endif  // IREE_HAL_CTS_UTIL_POOL_TEST_UTIL_H_
