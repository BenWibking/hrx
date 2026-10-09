// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/emit/wasm/lower/vector_carrier.h"

#include "loom/ir/scalar_type.h"

uint16_t loom_wasm_scalar_type_physical_bit_count(
    loom_scalar_type_t element_type) {
  switch (element_type) {
    case LOOM_SCALAR_TYPE_INDEX:
    case LOOM_SCALAR_TYPE_OFFSET:
      return LOOM_WASM_ADDRESS_CARRIER_BIT_COUNT;
    case LOOM_SCALAR_TYPE_I1:
      return LOOM_WASM_PREDICATE_CARRIER_BIT_COUNT;
    default: {
      const int32_t bit_count = loom_scalar_type_bitwidth(element_type);
      return bit_count > 0 ? (uint16_t)bit_count : 0;
    }
  }
}

loom_wasm_vector_carrier_t loom_wasm_vector_carrier_for_type(loom_type_t type) {
  uint64_t element_count = 0;
  if (!loom_type_is_vector(type) || !loom_type_is_all_static(type) ||
      !loom_type_static_element_count(type, &element_count) ||
      element_count == 0) {
    return (loom_wasm_vector_carrier_t){0};
  }

  const uint16_t element_bit_count =
      loom_wasm_scalar_type_physical_bit_count(loom_type_element_type(type));
  if (element_bit_count == 0 || (element_bit_count & 7u) != 0 ||
      element_count > UINT16_MAX * 8u / element_bit_count) {
    return (loom_wasm_vector_carrier_t){0};
  }
  const uint32_t payload_byte_count =
      (uint32_t)(element_count * element_bit_count / 8u);
  const uint32_t packet_count =
      (payload_byte_count + LOOM_WASM_VECTOR_CARRIER_PACKET_BYTE_COUNT - 1u) /
      LOOM_WASM_VECTOR_CARRIER_PACKET_BYTE_COUNT;
  if (packet_count == 0 || packet_count > UINT16_MAX) {
    return (loom_wasm_vector_carrier_t){0};
  }
  return (loom_wasm_vector_carrier_t){
      .payload_byte_count = (uint16_t)payload_byte_count,
      .packet_count = (uint16_t)packet_count,
      .element_bit_count = (uint8_t)element_bit_count,
  };
}

bool loom_wasm_vector_type_is_callable(loom_type_t type) {
  if (!loom_type_is_vector(type) || loom_type_rank(type) != 1 ||
      !loom_type_is_all_static(type)) {
    return false;
  }
  const int64_t lane_count = loom_type_dim_static_size_at(type, 0);
  switch (loom_type_element_type(type)) {
    case LOOM_SCALAR_TYPE_I8:
    case LOOM_SCALAR_TYPE_F8E4M3:
    case LOOM_SCALAR_TYPE_F8E5M2:
      return lane_count == 16;
    case LOOM_SCALAR_TYPE_I16:
    case LOOM_SCALAR_TYPE_F16:
    case LOOM_SCALAR_TYPE_BF16:
      return lane_count == 8;
    case LOOM_SCALAR_TYPE_I1:
    case LOOM_SCALAR_TYPE_I32:
    case LOOM_SCALAR_TYPE_F32:
      return lane_count == 4;
    case LOOM_SCALAR_TYPE_I64:
    case LOOM_SCALAR_TYPE_F64:
      return lane_count == 2;
    default:
      return false;
  }
}
