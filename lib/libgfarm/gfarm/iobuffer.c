#include <stdlib.h>
#include <string.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include "iobuffer.h"

/* XXX - This implementation is somewhat slow, but probably acceptable */

struct gfarm_iobuffer {
	char *buffer;
	int bufsize;
	int head, tail;

	int (*read_func)(struct gfarm_iobuffer *, void *, int, void *, int);
	void *read_cookie;
	int read_fd; /* for file descriptor i/o */

	int (*write_func)(struct gfarm_iobuffer *, void *, int, void *, int);
	void *write_cookie;
	int write_fd; /* for file descriptor i/o */

	void (*write_close_func)(struct gfarm_iobuffer *, void *, int);

	int read_eof; /* eof is detected on read side */
	int write_eof; /* eof reached on write side */
	int error;
};

struct gfarm_iobuffer *
gfarm_iobuffer_alloc(int bufsize)
{
	struct gfarm_iobuffer *b;

	GFARM_MALLOC(b);
	if (b == NULL)
		return (NULL);
	GFARM_MALLOC_ARRAY(b->buffer, bufsize);
	if (b->buffer == NULL) {
		free(b);
		return (NULL);
	}
	b->bufsize = bufsize;
	b->head = b->tail = 0;

	b->read_func = NULL;
	b->read_cookie = NULL;
	b->read_fd = -1;

	b->write_func = NULL;
	b->write_cookie = NULL;
	b->write_fd = -1;

	b->write_close_func = gfarm_iobuffer_write_close_nop;

	b->read_eof = 0;
	b->write_eof = 0;
	b->error = 0;

	return (b);
}

void
gfarm_iobuffer_free(struct gfarm_iobuffer *b)
{
	free(b->buffer);
	free(b);
}

void
gfarm_iobuffer_set_error(struct gfarm_iobuffer *b, int error)
{
	b->error = error;
}

int
gfarm_iobuffer_get_error(struct gfarm_iobuffer *b)
{
	return (b->error);
}

void
gfarm_iobuffer_set_read(struct gfarm_iobuffer *b,
	int (*rf)(struct gfarm_iobuffer *, void *, int, void *, int),
	void *cookie, int fd)
{
	b->read_func = rf;
	b->read_cookie = cookie;
	b->read_fd = fd;
}

void *
gfarm_iobuffer_get_read_cookie(struct gfarm_iobuffer *b)
{
	return (b->read_cookie);
}

int
gfarm_iobuffer_get_read_fd(struct gfarm_iobuffer *b)
{
	return (b->read_fd);
}

void
gfarm_iobuffer_set_write_close(struct gfarm_iobuffer *b,
	void (*wcf)(struct gfarm_iobuffer *, void *, int))
{
	b->write_close_func = wcf;
}

void
gfarm_iobuffer_set_write(struct gfarm_iobuffer *b,
	int (*wf)(struct gfarm_iobuffer *, void *, int, void *, int),
	void *cookie, int fd)
{
	b->write_func = wf;
	b->write_cookie = cookie;
	b->write_fd = fd;
}

void *
gfarm_iobuffer_get_write_cookie(struct gfarm_iobuffer *b)
{
	return (b->write_cookie);
}

int
gfarm_iobuffer_get_write_fd(struct gfarm_iobuffer *b)
{
	return (b->write_fd);
}

/*
 * gfarm_iobuffer_empty(b) is a synonym of:
 *	gfarm_iobuffer_avail_length(b) == 0
 */

#define IOBUFFER_IS_EMPTY(b)	((b)->head >= (b)->tail)

int
gfarm_iobuffer_empty(struct gfarm_iobuffer *b)
{
	return (IOBUFFER_IS_EMPTY(b));
}

#define IOBUFFER_IS_FULL(b)	((b)->head <= 0 && (b)->tail >= (b)->bufsize)

int
gfarm_iobuffer_full(struct gfarm_iobuffer *b)
{
	return (IOBUFFER_IS_FULL(b));
}

#define IOBUFFER_AVAIL_LENGTH(b)	((b)->tail - (b)->head)

int
gfarm_iobuffer_avail_length(struct gfarm_iobuffer *b)
{
	return (IOBUFFER_AVAIL_LENGTH(b));
}

void
gfarm_iobuffer_set_read_eof(struct gfarm_iobuffer *b)
{
	b->read_eof = 1;
}

void
gfarm_iobuffer_clear_read_eof(struct gfarm_iobuffer *b)
{
	b->read_eof = 0;
}

int
gfarm_iobuffer_is_read_eof(struct gfarm_iobuffer *b)
{
	return (b->read_eof);
}

void
gfarm_iobuffer_clear_write_eof(struct gfarm_iobuffer *b)
{
	b->write_eof = 0;
}

int
gfarm_iobuffer_is_write_eof(struct gfarm_iobuffer *b)
{
	return (b->write_eof);
}

