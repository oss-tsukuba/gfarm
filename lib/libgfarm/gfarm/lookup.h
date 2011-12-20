struct gfm_connection;

gfarm_error_t gfarm_url_parse_metadb(const char **,
	struct gfm_connection **);
gfarm_error_t gfarm_get_hostname_by_url(const char *, char **, int *);
gfarm_error_t gfm_client_connection_and_process_acquire_by_path(const char *,
	struct gfm_connection **);
gfarm_error_t gfm_client_connection_and_process_acquire_by_path_follow(
	const char *, struct gfm_connection **);
int gfm_is_mounted(struct gfm_connection *);

gfarm_error_t gfm_name_success_op_connection_free(struct gfm_connection *,
	void *, int, const char *, gfarm_ino_t);
gfarm_error_t gfm_name_op(const char *, gfarm_error_t,
	gfarm_error_t (*)(struct gfm_connection *, void *, const char *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *, int, const char *,
	    gfarm_ino_t),
	void *);

gfarm_error_t gfm_name2_success_op_connection_free(struct gfm_connection *,
	void *);
gfarm_error_t gfm_name2_op(const char *, const char *, int,
	gfarm_error_t (*)(struct gfm_connection *, void *,
	    const char *),
	gfarm_error_t (*)(struct gfm_connection *, void *,
	    const char *, const char *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	void (*)(struct gfm_connection *, void *), void *);

gfarm_error_t gfm_inode_success_op_connection_free(struct gfm_connection *,
	void *, int, const char *, gfarm_ino_t);
gfarm_error_t gfm_inode_op(const char *, int,
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *, int, const char *,
	    gfarm_ino_t),
	void (*)(struct gfm_connection *, void *),
	void *);
gfarm_error_t gfm_inode_op_no_follow(const char *, int,
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *, int, const char *,
	    gfarm_ino_t),
	void (*)(struct gfm_connection *, void *),
	void *);
