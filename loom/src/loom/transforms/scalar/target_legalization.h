// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Shared scalar target rewrites and generic reference legalizers.

#ifndef LOOM_TRANSFORMS_SCALAR_TARGET_LEGALIZATION_H_
#define LOOM_TRANSFORMS_SCALAR_TARGET_LEGALIZATION_H_

#include "loom/target/legalization.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fuses an unflagged scalar.muli owned solely by an unflagged scalar.addi into
// scalar.fmai. The caller establishes that its target can retain the fused type
// and operand domain before invoking the rewrite.
iree_status_t loom_scalar_fuse_multiply_add_rewrite_op(
    loom_rewriter_t* rewriter, loom_op_t* op, bool* out_rewritten);

// Returns the generic scalar legalizer provider. Pipelines should compose this
// after target-specific providers so native target rewrites win before scalar
// reference decomposition.
const loom_target_legalizer_provider_t* loom_scalar_target_legalizer_provider(
    void);

#ifdef __cplusplus
}
#endif

#endif  // LOOM_TRANSFORMS_SCALAR_TARGET_LEGALIZATION_H_
