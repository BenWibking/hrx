// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Included with IREE_NET_RDMA_SYMBOL(library, result, name, arguments).

// Canonical CM inventory preserves context identity across connections.
IREE_NET_RDMA_SYMBOL(cm, struct ibv_context**, rdma_get_devices, (int*))
// Releases one canonical inventory reference.
IREE_NET_RDMA_SYMBOL(cm, void, rdma_free_devices, (struct ibv_context**))

// Returns a borrowed device name for explicit selection.
IREE_NET_RDMA_SYMBOL(verbs, const char*, ibv_get_device_name,
                     (struct ibv_device*))
// Queries creation-time device limits.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_query_device,
                     (struct ibv_context*, struct ibv_device_attr*))
// Compatibility symbol used by the extended port-query wrapper.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_query_port,
                     (struct ibv_context*, uint8_t,
                      struct _compat_ibv_port_attr*))
// Queries all valid GIDs with an explicit entry stride.
IREE_NET_RDMA_SYMBOL(verbs, ssize_t, _ibv_query_gid_table,
                     (struct ibv_context*, struct ibv_gid_entry*, size_t,
                      uint32_t, size_t))
// Creates an independently owned protection domain.
IREE_NET_RDMA_SYMBOL(verbs, struct ibv_pd*, ibv_alloc_pd, (struct ibv_context*))
// Releases a domain after its queues and registrations have retired.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_dealloc_pd, (struct ibv_pd*))

// Registration uses an explicit NIC address, independent of a CPU mapping.
IREE_NET_RDMA_SYMBOL(verbs, struct ibv_mr*, ibv_reg_mr_iova2,
                     (struct ibv_pd*, void*, size_t, uint64_t, unsigned int))
// Deregisters memory after every native access has joined.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_dereg_mr, (struct ibv_mr*))

// Creates a pollable completion-notification channel.
IREE_NET_RDMA_SYMBOL(verbs, struct ibv_comp_channel*, ibv_create_comp_channel,
                     (struct ibv_context*))
// Releases a channel after its CQs and poll monitors have retired.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_destroy_comp_channel,
                     (struct ibv_comp_channel*))
// Creates a bounded native completion queue.
IREE_NET_RDMA_SYMBOL(verbs, struct ibv_cq*, ibv_create_cq,
                     (struct ibv_context*, int, void*, struct ibv_comp_channel*,
                      int))
// Releases a CQ after its work and notifications have retired.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_destroy_cq, (struct ibv_cq*))
// Consumes a channel notification, separately from its CQ entries.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_get_cq_event,
                     (struct ibv_comp_channel*, struct ibv_cq**, void**))
// Returns ownership of consumed channel notifications.
IREE_NET_RDMA_SYMBOL(verbs, void, ibv_ack_cq_events,
                     (struct ibv_cq*, unsigned int))
// Creates a native queue pair in an explicit protection domain.
IREE_NET_RDMA_SYMBOL(verbs, struct ibv_qp*, ibv_create_qp,
                     (struct ibv_pd*, struct ibv_qp_init_attr*))
// Configures or transitions a queue pair, including terminal shutdown.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_modify_qp,
                     (struct ibv_qp*, struct ibv_qp_attr*, int))
// Destroys a queue pair and retires its native access.
IREE_NET_RDMA_SYMBOL(verbs, int, ibv_destroy_qp, (struct ibv_qp*))

#undef IREE_NET_RDMA_SYMBOL
