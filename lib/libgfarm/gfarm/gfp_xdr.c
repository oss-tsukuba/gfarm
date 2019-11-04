#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <inttypes.h>	/* more portable than <stdint.h> on UNIX variants */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <netinet/in.h> /* ntoh[ls]()/hton[ls]() on glibc */

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h" /* gflog_fatal() */

#include "liberror.h"
#include "iobuffer.h"
#include "gfp_xdr.h"

#ifndef va_copy /* since C99 standard */
#define va_copy(dst, src)	((dst) = (src))
#endif

#ifndef INT64T_IS_FLOAT
#define INT64T_IS_FLOAT 0
#endif /* INT64T_IS_FLOAT */

#if INT64T_IS_FLOAT
#include <math.h>

#define POWER2_32	4294967296.0		/* 2^32 */
#endif

#define GFP_XDR_BUFSIZE	16384

struct gfp_xdr {
	struct gfarm_iobuffer *recvbuffer;
	struct gfarm_iobuffer *sendbuffer;

	struct gfp_iobuffer_ops *iob_ops;
	void *cookie;
	int fd;
};

/*
 * switch to new iobuffer operation,
 * and (possibly) switch to new cookie/fd
 */
void
gfp_xdr_set(struct gfp_xdr *conn,
	struct gfp_iobuffer_ops *ops, void *cookie, int fd)
{
	conn->iob_ops = ops;
	conn->cookie = cookie;
	conn->fd = fd;

	if (conn->recvbuffer) {
		gfarm_iobuffer_set_read_timeout(conn->recvbuffer,
		    ops->blocking_read_timeout, cookie, fd);
		gfarm_iobuffer_set_read_notimeout(conn->recvbuffer,
		    ops->blocking_read_notimeout, cookie, fd);
	}
	if (conn->sendbuffer)
		gfarm_iobuffer_set_write(conn->sendbuffer, ops->blocking_write,
		    cookie, fd);
}

#define GFP_XDR_NEW_RECV	1
#define GFP_XDR_NEW_SEND	2

