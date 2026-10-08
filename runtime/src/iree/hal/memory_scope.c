// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/memory_scope.h"

#include "iree/hal/device_group.h"
#include "iree/hal/pool.h"

IREE_API_EXPORT iree_status_t iree_hal_device_group_resolve_memory_scope(
    const iree_hal_device_group_t* group, iree_hal_memory_site_t site,
    iree_hal_memory_scope_t* out_scope) {
  *out_scope = (iree_hal_memory_scope_t){
      .id = IREE_HAL_MEMORY_SCOPE_INVALID,
  };
  if (!group) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "memory scope requires a sealed device group");
  }
  if (site.kind == IREE_HAL_MEMORY_SITE_HOST) {
    *out_scope = (iree_hal_memory_scope_t){
        .domain = iree_hal_device_group_memory_domain(group),
        .id = 1,
    };
    return iree_ok_status();
  }
  if (site.kind != IREE_HAL_MEMORY_SITE_QUEUE &&
      site.kind != IREE_HAL_MEMORY_SITE_PROGRAM) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "invalid memory site kind %u", site.kind);
  }
  if (!site.family) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "queue/program memory site requires a family");
  }
  iree_hal_device_t* device = iree_hal_queue_family_device(site.family);
  bool is_member = false;
  for (iree_host_size_t i = 0; i < iree_hal_device_group_device_count(group);
       ++i) {
    is_member |= iree_hal_device_group_device_at(group, i) == device;
  }
  if (!is_member ||
      site.family->ordinal >=
          iree_hal_device_spec_queues(iree_hal_device_spec(device))
              ->family_count ||
      iree_hal_device_queue_family(device, site.family->ordinal) !=
          site.family) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "memory site family is not a canonical group member");
  }
  *out_scope = (iree_hal_memory_scope_t){
      .domain = site.family->memory.domain,
      .id = site.family->memory.queue_scope_id +
            (site.kind == IREE_HAL_MEMORY_SITE_PROGRAM ? 1 : 0),
  };
  return iree_ok_status();
}

IREE_API_EXPORT iree_status_t iree_hal_memory_contract_create(
    const void* domain, uint32_t scope_count,
    const iree_hal_buffer_binding_layout_t* binding_layout,
    iree_allocator_t host_allocator,
    iree_hal_memory_contract_t** out_contract) {
  *out_contract = NULL;
  iree_hal_memory_contract_t* contract = NULL;
  IREE_RETURN_IF_ERROR(iree_allocator_malloc(
      host_allocator,
      sizeof(*contract) + scope_count * sizeof(*contract->scopes) +
          binding_layout->binding_count * sizeof(*binding_layout->types),
      (void**)&contract));
  iree_atomic_ref_count_init(&contract->ref_count);
  contract->host_allocator = host_allocator;
  contract->domain = domain;
  contract->scope_count = scope_count;
  uint16_t* binding_types = (uint16_t*)&contract->scopes[scope_count];
  memcpy(binding_types, binding_layout->types,
         binding_layout->binding_count * sizeof(*binding_types));
  contract->binding_layout = *binding_layout;
  contract->binding_layout.types = binding_types;
  for (uint32_t i = 0; i < scope_count; ++i) {
    memset(contract->scopes[i].bindings, 0xFF,
           sizeof(contract->scopes[i].bindings));
  }
  *out_contract = contract;
  return iree_ok_status();
}

IREE_API_EXPORT void iree_hal_memory_contract_retain(
    iree_hal_memory_contract_t* contract) {
  if (contract) {
    iree_atomic_ref_count_inc(&contract->ref_count);
  }
}

IREE_API_EXPORT void iree_hal_memory_contract_release(
    iree_hal_memory_contract_t* contract) {
  if (contract && iree_atomic_ref_count_dec(&contract->ref_count) == 1) {
    iree_allocator_free(contract->host_allocator, contract);
  }
}

