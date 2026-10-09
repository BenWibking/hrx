// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Wasm SIMD128 carriers for internal source vectors.

#ifndef LOOM_TARGET_EMIT_WASM_LOWER_VECTOR_CARRIER_H_
#define LOOM_TARGET_EMIT_WASM_LOWER_VECTOR_CARRIER_H_

#include <stdint.h>

#include "loom/ir/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  // Physical bytes in one Wasm v128 register.
  LOOM_WASM_VECTOR_CARRIER_PACKET_BYTE_COUNT = 16,
  // Physical Wasm representation of source index and offset lanes.
  LOOM_WASM_ADDRESS_CARRIER_BIT_COUNT = 32,
  // Physical Wasm representation of source predicate lanes.
  LOOM_WASM_PREDICATE_CARRIER_BIT_COUNT = 32,
};

typedef struct loom_wasm_vector_carrier_t {
  // Meaningful physical bytes in the source vector.
  uint16_t payload_byte_count;
  // Number of v128 register units occupied by the carrier.
  uint16_t packet_count;
  // Physical width of each source element in bits.
  uint8_t element_bit_count;
} loom_wasm_vector_carrier_t;

// Returns the physical Wasm lane width for |element_type|. Predicates and
// address domains use their target representation instead of their abstract
// source width.
uint16_t loom_wasm_scalar_type_physical_bit_count(
    loom_scalar_type_t element_type);

// Returns the complete internal Wasm carrier mapping for |type|. Unsupported,
// dynamic, empty, and over-bound vectors return a zero carrier.
loom_wasm_vector_carrier_t loom_wasm_vector_carrier_for_type(loom_type_t type);

// Returns true when |type| is one exact v128 vector admitted at a Wasm
// function boundary. Internal tuple carriers do not widen the callable ABI.
bool loom_wasm_vector_type_is_callable(loom_type_t type);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_EMIT_WASM_LOWER_VECTOR_CARRIER_H_
