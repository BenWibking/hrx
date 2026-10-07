// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_BINDING_C_TARGET_SPIRV_PROFILE_MATCH_H_
#define LOOM_BINDING_C_TARGET_SPIRV_PROFILE_MATCH_H_

#include "loomc/target.h"
#include "visibility.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns whether |profile| belongs to the SPIR-V family and, when it does,
// whether it has the same compilation contract as the named |selector|.
// Dynamic device facts that do not change compilation semantics do not affect
// the selector match. |out_matches| may be NULL when only family routing is
// required.
LOOMC_API_PRIVATE loomc_status_t loomc_spirv_target_profile_match_selector(
    const loomc_target_profile_t* profile, loomc_string_view_t selector,
    bool* out_is_spirv, bool* out_matches);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_BINDING_C_TARGET_SPIRV_PROFILE_MATCH_H_
