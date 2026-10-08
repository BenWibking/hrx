// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Routed Q4_K gate/up SwiGLU compiler workload.
//
// One wave computes one routed output channel. Each loop iteration consumes a
// 1024-element K stripe through paired Q4_K decode, Q8_1 dot products, and
// gate/up accumulation. Varying input_size changes the loop bound while keeping
// the compiled body bounded.

#include <loomcxx/kernel.h>
#include <loomcxx/predicate.h>
#include <loomcxx/scalar.h>
#include <loomcxx/vector.h>

#include <stdfloat>

using F16x4 = std::float16_t __attribute__((ext_vector_type(4)));
using F16x8 = std::float16_t __attribute__((ext_vector_type(8)));
using I8x16 = signed char __attribute__((ext_vector_type(16)));
using I32x4 = int __attribute__((ext_vector_type(4)));
using U8x16 = unsigned char __attribute__((ext_vector_type(16)));
using U32x4 = unsigned __attribute__((ext_vector_type(4)));

struct alignas(16) Q4KBlock {
  std::float16_t scale;
  std::float16_t minimum;
  unsigned char scales[12];
  unsigned char quants[128];
};
static_assert(sizeof(Q4KBlock) == 144);
static_assert(__builtin_offsetof(Q4KBlock, scales) == 4);
static_assert(__builtin_offsetof(Q4KBlock, quants) == 16);

struct alignas(16) Q8_1Group {
  F16x8 scales_and_sums;
  signed char quants[128];
};
static_assert(sizeof(Q8_1Group) == 144);
static_assert(__builtin_offsetof(Q8_1Group, quants) == 16);

struct Q4ScaleMinimum {
  unsigned scale;
  unsigned minimum;
};

struct Q4ChunkPair {
  U8x16 low;
  float low_scale;
  float low_minimum;
  U8x16 high;
  float high_scale;
  float high_minimum;
};

[[loom::config("ffn_routed_gate_up.input_size"),
  loom::where(loom::predicate::range(512u, 32768u) &&
              loom::predicate::multiple_of(512u))]]
extern const unsigned ffn_input_size;
[[loom::config("ffn_routed_gate_up.expert_count")]]
const unsigned ffn_expert_count = 128u;
[[loom::config("ffn_routed_gate_up.route_count")]]
const unsigned ffn_route_count = 8u;
[[loom::config("ffn_routed_gate_up.output_size")]]
const unsigned ffn_output_size = 768u;
[[loom::config("ffn_routed_gate_up.token_capacity")]]
const unsigned ffn_token_capacity = 16u;

static LOOM_FORCE_INLINE Q4ScaleMinimum q4k_scale_from_header(unsigned scale0,
                                                              unsigned scale1,
                                                              unsigned scale2,
                                                              unsigned group) {
  loom::assume(group < 8u);
  bool low_group = group < 4u;
  unsigned scale_shift = (group % 4u) * 8u;
  unsigned high_shift = scale_shift + 2u;
  unsigned minimum_shift = scale_shift + 4u;
  unsigned scale_source = low_group ? scale0 : scale2;
  unsigned minimum_source = low_group ? scale1 : scale2;
  unsigned scale_high_shift = low_group ? scale_shift : high_shift;
  unsigned minimum_low_shift = low_group ? scale_shift : minimum_shift;
  unsigned scale = ((scale_source >> scale_shift) & 15u) |
                   ((scale0 >> scale_high_shift) & 48u);
  unsigned minimum = ((minimum_source >> minimum_low_shift) & 15u) |
                     ((scale1 >> scale_high_shift) & 48u);
  return {scale, minimum};
}

