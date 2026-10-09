// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 WITH LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/base/internal/arena.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"
#include "loom/ops/test/ops.h"
#include "loom/rewrite/rewriter.h"

namespace loom {
namespace {

class ErasureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(), &pool_);
    iree_arena_initialize(&pool_, &arena_);
    loom_context_initialize(iree_allocator_system(), &context_);
    iree_host_size_t count = 0;
    const loom_op_vtable_t* const* vtables = loom_test_dialect_vtables(&count);
    IREE_ASSERT_OK(loom_context_register_dialect(&context_, LOOM_DIALECT_TEST,
                                                 vtables, (uint16_t)count));
    IREE_ASSERT_OK(loom_context_finalize(&context_));
    IREE_ASSERT_OK(loom_module_allocate(&context_, IREE_SV("erasure"), &pool_,
                                        nullptr, iree_allocator_system(),
                                        &module_));
    loom_rewriter_initialize(&rewriter_, module_, &arena_);
    loom_builder_set_block(&rewriter_.builder, loom_module_block(module_));
  }

  void TearDown() override {
    loom_rewriter_deinitialize(&rewriter_);
    loom_module_free(module_);
    loom_context_deinitialize(&context_);
    iree_arena_deinitialize(&arena_);
    iree_arena_block_pool_deinitialize(&pool_);
  }

  // Shared backing for source and rewrite allocations.
  iree_arena_block_pool_t pool_;
  // Rewrite worklist and temporary storage.
  iree_arena_allocator_t arena_;
  // Registered vocabulary of fixture operations.
  loom_context_t context_;
  // Source module owned by this fixture.
  loom_module_t* module_ = nullptr;
  // Consumer that maintains uses, effects, and the rewrite worklist.
  loom_rewriter_t rewriter_ = {};
};

TEST_F(ErasureTest, ClosedSetRetiresReferencesInProducerOrder) {
  const loom_type_t type = loom_type_scalar(LOOM_SCALAR_TYPE_I32);
  loom_op_t* shared = nullptr;
  IREE_ASSERT_OK(loom_test_constant_build(&rewriter_.builder, loom_attr_i64(7),
                                          type, LOOM_LOCATION_UNKNOWN,
                                          &shared));
  loom_op_t* source = nullptr;
  IREE_ASSERT_OK(
      loom_test_effectful_constant_build(&rewriter_.builder, loom_attr_i64(3),
                                         type, LOOM_LOCATION_UNKNOWN, &source));
  const loom_value_id_t shared_value = loom_test_constant_result(shared);
  const loom_value_id_t source_value =
      loom_test_effectful_constant_result(source);
  loom_op_t* consumer = nullptr;
  IREE_ASSERT_OK(loom_test_addi_build(&rewriter_.builder, source_value,
                                      shared_value, type, LOOM_LOCATION_UNKNOWN,
                                      &consumer));
  loom_op_t* survivor = nullptr;
  IREE_ASSERT_OK(loom_test_addi_build(&rewriter_.builder, shared_value,
                                      shared_value, type, LOOM_LOCATION_UNKNOWN,
                                      &survivor));
  EXPECT_TRUE(loom_region_has_observable_effects(module_->body));
  IREE_ASSERT_OK(loom_rewriter_enable_worklist(&rewriter_));
  loom_op_t* consumed[] = {source, consumer};
  IREE_ASSERT_OK(loom_rewriter_erase_closed_set(&rewriter_, consumed,
                                                IREE_ARRAYSIZE(consumed)));
  EXPECT_TRUE(iree_any_bit_set(source->flags, LOOM_OP_FLAG_DEAD));
  EXPECT_TRUE(iree_any_bit_set(consumer->flags, LOOM_OP_FLAG_DEAD));
  EXPECT_FALSE(iree_any_bit_set(survivor->flags, LOOM_OP_FLAG_DEAD));
  EXPECT_EQ(loom_module_value(module_, source_value)->use_count, 0u);
  EXPECT_EQ(loom_module_value(module_, shared_value)->use_count, 2u);
  EXPECT_FALSE(loom_region_has_observable_effects(module_->body));
  EXPECT_EQ(loom_rewriter_pop(&rewriter_), shared);
  EXPECT_EQ(loom_rewriter_pop(&rewriter_), nullptr);
  EXPECT_EQ(loom_test_addi_lhs(survivor), shared_value);
  EXPECT_EQ(loom_test_addi_rhs(survivor), shared_value);
}

}  // namespace
}  // namespace loom
