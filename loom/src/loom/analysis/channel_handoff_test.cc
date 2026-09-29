// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/analysis/channel_handoff.h"

#include <vector>

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"
#include "loom/ops/cfg/ops.h"
#include "loom/ops/channel/ops.h"
#include "loom/ops/func/ops.h"
#include "loom/ops/type_registry.h"
#include "loom/ops/view/ops.h"

namespace loom {
namespace {

class ChannelHandoffTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(), &pool_);
    iree_arena_initialize(&pool_, &arena_);
    loom_context_initialize(iree_allocator_system(), &context_);
    Register(LOOM_DIALECT_CFG, loom_cfg_dialect_vtables);
    Register(LOOM_DIALECT_CHANNEL, loom_channel_dialect_vtables);
    Register(LOOM_DIALECT_FUNC, loom_func_dialect_vtables);
    Register(LOOM_DIALECT_VIEW, loom_view_dialect_vtables);
    IREE_ASSERT_OK(loom_context_finalize(&context_));
    IREE_ASSERT_OK(loom_module_allocate(&context_, IREE_SV("handoff"), &pool_,
                                        nullptr, iree_allocator_system(),
                                        &module_));
    loom_type_id_t payload;
    IREE_ASSERT_OK(loom_module_intern_type_id(
        module_,
        loom_type_shaped_1d(LOOM_TYPE_TILE, LOOM_SCALAR_TYPE_I32,
                            loom_dim_pack_static(1), 0),
        &payload));
    IREE_ASSERT_OK(loom_channel_type_make(module_, payload, &channel_type_));
    IREE_ASSERT_OK(loom_read_type_make(module_, 0, payload,
                                       (loom_read_type_mode_t)0, &read_type_));
    IREE_ASSERT_OK(loom_write_type_make(module_, payload, &write_type_));
    view_type_ = loom_type_shaped_1d(LOOM_TYPE_VIEW, LOOM_SCALAR_TYPE_I32,
                                     loom_dim_pack_static(1), 0);
    loom_builder_initialize(module_, &module_->arena,
                            loom_module_block(module_), &builder_);
  }

  void TearDown() override {
    loom_local_value_domain_release(&domain_);
    loom_module_free(module_);
    loom_context_deinitialize(&context_);
    iree_arena_deinitialize(&arena_);
    iree_arena_block_pool_deinitialize(&pool_);
  }

  void Register(uint8_t id,
                const loom_op_vtable_t* const* (*get)(iree_host_size_t*)) {
    iree_host_size_t count;
    const auto* vtables = get(&count);
    IREE_ASSERT_OK(
        loom_context_register_dialect(&context_, id, vtables, count));
  }

  loom_value_id_t ReserveDestination() {
    loom_op_t* write;
    IREE_CHECK_OK(loom_channel_reserve_build(&builder_, destination_,
                                             write_type_, view_type_,
                                             LOOM_LOCATION_UNKNOWN, &write));
    return loom_channel_reserve_write(write);
  }

  loom_value_id_t SendAndRetain(
      loom_value_id_t destination_write = LOOM_VALUE_ID_INVALID) {
    loom_op_t* write;
    IREE_CHECK_OK(loom_channel_reserve_build(&builder_, source_, write_type_,
                                             view_type_, LOOM_LOCATION_UNKNOWN,
                                             &write));
    reservations_.push_back(write);
    loom_op_t* op;
    const int64_t index = 0;
    IREE_CHECK_OK(loom_view_store_build(
        &builder_, 0, 0, payload_, loom_channel_reserve_view(write), nullptr, 0,
        &index, 1, 0, 0, LOOM_LOCATION_UNKNOWN, &op));
    IREE_CHECK_OK(loom_channel_publish_build(&builder_,
                                             loom_channel_reserve_write(write),
                                             LOOM_LOCATION_UNKNOWN, &op));
    IREE_CHECK_OK(loom_channel_accept_build(
        &builder_, 0, 0, source_, read_type_, LOOM_LOCATION_UNKNOWN, &op));
    const loom_type_t readers[] = {read_type_, read_type_};
    loom_op_t* fanout;
    IREE_CHECK_OK(loom_channel_fanout_build(
        &builder_, loom_channel_accept_read(op), readers, 2, nullptr, 0,
        LOOM_LOCATION_UNKNOWN, &fanout));
    if (destination_write == LOOM_VALUE_ID_INVALID) {
      destination_write = ReserveDestination();
    }
    IREE_CHECK_OK(loom_channel_copy_build(&builder_, loom_op_results(fanout)[0],
                                          destination_write,
                                          LOOM_LOCATION_UNKNOWN, &op));
    return loom_op_results(fanout)[1];
  }

  void Release(loom_value_id_t read) {
    loom_op_t* op;
    IREE_CHECK_OK(loom_channel_release_build(&builder_, read,
                                             LOOM_LOCATION_UNKNOWN, &op));
  }

  // Emit an ordinary loop retaining N previous outputs. Each iteration submits
  // its new copy before releasing the oldest history and rotating the others.
  enum class DestinationOrder { Sequential, ReversedPrefix };

  void BuildHistoryLoop(size_t history_count,
                        DestinationOrder order = DestinationOrder::Sequential) {
    loom_string_id_t name;
    IREE_ASSERT_OK(
        loom_module_intern_string(module_, IREE_SV("history"), &name));
    loom_symbol_id_t symbol;
    IREE_ASSERT_OK(loom_module_add_symbol(module_, name, &symbol));
    const loom_type_t arguments[] = {channel_type_, channel_type_,
                                     loom_type_scalar(LOOM_SCALAR_TYPE_I1),
                                     loom_type_scalar(LOOM_SCALAR_TYPE_I32)};
    loom_op_t* function;
    IREE_ASSERT_OK(loom_func_def_build(
        &builder_, 0, 0, 0, 0, 0, 0, 0, loom_symbol_ref_null(), 0,
        loom_named_attr_slice_empty(), LOOM_STRING_ID_INVALID,
        loom_named_attr_slice_empty(), {0, symbol}, arguments, 4, nullptr, 0,
        nullptr, 0, nullptr, 0, LOOM_LOCATION_UNKNOWN, &function));
    region_ = loom_func_def_body(function);
    loom_builder_enter_region(&builder_, function, region_);
    auto* entry = loom_region_entry_block(region_);
    source_ = entry->arg_ids[0];
    destination_ = entry->arg_ids[1];
    payload_ = entry->arg_ids[3];
    loom_block_t* header;
    loom_block_t* body;
    loom_block_t* exit;
    IREE_ASSERT_OK(loom_region_append_block(module_, region_, &header));
    IREE_ASSERT_OK(loom_region_append_block(module_, region_, &body));
    IREE_ASSERT_OK(loom_region_append_block(module_, region_, &exit));
    for (size_t i = 0; i < history_count; ++i) {
      loom_value_id_t read;
      IREE_ASSERT_OK(loom_module_define_value(module_, read_type_, &read));
      IREE_ASSERT_OK(loom_block_add_arg(module_, header, read));
      history_.push_back(read);
    }
    std::vector<loom_value_id_t> initial;
    if (order == DestinationOrder::ReversedPrefix) {
      std::vector<loom_value_id_t> writes;
      for (size_t i = 0; i < history_count; ++i) {
        writes.push_back(ReserveDestination());
      }
      for (auto write = writes.rbegin(); write != writes.rend(); ++write) {
        initial.push_back(SendAndRetain(*write));
      }
    } else {
      for (size_t i = 0; i < history_count; ++i) {
        initial.push_back(SendAndRetain());
      }
    }
    loom_op_t* op;
    IREE_ASSERT_OK(loom_cfg_br_build(&builder_, header, initial.data(),
                                     initial.size(), LOOM_LOCATION_UNKNOWN,
                                     &op));
    loom_builder_set_block(&builder_, header);
    IREE_ASSERT_OK(loom_cfg_cond_br_build(&builder_, entry->arg_ids[2], body,
                                          exit, LOOM_LOCATION_UNKNOWN, &op));
    loom_builder_set_block(&builder_, body);
    IREE_ASSERT_OK(loom_channel_wait_build(
        &builder_, history_.front(), view_type_, LOOM_LOCATION_UNKNOWN, &op));
    auto next = SendAndRetain();
    Release(history_.front());
    std::vector<loom_value_id_t> rotated(history_.begin() + 1, history_.end());
    rotated.push_back(next);
    IREE_ASSERT_OK(loom_cfg_br_build(&builder_, header, rotated.data(),
                                     rotated.size(), LOOM_LOCATION_UNKNOWN,
                                     &op));
    loom_builder_set_block(&builder_, exit);
    for (auto read : history_) {
      Release(read);
    }
    IREE_ASSERT_OK(loom_func_return_build(&builder_, nullptr, 0,
                                          LOOM_LOCATION_UNKNOWN, &op));
    IREE_ASSERT_OK(loom_local_value_domain_acquire_for_region(
        module_, region_, &arena_, &domain_));
    const loom_channel_plan_binding_t bindings[] = {
        {source_, source_}, {destination_, destination_}};
    loom_channel_plan_rejection_t rejection;
    IREE_ASSERT_OK(loom_channel_plan_build(&domain_, bindings, 2, nullptr,
                                           &arena_, &plan_, &rejection));
    ASSERT_EQ(rejection.kind, LOOM_CHANNEL_PLAN_REJECTION_NONE);
    IREE_ASSERT_OK(loom_cfg_graph_build(module_, region_, &arena_, &graph_));
  }

  loom_channel_handoff_rejection_t Analyze(uint64_t capacity) {
    const loom_channel_handoff_t handoff = {source_, destination_, capacity};
    loom_channel_handoff_rejection_t rejection;
    IREE_CHECK_OK(loom_channel_handoff_analyze(&plan_, &graph_, &handoff,
                                               &arena_, &rejection));
    return rejection;
  }

  // Shared module/analysis backing.
  iree_arena_block_pool_t pool_;
  // Lifetime of plans and exact protocol snapshots.
  iree_arena_allocator_t arena_;
  // Minimal operation vocabulary used by the fixture.
  loom_context_t context_;
  // Source IR owned by this fixture.
  loom_module_t* module_ = nullptr;
  // Insertion point within the current source block.
  loom_builder_t builder_ = {};
  // Region containing the complete ownership loop.
  loom_region_t* region_ = nullptr;
  // Acquired source value correspondence.
  loom_local_value_domain_t domain_ = {};
  // Independent channel identities and retained source actions.
  loom_channel_plan_t plan_ = {};
  // Canonical source control topology.
  loom_cfg_graph_t graph_ = {};
  // Single-word channel type.
  loom_type_t channel_type_;
  // Immutable read obligation.
  loom_type_t read_type_;
  // Producer reservation.
  loom_type_t write_type_;
  // Borrowed single-word payload.
  loom_type_t view_type_;
  // Locally produced output channel.
  loom_value_id_t source_;
  // Independently realized transfer destination.
  loom_value_id_t destination_;
  // Initialized scalar stored in each output record.
  loom_value_id_t payload_;
  // Retained history in oldest-to-newest order at the loop header.
  std::vector<loom_value_id_t> history_;
  // Original reservations for locating capacity conflicts precisely.
  std::vector<loom_op_t*> reservations_;
};

