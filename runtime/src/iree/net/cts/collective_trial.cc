// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/net/cts/collective_trial.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "iree/base/alignment.h"
#include "iree/net/channel/queue/queue_channel.h"
#include "iree/net/connection.h"
#include "iree/net/direct_endpoint.h"

namespace iree::net::cts {
namespace {

// Rank-local data progress never reads another rank's application state.
// This owner coordinates only setup, phase boundaries and terminal failure.
struct Control {
  // Executors borrowed until every application thread joins.
  std::vector<iree_async_proactor_t*> proactors;
  // Current phase: zero for setup, one for warm-up and two for measurement.
  std::atomic<uint32_t> phase{0};
  // Terminal failure wakes both the coordinator and application poll owners.
  std::atomic<bool> failed{false};
  // Coordinator publication authorizing local retirement.
  std::atomic<bool> closing{false};
  // Protects phase arrivals and diagnostic ownership, never payload progress.
  std::mutex mutex;
  // Coordinator sleeps until all ranks arrive or a rank reports failure.
  std::condition_variable condition;
  // Number of ranks that have completed the current phase.
  size_t arrivals = 0;
  // Joined error returned after callback and native retirement.
  iree::Status error;

  void Wake() {
    for (auto* proactor : proactors) {
      iree_async_proactor_wake(proactor);
    }
  }
  void Fail(iree_status_t status) {
    if (iree_status_is_ok(status)) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      error = iree_status_join(error.release(), status);
      failed.store(true, std::memory_order_release);
    }
    condition.notify_all();
    Wake();
  }
  void EndpointError(iree_status_t status) {
    if (closing.load(std::memory_order_acquire) &&
        (iree_status_is_cancelled(status) ||
         iree_status_is_unavailable(status) ||
         iree_status_is_out_of_range(status))) {
      // All ranks are retiring; these are terminal peer-close receipts.
      iree_status_free(status);
    } else {
      Fail(status);
    }
  }
  void Arrive() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      ++arrivals;
    }
    condition.notify_one();
  }
  void Wait() {
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [&] {
      return arrivals == proactors.size() ||
             failed.load(std::memory_order_acquire);
    });
  }
  void Start(uint32_t value) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      arrivals = 0;
      phase.store(value, std::memory_order_release);
    }
    Wake();
  }
};

enum class Direction { kIncoming, kOutgoing };

// One directed data edge and its reverse consumed-coordinate control path.
struct Link {
  struct Source {
    // Stable callback owner, retained through connection deactivation.
    Link* link = nullptr;
    // Exact source completion byte count, including message framing above net.
    size_t expected_length = 0;
    // This slot cannot capture another source until its callback returns.
    bool pending = false;
  };
  struct Input {
    // Exact logical block occupying this slot; zero means free.
    uint32_t sequence = 0;
    // Borrowed received bytes, or the explicitly registered target view.
    iree_const_byte_span_t bytes = {};
    // Moved message storage, empty for explicit registered targets.
    iree_async_buffer_lease_t lease = {};
  };

