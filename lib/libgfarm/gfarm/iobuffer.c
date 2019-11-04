#include <stdlib.h>
#include <string.h>
#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include "iobuffer.h"
#include "crc32.h"

/* XXX - This implementation is somewhat slow, but probably acceptable */

struct gfarm_iobuffer {
	char *buffer;
	int bufsize;
	int head, tail;

	int (*read_timeout_func)(struct gfarm_iobuffer *, void *, int,
				 void *, int);
	int (*read_notimeout_func)(struct gfarm_iobuffer *, void *, int,
				   void *, int);
	void *read_cookie;
	int read_fd; /* for file descriptor i/o */

	int (*write_func)(struct gfarm_iobuffer *, void *, int, void *, int);
	void *write_cookie;
	int write_fd; /* for file descriptor i/o */

	void (*write_close_func)(struct gfarm_iobuffer *, void *, int);

	int read_eof; /* eof is detected on read side */
	int write_eof; /* eof reached on write side */
	int error;
	int read_auto_expansion; /* auto extends 'buffer' at reading */
	int pindown; /* don't overwrite 'buffer */
};

#define IOBUFFER_IS_EMPTY(b)	((b)->head >= (b)->tail)
#define IOBUFFER_IS_FULL(b)	((b)->head <= 0 && (b)->tail >= (b)->bufsize)
#define IOBUFFER_AVAIL_LENGTH(b)	((b)->tail - (b)->head)
#define IOBUFFER_SPACE_SIZE(b)	((b)->head + ((b)->bufsize - (b)->tail))

