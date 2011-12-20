/* XXX should have metadata server as an argument */
gfarm_error_t gfm_create_fd(const char *, int, gfarm_mode_t,
	struct gfm_connection **, int *, int *,
	gfarm_ino_t *, gfarm_uint64_t *, char **);
gfarm_error_t gfm_open_fd(const char *, int,
	struct gfm_connection **, int *, int *);
gfarm_error_t gfm_open_fd_with_ino(const char *, int,
	struct gfm_connection **, int *, int *, char **, gfarm_ino_t *);
gfarm_error_t gfm_close_fd(struct gfm_connection *, int);
