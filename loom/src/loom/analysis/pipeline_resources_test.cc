// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/analysis/pipeline_resources.h"

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/error/error_catalog.h"

namespace loom {
namespace {

struct CapacityDiagnostic {
  // Number of storage-capacity diagnostics emitted.
  uint32_t count = 0;
  // Canonical backing row whose requirements do not fit.
  uint64_t pool_index = UINT64_MAX;
  // Complete required byte extent, including service and worker storage.
  uint64_t required_bytes = 0;
  // Capacity supplied by the resource owner.
  uint64_t capacity_bytes = 0;
};

static iree_status_t CaptureCapacity(
    void* user_data, const loom_diagnostic_emission_t* emission) {
  auto* state = static_cast<CapacityDiagnostic*>(user_data);
  EXPECT_EQ(emission->error, LOOM_ERR_LOWERING_067);
  EXPECT_EQ(emission->param_count, 3u);
  ++state->count;
  state->pool_index = emission->params[0].u64;
  state->required_bytes = emission->params[1].u64;
  state->capacity_bytes = emission->params[2].u64;
  return iree_ok_status();
}

TEST(PipelineResourcesTest, CapacityIncludesFixedAndCompiledReservations) {
  iree_arena_block_pool_t blocks;
  iree_arena_block_pool_initialize(4096, iree_allocator_system(), &blocks);
  iree_arena_allocator_t arena;
  iree_arena_initialize(&blocks, &arena);
  const loom_source_storage_packing_range_t service[] = {{480, 32}};
  const loom_pipeline_resource_pool_t pools[] = {
      {LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP, 512, service, 1},
      {LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP, 512, service, 1},
  };
  loom_source_storage_packing_t* packings[2];
  for (auto& packing : packings) {
    IREE_ASSERT_OK(
        loom_source_storage_packing_create({}, service, 1, &arena, &packing));
  }
  loom_pipeline_resources_t resources = {};
  resources.pools = pools;
  resources.packings = packings;
  resources.pool_count = IREE_ARRAYSIZE(pools);
  uint64_t offset = UINT64_MAX;
  IREE_ASSERT_OK(
      loom_source_storage_packing_append(packings[1], 1, 448, 16, &offset));
  CapacityDiagnostic diagnostic;
  iree_diagnostic_emitter_t emitter = {CaptureCapacity, &diagnostic};
  bool valid = false;
  IREE_ASSERT_OK(loom_pipeline_resources_check_capacity(&resources, nullptr,
                                                        emitter, &valid));
  EXPECT_TRUE(valid);
  EXPECT_EQ(diagnostic.count, 0u);

  IREE_ASSERT_OK(
      loom_source_storage_packing_reserve(packings[1], 64, 16, &offset));
  EXPECT_EQ(offset, 512u);
  IREE_ASSERT_OK(loom_pipeline_resources_check_capacity(&resources, nullptr,
                                                        emitter, &valid));
  EXPECT_FALSE(valid);
  EXPECT_EQ(diagnostic.count, 1u);
  EXPECT_EQ(diagnostic.pool_index, 1u);
  EXPECT_EQ(diagnostic.required_bytes, 576u);
  EXPECT_EQ(diagnostic.capacity_bytes, 512u);
  iree_arena_deinitialize(&arena);
  iree_arena_block_pool_deinitialize(&blocks);
}

}  // namespace
}  // namespace loom
