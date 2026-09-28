// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_TARGET_ARCH_AMD_XDNA_AIE2P_EMIT_CONFIGURATION_H_
#define LOOM_TARGET_ARCH_AMD_XDNA_AIE2P_EMIT_CONFIGURATION_H_

#include "loom/target/arch/amd/xdna/aie2p/emit/artifact.h"
#include "loom/target/arch/amd/xdna/aie2p/emit/xdna_product.h"

#ifdef __cplusplus
extern "C" {
#endif

// Emits a physical configuration entry and its referenced complete core
// programs.
//
// The entry names straight-line initialization and invocation functions. Their
// shared Low schedules supply instruction order and operand identities. Binding
// declarations supply complete external extents, including auxiliary services;
// range operands select relocation spans within those extents. Initialization
// loads already complete workers and never adds a firing loop or port protocol.
// Invocation contains runtime-address relocations and explicit final DMA waits.
// Low verification has established closed signatures, command phases, and
// complete physical worker definitions. Construction admits evaluated physical
// operands against the device profile. Input diagnostics leave |out_valid|
// false and no entry may be consumed; statuses report infrastructure failures.
// Referenced workers are materialized Low definitions: source specialization,
// call expansion, and storage/resource binding precede this physical form.
//
// The amd.xdna.aie2p.configuration representation has three operand classes:
// config.scalar contains exact nonnegative integers, config.binding identifies
// an invocation buffer, and config.span selects bytes relative to that buffer.
// The target assembly instructions have the following semantics:
//
//   constant value : reg<aie2p.config.scalar : i64>
//     Materializes an exact nonnegative scalar used by physical commands.
//   entry columns, @initialize, @invoke
//     Names argument-free configuration functions and the context-relative
//     column footprint. The public retained array_program contains one entry.
//   binding ordinal, access, length, alignment
//     Declares an invocation buffer's complete required extent and base
//     alignment, including auxiliary services. Access is read=1, write=2, or
//     both=3. Ordinals are dense in declaration order. A binding may start at
//     any logical buffer offset satisfying its extent and alignment contract.
//   range binding, offset, length
//     Selects a nonempty byte span within the declared binding extent.
//   write32 address, value; write.mask32 address, mask, value
//   write.block32 (address) [words...]
//     Perform ordered physical writes within one tile's register aperture.
//   write.address address, span
//     Writes the span's relocated runtime address as two consecutive words.
//   shim.descriptor address, span, length, flags, dim0, dim1, dim2, iter,
//   control
//     Writes eight shim DMA descriptor words with the span supplying the
//     address bits. Length and other fields use the native register encoding;
//     the span covers every byte reachable by the encoded dimensions.
//   program.load column, row, @worker
//     Loads a complete argument-free core definition during initialization.
//     The configuration keeps the core reset until its state is ready.
//   dma.wait column, row, direction, channel, columns, rows
//     Waits for task-completion tokens across the explicit rectangular range.
//     Direction is memory-to-stream=1 or stream-to-memory=2.
//
// Initialization executes once; subsequent invocations reuse its state. The
// invocation function supplies external bindings and all relocated writes.
// Configuration functions are statically evaluated straight-line Low programs;
// worker functions retain their complete authored control flow. Each distinct
// worker is compiled once and may be loaded onto several compute tiles.
//
// All storage in |out_entry| belongs to the request's arena. Unsupported
// authored configuration, unresolved physical values and native code/storage
// overflow fail before any artifact is returned.
iree_status_t loom_aie2p_configuration_emit(
    const loom_aie2p_xdna_artifact_request_t* request,
    const loom_op_t* entry_op, const loom_xdna_device_profile_t* device_profile,
    loom_aie2p_xdna_entry_t* out_entry, bool* out_valid);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LOOM_TARGET_ARCH_AMD_XDNA_AIE2P_EMIT_CONFIGURATION_H_
