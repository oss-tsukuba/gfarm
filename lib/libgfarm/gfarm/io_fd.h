/* iobuffer operation: file descriptor read/write */

/* nonblocking i/o */
void gfarm_iobuffer_set_nonblocking_read_fd(struct gfarm_iobuffer *, int);
void gfarm_iobuffer_set_nonblocking_write_fd(struct gfarm_iobuffer *, int);

/* an option for gfarm_iobuffer_set_write_close() */
#if 0 /* currently not used */
void gfarm_iobuffer_write_close_fd_op(struct gfarm_iobuffer *, void *, int);
#endif

/* gfp_xdr operation */
struct gfp_xdr;

gfarm_error_t gfp_xdr_new_socket(int, struct gfp_xdr **);
gfarm_error_t gfp_xdr_set_socket(struct gfp_xdr *, int);

/* the followings are refered from "gsi_auth" method implementation */
int gfarm_iobuffer_nonblocking_read_fd_op(struct gfarm_iobuffer *,
	void *, int, void *, int);
int gfarm_iobuffer_nonblocking_write_socket_op(struct gfarm_iobuffer *,
	void *, int, void *, int);
int gfarm_iobuffer_blocking_read_fd_op(struct gfarm_iobuffer *,
	void *, int, void *, int);
int gfarm_iobuffer_blocking_write_socket_op(struct gfarm_iobuffer *,
	void *, int, void *, int);
