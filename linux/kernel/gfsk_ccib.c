#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/wait.h>                 /* wait_queue_head_t, etc */
#include <linux/spinlock.h>             /* spinlock_t, etc */
#include <linux/interrupt.h>		/* tasklet_schecdule */
#include <linux/timex.h>		/* get_cycles */
#include <linux/cpufreq.h>		/* cpufreq_quick_get */
#include <asm/atomic.h>                 /* atomic_t, etc */
#include <gfarm/gfarm.h>
#ifdef RDMA_IB_VERBS_H
#include RDMA_IB_VERBS_H
#else
#include <rdma/ib_verbs.h>
#endif
#include "gfsk.h"
#include "gfsk_fs.h"
#include "gfsk_ccutil.h"
#include "gfsk_ccib.h"
#include "gfsk_stack.h"
#include "gfsk_proc.h"

struct gfcc_ctx;
struct gfcc_ib_device {
	struct list_head	list;
	struct ib_device	*device;
	int			port_cnt;
	spinlock_t		spinlock;	/* protect the above */
	atomic_t		refcount;
	struct work_struct	free_work;
};

static struct gfcc_ib_device *gfcc_open_device(struct ib_device *device,
		struct gfcc_ctx *ctx);
static void gfcc_close_device(struct gfcc_ib_device *gidev,
		struct gfcc_ctx *ctx);
static struct ib_device *gfcc_ib_find_dev(char *ib_devname, int *port);

static inline uint64_t
gfib_dma_map_single(struct ib_device *dev,
	    void *cpu_addr, size_t size, enum dma_data_direction direction)
{
	uint64_t a;

	a = ib_dma_map_single(dev, cpu_addr, size, direction);
	if (ib_dma_mapping_error(dev, a)) {
		gflog_error(GFARM_MSG_1004820, "ib_dma_map_single fail");
		a = 0;
	}
	return (a);
}
static inline uint64_t
gfib_dma_map_page(struct ib_device *dev, struct page *page,
	  unsigned long offset, size_t size, enum dma_data_direction direction)
{
	uint64_t a;

	a = ib_dma_map_page(dev, page, offset, size, direction);
	if (ib_dma_mapping_error(dev, a)) {
		gflog_error(GFARM_MSG_1004821, "ib_dma_map_page fail");
		a = 0;
	}
	return (a);
}
static inline void
gfib_dma_unmap_single(struct ib_device *dev,
	    uint64_t addr, size_t size, enum dma_data_direction dir)
{
	ib_dma_unmap_single(dev, addr, size, dir);
}
static inline void
gfib_dma_unmap_page(struct ib_device *dev,
	    uint64_t addr, size_t size, enum dma_data_direction dir)
{
	ib_dma_unmap_page(dev, addr, size, dir);

}
/* ---------------------------------- */
static char*
pr_sge(char *buf, struct ib_send_wr *wr)
{
	int n;
	n = sprintf(buf, "wr_id=%lx num_sge=%d flag=%x sge0:%lx %d %x",
			(long)wr->wr_id, wr->num_sge, wr->send_flags,
			 (long)wr->sg_list[0].addr,
			 wr->sg_list[0].length,
			 wr->sg_list[0].lkey);
	if (wr->num_sge > 1)
		n = sprintf(buf + n, " sge1:%lx %d %x ",
				 (long)wr->sg_list[1].addr,
				 wr->sg_list[1].length,
				 wr->sg_list[1].lkey);
	return (buf);
}
static int
pr_ibaddr(char *buf, int len, struct gfcc_ibaddr *ibaddr)
{
	int n;

	n = snprintf(buf, len, "lid=%d qpn=%d psn=%d qkey=%d sl=%d\n",
		ibaddr->ca_lid,
		ibaddr->ca_qpn,
		ibaddr->ca_psn,
		ibaddr->ca_qkey,
		ibaddr->ca_sl);
	return (n);
}
static int
pr_cc_obj(char *buf, int len, struct gfcc_obj  *obj)
{
	int n;

	n = snprintf(buf, len, "ino=%ld gen=%ld off=0x%lx len=0x%lx ",
		obj->co_ino, obj->co_gen, obj->co_off, obj->co_len);
	return (n);
}
static int
rd_ibaddr(char *buf, int len, struct gfcc_ibaddr *ibaddr)
{
	int n;
	int	lid;
	int	qpn;
	int	psn;
	int	qkey;
	int	sl;

	n = sscanf(buf, "lid=%d qpn=%d psn=%d qkey=%d sl=%d",
			&lid, &qpn, &psn, &qkey, &sl);

	if (n == 5) {
		ibaddr->ca_lid = lid;
		ibaddr->ca_qpn = qpn;
		ibaddr->ca_psn = psn;
		ibaddr->ca_qkey = qkey;
		ibaddr->ca_sl = sl;
		return (5);
	}
	return (n ? -EINVAL : 0);
}
/* ---------------------------------- */
static inline int
gfib_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr, int mask)
{
	int	err;
	err = ib_modify_qp(qp, attr, mask);
	if (err) {
		gflog_error(GFARM_MSG_1004822,
			"Failed modify QP mask=0x%x err=%d\n",
			mask, err);
	}
	return (err);
}
static inline int
gfib_post_send(struct ib_qp *qp, struct ib_send_wr *wr)
{
	struct ib_send_wr *bad_wr;
	int err;
	char buf[256];

	gflog_verbose(GFARM_MSG_1004823, "ib_post_send:%s", pr_sge(buf, wr));
	err = ib_post_send(qp, wr, &bad_wr);
	if (err) {
		gflog_error(GFARM_MSG_1004824, "ib_post_send:err=%d %s",
			err, pr_sge(buf, wr));
	}
	return (err);
}
static inline int
gfib_post_recv(struct ib_qp *qp, struct ib_recv_wr *wr)
{
	struct ib_recv_wr *bad_wr;
	char buf[128];
	int err;

	gflog_verbose(GFARM_MSG_1004825, "ib_post_recv:%s",
		pr_sge(buf, (struct ib_send_wr *)wr));
	err = ib_post_recv(qp, wr, &bad_wr);
	if (err) {
		gflog_error(GFARM_MSG_1004826, "ib_post_recv:err=%d %s"
			, err, pr_sge(buf, (struct ib_send_wr *)wr));
	}
	return (err);
}
static inline struct ib_ah *
gfib_create_ah(struct ib_pd *pd, struct ib_ah_attr *attr)
{
	struct ib_ah *ah;
	ah = ib_create_ah(pd, attr);
	if (IS_ERR(ah))
		ah = NULL;
	if (!ah) {
		gflog_error(GFARM_MSG_1004827, "create_ah");
	}
	return (ah);
}
static inline int
gfib_destroy_ah(struct ib_ah *ah)
{
	int	err;
	err = ib_destroy_ah(ah);
	if (err) {
		gflog_error(GFARM_MSG_1004828, "destroy_ah err=%d", err);
	}
	return (err);
}
static inline struct ib_qp *
gfib_create_qp(struct ib_pd *pd, struct ib_qp_init_attr *attr)
{
	struct ib_qp *qp;
	qp = ib_create_qp(pd, attr);
	if (IS_ERR(qp))
		qp = NULL;
	if (!qp) {
		gflog_error(GFARM_MSG_1004829, "create_qp fail");
	}
	return (qp);
}
static inline int
gfib_destroy_qp(struct ib_qp *qp)
{
	int	err;
	err = ib_destroy_qp(qp);
	if (err) {
		gflog_error(GFARM_MSG_1004830, "destroy_qp err=%d", err);
	}
	return (err);
}
static inline int
gfib_destroy_cq(struct ib_cq *cq)
{
	int	err;
	err = ib_destroy_cq(cq);
	if (err) {
		gflog_error(GFARM_MSG_1004831, "destroy_cq err=%d", err);
	}
	return (err);
}
static inline int
gfib_req_notify_cq(struct ib_cq *cq, int flags)
{
	int	err;
	err = ib_req_notify_cq(cq, flags);
	if (err) {
		gflog_error(GFARM_MSG_1004832, "req_notify_cq err=%d", err);
	}
	return (err);
}
static inline int
gfib_query_port(struct gfcc_ib_device *context, uint8_t port_num,
		struct ib_port_attr *port_attr)
{
	int err;
	err = ib_query_port(context->device, port_num, port_attr);
	if (err) {
		gflog_error(GFARM_MSG_1004833, "query_port %d", err);
	}
	return (err);
}
static inline int
gfib_query_gid(struct gfcc_ib_device *context, uint8_t port_num,
		int index, union ib_gid *gid)
{
	int err;
#if defined(IB_QUERY_GID_ARG) && IB_QUERY_GID_ARG == 5
	err = ib_query_gid(context->device, port_num, index, gid, NULL);
#else
	err = ib_query_gid(context->device, port_num, index, gid);
#endif
	if (err) {
		gflog_error(GFARM_MSG_1004834, "query_gid %d", err);
	}
	return (err);
}
static inline struct ib_pd*
gfib_alloc_pd(struct gfcc_ib_device *context)
{
	struct ib_pd *pd;
	pd = ib_alloc_pd(context->device);
	if (IS_ERR(pd))
		pd = NULL;
	if (!pd) {
		gflog_error(GFARM_MSG_1004835, "ib_alloc_pd fail");
	}
	return (pd);
}
static inline int
gfib_dereg_mr(struct ib_mr *mr)
{
	int err;
	err = ib_dereg_mr(mr);
	if (err) {
		gflog_error(GFARM_MSG_1004836, "dereg_mr err=%d", err);
	}
	return (err);
}
static inline int
gfib_dealloc_pd(struct ib_pd *pd)
{
	int err;
	err = ib_dealloc_pd(pd);
	if (err) {
		gflog_error(GFARM_MSG_1004837, "dealloc_pd err=%d", err);
	}
	return (err);
}
static struct gfcc_ib_device *
gfib_open_device(struct ib_device *device, struct gfcc_ctx *ctx)
{
	struct gfcc_ib_device *context;
	context = gfcc_open_device(device, ctx);
	if (!context) {
		gflog_error(GFARM_MSG_1004838, "open_device fail");
	}
	return (context);
}
static inline int
gfib_close_device(struct gfcc_ib_device *context, struct gfcc_ctx *ctx)
{
	int err = 0;
	gfcc_close_device(context, ctx);
	if (err) {
		gflog_error(GFARM_MSG_1004839, "close_device err=%d", err);
	}
	return (err);
}
/* ------------------------------------------------------------------------ */
static void gfcc_ib_reap_task(struct work_struct *work);
static void gfcc_ib_recv_task(struct work_struct *work);
static void gfcc_ib_send_task(struct work_struct *work);
static void gfcc_ib_recv_comp(struct ib_cq *cq, void *ctx_ptr);
static void gfcc_ib_send_comp(struct ib_cq *cq, void *ctx_ptr);
static LIST_HEAD(gfcc_ib_devices);
static DEFINE_SPINLOCK(gfcc_ib_devices_lock);
static struct workqueue_struct *gfcc_wq;

