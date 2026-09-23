// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/pass_environment.h"

static loom_pass_environment_t
loom_codegen_pass_environment_storage_initialize_with_target(
    const loom_codegen_pass_environment_options_t* options,
    loom_target_pass_capability_t target_capability,
    loom_codegen_pass_environment_storage_t* out_storage) {
  out_storage->target_capability = target_capability;
  out_storage->low_capability = loom_low_pass_capability_make(
      options->descriptor_registry, options->lower_policy_registry,
      options->legality_provider_list, options->legalizer_registry,
      options->compile_report);
  out_storage->math_capability = loom_target_math_pass_capability_make(
      options->math_policy_registry, options->compile_report);
  out_storage->cleanup_capability =
      loom_cleanup_pass_capability_make(options->cleanup_pattern_registry);
  out_storage->capabilities[0] = &out_storage->target_capability.base;
  out_storage->capabilities[1] = &out_storage->low_capability.base;
  out_storage->capabilities[2] = &out_storage->math_capability.base;
  out_storage->capabilities[3] = &out_storage->cleanup_capability.base;
  out_storage->environment = loom_pass_environment_make(
      out_storage->capabilities, IREE_ARRAYSIZE(out_storage->capabilities));
  return out_storage->environment;
}

loom_pass_environment_t loom_codegen_pass_environment_storage_initialize(
    const loom_codegen_pass_environment_options_t* options,
    const loom_function_version_list_t* function_versions,
    loom_codegen_pass_environment_storage_t* out_storage) {
  return loom_codegen_pass_environment_storage_initialize_with_target(
      options,
      loom_target_pass_capability_make(options->target_environment,
                                       function_versions),
      out_storage);
}

loom_pass_environment_t
loom_codegen_pass_environment_storage_initialize_mutable(
    const loom_codegen_pass_environment_options_t* options,
    loom_function_version_owner_t* function_version_owner,
    loom_codegen_pass_environment_storage_t* out_storage) {
  return loom_codegen_pass_environment_storage_initialize_with_target(
      options,
      loom_target_pass_capability_make_mutable(options->target_environment,
                                               function_version_owner),
      out_storage);
}
