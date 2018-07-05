/*
 * written by Shin Sasaki and Kazushi Takahashi (2015)
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <gfarm/gfarm.h>

#include "gfs_rdma.h"
#include "gfutil.h"
#include "context.h"
#include "gfs_proto.h"

#if defined(HAVE_INFINIBAND) && !defined(__KERNEL__)
#include <infiniband/verbs.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <sys/resource.h>

#define gfarm_errno_to_error2(err) \
	(err) ? gfarm_errno_to_error(err) : GFARM_ERR_UNKNOWN

#define MAX_SEND_WR 1
#define MAX_RECV_WR 1
#define WR_ID_NUM 0x12345
#define RDMA_MIN_BUF 256
#define RDMA_MIN_RNR_TIMER	12
#define RDMA_TIME_OUT		20
#define RDMA_RETRY_COUNT	7
#define RDMA_RNR_RETRY		7

static int page_size;
#define roundup_page(x)	(((x) + (page_size - 1)) & ~page_size)

struct rdma_context {
	struct ibv_context *ib_ctx;

	int port;
	int sl;
	enum ibv_mtu mtu;

	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	struct ibv_mr *mr;

	gfarm_uint32_t local_lid, remote_lid;
	gfarm_uint32_t local_qpn, remote_qpn;
	gfarm_uint32_t local_psn, remote_psn;
	union ibv_gid	local_gid, remote_gid;

	void *buffer;	/* for gfsd */
	int size;
	int is_server;
	int rdma_available;
	int reg_mr_fail;
};

#define staticp	(gfarm_ctxp->ib_rdma_static)

struct gfs_ib_rdma_static {
	struct ibv_context *ib_ctx;
	uint32_t lid;
	uint32_t port;
	enum	ibv_mtu mtu;
	int	sl;
	int	local_rdma_available;
	union ibv_gid gid;
	struct ibv_device **dev_list;
	struct ibv_device *ib_dev;
	unsigned long mlock_limit;
};

static int ib_rdma_disable;

gfarm_error_t
gfs_ib_rdma_static_init(struct gfarm_context *ctxp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct gfs_ib_rdma_static *cr_ctx;

	GFARM_MALLOC(cr_ctx);
	if (cr_ctx == NULL) {
		gflog_fatal(GFARM_MSG_1004554, "%s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
		e = GFARM_ERR_NO_MEMORY;
	} else
		memset(cr_ctx, 0, sizeof(*cr_ctx));
	ctxp->ib_rdma_static = cr_ctx;
	page_size = sysconf(_SC_PAGESIZE);

	return (e);
}

void
gfs_ib_rdma_static_term(struct gfarm_context *ctxp)
{
	struct gfs_ib_rdma_static *s = ctxp->ib_rdma_static;

	if (s == NULL)
		return;

	if (s->dev_list) {
		ibv_free_device_list(s->dev_list);
	}
	if (s->ib_ctx) {
		ibv_close_device(s->ib_ctx);
	}
	free(s);
	return;
}
void
gfs_rdma_print_qp(char *com, struct ibv_qp * qp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int rc, mask;
	struct ibv_qp_attr	xa;
	struct ibv_qp_init_attr	ia;

	memset(&xa, 0, sizeof(xa));
	memset(&ia, 0, sizeof(ia));

	mask = 0
			| IBV_QP_ACCESS_FLAGS
			| IBV_QP_AV
			| IBV_QP_CUR_STATE
			| IBV_QP_DEST_QPN
			| IBV_QP_MAX_DEST_RD_ATOMIC
			| IBV_QP_MAX_QP_RD_ATOMIC
			| IBV_QP_MIN_RNR_TIMER
			| IBV_QP_PATH_MTU
			| IBV_QP_PKEY_INDEX
			| IBV_QP_PORT
			| IBV_QP_RETRY_CNT
			| IBV_QP_RNR_RETRY
			| IBV_QP_RQ_PSN
			| IBV_QP_SQ_PSN
			| IBV_QP_STATE
			| IBV_QP_TIMEOUT
	;

	if ((rc = ibv_query_qp(qp, &xa, mask, &ia))) {
		e = gfarm_errno_to_error2(rc);
		gflog_error(GFARM_MSG_1004555,
			"ibv_query_qp() failed, %s",
			gfarm_error_string(e));
	}
	gflog_info(GFARM_MSG_1004556, "%s: qp=%p "
		"qp_state=0x%x "
		"cur_qp_state=0x%x "
		"path_mtu=0x%x "
		"qp_access_flags=0x%x "
		"qkey=0x%x "
		"rq_psn=0x%x "
		"sq_psn=0x%x "
		"dest_qp_num=0x%x "
		"port_num=0x%x "
		, com, qp
		, xa.qp_state
		, xa.cur_qp_state
		, xa.path_mtu
		, xa.qp_access_flags
		, xa.qkey
		, xa.rq_psn
		, xa.sq_psn
		, xa.dest_qp_num
		, xa.port_num
	);
}
gfarm_error_t
gfs_ib_rdma_disable(void)
{
	ib_rdma_disable = 1;
	return (GFARM_ERR_NO_ERROR);
}
static unsigned long
gfs_memlock_set(void)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct rlimit rlim = {0, 0};

	if (!getrlimit(RLIMIT_MEMLOCK, &rlim)
		&& rlim.rlim_cur < rlim.rlim_max) {
		rlim.rlim_cur = rlim.rlim_max;
		if (setrlimit(RLIMIT_MEMLOCK, &rlim))
			e = gfarm_errno_to_error2(errno);
	} else
		e = gfarm_errno_to_error2(errno);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1004748, "gfs_memlock_set: %s",
			gfarm_error_string(e));
	gflog_debug(GFARM_MSG_1004557, "RLIMIT_MEMLOCK 0x%lx",
		(long) rlim.rlim_cur);
	if (rlim.rlim_cur >= (unsigned long)(LONG_MAX))
		return (LONG_MAX);
	else
		return (rlim.rlim_cur);
}
gfarm_error_t
gfs_ib_rdma_initialize(int stayopen)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct ibv_device **dev_list = NULL;
	struct ibv_context *ib_ctx = NULL;
	struct ibv_device *ib_dev;
	struct ibv_device_attr device_attr;
	struct ibv_port_attr port_attr;
	unsigned long rlim, rlimit;
	int	port, conf_port;
	char	*p, *conf_dev, *dev_name;
	int	rc;
	int	num, i;

	staticp->local_rdma_available = 0;
	if (!gfarm_ctxp->ib_rdma || ib_rdma_disable) {
		gflog_debug(GFARM_MSG_1004558, "ib_rdma not set");
		return (GFARM_ERR_NO_ERROR);
	}
	if (getenv("GFARM_RDMA_DISABLE")) {
		gflog_debug(GFARM_MSG_1004559, "GFARM_RDMA_DISABLE is set");
		return (GFARM_ERR_NO_ERROR);
	}
	rlimit = gfs_memlock_set();

