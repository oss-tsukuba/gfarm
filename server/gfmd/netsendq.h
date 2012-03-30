/* for struct mdhost */
#define NETSENDQ_TYPE_GFM_REPLY				0
#define NETSENDQ_TYPE_GFM_PROTO_JOURNAL_READY_TO_RECV	1
#define NETSENDQ_TYPE_GFM_PROTO_JOURNAL_SEND		2
#define NETSENDQ_TYPE_GFM_PROTO_PEER_FREE		3
#define NETSENDQ_TYPE_GFM_PROTO_PEER_ALLOC		4
#define NETSENDQ_TYPE_GFM_PROTO_GFS_RPC			5
#define NETSENDQ_TYPE_GFM_PROTO_RPC			6
#define NETSENDQ_TYPE_GFM_PROTO_NUM_TYPES		7
/* for struct host */
#define NETSENDQ_TYPE_GFS_PROTO_STATUS			0
#define NETSENDQ_TYPE_GFS_REPLY				1
#define NETSENDQ_TYPE_GFS_PROTO_FHREMOVE		2
#define NETSENDQ_TYPE_GFS_PROTO_REPLICATION_REQUEST	3
#define NETSENDQ_TYPE_GFS_PROTO_FHSTAT			4
/*#define NETSENDQ_TYPE_GFS_PROTO_REPLICATION_CANCEL*/
#define NETSENDQ_TYPE_GFS_PROTO_NUM_TYPES		5

/* struct netsendq_type::flags */
#define	NETSENDQ_FLAG_QUEUEABLE_IF_DOWN			1
#define NETSENDQ_FLAG_PRIOR_ONE_SHOT			2

struct netsendq_type;
struct netsendq_manager;
struct netsendq;
struct netsendq_entry;

gfarm_error_t netsendq_add_entry(struct netsendq *, struct netsendq_entry *,
	int);
/* use a thread to handle an error, instead of returning the error code */
#define NETSENDQ_ADD_FLAG_DETACH_ERROR_HANDLING		1

void netsendq_remove_entry(struct netsendq *, struct netsendq_entry *,
	gfarm_error_t);

gfarm_error_t netsendq_new(struct netsendq_manager *, struct abstract_host *,
	struct netsendq **);
void netsendq_was_sent_to_host(struct netsendq *);
void netsendq_host_becomes_down(struct netsendq *);
void netsendq_host_becomes_up(struct netsendq *);

struct netsendq_manager *netsendq_manager_new(
	int, const struct netsendq_type *const *, int, int, const char *);
