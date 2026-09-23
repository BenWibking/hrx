// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/net/rdma/region.h"

#include <fcntl.h>
#include <netinet/in.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <vector>

#include "iree/async/platform/io_uring/api.h"
#include "iree/async/platform/posix/api.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace iree::net::rdma {
namespace {

// Function pointer declarations must match the header ABI we load at runtime.
#define IREE_NET_RDMA_SYMBOL(library, result, name, arguments)          \
  static_assert(std::is_same_v<decltype(iree_net_rdma_library_t::name), \
                               decltype(&name)>);
#include "iree/net/rdma/library_symbols.h"

static void CheckNative(int error) {
  if (error) {
    iree_status_abort(iree_make_status(iree_status_code_from_errno(error),
                                       "native RDMA test operation: %s",
                                       strerror(error)));
  }
}

template <typename Predicate>
static void PollUntil(iree_async_proactor_t* proactor, Predicate ready) {
  while (!ready()) {
    IREE_CHECK_OK(
        iree_async_proactor_poll(proactor, iree_infinite_timeout(), nullptr));
  }
}

// The test owns real native queues, not a substitute registration provider.
// Each pair has its own poll owner so destroying a pair proves that surviving
// registrations have no hidden proactor lifetime dependency.
class NativePair {
 public:
  NativePair(iree_async_region_t* first, iree_async_region_t* second,
             const char* backend, uint32_t source_offset)
      : regions_{first, second}, source_offset_(source_offset) {
    for (auto* region : regions_) {
      iree_async_region_retain(region);
    }
    context_ = iree_net_rdma_region_context(first);
    library_ = iree_net_rdma_context_library(context_);
    auto* device = iree_net_rdma_context_device(context_);
    EXPECT_EQ(device, iree_net_rdma_context_device(
                          iree_net_rdma_region_context(second)));
    auto options = iree_async_proactor_options_default();
    if (strcmp(backend, "io_uring") == 0) {
      IREE_CHECK_OK(iree_async_proactor_create_io_uring(
          options, iree_allocator_system(), &proactor_));
    } else {
      IREE_CHECK_OK(iree_async_proactor_create_posix(
          options, iree_allocator_system(), &proactor_));
    }
    channel_ = library_->ibv_create_comp_channel(device);
    if (!channel_) {
      CheckNative(errno);
    }
    int flags = fcntl(channel_->fd, F_GETFL);
    if (flags < 0) {
      CheckNative(errno);
    }
    if (fcntl(channel_->fd, F_SETFL, flags | O_NONBLOCK) != 0) {
      CheckNative(errno);
    }
    queue_ = library_->ibv_create_cq(device, 32, this, channel_, 0);
    if (!queue_) {
      CheckNative(errno);
    }

    uint8_t port = iree_net_rdma_context_port_number(context_);
    struct ibv_port_attr port_attributes = {};
    CheckNative(iree_net_rdma_library_query_port(library_, device, port,
                                                 &port_attributes));
    // The native inventory contains every port's GIDs. Account for all ports
    // instead of assuming the selected port's table bounds the whole device.
    size_t gid_capacity = 0;
    for (uint32_t i = 1;
         i <= iree_net_rdma_context_device_attributes(context_)->phys_port_cnt;
         ++i) {
      struct ibv_port_attr attributes = {};
      CheckNative(
          iree_net_rdma_library_query_port(library_, device, i, &attributes));
      gid_capacity += attributes.gid_tbl_len;
    }
    std::vector<ibv_gid_entry> gids(gid_capacity);
    ssize_t gid_count = library_->_ibv_query_gid_table(
        device, gids.data(), gids.size(), 0, sizeof(ibv_gid_entry));
    if (gid_count < 0) {
      CheckNative(static_cast<int>(-gid_count));
    }
    ibv_gid_entry gid = {};
    for (ssize_t i = 0; i < gid_count; ++i) {
      struct in6_addr address = {};
      memcpy(&address, &gids[i].gid, sizeof(address));
      if (gids[i].port_num == port &&
          ((gids[i].gid_type == IBV_GID_TYPE_ROCE_V2 &&
            IN6_IS_ADDR_V4MAPPED(&address)) ||
           gids[i].gid_type == IBV_GID_TYPE_IB)) {
        gid = gids[i];
        break;
      }
    }
    if (!gid.port_num) {
      iree_status_abort(
          iree_make_status(IREE_STATUS_UNAVAILABLE,
                           "test port needs an IB or IPv4 RoCE v2 GID"));
    }

    for (uint32_t side = 0; side < 2; ++side) {
      ibv_qp_init_attr options = {};
      options.send_cq = queue_;
      options.recv_cq = queue_;
      options.qp_type = IBV_QPT_RC;
      options.cap.max_send_wr = 4;
      options.cap.max_recv_wr = 4;
      options.cap.max_send_sge = 1;
      options.cap.max_recv_sge = 1;
      queues_[side] = library_->ibv_create_qp(
          iree_net_rdma_context_protection_domain(
              iree_net_rdma_region_context(regions_[side])),
          &options);
      if (!queues_[side]) {
        CheckNative(errno);
      }
      ibv_qp_attr attributes = {};
      attributes.qp_state = IBV_QPS_INIT;
      attributes.port_num = port;
      attributes.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
      CheckNative(library_->ibv_modify_qp(queues_[side], &attributes,
                                          IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                              IBV_QP_PORT |
                                              IBV_QP_ACCESS_FLAGS));
    }
    for (uint32_t side = 0; side < 2; ++side) {
      ibv_qp_attr attributes = {};
      attributes.qp_state = IBV_QPS_RTR;
      attributes.path_mtu = port_attributes.active_mtu;
      attributes.dest_qp_num = queues_[1 - side]->qp_num;
      attributes.max_dest_rd_atomic = 1;
      attributes.min_rnr_timer = 12;
      attributes.ah_attr.port_num = port;
      attributes.ah_attr.dlid = port_attributes.lid;
      attributes.ah_attr.is_global = 1;
      attributes.ah_attr.grh.dgid = gid.gid;
      attributes.ah_attr.grh.sgid_index = gid.gid_index;
      attributes.ah_attr.grh.hop_limit = 1;
      CheckNative(library_->ibv_modify_qp(
          queues_[side], &attributes,
          IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
              IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
              IBV_QP_MIN_RNR_TIMER));
      attributes = {};
      attributes.qp_state = IBV_QPS_RTS;
      attributes.timeout = 14;
      attributes.retry_cnt = 7;
      attributes.rnr_retry = 7;
      attributes.max_rd_atomic = 1;
      CheckNative(library_->ibv_modify_qp(
          queues_[side], &attributes,
          IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
              IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC));
    }
    CheckNative(ibv_req_notify_cq(queue_, 0));
    IREE_CHECK_OK(iree_async_proactor_register_event_source(
        proactor_, iree_async_primitive_from_fd(channel_->fd),
        {+[](void* user_data, iree_async_event_source_t*,
             iree_async_poll_events_t events) {
           EXPECT_FALSE(iree_async_poll_has_error(events));
           static_cast<NativePair*>(user_data)->Service();
         },
         this},
        &monitor_));
  }