void
gfcc_ib_gidev_get(struct gfcc_ib_device *gidev)
{
	atomic_inc(&gidev->refcount);
}
void
gfcc_ib_gidev_put(struct gfcc_ib_device *gidev)
{
	BUG_ON(atomic_read(&gidev->refcount) <= 0);
	if (atomic_dec_and_test(&gidev->refcount))
		queue_work(gfcc_wq, &gidev->free_work);
}
static struct gfcc_ib_device*
gfcc_ib_find_gidev(char *name, int *port)
{
	struct gfcc_ib_device *gidev;

	spin_lock(&gfcc_ib_devices_lock);
	list_for_each_entry(gidev, &gfcc_ib_devices, list) {
		if (name && strcmp(gidev->device->name, name))
			continue;
		if (name && *port && *port >= gidev->port_cnt)
			continue;
		if (!name || !*port)
			*port = gidev->port_cnt;
		gfcc_ib_gidev_get(gidev);
		spin_unlock(&gfcc_ib_devices_lock);
		return (gidev);
	}
	spin_unlock(&gfcc_ib_devices_lock);
	return (NULL);
}
static struct ib_device*
gfcc_ib_find_dev(char *name, int *port)
{
	struct gfcc_ib_device *gidev;
	struct ib_device *dev = NULL;

	if ((gidev = gfcc_ib_find_gidev(name, port))) {
		dev = gidev->device;
		gfcc_ib_gidev_put(gidev);
	}
	return (dev);
}
static void
gfcc_ib_dev_free(struct work_struct *work)
{
	struct gfcc_ib_device *gibdev = container_of(work,
					struct gfcc_ib_device, free_work);
	kfree(gibdev);
}
static struct gfcc_ib_device *
gfcc_open_device(struct ib_device *device, struct gfcc_ctx *ctx)
{
	int port = 0;

	return (gfcc_ib_find_gidev(device->name, &port));
}
static void
gfcc_close_device(struct gfcc_ib_device *gidev, struct gfcc_ctx *ctx)
{
	gfcc_ib_gidev_put(gidev);
}
static void gfcc_ib_add_one(struct ib_device *device);
static void gfcc_ib_remove_one(struct ib_device *device);
static struct ib_client gfcc_ib_client = {
	.name   = "gfcc",
	.add    = gfcc_ib_add_one,
	.remove = gfcc_ib_remove_one
};

static void
gfcc_ib_add_one(struct ib_device *device)
{
	struct gfcc_ib_device *gidev;

	/* Only handle IB (no iWARP) devices */
	if (device->node_type != RDMA_NODE_IB_CA) {
		printk(KERN_INFO "gfcc_ib_add_one:%p %s node_type=%x, not %x."
#ifdef NOIB_INCLUDE
		"Possibly, new include file for IB modules may be necessity."
#endif
		"\n",
		device, device->name, device->node_type, RDMA_NODE_IB_CA);
		return;
	}

	gidev = kzalloc(sizeof(struct gfcc_ib_device), GFP_KERNEL);
	if (!gidev) {
		printk(KERN_ERR"gfcc_ib_add_one:no mem\n");
		return;
	}

	spin_lock_init(&gidev->spinlock);
	atomic_set(&gidev->refcount, 1);

	gidev->device = device;
	gidev->port_cnt = device->phys_port_cnt;

	spin_lock(&gfcc_ib_devices_lock);
	list_add_tail_rcu(&gidev->list, &gfcc_ib_devices);
	spin_unlock(&gfcc_ib_devices_lock);
	atomic_inc(&gidev->refcount);

	ib_set_client_data(device, &gfcc_ib_client, gidev);
	atomic_inc(&gidev->refcount);

	INIT_WORK(&gidev->free_work, gfcc_ib_dev_free);

	printk(KERN_DEBUG"gfcc_ib_add_one:add %s\n", device->name);
	gfcc_ib_gidev_put(gidev);
}

struct gfcc_ib_device *
gfcc_ib_get_client_data(struct ib_device *device)
{
	struct gfcc_ib_device *gidev;

	rcu_read_lock();
	gidev = ib_get_client_data(device, &gfcc_ib_client);
	if (gidev)
		atomic_inc(&gidev->refcount);
	rcu_read_unlock();
	return (gidev);
}

static void
gfcc_ib_remove_one(struct ib_device *device)
{
	struct gfcc_ib_device *gidev;

	gidev = ib_get_client_data(device, &gfcc_ib_client);
	if (!gidev)
		return;

#if 0
	gfcc_ib_dev_shutdown(gidev);
#endif

	ib_set_client_data(device, &gfcc_ib_client, NULL);

	spin_lock(&gfcc_ib_devices_lock);
	list_del_rcu(&gidev->list);
	spin_unlock(&gfcc_ib_devices_lock);

	/*
	 * This synchronize rcu is waiting for readers of both the ib
	 * client data and the devices list to finish before we drop
	 * both of those references.
	 */
	synchronize_rcu();
	gfcc_ib_gidev_put(gidev);
	gfcc_ib_gidev_put(gidev);
}

int
gfcc_ib_init(void)
{
	int err;

	if ((err = ib_register_client(&gfcc_ib_client))) {
		printk(KERN_ERR "gfcc_init_module:ib_register_client fail %d\n",
			err);
		goto end_err;
	}
	if (!(gfcc_wq = create_singlethread_workqueue("gfcc"))) {
		err = -ENOMEM;
		printk(KERN_ERR "gfcc_init_module:create_singlethread fail %d\n",
			err);
	}
end_err:
	return (err);
}
void
gfcc_ib_fini(void)
{
	ib_unregister_client(&gfcc_ib_client);
	if (gfcc_wq)
		destroy_workqueue(gfcc_wq);
}

/* ------------------------------------------------------------------------ */
static int page_size = PAGE_SIZE;
struct gfcc_rpc;
#define GFCC_UD_HEAD	16
#define GFCC_UD_GRH	40
#define GFCC_UD_MTU  2048

#define GFCC_RPC_LISTEN		1
#define GFCC_RPC_READ_REQ	2
#define GFCC_RPC_READ_RES	3

#define GFCC_RPC2_VER		"gfcc1"
struct gfcc_rpc2_req {
	uint8_t		r2_ver[8];
	uint64_t	r2_id;
	struct gfcc_obj	r2_obj;
	uint32_t	r2_qkey;	/* UD: define AP each other */
	uint32_t	r2_psn;	/* RC: Packet Serial Number */
	uint32_t	r2_cmd;
	uint32_t	r2_psize;	/* KB / page */
	int	r2_npage;	/* requested page */
	int	r2_npblk;
	struct gfcc_pblk	r2_pblk[1];
};
#define GFCC_PBLK_MAX  \
	((GFCC_UD_MTU - GFCC_UD_GRH - sizeof(struct gfcc_rpc2_req)) \
	/ sizeof(struct gfcc_pblk) + 1)

#define GFCC_RPC_VER	"GFCCrpc"
struct gfcc_rpc2_res {
	uint8_t		r2_ver[8];
	uint64_t	r2_id;
	int		r2_err;	/* gfarm_error */
	int		r2_npage;	/* returned page */
	int		r2_nseg;
};

#define GFCC_RPCST_INIT		1
#define GFCC_RPCST_WAIT		2
#define GFCC_RPCST_ASYNC	3
#define GFCC_RPCST_DONE		100

#define GFCC_RPCFL_RESET	1
struct gfcc_rpc3_req {
	GFCC_MUTEX_T	r3_lock;
	GFCC_COND_T	r3_wait;
	int16_t		r3_status;
	int16_t		r3_flags;
	int		r3_cmd;
	int		r3_err;
	int		r3_done;
	struct ib_qp	*r3_qp;
	int		(*r3_cb)(struct gfcc_rpc *, struct ib_wc *);
	uint64_t	r3_dma_page[GFCC_PAGES_MAX];
	int16_t		r3_nsend;
	int16_t		r3_nrecv;
	int16_t		r3_nwr;
};
/* ---------------------------------- */
struct gfcc_rpc4_req {
	uint64_t	r4_rid;
	int		r4_status;
	int		r4_err;
	struct gfcc_ibaddr *r4_ibaddr;
	struct gfcc_obj *r4_obj;
	int		(*r4_cb)(struct gfcc_rpc *rpc, int err);
	int		r4_cmd;
	int		r4_npage;	/* # page cache */
	struct gfcc_pages *r4_pages;
};
struct gfcc_rpcres {
	char r_grhbuf[GFCC_UD_GRH];
	struct gfcc_rpc2_res rpc2_res;
};
#define GFCC_TM_SEND_REQ	0	/* create request - receive responce */
#define GFCC_TM_RECV_IB		1	/* send request - receive reply */
#define GFCC_TM_RECV_REQ	2	/* receive request - send responce */
#define GFCC_TM_MAX		3

#define GFCC_RPC_MAGIC		"gfcc1"
struct gfcc_rpc {
	struct list_head	r_list;
	long		r_timeout;
	uint8_t		r_magic[8];
	atomic_t	r_ref;
	struct gfcc_ctx *r_ctx;
	struct gfcc_rpc3_req	r_rpc3;
	struct gfcc_rpc4_req	r_rpc4;
	struct gfcc_rpc2_req	*r_rpc2;
	uint64_t	r_reqdma;
	uint64_t	r_resdma;
	char		*r_reqbuf;
	char		*r_resbuf;
	char		q[GFCC_UD_MTU];
	struct gfcc_rpcres a;
	uint64_t	r_time[GFCC_TM_MAX];

};
#define gfcc_rpc_get(rpc)	atomic_inc(&rpc->r_ref)
#define gfcc_rpc_put(rpc)	\
		do {if (rpc && atomic_dec_and_test(&(rpc)->r_ref)) \
				gfcc_rpc_free(rpc); } while (0)
#define GFCC_HOST_MAX	128

#define GFCC_ST_INIT	0
#define GFCC_ST_ACTIVE	1
#define GFCC_ST_STOPPING	2
#define GFCC_ST_DOWN	3

