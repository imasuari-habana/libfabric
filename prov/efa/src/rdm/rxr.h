/*
 * Copyright (c) 2019-2022 Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#ifndef _RXR_H_
#define _RXR_H_

#include <pthread.h>
#include <rdma/fabric.h>
#include <rdma/fi_atomic.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_trigger.h>
#include <rdma/fi_ext.h>

#include <ofi.h>
#include <ofi_iov.h>
#include <ofi_proto.h>
#include <ofi_enosys.h>
#include <ofi_rbuf.h>
#include <ofi_list.h>
#include <ofi_util.h>
#include <ofi_tree.h>
#include <uthash.h>
#include <ofi_recvwin.h>
#include <ofi_perf.h>
#include <ofi_hmem.h>

#include "efa_base_ep.h"
#include "rxr_pkt_type.h"
#include "rxr_op_entry.h"
#include "rxr_env.h"

#define RXR_FI_VERSION		OFI_VERSION_LATEST

#ifdef ENABLE_EFA_POISONING
static inline void rxr_poison_mem_region(void *ptr, size_t size)
{
	uint32_t rxr_poison_value = 0xdeadbeef;
	for (int i = 0; i < size / sizeof(rxr_poison_value); i++)
		memcpy((uint32_t *)ptr + i, &rxr_poison_value, sizeof(rxr_poison_value));
}

static inline void rxr_poison_pkt_entry(struct rxr_pkt_entry *pkt_entry, size_t wiredata_size)
{
	rxr_poison_mem_region(pkt_entry->wiredata, wiredata_size);
	/*
	 * Don't poison pkt_entry->wiredata, which is the last element in rxr_pkt_entry
	 * pkt_entry->wiredata is released when the pkt_entry is released
	 */
	rxr_poison_mem_region(pkt_entry, sizeof(struct rxr_pkt_entry) - sizeof(char *));
}
#endif

/*
 * Set alignment to x86 cache line size.
 */
#define RXR_BUF_POOL_ALIGNMENT	(64)

/*
 * will add following parameters to env variable for tuning
 */
#define RXR_DEF_CQ_SIZE			(8192)
#define RXR_REMOTE_CQ_DATA_LEN		(8)

/* the default value for rxr_env.rnr_backoff_wait_time_cap */
#define RXR_DEFAULT_RNR_BACKOFF_WAIT_TIME_CAP	(1000000)

/*
 * the maximum value for rxr_env.rnr_backoff_wait_time_cap
 * Because the backoff wait time is multiplied by 2 when
 * RNR is encountered, its value must be < INT_MAX/2.
 * Therefore, its cap must be < INT_MAX/2 too.
 */
#define RXR_MAX_RNR_BACKOFF_WAIT_TIME_CAP	(INT_MAX/2 - 1)

/* bounds for random RNR backoff timeout */
#define RXR_RAND_MIN_TIMEOUT		(40)
#define RXR_RAND_MAX_TIMEOUT		(120)

/* bounds for flow control */
#define RXR_DEF_MIN_TX_CREDITS		(32)

/*
 * maximum time (microseconds) we will allow available_bufs for large msgs to
 * be exhausted
 */
#define RXR_AVAILABLE_DATA_BUFS_TIMEOUT	(5000000)

#if ENABLE_DEBUG
#define RXR_TX_PKT_DBG_SIZE	(16384)
#define RXR_RX_PKT_DBG_SIZE	(16384)
#endif

/*
 * Based on size of tx_id and rx_id in headers, can be arbitrary once those are
 * removed.
 */
#define RXR_MAX_RX_QUEUE_SIZE (UINT32_MAX)
#define RXR_MAX_TX_QUEUE_SIZE (UINT32_MAX)

/*
 * The maximum supported source address length in bytes
 */
#define RXR_MAX_NAME_LENGTH	(32)

/*
 * RxR specific flags that are sent over the wire.
 */
#define RXR_TAGGED		BIT_ULL(0)
#define RXR_REMOTE_CQ_DATA	BIT_ULL(1)
#define RXR_REMOTE_SRC_ADDR	BIT_ULL(2)

/*
 * TODO: In future we will send RECV_CANCEL signal to sender,
 * to stop transmitting large message, this flag is also
 * used for fi_discard which has similar behavior.
 */
