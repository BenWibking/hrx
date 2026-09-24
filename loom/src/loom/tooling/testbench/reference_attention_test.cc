// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/testbench/reference_attention.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "iree/hal/api.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace loom {
namespace {

enum {
  kPageRows = 16,
  kHeadElements = 512,
  kScaleGroups = 16,
  kPayloadPlaneBytes = 8192,
  kPageBytes = 8448,
};

template <typename T>
struct BufferContents {
  const T* values;
  iree_host_size_t count;
};

template <typename T>
static iree_status_t FillBufferView(void* user_data,
                                    iree_byte_span_t buffer_contents) {
  const BufferContents<T>* values =
      static_cast<const BufferContents<T>*>(user_data);
  memcpy(buffer_contents.data, values->values,
         values->count * sizeof(values->values[0]));
  return iree_ok_status();
}

class ReferenceAttentionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    IREE_ASSERT_OK(iree_hal_allocator_create_heap(
        IREE_SV("reference_attention_test"), host_allocator_, host_allocator_,
        &device_allocator_));
    options_ = {
        /*.device_allocator=*/device_allocator_,
        /*.result_buffer_params=*/BufferParams(),
        /*.host_allocator=*/host_allocator_,
    };
  }

  void TearDown() override { iree_hal_allocator_release(device_allocator_); }

  iree_hal_buffer_params_t BufferParams() const {
    return iree_hal_buffer_params_t{
        /*.usage=*/IREE_HAL_BUFFER_USAGE_DEFAULT |
            IREE_HAL_BUFFER_USAGE_TRANSFER | IREE_HAL_BUFFER_USAGE_MAPPING,
        /*.access=*/IREE_HAL_MEMORY_ACCESS_ALL,
        /*.type=*/IREE_HAL_MEMORY_TYPE_HOST_LOCAL |
            IREE_HAL_MEMORY_TYPE_DEVICE_VISIBLE,
        /*.queue_family_affinity=*/IREE_HAL_QUEUE_FAMILY_AFFINITY_ANY,
    };
  }

  template <typename T>
  loom_testbench_value_t MakeBufferView(std::vector<iree_hal_dim_t> shape,
                                        iree_hal_element_type_t element_type,
                                        const std::vector<T>& values) {
    BufferContents<T> contents = {
        /*.values=*/values.data(),
        /*.count=*/values.size(),
    };
    iree_hal_buffer_view_t* buffer_view = nullptr;
    IREE_CHECK_OK(iree_hal_buffer_view_generate(
        device_allocator_, BufferParams(), shape.size(), shape.data(),
        element_type, IREE_HAL_ENCODING_TYPE_DENSE_ROW_MAJOR, FillBufferView<T>,
        &contents, &buffer_view));
    loom_testbench_value_t value = {};
    IREE_CHECK_OK(
        loom_testbench_value_set_buffer_view_move(buffer_view, &value));
    return value;
  }

  std::vector<float> ReadF32BufferView(
      const loom_testbench_value_t& value,
      const std::vector<iree_hal_dim_t>& expected_shape) {
    iree_hal_buffer_view_t* buffer_view =
        loom_testbench_value_buffer_view(&value);
    EXPECT_NE(buffer_view, nullptr);
    EXPECT_EQ(iree_hal_buffer_view_shape_rank(buffer_view),
              expected_shape.size());
    iree_host_size_t element_count = 1;
    for (iree_host_size_t i = 0; i < expected_shape.size(); ++i) {
      EXPECT_EQ(iree_hal_buffer_view_shape_dim(buffer_view, i),
                expected_shape[i]);
      element_count *= (iree_host_size_t)expected_shape[i];
    }
    EXPECT_EQ(iree_hal_buffer_view_element_type(buffer_view),
              IREE_HAL_ELEMENT_TYPE_FLOAT_32);
    std::vector<float> values(element_count);
    IREE_CHECK_OK(iree_hal_buffer_map_read(
        iree_hal_buffer_view_buffer(buffer_view), /*source_offset=*/0,
        values.data(), values.size() * sizeof(values[0])));
    return values;
  }

  void Invoke(loom_testbench_value_t* inputs,
              loom_testbench_value_t* out_results) {
    loom_testbench_oracle_provider_t provider = {};
    loom_testbench_reference_mxfp8_paged_attention_oracle_provider_initialize(
        &options_, &provider);
    loom_testbench_invocation_plan_t invocation = {};
    IREE_ASSERT_OK(provider.provider.invoke(
        provider.provider.user_data, &invocation, /*workload_count=*/0,
        /*workloads=*/nullptr, /*input_count=*/5, inputs,
        /*result_count=*/2, out_results));
  }

  iree_allocator_t host_allocator_ = iree_allocator_system();
  iree_hal_allocator_t* device_allocator_ = nullptr;
  loom_testbench_reference_oracle_options_t options_ = {};
};