struct gfcc_ctx {
	GFCC_MUTEX_T		ib_lock;
	struct gfcc_ib_device	*context;
	struct ib_pd		*pd;
	struct ib_mr		*mr;
	struct ib_cq		*scq;
	struct ib_cq		*rcq;
	struct ib_qp		*qp;
	struct gfcc_stack	rpc_pool;
	struct list_head	rpc_list;
	struct {
		struct ib_ah		*ah;
		struct gfcc_ibaddr	ibaddr;
	} peer[GFCC_HOST_MAX];
	struct gfcc_ibaddr	my_ibaddr;
	struct gfcc_param	*param;
	int			status;
	int			iosize;
	int			send_sge;
	struct gfsk_procop	proc_ibaddr;
	struct gfsk_procop	proc_stat;
	struct gfsk_fs_context *fs_ctxp;

	struct workqueue_struct   *workqueue;
	struct work_struct	recv_task;
	struct work_struct	send_task;
	struct delayed_work	reap_dwork;
	unsigned long		tojiffies;
	atomic64_t	r_err;
	atomic64_t	r_recv_req;
	atomic64_t	r_recv_res;
	atomic64_t	r_recv_err;
	atomic64_t	r_recv_0;
	atomic64_t	r_recv_less;
	atomic64_t	r_send_req;
	atomic64_t	r_send_res;
	atomic64_t	r_send_err;
	atomic64_t	r_time[GFCC_TM_MAX];
	atomic64_t	r_send_page;
	atomic64_t	r_recv_page;
	atomic64_t	r_req_page;
	int	inx;
};
static void gfcc_rpc_free(struct gfcc_rpc *rpc);
static int rpc3_listen(struct gfcc_ctx *ctx);
static int rpc3_recv_read_req(struct gfcc_rpc *rpc, struct gfcc_ibaddr *ibaddr);
static int rpc3_post_listen(struct gfcc_ctx *ctx);
static int gfcc_prstat(char *buf, int size, struct gfcc_ctx *ctx);

static void
gfcc_wc_status_pr(struct ib_wc *wc, int *errp, char *msg)
{
	char str[16];
	sprintf(str, "%d", wc->status);

	gflog_error(GFARM_MSG_1004840,
		"%s, wr_id %lx opcode=%x syndrom 0x%x status:%s ",
		msg, (long)wc->wr_id, wc->opcode, wc->vendor_err, str);

	if (wc->status)
		*errp = EINVAL;
}
static int
gfcc_ib_mtu2byte(int mtu)
{
	int byte;
	switch (mtu) {
	case IB_MTU_256:
		byte = 256; break;
	case IB_MTU_512:
		byte = 512; break;
	case IB_MTU_1024:
		byte = 1024; break;
	case IB_MTU_2048:
		byte = 2048; break;
	case IB_MTU_4096:
		byte = 4096; break;
	default:
		gflog_error(GFARM_MSG_1004841, "bad mtu %d\n", mtu);
		byte = 2048;
	}
	return (byte);
}
static int
gfcc_ib_byte2mtu(int byte)
{
	int mtu;
	if (byte <= 256)
		mtu = IB_MTU_256;
	else if (byte <= 512)
		mtu = IB_MTU_512;
	else if (byte <= 1024)
		mtu = IB_MTU_1024;
	else if (byte <= 2048)
		mtu = IB_MTU_2048;
	else
		mtu = IB_MTU_4096;
	return (mtu);
}
/* ---------------------------------- */
static void
gfcc_pr_rpc(char *str, struct gfcc_rpc *rpc)
{
	struct gfcc_rpc3_req *rpc3 = &rpc->r_rpc3;
	struct gfcc_rpc4_req *rpc4 = &rpc->r_rpc4;
	gflog_error(GFARM_MSG_1004842, "%s: rpc=%p ref=%d\n"
		"r3_cmd=%d cb=%p status=%d err=%d nwr=%d done=%d\n"
		"r4_cmd=%d cb=%p status=%d npage=%d\n",
		str, rpc, atomic_read(&rpc->r_ref),
		rpc3->r3_cmd, rpc3->r3_cb, rpc3->r3_status, rpc3->r3_err,
		rpc3->r3_nwr, rpc3->r3_done,
		rpc4->r4_cmd, rpc4->r4_cb, rpc4->r4_status, rpc4->r4_npage);
}

static struct gfcc_rpc *
gfcc_rpc_find(struct gfcc_ctx *ctx, uint64_t wr_id)
{
	if (virt_addr_valid(wr_id) && !memcmp(((struct gfcc_rpc *)wr_id)->
		r_magic, GFCC_RPC_MAGIC, strlen(GFCC_RPC_MAGIC))) {
		return ((struct gfcc_rpc *) wr_id);
	}
	gflog_error(GFARM_MSG_1004843, "not found wr_id %lx", (long)wr_id);
	return (NULL);
}
/*------------------------------------------------------------*/
static struct ib_ah*
gfcc_create_ah(struct gfcc_ctx *ctx, struct gfcc_ibaddr *ibaddr)
{
	struct ib_ah *ah;
	struct ib_ah_attr ah_attr;

	ah_attr.dlid  = ibaddr->ca_lid;
	if (ctx->param->gid_index < 0) {
		ah_attr.sl	 = ctx->param->sl;
	} else {
		ah_attr.grh.dgid   = *(union ib_gid *)&ibaddr->ca_gid;
		ah_attr.grh.sgid_index = ctx->param->gid_index;
		ah_attr.grh.hop_limit = 1;
		ah_attr.sl	 = 0;
	}
	ah_attr.src_path_bits = 0;
	ah_attr.port_num   = ctx->param->ib_port;
	if (!(ah = gfib_create_ah(ctx->pd, &ah_attr))) {
		gflog_error(GFARM_MSG_1004844, "ib_create_ah fail");
	}
	return (ah);
}
static struct ib_ah *
gfcc_find_ah(struct gfcc_ctx *ctx, struct gfcc_ibaddr *ibaddr,
	int create)
{
	struct ib_ah *ah = NULL, *new = NULL;
	int	i;
loop:
	GFCC_MUTEX_LOCK(&ctx->ib_lock);
	for (i = 0; i < GFCC_HOST_MAX && ctx->peer[i].ah; i++) {
		if (ctx->peer[i].ibaddr.ca_lid == ibaddr->ca_lid) {
			ah = ctx->peer[i].ah;
			break;
		}
	}
	if (!ah && new) {
		if (i < GFCC_HOST_MAX) {
			ctx->peer[i].ah = new;
			memcpy(&ctx->peer[i].ibaddr, ibaddr, sizeof(*ibaddr));
			new = NULL;
		}
	}
	GFCC_MUTEX_UNLOCK(&ctx->ib_lock);

	if (new) {
		gfib_destroy_ah(new);
		if (!ah) {
			gflog_error(GFARM_MSG_1004845, "too many ibaddr");
			goto end_err;
		}
	}
	if (!ah && i < GFCC_HOST_MAX) {
		if (!(new = gfcc_create_ah(ctx, ibaddr))) {
			goto end_err;
		}
		goto loop;
	}
end_err:
	return (ah);
}

/******************************************************************************/
static int
gfcc_proc_ibaddr_read(char *buf, int size, loff_t off, struct gfsk_procop *op)
{
	int i, n = 0;
	struct gfcc_ctx *ctx = op->ctx;

	if (off)
		return (0);
	n += pr_ibaddr(buf + n, size - n, &ctx->my_ibaddr);
	for (i = 0; i < GFCC_HOST_MAX && n < size - 80; i++) {
		if (!ctx->peer[i].ibaddr.ca_qpn)
			break;
		n += pr_ibaddr(buf + n, size - n, &ctx->peer[i].ibaddr);
	}
	return (n);
}
static int
gfcc_proc_ibaddr_write(char *buf, int size, loff_t off, struct gfsk_procop *op)
{
	int err = 0;
	int i, j, k, r, n;
	struct gfcc_ctx *ctx = op->ctx;
	struct gfcc_ibaddr ibaddr;

	for (n = i = 0; i < GFCC_HOST_MAX; i++)
		if (!ctx->peer[i].ah && !ctx->peer[i].ibaddr.ca_qpn)
			break;

	for (j = 0; i < GFCC_HOST_MAX; j++) {
		for (k = j; j < size; j++) {
			if (buf[j] == '\n') {
				buf[j] = 0;
				break;
			}
		}
		if (j >= size)
			break;
		if ((r = rd_ibaddr(buf + k, j - k, &ibaddr)) < 0) {
			err = -EINVAL;
			break;
		}
		if (!r)
			continue;
		GFCC_MUTEX_LOCK(&ctx->ib_lock);
		for (; i < GFCC_HOST_MAX; i++) {
			if (!ctx->peer[i].ah &&
				!ctx->peer[i].ibaddr.ca_qpn){
				ctx->peer[i].ibaddr = ibaddr;
				n++;
				break;
			}
		}
		GFCC_MUTEX_UNLOCK(&ctx->ib_lock);
	}
	if (i >= GFCC_HOST_MAX)
		err = -EFBIG;
	return (err ? err : size);
}
static int
gfcc_proc_stat_read(char *buf, int size, loff_t off, struct gfsk_procop *op)
{
	int n;
	struct gfcc_ctx *ctx = op->ctx;

	if (off)
		return (0);
	n = gfcc_prstat(buf, size, ctx);
	return (n);
}
static int
gfcc_proc_stat_write(char *buf, int size, loff_t off, struct gfsk_procop *op)
{
	struct gfcc_ctx *ctx = op->ctx;

	if (off)
		return (-EINVAL);
	atomic64_set(&ctx->r_send_req, 0);
	atomic64_set(&ctx->r_recv_res, 0);
	atomic64_set(&ctx->r_recv_less, 0);
	atomic64_set(&ctx->r_recv_0, 0);
	atomic64_set(&ctx->r_recv_err, 0);
	atomic64_set(&ctx->r_recv_page, 0);
	atomic64_set(&ctx->r_time[GFCC_TM_SEND_REQ], 0);
	atomic64_set(&ctx->r_req_page, 0);
	atomic64_set(&ctx->r_recv_req, 0);
	atomic64_set(&ctx->r_send_res, 0);
	atomic64_set(&ctx->r_send_err, 0);
	atomic64_set(&ctx->r_send_page, 0);
	atomic64_set(&ctx->r_time[GFCC_TM_RECV_REQ], 0);
	atomic64_set(&ctx->r_err, 0);
	return (size);
}
int
gfsk_cc_find_host(struct gfcc_obj *obj, struct gfcc_ibaddr *ibaddr)
{
	int i;
	int err = -ENOENT;
	struct gfcc_ctx *ctx = gfsk_fsp->gf_cc_ctxp;

	for (i = 0; i < GFCC_HOST_MAX; i++) {
		if (!ctx->peer[i].ibaddr.ca_qpn)
			break;
		*ibaddr = ctx->peer[i].ibaddr;
		err = 0;
		break;
	}
	return (err);
}
/******************************************************************************/
static long
gfcc_page2seg(struct gfcc_ctx *ctx, int npage)
{
	long size;
	size = ((long)npage * page_size + sizeof(struct gfcc_rpc2_res)
			+ ctx->iosize - 1) / ctx->iosize;
	return (size);
}
static int
gfcc_reuse_qp(struct ib_qp *qp, int port, int qkey)
{
	int err;
	struct ib_qp_attr attr;

	attr.qp_state	= IB_QPS_RESET;
	if ((err = gfib_modify_qp(qp, &attr, IB_QP_STATE)))
		goto end_ret;

	attr.qp_state	= IB_QPS_INIT;
	attr.pkey_index      = 0;
	attr.port_num	= port;
	attr.qkey = qkey;

	if ((err = gfib_modify_qp(qp, &attr,
		IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT|IB_QP_QKEY)))
		goto end_ret;
	attr.qp_state	= IB_QPS_RTR;
	if ((err = gfib_modify_qp(qp, &attr, IB_QP_STATE)))
		goto end_ret;
	attr.sq_psn	= 100;
	attr.qp_state	= IB_QPS_RTS;
	if ((err = gfib_modify_qp(qp, &attr, IB_QP_STATE|IB_QP_SQ_PSN)))
		goto end_ret;
end_ret:
	return (err);
}
static void
gfcc_rpc_list_add(struct gfcc_rpc *rpc, struct gfcc_ctx *ctx)
{
	rpc->r_timeout = jiffies + ctx->tojiffies;
	GFCC_MUTEX_LOCK(&ctx->ib_lock);
	list_add_tail(&rpc->r_list, &ctx->rpc_list);
	GFCC_MUTEX_UNLOCK(&ctx->ib_lock);
	queue_delayed_work(ctx->workqueue, &ctx->reap_dwork,
				ctx->tojiffies * 2);
}
static void
gfcc_rpc_list_del(struct gfcc_rpc *rpc, struct gfcc_ctx *ctx)
{
	if (rpc->r_list.next && rpc->r_list.next != LIST_POISON1) {
		GFCC_MUTEX_LOCK(&ctx->ib_lock);
		list_del(&rpc->r_list);
		GFCC_MUTEX_UNLOCK(&ctx->ib_lock);
	}
}
static void
gfcc_rpc_list_check(struct gfcc_ctx *ctx)
{
	struct ib_wc wc;
	long cur = jiffies;
	struct gfcc_rpc *rpc;

	wc.status = IB_WC_SUCCESS + 100;
	GFCC_MUTEX_LOCK(&ctx->ib_lock);
	while (ctx->status == GFCC_ST_ACTIVE &&
			!list_empty(&ctx->rpc_list)) {
		rpc = list_entry(ctx->rpc_list.next, struct gfcc_rpc, r_list);
		if (rpc->r_timeout > cur)
			break;
		list_del(&rpc->r_list);
		GFCC_MUTEX_UNLOCK(&ctx->ib_lock);
		gfcc_pr_rpc("reap", rpc);
		if (rpc->r_rpc3.r3_cb)
			rpc->r_rpc3.r3_cb(rpc, &wc);
		else
			gfcc_rpc_put(rpc);
		GFCC_MUTEX_LOCK(&ctx->ib_lock);
	}
	GFCC_MUTEX_UNLOCK(&ctx->ib_lock);
}

