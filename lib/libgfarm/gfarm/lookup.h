struct gfm_connection;

gfarm_error_t gfm_lookup_dir_request(struct gfm_connection *,
	const char *, const char **);
gfarm_error_t gfm_lookup_dir_result(struct gfm_connection *,
	const char *, const char **);

gfarm_error_t gfm_tmp_open_request(struct gfm_connection *, const char *, int);
gfarm_error_t gfm_tmp_open_result(struct gfm_connection *,
	const char *, int *);

gfarm_error_t gfm_open_request(struct gfm_connection *, const char *, int);
gfarm_error_t gfm_open_result(struct gfm_connection *,
	const char *, gfarm_int32_t *, int *);
