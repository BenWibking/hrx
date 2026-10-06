// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "iree/async/frontier_tracker.h"
#include "iree/async/notification.h"
#include "iree/async/proactor.h"
#include "iree/async/proactor_platform.h"
#include "iree/hal/api.h"
#include "iree/hal/memory/cpu_slab_provider.h"
#include "iree/hal/memory/tlsf_pool.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

// Gates one underlying allocation or release. All successful operations use the
// real system allocator, so the tests exercise actual backing and mapped bytes.
class GatedAllocator {
 public:
  iree_allocator_t allocator() { return {this, Control}; }

  void Arm(iree_allocator_command_t command) {
    std::lock_guard<std::mutex> lock(mutex_);
    command_ = command;
    armed_ = true;
    entered_ = false;
    resumed_ = false;
  }

  void AwaitEntry() {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [&] { return entered_; });
  }

  void Resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    resumed_ = true;
    condition_.notify_all();
  }

  void FailNextAllocation() { FailAfterAllocations(0); }

  void FailAfterAllocations(int count) {
    allocations_until_failure_.store(count);
  }

  size_t live_allocations() const { return live_allocations_.load(); }
  size_t allocation_calls() const { return allocation_calls_.load(); }

 private:
  static iree_status_t Control(void* self, iree_allocator_command_t command,
                               const void* params, void** inout_ptr) {
    auto* allocator = static_cast<GatedAllocator*>(self);
    {
      std::unique_lock<std::mutex> lock(allocator->mutex_);
      if (allocator->armed_ && command == allocator->command_) {
        allocator->armed_ = false;
        allocator->entered_ = true;
        allocator->condition_.notify_all();
        allocator->condition_.wait(lock, [&] { return allocator->resumed_; });
      }
    }
    if (command != IREE_ALLOCATOR_COMMAND_FREE) {
      allocator->allocation_calls_.fetch_add(1);
    }
    if (command != IREE_ALLOCATOR_COMMAND_FREE) {
      int remaining = allocator->allocations_until_failure_.load();
      while (remaining >= 0 &&
             !allocator->allocations_until_failure_.compare_exchange_weak(
                 remaining, remaining - 1)) {
      }
      if (remaining == 0) {
        return iree_make_status(IREE_STATUS_RESOURCE_EXHAUSTED,
                                "allocation unavailable");
      }
    }
    const bool had_allocation = *inout_ptr != nullptr;
    iree_allocator_t system_allocator = iree_allocator_system();
    iree_status_t status =
        system_allocator.ctl(system_allocator.self, command, params, inout_ptr);
    if (iree_status_is_ok(status)) {
      if (command == IREE_ALLOCATOR_COMMAND_FREE) {
        if (had_allocation) {
          allocator->live_allocations_.fetch_sub(1);
        }
      } else if (command != IREE_ALLOCATOR_COMMAND_REALLOC || !had_allocation) {
        allocator->live_allocations_.fetch_add(1);
      }
    }
    return status;
  }

  // Protects the selected operation's admission and resumption.
  std::mutex mutex_;
  // Explicit handshake with the thread performing the native operation.
  std::condition_variable condition_;
  // Native command selected for the next handshake.
  iree_allocator_command_t command_ = IREE_ALLOCATOR_COMMAND_MALLOC;
  // Whether the next matching command should stop at the gate.
  bool armed_ = false;
  // Whether a matching command has entered the gate.
  bool entered_ = false;
  // Whether the test has released the selected command.
  bool resumed_ = false;
  // Successful allocations before one failure; -1 disables failure injection.
  std::atomic<int> allocations_until_failure_{-1};
  // Allocation-like calls, including unsuccessful attempts.
  std::atomic<size_t> allocation_calls_{0};
  // Live native backing, provider, and materialized-buffer allocations.
  std::atomic<size_t> live_allocations_{0};
};

static iree_hal_pool_reservation_request_t Request(iree_device_size_t length) {
  iree_hal_pool_reservation_request_t request = {};
  request.params.type = IREE_HAL_MEMORY_TYPE_HOST_LOCAL;
  request.params.access = IREE_HAL_MEMORY_ACCESS_ALL;
  request.params.usage =
      IREE_HAL_BUFFER_USAGE_TRANSFER | IREE_HAL_BUFFER_USAGE_MAPPING_SCOPED;
  request.params.min_alignment = 16;
  request.allocation_size = length;
  return request;
}

class TLSFPoolConcurrencyTest : public ::testing::Test {
 protected:
  static bool QueryEpoch(void* user_data, iree_async_axis_t axis,
                         uint64_t epoch) {
    auto* self = static_cast<TLSFPoolConcurrencyTest*>(user_data);
    self->epoch_queries_.fetch_add(1);
    return iree_async_frontier_tracker_query_epoch(self->tracker_, axis, epoch);
  }