static void
gfcc_rpc_unset(struct gfcc_rpc *rpc)
{
	struct gfcc_rpc3_req *rpc3 = &rpc->r_rpc3;

	if (rpc->r_rpc4.r4_pages) {
		if (rpc->r_rpc4.r4_cmd == GFCC_RPC_READ_RES)
			gfsk_release_pages(rpc->r_rpc4.r4_pages);
		else
			gfsk_validate_pages(rpc->r_rpc4.r4_pages, 0, 1);
		gfcc_pages_free(rpc->r_rpc4.r4_pages);
		rpc->r_rpc4.r4_pages = NULL;
	}
	if (rpc3->r3_dma_page[0]) {
		struct ib_device *dev = rpc->r_ctx->context->device;
		int i;
		enum  dma_data_direction dir =
			rpc3->r3_cmd == GFCC_RPC_READ_RES ?
				DMA_TO_DEVICE : DMA_FROM_DEVICE;

		for (i = 0; i < GFCC_PAGES_MAX; i++) {
			if (rpc3->r3_dma_page[i]) {
				gfib_dma_unmap_page(dev, rpc3->r3_dma_page[i],
					page_size, dir);
				rpc3->r3_dma_page[i] = 0;
			}
		}
		if ((rpc3->r3_flags & GFCC_RPCFL_RESET)) {
			gfcc_reuse_qp(rpc3->r3_qp, rpc->r_ctx->param->ib_port,
					rpc->r_ctx->param->qkey);
		}
	}
	rpc->r_rpc4.r4_cb = NULL;
	rpc->r_rpc3.r3_cb = NULL;
}
static void
gfcc_rpc_free(struct gfcc_rpc *rpc)
{
	if (rpc) {
		gflog_verbose(GFARM_MSG_1004846, "rpc %p", rpc);
		gfcc_rpc_unset(rpc);
		gfcc_stack_put(rpc, &rpc->r_ctx->rpc_pool);
	}
}

static void
gfcc_rpc_unset0(struct gfcc_rpc *rpc)
{
	if (rpc->r_reqdma) {
		gfib_dma_unmap_single(rpc->r_ctx->context->device,
			rpc->r_reqdma, GFCC_UD_MTU,
			DMA_BIDIRECTIONAL);
		rpc->r_reqdma = 0;
	}
	if (rpc->r_resdma) {
		gfib_dma_unmap_single(rpc->r_ctx->context->device,
			rpc->r_resdma, sizeof(struct gfcc_rpcres),
			DMA_BIDIRECTIONAL);
		rpc->r_resdma = 0;
	}
	if (rpc->r_rpc3.r3_qp) {
		gfib_destroy_qp(rpc->r_rpc3.r3_qp);
		rpc->r_rpc3.r3_qp = NULL;
	}
	memset(rpc->r_magic, 0, sizeof(GFCC_RPC_MAGIC));
}


static struct gfcc_rpc*
gfcc_rpc_alloc(struct gfcc_ctx *ctx, int nowait)
{
	int err;
	struct gfcc_rpc *rpc;
	struct ib_qp_init_attr iattr;

	if (!(rpc = gfcc_stack_get(&ctx->rpc_pool, nowait))) {
		goto err_end;
	}
	if ((err = atomic_read(&rpc->r_ref)) != 0) {
		gflog_error(GFARM_MSG_1004847, "rpc %p ref=%d", rpc, err);
		goto err_end;
	}
	atomic_set(&rpc->r_ref, 1);
	if (rpc->r_rpc3.r3_qp) {
		rpc->r_rpc4.r4_err = 0;
		rpc->r_rpc3.r3_err = 0;
		rpc->r_rpc3.r3_flags = 0;
		rpc->r_rpc3.r3_done = 0;
		rpc->r_rpc3.r3_nsend = 0;
		rpc->r_rpc3.r3_nrecv = 0;
		rpc->r_rpc4.r4_npage = 0;
		return (rpc);
	}
	rpc->r_reqbuf = rpc->q;
	rpc->r_resbuf = (char *) &rpc->a;
	memcpy(rpc->r_magic, GFCC_RPC_MAGIC, strlen(GFCC_RPC_MAGIC));
	rpc->r_ctx = ctx;
	memset(&iattr, 0, sizeof(iattr));

	GFCC_MUTEX_INIT(&rpc->r_rpc3.r3_lock);
	GFCC_COND_INIT(&rpc->r_rpc3.r3_wait);

	iattr.send_cq = ctx->scq;
	iattr.recv_cq = ctx->rcq;
	iattr.cap.max_send_wr  = gfcc_page2seg(ctx, GFCC_PAGES_MAX);
	iattr.cap.max_recv_wr  = gfcc_page2seg(ctx, GFCC_PAGES_MAX);
	iattr.cap.max_send_sge = ctx->send_sge;
	iattr.cap.max_recv_sge = ctx->send_sge + 1;	/* for GRH */
	iattr.cap.max_inline_data = 0;
	iattr.qp_type = IB_QPT_UD;
	iattr.sq_sig_type = IB_SIGNAL_REQ_WR;
	if (!(rpc->r_rpc3.r3_qp = gfib_create_qp(ctx->pd, &iattr))) {
		gflog_error(GFARM_MSG_1004848, "Couldn't create QP\n");
		err = EINVAL;
		goto err_end1;
	}
	if ((err = gfcc_reuse_qp(rpc->r_rpc3.r3_qp, ctx->param->ib_port,
				ctx->param->qkey))) {
		err = EINVAL;
		goto err_end1;
	}

	rpc->r_reqdma = gfib_dma_map_single(ctx->context->device,
		rpc->r_reqbuf, GFCC_UD_MTU, DMA_BIDIRECTIONAL);
	if (!rpc->r_reqdma) {
		goto err_end1;
	}
	rpc->r_resdma = gfib_dma_map_single(ctx->context->device,
		rpc->r_resbuf, sizeof(struct gfcc_rpcres), DMA_BIDIRECTIONAL);
	if (!rpc->r_resdma) {
		goto err_end1;
	}
	gflog_verbose(GFARM_MSG_1004849, "rpc %p", rpc);
	return (rpc);
err_end1:
	gfcc_rpc_unset0(rpc);
	gfcc_rpc_put(rpc);
err_end:
	return (NULL);
}
static void
gfcc_rpc_dtr(void *foo)
{
	struct gfcc_rpc *rpc = (struct gfcc_rpc *)foo;
	gfcc_rpc_unset(rpc);
	gfcc_rpc_unset0(rpc);
}
/******************************************************************************
 *
 ******************************************************************************/
