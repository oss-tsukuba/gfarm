/*
 * $Id$
 */

struct mdhost;
struct peer;
struct abstract_host;
struct gfarm_metadb_server;

struct abstract_host *mdhost_to_abstract_host(struct mdhost *);

typedef gfarm_error_t (*mdhost_modify_hook_t)(struct mdhost *);

/* for gfmd_channel.c */
void mdhost_set_update_hook_for_journal_send(void (*)(void));

void mdhost_set_switch_to_sync_hook(mdhost_modify_hook_t closure);
void mdhost_set_switch_to_async_hook(mdhost_modify_hook_t closure);

void mdhost_init(void);
void mdhost_initial_entry(void);
const char *mdhost_get_name(struct mdhost *);
const char *mdhost_get_name_unlocked(struct mdhost *);
int mdhost_get_port(struct mdhost *);
int mdhost_is_self(struct mdhost *);
struct mdhost *mdhost_lookup(const char *);
struct mdhost *mdhost_lookup_self(void);
void mdhost_foreach(int (*)(struct mdhost *, void *), void *);
int mdhost_self_is_master(void);
int mdhost_self_is_readonly(void);
int mdhost_self_is_readonly_unlocked(void);

struct peer;
gfarm_error_t gfm_server_metadb_server_get(struct peer *, int, int);
gfarm_error_t gfm_server_metadb_server_get_all(struct peer *, int, int);
gfarm_error_t gfm_server_metadb_server_set(struct peer *, int, int);
gfarm_error_t gfm_server_metadb_server_modify(struct peer *, int, int);
gfarm_error_t gfm_server_metadb_server_remove(struct peer *, int, int);

void mdhost_set_self_as_master(void);
void mdhost_set_self_as_default_master(void);
void mdhost_read_only_disabled();
int mdhost_is_sync_replication(struct mdhost *);
int mdhost_get_flags(struct mdhost *);
void mdhost_set_seqnum_unknown(struct mdhost *);
void mdhost_set_seqnum_out_of_sync(struct mdhost *);
void mdhost_set_seqnum_ok(struct mdhost *);
void mdhost_set_seqnum_error(struct mdhost *);
void mdhost_set_seqnum_behind(struct mdhost *);
void mdhost_set_seqnum_state_by_error(struct mdhost *, gfarm_error_t);
int mdhost_may_transfer_journal(struct mdhost *);
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
void mdhost_put_peer(struct mdhost *, struct peer *);
#endif
int mdhost_has_async_replication_target(void);
int mdhost_is_master(struct mdhost *);
void mdhost_set_is_master(struct mdhost *, int);
struct mdcluster *mdhost_get_cluster_unlocked(struct mdhost *);
void mdhost_set_cluster_unlocked(struct mdhost *, struct mdcluster *);
const char *mdhost_get_cluster_name(struct mdhost *);
const char *mdhost_get_cluster_name_unlocked(struct mdhost *);
void mdhost_activate(struct mdhost *);
void mdhost_set_peer(struct mdhost *, struct peer *, int);
int mdhost_is_up(struct mdhost *);
void mdhost_disconnect_request(struct mdhost *, struct peer *);
void mdhost_master_disconnect_request(struct peer *);
gfarm_error_t mdhost_enter(struct gfarm_metadb_server *, struct mdhost **);
gfarm_error_t mdhost_modify_in_cache(struct mdhost *,
	struct gfarm_metadb_server *);
gfarm_error_t mdhost_remove_in_cache(const char *);
struct mdhost *mdhost_lookup_master(void);
struct mdhost *mdhost_lookup_metadb_server(struct gfarm_metadb_server *);
int mdhost_self_is_master_candidate(void);
