/*
 * $Id$
 */

void host_init(void);

struct host;
struct sockaddr;
struct peer;
struct callout;
struct dead_file_copy;

struct host_status {
	double loadavg_1min, loadavg_5min, loadavg_15min;
	gfarm_off_t disk_used, disk_avail;
};

struct host *host_lookup(const char *);
struct host *host_addr_lookup(const char *, struct sockaddr *);

int host_status_callout_retry(struct host *);
void host_peer_set(struct host *, struct peer *, int);
void host_disconnect(struct host *);
void host_disconnect_request(struct host *);
struct callout *host_status_callout(struct host *);
int host_peer_unset_pending(struct host *);
struct peer *host_peer(struct host *);
gfarm_error_t host_sender_lock(struct host *, struct peer **);
gfarm_error_t host_sender_trylock(struct host *, struct peer **);
void host_sender_unlock(struct host *);
gfarm_error_t host_receiver_lock(struct host *, struct peer **);
void host_receiver_unlock(struct host *);

char *host_name(struct host *);
int host_port(struct host *);
int host_supports_async_protocols(struct host *);
void host_set_callback(struct host *,
	gfarm_int32_t (*)(void *, size_t), void *);
int host_get_callback(struct host *,
	gfarm_int32_t (**)(void *, size_t), void **);

int host_is_up(struct host *);
int host_is_active(struct host *);

void host_peer_busy(struct host *);
void host_peer_unbusy(struct host *);
int host_check_busy(struct host *host, gfarm_int64_t);


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

struct file_replicating *host_replicating_new(struct host *);
void host_replicating_free(struct file_replicating *);
void file_replicating_set_handle(struct file_replicating *, gfarm_int64_t);
gfarm_int64_t file_replicating_get_handle(struct file_replicating *);
gfarm_error_t host_replicated(struct host *, gfarm_ino_t, gfarm_int64_t, 
	gfarm_int64_t, gfarm_int32_t, gfarm_int32_t, gfarm_off_t);


void host_replica_removed(struct host *, gfarm_ino_t, gfarm_int64_t,
	gfarm_error_t);
void host_status_reply_waiting(struct host *);
int host_status_reply_is_waiting(struct host *);
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