static int
gfcc_set_link_layer(struct gfcc_ctx *ctx)
{

	struct ib_port_attr port_attr;
	int byte, mtu;

	if (gfib_query_port(ctx->context, ctx->param->ib_port, &port_attr)) {
		gflog_info(GFARM_MSG_1004850, "Unable to query port\n");
		return (-1);
	}
	if (!(byte = ctx->param->mtu))
		mtu = port_attr.active_mtu;
	else {
		mtu = gfcc_ib_byte2mtu(byte);
		if (mtu > port_attr.max_mtu) {
			gflog_info(GFARM_MSG_1004851,
			"mtu(%d) shrink to %d\n", mtu, port_attr.max_mtu);
			mtu = port_attr.max_mtu;
		}
		if (mtu > port_attr.active_mtu) {
			gflog_info(GFARM_MSG_1004852,
			"Please change active_mtu(%d) to %d\n",
					port_attr.active_mtu, mtu);
			mtu = port_attr.active_mtu;
		}
	}
	ctx->param->mtu = gfcc_ib_mtu2byte(mtu);
	ctx->my_ibaddr.ca_lid = port_attr.lid;

	if (gfib_query_gid(ctx->context, ctx->param->ib_port,
		ctx->param->gid_index,
		(union ib_gid *) &ctx->my_ibaddr.ca_gid)) {
		gflog_error(GFARM_MSG_1004853, "Fail query_gid");
	}

	return (0);
}
struct gfcc_param *
gfcc_param_init(void)
{
	struct gfcc_param *param;
	if (!(param = kmalloc(sizeof(*param), GFP_KERNEL))) {
		gflog_error(GFARM_MSG_1004854, "malloc fail");
		return (NULL);
	}
	memset(param, 0, sizeof(*param));

	param->mtu = 0;
	param->gid_index = 0;
	param->ib_port = 1;
	param->sl = 0;
	param->qkey = 111;
	param->num_srpc = 64;
	param->num_rrpc = 128;
	param->tomsec = 3 * 60 * 1000;
	return (param);
}
static struct gfsk_procop procop_ibaddr = {
	"ibaddr", 0644, gfcc_proc_ibaddr_read, gfcc_proc_ibaddr_write};
static struct gfsk_procop procop_prstat = {
	"ibstat", 0644, gfcc_proc_stat_read, gfcc_proc_stat_write};
int
gfcc_ctx_init(struct gfcc_param *param, struct gfcc_ctx **ctxp)
{
	int err = -EINVAL;
	int  depth, vec, num_comp_vectors;
	struct gfcc_ctx *ctx;
	struct ib_qp_init_attr iattr;
	struct ib_device *ib_dev;
	char *devname = param->devname[0] ? param->devname : NULL;

	*ctxp = NULL;
	if (!(ib_dev = gfcc_ib_find_dev(devname, &param->ib_port)))
		return (-ENOENT);

	if (!(ctx = kmalloc(sizeof(*ctx), GFP_KERNEL))) {
		gflog_error(GFARM_MSG_1004855, "malloc fail");
		return (-ENOMEM);
	}
	memset(ctx, 0, sizeof(*ctx));
	*ctxp = ctx;
	ctx->status = GFCC_ST_INIT;
	ctx->param = param;
	INIT_LIST_HEAD(&ctx->rpc_list);
	GFCC_MUTEX_INIT(&ctx->ib_lock);
	ctx->fs_ctxp = gfsk_fsp;

	ctx->context = gfib_open_device(ib_dev, ctx);
	if (!ctx->context) {
		gflog_error(GFARM_MSG_1004856, "Couldn't get context for %s\n",
			devname ? devname : "");
		goto end_err;
	}
	/* Finds the link type and configure the HCA accordingly. */
	if (gfcc_set_link_layer(ctx)) {
		gflog_error(GFARM_MSG_1004857, "Couldn't set the link layer\n");
		goto end_err;
	}

	ctx->iosize = ctx->param->mtu - GFCC_UD_GRH;
	/* <p|page|p> <page|p> <p|p> */
	ctx->send_sge = (ctx->iosize + (page_size - 1) * 2) / page_size;
	gflog_info(GFARM_MSG_1004858, "mtu=%d iosize=%d send_sge=%d",
		ctx->param->mtu, ctx->iosize, ctx->send_sge);

	ctx->pd = gfib_alloc_pd(ctx->context);
	if (!ctx->pd) {
		gflog_error(GFARM_MSG_1004859, "Couldn't allocate PD\n");
		goto end_err;
	}

	num_comp_vectors = ctx->context->device->num_comp_vectors;
	ctx->mr = ib_get_dma_mr(ctx->pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(ctx->mr)) {
		err = PTR_ERR(ctx->mr);
		ctx->mr = NULL;
	}
	if (!ctx->mr) {
		gflog_error(GFARM_MSG_1004860, "Couldn't allocate MR %d", err);
		goto end_err;
	}

	/* Create the CQ according to Client/Server or Duplex setting. */
	depth = param->num_rrpc +
		param->num_srpc * gfcc_page2seg(ctx, GFCC_PAGES_MAX);
	gflog_info(GFARM_MSG_1004861, "num_comp_vectors=%d depth=%d",
		num_comp_vectors, depth);
	/* vec = num_comp_vectors -1 :: works bad */
	vec = 0;
	ctx->workqueue = create_singlethread_workqueue("gfcc");

	INIT_WORK(&ctx->recv_task, gfcc_ib_recv_task);
	INIT_WORK(&ctx->send_task, gfcc_ib_send_task);
	INIT_DELAYED_WORK(&ctx->reap_dwork, gfcc_ib_reap_task);
	ctx->tojiffies = msecs_to_jiffies(ctx->param->tomsec);
	ctx->scq = ib_create_cq(ctx->context->device,
			gfcc_ib_send_comp, NULL, ctx, depth, vec);
	ctx->rcq = ib_create_cq(ctx->context->device,
			gfcc_ib_recv_comp, NULL, ctx, depth, vec);
	if (IS_ERR(ctx->scq)) {
		err = PTR_ERR(ctx->scq);
		ctx->scq = NULL;
	} else
		gfib_req_notify_cq(ctx->scq, IB_CQ_NEXT_COMP);
	if (IS_ERR(ctx->rcq)) {
		err = PTR_ERR(ctx->rcq);
		ctx->rcq = NULL;
	} else
		gfib_req_notify_cq(ctx->rcq, IB_CQ_NEXT_COMP);
	if (ctx->scq == NULL || ctx->rcq == NULL) {
		gflog_error(GFARM_MSG_1004862, "Couldn't create CQ %d\n", err);
		goto end_err;
	}

	memset(&iattr, 0, sizeof(struct ib_qp_init_attr));
	iattr.send_cq = ctx->scq;
	iattr.recv_cq = ctx->rcq;
	iattr.cap.max_send_wr  = 0;
	iattr.cap.max_recv_wr  = param->num_rrpc + param->num_srpc;
	iattr.cap.max_send_sge = 0;
	iattr.cap.max_recv_sge = 1;
	iattr.cap.max_inline_data = 0;
	iattr.qp_type = IB_QPT_UD;

	ctx->qp = gfib_create_qp(ctx->pd, &iattr);
	if (!ctx->qp)  {
		gflog_error(GFARM_MSG_1004863, "Couldn't create QP\n");
		goto end_err;
	}
	ctx->my_ibaddr.ca_qpn = ctx->qp->qp_num;
	ctx->my_ibaddr.ca_qkey = ctx->param->qkey;
	ctx->my_ibaddr.ca_sl = ctx->param->sl;
	ctx->my_ibaddr.ca_psn = ((long) &ctx->my_ibaddr.ca_psn) & 0xfffff;

	if ((err = gfcc_stack_init(&ctx->rpc_pool,
		ctx->param->num_rrpc + ctx->param->num_srpc,
		sizeof(struct gfcc_rpc), NULL, 1))) {
		gflog_error(GFARM_MSG_1004864, "Couldn't create cc_cachep\n");
		goto end_err;
	}
	ctx->proc_ibaddr = procop_ibaddr;
	ctx->proc_ibaddr.ctx = ctx;
	if ((err = gfarm_procop_create(gfsk_fsp->gf_pde, &ctx->proc_ibaddr))) {
		gflog_error(GFARM_MSG_1004865, "Couldn't create proc\n");
	}
	ctx->proc_stat = procop_prstat;
	ctx->proc_stat.ctx = ctx;
	if ((err = gfarm_procop_create(gfsk_fsp->gf_pde, &ctx->proc_stat))) {
		gflog_error(GFARM_MSG_1004866, "Couldn't create proc\n");
	}
	ctx->status = GFCC_ST_ACTIVE;
	if ((err = rpc3_listen(ctx))) {
		goto end_err;
	}
	if (gfarm_cc_register(&ctx->my_ibaddr)) {
		gflog_error(GFARM_MSG_1004867, "gfarm_cc_register fail");
		err = -EINVAL;
		goto end_err;
	}

	return (0);
end_err:
	*ctxp = NULL;
	gfcc_ctx_fini(ctx);
	return (err);
}

/******************************************************************************/
void
gfcc_ctx_fini(struct gfcc_ctx *ctx)
{
	int i;

	if (!ctx)
		return;
	GFCC_MUTEX_LOCK(&ctx->ib_lock);
	ctx->status = GFCC_ST_STOPPING;
	GFCC_MUTEX_UNLOCK(&ctx->ib_lock);
	gfarm_procop_remove(gfsk_fsp->gf_pde, &ctx->proc_ibaddr);
	gfarm_procop_remove(gfsk_fsp->gf_pde, &ctx->proc_stat);
	for (i = 0; i < GFCC_HOST_MAX; i++) {
		if (!ctx->peer[i].ah)
			break;
		if (gfib_destroy_ah(ctx->peer[i].ah)) {
			gflog_error(GFARM_MSG_1004868, "faile to destroy AH");
		}
		ctx->peer[i].ah = NULL;
	}
	if (ctx->workqueue) {
		cancel_delayed_work(&ctx->reap_dwork);
		destroy_workqueue(ctx->workqueue);
	}
	/* firstly destroy ctx->qp, then free recv buffer */
	if (ctx->qp)
		gfib_destroy_qp(ctx->qp);
	gfcc_stack_fini(&ctx->rpc_pool, gfcc_rpc_dtr);

	if (gfib_destroy_cq(ctx->scq)) {
		gflog_error(GFARM_MSG_1004869, "failed to destroy SCQ\n");
	}
	if (gfib_destroy_cq(ctx->rcq)) {
		gflog_error(GFARM_MSG_1004870, "failed to destroy RCQ\n");
	}

	if (gfib_dereg_mr(ctx->mr)) {
		gflog_error(GFARM_MSG_1004871, "failed to destroy MR\n");
	}

	if (gfib_dealloc_pd(ctx->pd)) {
		gflog_error(GFARM_MSG_1004872, "failed to destroy PD\n");
	}


	if (gfib_close_device(ctx->context, ctx)) {
		gflog_error(GFARM_MSG_1004873, "failed to close context\n");
	}
	GFCC_MUTEX_DESTROY(&ctx->ib_lock);
	kfree(ctx->param);
	kfree(ctx);
}
/******************************************************************************
 *
 ******************************************************************************/
