/*
 * $Id$
 */

/* gfs_pio_failover.c */
int gfm_client_connection_should_failover(struct gfm_connection *,
	gfarm_error_t);
gfarm_error_t gfm_client_connection_failover(struct gfm_connection *);
gfarm_error_t gfm_client_connection_failover_pre_connect(
	const char *, int, const char *);
gfarm_error_t gfs_pio_failover(GFS_File);

gfarm_error_t gfm_client_rpc_with_failover(
	gfarm_error_t (*)(struct gfm_connection **, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	void (*)(struct gfm_connection *, gfarm_error_t, void *),
	int (*)(gfarm_error_t, void *),
	void *);
gfarm_error_t gfm_client_compound_file_op_readonly(GFS_File,
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	void (*)(struct gfm_connection *, void *),
	void *);
gfarm_error_t gfm_client_compound_file_op_modifiable(GFS_File,
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	void (*)(struct gfm_connection *, void *),
	int (*)(gfarm_error_t, void *),
	void *);
