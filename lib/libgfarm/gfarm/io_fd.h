/* iobuffer operation: file descriptor read/write */

/* nonblocking i/o */
void gfarm_iobuffer_set_nonblocking_read_fd(struct gfarm_iobuffer *, int);
void gfarm_iobuffer_set_nonblocking_write_fd(struct gfarm_iobuffer *, int);

/* blocking i/o */
void gfarm_iobuffer_set_blocking_read_fd(struct gfarm_iobuffer *, int);
void gfarm_iobuffer_set_blocking_write_fd(struct gfarm_iobuffer *, int);

/* an option for gfarm_iobuffer_set_write_close() */
void gfarm_iobuffer_write_close_fd_op(struct gfarm_iobuffer *, void *, int);

/* xxx_connection operation */
struct xxx_connection;

char *xxx_fd_connection_new(int, struct xxx_connection **);
char *xxx_connection_set_fd(struct xxx_connection *, int);
