// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/net/carrier/rdma/connection_control.h"

#include <netinet/in.h>

#include <array>
#include <climits>
#include <cstdlib>
#include <tuple>
#include <vector>

#include "iree/async/platform/io_uring/api.h"
#include "iree/async/platform/posix/api.h"
#include "iree/base/alignment.h"
#include "iree/net/carrier/rdma/connection_events.h"
#include "iree/net/rdma/region.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace iree::net::rdma {
namespace {

static void CheckCM(int result) {
  if (result) {
    iree_status_abort(iree_make_status(iree_status_code_from_errno(errno),
                                       "native CM operation: %s",
                                       strerror(errno)));
  }
}

static void CheckVerbs(int error) {
  if (error) {
    iree_status_abort(iree_make_status(iree_status_code_from_errno(error),
                                       "native verbs operation: %s",
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

struct ControlledAllocator {
  // Negative admits all allocations; zero fails without touching output.
  int allocations_before_failure = -1;
  // Count of live allocations through this owner.
  uint32_t live_allocations = 0;

  static iree_status_t Control(void* user_data,
                               iree_allocator_command_t command,
                               const void* params, void** inout_ptr) {
    auto* self = static_cast<ControlledAllocator*>(user_data);
    bool allocate = command == IREE_ALLOCATOR_COMMAND_MALLOC ||
                    command == IREE_ALLOCATOR_COMMAND_CALLOC;
    if (allocate && self->allocations_before_failure == 0) {
      return iree_make_status(IREE_STATUS_RESOURCE_EXHAUSTED,
                              "intentional allocation failure");
    }
    if (allocate && self->allocations_before_failure > 0) {
      --self->allocations_before_failure;
    }
    bool release = command == IREE_ALLOCATOR_COMMAND_FREE && *inout_ptr;
    auto system = iree_allocator_system();
    iree_status_t status = system.ctl(system.self, command, params, inout_ptr);
    if (iree_status_is_ok(status)) {
      if (allocate) {
        ++self->live_allocations;
      }
      if (release) {
        --self->live_allocations;
      }
    }
    return status;
  }

  iree_allocator_t value() { return {this, Control}; }
};

class ControlPeer {
 public:
  enum Flag : uint32_t {
    kStarted = 1u << 0,
    kReady = 1u << 1,
    kStopping = 1u << 2,
    kStopped = 1u << 3,
    kDestroyOnStop = 1u << 4,
    kStopOnReady = 1u << 5,
    kStopOnRecord = 1u << 6,
    kRemoteDataReady = 1u << 7,
    kStopAfterAccept = 1u << 8,
  };

  ControlPeer(iree_net_rdma_context_t* context, iree_async_proactor_t* proactor,
              uint32_t service_batch_size, uint32_t identity,
              iree_allocator_t allocator = iree_allocator_system())
      : context_(context), proactor_(proactor), identity_(identity) {
    library_ = iree_net_rdma_context_library(context);
    std::array<uint8_t, 4> hello = {};
    iree_unaligned_store_le_u32(hello.data(), identity);
    iree_net_rdma_connection_control_options_t options = {};
    options.send_count = 3;
    options.receive_count = 2;
    options.data_work_capacity = 4;
    options.service_batch_size = service_batch_size;
    options.resolution_timeout_ms = INT_MAX;
    options.minimum_rnr_timer = 1;
    IREE_CHECK_OK(iree_net_rdma_connection_control_create(
        context, proactor, options,
        iree_make_const_byte_span(hello.data(), hello.size()),
        {+[](void* user_data, iree_const_byte_span_t private_data) {
           auto* self = static_cast<ControlPeer*>(user_data);
           ASSERT_GE(private_data.data_length, 4u);
           EXPECT_EQ(iree_unaligned_load_le_u32(private_data.data),
                     1 - self->identity_);
           self->flags_ |= kReady;
           if (self->has(kStopOnReady)) {
             self->Stop();
           } else {
             self->Pump();
           }
         },
         +[](void* user_data, iree_const_byte_span_t record) -> iree_status_t {
           auto* self = static_cast<ControlPeer*>(user_data);
           EXPECT_TRUE(self->has(kReady));
           EXPECT_EQ(record.data_length, IREE_NET_RDMA_CONTROL_RECORD_SIZE);
           const uint8_t* bytes = record.data;
           if (iree_unaligned_load_le_u32(bytes) == 2) {
             self->remote_data_.queue_number =
                 iree_unaligned_load_le_u32(bytes + 4);
             self->remote_data_.sequence_number =
                 iree_unaligned_load_le_u32(bytes + 8);
             self->remote_data_.address =
                 iree_unaligned_load_le_u64(bytes + 16);
             self->remote_data_.key = iree_unaligned_load_le_u32(bytes + 24);
             self->flags_ |= kRemoteDataReady;
           } else {
             EXPECT_EQ(iree_unaligned_load_le_u32(bytes), 1u);
             EXPECT_EQ(iree_unaligned_load_le_u32(bytes + 4), self->received_);
             for (uint32_t i = 8; i < record.data_length; ++i) {
               EXPECT_EQ(bytes[i], uint8_t(i ^ self->received_ ^
                                           ((1 - self->identity_) << 4)));
             }
             ++self->received_;
           }
           if (self->has(kStopOnRecord)) {
             self->Stop();
           }
           return iree_ok_status();
         },
         +[](void* user_data) { static_cast<ControlPeer*>(user_data)->Pump(); },
         +[](void* user_data, iree_host_size_t count,
             const ibv_wc* completions) {
           auto* self = static_cast<ControlPeer*>(user_data);
           for (iree_host_size_t i = 0; i < count; ++i) {
             EXPECT_EQ(completions[i].status, IBV_WC_SUCCESS);
             self->data_completions_.push_back(completions[i]);
           }
         },
         +[](void* user_data, iree_status_t status) {
           auto* self = static_cast<ControlPeer*>(user_data);
           ++self->error_count_;
           self->error_code_ = iree_status_code(status);
           iree_status_free(status);
           self->Stop();
         },
         this},
        allocator, &control_));
  }

  ~ControlPeer() {
    if (has(kStarted)) {
      Stop();
      PollUntil(proactor_, [&] { return has(kStopped); });
    }
    iree_net_rdma_connection_control_destroy(control_);
    iree_async_region_release(data_region_);
  }

  void Connect(const iree_async_address_t& address) {
    flags_ |= kStarted;
    iree_net_rdma_connection_control_connect(control_, &address);
  }

  void Accept(const rdma_cm_event& event) {
    flags_ |= kStarted;
    iree_net_rdma_connection_control_accept(
        control_, event.id,
        iree_make_const_byte_span(event.param.conn.private_data,
                                  event.param.conn.private_data_len));
    if (has(kStopAfterAccept)) {
      Stop();
    }
  }

  void Stop() {
    if (has(kStopping)) {
      return;
    }
    flags_ |= kStopping;
    RetireData();
    iree_net_rdma_connection_control_deactivate(
        control_, {+[](void* user_data) {
                     auto* self = static_cast<ControlPeer*>(user_data);
                     self->flags_ |= kStopped;
                     if (self->has(kDestroyOnStop)) {
                       iree_net_rdma_connection_control_destroy(self->control_);
                       self->control_ = nullptr;
                     }
                   },
                   this});
  }

  void Pump() {
    std::array<uint8_t, IREE_NET_RDMA_CONTROL_RECORD_SIZE> record = {};
    while (sent_ < send_goal_) {
      iree_unaligned_store_le_u32(record.data(), 1);
      iree_unaligned_store_le_u32(record.data() + 4, sent_);
      for (uint32_t i = 8; i < record.size(); ++i) {
        record[i] = uint8_t(i ^ sent_ ^ (identity_ << 4));
      }
      if (!iree_net_rdma_connection_control_try_send(control_, record.data())) {
        break;
      }
      ++sent_;
    }
  }

  void PrepareData() {
    ibv_qp_init_attr options = {};
    options.qp_type = IBV_QPT_RC;
    options.send_cq =
        iree_net_rdma_connection_control_completion_queue(control_);
    options.recv_cq = options.send_cq;
    options.cap.max_send_wr = 2;
    options.cap.max_recv_wr = 2;
    options.cap.max_send_sge = 1;
    options.cap.max_recv_sge = 1;
    data_queue_ = library_->ibv_create_qp(
        iree_net_rdma_context_protection_domain(context_), &options);
    ASSERT_NE(data_queue_, nullptr);
    auto slab_options = iree_async_slab_options_default();
    slab_options.buffer_size = 4096;
    slab_options.buffer_count = 2;
    iree_async_slab_t* slab = nullptr;
    IREE_ASSERT_OK(
        iree_async_slab_create(slab_options, iree_allocator_system(), &slab));
    IREE_ASSERT_OK(iree_net_rdma_region_register_slab(
        context_, slab, uint64_t(identity_ + 1) * 0x10000000,
        IREE_ASYNC_BUFFER_ACCESS_FLAG_READ |
            IREE_ASYNC_BUFFER_ACCESS_FLAG_WRITE |
            IREE_ASYNC_BUFFER_ACCESS_FLAG_REMOTE_WRITE,
        iree_allocator_system(), &data_region_));
    iree_async_slab_release(slab);
    std::array<uint8_t, IREE_NET_RDMA_CONTROL_RECORD_SIZE> record = {};
    iree_unaligned_store_le_u32(record.data(), 2);
    iree_unaligned_store_le_u32(record.data() + 4, data_queue_->qp_num);
    iree_unaligned_store_le_u32(record.data() + 8, 0x123400 + identity_);
    iree_unaligned_store_le_u64(record.data() + 16,
                                data_region_->handles.rdma.address + 4096);
    iree_unaligned_store_le_u32(record.data() + 24,
                                data_region_->handles.rdma.rkey);
    ASSERT_TRUE(
        iree_net_rdma_connection_control_try_send(control_, record.data()));
  }

  void ConnectData() {
    ASSERT_TRUE(has(kRemoteDataReady));
    IREE_ASSERT_OK(iree_net_rdma_connection_route_connect_queue(
        context_, iree_net_rdma_connection_control_route(control_), data_queue_,
        0x123400 + identity_, remote_data_.queue_number,
        remote_data_.sequence_number));
  }

  void WriteData(ControlPeer& peer) {
    ibv_recv_wr receive = {};
    receive.wr_id = 2;
    ibv_recv_wr* rejected_receive = nullptr;
    CheckVerbs(ibv_post_recv(peer.data_queue_, &receive, &rejected_receive));
    ibv_sge span = {data_region_->handles.rdma.address, 256,
                    data_region_->handles.rdma.lkey};
    ibv_send_wr request = {};
    request.wr_id = 1;
    request.sg_list = &span;
    request.num_sge = 1;
    request.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    request.send_flags = IBV_SEND_SIGNALED;
    request.wr.rdma.remote_addr = remote_data_.address;
    request.wr.rdma.rkey = remote_data_.key;
    ibv_send_wr* rejected_send = nullptr;
    size_t local_goal = data_completions_.size() + 1;
    size_t peer_goal = peer.data_completions_.size() + 1;
    CheckVerbs(ibv_post_send(data_queue_, &request, &rejected_send));
    PollUntil(proactor_, [&] {
      return data_completions_.size() == local_goal &&
             peer.data_completions_.size() == peer_goal;
    });
    EXPECT_EQ(data_completions_.back().wr_id, 1u);
    EXPECT_EQ(peer.data_completions_.back().wr_id, 2u);
  }

  void RetireData() {
    if (!data_queue_) {
      return;
    }
    ibv_qp_attr attributes = {};
    attributes.qp_state = IBV_QPS_ERR;
    CheckVerbs(library_->ibv_modify_qp(data_queue_, &attributes, IBV_QP_STATE));
    CheckVerbs(library_->ibv_destroy_qp(data_queue_));
    data_queue_ = nullptr;
  }

  bool has(Flag flag) const { return (flags_ & flag) != 0; }
  void add_flags(uint32_t flags) { flags_ |= flags; }
  uint32_t sent() const { return sent_; }
  uint32_t received() const { return received_; }
  uint32_t error_count() const { return error_count_; }
  iree_status_code_t error_code() const { return error_code_; }
  void set_send_goal(uint32_t value) { send_goal_ = value; }
  uint8_t* source() { return static_cast<uint8_t*>(data_region_->base_ptr); }
  uint8_t* target() { return source() + 4096; }

 private:
  // Borrowed fixture owners outlive the control's joined native work.
  iree_net_rdma_context_t* context_;
  // Borrowed callback executor for explicit test progress.
  iree_async_proactor_t* proactor_;
  // Borrowed exact native symbols owned by context_.
  const iree_net_rdma_library_t* library_;
  // Owned production control; optional destruction occurs in its joined
  // callback.
  iree_net_rdma_connection_control_t* control_ = nullptr;
  // Distinct peer identity carried through actual native handshake bytes.
  uint32_t identity_;
  // Observed lifecycle and explicit callback-action configuration.
  uint32_t flags_ = 0;
  // Requested finite record stream, independent of bounded native capacity.
  uint32_t send_goal_ = 0;
  // Number of accepted control records.
  uint32_t sent_ = 0;
  // Number of checked incoming control records.
  uint32_t received_ = 0;
  // Number of terminal error notifications.
  uint32_t error_count_ = 0;
  // Last terminal diagnostic, consumed at the test ownership boundary.
  iree_status_code_t error_code_ = IREE_STATUS_OK;
  // Independently owned data QP sharing the production control's CQ.
  ibv_qp* data_queue_ = nullptr;
  // Independently retained source/target storage, kept after control
  // retirement.
  iree_async_region_t* data_region_ = nullptr;
  // Facts received over the actual private control QP, not copied from a peer.
  struct {
    // Remote data QP number.
    uint32_t queue_number = 0;
    // Remote data QP initial packet sequence.
    uint32_t sequence_number = 0;
    // Remote registered target IOVA, distinct from its CPU address.
    uint64_t address = 0;
    // Key for the peer's actual retained registration.
    uint32_t key = 0;
  } remote_data_;
  // Native data completion identities forwarded by the shared control service.
  std::vector<ibv_wc> data_completions_;
};

class ConnectionControlTest
    : public ::testing::TestWithParam<std::tuple<const char*, uint32_t>> {
 protected:
  void SetUp() override {
    const char* address = std::getenv("IREE_NET_RDMA_CM_TEST_ADDRESS");
    if (!address) {
      GTEST_SKIP() << "Set IREE_NET_RDMA_CM_TEST_ADDRESS and its device for "
                      "native control qualification.";
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
           auto* self = static_cast<ConnectionControlTest*>(user_data);
           ASSERT_EQ(event->event, RDMA_CM_EVENT_CONNECT_REQUEST);
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
    CheckCM(library_->rdma_listen(listener_id_, 32));
    auto* bound = rdma_get_local_addr(listener_id_);
    address_.length = bound->sa_family == AF_INET ? sizeof(sockaddr_in)
                                                  : sizeof(sockaddr_in6);
    memcpy(address_.storage, bound, address_.length);
  }

  void StopListener() {
    if (listener_id_) {
      CheckCM(library_->rdma_destroy_id(listener_id_));
      listener_id_ = nullptr;
    }
    if (listener_events_) {
      bool stopped = false;
      iree_net_rdma_connection_events_deactivate(
          listener_events_,
          {+[](void* value) { *static_cast<bool*>(value) = true; }, &stopped});
      PollUntil(proactor_, [&] { return stopped; });
      iree_net_rdma_connection_events_destroy(listener_events_);
      listener_events_ = nullptr;
    }
  }

  void TearDown() override {
    StopListener();
    if (proactor_) {
      EXPECT_EQ(proactor_->progress_list, nullptr);
    }
    iree_async_proactor_release(proactor_);
    iree_net_rdma_context_release(context_);
  }

  void Connect(ControlPeer& first, ControlPeer& second) {
    accept_target_ = &second;
    first.Connect(address_);
    PollUntil(proactor_, [&] {
      return first.has(ControlPeer::kReady) && second.has(ControlPeer::kReady);
    });
  }

  // Explicit shared native context used by both peers and their registrations.
  iree_net_rdma_context_t* context_ = nullptr;
  // Actual configured proactor backend, never a native-progress substitute.
  iree_async_proactor_t* proactor_ = nullptr;
  // Borrowed native entry points for the independently owned listener.
  const iree_net_rdma_library_t* library_ = nullptr;
  // Owned native listener monitoring.
  iree_net_rdma_connection_events_t* listener_events_ = nullptr;
  // Owned listener identity, destroyed before its channel.
  rdma_cm_id* listener_id_ = nullptr;
  // Actual bound address with a dynamic local port.
  iree_async_address_t address_ = {};
  // Peer receiving the acknowledged native request, or NULL to reject it.
  ControlPeer* accept_target_ = nullptr;
};

TEST_P(ConnectionControlTest, BoundedBidirectionalRecordsAndIsolatedTail) {
  ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0);
  ControlPeer second(context_, proactor_, std::get<1>(GetParam()), 1);
  first.set_send_goal(129);
  second.set_send_goal(129);
  Connect(first, second);
  StopListener();
  PollUntil(proactor_, [&] {
    return first.received() == 129 && second.received() == 129;
  });
  EXPECT_EQ(first.sent(), 129u);
  EXPECT_EQ(second.sent(), 129u);
  first.set_send_goal(130);
  first.Pump();
  PollUntil(proactor_, [&] { return second.received() == 130; });
  EXPECT_EQ(first.error_count(), 0u);
  EXPECT_EQ(second.error_count(), 0u);
}

TEST_P(ConnectionControlTest, SharedCQRoutesDataAndRetainsTargetAfterClose) {
  ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0);
  ControlPeer second(context_, proactor_, std::get<1>(GetParam()), 1);
  Connect(first, second);
  StopListener();
  ASSERT_NO_FATAL_FAILURE(first.PrepareData());
  ASSERT_NO_FATAL_FAILURE(second.PrepareData());
  PollUntil(proactor_, [&] {
    return first.has(ControlPeer::kRemoteDataReady) &&
           second.has(ControlPeer::kRemoteDataReady);
  });
  ASSERT_NO_FATAL_FAILURE(first.ConnectData());
  ASSERT_NO_FATAL_FAILURE(second.ConnectData());
  for (uint32_t i = 0; i < 256; ++i) {
    first.source()[i] = uint8_t(i ^ 0x6a);
  }
  ASSERT_NO_FATAL_FAILURE(first.WriteData(second));
  for (uint32_t i = 0; i < 256; ++i) {
    ASSERT_EQ(second.target()[i], uint8_t(i ^ 0x6a));
    second.source()[i] = second.target()[i] ^ 0x93;
  }
  ASSERT_NO_FATAL_FAILURE(second.WriteData(first));
  for (uint32_t i = 0; i < 256; ++i) {
    ASSERT_EQ(first.target()[i], uint8_t(i ^ 0x6a ^ 0x93));
  }
  EXPECT_EQ(first.error_count(), 0u);
  EXPECT_EQ(second.error_count(), 0u);
  first.add_flags(ControlPeer::kDestroyOnStop);
  second.add_flags(ControlPeer::kDestroyOnStop);
  first.Stop();
  PollUntil(proactor_, [&] {
    return first.has(ControlPeer::kStopped) &&
           second.has(ControlPeer::kStopped);
  });
  for (uint32_t i = 0; i < 256; ++i) {
    EXPECT_EQ(first.target()[i], uint8_t(i ^ 0x6a ^ 0x93));
    EXPECT_EQ(second.target()[i], uint8_t(i ^ 0x6a));
  }
}

TEST_P(ConnectionControlTest, CancelResolutionWithoutWaitingForPeer) {
  ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0);
  first.add_flags(ControlPeer::kDestroyOnStop);
  first.Connect(address_);
  first.Stop();
  PollUntil(proactor_, [&] { return first.has(ControlPeer::kStopped); });
  EXPECT_FALSE(first.has(ControlPeer::kReady));
}

TEST_P(ConnectionControlTest, RejectionJoinsSetupAndDestroysInCallback) {
  ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0);
  first.add_flags(ControlPeer::kDestroyOnStop);
  first.Connect(address_);
  PollUntil(proactor_, [&] { return first.has(ControlPeer::kStopped); });
  EXPECT_EQ(first.error_count(), 1u);
  EXPECT_EQ(first.error_code(), IREE_STATUS_UNAVAILABLE);
  EXPECT_FALSE(first.has(ControlPeer::kReady));
}

TEST_P(ConnectionControlTest, CloseFromReadyWithNativeReceivesPosted) {
  ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0);
  ControlPeer second(context_, proactor_, std::get<1>(GetParam()), 1);
  first.add_flags(ControlPeer::kDestroyOnStop | ControlPeer::kStopOnReady);
  second.add_flags(ControlPeer::kDestroyOnStop);
  accept_target_ = &second;
  first.Connect(address_);
  PollUntil(proactor_, [&] {
    return first.has(ControlPeer::kStopped) &&
           second.has(ControlPeer::kStopped);
  });
  EXPECT_TRUE(first.has(ControlPeer::kReady));
}

TEST_P(ConnectionControlTest, CloseFromReceiveWithFullControlWindows) {
  ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0);
  ControlPeer second(context_, proactor_, std::get<1>(GetParam()), 1);
  first.add_flags(ControlPeer::kDestroyOnStop | ControlPeer::kStopOnRecord);
  second.add_flags(ControlPeer::kDestroyOnStop);
  first.set_send_goal(129);
  second.set_send_goal(129);
  accept_target_ = &second;
  first.Connect(address_);
  PollUntil(proactor_, [&] {
    return first.has(ControlPeer::kStopped) &&
           second.has(ControlPeer::kStopped);
  });
  EXPECT_EQ(first.received(), 1u);
  EXPECT_LT(first.sent(), 129u);
}

