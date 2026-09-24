// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amdgpu/runtime_requirements.h"

#include "iree/base/internal/arena.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"

namespace {

class AmdgpuRuntimeRequirementsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(),
                                     &block_pool_);
    loom_context_initialize(iree_allocator_system(), &context_);
    IREE_ASSERT_OK(loom_context_finalize(&context_));
    IREE_ASSERT_OK(loom_module_allocate(&context_, IREE_SV("test"),
                                        &block_pool_, /*source_resolver=*/NULL,
                                        iree_allocator_system(), &module_));
  }

  void TearDown() override {
    loom_module_free(module_);
    loom_context_deinitialize(&context_);
    iree_arena_block_pool_deinitialize(&block_pool_);
  }

  void AddSymbol(iree_string_view_t name) {
    loom_string_id_t name_id = LOOM_STRING_ID_INVALID;
    IREE_ASSERT_OK(loom_module_intern_string(module_, name, &name_id));
    uint16_t symbol_id = LOOM_SYMBOL_ID_INVALID;
    IREE_ASSERT_OK(loom_module_add_symbol(module_, name_id, &symbol_id));
  }

  iree_arena_block_pool_t block_pool_;
  loom_context_t context_ = {};
  loom_module_t* module_ = nullptr;
};

TEST_F(AmdgpuRuntimeRequirementsTest, UnreferencedModuleRequiresNothing) {
  AddSymbol(IREE_SV("ordinary_symbol"));
  EXPECT_EQ(loom_amdgpu_runtime_requirements_from_target_low_module(module_),
            LOOM_AMDGPU_RUNTIME_REQUIREMENT_NONE);
}

TEST_F(AmdgpuRuntimeRequirementsTest, RecognizesFeedbackConfigSymbol) {
  AddSymbol(IREE_SV("iree_feedback_config"));
  EXPECT_EQ(loom_amdgpu_runtime_requirements_from_target_low_module(module_),
            LOOM_AMDGPU_RUNTIME_REQUIREMENT_FEEDBACK);
}

TEST_F(AmdgpuRuntimeRequirementsTest, RecognizesAsanConfigSymbol) {
  AddSymbol(IREE_SV("iree_asan_config"));
  EXPECT_EQ(loom_amdgpu_runtime_requirements_from_target_low_module(module_),
            LOOM_AMDGPU_RUNTIME_REQUIREMENT_ASAN_SHADOW);
}

TEST_F(AmdgpuRuntimeRequirementsTest, RecognizesTsanConfigSymbol) {
  AddSymbol(IREE_SV("iree_tsan_config"));
  EXPECT_EQ(loom_amdgpu_runtime_requirements_from_target_low_module(module_),
            LOOM_AMDGPU_RUNTIME_REQUIREMENT_TSAN_SHADOW);
}

}  // namespace