  // Immutable workload dimensions and delivery strategy.
  const CollectiveTrialOptions& options;
  // Phase and failure owner, independent of edge progress.
  Control& control;
  // Whether this connection sends data or receives it.
  Direction direction;
  // Reusable target storage owned by the rank, not this borrowed view.
  iree_async_span_t storage;
  // Caller-owned connect operation joined before destruction.
  iree_net_transport_connect_operation_t connect_operation;
  // Owned connection, released only after asynchronous deactivation.
  iree_net_connection_t* connection = nullptr;
  // Message endpoint carrying the queue protocol.
  iree_net_message_endpoint_t message = {};
  // Optional registered-placement endpoint on the same connection.
  iree_net_direct_endpoint_t direct = {};
  // Queue protocol owner, freed after the connection join.
  iree_net_queue_channel_t* channel = nullptr;
  // Borrowed peer registration facts imported once during setup.
  iree_net_direct_target_t target = {};
  // Serialized target grant, retained only for setup.
  std::vector<uint8_t> description;
  // Callback records bounded independently of native SQ geometry.
  std::vector<Source> sources;
  // Application inputs bounded by the advertised consumption window.
  std::vector<Input> inputs;
  // Setup and retirement obligations owned by this poll thread.
  enum Flag : uint32_t {
    kConnecting = 1u << 0,
    kMessageReady = 1u << 1,
    kDirectReady = 1u << 2,
    kGrantSent = 1u << 3,
    kTargetReady = 1u << 4,
    kDrained = 1u << 5,
  };
  // Current setup and retirement obligations.
  uint32_t flags = 0;
  // Last admitted data block, independent of callback order.
  uint32_t submitted = 0;
  // Receiver-consumed prefix observed over the real control channel.
  uint32_t remote_consumed = 0;
  // Application-consumed local prefix, never inferred from placement order.
  uint32_t consumed = 0;
  // Last consumed coordinate admitted to the control channel.
  uint32_t published = 0;
  // Exact accepted data-source returns.
  uint64_t completions = 0;
  // Accepted control messages requiring terminal callbacks.
  uint64_t control_sends = 0;
  // Joined terminal control-message callbacks.
  uint64_t control_completions = 0;
  // Actual payload bytes sent, excluding control and queue headers.
  uint64_t payload_bytes = 0;
  // Maximum admitted but not yet consumed blocks.
  uint64_t high_water = 0;

