struct peer;
struct thread_pool;
struct abstract_host;
struct local_peer;
struct remote_peer;

enum peer_type {
	peer_type_foreground_channel,
	peer_type_back_channel,
	peer_type_gfmd_channel
};

struct local_peer *peer_to_local_peer(struct peer *);
struct remote_peer *peer_to_remote_peer(struct peer *);

#ifdef PEER_REFCOUNT_DEBUG
void peer_add_ref_impl(struct peer *, const char *, int, const char *);
int peer_del_ref_impl(struct peer *, const char *, int, const char *);
#define peer_add_ref(peer) peer_add_ref_impl(peer, __FILE__, __LINE__, __func__)
#define peer_del_ref(peer) peer_del_ref_impl(peer, __FILE__, __LINE__, __func__)
#else
void peer_add_ref(struct peer *);
int peer_del_ref(struct peer *);
#endif
void peer_free_request(struct peer *);

void peer_init(void);

void peer_free(struct peer *);
const char *peer_get_service_name(struct peer *);

struct peer *peer_by_fd(int);
gfarm_error_t peer_free_by_fd(int);

struct gfp_xdr *peer_get_conn(struct peer *);

/* (struct gfp_xdr_aync_peer *) == gfp_xdr_async_peer_t XXX  */
struct gfp_xdr_async_peer;
struct gfp_xdr_async_peer *peer_get_async(struct peer *);

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
enum peer_type peer_get_peer_type(struct peer *);
void peer_set_peer_type(struct peer *, enum peer_type);

struct sockaddr;
void peer_authorized_common(struct peer *, enum gfarm_auth_id_type, char *,
	char *, struct sockaddr *, enum gfarm_auth_method);

struct inode;
void peer_set_pending_new_generation_by_fd(struct peer *, struct inode *);
void peer_reset_pending_new_generation_by_fd(struct peer *);
gfarm_error_t peer_add_pending_new_generation_by_cookie(
	struct peer *, struct inode *, gfarm_uint64_t *);
int peer_remove_pending_new_generation_by_cookie(struct peer *, gfarm_uint64_t,
	struct inode **);
void peer_unset_pending_new_generation(struct peer *, gfarm_error_t);

struct process;
struct process *peer_get_process(struct peer *);
void peer_set_process(struct peer *, struct process *);
void peer_unset_process(struct peer *);

void peer_record_protocol_error(struct peer *);
int peer_had_protocol_error(struct peer *);

struct protocol_state;
struct protocol_state *peer_get_protocol_state(struct peer *);

/* XXX */
struct job_table_entry;
struct job_table_entry **peer_get_jobs_ref(struct peer *);

void peer_fdpair_clear(struct peer *);
gfarm_error_t peer_fdpair_externalize_current(struct peer *);
gfarm_error_t peer_fdpair_close_current(struct peer *);
void peer_fdpair_set_current(struct peer *, gfarm_int32_t);
gfarm_error_t peer_fdpair_get_current(struct peer *, gfarm_int32_t *);
gfarm_error_t peer_fdpair_get_saved(struct peer *, gfarm_int32_t *);
gfarm_error_t peer_fdpair_save(struct peer *);
gfarm_error_t peer_fdpair_restore(struct peer *);

struct inum_path_array;
void peer_findxmlattrctx_set(struct peer *, struct inum_path_array *);
struct inum_path_array *peer_findxmlattrctx_get(struct peer *);

void peer_stat_add(struct peer *, unsigned int, int);

gfarm_error_t peer_get_port(struct peer *, int *);
gfarm_int64_t peer_get_private_peer_id(struct peer *);
void peer_set_private_peer_id(struct peer *);
struct peer* peer_get_parent(struct peer *);

/* only used by gfmd channel */
struct gfmdc_peer_record *peer_get_gfmdc_record(struct peer *);
void peer_set_gfmdc_record(struct peer *, struct gfmdc_peer_record *);
