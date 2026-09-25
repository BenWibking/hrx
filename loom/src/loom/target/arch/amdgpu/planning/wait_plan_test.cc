// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amdgpu/planning/wait_plan.h"

#include <vector>

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/codegen/low/builder.h"
#include "loom/codegen/low/immediates.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"
#include "loom/target/arch/amdgpu/descriptors/low_registry.h"
#include "loom/target/arch/amdgpu/facts.h"
#include "loom/target/arch/amdgpu/refs/target_refs.h"

namespace loom {
namespace {

// The planner consumes scheduled descriptor facts and dependency edges, not
// source IR or assembly. These single-block fixtures keep the independent
// issue-limit and memory-completion inputs explicit.
class AmdgpuWaitPlanTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(), &pool_);
    iree_arena_initialize(&pool_, &arena_);
    loom_context_initialize(iree_allocator_system(), &context_);
    iree_host_size_t vtable_count = 0;
    const auto* vtables = loom_low_dialect_vtables(&vtable_count);
    IREE_ASSERT_OK(loom_context_register_dialect(&context_, LOOM_DIALECT_LOW,
                                                 vtables, vtable_count));
    IREE_ASSERT_OK(loom_context_finalize(&context_));
    IREE_ASSERT_OK(loom_module_allocate(&context_, IREE_SV("wait_plan"), &pool_,
                                        nullptr, iree_allocator_system(),
                                        &module_));
    loom_amdgpu_low_descriptor_registry_initialize(&registry_);
    descriptors_ = loom_low_descriptor_registry_lookup(
        &registry_.registry, IREE_SV("amdgpu.rdna4.gfx125x.core"));
    ASSERT_NE(descriptors_, nullptr);
    const loom_amdgpu_processor_info_t* processor = nullptr;
    IREE_ASSERT_OK(loom_amdgpu_target_info_lookup_processor(IREE_SV("gfx1250"),
                                                            &processor));
    facts_.base.fact_type = &loom_amdgpu_target_fact_type;
    facts_.properties.processor = &processor->properties;

