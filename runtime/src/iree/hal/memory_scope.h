// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_MEMORY_SCOPE_H_
#define IREE_HAL_MEMORY_SCOPE_H_

#include "iree/base/api.h"
#include "iree/hal/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct iree_hal_device_group_t iree_hal_device_group_t;
typedef struct iree_hal_pool_t iree_hal_pool_t;

// Dense coordinate in one sealed group's memory namespace. Zero denotes all
// admitted sites; it does not grant access to an otherwise excluded site.
typedef uint32_t iree_hal_memory_scope_id_t;
#define IREE_HAL_MEMORY_SCOPE_ANY UINT32_C(0)
#define IREE_HAL_MEMORY_SCOPE_INVALID UINT32_MAX

// Borrowed process-local identity. The group outlives every use of its scopes.
typedef struct iree_hal_memory_scope_t {
  // Identity token only; consumers never follow it to recover topology facts.
  const void* domain;
  // Dense coordinate within the domain, or MEMORY_SCOPE_INVALID.
  iree_hal_memory_scope_id_t id;
} iree_hal_memory_scope_t;

typedef enum iree_hal_memory_site_kind_e {
  IREE_HAL_MEMORY_SITE_HOST = 0,
  IREE_HAL_MEMORY_SITE_QUEUE = 1,
  IREE_HAL_MEMORY_SITE_PROGRAM = 2,
} iree_hal_memory_site_kind_t;

// Construction-time access site. Queue engines and their executable programs
// are distinct sites even when they share a physical device.
typedef struct iree_hal_memory_site_t {
  // Kind of access performed at this site.
  iree_hal_memory_site_kind_t kind;
  // Canonical borrowed family for QUEUE/PROGRAM; ignored for HOST.
  const iree_hal_queue_family_t* family;
} iree_hal_memory_site_t;

// Validates exact group and canonical-family membership at a public boundary.
// A device-local family ordinal cannot substitute for membership.
IREE_API_EXPORT iree_status_t iree_hal_device_group_resolve_memory_scope(
    const iree_hal_device_group_t* group, iree_hal_memory_site_t site,
    iree_hal_memory_scope_t* out_scope);

// Additional concrete native interfaces, with bit N denoting interface N.
typedef uint64_t iree_hal_buffer_interfaces_t;

typedef uint32_t iree_hal_pool_access_requirements_t;
enum iree_hal_pool_access_requirement_bits_e {
  // This family's access is coherent with the requested public host mapping.
  IREE_HAL_POOL_ACCESS_REQUIRE_COHERENT_WITH_HOST = 1u << 0,
  // This family uses an uncached native device access path.
  IREE_HAL_POOL_ACCESS_REQUIRE_UNCACHED = 1u << 1,
};

typedef enum iree_hal_host_cacheability_e {
  IREE_HAL_HOST_CACHEABILITY_UNKNOWN = 0,
  IREE_HAL_HOST_CACHEABILITY_WRITE_BACK = 1,
  IREE_HAL_HOST_CACHEABILITY_WRITE_COMBINED = 2,
  IREE_HAL_HOST_CACHEABILITY_UNCACHED = 3,
} iree_hal_host_cacheability_t;

typedef enum iree_hal_pool_placement_mode_e {
  IREE_HAL_POOL_PLACEMENT_AUTOMATIC = 0,
  IREE_HAL_POOL_PLACEMENT_PREFERRED = 1,
  IREE_HAL_POOL_PLACEMENT_REQUIRED = 2,
} iree_hal_pool_placement_mode_t;

typedef struct iree_hal_pool_placement_t {
  // Requested placement policy. In an achieved result, REQUIRED means the
  // native allocator enforces the node; AUTOMATIC makes no node guarantee.
  iree_hal_pool_placement_mode_t mode;
  // Group topology node; ignored for AUTOMATIC.
  uint32_t node;
} iree_hal_pool_placement_t;

typedef struct iree_hal_pool_family_access_t {
  // Exact canonical family in the sealed group. Order never selects placement.
  const iree_hal_queue_family_t* family;
  // Required transfer/storage/dispatch operations on all admitted storage.
  iree_hal_buffer_usage_t usage;
  // Required interfaces in addition to the backend's ordinary execution ABI.
  iree_hal_buffer_interfaces_t interfaces;
  // Hard access properties qualified together with every other participant.
  iree_hal_pool_access_requirements_t requirements;
} iree_hal_pool_family_access_t;

