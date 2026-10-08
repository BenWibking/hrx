// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/x86/facts.h"

#include <string.h>

static bool loom_x86_cpu_data_equal(const iree_cpu_data_t* lhs,
                                    const iree_cpu_data_t* rhs) {
  return lhs->architecture == rhs->architecture &&
         memcmp(lhs->fields, rhs->fields, sizeof(lhs->fields)) == 0;
}

static bool loom_x86_target_facts_satisfy_specialization_requirement(
    const loom_target_facts_t* effective_base,
    const loom_target_facts_t* requirement_base) {
  const loom_x86_target_facts_t* effective =
      (const loom_x86_target_facts_t*)effective_base;
  const loom_x86_target_facts_t* requirement =
      (const loom_x86_target_facts_t*)requirement_base;
  if (effective->base.selector != requirement->base.selector ||
      !loom_target_snapshot_satisfies_specialization_requirement(
          &effective->base.storage.snapshot,
          &requirement->base.storage.snapshot)) {
    return false;
  }
  if (loom_target_facts_field_is_explicit(
          requirement_base, LOOM_TARGET_FACT_FIELD_CONTRACT_SET_KEY) &&
      !iree_string_view_equal(
          effective->base.storage.config.contract_set_key,
          requirement->base.storage.config.contract_set_key)) {
    return false;
  }
  if (!iree_all_bits_set(
          effective->base.storage.config.contract_feature_bits,
          requirement->base.storage.config.contract_feature_bits)) {
    return false;
  }
  return requirement->cpu_data.architecture == IREE_CPU_ARCHITECTURE_UNKNOWN ||
         loom_x86_cpu_data_equal(&effective->cpu_data, &requirement->cpu_data);
}

const loom_target_fact_type_t loom_x86_target_fact_type = {
    .name = IREE_SVL("x86"),
    .storage_size = sizeof(loom_x86_target_facts_t),
    .satisfies_identity_requirement =
        loom_target_facts_selector_satisfies_identity_requirement,
    .satisfies_specialization_requirement =
        loom_x86_target_facts_satisfy_specialization_requirement,
};