static int
gfcc_post_recv(struct gfcc_rpc *rpc, struct ib_qp *qp)
{
	int err = 0;
	struct ib_recv_wr	wr;
	struct ib_sge sge[1];

	sge[0].addr = rpc->r_reqdma;
	sge[0].length = GFCC_UD_MTU;
	sge[0].lkey = rpc->r_ctx->mr->lkey;

	wr.next = NULL;
	wr.wr_id = (uint64_t) rpc;;
	wr.sg_list = sge;
	wr.num_sge = 1;

	gfcc_rpc_get(rpc);
	err = gfib_post_recv(qp, &wr);
	if (!err)
		rpc->r_rpc3.r3_nrecv = 1;
	else
		gfcc_rpc_put(rpc);
	return (err);
}
static int
gfcc_post_send(struct gfcc_rpc *rpc, struct ib_qp *qp, int len,
				struct ib_ah *ah)
{
	int err = 0;
	struct ib_send_wr	wr;
	struct ib_sge sge[1];

	sge[0].addr = rpc->r_reqdma;
	sge[0].length = len;
	sge[0].lkey = rpc->r_ctx->mr->lkey;

	wr.next = NULL;
	wr.wr_id = (uint64_t) rpc;
	wr.sg_list = sge;
	wr.num_sge = 1;
	wr.opcode = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED|IB_SEND_SOLICITED;
	wr.wr.ud.ah = ah;
	wr.wr.ud.remote_qkey = rpc->r_rpc4.r4_ibaddr->ca_qkey;
	wr.wr.ud.remote_qpn = rpc->r_rpc4.r4_ibaddr->ca_qpn;

	gfcc_rpc_get(rpc);
	err = gfib_post_send(qp, &wr);
	if (!err)
		rpc->r_rpc3.r3_nsend++;
	else
		gfcc_rpc_put(rpc);
	return (err);
}
static int
gfcc_post_recv_pages(struct gfcc_rpc *rpc)
{
	int err = 0;
	int i, len, l, iosize, iores, nseg = 0, ng;
	uint64_t addr, *addrs = rpc->r_rpc3.r3_dma_page;
	struct ib_qp *qp = rpc->r_rpc3.r3_qp;
	int npage = rpc->r_rpc2->r2_npage;
	struct ib_recv_wr	wr;
	struct ib_sge sge[8];

	sge[0].addr = rpc->r_resdma;
	sge[0].length = GFCC_UD_GRH + sizeof(struct gfcc_rpc2_res);
	sge[0].lkey = sge[1].lkey = sge[2].lkey = sge[3].lkey =
	sge[4].lkey = sge[5].lkey = sge[6].lkey = sge[7].lkey =
			rpc->r_ctx->mr->lkey;
	iosize = rpc->r_ctx->iosize;
	iores = iosize - sizeof(struct gfcc_rpc2_res);
	ng = 1;

	wr.next = NULL;
	wr.wr_id = (uint64_t) rpc;
	wr.sg_list = sge;
	gfcc_rpc_get(rpc);

	for (i = 0; i < npage && !err; i++) {
		addr = addrs[i];
		for (len = page_size; len > 0;) {
			sge[ng].addr = addr;
			l = sge[ng].length = len < iores ? len : iores;
			iores -= l;
			len -= l;
			addr += l;
			ng++;
			if (!iores) {
				wr.num_sge = ng;
				if ((err = gfib_post_recv(qp, &wr)))
					break;
				nseg++;
				sge[0].length = GFCC_UD_GRH;
				ng = 1;
				iores = iosize;
			}
		}
	}
	if (!err && ng > 1) {
		wr.num_sge = ng;
		if (!(err = gfib_post_recv(qp, &wr)))
			nseg++;
	}
	if (err && !nseg) {
		gfcc_rpc_put(rpc);	/* no call back */
	}
	rpc->r_rpc3.r3_nwr = nseg;
	rpc->r_rpc3.r3_nrecv = nseg;
	gflog_debug(GFARM_MSG_1004874, "npage=%d nseg=%d", npage, nseg);
	return (err);
}
static int
gfcc_post_send_pages(struct gfcc_rpc *rpc, int npage, struct ib_ah *ah)
{
	int err = 0;
	int i, len, l, ng, iosize, iores, nseg = 0;
	struct ib_qp *qp = rpc->r_rpc3.r3_qp;
	uint64_t addr;
	struct ib_send_wr	wr;
	struct ib_sge sge[6];

	sge[0].addr = rpc->r_resdma;
	sge[0].length = sizeof(struct gfcc_rpc2_res);
	sge[0].lkey = sge[1].lkey = sge[2].lkey = sge[3].lkey =
	sge[4].lkey = sge[5].lkey = rpc->r_ctx->mr->lkey;
	iosize = rpc->r_ctx->iosize;
	iores = iosize - sizeof(struct gfcc_rpc2_res);
	ng = 1;

	wr.next = NULL;
	wr.wr_id = (uint64_t) rpc;;
	wr.sg_list = sge;
	wr.opcode = IB_WR_SEND;
	wr.send_flags = 0;
	wr.wr.ud.ah = ah;
	wr.wr.ud.remote_qkey = rpc->r_rpc4.r4_ibaddr->ca_qkey;
	wr.wr.ud.remote_qpn = rpc->r_rpc4.r4_ibaddr->ca_qpn;
	gfcc_rpc_get(rpc);
	if (!npage) {
		wr.num_sge = 1;
		wr.send_flags |= IB_SEND_SIGNALED|IB_SEND_SOLICITED;
		if ((err = gfib_post_send(rpc->r_rpc3.r3_qp, &wr))) {
			gfcc_rpc_put(rpc);
		}
		return (err);
	}

	iosize = rpc->r_ctx->iosize;
	for (i = 0; i < npage && !err; i++) {
		addr = rpc->r_rpc3.r3_dma_page[i];
		for (len = page_size; len > 0;) {
			sge[ng].addr = addr;
			l = sge[ng].length = len < iores ? len : iores;
			iores -= l;
			len -= l;
			addr += l;
			ng++;
			if (!iores) {
				wr.num_sge = ng;
				if (i == npage - 1 && !len)
					wr.send_flags |=
					IB_SEND_SIGNALED|IB_SEND_SOLICITED;
				if ((err = gfib_post_send(qp, &wr)))
					break;
				nseg++;
				ng = 0;
				iores = iosize;
			}
		}
	}
	if (!err && ng > 0) {
		wr.num_sge = ng;
		wr.send_flags |= IB_SEND_SIGNALED|IB_SEND_SOLICITED;
		if (!(err = gfib_post_send(qp, &wr)))
			nseg++;
	}
	if (err) {
		gfcc_rpc_put(rpc);	/* no IB_SEND_SIGNALED */
	}
	if (rpc->r_rpc3.r3_nwr != nseg) {
		gflog_error(GFARM_MSG_1004875, "invalid nseg %d:%d",
			rpc->r_rpc3.r3_nwr, nseg);
	}
	rpc->r_rpc3.r3_nsend = nseg;
	gflog_debug(GFARM_MSG_1004876, "npage=%d nseg=%d", npage, nseg);
	return (err);
}
static int
gfcc_map_send_pages(struct gfcc_ctx *ctx, int npage, uint64_t *addr,
	struct gfcc_pages *pages, enum dma_data_direction dir)
{
	struct ib_device *dev = ctx->context->device;
	int i;

	for (i = 0; i < npage; i++) {
		if (!(addr[i] = gfib_dma_map_page(dev, pages->cp_pages[i],
			0, page_size, dir)))
			break;
	}
	return (i);
}
static int
gfcc_map_recv_pages(struct gfcc_ctx *ctx, int npage, uint64_t *addr,
	int *npblk, struct gfcc_pblk *pblk, struct gfcc_pages *pages,
	enum dma_data_direction dir)
{
	struct page *page;
	struct ib_device *dev = ctx->context->device;
	int i = 0, j = -1;
	int64_t  index = -1;

	for (i = 0; i < npage; ) {
		page = pages->cp_pages[i];
		if (!(addr[i] = gfib_dma_map_page(dev, page, 0,
					page_size, dir)))
			break;
		if (index == page->index)
			pblk[j].cs_npage++;
		else {
			if (j + 1 == *npblk) {
				gflog_warning(GFARM_MSG_1004877,
					"too many pblk %d", j);
				break;
			}
			j++;
			index = pblk[j].cs_index = page->index;
			pblk[j].cs_npage = 1;
		}
		if (++i == npage) {
			gflog_verbose(GFARM_MSG_1004878, "too many page %d",
						j);
			break;
		}
		index++;
	}
	*npblk = j+1;
	return (i);
}
static int
rpc3_listen_cb(struct gfcc_rpc *rpc, struct ib_wc *wc)
{

	struct gfcc_ibaddr *ibaddr;
	struct gfcc_rpc2_req *rpc2;

	rpc->r_time[GFCC_TM_RECV_REQ] = get_cycles();
	if (wc->status != IB_WC_SUCCESS) {
		goto end_reuse;
	}
	rpc->r_rpc2 = rpc2 = (struct gfcc_rpc2_req *)
		(rpc->r_reqbuf + GFCC_UD_GRH);
	if (memcmp(rpc2->r2_ver, GFCC_RPC2_VER, strlen(GFCC_RPC2_VER))) {
		gflog_error(GFARM_MSG_1004879, "version is different");
		goto end_reuse;
	}
	if (rpc2->r2_cmd != GFCC_RPC_READ_REQ) {
		gflog_error(GFARM_MSG_1004880, "unknown command");
		goto end_reuse;
	}
	atomic64_inc(&rpc->r_ctx->r_recv_req);
	ibaddr = (struct gfcc_ibaddr *)rpc->r_reqbuf;
	if (wc->wc_flags & IB_WC_GRH) {
		struct ib_grh *grh = (struct ib_grh *)rpc->r_reqbuf;
		*(union ib_gid *)&ibaddr->ca_gid = grh->sgid;
	}
	ibaddr->ca_lid = wc->slid;
	ibaddr->ca_qpn = wc->src_qp;
	ibaddr->ca_qkey = rpc2->r2_qkey;
	ibaddr->ca_sl = wc->sl;
	ibaddr->ca_psn = rpc2->r2_psn;

	rpc3_recv_read_req(rpc, ibaddr);
	rpc3_post_listen(rpc->r_ctx);
	goto end_ret;
end_reuse:
	rpc->r_rpc3.r3_status = GFCC_RPCST_WAIT;
	rpc->r_rpc3.r3_nrecv = 1;
	if (gfcc_post_recv(rpc, rpc->r_ctx->qp)) {
		gflog_error(GFARM_MSG_1004881, "post fail");
		rpc3_post_listen(rpc->r_ctx);
	}
end_ret:
	gfcc_rpc_put(rpc);	/* for callback */
	return (0);
}
static int
rpc3_post_listen(struct gfcc_ctx *ctx)
{
	int err;
	struct gfcc_rpc *rpc;
	struct gfcc_rpc3_req *rpc3;

	if (!(rpc = gfcc_rpc_alloc(ctx, 1)))
		return (-ENOMEM);
	rpc3 = &rpc->r_rpc3;
	rpc3->r3_cb = rpc3_listen_cb;
	rpc3->r3_status = GFCC_RPCST_WAIT;
	rpc3->r3_cmd = GFCC_RPC_LISTEN;

	if ((err = gfcc_post_recv(rpc, ctx->qp))) {
		gflog_error(GFARM_MSG_1004882, "post fail");
	}
	gfcc_rpc_put(rpc);	/* for allocator */
	return (0);
}
static int
rpc3_listen(struct gfcc_ctx *ctx)
{
	int	err = 0, i;

	if ((err = gfcc_reuse_qp(ctx->qp, ctx->param->ib_port,
					ctx->param->qkey)))
		goto err_end;
	for (i = 0; i < ctx->param->num_rrpc; i++) {
		if (rpc3_post_listen(ctx))
			break;
	}
	gflog_info(GFARM_MSG_1004883, "post_recv %d", i);
	if (!i)
		err = ENOMEM;
err_end:
	return (err);
}