  void SetUp() override {
    IREE_ASSERT_OK(iree_async_proactor_create_platform(
        iree_async_proactor_options_default(), iree_allocator_system(),
        &proactor_));
    IREE_ASSERT_OK(iree_async_notification_create(
        proactor_, IREE_ASYNC_NOTIFICATION_FLAG_NONE, &notification_));
    IREE_ASSERT_OK(iree_async_frontier_tracker_create(
        iree_async_frontier_tracker_options_default(), iree_allocator_system(),
        &tracker_));
    IREE_ASSERT_OK(iree_async_frontier_tracker_register_axis(
        tracker_, iree_async_axis_make_queue(1, 0, 0, 0, 0), nullptr));
    IREE_ASSERT_OK(iree_hal_cpu_slab_provider_create(
        native_allocator_.allocator(), &provider_));
    iree_hal_tlsf_pool_options_t options = {};
    options.tlsf_options.range_length = 4096;
    options.tlsf_options.alignment = 16;
    options.tlsf_options.initial_block_capacity = 4;
    options.tlsf_options.frontier_capacity = 2;
    IREE_ASSERT_OK(iree_hal_tlsf_pool_create(
        options, provider_, notification_, tracker_, {QueryEpoch, this},
        metadata_allocator_.allocator(), &pool_));
  }

  void TearDown() override {
    iree_hal_pool_release(pool_);
    EXPECT_EQ(metadata_allocator_.live_allocations(), 0u);
    iree_hal_slab_provider_release(provider_);
    EXPECT_EQ(native_allocator_.live_allocations(), 0u);
    iree_async_notification_release(notification_);
    iree_async_frontier_tracker_release(tracker_);
    iree_async_proactor_release(proactor_);
  }

  iree_status_t Acquire(iree_device_size_t length,
                        iree_hal_pool_reservation_t* out_reservation) {
    const auto request = Request(length);
    iree_hal_pool_acquire_info_t info;
    iree_hal_pool_acquire_result_t result;
    iree_status_t status = iree_hal_pool_acquire_reservations(
        pool_, 1, &request, nullptr, IREE_HAL_POOL_RESERVE_FLAG_NONE,
        out_reservation, &info, &result);
    if (iree_status_is_ok(status)) {
      EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
    }
    return status;
  }

  // Populate two or four records without growing the offset allocator.
  // Later tests gate metadata preparation after a provisional batch prefix.
  void WarmReleaseRecords(size_t count = 4) {
    iree_hal_pool_reservation_t reservations[4];
    for (size_t i = 0; i < count; ++i) {
      IREE_ASSERT_OK(Acquire(4096 / count, &reservations[i]));
    }
    iree_hal_pool_release_reservations(pool_, count, reservations, nullptr);
  }

  // Uses only already-published capacity and verifies its contents through a
  // real native buffer. This call must finish before the native gate resumes.
  void CheckExistingCapacity(iree_device_size_t length) {
    const auto request = Request(length);
    iree_hal_pool_reservation_t reservation;
    iree_hal_pool_acquire_info_t info;
    iree_hal_pool_acquire_result_t result;
    IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
        pool_, 1, &request, nullptr, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
        &reservation, &info, &result));
    ASSERT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
    CheckReservationContents(length, reservation);
    iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
  }

  void CheckReservationContents(
      iree_device_size_t length,
      const iree_hal_pool_reservation_t& reservation) {
    const auto request = Request(length);
    iree_hal_buffer_t* buffer = nullptr;
    iree_status_t status = iree_hal_pool_materialize_reservations(
        pool_, 1, &request, &reservation, IREE_HAL_POOL_MATERIALIZE_FLAG_NONE,
        &buffer);
    if (iree_status_is_ok(status)) {
      const uint8_t pattern = 0x6B;
      status = iree_hal_buffer_map_fill(buffer, 0, length, &pattern,
                                        sizeof(pattern));
      uint8_t actual[4096] = {};
      if (iree_status_is_ok(status)) {
        status = iree_hal_buffer_map_read(buffer, 0, actual, length);
      }
      if (iree_status_is_ok(status)) {
        for (iree_device_size_t i = 0; i < length; ++i) {
          EXPECT_EQ(actual[i], pattern);
        }
      }
    }
    IREE_EXPECT_OK(status);
    iree_hal_buffer_release(buffer);
  }

  // Owns the real native allocation dependency and its handshake state.
  GatedAllocator native_allocator_;
  // Counts the TLSF pool's actual host metadata allocations.
  GatedAllocator metadata_allocator_;
  // Counts real tracker queries made while checking candidate prerequisites.
  std::atomic<size_t> epoch_queries_{0};
  // Progress owner retained for the fixture's notification.
  iree_async_proactor_t* proactor_ = nullptr;
  // Capacity publication observed by the tests.
  iree_async_notification_t* notification_ = nullptr;
  // Shared completion tracker captured by prepared buffers.
  iree_async_frontier_tracker_t* tracker_ = nullptr;
  // Native CPU source using the gated allocator.
  iree_hal_slab_provider_t* provider_ = nullptr;
  // TLSF subject under test, with counted system-allocated host metadata.
  iree_hal_pool_t* pool_ = nullptr;
};

