// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "libamdf/cts/gpu/kernels/device_sdma.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "libamdf/cts/gpu/aql/dispatch_fixture.h"
#include "libamdf/cts/gpu/kernels/device_sdma_kernels.h"
#include "libamdf/cts/gpu/sdma/encoding/commands.h"

namespace {

class DeviceGeneratedSdmaTest : public AqlDispatchTest {
 protected:
  amdf_status_t MatchGpuEndpoint(amdf_endpoint_t* endpoint,
                                 bool* out_matches) override {
    const GpuQueueRequirements requirements = {
        .command_type = AMDF_QUEUE_COMMAND_TYPE_GPU_SDMA,
        .roles = AMDF_QUEUE_ROLE_TRANSFER,
        .user_queue_capabilities = AMDF_USER_QUEUE_CAPABILITY_DEVICE_PRODUCER,
    };
    amdf_queue_family_info_t sdma_family = {};
    bool matches = false;
    amdf_status_t status = FindGpuQueueFamily(api_, endpoint, requirements,
                                              &sdma_family, &matches);
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    if (!matches) {
      *out_matches = false;
      return AMDF_STATUS_OK;
    }
    status = AqlDispatchTest::MatchGpuEndpoint(endpoint, &matches);
    if (!amdf_status_is_ok(status)) {
      return status;
    }
    if (matches) {
      sdma_family_ = sdma_family;
    }
    *out_matches = matches;
    return AMDF_STATUS_OK;
  }

  void QueryPair(const amdf_memory_site_t& producer,
                 const amdf_memory_site_t& consumer,
                 amdf_memory_pair_info_t* out_pair) {
    out_pair->type = AMDF_STRUCTURE_TYPE_MEMORY_PAIR_INFO;
    out_pair->structure_size = sizeof(*out_pair);
    ASSERT_EQ(api_->memory_query_pair_info(&producer, &consumer, out_pair),
              AMDF_STATUS_OK);
    ASSERT_NE(out_pair->flags & AMDF_MEMORY_PAIR_FLAG_SHARED_BACKING_REACHABLE,
              0u);
  }

  void CheckTransition(const amdf_cache_transition_t& transition,
                       amdf_cache_operation_t operation) {
    ASSERT_EQ(transition.kind, operation == AMDF_CACHE_OPERATION_NONE
                                   ? AMDF_CACHE_TRANSITION_KIND_NONE
                                   : AMDF_CACHE_TRANSITION_KIND_GLOBAL);
    ASSERT_EQ(transition.executor, operation == AMDF_CACHE_OPERATION_NONE
                                       ? AMDF_CACHE_TRANSITION_EXECUTOR_NONE
                                       : AMDF_CACHE_TRANSITION_EXECUTOR_QUEUE);
    ASSERT_EQ(transition.operation, operation);
    ASSERT_EQ(transition.host_operation, AMDF_HOST_CACHE_OPERATION_NONE);
    ASSERT_EQ(transition.host_instruction, AMDF_HOST_CACHE_INSTRUCTION_NONE);
    ASSERT_EQ(transition.host_fence_before, AMDF_HOST_CACHE_FENCE_NONE);
    ASSERT_EQ(transition.host_fence_after, AMDF_HOST_CACHE_FENCE_NONE);
    ASSERT_EQ(transition.range_granularity, 0u);
  }

  void ResolveSdmaTransition(const amdf_cache_transition_t& transition,
                             amdf_cache_operation_t operation,
                             uint32_t* inout_cache_flags) {
    if (transition.kind == AMDF_CACHE_TRANSITION_KIND_NONE) {
      ASSERT_NO_FATAL_FAILURE(
          CheckTransition(transition, AMDF_CACHE_OPERATION_NONE));
      return;
    }
    ASSERT_NO_FATAL_FAILURE(CheckTransition(transition, operation));
    ASSERT_NE(
        sdma_family_.format_features & AMDF_GPU_SDMA_FORMAT_FEATURE_USER_GCR,
        0u);
    ASSERT_NE(sdma_family_.cache_operations & (UINT64_C(1) << operation), 0u);
    ASSERT_NE(sdma_family_.cache_transition_kinds &
                  AMDF_CACHE_TRANSITION_KINDS_GLOBAL,
              0u);
    *inout_cache_flags |=
        operation == AMDF_CACHE_OPERATION_ACQUIRE_FROM_SYSTEM ? 1u : 2u;
  }

