# SDMA broadcast and multicast copies

SDMA can copy one source range to several destinations without a shader
dispatch. A paired broadcast packet carries two destinations; a multicast
packet carries a variable-length destination list. A runtime can also expand
the operation into ordinary copies on one or several engines. Those forms
have different command sizes, routing, cache fields, and completion protocols.

## Applicability and runtime selection

ROCr's `BlitSdma::Initialize` admits paired broadcast when compiler ISA major
is at least 10, or major is 9 with minor at least 4 or minor 0 and stepping at
least 10. It admits multicast and the associated fused wait/signal form when
`major == 12 && minor >= 5`, named `IsGfx125Plus` in the source. Initialization
also rejects `HSA_PROFILE_FULL`. These are runtime predicates, separate from
native SDMA IP and transport admission. [Initialization][initialize]

The public `HSA_AMD_MEMORY_COPY_OP_LINEAR_BROADCAST` operation supplies one
source, one positive byte length, destination pointer/agent arrays, and a
completion signal. Its dispatcher requires a GPU source agent and selects
that agent as the copy executor. `GpuAgent::DmaCopyBroadcast` selects the
following path for byte length `L`; its singleton broadcast/multicast path
uses the executor's `BlitHostToDev` object. That object name does not change
the source's placement or make this an exclusively host-to-device operation.
[Descriptor][descriptor] [Dispatcher][dispatcher] [Selector][select]

| Runtime case | Selected operation |
| --- | --- |
| `IsGfx125Plus`, default policy, `L <= 256 KiB` | Single-engine multicast. |
| `IsGfx125Plus`, `HSA_SDMA_MULTICAST=1` | Multicast regardless of length. |
| `IsGfx125Plus`, `HSA_SDMA_MULTICAST=0`, or default policy above 256 KiB | Ordinary-copy fan-out. |
| Other admitted targets, default policy, `16 KiB <= L <= 64 KiB` | Ordinary copies through the fan-out helper, limited to one engine. The source calls this linear B2B. |
| Other admitted targets, `HSA_SDMA_LINEAR_B2B=1` | The same single-engine ordinary-copy path regardless of length. |
| Other admitted targets, B2B not selected, broadcast supported, `L < 16 KiB` | Paired broadcast, plus an ordinary copy for an odd destination. |
| Remaining cases | Ordinary-copy fan-out across selected engines. |

The flag parser treats exactly `0` as disabled and `1` as enabled; other
values select the default policy. Disabling B2B does not force paired
broadcast at larger lengths: the `< 16 KiB` condition still applies. These
thresholds are consumer policy, not packet limits or measured crossover
guarantees. The executable B2B upper bound is 64 KiB despite a nearby comment
saying 256 KiB. [Flags][flags] [Selector][select]

Selection does not automatically recover from a builder's rejection. More
than 1024 destinations fail the selected multicast path, and incompatible
destination offsets fail paired broadcast; neither return triggers another
copy path. If the runtime's SDMA blit is disabled, the batch dispatcher rejects
broadcast in its shader fallback, even though ordinary linear copies have a
shader path. [Multicast submission][multicast-submit]
[Broadcast submission][broadcast-submit] [Batch dispatch][batch-dispatch]
[Shader fallback][shader-fallback]

## Paired broadcast representation

ROCr names the nine-DWORD form `SDMA_PKT_COPY_LINEAR_BROADCAST`; Linux's
generated spelling is `SDMA_PKT_COPY_BROADCAST_LINEAR`. ROCr's builder emits
the following fields, zero-initializing the rest. [Layout][broadcast-layout]
[Builder][broadcast-build]

| DWORD | Fields and emitted values |
| --- | --- |
| 0 | `op` bits 7:0 = 1; `sub_op` bits 15:8 = 0; `broadcast` bit 27 = 1. `enc` bit 16, `tmz` bit 18, `alg` bits 29:28 and `aes` bit 30 remain zero. |
| 1 | Positive chunk byte length minus one in the selected 22-bit or 30-bit count union. |
| 2 | `dst2_swap` bits 9:8, `dst1_swap` bits 17:16, and `src_swap` bits 25:24 remain zero. The three-bit cache-policy fields at 12:10, 20:18, and 28:26 also remain zero. |
| 3–4 | Full source byte address, low/high. |
| 5–6 | First destination byte address, low/high. |
| 7–8 | Second destination byte address, low/high. |

