// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amdgpu/runtime_requirements.h"

#include "loom/ir/module.h"
#include "loom/target/arch/amdgpu/abi/asan.h"
#include "loom/target/arch/amdgpu/abi/feedback.h"
#include "loom/target/arch/amdgpu/abi/tsan.h"

static bool loom_amdgpu_runtime_requirements_has_symbol(
    const loom_module_t* module, iree_string_view_t name) {
  const loom_string_id_t name_id = loom_module_lookup_string(module, name);
  if (name_id == LOOM_STRING_ID_INVALID) {
    return false;
  }
  return loom_module_find_symbol(module, name_id) != LOOM_SYMBOL_ID_INVALID;
}

loom_amdgpu_runtime_requirements_t
loom_amdgpu_runtime_requirements_from_target_low_module(
    const loom_module_t* module) {
  loom_amdgpu_runtime_requirements_t requirements =
      LOOM_AMDGPU_RUNTIME_REQUIREMENT_NONE;
  if (loom_amdgpu_runtime_requirements_has_symbol(
          module, IREE_SV(LOOM_AMDGPU_FEEDBACK_CONFIG_GLOBAL_NAME))) {
    requirements |= LOOM_AMDGPU_RUNTIME_REQUIREMENT_FEEDBACK;
  }
  if (loom_amdgpu_runtime_requirements_has_symbol(
          module, IREE_SV(LOOM_AMDGPU_ASAN_CONFIG_GLOBAL_NAME))) {
    requirements |= LOOM_AMDGPU_RUNTIME_REQUIREMENT_ASAN_SHADOW;
  }
  if (loom_amdgpu_runtime_requirements_has_symbol(
          module, IREE_SV(LOOM_AMDGPU_TSAN_CONFIG_GLOBAL_NAME))) {
    requirements |= LOOM_AMDGPU_RUNTIME_REQUIREMENT_TSAN_SHADOW;
  }
  return requirements;
}
