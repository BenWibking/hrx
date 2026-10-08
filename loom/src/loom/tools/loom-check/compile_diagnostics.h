// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Adapts public LoomC diagnostics to loom-check annotation and JSON results.

#ifndef LOOM_TOOLS_LOOM_CHECK_COMPILE_DIAGNOSTICS_H_
#define LOOM_TOOLS_LOOM_CHECK_COMPILE_DIAGNOSTICS_H_

#include "loom/tooling/io/source_path.h"
#include "loom/tools/loom-check/diagnostics.h"
#include "loomc/result.h"

#ifdef __cplusplus
extern "C" {
#endif

// Copies every diagnostic in |source_result| into |collector|. Source
// identifiers are normalized before annotation matching and JSON reporting.
iree_status_t loom_check_compile_append_result_diagnostics(
    loom_check_diagnostic_collector_t* collector,
    const loomc_result_t* source_result,
    const loom_tooling_source_path_options_t* source_path_options);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLS_LOOM_CHECK_COMPILE_DIAGNOSTICS_H_
