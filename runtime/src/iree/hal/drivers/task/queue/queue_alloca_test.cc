// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <array>
#include <thread>

#include "iree/async/frontier_tracker.h"
#include "iree/async/notification.h"
#include "iree/async/operations/scheduling.h"
#include "iree/async/util/proactor_pool.h"
#include "iree/hal/device_group.h"
#include "iree/hal/drivers/task/device.h"
#include "iree/hal/drivers/task/queue/queue.h"
#include "iree/hal/memory/fixed_block_pool.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

#if defined(IREE_PLATFORM_WINDOWS)
#include "iree/async/platform/iocp/api.h"
#else
#include "iree/async/platform/posix/api.h"
#endif

namespace {

class TaskQueueAllocaTest : public ::testing::TestWithParam<iree_host_size_t> {
 protected:
  void SetUp() override {
    iree_task_topology_t topology;
    iree_task_topology_initialize_from_group_count(2, &topology);
    iree_task_executor_options_t executor_options;
    iree_task_executor_options_initialize(&executor_options);
    iree_status_t status = iree_task_executor_create(
        executor_options, &topology, iree_allocator_system(), &executor_);
    iree_task_topology_deinitialize(&topology);
    IREE_ASSERT_OK(status);
    IREE_ASSERT_OK(iree_hal_allocator_create_heap(
        IREE_SV("shared_pool"), iree_allocator_system(),
        iree_allocator_system(), &allocator_));

    auto progress_options = iree_async_proactor_pool_options_default();
    // The test owns polling so it can observe both waits registered on the
    // notification owner without racing the backend's intrusive wait list.
    progress_options.runner = {};
#if defined(IREE_PLATFORM_WINDOWS)
    progress_options.proactor_create = iree_async_proactor_create_iocp;
#else
    progress_options.proactor_create = iree_async_proactor_create_posix;
#endif
    const iree_string_view_t identifiers[] = {IREE_SV("consumer_a"),
                                              IREE_SV("consumer_b")};
    for (iree_host_size_t i = 0; i < devices_.size(); ++i) {
      IREE_ASSERT_OK(iree_async_proactor_pool_create(
          1, /*node_ids=*/nullptr, progress_options, iree_allocator_system(),
          &progress_[i]));
      iree_hal_task_device_params_t device_params;
      iree_hal_task_device_params_initialize(&device_params);
      auto create_params = iree_hal_device_create_params_default();
      create_params.proactor_pool = progress_[i];
      IREE_ASSERT_OK(iree_hal_task_device_create(
          identifiers[i], &device_params, 1, &executor_, 0, nullptr, allocator_,
          &create_params, iree_allocator_system(), &devices_[i]));
      queues_[i] = iree_hal_device_queue(devices_[i], 0, 0);
    }
    ASSERT_NE(reinterpret_cast<iree_hal_task_queue_t*>(queues_[0])->proactor,
              reinterpret_cast<iree_hal_task_queue_t*>(queues_[1])->proactor);

    iree_async_frontier_tracker_t* tracker = nullptr;
    IREE_ASSERT_OK(iree_async_frontier_tracker_create(
        iree_async_frontier_tracker_options_default(), iree_allocator_system(),
        &tracker));
    iree_hal_device_group_builder_t builder;
    iree_hal_device_group_builder_initialize(&builder, tracker);
    iree_async_frontier_tracker_release(tracker);
    for (iree_host_size_t i = 0;
         i < devices_.size() && iree_status_is_ok(status); ++i) {
      status = iree_hal_device_group_builder_add_device(&builder, devices_[i]);
    }
    if (iree_status_is_ok(status)) {
      status = iree_hal_device_group_builder_finalize(
          &builder, iree_allocator_system(), &group_);
    }
    iree_hal_device_group_builder_deinitialize(&builder);
    IREE_ASSERT_OK(status);

    iree_hal_queue_pool_backend_t backend = {};
    IREE_ASSERT_OK(iree_hal_device_query_queue_pool_backend(
        devices_[GetParam()], iree_hal_queue_family(queues_[GetParam()]),
        &backend));
    notification_ = backend.notification;
    IREE_ASSERT_OK(CreatePool(backend, &pool_));
    for (iree_host_size_t i = 0; i < semaphores_.size(); ++i) {
      IREE_ASSERT_OK(iree_hal_semaphore_create(
          devices_[i == 0 ? 0 : i - 1], IREE_HAL_QUEUE_FAMILY_AFFINITY_ANY, 0,
          IREE_HAL_SEMAPHORE_FLAG_DEFAULT, &semaphores_[i]));
    }
  }

