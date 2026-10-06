# PM4 command buffers

INDIRECT_BUFFER redirects command fetch to a caller-owned GPU address and
count. A primary compute ring uses CHAIN clear to call a first-level IB and
resume afterward. A compute IB uses CHAIN set to replace its remaining stream
with another IB; it cannot nest an IB2 call and return. This creates two memory
owners: the referring ring or command stream, and the separately referenced
command storage. A shader launched by that body may remain active after command
parsing has advanced. [Compute call and chain rules][pal-compute-chain]

The libhsakmt KFD utility provides a concrete first-level raw-compute caller:
`Dispatch::Submit` builds an IB, places its reference on the primary ring, and
then emits the event-producing release on that ring. The body separately joins
its shader before return. Its memory-copy caller waits for the event and
destroys the queue before releasing the dispatch and memory owners.
[Caller and cleanup][kfd-caller] [Submission][kfd-submit] [Body join][kfd-body]

[Queue publication](publication.md) describes primary-ring construction,
write-pointer visibility and doorbell protocols for KFD, scheduled DRM and
DRM user queues. Those transport rules surround the indirect-command
storage and entry/return protocol described here.

[Conditional execution](conditional.md) describes `COND_EXEC` inline ranges
and `COND_INDIRECT_BUFFER` pass/fail blocks. The conditional form shares the
ordinary IB opcode but has a different packet layout and comparison operands.
Its selected branches retain the command ownership described here.

## Representation and entry context

The ordinary MEC INDIRECT_BUFFER form is four DWORDs, opcode `0x3f`:

| Word | Fields and units |
| --- | --- |
| 0 | Type-3 header, count 2. |
| 1 | IB byte address low word; bits 1:0 are zero, giving four-byte alignment. |
| 2 | High address word; the older KFD structure defines 16 address bits here, while PAL's later MEC structure defines 32. |
| 3 bits 19:0 | Direct positive DWORD count, not bytes or count-minus-one. |
| 3 bit 20 | CHAIN: zero selects return to the containing stream. |
| 3 bit 21 | OFFLOAD_POLLING, clear in the KFD caller. |
| 3 bit 22 | Reserved in PAL's layout; KFD's older `volatile_setting` is zero. |
| 3 bit 23 | VALID, set for MEC execution. |
| 3 bits 27:24 | VMID operand. KFD writes zero; this is not a report that the process executes in hardware VMID zero. |
| 3 bits 29:28 | Cache policy; interpretation depends on the packet definition below. |
| 3 bits 31:30 | Reserved, zero in these ordinary forms. |

[KFD construction][kfd-packet] [KFD layout][kfd-layout] [PAL MEC layout][pal-layout]

The count's field capacity does not override the allocation extent, address
width or transport's command-size/alignment requirements. The complete
referenced interval must fit the selected address representation and remain
readable. An aligned entry alone does not establish those facts.

The callers differ in two details. KFD uses header `0xc0023f02` and numeric cache
policy 2, named BYPASS in its older definition. PAL's builder uses header
`0xc0023f00` and the zero/LRU default; PAL's MEC enumeration calls 2 NOA and
3 BYPASS. These names cannot be merged into one universal interpretation.
PAL rejects compute preemption in this builder, whereas the graphics packet
has a distinct preemption field. [KFD packet][kfd-packet]
[PAL builder][pal-builder] [Header defaults][pal-header] [Policy values][pal-layout]

Entry and return do not save and restore shader register bindings. Each
state-dependent body needs the compatible program, resource, geometry and
argument state it consumes. PAL's public command-buffer contract likewise
requires explicit state for independent submitted buffers. Nested graphics
IB2, first-level MEC IBs and [AQL vendor carriers](../aql/transfers.md) are
separate entry/context contracts. RADV inlines compute secondary buffers in
its ordinary secondary-execution path while allowing graphics IB2 execution.
[Command state contract][pal-reset-contract] [RADV secondary execution][mesa-secondary]

## Chained blocks and continuations