gfarm_error_t
gfp_xdr_new(struct gfp_iobuffer_ops *ops, void *cookie, int fd,
	int flags, struct gfp_xdr **connp)
{
	struct gfp_xdr *conn;

	GFARM_MALLOC(conn);
	if (conn == NULL) {
		gflog_debug(GFARM_MSG_1000996,
			"allocation of 'conn' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((flags & GFP_XDR_NEW_RECV) != 0) {
		conn->recvbuffer = gfarm_iobuffer_alloc(GFP_XDR_BUFSIZE);
		if (conn->recvbuffer == NULL) {
			free(conn);
			gflog_debug(GFARM_MSG_1000997,
				"allocation of 'conn recvbuffer' failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		if ((flags & GFP_XDR_NEW_AUTO_RECV_EXPANSION) != 0) {
			gfarm_iobuffer_set_read_auto_expansion(
			    conn->recvbuffer, 1);
		}
	} else
		conn->recvbuffer = NULL;

	if ((flags & GFP_XDR_NEW_SEND) != 0) {
		conn->sendbuffer = gfarm_iobuffer_alloc(GFP_XDR_BUFSIZE);
		if (conn->sendbuffer == NULL) {
			gfarm_iobuffer_free(conn->recvbuffer);
			free(conn);
			gflog_debug(GFARM_MSG_1000998,
				"allocation of 'conn sendbuffer' failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
	} else
		conn->sendbuffer = NULL;

	gfp_xdr_set(conn, ops, cookie, fd);

	*connp = conn;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_free(struct gfp_xdr *conn)
{
	gfarm_error_t e, e_save;

	e_save = gfp_xdr_flush(conn);
	gfarm_iobuffer_free(conn->sendbuffer);
	gfarm_iobuffer_free(conn->recvbuffer);

	e = (*conn->iob_ops->close)(conn->cookie, conn->fd);
	if (e_save == GFARM_ERR_NO_ERROR)
		e_save = e;

	free(conn);
	return (e);
}

void *
gfp_xdr_cookie(struct gfp_xdr *conn)
{
	return (conn->cookie);
}

int
gfp_xdr_fd(struct gfp_xdr *conn)
{
	return (conn->fd);
}

gfarm_error_t
gfp_xdr_sendbuffer_check_size(struct gfp_xdr *conn, int size)
{
	if (size > gfarm_iobuffer_get_size(conn->sendbuffer))
		return (GFARM_ERR_NO_BUFFER_SPACE_AVAILABLE);
	else if (size > gfarm_iobuffer_avail_length(conn->sendbuffer))
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
	else
		return (GFARM_ERR_NO_ERROR);
}

void
gfp_xdr_recvbuffer_clear_read_eof(struct gfp_xdr *conn)
{
	gfarm_iobuffer_clear_read_eof(conn->recvbuffer);
}

gfarm_error_t
gfp_xdr_export_credential(struct gfp_xdr *conn)
{
	return ((*conn->iob_ops->export_credential)(conn->cookie));
}

gfarm_error_t
gfp_xdr_delete_credential(struct gfp_xdr *conn, int sighandler)
{
	return ((*conn->iob_ops->delete_credential)(conn->cookie, sighandler));
}

char *
gfp_xdr_env_for_credential(struct gfp_xdr *conn)
{
	return ((*conn->iob_ops->env_for_credential)(conn->cookie));
}


int
gfp_xdr_recv_is_ready(struct gfp_xdr *conn)
{
	return (!gfarm_iobuffer_empty(conn->recvbuffer) ||
		gfarm_iobuffer_is_eof(conn->recvbuffer));
}

int
gfp_xdr_is_empty(struct gfp_xdr *conn)
{
	return (gfarm_iobuffer_empty(conn->recvbuffer) &&
		gfarm_iobuffer_empty(conn->sendbuffer));
}

gfarm_error_t
gfp_xdr_flush(struct gfp_xdr *conn)
{
	if (conn->sendbuffer == NULL)
		return (GFARM_ERR_NO_ERROR);
	gfarm_iobuffer_flush_write(conn->sendbuffer);
	return (gfarm_iobuffer_get_error(conn->sendbuffer));
}

gfarm_error_t
gfp_xdr_purge_sized(struct gfp_xdr *conn, int just, int len, size_t *sizep)
{
	gfarm_error_t e;
	int rv;

	if (*sizep < len) {
		gflog_debug(GFARM_MSG_1000999, "gfp_xdr_purge_sized: "
		    "%d bytes expected, but only %d bytes remains",
		    len, (int)*sizep);
		return (GFARM_ERR_PROTOCOL);
	}
	rv = gfarm_iobuffer_purge_read_x(conn->recvbuffer, len, just, 1);
	*sizep -= rv;
	if (rv != len) {
		e = gfarm_iobuffer_get_error(conn->recvbuffer);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		return (GFARM_ERR_UNEXPECTED_EOF);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_purge(struct gfp_xdr *conn, int just, int len)
{
	if (gfarm_iobuffer_purge_read_x(conn->recvbuffer, len, just, 1)
	    != len) {
		gflog_debug(GFARM_MSG_1001000,
			"gfarm_iobuffer_purge_read_x() failed: %s",
			gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
		return (GFARM_ERR_UNEXPECTED_EOF);
	}
	return (GFARM_ERR_NO_ERROR);
}

void
gfp_xdr_purge_all(struct gfp_xdr *conn)
{
	if (conn->sendbuffer)
		while (gfarm_iobuffer_purge(conn->sendbuffer, NULL) > 0)
			;
	if (conn->recvbuffer)
		while (gfarm_iobuffer_purge(conn->recvbuffer, NULL) > 0)
			;
}

gfarm_error_t
gfp_xdr_vsend_size_add(size_t *sizep, const char **formatp, va_list *app)
{
	const char *format = *formatp;
	size_t size = *sizep;
	gfarm_uint8_t c;
	gfarm_int16_t h;
	gfarm_int32_t i, n;
	gfarm_uint32_t lv[2];
#if INT64T_IS_FLOAT
	int minus;
#endif
#ifndef __KERNEL__	/* double */
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nd;
#else
	double d;
#	define nd d
#endif
#endif /* __KERNEL__ */
	const char *s;

	for (; *format; format++) {
		switch (*format) {
		case 'c':
			c = va_arg(*app, int);
			size += sizeof(c);
			continue;
		case 'h':
			h = va_arg(*app, int);
			size += sizeof(h);
			continue;
		case 'i':
			i = va_arg(*app, gfarm_int32_t);
			size += sizeof(i);
			continue;
		case 'l':
			/*
			 * note that because actual type of gfarm_int64_t
			 * may be diffenent (int64_t or double), we use lv here
			 */
			(void)va_arg(*app, gfarm_int64_t);
			size += sizeof(lv);
			continue;
		case 's':
			s = va_arg(*app, const char *);
			n = strlen(s);
			size += sizeof(i);
			size += n;
			continue;
		case 'S':
			(void)va_arg(*app, const char *);
			n = va_arg(*app, size_t);
			size += sizeof(i);
			size += n;
			continue;
		case 'b':
			/*
			 * note that because actual type of size_t may be
			 * diffenent ([u]int32_t or [u]int64_t), we must not
			 * pass this as is via network.
			 */
			n = va_arg(*app, size_t);
			(void)va_arg(*app, const char *);
			size += sizeof(i);
			size += n;
			continue;
		case 'r':
			n = va_arg(*app, size_t);
			(void)va_arg(*app, const char *);
			size += n;
			continue;
		case 'f':
#ifndef __KERNEL__	/* double */
			(void)va_arg(*app, double);
			size += sizeof(nd);
			continue;
#else /* __KERNEL__ */
			gflog_fatal(GFARM_MSG_1003867,
			    "floating format is not "
			    "supported. '%s'", *formatp);
			return (GFARM_ERR_PROTOCOL);  /* floating */
#endif /* __KERNEL__ */

		default:
			break;
		}

		break;
	}
	*sizep = size;
	*formatp = format;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_vsend(struct gfp_xdr *conn,
	const char **formatp, va_list *app)
{
	const char *format = *formatp;
	gfarm_uint8_t c;
	gfarm_int16_t h;
	gfarm_int32_t i, n;
	gfarm_int64_t o;
	gfarm_uint32_t lv[2];
#if INT64T_IS_FLOAT
	int minus;
#endif
#ifndef __KERNEL__	/* double */
	double d;
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nd;
#else
#	define nd d
#endif
#endif /* __KERNEL__ */
	const char *s;

	for (; *format; format++) {
		switch (*format) {
		case 'c':
			c = va_arg(*app, int);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &c, sizeof(c));
			continue;
		case 'h':
			h = va_arg(*app, int);
			h = htons(h);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &h, sizeof(h));
			continue;
		case 'i':
			i = va_arg(*app, gfarm_int32_t);
			i = htonl(i);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &i, sizeof(i));
			continue;
		case 'l':
			/*
			 * note that because actual type of gfarm_int64_t
			 * may be diffenent (int64_t or double), we must
			 * not pass this as is via network.
			 */
			o = va_arg(*app, gfarm_int64_t);
#if INT64T_IS_FLOAT
			minus = o < 0;
			if (minus)
				o = -o;
			lv[0] = o / POWER2_32;
			lv[1] = o - lv[0] * POWER2_32;
			if (minus) {
				lv[0] = ~lv[0];
				lv[1] = ~lv[1];
				if (++lv[1] == 0)
					++lv[0];
			}
#else
			lv[0] = o >> 32;
			lv[1] = o;
#endif
			lv[0] = htonl(lv[0]);
			lv[1] = htonl(lv[1]);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    lv, sizeof(lv));
			continue;
		case 's':
			s = va_arg(*app, const char *);
			n = strlen(s);
			i = htonl(n);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &i, sizeof(i));
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    s, n);
			continue;
		case 'S':
			s = va_arg(*app, const char *);
			n = va_arg(*app, size_t);
			i = htonl(n);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &i, sizeof(i));
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    s, n);
			continue;
		case 'b':
			/*
			 * note that because actual type of size_t may be
			 * diffenent ([u]int32_t or [u]int64_t), we must not
			 * pass this as is via network.
			 */
			n = va_arg(*app, size_t);
			i = htonl(n);
			s = va_arg(*app, const char *);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &i, sizeof(i));
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    s, n);
			continue;
		case 'r':
			n = va_arg(*app, size_t);
			s = va_arg(*app, const char *);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    s, n);
			continue;
		case 'f':
#ifndef __KERNEL__	/* double */

			d = va_arg(*app, double);
#ifndef WORDS_BIGENDIAN
			swab(&d, &nd, sizeof(nd));
#endif
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &nd, sizeof(nd));
			continue;
