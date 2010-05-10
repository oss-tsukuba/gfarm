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
void host_disconnect(struct host *, struct peer *);
void host_disconnect_request(struct host *, struct peer *);
struct callout *host_status_callout(struct host *);
struct peer *host_peer(struct host *);
gfarm_error_t host_sender_lock(struct host *, struct peer **);
gfarm_error_t host_sender_trylock(struct host *, struct peer **);
void host_sender_unlock(struct host *, struct peer *);
gfarm_error_t host_receiver_lock(struct host *, struct peer **);
void host_receiver_unlock(struct host *, struct peer *);

char *host_name(struct host *);
int host_port(struct host *);
int host_supports_async_protocols(struct host *);
int host_is_disk_available(struct host *, gfarm_off_t);

#ifdef COMPAT_GFARM_2_3
void host_set_callback(struct host *, struct peer *,
	gfarm_int32_t (*)(void *, void *, size_t),
	void (*)(void *, void *), void *);
int host_get_result_callback(struct host *, struct peer *,
	gfarm_int32_t (**)(void *, void *, size_t), void **);
int host_get_disconnect_callback(struct host *,
	void (**)(void *, void *), struct peer **, void **);
#endif

int host_is_up(struct host *);
int host_is_active(struct host *);

void host_peer_busy(struct host *);
void host_peer_unbusy(struct host *);
int host_check_busy(struct host *host, gfarm_int64_t);

struct file_replicating;
gfarm_error_t host_replicating_new(struct host *, struct file_replicating **);

void host_status_reply_waiting(struct host *);
int host_status_reply_is_waiting(struct host *);
void host_status_update(struct host *, struct host_status *);

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