PAL places a chaining INDIRECT_BUFFER in a reserved postamble at the end of
its source block. `GfxCmdStream::EndCommandBlock` accounts for that postamble
when computing the aligned block length and inserts any NOP padding before
it. Both the GFX9 and GFX12 command-stream implementations use this
construction. RADV likewise reserves four final DWORDs during finalization;
its chain operation replaces exactly those DWORDs with the next address,
count, CHAIN and VALID fields. [PAL block finalization][pal-block-end]
[GFX9 chunk end][pal-chunk-end] [GFX12 chunk end][pal12-chunk-end]
[RADV finalization][mesa-finalize] [RADV chaining][mesa-chain]

```text
source block: commands → alignment NOPs → CHAIN(next address, next DWORD count)
next block:   commands → alignment NOPs → next chain or terminal NOP postamble
```

These builders establish a complete block layout, including the placement of
padding. Appending padding after a chain produces a different stream: CHAIN
replaces the remaining stream, so those trailing NOPs are not its executed
epilogue. The source agreement establishes the callers' construction rule;
it does not specify the hardware response to every other layout.

A chain does not save a return address within its source IB. PAL implements a
compute secondary call by ending the parent block with a chain to the child,
then patching the child's reserved tail to the parent's continuation block.
The continuation's address and complete length become known when that block
is finalized. This path requires exclusive submission of the child because
the caller rewrites its tail. The return is another explicit chain, rather
than an IB2 return stack. [Compute continuation patching][pal-chain-return]
[Pending patch resolution][pal-block-end]

The per-block chain postamble is distinct from a scheduled submission's
postamble stream. PAL's AMDGPU queue chains eligible command buffers but adds
its submission postambles as separate kernel-launched streams. The cited
policy keeps those postambles reachable even when a graphics frame is
preempted; it is not a compute-preemption guarantee. A command owner cannot
silently absorb an external retirement stream into its own chain.
[Scheduled postamble ownership][pal-submit-postamble]

## Publication and memory ownership

Command bytes must be visible before the CP can fetch their reference. KFD's
host producer orders command-memory writes before the write pointer and then
the doorbell. An ACQUIRE_MEM inside the IB cannot publish the bytes that must
already have been fetched to execute it. [Ring publication][kfd-publication]

The KFD IB owner requests uncached executable system storage. In Linux's
GMC11 path, COHERENT or UNCACHED allocation flags become corresponding GEM
flags and select GPU MTYPE_UC. CPU cacheability is a different choice: TTM
selects cached backing unless CPU_GTT_USWC requests write combining, and
cached GTT receives SYSTEM/SNOOPED PTE treatment. This is a specific native
mapping composition, not a property of every host-visible allocation.
[IB allocation][kfd-owner] [Flag realization][linux-flags]
[GPU mapping][linux-mapping] [CPU caching][cpu-cache] [System snooping][system-pte]

