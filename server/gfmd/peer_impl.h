struct peer;
struct local_peer;
struct remote_peer;
struct cookie;

struct peer_ops {
	/* downcast functions */
	struct local_peer *(*peer_to_local_peer)(struct peer *);
	struct remote_peer *(*peer_to_remote_peer)(struct peer *);

	struct gfp_xdr *(*get_conn)(struct peer *);
	gfp_xdr_async_peer_t (*get_async)(struct peer *);
	gfarm_error_t (*get_port)(struct peer *, int *);
	int (*is_busy)(struct peer *);
	void (*notice_disconnected)(struct peer *, const char *, const char *);
	void (*shutdown)(struct peer *);
	void (*free)(struct peer *);
};

struct cookie {
	gfarm_uint64_t id;
	GFARM_HCIRCLEQ_ENTRY(cookie) hcircleq;
};

struct peer {
	struct peer_ops *ops;

	struct peer *next_close;
	int refcount, free_requested;

	enum gfarm_auth_id_type id_type;
	char *username, *hostname;
	struct user *user;
	struct abstract_host *host;

	struct process *process;
	int protocol_error;
	pthread_mutex_t protocol_error_mutex;

	struct protocol_state pstate;

	gfarm_int32_t fd_current, fd_saved;
	int flags;
#define PEER_FLAGS_FD_CURRENT_EXTERNALIZED	1
#define PEER_FLAGS_FD_SAVED_EXTERNALIZED	2

	struct inum_path_array *findxmlattrctx;

	/* only one pending GFM_PROTO_GENERATION_UPDATED per peer is allowed */
	struct inode *pending_new_generation;

	union {
		struct {
			/* only used by "gfrun" client */
			struct job_table_entry *jobs;
		} client;
	} u;

	/* the followings are only used for gfsd back channel */
	pthread_mutex_t replication_mutex;
	int simultaneous_replication_receivers;
	struct file_replicating replicating_inodes; /* dummy header */

	GFARM_HCIRCLEQ_HEAD(cookie) cookies;

	/*
	 * to support remote peer
	 */
	gfarm_int64_t peer_id;
};

void peer_construct_common(struct peer *, struct peer_ops *ops, const char *);
void peer_clear_common(struct peer *);
void peer_free_common(struct peer *, const char *);
void peer_closer_wakeup(struct peer *);
