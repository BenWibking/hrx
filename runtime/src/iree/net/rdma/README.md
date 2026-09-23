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

The host adapter's CM service in
`runtime/src/iree/net/carrier/rdma/connection_events.h` owns a native event
channel and its proactor monitor, while callers own the connection IDs. It
acknowledges native records before dispatching borrowed snapshots so an owner
can migrate an accepted ID or destroy a rejected ID inside its callback.
Channel deactivation joins monitoring separately from native connection
retirement; observing a disconnect event alone does not establish DMA
retirement.

CM qualification requires a local IP address routed through the selected RDMA
device. Port zero selects an available listening port:

```sh
iree-bazel-test --config=asan \
  --test_env=IREE_NET_RDMA_CM_TEST_DEVICE=<device-name> \
  --test_env=IREE_NET_RDMA_CM_TEST_ADDRESS=<local-IP>:0 \
  //runtime/src/iree/net/carrier/rdma:connection_events_test
```

Without the address, this test skips. With it, connection or device failures
fail the test. The test establishes real connections, migrates accepted IDs
away from their listener, destroys the listener, and checks bidirectional
registered-memory transfers before completing the disconnect handshake. It
also rejects connections and retires their IDs directly from event callbacks.
Both io_uring and POSIX polling exercise these paths with bounded event batches.

`runtime/src/iree/net/carrier/rdma/connection_route.h` captures an established
IB/RoCE connection's native route for independently owned SEND/WRITE data QPs.
Each data QP uses its own exchanged queue number and packet sequence numbers;
the control QP's identity and READ/atomic resources are not inherited. Native
context/registration ownership remains separate from this host setup policy.

The native integration test exchanges those identities over the actual control
QP, checks data and transformed results on two derived QPs, and retires one
while retaining its target bytes. It also removes both control CM owners before
probing data liveness: the data QPs still work and require explicit retirement.
This is a teardown test, not permission for a public connection to keep accepting
work after control failure. Control disconnect alone never returns outstanding
data ownership.

## Host Connection Control

`runtime/src/iree/net/carrier/rdma/connection_control.h` composes those services
for one host connection. It owns a private control QP and registered record
storage, and supplies a CQ shared with the connection's independently owned
data QPs. Control receives are consumed and replenished internally; application
leases and retained targets cannot hold that capacity. CQ and CM readiness may
arrive in either order, so records received during setup remain in their original
registered storage until the establishment callback has returned.

The control QP is explicitly configured using CM-resolved attributes before
the peer can use it. This permits an explicit RNR retry delay instead of the
CM default encoding zero, which means about 655 ms. Retry spacing controls
native backpressure, not connection failure detection or a teardown deadline.
The shared native context and registration layer impose no host retry policy.

Cold construction allocates and registers storage without starting callbacks.
Connect/accept then begins native setup on the caller's poll owner. A failure
after one monitor is armed follows the same asynchronous drain as a connected
peer: retire independent data QPs, destroy native control access, join both
monitors and the retirement callback, then release ownership. Native destruction
establishes quiescence without waiting for a guessed number of flush completions.
No private worker or synchronous polling loop participates in this lifecycle.

`runtime/src/iree/net/carrier/rdma/connection_control_test.cc` uses the same CM
qualification environment above. It checks bounded bilateral control pressure,
an isolated final record, actual independent data placement and transformed
results through the shared CQ, retained target bytes after control retirement,
and cancellation/allocation failure on both sides of connection setup.
