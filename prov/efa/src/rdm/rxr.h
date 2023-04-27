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
#include "efa_prov.h"
#include "efa_base_ep.h"
#include "rxr_pkt_type.h"
#include "efa_rdm_ope.h"
#include "efa_env.h"
#include "efa_rdm_ep.h"

/*
 * The CUDA memory alignment
 */
#define EFA_RDM_CUDA_MEMORY_ALIGNMENT (64)

/*
 * The alignment to support in-order aligned ops.
 */
#define EFA_RDM_IN_ORDER_ALIGNMENT (128)

/*
 * Set alignment to x86 cache line size.
 */
#define RXR_BUF_POOL_ALIGNMENT	(64)

/* Aborts if unable to write to the eq */
static inline void efa_eq_write_error(struct util_ep *ep, ssize_t err,
				      ssize_t prov_errno)
{
	struct fi_eq_err_entry err_entry;
	int ret = -FI_ENOEQ;

	EFA_WARN(FI_LOG_EQ, "Writing error to EQ: err: %s (%zd) prov_errno: %s (%zd)\n",
	         fi_strerror(err), err, efa_strerror(prov_errno, NULL), prov_errno);
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

	EFA_WARN(FI_LOG_EQ, "Unable to write to EQ\n");
	fprintf(stderr,
		"Libfabric EFA provider has encountered an internal error:\n\n"
		"Libfabric error: (%zd) %s\n"
		"EFA internal error: (%zd) %s\n\n"
		"Your application will now abort().\n",
		err, fi_strerror(err),
		prov_errno, efa_strerror(prov_errno, NULL));
	abort();
}

#endif