TEST_F(TLSFPoolConcurrencyTest, NativeGrowthRestoresPrefixBeforeUnlocking) {
  iree_hal_pool_reservation_t held;
  IREE_ASSERT_OK(Acquire(64, &held));
  const iree_hal_pool_reservation_request_t requests[] = {
      Request(512),
      Request(4096),
  };
  iree_hal_pool_reservation_t reservations[2] = {};
  iree_hal_pool_acquire_info_t infos[2];
  iree_hal_pool_acquire_result_t result = IREE_HAL_POOL_ACQUIRE_NONE;
  native_allocator_.Arm(IREE_ALLOCATOR_COMMAND_CALLOC);
  std::thread growing([&] {
    IREE_EXPECT_OK(iree_hal_pool_acquire_reservations(
        pool_, 2, requests, nullptr, IREE_HAL_POOL_RESERVE_FLAG_NONE,
        reservations, infos, &result));
  });
  native_allocator_.AwaitEntry();
  // This needs the entire free extent, including the batch's restored prefix.
  CheckExistingCapacity(4032);
  native_allocator_.Resume();
  growing.join();
  EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
  for (const auto& reservation : reservations) {
    if (reservation.block_handle) {
      iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
    }
  }
  iree_hal_pool_release_reservations(pool_, 1, &held, nullptr);
}

TEST_F(TLSFPoolConcurrencyTest, ReusesCapacityReturnedDuringNativePreparation) {
  iree_hal_pool_reservation_t held;
  IREE_ASSERT_OK(Acquire(4096, &held));
  iree_hal_pool_reservation_t reservation = {};
  native_allocator_.Arm(IREE_ALLOCATOR_COMMAND_CALLOC);
  std::thread growing([&] { IREE_EXPECT_OK(Acquire(4096, &reservation)); });
  native_allocator_.AwaitEntry();
  iree_hal_pool_release_reservations(pool_, 1, &held, nullptr);
  native_allocator_.Resume();
  growing.join();
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 1u);
  EXPECT_EQ(stats.bytes_committed, 4096u);
  // One provider object and one native slab remain. Unneeded prepared backing
  // was returned without entering the pool's capacity inventory.
  EXPECT_EQ(native_allocator_.live_allocations(), 2u);
  if (reservation.block_handle) {
    iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
  }
}

TEST_F(TLSFPoolConcurrencyTest, NativeRetirementDoesNotBlockOtherSlabs) {
  iree_hal_pool_reservation_t held;
  IREE_ASSERT_OK(Acquire(512, &held));
  iree_hal_pool_reservation_t unused;
  IREE_ASSERT_OK(Acquire(4096, &unused));
  iree_hal_pool_release_reservations(pool_, 1, &unused, nullptr);
  native_allocator_.Arm(IREE_ALLOCATOR_COMMAND_FREE);
  std::thread trimming(
      [&] { iree_hal_pool_trim(pool_, IREE_HAL_POOL_TRIM_FLAG_ALL, 0); });
  native_allocator_.AwaitEntry();
  CheckExistingCapacity(3584);
  native_allocator_.Resume();
  trimming.join();
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 1u);
  EXPECT_EQ(stats.reservation_count, 1u);
  EXPECT_EQ(stats.bytes_reserved, 512u);
  iree_hal_pool_release_reservations(pool_, 1, &held, nullptr);
}

