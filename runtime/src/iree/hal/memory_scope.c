// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/memory_scope.h"

#include "iree/base/internal/math.h"
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
  // A flattened prepared key uses 32 bits. This bound also keeps rounding the
  // row stride and multiplying the cell count representable on every host.
  if (scope_count < 2 || scope_count > UINT16_MAX) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "memory scope count does not fit the table layout");
  }
  const uint32_t row_shift =
      32u - iree_math_count_leading_zeros_u32(scope_count - 1);
  const uint32_t cell_count = scope_count << row_shift;
  iree_host_size_t total_size = 0;
  iree_host_size_t scopes_offset = 0;
  iree_host_size_t types_offset = 0;
  IREE_RETURN_IF_ERROR(IREE_STRUCT_LAYOUT(
      sizeof(iree_hal_memory_contract_t), &total_size,
      IREE_STRUCT_FIELD_FAM(cell_count, uint64_t),
      IREE_STRUCT_FIELD_ALIGNED(scope_count, iree_hal_memory_scope_access_t,
                                iree_alignof(iree_hal_memory_scope_access_t),
                                &scopes_offset),
      IREE_STRUCT_FIELD(binding_layout->binding_count, uint16_t,
                        &types_offset)));
  iree_hal_memory_contract_t* contract = NULL;
  IREE_RETURN_IF_ERROR(
      iree_allocator_malloc(host_allocator, total_size, (void**)&contract));
  iree_atomic_ref_count_init(&contract->ref_count);
  contract->host_allocator = host_allocator;
  contract->domain = domain;
  contract->scope_count = scope_count;
  contract->transition_row_shift = row_shift;
  contract->scopes =
      (iree_hal_memory_scope_access_t*)((uint8_t*)contract + scopes_offset);
  uint16_t* binding_types = (uint16_t*)((uint8_t*)contract + types_offset);
  if (binding_layout->binding_count) {
    memcpy(binding_types, binding_layout->types,
           binding_layout->binding_count * sizeof(*binding_types));
  }
  contract->binding_layout = *binding_layout;
  contract->binding_layout.types = binding_types;
  const uint64_t unsupported =
      (uint64_t)IREE_HAL_MEMORY_EFFECT_UNSUPPORTED |
      ((uint64_t)IREE_HAL_MEMORY_EFFECT_UNSUPPORTED << 32);
  for (uint32_t i = 0; i < cell_count; ++i) {
    contract->transitions[i] = unsupported;
  }
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
    iree_allocator_free(contract->host_allocator, contract->transition_details);
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

IREE_API_EXPORT iree_hal_memory_transition_table_t
iree_hal_pool_transition_table(const iree_hal_pool_t* pool) {
  return (iree_hal_memory_transition_table_t){pool->memory_contract};
}

// One allocation holds the cold cell indices and interned exact-pair records.
// Index zero is the all-unknown record, including for wildcard/padding cells.
struct iree_hal_memory_transition_details_t {
  // Interned records following the indices in this allocation.
  iree_hal_memory_pair_info_t* infos;
  // Exact-pair record index per flattened transition cell.
  uint32_t indices[];
};

static bool iree_hal_memory_scope_can_read(
    const iree_hal_memory_contract_t* contract, uint32_t id) {
  if (id == 1) {
    return iree_any_bit_set(contract->host.access, IREE_HAL_MEMORY_ACCESS_READ);
  }
  return contract->scopes[id].interfaces &&
         iree_any_bit_set(
             contract->scopes[id].usage,
             IREE_HAL_BUFFER_USAGE_TRANSFER_SOURCE |
                 IREE_HAL_BUFFER_USAGE_STORAGE_READ |
                 IREE_HAL_BUFFER_USAGE_DISPATCH_INDIRECT_PARAMETERS |
                 IREE_HAL_BUFFER_USAGE_DISPATCH_UNIFORM_READ |
                 IREE_HAL_BUFFER_USAGE_DISPATCH_IMAGE_READ);
}

static bool iree_hal_memory_scope_can_write(
    const iree_hal_memory_contract_t* contract, uint32_t id) {
  if (id == 1) {
    return iree_any_bit_set(contract->host.access,
                            IREE_HAL_MEMORY_ACCESS_WRITE);
  }
  return contract->scopes[id].interfaces &&
         iree_any_bit_set(contract->scopes[id].usage,
                          IREE_HAL_BUFFER_USAGE_TRANSFER_TARGET |
                              IREE_HAL_BUFFER_USAGE_STORAGE_WRITE |
                              IREE_HAL_BUFFER_USAGE_DISPATCH_IMAGE_WRITE);
}

