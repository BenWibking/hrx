// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOMCXX_BUFFER_H_
#define LOOMCXX_BUFFER_H_

namespace loom {

// Storage domain attached to an allocated or externally supplied buffer root.
// Target lowering decides which domains are legal for each program kind.
enum class memory_space {
  // Device-visible global storage.
  global = 1,
  // Storage shared by invocations within a workgroup.
  workgroup = 2,
  // Storage private to one function or kernel invocation.
  private_ = 3,
  // Immutable target storage.
  constant = 4,
  // Host-visible storage.
  host = 5,
  // Descriptor storage interpreted by a target interface.
  descriptor = 6,
  // Storage whose concrete domain is selected during lowering.
  generic = 7,
};

namespace buffer {

// Allocates element_count uninitialized T objects in Space. Each execution
// produces a distinct buffer root. Alignment is a minimum byte alignment and
// cannot weaken T's natural alignment.
template <class T, loom::memory_space Space,
          __SIZE_TYPE__ Alignment = alignof(T)>
[[loom::op("buffer.alloca")]] T* alloca(__SIZE_TYPE__ element_count);

}  // namespace buffer
}  // namespace loom

#endif  // LOOMCXX_BUFFER_H_
