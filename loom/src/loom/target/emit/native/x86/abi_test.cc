// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/emit/native/x86/abi.h"

#include "iree/testing/gtest.h"
#include "loom/target/arch/x86/descriptors/avx10_2_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx2_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx512_bf16_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx512_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx512_packed_dot_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx512_vnni_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx_vnni_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx_vnni_int16_descriptors.h"
#include "loom/target/arch/x86/descriptors/avx_vnni_int8_descriptors.h"
#include "loom/target/arch/x86/descriptors/packed_dot_descriptors.h"
#include "loom/target/arch/x86/descriptors/scalar_descriptors.h"
#include "loom/target/arch/x86/descriptors/simd128_descriptors.h"
#include "loom/target/arch/x86/register_classes.h"

namespace loom {
namespace {

using DescriptorSetProvider = const loom_low_descriptor_set_t* (*)(void);

struct ProfileExpectation {
  const char* name;
  DescriptorSetProvider provider;
  uint16_t vector_register_class;
  uint32_t vector_register_count;
  uint32_t mask_register_count;
};

TEST(X86FunctionAbiTest, CommonClobbersCoverEveryDescriptorProfile) {
  const ProfileExpectation profiles[] = {
      {/*.name=*/"scalar",
       /*.provider=*/loom_x86_scalar_core_descriptor_set,
       /*.vector_register_class=*/LOOM_LOW_REG_CLASS_NONE,
       /*.vector_register_count=*/0,
       /*.mask_register_count=*/0},
      {/*.name=*/"simd128",
       /*.provider=*/loom_x86_simd128_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_XMM,
       /*.vector_register_count=*/16,
       /*.mask_register_count=*/0},
      {/*.name=*/"avx2",
       /*.provider=*/loom_x86_avx2_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_YMM,
       /*.vector_register_count=*/16,
       /*.mask_register_count=*/0},
      {/*.name=*/"avx512",
       /*.provider=*/loom_x86_avx512_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_ZMM,
       /*.vector_register_count=*/32,
       /*.mask_register_count=*/8},
      {/*.name=*/"packed_dot",
       /*.provider=*/loom_x86_packed_dot_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_ZMM,
       /*.vector_register_count=*/32,
       /*.mask_register_count=*/0},
      {/*.name=*/"avx512_packed_dot",
       /*.provider=*/loom_x86_avx512_packed_dot_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_ZMM,
       /*.vector_register_count=*/32,
       /*.mask_register_count=*/8},
      {/*.name=*/"avx512_vnni",
       /*.provider=*/loom_x86_avx512_vnni_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_ZMM,
       /*.vector_register_count=*/32,
       /*.mask_register_count=*/0},
      {/*.name=*/"avx512_bf16",
       /*.provider=*/loom_x86_avx512_bf16_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_ZMM,
       /*.vector_register_count=*/32,
       /*.mask_register_count=*/0},
      {/*.name=*/"avx_vnni",
       /*.provider=*/loom_x86_avx_vnni_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_YMM,
       /*.vector_register_count=*/16,
       /*.mask_register_count=*/0},
      {/*.name=*/"avx_vnni_int8",
       /*.provider=*/loom_x86_avx_vnni_int8_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_YMM,
       /*.vector_register_count=*/16,
       /*.mask_register_count=*/0},
      {/*.name=*/"avx_vnni_int16",
       /*.provider=*/loom_x86_avx_vnni_int16_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_YMM,
       /*.vector_register_count=*/16,
       /*.mask_register_count=*/0},
      {/*.name=*/"avx10_2",
       /*.provider=*/loom_x86_avx10_2_core_descriptor_set,
       /*.vector_register_class=*/LOOM_X86_REGISTER_CLASS_ZMM,
       /*.vector_register_count=*/32,
       /*.mask_register_count=*/0},
  };

  for (const ProfileExpectation& profile : profiles) {
    SCOPED_TRACE(profile.name);
    const loom_low_descriptor_set_t* descriptor_set = profile.provider();
    const loom_low_call_clobber_list_t clobbers =
        loom_x86_function_common_call_clobbers(nullptr, descriptor_set);
    const iree_host_size_t expected_row_count =
        (profile.vector_register_count != 0 ? 1 : 0) +
        (profile.mask_register_count != 0 ? 1 : 0);
    ASSERT_EQ(clobbers.count, expected_row_count);
    if (profile.vector_register_count == 0) {
      EXPECT_EQ(clobbers.values, nullptr);
      continue;
    }

    const loom_low_call_clobber_t& vector_clobber = clobbers.values[0];
    EXPECT_EQ(vector_clobber.register_class, profile.vector_register_class);
    EXPECT_EQ(vector_clobber.location, 0u);
    EXPECT_EQ(vector_clobber.count, profile.vector_register_count);
    ASSERT_LT(vector_clobber.register_class, descriptor_set->reg_class_count);
    EXPECT_EQ(descriptor_set->reg_classes[vector_clobber.register_class]
                  .allocatable_count,
              vector_clobber.count);

    if (profile.mask_register_count != 0) {
      const loom_low_call_clobber_t& mask_clobber = clobbers.values[1];
      EXPECT_EQ(mask_clobber.register_class, LOOM_X86_REGISTER_CLASS_K);
      EXPECT_EQ(mask_clobber.location, 0u);
      EXPECT_EQ(mask_clobber.count, profile.mask_register_count);
      ASSERT_LT(mask_clobber.register_class, descriptor_set->reg_class_count);
      EXPECT_EQ(descriptor_set->reg_classes[mask_clobber.register_class]
                    .allocatable_count,
                mask_clobber.count);
    }
  }
}

}  // namespace
}  // namespace loom
