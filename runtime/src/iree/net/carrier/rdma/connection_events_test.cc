// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/net/carrier/rdma/connection_events.h"

#include <limits.h>
#include <netinet/in.h>

#include <array>
#include <cstdlib>
#include <tuple>

#include "iree/async/platform/io_uring/api.h"
#include "iree/async/platform/posix/api.h"
#include "iree/base/alignment.h"
#include "iree/net/carrier/rdma/completion_queue.h"
#include "iree/net/rdma/region.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace iree::net::rdma {
namespace {

static void CheckCM(int result) {
  if (result) {
    iree_status_abort(iree_make_status(IREE_STATUS_UNAVAILABLE,
                                       "native CM operation failed (%d): %s",
                                       result, strerror(errno)));
  }
}

static void CheckVerbs(int error) {
  if (error) {
    iree_status_abort(iree_make_status(iree_status_code_from_errno(error),
                                       "native RDMA operation: %s",
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

static void StopEvents(iree_async_proactor_t* proactor,
                       iree_net_rdma_connection_events_t* events) {
  bool joined = false;
  iree_net_rdma_connection_events_deactivate(
      events,
      {+[](void* value) { *static_cast<bool*>(value) = true; }, &joined});
  PollUntil(proactor, [&] { return joined; });
  iree_net_rdma_connection_events_destroy(events);
}

// Native CM/QP owner consuming the production registration and event services.
// Its small bootstrap record describes a checked target for this test, not the
// transport's public target-description wire format.
class NativeConnection {
 public:
  enum StateBits : uint32_t {
    kEstablished = 1u << 0,
    kDisconnected = 1u << 1,
    kRejected = 1u << 2,
    kDisconnectRequested = 1u << 3,
  };

  NativeConnection(iree_net_rdma_context_t* context,
                   iree_async_proactor_t* proactor, uint32_t service_batch_size,
                   uint64_t address)
      : context_(context), proactor_(proactor) {
    library_ = iree_net_rdma_context_library(context);
    IREE_CHECK_OK(iree_net_rdma_connection_events_create(
        context, proactor, service_batch_size,
        {+[](void* user_data, const rdma_cm_event* event) {
           static_cast<NativeConnection*>(user_data)->OnEvent(*event);
         },
         +[](void*, iree_status_t status) { iree_status_abort(status); }, this},
        iree_allocator_system(), &events_));
    iree_net_rdma_completion_queue_options_t queue_options = {};
    queue_options.capacity = 8;
    queue_options.service_batch_size = service_batch_size;
    IREE_CHECK_OK(iree_net_rdma_completion_queue_create(
        context, proactor, queue_options,
        {+[](void* user_data, iree_host_size_t count,
             const ibv_wc* completions) {
           auto* self = static_cast<NativeConnection*>(user_data);
           for (iree_host_size_t i = 0; i < count; ++i) {
             ASSERT_GT(self->pending_, 0u);
             --self->pending_;
             EXPECT_EQ(completions[i].status, IBV_WC_SUCCESS);
             EXPECT_EQ(completions[i].wr_id, self->expected_completion_id_);
           }
         },
         +[](void*, iree_status_t status) { iree_status_abort(status); }, this},
        iree_allocator_system(), &completions_));
    auto slab_options = iree_async_slab_options_default();
    slab_options.buffer_size = 8192;
    slab_options.buffer_count = 1;
    iree_async_slab_t* slab = nullptr;
    IREE_CHECK_OK(
        iree_async_slab_create(slab_options, iree_allocator_system(), &slab));
    IREE_CHECK_OK(iree_net_rdma_region_register_slab(
        context, slab, address,
        IREE_ASYNC_BUFFER_ACCESS_FLAG_READ |
            IREE_ASYNC_BUFFER_ACCESS_FLAG_WRITE |
            IREE_ASYNC_BUFFER_ACCESS_FLAG_REMOTE_WRITE,
        iree_allocator_system(), &region_));
    iree_async_slab_release(slab);
    iree_unaligned_store_le_u64(hello_.data(), address + 4096);
    iree_unaligned_store_le_u32(hello_.data() + 8, region_->handles.rdma.rkey);
    iree_unaligned_store_le_u32(hello_.data() + 12, 256);
  }

  ~NativeConnection() {
    EXPECT_EQ(pending_, 0u);
    if (id_) {
      if (id_->qp) {
        ibv_qp_attr attributes = {};
        attributes.qp_state = IBV_QPS_ERR;
        CheckVerbs(library_->ibv_modify_qp(id_->qp, &attributes, IBV_QP_STATE));
        // CQs belong to the production service, not the CM convenience API.
        EXPECT_EQ(id_->send_cq, nullptr);
        EXPECT_EQ(id_->recv_cq, nullptr);
        CheckVerbs(library_->ibv_destroy_qp(id_->qp));
        id_->qp = nullptr;
      }
      CheckCM(library_->rdma_destroy_id(id_));
    }
    StopEvents(proactor_, events_);
    bool joined = false;
    iree_net_rdma_completion_queue_deactivate(
        completions_,
        {+[](void* value) { *static_cast<bool*>(value) = true; }, &joined});
    PollUntil(proactor_, [&] { return joined; });
    iree_net_rdma_completion_queue_destroy(completions_);
    iree_async_region_release(region_);
  }

  void Connect(iree_async_address_t address) {
    CheckCM(library_->rdma_create_id(
        iree_net_rdma_connection_events_handle(events_), &id_, this,
        RDMA_PS_TCP));
    // Native resolution requires a finite millisecond argument. The outer
    // test harness, not an in-test deadline, diagnoses a stuck local route.
    CheckCM(library_->rdma_resolve_addr(
        id_, nullptr, reinterpret_cast<sockaddr*>(address.storage), INT_MAX));
  }

  void Accept(const rdma_cm_event& event) {
    id_ = event.id;
    ASSERT_NO_FATAL_FAILURE(ReadTarget(event.param.conn));
    CheckCM(library_->rdma_migrate_id(
        id_, iree_net_rdma_connection_events_handle(events_)));
    id_->context = this;
    CreateQueue();
    auto options = ConnectOptions();
    CheckCM(library_->rdma_accept(id_, &options));
  }

  void Disconnect() {
    state_ |= kDisconnectRequested;
    CheckCM(library_->rdma_disconnect(id_));
  }

  bool has_state(StateBits state) const { return (state_ & state) != 0; }

  uint8_t* source() { return static_cast<uint8_t*>(region_->base_ptr); }
  uint8_t* target() { return source() + 4096; }

  void Write(NativeConnection& peer) {
    ASSERT_EQ(pending_, 0u);
    ASSERT_EQ(peer.pending_, 0u);
    ibv_recv_wr receive = {};
    receive.wr_id = 2;
    ibv_recv_wr* rejected_receive = nullptr;
    CheckVerbs(ibv_post_recv(peer.id_->qp, &receive, &rejected_receive));
    peer.pending_ = 1;
    peer.expected_completion_id_ = 2;
    ibv_sge span = {region_->handles.rdma.address, 256,
                    region_->handles.rdma.lkey};
    ibv_send_wr request = {};
    request.wr_id = 1;
    request.sg_list = &span;
    request.num_sge = 1;
    request.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    request.send_flags = IBV_SEND_SIGNALED;
    request.wr.rdma.remote_addr = remote_target_.address;
    request.wr.rdma.rkey = remote_target_.key;
    ibv_send_wr* rejected_send = nullptr;
    CheckVerbs(ibv_post_send(id_->qp, &request, &rejected_send));
    pending_ = 1;
    expected_completion_id_ = 1;
    PollUntil(proactor_, [&] { return !pending_ && !peer.pending_; });
  }

 private:
  rdma_conn_param ConnectOptions() {
    rdma_conn_param options = {};
    options.private_data = hello_.data();
    options.private_data_len = hello_.size();
    options.responder_resources = 1;
    options.initiator_depth = 1;
    options.retry_count = 7;
    options.rnr_retry_count = 7;
    return options;
  }

  void CreateQueue() {
    EXPECT_EQ(id_->verbs, iree_net_rdma_context_device(context_));
    ibv_qp_init_attr options = {};
    options.qp_type = IBV_QPT_RC;
    options.send_cq = iree_net_rdma_completion_queue_handle(completions_);
    options.recv_cq = options.send_cq;
    options.cap.max_send_wr = 4;
    options.cap.max_recv_wr = 4;
    options.cap.max_send_sge = 1;
    options.cap.max_recv_sge = 1;
    CheckCM(library_->rdma_create_qp(
        id_, iree_net_rdma_context_protection_domain(context_), &options));
  }

  void ReadTarget(const rdma_conn_param& options) {
    ASSERT_GE(options.private_data_len, hello_.size());
    auto* data = static_cast<const uint8_t*>(options.private_data);
    remote_target_.address = iree_unaligned_load_le_u64(data);
    remote_target_.key = iree_unaligned_load_le_u32(data + 8);
    ASSERT_EQ(iree_unaligned_load_le_u32(data + 12), 256u);
  }

  void OnEvent(const rdma_cm_event& event) {
    EXPECT_EQ(event.id, id_);
    if (event.event != RDMA_CM_EVENT_REJECTED) {
      EXPECT_EQ(event.status, 0);
    }
    switch (event.event) {
      case RDMA_CM_EVENT_ADDR_RESOLVED:
        CheckCM(library_->rdma_resolve_route(id_, INT_MAX));
        break;
      case RDMA_CM_EVENT_ROUTE_RESOLVED: {
        CreateQueue();
        auto options = ConnectOptions();
        CheckCM(library_->rdma_connect(id_, &options));
        break;
      }
      case RDMA_CM_EVENT_ESTABLISHED:
        if (!remote_target_.address) {
          ReadTarget(event.param.conn);
        }
        state_ |= kEstablished;
        break;
      case RDMA_CM_EVENT_DISCONNECTED:
        // Native CM disconnect both initiates and replies to the handshake;
        // merely observing the request does not let its initiator finish.
        if (!has_state(kDisconnectRequested)) {
          Disconnect();
        }
        state_ |= kDisconnected;
        break;
      case RDMA_CM_EVENT_REJECTED:
        state_ |= kRejected;
        break;
      case RDMA_CM_EVENT_TIMEWAIT_EXIT:
        break;
      default:
        FAIL() << "Unexpected native CM event " << event.event;
    }
  }

  // Borrowed explicit owner retained by the production service/registration.
  iree_net_rdma_context_t* context_;
  // Borrowed caller-owned poll loop retained by each service.
  iree_async_proactor_t* proactor_;
  // Borrowed symbols from context_.
  const iree_net_rdma_library_t* library_;
  // Owned CM channel/monitor, independent of the listener.
  iree_net_rdma_connection_events_t* events_ = nullptr;
  // Owned CQ and event/progress service.
  iree_net_rdma_completion_queue_t* completions_ = nullptr;
  // Registered source/target backing retained through native teardown.
  iree_async_region_t* region_ = nullptr;
  // Explicit CM ID and its native QP.
  rdma_cm_id* id_ = nullptr;
  // Test bootstrap target description supplied as native private data.
  std::array<uint8_t, 16> hello_ = {};
  // Actual peer target learned through the production CM event callback.
  struct {
    // Peer NIC address, unrelated to the local CPU mapping.
    uint64_t address = 0;
    // Peer key authorizing the registered test target.
    uint32_t key = 0;
  } remote_target_;
  // Observed terminal/setup events, not an ordering model for completions.
  uint32_t state_ = 0;
  // Admitted native work awaiting exact CQE return in this test phase.
  uint32_t pending_ = 0;
  // The one WR identity submitted in this test phase.
  uint64_t expected_completion_id_ = 0;
};

class ConnectionEventsTest
    : public ::testing::TestWithParam<std::tuple<const char*, uint32_t>> {
 protected:
  void SetUp() override {
    const char* address = std::getenv("IREE_NET_RDMA_CM_TEST_ADDRESS");
    if (!address) {
      GTEST_SKIP() << "Set IREE_NET_RDMA_CM_TEST_ADDRESS to a routable local "
                      "IP:port and IREE_NET_RDMA_CM_TEST_DEVICE to its device.";
    }
    IREE_ASSERT_OK(iree_async_address_from_string(
        iree_make_cstring_view(address), &address_));
    auto context_options = iree_net_rdma_context_options_default();
    context_options.device_name =
        iree_make_cstring_view(std::getenv("IREE_NET_RDMA_CM_TEST_DEVICE"));
    IREE_ASSERT_OK(iree_net_rdma_context_create(
        context_options, iree_allocator_system(), &context_));
    library_ = iree_net_rdma_context_library(context_);
    auto options = iree_async_proactor_options_default();
    if (strcmp(std::get<0>(GetParam()), "io_uring") == 0) {
      IREE_ASSERT_OK(iree_async_proactor_create_io_uring(
          options, iree_allocator_system(), &proactor_));
    } else {
      IREE_ASSERT_OK(iree_async_proactor_create_posix(
          options, iree_allocator_system(), &proactor_));
    }
    IREE_ASSERT_OK(iree_net_rdma_connection_events_create(
        context_, proactor_, std::get<1>(GetParam()),
        {+[](void* user_data, const rdma_cm_event* event) {
           auto* self = static_cast<ConnectionEventsTest*>(user_data);
           ASSERT_EQ(event->event, RDMA_CM_EVENT_CONNECT_REQUEST);
           EXPECT_EQ(event->listen_id, self->listener_id_);
           if (self->accept_target_) {
             self->accept_target_->Accept(*event);
           } else {
             CheckCM(self->library_->rdma_reject(event->id, nullptr, 0));
             CheckCM(self->library_->rdma_destroy_id(event->id));
           }
         },
         +[](void*, iree_status_t status) { iree_status_abort(status); }, this},
        iree_allocator_system(), &listener_events_));
    CheckCM(library_->rdma_create_id(
        iree_net_rdma_connection_events_handle(listener_events_), &listener_id_,
        this, RDMA_PS_TCP));
    CheckCM(library_->rdma_bind_addr(
        listener_id_, reinterpret_cast<sockaddr*>(address_.storage)));
    CheckCM(library_->rdma_listen(listener_id_, 8));
    auto* bound = rdma_get_local_addr(listener_id_);
    address_.length = bound->sa_family == AF_INET ? sizeof(sockaddr_in)
                                                  : sizeof(sockaddr_in6);
    memcpy(address_.storage, bound, address_.length);
  }

  void TearDown() override {
    StopListener();
    iree_async_proactor_release(proactor_);
    iree_net_rdma_context_release(context_);
  }

  void StopListener() {
    if (listener_id_) {
      CheckCM(library_->rdma_destroy_id(listener_id_));
      listener_id_ = nullptr;
    }
    if (listener_events_) {
      StopEvents(proactor_, listener_events_);
      listener_events_ = nullptr;
    }
  }

  // Explicit selected native resources for the qualification device.
  iree_net_rdma_context_t* context_ = nullptr;
  // One real poll owner shared by independent native CM/CQ services.
  iree_async_proactor_t* proactor_ = nullptr;
  // Borrowed native symbols, valid through fixture teardown.
  const iree_net_rdma_library_t* library_ = nullptr;
  // Owned listener channel/monitor, retired independently of accepted IDs.
  iree_net_rdma_connection_events_t* listener_events_ = nullptr;
  // Owned listener ID, never used as an accepted connection ID.
  rdma_cm_id* listener_id_ = nullptr;
  // Actual dynamic listening address on the configured local interface.
  iree_async_address_t address_ = {};
  // Borrowed accepted-connection owner, or NULL to reject incoming requests.
  NativeConnection* accept_target_ = nullptr;
};

TEST_P(ConnectionEventsTest, AcceptedConnectionOutlivesListenerAndTransfers) {
  NativeConnection first(context_, proactor_, std::get<1>(GetParam()),
                         0x50000000);
  NativeConnection second(context_, proactor_, std::get<1>(GetParam()),
                          0x60000000);
  accept_target_ = &second;
  first.Connect(address_);
  PollUntil(proactor_, [&] {
    return first.has_state(NativeConnection::kEstablished) &&
           second.has_state(NativeConnection::kEstablished);
  });
  StopListener();
  for (uint32_t round = 0; round < 16; ++round) {
    for (uint32_t i = 0; i < 256; ++i) {
      first.source()[i] = uint8_t(i + round);
    }
    ASSERT_NO_FATAL_FAILURE(first.Write(second));
    for (uint32_t i = 0; i < 256; ++i) {
      ASSERT_EQ(second.target()[i], uint8_t(i + round));
      second.source()[i] = second.target()[i] ^ 0x5a;
    }
    ASSERT_NO_FATAL_FAILURE(second.Write(first));
    for (uint32_t i = 0; i < 256; ++i) {
      ASSERT_EQ(first.target()[i], (uint8_t(i + round) ^ 0x5a));
    }
  }
  first.Disconnect();
  PollUntil(proactor_, [&] {
    return first.has_state(NativeConnection::kDisconnected) &&
           second.has_state(NativeConnection::kDisconnected);
  });
}

TEST_P(ConnectionEventsTest, RejectedRequestRetiresItsIdInsideCallback) {
  NativeConnection connection(context_, proactor_, std::get<1>(GetParam()),
                              0x50000000);
  connection.Connect(address_);
  PollUntil(proactor_,
            [&] { return connection.has_state(NativeConnection::kRejected); });
}

INSTANTIATE_TEST_SUITE_P(Backends, ConnectionEventsTest,
                         ::testing::Combine(::testing::Values("io_uring",
                                                              "posix"),
                                            ::testing::Values(1u, 8u)));

}  // namespace
}  // namespace iree::net::rdma