IREE_API_EXPORT iree_status_t iree_hal_pool_resolve_binding(
    const iree_hal_pool_t* pool, iree_hal_memory_scope_t scope,
    iree_hal_buffer_interface_t type,
    iree_hal_buffer_native_binding_slot_t* out_slot) {
  *out_slot = (iree_hal_buffer_native_binding_slot_t){
      .index = IREE_HAL_BUFFER_NATIVE_BINDING_INDEX_NONE,
  };
  const iree_hal_memory_contract_t* contract = pool->memory_contract;
  if (!contract || scope.domain != contract->domain ||
      scope.id == IREE_HAL_MEMORY_SCOPE_ANY ||
      scope.id >= contract->scope_count) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "binding scope is outside the pool memory domain");
  }
  if ((uint32_t)type >= IREE_ARRAYSIZE(contract->scopes[0].bindings)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "invalid native binding interface %u", type);
  }
  uint16_t index = contract->scopes[scope.id].bindings[type];
  if (index == IREE_HAL_BUFFER_NATIVE_BINDING_INDEX_NONE) {
    return iree_make_status(IREE_STATUS_PERMISSION_DENIED,
                            "native interface is not prepared for this scope");
  }
  *out_slot = (iree_hal_buffer_native_binding_slot_t){
      .index = index,
      .type = (uint16_t)type,
  };
  return iree_ok_status();
}

IREE_API_EXPORT iree_hal_pool_host_access_t
iree_hal_pool_query_host_access(const iree_hal_pool_t* pool) {
  return pool->memory_contract ? pool->memory_contract->host
                               : (iree_hal_pool_host_access_t){0};
}

static const iree_hal_memory_scope_access_t*
iree_hal_memory_contract_family_access(
    const iree_hal_memory_contract_t* contract,
    const iree_hal_queue_family_t* family) {
  if (family->memory.domain != contract->domain ||
      family->memory.queue_scope_id >= contract->scope_count) {
    return NULL;
  }
  const iree_hal_memory_scope_access_t* access =
      &contract->scopes[family->memory.queue_scope_id];
  return access->interfaces ? access : NULL;
}

IREE_API_EXPORT iree_hal_buffer_usage_t iree_hal_buffer_family_usage(
    const iree_hal_buffer_t* buffer, const iree_hal_queue_family_t* family) {
  if (!buffer->memory.contract) {
    return buffer->allowed_usage;
  }
  const iree_hal_memory_scope_access_t* access =
      iree_hal_memory_contract_family_access(buffer->memory.contract, family);
  return access ? access->usage : IREE_HAL_BUFFER_USAGE_NONE;
}

IREE_API_EXPORT iree_status_t iree_hal_buffer_validate_family_usage(
    const iree_hal_buffer_t* buffer, const iree_hal_queue_family_t* family,
    iree_hal_buffer_usage_t required_usage) {
  if (!buffer->memory.contract) {
    return iree_hal_buffer_validate_usage(buffer->allowed_usage,
                                          required_usage);
  }
  const iree_hal_memory_scope_access_t* access =
      iree_hal_memory_contract_family_access(buffer->memory.contract, family);
  if (!access) {
    return iree_make_status(IREE_STATUS_PERMISSION_DENIED,
                            "queue family is outside the buffer memory scope");
  }
  return iree_hal_buffer_validate_usage(access->usage, required_usage);
}

IREE_API_EXPORT iree_status_t iree_hal_pool_validate_family_usage(
    const iree_hal_pool_t* pool, const iree_hal_queue_family_t* family,
    iree_hal_buffer_usage_t required_usage) {
  if (!pool->memory_contract) {
    return iree_ok_status();
  }
  const iree_hal_memory_scope_access_t* access =
      iree_hal_memory_contract_family_access(pool->memory_contract, family);
  if (!access) {
    return iree_make_status(IREE_STATUS_PERMISSION_DENIED,
                            "queue family is outside the pool memory scope");
  }
  return iree_hal_buffer_validate_usage(access->usage, required_usage);
}
