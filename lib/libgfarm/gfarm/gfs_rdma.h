/*
 * written by Shin Sasaki and Kazushi Takahashi (2015)
 */

struct rdma_context;
struct ibv_mr;

#define GFS_PROTO_MAX_RDMASIZE 0x1000000
#define GFARM_RDMA_REG_MR_STATIC	0x1
#define GFARM_RDMA_REG_MR_DYNAMIC	0x2
#define GFARM_RDMA_REG_MR_FAIL		0x4
#define GFARM_RDMA_MIN_SIZE		0x10000
#define GFARM_RDMA_REG_MIN_SIZE		0x100000
#define GFARM_RDMA_REG_MAX_SIZE		0x1000000

gfarm_error_t gfs_ib_rdma_initialize(int stayopen);
gfarm_error_t gfs_rdma_init(int is_server, struct rdma_context **ctx);
gfarm_error_t gfs_rdma_finish(struct rdma_context *ctx);
gfarm_error_t gfs_rdma_connect(struct rdma_context *ctx);

gfarm_error_t gfs_rdma_reg_mr_remote_read_write(struct rdma_context *,
		void *, ssize_t, void **);
gfarm_error_t gfs_rdma_dereg_mr(struct rdma_context *, void **);

void gfs_rdma_set_remote_lid(struct rdma_context *, gfarm_uint32_t);
void gfs_rdma_set_remote_qpn(struct rdma_context *, gfarm_uint32_t);
void gfs_rdma_set_remote_psn(struct rdma_context *, gfarm_uint32_t);
void gfs_rdma_set_remote_gid(struct rdma_context *ctx, void *buf);
void *gfs_rdma_get_remote_gid(struct rdma_context *ctx);

gfarm_uint32_t gfs_rdma_get_local_lid(struct rdma_context *);
gfarm_uint32_t gfs_rdma_get_local_qpn(struct rdma_context *);
gfarm_uint32_t gfs_rdma_get_local_psn(struct rdma_context *);
void *gfs_rdma_get_local_gid(struct rdma_context *ctx);
gfarm_uint32_t gfs_rdma_get_rkey(struct ibv_mr *);
gfarm_uint64_t gfs_rdma_get_addr(struct rdma_context *ctx);

int gfs_rdma_resize_buffer(struct rdma_context *ctx, int size);
unsigned char *gfs_rdma_get_buffer(struct rdma_context *ctx);
int gfs_rdma_get_bufsize(struct rdma_context *ctx);
int gfs_rdma_get_bufinfo(struct rdma_context *ctx,
	void **bufp, int *sizep, gfarm_uint32_t *rkeyp);

unsigned long gfs_rdma_get_mlock_limit(void);
struct ibv_mr *gfs_rdma_get_mr(struct rdma_context *ctx);
int gfs_rdma_get_gid_size(void);

void gfs_rdma_enable(struct rdma_context *);
void gfs_rdma_disable(struct rdma_context *);
int gfs_rdma_check(struct rdma_context *);

gfarm_error_t gfs_rdma_remote_write(struct rdma_context *ctx,
	gfarm_uint32_t rkey, gfarm_uint64_t remote_addr, ssize_t remote_size);
gfarm_error_t gfs_rdma_remote_read(struct rdma_context *ctx,
	gfarm_uint32_t rkey, gfarm_uint64_t remote_addr, ssize_t remote_size);
gfarm_error_t gfs_ib_rdma_disable(void);

