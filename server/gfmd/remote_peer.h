struct gfarm_thr_statewait;

struct peer *remote_peer_to_peer(struct remote_peer *);
enum peer_type remote_peer_get_peer_type(struct remote_peer *);
gfarm_int64_t remote_peer_get_remote_peer_id(struct remote_peer *);

gfarm_error_t remote_peer_alloc(struct peer *, gfarm_int64_t,
	gfarm_int32_t, char *, char *, enum gfarm_auth_method, int, int, int);
void remote_peer_free_simply(struct remote_peer *);
void remote_peer_for_each_sibling(struct remote_peer *,
	void (*)(struct remote_peer *));
struct remote_peer *remote_peer_id_lookup_from_siblings(struct remote_peer *,
	gfarm_int64_t);
gfarm_uint64_t remote_peer_get_db_update_seqnum(struct remote_peer *);
void remote_peer_set_db_update_seqnum(struct remote_peer *, gfarm_uint64_t);
gfarm_uint64_t remote_peer_get_db_update_flags(struct remote_peer *);
void remote_peer_merge_db_update_flags(struct remote_peer *, gfarm_uint64_t);
void remote_peer_clear_db_update_info(struct remote_peer *);
struct gfarm_thr_statewait *remote_peer_get_statewait(struct remote_peer *);
void remote_peer_set_received_remote_peer_free(struct remote_peer *);

