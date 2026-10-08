// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <mimalloc.h>

#include <cstdint>

#include "iree/base/api.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

TEST(MimallocAllocatorTest, OwnsSystemAllocations) {
  iree_allocator_t allocator = iree_allocator_system();
  void* allocation = nullptr;
  IREE_ASSERT_OK(
      iree_allocator_malloc_uninitialized(allocator, 64, &allocation));
  ASSERT_NE(allocation, nullptr);
  EXPECT_TRUE(mi_is_in_heap_region(allocation));

  IREE_ASSERT_OK(iree_allocator_realloc(allocator, 256, &allocation));
  ASSERT_NE(allocation, nullptr);
  EXPECT_TRUE(mi_is_in_heap_region(allocation));

  iree_allocator_free(allocator, allocation);
}

TEST(MimallocAllocatorTest, ZeroInitializesSystemAllocations) {
  iree_allocator_t allocator = iree_allocator_system();
  void* allocation = nullptr;
  IREE_ASSERT_OK(iree_allocator_malloc(allocator, 64, &allocation));
  ASSERT_NE(allocation, nullptr);
  const auto* bytes = static_cast<const std::uint8_t*>(allocation);
  for (iree_host_size_t i = 0; i < 64; ++i) {
    EXPECT_EQ(bytes[i], 0);
  }
  iree_allocator_free(allocator, allocation);
}

}  // namespace