TEST_P(ConnectionControlTest, SetupAllocationFailureJoinsArmedMonitor) {
  // The owner allocation/registration succeeds before setup begins. Failing
  // each subsequent service allocation exercises both no-monitor and already-
  // armed-CQ rollback, without substituting native providers or service calls.
  for (int budget = 0; budget < 2; ++budget) {
    ControlledAllocator allocator;
    {
      ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0,
                        allocator.value());
      first.add_flags(ControlPeer::kDestroyOnStop);
      allocator.allocations_before_failure = budget;
      first.Connect(address_);
      PollUntil(proactor_, [&] { return first.has(ControlPeer::kStopped); });
      EXPECT_EQ(first.error_count(), 1u);
      EXPECT_EQ(first.error_code(), IREE_STATUS_RESOURCE_EXHAUSTED);
      EXPECT_FALSE(first.has(ControlPeer::kReady));
    }
    EXPECT_EQ(allocator.live_allocations, 0u);
  }
}

TEST_P(ConnectionControlTest, CancelAcceptedSetupBeforeNativeReady) {
  ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0);
  ControlPeer second(context_, proactor_, std::get<1>(GetParam()), 1);
  first.add_flags(ControlPeer::kDestroyOnStop);
  second.add_flags(ControlPeer::kDestroyOnStop | ControlPeer::kStopAfterAccept);
  accept_target_ = &second;
  first.Connect(address_);
  PollUntil(proactor_, [&] {
    return first.has(ControlPeer::kStopped) &&
           second.has(ControlPeer::kStopped);
  });
  EXPECT_FALSE(second.has(ControlPeer::kReady));
  EXPECT_EQ(first.error_count(), 1u);
}