TEST_F(ChannelHandoffTest, RetainsHistoryAfterIssuingNextCopy) {
  BuildHistoryLoop(1);
  EXPECT_EQ(Analyze(2).kind, LOOM_CHANNEL_HANDOFF_REJECTION_NONE);
  const auto conflict = Analyze(1);
  EXPECT_EQ(conflict.kind, LOOM_CHANNEL_HANDOFF_REJECTION_HISTORY_ALIAS);
  EXPECT_EQ(conflict.op, reservations_.back());
  EXPECT_EQ(conflict.value_id, history_.front());
}

TEST_F(ChannelHandoffTest,
       RotatesTwoHistoryRecordsThroughParallelEdgeTransfers) {
  BuildHistoryLoop(2);
  EXPECT_EQ(Analyze(3).kind, LOOM_CHANNEL_HANDOFF_REJECTION_NONE);
  const auto conflict = Analyze(2);
  EXPECT_EQ(conflict.kind, LOOM_CHANNEL_HANDOFF_REJECTION_HISTORY_ALIAS);
  EXPECT_EQ(conflict.op, reservations_.back());
  EXPECT_EQ(conflict.value_id, history_.front());
}

TEST_F(ChannelHandoffTest, ReservedDestinationsCannotLoseTheirIndividualOrder) {
  BuildHistoryLoop(2, DestinationOrder::ReversedPrefix);
  EXPECT_EQ(Analyze(3).kind,
            LOOM_CHANNEL_HANDOFF_REJECTION_DESTINATION_OVERLAP);
}

}  // namespace
}  // namespace loom
