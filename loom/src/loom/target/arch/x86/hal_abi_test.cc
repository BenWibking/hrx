// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/x86/hal_abi.h"

#include <vector>

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"

namespace loom {
namespace {

class HalAbiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(), &pool_);
    iree_arena_initialize(&pool_, &scratch_);
    loom_context_initialize(iree_allocator_system(), &context_);
    IREE_ASSERT_OK(loom_context_finalize(&context_));
    IREE_ASSERT_OK(loom_module_allocate(&context_, IREE_SV("parameters"),
                                        &pool_, nullptr,
                                        iree_allocator_system(), &module_));
  }
  void TearDown() override {
    loom_module_free(module_);
    loom_context_deinitialize(&context_);
    iree_arena_deinitialize(&scratch_);
    iree_arena_block_pool_deinitialize(&pool_);
  }

  // Shared backing blocks for module storage and temporary layouts.
  iree_arena_block_pool_t pool_;
  // Temporary layout storage that must not escape into the module.
  iree_arena_allocator_t scratch_;
  // Context owning the built-in type vocabulary.
  loom_context_t context_;
  // Module owning the retained logical interface.
  loom_module_t* module_ = nullptr;
};

TEST_F(HalAbiTest, PackingUsesLogicalElementsAndSeparateBindingOrdinals) {
  struct Payload {
    // Logical element passed through the byte-addressed constant segment.
    loom_scalar_type_t type;
    // Storage width of each logical element, including byte-sized booleans.
    uint8_t bytes;
  };
  const Payload payloads[] = {
      {LOOM_SCALAR_TYPE_I1, 1},     {LOOM_SCALAR_TYPE_I8, 1},
      {LOOM_SCALAR_TYPE_I16, 2},    {LOOM_SCALAR_TYPE_I32, 4},
      {LOOM_SCALAR_TYPE_I64, 8},    {LOOM_SCALAR_TYPE_F8E4M3, 1},
      {LOOM_SCALAR_TYPE_F8E5M2, 1}, {LOOM_SCALAR_TYPE_F16, 2},
      {LOOM_SCALAR_TYPE_BF16, 2},   {LOOM_SCALAR_TYPE_F32, 4},
      {LOOM_SCALAR_TYPE_F64, 8},    {LOOM_SCALAR_TYPE_INDEX, 8},
      {LOOM_SCALAR_TYPE_OFFSET, 8},
  };
  for (const auto& payload : payloads) {
    for (uint32_t lanes : {1u, 4u}) {
      SCOPED_TRACE(static_cast<int>(payload.type));
      SCOPED_TRACE(lanes);
      const loom_type_t value =
          lanes == 1 ? loom_type_scalar(payload.type)
                     : loom_type_shaped_1d(LOOM_TYPE_VECTOR, payload.type,
                                           loom_dim_pack_static(lanes), 0);
      const loom_type_t types[] = {loom_type_buffer(),
                                   loom_type_scalar(LOOM_SCALAR_TYPE_I8), value,
                                   loom_type_buffer()};
      loom_named_attr_slice_t layout;
      IREE_ASSERT_OK(loom_x86_hal_abi_layout_build(
          module_, types, IREE_ARRAYSIZE(types), &scratch_, &layout));
      // The layout survives the scratch arena that computed its byte offsets.
      iree_arena_reset(&scratch_);
      loom_x86_hal_abi_t abi;
      IREE_ASSERT_OK(loom_x86_hal_abi_parse(module_, layout, &scratch_, &abi));
      ASSERT_EQ(abi.attributes.parameter_count, 4u);
      EXPECT_EQ(abi.attributes.binding_count, 2u);
      EXPECT_EQ(abi.attributes.constant_byte_length,
                payload.bytes * (lanes + 1));
      EXPECT_EQ(abi.parameters[0].offset, 0u);
      EXPECT_EQ(abi.parameters[1].offset, 0u);
      EXPECT_EQ(abi.parameters[2].offset, payload.bytes);
      EXPECT_EQ(abi.parameters[2].size, payload.bytes * lanes);
      EXPECT_EQ(abi.parameters[3].offset, 1u);
    }
  }
}

TEST_F(HalAbiTest, ExactDispatchCapacityAndEmptyInterface) {
  for (loom_type_t type :
       {loom_type_buffer(), loom_type_scalar(LOOM_SCALAR_TYPE_I64)}) {
    const size_t capacity = loom_type_is_buffer(type) ? 64 : 32;
    std::vector<loom_type_t> types(capacity, type);
    loom_named_attr_slice_t layout;
    IREE_ASSERT_OK(loom_x86_hal_abi_layout_build(
        module_, types.data(), types.size(), &scratch_, &layout));
    loom_x86_hal_abi_t abi;
    IREE_ASSERT_OK(loom_x86_hal_abi_parse(module_, layout, &scratch_, &abi));
    EXPECT_EQ(abi.attributes.binding_count,
              loom_type_is_buffer(type) ? 64u : 0u);
    EXPECT_EQ(abi.attributes.constant_byte_length,
              loom_type_is_buffer(type) ? 0u : 256u);
    types.push_back(type);
    IREE_EXPECT_STATUS_IS(
        IREE_STATUS_INVALID_ARGUMENT,
        loom_x86_hal_abi_layout_build(module_, types.data(), types.size(),
                                      &scratch_, &layout));
  }
  loom_named_attr_slice_t layout;
  IREE_ASSERT_OK(
      loom_x86_hal_abi_layout_build(module_, nullptr, 0, &scratch_, &layout));
  loom_x86_hal_abi_t abi;
  IREE_ASSERT_OK(loom_x86_hal_abi_parse(module_, layout, &scratch_, &abi));
  EXPECT_EQ(abi.attributes.parameter_count, 0u);
  EXPECT_EQ(abi.attributes.binding_count, 0u);
  EXPECT_EQ(abi.attributes.constant_byte_length, 0u);
}

TEST(HalBuiltinTest, StateFieldsMatchTheVersionedSchema) {
  const size_t offsets[] = {
      offsetof(iree_hal_executable_workgroup_state_v0_t, workgroup_id_x),
      offsetof(iree_hal_executable_workgroup_state_v0_t, workgroup_id_y),
      offsetof(iree_hal_executable_workgroup_state_v0_t, workgroup_id_z),
      offsetof(iree_hal_executable_dispatch_state_v0_t, workgroup_count_x),
      offsetof(iree_hal_executable_dispatch_state_v0_t, workgroup_count_y),
      offsetof(iree_hal_executable_dispatch_state_v0_t, workgroup_count_z),
  };
  ASSERT_EQ(IREE_ARRAYSIZE(offsets), LOOM_X86_HAL_BUILTIN_COUNT_);
  for (size_t i = 0; i < IREE_ARRAYSIZE(offsets); ++i) {
    EXPECT_EQ(loom_x86_hal_builtins[i].offset, offsets[i]);
    EXPECT_EQ(loom_x86_hal_builtins[i].size, i % 3 == 2 ? 2u : 4u);
    EXPECT_EQ(loom_x86_hal_builtins[i].state_argument, i < 3 ? 2u : 1u);
  }
}

}  // namespace
}  // namespace loom
