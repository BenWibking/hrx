// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/tooling/testbench/reference_attention.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
  LOOM_TESTBENCH_MXFP8_PAGE_ROW_COUNT = 16,
  LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT = 512,
  LOOM_TESTBENCH_MXFP8_SCALE_GROUP_ELEMENT_COUNT = 32,
  LOOM_TESTBENCH_MXFP8_SCALE_ROW_BYTE_COUNT = 16,
  LOOM_TESTBENCH_MXFP8_PAYLOAD_PLANE_BYTE_COUNT = 8192,
  LOOM_TESTBENCH_MXFP8_PAGE_BYTE_COUNT = 8448,
};

typedef struct loom_testbench_reference_rank1_view_t {
  // Borrowed HAL buffer view.
  iree_hal_buffer_view_t* view;
  // Number of elements in the view.
  iree_host_size_t element_count;
} loom_testbench_reference_rank1_view_t;

typedef struct loom_testbench_reference_rank2_view_t {
  // Borrowed HAL buffer view.
  iree_hal_buffer_view_t* view;
  // Number of rows in the view.
  iree_host_size_t row_count;
  // Number of columns in the view.
  iree_host_size_t column_count;
} loom_testbench_reference_rank2_view_t;

typedef struct loom_testbench_reference_mapping_t {
  // Active HAL mapping when |is_mapped| is true.
  iree_hal_buffer_mapping_t mapping;
  // Whether |mapping| must be unmapped.
  bool is_mapped;
} loom_testbench_reference_mapping_t;

typedef struct loom_testbench_reference_attention_inputs_t {
  // Sequence lengths indexed by instance.
  const int32_t* lengths;
  // Dense query rows indexed by instance and channel.
  const float* queries;
  // Logical-to-physical page IDs indexed by instance and page slot.
  const int32_t* page_table;
  // Raw K cache bytes in V4.1 page layout.
  const uint8_t* key_cache;
  // Raw V cache bytes in V4.1 page layout.
  const uint8_t* value_cache;
  // Number of independent query instances.
  iree_host_size_t instance_count;
  // Number of logical page slots per instance.
  iree_host_size_t page_slot_count;
  // Number of physical cache pages.
  iree_host_size_t physical_page_count;
} loom_testbench_reference_attention_inputs_t;

typedef struct loom_testbench_reference_result_contents_t {
  // Borrowed dense result bytes.
  const uint8_t* data;
  // Number of bytes in |data|.
  iree_host_size_t data_length;
} loom_testbench_reference_result_contents_t;

static iree_status_t loom_testbench_reference_attention_dim_to_host_size(
    iree_hal_dim_t dim, iree_host_size_t* out_value) {
  if (dim > IREE_HOST_SIZE_MAX) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "buffer view dimension exceeds host size range");
  }
  *out_value = (iree_host_size_t)dim;
  return iree_ok_status();
}

static iree_status_t loom_testbench_reference_attention_value_buffer_view(
    const loom_testbench_value_t* value, iree_string_view_t name,
    iree_hal_buffer_view_t** out_view) {
  *out_view = loom_testbench_value_buffer_view(value);
  if (*out_view == NULL) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "reference.mxfp8_paged_attention %.*s input must "
                            "be a buffer view",
                            (int)name.size, name.data);
  }
  return iree_ok_status();
}

static iree_status_t loom_testbench_reference_attention_rank1_view_initialize(
    const loom_testbench_value_t* value, iree_string_view_t name,
    iree_hal_element_type_t element_type,
    loom_testbench_reference_rank1_view_t* out_view) {
  iree_hal_buffer_view_t* view = NULL;
  IREE_RETURN_IF_ERROR(
      loom_testbench_reference_attention_value_buffer_view(value, name, &view));
  if (iree_hal_buffer_view_shape_rank(view) != 1 ||
      iree_hal_buffer_view_element_type(view) != element_type) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "reference.mxfp8_paged_attention %.*s input has the wrong type",
        (int)name.size, name.data);
  }
  *out_view = (loom_testbench_reference_rank1_view_t){.view = view};
  return loom_testbench_reference_attention_dim_to_host_size(
      iree_hal_buffer_view_shape_dim(view, 0), &out_view->element_count);
}