static bool iree_hal_memory_transition_has_roles(
    const iree_hal_memory_contract_t* contract,
    iree_hal_memory_scope_t producer, iree_hal_memory_scope_t consumer) {
  return contract && producer.domain == contract->domain &&
         consumer.domain == contract->domain &&
         producer.id < contract->scope_count &&
         consumer.id < contract->scope_count && (producer.id || consumer.id) &&
         (!producer.id ||
          iree_hal_memory_scope_can_write(contract, producer.id)) &&
         (!consumer.id ||
          iree_hal_memory_scope_can_read(contract, consumer.id));
}

IREE_API_EXPORT iree_status_t iree_hal_memory_transition_prepare_pair(
    iree_hal_memory_transition_table_t table, iree_hal_memory_scope_t producer,
    iree_hal_memory_scope_t consumer,
    iree_hal_memory_transition_action_t action,
    iree_hal_memory_transition_pair_t* out_pair) {
  *out_pair = (iree_hal_memory_transition_pair_t){0};
  if ((action != IREE_HAL_MEMORY_TRANSITION_RELEASE &&
       action != IREE_HAL_MEMORY_TRANSITION_ACQUIRE) ||
      (!producer.id && action == IREE_HAL_MEMORY_TRANSITION_RELEASE) ||
      (!consumer.id && action == IREE_HAL_MEMORY_TRANSITION_ACQUIRE)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "transition requires an exact local action site");
  }
  if (!iree_hal_memory_transition_has_roles(table.contract, producer,
                                            consumer)) {
    return iree_make_status(
        IREE_STATUS_PERMISSION_DENIED,
        "transition scopes require admitted writes and reads in one domain");
  }
  out_pair->cell_index =
      (producer.id << table.contract->transition_row_shift) + consumer.id;
  return iree_ok_status();
}

static iree_hal_memory_transition_t iree_hal_memory_contract_query_transition(
    const iree_hal_memory_contract_t* contract,
    iree_hal_memory_scope_t producer, iree_hal_memory_scope_t consumer) {
  if (!iree_hal_memory_transition_has_roles(contract, producer, consumer)) {
    return (iree_hal_memory_transition_t){
        .release = {IREE_HAL_MEMORY_EFFECT_UNSUPPORTED},
        .acquire = {IREE_HAL_MEMORY_EFFECT_UNSUPPORTED},
    };
  }
  return iree_hal_memory_transition_query(
      (iree_hal_memory_transition_table_t){contract},
      (iree_hal_memory_transition_pair_t){
          (producer.id << contract->transition_row_shift) + consumer.id});
}

IREE_API_EXPORT iree_hal_memory_transition_t iree_hal_pool_query_transition(
    const iree_hal_pool_t* pool, iree_hal_memory_scope_t producer,
    iree_hal_memory_scope_t consumer) {
  return iree_hal_memory_contract_query_transition(pool->memory_contract,
                                                   producer, consumer);
}

IREE_API_EXPORT iree_hal_memory_transition_t iree_hal_buffer_query_transition(
    const iree_hal_buffer_t* buffer, iree_hal_memory_scope_t producer,
    iree_hal_memory_scope_t consumer) {
  return iree_hal_memory_contract_query_transition(buffer->memory.contract,
                                                   producer, consumer);
}

IREE_API_EXPORT iree_hal_memory_pair_info_t
iree_hal_memory_transition_query_info(iree_hal_memory_transition_table_t table,
                                      iree_hal_memory_transition_pair_t pair) {
  const iree_hal_memory_transition_details_t* details =
      table.contract->transition_details;
  return details ? details->infos[details->indices[pair.cell_index]]
                 : (iree_hal_memory_pair_info_t){0};
}

//===----------------------------------------------------------------------===//
// Cold qualification capture
//===----------------------------------------------------------------------===//

