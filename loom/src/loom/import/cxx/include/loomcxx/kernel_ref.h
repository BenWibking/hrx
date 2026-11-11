// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOMCXX_KERNEL_REF_H_
#define LOOMCXX_KERNEL_REF_H_

// Host launch descriptors for single-source C++.
//
// A translation unit compiles twice: once by a host C++ compiler and once by
// Loom's C++ importer with --root=@launched. Host code names a kernel K by
// odr-using loom::kernel::kernel_ref<&K>. The host compile only references
// that variable template specialization's (Itanium-mangled) symbol. The
// importer treats the same odr-use as a launch root: it exports K and, with
// --kernel-descriptors=<file>, writes assembly that defines the symbol as a
// descriptor of K's export name and kernel argument layout. Linking that
// object with the host object resolves the reference, so the host can pack
// arguments and dispatch K without naming conventions or side tables.
//
// This header has no other dependencies so that host code can include it.

namespace loom::kernel {

// One kernel ABI argument, copied from a host parameter object.
struct descriptor_leaf {
  // Kernel parameter whose object holds the leaf.
  unsigned int parameter;
  // descriptor_leaf_scalar: by-value bytes. descriptor_leaf_buffer: a device
  // pointer, which Loom passes as a buffer binding.
  unsigned int kind;
  // Byte offset of the leaf in the host parameter object.
  unsigned int source_offset;
  // Byte offset of the argument in the explicit kernel argument segment.
  unsigned int kernarg_offset;
  // Byte size of the leaf and of its argument.
  unsigned int size;
};

inline constexpr unsigned int descriptor_leaf_scalar = 0;
inline constexpr unsigned int descriptor_leaf_buffer = 1;

inline constexpr unsigned int descriptor_version = 1;
// The kernel was imported. Without it the importer reached the kernel_ref but
// dropped or excluded the kernel, and the descriptor names no device code.
inline constexpr unsigned int descriptor_flag_available = 1u << 0;

// Launch facts of one kernel. The importer defines one per reached kernel_ref
// specialization as a weak COMDAT object, so translation units may share one.
struct descriptor {
  // descriptor_version.
  unsigned int version;
  // descriptor_flag_* bits.
  unsigned int flags;
  // NUL-terminated export name of the kernel in the compiled executable.
  const char* name;
  // Workgroup size fixed by a loom::workgroup_size annotation, or zeros when
  // the kernel's launch configuration computes it at dispatch.
  unsigned int workgroup_size[3];
  // Byte size of the explicit kernel argument segment, a multiple of 8.
  unsigned int kernarg_size;
  // Number of kernel parameters.
  unsigned int parameter_count;
  // Number of entries in |leaves|, in kernel argument order.
  unsigned int leaf_count;
  const descriptor_leaf* leaves;
};

// The descriptor of kernel K. Odr-use it, for example by taking its address,
// from host code that launches K. It has no definition in C++; the importer's
// --kernel-descriptors output defines each specialization.
template <auto Kernel>
[[loom::kernel_ref]] extern const descriptor kernel_ref;

}  // namespace loom::kernel

#endif  // LOOMCXX_KERNEL_REF_H_