#define PARA_NUM	1
#define QP_CQ_PAGE	(page_size * 4)
#define MEM_WRAP	(page_size)
	rlim = rlimit / PARA_NUM - QP_CQ_PAGE - MEM_WRAP;
	if (rlim < gfarm_ctxp->rdma_min_size) {
		gflog_error(GFARM_MSG_1004560, "insufficient memlock size"
			"(0x%lx) for rdma(0x%x), please expand memlock size.\n"
			"edit /etc/security/limits.conf	and set "
			"'* hard memlock unlimited'"
			, rlimit, gfarm_ctxp->rdma_min_size);
		return (GFARM_ERR_NO_SPACE);
	}
	if ((conf_dev = getenv("GFARM_RDMA_DEVICE")) == NULL)
		conf_dev = gfarm_ctxp->rdma_device;

	if ((p = getenv("GFARM_RDMA_PORT")) == NULL
	|| (conf_port = strtol(p, 0, 0)) < 0)
		conf_port = gfarm_ctxp->rdma_port;

	dev_list = ibv_get_device_list(&num);
	if (!dev_list) {
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004561,
			"ibv_get_device_list() failed, %s",
			gfarm_error_string(e));
		return (e);
	}
	gflog_debug(GFARM_MSG_1004562, "memlock=0x%lx, rdma_size=0x%lx",
			rlimit, rlim);

	for (i = 0; i < num; i++) {
		ib_dev = dev_list[i];
		dev_name = (char *)ibv_get_device_name(ib_dev);
		if (conf_dev != NULL && strcmp(conf_dev, dev_name)) {
			gflog_debug(GFARM_MSG_1004563,
				"device %s is not '%s'", dev_name, conf_dev);
			continue;
		}
		if (ib_dev->transport_type != IBV_TRANSPORT_IB) { /* XXX */
			gflog_debug(GFARM_MSG_1004564,
				"device %s is not suit type, node=%d xport=%d",
				dev_name, ib_dev->node_type,
				ib_dev->transport_type);
			continue;
		}
		if ((ib_ctx = ibv_open_device(ib_dev)) == NULL) {
			e = gfarm_errno_to_error2(errno);
			gflog_error(GFARM_MSG_1004565,
				"ibv_open_device(%s) failed, %s",
				dev_name, gfarm_error_string(e));
			goto cleanup;
		}
		if ((rc = ibv_query_device(ib_ctx, &device_attr)) != 0) {
			e = gfarm_errno_to_error2(rc);
			gflog_error(GFARM_MSG_1004566,
				"ibv_query_device(%s) failed, %s",
				dev_name, gfarm_error_string(e));
			goto cleanup;
		}
		for (port = 1; port <= device_attr.phys_port_cnt; ++port) {
			if (conf_port != 0 && conf_port != port) {
				gflog_debug(GFARM_MSG_1004567,
					"device %s port is not %d", dev_name,
					conf_port);
				continue;
			}
			if ((rc = ibv_query_port(ib_ctx, port, &port_attr))
				!= 0) {
				e = gfarm_errno_to_error2(rc);
				gflog_error(GFARM_MSG_1004568,
					"ibv_query_port(%s) failed, %s",
					dev_name, gfarm_error_string(e));
				goto cleanup;
			}
			if (
#ifdef IBV_LINK_LAYER_INFINIBAND
			(port_attr.link_layer != IBV_LINK_LAYER_INFINIBAND) ||
#endif
			!port_attr.lid ||
			port_attr.state != IBV_PORT_ACTIVE) {
				gflog_debug(GFARM_MSG_1004569,
					"port %d of %s is not suit state",
					port, dev_name);
				continue;
			}
			if ((rc = ibv_query_gid(ib_ctx, port, 0,
						&staticp->gid))) {
				e = gfarm_errno_to_error2(rc);
				gflog_error(GFARM_MSG_1004570,
					"ibv_query_gid(%s) failed, %s",
					dev_name, gfarm_error_string(e));
				goto cleanup;
			}
			if (stayopen) {
				staticp->ib_ctx = ib_ctx;
				ib_ctx = NULL;
			}
			staticp->dev_list = dev_list;
			dev_list = NULL;
			staticp->ib_dev = ib_dev;
			staticp->port = port;
			staticp->lid = port_attr.lid;
			staticp->mtu = port_attr.active_mtu;
			staticp->mlock_limit = rlim;
			staticp->sl = 0;
			staticp->local_rdma_available = 1;
			gflog_debug(GFARM_MSG_1004571,
				"gfs_ib_rdma_initialize:"
				"device=%s port=%d lid=%d mtu=%d",
				dev_name, port, staticp->lid, staticp->mtu);
			goto cleanup;
		}
		ibv_close_device(ib_ctx);
		ib_ctx = NULL;
	}
	gflog_error(GFARM_MSG_1004572,
		"No suitable IB device in %d devices",
		num);
