/*
 * FIFO style input/output buffer
 *
 *	enqueue functions:
 *		gfarm_iobuffer_read()
 *		gfarm_iobuffer_put()
 *
 *	dequeue functions:
 *		gfarm_iobuffer_write()
 *		gfarm_iobuffer_get()
 *		gfarm_iobuffer_purge()
 *		gfarm_iobuffer_flush_write()
 *
 * enqueue side can notify EOF by either
 *	the read_func() hook function returns 0
 * or
 *	calling gfarm_iobuffer_set_read_eof() function
 *
 * dequeue side can notice EOF by either
 *	gfarm_iobuffer_is_writable(b) &&
 *	gfarm_iobuffer_empty(b)
 *  i.e.
 *	gfarm_iobuffer_is_writable(b) &&
 *	gfarm_iobuffer_avail_length(b) == 0
 *  i.e.
 *	gfarm_iobuffer_is_eof(b) &&
 *	!gfarm_iobuffer_is_write_eof(b)
 * or
 *	the write_close() hook function is called at dequeue operation.
 *	note that the write_close() hook will be called at the dequeue
 *	function call just after the above conditions become true.
 *
 * Once a dequeue function is called after the above conditions
 * (i.e. once a write_close() hook is called), the above conditions becomes
 * untrue, and the following conditions become true:
 *	gfarm_iobuffer_is_write_eof(b)
 * and
 *	!gfarm_iobuffer_is_writable(b)
 *
 * of course, the following conditions are all true at that time:
 *	gfarm_iobuffer_empty(b)
 * and
 *	gfarm_iobuffer_avail_length(b) == 0
 * and
 *	gfarm_iobuffer_is_read_eof(b)
 * and
 *	gfarm_iobuffer_is_eof(b)
 *
 * Note that users of this module should check gfarm_iobuffer_get_error(b)
 * as well as the above eof conditions.
 */

struct gfarm_iobuffer;

struct gfarm_iobuffer *gfarm_iobuffer_alloc(int);
void gfarm_iobuffer_free(struct gfarm_iobuffer *);

void gfarm_iobuffer_set_error(struct gfarm_iobuffer *, char *);
char *gfarm_iobuffer_get_error(struct gfarm_iobuffer *);

int gfarm_iobuffer_empty(struct gfarm_iobuffer *);
int gfarm_iobuffer_full(struct gfarm_iobuffer *);
int gfarm_iobuffer_avail_length(struct gfarm_iobuffer *);

void gfarm_iobuffer_set_read_eof(struct gfarm_iobuffer *);
void gfarm_iobuffer_clear_read_eof(struct gfarm_iobuffer *);
int gfarm_iobuffer_is_read_eof(struct gfarm_iobuffer *);
void gfarm_iobuffer_clear_write_eof(struct gfarm_iobuffer *);
int gfarm_iobuffer_is_write_eof(struct gfarm_iobuffer *);

int gfarm_iobuffer_is_readable(struct gfarm_iobuffer *);
int gfarm_iobuffer_is_writable(struct gfarm_iobuffer *);
int gfarm_iobuffer_is_eof(struct gfarm_iobuffer *);

/* enqueue */
void gfarm_iobuffer_set_read(struct gfarm_iobuffer *,
	int (*)(struct gfarm_iobuffer *, void *, int, void *, int),
	void *, int);
void *gfarm_iobuffer_get_read_cookie(struct gfarm_iobuffer *);
int gfarm_iobuffer_get_read_fd(struct gfarm_iobuffer *);
void gfarm_iobuffer_read(struct gfarm_iobuffer *, int *);
int gfarm_iobuffer_put(struct gfarm_iobuffer *, void *, int);

/* dequeue */
void gfarm_iobuffer_set_write_close(struct gfarm_iobuffer *,
	void (*)(struct gfarm_iobuffer *, void *, int));
void gfarm_iobuffer_set_write(struct gfarm_iobuffer *,
	int (*)(struct gfarm_iobuffer *, void *, int, void *, int),
	void *, int);
void *gfarm_iobuffer_get_write_cookie(struct gfarm_iobuffer *);
int gfarm_iobuffer_get_write_fd(struct gfarm_iobuffer *);
void gfarm_iobuffer_write(struct gfarm_iobuffer *, int *);
int gfarm_iobuffer_purge(struct gfarm_iobuffer *, int *);
int gfarm_iobuffer_get(struct gfarm_iobuffer *, void *, int);
void gfarm_iobuffer_flush_write(struct gfarm_iobuffer *);

/* enqueue by memory copy, dequeue by write */
int gfarm_iobuffer_put_write(struct gfarm_iobuffer *, void *, int);
/* enqueue by read, dequeue by purge */
int gfarm_iobuffer_purge_read_x(struct gfarm_iobuffer *, int, int);
/* enqueue by read, dequeue by memory copy */
int gfarm_iobuffer_get_read_x(struct gfarm_iobuffer *, void *, int, int);
int gfarm_iobuffer_get_read_just(struct gfarm_iobuffer *, void *, int);
int gfarm_iobuffer_get_read(struct gfarm_iobuffer *, void *, int);
int gfarm_iobuffer_get_read_partial_x(struct gfarm_iobuffer *, void *,int,int);
int gfarm_iobuffer_get_read_partial_just(struct gfarm_iobuffer *, void *, int);
int gfarm_iobuffer_get_read_partial(struct gfarm_iobuffer *, void *, int);
/*
 * gfarm_iobuffer_get_read{,_partial}_just() functions doesn't perform
 * read ahead for given stream, so the caller can perform read operation
 * for the stream without calling iobuffer layer or with another iobuffer
 * at subsequent the call.
 * gfarm_iobuffer_{purge_read,get_read{,_partial}}_x() functions take
 * the `just' flag at its last argument.
 */

/* default operation for gfarm_iobuffer_set_write_close() */
void gfarm_iobuffer_write_close_nop(struct gfarm_iobuffer *, void *, int);
