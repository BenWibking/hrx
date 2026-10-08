// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loomc/interop.h"

#include "context.h"
#include "module.h"
#include "result.h"
#include "target.h"

loomc_status_t loomc_module_get_interop_view(
    loomc_module_t* module, loomc_allocator_t allocator,
    loomc_module_interop_view_t* out_view, loomc_result_t** out_result) {
  if (out_view != NULL) {
    *out_view = (loomc_module_interop_view_t){0};
  }
  if (out_result != NULL) {
    *out_result = NULL;
  }
  if (module == NULL || out_view == NULL || out_result == NULL) {
    return loomc_make_status(
        LOOMC_STATUS_INVALID_ARGUMENT,
        "module, out_view, and out_result must not be NULL");
  }
  if (loomc_module_const_loom_module(module) == NULL) {
    return loomc_make_status(LOOMC_STATUS_FAILED_PRECONDITION,
                             "module does not contain internal IR");
  }

  loomc_result_t* result = NULL;
  LOOMC_RETURN_IF_ERROR(loomc_result_create(
      LOOMC_RESULT_STATE_SUCCEEDED,
      loomc_context_source_retention(loomc_module_context(module)), allocator,
      &result));
  loomc_status_t status = loomc_module_verify_structural(module, result);
  if (loomc_status_is_ok(status)) {
    if (loomc_result_succeeded(result)) {
      *out_view = (loomc_module_interop_view_t){
          .module = loomc_module_const_loom_module(module),
          .source_table = loomc_module_source_table(module),
      };
    }
    *out_result = result;
  } else {
    loomc_result_release(result);
  }
  return status;
}

const loom_target_environment_t* loomc_target_environment_get_interop_view(
    const loomc_target_environment_t* target_environment) {
  return loomc_target_environment_loom_target_environment(target_environment);
}
