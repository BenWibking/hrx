// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_NET_CTS_COLLECTIVE_TRIAL_H_
#define IREE_NET_CTS_COLLECTIVE_TRIAL_H_

#include "iree/net/cts/transfer_trial.h"

namespace iree::net::cts {

// Explicit payload strategy; registered placement never falls back to messages.
enum class CollectiveDelivery { kMessage, kRegistered };

// Checked host all-reduce schedule shared by CTS and fixed-work benchmarks.
struct CollectiveTrialOptions {
  // Independently polling application ranks arranged in a ring.
  uint32_t rank_count = 2;
  // Full tensor bytes per rank, divisible by rank_count * sizeof(uint32_t).
  size_t tensor_size = 4096;
  // Maximum bytes per transfer, divisible by sizeof(uint32_t); tails are exact.
  size_t block_size = 1024;
  // Maximum unconsumed blocks on each directed rank edge.
  uint32_t window_size = 4;
  // Complete all-reduces before measurement.
  uint32_t warmup_rounds = 2;
  // Dependent all-reduces in the measured phase.
  uint32_t measured_rounds = 17;
  // Payload ownership strategy used by every rank.
  CollectiveDelivery delivery = CollectiveDelivery::kMessage;
};

struct CollectiveTrialResult {
  // All listeners were created; subsequent transport failures are not skips.
  bool available = false;
  // Wall time through all ranks checking results and joining accepted sources.
  double elapsed_seconds = 0;
  // Whole all-reduces completed by the rank group, not multiplied by ranks.
  uint64_t collectives = 0;
  // Payload bytes sent across all edges, excluding control and framing.
  uint64_t payload_bytes = 0;
  // Accepted data sends across all ranks.
  uint64_t sends = 0;
  // Exact terminal data-source callbacks across all ranks.
  uint64_t source_completions = 0;
  // Maximum admitted-minus-consumed blocks on any one rank edge.
  uint64_t window_high_water = 0;
  // Application tensor and receive-slot storage across all ranks.
  uint64_t payload_storage_bytes = 0;
};

// Runs a ring reduce-scatter followed by all-gather on actual connections.
// Each rank owns an application poll thread, one tensor and bounded receive
// slots. Incoming blocks are consumed before forwarding; outgoing sources join
// before that tensor storage is modified. Every rank checks its entire reduced
// tensor after each round. Subsequent rounds depend on that result check.
//
// Registered mode places into reusable reduction/gather inputs without message
// staging. Message mode consumes the transport's actual receive leases. Only
// bootstrap, phase joins and errors use cross-thread state outside the net
// APIs. Setup, warm-up and teardown are excluded from timing; host reduction,
// validation, progress feedback and every source callback are included.
// This same-process host schedule is not a GPU collective implementation or
// a measurement of multi-host topology, isolated wire latency or link
// bandwidth.
iree_status_t RunCollectiveTrial(
    const TransportBackend& transport,
    const iree::async::cts::ProactorFactory& create_proactor,
    const CollectiveTrialOptions& options, CollectiveTrialResult* out_result,
    TransferTrialMeasurement measurement = {});

}  // namespace iree::net::cts

#endif  // IREE_NET_CTS_COLLECTIVE_TRIAL_H_