TEST_F(TLSFPoolConcurrencyTest, MetadataGrowthRestoresPrefixBeforeUnlocking) {
  WarmReleaseRecords();
  iree_hal_pool_reservation_t held[2];
  IREE_ASSERT_OK(Acquire(64, &held[0]));
  IREE_ASSERT_OK(Acquire(64, &held[1]));
  const iree_hal_pool_reservation_request_t requests[] = {Request(512),
                                                          Request(512)};
  iree_hal_pool_reservation_t reservations[2] = {};
  iree_hal_pool_acquire_info_t infos[2];
  iree_hal_pool_acquire_result_t result = IREE_HAL_POOL_ACQUIRE_NONE;
  metadata_allocator_.Arm(IREE_ALLOCATOR_COMMAND_CALLOC);
  std::thread growing([&] {
    IREE_EXPECT_OK(iree_hal_pool_acquire_reservations(
        pool_, 2, requests, nullptr, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
        reservations, infos, &result));
  });
  metadata_allocator_.AwaitEntry();
  // The first 512-byte claim has rolled back. Use the entire remaining extent
  // in this same slab while split-node preparation is still paused.
  CheckExistingCapacity(3968);
  metadata_allocator_.Resume();
  growing.join();
  EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 1u);
  EXPECT_EQ(stats.reservation_count, 4u);
  EXPECT_EQ(stats.bytes_reserved, 1152u);
  for (const auto& reservation : reservations) {
    if (reservation.block_handle) {
      iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
    }
  }
  iree_hal_pool_release_reservations(pool_, 2, held, nullptr);
}

TEST_F(TLSFPoolConcurrencyTest, TrimRetiresSlabDuringMetadataPreparation) {
  iree_hal_pool_reservation_t held[3];
  for (auto& reservation : held) {
    IREE_ASSERT_OK(Acquire(64, &reservation));
  }
  iree_hal_pool_reservation_t reservation = {};
  metadata_allocator_.Arm(IREE_ALLOCATOR_COMMAND_CALLOC);
  std::thread growing([&] { IREE_EXPECT_OK(Acquire(512, &reservation)); });
  metadata_allocator_.AwaitEntry();
  iree_hal_pool_release_reservations(pool_, 3, held, nullptr);
  iree_hal_pool_trim(pool_, IREE_HAL_POOL_TRIM_FLAG_ALL, 0);
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 0u);
  EXPECT_EQ(stats.bytes_committed, 0u);
  EXPECT_EQ(native_allocator_.live_allocations(), 1u);
  metadata_allocator_.Resume();
  growing.join();
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 1u);
  EXPECT_EQ(stats.bytes_committed, 4096u);
  EXPECT_EQ(stats.reservation_count, 1u);
  if (reservation.block_handle) {
    iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
  }
  CheckExistingCapacity(4096);
}

TEST_F(TLSFPoolConcurrencyTest, DiscardsUnneededPreparedMetadata) {
  iree_hal_pool_reservation_t held[3];
  for (auto& reservation : held) {
    IREE_ASSERT_OK(Acquire(64, &reservation));
  }
  const size_t live_allocations = metadata_allocator_.live_allocations();
  const size_t allocation_calls = metadata_allocator_.allocation_calls();
  iree_hal_pool_reservation_t reservation = {};
  metadata_allocator_.Arm(IREE_ALLOCATOR_COMMAND_CALLOC);
  std::thread growing([&] { IREE_EXPECT_OK(Acquire(64, &reservation)); });
  metadata_allocator_.AwaitEntry();
  // This exact-sized free block needs neither a split nor a new release record.
  iree_hal_pool_release_reservations(pool_, 1, &held[0], nullptr);
  metadata_allocator_.Resume();
  growing.join();
  EXPECT_EQ(reservation.offset, held[0].offset);
  EXPECT_EQ(metadata_allocator_.live_allocations(), live_allocations);
  // Candidate selection captured both shortages. The prepared segment and
  // release record are now both unnecessary, and neither remains retained.
  EXPECT_EQ(metadata_allocator_.allocation_calls() - allocation_calls, 2u);
  if (reservation.block_handle) {
    iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
  }
  iree_hal_pool_release_reservations(pool_, 2, &held[1], nullptr);
}

TEST_F(TLSFPoolConcurrencyTest, ConcurrentMetadataGrowthMakesProgress) {
  iree_hal_pool_reservation_t held[3];
  for (auto& reservation : held) {
    IREE_ASSERT_OK(Acquire(64, &reservation));
  }
  iree_hal_pool_reservation_t reservation = {};
  metadata_allocator_.Arm(IREE_ALLOCATOR_COMMAND_CALLOC);
  std::thread growing([&] { IREE_EXPECT_OK(Acquire(64, &reservation)); });
  metadata_allocator_.AwaitEntry();
  // A competing split prepares and publishes its own metadata, writes real
  // bytes, and releases them before the first preparation resumes.
  CheckExistingCapacity(32);
  metadata_allocator_.Resume();
  growing.join();
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 1u);
  EXPECT_EQ(stats.reservation_count, 4u);
  EXPECT_EQ(stats.bytes_reserved, 256u);
  if (reservation.block_handle) {
    iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
  }
  iree_hal_pool_release_reservations(pool_, 3, held, nullptr);
}