The owner retains the full command backing through all published references,
including readable fetch storage outside the useful packet body. A return
establishes architectural continuation; it is not a timestamp for the last
speculative fetch. Shader executable padding has its own
[fetch requirements](dispatch.md#applicability-and-executable-ownership).

A complete raw-MEC lifetime composition is:

```text
write complete command bytes → publish the first-level reference
  → execute IB → CHAIN=0 return to the primary ring
  → join all shader/DMA users and perform required cache work
  → publish and observe a confirmed completion
  → retire the containing command range
  → reuse only storage with no remaining published reference
```

The shader join follows the applicable event/firmware rule in
[memory commands](memory-commands.md#compute-completion-and-firmware).
The KFD caller's in-body join uses a RELEASE_MEM fence in a NOP payload followed
by WAIT_REG_MEM; that body is not immutable. Its placement demonstrates the
separate execution join, not a requirement to store fences in command bytes.
[Body join][kfd-body] [Compute-idle builder][pal-completion]

## Native submission retirement

WDDM hardware-queue submission supplies a GPU `CommandBuffer` address, a
`CommandLength` in bytes, and a `HwQueueProgressFenceId` identifying native
completion. A command's own output marker and that progress fence describe
different observations. The native fence's update owner depends on the node's
`RingBufferFenceRelease` capability: [Submission fields][wddm-submit]
[Native fence contract][wddm-native-submit]

| Node capability | Native progress-fence update contract |
| --- | --- |
| `RingBufferFenceRelease = 0` | For a UMD submission, the UMD places the update at the end of the DMA buffer. Kernel submissions use the corresponding driver signaling path. |
| `RingBufferFenceRelease = 1` | The driver/GPU updates progress after neither GPU nor CPU uses the DMA buffer; the exact mechanism belongs to the native implementation. |

Microsoft defines this bit separately from context scheduling support.
Hardware scheduling being enabled alone does not identify the fence-update
contract. [Node capabilities][wddm-node-flags]

ROCr's `WDDMDevice::SubmitToHwQueue` provides a public caller: it fills WKMI
private data, submits the address, byte length and progress point, then frees
the host private data after the call. That private-data lifetime is expressly
permitted by the WDDM DDI. The public WKMI submission helper accepts no
continuation address and returns no native trailer location. These interfaces
therefore do not supply an address to which a caller could chain to resume an
opaque native wrapper. [ROCr hardware-queue submission][rocr-wddm-submit]
[WKMI submission interface][wkmi-submit] [Private-data lifetime][wddm-native-submit]

The complete scheduled lifetime consequently needs both the command graph's
execution/cache completion and the selected transport's native retirement.
Seeing a payload marker in the final chained body alone does not establish
that the native submission has released its command backing.

## CPU rebuild after completed use

PAL explicitly permits retained-address reuse. `Reset(false)` retains command
chunks, but the client must first ensure the buffer is neither queued nor
executing and every command buffer that referenced it through nested execution
has also been reset. `Reset` does not perform that wait. The retained chunks
have their recording cursors reset, `GetNextChunk` selects them for subsequent
recording, and finalization copies staged command bytes into mapped GPU storage
when needed. [Reset contract][pal-reset-contract] [Retained chunks][pal-reset]
[Chunk selection][pal-retained] [Finalization][pal-finalize]

The new recording's cache history has a different scope from the lifetime of
those retained chunks. PAL initializes conservative shader/cache state because
other work on the same queue can remain outstanding. Its nested-execution path
also carries the callee's recorded BLT/cache effects into the caller. The
[execution and cache history](cache.md#recorded-execution-and-cache-history)
describes those summaries and the distinct waits and cache actions that clear
them; resetting a summary does not establish storage retirement.

PAL's automatic allocator path has a different owner check: root submit/done
counts and generation tracking determine idleness. The compute postamble
waits for shaders because they may read or write command memory, then increments
the tracker. Its comment relies on a KMD EOP to flush the counter to memory.
That scheduled-submission guarantee is not implicit on a KFD user ring.
[Tracker idleness][pal-idle] [Automatic reuse][pal-reuse]
[Compute postamble][pal-postamble]

For a CPU rebuild of a raw first-level IB, the preceding lifetime composition
is therefore a cross-source construction: return to the primary stream,
explicit execution/cache completion, acquired completion, actual primary-ring
retirement, and no other queued reference to the old bytes. Keeping the full
backing mapped through queue removal avoids treating completion as proof of a
precise speculative-fetch endpoint. The CPU then writes and publishes the new
complete body before its next reference. Shader idle alone and ring consumption
alone are insufficient.

Repeated KFD `Submit` calls do not themselves demonstrate same-word rebuild:
its `IndirectBuffer::AddPacket` advances an append cursor. PAL's retained reset
path supplies the distinct rebuild owner evidence. [KFD append behavior][kfd-owner]

## GPU-produced command storage

PAL's compute command generator has a different publication protocol. It
requires one-time or exclusive submission, generates owned command chunks with
a shader, waits for that shader, emits ACQUIRE_MEM with scalar-cache
invalidation, then REWIND with offload disabled and valid set. The REWIND builder
explicitly reloads following command data; PFP_SYNC_ME is not available to this
MEC path. [Generation and ordering][pal-generated] [REWIND contract][pal-rewind]

The stream chains into generated chunks and reserves trailers to chain back.
Finalization attaches generated chunks to the main root's lifetime tracking.
This is a complete scheduled PAL protocol, not permission for concurrent CPU
mutation or a substitute for the firmware contract of an AQL carrier.
[Chain continuation][pal-generated-chain] [Generated owners][pal-generated-owner]

[kfd-caller]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/projects/rocr-runtime/libhsakmt/tests/kfdtest/src/KFDTestUtil.cpp#L283-L323
[kfd-submit]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/projects/rocr-runtime/libhsakmt/tests/kfdtest/src/Dispatch.cpp#L82-L104
[kfd-body]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/projects/rocr-runtime/libhsakmt/tests/kfdtest/src/Dispatch.cpp#L265-L276
[kfd-packet]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/projects/rocr-runtime/libhsakmt/tests/kfdtest/src/PM4Packet.cpp#L302-L323
[kfd-layout]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/projects/rocr-runtime/libhsakmt/tests/kfdtest/include/pm4_pkt_struct_common.h#L211-L256
[pal-layout]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/chip/gfx9_plus_merged_f32_mec_pm4_packets.h#L1571-L1632
[pal-compute-chain]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfxCmdStream.cpp#L466-L546
[pal-builder]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/gfx9CmdUtil.cpp#L2670-L2716
[pal-header]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/gfx9CmdUtil.h#L805-L810
[pal-reset-contract]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/inc/core/palCmdBuffer.h#L2244-L2306
[mesa-secondary]: https://gitlab.freedesktop.org/mesa/mesa/-/blob/0ba4b08edc65075e9346d20d5310261939aaaf48/src/amd/vulkan/winsys/amdgpu/radv_amdgpu_cs.c#L697-L756
[kfd-publication]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/projects/rocr-runtime/libhsakmt/tests/kfdtest/src/PM4Queue.cpp#L57-L76
[kfd-owner]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/projects/rocr-runtime/libhsakmt/tests/kfdtest/src/IndirectBuffer.cpp#L30-L53
[linux-flags]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/amdgpu_amdkfd_gpuvm.c#L1762-L1787
[linux-mapping]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/gmc_v11_0.c#L492-L525
[cpu-cache]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c#L1190-L1214
[system-pte]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c#L1432-L1476
[pal-completion]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/gfx9CmdUtil.cpp#L4284-L4337
[pal-reset]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/cmdStream.cpp#L482-L525
[pal-retained]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/cmdStream.cpp#L365-L395
[pal-finalize]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/cmdStreamAllocation.cpp#L347-L389
[pal-idle]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/cmdStreamAllocation.cpp#L470-L479
[pal-reuse]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/cmdAllocator.cpp#L703-L739
[pal-postamble]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/gfx9ComputeCmdBuffer.cpp#L1230-L1267
[pal-generated]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/gfx9ComputeCmdBuffer.cpp#L1444-L1543
[pal-rewind]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/gfx9CmdUtil.cpp#L3541-L3561
[pal-generated-chain]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfxCmdStream.cpp#L571-L606
[pal-generated-owner]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfxCmdBuffer.cpp#L238-L257
[pal-block-end]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfxCmdStream.cpp#L115-L205
[pal-chunk-end]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/gfx9CmdStream.cpp#L1525-L1547
[pal12-chunk-end]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx12/gfx12CmdStream.cpp#L68-L94
[mesa-finalize]: https://gitlab.freedesktop.org/mesa/mesa/-/blob/0ba4b08edc65075e9346d20d5310261939aaaf48/src/amd/vulkan/winsys/amdgpu/radv_amdgpu_cs.c#L478-L510
[mesa-chain]: https://gitlab.freedesktop.org/mesa/mesa/-/blob/0ba4b08edc65075e9346d20d5310261939aaaf48/src/amd/vulkan/winsys/amdgpu/radv_amdgpu_cs.c#L560-L592
[pal-chain-return]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfxCmdStream.cpp#L519-L549
[pal-submit-postamble]: https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/os/amdgpu/amdgpuQueue.cpp#L1235-L1313
[wddm-submit]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_submitcommandtohwqueue
[wddm-native-submit]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_submitcommandtohwqueue
[wddm-node-flags]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmdt/ns-d3dkmdt-_dxgk_nodemetadata_flags
[rocr-wddm-submit]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/projects/rocr-runtime/libhsakmt/src/dxg/wddm/device.cpp#L1134-L1164
[wkmi-submit]: https://github.com/ROCm/rocm-systems/blob/8d57824901ffa7d961c00a37d055a108723b93ca/shared/amdgpu-windows-interop/wkmi/wkmi.h#L299-L312