cleanup:
	if (ib_ctx)
		ibv_close_device(ib_ctx);
	if (dev_list)
		ibv_free_device_list(dev_list);
	return (e);
}
gfarm_error_t
gfs_rdma_init(int is_server, struct rdma_context **ctxp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct rdma_context *ctx;
	int rc, size;

	*ctxp = NULL;
	if (!staticp->local_rdma_available || ib_rdma_disable) {
		return (GFARM_ERR_NO_ERROR);
	}
	GFARM_MALLOC(ctx);
	if (ctx == NULL) {
		gflog_fatal(GFARM_MSG_1004573, "gfs_rdma_init:%s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(ctx, 0, sizeof(*ctx));

	if (is_server)
		size = GFS_PROTO_MAX_IOSIZE;
	else if (gfarm_ctxp->rdma_mr_reg_mode & GFARM_RDMA_REG_MR_STATIC) {
		if (gfarm_ctxp->rdma_mr_reg_static_min_size <
					gfarm_ctxp->rdma_min_size) {
			gflog_warning(GFARM_MSG_1004574,
				"rdma_mr_reg_static_min_size(%d)"
				" < rdma_min_size(%d), change size",
				gfarm_ctxp->rdma_mr_reg_static_min_size,
				gfarm_ctxp->rdma_min_size);
			gfarm_ctxp->rdma_mr_reg_static_min_size =
					gfarm_ctxp->rdma_min_size;
		}
		if (gfarm_ctxp->rdma_mr_reg_static_min_size < page_size) {
			gfarm_ctxp->rdma_mr_reg_static_min_size = page_size;
		}
		size = gfarm_ctxp->rdma_mr_reg_static_min_size;
	} else
		size = RDMA_MIN_BUF;
	if (size > staticp->mlock_limit) {
		gflog_error(GFARM_MSG_1004575, "insufficient memlock size"
			"(0x%lx), please expand memlock size.\n"
			"edit /etc/security/limits.conf	and set "
			"'* hard memlock unlimited'"
			, gfs_memlock_set());
		goto clean_context;
	}
	GFARM_MALLOC_ARRAY(ctx->buffer, size);
	if (ctx->buffer == NULL) {
		gflog_fatal(GFARM_MSG_1004576, "gfs_rdma_init allocate buf:%s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto clean_context;
	}
	ctx->size = size;
	if (staticp->ib_ctx) {
		ctx->ib_ctx = staticp->ib_ctx;
	} else if ((ctx->ib_ctx = ibv_open_device(staticp->ib_dev)) == NULL) {
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004577,
			"ibv_open_device(%s) failed, %s",
			ibv_get_device_name(staticp->ib_dev),
			gfarm_error_string(e));
		goto clean_context;
	}

	ctx->rdma_available = 0;
	ctx->local_lid = staticp->lid;
	ctx->local_gid = staticp->gid;
	ctx->port = staticp->port;
	ctx->remote_lid = 0;

	ctx->pd = ibv_alloc_pd(ctx->ib_ctx);
	if (!ctx->pd) {
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004578, "ibv_alloc_pd(%p) failed, %s",
			ctx->ib_ctx, gfarm_error_string(e));
		goto clean_fd;
	}

	ctx->cq = ibv_create_cq(ctx->ib_ctx, 2, NULL, NULL, 0);
	if (!ctx->cq) {
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004579, "ibv_create_cq() failed, %s",
			gfarm_error_string(e));
		goto clean_pd;
	}

	ctx->mr = ibv_reg_mr(ctx->pd, ctx->buffer,
		size, is_server ? (IBV_ACCESS_LOCAL_WRITE)
			: (IBV_ACCESS_LOCAL_WRITE |
			IBV_ACCESS_REMOTE_WRITE |
			IBV_ACCESS_REMOTE_READ));
	if (!ctx->mr) {
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004580, "ibv_reg_mr() failed, %s",
			gfarm_error_string(e));
		goto clean_cq;
	}


	{
	struct ibv_qp_init_attr attr = {
		.send_cq = ctx->cq,
		.recv_cq = ctx->cq,
		.cap = {
			.max_send_wr = MAX_SEND_WR,
			.max_recv_wr = MAX_RECV_WR,
			.max_send_sge = 1,
			.max_recv_sge = 1
		},
		.qp_type = IBV_QPT_RC,
		.sq_sig_all = 0,
	};

	ctx->qp = ibv_create_qp(ctx->pd, &attr);
	if (!ctx->qp) {
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004581, "ibv_create_qp() failed, %s",
			gfarm_error_string(e));
		goto clean_mr;
	}
	}

	struct ibv_qp_attr attr = {
		.qp_state = IBV_QPS_INIT,
		.pkey_index = 0,
		.port_num = ctx->port,
		.qp_access_flags = IBV_ACCESS_REMOTE_WRITE
				| IBV_ACCESS_REMOTE_READ
	};

	if ((rc = ibv_modify_qp(ctx->qp, &attr,
					 IBV_QP_STATE |
					 IBV_QP_PKEY_INDEX |
					 IBV_QP_PORT |
					 IBV_QP_ACCESS_FLAGS)) != 0) {
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004582, "ibv_modify_qp() failed, %s",
			gfarm_error_string(e));
		gfs_rdma_print_qp("modify INIT fail", ctx->qp);
		goto clean_qp;
	}

	ctx->local_qpn = ctx->qp->qp_num;
	ctx->local_psn = gfarm_random() & 0xffffff;

	ctx->rdma_available = 1;
	ctx->is_server = is_server;

	*ctxp = ctx;

	return (e);

