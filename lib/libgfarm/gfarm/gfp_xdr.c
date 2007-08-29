#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h> /* ntoh[ls]()/hton[ls]() on glibc */
#include <gfarm/gfarm_config.h>
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
	if (conn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	conn->recvbuffer = gfarm_iobuffer_alloc(GFP_XDR_BUFSIZE);
	if (conn->recvbuffer == NULL) {
		free(conn);
		return (GFARM_ERR_NO_MEMORY);
	}
	conn->sendbuffer = gfarm_iobuffer_alloc(GFP_XDR_BUFSIZE);
	if (conn->sendbuffer == NULL) {
		gfarm_iobuffer_free(conn->recvbuffer);
		free(conn);
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

gfarm_error_t
gfp_xdr_flush(struct gfp_xdr *conn)
{
	gfarm_iobuffer_flush_write(conn->sendbuffer);
	return (gfarm_iobuffer_get_error(conn->sendbuffer));
}

gfarm_error_t
gfp_xdr_purge(struct gfp_xdr *conn, int just, int len)
{
	if (gfarm_iobuffer_purge_read_x(conn->recvbuffer, len, just) != len)
		return (GFARM_ERR_UNEXPECTED_EOF);
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
			break;
		case 'h':
			h = va_arg(*app, int);
			h = htons(h);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &h, sizeof(h));
			break;
		case 'i':
			i = va_arg(*app, gfarm_int32_t);
			i = htonl(i);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &i, sizeof(i));
			break;
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
			break;
		case 's':
			s = va_arg(*app, const char *);
			n = strlen(s);
			i = htonl(n);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &i, sizeof(i));
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    s, n);
			break;
		case 'S':
			s = va_arg(*app, const char *);
			n = va_arg(*app, size_t);
			i = htonl(n);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &i, sizeof(i));
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    s, n);
			break;
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
			break;
		case 'f':
			d = va_arg(*app, double);
#ifndef WORDS_BIGENDIAN
			swab(&d, &nd, sizeof(nd));
#endif
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &nd, sizeof(nd));
			break;

		default:
			goto finish;
		}
	}
 finish:
	*formatp = format;
	return (gfarm_iobuffer_get_error(conn->sendbuffer));
}

gfarm_error_t
gfp_xdr_vrecv(struct gfp_xdr *conn, int just, int *eofp,
	const char **formatp, va_list *app)
{
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


	/* do not call gfp_xdr_flush() here for a compound procedure */
	*eofp = 1;

	for (; *format; format++) {
		switch (*format) {
		case 'c':
			cp = va_arg(*app, gfarm_int8_t *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    cp, sizeof(*cp), just) != sizeof(*cp))
				return (GFARM_ERR_NO_ERROR);
			break;
		case 'h':
			hp = va_arg(*app, gfarm_int16_t *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    hp, sizeof(*hp), just) != sizeof(*hp))
				return (GFARM_ERR_NO_ERROR);
			*hp = ntohs(*hp);
			break;
		case 'i':
			ip = va_arg(*app, gfarm_int32_t *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    ip, sizeof(*ip), just) != sizeof(*ip))
				return (GFARM_ERR_NO_ERROR);
			*ip = ntohl(*ip);
			break;
		case 'l':
			/*
			 * note that because actual type of gfarm_int64_t
			 * may be diffenent (int64_t or double), we must
			 * not pass this as is via network.
			 */
			op = va_arg(*app, gfarm_int64_t *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    lv, sizeof(lv), just) != sizeof(lv))
				return (GFARM_ERR_NO_ERROR);
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
			break;
		case 's':
			sp = va_arg(*app, char **);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    &i, sizeof(i), just) != sizeof(i))
				return (GFARM_ERR_NO_ERROR);
			i = ntohl(i);
			size = gfarm_size_add(&overflow, i, 1);
			if (!overflow)
				GFARM_MALLOC_ARRAY(*sp, size);
			if (!overflow && *sp != NULL) {
				/* caller should check whether *sp == NULL */
				if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
				    *sp, i, just) != i)
					return (GFARM_ERR_NO_ERROR);
				(*sp)[i] = '\0';
			}
			break;
		case 'b':
			/*
			 * note that because actual type of size_t may be
			 * diffenent ([u]int32_t or [u]int64_t), we must not
			 * pass this as is via network.
			 */
			sz = va_arg(*app, size_t);
			szp = va_arg(*app, size_t *);
			s = va_arg(*app, char *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    &i, sizeof(i), just) != sizeof(i))
				return (GFARM_ERR_NO_ERROR);
			i = ntohl(i);
			*szp = i;
			if (i <= sz) {
				if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
				    s, i, just) != i)
					return (GFARM_ERR_NO_ERROR);
			} else {
				if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
				    s, sz, just) != sz)
					return (GFARM_ERR_NO_ERROR);
				/* abandon (i - sz) bytes */
				if (gfarm_iobuffer_purge_read_x(
				    conn->recvbuffer, i - sz, just) != i - sz)
					return (GFARM_ERR_NO_ERROR);
			}
			break;
		case 'f':
			dp = va_arg(*app, double *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    dp, sizeof(*dp), just) != sizeof(*dp))
				return (GFARM_ERR_NO_ERROR);