static int
rpc2_poll(struct gfcc_ctx *ctx, struct ib_cq *cq)
{
	int	i, ne;
#define GFCCC_NUM_COMP	8
	struct ib_wc wc[GFCCC_NUM_COMP];

	gflog_debug(GFARM_MSG_1004884, "rpc poll %p", cq);
	for (; ctx->status == GFCC_ST_ACTIVE &&
		(ne = ib_poll_cq(cq, GFCCC_NUM_COMP, wc)) > 0;) {
		for (i = 0; i < ne; i++) {
			struct gfcc_rpc  *rpc;
			struct gfcc_rpc3_req *rpc3;
			int wcerr = 0;
			if (wc[i].status != IB_WC_SUCCESS) {
				gfcc_wc_status_pr(&wc[i], &wcerr, "receive");
				atomic64_inc(&ctx->r_err);
			}
			if (!(rpc = gfcc_rpc_find(ctx, wc[i].wr_id)))
				continue;
			rpc3 = &rpc->r_rpc3;
			gflog_verbose(GFARM_MSG_1004885, "rpc=%p done=%d",
				rpc, rpc3->r3_done);
			if (rpc3->r3_status != GFCC_RPCST_WAIT
			&&  rpc3->r3_status != GFCC_RPCST_ASYNC) {
				atomic64_inc(&ctx->r_err);
				gflog_error(GFARM_MSG_1004886,
				"Not waiting(%p) %d ", rpc3, rpc3->r3_status);
				continue;
			}
			if (wc[i].opcode == IB_WC_RECV) {
				rpc3->r3_nrecv--;
			}
			rpc3->r3_done++;
			if (wcerr) {
				rpc3->r3_err = wcerr;
			}
			if (rpc3->r3_cb)
				rpc3->r3_cb(rpc, &wc[i]);
			else {
				gfcc_wc_status_pr(&wc[i], &wcerr, "no cb");
			}
		}
		if (ne < GFCCC_NUM_COMP) /* give chance to others */
			break;
	}
	if (ne < 0) {
		gflog_error(GFARM_MSG_1004887, "poll CQ failed %d\n", ne);
		return (EIO);
	}
	return (0);
}
static int
rpc3_send_read_res_cb(struct gfcc_rpc *rpc, struct ib_wc *wc)
{
	struct gfcc_rpc3_req *rpc3 = &rpc->r_rpc3;
	gflog_debug(GFARM_MSG_1004888, "rpc3(%p) done=%d nwr=%d", rpc3,
		rpc3->r3_done, rpc3->r3_nwr);
	if (rpc3->r3_nwr == rpc3->r3_done || rpc3->r3_done) {
		uint64_t tm = get_cycles();
		tm = tm - rpc->r_time[GFCC_TM_RECV_REQ];
		atomic64_add(tm, &rpc->r_ctx->r_time[GFCC_TM_RECV_REQ]);
		rpc3->r3_done = rpc3->r3_nwr;
		if (rpc->r_rpc4.r4_cb)
			rpc->r_rpc4.r4_cb(rpc, rpc3->r3_err);
		gfcc_rpc_put(rpc);	/* for call back */
	}
	return (0);
}
static int
rpc3_send_read_res(struct gfcc_rpc *rpc, int ans)
{
	int err = 0, npage;
	struct gfcc_rpc3_req *rpc3 = &rpc->r_rpc3;
	struct gfcc_rpc4_req *rpc4 = &rpc->r_rpc4;
	struct ib_ah *ah = NULL;
	struct gfcc_rpc2_res *res;

	gflog_debug(GFARM_MSG_1004889, "rpc3(%p) sending", rpc3);
	rpc3->r3_status = GFCC_RPCST_INIT;
	rpc3->r3_cmd = GFCC_RPC_READ_RES;
	rpc3->r3_cb = rpc3_send_read_res_cb;

	err = -ENOMEM;
	if (!(ah = gfcc_create_ah(rpc->r_ctx, rpc4->r4_ibaddr)))
		goto err_end;

	npage = rpc4->r4_npage;

	res = (struct gfcc_rpc2_res *)(rpc->r_resbuf);
	res->r2_id = rpc->r_rpc2->r2_id;
	if (!ans && npage) {
		npage = gfcc_map_send_pages(rpc->r_ctx, npage,
			rpc3->r3_dma_page, rpc4->r4_pages, DMA_TO_DEVICE);
		if (!npage)
			ans = -ENOMEM;
	} else
		npage = 0;
	res->r2_err = ans < 0 ? -ans : ans;
	res->r2_npage = npage;
	rpc3->r3_nwr = res->r2_nseg = !npage ? 1
				: gfcc_page2seg(rpc->r_ctx, npage);
	rpc3->r3_done = 0;
	rpc3->r3_nsend = 0;
	rpc3->r3_nrecv = 0;
	rpc3->r3_status = GFCC_RPCST_ASYNC;
	err = gfcc_post_send_pages(rpc, npage, ah);
	atomic64_inc(&rpc->r_ctx->r_send_res);
	atomic64_add(res->r2_npage, &rpc->r_ctx->r_send_page);
err_end:
	gfib_destroy_ah(ah);
	return (err);
}
static int
rpc3_send_read_req_cb(struct gfcc_rpc *rpc, struct ib_wc *wc)
{
	struct gfcc_rpc2_res *res;
	struct gfcc_rpc3_req *rpc3 = &rpc->r_rpc3;
	struct gfcc_rpc4_req *rpc4 = &rpc->r_rpc4;

	if (wc->status != IB_WC_SUCCESS) {
		goto end_err;
	}
	if (wc->opcode == IB_WC_SEND) {
		if (rpc3->r3_done != 1) {
			gflog_verbose(GFARM_MSG_1004890,
				"%p done is %d not 1, opcode=%x expected=%x",
				rpc, rpc3->r3_done, wc->opcode, IB_WC_SEND);
		}
		gflog_debug(GFARM_MSG_1004891, "rpc3(%p) send done", rpc3);
	}
	gflog_verbose(GFARM_MSG_1004892, "rpc(%p) done=%d", rpc, rpc3->r3_done);
	if (rpc3->r3_done == 2) {
		res = (struct gfcc_rpc2_res *) (rpc->r_resbuf + GFCC_UD_GRH);
		if (res->r2_id != (uint64_t) rpc) {
			gflog_error(GFARM_MSG_1004893, "invalid r2_id");
			rpc3->r3_err = EPROTO;
			goto end_err;
		}
		if (res->r2_err != 0) {
			char buf[128];
			rpc3->r3_err = res->r2_err;
			pr_cc_obj(buf, sizeof(buf), &rpc->r_rpc2->r2_obj);
			gflog_debug(GFARM_MSG_1004894, "rpc obj:%s err=%d",
				buf, (int)res->r2_err);
			goto end_err;
		}
		if (res->r2_npage != rpc->r_rpc2->r2_npage) {
			gflog_debug(GFARM_MSG_1004895, "%p return page %d < %d",
				rpc, res->r2_npage, rpc->r_rpc2->r2_npage);
			rpc3->r3_nwr = res->r2_nseg;
			rpc3->r3_flags |= GFCC_RPCFL_RESET;
			if (res->r2_npage)
				atomic64_inc(&rpc->r_ctx->r_recv_less);
			else
				atomic64_inc(&rpc->r_ctx->r_recv_0);
		}
		rpc4->r4_npage = res->r2_npage;
		atomic64_add(res->r2_npage, &rpc->r_ctx->r_recv_page);
	}
	if (rpc3->r3_done - 1 == rpc3->r3_nwr) {
		uint64_t tm = get_cycles();
		atomic64_inc(&rpc->r_ctx->r_recv_res);
		tm -= rpc->r_time[GFCC_TM_RECV_IB];
		atomic64_add(tm, &rpc->r_ctx->r_time[GFCC_TM_RECV_IB]);
		goto end_done;
	}
	return (0);
end_err:
	atomic64_inc(&rpc->r_ctx->r_recv_err);
	rpc3->r3_flags |= GFCC_RPCFL_RESET;
end_done:
	gfcc_rpc_list_del(rpc, rpc->r_ctx);
	gflog_debug(GFARM_MSG_1004896, "rpc3(%p) done", rpc3);
	GFCC_MUTEX_LOCK(&rpc3->r3_lock);
	if (rpc4->r4_cb)
		rpc4->r4_cb(rpc, rpc3->r3_err);
	rpc3->r3_status = GFCC_RPCST_DONE;
	if (rpc4->r4_status == GFCC_RPCST_WAIT) {
		GFCC_COND_SIGNAL(&rpc3->r3_wait, &rpc3->r3_lock);
	}
	GFCC_MUTEX_UNLOCK(&rpc3->r3_lock);
	gfcc_rpc_put(rpc);	/* for call back */
	return (0);
}