clean_qp:
	ibv_destroy_qp(ctx->qp);

clean_cq:
	ibv_destroy_cq(ctx->cq);

clean_mr:
	ibv_dereg_mr(ctx->mr);

clean_pd:
	ibv_dealloc_pd(ctx->pd);
clean_fd:
	if (!staticp->ib_ctx)
		ibv_close_device(ctx->ib_ctx);
clean_context:
	if (ctx && ctx->buffer)
		free(ctx->buffer);
	free(ctx);
	return (e);
}

gfarm_error_t
gfs_rdma_finish(struct rdma_context *ctx)
{
	int rc;

	if (!ctx) {
		return (GFARM_ERR_NO_ERROR);
	}
	if ((rc = ibv_destroy_qp(ctx->qp)) != 0) {
		gflog_debug(GFARM_MSG_1004583, "ibv_destroy_qp() failed, %s",
			gfarm_error_string(gfarm_errno_to_error2(rc)));
	}

	if ((rc = ibv_destroy_cq(ctx->cq)) != 0) {
		gflog_debug(GFARM_MSG_1004584, "ibv_destroy_cq() failed, %s",
			gfarm_error_string(gfarm_errno_to_error2(rc)));
	}

	if (ctx->mr && (rc = ibv_dereg_mr(ctx->mr)) != 0) {
		gflog_debug(GFARM_MSG_1004585, "ibv_dereg_mr() failed, %s",
			gfarm_error_string(gfarm_errno_to_error2(rc)));
	}

	if ((rc = ibv_dealloc_pd(ctx->pd)) != 0) {
		gflog_debug(GFARM_MSG_1004586, "ibv_dealloc_pd() failed, %s",
			gfarm_error_string(gfarm_errno_to_error2(rc)));
	}
	if (!staticp->ib_ctx && (rc = ibv_close_device(ctx->ib_ctx)) != 0) {
		gflog_debug(GFARM_MSG_1004587, "ibv_close_device() failed, %s",
			gfarm_error_string(gfarm_errno_to_error2(rc)));
	}

	if (ctx->buffer)
		free(ctx->buffer);
	free(ctx);

	gflog_debug(GFARM_MSG_1004588, "gfs_rdma_finish()");
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_rdma_connect(struct rdma_context *ctx)
{
	gfarm_error_t e;
	int ret;

	struct ibv_qp_attr attr = {
		.qp_state	= IBV_QPS_RTR,
		.path_mtu	= staticp->mtu,
		.dest_qp_num	= ctx->remote_qpn, /* QPN */
		.rq_psn	= ctx->remote_psn, /* PSN */
		.max_dest_rd_atomic	= 1,
		.min_rnr_timer	= RDMA_MIN_RNR_TIMER,
		.ah_attr	= {
			.is_global	= 0,
			.dlid	= ctx->remote_lid, /* LID */
			.sl	= staticp->sl,
			.src_path_bits	= 0,
			.port_num	= ctx->port,
		},
	};

	if (!gfs_rdma_check(ctx)) {
		return (GFARM_ERR_NO_ERROR);
	}

	ret = ibv_modify_qp(ctx->qp, &attr,
						IBV_QP_STATE |
						IBV_QP_AV |
						IBV_QP_PATH_MTU |
						IBV_QP_DEST_QPN |
						IBV_QP_RQ_PSN |
						IBV_QP_MAX_DEST_RD_ATOMIC |
						IBV_QP_MIN_RNR_TIMER);
	if (ret) {
		e = gfarm_errno_to_error2(ret);
		gflog_error(GFARM_MSG_1004589, "Failed to modify QP to RTR, %s",
			gfarm_error_string(e));
		gfs_rdma_print_qp("modify RTR fail", ctx->qp);
		gfs_rdma_disable(ctx);
		return (e);
	}

	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = RDMA_TIME_OUT;
	attr.retry_cnt = RDMA_RETRY_COUNT;
	attr.rnr_retry = RDMA_RNR_RETRY;
	attr.sq_psn = ctx->local_psn;
	attr.max_rd_atomic = 1;

	ret = ibv_modify_qp(ctx->qp, &attr,
						IBV_QP_STATE |
						IBV_QP_TIMEOUT |
						IBV_QP_RETRY_CNT |
						IBV_QP_RNR_RETRY |
						IBV_QP_SQ_PSN |
						IBV_QP_MAX_QP_RD_ATOMIC);
	if (ret) {
		e = gfarm_errno_to_error2(ret);
		gflog_error(GFARM_MSG_1004590, "Failed to modify QP to RTS, %s",
			gfarm_error_string(e));
		gfs_rdma_disable(ctx);
		gfs_rdma_print_qp("modify RTS fail", ctx->qp);
		return (e);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_rdma_reg_mr_remote_read_write(struct rdma_context *ctx,
		void *buffer, ssize_t size, void **mrp)
{
	if (!ctx) {
		gflog_error(GFARM_MSG_1004591, "No rdma_context");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (ctx->reg_mr_fail > 0) {
		*mrp = NULL;
		return (GFARM_ERR_NO_MEMORY);
	}
	if (ctx->mr &&
		!(gfarm_ctxp->rdma_mr_reg_mode & GFARM_RDMA_REG_MR_STATIC)) {
		ibv_dereg_mr(ctx->mr);
		ctx->mr = NULL;
	}

	*mrp = ibv_reg_mr(ctx->pd, buffer, size, IBV_ACCESS_LOCAL_WRITE |
						IBV_ACCESS_REMOTE_WRITE |
						IBV_ACCESS_REMOTE_READ);

	if (!*mrp) {
		gfarm_error_t e;
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004592, "ibv_reg_mr() failed, %s",
			gfarm_error_string(e));
		ctx->reg_mr_fail = GFARM_RDMA_REG_MR_FAIL;
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_rdma_dereg_mr(struct rdma_context *ctx, void **mrp)
{
	int rc;

	if (!ctx || !*mrp) {
		gflog_error(GFARM_MSG_1004593, "No rdma_context");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if ((rc = ibv_dereg_mr(*mrp)) != 0) {
		gfarm_error_t e;
		e = gfarm_errno_to_error2(rc);
		return (e);
	}
	*mrp = NULL;

	return (GFARM_ERR_NO_ERROR);
}

static int
rdma_wait_completion(struct rdma_context *ctx, enum ibv_wc_opcode op)
{
	int ret;
	struct ibv_wc wc;

	while ((ret = ibv_poll_cq(ctx->cq, 1, &wc)) == 0)
		;

	if (ret < 0) {
		gfarm_error_t e;
		ret = errno;
		e = gfarm_errno_to_error2(errno);
		gflog_error(GFARM_MSG_1004594, "ibv_poll_cq() failed, %s",
			gfarm_error_string(e));
		return (ret);
	}

	if (wc.status != IBV_WC_SUCCESS) {
		gflog_debug(GFARM_MSG_1004595, "Completion error: %s",
				ibv_wc_status_str(wc.status));
		return (-1);
	}

	if (wc.wr_id != (uint64_t)((uintptr_t)ctx)) {
		gflog_debug(GFARM_MSG_1004596, "ibv_poll_cq() wr_id unmatch");
		return (-1);
	}
	if (wc.opcode != op) {
		gflog_debug(GFARM_MSG_1004597, "ibv_poll_cq() opcode unmatch");
		return (-1);
	}

	return (0);
}

gfarm_error_t
gfs_rdma_remote_write(struct rdma_context *ctx, gfarm_uint32_t rkey,
			gfarm_uint64_t remote_addr, ssize_t remote_size)
{
	gfarm_error_t e;
	int rc;
	struct ibv_sge list = {
		.addr = (uintptr_t)ctx->buffer,
		.length = remote_size,
		.lkey = ctx->mr->lkey
	};

	struct ibv_send_wr wr = {
		.wr_id = (uint64_t)((uintptr_t) ctx),
		.sg_list = &list,
		.num_sge = 1,
		.opcode = IBV_WR_RDMA_WRITE,
		.send_flags = IBV_SEND_SIGNALED,
		.wr.rdma.remote_addr = (uintptr_t)remote_addr,
		.wr.rdma.rkey = rkey,
	};

	struct ibv_send_wr *bad_wr;

	if (remote_size > ctx->size) {
		gflog_error(GFARM_MSG_1004598, "Too big data %ld",
			(long)remote_size);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if ((rc = ibv_post_send(ctx->qp, &wr, &bad_wr)) != 0) {
		e = gfarm_errno_to_error2(rc);
		gflog_error(GFARM_MSG_1004599, "ibv_post_send() failed, %s",
			gfarm_error_string(e));
		return (e);
	}

	if (rdma_wait_completion(ctx, IBV_WC_RDMA_WRITE)) {
		gflog_error(GFARM_MSG_1004600, "rdma_wait_completion() failed");
		return (GFARM_ERR_UNKNOWN);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_rdma_remote_read(struct rdma_context *ctx, gfarm_uint32_t rkey,
		 gfarm_uint64_t remote_addr, ssize_t remote_size)
{
	gfarm_error_t e;
	int rc;
	struct ibv_sge list = {
		.addr = (uintptr_t)ctx->buffer,
		.length = remote_size,
		.lkey = ctx->mr->lkey
	};

	struct ibv_send_wr wr = {
		.wr_id = (uint64_t) ((uintptr_t)ctx),
		.sg_list = &list,
		.num_sge = 1,
		.opcode = IBV_WR_RDMA_READ,
		.send_flags = IBV_SEND_SIGNALED,
		.wr.rdma.remote_addr = (uintptr_t)remote_addr,
		.wr.rdma.rkey = rkey,
	};
	struct ibv_send_wr *bad_wr;

	if (remote_size > ctx->size) {
		gflog_error(GFARM_MSG_1004601, "Too big data %ld",
				(long)remote_size);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if ((rc = ibv_post_send(ctx->qp, &wr, &bad_wr)) != 0) {
		e = gfarm_errno_to_error2(rc);
		gflog_error(GFARM_MSG_1004602, "ibv_post_send() failed, %s",
			gfarm_error_string(e));
		return (e);
	}

	if (rdma_wait_completion(ctx, IBV_WC_RDMA_READ)) {
		gflog_error(GFARM_MSG_1004603, "rdma_wait_completion() failed");
		return (GFARM_ERR_UNKNOWN);
	}

	return (GFARM_ERR_NO_ERROR);
}

void
gfs_rdma_set_remote_lid(struct rdma_context *ctx, gfarm_uint32_t lid)
{
	ctx->remote_lid = lid;
}

void
gfs_rdma_set_remote_qpn(struct rdma_context *ctx, gfarm_uint32_t qpn)
{
	ctx->remote_qpn = qpn;
}

void
gfs_rdma_set_remote_psn(struct rdma_context *ctx, gfarm_uint32_t psn)
{
	ctx->remote_psn = psn;
}
void
gfs_rdma_set_remote_gid(struct rdma_context *ctx, void *buf)
{
	memcpy(&ctx->remote_gid, buf, sizeof(union ibv_gid));
}
void *
gfs_rdma_get_remote_gid(struct rdma_context *ctx)
{
	return (&ctx->remote_gid);
}

gfarm_uint32_t
gfs_rdma_get_local_lid(struct rdma_context *ctx)
{
	return (ctx->local_lid);
}

gfarm_uint32_t
gfs_rdma_get_local_qpn(struct rdma_context *ctx)
{
	return (ctx->local_qpn);
}

gfarm_uint32_t
gfs_rdma_get_local_psn(struct rdma_context *ctx)
{
	return (ctx->local_psn);
}
int
gfs_rdma_resize_buffer(struct rdma_context *ctx, int size)
{
	struct ibv_mr *mr;
	char *buf;

	if (ctx->reg_mr_fail > 0)
		return (ctx->size);

	size = roundup_page(size);
	if (size <= ctx->size)
		return (ctx->size);

	if (size >= gfarm_ctxp->rdma_mr_reg_static_max_size) {
		size = gfarm_ctxp->rdma_mr_reg_static_max_size;
		if (size <= ctx->size)
			return (ctx->size);
	}

	GFARM_MALLOC_ARRAY(buf, size);
	if (buf == NULL) {
		gflog_error(GFARM_MSG_1004604, "gfs_rdma_resize_buffer"
			"fail size=%d", size);
		return (ctx->size);
	}
	mr = ibv_reg_mr(ctx->pd, buf, size,
		ctx->is_server ? (IBV_ACCESS_LOCAL_WRITE)
			: (IBV_ACCESS_LOCAL_WRITE |
			IBV_ACCESS_REMOTE_WRITE |
			IBV_ACCESS_REMOTE_READ));
	if (!mr) {
		gflog_error(GFARM_MSG_1004605, "ibv_reg_mr() failed, %s",
			gfarm_error_string(gfarm_errno_to_error2(errno)));
		free(buf);
		ctx->reg_mr_fail = GFARM_RDMA_REG_MR_FAIL;
		return (ctx->size);
	}
	ibv_dereg_mr(ctx->mr);
	ctx->mr = mr;
	free(ctx->buffer);
	gflog_debug(GFARM_MSG_1004606, "gfs_rdma_resize_buffer:resize 0x%x->%x",
		ctx->size, size);
	ctx->buffer = buf;
	ctx->size = size;

	return (ctx->size);
}
unsigned char *
gfs_rdma_get_buffer(struct rdma_context *ctx)
{
	return (ctx->buffer);
}
int
gfs_rdma_get_bufsize(struct rdma_context *ctx)
{
	return (ctx->size);
}
int
gfs_rdma_get_bufinfo(struct rdma_context *ctx, void **bufp,
	int *sizep, gfarm_uint32_t *rkeyp)
{
	*bufp = ctx->buffer;
	*sizep = ctx->size;
	if (ctx->mr)
		*rkeyp = ctx->mr->rkey;
	else
		*rkeyp = 0;

	return (ctx->reg_mr_fail);
}
unsigned long
gfs_rdma_get_mlock_limit(void)
{
	return (staticp->mlock_limit);
}
struct ibv_mr *
gfs_rdma_get_mr(struct rdma_context *ctx)
{
	return (ctx->mr);
}
void *
gfs_rdma_get_local_gid(struct rdma_context *ctx)
{
	return (&ctx->local_gid);
}
int
gfs_rdma_get_gid_size(void)
{
	return (sizeof(union ibv_gid));
}

gfarm_uint32_t
gfs_rdma_get_rkey(struct ibv_mr *mr)
{
	return (mr->rkey);
}

gfarm_uint64_t
gfs_rdma_get_addr(struct rdma_context *ctx)
{
	return ((gfarm_uint64_t)((uintptr_t)ctx->buffer));
}

void
gfs_rdma_enable(struct rdma_context *ctx)
{
	ctx->rdma_available = 1;
}

void
gfs_rdma_disable(struct rdma_context *ctx)
{
	if (ctx)
		ctx->rdma_available = 0;
}

int
gfs_rdma_check(struct rdma_context *ctx)
{
	return (ctx && ctx->rdma_available);
}
#else
gfarm_error_t
gfs_ib_rdma_static_init(struct gfarm_context *ctxp)
{
	return (GFARM_ERR_NO_ERROR);
}
void
gfs_ib_rdma_static_term(struct gfarm_context *ctxp)
{
}
gfarm_error_t
gfs_ib_rdma_disable(void)
{
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfs_rdma_init(int is_server, struct rdma_context **ctx)
{
	*ctx = NULL;
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}
gfarm_error_t
gfs_rdma_finish(struct rdma_context *ctx)
{
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfs_rdma_connect(struct rdma_context *ctx)
{
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfs_rdma_reg_mr_remote_read_write(struct rdma_context *ctx,
			void *buffer, ssize_t size, void **mrp)
{
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfs_rdma_dereg_mr(struct rdma_context *ctx, void **mrp)
{
	return (GFARM_ERR_NO_ERROR);
}
void
gfs_rdma_set_remote_lid(struct rdma_context *ctx, gfarm_uint32_t lid)
{
}
void
gfs_rdma_set_remote_qpn(struct rdma_context *ctx, gfarm_uint32_t qpn)
{
}
void
gfs_rdma_set_remote_psn(struct rdma_context *ctx, gfarm_uint32_t psn)
{
}
void
gfs_rdma_set_remote_gid(struct rdma_context *ctx, void *buf)
{
}
void *
gfs_rdma_get_remote_gid(struct rdma_context *ctx)
{
	return (NULL);
}
gfarm_uint32_t
gfs_rdma_get_local_lid(struct rdma_context *ctx)
{
	return (0);
}
gfarm_uint32_t
gfs_rdma_get_local_qpn(struct rdma_context *ctx)
{
	return (0);
}
gfarm_uint32_t
gfs_rdma_get_local_psn(struct rdma_context *ctx)
{
	return (0);
}
void *gfs_rdma_get_local_gid(struct rdma_context *ctx)
{
	return ("");
}
gfarm_uint32_t
gfs_rdma_get_rkey(struct ibv_mr *mr)
{
	return (0);
}
gfarm_uint64_t
gfs_rdma_get_addr(struct rdma_context *ctx)
{
	return (0);
}
int
gfs_rdma_resize_buffer(struct rdma_context *ctx, int size)
{
	return (0);
}
unsigned char *
gfs_rdma_get_buffer(struct rdma_context *ctx)
{
	return ((unsigned char *) "");
}
int
gfs_rdma_get_bufsize(struct rdma_context *ctx)
{
	return (0);
}
int
gfs_rdma_get_bufinfo(struct rdma_context *ctx, void **bufp,
	int *sizep, gfarm_uint32_t *rkeyp)
{
	return (0);
}
unsigned long
gfs_rdma_get_mlock_limit(void)
{
	return (0);
}
struct ibv_mr *
gfs_rdma_get_mr(struct rdma_context *ctx)
{
	return (NULL);
}

int
gfs_rdma_get_gid_size(void)
{
	return (0);
}
void
gfs_rdma_enable(struct rdma_context *ctx)
{
}

void
gfs_rdma_disable(struct rdma_context *ctx)
{
}

int
gfs_rdma_check(struct rdma_context *ctx)
{
	return (0);
}
gfarm_error_t
gfs_rdma_remote_write(struct rdma_context *ctx,
	gfarm_uint32_t rkey, gfarm_uint64_t remote_addr, ssize_t remote_size)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}
gfarm_error_t
gfs_rdma_remote_read(struct rdma_context *ctx,
	gfarm_uint32_t rkey, gfarm_uint64_t remote_addr, ssize_t remote_size)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}
#endif