    loom_builder_initialize(module_, &module_->arena,
                            loom_module_block(module_), &builder_);
    loom_string_id_t name = LOOM_STRING_ID_INVALID;
    loom_string_id_t descriptor_set = LOOM_STRING_ID_INVALID;
    IREE_ASSERT_OK(
        loom_builder_intern_string(&builder_, IREE_SV("transfers"), &name));
    IREE_ASSERT_OK(loom_builder_intern_string(
        &builder_, IREE_SV("amdgpu.rdna4.gfx125x.core"), &descriptor_set));
    uint16_t symbol = LOOM_SYMBOL_ID_INVALID;
    IREE_ASSERT_OK(loom_module_add_symbol(module_, name, &symbol));
    loom_type_t argument_types[4];
    const uint32_t widths[] = {4, 8, 4, 4};
    for (uint32_t i = 0; i < 4; ++i) {
      IREE_ASSERT_OK(loom_low_build_register_type(
          descriptors_, LOOM_AMDGPU_REG_CLASS_ID_SGPR, widths[i],
          &argument_types[i]));
    }
    loom_op_t* function = nullptr;
    IREE_ASSERT_OK(loom_low_func_def_build(
        &builder_, 0, 0, 0, 0, 0, 0, 0, 0, descriptor_set, {}, 0, {}, {},
        LOOM_STRING_ID_INVALID, {}, {0, symbol}, argument_types, 4, nullptr, 0,
        nullptr, 0, nullptr, 0, LOOM_LOCATION_UNKNOWN, &function));
    loom_block_t* block =
        loom_region_entry_block(loom_low_func_def_body(function));
    block_.block = block;
    loom_builder_initialize(module_, &module_->arena, block, &builder_);
    schedule_.module = module_;
    schedule_.function_op = function;
    schedule_.target.descriptor_set = descriptors_;
    schedule_.target.target_facts = &facts_.base;
    schedule_.blocks = &block_;
    schedule_.block_count = 1;
    schedule_.value_count = 4;
    schedule_.value_ids = block->arg_ids;
    loom_low_schedule_dependency_graph_initialize(&schedule_.dependencies);
    const uint32_t locations[] = {0, 8, 4, 16};
    for (uint32_t i = 0; i < 4; ++i) {
      assignments_[i].value_id = block->arg_ids[i];
      assignments_[i].descriptor_reg_class_id = LOOM_AMDGPU_REG_CLASS_ID_SGPR;
      assignments_[i].unit_count = widths[i];
      assignments_[i].location_kind =
          LOOM_LOW_ALLOCATION_LOCATION_PHYSICAL_REGISTER;
      assignments_[i].location_base = locations[i];
      assignments_[i].location_count = widths[i];
      assignment_indices_[i] = i;
    }
    allocation_.module = module_;
    allocation_.function_op = function;
    allocation_.target = schedule_.target;
    allocation_.liveness.value_ids = block->arg_ids;
    allocation_.liveness.value_count = 4;
    allocation_.assignments = assignments_;
    allocation_.assignment_count = 4;
    allocation_.assignment_indices_by_value_ordinal = assignment_indices_;
  }

  void TearDown() override {
    loom_module_free(module_);
    loom_context_deinitialize(&context_);
    iree_arena_deinitialize(&arena_);
    iree_arena_block_pool_deinitialize(&pool_);
  }

  void Tensor(loom_amdgpu_descriptor_ref_t descriptor_ref =
                  LOOM_AMDGPU_DESCRIPTOR_REF_TENSOR_LOAD_TO_LDS_D2) {
    Append(descriptor_ref, loom_named_attr_slice_empty());
  }

  void Wait(uint16_t bound) {
    loom_string_id_t name = LOOM_STRING_ID_INVALID;
    IREE_ASSERT_OK(
        loom_builder_intern_string(&builder_, IREE_SV("tensorcnt"), &name));
    loom_named_attr_t attr = {};
    attr.name_id = name;
    attr.value = loom_attr_i64(bound);
    Append(LOOM_AMDGPU_DESCRIPTOR_REF_S_WAIT_TENSORCNT,
           loom_make_named_attr_slice(&attr, 1));
  }

  void MemoryDependency(uint32_t producer, uint32_t consumer) {
    loom_low_schedule_dependency_t dependency = {};
    dependency.producer_node = producer;
    dependency.consumer_node = consumer;
    dependency.kind = LOOM_LOW_SCHEDULE_DEPENDENCY_EFFECT;
    IREE_ASSERT_OK(loom_low_schedule_dependency_graph_append(
        &schedule_.dependencies, dependency, &arena_));
  }

  void Build() {
    loom_op_t* return_op = nullptr;
    IREE_ASSERT_OK(loom_low_return_build(&builder_, nullptr, 0,
                                         LOOM_LOCATION_UNKNOWN, &return_op));
    loom_low_schedule_node_t node = {};
    node.op = return_op;
    node.kind = LOOM_LOW_SCHEDULE_NODE_TERMINATOR;
    node.traits = return_op->traits;
    node.source_ordinal = nodes_.size();
    node.scheduled_ordinal = nodes_.size();
    nodes_.push_back(node);
    order_.resize(nodes_.size());
    for (uint32_t i = 0; i < order_.size(); ++i) {
      order_[i] = i;
    }
    block_.node_count = nodes_.size();
    block_.scheduled_node_count = nodes_.size();
    schedule_.nodes = nodes_.data();
    schedule_.node_count = nodes_.size();
    schedule_.scheduled_node_indices = order_.data();
    schedule_.scheduled_node_count = order_.size();
    schedule_.effect_uses = effects_.data();
    schedule_.effect_use_count = effects_.size();
    schedule_.hazard_uses = hazards_.data();
    schedule_.hazard_use_count = hazards_.size();
    const loom_amdgpu_address_state_plan_t address_state = {};
    IREE_ASSERT_OK(loom_amdgpu_wait_plan_build(
        &schedule_, &allocation_, &address_state, &arena_, &arena_, &plan_));
  }

  void ExpectAction(iree_host_size_t index, uint32_t node, uint16_t bound,
                    loom_amdgpu_wait_plan_reason_t reason) {
    ASSERT_LT(index, plan_.action_count);
    const auto& action = plan_.actions[index];
    EXPECT_EQ(action.node_index, node);
    EXPECT_EQ(action.counter_id, LOOM_AMDGPU_WAIT_COUNTER_TENSOR);
    EXPECT_EQ(action.target_count, bound);
    EXPECT_EQ(action.reason, reason);
  }

  void Append(loom_amdgpu_descriptor_ref_t descriptor_ref,
              loom_named_attr_slice_t attrs) {
    const auto* descriptor =
        loom_amdgpu_descriptor_ref_descriptor(descriptors_, descriptor_ref);
    const auto* view =
        loom_low_descriptor_set_descriptor_view(descriptors_, descriptor);
    loom_low_schedule_node_t node = {};
    node.source_ordinal = nodes_.size();
    node.scheduled_ordinal = nodes_.size();
    node.kind = LOOM_LOW_SCHEDULE_NODE_DESCRIPTOR;
    node.descriptor = descriptor;
    node.source_descriptor_ordinal =
        loom_low_descriptor_set_descriptor_ordinal(descriptors_, descriptor);
    node.schedule_class_id = view->schedule_class_id;
    node.schedule_class =
        &descriptors_->schedule_classes[view->schedule_class_id];
    node.operand_count =
        descriptor_ref == LOOM_AMDGPU_DESCRIPTOR_REF_S_WAIT_TENSORCNT ? 0
        : descriptor_ref == LOOM_AMDGPU_DESCRIPTOR_REF_TENSOR_LOAD_TO_LDS_D4
            ? 4
            : 2;
    for (uint16_t i = 0; i < node.operand_count; ++i) {
      loom_low_schedule_node_operand_ordinals(&node)[i] = i;
    }
    loom_op_t* op = nullptr;
    IREE_ASSERT_OK(loom_low_build_resolved_descriptor_op(
        &builder_, descriptors_, descriptor, 0, block_.block->arg_ids,
        node.operand_count, attrs, nullptr, 0, nullptr, 0,
        LOOM_LOCATION_UNKNOWN, &op));
    node.op = op;
    node.traits = op->traits;
    node.immediate_presence = loom_low_bind_immediate_presence(
        module_, descriptors_, descriptor, loom_low_op_attrs(op));
    nodes_.push_back(node);
    for (uint16_t i = 0; i < descriptor->effect_count; ++i) {
      const auto& effect = descriptors_->effects[descriptor->effect_start + i];
      loom_low_schedule_effect_use_t use = {};
      use.node_index = node.source_ordinal;
      use.scheduled_ordinal = node.scheduled_ordinal;
      use.effect_ordinal = i;
      use.kind = effect.kind;
      use.memory_space = effect.memory_space;
      use.scope_id = effect.scope_id;
      use.effect_flags = effect.flags;
      use.counter_id = effect.counter_id;
      use.width_bits = effect.width_bits;
      effects_.push_back(use);
    }
    for (uint16_t i = 0; i < node.schedule_class->hazard_count; ++i) {
      const auto& hazard =
          descriptors_->hazards[node.schedule_class->hazard_start + i];
      loom_low_schedule_hazard_use_t use = {};
      use.node_index = node.source_ordinal;
      use.scheduled_ordinal = node.scheduled_ordinal;
      use.hazard_ordinal = i;
      use.kind = hazard.kind;
      use.reference_kind = hazard.reference_kind;
      use.reference_id = hazard.reference_id;
      use.producer_stage = hazard.producer_stage;
      use.consumer_stage = hazard.consumer_stage;
      use.distance = hazard.distance;
      use.hazard_flags = hazard.flags;
      hazards_.push_back(use);
    }
  }

  // Arena storage shared by the module and planner fixtures.
  iree_arena_block_pool_t pool_;
  // Planner result and scratch lifetime.
  iree_arena_allocator_t arena_;
  // IR ownership for descriptor immediates and operand identities.
  loom_context_t context_;
  // Small Low function owning the scheduled operations.
  loom_module_t* module_ = nullptr;
  // Appends descriptor operations to the fixture block.
  loom_builder_t builder_;
  // Canonical native descriptor registry.
  loom_target_low_descriptor_registry_t registry_ = {};
  // Descriptor contract used by every scheduled node.
  const loom_low_descriptor_set_t* descriptors_ = nullptr;
  // Canonical processor properties selecting the issue constraint.
  loom_amdgpu_target_facts_t facts_ = {};
  // Single straight-line block under test.
  loom_low_schedule_block_t block_ = {};
  // Descriptor nodes in scheduled order.
  std::vector<loom_low_schedule_node_t> nodes_;
  // Node indices retained for the result's borrowed schedule.
  std::vector<uint32_t> order_;
  // Descriptor memory and counter effects.
  std::vector<loom_low_schedule_effect_use_t> effects_;
  // Descriptor counter hazards.
  std::vector<loom_low_schedule_hazard_use_t> hazards_;
  // Immutable input assembled before the planner call.
  loom_low_schedule_table_t schedule_ = {};
  // Nonoverlapping physical SGPR tuples backing the function arguments.
  loom_low_allocation_assignment_t assignments_[4] = {};
  // Direct value-ordinal to assignment mapping.
  uint32_t assignment_indices_[4] = {};
  // Successful allocation consumed by the production planner.
  loom_low_allocation_table_t allocation_ = {};
  // Actual production planner result inspected by each test.
  loom_amdgpu_wait_plan_t plan_ = {};
};

