// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Canonical AIE2P XDNA ELF product emission.

#ifndef LOOM_TARGET_ARCH_AMD_XDNA_AIE2P_EMIT_XDNA_PRODUCT_H_
#define LOOM_TARGET_ARCH_AMD_XDNA_AIE2P_EMIT_XDNA_PRODUCT_H_

#include "iree/base/api.h"
#include "iree/base/internal/arena.h"
#include "iree/io/stream.h"
#include "iree/schemas/xdna_executable.h"
#include "loom/target/arch/amd/xdna/aie2p/emit/program.h"
#include "loom/target/arch/amd/xdna/aie2p/emit/tile_link.h"
#include "loom/target/arch/amd/xdna/device/profile.h"

#ifdef __cplusplus
extern "C" {
#endif

// One linked resident tile program and its physical placement.
typedef struct loom_aie2p_xdna_tile_t {
  // Physical compute tile executing the program.
  loom_xdna_tile_coordinate_t coordinate;
  // Detached contribution from which symbols and sizes are retained.
  const loom_aie2p_leaf_contribution_t* contribution;
  // Fully placed and fixed-up native sections.
  const loom_aie2p_linked_tile_t* linked_tile;
} loom_aie2p_xdna_tile_t;

// One independently dispatchable array entry in an XDNA product.
typedef struct loom_aie2p_xdna_entry_t {
  // Diagnostic and runtime export name.
  iree_string_view_t name;
  // Required partition width, including worker-free service resources.
  uint16_t column_count;
  // Complete external requirements in binding-ordinal order. Extents include
  // control and service storage as well as payloads; commands do not imply
  // them.
  const iree_xdna_elf_binding_record_t* bindings;
  // Number of records in |bindings|.
  iree_host_size_t binding_count;
  // Typed array and invocation-control program awaiting final ordinals.
  const loom_aie2p_array_program_t* array_program;
  // Resident tile programs in worker order.
  const loom_aie2p_xdna_tile_t* tiles;
  // Number of records in |tiles|.
  iree_host_size_t tile_count;
} loom_aie2p_xdna_entry_t;

// Complete inputs to one canonical multi-entry AIE2P XDNA product.
typedef struct loom_aie2p_xdna_product_t {
  // Exact deployment profile serialized into image metadata.
  const loom_xdna_device_profile_t* device_profile;
  // Independently dispatchable entries in stable export-ordinal order.
  const loom_aie2p_xdna_entry_t* entries;
  // Number of records in |entries|.
  iree_host_size_t entry_count;
} loom_aie2p_xdna_product_t;

// Writes one canonical ELF32LE `.xdna` product.
//
// Native commands and entry requirements are supplied by their producers. Load
// ranges splice shared linked initialized sections and command fragments into
// caller-owned backing; identical payloads and repeat bodies each occupy one
// file range. Entries without per-invocation control records publish a
// header-only self-looping continuation and no empty load ranges. Entries
// publish exact storage, binding, relocation and invocation requirements.
// Placed uninitialized worker storage keeps its addresses without a load
// operation. Temporary metadata and native bytes use |scratch_arena| for this
// call.
iree_status_t loom_aie2p_xdna_product_write(
    const loom_aie2p_xdna_product_t* product, iree_io_stream_t* stream,
    iree_arena_allocator_t* scratch_arena);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_ARCH_AMD_XDNA_AIE2P_EMIT_XDNA_PRODUCT_H_