static uint32_t iree_hal_memory_transition_encode_effects(
    const iree_hal_memory_transition_recipe_info_t* info) {
  if (info->kind == IREE_HAL_MEMORY_TRANSITION_KIND_UNKNOWN) {
    return IREE_HAL_MEMORY_EFFECT_UNSUPPORTED;
  } else if (info->kind == IREE_HAL_MEMORY_TRANSITION_KIND_NONE) {
    return 0;
  }
  uint32_t bits = 0;
  switch (info->operation) {
    case IREE_HAL_MEMORY_TRANSITION_OPERATION_RELEASE_TO_SYSTEM:
      bits = IREE_HAL_MEMORY_EFFECT_RELEASE_TO_SYSTEM;
      break;
    case IREE_HAL_MEMORY_TRANSITION_OPERATION_ACQUIRE_FROM_SYSTEM:
      bits = IREE_HAL_MEMORY_EFFECT_ACQUIRE_FROM_SYSTEM;
      break;
    case IREE_HAL_MEMORY_TRANSITION_OPERATION_HOST_FLUSH:
      bits = IREE_HAL_MEMORY_EFFECT_HOST_FLUSH;
      break;
    case IREE_HAL_MEMORY_TRANSITION_OPERATION_HOST_INVALIDATE:
      bits = IREE_HAL_MEMORY_EFFECT_HOST_INVALIDATE;
      break;
    case IREE_HAL_MEMORY_TRANSITION_OPERATION_NATIVE_OWNERSHIP:
      bits = IREE_HAL_MEMORY_EFFECT_NATIVE_OWNERSHIP;
      break;
    default:
      return IREE_HAL_MEMORY_EFFECT_UNSUPPORTED;
  }
  if (info->kind == IREE_HAL_MEMORY_TRANSITION_KIND_RANGE ||
      info->executor == IREE_HAL_MEMORY_TRANSITION_EXECUTOR_HOST_DIRECT ||
      info->executor == IREE_HAL_MEMORY_TRANSITION_EXECUTOR_HOST_API ||
      info->executor == IREE_HAL_MEMORY_TRANSITION_EXECUTOR_EXTERNAL ||
      info->operation ==
          IREE_HAL_MEMORY_TRANSITION_OPERATION_NATIVE_OWNERSHIP) {
    bits |= IREE_HAL_MEMORY_EFFECT_RESOURCE_OPERANDS;
  }
  if (info->executor == IREE_HAL_MEMORY_TRANSITION_EXECUTOR_PROGRAM) {
    bits |= IREE_HAL_MEMORY_EFFECT_PROGRAM_EXECUTOR;
  }
  return bits;
}

static uint64_t iree_hal_memory_pair_encode(
    const iree_hal_memory_pair_info_t* info) {
  if (!iree_any_bit_set(info->flags,
                        IREE_HAL_MEMORY_PAIR_SHARED_BACKING_REACHABLE)) {
    return (uint64_t)IREE_HAL_MEMORY_EFFECT_UNSUPPORTED |
           ((uint64_t)IREE_HAL_MEMORY_EFFECT_UNSUPPORTED << 32);
  }
  return (uint64_t)iree_hal_memory_transition_encode_effects(&info->release) |
         ((uint64_t)iree_hal_memory_transition_encode_effects(&info->acquire)
          << 32);
}

static bool iree_hal_memory_recipe_info_equal(
    const iree_hal_memory_transition_recipe_info_t* lhs,
    const iree_hal_memory_transition_recipe_info_t* rhs) {
  return lhs->kind == rhs->kind && lhs->executor == rhs->executor &&
         lhs->operation == rhs->operation &&
         lhs->range_granularity == rhs->range_granularity &&
         lhs->host.instruction == rhs->host.instruction &&
         lhs->host.fence_before == rhs->host.fence_before &&
         lhs->host.fence_after == rhs->host.fence_after;
}

static bool iree_hal_memory_pair_info_equal(
    const iree_hal_memory_pair_info_t* lhs,
    const iree_hal_memory_pair_info_t* rhs) {
  return lhs->flags == rhs->flags &&
         iree_hal_memory_recipe_info_equal(&lhs->release, &rhs->release) &&
         iree_hal_memory_recipe_info_equal(&lhs->acquire, &rhs->acquire) &&
         lhs->atomic_reach.scope_32 == rhs->atomic_reach.scope_32 &&
         lhs->atomic_reach.scope_64 == rhs->atomic_reach.scope_64 &&
         lhs->estimated_fixed_cost_nanoseconds ==
             rhs->estimated_fixed_cost_nanoseconds;
}

// Only the local side contributes to a wildcard. An inapplicable read-only
// producer or write-only consumer is omitted; an applicable UNKNOWN poisons
// the join. This preserves missing PROGRAM qualification instead of silently
// treating a queue-only native query as proof for executable program accesses.
static void iree_hal_memory_transition_join_wildcards(
    const iree_hal_memory_contract_t* contract, uint64_t* cells) {
  const uint32_t shift = contract->transition_row_shift;
  for (uint32_t local = 1; local < contract->scope_count; ++local) {
    if (iree_hal_memory_scope_can_write(contract, local)) {
      uint32_t release = 0;
      for (uint32_t remote = 1; remote < contract->scope_count; ++remote) {
        if (iree_hal_memory_scope_can_read(contract, remote)) {
          release |= (uint32_t)cells[(local << shift) + remote];
        }
      }
      cells[local << shift] =
          (uint64_t)release |
          ((uint64_t)IREE_HAL_MEMORY_EFFECT_UNSUPPORTED << 32);
    }
    if (iree_hal_memory_scope_can_read(contract, local)) {
      uint32_t acquire = 0;
      for (uint32_t remote = 1; remote < contract->scope_count; ++remote) {
        if (iree_hal_memory_scope_can_write(contract, remote)) {
          acquire |= (uint32_t)(cells[(remote << shift) + local] >> 32);
        }
      }
      cells[local] = (uint64_t)IREE_HAL_MEMORY_EFFECT_UNSUPPORTED |
                     ((uint64_t)acquire << 32);
    }
  }
}