The submission checks that each consecutive destination pair has the same
low five address bits: `dst0 % 32 == dst1 % 32`. This is equal offset within
32-byte units, not a requirement that both addresses be multiples of 32.
The builder advances the source and both destinations by the same exact byte
count, preserving that relation across chunks. It uses the ordinary
[runtime copy cap](copy.md#runtime-caps-and-policy-margins), emits every chunk
for one pair before moving to the next pair, and handles an odd destination
with ordinary `COPY_LINEAR`. [Submission][broadcast-submit]
[Builder][broadcast-build]

For `N` destinations and `C = ceil(L / cap)` chunks, the copy commands occupy
`C * (9 * floor(N / 2) + 7 * (N % 2))` DWORDs, before dependencies, cache
operations, completion, notification, and padding. The host destination array
is read while constructing these inline address operands; the engine does not
receive a pointer to that array. [Submission][broadcast-submit]

The whole set of paired and trailing copies passes through `SubmitCommand`,
which handles dependencies, selected cache maintenance and timestamps, then
one operation completion and any notification tail. Its gfx9.0 predicate
excluding stepping 10 uses host-side dependency callbacks when a dependency
is still pending. That path retains a copy of the already serialized command
bytes before returning; it does not retain the caller's destination array.
This inherited [poll workaround](poll.md) means packet selection alone does
not establish a wholly device-side handoff. [Wrapper][submit-command]
[Workaround predicate][software-poll] [Deferred command owner][deferred-owner]

### Generation and policy differences

Linux's generated headers establish several representations, without supplying
a broadcast caller or the runtime's admission predicate:

| Header family | Broadcast fields |
| --- | --- |
| Iceland/Tonga | Nine DWORDs, broadcast bit 27, 22-bit count, swap fields, and HA fields at parameter bits 14, 22, and 30. [Iceland][linux-iceland] [Tonga][linux-tonga] |
| Vega10/Navi10 | Nine DWORDs, 22-bit count, ENC/TMZ and swap fields. [Vega10][linux-vega] [Navi10][linux-navi] |
| SDMA6.0 / SDMA7.1 | Nine DWORDs, 30-bit count, CPV at header bit 19, and three-bit destination/source cache policies. [SDMA6][linux6] [SDMA7.1][linux71] |

ROCr's header labels the form SDMA5.2+, but its initialization predicate admits
earlier compiler targets, and older Linux headers already name broadcast.
Those facts do not resolve the supported firmware/native-interface matrix.
The old headers also do not establish ROCr's count-minus-one interpretation
for an older engine. [Initialization][initialize] [ROCr layout][broadcast-layout]

The SDMA6/7.1 CPV bit falls inside a reserved range in ROCr's broadcast header.
ROCr leaves it and all parameter policies zero, including when its ordinary
linear-copy template emits explicit SYS scopes. The three-bit broadcast cache
policies are not interchangeable with the two-bit scope fields of ordinary
scoped copy. Surrounding [cache maintenance](cache.md) retains its own
template, link, and native-driver predicates. [Broadcast builder][broadcast-build]
[Ordinary builder][ordinary-build]

## Multicast representation

`SDMA_PKT_COPY_LINEAR_MULTICAST_GFX1250` uses opcode 1, suboperation 10
(`SDMA_SUBOP_COPY_MULTICAST`). Its plain form occupies `5 + 2N` DWORDs.
`N - 1` fits in a ten-bit field, giving 1–1024 destinations. ROCr's submission
enforces that bound before writing the packet. Its public descriptor's
`uint16_t num_entries` is a different limit: the header prose's stated 65536
entries cannot be represented by that field. [Constants][opcodes]
[Layout][multicast-layout] [Submission][multicast-submit] [Descriptor][descriptor]

| Plain-packet DWORD | Fields and emitted values |
| --- | --- |
| 0 | `op` bits 7:0 = 1; `sub_op` bits 15:8 = 10. TMZ bit 18 and NPD bit 28 remain zero. |
| 1 | `count` bits 29:0 = positive chunk byte length minus one. |
| 2 | `num_of_destination` bits 9:0 = `N - 1`; `dst_scope` bits 19:18 and `src_scope` bits 27:26 = SYS (3). Destination/source temporal hints at bits 22:20 and 30:28 remain zero. |
| 3–4 | Full source byte address, low/high. |
| `5 + 2j`, `6 + 2j` | Destination `j` byte address, low/high, for `0 <= j < N`. |

The builder zeroes reserved fields and advances every address by the chunk
length. The C structure models only the first destination; its `sizeof` is
not the serialized extent for `N > 1`. The multicast builder does not impose
the paired-broadcast low-five-bit relationship. [Builder][multicast-build]

### Fused wait, copy, and signal

`SDMA_PKT_COPY_LINEAR_MULTICAST_WAITSIGNAL_GFX1250` adds WAIT at header bit 30
and SIGNAL at bit 31. Its serialized blocks are:

| Block | DWORD extent and fields |
| --- | --- |
| Header | One DWORD, including the two presence bits. |
| Optional WAIT | Seven DWORDs: function/scope/hint; address low/high; reference low/high; mask low/high. Function occupies bits 2:0, scope bits 19:18, and hint bits 22:20 of the block's first word. |
| Copy | Four DWORDs: count, destination-count/scopes/hints, source low/high; followed by `2N` destination DWORDs. Fields match the plain copy block. |
| Optional SIGNAL | Five DWORDs: operation/scope/hint; address low/high; data low/high. Operation occupies bits 6:0, scope bits 19:18, and hint bits 22:20 of the block's first word. |

With Boolean presence values `W` and `S`, the total is
`1 + 7W + 4 + 2N + 5S` DWORDs. Absent blocks occupy no space. The copy count
starts at DWORD `1 + 7W`; the signal block, when present, starts at
`5 + 7W + 2N`. Extra destinations therefore move the signal block beyond its
position in the single-destination C structure. [Layout][fused-layout]
[Serialization][fused-build]

ROCr emits WAIT function 3 (equal), a zero reference, an all-ones 64-bit mask,
and SYS scope. It emits SIGNAL operation `0x70` (64-bit subtract), operand 1,
and SYS scope. Both control addresses preserve address bits 31:3 and 63:32,
with low bits 2:0 zero: the cells are eight-byte aligned. Temporal hints,
NPD, TMZ, and reserved fields remain zero. These are the builder's selected
operands, distinct from the classic [ATOMIC ADD64](atomics.md#copy-completion-through-add64)
completion encoding. [Builder][fused-build]

### Chunk completion and profiling

Without profiling, ROCr reserves one stream extent containing any selected
GCR acquire, separate 64-bit polls for dependencies after the first, the fused
multicast chunks, and any selected GCR release and mailbox FENCE/TRAP.
The first dependency folds into **every** chunk's WAIT. Every chunk also
subtracts one from the output signal, independent of destination count. Before
publication the host adds `C - 1`, so `C` chunk completions produce a net
decrement of one for the operation. This is per-chunk synchronization despite
an earlier comment describing only first/last-chunk synchronization.
[Submission][multicast-submit] [Builder][fused-build]

The multicast tail omits a poll of its own output signal. The source comment
attributes this choice to a gfx1250 failure to observe the inline signal with
an immediately following `POLL_REGMEM`; it supplies neither a firmware range
nor an independent architectural explanation. This is a specifically
attributed runtime workaround, not a general prohibition on polling another
engine's completion. [Tail and comment][multicast-tail]

With profiling enabled, the same operation uses plain multicast packets
inside `SubmitCommand`, which surrounds them with dependency/cache work,
start/end timestamps, and one operation completion. Profiling therefore
changes packet selection and completion overhead as well as recording time.
The [timestamp chapter](timing.md) identifies the sampled interval and clock;
profiled and unprofiled command counts are different baselines.
[Multicast selection][multicast-submit] [Submission wrapper][submit-command]

## Ordinary-copy fan-out and engine joins

`GpuAgent::DmaCopyBroadcast` expands the source and length into per-destination
entries. `DmaCopyFanOutOp` assigns engines and groups entries by engine using
the directed preferred-engine masks and runtime blit identities described in
[engine selection](engine-selection.md). Limiting the helper to one engine
keeps every entry on the coordinator. For `IsGfx125Plus` with destination-agent
information, available engines, and every entry at least 1 GiB, it assigns
successive groups of eight entries to engines from the preferred mask. Other
multi-engine cases select per-entry engines from the directed masks, with
unused-engine and round-robin choices when a recommendation is absent.
These are scheduling policies, not physical engine-number encodings.
[Expansion][select] [Assignment][fanout-assign]

The classic path publishes a coordinator prologue that waits for dependencies,
performs selected cache/timestamp work, and clears an internal start signal.
Each engine polls that signal before its ordinary copies. A coordinator
epilogue joins the body completions. On `IsGfx125Plus`, each engine group
instead folds its wait into the first copy packet and its completion into the
last. Unlike standalone multicast, ordinary fan-out does not attach a signal
to every chunk. In this fused path, profiling supplies a separate start signal;
without profiling the bodies consume the original dependencies. [Owner][fanout-owner]
[Prologue][prologue] [Bodies][bodies] [Boundary builder][boundary-build]

### Completion ownership

For one operation whose output `F` starts at one, let `G` be its number of
nonempty engine groups. The runtime emits these protocols:

| Path | Join and completion |
| --- | --- |
| Classic, platform 64-bit atomics available | Host adds `G` to `F`; each group decrements it once. Coordinator polls `F == 1`, performs its release/timestamp work, then decrements to zero. |
| Classic, platform atomics unavailable | Each group clears its own internal signal. Coordinator polls all those signals, performs release/timestamp work, and stores zero to the output's low DWORD. |
| Fused, with release, profiling, or mailbox tail | Host adds `G`; each group's final copy subtracts one. Coordinator polls `F == 1`, performs selected release/timestamp work, then stores zero with `FENCE_64B` before notification. |
| Fused, no GCR, profiling, or mailbox | Host adds `G` and removes the extra coordinator credit with a host decrement. The `G` final copy signals take `F` to zero; no coordinator poll/fence tail is emitted. |

The classic atomic predicate comes from the directed host link, with the
source's gfx7.0.1 exception; it is not implied by broadcast support. Fused
non-coordinator bodies are published before the coordinator's combined
prologue/body/epilogue reservation. Successful submission transfers internal
start/body signal ownership to an asynchronous handler observing output zero.
[Atomic predicate][initialize] [Owner][fanout-owner] [Epilogue][epilogue]
[Fused coordinator][coordinator]

The public batch API says all operations share a signal initialized to the
operation count. Its caller in CLR's `rocrCopyBufferBatch` instead requests a
separate `ActiveSignal(1, ...)` for every merged operation. The fixed-one joins
and zero stores above match the latter counter model; they do not implement
an arbitrary shared batch count. CLR preserves the independent signals and
`VirtualGPU::submitBatchCopyMemory` joins external completions before publishing
the enclosing command's completion. The public description and implementation
are different contracts at the cited revision. [API description][batch-api]
[CLR grouping/signals][clr-grouping] [CLR join][clr-join]

The descriptor also advertises raw wait/signal operands and a traffic class.
The public dispatcher validates raw wait/signal fields, but
`DmaCopyBroadcast` passes HSA dependencies and the HSA output signal to its
builders without those raw operands or traffic class. Consequently those
declarations do not establish configurable raw comparisons or QoS for this
copy path. The fused operands above describe what it actually emits.
[Descriptor][descriptor] [Validation][dispatcher] [Caller][select]

## Publication, visibility, and storage lifetime

A complete successful fan-out flow has the following owners:

1. The caller makes the source, each destination, and control cells accessible
   to every selected engine, using the required native mappings and peer access.
   The source remains unchanged through all engines' final reads. Independent
   destination ranges remove write/write and copy-overlap hazards; a destination
   address list does not establish an ordering between conflicting writes.
2. The producer makes payload and commands visible through the selected
   [cache protocol](cache.md) and [ring publication](publication.md). Each
   engine receives explicit dependencies. CPU descriptor arrays can cease to
   exist after synchronous command construction; payload and control mappings
   remain live for device access.
3. Paired broadcast and profiled multicast use the normal submission wrapper.
   Unprofiled multicast counts chunk signals. Ordinary fan-out joins engine
   groups using the protocol above. A downstream consumer acquires the result
   through the visibility edge appropriate to its own access path; the number
   of destination addresses does not change the required scope.
4. Output zero can precede a mailbox FENCE/TRAP. Notification storage remains
   live through that tail, and dependency signals remain stable through their
   final waiter. In particular, multicast can revisit the first dependency on
   every chunk. Payload completion does not retire a signal still being read.
5. Native read-index/commit rules govern primary-ring byte reuse separately
   from transfer completion. Source and destinations can be reused only after
   their final readers/writers, including downstream consumers, have finished.

These relationships follow the submission wrappers and the per-engine owner;
they are not additional effects supplied by a bare copy packet.
[Submission wrapper][submit-command] [Multicast owner][multicast-submit]
[Fan-out owner][fanout-owner] [CLR completion collection][clr-collection]

Error returns have a separate boundary. Fan-out publishes engine groups one
at a time and registers asynchronous internal-signal cleanup after the last
successful submission. Its early-return paths do not join already published
groups. The successful-flow protocol therefore supplies no general
transactional rollback or cancellation guarantee for a partially submitted
operation. [Owner and cleanup][fanout-owner]

## Command size and bandwidth

For one chunk and three destinations, paired broadcast needs 16 copy DWORDs
(nine plus seven), ordinary linear copies need 21, and plain multicast needs
11. Fused multicast with completion needs 16 without a wait or 23 with a wait.
These counts exclude surrounding polls, cache work, timestamps, notification,
and ring padding. They are command-size arithmetic, not latency measurements.
[Builders][broadcast-build] [Multicast serialization][fused-build]

Every destination still receives `L` bytes: the logical output volume is
`N * L`. A packet with one source operand does not by itself prove one DRAM
read, simultaneous writes, or `N`-fold bandwidth. Engine assignment, shared
memory links, packet selection, and profiling all affect the interval a
bandwidth comparison measures. The runtime's byte accounting records `N * L`,
while its size thresholds remain a separate scheduling policy.
[Accounting][multicast-submit] [Policy][select]

[initialize]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L156-L218
[descriptor]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/inc/hsa_ext_amd.h#L2235-L2394
[dispatcher]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/hsa_ext_amd.cpp#L520-L777
[select]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_gpu_agent.cpp#L1921-L2030
[flags]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/util/flag.h#L390-L397
[batch-dispatch]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_gpu_agent.cpp#L2141-L2234
[shader-fallback]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_gpu_agent.cpp#L2087-L2139
[broadcast-layout]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/inc/sdma_registers.h#L152-L242
[broadcast-build]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L2188-L2234
[broadcast-submit]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L1639-L1697
[ordinary-build]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L2142-L2186
[linux-iceland]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/iceland_sdma_pkt_open.h#L150-L256
[linux-tonga]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/tonga_sdma_pkt_open.h#L150-L256
[linux-vega]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/vega10_sdma_pkt_open.h#L412-L512
[linux-navi]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/navi10_sdma_pkt_open.h#L568-L668
[linux6]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/sdma_v6_0_0_pkt_open.h#L625-L749
[linux71]: https://github.com/torvalds/linux/blob/50d05c7c76c96b90462f24debacca971d2e86713/drivers/gpu/drm/amd/amdgpu/sdma_v7_1_0_pkt_open.h#L625-L749
[opcodes]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/inc/sdma_registers.h#L37-L77
[multicast-layout]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/inc/sdma_registers.h#L1200-L1264
[multicast-build]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L2236-L2280
[multicast-submit]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L1699-L1858
[multicast-tail]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L1823-L1857
[fused-layout]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/inc/sdma_registers.h#L1606-L1766
[fused-build]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L2282-L2375
[submit-command]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L398-L666
[software-poll]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L264-L268
[deferred-owner]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L310-L364
[fanout-assign]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_gpu_agent.cpp#L1547-L1670
[fanout-owner]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_gpu_agent.cpp#L1716-L1902
[prologue]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L808-L943
[bodies]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L1350-L1577
[boundary-build]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L2715-L2798
[epilogue]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L944-L1075
[coordinator]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/core/runtime/amd_blit_sdma.cpp#L1077-L1348
[batch-api]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/rocr-runtime/runtime/hsa-runtime/inc/hsa_ext_amd.h#L2395-L2430
[clr-grouping]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/clr/rocclr/device/rocm/rocblit.cpp#L879-L956
[clr-collection]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/clr/rocclr/device/rocm/rocblit.cpp#L3160-L3280
[clr-join]: https://github.com/ROCm/rocm-systems/blob/f9ba16bbe70e365b2f59b268e847bef19ad9db6e/projects/clr/rocclr/device/rocm/rocvirtual.cpp#L3730-L3797