typedef struct iree_hal_pool_host_access_t {
  // Public mapping permissions, independent of CPU execution addresses.
  iree_hal_memory_access_t access;
  // Required mapping modes; zero with access zero disables public maps.
  iree_hal_mapping_mode_t modes;
  // Required mapping class; UNKNOWN permits the backend to choose.
  iree_hal_host_cacheability_t cacheability;
} iree_hal_pool_host_access_t;

// Complete simultaneous execution and host access. Arrays are borrowed only
// during construction; native storage preparation happens before publication.
typedef struct iree_hal_pool_scope_t {
  // Number of execution families whose accesses must all be supported.
  iree_host_size_t family_count;
  // Correlated permissions and interfaces for each distinct family.
  const iree_hal_pool_family_access_t* families;
  // Public host access; zero permits execution-only storage.
  iree_hal_pool_host_access_t host;
} iree_hal_pool_scope_t;

// Prepares an indexed native binding slot for this pool's captured contract.
// A slot is reusable for inherited child pools/views with the same contract.
IREE_API_EXPORT iree_status_t iree_hal_pool_resolve_binding(
    const iree_hal_pool_t* pool, iree_hal_memory_scope_t scope,
    iree_hal_buffer_interface_t type,
    iree_hal_buffer_native_binding_slot_t* out_slot);

// Returns captured public host access without traversing parent pools/topology.
IREE_API_EXPORT iree_hal_pool_host_access_t
iree_hal_pool_query_host_access(const iree_hal_pool_t* pool);

//===----------------------------------------------------------------------===//
// Prepared access contracts used by pool and buffer implementations
//===----------------------------------------------------------------------===//

// Immutable site facts. An excluded site has zero usage and no binding slots.
typedef struct iree_hal_memory_scope_access_t {
  // Semantic operations qualified at this exact site.
  iree_hal_buffer_usage_t usage;
  // Prepared representations; zero identifies an excluded site.
  uint32_t interfaces;
  // Prepared slot per native interface, or NATIVE_BINDING_INDEX_NONE.
  uint16_t bindings[8];
} iree_hal_memory_scope_access_t;

// Shared cold metadata retained by pools; buffers borrow it from their owner.
// No topology or parent-chain pointer is retained. Construction initializes all
// entries before publishing a pool; consumers only perform indexed loads.
typedef struct iree_hal_memory_contract_t {
  // Cold pool references, independent of native allocation lifetimes.
  iree_atomic_ref_count_t ref_count;
  // Allocator for this contract and its inline site array.
  iree_allocator_t host_allocator;
  // Borrowed namespace identity from the sealed group.
  const void* domain;
  // Number of dense site entries, including wildcard zero.
  uint32_t scope_count;
  // Global permission projection consumed by native buffer wrappers.
  iree_hal_buffer_params_t buffer_params;
  // Achieved owned-backing guarantee, independent of the original preference.
  iree_hal_pool_placement_t placement;
  // Achieved public host mapping contract.
  iree_hal_pool_host_access_t host;
  // Inline immutable site facts, indexed by memory scope ID.
  iree_hal_memory_scope_access_t scopes[];
} iree_hal_memory_contract_t;

// Allocates empty construction metadata with no native bindings. The producer
// fills trusted qualified facts before publishing any pool or buffer.
IREE_API_EXPORT iree_status_t iree_hal_memory_contract_create(
    const void* domain, uint32_t scope_count, iree_allocator_t host_allocator,
    iree_hal_memory_contract_t** out_contract);
IREE_API_EXPORT void iree_hal_memory_contract_retain(
    iree_hal_memory_contract_t* contract);
IREE_API_EXPORT void iree_hal_memory_contract_release(
    iree_hal_memory_contract_t* contract);

// Validates a public request against one already-qualified exact family. Zero
// required usage checks membership alone. Unscoped buffers use their original
// allocation permissions.
IREE_API_EXPORT iree_status_t iree_hal_buffer_validate_family_usage(
    const iree_hal_buffer_t* buffer, const iree_hal_queue_family_t* family,
    iree_hal_buffer_usage_t required_usage);

// Checks queue allocation ownership against the pool's qualified family set.
IREE_API_EXPORT iree_status_t iree_hal_pool_validate_family_usage(
    const iree_hal_pool_t* pool, const iree_hal_queue_family_t* family,
    iree_hal_buffer_usage_t required_usage);

// Returns this family's captured usage, or zero for a foreign/excluded family.
// The queue and program sites share the family's semantic permission set.
IREE_API_EXPORT iree_hal_buffer_usage_t iree_hal_buffer_family_usage(
    const iree_hal_buffer_t* buffer, const iree_hal_queue_family_t* family);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_MEMORY_SCOPE_H_
