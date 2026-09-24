// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Host reference attention oracles for check.oracle.call.

#ifndef LOOM_TOOLING_TESTBENCH_REFERENCE_ATTENTION_H_
#define LOOM_TOOLING_TESTBENCH_REFERENCE_ATTENTION_H_

#include "loom/tooling/testbench/reference.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the reference.mxfp8_paged_attention oracle provider.
//
// The provider computes scaled dot-product decode attention over the DeepSeek
// V4.1 MXFP8 cache layout. Inputs are lengths [I]xi32, query [I, 512]xf32,
// page table [I, P]xi32, and separate K/V caches [C, 8448]xi8. Each physical
// cache page stores sixteen 512-byte E4M3FN payload rows followed by sixteen
// 16-byte E8M0 scale rows; each scale byte owns 32 payload values. Negative
// page IDs denote absent logical pages. Results are online state [I, 2]xf32
// containing maximum and denominator, and normalized output [I, 512]xf32.
// Reference accumulation and exponential evaluation use host f64 and decode
// raw FP8/E8M0 bits independently of target lowering.
//
// |options| is borrowed and must outlive invocations using |out_provider|.
void loom_testbench_reference_mxfp8_paged_attention_oracle_provider_initialize(
    const loom_testbench_reference_oracle_options_t* options,
    loom_testbench_oracle_provider_t* out_provider);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLING_TESTBENCH_REFERENCE_ATTENTION_H_
