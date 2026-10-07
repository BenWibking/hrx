// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/low/schedule/memory_completion.h"

#include <array>

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/util/cfg_graph_test_util.h"

namespace loom {
namespace {

class MemoryCompletionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(), &pool_);
    iree_arena_initialize(&pool_, &arena_);
    iree_arena_initialize(&pool_, &scratch_arena_);
    IREE_ASSERT_OK(
        loom_low_memory_access_map_create(&arena_, &memory_accesses_));
  }

  void TearDown() override {
    iree_arena_deinitialize(&scratch_arena_);
    iree_arena_deinitialize(&arena_);
    iree_arena_block_pool_deinitialize(&pool_);
  }

  static loom_low_memory_access_summary_t Access(uint32_t alias_root_id) {
    loom_low_memory_access_summary_t access = {};
    access.memory_space = LOOM_LOW_MEMORY_SPACE_WORKGROUP;
    access.alias_root_id = alias_root_id;
    access.alias_group_id = LOOM_LOW_MEMORY_ALIAS_ID_NONE;
    access.precision_flags = LOOM_LOW_MEMORY_ACCESS_PRECISION_SPACE |
                             LOOM_LOW_MEMORY_ACCESS_PRECISION_ROOT;
    return access;
  }

  void AddEffect(uint32_t block_index, loom_low_effect_kind_t kind,
                 const loom_low_memory_access_summary_t& access,
                 uint32_t* out_index, loom_low_effect_flags_t extra_flags = 0) {
    const uint32_t index = effect_count_++;
    IREE_ASSERT_LT(index, effects_.size());
    nodes_[index].op = &ops_[index];
    nodes_[index].block_index = block_index;
    effects_[index].node_index = index;
    effects_[index].block_index = block_index;
    effects_[index].effect_ordinal = 0;
    effects_[index].kind = kind;
    effects_[index].memory_space = LOOM_LOW_MEMORY_SPACE_WORKGROUP;
    effects_[index].effect_flags =
        LOOM_LOW_EFFECT_FLAG_DEPENDENCY | extra_flags;
    IREE_ASSERT_OK(loom_low_memory_access_map_insert(
        memory_accesses_, &ops_[index], /*effect_ordinal=*/0, &access));
    *out_index = index;
  }

  void Analyze(const loom_cfg_graph_t* graph) {
    state_.memory_accesses = memory_accesses_;
    state_.cfg_graph = graph;
    state_.nodes = nodes_.data();
    state_.effect_uses = effects_.data();
    state_.effect_use_count = effect_count_;
    state_.effect_use_capacity = effects_.size();
    state_.arena = &arena_;
    state_.scratch_arena = &scratch_arena_;
    IREE_ASSERT_OK(loom_low_schedule_build_acyclic_memory_completions(&state_));
  }

  bool IsRefined(uint32_t effect_use) const {
    return iree_any_bit_set(effects_[effect_use].flags,
                            LOOM_LOW_SCHEDULE_EFFECT_USE_FLAG_REFINED_MEMORY);
  }

  iree_arena_block_pool_t pool_;
  iree_arena_allocator_t arena_;
  iree_arena_allocator_t scratch_arena_;
  loom_low_memory_access_map_t* memory_accesses_ = nullptr;
  std::array<loom_op_t, 8> ops_ = {};
  std::array<loom_low_schedule_node_t, 8> nodes_ = {};
  std::array<loom_low_schedule_effect_use_t, 8> effects_ = {};
  uint32_t effect_count_ = 0;
  loom_low_schedule_build_state_t state_ = {};
};

