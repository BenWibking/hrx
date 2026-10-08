// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Command-line option adapters for LoomC compiler invocations.

#ifndef LOOM_TOOLING_CLI_LOOMC_OPTIONS_H_
#define LOOM_TOOLING_CLI_LOOMC_OPTIONS_H_

#include "iree/base/api.h"
#include "loom/sanitizer/options.h"
#include "loom/tooling/config/config.h"
#include "loomc/config.h"
#include "loomc/sanitizer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Adapts an owned command-line config set to borrowed LoomC options. The
// returned binding array is owned by |allocator| and must be freed by the
// caller. Binding strings continue to borrow from |config_set|.
iree_status_t loom_tooling_cli_make_loomc_config_options(
    const loom_tooling_config_set_t* config_set, iree_allocator_t allocator,
    loomc_config_binding_t** out_bindings, loomc_config_options_t* out_options);

// Adapts parsed command-line sanitizer options to the LoomC descriptor used by
// compiler operations. The returned descriptor borrows no storage.
void loom_tooling_cli_make_loomc_sanitizer_options(
    const loom_sanitizer_options_t* options,
    loomc_sanitizer_options_t* out_options);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TOOLING_CLI_LOOMC_OPTIONS_H_
