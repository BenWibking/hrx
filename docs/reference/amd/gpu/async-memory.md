# Asynchronous global/LDS transfers

A shader wave can move data between global memory and local data share (LDS)
while other waves consume an earlier tile. Transfer completion makes a slot
ready for its readers; a separate acknowledgment from those readers permits
the next transfer to overwrite it. These two ownership edges allow a resident
pipeline to reuse LDS without a host round trip between stages.

This chapter covers CDNA5's asynchronous memory instructions, Tensor Data
Mover (TDM), and LDS split barriers in the **27 July 2026 ISA guide**. The
[architecture map](architectures.md#cdna5-and-gfx1250) connects CDNA5 to
`gfx1250`. Triton's pinned CDNA5 lowering and GEMM example supply a concrete
single-workgroup producer/consumer protocol. Their choices of barrier width,
participant count and polling sequence remain compiler policy. The
[shader memory chapter](shader-memory.md) owns the surrounding global-memory
cache and scope rules; these shader-issued transfers have a different command
and completion path from an [SDMA queue](sdma/README.md).

## Transfer families and completion units

| Operation | Data path | Issuing wave's completion class |
| --- | --- | --- |
| `GLOBAL_LOAD_ASYNC_TO_LDS_B{8,32,64,128}` | Global memory to LDS, without a payload round trip through VGPRs. | `ASYNCcnt`, waited by `S_WAIT_ASYNCCNT`. |
| `GLOBAL_STORE_ASYNC_FROM_LDS_B{8,32,64,128}` | LDS to global memory. | `ASYNCcnt`, shared with asynchronous loads. |
| `TENSOR_LOAD_TO_LDS`, `TENSOR_STORE_FROM_LDS` | Descriptor-directed tensor transfer between global memory and LDS. | `TENSORcnt`, waited by `S_WAIT_TENSORCNT`. |
| Ordinary `DS_LOAD_*`, `DS_STORE_*` and returning LDS barrier arrival | Shader access to LDS, including the consumer's reads and acknowledgments. | `DScnt`, separate from both transfer counters. |

The transfer counters are six bits wide. Tensor completion decrements its
counter once per native instruction, regardless of the number of memory
transactions that instruction generates. Reaching zero drains the named
class for the issuing wave; it neither joins another wave nor drains the
other classes. [Counter definitions, §5.7][isa-counters]
[Asynchronous operations, §10.8][isa-async] [Tensor issue, §10.11.1][isa-tensor]

Asynchronous loads and stores may access LDS out of issue order, including
two loads writing LDS. Load completions are ordered against other load
completions, and store completions against other store completions, but the
two classes can report completion out of order relative to each other.
A nonzero `ASYNCcnt` threshold therefore cannot identify an arbitrary prefix
of a mixed load/store sequence. Overlapping writes also need their own
ordering edge; a final zero wait alone does not select which unordered write
wins. [LDS access and completion order, §10.8][isa-async]

Tensor loads and stores report completion in order within one wave. Tensor
instructions from another wave, and ordinary memory instructions from the
same wave, have no such implied ordering. A later tensor notification can
cover earlier tensor work from its issuing wave, while a workgroup-wide
result needs arrivals from every contributing wave.
[Tensor ordering, §10.11.1][isa-tensor]

Compiler-level operation counts can differ from hardware instruction counts.
Triton's `AsyncTDMWait` counts tensor IR operations; its
`AsyncTDMIntrinsicWait` counts the native instructions produced by lowering.
A partitioned transfer can expand into several instructions. The compiler
calculates that expansion before emitting the `WaitTensorcnt` intrinsic.
[Operation units][triton-wait-ops] [Count conversion][triton-wait-conversion]
[Expansion accounting][triton-wait-counts] [Counter emission][triton-wait-lowering]

## Tensor descriptors and completion notification

The tensor descriptor, written `D#` in the ISA, is supplied in groups of
SGPRs. A tensor instruction executes once per wave and **ignores `EXEC`**;
disabling lanes does not suppress its transfer. The descriptor supplies the
global/LDS addresses, tensor layout and optional completion notification.
[Descriptor operands, §10.11.1][isa-tensor]

The fields that control notification are:

| Descriptor field | Location | Meaning |
| --- | --- | --- |
| `count` | Group 0, DWORD 0, bits 1:0 | Normal issue uses `1`. `0` is a null descriptor: neither a transfer nor an atomic barrier arrival occurs. Other values belong to context restore. |
| `atomic_barrier_enable` | Group 1, DWORD 0, bit 18 | Requests an LDS barrier arrival after tensor completion. |
| `atomic_barrier_address` | Group 1, DWORD 1, bits 15:0 | LDS byte address divided by eight; the 64-bit barrier object is eight-byte aligned. |

[Descriptor tables 62–63][isa-descriptor] [Barrier-address field][isa-barrier-address]
[Triton notification fields][triton-descriptor-barrier]

Triton suppresses redundant waves by making their descriptors null. With one
notification per participating wave, the barrier's expected TDM arrivals
count **non-null signaling waves**, rather than lanes, bytes or all waves
named by the launch. A masked-out transfer does not provide an arrival
merely because its instruction was issued.
[Wave selection][triton-descriptor-predicate]

The descriptor notification is ordered after its tensor operation and prior
tensor descriptors from the same wave. Triton's lowering consequently
attaches an explicit barrier operand only to the last native instruction
of an expanded transfer. Its GEMM producer goes further: it transfers operand
A without a notification, then operand B with one notification covering both operands.
The ready barrier joins those per-wave arrivals.
[Tensor notification, §10.11.3][isa-notification]
[Expanded transfer emission][triton-last-transfer]
[Two-operand producer][triton-producer]

### Shader-issued asynchronous arrival

`DS_ATOMIC_ASYNC_BARRIER_ARRIVE_B64` can also be issued directly by a shader.
It schedules a decrement of one after preceding asynchronous global loads
into LDS and itself contributes to `ASYNCcnt`. It returns no barrier state.
Triton's `async_copy_mbarrier_arrive` contract explicitly excludes TDM and
lowers to this instruction's intrinsic.
[Instruction contract, §15.15][isa-async-arrive]
[Compiler operation contract][triton-async-arrive]
[Compiler lowering][triton-async-arrive-lowering]

The ISA overview in §11.2.2 mentions TDM when describing asynchronous
arrival, while its detailed shader-instruction rule names prior asynchronous
global loads. Sections 10.11.3 and 11.2.2.1 separately describe the arrival
generated by the TDM descriptor. The descriptor route and the standalone
instruction therefore retain separate source contracts: the latter's name
does not establish a drain of tensor operations or asynchronous stores.
[Overview and TDM integration][isa-barrier-continuation]
[Descriptor-generated arrival][isa-notification]

## LDS barrier state and phases

An LDS split barrier stores its state in one 64-bit object. In the phased
modes, arrival advances the phase when the pending count underflows and
reloads the count for the next use. A waiter observes a phase change,
allowing the arriving waves to continue without waiting for every reader.
[Barrier semantics and table 70, §11.2.2][isa-barrier]

| Bits in the 64-bit object | Contents |
| --- | --- |
| `WIDTH-1:0` | Pending count. |
| `31:WIDTH` | Phase. |
| `47:32` | Reload count. |
| `63:48` | Zero. |

Table 70 lists pending widths of 21, 28, 29, 31 and 32 bits; width 32 leaves
no phase bits. Triton's implementation assumes **width 29** and observes only
bit 29 as parity. For an expected count `N`, it initializes both counts to
`N-1`, leaves the phase zero, and synchronizes the workgroup before use.
That count adjustment translates the API's N arrivals into the hardware's
underflow rule. The reload field remains 16 bits wide, independently of the
pending-count width. The cited lowering establishes its 29-bit representation,
but does not identify how hardware selects among the widths in table 70.
[Compiler initialization and layout][triton-barrier-init]

The manual's two rollover expressions disagree: §11.2.2, printed page 153,
tests `WIDTH == 32`, while the detailed instruction expressions in §15.15,
printed pages 730 and 738, test `WIDTH != 32` and the top pending-count bit.
Those expressions cannot be treated as one literal algorithm. The
source-attributed 29-bit consumer protocol here preserves that distinction
instead of selecting a width-independent correction.
[Overview expression][isa-barrier-continuation]
[Asynchronous instruction expression][isa-async-arrive]
[Returning instruction expression][isa-ordinary-arrive]

`DS_ATOMIC_BARRIER_ARRIVE_RTN_B64` follows ordinary LDS operations, uses
`DScnt`, subtracts the supplied update value, and returns the prior barrier
state. It supplies the consumer-to-producer acknowledgment after LDS reads,
or the compute-to-epilogue acknowledgment after LDS writes. Triton emits
this operation for ordinary arrival and implements waiting as LDS phase
loads separated by `S_SLEEP 1`. The architecture wakes sleeping workgroup
waves when the barrier completes. This is the cited compiler's polling
strategy, without a latency guarantee.
[Ordinary LDS ordering and wakeup][isa-barrier]
[Arrival and wait lowering][triton-barrier-arrive-wait]

Parity alone cannot distinguish a phase from another two generations later.
The ISA assigns the program responsibility for keeping participants within
one phase of each other. Returning storage credit before the next producer
use provides that backpressure in the following protocol.
[Phase discipline][isa-barrier]

## Reusable input and output slots

Triton's single-workgroup GEMM assigns different waves to input transfer and
compute. Each input slot owns two 64-bit barrier objects, `ready` and
`empty`. The sample counts producer waves for `ready` and consumer threads
for `empty`: the two notification paths have different arrival units.
[Allocation and participant counts][triton-ring-init]

For `B` slots, logical use `k` selects slot `k % B` and generation
`g = floor(k / B)`. The example's ready waiter uses parity `g & 1`, and
the empty waiter uses `(g + 1) & 1`; waiting ends when the stored parity
differs. Both objects start at phase zero, so the producer's first empty
wait succeeds immediately. Later traversals wait for the preceding reader
generation. The slot's actual use count, including any outer-loop reuse,
determines its generation. [Phase bookkeeping][triton-phase]
[Producer wait][triton-producer] [Consumer wait and acknowledgment][triton-consumer]

The input flow is:

1. The workgroup allocates disjoint LDS slots and barrier objects, initializes
   them, and joins initialization before the producer and consumer roles run.
   Global input data already satisfies the transfer's mapping and visibility
   contract.
2. Producer waves wait for the selected slot's `empty` phase. They issue
   the operand transfers and attach `ready` notification to the final tensor
   operation in each participating wave.
3. Compute waves wait for the slot's `ready` phase, then read its LDS data.
   A leader's own `TENSORcnt` wait would not join the other producers.
4. Every counted reader arrives on `empty` after its LDS accesses. The last
   arrival advances the phase, allowing the next generation's transfer.
   Register-held values may remain live after their LDS source is released;
   the reuse boundary is the final access to that LDS slot.
5. Producer and consumer roles repeat using the next slot and its matching
   generation. A transfer-ready notification never substitutes for reader
   credit on an earlier generation.

[Producer and consumer caller][triton-input-flow]

The persistent variant also stages output accumulators in LDS. Compute
threads store the accumulator and arrive on its `ready` barrier. Epilogue
waves wait for that phase, issue `TENSOR_STORE_FROM_LDS`, and attach the
accumulator's `empty` barrier to the transfer. Completion returns source
storage to compute. Here `ready` counts compute threads while `empty`
counts epilogue waves—the reverse of the input slot's arrival units.
[Accumulator producer][triton-accumulator]
[Tensor-store consumer and final wait][triton-epilogue]
[Output participant counts][triton-output-counts]

This separates data readiness from storage retirement in both directions.
Triton's memory-dependency analysis makes the same distinction: a wait can
discharge the read-after-write edge from an asynchronous fill to an LDS
reader, but it preserves the write-after-read edge before a later refill.
[Compiler dependency rule][triton-reuse-dependency]

## Final drain, external publication and clusters

The last iteration has no later reuse wait to retire its resources
incidentally. Each resource instead follows its final user:

| Resource | Final ownership edge |
| --- | --- |
| Global input | Completion of every transfer that still reads it. |
| Input LDS slot | Final compute reads and their acknowledgment. |
| Accumulator LDS slot | Completion of the final TDM store that reads it. |
| Barrier object | Final asynchronous arrival, ordinary arrival and polling read. |
| Workgroup LDS allocation | All participating roles finish their resource-specific accesses and join. |
| Global output | Transfer completion followed by the publication/acquire operations required by its external observer. |

The epilogue example ends with `tdm.async_wait(0)`. Warp-specialization
lowering independently joins the worker partitions and the default
partition, then signals their exit state. Those are separate mechanisms:
the per-slot barrier communicates ownership, the tensor wait drains the
issuing wave's transfer class, and the partition join accounts for the
other participants. [Epilogue drain][triton-epilogue]
[Worker join][triton-worker-join] [Default join and exit][triton-partition-exit]
[AMD workgroup-barrier callback][triton-join-callback]

CDNA5's `S_ENDPGM` implicitly performs `S_WAIT_IDLE`, which waits for all
activity and dependency counters of that wave. This final termination rule
does not order an earlier mid-kernel reuse or drain another wave's work.
It also does not replace the global-memory observer's
[release/acquire contract](shader-memory.md#global-release-and-acquire-sequences).
[Wave idle][isa-idle] [Program termination][isa-endpgm]

Cluster transfers add different participants. The TDM `workgroup_mask`
selects cluster asynchronous loads for tensor loads; stores ignore it,
and a wave outside a cluster uses a zero mask. Cluster-load completion is
reported to the requesting wave; other receiving waves need an explicit
barrier or memory-atomic synchronization path. A local ready/empty ring's
counts and final-reader join consequently cannot be inherited unchanged
by multicast receivers. [Tensor cluster selection][isa-notification]
[Cluster completion, §10.7][isa-cluster]

[isa-counters]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=63
[isa-async]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=144
[isa-tensor]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=149
[isa-descriptor]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=152
[isa-barrier-address]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=154
[isa-notification]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=151
[isa-barrier]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=161
[isa-barrier-continuation]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=162
[isa-async-arrive]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=739
[isa-ordinary-arrive]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=747
[isa-idle]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=295
[isa-endpgm]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=300
[isa-cluster]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf#page=143
[triton-wait-ops]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/include/Dialect/TritonAMDGPU/IR/TritonAMDGPUOps.td#L1111-L1144
[triton-wait-conversion]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUTransforms/UpdateAsyncWaitCount.cpp#L355-L410
[triton-wait-counts]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUTransforms/UpdateAsyncWaitCount.cpp#L461-L511
[triton-wait-lowering]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/LoadStoreOpToLLVM.cpp#L2474-L2489
[triton-descriptor-barrier]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/TDMUtility.cpp#L976-L989
[triton-descriptor-predicate]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/TDMUtility.cpp#L922-L955
[triton-last-transfer]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/TDMUtility.cpp#L1288-L1383
[triton-producer]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/python/examples/gluon/f16_gemm_cdna5.py#L807-L838
[triton-async-arrive]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/include/Dialect/TritonAMDGPU/IR/TritonAMDGPUOps.td#L1362-L1373
[triton-async-arrive-lowering]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/LoadStoreOpToLLVM.cpp#L2517-L2535
[triton-barrier-init]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/BarrierOpToLLVM.cpp#L10-L53
[triton-barrier-arrive-wait]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/BarrierOpToLLVM.cpp#L58-L118
[triton-ring-init]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/python/examples/gluon/f16_gemm_cdna5.py#L903-L942
[triton-phase]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/python/examples/gluon/f16_gemm_cdna5.py#L779-L804
[triton-consumer]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/python/examples/gluon/f16_gemm_cdna5.py#L842-L876
[triton-input-flow]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/python/examples/gluon/f16_gemm_cdna5.py#L807-L942
[triton-accumulator]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/python/examples/gluon/f16_gemm_cdna5.py#L1182-L1214
[triton-epilogue]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/python/examples/gluon/f16_gemm_cdna5.py#L1217-L1244
[triton-output-counts]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/python/examples/gluon/f16_gemm_cdna5.py#L1468-L1484
[triton-reuse-dependency]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/MembarUtility.cpp#L68-L77
[triton-worker-join]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/lib/Conversion/TritonGPUToLLVM/WarpSpecializeUtility.cpp#L415-L427
[triton-partition-exit]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/lib/Conversion/TritonGPUToLLVM/WarpSpecializeUtility.cpp#L563-L593
[triton-join-callback]: https://github.com/triton-lang/triton/blob/8262c9a91a1d6828ad4f36437fa0046daa67720d/third_party/amd/lib/TritonAMDGPUToLLVM/ConvertWarpSpecializeToLLVM.cpp#L159-L173