#define RXR_RECV_CANCEL		BIT_ULL(3)

/*
 * Flags to tell if the rx_entry is tracking FI_MULTI_RECV buffers
 */
#define RXR_MULTI_RECV_POSTED	BIT_ULL(4)
#define RXR_MULTI_RECV_CONSUMER	BIT_ULL(5)

/*
 * Flag to tell if the transmission is using FI_DELIVERY_COMPLETE
 * protocols
 */

#define RXR_DELIVERY_COMPLETE_REQUESTED	BIT_ULL(6)

#define RXR_OP_ENTRY_QUEUED_RNR BIT_ULL(9)

/*
 * Flag to indicate an rx_entry has an EOR
 * in flight (the EOR has been sent or queued,
 * and has not got send completion)
 * hence the rx_entry cannot be released
 */
#define RXR_EOR_IN_FLIGHT BIT_ULL(10)

/*
 * Flag to indicate a tx_entry has already
 * written an cq error entry for RNR
 */
#define RXR_TX_ENTRY_WRITTEN_RNR_CQ_ERR_ENTRY BIT_ULL(10)

/*
 * Flag to indicate an op_entry has queued ctrl packet,
 * and is on ep->op_entry_queued_ctrl_list
 */
#define RXR_OP_ENTRY_QUEUED_CTRL BIT_ULL(11)

/*
 * OFI flags
 * The 64-bit flag field is used as follows:
 * 1-grow up    common (usable with multiple operations)
 * 59-grow down operation specific (used for single call/class)
 * 60 - 63      provider specific
 */
#define RXR_NO_COMPLETION	BIT_ULL(60)
#define RXR_NO_COUNTER		BIT_ULL(61)

/*
 * RM flags
 */
#define RXR_RM_TX_CQ_FULL	BIT_ULL(0)
#define RXR_RM_RX_CQ_FULL	BIT_ULL(1)

#define RXR_MTU_MAX_LIMIT	BIT_ULL(15)

enum ibv_cq_ex_type {
	IBV_CQ,
	EFADV_CQ
};

extern struct fi_provider rxr_prov;
extern struct fi_fabric_attr rxr_fabric_attr;
extern struct util_prov rxr_util_prov;
enum rxr_lower_ep_type {
	EFA_EP = 1,
	SHM_EP,
};

/** @brief Information of a queued copy.
 *
 * This struct is used when receiving buffer is on device.
 * Under such circumstance, batching a series copies to
 * do them at the same time can avoid memory barriers between
 * copies, and improve performance.
 */
struct rxr_queued_copy {
	struct rxr_pkt_entry *pkt_entry;
	char *data;
	size_t data_size;
	size_t data_offset;
};

#define RXR_EP_MAX_QUEUED_COPY (8)

struct rxr_ep {
	struct efa_base_ep base_ep;

	uint8_t core_addr[RXR_MAX_NAME_LENGTH];
	size_t core_addrlen;

	/* per-version extra feature/request flag */
	uint64_t extra_info[RXR_MAX_NUM_EXINFO];

	struct ibv_cq_ex *ibv_cq_ex;

	enum ibv_cq_ex_type ibv_cq_ex_type;

	/* shm provider fid */
	bool use_shm_for_tx;
	struct fid_ep *shm_ep;
	struct fid_cq *shm_cq;

	/*
	 * RxR rx/tx queue sizes. These may be different from the core
	 * provider's rx/tx size and will either limit the number of possible
	 * receives/sends or allow queueing.
	 */
	size_t rx_size;
	size_t tx_size;
	size_t mtu_size;
	size_t rx_iov_limit;
	size_t tx_iov_limit;
	size_t inject_size;

	/* core's capabilities */
	uint64_t core_caps;

	/* Endpoint's capability to support zero-copy rx */
	bool use_zcpy_rx;

	/* Application requested resource management support */
	int handle_resource_management;

	/* rx/tx queue size of core provider */
	size_t core_rx_size;
	size_t efa_max_outstanding_tx_ops;
	size_t core_inject_size;
	size_t max_data_payload_size;

	/* Resource management flag */
	uint64_t rm_full;

