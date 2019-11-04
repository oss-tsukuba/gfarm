/*
 * $Id$
 */

struct peer;
struct file_replicating;
struct host;
struct watcher;
struct thread_pool;

gfarm_error_t async_back_channel_replication_cksum_request(char *, int,
	struct host *, gfarm_ino_t, gfarm_int64_t, gfarm_int64_t,
	char *, size_t, char *, gfarm_int32_t, struct file_replicating *fr);
gfarm_error_t async_back_channel_replication_request(char *, int,
	struct host *, gfarm_ino_t, gfarm_int64_t, struct file_replicating *);

gfarm_error_t gfm_server_switch_back_channel(struct peer *, int, int);
gfarm_error_t gfm_server_switch_async_back_channel(struct peer *, int, int);

struct watcher *back_channel_watcher(void);
struct thread_pool *back_channel_recv_thrpool(void);

void back_channel_init(void);