#else /* __KERNEL__ */
			gflog_fatal(GFARM_MSG_1003868,
			    "floating format is not "
			    "supported. '%s'", *formatp);
			return (GFARM_ERR_PROTOCOL);  /* floating */
#endif /* __KERNEL__ */

		default:
			break;
		}

		break;
	}
	*formatp = format;
	return (gfarm_iobuffer_get_error(conn->sendbuffer));
}

static gfarm_error_t
recv_sized(struct gfp_xdr *conn, int just, int do_timeout, void *p, size_t sz,
	size_t *sizep)
{
	gfarm_error_t e;
	int rv;

	if (*sizep < sz) {
		gflog_debug(GFARM_MSG_1001001, "recv_size: "
		    "%d bytes expected, but only %d bytes remains",
		    (int)sz, (int)*sizep);
		return (GFARM_ERR_PROTOCOL);  /* too short message */
	}
	rv = gfarm_iobuffer_get_read_x(conn->recvbuffer, p, sz, just,
	    do_timeout);
	*sizep -= rv;
	if (rv != sz) {
		gflog_debug(GFARM_MSG_1001002, "recv_size: "
		    "%d bytes expected, but only %d bytes read",
		    (int)sz, rv);
		e = gfarm_iobuffer_get_error(conn->recvbuffer);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if (rv == 0) /* maybe usual EOF */
			return (GFARM_ERR_UNEXPECTED_EOF);
		return (GFARM_ERR_PROTOCOL);	/* really unexpected EOF */
	}
	return (GFARM_ERR_NO_ERROR); /* rv may be 0, if sz == 0 */
}