TEST_F(ReferenceAttentionTest, DecodesSelectedPagePayloadAndScaleGroups) {
  std::vector<int32_t> lengths = {1};
  std::vector<float> query(kHeadElements, 0.0f);
  query[0] = 1.0f;
  std::vector<int32_t> page_table(64, -1);
  page_table[0] = 1;
  std::vector<int8_t> key_cache(2 * kPageBytes, 0x7F);
  std::vector<int8_t> value_cache(2 * kPageBytes, 0x7F);

  const iree_host_size_t selected_page = kPageBytes;
  memset(&key_cache[selected_page], 0, kHeadElements * sizeof(key_cache[0]));
  key_cache[selected_page] = 0x38;
  memset(&key_cache[selected_page + kPayloadPlaneBytes], 127, kScaleGroups);

  static const uint8_t kPayloadBits[kScaleGroups] = {
      0x38, 0xB8, 0x01, 0x38, 0x38, 0x38, 0x38, 0x38,
      0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x7E, 0x38,
  };
  static const uint8_t kScaleBits[kScaleGroups] = {
      0,   113, 114, 115, 116, 117, 118, 119,
      120, 121, 122, 123, 124, 125, 126, 255,
  };
  for (iree_host_size_t group = 0; group < kScaleGroups; ++group) {
    memset(&value_cache[selected_page + group * 32], kPayloadBits[group], 32);
    value_cache[selected_page + kPayloadPlaneBytes + group] =
        (int8_t)kScaleBits[group];
  }

  loom_testbench_value_t inputs[5] = {
      MakeBufferView<int32_t>({1}, IREE_HAL_ELEMENT_TYPE_SINT_32, lengths),
      MakeBufferView<float>({1, kHeadElements}, IREE_HAL_ELEMENT_TYPE_FLOAT_32,
                            query),
      MakeBufferView<int32_t>({1, 64}, IREE_HAL_ELEMENT_TYPE_SINT_32,
                              page_table),
      MakeBufferView<int8_t>({2, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             key_cache),
      MakeBufferView<int8_t>({2, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             value_cache),
  };
  loom_testbench_value_t results[2] = {};
  Invoke(inputs, results);

  const std::vector<float> state = ReadF32BufferView(results[0], {1, 2});
  EXPECT_FLOAT_EQ(state[0], 0.044194173824159220275f);
  EXPECT_FLOAT_EQ(state[1], 1.0f);
  const std::vector<float> output =
      ReadF32BufferView(results[1], {1, kHeadElements});
  for (iree_host_size_t group = 0; group < kScaleGroups; ++group) {
    const float payload = group == 1    ? -1.0f
                          : group == 2  ? std::ldexp(1.0f, -9)
                          : group == 14 ? 448.0f
                                        : 1.0f;
    const float expected =
        kScaleBits[group] == 255
            ? NAN
            : payload * std::ldexp(1.0f, (int)kScaleBits[group] - 127);
    for (iree_host_size_t channel = group * 32; channel < (group + 1) * 32;
         ++channel) {
      if (std::isnan(expected)) {
        EXPECT_TRUE(std::isnan(output[channel]));
      } else {
        EXPECT_FLOAT_EQ(output[channel], expected);
      }
    }
  }

  for (loom_testbench_value_t& result : results) {
    loom_testbench_value_deinitialize(&result);
  }
  for (loom_testbench_value_t& input : inputs) {
    loom_testbench_value_deinitialize(&input);
  }
}

TEST_F(ReferenceAttentionTest, ComputesStableSoftmaxAcrossCacheRows) {
  std::vector<int32_t> lengths = {2};
  std::vector<float> query(kHeadElements, 0.0f);
  query[0] = 1.0f;
  std::vector<int32_t> page_table(64, -1);
  page_table[0] = 0;
  std::vector<int8_t> key_cache(kPageBytes, 0);
  std::vector<int8_t> value_cache(kPageBytes, 0);
  memset(&key_cache[kPayloadPlaneBytes], 127,
         2 * kScaleGroups * sizeof(key_cache[0]));
  memset(&value_cache[kPayloadPlaneBytes], 127,
         2 * kScaleGroups * sizeof(value_cache[0]));
  key_cache[0] = 0x38;
  key_cache[kHeadElements] = 0x40;
  value_cache[0] = 0x38;
  value_cache[kHeadElements] = 0x40;

  loom_testbench_value_t inputs[5] = {
      MakeBufferView<int32_t>({1}, IREE_HAL_ELEMENT_TYPE_SINT_32, lengths),
      MakeBufferView<float>({1, kHeadElements}, IREE_HAL_ELEMENT_TYPE_FLOAT_32,
                            query),
      MakeBufferView<int32_t>({1, 64}, IREE_HAL_ELEMENT_TYPE_SINT_32,
                              page_table),
      MakeBufferView<int8_t>({1, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             key_cache),
      MakeBufferView<int8_t>({1, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             value_cache),
  };
  loom_testbench_value_t results[2] = {};
  Invoke(inputs, results);

  constexpr double kScoreScale = 0.044194173824159220275;
  const double first_weight = std::exp(-kScoreScale);
  const float expected_denominator = (float)(first_weight + 1.0);
  const float expected_value =
      (float)((first_weight + 2.0) / (first_weight + 1.0));
  const std::vector<float> state = ReadF32BufferView(results[0], {1, 2});
  EXPECT_FLOAT_EQ(state[0], (float)(2.0 * kScoreScale));
  EXPECT_FLOAT_EQ(state[1], expected_denominator);
  const std::vector<float> output =
      ReadF32BufferView(results[1], {1, kHeadElements});
  EXPECT_FLOAT_EQ(output[0], expected_value);
  for (iree_host_size_t channel = 1; channel < kHeadElements; ++channel) {
    EXPECT_EQ(output[channel], 0.0f);
  }

  for (loom_testbench_value_t& result : results) {
    loom_testbench_value_deinitialize(&result);
  }
  for (loom_testbench_value_t& input : inputs) {
    loom_testbench_value_deinitialize(&input);
  }
}

TEST_F(ReferenceAttentionTest, SkipsAbsentPagesWithoutDecodingPoison) {
  std::vector<int32_t> lengths = {1024};
  std::vector<float> query(kHeadElements, 1.0f);
  std::vector<int32_t> page_table(64, -1);
  std::vector<int8_t> poison(kPageBytes, 0x7F);
  memset(&poison[kPayloadPlaneBytes], 0xFF, kPageBytes - kPayloadPlaneBytes);
  loom_testbench_value_t inputs[5] = {
      MakeBufferView<int32_t>({1}, IREE_HAL_ELEMENT_TYPE_SINT_32, lengths),
      MakeBufferView<float>({1, kHeadElements}, IREE_HAL_ELEMENT_TYPE_FLOAT_32,
                            query),
      MakeBufferView<int32_t>({1, 64}, IREE_HAL_ELEMENT_TYPE_SINT_32,
                              page_table),
      MakeBufferView<int8_t>({1, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             poison),
      MakeBufferView<int8_t>({1, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             poison),
  };
  loom_testbench_value_t results[2] = {};
  Invoke(inputs, results);

  EXPECT_EQ(ReadF32BufferView(results[0], {1, 2}),
            (std::vector<float>{-1000000000.0f, 0.0f}));
  EXPECT_EQ(ReadF32BufferView(results[1], {1, kHeadElements}),
            std::vector<float>(kHeadElements, 0.0f));

  for (loom_testbench_value_t& result : results) {
    loom_testbench_value_deinitialize(&result);
  }
  for (loom_testbench_value_t& input : inputs) {
    loom_testbench_value_deinitialize(&input);
  }
}

TEST_F(ReferenceAttentionTest, RejectsActivePageOutsideCache) {
  std::vector<int32_t> lengths = {1};
  std::vector<float> query(kHeadElements, 0.0f);
  std::vector<int32_t> page_table(64, -1);
  page_table[0] = 1;
  std::vector<int8_t> cache(kPageBytes, 0);
  loom_testbench_value_t inputs[5] = {
      MakeBufferView<int32_t>({1}, IREE_HAL_ELEMENT_TYPE_SINT_32, lengths),
      MakeBufferView<float>({1, kHeadElements}, IREE_HAL_ELEMENT_TYPE_FLOAT_32,
                            query),
      MakeBufferView<int32_t>({1, 64}, IREE_HAL_ELEMENT_TYPE_SINT_32,
                              page_table),
      MakeBufferView<int8_t>({1, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             cache),
      MakeBufferView<int8_t>({1, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             cache),
  };
  loom_testbench_oracle_provider_t provider = {};
  loom_testbench_reference_mxfp8_paged_attention_oracle_provider_initialize(
      &options_, &provider);
  loom_testbench_invocation_plan_t invocation = {};
  loom_testbench_value_t results[2] = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_OUT_OF_RANGE,
      provider.provider.invoke(provider.provider.user_data, &invocation,
                               /*workload_count=*/0, /*workloads=*/nullptr,
                               IREE_ARRAYSIZE(inputs), inputs,
                               IREE_ARRAYSIZE(results), results));
  for (loom_testbench_value_t& input : inputs) {
    loom_testbench_value_deinitialize(&input);
  }
}

TEST_F(ReferenceAttentionTest, RejectsLengthOutsidePageTable) {
  std::vector<int32_t> lengths = {17};
  std::vector<float> query(kHeadElements, 0.0f);
  std::vector<int32_t> page_table = {0};
  std::vector<int8_t> cache(kPageBytes, 0);
  loom_testbench_value_t inputs[5] = {
      MakeBufferView<int32_t>({1}, IREE_HAL_ELEMENT_TYPE_SINT_32, lengths),
      MakeBufferView<float>({1, kHeadElements}, IREE_HAL_ELEMENT_TYPE_FLOAT_32,
                            query),
      MakeBufferView<int32_t>({1, 1}, IREE_HAL_ELEMENT_TYPE_SINT_32,
                              page_table),
      MakeBufferView<int8_t>({1, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             cache),
      MakeBufferView<int8_t>({1, kPageBytes}, IREE_HAL_ELEMENT_TYPE_SINT_8,
                             cache),
  };
  loom_testbench_oracle_provider_t provider = {};
  loom_testbench_reference_mxfp8_paged_attention_oracle_provider_initialize(
      &options_, &provider);
  loom_testbench_invocation_plan_t invocation = {};
  loom_testbench_value_t results[2] = {};
  IREE_EXPECT_STATUS_IS(
      IREE_STATUS_OUT_OF_RANGE,
      provider.provider.invoke(provider.provider.user_data, &invocation,
                               /*workload_count=*/0, /*workloads=*/nullptr,
                               IREE_ARRAYSIZE(inputs), inputs,
                               IREE_ARRAYSIZE(results), results));
  for (loom_testbench_value_t& input : inputs) {
    loom_testbench_value_deinitialize(&input);
  }
}

}  // namespace
}  // namespace loom
