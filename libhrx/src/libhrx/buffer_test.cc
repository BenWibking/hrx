// Copyright 2026 The HRX Authors
// SPDX-License-Identifier: Apache-2.0

#include <cstring>

#include "hrx_internal.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

class CpuBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    IREE_ASSERT_OK(hrx_status_to_iree(hrx_cpu_initialize(/*flags=*/0)));
    IREE_ASSERT_OK(hrx_status_to_iree(hrx_cpu_device_get(0, &device_)));
    IREE_ASSERT_OK(hrx_status_to_iree(hrx_stream_create(device_, 0, &stream_)));
  }

  void TearDown() override {
    hrx_stream_release(stream_);
    IREE_EXPECT_OK(hrx_status_to_iree(hrx_cpu_shutdown()));
  }

  // CPU device borrowed from the initialized runtime.
  hrx_device_t device_ = nullptr;
  // Stream providing real allocation and transfer ordering.
  hrx_stream_t stream_ = nullptr;
};

TEST_F(CpuBufferTest, NativeAndMappedPointersPreserveImportedSubspanContents) {
  const iree_hal_buffer_usage_t usages[] = {
      IREE_HAL_BUFFER_USAGE_MAPPING_SCOPED |
          IREE_HAL_BUFFER_USAGE_MAPPING_PERSISTENT,
      IREE_HAL_BUFFER_USAGE_MAPPING_SCOPED,
  };
  for (iree_hal_buffer_usage_t usage : usages) {
    alignas(64) uint8_t storage[64];
    for (size_t i = 0; i < sizeof(storage); ++i) {
      storage[i] = i;
    }
    iree_hal_buffer_t* root = nullptr;
    IREE_ASSERT_OK(iree_hal_heap_buffer_wrap(
        iree_hal_buffer_placement_undefined(), IREE_HAL_MEMORY_TYPE_HOST_LOCAL,
        IREE_HAL_MEMORY_ACCESS_ALL, usage, sizeof(storage),
        iree_make_byte_span(storage, sizeof(storage)),
        iree_hal_buffer_release_callback_null(), iree_allocator_system(),
        &root));
    iree_hal_buffer_t* view = nullptr;
    IREE_ASSERT_OK(
        iree_hal_buffer_subspan(root, 13, 23, iree_allocator_system(), &view));
    hrx_buffer_t buffer = nullptr;
    IREE_ASSERT_OK(hrx_buffer_create_from_hal(
        view, device_, HRX_MEMORY_TYPE_HOST_LOCAL, 23, nullptr, &buffer));
    iree_hal_buffer_release(view);
    iree_hal_buffer_release(root);

    void* pointer = nullptr;
    IREE_ASSERT_OK(
        hrx_status_to_iree(hrx_buffer_get_device_ptr(buffer, &pointer)));
    EXPECT_EQ(storage + 13, pointer);
    EXPECT_EQ(usage == IREE_HAL_BUFFER_USAGE_MAPPING_SCOPED, buffer->is_mapped);
    for (size_t i = 0; i < sizeof(storage); ++i) {
      EXPECT_EQ(i, storage[i]);
    }
    void* repeated_pointer = nullptr;
    IREE_ASSERT_OK(hrx_status_to_iree(
        hrx_buffer_get_device_ptr(buffer, &repeated_pointer)));
    EXPECT_EQ(pointer, repeated_pointer);
    hrx_buffer_release(buffer);
    for (size_t i = 0; i < sizeof(storage); ++i) {
      EXPECT_EQ(i, storage[i]);
    }
  }
}

TEST_F(CpuBufferTest, MappingPermissionsAndDiscardProduceExpectedStreamOutput) {
  hrx_buffer_t buffer = nullptr;
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_buffer_allocate(
      stream_, 64, HRX_MEMORY_TYPE_HOST_LOCAL | HRX_MEMORY_TYPE_DEVICE_VISIBLE,
      HRX_BUFFER_USAGE_DEFAULT | HRX_BUFFER_USAGE_MAPPING_SCOPED, &buffer)));
  const uint8_t pattern = 0x5A;
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_stream_fill_buffer(
      stream_, buffer, 0, 64, &pattern, sizeof(pattern))));
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_stream_synchronize(stream_)));

  void* pointer = nullptr;
  IREE_ASSERT_OK(hrx_status_to_iree(
      hrx_buffer_map(buffer, HRX_MAP_READ | HRX_MAP_WRITE | HRX_MAP_MAY_ALIAS,
                     0, 64, &pointer)));
  for (size_t i = 0; i < 64; ++i) {
    EXPECT_EQ(pattern, static_cast<uint8_t*>(pointer)[i]);
  }
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_buffer_unmap(buffer)));

  // DISCARD remains an operation request and implies WRITE in the HRX API.
  IREE_ASSERT_OK(hrx_status_to_iree(
      hrx_buffer_map(buffer, HRX_MAP_DISCARD, 13, 23, &pointer)));
  memset(pointer, 0xA7, 23);
  IREE_ASSERT_OK(hrx_status_to_iree(hrx_buffer_unmap(buffer)));
  uint8_t result[64] = {};
  IREE_ASSERT_OK(hrx_status_to_iree(
      hrx_synchronous_d2h(device_, buffer, 0, result, sizeof(result))));
  for (size_t i = 0; i < sizeof(result); ++i) {
    EXPECT_EQ(i >= 13 && i < 36 ? 0xA7 : pattern, result[i]);
  }
  hrx_buffer_release(buffer);
}

}  // namespace