struct gfarm_iobuffer *
gfarm_iobuffer_alloc(int bufsize)
{
	struct gfarm_iobuffer *b;

	GFARM_MALLOC(b);
	if (b == NULL) {
		gflog_debug(GFARM_MSG_1000994,
			"allocation of struct gfarm_iobuffer failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (NULL);
	}
	GFARM_MALLOC_ARRAY(b->buffer, bufsize);
	if (b->buffer == NULL) {
		free(b);
		gflog_debug(GFARM_MSG_1000995,
			"allocation of buffer with size(%d) failed: %s",
			bufsize,
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (NULL);
	}
	b->bufsize = bufsize;
	b->head = b->tail = 0;

	b->read_timeout_func = NULL;
	b->read_notimeout_func = NULL;
	b->read_cookie = NULL;
	b->read_fd = -1;

	b->write_func = NULL;
	b->write_cookie = NULL;
	b->write_fd = -1;

	b->write_close_func = gfarm_iobuffer_write_close_nop;

	b->read_eof = 0;
	b->write_eof = 0;
	b->error = 0;
	b->read_auto_expansion = 0;
	b->pindown = 0;

	return (b);
}

void
gfarm_iobuffer_free(struct gfarm_iobuffer *b)
{
	if (b == NULL)
		return;
	free(b->buffer);
	free(b);
}

static void
gfarm_iobuffer_squeeze(struct gfarm_iobuffer *b)
{
	if (b->head == 0)
		return;
	memmove(b->buffer, b->buffer + b->head, IOBUFFER_AVAIL_LENGTH(b));
	b->tail -= b->head;
	b->head = 0;
}

static void
gfarm_iobuffer_resize(struct gfarm_iobuffer *b, int new_bufsize)
{
	void *new_buffer;

	if (new_bufsize <= b->bufsize)
		return;

	GFARM_REALLOC_ARRAY(new_buffer, b->buffer, new_bufsize);
	if (new_buffer == NULL) {
		gflog_fatal(GFARM_MSG_1003447,
		    "failed to extend bufsize of struct gfarm_iobuffer: %s",
		    gfarm_error_string(GFARM_ERR_NO_MEMORY));
	}
	gflog_debug(GFARM_MSG_1003448,
	    "bufsize of struct iobuffer extended: %d -> %d",
	    b->bufsize, new_bufsize);
	b->bufsize = new_bufsize;
	b->buffer = new_buffer;
}

int
gfarm_iobuffer_get_size(struct gfarm_iobuffer *b)
{
	return (b->bufsize);
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
gfarm_iobuffer_set_read_timeout(struct gfarm_iobuffer *b,
	int (*func)(struct gfarm_iobuffer *, void *, int, void *, int),
	void *cookie, int fd)
{
	b->read_timeout_func = func;
	b->read_cookie = cookie;
	b->read_fd = fd;
}

void
gfarm_iobuffer_set_read_notimeout(struct gfarm_iobuffer *b,
	int (*func)(struct gfarm_iobuffer *, void *, int, void *, int),
	void *cookie, int fd)
{
	b->read_notimeout_func = func;
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

int
gfarm_iobuffer_empty(struct gfarm_iobuffer *b)
{
	return (IOBUFFER_IS_EMPTY(b));
}

int
gfarm_iobuffer_full(struct gfarm_iobuffer *b)
{
	return (IOBUFFER_IS_FULL(b));
}

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
	if (b->read_auto_expansion)
		return (!b->read_eof);
	else
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
gfarm_iobuffer_set_read_auto_expansion(struct gfarm_iobuffer *b, int flag)
{
	b->read_auto_expansion = flag;
}

void
gfarm_iobuffer_begin_pindown(struct gfarm_iobuffer *b)
{
	b->pindown = 1;
}

void
gfarm_iobuffer_end_pindown(struct gfarm_iobuffer *b)
{
	b->pindown = 0;
}

/* enqueue */
static void
gfarm_iobuffer_read(struct gfarm_iobuffer *b, int *residualp, int do_timeout)
{
	int space, rv;
	int (*func)(struct gfarm_iobuffer *, void *, int, void *, int);
	int new_bufsize;

	if (!b->read_auto_expansion && IOBUFFER_IS_FULL(b))
		return;

	space = IOBUFFER_SPACE_SIZE(b);
	if (residualp == NULL) /* unlimited */
		residualp = &space;
	if (*residualp <= 0)
		return;
	if (!b->pindown &&
	    *residualp > b->bufsize - b->tail && b->head > 0)
		gfarm_iobuffer_squeeze(b);
	if (b->read_auto_expansion && *residualp > space) {
		new_bufsize = b->bufsize - space + *residualp;
		gfarm_iobuffer_resize(b, new_bufsize);
		space = IOBUFFER_SPACE_SIZE(b);
	}

	func = do_timeout ? b->read_timeout_func : b->read_notimeout_func;
	rv = (*func)(b, b->read_cookie, b->read_fd, b->buffer + b->tail,
		     *residualp < space ? *residualp : space);
	if (rv == 0) {
		b->read_eof = 1;
	} else if (rv > 0) {
		b->tail += rv;
		*residualp -= rv;
	}
}

/* enqueue */
static int
gfarm_iobuffer_put(struct gfarm_iobuffer *b, const void *data, int len)
{
	int space, iolen;
	int new_bufsize;

	if (len <= 0)
		return (0);

	if (!b->pindown && IOBUFFER_IS_FULL(b))
		return (0);

	space = IOBUFFER_SPACE_SIZE(b);
	if (b->pindown) {
		if (space < len) {
			new_bufsize = b->bufsize - space + len;
			gfarm_iobuffer_resize(b, new_bufsize);
			space = IOBUFFER_SPACE_SIZE(b);
		}
	} else {
		if (len > b->bufsize - b->tail && b->head > 0)
			gfarm_iobuffer_squeeze(b);
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

/* dequeue */
static void
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
			gfarm_iobuffer_squeeze(b);
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
		gfarm_iobuffer_squeeze(b);
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

/* dequeue */
static int
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
		gfarm_iobuffer_squeeze(b);
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
		if (!b->pindown && IOBUFFER_IS_FULL(b))
			gfarm_iobuffer_write(b, NULL);
		rv = gfarm_iobuffer_put(b, p, residual);
		if (rv == 0) /* error */
			break;
	}
	if (!b->pindown && IOBUFFER_IS_FULL(b))
		gfarm_iobuffer_write(b, NULL);
	return (len - residual);
}

int
gfarm_iobuffer_purge_read_x(struct gfarm_iobuffer *b, int len, int just,
			    int do_timeout)
{
	int rv, residual, tmp, *justp = just ? &tmp : NULL;

	for (residual = len; residual > 0; ) {
		if (IOBUFFER_IS_EMPTY(b)) {
			tmp = residual;
			gfarm_iobuffer_read(b, justp, do_timeout);
		}
		rv = gfarm_iobuffer_purge(b, &residual);
		if (rv == 0) /* EOF or error */
			break;
	}
	return (len - residual);
}

int
gfarm_iobuffer_get_read_x(struct gfarm_iobuffer *b, void *data,
			  int len, int just, int do_timeout)
{
	char *p;
	int rv, residual, tmp, *justp = just ? &tmp : NULL;

	for (p = data, residual = len; residual > 0; residual -= rv, p += rv) {
		if (IOBUFFER_IS_EMPTY(b)) {
			tmp = residual;
			gfarm_iobuffer_read(b, justp, do_timeout);
		}
		rv = gfarm_iobuffer_get(b, p, residual);
		if (rv == 0) /* EOF or error */
			break;
	}
	return (len - residual);
}

/*
 * gfarm_iobuffer_get_read*() wait until desired length of data is
 * received.
 * gfarm_iobuffer_get_read_partial*() don't wait like that, but return
 * the contents of current buffer or the result of (*b->read_timeout_func)
 * or (*b->read_notimeout_func) if current buffer is empty.
 */
int
gfarm_iobuffer_get_read_partial_x(struct gfarm_iobuffer *b, void *data,
				  int len, int just, int do_timeout)
{
	if (IOBUFFER_IS_EMPTY(b)) {
		int tmp = len;

		gfarm_iobuffer_read(b, just ? &tmp : NULL, do_timeout);
	}
	return (gfarm_iobuffer_get(b, data, len));
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

gfarm_uint32_t
gfarm_iobuffer_calc_crc32(struct gfarm_iobuffer *b, gfarm_uint32_t crc,
	int offset, int len, int head_or_tail)
{
	return (gfarm_crc32(crc, b->buffer + (head_or_tail ? b->head : b->tail)
		+ offset, len));
}

int
gfarm_iobuffer_get_read_x_ahead(struct gfarm_iobuffer *b, void *data, int len,
	int just, int do_timeout, int offset, int *errp)
{
	int rlen;
	int head0 = b->head;
	int tail0 = b->tail;
	int read_eof0 = b->read_eof;
	int error0 = b->error;

	if (b->head + offset > b->tail)
		return (0);
	b->head += offset;
	rlen = gfarm_iobuffer_get_read_x(b, data, len, just, do_timeout);
	if (rlen == 0)
		*errp = b->error;
	b->head = head0;
	b->tail = tail0;
	b->read_eof = read_eof0;
	b->error = error0;
	return (rlen);
}

/* this function is used to read from file. i.e. server/gfmd/journal_file.c */
int
gfarm_iobuffer_read_ahead(struct gfarm_iobuffer *b, int len)
{
	size_t alen = IOBUFFER_AVAIL_LENGTH(b);
	int rlen;

	if (alen > len)
		return (alen);
	rlen = len - alen;
	while (rlen > 0 && gfarm_iobuffer_is_readable(b) && b->error == 0)
		gfarm_iobuffer_read(b, &rlen, 0);
	return (len - rlen);
}