TEST_P(ConnectionControlTest, AcceptedSetupFailureRetiresUnmigratedID) {
  for (int budget = 0; budget < 2; ++budget) {
    ControlledAllocator allocator;
    {
      ControlPeer first(context_, proactor_, std::get<1>(GetParam()), 0);
      ControlPeer second(context_, proactor_, std::get<1>(GetParam()), 1,
                         allocator.value());
      first.add_flags(ControlPeer::kDestroyOnStop);
      second.add_flags(ControlPeer::kDestroyOnStop);
      allocator.allocations_before_failure = budget;
      accept_target_ = &second;
      first.Connect(address_);
      PollUntil(proactor_, [&] {
        return first.has(ControlPeer::kStopped) &&
               second.has(ControlPeer::kStopped);
      });
      EXPECT_EQ(second.error_count(), 1u);
      EXPECT_EQ(second.error_code(), IREE_STATUS_RESOURCE_EXHAUSTED);
      EXPECT_EQ(first.error_count(), 1u);
      EXPECT_FALSE(second.has(ControlPeer::kReady));
      accept_target_ = nullptr;
    }
    EXPECT_EQ(allocator.live_allocations, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(Backends, ConnectionControlTest,
                         ::testing::Combine(::testing::Values("io_uring",
                                                              "posix"),
                                            ::testing::Values(1u, 8u)));

}  // namespace
}  // namespace iree::net::rdma
