/*
 * $Id$
 */

struct peer;

gfarm_error_t gfs_client_fhremove(struct peer *, gfarm_ino_t, gfarm_uint64_t);
gfarm_error_t gfs_client_status(struct peer *,
	double *, double *, double *, gfarm_off_t *, gfarm_off_t *);

gfarm_error_t gfm_server_switch_back_channel(struct peer *, int, int);