TEST_F(TLSFPoolConcurrencyTest, FailedMetadataGrowthPreservesExactHistory) {
  WarmReleaseRecords();
  iree_hal_pool_reservation_t seed;
  IREE_ASSERT_OK(Acquire(4096, &seed));
  alignas(16) uint8_t storage[sizeof(iree_async_frontier_t) +
                              sizeof(iree_async_frontier_entry_t)] = {};
  auto* death = reinterpret_cast<iree_async_frontier_t*>(storage);
  iree_async_frontier_initialize(death, 1);
  death->entries[0] = {iree_async_axis_make_queue(1, 0, 0, 0, 0), 7};
  iree_hal_pool_release_reservations(pool_, 1, &seed, death);
  const iree_hal_pool_reservation_request_t held_requests[] = {Request(64),
                                                               Request(64)};
  iree_hal_pool_reservation_t held[2];
  iree_hal_pool_acquire_info_t infos[2];
  iree_hal_pool_acquire_result_t result;
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool_, 2, held_requests, death,
      IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH, held, infos, &result));
  ASSERT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK);
  const iree_hal_pool_reservation_request_t requests[] = {Request(512),
                                                          Request(512)};
  iree_hal_pool_reservation_t reservations[2];
  memset(reservations, 0xA5, sizeof(reservations));
  iree_hal_pool_reservation_t originals[2];
  memcpy(originals, reservations, sizeof(originals));
  const uint32_t token = iree_async_notification_begin_observe(notification_);
  metadata_allocator_.FailNextAllocation();
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_RESOURCE_EXHAUSTED,
      iree_hal_pool_acquire_reservations(
          pool_, 2, requests, death, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
          reservations, infos, &result));
  EXPECT_EQ(memcmp(reservations, originals, sizeof(originals)), 0);
  EXPECT_FALSE(iree_async_notification_wait_for_token(
      notification_, token, iree_immediate_timeout()));
  iree_async_notification_end_observe(notification_);
  const auto request = Request(3968);
  iree_hal_pool_reservation_t remaining;
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool_, 1, &request, death, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
      &remaining, infos, &result));
  EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK);
  ASSERT_NE(infos[0].reuse_frontier, nullptr);
  EXPECT_EQ(infos[0].reuse_frontier->entry_count, 1u);
  EXPECT_EQ(infos[0].reuse_frontier->entries[0].epoch, 7u);
  EXPECT_EQ(remaining.offset, 128u);
  iree_hal_pool_release_reservations(pool_, 1, &remaining, death);
  iree_hal_pool_release_reservations(pool_, 2, held, death);
}

TEST_F(TLSFPoolConcurrencyTest,
       ReleaseRecordGrowthRestoresPrefixBeforeUnlocking) {
  WarmReleaseRecords(2);
  const iree_hal_pool_reservation_request_t requests[] = {
      Request(512), Request(512), Request(512)};
  iree_hal_pool_reservation_t reservations[3] = {};
  iree_hal_pool_acquire_info_t infos[3];
  iree_hal_pool_acquire_result_t result = IREE_HAL_POOL_ACQUIRE_NONE;
  metadata_allocator_.Arm(IREE_ALLOCATOR_COMMAND_CALLOC);
  std::thread growing([&] {
    IREE_EXPECT_OK(iree_hal_pool_acquire_reservations(
        pool_, 3, requests, nullptr, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
        reservations, infos, &result));
  });
  metadata_allocator_.AwaitEntry();
  // Split metadata is sufficient. The first two claims must be restored while
  // the third reservation's release record is being allocated.
  CheckExistingCapacity(4096);
  metadata_allocator_.Resume();
  growing.join();
  EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 1u);
  EXPECT_EQ(stats.reservation_count, 3u);
  EXPECT_EQ(stats.bytes_reserved, 1536u);
  for (const auto& reservation : reservations) {
    if (reservation.block_handle) {
      CheckReservationContents(512, reservation);
      iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
    }
  }
}