static iree_status_t loom_testbench_reference_attention_rank2_view_initialize(
    const loom_testbench_value_t* value, iree_string_view_t name,
    iree_hal_element_type_t element_type,
    loom_testbench_reference_rank2_view_t* out_view) {
  iree_hal_buffer_view_t* view = NULL;
  IREE_RETURN_IF_ERROR(
      loom_testbench_reference_attention_value_buffer_view(value, name, &view));
  if (iree_hal_buffer_view_shape_rank(view) != 2 ||
      iree_hal_buffer_view_element_type(view) != element_type) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "reference.mxfp8_paged_attention %.*s input has the wrong type",
        (int)name.size, name.data);
  }
  *out_view = (loom_testbench_reference_rank2_view_t){.view = view};
  IREE_RETURN_IF_ERROR(loom_testbench_reference_attention_dim_to_host_size(
      iree_hal_buffer_view_shape_dim(view, 0), &out_view->row_count));
  return loom_testbench_reference_attention_dim_to_host_size(
      iree_hal_buffer_view_shape_dim(view, 1), &out_view->column_count);
}

static iree_status_t loom_testbench_reference_attention_mapping_initialize(
    iree_hal_buffer_view_t* view,
    loom_testbench_reference_mapping_t* out_mapping) {
  memset(out_mapping, 0, sizeof(*out_mapping));
  IREE_RETURN_IF_ERROR(iree_hal_buffer_map_range(
      iree_hal_buffer_view_buffer(view), IREE_HAL_MAPPING_MODE_SCOPED,
      IREE_HAL_MEMORY_ACCESS_READ, 0, IREE_HAL_WHOLE_BUFFER,
      &out_mapping->mapping));
  out_mapping->is_mapped = true;
  return iree_ok_status();
}

static iree_status_t loom_testbench_reference_attention_mapping_deinitialize(
    loom_testbench_reference_mapping_t* mapping) {
  if (!mapping->is_mapped) {
    return iree_ok_status();
  }
  mapping->is_mapped = false;
  return iree_hal_buffer_unmap_range(&mapping->mapping);
}

// Decodes one E4M3FN byte without using the compiler's scalarization or the
// runtime float8 conversion helpers.
static double loom_testbench_reference_attention_decode_e4m3fn(uint8_t bits) {
  const uint8_t magnitude = bits & 0x7Fu;
  const uint8_t exponent = magnitude >> 3;
  const uint8_t mantissa = magnitude & 0x7u;
  if (exponent == 0xFu && mantissa == 0x7u) {
    return NAN;
  }
  const double value = exponent == 0
                           ? ldexp((double)mantissa, -9)
                           : ldexp((double)(8u + mantissa), exponent - 10);
  return (bits & 0x80u) != 0 ? -value : value;
}

static double loom_testbench_reference_attention_decode_e8m0(uint8_t bits) {
  return bits == UINT8_MAX ? NAN : ldexp(1.0, (int)bits - 127);
}

static double loom_testbench_reference_attention_cache_element(
    const uint8_t* cache, iree_host_size_t page, iree_host_size_t row,
    iree_host_size_t channel) {
  const iree_host_size_t page_base =
      page * LOOM_TESTBENCH_MXFP8_PAGE_BYTE_COUNT;
  const iree_host_size_t payload_offset =
      page_base + row * LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT + channel;
  const iree_host_size_t scale_offset =
      page_base + LOOM_TESTBENCH_MXFP8_PAYLOAD_PLANE_BYTE_COUNT +
      row * LOOM_TESTBENCH_MXFP8_SCALE_ROW_BYTE_COUNT +
      channel / LOOM_TESTBENCH_MXFP8_SCALE_GROUP_ELEMENT_COUNT;
  return loom_testbench_reference_attention_decode_e4m3fn(
             cache[payload_offset]) *
         loom_testbench_reference_attention_decode_e8m0(cache[scale_offset]);
}

static iree_status_t loom_testbench_reference_attention_resolve_token(
    const loom_testbench_reference_attention_inputs_t* inputs,
    iree_host_size_t instance, iree_host_size_t token, bool* out_present,
    iree_host_size_t* out_page, iree_host_size_t* out_row) {
  const iree_host_size_t page_slot =
      token / LOOM_TESTBENCH_MXFP8_PAGE_ROW_COUNT;
  const int32_t page_id =
      inputs->page_table[instance * inputs->page_slot_count + page_slot];
  if (page_id < 0) {
    *out_present = false;
    *out_page = 0;
    *out_row = 0;
    return iree_ok_status();
  }
  if ((iree_host_size_t)page_id >= inputs->physical_page_count) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "reference.mxfp8_paged_attention page ID %" PRId32
                            " exceeds the physical cache",
                            page_id);
  }
  *out_present = true;
  *out_page = (iree_host_size_t)page_id;
  *out_row = token % LOOM_TESTBENCH_MXFP8_PAGE_ROW_COUNT;
  return iree_ok_status();
}