  ~NativePair() {
    uint32_t pending_on_close = pending_;
    uint32_t completed_before_close = completed_count_;
    for (auto* queue : queues_) {
      ibv_qp_attr attributes = {};
      attributes.qp_state = IBV_QPS_ERR;
      CheckNative(library_->ibv_modify_qp(queue, &attributes, IBV_QP_STATE));
    }
    PollUntil(proactor_, [&] { return pending_ == 0; });
    EXPECT_EQ(completed_count_, completed_before_close + pending_on_close);
    for (uint32_t i = completed_before_close; i < completed_count_; ++i) {
      EXPECT_EQ(completed_[i].wr_id, 1u);
      EXPECT_EQ(completed_[i].status, IBV_WC_WR_FLUSH_ERR);
    }
    for (auto* queue : queues_) {
      CheckNative(library_->ibv_destroy_qp(queue));
    }
    bool joined = false;
    iree_async_proactor_unregister_event_source(
        proactor_, monitor_,
        {+[](void* user_data) { *static_cast<bool*>(user_data) = true; },
         &joined});
    PollUntil(proactor_, [&] { return joined; });
    ConsumeEvents();
    CheckNative(library_->ibv_destroy_cq(queue_));
    CheckNative(library_->ibv_destroy_comp_channel(channel_));
    iree_async_proactor_release(proactor_);
    for (auto* region : regions_) {
      iree_async_region_release(region);
    }
  }

