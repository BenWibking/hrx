// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstdlib>

#include "iree/net/carrier/rdma/factory.h"
#include "iree/net/cts/transport_backend.h"
#include "iree/net/rdma/region.h"

namespace iree::net::cts {
namespace {

iree_status_t CreateContext(iree_allocator_t host_allocator,
                            iree_net_rdma_context_t** out_context) {
  if (!std::getenv("IREE_NET_RDMA_CM_TEST_ADDRESS")) {
    return iree_make_status(IREE_STATUS_UNAVAILABLE,
                            "set IREE_NET_RDMA_CM_TEST_ADDRESS and "
                            "IREE_NET_RDMA_CM_TEST_DEVICE for native RDMA CTS");
  }
  auto options = iree_net_rdma_context_options_default();
  options.device_name =
      iree_make_cstring_view(std::getenv("IREE_NET_RDMA_CM_TEST_DEVICE"));
  iree_status_t status =
      iree_net_rdma_context_create(options, host_allocator, out_context);
  if (iree_status_is_unavailable(status)) {
    return iree_status_join(
        iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                         "explicitly requested RDMA context is unavailable"),
        status);
  }
  return status;
}

iree_status_t CreateFactory(iree_allocator_t host_allocator,
                            iree_net_transport_factory_t** out_factory) {
  *out_factory = nullptr;
  iree_net_rdma_context_t* context = nullptr;
  IREE_RETURN_IF_ERROR(CreateContext(host_allocator, &context));
  iree_status_t status = iree_net_rdma_factory_create(
      context, nullptr, host_allocator, out_factory);
  iree_net_rdma_context_release(context);
  return status;
}

iree_status_t CreateRegisteredFactory(
    iree_async_slab_t* slab, iree_allocator_t host_allocator,
    iree_net_transport_factory_t** out_factory,
    iree_async_region_t** out_region) {
  *out_factory = nullptr;
  *out_region = nullptr;
  iree_net_rdma_context_t* context = nullptr;
  IREE_RETURN_IF_ERROR(CreateContext(host_allocator, &context));
  auto options = iree_net_rdma_factory_options_default();
#if defined(IREE_NET_RDMA_CTS_MAX_REQUEST_LENGTH)
  options.connection.direct.max_request_length =
      IREE_NET_RDMA_CTS_MAX_REQUEST_LENGTH;
#endif
#if defined(IREE_NET_RDMA_CTS_POST_BATCH_SIZE)
  options.connection.direct.post_batch_size = IREE_NET_RDMA_CTS_POST_BATCH_SIZE;
#endif
  iree_status_t status = iree_net_rdma_factory_create(
      context, &options, host_allocator, out_factory);
  if (iree_status_is_ok(status)) {
    status = iree_net_rdma_region_register_slab(
        context, slab, UINT64_C(0x10000000),
        IREE_ASYNC_BUFFER_ACCESS_FLAG_READ |
            IREE_ASYNC_BUFFER_ACCESS_FLAG_WRITE |
            IREE_ASYNC_BUFFER_ACCESS_FLAG_REMOTE_WRITE,
        host_allocator, out_region);
  }
  if (!iree_status_is_ok(status)) {
    iree_net_transport_factory_release(*out_factory);
    *out_factory = nullptr;
  }
  iree_net_rdma_context_release(context);
  return status;
}

iree_status_t MakeBindAddress(std::string* out_address) {
  const char* address = std::getenv("IREE_NET_RDMA_CM_TEST_ADDRESS");
  if (!address) {
    return iree_make_status(
        IREE_STATUS_UNAVAILABLE,
        "set IREE_NET_RDMA_CM_TEST_ADDRESS for native RDMA CTS");
  }
  *out_address = address;
  return iree_ok_status();
}

}  // namespace

const TransportBackend& GetTransportBackend() {
  static const TransportBackend backend = {
      "rdma",
      IREE_NET_TRANSPORT_CAPABILITY_RELIABLE |
          IREE_NET_TRANSPORT_CAPABILITY_ORDERED,
      CreateFactory,
      MakeBindAddress,
      CreateRegisteredFactory,
  };
  return backend;
}

}  // namespace iree::net::cts
