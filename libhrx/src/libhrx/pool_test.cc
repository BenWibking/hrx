// Copyright 2026 The HRX Authors
// SPDX-License-Identifier: Apache-2.0

#include <cstring>

#include "hrx_internal.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

class CpuPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    IREE_ASSERT_OK(hrx_status_to_iree(hrx_cpu_initialize(/*flags=*/0)));
    IREE_ASSERT_OK(
        hrx_status_to_iree(hrx_cpu_device_get(/*index=*/0, &device_)));
  }

  void TearDown() override {
    IREE_EXPECT_OK(hrx_status_to_iree(hrx_cpu_shutdown()));
  }

  iree_hal_buffer_params_t BufferParams() const {
    return {
        /*.usage=*/IREE_HAL_BUFFER_USAGE_DEFAULT |
            IREE_HAL_BUFFER_USAGE_MAPPING_SCOPED,
        /*.access=*/IREE_HAL_MEMORY_ACCESS_ALL,
        /*.type=*/IREE_HAL_MEMORY_TYPE_HOST_LOCAL |
            IREE_HAL_MEMORY_TYPE_DEVICE_VISIBLE,
    };
  }

  // CPU device supplying backing storage for the pools under test.
  hrx_device_t device_ = nullptr;
};

TEST_F(CpuPoolTest, BatchedReservationsRetainPoolUntilRelease) {
  const iree_hal_buffer_params_t params = BufferParams();
  iree_hal_pool_t* pool = nullptr;
  IREE_ASSERT_OK(hrx_iree_exact_pool_create(device_->allocator.hal_allocator,
                                            params, &pool));

  const iree_hal_pool_reservation_request_t requests[2] = {
      {/*.params=*/params, /*.allocation_size=*/4096},
      {/*.params=*/params, /*.allocation_size=*/8192},
  };
  iree_hal_pool_reservation_t reservations[2];
  iree_hal_pool_acquire_info_t acquire_infos[2];
  iree_hal_pool_acquire_result_t result;
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool, IREE_ARRAYSIZE(requests), requests,
      /*requester_frontier=*/nullptr, IREE_HAL_POOL_RESERVE_FLAG_NONE,
      reservations, acquire_infos, &result));
  ASSERT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
  EXPECT_EQ(acquire_infos[0].result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
  EXPECT_EQ(acquire_infos[1].result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);

  iree_hal_buffer_t* borrowed_buffers[2];
  IREE_ASSERT_OK(iree_hal_pool_materialize_reservations(
      pool, IREE_ARRAYSIZE(reservations), requests, reservations,
      IREE_HAL_POOL_MATERIALIZE_FLAG_NONE, borrowed_buffers));
  EXPECT_EQ(iree_hal_buffer_byte_length(borrowed_buffers[0]), 4096u);
  EXPECT_EQ(iree_hal_buffer_byte_length(borrowed_buffers[1]), 8192u);

  // Model HRX releasing its public buffer wrapper while queue operations still
  // retain transient buffers backed by these reservations. Each reservation
  // keeps the exact pool alive through the later retirement callback.
  iree_hal_pool_release(pool);
  iree_hal_buffer_release(borrowed_buffers[0]);
  iree_hal_buffer_release(borrowed_buffers[1]);
  iree_hal_pool_release_reservations(pool, IREE_ARRAYSIZE(reservations),
                                     reservations,
                                     /*death_frontier=*/nullptr);
}

TEST_F(CpuPoolTest, TransferredReservationOwnsBackingBuffer) {
  const iree_hal_buffer_params_t params = BufferParams();
  iree_hal_pool_t* pool = nullptr;
  IREE_ASSERT_OK(hrx_iree_exact_pool_create(device_->allocator.hal_allocator,
                                            params, &pool));

  iree_hal_buffer_t* buffer = nullptr;
  IREE_ASSERT_OK(iree_hal_pool_allocate_buffer(
      pool, params, /*allocation_size=*/4096, /*requester_frontier=*/nullptr,
      iree_infinite_timeout(), &buffer));

  iree_hal_buffer_release(buffer);
  iree_hal_pool_release(pool);
}