static double loom_testbench_reference_attention_score(
    const loom_testbench_reference_attention_inputs_t* inputs,
    iree_host_size_t instance, iree_host_size_t page, iree_host_size_t row) {
  const float* query =
      &inputs->queries[instance * LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT];
  double dot = 0.0;
  for (iree_host_size_t channel = 0;
       channel < LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT; ++channel) {
    dot += (double)query[channel] *
           loom_testbench_reference_attention_cache_element(inputs->key_cache,
                                                            page, row, channel);
  }
  return dot * 0.044194173824159220275;
}

static iree_status_t loom_testbench_reference_attention_compute(
    const loom_testbench_reference_attention_inputs_t* inputs,
    double* accumulator, float* state, float* output) {
  iree_host_size_t token_capacity = 0;
  if (!iree_host_size_checked_mul(inputs->page_slot_count,
                                  LOOM_TESTBENCH_MXFP8_PAGE_ROW_COUNT,
                                  &token_capacity)) {
    return iree_make_status(
        IREE_STATUS_OUT_OF_RANGE,
        "reference.mxfp8_paged_attention token capacity overflow");
  }
  iree_status_t status = iree_ok_status();
  for (iree_host_size_t instance = 0;
       iree_status_is_ok(status) && instance < inputs->instance_count;
       ++instance) {
    const int32_t signed_length = inputs->lengths[instance];
    if (signed_length < 0 || (iree_host_size_t)signed_length > token_capacity) {
      status =
          iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                           "reference.mxfp8_paged_attention length %" PRId32
                           " exceeds the page table",
                           signed_length);
      break;
    }
    const iree_host_size_t length = (iree_host_size_t)signed_length;
    double maximum = -1000000000.0;
    iree_host_size_t present_count = 0;
    for (iree_host_size_t token = 0;
         iree_status_is_ok(status) && token < length; ++token) {
      bool present = false;
      iree_host_size_t page = 0;
      iree_host_size_t row = 0;
      status = loom_testbench_reference_attention_resolve_token(
          inputs, instance, token, &present, &page, &row);
      if (iree_status_is_ok(status) && present) {
        maximum = fmax(maximum, loom_testbench_reference_attention_score(
                                    inputs, instance, page, row));
        ++present_count;
      }
    }

    memset(accumulator, 0,
           LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT * sizeof(*accumulator));
    double denominator = 0.0;
    for (iree_host_size_t token = 0;
         iree_status_is_ok(status) && token < length && present_count != 0;
         ++token) {
      bool present = false;
      iree_host_size_t page = 0;
      iree_host_size_t row = 0;
      status = loom_testbench_reference_attention_resolve_token(
          inputs, instance, token, &present, &page, &row);
      if (!iree_status_is_ok(status) || !present) {
        continue;
      }
      const double weight = exp(loom_testbench_reference_attention_score(
                                    inputs, instance, page, row) -
                                maximum);
      denominator += weight;
      for (iree_host_size_t channel = 0;
           channel < LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT; ++channel) {
        accumulator[channel] +=
            weight * loom_testbench_reference_attention_cache_element(
                         inputs->value_cache, page, row, channel);
      }
    }

    if (!iree_status_is_ok(status)) {
      break;
    }
    state[instance * 2] = (float)maximum;
    state[instance * 2 + 1] = (float)denominator;
    float* instance_output =
        &output[instance * LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT];
    for (iree_host_size_t channel = 0;
         channel < LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT; ++channel) {
      instance_output[channel] =
          present_count == 0 ? 0.0f
                             : (float)(accumulator[channel] / denominator);
    }
  }
  return status;
}

static iree_status_t loom_testbench_reference_attention_fill_result(
    void* user_data, iree_byte_span_t contents) {
  const loom_testbench_reference_result_contents_t* result =
      (const loom_testbench_reference_result_contents_t*)user_data;
  if (contents.data_length != result->data_length) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "reference.mxfp8_paged_attention result byte count mismatch");
  }
  memcpy(contents.data, result->data, result->data_length);
  return iree_ok_status();
}

