// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// AMDGPU target runtime requirements.

#ifndef LOOM_TARGET_ARCH_AMDGPU_RUNTIME_REQUIREMENTS_H_
#define LOOM_TARGET_ARCH_AMDGPU_RUNTIME_REQUIREMENTS_H_

#include "iree/base/api.h"
#include "loom/ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

enum loom_amdgpu_runtime_requirement_bit_e {
  // No AMDGPU target runtime support is required.
  LOOM_AMDGPU_RUNTIME_REQUIREMENT_NONE = 0u,
  // Requires the common kernel-to-runtime feedback channel.
  LOOM_AMDGPU_RUNTIME_REQUIREMENT_FEEDBACK = 1u << 0,
  // Requires the AMDGPU ASAN shadow configuration global.
  LOOM_AMDGPU_RUNTIME_REQUIREMENT_ASAN_SHADOW = 1u << 1,
  // Requires the AMDGPU TSAN shadow configuration global.
  LOOM_AMDGPU_RUNTIME_REQUIREMENT_TSAN_SHADOW = 1u << 2,
};

// Bitset of loom_amdgpu_runtime_requirement_bit_e values.
typedef uint32_t loom_amdgpu_runtime_requirements_t;

// Returns runtime globals reserved by prepared target-low |module|.
//
// Artifact emission consumes the lowered IR as its source of truth instead of
// reconstructing possible references from the source pipeline options. Target
// lowering reserves each ABI symbol only when it emits a corresponding
// reference.
loom_amdgpu_runtime_requirements_t
loom_amdgpu_runtime_requirements_from_target_low_module(
    const loom_module_t* module);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_ARCH_AMDGPU_RUNTIME_REQUIREMENTS_H_