  Link(const CollectiveTrialOptions& options, Control& control,
       Direction direction, iree_async_span_t storage)
      : options(options),
        control(control),
        direction(direction),
        storage(storage),
        sources(options.window_size),
        inputs(options.window_size) {
    iree_net_transport_connect_operation_initialize(&connect_operation);
    for (auto& source : sources) {
      source.link = this;
    }
  }
  ~Link() {
    for (auto& input : inputs) {
      iree_async_buffer_lease_release(&input.lease);
    }
    iree_net_queue_channel_free(channel);
    iree_net_connection_release(connection);
    iree_net_transport_connect_operation_deinitialize(&connect_operation);
  }
  bool Has(Flag flag) const { return (flags & flag) != 0; }
  bool Registered() const {
    return options.delivery == CollectiveDelivery::kRegistered;
  }
  bool Ready() const {
    return Has(kMessageReady) &&
           (!Registered() ||
            (Has(kDirectReady) &&
             Has(direction == Direction::kIncoming ? kGrantSent
                                                   : kTargetReady)));
  }
  static void OnError(void* value, iree_status_t status) {
    static_cast<Link*>(value)->control.EndpointError(status);
  }
  static void OnControlSent(void* value, iree_status_t status, size_t) {
    auto& link = *static_cast<Link*>(value);
    ++link.control_completions;
    link.control.EndpointError(status);
  }
  static void OnSourceReturned(void* value, iree_status_t status,
                               size_t transferred) {
    auto& source = *static_cast<Source*>(value);
    source.pending = false;
    ++source.link->completions;
    if (iree_status_is_ok(status) && transferred != source.expected_length) {
      status = iree_make_status(IREE_STATUS_DATA_LOSS,
                                "collective source length differs");
    }
    source.link->control.EndpointError(status);
  }
  iree_status_t Receive(uint32_t sequence, iree_const_byte_span_t bytes,
                        iree_async_buffer_lease_t* lease) {
    if (!sequence || sequence <= consumed ||
        sequence - consumed > options.window_size) {
      return iree_make_status(IREE_STATUS_DATA_LOSS,
                              "collective input exceeds its reuse window");
    }
    auto& input = inputs[(sequence - 1) % inputs.size()];
    if (input.sequence) {
      return iree_make_status(IREE_STATUS_DATA_LOSS,
                              "collective input overwrote an unconsumed slot");
    }
    input.sequence = sequence;
    input.bytes = bytes;
    if (lease) {
      input.lease = *lease;
      memset(lease, 0, sizeof(*lease));
    }
    return iree_ok_status();
  }
  static iree_status_t OnCommand(void* value, uint32_t queue_id,
                                 const iree_net_queue_frontier_view_t* waits,
                                 const iree_net_queue_frontier_view_t* signals,
                                 iree_const_byte_span_t payload,
                                 iree_async_buffer_lease_t* lease) {
    auto& link = *static_cast<Link*>(value);
    if (queue_id == 0 && link.direction == Direction::kOutgoing &&
        link.Registered() && link.description.empty() && !waits->count &&
        !signals->count) {
      link.description.assign(payload.data, payload.data + payload.data_length);
      return iree_ok_status();
    }
    if (queue_id != 1 || link.direction != Direction::kIncoming ||
        link.Registered() || waits->count || signals->count != 1 ||
        payload.data_length > link.options.block_size) {
      return iree_make_status(IREE_STATUS_DATA_LOSS,
                              "unexpected collective data message");
    }
    auto coordinate = iree_net_queue_frontier_view_get(signals, 0);
    if (coordinate.axis != 1 || coordinate.epoch > UINT32_MAX) {
      return iree_make_status(IREE_STATUS_DATA_LOSS,
                              "unexpected collective block coordinate");
    }
    return link.Receive(static_cast<uint32_t>(coordinate.epoch), payload,
                        lease);
  }
  static iree_status_t OnAdvance(void* value,
                                 const iree_net_queue_frontier_view_t* signals,
                                 iree_const_byte_span_t payload,
                                 iree_async_buffer_lease_t*) {
    auto& link = *static_cast<Link*>(value);
    if (link.direction != Direction::kOutgoing || signals->count != 1 ||
        payload.data_length) {
      return iree_make_status(IREE_STATUS_DATA_LOSS,
                              "unexpected collective consumed message");
    }
    auto coordinate = iree_net_queue_frontier_view_get(signals, 0);
    if (coordinate.axis != 1 || coordinate.epoch > link.submitted) {
      return iree_make_status(IREE_STATUS_DATA_LOSS,
                              "collective consumption exceeds submission");
    }
    link.remote_consumed =
        std::max(link.remote_consumed, static_cast<uint32_t>(coordinate.epoch));
    return iree_ok_status();
  }
  static iree_status_t OnPlacement(void* value, uint32_t sequence) {
    auto& link = *static_cast<Link*>(value);
    if (link.direction != Direction::kIncoming || !sequence) {
      return iree_make_status(IREE_STATUS_DATA_LOSS,
                              "unexpected collective placement");
    }
    size_t offset =
        ((sequence - 1) % link.inputs.size()) * link.options.block_size;
    return link.Receive(
        sequence,
        iree_make_const_byte_span(iree_async_span_ptr(link.storage) + offset,
                                  link.options.block_size),
        nullptr);
  }
  static void OnMessageReady(void* value, iree_status_t status,
                             iree_net_message_endpoint_t endpoint) {
    auto& link = *static_cast<Link*>(value);
    if (iree_status_is_ok(status)) {
      link.message = endpoint;
      status = iree_net_queue_channel_allocate(
          endpoint, {OnCommand, OnAdvance, OnError, &link},
          iree_allocator_system(), &link.channel);
    }
    if (iree_status_is_ok(status)) {
      iree_net_queue_channel_attach(link.channel);
      status = iree_net_message_endpoint_activate(endpoint);
    }
    if (iree_status_is_ok(status)) {
      link.flags |= kMessageReady;
    }
    link.control.EndpointError(status);
  }
  static void OnDirectReady(void* value, iree_status_t status,
                            iree_net_direct_endpoint_t endpoint) {
    auto& link = *static_cast<Link*>(value);
    if (iree_status_is_ok(status)) {
      link.direct = endpoint;
      iree_net_direct_endpoint_set_callbacks(endpoint,
                                             {OnPlacement, OnError, &link});
      status = iree_net_direct_endpoint_activate(endpoint);
    }
    if (iree_status_is_ok(status)) {
      link.flags |= kDirectReady;
    }
    link.control.EndpointError(status);
  }
  static void Connected(void* value, iree_status_t status,
                        iree_net_connection_t* connection) {
    auto& link = *static_cast<Link*>(value);
    link.flags &= ~kConnecting;
    link.connection = connection;
    if (iree_status_is_ok(status) &&
        !link.control.closing.load(std::memory_order_acquire)) {
      status = iree_net_connection_open_endpoint(connection,
                                                 {OnMessageReady, &link});
      if (iree_status_is_ok(status) && link.Registered()) {
        status = iree_net_connection_open_direct_endpoint(
            connection, {OnDirectReady, &link});
      }
    }
    link.control.EndpointError(status);
  }
  void Pump() {
    if (!Has(kMessageReady)) {
      return;
    }
    if (Registered() && Has(kDirectReady)) {
      if (direction == Direction::kOutgoing && !Has(kTargetReady) &&
          !description.empty()) {
        iree_status_t status = iree_net_direct_endpoint_import_target(
            direct,
            iree_make_const_byte_span(description.data(), description.size()),
            &target);
        if (iree_status_is_ok(status) && target.length != storage.length) {
          status = iree_make_status(IREE_STATUS_DATA_LOSS,
                                    "collective target geometry differs");
        }
        if (iree_status_is_ok(status)) {
          flags |= kTargetReady;
        }
        control.Fail(status);
      } else if (direction == Direction::kIncoming && !Has(kGrantSent) &&
                 iree_net_queue_channel_query_send_budget(channel).slots) {
        if (description.empty()) {
          std::array<uint8_t, 256> data;
          size_t length = 0;
          iree_status_t status = iree_net_direct_endpoint_export_target(
              direct, storage, IREE_ASYNC_BUFFER_ACCESS_FLAG_REMOTE_WRITE,
              iree_make_byte_span(data.data(), data.size()), &length);
          if (!iree_status_is_ok(status)) {
            control.Fail(status);
            return;
          }
          description.assign(data.data(), data.data() + length);
        }
        iree_net_queue_channel_send_params_t params = {};
        params.generated_payload_length = description.size();
        params.build =
            +[](void* value, const iree_net_queue_message_builder_t* builder) {
              auto& link = *static_cast<Link*>(value);
              memcpy(builder->generated_payload.data, link.description.data(),
                     link.description.size());
              return iree_ok_status();
            };
        params.build_user_data = this;
        params.completion_callback = {OnControlSent, this};
        iree_status_t status =
            iree_net_queue_channel_send_command(channel, 0, &params);
        if (iree_status_is_ok(status)) {
          flags |= kGrantSent;
          ++control_sends;
        }
        control.Fail(status);
      }
    }
    if (direction == Direction::kIncoming && consumed != published &&
        iree_net_queue_channel_query_send_budget(channel).slots) {
      iree_net_queue_channel_send_params_t params = {};
      params.signal_frontier_count = 1;
      params.build =
          +[](void* value, const iree_net_queue_message_builder_t* builder) {
            auto& link = *static_cast<Link*>(value);
            iree_net_queue_frontier_builder_set(&builder->signal_frontier, 0,
                                                {1, link.consumed});
            return iree_ok_status();
          };
      params.build_user_data = this;
      params.completion_callback = {OnControlSent, this};
      iree_status_t status =
          iree_net_queue_channel_send_advance(channel, &params);
      if (iree_status_is_ok(status)) {
        published = consumed;
        ++control_sends;
      }
      control.Fail(status);
    }
  }
  bool CanSend() const {
    if (!Ready() || submitted - remote_consumed == options.window_size ||
        sources[submitted % sources.size()].pending) {
      return false;
    }
    return Registered()
               ? iree_net_direct_endpoint_query_write_budget(direct).slots != 0
               : iree_net_queue_channel_query_send_budget(channel).slots != 0;
  }
  void Send(iree_async_span_t data) {
    uint32_t sequence = submitted + 1;
    auto& source = sources[submitted % sources.size()];
    source.pending = true;
    source.expected_length = data.length;
    iree_status_t status = iree_ok_status();
    if (Registered()) {
      iree_net_direct_write_entry_t entry = {
          data, &target, (submitted % sources.size()) * options.block_size};
      iree_net_direct_write_params_t params = {};
      params.flags = IREE_NET_DIRECT_WRITE_FLAG_NOTIFY;
      params.notification_cookie = sequence;
      params.entry_count = 1;
      params.entries = &entry;
      params.completion_callback = {OnSourceReturned, &source};
      status = iree_net_direct_endpoint_write(direct, &params);
    } else {
      source.expected_length += IREE_NET_QUEUE_MESSAGE_HEADER_SIZE +
                                IREE_NET_QUEUE_FRONTIER_ENTRY_SIZE;
      iree_net_queue_channel_send_params_t params = {};
      params.signal_frontier_count = 1;
      params.build = +[](void* value,
                         const iree_net_queue_message_builder_t* builder) {
        iree_net_queue_frontier_builder_set(
            &builder->signal_frontier, 0, {1, *static_cast<uint32_t*>(value)});
        return iree_ok_status();
      };
      params.build_user_data = &sequence;
      params.payload = iree_async_span_list_make(&data, 1);
      params.completion_callback = {OnSourceReturned, &source};
      status = iree_net_queue_channel_send_command(channel, 1, &params);
    }
    if (iree_status_is_ok(status)) {
      submitted = sequence;
      payload_bytes += data.length;
      high_water = std::max<uint64_t>(high_water, submitted - remote_consumed);
    } else {
      source.pending = false;
    }
    control.Fail(status);
  }
  const Input* NextInput() const {
    const auto& input = inputs[consumed % inputs.size()];
    return input.sequence == consumed + 1 ? &input : nullptr;
  }
  void Consume() {
    auto& input = inputs[consumed % inputs.size()];
    iree_async_buffer_lease_release(&input.lease);
    input = {};
    ++consumed;
  }
  bool Idle() const {
    return submitted == completions && submitted == remote_consumed &&
           consumed == published && control_sends == control_completions;
  }
};

