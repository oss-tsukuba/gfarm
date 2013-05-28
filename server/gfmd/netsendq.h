/*
 * Netsendq types for 'struct mdhost'.
 */
#ifdef not_def_REPLY_QUEUE
#define NETSENDQ_TYPE_GFM_PROTO_REPLY_TO_GFMD		0
#define NETSENDQ_TYPE_GFM_PROTO_JOURNAL_READY_TO_RECV	1
#endif

#if 1
/* Not used currently. */
#define NETSENDQ_TYPE_GFM_PROTO_NUM_TYPES		0
#else
#define NETSENDQ_TYPE_GFM_PROTO_JOURNAL_SEND		0
#define NETSENDQ_TYPE_GFM_PROTO_PEER_FREE		1
#define NETSENDQ_TYPE_GFM_PROTO_PEER_ALLOC		2
#define NETSENDQ_TYPE_GFM_PROTO_GFS_RPC			3
#define NETSENDQ_TYPE_GFM_PROTO_RPC			4
#define NETSENDQ_TYPE_GFM_PROTO_NUM_TYPES		5
#endif

/*
 * Netsendq types for 'struct host'.
 */
#define NETSENDQ_TYPE_GFS_PROTO_STATUS			0
#ifdef not_def_REPLY_QUEUE
#define NETSENDQ_TYPE_GFM_PROTO_REPLY_TO_GFSD		1
#endif
#define NETSENDQ_TYPE_GFS_PROTO_FHREMOVE		1
#define NETSENDQ_TYPE_GFS_PROTO_REPLICATION_REQUEST	2
#if 1
#define NETSENDQ_TYPE_GFS_PROTO_NUM_TYPES		3
#else /* 0 */
#define NETSENDQ_TYPE_GFS_PROTO_REPLICATION_CANCEL	3
#define NETSENDQ_TYPE_GFS_PROTO_FHSTAT			4
#define NETSENDQ_TYPE_GFS_PROTO_NUM_TYPES		5
#endif /* 0 */

/* struct netsendq_type::flags */
#define	NETSENDQ_FLAG_QUEUEABLE_IF_DOWN			1
#define NETSENDQ_FLAG_PRIOR_ONE_SHOT			2

struct netsendq_type;
struct netsendq_manager;
struct netsendq;
struct netsendq_entry;

int netsendq_window_is_full(struct netsendq *, struct netsendq_type *);
gfarm_error_t netsendq_add_entry(struct netsendq *, struct netsendq_entry *,
	int);
/* use a thread to handle an error, instead of returning the error code */
#define NETSENDQ_ADD_FLAG_DETACH_ERROR_HANDLING		1

void netsendq_entry_was_sent(struct netsendq *, struct netsendq_entry *);
void netsendq_remove_entry(struct netsendq *, struct netsendq_entry *,
	gfarm_error_t);

gfarm_error_t netsendq_new(struct netsendq_manager *, struct abstract_host *,
	struct netsendq **);
void netsendq_host_remove(struct netsendq *);
void netsendq_host_becomes_down(struct netsendq *);
void netsendq_host_becomes_up(struct netsendq *);

struct netsendq_manager *netsendq_manager_new(
	int, const struct netsendq_type *const *, int, int, const char *);
struct thread_pool *netsendq_manager_get_thrpool(struct netsendq_manager *);
