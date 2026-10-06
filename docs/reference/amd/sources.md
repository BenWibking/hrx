# Primary sources

The reference combines architecture and ABI definitions with firmware-facing
packet layouts, native drivers, and upstream consumers. Each chapter cites the
specific definition, builder, caller, or resource owner that supports a claim.

| Source | Revision | Role |
| --- | --- | --- |
| [PAL][pal] | `c5e800072a32f68b6ccc4422936d96167c6e0728` | Generation-specific packet definitions, command builders, cache transitions, and command-storage ownership. |
| [Mesa][mesa] | `0ba4b08edc65075e9346d20d5310261939aaaf48` | RADV/radeonsi command composition, memory policy, synchronization, and native submission. |
| [Mesa counter, barrier and shader policy][mesa-counter-barriers] | `44cc4ca677a4752a10c14194289bde5a6468675e` | GFX12 performance-query admission, event selection and SQG sample ordering; consumer-stage selection for graphics PWS waits; compiler-selected MEM_ORDERED mode, linked-program requirements and per-SA compute affinity. |
| [ROCm systems][rocm] | `8d57824901ffa7d961c00a37d055a108723b93ca` | ROCr queue/signal/copy protocols, HIP/CLR consumers, native queue construction, and profiling. |
| [ROCm XIO][xio] | `cbe97e6392066bef7901121965ffadad19404da4` | Device-authored SDMA packets, reservation/commit protocols and native queue-resource ownership; compared with the related rocSHMEM Anvil implementation in ROCm systems. |
| [ROCm native memory and engine policy][rocm-native] | `f9ba16bbe70e365b2f59b268e847bef19ad9db6e` | BO-backed queue storage, AQL metadata publication and scratch reclamation, signal/event notification and host waits, pageable host SVM allocation, context-save layout and ownership, DRM import/mapping ownership, CLR peer-engine selection, broadcast/multicast copy selection and completion ownership, compute affinity, native queue priority and cooperative launch admission/ownership. |
| [ROCm device libraries][rocm-device-libs] | `8cd9ac8c8f12ab07e92229ea9d690b49e866b8ec` | OCKL hidden grid state, GWS versus atomic barrier selection, split arrival/wait and workgroup/agent/system fence composition. |
| [Linux][linux] | `50d05c7c76c96b90462f24debacca971d2e86713` | Native UAPI, queue descriptors, memory mappings, engine emitters, and driver resource lifetimes. |
| [Linux DMA-BUF attachment updates][linux-dmabuf] | `fe2ec83746e501645709761605c2464a44fd2929` | Exporter runtime PM references and their relation to P2P eligibility and memory placement. |
| [Vulkan specification][vulkan] | `01aaacd99480487bf63830959513c5ca8ceb996d` | External memory and semaphore capabilities, handle ownership, host-pointer imports, resource ownership transfers, host visibility, and performance-query lifetimes. |
| [LLVM ABI and memory model][llvm-abi] | `6e714c8d91116794cb699cdf80c26afe9cda3ef3` | Kernel descriptors, initial registers, address spaces, shader memory ordering, availability/visibility, and the matching cache-control emitter and target predicates. |
| [LLVM compiler implementation][llvm-compiler] | `6dfe1677ab8dffbc6ec13d53a1e0215d75147689` | Executable fetch padding, dispatch inputs, partial workgroups, and target feature selection. |
| [Triton CDNA5 transfer lowering][triton-cdna5] | `8262c9a91a1d6828ad4f36437fa0046daa67720d` | Asynchronous and tensor transfer counts, descriptor notification, LDS split barriers and a workgroup producer/consumer reuse protocol. |
| [ROCm target identities][therock-targets] | `1cc8ec570e9f5c720de9ce52dc485228c2df0274` | CDNA5 product-to-compiler-target mapping, kept separate from native GC and SDMA IP. |
| [AMD atomic-operation tables][legacy-rocm] | `85a16825737e43a14ff431754b359380e78062a7` | Architecture-specific atomic operation tables and their separate PCIe-route interpretations. |
| [Windows DDI][windows-ddi] | `7515063cea4c9e98db6a92986c5b4ddb0463fd16` | Native submission, monitored fences, mapping, residency, and destruction contracts. |
| [XDNA driver][xdna] | `8dfda66f67a84aecf26cf68336efc9e4cc1756c3` | Array contexts, firmware command envelopes, native completion, power management, and diagnostic access. |
| [AI Engine driver][aie] | `2855a032366e3d19dab893e7c263b14bb920cd64` | Register descriptions, timers, event counters, stream switches, DMA, and trace configuration. |
| [AI Engine binary utilities][aiebu] | `e82e28cbb237dcfd6c3029d85dc604515367ee24` | AIE2 transaction encoding, AIE2PS CERT controller instructions and task-wait ownership, and ELF platform identities. |
| [MLIR-AIE][mlir-aie] | `c69fb4c8f2fb853d5ca62d19f829796d3ae4ba34` | Array register data, trace-flow construction, and trace decoding. |
| [MLIR-AIE and IRON dataflow][mlir-aie-dataflow] | `41fa359ea1f66f7e5c572f8d0cc8c7646262adf5` | Object FIFOs, task ownership, descriptor allocation, DMA lowering, and stream routes. |
| [XRT runtime][xrt] | `ecad6cf22171ffec754fdd36afc2ae200af5c3a6` | Buffer synchronization delegation, ZYNQ/ZOCL GMIO channel completion, and managed external-buffer task ownership. |
| [XDP][xdp] | `03ba80bf6c4942f51eebc71f7d154d9254426396` | Array profiling, trace storage and offload, and host/device timeline consumers. |
| [DynamicDispatch][dynamic-dispatch] | `b3051f03e20aab237cda3bbe4cd2081f76b72b06` | Firmware transaction construction and timestamp-marker encoding. |