static int
rpc3_send_read_req(struct gfcc_rpc *rpc)
{
	struct gfcc_rpc4_req *rpc4 = &rpc->r_rpc4;
	struct gfcc_rpc3_req *rpc3 = &rpc->r_rpc3;
	struct gfcc_rpc2_req *rpc2 = (struct gfcc_rpc2_req *)rpc->r_reqbuf;
	struct ib_ah *ah = NULL;
	int err, npage;

	rpc3->r3_status = GFCC_RPCST_INIT;
	rpc3->r3_cmd = GFCC_RPC_READ_REQ;

	if (!(ah = gfcc_find_ah(rpc->r_ctx, rpc4->r4_ibaddr, 1)))
		goto err_end;
	if (rpc4->r4_npage > GFCC_PAGES_MAX) {
		npage = GFCC_PAGES_MAX;
		gflog_warning(GFARM_MSG_1004897, "shrink page %d -> %d",
			rpc4->r4_npage, GFCC_PAGES_MAX);
	} else
		npage = rpc4->r4_npage;
	rpc2->r2_npblk = GFCC_PBLK_MAX;
	if ((npage = gfcc_map_recv_pages(rpc->r_ctx, npage, rpc3->r3_dma_page,
		&rpc2->r2_npblk, rpc2->r2_pblk, rpc4->r4_pages,
				DMA_FROM_DEVICE)) < 1) {
		gflog_error(GFARM_MSG_1004898, "can't map");
		err = ENOMEM;
		goto err_end;
	}
	rpc2->r2_npage = npage;
	rpc3->r3_cb = rpc3_send_read_req_cb;
	rpc->r_rpc2 = rpc2;

	if ((err = gfcc_post_recv_pages(rpc)))
		goto err_end;
	gfcc_rpc_put(rpc);	/* no need cb */
	atomic64_inc(&rpc->r_ctx->r_send_req);
	atomic64_add(npage, &rpc->r_ctx->r_req_page);

	rpc3->r3_nsend = 1;
	memcpy(rpc2->r2_ver, GFCC_RPC2_VER, strlen(GFCC_RPC2_VER));
	rpc2->r2_id = (uint64_t) rpc;
	rpc2->r2_qkey = (uint64_t) rpc->r_ctx->param->qkey;
	rpc2->r2_psize = page_size;
	rpc2->r2_cmd = GFCC_RPC_READ_REQ;
	rpc2->r2_obj = *rpc4->r4_obj;

	rpc3->r3_status = rpc4->r4_status;
	gfcc_rpc_list_add(rpc, rpc->r_ctx);
	rpc->r_time[GFCC_TM_RECV_IB] = get_cycles();
	if ((err = gfcc_post_send(rpc, rpc3->r3_qp, sizeof(*rpc2) +
		sizeof(struct gfcc_pblk) * (rpc2->r2_npblk - 1), ah))) {
		gfcc_rpc_list_del(rpc, rpc->r_ctx);
		goto err_end;
	}
	if (rpc4->r4_status != GFCC_RPCST_WAIT) {
		return (0);
	}
	GFCC_MUTEX_LOCK(&rpc3->r3_lock);
	err = GFCC_COND_WAIT(rpc3->r3_wait, &rpc3->r3_lock,
				rpc3->r3_status != GFCC_RPCST_WAIT);
	GFCC_MUTEX_UNLOCK(&rpc3->r3_lock);

	if (!err)
		err = rpc3->r3_err;
	if (!err) {
		struct gfcc_rpc2_res *res = (struct gfcc_rpc2_res *)
				(rpc->r_resbuf + GFCC_UD_GRH);
		gfsk_validate_pages(rpc4->r4_pages, res->r2_npage, 0);
		rpc4->r4_npage = res->r2_npage;
	}
err_end:
	rpc3->r3_status = GFCC_RPCST_DONE;

	return (err);
}
static int
rpc4_recv_read_req_cb(struct gfcc_rpc *rpc, int err)
{
	struct gfcc_rpc4_req *rpc4 = &rpc->r_rpc4;

	gflog_debug(GFARM_MSG_1004899, "rpc4(%p) err=%d", rpc4, err);
	return (0);
}

static int
rpc3_recv_read_req(struct gfcc_rpc *rpc, struct gfcc_ibaddr *ibaddr)
{
	int	err = 0, err0;
	struct gfcc_obj *obj;
	struct gfcc_rpc4_req *rpc4 = &rpc->r_rpc4;
	struct gfcc_rpc2_req *rpc2 = rpc->r_rpc2;

	rpc4->r4_pages = gfcc_pages_alloc(0);
	if (!rpc4->r4_pages)
		err0 = ENOMEM;
	else {
		if (rpc2->r2_psize != page_size)
			err0 = ENXIO;
		else {
			obj = &rpc2->r2_obj;
			rpc4->r4_npage = rpc2->r2_npage;
			rpc4->r4_pages->cp_obj = *obj;
			err0 = gfsk_find_pages(rpc->r_ctx, rpc2->r2_npblk,
				rpc2->r2_pblk, rpc4->r4_pages, &rpc4->r4_npage);
		}
	}
	rpc4->r4_rid = 0;
	rpc4->r4_status = GFCC_RPCST_ASYNC;
	rpc4->r4_cmd = GFCC_RPC_READ_RES;
	rpc4->r4_ibaddr = ibaddr;
	rpc4->r4_err = 0;
	rpc4->r4_cb = rpc4_recv_read_req_cb;
	err = rpc3_send_read_res(rpc, err0);
	return (err);
}
static int
gfsk_cc_read_cb(struct gfcc_rpc *rpc, int err)
{
	struct gfcc_rpc4_req *rpc4 = &rpc->r_rpc4;
	uint64_t tm;

	tm = get_cycles() - rpc->r_time[GFCC_TM_SEND_REQ];
	atomic64_add(tm, &rpc->r_ctx->r_time[GFCC_TM_SEND_REQ]);
	gfsk_validate_pages(rpc4->r4_pages, err ? 0 : rpc4->r4_npage,
			(rpc4->r4_status == GFCC_RPCST_ASYNC));

	return (0);
}
int
gfsk_cc_read(struct gfcc_ctx *ctx, struct gfcc_ibaddr *ibaddr,
	struct gfcc_pages *pages, int async)
{
	struct gfcc_rpc *rpc;
	struct gfcc_rpc4_req *rpc4;
	int npage, err;

	if (!(rpc = gfcc_rpc_alloc(ctx, 0)))
		goto err_end;
	rpc->r_time[GFCC_TM_SEND_REQ] = get_cycles();
	rpc4 = &rpc->r_rpc4;
	rpc4->r4_rid = 0;
	rpc4->r4_pages = pages;
	rpc4->r4_status = async ? GFCC_RPCST_ASYNC : GFCC_RPCST_WAIT;
	rpc4->r4_cmd = GFCC_RPC_READ_REQ;
	rpc4->r4_ibaddr = ibaddr;
	rpc4->r4_err = 0;
	rpc4->r4_cb = gfsk_cc_read_cb;
	npage = rpc4->r4_npage = pages->cp_npage;	/* original request */
	rpc4->r4_obj = &pages->cp_obj;

	if ((err = rpc3_send_read_req(rpc)))
		goto err_end;
	if (async)
		npage = 0;
	else
		npage = pages->cp_npage;
err_end:
	if ((!async || err) && rpc)
		rpc4->r4_pages = NULL;	/* caller use it */
	gfcc_rpc_put(rpc);	/* for allocator */

	return (npage);
}
static void
gfcc_ib_reap_task(struct work_struct *work)
{
	struct gfcc_ctx *ctx = container_of(work, struct gfcc_ctx,
				reap_dwork.work);
	GFSK_CTX_DECLARE_FSP(ctx->fs_ctxp);

	GFSK_CTX_SET();
	gfcc_rpc_list_check(ctx);
	GFSK_CTX_UNSET();
}
static void
gfcc_ib_recv_task(struct work_struct *work)
{
	struct gfcc_ctx *ctx = container_of(work, struct gfcc_ctx, recv_task);
	GFSK_CTX_DECLARE_FSP(ctx->fs_ctxp);

	GFSK_CTX_SET();
	rpc2_poll(ctx, ctx->rcq);
	GFSK_CTX_UNSET();
	ib_req_notify_cq(ctx->rcq, IB_CQ_NEXT_COMP);
}
static void
gfcc_ib_recv_comp(struct ib_cq *cq, void *ctx_ptr)
{
	struct gfcc_ctx *ctx = (struct gfcc_ctx *)ctx_ptr;

	queue_work(ctx->workqueue, &ctx->recv_task);
}
static void
gfcc_ib_send_task(struct work_struct *work)
{
	struct gfcc_ctx *ctx = container_of(work, struct gfcc_ctx, send_task);
	GFSK_CTX_DECLARE_FSP(ctx->fs_ctxp);

	GFSK_CTX_SET();
	rpc2_poll(ctx, ctx->scq);
	GFSK_CTX_UNSET();
	ib_req_notify_cq(ctx->scq, IB_CQ_NEXT_COMP);
}
static void
gfcc_ib_send_comp(struct ib_cq *cq, void *ctx_ptr)
{
	struct gfcc_ctx *ctx = (struct gfcc_ctx *)ctx_ptr;

	queue_work(ctx->workqueue, &ctx->send_task);
}
/******************************************************************************/
static int
gfcc_prstat(char *buf, int size, struct gfcc_ctx *ctx)
{
	long cycles_to_units;
	char *cp = buf;
	int n;

	cycles_to_units = cpufreq_quick_get(0) * 1000;
	if (!cycles_to_units)
		cycles_to_units = cpu_khz * 1000;
#define GFCC_MBPS(tm, np) ((tm) == 0 ? 0 : \
		((((long)(np) * page_size * cycles_to_units)/0x100000) / (tm)))

	n = snprintf(buf, size,
	"C: send_req=%ld recv_res=%ld recv_less=%ld recv_0=%ld recv_err=%ld "
	"recv_page=%ld msec=%ld req_page=%ld\n"
	"S: recv_req=%ld send_res=%ld send_err=%ld send_page=%ld msec=%ld "
	"err=%ld\n",
		atomic64_read(&ctx->r_send_req),
		atomic64_read(&ctx->r_recv_res),
		atomic64_read(&ctx->r_recv_less),
		atomic64_read(&ctx->r_recv_0),
		atomic64_read(&ctx->r_recv_err),
		atomic64_read(&ctx->r_recv_page),
		atomic64_read(&ctx->r_time[GFCC_TM_SEND_REQ])
			/ (cycles_to_units/1000),
		atomic64_read(&ctx->r_req_page),
		atomic64_read(&ctx->r_recv_req),
		atomic64_read(&ctx->r_send_res),
		atomic64_read(&ctx->r_send_err),
		atomic64_read(&ctx->r_send_page),
		atomic64_read(&ctx->r_time[GFCC_TM_RECV_REQ])
			/ (cycles_to_units/1000),
		atomic64_read(&ctx->r_err)
		);


	n += snprintf(cp + n, size - n,
		"P: size=%d iter=%d send_req=%ld recv_ib=%ld recv_req=%ld\n",
		ctx->param->mtu, 0,
		GFCC_MBPS(atomic64_read(&ctx->r_time[GFCC_TM_SEND_REQ]),
			atomic64_read(&ctx->r_recv_page)),
		GFCC_MBPS(atomic64_read(&ctx->r_time[GFCC_TM_RECV_IB]),
			atomic64_read(&ctx->r_recv_page)),
		GFCC_MBPS(atomic64_read(&ctx->r_time[GFCC_TM_RECV_REQ]),
			atomic64_read(&ctx->r_send_page)));

	return (n);
}
