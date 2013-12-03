/*
 * $Id$
 */

#define GFS_FAILOVER_RETRY_COUNT	1

struct gfs_failover_file;

struct gfs_failover_file_ops {
	int type;

	struct gfm_connection *(*get_connection)(struct gfs_failover_file *);
	void (*set_connection)(struct gfs_failover_file *,
		struct gfm_connection *);
	gfarm_int32_t (*get_fileno)(struct gfs_failover_file *);
	void (*set_fileno)(struct gfs_failover_file *, gfarm_int32_t);
	const char *(*get_url)(struct gfs_failover_file *);
	gfarm_ino_t (*get_ino)(struct gfs_failover_file *);
};

/* gfs_pio_failover.c */
int gfm_client_connection_should_failover(struct gfm_connection *,
	gfarm_error_t);
int gfs_pio_should_failover(GFS_File gf, gfarm_error_t);
int gfs_pio_should_failover_at_gfs_open(GFS_File gf, gfarm_error_t);
int gfs_pio_failover_check_retry(GFS_File gf, gfarm_error_t *);
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
gfarm_error_t gfm_client_compound_fd_op_readonly(struct gfs_failover_file *,
	struct gfs_failover_file_ops *,
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	void (*)(struct gfm_connection *, void *),
	void *);
