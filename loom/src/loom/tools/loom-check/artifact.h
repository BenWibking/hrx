// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Target artifact adaptation shared by loom-check emit providers.

#ifndef LOOM_TOOLS_LOOM_CHECK_ARTIFACT_H_
#define LOOM_TOOLS_LOOM_CHECK_ARTIFACT_H_

#include "iree/base/api.h"
#include "loom/target/provider.h"
#include "loom/tools/loom-check/execute.h"

#ifdef __cplusplus
extern "C" {
#endif

// Invokes the target emitter named by |public_artifact_format| with the module
// and checker-owned resources in |request|. Target diagnostics are captured by
// the active check case. |function_versions| may be NULL for already-prepared
// target-low modules that do not carry invocation-refined target facts.
iree_status_t loom_check_emit_target_artifact(
    const loom_check_emit_provider_request_t* request,
    iree_string_view_t public_artifact_format,
    const loom_function_version_list_t* function_versions, bool* out_emitted,
    loom_target_emit_artifact_t* out_artifact);

// Borrows contiguous primary artifact contents when possible and otherwise
// clones them into |allocator|. The caller frees |out_owned_contents| and must
// keep |artifact| alive while using borrowed |out_contents|.
iree_status_t loom_check_target_artifact_borrow_or_clone_contents(
    const loom_target_emit_artifact_t* artifact, iree_allocator_t allocator,
    iree_const_byte_span_t* out_contents, iree_byte_span_t* out_owned_contents);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLS_LOOM_CHECK_ARTIFACT_H_