  uint32_t source_offset() const { return source_offset_; }

  void Transfer(uint32_t side, uint32_t target_offset, uint32_t length) {
    completed_count_ = 0;
    ibv_recv_wr receive = {};
    receive.wr_id = 2;
    ibv_recv_wr* rejected_receive = nullptr;
    CheckNative(ibv_post_recv(queues_[1 - side], &receive, &rejected_receive));
    ++pending_;
    PostWrite(side, target_offset, length);
    PollUntil(proactor_, [&] { return pending_ == 0; });
    ASSERT_EQ(completed_count_, 2u);
    uint32_t kinds = 0;
    for (uint32_t i = 0; i < completed_count_; ++i) {
      EXPECT_EQ(completed_[i].status, IBV_WC_SUCCESS);
      kinds |= 1u << completed_[i].wr_id;
      if (completed_[i].wr_id == 2) {
        EXPECT_EQ(completed_[i].byte_len, length);
        EXPECT_TRUE(completed_[i].wc_flags & IBV_WC_WITH_IMM);
      }
    }
    EXPECT_EQ(kinds, (1u << 1) | (1u << 2));
  }

  // One accepted source with no receive notification credit. Destruction must
  // retire it explicitly before releasing this pair's region references.
  void PostBlockedWrite(uint32_t target_offset) {
    completed_count_ = 0;
    PostWrite(0, target_offset, 256);
    EXPECT_EQ(pending_, 1u);
  }

 private:
  void PostWrite(uint32_t side, uint32_t target_offset, uint32_t length) {
    ibv_sge span = {regions_[side]->handles.rdma.address + source_offset_,
                    length, regions_[side]->handles.rdma.lkey};
    ibv_send_wr request = {};
    request.wr_id = 1;
    request.sg_list = &span;
    request.num_sge = 1;
    request.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    request.send_flags = IBV_SEND_SIGNALED;
    request.imm_data = 0;
    request.wr.rdma.remote_addr =
        regions_[1 - side]->handles.rdma.address + target_offset;
    request.wr.rdma.rkey = regions_[1 - side]->handles.rdma.rkey;
    ibv_send_wr* rejected = nullptr;
    CheckNative(ibv_post_send(queues_[side], &request, &rejected));
    ++pending_;
  }

  void ConsumeEvents() {
    ibv_cq* queue = nullptr;
    void* owner = nullptr;
    while (library_->ibv_get_cq_event(channel_, &queue, &owner) == 0) {
      EXPECT_EQ(queue, queue_);
      EXPECT_EQ(owner, this);
      library_->ibv_ack_cq_events(queue, 1);
    }
    CheckNative(errno == EAGAIN ? 0 : errno);
  }

  void Service() {
    ConsumeEvents();
    CheckNative(ibv_req_notify_cq(queue_, 0));
    ibv_wc completion = {};
    int count = 0;
    while ((count = ibv_poll_cq(queue_, 1, &completion)) > 0) {
      ASSERT_GT(pending_, 0u);
      --pending_;
      ASSERT_LT(completed_count_, completed_.size());
      completed_[completed_count_++] = completion;
    }
    if (count < 0) {
      CheckNative(-count);
    }
  }