static iree_status_t loom_testbench_reference_attention_allocate_result(
    const loom_testbench_reference_oracle_options_t* options,
    iree_host_size_t rows, iree_host_size_t columns, const float* values,
    iree_hal_buffer_view_t** out_view) {
  iree_host_size_t element_count = 0;
  iree_host_size_t byte_count = 0;
  if (!iree_host_size_checked_mul(rows, columns, &element_count) ||
      !iree_host_size_checked_mul(element_count, sizeof(*values),
                                  &byte_count)) {
    return iree_make_status(
        IREE_STATUS_OUT_OF_RANGE,
        "reference.mxfp8_paged_attention result size overflow");
  }
  const iree_hal_dim_t shape[] = {(iree_hal_dim_t)rows,
                                  (iree_hal_dim_t)columns};
  const loom_testbench_reference_result_contents_t contents = {
      .data = (const uint8_t*)values,
      .data_length = byte_count,
  };
  return iree_hal_buffer_view_generate(
      options->device_allocator, options->result_buffer_params,
      IREE_ARRAYSIZE(shape), shape, IREE_HAL_ELEMENT_TYPE_FLOAT_32,
      IREE_HAL_ENCODING_TYPE_DENSE_ROW_MAJOR,
      loom_testbench_reference_attention_fill_result, (void*)&contents,
      out_view);
}

static iree_status_t loom_testbench_reference_attention_validate_shapes(
    const loom_testbench_reference_rank1_view_t* lengths,
    const loom_testbench_reference_rank2_view_t* queries,
    const loom_testbench_reference_rank2_view_t* page_table,
    const loom_testbench_reference_rank2_view_t* key_cache,
    const loom_testbench_reference_rank2_view_t* value_cache) {
  if (queries->row_count != lengths->element_count ||
      page_table->row_count != lengths->element_count ||
      queries->column_count != LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "reference.mxfp8_paged_attention query/page-table shape mismatch");
  }
  if (page_table->column_count == 0 || key_cache->row_count == 0 ||
      key_cache->row_count != value_cache->row_count ||
      key_cache->column_count != LOOM_TESTBENCH_MXFP8_PAGE_BYTE_COUNT ||
      value_cache->column_count != LOOM_TESTBENCH_MXFP8_PAGE_BYTE_COUNT) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "reference.mxfp8_paged_attention cache shape mismatch");
  }
  return iree_ok_status();
}