TEST_F(MemoryCompletionTest, DisjointForwardEffectsNeedNoCompletion) {
  testing::CfgGraph graph({{1}, {}});
  uint32_t producer = 0;
  uint32_t consumer = 0;
  AddEffect(0, LOOM_LOW_EFFECT_KIND_WRITE, Access(1), &producer);
  AddEffect(1, LOOM_LOW_EFFECT_KIND_READ, Access(2), &consumer);

  Analyze(graph.get());

  EXPECT_TRUE(IsRefined(producer));
  EXPECT_TRUE(IsRefined(consumer));
  EXPECT_EQ(state_.memory_completion_edge_count, 0u);
}

TEST_F(MemoryCompletionTest, AliasingForwardEffectsRetainExactCompletion) {
  testing::CfgGraph graph({{1}, {}});
  uint32_t producer = 0;
  uint32_t consumer = 0;
  AddEffect(0, LOOM_LOW_EFFECT_KIND_WRITE, Access(1), &producer);
  AddEffect(1, LOOM_LOW_EFFECT_KIND_READ, Access(1), &consumer);

  Analyze(graph.get());

  ASSERT_EQ(state_.memory_completion_edge_count, 1u);
  EXPECT_EQ(state_.memory_completion_edges[0].producer_effect_use, producer);
  EXPECT_EQ(state_.memory_completion_edges[0].consumer_effect_use, consumer);
}

TEST_F(MemoryCompletionTest, ReadsCompleteBeforeAliasingForwardWrites) {
  testing::CfgGraph graph({{1}, {}});
  uint32_t producer = 0;
  uint32_t consumer = 0;
  AddEffect(0, LOOM_LOW_EFFECT_KIND_READ, Access(1), &producer);
  AddEffect(1, LOOM_LOW_EFFECT_KIND_WRITE, Access(1), &consumer);

  Analyze(graph.get());

  ASSERT_EQ(state_.memory_completion_edge_count, 1u);
  EXPECT_EQ(state_.memory_completion_edges[0].producer_effect_use, producer);
  EXPECT_EQ(state_.memory_completion_edges[0].consumer_effect_use, consumer);
}

TEST_F(MemoryCompletionTest, WritesOnlyRequireIssueOrdering) {
  testing::CfgGraph graph({{1}, {}});
  uint32_t effect = 0;
  AddEffect(0, LOOM_LOW_EFFECT_KIND_WRITE, Access(1), &effect);
  AddEffect(1, LOOM_LOW_EFFECT_KIND_WRITE, Access(1), &effect);

  Analyze(graph.get());

  EXPECT_EQ(state_.memory_completion_edge_count, 0u);
}

TEST_F(MemoryCompletionTest, BackedgesDoNotCarryRefinedEffects) {
  testing::CfgGraph graph({{1}, {0}});
  uint32_t block0_effect = 0;
  uint32_t block1_effect = 0;
  AddEffect(0, LOOM_LOW_EFFECT_KIND_READ, Access(1), &block0_effect);
  AddEffect(1, LOOM_LOW_EFFECT_KIND_WRITE, Access(1), &block1_effect);

  Analyze(graph.get());

  ASSERT_EQ(state_.memory_completion_edge_count, 1u);
  EXPECT_EQ(state_.memory_completion_edges[0].producer_effect_use,
            block0_effect);
  EXPECT_EQ(state_.memory_completion_edges[0].consumer_effect_use,
            block1_effect);
}

TEST_F(MemoryCompletionTest, OrderedEffectsRemainConservative) {
  testing::CfgGraph graph({{1}, {}});
  uint32_t producer = 0;
  uint32_t consumer = 0;
  AddEffect(0, LOOM_LOW_EFFECT_KIND_WRITE, Access(1), &producer,
            LOOM_LOW_EFFECT_FLAG_ORDERED);
  AddEffect(1, LOOM_LOW_EFFECT_KIND_READ, Access(1), &consumer);

  Analyze(graph.get());

  EXPECT_FALSE(IsRefined(producer));
  EXPECT_TRUE(IsRefined(consumer));
  EXPECT_EQ(state_.memory_completion_edge_count, 0u);
}

}  // namespace
}  // namespace loom