  // Retained registrations keep native domains/libraries and backing alive.
  std::array<iree_async_region_t*, 2> regions_;
  // Disjoint source range held exclusively by this pair until completion.
  uint32_t source_offset_;
  // Borrowed context from regions_[0].
  iree_net_rdma_context_t* context_ = nullptr;
  // Borrowed symbols from context_.
  const iree_net_rdma_library_t* library_ = nullptr;
  // Independently owned polling lifetime for this native pair.
  iree_async_proactor_t* proactor_ = nullptr;
  // Native notification channel, closed only after monitor retirement.
  ibv_comp_channel* channel_ = nullptr;
  // CQ sized for all enforced work on both QPs, including errors.
  ibv_cq* queue_ = nullptr;
  // Native peer QPs; no global lookup associates them with registrations.
  std::array<ibv_qp*, 2> queues_ = {};
  // Native monitor whose unregister completion joins channel ownership.
  iree_async_event_source_t* monitor_ = nullptr;
  // Accepted native WRs not yet observed through their exact CQEs.
  uint32_t pending_ = 0;
  // Fixed result capture for a single bounded transfer or shutdown phase.
  std::array<ibv_wc, 16> completed_ = {};
  // Initialized completion entries, without assuming delivery order.
  uint32_t completed_count_ = 0;
};

class RegionTest : public ::testing::TestWithParam<const char*> {
 protected:
  void SetUp() override {
    options_ = iree_net_rdma_context_options_default();
    const char* device_name = std::getenv("IREE_NET_RDMA_TEST_DEVICE");
    options_.device_name = iree_make_cstring_view(device_name);
    iree_status_t status = iree_net_rdma_context_create(
        options_, iree_allocator_system(), &contexts_[0]);
    if (!device_name && (iree_status_is_unavailable(status) ||
                         iree_status_is_not_found(status))) {
      iree_status_free(status);
      GTEST_SKIP() << "No RDMA runtime/device; set IREE_NET_RDMA_TEST_DEVICE "
                      "to require a particular provider.";
    }
    IREE_ASSERT_OK(status);
    IREE_ASSERT_OK(iree_net_rdma_context_create(
        options_, iree_allocator_system(), &contexts_[1]));
    for (uint32_t side = 0; side < 2; ++side) {
      auto slab_options = iree_async_slab_options_default();
      slab_options.buffer_size = 16384;
      slab_options.buffer_count = 1;
      iree_async_slab_t* slab = nullptr;
      IREE_ASSERT_OK(
          iree_async_slab_create(slab_options, iree_allocator_system(), &slab));
      status = iree_net_rdma_region_register_slab(
          contexts_[side], slab, 0x50000000 + side * 0x10000000,
          IREE_ASYNC_BUFFER_ACCESS_FLAG_READ |
              IREE_ASYNC_BUFFER_ACCESS_FLAG_WRITE |
              IREE_ASYNC_BUFFER_ACCESS_FLAG_REMOTE_WRITE,
          iree_allocator_system(), &regions_[side]);
      iree_async_slab_release(slab);
      IREE_ASSERT_OK(status);
      ASSERT_NE(reinterpret_cast<uintptr_t>(regions_[side]->base_ptr),
                regions_[side]->handles.rdma.address);
    }
  }

  void TearDown() override {
    for (auto* region : regions_) {
      iree_async_region_release(region);
    }
    for (auto* context : contexts_) {
      iree_net_rdma_context_release(context);
    }
  }

  void CheckRoundTrip(NativePair& pair, uint32_t target_offset, uint8_t seed) {
    auto* first = static_cast<uint8_t*>(regions_[0]->base_ptr);
    auto* second = static_cast<uint8_t*>(regions_[1]->base_ptr);
    for (uint32_t i = 0; i < 256; ++i) {
      first[pair.source_offset() + i] = uint8_t(seed + i);
    }
    ASSERT_NO_FATAL_FAILURE(pair.Transfer(0, target_offset, 256));
    for (uint32_t i = 0; i < 256; ++i) {
      ASSERT_EQ(second[target_offset + i], uint8_t(seed + i));
      second[pair.source_offset() + i] = second[target_offset + i] ^ 0x5a;
    }
    ASSERT_NO_FATAL_FAILURE(pair.Transfer(1, target_offset, 256));
    for (uint32_t i = 0; i < 256; ++i) {
      ASSERT_EQ(first[target_offset + i], (uint8_t(seed + i) ^ 0x5a));
    }
  }