static LOOM_FORCE_INLINE Q4ChunkPair q4k_chunk_pair(const Q4KBlock* block,
                                                    unsigned group_pair,
                                                    unsigned half,
                                                    U32x4 header_words) {
  loom::assume(group_pair < 4u && half < 2u);
  F16x8 header_halves = __builtin_bit_cast(F16x8, header_words);
  float scale = float(header_halves[0]);
  float minimum = float(header_halves[1]);
  unsigned scale0 = header_words[1];
  unsigned scale1 = header_words[2];
  unsigned scale2 = header_words[3];

  const auto* chunks = reinterpret_cast<const U32x4*>(block->quants);
  U32x4 packed = chunks[group_pair * 2u + half];
  U32x4 low_words = packed & 0x0f0f0f0fu;
  U32x4 high_words = (packed >> 4u) & 0x0f0f0f0fu;
  U8x16 low = __builtin_bit_cast(U8x16, low_words);
  U8x16 high = __builtin_bit_cast(U8x16, high_words);

  Q4ScaleMinimum low_values =
      q4k_scale_from_header(scale0, scale1, scale2, group_pair * 2u);
  Q4ScaleMinimum high_values =
      q4k_scale_from_header(scale0, scale1, scale2, group_pair * 2u + 1u);
  return {low,
          scale * float(low_values.scale),
          minimum * float(low_values.minimum),
          high,
          scale * float(high_values.scale),
          minimum * float(high_values.minimum)};
}

static LOOM_FORCE_INLINE float q4k_q8_1_dot(U8x16 q4_values, float scale,
                                            float minimum, I8x16 q8_values,
                                            float q8_scale, float q8_sum) {
  I32x4 partial = {};
  partial = loom::vector::dot4i(q4_values, q8_values, partial);
  int sum = loom::vector::reduce::addi(partial, 0);
  return q8_scale * scale * float(sum) - minimum * (q8_sum * 0.5f);
}

LOOM_DEVICE
LOOM_TEMPLATE_DECL("ffn_routed_gate_up.q4k_q8.body")
void ffn_routed_gate_up_q4k_q8_body(
    bool publish_output, unsigned token_count, unsigned token,
    unsigned route_count, unsigned route, unsigned route_stride,
    unsigned expert_count, unsigned output_size, unsigned channel,
    unsigned lane, const Q8_1Group* q8_input, const int* route_ids,
    const Q4KBlock* gate_weight, const Q4KBlock* up_weight, float* output);

LOOM_TEMPLATE_DEF(ffn_routed_gate_up_q4k_q8_body)
void ffn_routed_gate_up_swiglu_q4k_q8_body(
    bool publish_output, unsigned token_count, unsigned token,
    unsigned route_count, unsigned route, unsigned route_stride,
    unsigned expert_count, unsigned output_size, unsigned channel,
    unsigned lane, const Q8_1Group* q8_input, const int* route_ids,
    const Q4KBlock* gate_weight, const Q4KBlock* up_weight, float* output) {
  loom::assume(token_count >= 1u && token_count <= 512u);
  loom::assume(route_count >= 1u && route_count <= 8u);
  loom::assume(route_stride >= 1u && route_stride <= 128u);
  loom::assume(route_count <= route_stride);
  loom::assume(expert_count >= 1u && expert_count <= 128u);
  loom::assume(output_size >= 1u && output_size <= 4096u);
  loom::assume(token < token_count && route < route_count &&
               channel < output_size && lane < 32u);

  unsigned q4_block_count = ffn_input_size / 256u;
  unsigned q8_group_count = ffn_input_size / 128u;
  unsigned expert = unsigned(route_ids[token * route_stride + route]);
  loom::assume(expert < expert_count);
  loom::assume(expert < 128u);
  unsigned row_block_base = (expert * output_size + channel) * q4_block_count;
  unsigned q4_group_pair = (lane / 2u) % 4u;
  unsigned q4_half = lane % 2u;
  unsigned lane_q4_block = lane / 8u;
  unsigned q8_group_in_block = q4_group_pair / 2u;
  unsigned pair_in_q8_group = q4_group_pair % 2u;
  unsigned q8_low_inner_block = pair_in_q8_group * 2u;
  unsigned q8_high_inner_block = q8_low_inner_block + 1u;

  float gate_accumulator = 0.0f;
  float up_accumulator = 0.0f;
  unsigned iteration_count = (ffn_input_size + 1023u) / 1024u;
  for (unsigned iteration = 0; iteration < iteration_count; ++iteration) {
    unsigned q4_block = iteration * 4u + lane_q4_block;
    if (q4_block < q4_block_count) {
      unsigned q8_group = q4_block * 2u + q8_group_in_block;
      const Q8_1Group* group = &q8_input[token * q8_group_count + q8_group];
      const auto* q8_header_pairs =
          reinterpret_cast<const F16x4*>(&group->scales_and_sums);
      F16x4 q8_scales = q8_header_pairs[pair_in_q8_group];
      float q8_low_scale = float(q8_scales[0]);
      float q8_low_sum = float(q8_scales[1]);
      float q8_high_scale = float(q8_scales[2]);
      float q8_high_sum = float(q8_scales[3]);
      const auto* q8_chunks = reinterpret_cast<const I8x16*>(group->quants);
      I8x16 q8_low = q8_chunks[q8_low_inner_block * 2u + q4_half];
      I8x16 q8_high = q8_chunks[q8_high_inner_block * 2u + q4_half];

      const Q4KBlock* gate_block = &gate_weight[row_block_base + q4_block];
      const Q4KBlock* up_block = &up_weight[row_block_base + q4_block];
      U32x4 gate_header = *reinterpret_cast<const U32x4*>(gate_block);
      U32x4 up_header = *reinterpret_cast<const U32x4*>(up_block);
      Q4ChunkPair gate =
          q4k_chunk_pair(gate_block, q4_group_pair, q4_half, gate_header);
      Q4ChunkPair up =
          q4k_chunk_pair(up_block, q4_group_pair, q4_half, up_header);

      gate_accumulator +=
          q4k_q8_1_dot(gate.low, gate.low_scale, gate.low_minimum, q8_low,
                       q8_low_scale, q8_low_sum) +
          q4k_q8_1_dot(gate.high, gate.high_scale, gate.high_minimum, q8_high,
                       q8_high_scale, q8_high_sum);
      up_accumulator += q4k_q8_1_dot(up.low, up.low_scale, up.low_minimum,
                                     q8_low, q8_low_scale, q8_low_sum) +
                        q4k_q8_1_dot(up.high, up.high_scale, up.high_minimum,
                                     q8_high, q8_high_scale, q8_high_sum);
    }
  }

  float gate = loom::kernel::subgroup::reduce::addf(gate_accumulator);
  float up = loom::kernel::subgroup::reduce::addf(up_accumulator);
  if (publish_output && lane == 0u) {
    output[(token * route_count + route) * output_size + channel] =
        loom::scalar::siluf(gate) * up;
  }
}

