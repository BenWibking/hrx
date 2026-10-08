// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOMC_INTEROP_H_
#define LOOMC_INTEROP_H_

#include "loom/error/source.h"
#include "loom/ir/module.h"
#include "loom/target/provider.h"
#include "loomc/module.h"
#include "loomc/result.h"
#include "loomc/target.h"

/// @file
/// Exact-version interop between public LoomC handles and native Loom APIs.
///
/// This optional adapter exposes compiler-native types and is intentionally not
/// part of `loomc/loomc.h` or the stable LoomC ABI. The application, LoomC, and
/// native Loom consumers must come from the same source revision and build
/// configuration. Mixing versions or configurations is undefined behavior.

#ifdef __cplusplus
extern "C" {
#endif

/// Read-only native projection of a structurally verified LoomC module.
///
/// @lifetime
/// Both pointers borrow from the public module that produced the view and
/// remain valid until that module is mutated or released. Native consumers may
/// inspect but must not mutate the projected module or source table.
typedef struct loomc_module_interop_view_t {
  /// Native module owned by the public module handle.
  const loom_module_t* module;

  /// Exact source snapshots indexed by native module source ID.
  const loom_source_table_resolver_t* source_table;
} loomc_module_interop_view_t;

/// Mutable native projection of a LoomC module for exact-version producers.
///
/// Requesting this view invalidates cached verification and compiler products
/// before exposing the native module. The caller may mutate the module using
/// native Loom APIs and must leave it structurally valid before the next public
/// LoomC operation. That operation re-establishes the required invariants.
///
/// The source table is read-only: native mutations may reuse existing source
/// IDs but must not invent source snapshots or retain either pointer beyond the
/// public module lifetime.
typedef struct loomc_module_mutable_interop_view_t {
  /// Mutable native module owned by the public module handle.
  loom_module_t* module;

  /// Exact source snapshots indexed by native module source ID.
  const loom_source_table_resolver_t* source_table;
} loomc_module_mutable_interop_view_t;

/// Structurally verifies a public module and projects its native read-only
/// view.
///
/// This establishes only target-independent Loom IR invariants. Target-Low
/// legality remains owned by the compile or emit operation using the selected
/// target environment.
///
/// @param module Module to verify and inspect.
/// @param allocator Host allocator used for the returned result.
/// @param out_view Receives a borrowed view when the result succeeds. Receives
/// a zeroed view when verification fails.
/// @param out_result Receives a retained verification result.
/// @return OK when verification ran to a result. Non-OK statuses represent API
/// misuse or infrastructure failures before a result could be produced.
///
/// @ownership
/// The caller retains `module` and owns `out_result` on an OK return, releasing
/// it with `loomc_result_release`.
///
/// @thread_safety
/// This operation may establish cached verification state on `module` and
/// requires exclusive access. The returned view may subsequently be read
/// concurrently while no operation mutates the module.
LOOMC_API_EXPORT loomc_status_t loomc_module_get_interop_view(
    loomc_module_t* module, loomc_allocator_t allocator,
    loomc_module_interop_view_t* out_view, loomc_result_t** out_result);

/// Projects a mutable native view after invalidating derived module state.
///
/// This is an exact-version escape hatch for producers that construct or adapt
/// Loom IR with native builder APIs before returning to public LoomC
/// compilation. It performs no verification and returns a zero view when
/// `module` is NULL or does not contain native IR.
///
/// @param module Module whose native IR will be mutated.
/// @return Borrowed mutable module and read-only source-table pointers.
///
/// @lifetime
/// The returned pointers remain valid until `module` is replaced, mutated by a
/// public operation, or released. Concurrent access to `module` is invalid.
LOOMC_API_EXPORT loomc_module_mutable_interop_view_t
loomc_module_get_mutable_interop_view(loomc_module_t* module);

/// Returns the immutable native environment owned by a public target handle.
///
/// @param target_environment Target environment to inspect. May be `NULL`.
/// @return Borrowed native environment, or `NULL` when the input is `NULL`.
///
/// @lifetime
/// The returned pointer remains valid until `target_environment` is released.
LOOMC_API_EXPORT const loom_target_environment_t*
loomc_target_environment_get_interop_view(
    const loomc_target_environment_t* target_environment);

/// Returns the immutable native profile owned by a public target handle.
///
/// @param target_profile Target profile to inspect. May be `NULL`.
/// @return Borrowed native profile, or `NULL` when the input is `NULL`.
///
/// @lifetime
/// The returned pointer remains valid until `target_profile` is released.
LOOMC_API_EXPORT const loom_target_profile_t*
loomc_target_profile_get_interop_view(
    const loomc_target_profile_t* target_profile);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOMC_INTEROP_H_