  iree_status_t CreatePool(const iree_hal_queue_pool_backend_t& backend,
                           iree_hal_pool_t** out_pool) {
    iree_hal_fixed_block_pool_options_t options = {};
    options.block_allocator_options.block_size = kBlockSize;
    options.block_allocator_options.block_count = 2;
    options.block_allocator_options.frontier_capacity = 2;
    options.asan = backend.asan;
    return iree_hal_fixed_block_pool_create(
        options, backend.slab_provider, backend.notification,
        backend.frontier_tracker, backend.epoch_query, iree_allocator_system(),
        out_pool);
  }

  void TearDown() override {
    for (auto* buffer : initial_buffers_) {
      iree_hal_buffer_release(buffer);
    }
    for (auto* buffer : pending_buffers_) {
      iree_hal_buffer_release(buffer);
    }
    for (auto* semaphore : semaphores_) {
      iree_hal_semaphore_release(semaphore);
    }
    iree_hal_pool_release(pool_);
    iree_hal_device_group_release(group_);
    for (auto* device : devices_) {
      iree_hal_device_release(device);
    }
    for (auto* progress : progress_) {
      iree_async_proactor_pool_release(progress);
    }
    iree_hal_allocator_release(allocator_);
    iree_task_executor_release(executor_);
  }

  void PollOwner() {
    iree_status_t status = iree_async_proactor_poll(
        notification_->proactor, iree_immediate_timeout(), nullptr);
    if (iree_status_is_deadline_exceeded(status)) {
      iree_status_free(status);
    } else {
      IREE_ASSERT_OK(status);
    }
    std::this_thread::yield();
  }

  void Wait(iree_hal_semaphore_t* semaphore, uint64_t value) {
    uint64_t current_value = 0;
    IREE_ASSERT_OK(iree_hal_semaphore_query(semaphore, &current_value));
    while (current_value < value) {
      ASSERT_NO_FATAL_FAILURE(PollOwner());
      IREE_ASSERT_OK(iree_hal_semaphore_query(semaphore, &current_value));
    }
  }

  iree_host_size_t RegisteredWaitCount() {
    // Only this thread polls the owner, so its wait list cannot change while
    // inspected. The other queue's proactor is never polled.
#if defined(IREE_PLATFORM_WINDOWS)
    auto* wait = notification_->platform.iocp.pending_waits;
#else
    auto* wait = notification_->platform.posix.pending_waits;
#endif
    iree_host_size_t count = 0;
    for (; wait;
         wait = reinterpret_cast<iree_async_notification_wait_operation_t*>(
             wait->base.next)) {
      ++count;
    }
    return count;
  }

