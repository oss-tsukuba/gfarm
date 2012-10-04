struct file_replication;
struct inode_replication_state;
struct peer;
struct host;
struct inode;
struct dead_file_copy;

struct host *file_replication_get_dst(struct file_replication *);
struct inode *file_replication_get_inode(struct file_replication *);
gfarm_uint64_t file_replication_get_gen(struct file_replication *);
struct dead_file_copy *file_replication_get_dead_file_copy(
	struct file_replication *);

gfarm_int64_t file_replication_get_handle(struct file_replication *);

int file_replication_is_busy(struct host *);
void file_replication_start(struct inode_replication_state *, gfarm_uint64_t);
void file_replication_close_check(struct inode_replication_state **);

gfarm_error_t gfm_async_server_replication_result(struct host *,
	struct peer *, gfp_xdr_xid_t, size_t);

struct inode_replication_state;
gfarm_error_t file_replication_new(struct inode *, gfarm_uint64_t,
	struct host *, struct host *, struct dead_file_copy *,
	struct inode_replication_state **, struct file_replication **);
void file_replication_free(struct file_replication *,
	struct inode_replication_state **);

void file_replication_init(void);

struct netsendq_type;
extern struct netsendq_type gfs_proto_replication_request_queue;