TEST_F(TLSFPoolConcurrencyTest, ReturnedReleaseRecordSupersedesPreparation) {
  iree_hal_pool_reservation_t held;
  IREE_ASSERT_OK(Acquire(64, &held));
  const size_t live_allocations = metadata_allocator_.live_allocations();
  const size_t allocation_calls = metadata_allocator_.allocation_calls();
  iree_hal_pool_reservation_t reservation = {};
  metadata_allocator_.Arm(IREE_ALLOCATOR_COMMAND_CALLOC);
  std::thread growing([&] { IREE_EXPECT_OK(Acquire(64, &reservation)); });
  metadata_allocator_.AwaitEntry();
  iree_hal_pool_release_reservations(pool_, 1, &held, nullptr);
  metadata_allocator_.Resume();
  growing.join();
  EXPECT_EQ(reservation.block_handle, held.block_handle);
  EXPECT_EQ(metadata_allocator_.live_allocations(), live_allocations);
  EXPECT_EQ(metadata_allocator_.allocation_calls() - allocation_calls, 1u);
  if (reservation.block_handle) {
    CheckReservationContents(64, reservation);
    iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
  }
}

TEST_F(TLSFPoolConcurrencyTest, FailedReleaseRecordGrowthPreservesOutputs) {
  WarmReleaseRecords(2);
  const iree_hal_pool_reservation_request_t requests[] = {
      Request(512), Request(512), Request(512)};
  iree_hal_pool_reservation_t reservations[3];
  memset(reservations, 0xA5, sizeof(reservations));
  iree_hal_pool_reservation_t originals[3];
  memcpy(originals, reservations, sizeof(originals));
  iree_hal_pool_acquire_info_t infos[3];
  iree_hal_pool_acquire_result_t result;
  const uint32_t token = iree_async_notification_begin_observe(notification_);
  metadata_allocator_.FailNextAllocation();
  IREE_EXPECT_STATUS_IS(IREE_STATUS_RESOURCE_EXHAUSTED,
                        iree_hal_pool_acquire_reservations(
                            pool_, 3, requests, nullptr,
                            IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
                            reservations, infos, &result));
  EXPECT_EQ(memcmp(reservations, originals, sizeof(originals)), 0);
  EXPECT_FALSE(iree_async_notification_wait_for_token(
      notification_, token, iree_immediate_timeout()));
  iree_async_notification_end_observe(notification_);
  CheckExistingCapacity(4096);
}

TEST_F(TLSFPoolConcurrencyTest,
       PartialReleasePreparationFailureReturnsStorage) {
  const size_t live_allocations = metadata_allocator_.live_allocations();
  const iree_hal_pool_reservation_request_t requests[] = {
      Request(512), Request(512), Request(512)};
  iree_hal_pool_reservation_t reservations[3];
  memset(reservations, 0xA5, sizeof(reservations));
  iree_hal_pool_reservation_t originals[3];
  memcpy(originals, reservations, sizeof(originals));
  iree_hal_pool_acquire_info_t infos[3];
  iree_hal_pool_acquire_result_t result;
  metadata_allocator_.FailAfterAllocations(1);
  IREE_EXPECT_STATUS_IS(IREE_STATUS_RESOURCE_EXHAUSTED,
                        iree_hal_pool_acquire_reservations(
                            pool_, 3, requests, nullptr,
                            IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
                            reservations, infos, &result));
  EXPECT_EQ(memcmp(reservations, originals, sizeof(originals)), 0);
  EXPECT_EQ(metadata_allocator_.live_allocations(), live_allocations);
  CheckExistingCapacity(4096);
}

TEST_F(TLSFPoolConcurrencyTest, TrimPreservesLiveSlabsAndRetainedFloor) {
  constexpr size_t kCount = 12;
  iree_hal_pool_reservation_t reservations[kCount];
  for (auto& reservation : reservations) {
    IREE_ASSERT_OK(Acquire(4096, &reservation));
  }
  // Retire the head, tail, and interleaved interior slabs while five remain
  // live.
  for (size_t i = 0; i < kCount; ++i) {
    if (i % 2 == 0 || i == kCount - 1) {
      iree_hal_pool_release_reservations(pool_, 1, &reservations[i], nullptr);
    }
  }
  iree_hal_pool_trim(pool_, IREE_HAL_POOL_TRIM_FLAG_ALL, 0);
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 5u);
  EXPECT_EQ(stats.bytes_committed, 5u * 4096u);
  for (size_t i = 1; i < kCount - 1; i += 2) {
    CheckReservationContents(4096, reservations[i]);
  }
  // Append after the new tail, then release all ranges and retain two slabs.
  iree_hal_pool_reservation_t appended;
  IREE_ASSERT_OK(Acquire(4096, &appended));
  CheckReservationContents(4096, appended);
  iree_hal_pool_release_reservations(pool_, 1, &appended, nullptr);
  for (size_t i = 1; i < kCount - 1; i += 2) {
    iree_hal_pool_release_reservations(pool_, 1, &reservations[i], nullptr);
  }
  iree_hal_pool_trim(pool_, IREE_HAL_POOL_TRIM_FLAG_EXCESS, 2 * 4096);
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 2u);
  EXPECT_EQ(stats.bytes_committed, 2u * 4096u);
  EXPECT_EQ(stats.reservation_count, 0u);
  CheckExistingCapacity(4096);
}

