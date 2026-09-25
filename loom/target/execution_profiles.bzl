# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Native hardware policies for shared GPU source tests."""

load(
    "//loom/src/loom/tooling/target/spirv:execution_profiles.bzl",
    "SPIRV_VULKAN_HARDWARE_PROFILE",
    "SPIRV_VULKAN_HARDWARE_VM_ORACLE_PROFILE",
)
load(
    "//loom/target/amdgpu:execution_profiles.bzl",
    "AMDGPU_HARDWARE_PROFILE",
    "AMDGPU_HARDWARE_VM_ORACLE_PROFILE",
)

# Each child executes on a compatible local device; offline compiler profiles
# separately qualify code generation for explicit architecture selectors.
GPU_HARDWARE_PROFILES = [
    AMDGPU_HARDWARE_PROFILE,
    SPIRV_VULKAN_HARDWARE_PROFILE,
]

# Hardware target execution with the in-process VM reference oracle linked into
# the runner. These profiles keep differential suites incompatible when their
# requested oracle is absent instead of failing after test execution begins.
GPU_HARDWARE_VM_ORACLE_PROFILES = [
    AMDGPU_HARDWARE_VM_ORACLE_PROFILE,
    SPIRV_VULKAN_HARDWARE_VM_ORACLE_PROFILE,
]
