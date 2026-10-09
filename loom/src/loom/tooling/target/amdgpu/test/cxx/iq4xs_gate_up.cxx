// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Qwen3.8 decode gate/up projection for original GGUF IQ4_XS weights. A
// 256-thread workgroup computes 16 channels for one routed expert, staging the
// 2,560-element activation in five 512-element workgroup tiles.
#include <loomcxx/kernel.h>
#include <loomcxx/predicate.h>
#include <loomcxx/scalar.h>
#include <loomcxx/target/amdgpu.h>
#include <loomcxx/vector.h>

#include <stdfloat>

using BFloat2 = std::bfloat16_t __attribute__((ext_vector_type(2)));
using BFloat4 = std::bfloat16_t __attribute__((ext_vector_type(4)));
using Bytes4 = unsigned char __attribute__((ext_vector_type(4)));
using Codes4 = signed char __attribute__((ext_vector_type(4)));
using Codes16 = signed char __attribute__((ext_vector_type(16)));
using Float2 = float __attribute__((ext_vector_type(2)));
using Float4 = float __attribute__((ext_vector_type(4)));
using Half2 = std::float16_t __attribute__((ext_vector_type(2)));
using Words2 = unsigned __attribute__((ext_vector_type(2)));

struct IQ4XSBlock {
  // Base multiplier shared by all eight 32-value groups.
  std::float16_t scale;
  // Two high bits of each signed six-bit group-scale code.
  unsigned short scales_high;
  // Two low four-bit group-scale codes per byte.
  unsigned char scales_low[4];
  // Sixteen packed bytes per group, each storing two nonlinear codes.
  unsigned char quants[128];
};
static_assert(sizeof(IQ4XSBlock) == 136 && alignof(IQ4XSBlock) == 2);
static_assert(__builtin_offsetof(IQ4XSBlock, scales_high) == 2);
static_assert(__builtin_offsetof(IQ4XSBlock, scales_low) == 4);
static_assert(__builtin_offsetof(IQ4XSBlock, quants) == 8);

[[loom::config("qwen38.iq4xs.async_staging")]]
extern const bool async_staging;
[[loom::config("qwen38.iq4xs.packet_unroll_factor"),
  loom::where(loom::predicate::range(1u, 4u))]]
extern const unsigned packet_unroll_factor;
[[loom::config("qwen38.iq4xs.block_unroll_factor"),
  loom::where(loom::predicate::range(1u, 2u))]]
extern const unsigned block_unroll_factor;
[[loom::config("qwen38.iq4xs.tile_unroll_factor"),
  loom::where(loom::predicate::range(1u, 5u))]]
extern const unsigned tile_unroll_factor;

constexpr loom::amdgpu::target iq4xs_gfx9_4_target{
    .kind = "gfx9-4-generic",
};

LOOM_DEVICE
LOOM_TEMPLATE_DECL("qwen38.iq4xs.stage_input")
void stage_iq4xs_input(bool use_async, unsigned tile, unsigned workitem,
                       const BFloat2* input, BFloat2* scratch);

LOOM_TEMPLATE_DEF(stage_iq4xs_input)
[[loom::priority(1)]] void stage_iq4xs_input_synchronous(
    bool use_async, unsigned tile, unsigned workitem, const BFloat2* input,
    BFloat2* scratch) [[loom::where(!use_async)]] {
  scratch[workitem] = input[tile * 256u + workitem];
  loom::kernel::barrier<loom::memory_space::workgroup,
                        loom::atomic::scope::workgroup,
                        loom::atomic::ordering::acq_rel>();
}