struct Rank {
  // Shared immutable schedule dimensions.
  const CollectiveTrialOptions& options;
  // Bootstrap/phase/error owner, not collective progress.
  Control& control;
  // Position in the ring, independent of connection arrival order.
  uint32_t index;
  // Application-owned executor, polled only by this rank's thread.
  iree_async_proactor_t* proactor = nullptr;
  // Tensor followed by the fixed incoming block window.
  iree_async_slab_t* slab = nullptr;
  // Optional registration shared by both rank edges.
  iree_async_region_t* region = nullptr;
  // Explicit transport owner, independent of registration lifetime.
  iree_net_transport_factory_t* factory = nullptr;
  // Listener accepting the rank's single predecessor.
  iree_net_listener_t* listener = nullptr;
  // Numeric/native bound address copied during setup.
  std::string address;
  // Listener stop callback has joined every unpublished connection.
  bool listener_stopped = false;
  // Extra accepted connections being retired on this poll owner.
  size_t rejections = 0;
  // Incoming data and outgoing control owner.
  std::unique_ptr<Link> incoming;
  // Outgoing data and incoming control owner.
  std::unique_ptr<Link> outgoing;
  // Rank-local cumulative counters captured after each phase.
  std::array<CollectiveTrialResult, 2> results;

  Rank(const CollectiveTrialOptions& options, Control& control, uint32_t index)
      : options(options), control(control), index(index) {}
  ~Rank() {
    outgoing.reset();
    incoming.reset();
    iree_net_transport_factory_release(factory);
    iree_async_region_release(region);
    iree_async_slab_release(slab);
    iree_async_proactor_release(proactor);
  }
  iree_async_span_t Span(size_t offset, size_t length) const {
    return region ? iree_async_span_make(region, offset, length)
                  : iree_async_span_from_ptr(
                        static_cast<uint8_t*>(iree_async_slab_base_ptr(slab)) +
                            offset,
                        length);
  }
  iree_status_t Initialize(
      const TransportBackend& transport,
      const iree::async::cts::ProactorFactory& create_proactor) {
    auto proactor_options = iree_async_proactor_options_default();
    proactor_options.threading_mode =
        IREE_ASYNC_PROACTOR_THREADING_CROSS_THREAD;
    auto created = create_proactor(proactor_options);
    if (!created.ok()) {
      return std::move(created).status().release();
    }
    proactor = *created;
    auto slab_options = iree_async_slab_options_default();
    slab_options.buffer_size =
        options.tensor_size + options.block_size * options.window_size;
    slab_options.buffer_count = 1;
    IREE_RETURN_IF_ERROR(
        iree_async_slab_create(slab_options, iree_allocator_system(), &slab));
    if (options.delivery == CollectiveDelivery::kRegistered) {
      IREE_RETURN_IF_ERROR(transport.create_registered_factory(
          slab, iree_allocator_system(), &factory, &region));
    } else {
      IREE_RETURN_IF_ERROR(
          transport.create_factory(iree_allocator_system(), &factory));
    }
    auto target =
        Span(options.tensor_size, options.block_size * options.window_size);
    incoming =
        std::make_unique<Link>(options, control, Direction::kIncoming, target);
    outgoing =
        std::make_unique<Link>(options, control, Direction::kOutgoing, target);
    return iree_ok_status();
  }
  static void Accepted(void* value, iree_status_t status,
                       iree_net_connection_t* connection) {
    auto& rank = *static_cast<Rank*>(value);
    if (iree_status_is_ok(status) && !rank.incoming->connection) {
      Link::Connected(rank.incoming.get(), status, connection);
      return;
    }
    if (iree_status_is_ok(status)) {
      status = iree_make_status(IREE_STATUS_DATA_LOSS,
                                "unexpected extra collective connection");
    }
    rank.control.EndpointError(status);
    if (connection) {
      struct Rejection {
        // Rank remains alive through this callback.
        Rank* rank;
        // Transferred accept reference awaiting native retirement.
        iree_net_connection_t* connection;
      };
      ++rank.rejections;
      auto* rejection = new Rejection{&rank, connection};
      iree_net_connection_deactivate(
          connection, {+[](void* value) {
                         auto* rejection = static_cast<Rejection*>(value);
                         --rejection->rank->rejections;
                         iree_net_connection_release(rejection->connection);
                         delete rejection;
                       },
                       rejection});
    }
  }
  iree_status_t Listen(const TransportBackend& transport) {
    std::string requested;
    IREE_RETURN_IF_ERROR(transport.make_bind_address(&requested));
    IREE_RETURN_IF_ERROR(iree_net_transport_factory_create_listener(
        factory, iree_make_string_view(requested.data(), requested.size()),
        proactor, nullptr, {Accepted, this}, iree_allocator_system(),
        &listener));
    std::array<char, 1024> data;
    iree_string_view_t bound;
    IREE_RETURN_IF_ERROR(iree_net_listener_query_bound_address(
        listener, data.size(), data.data(), &bound));
    address.assign(bound.data, bound.size);
    return iree_ok_status();
  }
  void Poll() {
    control.Fail(
        iree_async_proactor_poll(proactor, iree_infinite_timeout(), nullptr));
  }
  void Pump() {
    incoming->Pump();
    outgoing->Pump();
  }
  static uint32_t Value(uint32_t rank, uint32_t round, size_t element) {
    return rank * 19u + round * 37u + static_cast<uint32_t>(element) * 13u + 1u;
  }
  void Reduce(uint32_t round) {
    auto* tensor = static_cast<uint32_t*>(iree_async_slab_base_ptr(slab));
    size_t elements = options.tensor_size / sizeof(uint32_t);
    for (size_t i = 0; i < elements; ++i) {
      tensor[i] = Value(index, round, i);
    }
    size_t shard_size = options.tensor_size / options.rank_count;
    size_t chunks = 1 + (shard_size - 1) / options.block_size;
    for (uint32_t step = 0; step < 2 * (options.rank_count - 1) &&
                            !control.failed.load(std::memory_order_acquire);
         ++step) {
      bool gathering = step >= options.rank_count - 1;
      uint32_t send_shard =
          gathering ? (index + options.rank_count + 1 -
                       (step - (options.rank_count - 1))) %
                          options.rank_count
                    : (index + options.rank_count - step) % options.rank_count;
      uint32_t receive_shard =
          (send_shard + options.rank_count - 1) % options.rank_count;
      size_t sent = 0;
      size_t received = 0;
      while (!control.failed.load(std::memory_order_acquire)) {
        Pump();
        while (sent < chunks && outgoing->CanSend() &&
               !control.failed.load(std::memory_order_acquire)) {
          size_t offset = sent * options.block_size;
          size_t length = std::min(options.block_size, shard_size - offset);
          outgoing->Send(Span(send_shard * shard_size + offset, length));
          ++sent;
        }
        while (received < chunks && incoming->NextInput() &&
               !control.failed.load(std::memory_order_acquire)) {
          const auto& input = *incoming->NextInput();
          size_t offset = received * options.block_size;
          size_t length = std::min(options.block_size, shard_size - offset);
          if (!incoming->Registered() && input.bytes.data_length != length) {
            control.Fail(iree_make_status(IREE_STATUS_DATA_LOSS,
                                          "collective block length differs"));
            break;
          }
          auto* destination =
              tensor + (receive_shard * shard_size + offset) / sizeof(uint32_t);
          for (size_t i = 0; i < length / sizeof(uint32_t); ++i) {
            uint32_t value = iree_unaligned_load_le_u32(input.bytes.data +
                                                        i * sizeof(uint32_t));
            destination[i] = gathering ? value : destination[i] + value;
          }
          incoming->Consume();
          ++received;
        }
        Pump();
        if (sent == chunks && received == chunks &&
            outgoing->submitted == outgoing->completions) {
          break;
        }
        if (!control.failed.load(std::memory_order_acquire)) {
          Poll();
        }
      }
    }
    if (!control.failed.load(std::memory_order_acquire)) {
      for (size_t i = 0; i < elements; ++i) {
        uint32_t expected =
            options.rank_count * Value(0, round, i) +
            19u * options.rank_count * (options.rank_count - 1) / 2;
        if (tensor[i] != expected) {
          control.Fail(iree_make_status(
              IREE_STATUS_DATA_LOSS,
              "all-reduce result differs at rank %u element %zu", index, i));
          break;
        }
      }
    }
  }
  void Shutdown() {
    if (listener) {
      control.Fail(iree_net_listener_stop(
          listener, {+[](void* value) {
                       static_cast<Rank*>(value)->listener_stopped = true;
                     },
                     this}));
      while (!listener_stopped) {
        Poll();
      }
      iree_net_listener_free(listener);
      listener = nullptr;
    }
    iree_net_transport_connect_operation_cancel(&outgoing->connect_operation);
    while (outgoing->Has(Link::kConnecting)) {
      Poll();
    }
    for (auto* link : {incoming.get(), outgoing.get()}) {
      if (link->connection) {
        iree_net_connection_deactivate(link->connection,
                                       {+[](void* value) {
                                          static_cast<Link*>(value)->flags |=
                                              Link::kDrained;
                                        },
                                        link});
      }
    }
    while (rejections ||
           (incoming->connection && !incoming->Has(Link::kDrained)) ||
           (outgoing->connection && !outgoing->Has(Link::kDrained))) {
      Poll();
    }
    // Connection-owned receive registrations must be released before their
    // poll owner exits, even after all asynchronous callbacks have joined.
    outgoing.reset();
    incoming.reset();
    iree_async_proactor_end_polling(proactor);
  }
  void Run(const std::string& next_address) {
    if (!control.failed.load(std::memory_order_acquire)) {
      outgoing->flags |= Link::kConnecting;
      iree_status_t status = iree_net_transport_factory_connect(
          factory,
          iree_make_string_view(next_address.data(), next_address.size()),
          proactor, nullptr, {Link::Connected, outgoing.get()},
          &outgoing->connect_operation);
      if (!iree_status_is_ok(status)) {
        outgoing->flags &= ~Link::kConnecting;
      }
      control.Fail(status);
    }
    while (!control.failed.load(std::memory_order_acquire)) {
      Pump();
      if (incoming->Ready() && outgoing->Ready()) {
        break;
      }
      Poll();
    }
    control.Arrive();
    uint32_t round = 0;
    for (uint32_t phase = 1;
         phase <= 2 && !control.failed.load(std::memory_order_acquire);
         ++phase) {
      while (control.phase.load(std::memory_order_acquire) < phase &&
             !control.failed.load(std::memory_order_acquire)) {
        Poll();
      }
      uint32_t count =
          phase == 1 ? options.warmup_rounds : options.measured_rounds;
      outgoing->high_water = 0;
      for (uint32_t i = 0;
           i < count && !control.failed.load(std::memory_order_acquire); ++i) {
        Reduce(++round);
      }
      // Phase timing includes every source return and consumed observation,
      // without imposing a control-message barrier between collective steps.
      while (!control.failed.load(std::memory_order_acquire)) {
        Pump();
        if (incoming->Idle() && outgoing->Idle()) {
          break;
        }
        Poll();
      }
      results[phase - 1].sends = outgoing->submitted;
      results[phase - 1].source_completions = outgoing->completions;
      results[phase - 1].payload_bytes = outgoing->payload_bytes;
      results[phase - 1].window_high_water = outgoing->high_water;
      control.Arrive();
    }
    while (!control.closing.load(std::memory_order_acquire)) {
      Poll();
    }
    Shutdown();
  }
};

}  // namespace

