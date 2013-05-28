/*
 * $Id$
 */

struct mdhost;
struct peer;
struct abstract_host;
struct gfarm_metadb_server;

struct abstract_host *mdhost_to_abstract_host(struct mdhost *);

/* for gfmd_channel.c */
void mdhost_set_update_hook_for_journal_send(void (*)(void));

void mdhost_init(void);
const char *mdhost_get_name(struct mdhost *);
int mdhost_get_port(struct mdhost *);
int mdhost_is_self(struct mdhost *);
struct mdhost *mdhost_lookup(const char *);
struct mdhost *mdhost_lookup_self(void);
void mdhost_foreach(int (*)(struct mdhost *, void *), void *);
int mdhost_self_is_master(void);
int mdhost_self_is_readonly(void);
int mdhost_self_is_readonly_unlocked(void);

struct peer;
gfarm_error_t gfm_server_metadb_server_get(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_metadb_server_get_all(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_metadb_server_set(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_metadb_server_modify(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_metadb_server_remove(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);

void mdhost_set_self_as_master(void);
void mdhost_set_self_as_default_master(void);
int mdhost_is_sync_replication(struct mdhost *);
int mdhost_get_flags(struct mdhost *);
void mdhost_set_seqnum_unknown(struct mdhost *);
void mdhost_set_seqnum_out_of_sync(struct mdhost *);
void mdhost_set_seqnum_ok(struct mdhost *);
void mdhost_set_seqnum_error(struct mdhost *);
void mdhost_set_seqnum_state_by_error(struct mdhost *, gfarm_error_t);
#ifdef PEER_REFCOUNT_DEBUG
struct peer *mdhost_get_peer_impl(struct mdhost *,
	const char *, int, const char *);
void mdhost_put_peer_impl(struct mdhost *, struct peer *,
	const char *, int, const char *);
#define mdhost_get_peer(mh) \
	mdhost_get_peer_impl(mh, __FILE__, __LINE__, __func__)
#define mdhost_put_peer(mh, peer) \
	mdhost_put_peer_impl(mh, peer, __FILE__, __LINE__, __func__)
#else
struct peer *mdhost_get_peer(struct mdhost *);
#endif
void mdhost_put_peer(struct mdhost *, struct peer *);
int mdhost_has_async_replication_target(void);
int mdhost_is_master(struct mdhost *);
void mdhost_set_is_master(struct mdhost *, int);
struct mdcluster *mdhost_get_cluster(struct mdhost *);
void mdhost_set_cluster(struct mdhost *, struct mdcluster *);
const char *mdhost_get_cluster_name(struct mdhost *);
void mdhost_activate(struct mdhost *);
void mdhost_set_peer(struct mdhost *, struct peer *, int);
int mdhost_is_up(struct mdhost *);
void mdhost_disconnect_request(struct mdhost *, struct peer *);
gfarm_error_t mdhost_enter(struct gfarm_metadb_server *, struct mdhost **);
gfarm_error_t mdhost_modify_in_cache(struct mdhost *,
	struct gfarm_metadb_server *);
gfarm_error_t mdhost_remove_in_cache(const char *);
struct mdhost *mdhost_lookup_master(void);
struct mdhost *mdhost_lookup_metadb_server(struct gfarm_metadb_server *);
int mdhost_get_count(void);
int mdhost_self_is_master_candidate(void);
struct thread_pool *mdhost_send_manager_get_thrpool(void);