TEST_F(TLSFPoolConcurrencyTest, FailedNativeGrowthPreservesCapacityAndOutputs) {
  const iree_hal_pool_reservation_request_t requests[] = {
      Request(3072),
      Request(3072),
  };
  iree_hal_pool_reservation_t reservations[2];
  memset(reservations, 0xA5, sizeof(reservations));
  iree_hal_pool_reservation_t originals[2];
  memcpy(originals, reservations, sizeof(originals));
  iree_hal_pool_acquire_info_t infos[2];
  iree_hal_pool_acquire_result_t result;
  const uint32_t token = iree_async_notification_begin_observe(notification_);
  native_allocator_.FailNextAllocation();
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_RESOURCE_EXHAUSTED,
      iree_hal_pool_acquire_reservations(pool_, 2, requests, nullptr,
                                         IREE_HAL_POOL_RESERVE_FLAG_NONE,
                                         reservations, infos, &result));
  EXPECT_EQ(memcmp(reservations, originals, sizeof(originals)), 0);
  EXPECT_FALSE(iree_async_notification_wait_for_token(
      notification_, token, iree_immediate_timeout()));
  iree_async_notification_end_observe(notification_);
  CheckExistingCapacity(4096);
}

TEST_F(TLSFPoolConcurrencyTest, RollbackDoesNotPublishCapacityProgress) {
  const iree_hal_pool_reservation_request_t requests[] = {
      Request(3072),
      Request(3072),
  };
  iree_hal_pool_reservation_t reservations[2];
  iree_hal_pool_acquire_info_t infos[2];
  iree_hal_pool_acquire_result_t result;
  const uint32_t token = iree_async_notification_begin_observe(notification_);
  for (int i = 0; i < 3; ++i) {
    IREE_EXPECT_OK(iree_hal_pool_acquire_reservations(
        pool_, 2, requests, nullptr, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
        reservations, infos, &result));
    EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_EXHAUSTED);
    EXPECT_FALSE(iree_async_notification_wait_for_token(
        notification_, token, iree_immediate_timeout()));
  }
  iree_async_notification_end_observe(notification_);
  CheckExistingCapacity(4096);
}

TEST_F(TLSFPoolConcurrencyTest, GrowthPublishesSpareCapacity) {
  iree_hal_pool_reservation_t held;
  IREE_ASSERT_OK(Acquire(4096, &held));
  const uint32_t token = iree_async_notification_begin_observe(notification_);
  iree_hal_pool_reservation_t reservation = {};
  IREE_EXPECT_OK(Acquire(512, &reservation));
  EXPECT_TRUE(iree_async_notification_wait_for_token(notification_, token,
                                                     iree_immediate_timeout()));
  iree_async_notification_end_observe(notification_);
  CheckExistingCapacity(3584);
  iree_hal_pool_release_reservations(pool_, 1, &held, nullptr);
  if (reservation.block_handle) {
    iree_hal_pool_release_reservations(pool_, 1, &reservation, nullptr);
  }
}

TEST_F(TLSFPoolConcurrencyTest, GrowthBeforeBudgetFailurePublishesOnlyOnce) {
  iree_hal_pool_release(pool_);
  pool_ = nullptr;
  iree_hal_tlsf_pool_options_t options = {};
  options.tlsf_options.range_length = 4096;
  options.budget_limit = 6144;
  IREE_ASSERT_OK(iree_hal_tlsf_pool_create(
      options, provider_, notification_, tracker_, {QueryEpoch, this},
      metadata_allocator_.allocator(), &pool_));
  const iree_hal_pool_reservation_request_t requests[] = {
      Request(3072),
      Request(3072),
      Request(16),
  };
  iree_hal_pool_reservation_t reservations[3];
  iree_hal_pool_acquire_info_t infos[3];
  iree_hal_pool_acquire_result_t result;
  for (int i = 0; i < 2; ++i) {
    const uint32_t token = iree_async_notification_begin_observe(notification_);
    IREE_EXPECT_OK(iree_hal_pool_acquire_reservations(
        pool_, 3, requests, nullptr, IREE_HAL_POOL_RESERVE_FLAG_NONE,
        reservations, infos, &result));
    EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_OVER_BUDGET);
    EXPECT_EQ(iree_async_notification_wait_for_token(notification_, token,
                                                     iree_immediate_timeout()),
              i == 0);
    iree_async_notification_end_observe(notification_);
  }
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 2u);
  EXPECT_EQ(stats.bytes_reserved, 0u);
  EXPECT_EQ(stats.reservation_count, 0u);
}

