# GPU programming

AMD GPUs expose command-processing and transfer engines with distinct packet
representations, execution boundaries, and cache responsibilities. A command
stream combines these mechanisms with the native queue's publication and
resource-lifetime protocol.

| Area | Topics |
| --- | --- |
| [Architecture and transport](architectures.md) | Physical IP, compiler targets, native queues, and submission ownership. |
| [Compute affinity and queue priority](scheduling.md) | CU/WGP mask units, harvested SE/SH/XCC placement, `COMPUTE_STATIC_THREAD_MGMT_SE*`, native priority encodings and update ownership. |
| [Cooperative execution and grid synchronization](cooperative.md) | Shared cooperative queues, `ALLOC_QUEUE_GWS`, firmware admission, occupancy, hidden grid state and GWS/atomic barrier scopes. |
| [Compute context save and restore](context-save.md) | CWSR, `ctx_save_restore_size`, per-XCC storage, control-stack inspection, suspension and final ownership. |
| [Shader memory publication](shader-memory.md) | Resident release/acquire sequences, `s_waitcnt`, `buffer_inv`, `global_wb`, cache scopes, and wave/workgroup joins across GCN, CDNA and RDNA. |
| [Native signals and host notification](notifications.md) | `event_mailbox_ptr`, KFD event ages, interrupt decoding, sleeping waits and notification-storage lifetime. |
| [PM4](pm4/README.md) | Compute dispatch, memory commands, cache control, and command-buffer execution. |
| [AQL](aql/README.md) | Packet publication, dispatch, dependencies, vendor command carriers, and profiling. |
| [SDMA](sdma/README.md) | Transfer-engine operations and signaling protocols. |
| [Programming recipes](recipes/README.md) | Host/device, cross-engine, and local-memory producer/consumer flows. |
| [CPU, GPU and NPU interop](../interop/README.md) | External sharing, directed device handoffs, resident pipelines and storage reuse. |
| [Timing and counters](observability.md) | Clock domains, timestamp conversion, counter ownership, and profiling interference. |

Compiler target, physical graphics/compute IP, SDMA IP, firmware, and native
transport identify different parts of a programming contract. The operation
chapters retain those conditions beside the packet fields and sequences they
govern.