static loom::kernel::configuration ffn_routed_gate_up_configuration(
    unsigned, unsigned, unsigned, unsigned, unsigned) {
  return {{(ffn_output_size + 3u) / 4u, ffn_route_count, ffn_token_capacity},
          {128u, 1u, 1u}};
}

[[loom::kernel(ffn_routed_gate_up_configuration)]] void
ffn_routed_gate_up_swiglu_q4k_q8(
    unsigned token_count, unsigned route_count, unsigned route_stride,
    unsigned expert_count, unsigned output_size,
    [[loom::noalias, loom::assume_aligned(64)]] const Q8_1Group* q8_input,
    [[loom::noalias, loom::assume_aligned(64)]] const int* route_ids,
    [[loom::noalias, loom::assume_aligned(64)]] const Q4KBlock* gate_weight,
    [[loom::noalias, loom::assume_aligned(64)]] const Q4KBlock* up_weight,
    [[loom::noalias, loom::assume_aligned(64)]] float* output) {
  loom::assume(token_count >= 1u && token_count <= ffn_token_capacity);
  loom::assume(route_count == ffn_route_count);
  loom::assume(route_stride >= 1u && route_stride <= 128u);
  loom::assume(expert_count == ffn_expert_count);
  loom::assume(output_size == ffn_output_size);

  unsigned token_candidate = loom::kernel::workgroup::id.z;
  unsigned channel_candidate =
      loom::kernel::workgroup::id.x * 4u + loom::kernel::subgroup::id();
  unsigned route = loom::kernel::workgroup::id.y;
  unsigned lane = loom::kernel::subgroup::lane_id();
  bool valid_token = token_candidate < token_count;
  unsigned token = valid_token ? token_candidate : 0u;
  unsigned channel = channel_candidate;
  loom::assume(token < token_count && route < route_count &&
               channel < output_size);
  ffn_routed_gate_up_q4k_q8_body(valid_token, token_count, token, route_count,
                                 route, route_stride, expert_count, output_size,
                                 channel, lane, q8_input, route_ids,
                                 gate_weight, up_weight, output);
}
