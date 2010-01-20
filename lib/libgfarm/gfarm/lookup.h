struct gfm_connection;

gfarm_error_t gfarm_url_parse_metadb(const char **,
	struct gfm_connection **);
gfarm_error_t gfm_client_connection_and_process_acquire_by_path(const char *,
	struct gfm_connection **);
gfarm_error_t gfm_lookup_dir_request(struct gfm_connection *,
	const char *, const char **);
gfarm_error_t gfm_lookup_dir_result(struct gfm_connection *,
	const char *, const char **);

gfarm_error_t gfm_tmp_lookup_parent_request(struct gfm_connection *,
	const char *, const char **);
gfarm_error_t gfm_tmp_lookup_parent_result(struct gfm_connection *,
	const char *, const char **);

gfarm_error_t gfm_tmp_open_request(struct gfm_connection *, const char *, int);
gfarm_error_t gfm_tmp_open_result(struct gfm_connection *, const char *, int*);

gfarm_error_t gfm_name_success_op_connection_free(struct gfm_connection *,
	void *);
gfarm_error_t gfm_name_op(const char *, gfarm_error_t,
	gfarm_error_t (*)(struct gfm_connection *, void *, const char *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	void *);

gfarm_error_t gfm_inode_success_op_connection_free(struct gfm_connection *,
	void *, int);
gfarm_error_t gfm_inode_op(const char *, int,
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *),
	gfarm_error_t (*)(struct gfm_connection *, void *, int),
	void (*)(struct gfm_connection *, void *),
	void *);