	/* application's ordering requirements */
	uint64_t msg_order;
	/* core's supported tx/rx msg_order */
	uint64_t core_msg_order;

	/* Application's maximum msg size hint */
	size_t max_msg_size;

	/* Applicaiton's message prefix size. */
	size_t msg_prefix_size;

	/* RxR protocol's max header size */
	size_t max_proto_hdr_size;

	/* tx iov limit of core provider */
	size_t core_iov_limit;

	/* threshold to release multi_recv buffer */
	size_t min_multi_recv_size;

	/* buffer pool for send & recv */
	struct rxr_pkt_pool *efa_tx_pkt_pool;
	struct rxr_pkt_pool *efa_rx_pkt_pool;

	/*
	 * buffer pool for send & recv for shm as mtu size is different from
	 * the one of efa, and do not require local memory registration
	 */
	struct rxr_pkt_pool *shm_tx_pkt_pool;
	struct rxr_pkt_pool *shm_rx_pkt_pool;

	/* staging area for unexpected and out-of-order packets */
	struct rxr_pkt_pool *rx_unexp_pkt_pool;
	struct rxr_pkt_pool *rx_ooo_pkt_pool;

	/* staging area for read copy */
	struct rxr_pkt_pool *rx_readcopy_pkt_pool;
	int rx_readcopy_pkt_pool_used;
	int rx_readcopy_pkt_pool_max_used;

	/* datastructure to maintain rxr send/recv states */
	struct ofi_bufpool *op_entry_pool;
	/* data structure to maintain read */
	struct ofi_bufpool *read_entry_pool;
	/* data structure to maintain pkt rx map */
	struct ofi_bufpool *map_entry_pool;
	/* rxr medium message pkt_entry to rx_entry map */
	struct rxr_pkt_rx_map *pkt_rx_map;
	/*
	 * buffer pool for atomic response data, used by
	 * emulated fetch and compare atomic.
	 */
	struct ofi_bufpool *rx_atomrsp_pool;
	/* rx_entries with recv buf */
	struct dlist_entry rx_list;
	/* rx_entries without recv buf (unexpected message) */
	struct dlist_entry rx_unexp_list;
	/* rx_entries with tagged recv buf */
	struct dlist_entry rx_tagged_list;
	/* rx_entries without tagged recv buf (unexpected message) */
	struct dlist_entry rx_unexp_tagged_list;
	/* list of pre-posted recv buffers */
	struct dlist_entry rx_posted_buf_list;
	/* list of pre-posted recv buffers for shm */
	struct dlist_entry rx_posted_buf_shm_list;
	/* op entries with queued rnr packets */
	struct dlist_entry op_entry_queued_rnr_list;
	/* op entries with queued ctrl packets */
	struct dlist_entry op_entry_queued_ctrl_list;
	/* tx/rx_entries used by long CTS msg/write/read protocol
         * which have data to be sent */
	struct dlist_entry op_entry_longcts_send_list;
	/* read entries with data to be read */
	struct dlist_entry read_pending_list;
	/* list of #efa_rdm_peer that are in backoff due to RNR */
	struct dlist_entry peer_backoff_list;
	/* list of #efa_rdm_peer that will retry posting handshake pkt */
	struct dlist_entry handshake_queued_peer_list;

#if ENABLE_DEBUG
	/* tx/rx_entries waiting to receive data in 
         * long CTS msg/read/write protocols */
	struct dlist_entry op_entry_recv_list;
	/* counter tracking op_entry_recv_list */
	size_t pending_recv_counter;

	/* rx packets being processed or waiting to be processed */
	struct dlist_entry rx_pkt_list;

	/* tx packets waiting for send completion */
	struct dlist_entry tx_pkt_list;

	size_t efa_total_posted_tx_ops;
	size_t shm_total_posted_tx_ops;
	size_t send_comps;
	size_t failed_send_comps;
	size_t recv_comps;
#endif
	/* track allocated rx_entries and tx_entries for endpoint cleanup */
	struct dlist_entry rx_entry_list;
	struct dlist_entry tx_entry_list;

