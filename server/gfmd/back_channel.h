/*
 * $Id$
 */

struct peer;

gfarm_error_t gfs_client_fhremove(struct peer *, gfarm_ino_t, gfarm_uint64_t);
gfarm_error_t gfs_client_status(struct peer *,
	double *, double *, double *, gfarm_off_t *, gfarm_off_t *);

gfarm_error_t async_back_channel_fhremove(struct host *,
	gfarm_ino_t, gfarm_int64_t);

gfarm_error_t async_back_channel_replication_request(char *, int,
	struct host *, gfarm_ino_t, gfarm_int64_t, struct file_replicating *);

gfarm_error_t gfm_server_switch_back_channel(struct peer *, int, int);
gfarm_error_t gfm_server_switch_async_back_channel(struct peer *, int, int);

void *async_remover(void *);
void back_channel_init(void);
