// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/x86/lower/kernel.h"

#include "loom/ir/module.h"
#include "loom/ops/kernel/ops.h"
#include "loom/ops/low/ops.h"
#include "loom/target/arch/x86/hal_abi.h"
#include "loom/target/arch/x86/register_classes.h"

static loom_low_lower_plan_id_t loom_x86_kernel_builtin_id(
    const loom_op_t* op) {
  if (loom_kernel_workgroup_id_isa(op)) {
    return LOOM_X86_HAL_BUILTIN_WORKGROUP_ID_X +
           loom_kernel_workgroup_id_dimension(op);
  }
  if (loom_kernel_workgroup_count_isa(op)) {
    return LOOM_X86_HAL_BUILTIN_WORKGROUP_COUNT_X +
           loom_kernel_workgroup_count_dimension(op);
  }
  return LOOM_LOW_LOWER_PLAN_ID_NONE;
}

iree_status_t loom_x86_select_kernel_builtin(void* user_data,
                                             loom_low_lower_context_t* context,
                                             const loom_op_t* source_op,
                                             loom_low_lower_plan_t* out_plan) {
  (void)user_data;
  *out_plan = loom_low_lower_plan_empty();
  if (loom_low_lower_context_bundle(context)->export_plan->abi_kind ==
      LOOM_TARGET_ABI_HAL_KERNEL) {
    *out_plan =
        loom_low_lower_plan_make(loom_x86_kernel_builtin_id(source_op), NULL);
  }
  return iree_ok_status();
}

iree_status_t loom_x86_emit_kernel_builtin(void* user_data,
                                           loom_low_lower_context_t* context,
                                           const loom_op_t* source_op,
                                           loom_low_lower_plan_t plan) {
  (void)user_data;
  loom_module_t* module = loom_low_lower_context_module(context);
  loom_string_id_t name;
  IREE_RETURN_IF_ERROR(loom_module_intern_string(
      module, loom_x86_hal_builtins[plan.id].name, &name));
  loom_type_t type;
  IREE_RETURN_IF_ERROR(loom_low_lower_make_register_type(
      context, LOOM_X86_REGISTER_CLASS_GPR64, 1, &type));
  loom_op_t* live_in = NULL;
  IREE_RETURN_IF_ERROR(loom_low_live_in_build(
      loom_low_lower_context_builder(context), 0, name,
      loom_named_attr_slice_empty(), type, source_op->location, &live_in));
  return loom_low_lower_bind_value(context, loom_op_const_results(source_op)[0],
                                   loom_low_live_in_result(live_in));
}

iree_status_t loom_x86_query_kernel_builtin(
    void* user_data,
    const loom_target_contract_query_environment_t* environment,
    const loom_op_t* source_op,
    loom_target_contract_query_result_t* out_result) {
  (void)user_data;
  *out_result = loom_target_contract_query_result_empty();
  if (loom_target_contract_query_environment_bundle(environment)
              ->export_plan->abi_kind == LOOM_TARGET_ABI_HAL_KERNEL &&
      loom_x86_kernel_builtin_id(source_op) != LOOM_LOW_LOWER_PLAN_ID_NONE) {
    out_result->outcome = LOOM_TARGET_CONTRACT_QUERY_LEGAL;
  }
  return iree_ok_status();
}