#ifndef WORDS_BIGENDIAN
			swab(dp, &nd, sizeof(nd));
			*dp = *(double *)&nd;
#endif
			break;
		default:
			goto finish;
		}
	}
 finish:
	*formatp = format;
	*eofp = 0;
	return (gfarm_iobuffer_get_error(conn->recvbuffer));
}

gfarm_error_t
gfp_xdr_send(struct gfp_xdr *conn, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vsend(conn, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (*format != '\0')
		return (GFARM_ERRMSG_GFP_XDR_SEND_INVALID_FORMAT_CHARACTER);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_recv(struct gfp_xdr *conn,
	int just, int *eofp, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv(conn, just, eofp, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (*eofp)
		return (GFARM_ERR_NO_ERROR);
	if (*format != '\0')
		return (GFARM_ERRMSG_GFP_XDR_RECV_INVALID_FORMAT_CHARACTER);
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
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfp_xdr_vsend(conn, formatp, app);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * get RPC result
 */
gfarm_error_t
gfp_xdr_vrpc_result(struct gfp_xdr *conn,
	int just, gfarm_int32_t *errorp, const char **formatp, va_list *app)
{
	gfarm_error_t e;
	int eof;

	/*
	 * receive response
	 */
	e = gfp_xdr_recv(conn, just, &eof, "i", errorp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (eof) /* rpc status missing */
		return (GFARM_ERR_UNEXPECTED_EOF);
	if (*errorp != 0) /* should examine the *errorp in this case */
		return (GFARM_ERR_NO_ERROR);
	e = gfp_xdr_vrecv(conn, just, &eof, formatp, app);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (eof) /* rpc return value missing */
		return (GFARM_ERR_UNEXPECTED_EOF);
	return (GFARM_ERR_NO_ERROR);
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
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (**formatp != '/') {
#if 1
		gflog_fatal(gfarm_error_string(GFARM_ERRMSG_GFP_XDR_VRPC_MISSING_RESULT_IN_FORMAT_STRING));
		abort();
#endif
		return (GFARM_ERRMSG_GFP_XDR_VRPC_MISSING_RESULT_IN_FORMAT_STRING);
	}
	(*formatp)++;

	e = gfp_xdr_vrpc_result(conn, just, errorp, formatp, app);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (*errorp != 0) /* should examine the *errorp in this case */
		return (GFARM_ERR_NO_ERROR);

	if (**formatp != '\0')
		return (GFARM_ERRMSG_GFP_XDR_VRPC_INVALID_FORMAT_CHARACTER);
	return (GFARM_ERR_NO_ERROR);
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
