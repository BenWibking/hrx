// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Source kernel queries imported from the x86 invocation ABI.

#ifndef LOOM_TARGET_ARCH_X86_LOWER_KERNEL_H_
#define LOOM_TARGET_ARCH_X86_LOWER_KERNEL_H_

#include "loom/codegen/low/lower/lower.h"

#ifdef __cplusplus
extern "C" {
#endif

iree_status_t loom_x86_select_kernel_builtin(void* user_data,
                                             loom_low_lower_context_t* context,
                                             const loom_op_t* source_op,
                                             loom_low_lower_plan_t* out_plan);

// Imports invocation-constant queries before any ordinary Low instructions.
iree_status_t loom_x86_emit_kernel_preamble(void* user_data,
                                            loom_low_lower_context_t* context);

iree_status_t loom_x86_emit_kernel_builtin(void* user_data,
                                           loom_low_lower_context_t* context,
                                           const loom_op_t* source_op,
                                           loom_low_lower_plan_t plan);

iree_status_t loom_x86_query_kernel_builtin(
    void* user_data,
    const loom_target_contract_query_environment_t* environment,
    const loom_op_t* source_op,
    loom_target_contract_query_result_t* out_result);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_ARCH_X86_LOWER_KERNEL_H_