	/*
	 * number of posted RX packets for shm
	 */
	size_t shm_rx_pkts_posted;
	/*
	 * number of RX packets to be posted by progress engine for shm.
	 * It exists because posting RX packets by bulk is more efficient.
	 */
	size_t shm_rx_pkts_to_post;
	/*
	 * number of posted RX packets for EFA device
	 */
	size_t efa_rx_pkts_posted;
	/*
	 * Number of RX packets to be posted by progress engine for EFA device.
	 * It exists because posting RX packets by bulk is more efficient.
	 */
	size_t efa_rx_pkts_to_post;

	/* number of outstanding tx ops on efa device */
	size_t efa_outstanding_tx_ops;
	/* number of outstanding tx ops on shm */
	size_t shm_outstanding_tx_ops;

	struct rxr_queued_copy queued_copy_vec[RXR_EP_MAX_QUEUED_COPY];
	int queued_copy_num;
	int blocking_copy_rx_entry_num; /* number of RX entries that are using gdrcopy/cudaMemcpy */

	int	hmem_p2p_opt; /* what to do for hmem transfers */
};

int rxr_ep_flush_queued_blocking_copy_to_hmem(struct rxr_ep *ep);

#define rxr_rx_flags(rxr_ep) ((rxr_ep)->base_ep.util_ep.rx_op_flags)
#define rxr_tx_flags(rxr_ep) ((rxr_ep)->base_ep.util_ep.tx_op_flags)

/*
 * Control header with completion data. CQ data length is static.
 */
#define RXR_CQ_DATA_SIZE (8)

static inline void rxr_copy_shm_cq_entry(struct fi_cq_tagged_entry *cq_tagged_entry,
					 struct fi_cq_data_entry *shm_cq_entry)
{
	cq_tagged_entry->op_context = shm_cq_entry->op_context;
	cq_tagged_entry->flags = shm_cq_entry->flags;
	cq_tagged_entry->len = shm_cq_entry->len;
	cq_tagged_entry->buf = shm_cq_entry->buf;
	cq_tagged_entry->data = shm_cq_entry->data;
	cq_tagged_entry->tag = 0; /* No tag for RMA; */

}

struct efa_ep_addr *rxr_ep_raw_addr(struct rxr_ep *ep);

const char *rxr_ep_raw_addr_str(struct rxr_ep *ep, char *buf, size_t *buflen);

struct efa_ep_addr *rxr_ep_get_peer_raw_addr(struct rxr_ep *ep, fi_addr_t addr);

const char *rxr_ep_get_peer_raw_addr_str(struct rxr_ep *ep, fi_addr_t addr, char *buf, size_t *buflen);

struct efa_rdm_peer *rxr_ep_get_peer(struct rxr_ep *ep, fi_addr_t addr);

void rxr_tx_entry_init(struct rxr_ep *rxr_ep, struct rxr_op_entry *tx_entry,
		       const struct fi_msg *msg, uint32_t op, uint64_t flags);

struct rxr_op_entry *rxr_ep_alloc_tx_entry(struct rxr_ep *rxr_ep,
					   const struct fi_msg *msg,
					   uint32_t op,
					   uint64_t tag,
					   uint64_t flags);

void rxr_release_tx_entry(struct rxr_ep *ep, struct rxr_op_entry *tx_entry);

struct rxr_op_entry *rxr_ep_alloc_rx_entry(struct rxr_ep *ep,
					   fi_addr_t addr, uint32_t op);

static inline void rxr_release_rx_entry(struct rxr_ep *ep,
					struct rxr_op_entry *rx_entry)
{
	struct rxr_pkt_entry *pkt_entry;
	struct dlist_entry *tmp;

	if (rx_entry->peer)
		dlist_remove(&rx_entry->peer_entry);

	dlist_remove(&rx_entry->ep_entry);

	if (!dlist_empty(&rx_entry->queued_pkts)) {
		dlist_foreach_container_safe(&rx_entry->queued_pkts,
					     struct rxr_pkt_entry,
					     pkt_entry, entry, tmp) {
			rxr_pkt_entry_release_tx(ep, pkt_entry);
		}
		dlist_remove(&rx_entry->queued_rnr_entry);
	}

	if (rx_entry->rxr_flags & RXR_OP_ENTRY_QUEUED_CTRL)
		dlist_remove(&rx_entry->queued_ctrl_entry);

#ifdef ENABLE_EFA_POISONING
	rxr_poison_mem_region(rx_entry,
			      sizeof(struct rxr_op_entry));
#endif
	rx_entry->state = RXR_OP_FREE;
	ofi_buf_free(rx_entry);
}