TEST_F(AmdgpuWaitPlanTest, IndependentTransfersKeepTheHardwareIssueBound) {
  Tensor();
  Tensor(LOOM_AMDGPU_DESCRIPTOR_REF_TENSOR_LOAD_TO_LDS_D4);
  Build();
  ASSERT_EQ(plan_.action_count, 1u);
  ExpectAction(0, 1, 10, LOOM_AMDGPU_WAIT_PLAN_REASON_TENSOR_ISSUE_DRAIN);
  EXPECT_EQ(plan_.actions[0].outstanding_before, 1u);
}

TEST_F(AmdgpuWaitPlanTest, AuthoredPartialWaitSatisfiesTheIssueBound) {
  Tensor();
  Wait(10);
  Tensor();
  Build();
  ASSERT_EQ(plan_.action_count, 1u);
  ExpectAction(0, 1, 10, LOOM_AMDGPU_WAIT_PLAN_REASON_EXPLICIT_PACKET);
}

TEST_F(AmdgpuWaitPlanTest, LargerAuthoredBoundDoesNotSatisfyTheIssueBound) {
  for (uint32_t i = 0; i < 12; ++i) {
    Tensor();
  }
  Wait(11);
  Tensor();
  Build();
  ASSERT_EQ(plan_.action_count, 13u);
  ExpectAction(11, 12, 11, LOOM_AMDGPU_WAIT_PLAN_REASON_EXPLICIT_PACKET);
  ExpectAction(12, 13, 10, LOOM_AMDGPU_WAIT_PLAN_REASON_TENSOR_ISSUE_DRAIN);
}