IREE_API_EXPORT iree_status_t iree_hal_memory_contract_initialize_transitions(
    iree_hal_memory_contract_t* contract, iree_hal_memory_pair_query_fn_t query,
    void* user_data) {
  const uint32_t cell_count = contract->scope_count
                              << contract->transition_row_shift;
  iree_host_size_t total_size = 0;
  iree_host_size_t cells_offset = 0;
  iree_host_size_t infos_offset = 0;
  IREE_RETURN_IF_ERROR(IREE_STRUCT_LAYOUT(
      sizeof(iree_hal_memory_transition_details_t), &total_size,
      IREE_STRUCT_FIELD_FAM(cell_count, uint32_t),
      IREE_STRUCT_FIELD_ALIGNED(cell_count, uint64_t, iree_alignof(uint64_t),
                                &cells_offset),
      IREE_STRUCT_FIELD_ALIGNED(cell_count, iree_hal_memory_pair_info_t,
                                iree_alignof(iree_hal_memory_pair_info_t),
                                &infos_offset)));
  iree_hal_memory_transition_details_t* scratch = NULL;
  IREE_RETURN_IF_ERROR(iree_allocator_malloc(contract->host_allocator,
                                             total_size, (void**)&scratch));
  uint64_t* cells = (uint64_t*)((uint8_t*)scratch + cells_offset);
  scratch->infos =
      (iree_hal_memory_pair_info_t*)((uint8_t*)scratch + infos_offset);
  memcpy(cells, contract->transitions, cell_count * sizeof(*cells));

  uint32_t info_count = 1;  // Record zero is the unqualified description.
  iree_status_t status = iree_ok_status();
  for (uint32_t producer = 1;
       producer < contract->scope_count && iree_status_is_ok(status);
       ++producer) {
    if (!iree_hal_memory_scope_can_write(contract, producer)) {
      continue;
    }
    for (uint32_t consumer = 1;
         consumer < contract->scope_count && iree_status_is_ok(status);
         ++consumer) {
      if (!iree_hal_memory_scope_can_read(contract, consumer)) {
        continue;
      }
      iree_hal_memory_pair_info_t info = {0};
      status = query(user_data, producer, consumer, &info);
      if (iree_status_is_ok(status)) {
        uint32_t index = 0;
        while (index < info_count && !iree_hal_memory_pair_info_equal(
                                         &scratch->infos[index], &info)) {
          ++index;
        }
        if (index == info_count) {
          scratch->infos[info_count++] = info;
        }
        const uint32_t cell_index =
            (producer << contract->transition_row_shift) + consumer;
        scratch->indices[cell_index] = index;
        cells[cell_index] = iree_hal_memory_pair_encode(&info);
      }
    }
  }

  iree_hal_memory_transition_details_t* details = NULL;
  if (iree_status_is_ok(status)) {
    status = IREE_STRUCT_LAYOUT(
        sizeof(*details), &total_size,
        IREE_STRUCT_FIELD_FAM(cell_count, uint32_t),
        IREE_STRUCT_FIELD_ALIGNED(info_count, iree_hal_memory_pair_info_t,
                                  iree_alignof(iree_hal_memory_pair_info_t),
                                  &infos_offset));
  }
  if (iree_status_is_ok(status)) {
    status = iree_allocator_malloc(contract->host_allocator, total_size,
                                   (void**)&details);
  }
  if (iree_status_is_ok(status)) {
    details->infos =
        (iree_hal_memory_pair_info_t*)((uint8_t*)details + infos_offset);
    memcpy(details->indices, scratch->indices,
           cell_count * sizeof(*details->indices));
    memcpy(details->infos, scratch->infos,
           info_count * sizeof(*details->infos));
    iree_hal_memory_transition_join_wildcards(contract, cells);
    contract->transition_details = details;
    memcpy(contract->transitions, cells, cell_count * sizeof(*cells));
  }
  iree_allocator_free(contract->host_allocator, scratch);
  return status;
}