static void
gfp_xdr_vrecv_free(int format_parsed, const char *format, va_list *app)
{
	char **sp;

	for (; --format_parsed >= 0 && *format; format++) {
		switch (*format) {
		case 'c':
			(void)va_arg(*app, gfarm_int8_t *);
			continue;
		case 'h':
			(void)va_arg(*app, gfarm_int16_t *);
			continue;
		case 'i':
			(void)va_arg(*app, gfarm_int32_t *);
			continue;
		case 'l':
			(void)va_arg(*app, gfarm_int64_t *);
			continue;
		case 'r':
			(void)va_arg(*app, size_t);
			(void)va_arg(*app, size_t *);
			(void)va_arg(*app, char *);
			continue;
		case 's':
			sp = va_arg(*app, char **);
			free(*sp);
			continue;
		case 'b':
			(void)va_arg(*app, size_t);
			(void)va_arg(*app, size_t *);
			(void)va_arg(*app, char *);
			continue;
		case 'B':
			(void)va_arg(*app, size_t *);
			sp = va_arg(*app, char **);
			free(*sp);
			continue;
		case 'f':
#ifndef __KERNEL__	/* double */
			(void)va_arg(*app, double *);
			continue;
#else
			gflog_fatal(GFARM_MSG_1003869,
			    "floating format is not supported. '%s'", format);
			return; /* floating */
#endif /* __KERNEL__ */
		case '/':
			break;

		default:
			break;
		}

		break;
	}
}

gfarm_error_t
gfp_xdr_vrecv_sized_x(struct gfp_xdr *conn, int just, int do_timeout,
	size_t *sizep, int *eofp, const char **formatp, va_list *app)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e_save = GFARM_ERR_NO_ERROR;
	const char *format = *formatp;
	gfarm_int8_t *cp;
	gfarm_int16_t *hp;
	gfarm_int32_t *ip, i;
	gfarm_int64_t *op;
	gfarm_uint32_t lv[2];
#if INT64T_IS_FLOAT
	int minus;