void rxr_ep_record_tx_op_submitted(struct rxr_ep *ep, struct rxr_pkt_entry *pkt_entry);

void rxr_ep_record_tx_op_completed(struct rxr_ep *ep, struct rxr_pkt_entry *pkt_entry);

static inline size_t rxr_get_rx_pool_chunk_cnt(struct rxr_ep *ep)
{
	return MIN(ep->core_rx_size, ep->rx_size);
}

static inline size_t rxr_get_tx_pool_chunk_cnt(struct rxr_ep *ep)
{
	return MIN(ep->efa_max_outstanding_tx_ops, ep->tx_size);
}

static inline int rxr_need_sas_ordering(struct rxr_ep *ep)
{
	return ep->msg_order & FI_ORDER_SAS;
}

static inline int rxr_ep_use_zcpy_rx(struct rxr_ep *ep, struct fi_info *info)
{
	return !(ep->base_ep.util_ep.caps & FI_DIRECTED_RECV) &&
		!(ep->base_ep.util_ep.caps & FI_TAGGED) &&
		!(ep->base_ep.util_ep.caps & FI_ATOMIC) &&
		(ep->max_msg_size <= ep->mtu_size - ep->max_proto_hdr_size) &&
		!rxr_need_sas_ordering(ep) &&
		info->mode & FI_MSG_PREFIX &&
		rxr_env.use_zcpy_rx;
}

/* Initialization functions */
int rxr_get_lower_rdm_info(uint32_t version, const char *node, const char *service,
			   uint64_t flags, const struct util_prov *util_prov,
			   const struct fi_info *util_hints,
			   struct fi_info **core_info);
int rxr_cq_open(struct fid_domain *domain, struct fi_cq_attr *attr,
		struct fid_cq **cq_fid, void *context);
int rxr_endpoint(struct fid_domain *domain, struct fi_info *info,
		 struct fid_ep **ep, void *context);

/* EP sub-functions */
void rxr_ep_progress(struct util_ep *util_ep);
void rxr_ep_progress_internal(struct rxr_ep *rxr_ep);

int rxr_ep_post_user_recv_buf(struct rxr_ep *ep, struct rxr_op_entry *rx_entry,
			      uint64_t flags);

struct efa_rdm_peer;

int rxr_ep_determine_rdma_support(struct rxr_ep *ep, fi_addr_t addr,
				  struct efa_rdm_peer *peer);

void rxr_convert_desc_for_shm(int numdesc, void **desc);

void rxr_prepare_desc_send(struct efa_domain *efa_domain,
			   struct rxr_op_entry *tx_entry);

struct rxr_op_entry *rxr_ep_lookup_mediumrtm_rx_entry(struct rxr_ep *ep,
						      struct rxr_pkt_entry *pkt_entry);

void rxr_ep_record_mediumrtm_rx_entry(struct rxr_ep *ep,
				      struct rxr_pkt_entry *pkt_entry,
				      struct rxr_op_entry *rx_entry);

/* CQ sub-functions */
void rxr_cq_queue_rnr_pkt(struct rxr_ep *ep,
			  struct dlist_entry *list,
			  struct rxr_pkt_entry *pkt_entry);

void rxr_cq_handle_shm_completion(struct rxr_ep *ep,
				  struct fi_cq_data_entry *cq_entry,
				  fi_addr_t src_addr);

void rxr_cq_handle_shm_rma_write_data(struct rxr_ep *ep,
				      struct fi_cq_data_entry *shm_comp,
				      fi_addr_t src_addr);