iree_status_t RunCollectiveTrial(
    const TransportBackend& transport,
    const iree::async::cts::ProactorFactory& create_proactor,
    const CollectiveTrialOptions& options, CollectiveTrialResult* out_result,
    TransferTrialMeasurement measurement) {
  *out_result = {};
  if (options.delivery == CollectiveDelivery::kRegistered &&
      !transport.create_registered_factory) {
    return iree_make_status(IREE_STATUS_UNAVAILABLE,
                            "transport has no registered collective setup");
  }
  if (options.rank_count < 2 || !options.tensor_size ||
      options.tensor_size % options.rank_count ||
      (options.tensor_size / options.rank_count) % sizeof(uint32_t) ||
      !options.block_size || options.block_size % sizeof(uint32_t) ||
      !options.window_size || !options.warmup_rounds ||
      !options.measured_rounds ||
      options.block_size >
          (SIZE_MAX - options.tensor_size) / options.window_size) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "invalid collective trial dimensions");
  }
  uint64_t chunks =
      1 + (options.tensor_size / options.rank_count - 1) / options.block_size;
  uint64_t rounds =
      static_cast<uint64_t>(options.warmup_rounds) + options.measured_rounds;
  if (rounds > UINT32_MAX ||
      chunks > UINT32_MAX / rounds / 2 / (options.rank_count - 1) ||
      options.tensor_size >
          UINT64_MAX / options.measured_rounds / 2 / (options.rank_count - 1)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "collective trial work extent overflow");
  }
  Control control;
  std::vector<std::unique_ptr<Rank>> ranks;
  for (uint32_t i = 0; i < options.rank_count; ++i) {
    auto rank = std::make_unique<Rank>(options, control, i);
    IREE_RETURN_IF_ERROR(rank->Initialize(transport, create_proactor));
    control.proactors.push_back(rank->proactor);
    ranks.push_back(std::move(rank));
  }
  for (auto& rank : ranks) {
    if (!control.failed.load(std::memory_order_acquire)) {
      control.Fail(rank->Listen(transport));
    }
  }
  out_result->available = !control.failed.load(std::memory_order_acquire);
  std::vector<std::thread> threads;
  for (size_t i = 0; i < ranks.size(); ++i) {
    threads.emplace_back(
        [&, i] { ranks[i]->Run(ranks[(i + 1) % ranks.size()]->address); });
  }
  control.Wait();
  if (!control.failed.load(std::memory_order_acquire)) {
    control.Start(1);
    control.Wait();
  }
  double elapsed_seconds = 0;
  if (!control.failed.load(std::memory_order_acquire)) {
    if (measurement.begin) {
      measurement.begin(measurement.user_data);
    }
    const auto start = std::chrono::steady_clock::now();
    control.Start(2);
    control.Wait();
    elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    if (measurement.end) {
      measurement.end(measurement.user_data);
    }
  }
  control.closing.store(true, std::memory_order_release);
  control.Wake();
  for (auto& thread : threads) {
    thread.join();
  }
  if (!control.failed.load(std::memory_order_acquire)) {
    out_result->elapsed_seconds = elapsed_seconds;
    out_result->collectives = options.measured_rounds;
    out_result->payload_storage_bytes =
        options.rank_count *
        (options.tensor_size + options.block_size * options.window_size);
    for (const auto& rank : ranks) {
      out_result->sends += rank->results[1].sends - rank->results[0].sends;
      out_result->source_completions += rank->results[1].source_completions -
                                        rank->results[0].source_completions;
      out_result->payload_bytes +=
          rank->results[1].payload_bytes - rank->results[0].payload_bytes;
      out_result->window_high_water = std::max(
          out_result->window_high_water, rank->results[1].window_high_water);
    }
  }
  return control.error.release();
}

}  // namespace iree::net::cts