  // Exact transfer family selected before borrowing the cached native device.
  amdf_queue_family_info_t sdma_family_ = {};
};

TEST_F(DeviceGeneratedSdmaTest, DependentCopiesReuseRingAndPayload) {
  const auto* kernel = kernels::device_sdma::kKernels.Find(gpu_endpoint_info_);
  ASSERT_NE(kernel, nullptr) << "missing device SDMA kernel for endpoint";
  ASSERT_EQ(kernel->private_segment_byte_length, 0u);
  ASSERT_EQ(kernel->group_segment_byte_length, 0u);
  ASSERT_EQ(kernel->workgroup_size(), 1u);
  ASSERT_LE(kernel->arguments.byte_length,
            sizeof(kernels::device_sdma::Arguments));
  RecordProperty("device_sdma_kernel_target", kernel->target);
  RecordProperty("device_sdma_kernel_sha256", kernel->hsaco_sha256);
  RecordProperty("sdma_format_features",
                 std::to_string(sdma_family_.format_features));

  constexpr uint32_t kRoundCount = 257;
  constexpr uint32_t kPageWordCount = 128;
  constexpr uint32_t kPayloadWordOffset = 16;
  constexpr uint32_t kPayloadWordCount = 64;
  constexpr uint32_t kRecordWordCount = 72;
  constexpr uint32_t kGuardWordCount = 16;
  constexpr uint32_t kAllocationWordCount = 4096 / sizeof(uint32_t);
  constexpr uint32_t kCompletionByteOffset = 64;
  constexpr uint32_t kSeed = 0x91e10da5u;
  constexpr uint32_t kSourceGuard = 0x75db8163u;
  constexpr uint32_t kDestinationGuard = 0x32a49bc7u;
  constexpr uint32_t kControlGuard = 0x69ca714bu;
  constexpr uint32_t kRecordGuard = 0xab3265c9u;
  constexpr uint64_t kRecordByteLength =
      ((uint64_t{kRoundCount} * kRecordWordCount + 2 * kGuardWordCount) *
           sizeof(uint32_t) +
       4095) &
      ~UINT64_C(4095);
  constexpr amdf_memory_access_t kReadWrite =
      AMDF_MEMORY_ACCESS_READ | AMDF_MEMORY_ACCESS_WRITE;
  GpuMemory* source = nullptr;
  GpuMemory* destination = nullptr;
  GpuMemory* control = nullptr;
  GpuMemory* records = nullptr;
  GpuMemory* arguments = nullptr;
  ASSERT_NO_FATAL_FAILURE(CreateMemory(AMDF_MEMORY_ACCESS_READ, 4096, &source));
  ASSERT_NO_FATAL_FAILURE(CreateMemory(kReadWrite, 4096, &destination));
  ASSERT_NO_FATAL_FAILURE(CreateMemory(kReadWrite, 4096, &control));
  ASSERT_NO_FATAL_FAILURE(
      CreateMemory(kReadWrite, kRecordByteLength, &records));
  ASSERT_NO_FATAL_FAILURE(
      CreateMemory(AMDF_MEMORY_ACCESS_READ, 4096, &arguments));

  amdf_memory_pair_info_t ingress = {};
  amdf_memory_pair_info_t copied = {};
  amdf_memory_pair_info_t egress = {};
  ASSERT_NO_FATAL_FAILURE(QueryPair(
      source->HostSite(), source->DeviceSite(sdma_family_.ordinal), &ingress));
  ASSERT_NO_FATAL_FAILURE(
      QueryPair(destination->DeviceSite(sdma_family_.ordinal),
                destination->DeviceSite(family_.ordinal), &copied));
  ASSERT_NO_FATAL_FAILURE(QueryPair(records->DeviceSite(family_.ordinal),
                                    records->HostSite(), &egress));
  ASSERT_NO_FATAL_FAILURE(
      CheckTransition(ingress.release, AMDF_CACHE_OPERATION_NONE));
  ASSERT_NO_FATAL_FAILURE(CheckTransition(
      copied.acquire, AMDF_CACHE_OPERATION_ACQUIRE_FROM_SYSTEM));
  ASSERT_NO_FATAL_FAILURE(
      CheckTransition(egress.release, AMDF_CACHE_OPERATION_RELEASE_TO_SYSTEM));
  ASSERT_NO_FATAL_FAILURE(
      CheckTransition(egress.acquire, AMDF_CACHE_OPERATION_NONE));
  uint32_t cache_flags = 0;
  ASSERT_NO_FATAL_FAILURE(ResolveSdmaTransition(
      ingress.acquire, AMDF_CACHE_OPERATION_ACQUIRE_FROM_SYSTEM, &cache_flags));
  ASSERT_NO_FATAL_FAILURE(ResolveSdmaTransition(
      copied.release, AMDF_CACHE_OPERATION_RELEASE_TO_SYSTEM, &cache_flags));
  RecordProperty("device_sdma_cache_flags", cache_flags);

  GpuUserQueue* producer = nullptr;
  GpuUserQueue* transfer = nullptr;
  ASSERT_NO_FATAL_FAILURE(CreateQueue(&producer));
  ASSERT_NO_FATAL_FAILURE(
      CreateQueue(sdma_family_, &transfer, AMDF_QUEUE_PRODUCER_MODE_SINGLE, {},
                  AMDF_USER_QUEUE_CAPABILITY_HOST_PRODUCER |
                      AMDF_USER_QUEUE_CAPABILITY_DEVICE_PRODUCER));
  const auto& mapping = transfer->producer.info;
  ASSERT_GE(mapping.ring_byte_length, 4096u);
  ASSERT_LE(mapping.ring_byte_length, UINT64_C(1) << 32);
  ASSERT_EQ(mapping.ring_byte_length & (mapping.ring_byte_length - 1), 0u);
  uint64_t packet_index = 0;
  uint64_t descriptor_address = 0;
  ASSERT_NO_FATAL_FAILURE(PublishKernel(*producer, *kernel, "device_sdma",
                                        &packet_index, &descriptor_address));

  std::array<uint32_t, kAllocationWordCount> source_words;
  source_words.fill(kSourceGuard);
  for (uint32_t page = 0; page < 8; ++page) {
    for (uint32_t word = 0; word < kPayloadWordCount; ++word) {
      source_words[page * kPageWordCount + kPayloadWordOffset + word] =
          0x31415927u + page * 0x243f6a89u + word * 0x1020305u;
    }
  }
  std::array<uint32_t, kAllocationWordCount> expected_destination;
  expected_destination.fill(kDestinationGuard);
  std::array<uint32_t, kAllocationWordCount> expected_control;
  expected_control.fill(kControlGuard);
  aql::Signal final_signal = {};
  final_signal.kind = 1;
  std::memcpy(expected_control.data(), &final_signal, sizeof(final_signal));
  expected_control[kCompletionByteOffset / sizeof(uint32_t)] = kRoundCount;
  std::vector<uint32_t> expected_records(kRecordByteLength / sizeof(uint32_t),
                                         kRecordGuard);
  std::memcpy(source->host.pointer, source_words.data(), sizeof(source_words));
  std::memcpy(destination->host.pointer, expected_destination.data(),
              sizeof(expected_destination));
  std::memcpy(control->host.pointer, expected_control.data(),
              sizeof(expected_control));
  auto& signal = *static_cast<aql::Signal*>(control->host.pointer);
  signal.value = 1;
  auto* completion = reinterpret_cast<uint32_t*>(
      static_cast<uint8_t*>(control->host.pointer) + kCompletionByteOffset);
  *completion = 0;
  std::memcpy(records->host.pointer, expected_records.data(),
              kRecordByteLength);

  // Only family-selected invariant fields come from the host encoder. The
  // shader authors every published packet and computes its addresses/length.
  std::array<uint32_t, 11> encoding;
  SdmaCommandWriter encoder(encoding.data(), sdma_family_.format_features);
  encoder.CopyLinear(source->device_address, destination->device_address, 4);
  encoder.Fence32(control->device_address + kCompletionByteOffset, 1);
  ASSERT_EQ(encoder.word_count(), encoding.size());
  const kernels::device_sdma::Arguments payload = {
      .ring = mapping.ring_address,
      .read_index = mapping.read_index_address,
      .write_index = mapping.write_index_address,
      .notification = mapping.doorbell_address,
      .destinations = destination->device_address,
      .completion = control->device_address + kCompletionByteOffset,
      .records = records->device_address + kGuardWordCount * sizeof(uint32_t),
      .source_address = source->device_address,
      .destination_address = destination->device_address,
      .completion_address = control->device_address + kCompletionByteOffset,
      .capacity = mapping.ring_byte_length,
      .round_count = kRoundCount,
      .seed = kSeed,
      .copy_control = encoding[2],
      .fence_header = encoding[7],
      .cache_flags = cache_flags,
  };
  ASSERT_EQ(arguments->device_address % kernel->arguments.alignment, 0u);
  std::memset(arguments->host.pointer, 0, arguments->info.byte_length);
  std::memcpy(arguments->host.pointer, &payload, kernel->arguments.byte_length);

  uint64_t expected_frontier = 0;
  uint32_t state = kSeed;
  uint32_t selected_pages = 0;
  uint32_t selected_slots = 0;
  std::array<bool, kPayloadWordCount> selected_lengths = {};
  const uint64_t chain_byte_length = 44 + ((cache_flags & 1) != 0 ? 20 : 0) +
                                     ((cache_flags & 2) != 0 ? 20 : 0);
  for (uint32_t round = 0; round < kRoundCount; ++round) {
    const uint32_t page = state & 7;
    const uint32_t slot = (state >> 3) & 3;
    const uint32_t word_count = ((state >> 8) & 63) + 1;
    selected_pages |= 1u << page;
    selected_slots |= 1u << slot;
    selected_lengths[word_count - 1] = true;
    const uint32_t source_offset = page * kPageWordCount + kPayloadWordOffset;
    const uint32_t destination_offset =
        slot * kPageWordCount + kPayloadWordOffset;
    std::copy_n(source_words.begin() + source_offset, word_count,
                expected_destination.begin() + destination_offset);
    const uint32_t next_state =
        expected_destination[destination_offset] ^
        static_cast<uint32_t>(
            uint64_t{
                expected_destination[destination_offset + word_count - 1]} +
            uint64_t{round + 1} * 0x9e3779b1u);
    const uint64_t tail =
        mapping.ring_byte_length - expected_frontier % mapping.ring_byte_length;
    if (tail < chain_byte_length) {
      expected_frontier += tail;
    }
    expected_frontier += chain_byte_length;
    const uint32_t row = kGuardWordCount + round * kRecordWordCount;
    expected_records[row] = page;
    expected_records[row + 1] = slot;
    expected_records[row + 2] = word_count;
    expected_records[row + 3] = next_state;
    expected_records[row + 4] = static_cast<uint32_t>(expected_frontier);
    expected_records[row + 5] = static_cast<uint32_t>(expected_frontier >> 32);
    expected_records[row + 6] = state;
    expected_records[row + 7] = round + 1;
    std::copy_n(expected_destination.begin() + destination_offset,
                kPayloadWordCount, expected_records.begin() + row + 8);
    state = next_state;
  }
  ASSERT_EQ(selected_pages, 0xffu);
  ASSERT_EQ(selected_slots, 0xfu);
  ASSERT_EQ(std::count(selected_lengths.begin(), selected_lengths.end(), true),
            kPayloadWordCount);
  ASSERT_GT(expected_frontier / mapping.ring_byte_length, 1u);

  std::vector<uint32_t> observed_records(expected_records.size());
  std::array<uint32_t, kAllocationWordCount> observed_destination;
  std::array<uint32_t, kAllocationWordCount> observed_source;
  std::array<uint32_t, kAllocationWordCount> observed_control;
  const auto dispatch = aql::Dispatch(
      aql::HeaderBarrier::kDisabled, {1, {1, 1, 1}, {1, 1, 1}},
      kernel->private_segment_byte_length, kernel->group_segment_byte_length,
      descriptor_address, arguments->device_address, control->device_address);
  GpuStoreRelease(producer->host.write_index_address, packet_index + 1);
  aql::Publish(*producer, packet_index++, dispatch);
  // This is the only workload join. Snapshot the GPU's actual copied-data
  // transcript before command-consumption queries or native teardown.
  GpuWaitEqual<int64_t>(reinterpret_cast<uintptr_t>(&signal.value), 0);
  std::memcpy(observed_records.data(), records->host.pointer,
              kRecordByteLength);
  std::memcpy(observed_destination.data(), destination->host.pointer,
              sizeof(observed_destination));
  std::memcpy(observed_source.data(), source->host.pointer,
              sizeof(observed_source));
  std::memcpy(observed_control.data(), control->host.pointer,
              sizeof(observed_control));
  EXPECT_EQ(observed_records, expected_records);
  EXPECT_EQ(observed_destination, expected_destination);
  EXPECT_EQ(observed_source, source_words);
  EXPECT_EQ(observed_control, expected_control);
  EXPECT_EQ(GpuLoadAcquire<uint64_t>(transfer->host.write_index_address),
            expected_frontier);
  EXPECT_NO_FATAL_FAILURE(producer->WaitConsumed(api_, packet_index));
  EXPECT_NO_FATAL_FAILURE(transfer->WaitConsumed(api_, expected_frontier));
  RecordProperty("device_sdma_completed_transfers", kRoundCount);
  RecordProperty("device_sdma_published_bytes",
                 std::to_string(expected_frontier));
  RecordProperty("device_sdma_ring_wraps",
                 std::to_string(expected_frontier / mapping.ring_byte_length));
  RecordProperty(
      "device_sdma_payload_lengths",
      std::count(selected_lengths.begin(), selected_lengths.end(), true));
}

}  // namespace