  static constexpr iree_device_size_t kBlockSize = 512;
  // Workers shared by the two independent Task devices.
  iree_task_executor_t* executor_ = nullptr;
  // Heap allocator retained by both devices.
  iree_hal_allocator_t* allocator_ = nullptr;
  // Distinct caller-driven progress services retained by each device.
  std::array<iree_async_proactor_pool_t*, 2> progress_ = {};
  // Devices retained independently and by the sealed group.
  std::array<iree_hal_device_t*, 2> devices_ = {};
  // Group establishing the devices' shared frontier coordinates.
  iree_hal_device_group_t* group_ = nullptr;
  // Provisioned queues borrowed from the devices.
  std::array<iree_hal_queue_t*, 2> queues_ = {};
  // Finite memory pool shared by both consumers.
  iree_hal_pool_t* pool_ = nullptr;
  // Availability notification borrowed from the selected backend.
  iree_async_notification_t* notification_ = nullptr;
  // Initial allocation and the two consumers' completion timelines.
  std::array<iree_hal_semaphore_t*, 3> semaphores_ = {};
  // Buffers occupying every block until explicit deallocation.
  std::array<iree_hal_buffer_t*, 2> initial_buffers_ = {};
  // Buffers materialized after the owner wakes both consumers.
  std::array<iree_hal_buffer_t*, 2> pending_buffers_ = {};
};

TEST_P(TaskQueueAllocaTest, SharedPoolResumesThroughNotificationOwner) {
  std::array<iree_hal_pool_reservation_request_t, 2> requests = {};
  for (auto& request : requests) {
    request.allocation_size = kBlockSize;
    request.params.type = IREE_HAL_MEMORY_TYPE_OPTIMAL_FOR_DEVICE;
    request.params.access = IREE_HAL_MEMORY_ACCESS_ALL;
    request.params.usage =
        IREE_HAL_BUFFER_USAGE_TRANSFER | IREE_HAL_BUFFER_USAGE_MAPPING_SCOPED;
    request.params.queue_family_affinity =
        iree_hal_make_queue_family_affinity(0);
  }
  uint64_t allocated_value = 1;
  uint64_t filled_value = 2;
  uint64_t released_value = 3;
  const auto no_waits = iree_hal_semaphore_list_empty();
  iree_hal_semaphore_list_t initial_allocated = {1, &semaphores_[0],
                                                 &allocated_value};
  IREE_ASSERT_OK(iree_hal_queue_alloca(queues_[0], no_waits, initial_allocated,
                                       pool_, requests.size(), requests.data(),
                                       initial_buffers_.data()));
  ASSERT_NO_FATAL_FAILURE(Wait(semaphores_[0], allocated_value));

  for (iree_host_size_t i = 0; i < queues_.size(); ++i) {
    iree_hal_semaphore_list_t allocated = {1, &semaphores_[i + 1],
                                           &allocated_value};
    IREE_ASSERT_OK(iree_hal_queue_alloca(queues_[i], no_waits, allocated, pool_,
                                         1, &requests[i],
                                         &pending_buffers_[i]));
  }
  // Explicit registration proves both queues reached exhaustion before any
  // block is returned; a submission-order or sleep-based check would not.
  while (RegisteredWaitCount() != queues_.size()) {
    ASSERT_NO_FATAL_FAILURE(PollOwner());
  }

  iree_hal_semaphore_list_t initial_released = {1, &semaphores_[0],
                                                &released_value};
  IREE_ASSERT_OK(iree_hal_queue_dealloca(
      queues_[0], initial_allocated, initial_released, initial_buffers_.size(),
      initial_buffers_.data()));
  ASSERT_NO_FATAL_FAILURE(Wait(semaphores_[0], released_value));
  for (iree_host_size_t i = 0; i < queues_.size(); ++i) {
    ASSERT_NO_FATAL_FAILURE(Wait(semaphores_[i + 1], allocated_value));
  }

  for (iree_host_size_t i = 0; i < queues_.size(); ++i) {
    iree_hal_semaphore_list_t allocated = {1, &semaphores_[i + 1],
                                           &allocated_value};
    iree_hal_semaphore_list_t filled = {1, &semaphores_[i + 1], &filled_value};
    iree_hal_semaphore_list_t released = {1, &semaphores_[i + 1],
                                          &released_value};
    const uint32_t pattern = 0xBADC0000u + i;
    IREE_ASSERT_OK(iree_hal_queue_fill(
        queues_[i], allocated, filled, pending_buffers_[i], 0, kBlockSize,
        &pattern, sizeof(pattern), IREE_HAL_FILL_FLAG_NONE));
    ASSERT_NO_FATAL_FAILURE(Wait(semaphores_[i + 1], filled_value));
    iree_hal_buffer_mapping_t mapping = {};
    IREE_ASSERT_OK(iree_hal_buffer_map_range(
        pending_buffers_[i], IREE_HAL_MAPPING_MODE_SCOPED,
        IREE_HAL_MEMORY_ACCESS_READ, 0, kBlockSize, &mapping));
    const auto* output =
        reinterpret_cast<const uint32_t*>(mapping.contents.data);
    for (iree_host_size_t word = 0; word < kBlockSize / sizeof(pattern);
         ++word) {
      EXPECT_EQ(output[word], pattern);
    }
    IREE_ASSERT_OK(iree_hal_buffer_unmap_range(&mapping));
    IREE_ASSERT_OK(iree_hal_queue_dealloca(queues_[i], filled, released, 1,
                                           &pending_buffers_[i]));
    ASSERT_NO_FATAL_FAILURE(Wait(semaphores_[i + 1], released_value));
  }
}

TEST_P(TaskQueueAllocaTest, CompletedDeallocationIsImmediatelyReusable) {
  // A completion probe is an optional reuse optimization. A completed Task
  // deallocation must return usable memory even when that probe is absent.
  iree_hal_queue_pool_backend_t backend = {};
  IREE_ASSERT_OK(iree_hal_device_query_queue_pool_backend(
      devices_[GetParam()], iree_hal_queue_family(queues_[GetParam()]),
      &backend));
  backend.epoch_query = iree_hal_pool_epoch_query_null();
  iree_hal_pool_release(pool_);
  pool_ = nullptr;
  IREE_ASSERT_OK(CreatePool(backend, &pool_));

  std::array<iree_hal_pool_reservation_request_t, 2> requests = {};
  for (auto& request : requests) {
    request.allocation_size = kBlockSize;
    request.params.type = IREE_HAL_MEMORY_TYPE_OPTIMAL_FOR_DEVICE;
    request.params.access = IREE_HAL_MEMORY_ACCESS_ALL;
    request.params.usage =
        IREE_HAL_BUFFER_USAGE_TRANSFER | IREE_HAL_BUFFER_USAGE_MAPPING_SCOPED;
    request.params.queue_family_affinity =
        iree_hal_make_queue_family_affinity(0);
  }
  uint64_t allocated_value = 1;
  uint64_t released_value = 2;
  const auto no_waits = iree_hal_semaphore_list_empty();
  iree_hal_semaphore_list_t allocated = {1, &semaphores_[0], &allocated_value};
  iree_hal_semaphore_list_t released = {1, &semaphores_[0], &released_value};
  IREE_ASSERT_OK(iree_hal_queue_alloca(queues_[0], no_waits, allocated, pool_,
                                       requests.size(), requests.data(),
                                       initial_buffers_.data()));
  IREE_ASSERT_OK(iree_hal_queue_dealloca(queues_[0], allocated, released,
                                         initial_buffers_.size(),
                                         initial_buffers_.data()));
  ASSERT_NO_FATAL_FAILURE(Wait(semaphores_[0], released_value));

  uint64_t filled_value = 0;
  for (auto*& buffer : pending_buffers_) {
    IREE_ASSERT_OK(
        iree_hal_pool_allocate_buffer(pool_, requests[0].params, kBlockSize,
                                      iree_immediate_timeout(), &buffer));
    const uint32_t pattern = 0x1234ABCDu;
    ++filled_value;
    iree_hal_semaphore_list_t filled = {1, &semaphores_[1], &filled_value};
    IREE_ASSERT_OK(iree_hal_queue_fill(queues_[1], no_waits, filled, buffer, 0,
                                       kBlockSize, &pattern, sizeof(pattern),
                                       IREE_HAL_FILL_FLAG_NONE));
    ASSERT_NO_FATAL_FAILURE(Wait(semaphores_[1], filled_value));
    iree_hal_buffer_mapping_t mapping = {};
    IREE_ASSERT_OK(iree_hal_buffer_map_range(
        buffer, IREE_HAL_MAPPING_MODE_SCOPED, IREE_HAL_MEMORY_ACCESS_READ, 0,
        kBlockSize, &mapping));
    const auto* output =
        reinterpret_cast<const uint32_t*>(mapping.contents.data);
    for (iree_host_size_t word = 0; word < kBlockSize / sizeof(pattern);
         ++word) {
      EXPECT_EQ(pattern, output[word]);
    }
    IREE_ASSERT_OK(iree_hal_buffer_unmap_range(&mapping));
  }
}

INSTANTIATE_TEST_SUITE_P(NotificationOwners, TaskQueueAllocaTest,
                         ::testing::Values(0, 1));

}  // namespace