#endif
#ifndef __KERNEL__	/* double */
	double *dp;
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nd;
#endif
#endif /* __KERNEL__ */
	char **sp, *s;
	size_t *szp, sz;
	size_t size;
	int overflow = 0;

	int format_parsed = 0;
	const char *format_start = *formatp;
	va_list ap_start;

	va_copy(ap_start, *app);

	if (sizep != NULL)
		size = *sizep;
	else
		size = SIZE_MAX;

	/* do not call gfp_xdr_flush() here for a compound procedure */
	*eofp = 1;

	for (; *format; format++) {
		switch (*format) {
		case 'c':
			cp = va_arg(*app, gfarm_int8_t *);
			if ((e = recv_sized(conn, just, do_timeout, cp,
			    sizeof(*cp), &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gfp_xdr_vrecv_free(format_parsed,
					    format_start, &ap_start);
					return (GFARM_ERR_NO_ERROR); /* EOF */
				}
				break;
			}
			format_parsed++;
			continue;
		case 'h':
			hp = va_arg(*app, gfarm_int16_t *);
			if ((e = recv_sized(conn, just, do_timeout, hp,
			    sizeof(*hp), &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gfp_xdr_vrecv_free(format_parsed,
					    format_start, &ap_start);
					return (GFARM_ERR_NO_ERROR); /* EOF */
				}
				break;
			}
			*hp = ntohs(*hp);
			format_parsed++;
			continue;
		case 'i':
			ip = va_arg(*app, gfarm_int32_t *);
			if ((e = recv_sized(conn, just, do_timeout, ip,
			    sizeof(*ip), &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gfp_xdr_vrecv_free(format_parsed,
					    format_start, &ap_start);
					return (GFARM_ERR_NO_ERROR); /* EOF */
				}
				break;
			}
			*ip = ntohl(*ip);
			format_parsed++;
			continue;
		case 'l':
			op = va_arg(*app, gfarm_int64_t *);
			/*
			 * note that because actual type of gfarm_int64_t
			 * may be diffenent (int64_t or double), we must
			 * not pass this as is via network.
			 */
			if ((e = recv_sized(conn, just, do_timeout, lv,
			    sizeof(lv), &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gfp_xdr_vrecv_free(format_parsed,
					    format_start, &ap_start);
					return (GFARM_ERR_NO_ERROR); /* EOF */
				}
				break;
			}
			lv[0] = ntohl(lv[0]);
			lv[1] = ntohl(lv[1]);
#if INT64T_IS_FLOAT
			minus = lv[0] & 0x80000000;
			if (minus) {
				lv[0] = ~lv[0];
				lv[1] = ~lv[1];
				if (++lv[1] == 0)
					++lv[0];
			}
			*op = lv[0] * POWER2_32 + lv[1];
			if (minus)
				*op = -*op;
#else
				*op = ((gfarm_int64_t)lv[0] << 32) | lv[1];
#endif
			format_parsed++;
			continue;
		case 's':
			sp = va_arg(*app, char **);
			if ((e = recv_sized(conn, just, do_timeout, &i,
			    sizeof(i), &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gfp_xdr_vrecv_free(format_parsed,
					    format_start, &ap_start);
					return (GFARM_ERR_NO_ERROR); /* EOF */
				}
				break;
			}
			i = ntohl(i);
			sz = gfarm_size_add(&overflow, i, 1);
			if (overflow) {
				e = GFARM_ERR_PROTOCOL;
				break;
			}
			GFARM_MALLOC_ARRAY(*sp, sz);
			format_parsed++;
			if (*sp == NULL) {
				if ((e = gfp_xdr_purge_sized(conn, just,
				    sz, &size)) != GFARM_ERR_NO_ERROR)
					break;
				e_save = GFARM_ERR_NO_MEMORY;
				continue;
			}
			if ((e = recv_sized(conn, just, do_timeout, *sp, i,
			    &size)) != GFARM_ERR_NO_ERROR)
				break;
			(*sp)[i] = '\0';
			continue;
		case 'b':
			sz = va_arg(*app, size_t);
			szp = va_arg(*app, size_t *);
			s = va_arg(*app, char *);
			/*
			 * note that because actual type of size_t may be
			 * diffenent ([u]int32_t or [u]int64_t), we must not
			 * pass this as is via network.
			 */
			if ((e = recv_sized(conn, just, do_timeout, &i,
			    sizeof(i), &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gfp_xdr_vrecv_free(format_parsed,
					    format_start, &ap_start);
					return (GFARM_ERR_NO_ERROR); /* EOF */
				}
				break;
			}
			i = ntohl(i);
			*szp = i;
			if (i <= sz) {
				if ((e = recv_sized(conn, just, do_timeout, s,
				    i, &size)) != GFARM_ERR_NO_ERROR)
					break;
			} else {
				if (size < i) {
					e = GFARM_ERR_PROTOCOL;
					break;
				}
				if ((e = recv_sized(conn, just, do_timeout, s,
				    sz, &size)) != GFARM_ERR_NO_ERROR)
					break;
				/* abandon (i - sz) bytes */
				if ((e = gfp_xdr_purge_sized(conn, just,
				    i - sz, &size)) != GFARM_ERR_NO_ERROR)
					break;
			}
			format_parsed++;
			continue;
		case 'B':
			szp = va_arg(*app, size_t *);
			sp = va_arg(*app, char **);
			/*
			 * note that because actual type of size_t may be
			 * diffenent ([u]int32_t or [u]int64_t), we must not
			 * pass this as is via network.
			 */
			if ((e = recv_sized(conn, just, do_timeout, &i,
			    sizeof(i), &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gfp_xdr_vrecv_free(format_parsed,
					    format_start, &ap_start);
					return (GFARM_ERR_NO_ERROR); /* EOF */
				}
				break;
			}
			i = ntohl(i);
			*szp = i;
			/* XXX is this +1 really necessary? */
			sz = gfarm_size_add(&overflow, i, 1);
			if (overflow) {
				e = GFARM_ERR_PROTOCOL;
				break;
			}
			GFARM_MALLOC_ARRAY(*sp, sz);
			format_parsed++;
			if (*sp == NULL) {
				if ((e = gfp_xdr_purge_sized(conn, just,
				    sz, &size)) != GFARM_ERR_NO_ERROR)
					break;
				e_save = GFARM_ERR_NO_MEMORY;
				continue;
			}
			if ((e = recv_sized(conn, just, do_timeout, *sp, i,
			    &size)) != GFARM_ERR_NO_ERROR)
				break;
			continue;
		case 'f':
#ifndef __KERNEL__	/* double */
			dp = va_arg(*app, double *);
			assert(sizeof(*dp) == 8);
			if ((e = recv_sized(conn, just, do_timeout, dp,
			    sizeof(*dp), &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gfp_xdr_vrecv_free(format_parsed,
					    format_start, &ap_start);
					return (GFARM_ERR_NO_ERROR); /* EOF */
				}
				break;
			}
#ifndef WORDS_BIGENDIAN
			swab(dp, &nd, sizeof(nd));
			*dp = *(double *)&nd;
#endif
			format_parsed++;
			continue;
#else /* __KERNEL__ */
			gflog_fatal(GFARM_MSG_1003870,
			    "floating format is not "
			    "supported. '%s'", *formatp);
			return (GFARM_ERR_PROTOCOL);  /* floating */
#endif /* __KERNEL__ */

		default:
			break;
		}

		break;
	}
	if (sizep != NULL)
		*sizep = size;
	*formatp = format;
	*eofp = 0;

	/* connection error has most precedence to avoid protocol confusion */
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_vrecv_free(format_parsed, format_start, &ap_start);
		return (e);
	}

	/* iobuffer error may be a connection error */
	if ((e = gfarm_iobuffer_get_error(conn->recvbuffer)) !=
	    GFARM_ERR_NO_ERROR) {
		gfp_xdr_vrecv_free(format_parsed, format_start, &ap_start);
		return (e);
	}

	if (e_save != GFARM_ERR_NO_ERROR)
		gfp_xdr_vrecv_free(format_parsed, format_start, &ap_start);

	return (e_save); /* NO_MEMORY or SUCCESS */
}

gfarm_error_t
gfp_xdr_vrecv_sized(struct gfp_xdr *conn, int just, int do_timeout,
	size_t *sizep, int *eofp, const char **formatp, va_list *app)
{
	return (gfp_xdr_vrecv_sized_x(conn, just, do_timeout,
	    sizep, eofp, formatp, app));
}

gfarm_error_t
gfp_xdr_vrecv(struct gfp_xdr *conn, int just, int do_timeout,
	int *eofp, const char **formatp, va_list *app)
{
	return (gfp_xdr_vrecv_sized_x(conn, just, do_timeout,
	    NULL, eofp, formatp, app));
}

gfarm_error_t
gfp_xdr_send_size_add(size_t *sizep, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vsend_size_add(sizep, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (*format != '\0') {
		gflog_debug(GFARM_MSG_1001003, "gfp_xdr_send_size_add: "
		    "invalid format character: %c(%x)", *format, *format);
		return (GFARM_ERRMSG_GFP_XDR_SEND_INVALID_FORMAT_CHARACTER);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_send(struct gfp_xdr *conn, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vsend(conn, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001004,
			"gfp_xdr_vsend() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (*format != '\0') {
		gflog_debug(GFARM_MSG_1001005, "gfp_xdr_send_size: "
		    "invalid format character: %c(%x)", *format, *format);
		return (GFARM_ERRMSG_GFP_XDR_SEND_INVALID_FORMAT_CHARACTER);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_uint32_t
gfp_xdr_send_calc_crc32(struct gfp_xdr *conn, gfarm_uint32_t crc, int offset,
	size_t length)
{
	return (gfarm_iobuffer_calc_crc32(conn->sendbuffer, crc,
		offset, length, 0));
}

gfarm_uint32_t
gfp_xdr_recv_calc_crc32(struct gfp_xdr *conn, gfarm_uint32_t crc, int offset,
	size_t length)
{
	return (gfarm_iobuffer_calc_crc32(conn->recvbuffer, crc,
		offset, length, 1));
}

gfarm_uint32_t
gfp_xdr_recv_get_crc32_ahead(struct gfp_xdr *conn, int offset)
{
	gfarm_uint32_t n;
	int err;
	int len = gfarm_iobuffer_get_read_x_ahead(conn->recvbuffer,
		&n, sizeof(n), 1, 1, offset, &err);

	if (len != sizeof(n))
		return (0);
	return (ntohl(n));
}

static gfarm_error_t
gfp_xdr_vrecv_sized_x_check_format(
	struct gfp_xdr *conn, int just, int do_timeout,
	size_t *sizep, int *eofp, const char *format, va_list *app)
{
	gfarm_error_t e;

	e = gfp_xdr_vrecv_sized_x(conn, just, do_timeout,
	    sizep, eofp, &format, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001007,
		    "gfp_xdr_vrecv_sized_x() failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (*eofp)
		return (GFARM_ERR_NO_ERROR);
	if (*format != '\0') {
		gflog_debug(GFARM_MSG_1001008,
		    "gfp_xdr_vrecv_sized_x_check_format(): "
		    "invalid format character: %c(%x)", *format, *format);
		return (GFARM_ERRMSG_GFP_XDR_RECV_INVALID_FORMAT_CHARACTER);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_recv_sized(struct gfp_xdr *conn, int just, int do_timeout,
	size_t *sizep, int *eofp, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv_sized_x_check_format(conn, just, do_timeout,
	    sizep, eofp, format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfp_xdr_recv(struct gfp_xdr *conn, int just,
	int *eofp, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv_sized_x_check_format(conn, just, 1,
	    NULL, eofp, format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfp_xdr_recv_notimeout(struct gfp_xdr *conn, int just,
	int *eofp, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv_sized_x_check_format(conn, just, 0,
	    NULL, eofp, format, &ap);
	va_end(ap);
	return (e);
}

/* this function is used to read from file. i.e. server/gfmd/journal_file.c */
gfarm_error_t
gfp_xdr_recv_ahead(struct gfp_xdr *conn, int len, size_t *availp)
{
	int r = gfarm_iobuffer_read_ahead(conn->recvbuffer, len);

	if (r == 0) {
		*availp = 0;
		return (gfarm_iobuffer_get_error(conn->recvbuffer));
	}
	*availp = gfarm_iobuffer_avail_length(conn->recvbuffer);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * do RPC request
 */
gfarm_error_t
gfp_xdr_vrpc_request(struct gfp_xdr *conn, gfarm_int32_t command,
	const char **formatp, va_list *app)
{
	gfarm_error_t e;

	/*
	 * send request
	 */
	e = gfp_xdr_send(conn, "i", command);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001009,
			"sending command (%d) failed: %s",
			command,
			gfarm_error_string(e));
		return (e);
	}
	return (gfp_xdr_vsend(conn, formatp, app));
}

/*
 * used by client side of both synchronous and asynchronous protocol.
 * if sizep == NULL, it's a synchronous protocol, otherwise asynchronous.
 * Note that this function assumes that async_header is already received.
 *
 * Callers of this function should check the followings:
 *	return value == GFARM_ERR_NOERROR
 *	*errorp == GFARM_ERR_NOERROR
 * And if there is no remaining output parameter:
 *	*sizep == 0
 */
static gfarm_error_t
gfp_xdr_vrpc_result_sized_internal(
	struct gfp_xdr *conn, int just, int do_timeout,
	size_t *sizep, gfarm_int32_t *errorp,
	const char **formatp, va_list *app)
{
	gfarm_error_t e;
	int eof;

	/*
	 * receive response
	 */

	/* timeout if it's asynchronous protocol, or do_timeout is specified */
	e = gfp_xdr_recv_sized(conn, just,
	    sizep != NULL || do_timeout,
	    sizep, &eof, "i", errorp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001010,
			"receiving response (%d) failed: %s",
			just, gfarm_error_string(e));
		return (e);
	}
	if (eof) { /* rpc status missing */
		gflog_debug(GFARM_MSG_1001011,
			"Unexpected EOF when receiving response: %s",
			gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
		return (GFARM_ERR_UNEXPECTED_EOF);
	}
	if (*errorp != GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);

	/* always do timeout here, because error code is already received */
	e = gfp_xdr_vrecv_sized_x(conn, just, 1, sizep, &eof, formatp, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001012,
			"gfp_xdr_vrecv_sized_x() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (eof) { /* rpc return value missing */
		gflog_debug(GFARM_MSG_1001013,
			"Unexpected EOF when doing xdr vrecv: %s",
			gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
		return (GFARM_ERR_UNEXPECTED_EOF);
	}
	if (**formatp != '\0') {
		gflog_debug(GFARM_MSG_1001014, "gfp_xdr_vrpc_result_sized: "
		    "invalid format character: %c(%x)", **formatp, **formatp);
		return (GFARM_ERRMSG_GFP_XDR_VRPC_INVALID_FORMAT_CHARACTER);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_vrpc_result_sized(struct gfp_xdr *conn, int just,
	size_t *sizep, gfarm_int32_t *errorp,
	const char **formatp, va_list *app)
{
	/*
	 * always do timeout, because this is only called
	 * after asynchronous protocol header is received
	 */
	return (gfp_xdr_vrpc_result_sized_internal(conn, just, 1,
	    sizep, errorp, formatp, app));
}

/*
 * get RPC result
 */
gfarm_error_t
gfp_xdr_vrpc_result(struct gfp_xdr *conn, int just, int do_timeout,
	gfarm_int32_t *errorp, const char **formatp, va_list *app)
{
	return (gfp_xdr_vrpc_result_sized_internal(conn, just, do_timeout,
	    NULL, errorp, formatp, app));
}

/*
 * do RPC with "request-args/result-args" format string.
 */
gfarm_error_t
gfp_xdr_vrpc(struct gfp_xdr *conn, int just, int do_timeout,
	gfarm_int32_t command, gfarm_int32_t *errorp,
	const char **formatp, va_list *app)
{
	gfarm_error_t e;

	/*
	 * send request
	 */
	e = gfp_xdr_vrpc_request(conn, command, formatp, app);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001015,
			"gfp_xdr_vrpc_request() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	if (**formatp != '/') {
#if 1
		gflog_fatal(GFARM_MSG_1000018, "%s",
		    gfarm_error_string(GFARM_ERRMSG_GFP_XDR_VRPC_MISSING_RESULT_IN_FORMAT_STRING));
#endif
		return (GFARM_ERRMSG_GFP_XDR_VRPC_MISSING_RESULT_IN_FORMAT_STRING);
	}
	(*formatp)++;

	return (gfp_xdr_vrpc_result(conn, just, do_timeout,
	    errorp, formatp, app));
}

/*
 * low level interface, this does not wait to receive desired length.
 */
gfarm_error_t
gfp_xdr_recv_partial(struct gfp_xdr *conn, int just, void *data, int length,
	int *receivedp)
{
	gfarm_error_t e;
	int received = gfarm_iobuffer_get_read_partial_x(
	    conn->recvbuffer, data, length, just, 1);

	if (received == 0 && (e = gfarm_iobuffer_get_error(conn->recvbuffer))
	    != GFARM_ERR_NO_ERROR)
		return (e);

	*receivedp = received;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_recv_get_error(struct gfp_xdr *conn)
{
	return (gfarm_iobuffer_get_error(conn->recvbuffer));
}

void
gfp_xdr_begin_sendbuffer_pindown(struct gfp_xdr *conn)
{
	gfarm_iobuffer_begin_pindown(conn->sendbuffer);
}

void
gfp_xdr_end_sendbuffer_pindown(struct gfp_xdr *conn)
{
	gfarm_iobuffer_end_pindown(conn->sendbuffer);
}
