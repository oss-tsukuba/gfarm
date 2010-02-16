/*
 * $Id$
 */

void host_init(void);

struct host;
struct sockaddr;
struct peer;
struct callout;

struct host_status {
	double loadavg_1min, loadavg_5min, loadavg_15min;
	gfarm_off_t disk_used, disk_avail;
};

struct host *host_lookup(const char *);
struct host *host_addr_lookup(const char *, struct sockaddr *);
void host_peer_set(struct host *, struct peer *, int, struct callout *);
void host_peer_unset(struct host *);
void host_peer_disconnect(struct host *);
struct callout *host_status_callout(struct host *);
int host_peer_unset_pending(struct host *);
struct peer *host_peer(struct host *);
char *host_name(struct host *);
int host_port(struct host *);
int host_is_up(struct host *);

struct file_replicating {
	/*
	 * end marker:
	 *	{fr->prev_inode, fr->next_inode}
	 *	== &fr->dst->replicating_inodes
	 */
	struct file_replicating *prev_inode, *next_inode;

	/*
	 * end marker:
	 *	{fr->prev_host, fr->next_host}
	 *	== &fr->inode->u.c.s.f.rstate->replicating_hosts
	 */
	struct file_replicating *prev_host, *next_host;

	struct host *dst;
	gfarm_int64_t handle;

	struct inode *inode;
	gfarm_int64_t igen; /* generation when replication started */
};

struct file_replicating *host_replicating_new(struct host *);
void host_replicating_free(struct file_replicating *);
void file_replicating_set_handle(struct file_replicating *, gfarm_int64_t);
gfarm_int64_t file_replicating_get_handle(struct file_replicating *);
gfarm_error_t host_replicated(struct host *, gfarm_int32_t,
	gfarm_int64_t, gfarm_off_t);

gfarm_error_t host_remove_replica_enq(
	struct host *, gfarm_ino_t, gfarm_uint64_t);
int host_count_dead_copies_all(gfarm_ino_t, int);
gfarm_error_t host_dead_copies_info_all(gfarm_ino_t, int,
	int *, char **, gfarm_int64_t *, gfarm_int32_t *);

gfarm_error_t host_remove_replica(struct host *, struct timespec *);
void host_remove_replica_dump_all(void);
void host_status_update(struct host *, struct host_status *);
void host_status_disable(struct host *);
gfarm_error_t host_update_status(struct host *);

gfarm_error_t gfm_server_host_info_get_all(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_get_by_architecture(struct peer *, int,int);
gfarm_error_t gfm_server_host_info_get_by_names(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_get_by_namealiases(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_set(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_modify(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_remove(struct peer *, int, int);

gfarm_error_t host_schedule_reply_n(struct peer *, gfarm_int32_t,const char *);
gfarm_error_t host_schedule_reply(struct host *, struct peer *, const char *);
gfarm_error_t host_schedule_reply_all(struct peer *, const char *,
	int (*)(struct host *, void *), void *);
gfarm_error_t host_schedule_reply_one_or_all(struct peer *, const char *);

gfarm_error_t gfm_server_hostname_set(struct peer *, int, int);
gfarm_error_t gfm_server_schedule_host_domain(struct peer *, int, int);
gfarm_error_t gfm_server_statfs(struct peer *, int, int);


/* exported for a use from a private extension */
struct gfp_xdr;
gfarm_error_t host_info_send(struct gfp_xdr *, struct host *);
gfarm_error_t host_info_remove_default(const char *, const char *);
extern gfarm_error_t (*host_info_remove)(const char *, const char *);