TEST_F(TLSFPoolConcurrencyTest, LargestReservationFitsWithoutSizeClassPadding) {
  iree_hal_pool_release(pool_);
  pool_ = nullptr;
  iree_hal_tlsf_pool_options_t options = {};
  options.tlsf_options.range_length = 4112;
  IREE_ASSERT_OK(iree_hal_tlsf_pool_create(
      options, provider_, notification_, tracker_, {QueryEpoch, this},
      metadata_allocator_.allocator(), &pool_));
  iree_hal_pool_reservation_t reservations[2];
  IREE_ASSERT_OK(Acquire(4112, &reservations[0]));
  IREE_ASSERT_OK(Acquire(4112, &reservations[1]));
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, 2u);
  EXPECT_EQ(stats.bytes_committed, 2u * 4112u);
  iree_hal_pool_release_reservations(pool_, 2, reservations, nullptr);
}

TEST_F(TLSFPoolConcurrencyTest, RejectedRangeNeedsOneQueryAndNoAllocations) {
  iree_hal_pool_reservation_t seed;
  IREE_ASSERT_OK(Acquire(4096, &seed));
  alignas(16) uint8_t storage[sizeof(iree_async_frontier_t) +
                              sizeof(iree_async_frontier_entry_t)] = {};
  auto* death = reinterpret_cast<iree_async_frontier_t*>(storage);
  iree_async_frontier_initialize(death, 1);
  death->entries[0] = {iree_async_axis_make_queue(1, 0, 0, 0, 0), 7};
  iree_hal_pool_release_reservations(pool_, 1, &seed, death);

  const size_t allocation_calls = metadata_allocator_.allocation_calls();
  const size_t epoch_queries = epoch_queries_.load();
  auto request = Request(16);
  iree_hal_pool_reservation_t reservation;
  iree_hal_pool_acquire_info_t info;
  iree_hal_pool_acquire_result_t result;
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool_, 1, &request, nullptr, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
      &reservation, &info, &result));
  EXPECT_EQ(result, IREE_HAL_POOL_ACQUIRE_EXHAUSTED);
  EXPECT_EQ(epoch_queries_.load() - epoch_queries, 1u);
  EXPECT_EQ(metadata_allocator_.allocation_calls() - allocation_calls, 0u);

  // The same entire range remains available to a requester covering its prior
  // use, with the original exact prerequisite still attached to the result.
  request.allocation_size = 4096;
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool_, 1, &request, death, IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH,
      &reservation, &info, &result));
  ASSERT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK);
  ASSERT_NE(info.reuse_frontier, nullptr);
  EXPECT_EQ(info.reuse_frontier->entry_count, 1u);
  EXPECT_EQ(info.reuse_frontier->entries[0].axis, death->entries[0].axis);
  EXPECT_EQ(info.reuse_frontier->entries[0].epoch, 7u);
  EXPECT_EQ(reservation.offset, seed.offset);
  EXPECT_EQ(reservation.byte_length, 4096u);
  iree_hal_pool_release_reservations(pool_, 1, &reservation,
                                     info.reuse_frontier);
}

TEST_F(TLSFPoolConcurrencyTest, LargeBatchUsesAllPublishedSlabs) {
  constexpr size_t kCount = 12;
  iree_hal_pool_reservation_request_t requests[kCount];
  iree_hal_pool_reservation_t reservations[kCount];
  iree_hal_pool_acquire_info_t infos[kCount];
  for (auto& request : requests) {
    request = Request(4096);
  }
  iree_hal_pool_acquire_result_t result;
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool_, kCount, requests, nullptr, IREE_HAL_POOL_RESERVE_FLAG_NONE,
      reservations, infos, &result));
  ASSERT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
  iree_hal_pool_stats_t stats;
  iree_hal_pool_query_stats(pool_, &stats);
  EXPECT_EQ(stats.slab_count, kCount);
  EXPECT_EQ(stats.reserve_count, kCount);
  EXPECT_EQ(stats.exhausted_count, 0u);
  iree_hal_pool_release_reservations(pool_, kCount, reservations, nullptr);
  IREE_ASSERT_OK(iree_hal_pool_acquire_reservations(
      pool_, kCount, requests, nullptr,
      IREE_HAL_POOL_RESERVE_FLAG_DISALLOW_GROWTH, reservations, infos,
      &result));
  ASSERT_EQ(result, IREE_HAL_POOL_ACQUIRE_OK_FRESH);
  iree_hal_pool_release_reservations(pool_, kCount, reservations, nullptr);
}

}  // namespace
