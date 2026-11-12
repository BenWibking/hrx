// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_IMPORT_CXX_SYMBOL_KERNEL_LAUNCH_H_
#define LOOM_IMPORT_CXX_SYMBOL_KERNEL_LAUNCH_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace loom::cxx_import {

// Host launch facts of one loom::kernel::kernel_ref specialization, as the
// importer knows them: the host object layout of the kernel's arguments and
// how they flatten into the imported kernel's argument list. The native
// argument layout (kernel argument offsets and the argument segment size) is
// not here: the target lowering that compiles the kernel decides it, and the
// compiled artifact reports it (see loomcxx/kernel_ref.h).

// One kernel argument copied from a host parameter object.
struct KernelLaunchLeaf {
  // Kernel parameter whose host object holds the leaf.
  uint32_t parameter;
  // loom::kernel::descriptor_leaf_scalar (0): by-value bytes, or
  // descriptor_leaf_buffer (1): a device pointer passed as a buffer binding.
  uint32_t kind;
  // Byte offset of the leaf in the host parameter object.
  uint64_t source_offset;
  // Byte size of the leaf in the host parameter object.
  uint64_t size;
};

struct KernelLaunch {
  // Itanium symbol of the kernel_ref specialization the host references.
  std::string symbol;
  // Whether the kernel was imported. A dropped or excluded kernel has no
  // device code and no leaves.
  bool available = false;
  // Export name of the kernel in the compiled executable.
  std::string export_name;
  // Exact workgroup size annotation, or zeros when the launch configuration
  // computes it.
  std::array<uint32_t, 3> workgroup_size = {0, 0, 0};
  // Number of C++ kernel parameters.
  uint32_t parameter_count = 0;
  // One leaf per argument of the imported kernel, in argument order: leaf i
  // is the kernel's argument i.
  std::vector<KernelLaunchLeaf> leaves;
};

}  // namespace loom::cxx_import

#endif  // LOOM_IMPORT_CXX_SYMBOL_KERNEL_LAUNCH_H_