  // Explicit selection reused for independent owners of one canonical device.
  iree_net_rdma_context_options_t options_ = {};
  // Creation references dropped separately from retained region ownership.
  std::array<iree_net_rdma_context_t*, 2> contexts_ = {};
  // Long-lived registered caller storage shared by both native connections.
  std::array<iree_async_region_t*, 2> regions_ = {};
};

TEST_P(RegionTest, SharedRegistrationSurvivesConnectionAndProactorRetirement) {
  auto first =
      std::make_unique<NativePair>(regions_[0], regions_[1], GetParam(), 0);
  auto second =
      std::make_unique<NativePair>(regions_[0], regions_[1], GetParam(), 512);
  for (auto*& context : contexts_) {
    iree_net_rdma_context_release(context);
    context = nullptr;
  }
  std::array<uint32_t, 2> keys = {regions_[0]->handles.rdma.rkey,
                                  regions_[1]->handles.rdma.rkey};
  ASSERT_NO_FATAL_FAILURE(CheckRoundTrip(*first, 4096, 17));
  // A separate target remains live while A owns an uncompleted native source.
  first->PostBlockedWrite(12288);
  ASSERT_NO_FATAL_FAILURE(CheckRoundTrip(*second, 8192, 33));
  first.reset();
  for (uint32_t round = 0; round < 16; ++round) {
    ASSERT_NO_FATAL_FAILURE(CheckRoundTrip(*second, 8192, 50 + round));
  }
  second.reset();
  for (uint32_t side = 0; side < 2; ++side) {
    EXPECT_EQ(regions_[side]->handles.rdma.rkey, keys[side]);
    auto* bytes = static_cast<uint8_t*>(regions_[side]->base_ptr);
    for (uint32_t i = 0; i < 256; ++i) {
      EXPECT_EQ(bytes[4096 + i],
                side ? uint8_t(17 + i) : (uint8_t(17 + i) ^ 0x5a));
    }
  }
}

TEST_P(RegionTest, RejectsUnsupportedAccessWithoutTakingSlabOwnership) {
  auto* slab = regions_[0]->slab;
  iree_async_region_t* rejected = nullptr;
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_INVALID_ARGUMENT,
      iree_net_rdma_region_register_slab(
          contexts_[0], slab, regions_[0]->handles.rdma.address,
          IREE_ASYNC_BUFFER_ACCESS_FLAG_REMOTE_WRITE, iree_allocator_system(),
          &rejected));
  EXPECT_EQ(rejected, nullptr);
  NativePair pair(regions_[0], regions_[1], GetParam(), 0);
  ASSERT_NO_FATAL_FAILURE(CheckRoundTrip(pair, 4096, 71));
}

TEST_P(RegionTest, FailedContextSelectionPreservesSharedNativeInventory) {
  auto options = iree_net_rdma_context_options_default();
  options.device_name = IREE_SV("iree_nonexistent_rdma_device");
  iree_net_rdma_context_t* rejected = nullptr;
  IREE_EXPECT_STATUS_IS(IREE_STATUS_NOT_FOUND,
                        iree_net_rdma_context_create(
                            options, iree_allocator_system(), &rejected));
  EXPECT_EQ(rejected, nullptr);
  NativePair pair(regions_[0], regions_[1], GetParam(), 0);
  ASSERT_NO_FATAL_FAILURE(CheckRoundTrip(pair, 4096, 91));
}

INSTANTIATE_TEST_SUITE_P(Backends, RegionTest,
                         ::testing::Values("io_uring", "posix"));

}  // namespace
}  // namespace iree::net::rdma