int
gfarm_iobuffer_is_readable(struct gfarm_iobuffer *b)
{
	return (!IOBUFFER_IS_FULL(b) && !b->read_eof);
}

int
gfarm_iobuffer_is_writable(struct gfarm_iobuffer *b)
{
	return (!IOBUFFER_IS_EMPTY(b) || (b->read_eof && !b->write_eof));
}

/*
 * gfarm_iobuffer_is_eof(b) is a synonym of:
 *	gfarm_iobuffer_empty(b) && gfarm_iobuffer_is_read_eof(b)
 */
int
gfarm_iobuffer_is_eof(struct gfarm_iobuffer *b)
{
	/*
	 * note that gfarm_iobuffer_is_writable(b) may be TRUE here,
	 * if gfarm_iobuffer_is_write_eof(b) is not TRUE yet.
	 */
	return (IOBUFFER_IS_EMPTY(b) && b->read_eof);
}

void
gfarm_iobuffer_read(struct gfarm_iobuffer *b, int *residualp)
{
	int space, rv;

	if (IOBUFFER_IS_FULL(b))
		return;

	space = b->head + (b->bufsize - b->tail);
	if (residualp == NULL) /* unlimited */
		residualp = &space;
	if (*residualp <= 0)
		return;
	if (*residualp > b->bufsize - b->tail && b->head > 0) {
		memmove(b->buffer, b->buffer + b->head,
			IOBUFFER_AVAIL_LENGTH(b));
		b->tail -= b->head;
		b->head = 0;
	}

	rv = (*b->read_func)(b, b->read_cookie, b->read_fd,
			     b->buffer + b->tail,
			     *residualp < space ? *residualp : space);
	if (rv == 0) {
		b->read_eof = 1;
	} else if (rv > 0) {
		b->tail += rv;
		*residualp -= rv;
	}
	return;
}

int
gfarm_iobuffer_put(struct gfarm_iobuffer *b, const void *data, int len)
{
	int space, iolen;

	if (len <= 0)
		return (0);

	if (IOBUFFER_IS_FULL(b))
		return (0);

	space = b->head + (b->bufsize - b->tail);
	if (len > b->bufsize - b->tail && b->head > 0) {
		memmove(b->buffer, b->buffer + b->head,
			IOBUFFER_AVAIL_LENGTH(b));
		b->tail -= b->head;
		b->head = 0;
	}
	iolen = len < space ? len : space;
	memcpy(b->buffer + b->tail, data, iolen);
	b->tail += iolen;
	return (iolen);
}

static void
gfarm_iobuffer_write_close(struct gfarm_iobuffer *b)
{
	(*b->write_close_func)(b, b->write_cookie, b->write_fd);
	b->write_eof = 1;
}

void
gfarm_iobuffer_write(struct gfarm_iobuffer *b, int *residualp)
{
	int avail, rv;

	if (IOBUFFER_IS_EMPTY(b)) {
		if (b->read_eof)
			gfarm_iobuffer_write_close(b);
		return;
	}

	avail = IOBUFFER_AVAIL_LENGTH(b);
	if (residualp == NULL) /* unlimited */
		residualp = &avail;
	if (*residualp <= 0)
		return;

	rv = (*b->write_func)(b, b->write_cookie, b->write_fd,
			      b->buffer + b->head,
			      *residualp < avail ? *residualp : avail);
	if (rv > 0) {
		b->head += rv;
		*residualp -= rv;
		if (IOBUFFER_IS_EMPTY(b)) {
			b->head = b->tail = 0;
			/*
			 * We don't do the following here:
			 *	if (b->read_eof)
			 *		gfarm_iobuffer_write_close(b);
			 * To make sure to produce the following condition
			 * at the EOF:
			 *	gfarm_iobuffer_is_writable(b) &&
			 *	gfarm_iobuffer_empty(b)
			 * i.e.
			 *	gfarm_iobuffer_is_eof(b) &&
			 *	!gfarm_iobuffer_is_write_eof(b)
			 */
		}
	}
	return;
}

int
gfarm_iobuffer_purge(struct gfarm_iobuffer *b, int *residualp)
{
	int avail, purgelen;

	if (IOBUFFER_IS_EMPTY(b)) {
		if (b->read_eof)
			gfarm_iobuffer_write_close(b);
		return (0);
	}

	avail = IOBUFFER_AVAIL_LENGTH(b);
	if (residualp == NULL) /* unlimited */
		residualp = &avail;
	if (*residualp <= 0)
		return (0);

	purgelen = *residualp < avail ? *residualp : avail;

	b->head += purgelen;
	*residualp -= purgelen;
	if (IOBUFFER_IS_EMPTY(b)) {
		b->head = b->tail = 0;
		/*
		 * We don't do the following here:
		 *	if (b->read_eof)
		 *		gfarm_iobuffer_write_close(b);
		 * To make sure to produce the following condition
		 * at the EOF:
		 *	gfarm_iobuffer_is_writable(b) &&
		 *	gfarm_iobuffer_empty(b)
		 * i.e.
		 *	gfarm_iobuffer_is_eof(b) &&
		 *	!gfarm_iobuffer_is_write_eof(b)
		 */
	}
	return (purgelen);
}

