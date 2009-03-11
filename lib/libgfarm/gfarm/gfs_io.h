/* XXX should have metadata server as an argument */
gfarm_error_t gfm_create_fd(const char *, int, gfarm_mode_t, int *, int *);
gfarm_error_t gfm_open_fd(const char *, int, int *, int *);
gfarm_error_t gfm_close_fd(int);