/* Aborts if unable to write to the eq */
static inline void efa_eq_write_error(struct util_ep *ep, ssize_t err,
				      ssize_t prov_errno)
{
	struct fi_eq_err_entry err_entry;
	int ret = -FI_ENOEQ;

	FI_WARN(&rxr_prov, FI_LOG_EQ,
		"Writing error to EQ: err: %s (%zd) prov_errno: %s (%zd)\n",
		fi_strerror(err), err,
		efa_strerror(prov_errno), prov_errno);
	if (ep->eq) {
		memset(&err_entry, 0, sizeof(err_entry));
		err_entry.err = err;
		err_entry.prov_errno = prov_errno;
		ret = fi_eq_write(&ep->eq->eq_fid, FI_NOTIFY,
				  &err_entry, sizeof(err_entry),
				  UTIL_FLAG_ERROR);

		if (ret == sizeof(err_entry))
			return;
	}

	FI_WARN(&rxr_prov, FI_LOG_EQ, "Unable to write to EQ\n");
	fprintf(stderr,
		"Libfabric EFA provider has encounterd an internal error:\n\n"
		"Libfabric error: (%zd) %s\n"
		"EFA internal error: (%zd) %s\n\n"
		"Your application will now abort().\n",
		err, fi_strerror(err),
		prov_errno, efa_strerror(prov_errno));
	abort();
}

static inline struct efa_domain *rxr_ep_domain(struct rxr_ep *ep)
{
	return container_of(ep->base_ep.util_ep.domain, struct efa_domain, util_domain);
}

/*
 * today we have only cq res check, in future we will have ctx, and other
 * resource check as well.
 */
static inline uint64_t is_tx_res_full(struct rxr_ep *ep)
{
	return ep->rm_full & RXR_RM_TX_CQ_FULL;
}

static inline uint64_t is_rx_res_full(struct rxr_ep *ep)
{
	return ep->rm_full & RXR_RM_RX_CQ_FULL;
}

static inline void rxr_rm_rx_cq_check(struct rxr_ep *ep, struct util_cq *rx_cq)
{
	ofi_genlock_lock(&rx_cq->cq_lock);
	if (ofi_cirque_isfull(rx_cq->cirq))
		ep->rm_full |= RXR_RM_RX_CQ_FULL;
	else
		ep->rm_full &= ~RXR_RM_RX_CQ_FULL;
	ofi_genlock_unlock(&rx_cq->cq_lock);
}

static inline void rxr_rm_tx_cq_check(struct rxr_ep *ep, struct util_cq *tx_cq)
{
	ofi_genlock_lock(&tx_cq->cq_lock);
	if (ofi_cirque_isfull(tx_cq->cirq))
		ep->rm_full |= RXR_RM_TX_CQ_FULL;
	else
		ep->rm_full &= ~RXR_RM_TX_CQ_FULL;
	ofi_genlock_unlock(&tx_cq->cq_lock);
}

/*
 * @brief: check whether we should use p2p for this transaction
 *
 * @param[in]	ep	rxr_ep
 * @param[in]	efa_mr	memory registration struct
 *
 * @return: 0 if p2p should not be used, 1 if it should, and negative FI code
 * if the transfer should fail.
 */
static inline int rxr_ep_use_p2p(struct rxr_ep *rxr_ep, struct efa_mr *efa_mr)
{
	if (!efa_mr)
		return 0;

	/*
	 * always send from host buffers if we have a descriptor
	 */
	if (efa_mr->peer.iface == FI_HMEM_SYSTEM)
		return 1;

	if (rxr_ep_domain(rxr_ep)->hmem_info[efa_mr->peer.iface].p2p_supported_by_device)
		return (rxr_ep->hmem_p2p_opt != FI_HMEM_P2P_DISABLED);

	if (rxr_ep->hmem_p2p_opt == FI_HMEM_P2P_REQUIRED) {
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
			 "Peer to peer support is currently required, but not available.\n");
		return -FI_ENOSYS;
	}

	return 0;
}

static inline ssize_t
efa_rdm_ep_post_flush(struct rxr_ep *ep, struct ibv_send_wr **bad_wr)
{
	ssize_t ret;

#if HAVE_LTTNG
	struct ibv_send_wr *head = ep->base_ep.xmit_more_wr_head.next;

	while (head) {
		efa_tracing(post_send, (void *) head->wr_id);
		head = head->next;
	}
#endif

	ret = ibv_post_send(ep->base_ep.qp->ibv_qp, ep->base_ep.xmit_more_wr_head.next, bad_wr);
	ep->base_ep.xmit_more_wr_head.next = NULL;
	ep->base_ep.xmit_more_wr_tail = &ep->base_ep.xmit_more_wr_head;
	return ret;
}

#endif
