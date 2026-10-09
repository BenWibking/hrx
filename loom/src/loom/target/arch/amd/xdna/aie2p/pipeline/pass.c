// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amd/xdna/aie2p/pipeline/pass.h"

#include "loom/ops/pipeline/ops.h"
#include "loom/target/arch/amd/xdna/aie2p/pipeline/native.h"

static const loom_pass_info_t loom_aie2p_pipeline_lower_pass_info_storage = {
    .name = IREE_SVL("aie2p-lower-pipeline"),
    .description =
        IREE_SVL("Lower resident pipelines to AIE2P array programs."),
    .kind = LOOM_PASS_FUNCTION,
};

const loom_pass_info_t* loom_aie2p_pipeline_lower_pass_info(void) {
  return &loom_aie2p_pipeline_lower_pass_info_storage;
}

iree_status_t loom_aie2p_pipeline_lower_run(loom_pass_t* pass,
                                            loom_module_t* module,
                                            loom_func_like_t function) {
  if (!loom_pipeline_def_isa(function.op)) {
    return iree_ok_status();
  }

  return loom_aie2p_pipeline_realize(pass, module, function);
}

static const loom_pass_descriptor_t kAie2pPipelinePassDescriptors[] = {
    {
        .key = IREE_SVL("aie2p-lower-pipeline"),
        .info = loom_aie2p_pipeline_lower_pass_info,
        .function_run = loom_aie2p_pipeline_lower_run,
    },
};

const loom_pass_registry_t loom_aie2p_pipeline_pass_registry = {
    .descriptors = kAie2pPipelinePassDescriptors,
    .descriptor_count = IREE_ARRAYSIZE(kAie2pPipelinePassDescriptors),
};