template <unsigned SubgroupIndex>
static LOOM_FORCE_INLINE void stage_iq4xs_input_subgroup(unsigned tile,
                                                         unsigned workitem,
                                                         const BFloat2* input,
                                                         BFloat2* scratch) {
  auto source = loom::buffer::view<5, 512>(
      reinterpret_cast<const std::bfloat16_t*>(input), {},
      loom::encoding::layout::dense<2>());
  auto source_pair =
      loom::view::subview<1, 2>(source, {tile, workitem * 2u}, {});
  auto destination = loom::buffer::view<64, 1, 2>(
      reinterpret_cast<std::bfloat16_t*>(scratch + SubgroupIndex * 64u), {},
      loom::encoding::layout::dense<3>());
  auto transfer = loom::kernel::async::gather<loom::cache::scope::device,
                                              loom::cache::temporal::regular>(
      source_pair, destination);
  auto group = loom::kernel::async::group(transfer);
  loom::kernel::async::wait<0>(group);
}

LOOM_TEMPLATE_DEF(stage_iq4xs_input)
[[loom::target(iq4xs_gfx9_4_target), loom::priority(20)]]
void stage_iq4xs_input_gfx9_4(bool use_async, unsigned tile, unsigned workitem,
                              const BFloat2* input, BFloat2* scratch)
    [[loom::where(use_async)]] {
  unsigned subgroup = loom::kernel::subgroup::id();
  if (subgroup == 0u) {
    stage_iq4xs_input_subgroup<0>(tile, workitem, input, scratch);
  } else if (subgroup == 1u) {
    stage_iq4xs_input_subgroup<1>(tile, workitem, input, scratch);
  } else if (subgroup == 2u) {
    stage_iq4xs_input_subgroup<2>(tile, workitem, input, scratch);
  } else {
    stage_iq4xs_input_subgroup<3>(tile, workitem, input, scratch);
  }
  loom::kernel::barrier<loom::memory_space::workgroup,
                        loom::atomic::scope::workgroup,
                        loom::atomic::ordering::acq_rel>();
}

static LOOM_FORCE_INLINE Codes16 iq4nl_table() {
  return {-127, -104, -83, -65, -49, -35, -22, -10,
          1,    13,   25,  38,  53,  69,  89,  113};
}

static LOOM_FORCE_INLINE Codes4 lookup(Codes16 table, Bytes4 indices) {
  return {table[indices[0]], table[indices[1]], table[indices[2]],
          table[indices[3]]};
}

static LOOM_FORCE_INLINE std::bfloat16_t group_scale(Words2 header,
                                                     unsigned group) {
  loom::assume(group < 8u);
  Half2 header_halves = __builtin_bit_cast(Half2, header[0]);
  float base = float(header_halves[0]);
  unsigned scales_high = header[0] >> 16u;
  unsigned scales_low = header[1];
  unsigned low = (scales_low >> (group * 4u)) & 15u;
  unsigned high = (scales_high >> (group * 2u)) & 3u;
  int centered = int(low | (high << 4u)) - 32;
  return std::bfloat16_t(base * float(centered));
}

static LOOM_FORCE_INLINE BFloat4 decode4(Codes16 table, Bytes4 packed,
                                         bool uses_high,
                                         std::bfloat16_t scale) {
  Bytes4 indices = (packed >> (4u * unsigned(uses_high))) & 15u;
  Codes4 codes = lookup(table, indices);
  Float4 values = __builtin_convertvector(codes, Float4);
  Float4 scaled = values * float(scale);
  return __builtin_convertvector(scaled, BFloat4);
}

[[loom::kernel, loom::workgroup_size(256, 1, 1),
  loom::workgroup_count(400, 1, 1)]]
