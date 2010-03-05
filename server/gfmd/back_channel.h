/*
 * $Id$
 */

struct peer;

gfarm_error_t async_back_channel_replication_request(char *, int,
	struct host *, gfarm_ino_t, gfarm_int64_t, struct file_replicating *);

gfarm_error_t gfm_server_switch_back_channel(struct peer *, int, int);
gfarm_error_t gfm_server_switch_async_back_channel(struct peer *, int, int);

void back_channel_init(void);
