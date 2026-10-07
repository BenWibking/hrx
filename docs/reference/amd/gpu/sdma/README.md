# SDMA

SDMA transfers data and executes control operations independently of shader
dispatch. Its packet representation, memory routing, and completion protocol
depend on the engine generation and native transport.

| Topic | Mechanisms |
| --- | --- |
| [Engine selection](engine-selection.md) | Ordinary/xGMI queue pools, directed engine masks, copy executors, runtime blit/native engine identities, read-only queue observations, and topology routing. |
| [Queue publication](publication.md) | Byte frontiers, reservation and ordered commit, wrap/padding, native visibility and storage ownership. |
| [Device-generated commands](device-publication.md) | GPUVM doorbells, shader-authored packets, pre-WPTR visibility, lane progress, and separate command/payload credits. |
| [Linear copy](copy.md) | `COPY_LINEAR`: byte ranges, count representation, runtime caps, alignment and chunking. |
| [Buffer exchange](swap.md) | `COPY_LINEAR_SWAP`, `COPY_LINEAR_SWAP_WAITSIGNAL_GFX1250`: equal-sized read/write operands, aligned chunks, engine joins and residency ownership. |
| [Indirect source and destination](indirect-copy.md) | `COPY_LINEAR_WAITSIGNAL_INDIRECT_GFX1250`, `LINEAR_INDIRECT_SRC` / `DST` / `SRCDST`: execution-time payload addresses, fixed lengths, slot publication and retirement. |
| [Broadcast and multicast](fanout.md) | `COPY_LINEAR_BROADCAST` / `COPY_BROADCAST_LINEAR`, `COPY_MULTICAST`, fused wait/signal blocks, destination pairing, and joins across copy engines. |
| [Batch composition](fanout.md#batch-composition-and-descriptor-ownership) | `hsa_amd_memory_async_batch_copy`: operation/entry/packet counts, agent and engine grouping, descriptor-control propagation and per-operation completion ownership. |
| [Rectangular copy](rectangular-copy.md) | `COPY_LINEAR_SUBWIN` / `COPY_LINEAR_RECT`: element units, row/slice pitches, subwindow layouts, tiling and geometry-specific cache controls. |
| [Constant fill](fill.md) | `CONST_FILL` / `CONSTANT_FILL`: legacy and DWORD count forms, chunk limits, cache/compression fields, caller selection and completion. |
| [Inline data writes](write.md) | `WRITE_LINEAR` / `WRITE_UNTILED`: command-carried DWORD data, policy layouts, dependent transfers and storage lifetime. |
| [Ordering](ordering.md) | Pending-transfer drains, overlap, NPD and resource ownership. |
| [Completion stores](fence.md) | `FENCE` / `FENCE_64B`: per-generation policy fields, notification and store widths. |
| [Memory dependencies](poll.md) | `POLL_REGMEM` / `POLL_MEM_64B`: comparisons, retry controls, signal lifetime and scoped layouts. |
| [Conditional execution](conditional.md) | `COND_EXE`: guarded DWORD ranges, Boolean64 sampling, per-execution predicate state and driver-owned submission conditions. |
| [Atomic operations and signaling](atomics.md) | `ATOMIC` (`ADD64`), `MEM_INCR`, `SEMAPHORE` and fused copy signaling: distinct completion and retirement protocols. |
| [Cache maintenance](cache.md) | USER_GCR, scheduled kernel GCR, HDP and command publication. |
| [Command buffers](command-buffers.md) | Generation-specific IB entries, body and submission alignment, context storage, direct rings and scheduled retirement. |
| [Timestamps](timing.md) | `TIMESTAMP_GET_GLOBAL`: policy layouts, transfer/gang/fanout intervals, sample readiness and lifetime, and native versus translated clock units. |

Payload visibility, a control-word update, notification, and storage
retirement are separate edges. The programming sequences identify which
operation establishes each edge and which actor still owns the referenced
memory.
