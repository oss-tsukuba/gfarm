#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h> /* ntoh[ls]()/hton[ls]() on glibc */
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h" /* gflog_fatal() */

#include "liberror.h"
#include "iobuffer.h"
#include "gfp_xdr.h"

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

	gfarm_iobuffer_set_read(conn->recvbuffer, ops->blocking_read,
	    cookie, fd);
	gfarm_iobuffer_set_write(conn->sendbuffer, ops->blocking_write,
	    cookie, fd);
}

gfarm_error_t
gfp_xdr_new(struct gfp_iobuffer_ops *ops, void *cookie, int fd,
	struct gfp_xdr **connp)
{
	struct gfp_xdr *conn;

	GFARM_MALLOC(conn);
	if (conn == NULL) {
		gflog_debug(GFARM_MSG_1000996,
			"allocation of 'conn' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	conn->recvbuffer = gfarm_iobuffer_alloc(GFP_XDR_BUFSIZE);
	if (conn->recvbuffer == NULL) {
		free(conn);
		gflog_debug(GFARM_MSG_1000997,
			"allocation of 'conn recvbuffer' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	conn->sendbuffer = gfarm_iobuffer_alloc(GFP_XDR_BUFSIZE);
	if (conn->sendbuffer == NULL) {
		gfarm_iobuffer_free(conn->recvbuffer);
		free(conn);
		gflog_debug(GFARM_MSG_1000998,
			"allocation of 'conn sendbuffer' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
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


void
gfarm_iobuffer_set_nonblocking_read_xxx(struct gfarm_iobuffer *b,
	struct gfp_xdr *conn)
{
	gfarm_iobuffer_set_read(b, conn->iob_ops->nonblocking_read,
	    conn->cookie, conn->fd);
}

void
gfarm_iobuffer_set_nonblocking_write_xxx(struct gfarm_iobuffer *b,
	struct gfp_xdr *conn)
{
	gfarm_iobuffer_set_write(b, conn->iob_ops->nonblocking_write,
	    conn->cookie, conn->fd);
}

int
gfp_xdr_recv_is_ready(struct gfp_xdr *conn)
{
	return (!gfarm_iobuffer_empty(conn->recvbuffer) ||
		gfarm_iobuffer_is_eof(conn->recvbuffer));
}

gfarm_error_t
gfp_xdr_flush(struct gfp_xdr *conn)
{
	gfarm_iobuffer_flush_write(conn->sendbuffer);
	return (gfarm_iobuffer_get_error(conn->sendbuffer));
}

gfarm_error_t
gfp_xdr_purge_sized(struct gfp_xdr *conn, int just, int len, size_t *sizep)
{
	int rv;

	if (*sizep < len) {
		gflog_debug(GFARM_MSG_1000999, "gfp_xdr_purge_sized: "
		    "%d bytes expected, but only %d bytes remains",
		    len, (int)*sizep);
		return (GFARM_ERR_PROTOCOL);
	}
	rv = gfarm_iobuffer_purge_read_x(conn->recvbuffer, len, just);
	*sizep -= rv;
	if (rv != len)
		return (GFARM_ERR_UNEXPECTED_EOF);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_purge(struct gfp_xdr *conn, int just, int len)
{
	if (gfarm_iobuffer_purge_read_x(conn->recvbuffer, len, just) != len) {
		gflog_debug(GFARM_MSG_1001000,
			"gfarm_iobuffer_purge_read_x() failed: %s",
			gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
		return (GFARM_ERR_UNEXPECTED_EOF);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_vsend_size_add(size_t *sizep, const char **formatp, va_list *app)
{
	const char *format = *formatp;
	size_t size = *sizep;
	gfarm_uint8_t c;
	gfarm_int16_t h;
	gfarm_int32_t i, n;
	gfarm_int64_t o;
	gfarm_uint32_t lv[2];
#if INT64T_IS_FLOAT
	int minus;
#endif
	double d;
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nd;
#else
#	define nd d
#endif
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
			o = va_arg(*app, gfarm_int64_t);
			size += sizeof(lv);
			continue;
		case 's':
			s = va_arg(*app, const char *);
			n = strlen(s);
			size += sizeof(i);
			size += n;
			continue;
		case 'S':
			s = va_arg(*app, const char *);
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
			s = va_arg(*app, const char *);
			size += sizeof(i);
			size += n;
			continue;
		case 'f':
			d = va_arg(*app, double);
			size += sizeof(nd);
			continue;

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
	double d;
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nd;
#else
#	define nd d
#endif
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
		case 'f':
			d = va_arg(*app, double);
#ifndef WORDS_BIGENDIAN
			swab(&d, &nd, sizeof(nd));
#endif
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &nd, sizeof(nd));
			continue;

		default:
			break;
		}

		break;
	}
	*formatp = format;
	return (gfarm_iobuffer_get_error(conn->sendbuffer));
}

static gfarm_error_t
recv_sized(struct gfp_xdr *conn, int just, void *p, size_t sz,
	size_t *sizep)
{
	int rv;

	if (*sizep < sz) {
		gflog_debug(GFARM_MSG_1001001, "recv_size: "
		    "%d bytes expected, but only %d bytes remains",
		    (int)sz, (int)*sizep);
		return (GFARM_ERR_PROTOCOL);  /* too short message */
	}
	rv = gfarm_iobuffer_get_read_x(conn->recvbuffer, p, sz, just);
	*sizep -= rv;
	if (rv != sz) {
		gflog_debug(GFARM_MSG_1001002, "recv_size: "
		    "%d bytes expected, but only %d bytes read",
		    (int)sz, rv);
		if (rv == 0) /* maybe usual EOF */
			return (GFARM_ERR_UNEXPECTED_EOF);
		return (GFARM_ERR_PROTOCOL);	/* really unexpected EOF */
	}
	return (GFARM_ERR_NO_ERROR); /* rv may be 0, if sz == 0 */
}

gfarm_error_t
gfp_xdr_vrecv_sized(struct gfp_xdr *conn, int just, size_t *sizep,
	int *eofp, const char **formatp, va_list *app)
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
	double *dp;
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nd;
#endif
	char **sp, *s;
	size_t *szp, sz;
	size_t size;
	int overflow = 0;

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
			if ((e = recv_sized(conn, just, cp, sizeof(*cp),
			    &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF)
					return (GFARM_ERR_NO_ERROR); /* EOF */
				break;
			}
			continue;
		case 'h':
			hp = va_arg(*app, gfarm_int16_t *);
			if ((e = recv_sized(conn, just, hp, sizeof(*hp),
			    &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF)
					return (GFARM_ERR_NO_ERROR); /* EOF */
				break;
			}
			*hp = ntohs(*hp);
			continue;
		case 'i':
			ip = va_arg(*app, gfarm_int32_t *);
			if ((e = recv_sized(conn, just, ip, sizeof(*ip),
			    &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF)
					return (GFARM_ERR_NO_ERROR); /* EOF */
				break;
			}
			*ip = ntohl(*ip);
			continue;
		case 'l':
			op = va_arg(*app, gfarm_int64_t *);
			/*
			 * note that because actual type of gfarm_int64_t
			 * may be diffenent (int64_t or double), we must
			 * not pass this as is via network.
			 */
			if ((e = recv_sized(conn, just, lv, sizeof(lv),
			    &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF)
					return (GFARM_ERR_NO_ERROR); /* EOF */
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
			continue;
		case 's':
			sp = va_arg(*app, char **);
			if ((e = recv_sized(conn, just, &i, sizeof(i),
			    &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF)
					return (GFARM_ERR_NO_ERROR); /* EOF */
				break;
			}
			i = ntohl(i);
			sz = gfarm_size_add(&overflow, i, 1);
			if (overflow) {
				e = GFARM_ERR_PROTOCOL;
				break;
			}
			GFARM_MALLOC_ARRAY(*sp, sz);
			if (*sp == NULL) {
				if ((e = gfp_xdr_purge_sized(conn, just,
				    sz, &size)) != GFARM_ERR_NO_ERROR)
					break;
				e_save = GFARM_ERR_NO_MEMORY;
				continue;
			}
			if ((e = recv_sized(conn, just, *sp, i, &size))
			    != GFARM_ERR_NO_ERROR)
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
			if ((e = recv_sized(conn, just, &i, sizeof(i),
			     &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF)
					return (GFARM_ERR_NO_ERROR); /* EOF */
				break;
			}
			i = ntohl(i);
			*szp = i;
			if (i <= sz) {
				if ((e = recv_sized(conn, just, s, i,
				     &size)) != GFARM_ERR_NO_ERROR)
					break;
			} else {
				if (size < i) {
					e = GFARM_ERR_PROTOCOL;
					break;
				}
				if ((e = recv_sized(conn, just, s, sz,
				     &size)) != GFARM_ERR_NO_ERROR)
					break;
				/* abandon (i - sz) bytes */
				if ((e = gfp_xdr_purge_sized(conn, just,
				    i - sz, &size)) != GFARM_ERR_NO_ERROR)
					break;
			}
			continue;
		case 'B':
			szp = va_arg(*app, size_t *);
			sp = va_arg(*app, char **);
			/*
			 * note that because actual type of size_t may be
			 * diffenent ([u]int32_t or [u]int64_t), we must not
			 * pass this as is via network.
			 */
			if ((e = recv_sized(conn, just, &i, sizeof(i),
			     &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF)
					return (GFARM_ERR_NO_ERROR); /* EOF */
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
			if (*sp == NULL) {
				if ((e = gfp_xdr_purge_sized(conn, just,
				    sz, &size)) != GFARM_ERR_NO_ERROR)
					break;
				e_save = GFARM_ERR_NO_MEMORY;
				continue;
			}
			if ((e = recv_sized(conn, just, *sp, i, &size))
			    != GFARM_ERR_NO_ERROR)
				break;
			continue;
		case 'f':
			dp = va_arg(*app, double *);
			assert(sizeof(*dp) == 8);
			if ((e = recv_sized(conn, just, dp, sizeof(*dp),
			     &size)) != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF)
					return (GFARM_ERR_NO_ERROR); /* EOF */
				break;
			}
#ifndef WORDS_BIGENDIAN
			swab(dp, &nd, sizeof(nd));
			*dp = *(double *)&nd;
#endif
			continue;

		default:
			break;
		}

		break;
	}
	if (sizep != NULL) 
		*sizep = size;
	*formatp = format;
	*eofp = 0;
	/* XXX FIXME free memory allocated by this funciton at an error */

	/* connection error has most precedence to avoid protocol confusion */
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/* iobuffer error may be a connection error */
	if ((e = gfarm_iobuffer_get_error(conn->recvbuffer)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);
	return (e_save); /* NO_MEMORY or SUCCESS */
}

gfarm_error_t
gfp_xdr_vrecv(struct gfp_xdr *conn, int just,
	int *eofp, const char **formatp, va_list *app)
{
	return (gfp_xdr_vrecv_sized(conn, just, NULL, eofp, formatp, app));
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

gfarm_error_t
gfp_xdr_recv_sized(struct gfp_xdr *conn, int just, size_t *sizep,
	int *eofp, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv_sized(conn, just, sizep, eofp, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (*eofp)
		return (GFARM_ERR_NO_ERROR);
	if (*format != '\0') {
		gflog_debug(GFARM_MSG_1001006, "gfp_xdr_recv_sized: "
		    "invalid format character: %c(%x)", *format, *format);
		return (GFARM_ERRMSG_GFP_XDR_RECV_INVALID_FORMAT_CHARACTER);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_recv(struct gfp_xdr *conn, int just,
	int *eofp, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv(conn, just, eofp, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001007,
			"gfp_xdr_vrecv() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (*eofp)
		return (GFARM_ERR_NO_ERROR);
	if (*format != '\0') {
		gflog_debug(GFARM_MSG_1001008, "gfp_xdr_recv: "
		    "invalid format character: %c(%x)", *format, *format);
		return (GFARM_ERRMSG_GFP_XDR_RECV_INVALID_FORMAT_CHARACTER);
	}
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
gfarm_error_t
gfp_xdr_vrpc_result_sized(struct gfp_xdr *conn,	int just, size_t *sizep,
	gfarm_int32_t *errorp, const char **formatp, va_list *app)
{
	gfarm_error_t e;
	int eof;

	/*
	 * receive response
	 */
	e = gfp_xdr_recv_sized(conn, just, sizep, &eof, "i", errorp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001010,
			"receiving response (%d) failed: %s",
			just,
			gfarm_error_string(e));
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

	e = gfp_xdr_vrecv_sized(conn, just, sizep, &eof, formatp, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001012,
			"gfp_xdr_vrecv_sized() failed: %s",
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

/*
 * get RPC result
 */
gfarm_error_t
gfp_xdr_vrpc_result(struct gfp_xdr *conn,
	int just, gfarm_int32_t *errorp, const char **formatp, va_list *app)
{
	return (gfp_xdr_vrpc_result_sized(conn, just, NULL,
	    errorp, formatp, app));
}

/*
 * do RPC with "request-args/result-args" format string.
 */
gfarm_error_t
gfp_xdr_vrpc(struct gfp_xdr *conn, int just, gfarm_int32_t command,
	gfarm_int32_t *errorp, const char **formatp, va_list *app)
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
		abort();
#endif
		return (GFARM_ERRMSG_GFP_XDR_VRPC_MISSING_RESULT_IN_FORMAT_STRING);
	}
	(*formatp)++;

	return (gfp_xdr_vrpc_result(conn, just, errorp, formatp, app));
}

/*
 * low level interface, this does not wait to receive desired length.
 */
int
gfp_xdr_recv_partial(struct gfp_xdr *conn, int just, void *data, int length)
{
	return (gfarm_iobuffer_get_read_partial_x(
	    conn->recvbuffer, data, length, just));
}

gfarm_error_t
gfp_xdr_recv_get_error(struct gfp_xdr *conn)
{
	return (gfarm_iobuffer_get_error(conn->recvbuffer));
}

/*
 * lowest level interface,
 * this does not wait to receive desired length, and
 * this does not honor iobuffer.
 */
gfarm_error_t
gfp_xdr_read_direct(struct gfp_xdr *conn, void *data, int length,
	int *resultp)
{
	int rv = (*conn->iob_ops->blocking_read)(conn->recvbuffer,
	    conn->cookie, conn->fd, data, length);

	if (rv == -1) {
		*resultp = 0;
		return (gfarm_iobuffer_get_error(conn->recvbuffer));
	}
	*resultp = rv;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_write_direct(struct gfp_xdr *conn, void *data, int length,
	int *resultp)
{
	int rv = (*conn->iob_ops->blocking_write)(conn->sendbuffer,
	    conn->cookie, conn->fd, data, length);

	if (rv == -1) {
		*resultp = 0;
		return (gfarm_iobuffer_get_error(conn->sendbuffer));
	}
	*resultp = rv;
	return (GFARM_ERR_NO_ERROR);
}