TEST_F(AmdgpuWaitPlanTest, DependencyCompletionAlsoSatisfiesTheIssueBound) {
  Tensor();
  Tensor();
  MemoryDependency(0, 1);
  Build();
  ASSERT_EQ(plan_.action_count, 1u);
  ExpectAction(0, 1, 0, LOOM_AMDGPU_WAIT_PLAN_REASON_MEMORY_EFFECT);
}

TEST_F(AmdgpuWaitPlanTest, PartialWaitCompletesAnOlderTransfer) {
  Tensor();
  Tensor();
  Wait(1);
  Tensor();
  MemoryDependency(0, 3);
  Build();
  ASSERT_EQ(plan_.action_count, 2u);
  ExpectAction(0, 1, 10, LOOM_AMDGPU_WAIT_PLAN_REASON_TENSOR_ISSUE_DRAIN);
  ExpectAction(1, 2, 1, LOOM_AMDGPU_WAIT_PLAN_REASON_EXPLICIT_PACKET);
}

TEST_F(AmdgpuWaitPlanTest, PartialWaitDoesNotCompleteTheNewestTransfer) {
  Tensor();
  Wait(10);
  Tensor();
  MemoryDependency(0, 2);
  Build();
  ASSERT_EQ(plan_.action_count, 2u);
  ExpectAction(0, 1, 10, LOOM_AMDGPU_WAIT_PLAN_REASON_EXPLICIT_PACKET);
  ExpectAction(1, 2, 0, LOOM_AMDGPU_WAIT_PLAN_REASON_MEMORY_EFFECT);
}

}  // namespace
}  // namespace loom