int
gfarm_iobuffer_get(struct gfarm_iobuffer *b, void *data, int len)
{
	int avail, iolen;

	if (IOBUFFER_IS_EMPTY(b)) {
		if (b->read_eof)
			gfarm_iobuffer_write_close(b);
		return (0);
	}
	if (len <= 0)
		return (0);

	avail = IOBUFFER_AVAIL_LENGTH(b);
	iolen = len < avail ? len : avail;
	memcpy(data, b->buffer + b->head, iolen);
	b->head += iolen;
	if (IOBUFFER_IS_EMPTY(b)) {
		b->head = b->tail = 0;
		/*
		 * We don't do the following here:
		 *	if (b->read_eof)
		 *		gfarm_iobuffer_write_close(b);
		 * To make sure to produce the following condition
		 * at the EOF:
		 *	gfarm_iobuffer_is_writable(b) &&
		 *	gfarm_iobuffer_empty(b)
		 * i.e.
		 *	gfarm_iobuffer_is_eof(b) &&
		 *	!gfarm_iobuffer_is_write_eof(b)
		 */
	}
	return (iolen);
}

void
gfarm_iobuffer_flush_write(struct gfarm_iobuffer *b)
{
	while (!IOBUFFER_IS_EMPTY(b) && b->error == 0)
		gfarm_iobuffer_write(b, NULL);
}

int
gfarm_iobuffer_put_write(struct gfarm_iobuffer *b, const void *data, int len)
{
	const char *p;
	int rv, residual;

	for (p = data, residual = len; residual > 0; residual -= rv, p += rv) {
		if (IOBUFFER_IS_FULL(b))
			gfarm_iobuffer_write(b, NULL);
		rv = gfarm_iobuffer_put(b, p, residual);
		if (rv == 0) /* error */
			break;
	}
	if (IOBUFFER_IS_FULL(b))
		gfarm_iobuffer_write(b, NULL);
	return (len - residual);
}

int
gfarm_iobuffer_purge_read_x(struct gfarm_iobuffer *b, int len, int just)
{
	int rv, residual, tmp, *justp = just ? &tmp : NULL;

	for (residual = len; residual > 0; ) {
		if (IOBUFFER_IS_EMPTY(b)) {
			tmp = residual;
			gfarm_iobuffer_read(b, justp);
		}
		rv = gfarm_iobuffer_purge(b, &residual);
		if (rv == 0) /* EOF or error */
			break;
	}
	return (len - residual);
}

int
gfarm_iobuffer_get_read_x(struct gfarm_iobuffer *b, void *data,
			  int len, int just)
{
	char *p;
	int rv, residual, tmp, *justp = just ? &tmp : NULL;

	for (p = data, residual = len; residual > 0; residual -= rv, p += rv) {
		if (IOBUFFER_IS_EMPTY(b)) {
			tmp = residual;
			gfarm_iobuffer_read(b, justp);
		}
		rv = gfarm_iobuffer_get(b, p, residual);
		if (rv == 0) /* EOF or error */
			break;
	}
	return (len - residual);
}

int
gfarm_iobuffer_get_read_just(struct gfarm_iobuffer *b, void *data, int len)
{
	return (gfarm_iobuffer_get_read_x(b, data, len, 1));
}

int
gfarm_iobuffer_get_read(struct gfarm_iobuffer *b, void *data, int len)
{
	return (gfarm_iobuffer_get_read_x(b, data, len, 0));
}

/*
 * gfarm_iobuffer_get_read*() wait until desired length of data is 
 * received.
 * gfarm_iobuffer_get_read_partial*() don't wait like that, but return
 * the contents of current buffer or the result of (*b->read_func)
 * if current buffer is empty.
 */
int
gfarm_iobuffer_get_read_partial_x(struct gfarm_iobuffer *b, void *data,
				  int len, int just)
{
	if (IOBUFFER_IS_EMPTY(b)) {
		int tmp = len;

		gfarm_iobuffer_read(b, just ? &tmp : NULL);
	}
	return (gfarm_iobuffer_get(b, data, len));
}

int
gfarm_iobuffer_get_read_partial_just(struct gfarm_iobuffer *b,
				     void *data, int len)
{
	return (gfarm_iobuffer_get_read_partial_x(b, data, len, 1));
}

int
gfarm_iobuffer_get_read_partial(struct gfarm_iobuffer *b, void *data, int len)
{
	return (gfarm_iobuffer_get_read_partial_x(b, data, len, 0));
}

/*
 * default operation for gfarm_iobuffer_set_write_close():
 *	do nothing at eof on writer side
 */
void
gfarm_iobuffer_write_close_nop(struct gfarm_iobuffer *b, void *cookie, int fd)
{
	/* nop */
}