static iree_status_t loom_testbench_reference_mxfp8_paged_attention_invoke(
    void* user_data, const loom_testbench_invocation_plan_t* invocation,
    iree_host_size_t workload_count, const loom_testbench_value_t* workloads,
    iree_host_size_t input_count, const loom_testbench_value_t* inputs,
    iree_host_size_t result_count, loom_testbench_value_t* out_results) {
  (void)workloads;
  const loom_testbench_reference_oracle_options_t* options =
      (const loom_testbench_reference_oracle_options_t*)user_data;
  if (workload_count != 0 || input_count != 5 || result_count != 2) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "reference.mxfp8_paged_attention expects 5 inputs and 2 results");
  }
  if (invocation->attrs.count != 0) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "reference.mxfp8_paged_attention does not accept attributes");
  }
  if (options->device_allocator == NULL) {
    return iree_make_status(
        IREE_STATUS_FAILED_PRECONDITION,
        "reference.mxfp8_paged_attention requires a HAL allocator");
  }

  loom_testbench_reference_rank1_view_t lengths = {0};
  IREE_RETURN_IF_ERROR(loom_testbench_reference_attention_rank1_view_initialize(
      &inputs[0], IREE_SV("lengths"), IREE_HAL_ELEMENT_TYPE_SINT_32, &lengths));
  loom_testbench_reference_rank2_view_t queries = {0};
  IREE_RETURN_IF_ERROR(loom_testbench_reference_attention_rank2_view_initialize(
      &inputs[1], IREE_SV("query"), IREE_HAL_ELEMENT_TYPE_FLOAT_32, &queries));
  loom_testbench_reference_rank2_view_t page_table = {0};
  IREE_RETURN_IF_ERROR(loom_testbench_reference_attention_rank2_view_initialize(
      &inputs[2], IREE_SV("page_table"), IREE_HAL_ELEMENT_TYPE_SINT_32,
      &page_table));
  loom_testbench_reference_rank2_view_t key_cache = {0};
  IREE_RETURN_IF_ERROR(loom_testbench_reference_attention_rank2_view_initialize(
      &inputs[3], IREE_SV("key_cache"), IREE_HAL_ELEMENT_TYPE_SINT_8,
      &key_cache));
  loom_testbench_reference_rank2_view_t value_cache = {0};
  IREE_RETURN_IF_ERROR(loom_testbench_reference_attention_rank2_view_initialize(
      &inputs[4], IREE_SV("value_cache"), IREE_HAL_ELEMENT_TYPE_SINT_8,
      &value_cache));
  IREE_RETURN_IF_ERROR(loom_testbench_reference_attention_validate_shapes(
      &lengths, &queries, &page_table, &key_cache, &value_cache));

  iree_allocator_t host_allocator = options->host_allocator;
  if (iree_allocator_is_null(host_allocator)) {
    host_allocator = iree_allocator_system();
  }
  iree_host_size_t state_element_count = 0;
  iree_host_size_t output_element_count = 0;
  if (!iree_host_size_checked_mul(lengths.element_count, 2,
                                  &state_element_count) ||
      !iree_host_size_checked_mul(lengths.element_count,
                                  LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT,
                                  &output_element_count)) {
    return iree_make_status(
        IREE_STATUS_OUT_OF_RANGE,
        "reference.mxfp8_paged_attention result size overflow");
  }

  float* state = NULL;
  float* output = NULL;
  double* accumulator = NULL;
  loom_testbench_reference_mapping_t mappings[5] = {0};
  iree_hal_buffer_view_t* state_view = NULL;
  iree_hal_buffer_view_t* output_view = NULL;
  iree_status_t status = iree_allocator_malloc_array(
      host_allocator, state_element_count, sizeof(*state), (void**)&state);
  if (iree_status_is_ok(status)) {
    status = iree_allocator_malloc_array(host_allocator, output_element_count,
                                         sizeof(*output), (void**)&output);
  }
  if (iree_status_is_ok(status)) {
    status = iree_allocator_malloc_array(
        host_allocator, LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT,
        sizeof(*accumulator), (void**)&accumulator);
  }
  iree_hal_buffer_view_t* input_views[] = {
      lengths.view,   queries.view,     page_table.view,
      key_cache.view, value_cache.view,
  };
  for (iree_host_size_t i = 0;
       iree_status_is_ok(status) && i < IREE_ARRAYSIZE(mappings); ++i) {
    status = loom_testbench_reference_attention_mapping_initialize(
        input_views[i], &mappings[i]);
  }
  if (iree_status_is_ok(status)) {
    const loom_testbench_reference_attention_inputs_t attention_inputs = {
        .lengths = (const int32_t*)mappings[0].mapping.contents.data,
        .queries = (const float*)mappings[1].mapping.contents.data,
        .page_table = (const int32_t*)mappings[2].mapping.contents.data,
        .key_cache = mappings[3].mapping.contents.data,
        .value_cache = mappings[4].mapping.contents.data,
        .instance_count = lengths.element_count,
        .page_slot_count = page_table.column_count,
        .physical_page_count = key_cache.row_count,
    };
    status = loom_testbench_reference_attention_compute(
        &attention_inputs, accumulator, state, output);
  }
  for (iree_host_size_t i = IREE_ARRAYSIZE(mappings); i > 0; --i) {
    status = iree_status_join(
        status, loom_testbench_reference_attention_mapping_deinitialize(
                    &mappings[i - 1]));
  }
  if (iree_status_is_ok(status)) {
    status = loom_testbench_reference_attention_allocate_result(
        options, lengths.element_count, 2, state, &state_view);
  }
  if (iree_status_is_ok(status)) {
    status = loom_testbench_reference_attention_allocate_result(
        options, lengths.element_count, LOOM_TESTBENCH_MXFP8_HEAD_ELEMENT_COUNT,
        output, &output_view);
  }
  iree_allocator_free(host_allocator, accumulator);
  iree_allocator_free(host_allocator, output);
  iree_allocator_free(host_allocator, state);

  bool state_assigned = false;
  if (iree_status_is_ok(status)) {
    status =
        loom_testbench_value_set_buffer_view_move(state_view, &out_results[0]);
    state_assigned = iree_status_is_ok(status);
    if (state_assigned) {
      state_view = NULL;
    }
  }
  if (iree_status_is_ok(status)) {
    status =
        loom_testbench_value_set_buffer_view_move(output_view, &out_results[1]);
    if (iree_status_is_ok(status)) {
      output_view = NULL;
    }
  }
  if (!iree_status_is_ok(status) && state_assigned) {
    loom_testbench_value_deinitialize(&out_results[0]);
  }
  iree_hal_buffer_view_release(output_view);
  iree_hal_buffer_view_release(state_view);
  return status;
}

void loom_testbench_reference_mxfp8_paged_attention_oracle_provider_initialize(
    const loom_testbench_reference_oracle_options_t* options,
    loom_testbench_oracle_provider_t* out_provider) {
  IREE_ASSERT_ARGUMENT(options);
  IREE_ASSERT_ARGUMENT(out_provider);
  *out_provider = (loom_testbench_oracle_provider_t){
      .name = IREE_SV("reference.mxfp8_paged_attention"),
      .provider =
          {
              .invoke = loom_testbench_reference_mxfp8_paged_attention_invoke,
              .user_data = (void*)options,
          },
  };
}
