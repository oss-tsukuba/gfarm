struct peer_watcher;
struct peer;
struct thread_pool;
struct abstract_host;

void peer_watcher_set_default_nfd(int);
struct peer_watcher *peer_watcher_alloc(int, int, void *(*)(void *),
	const char *);
struct thread_pool *peer_watcher_get_thrpool(struct peer_watcher *);

#ifdef PEER_REFCOUNT_DEBUG
void peer_add_ref_impl(struct peer *, const char *, int, const char *);
int peer_del_ref_impl(struct peer *, const char *, int, const char *);
#define peer_add_ref(peer) peer_add_ref_impl(peer, __FILE__, __LINE__, __func__)
#define peer_del_ref(peer) peer_del_ref_impl(peer, __FILE__, __LINE__, __func__)
#else
void peer_add_ref(struct peer *);
int peer_del_ref(struct peer *);
#endif
void peer_add_ref_for_replication(struct peer *);
int peer_del_ref_for_replication(struct peer *);
void peer_free_request(struct peer *);

void peer_init(int);

gfarm_error_t peer_alloc(int, struct peer **);
struct gfp_xdr;
gfarm_error_t peer_alloc_with_connection(struct peer **, struct gfp_xdr *,
	struct abstract_host *, int);
void peer_authorized(struct peer *,
	enum gfarm_auth_id_type, char *, char *, struct sockaddr *,
	enum gfarm_auth_method, struct peer_watcher *);
void peer_free(struct peer *);
void peer_shutdown_all(void);
void peer_invoked(struct peer *);
void peer_watch_access(struct peer *);
const char *peer_get_service_name(struct peer *);

struct peer *peer_by_fd(int);
gfarm_error_t peer_free_by_fd(int);

struct gfp_xdr *peer_get_conn(struct peer *);
int peer_get_fd(struct peer *);
gfarm_error_t peer_get_port(struct peer *, int *);

/* (struct gfp_xdr_aync_peer *) == gfp_xdr_async_peer_t XXX  */
struct gfp_xdr_async_peer;
void peer_set_async(struct peer *, struct gfp_xdr_async_peer *);
struct gfp_xdr_async_peer *peer_get_async(struct peer *);
void peer_set_free_async(void (*)(struct peer *, struct gfp_xdr_async_peer *));

gfarm_error_t peer_set_host(struct peer *, char *);
enum gfarm_auth_id_type peer_get_auth_id_type(struct peer *);
char *peer_get_username(struct peer *);
const char *peer_get_hostname(struct peer *);

struct user;
struct user *peer_get_user(struct peer *);
void peer_set_user(struct peer *, struct user *);
struct abstract_host;
struct abstract_host *peer_get_abstract_host(struct peer *);
struct host;
struct host *peer_get_host(struct peer *);
struct mdhost;
struct mdhost *peer_get_mdhost(struct peer *);

struct inode;
void peer_set_pending_new_generation(struct peer *, struct inode *);
void peer_reset_pending_new_generation(struct peer *);
void peer_unset_pending_new_generation(struct peer *);
gfarm_uint64_t peer_add_cookie(struct peer *);
int peer_delete_cookie(struct peer *, gfarm_uint64_t);

struct process;
struct process *peer_get_process(struct peer *);
void peer_set_process(struct peer *, struct process *);
void peer_unset_process(struct peer *, const char *diag);

void peer_record_protocol_error(struct peer *);
int peer_had_protocol_error(struct peer *);

void peer_set_watcher(struct peer *, struct peer_watcher *);

struct protocol_state;
struct protocol_state *peer_get_protocol_state(struct peer *);

/* XXX */
struct job_table_entry;
struct job_table_entry **peer_get_jobs_ref(struct peer *);

void peer_fdpair_clear(struct peer *, const char *);
gfarm_error_t peer_fdpair_externalize_current(struct peer *);
gfarm_error_t peer_fdpair_close_current(struct peer *);
void peer_fdpair_set_current(struct peer *, gfarm_int32_t, const char *);
gfarm_error_t peer_fdpair_get_current(struct peer *, gfarm_int32_t *);
gfarm_error_t peer_fdpair_get_saved(struct peer *, gfarm_int32_t *);
gfarm_error_t peer_fdpair_save(struct peer *, const char *);
gfarm_error_t peer_fdpair_restore(struct peer *, const char *);

void peer_findxmlattrctx_set(struct peer *, void *);
void *peer_findxmlattrctx_get(struct peer *);

void peer_stat_add(struct peer *, unsigned int, int);

struct dead_file_copy;
struct file_replicating {
	/*
	 * resources which are protected by the host::replication_mutex
	 */

	/*
	 * end marker:
	 *	{fr->prev_inode, fr->next_inode}
	 *	== &fr->dst->replicating_inodes
	 */
	struct file_replicating *prev_inode, *next_inode;

	/*
	 * resources which are protected by the giant_lock
	 */

	/*
	 * end marker:
	 *	{fr->prev_host, fr->next_host}
	 *	== &fr->inode->u.c.s.f.rstate->replicating_hosts
	 */
	struct file_replicating *prev_host, *next_host;

	struct peer *peer;
	struct host *dst;

	/*
	 * gfmd initialited replication: pid of destination side worker
	 * client initialited replication: -1
	 */
	gfarm_int64_t handle;

	struct inode *inode;
	gfarm_int64_t igen; /* generation when replication started */

	/*
	 * old generation which should be removed just after
	 * the completion of the replication,
	 * or, NULL
	 */
	struct dead_file_copy *cleanup;
};

void file_replicating_set_handle(struct file_replicating *, gfarm_int64_t);
gfarm_int64_t file_replicating_get_handle(struct file_replicating *);
struct peer *file_replicating_get_peer(struct file_replicating *);

gfarm_error_t peer_replicating_new(struct peer *, struct host *,
	struct file_replicating **);
void peer_replicating_free(struct file_replicating *);
gfarm_error_t peer_replicated(struct peer *,
	struct host *, gfarm_ino_t, gfarm_int64_t,
	gfarm_int64_t, gfarm_int32_t, gfarm_int32_t, gfarm_off_t);

/* only used by gfmd channel */
struct gfmdc_peer_record *peer_get_gfmdc_record(struct peer *);
void peer_set_gfmdc_record(struct peer *, struct gfmdc_peer_record *);
