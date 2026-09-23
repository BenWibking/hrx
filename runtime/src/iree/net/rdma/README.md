# Native RDMA Resources

This layer owns Linux rdma-core resources independently of a connection or an
async proactor. An explicit context retains the canonical device inventory,
one protection domain, and the dynamically loaded libraries. Registered async
regions retain that context and their backing slab. Connections can therefore
share registrations without registering the same memory for every connection
or making registration lifetime depend on a polling thread.

`runtime/src/iree/net/rdma/context.h` defines native ownership and device
selection. `runtime/src/iree/net/rdma/region.h` adapts slab registration to the
existing `iree_async_region_t` and span model. Native queues borrow the context's
handles while retaining their owner. This layer installs no worker, address
registry, proactor callback or host transfer scheduler.

## Addresses And Lifetime

Registration takes an explicit 64-bit NIC virtual address. Its page offset must
match the slab's CPU mapping, but the two addresses need not be equal. Native
requests use the region's RDMA address plus a span offset, never a reconstructed
CPU pointer. Registration is setup work; sources and targets reuse the same keys
across transfers and compatible connections.

The caller joins native accesses before releasing a registration. Source
completion, target consumption, and host callback retirement are distinct
ownership facts. Destroying a connection does not release registrations still
retained by another connection or application. A remote descriptor for part of
an MR does not independently revoke access to that part: the native key
authorizes its registered extent.

Last release deregisters the MR before releasing the slab and protection-domain
owner. Wrapped slabs still borrow their actual memory from the caller. Native
destruction is infallible under this lifetime contract; an inability to retire
native ownership is fatal, not a successful release. Context release performs
no implicit queue or GPU execution wait.

## Build And Qualification

Native RDMA is opt-in and does not register a host transport factory:

```sh
iree-bazel-configure -DIREE_NET_RDMA=ON
iree-bazel-test --config=asan \
  --test_env=IREE_NET_RDMA_TEST_DEVICE=<device-name> \
  //runtime/src/iree/net/carrier/rdma:completion_queue_test
```

The corresponding CMake option is `IREE_NET_RDMA=ON`. Both builds use the
third-party header facade; disabled builds do not fetch RDMA headers. The
runtime loads `libibverbs.so.1` and `librdmacm.so.1` through the platform loader's
normal search path. There is no link-time dependency on those libraries.

The native test requires an active IB or IPv4 RoCE v2 port. A configured
SoftRoCE device suffices for ownership qualification. Without an explicit device
requirement, absence of the runtime or an active device skips native tests;
setting `IREE_NET_RDMA_TEST_DEVICE` makes that absence a failure. Tests use real
registered memory and native writes, checked transformed results, and independent
connection/proactor retirement while another connection reuses unchanged MRs.
The host adapter's completion service drives these checks; native ownership
itself has no dependency on that service or its proactor.
Those checks establish software ownership, not physical NIC throughput or GPU
visibility.