TEST_F(CpuPoolTest, AcceptsWeakerAndRejectsInvalidOrStrongerAlignment) {
  iree_hal_buffer_params_t params = BufferParams();
  params.min_alignment = 8;
  iree_hal_pool_t* pool = nullptr;
  IREE_ASSERT_OK(hrx_iree_exact_pool_create(device_->allocator.hal_allocator,
                                            params, &pool));

  iree_hal_pool_reservation_request_t request = {
      /*.params=*/params,
      /*.allocation_size=*/4096,
  };
  iree_hal_pool_reservation_t reservation;
  iree_hal_pool_acquire_info_t acquire_info;
  iree_hal_pool_acquire_result_t result;

  request.params.min_alignment = 0;
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool, 1, &request, /*requester_frontier=*/nullptr,
      IREE_HAL_POOL_RESERVE_FLAG_NONE, &reservation, &acquire_info, &result));
  iree_hal_pool_release_reservations(pool, 1, &reservation,
                                     /*death_frontier=*/nullptr);

  request.params.min_alignment = 3;
  IREE_EXPECT_STATUS_IS(IREE_STATUS_INVALID_ARGUMENT,
                        iree_hal_pool_acquire_reservations(
                            pool, 1, &request, /*requester_frontier=*/nullptr,
                            IREE_HAL_POOL_RESERVE_FLAG_NONE, &reservation,
                            &acquire_info, &result));

  request.params.min_alignment = 16;
  IREE_EXPECT_STATUS_IS(IREE_STATUS_INVALID_ARGUMENT,
                        iree_hal_pool_acquire_reservations(
                            pool, 1, &request, /*requester_frontier=*/nullptr,
                            IREE_HAL_POOL_RESERVE_FLAG_NONE, &reservation,
                            &acquire_info, &result));

  request.params.min_alignment = 8;
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool, 1, &request, /*requester_frontier=*/nullptr,
      IREE_HAL_POOL_RESERVE_FLAG_NONE, &reservation, &acquire_info, &result));
  EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
  iree_hal_pool_release_reservations(pool, 1, &reservation,
                                     /*death_frontier=*/nullptr);
  iree_hal_pool_release(pool);
}

TEST_F(CpuPoolTest, MemoryPoolTrimPreservesLiveBuffersAndRetentionFloor) {
  const hrx_mem_pool_props_t properties = {};
  hrx_mem_pool_t pool = nullptr;
  IREE_ASSERT_OK(
      hrx_status_to_iree(hrx_mem_pool_create(device_, &properties, &pool)));

  const hrx_buffer_params_t params = {
      /*.type=*/HRX_MEMORY_TYPE_HOST_LOCAL,
      /*.access=*/HRX_MEMORY_ACCESS_ALL,
      /*.usage=*/HRX_BUFFER_USAGE_TRANSFER | HRX_BUFFER_USAGE_MAPPING_SCOPED,
      /*.queue_affinity=*/0,
  };
  hrx_buffer_t buffer = nullptr;
  IREE_ASSERT_OK(hrx_status_to_iree(
      hrx_mem_pool_allocate_buffer(pool, params, 1024, &buffer)));

  uint64_t committed_bytes = 0;
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_get_attribute(
      pool, HRX_MEM_POOL_ATTR_RESERVED_MEM_CURRENT, &committed_bytes)));
  ASSERT_GE(committed_bytes, 1024u);

  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_trim(pool, 0)));
  iree_hal_buffer_mapping_t mapping;
  IREE_ASSERT_OK(iree_hal_buffer_map_range(
      buffer->hal_buffer, IREE_HAL_MAPPING_MODE_SCOPED,
      IREE_HAL_MEMORY_ACCESS_READ | IREE_HAL_MEMORY_ACCESS_WRITE,
      /*byte_offset=*/0, /*byte_length=*/1024, &mapping));
  memset(mapping.contents.data, 0x3C, mapping.contents.data_length);
  EXPECT_EQ(mapping.contents.data[1023], 0x3C);
  IREE_ASSERT_OK(iree_hal_buffer_unmap_range(&mapping));
  hrx_buffer_release(buffer);

  IREE_ASSERT_OK(hrx_status_to_iree(
      hrx_mem_pool_trim(pool, static_cast<size_t>(committed_bytes))));
  uint64_t retained_bytes = 0;
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_get_attribute(
      pool, HRX_MEM_POOL_ATTR_RESERVED_MEM_CURRENT, &retained_bytes)));
  EXPECT_EQ(retained_bytes, committed_bytes);

  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_set_attribute(
      pool, HRX_MEM_POOL_ATTR_RELEASE_THRESHOLD, committed_bytes)));
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_release_unused(pool)));
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_get_attribute(
      pool, HRX_MEM_POOL_ATTR_RESERVED_MEM_CURRENT, &retained_bytes)));
  EXPECT_EQ(retained_bytes, committed_bytes);

  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_set_attribute(
      pool, HRX_MEM_POOL_ATTR_RELEASE_THRESHOLD, 0)));
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_release_unused(pool)));
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_get_attribute(
      pool, HRX_MEM_POOL_ATTR_RESERVED_MEM_CURRENT, &retained_bytes)));
  EXPECT_EQ(retained_bytes, 0u);

  IREE_ASSERT_OK(hrx_status_to_iree(
      hrx_mem_pool_allocate_buffer(pool, params, 1024, &buffer)));
  hrx_buffer_release(buffer);
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_trim(pool, 0)));
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_mem_pool_get_attribute(
      pool, HRX_MEM_POOL_ATTR_RESERVED_MEM_CURRENT, &retained_bytes)));
  EXPECT_EQ(retained_bytes, 0u);
  hrx_mem_pool_release(pool);
}

}  // namespace