void qwen38_iq4xs_gate_up_swiglu(
    [[loom::noalias, loom::assume_aligned(64)]] const std::bfloat16_t* input,
    [[loom::noalias, loom::assume_aligned(64)]] const int* route_ids,
    [[loom::noalias, loom::assume_aligned(64)]] const IQ4XSBlock* gate_weight,
    [[loom::noalias, loom::assume_aligned(64)]] const IQ4XSBlock* up_weight,
    [[loom::noalias, loom::assume_aligned(64)]] float* output) {
  unsigned workgroup = loom::kernel::workgroup::id.x;
  unsigned workitem = loom::kernel::workitem::id.x;
  loom::assume(workitem < 256u);
  unsigned lane = workitem % 16u;
  unsigned channel_in_workgroup = workitem / 16u;
  unsigned route = workgroup / 40u;
  unsigned channel = (workgroup % 40u) * 16u + channel_in_workgroup;
  unsigned group = lane % 8u;
  unsigned half = lane / 8u;
  loom::assume(route < 10u && channel < 640u && group < 8u && half < 2u);

  unsigned expert = unsigned(route_ids[route]);
  loom::assume(expert < 512u);
  unsigned row = expert * 640u + channel;
  const IQ4XSBlock* gate_row = gate_weight + row * 10u;
  const IQ4XSBlock* up_row = up_weight + row * 10u;

  auto* scratch =
      loom::buffer::alloca<std::bfloat16_t, loom::memory_space::workgroup, 16>(
          512u);
  auto* scratch_pairs = reinterpret_cast<BFloat2*>(scratch);
  auto* input_pairs = reinterpret_cast<const BFloat2*>(input);
  Codes16 table = iq4nl_table();
  unsigned stage_pair = workitem;
  unsigned group_value_base = group * 32u;
  unsigned half_value_add = half * 16u;
  unsigned lane_value_base = group_value_base + half_value_add;
  bool uses_high = half != 0u;

  Float2 gate_accumulator = {};
  Float2 up_accumulator = {};
  [[loom::unroll(tile_unroll_factor), loom::schedule("recurrence")]]
  for (unsigned tile = 0; tile < 5u; ++tile) {
    stage_iq4xs_input(async_staging, tile, stage_pair, input_pairs,
                      scratch_pairs);

    [[loom::unroll(block_unroll_factor), loom::schedule("recurrence")]]
    for (unsigned block_in_tile = 0; block_in_tile < 2u; ++block_in_tile) {
      unsigned block_index = tile * 2u + block_in_tile;
      const IQ4XSBlock* gate_block = &gate_row[block_index];
      const IQ4XSBlock* up_block = &up_row[block_index];
      Words2 gate_header = *reinterpret_cast<const Words2*>(gate_block);
      Words2 up_header = *reinterpret_cast<const Words2*>(up_block);
      std::bfloat16_t gate_scale = group_scale(gate_header, group);
      std::bfloat16_t up_scale = group_scale(up_header, group);
      const auto* gate_packets =
          reinterpret_cast<const Bytes4*>(gate_block->quants);
      const auto* up_packets =
          reinterpret_cast<const Bytes4*>(up_block->quants);
      unsigned group_packet_base = group * 4u;
      unsigned scratch_block_base = block_in_tile * 256u;

      [[loom::unroll(packet_unroll_factor), loom::schedule("recurrence")]]
      for (unsigned packet = 0; packet < 4u; ++packet) {
        Bytes4 gate_packed = gate_packets[group_packet_base + packet];
        Bytes4 up_packed = up_packets[group_packet_base + packet];
        BFloat4 gate_values =
            decode4(table, gate_packed, uses_high, gate_scale);
        BFloat4 up_values = decode4(table, up_packed, uses_high, up_scale);
        unsigned scratch_index =
            scratch_block_base + lane_value_base + packet * 4u;
        BFloat4 input_values =
            *reinterpret_cast<const BFloat4*>(&scratch[scratch_index]);
        gate_accumulator =
            loom::vector::dot2f(gate_values, input_values, gate_accumulator);
        up_accumulator =
            loom::vector::dot2f(up_values, input_values, up_accumulator);
      }
    }
    loom::kernel::barrier<loom::memory_space::workgroup,
                          loom::atomic::scope::workgroup,
                          loom::atomic::ordering::acq_rel>();
  }

  float gate_lane = loom::vector::reduce::addf(gate_accumulator, 0.0f);
  float up_lane = loom::vector::reduce::addf(up_accumulator, 0.0f);
  float gate = loom::kernel::subgroup::reduce::addf<16>(gate_lane);
  float up = loom::kernel::subgroup::reduce::addf<16>(up_lane);
  if (lane == 0u) {
    output[route * 640u + channel] = loom::scalar::siluf(gate) * up;
  }
}