HSA's [System Architecture 1.2][hsa-system] defines standard AQL packets,
publication, signal, and fence-scope semantics. Its [Programmer's Reference
Manual 1.2][hsa-prm] supplies work-item and workgroup execution rules. AMD's
[GPU architecture documentation index][gpu-manuals] locates individual ISA
manuals; shader instructions and command-processor packets have separate
representations and owners.

The [RDNA4 ISA guide, 7 April 2025][rdna4-isa], §5.7 and Table 26, supplies
the architecture's dependency-counter rules used in the
[shader wait-mode discussion](gpu/pm4/dispatch.md#shader-wait-counter-mode-mem_ordered).

The [CDNA5 ISA guide, 27 July 2026][cdna5-isa], covers cache policy in
§§4.1.1–2, barriers in §5.6, dependency counters in §5.7, asynchronous memory
in §10.8, tensor movement in §10.11 and LDS split barriers in §11.2.2. The
[architecture map](gpu/architectures.md#cdna5-and-gfx1250) connects its naming
to public compiler and native-driver sources.

GPUOpen's [6 August 2026 machine-readable ISA archive][isa-xml] contains
`amdgpu_isa_cdna5.xml`, whose document revision is 3 August 2026 and schema
version is 1.2.0. Its instruction definitions provide a second representation
of the cache and wait operations; the descriptions and manual are both AMD
sources, rather than independent hardware observations.

AMD's [Micro Engine Scheduler specification, April 2024][mes-manual],
introduced by GPUOpen as an RDNA3 scheduling overview, describes the native
MES API and its scheduling fields. Its `ADD_QUEUE` flags corroborate the
[cooperative-launch discussion](gpu/cooperative.md#what-reaches-the-scheduler);
that revision does not establish the behavior of later MES implementations.

Microsoft's public [D3D12 sharing][d3d12-sharing] and [queue synchronization][d3d12-sync]
documentation supplies the API contracts for shared heaps, resources and
fences. Those object and execution contracts complement the native Windows
DDI; neither defines a device-specific NPU import protocol. The external-memory
chapter cites the particular creation, residency, mapping and completion APIs.

The AI Engine chapters also use AMD's AM020 and AM027 architecture manuals,
the AM025 register manual, UG1079 kernel coding guide, UG1603 revision 2026.1
for managed GMIO and external-memory dataflow, and the versioned
2025.1/2026.1 intrinsic references.
Each chapter identifies the relevant document, section, architecture, and
toolchain version.
Those architecture manuals do not by themselves establish how a native host
driver exposes the same mechanism.

Arm's [AMBA AXI and ACE specification IHI 0022F.b][axi4] defines the AXI4
transaction attributes, response ordering and single-copy atomicity used in
the array's external-memory discussion. The array and host integration
determine where those protocol observations meet another device's memory
and cache path.

Several consumers can share the same generated packet definition or delegate
to the same runtime. Agreement along that chain corroborates the caller flow;
it is not independent evidence about the underlying representation. A driver
submission wrapper can supply operations absent from the command buffer it
executes. The corresponding chapter identifies those boundaries.

[pal]: https://github.com/GPUOpen-Drivers/pal/tree/c5e800072a32f68b6ccc4422936d96167c6e0728
[mesa]: https://gitlab.freedesktop.org/mesa/mesa/-/tree/0ba4b08edc65075e9346d20d5310261939aaaf48
[mesa-counter-barriers]: https://gitlab.freedesktop.org/mesa/mesa/-/tree/44cc4ca677a4752a10c14194289bde5a6468675e
[rocm]: https://github.com/ROCm/rocm-systems/tree/8d57824901ffa7d961c00a37d055a108723b93ca
[xio]: https://github.com/ROCm/rocm-xio/tree/cbe97e6392066bef7901121965ffadad19404da4
[rocm-native]: https://github.com/ROCm/rocm-systems/tree/f9ba16bbe70e365b2f59b268e847bef19ad9db6e
[rocm-device-libs]: https://github.com/ROCm/llvm-project/tree/8cd9ac8c8f12ab07e92229ea9d690b49e866b8ec/amd/device-libs
[linux]: https://github.com/torvalds/linux/tree/50d05c7c76c96b90462f24debacca971d2e86713
[linux-dmabuf]: https://github.com/torvalds/linux/blob/fe2ec83746e501645709761605c2464a44fd2929/drivers/gpu/drm/amd/amdgpu/amdgpu_dma_buf.c
[vulkan]: https://github.com/KhronosGroup/Vulkan-Docs/tree/01aaacd99480487bf63830959513c5ca8ceb996d
[llvm-abi]: https://github.com/llvm/llvm-project/tree/6e714c8d91116794cb699cdf80c26afe9cda3ef3
[llvm-compiler]: https://github.com/llvm/llvm-project/tree/6dfe1677ab8dffbc6ec13d53a1e0215d75147689
[triton-cdna5]: https://github.com/triton-lang/triton/tree/8262c9a91a1d6828ad4f36437fa0046daa67720d
[therock-targets]: https://github.com/ROCm/TheRock/tree/1cc8ec570e9f5c720de9ce52dc485228c2df0274
[legacy-rocm]: https://github.com/ROCm/legacy-rocm-build/tree/85a16825737e43a14ff431754b359380e78062a7
[windows-ddi]: https://github.com/MicrosoftDocs/windows-driver-docs-ddi/tree/7515063cea4c9e98db6a92986c5b4ddb0463fd16
[d3d12-sharing]: https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps
[d3d12-sync]: https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization
[xdna]: https://github.com/amd/xdna-driver/tree/8dfda66f67a84aecf26cf68336efc9e4cc1756c3
[aie]: https://github.com/Xilinx/aie-codegen/tree/2855a032366e3d19dab893e7c263b14bb920cd64
[aiebu]: https://github.com/Xilinx/aiebu/tree/e82e28cbb237dcfd6c3029d85dc604515367ee24
[mlir-aie]: https://github.com/Xilinx/mlir-aie/tree/c69fb4c8f2fb853d5ca62d19f829796d3ae4ba34
[mlir-aie-dataflow]: https://github.com/Xilinx/mlir-aie/tree/41fa359ea1f66f7e5c572f8d0cc8c7646262adf5
[xrt]: https://github.com/Xilinx/XRT/tree/ecad6cf22171ffec754fdd36afc2ae200af5c3a6
[xdp]: https://github.com/Xilinx/XDP/tree/03ba80bf6c4942f51eebc71f7d154d9254426396
[dynamic-dispatch]: https://github.com/amd/DynamicDispatch/tree/b3051f03e20aab237cda3bbe4cd2081f76b72b06
[hsa-system]: https://hsafoundation.com/wp-content/uploads/2021/02/HSA-SysArch-1.2.pdf
[hsa-prm]: https://hsafoundation.com/wp-content/uploads/2021/02/HSA-PRM-1.2.pdf
[gpu-manuals]: https://gpuopen.com/amd-gpu-architecture-programming-documentation/
[rdna4-isa]: https://gpuopen.com/download/rdna4-instruction-set-architecture.pdf
[cdna5-isa]: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/instruction-set-architectures/amd-instinct-cdna5-instruction-set-architecture.pdf
[isa-xml]: https://gpuopen.com/download/AMD_GPU_MR_ISA_XML_2026_08_06.zip
[mes-manual]: https://gpuopen.com/download/documentation/micro_engine_scheduler.pdf
[axi4]: https://documentation-service.arm.com/static/5f915bbcf86e16515cdc3b23
