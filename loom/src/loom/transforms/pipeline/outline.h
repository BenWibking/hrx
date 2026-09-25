// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_TRANSFORMS_PIPELINE_OUTLINE_H_
#define LOOM_TRANSFORMS_PIPELINE_OUTLINE_H_

#include "loom/pass/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Factors lexical strand bodies into ordinary calls with explicit, typed
// captures. Worker geometry and target selection stay on the strand. The
// helpers are transparent to execution-context requirements until a worker
// binding consumes the call; this pass runs before that binding, not before
// ordinary source inlining that would expand the helpers again.
const loom_pass_info_t* loom_pipeline_outline_pass_info(void);
iree_status_t loom_pipeline_outline_run(loom_pass_t* pass,
                                        loom_module_t* module);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TRANSFORMS_PIPELINE_OUTLINE_H_
