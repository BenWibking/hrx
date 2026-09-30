// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/analysis/pipeline_resources.h"

#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "loom/error/error_catalog.h"
#include "loom/ir/context.h"
#include "loom/ir/module.h"
#include "loom/ops/buffer/ops.h"
#include "loom/ops/channel/ops.h"
#include "loom/ops/func/ops.h"
#include "loom/ops/index/ops.h"
#include "loom/ops/pipeline/ops.h"
#include "loom/ops/type_registry.h"

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

static iree_status_t CapturePlacementRejection(
    void* user_data, const loom_diagnostic_emission_t* emission) {
  auto* count = static_cast<uint32_t*>(user_data);
  EXPECT_EQ(emission->error, LOOM_ERR_LOWERING_066);
  ++*count;
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

class PipelineConstructionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(), &blocks_);
    iree_arena_initialize(&blocks_, &arena_);
    loom_context_initialize(iree_allocator_system(), &context_);
    Register(LOOM_DIALECT_BUFFER, loom_buffer_dialect_vtables);
    Register(LOOM_DIALECT_CHANNEL, loom_channel_dialect_vtables);
    Register(LOOM_DIALECT_FUNC, loom_func_dialect_vtables);
    Register(LOOM_DIALECT_INDEX, loom_index_dialect_vtables);
    Register(LOOM_DIALECT_PIPELINE, loom_pipeline_dialect_vtables);
    IREE_ASSERT_OK(loom_context_finalize(&context_));
    IREE_ASSERT_OK(loom_module_allocate(&context_, IREE_SV("construction"),
                                        &blocks_, nullptr,
                                        iree_allocator_system(), &module_));
    loom_builder_initialize(module_, &module_->arena,
                            loom_module_block(module_), &builder_);
  }

  void TearDown() override {
    loom_module_free(module_);
    loom_context_deinitialize(&context_);
    iree_arena_deinitialize(&arena_);
    iree_arena_block_pool_deinitialize(&blocks_);
  }

  void Register(uint8_t id,
                const loom_op_vtable_t* const* (*get)(iree_host_size_t*)) {
    iree_host_size_t count;
    const auto* vtables = get(&count);
    IREE_ASSERT_OK(
        loom_context_register_dialect(&context_, id, vtables, count));
  }

  loom_symbol_ref_t Symbol(const char* name) {
    loom_string_id_t name_id;
    IREE_CHECK_OK(loom_module_intern_string(
        module_, iree_make_cstring_view(name), &name_id));
    loom_symbol_id_t symbol;
    IREE_CHECK_OK(loom_module_add_symbol(module_, name_id, &symbol));
    return {0, symbol};
  }

  loom_value_id_t Constant(int64_t value, loom_scalar_type_t type) {
    loom_op_t* op;
    IREE_CHECK_OK(loom_index_constant_build(&builder_, loom_attr_i64(value),
                                            loom_type_scalar(type),
                                            LOOM_LOCATION_UNKNOWN, &op));
    return loom_index_constant_result(op);
  }

  // Shared storage for module and analysis arenas.
  iree_arena_block_pool_t blocks_;
  // Retained resource and value-fact storage.
  iree_arena_allocator_t arena_;
  // Registered source construction vocabulary.
  loom_context_t context_;
  // Source module owned by this fixture.
  loom_module_t* module_ = nullptr;
  // Insertion point for the small construction and call fixtures.
  loom_builder_t builder_;
};

