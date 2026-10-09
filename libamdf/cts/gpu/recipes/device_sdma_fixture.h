// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef AMDF_CTS_GPU_RECIPES_DEVICE_SDMA_FIXTURE_H_
#define AMDF_CTS_GPU_RECIPES_DEVICE_SDMA_FIXTURE_H_

#include "libamdf/cts/gpu/aql/dispatch_fixture.h"

// Shares passive device-publication admission and queried cache transitions.
// Each recipe owns its queues, publication protocol and payload lifetimes.
class DeviceGeneratedSdmaTest : public AqlDispatchTest {
 protected:
  amdf_status_t MatchGpuEndpoint(amdf_endpoint_t* endpoint,
                                 bool* out_matches) override;

  void QueryPair(const amdf_memory_site_t& producer,
                 const amdf_memory_site_t& consumer,
                 amdf_memory_pair_info_t* out_pair);

  void CheckTransition(const amdf_cache_transition_t& transition,
                       amdf_cache_operation_t operation);

  void ResolveSdmaTransition(const amdf_cache_transition_t& transition,
                             amdf_cache_operation_t operation,
                             uint32_t* inout_cache_flags);

  void QueryTransferCacheFlags(GpuMemory* source, GpuMemory* destination,
                               GpuMemory* records, uint32_t* out_flags);

  void QueryDownloadCacheFlags(GpuMemory* output, GpuMemory* readback,
                               uint32_t* out_flags);

  // Exact transfer family selected before borrowing the cached native device.
  amdf_queue_family_info_t sdma_family_ = {};
};

#endif  // AMDF_CTS_GPU_RECIPES_DEVICE_SDMA_FIXTURE_H_