TEST_F(PipelineConstructionTest, CapturesKeepProtocolAndStorageSeparate) {
  loom_type_id_t payload;
  IREE_ASSERT_OK(loom_module_intern_type_id(
      module_,
      loom_type_shaped_1d(LOOM_TYPE_TILE, LOOM_SCALAR_TYPE_I32,
                          loom_dim_pack_static(4), 0),
      &payload));
  loom_type_t channel_type;
  IREE_ASSERT_OK(loom_channel_type_make(module_, payload, &channel_type));
  const loom_type_t arguments[] = {channel_type, channel_type};
  const auto worker_symbol = Symbol("worker");
  loom_op_t* worker;
  IREE_ASSERT_OK(loom_func_def_build(
      &builder_, 0, 0, 0, 0, 0, 0, 0, loom_symbol_ref_null(), 0,
      loom_named_attr_slice_empty(), LOOM_STRING_ID_INVALID,
      loom_named_attr_slice_empty(), worker_symbol, arguments, 2, nullptr, 0,
      nullptr, 0, nullptr, 0, LOOM_LOCATION_UNKNOWN, &worker));
  loom_builder_enter_region(&builder_, worker, loom_func_def_body(worker));
  loom_op_t* terminator;
  IREE_ASSERT_OK(loom_func_return_build(&builder_, nullptr, 0,
                                        LOOM_LOCATION_UNKNOWN, &terminator));

  loom_builder_set_block(&builder_, loom_module_block(module_));
  builder_.ip.parent_op = nullptr;
  loom_op_t* pipeline;
  const auto pool_type = loom_type_pool();
  IREE_ASSERT_OK(loom_pipeline_def_build(
      &builder_, 0, 0, 0, 0, loom_symbol_ref_null(), Symbol("pipeline"),
      &pool_type, 1, nullptr, 0, nullptr, 0, LOOM_LOCATION_UNKNOWN, &pipeline));
  auto* body = loom_pipeline_def_body(pipeline);
  const auto pool = loom_region_entry_block(body)->arg_ids[0];
  loom_builder_enter_region(&builder_, pipeline, body);
  const auto length = Constant(128, LOOM_SCALAR_TYPE_OFFSET);
  loom_op_t* allocation;
  IREE_ASSERT_OK(loom_buffer_alloca_build(
      &builder_, LOOM_BUFFER_ALLOCA_BUILD_FLAG_HAS_POOL,
      LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP, pool, 64, length,
      loom_type_buffer(), LOOM_LOCATION_UNKNOWN, &allocation));
  const auto root = loom_buffer_alloca_result(allocation);
  const auto origin = Constant(64, LOOM_SCALAR_TYPE_OFFSET);
  const auto storage_type =
      loom_type_shaped_2d(LOOM_TYPE_VIEW, LOOM_SCALAR_TYPE_I32,
                          loom_dim_pack_static(2), loom_dim_pack_static(4), 0);
  loom_op_t* storage;
  IREE_ASSERT_OK(loom_buffer_view_build(&builder_, root, origin, storage_type,
                                        LOOM_LOCATION_UNKNOWN, &storage));
  const auto capacity = Constant(2, LOOM_SCALAR_TYPE_INDEX);
  loom_value_id_t channels[2];
  for (auto& channel : channels) {
    loom_op_t* binding;
    IREE_ASSERT_OK(
        loom_channel_bind_build(&builder_, LOOM_CHANNEL_BIND_DISCIPLINE_FIFO,
                                loom_buffer_view_result(storage), capacity,
                                channel_type, LOOM_LOCATION_UNKNOWN, &binding));
    channel = loom_channel_bind_result(binding);
  }
  loom_op_t* calls[2];
  for (size_t i = 0; i < 2; ++i) {
    loom_builder_enter_region(&builder_, pipeline, body);
    const int64_t worker_origin = i;
    const int64_t one = 1;
    loom_op_t* strand;
    IREE_ASSERT_OK(loom_pipeline_strand_build(
        &builder_, 0, loom_symbol_ref_null(), nullptr, 0, &worker_origin, 1,
        nullptr, 0, &one, 1, nullptr, 0, &one, 1, LOOM_LOCATION_UNKNOWN,
        &strand));
    loom_builder_enter_region(&builder_, strand,
                              loom_pipeline_strand_body(strand));
    const loom_value_id_t captures[] = {channels[i], channels[1 - i]};
    IREE_ASSERT_OK(loom_func_call_build(&builder_, 0, 0, 0, 0, worker_symbol,
                                        captures, 2, nullptr, 0, nullptr, 0,
                                        LOOM_LOCATION_UNKNOWN, &calls[i]));
    IREE_ASSERT_OK(
        loom_pipeline_end_build(&builder_, LOOM_LOCATION_UNKNOWN, &terminator));
  }
  loom_builder_enter_region(&builder_, pipeline, body);
  IREE_ASSERT_OK(loom_pipeline_finish_build(&builder_, LOOM_LOCATION_UNKNOWN,
                                            &terminator));

  loom_value_fact_table_t facts = {};
  IREE_ASSERT_OK(
      loom_value_fact_table_initialize(&facts, &arena_, module_->values.count));
  loom_type_registry_configure_fact_context(&facts.context);
  const auto function = loom_func_like_cast(module_, pipeline);
  IREE_ASSERT_OK(loom_value_fact_table_compute(&facts, module_, function));
  const loom_pipeline_resource_pool_t pools[] = {
      {LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP, 256, nullptr, 0}};
  const loom_pipeline_resource_pool_binding_t bindings[] = {{pool, 0}};
  loom_pipeline_resources_t resources;
  bool valid = false;
  IREE_ASSERT_OK(loom_pipeline_resources_build(module_, function, &facts, pools,
                                               1, nullptr, 0, bindings, 1, {},
                                               &arena_, &resources, &valid));
  ASSERT_TRUE(valid);
  ASSERT_EQ(resources.allocation_count, 1u);
  ASSERT_EQ(resources.channel_count, 2u);
  ASSERT_EQ(resources.strand_count, 2u);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(resources.strands[i].call, calls[i]);
    const auto captures = loom_func_call_operands(resources.strands[i].call);
    EXPECT_EQ(captures.values[0], channels[i]);
    EXPECT_EQ(captures.values[1], channels[1 - i]);
    const auto* channel =
        loom_pipeline_resources_lookup_channel(&resources, captures.values[0]);
    ASSERT_NE(channel, nullptr);
    EXPECT_EQ(channel->value_id, channels[i]);
    EXPECT_EQ(channel->capacity, 2u);
    const auto* backing = loom_pipeline_resources_lookup_allocation(
        &resources, channel->storage.root_value_id);
    ASSERT_NE(backing, nullptr);
    EXPECT_EQ(backing->root_value_id, root);
    EXPECT_EQ(backing->pool_index, 0u);
    EXPECT_EQ(backing->byte_length, 128u);
    int64_t offset = -1;
    ASSERT_TRUE(loom_value_facts_as_exact_i64(channel->storage.base_byte_offset,
                                              &offset));
    EXPECT_EQ(offset, 64);
  }
}

TEST_F(PipelineConstructionTest, MemorySelectionsShareBackingNotAllocations) {
  loom_op_t* pipeline;
  const auto pool_type = loom_type_pool();
  IREE_ASSERT_OK(loom_pipeline_def_build(
      &builder_, LOOM_PIPELINE_DEF_BUILD_FLAG_HAS_SCOPE,
      LOOM_PIPELINE_DEF_SCOPE_KERNEL, 0, 0, loom_symbol_ref_null(),
      Symbol("pipeline"), &pool_type, 1, nullptr, 0, nullptr, 0,
      LOOM_LOCATION_UNKNOWN, &pipeline));
  auto* body = loom_pipeline_def_body(pipeline);
  const auto incoming_pool = loom_region_entry_block(body)->arg_ids[0];
  loom_builder_enter_region(&builder_, pipeline, body);
  const auto column = Constant(1, LOOM_SCALAR_TYPE_INDEX);
  const auto length = Constant(48, LOOM_SCALAR_TYPE_OFFSET);
  loom_value_id_t selected_pools[4];
  loom_value_id_t roots[4];
  for (size_t i = 0; i < 4; ++i) {
    // Two identical queries, an aliasing selection, and a different backing.
    const int64_t coordinates[] = {INT64_MIN, int64_t(i < 2 ? 2 : i + 1)};
    loom_op_t* memory;
    IREE_ASSERT_OK(loom_pipeline_memory_build(
        &builder_, LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP, &column, 1,
        coordinates, 2, pool_type, LOOM_LOCATION_UNKNOWN, &memory));
    selected_pools[i] = loom_pipeline_memory_result(memory);
    loom_op_t* allocation;
    IREE_ASSERT_OK(loom_buffer_alloca_build(
        &builder_, LOOM_BUFFER_ALLOCA_BUILD_FLAG_HAS_POOL,
        LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP, selected_pools[i], 64, length,
        loom_type_buffer(), LOOM_LOCATION_UNKNOWN, &allocation));
    roots[i] = loom_buffer_alloca_result(allocation);
  }
  loom_op_t* terminator;
  IREE_ASSERT_OK(loom_pipeline_finish_build(&builder_, LOOM_LOCATION_UNKNOWN,
                                            &terminator));

  loom_value_fact_table_t facts = {};
  IREE_ASSERT_OK(
      loom_value_fact_table_initialize(&facts, &arena_, module_->values.count));
  loom_type_registry_configure_fact_context(&facts.context);
  const auto function = loom_func_like_cast(module_, pipeline);
  IREE_ASSERT_OK(loom_value_fact_table_compute(&facts, module_, function));
  const loom_pipeline_resource_pool_t pools[] = {
      {LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP, 256, nullptr, 0},
      {LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP, 256, nullptr, 0},
  };
  const uint64_t coordinates[][2] = {{1, 2}, {1, 3}, {1, 4}};
  const loom_pipeline_resource_memory_t memories[] = {
      {coordinates[0], 2, 1},
      {coordinates[1], 2, 1},
      {coordinates[2], 2, 0},
  };
  const loom_pipeline_resource_pool_binding_t bindings[] = {{incoming_pool, 1}};
  loom_pipeline_resources_t resources;
  bool valid = false;
  IREE_ASSERT_OK(loom_pipeline_resources_build(
      module_, function, &facts, pools, IREE_ARRAYSIZE(pools), memories,
      IREE_ARRAYSIZE(memories), bindings, IREE_ARRAYSIZE(bindings), {}, &arena_,
      &resources, &valid));
  ASSERT_TRUE(valid);
  ASSERT_EQ(resources.allocation_count, 4u);
  ASSERT_EQ(resources.pool_binding_count, 5u);
  const auto* incoming =
      loom_pipeline_resources_lookup_pool(&resources, incoming_pool);
  ASSERT_NE(incoming, nullptr);
  EXPECT_EQ(incoming->pool_index, 1u);
  for (size_t i = 0; i < 4; ++i) {
    const auto* selection =
        loom_pipeline_resources_lookup_pool(&resources, selected_pools[i]);
    ASSERT_NE(selection, nullptr);
    EXPECT_EQ(selection->pool_index, i == 3 ? 0u : 1u);
    const auto* allocation =
        loom_pipeline_resources_lookup_allocation(&resources, roots[i]);
    ASSERT_NE(allocation, nullptr);
    EXPECT_EQ(allocation->pool_index, selection->pool_index);
    EXPECT_EQ(allocation->byte_offset, i == 3 ? 0u : i * 64u);
    EXPECT_EQ(allocation->byte_length, 48u);
  }

  // Another admitted invocation need not expose the fourth selection. A miss
  // must reject construction instead of substituting its first local pool or
  // publishing the allocations accumulated before the unsupported query.
  uint32_t rejection_count = 0;
  IREE_ASSERT_OK(loom_pipeline_resources_build(
      module_, function, &facts, pools, IREE_ARRAYSIZE(pools), memories, 2,
      bindings, IREE_ARRAYSIZE(bindings),
      {CapturePlacementRejection, &rejection_count}, &arena_, &resources,
      &valid));
  EXPECT_FALSE(valid);
  EXPECT_EQ(rejection_count, 1u);
  EXPECT_EQ(resources.allocation_count, 0u);
  EXPECT_EQ(resources.pool_binding_count, 0u);
}

}  // namespace
}  // namespace loom
