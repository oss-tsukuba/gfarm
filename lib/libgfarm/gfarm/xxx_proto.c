#include <sys/types.h>
#include <netinet/in.h> /* ntoh[ls]()/hton[ls]() on glibc */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "iobuffer.h"
#include "gfutil.h"
#include "xxx_proto.h"

#if FILE_OFFSET_T_IS_FLOAT
#include <math.h>

#define POWER2_32	4294967296.0		/* 2^32 */
#endif

#define XXX_BUFSIZE	16384

struct xxx_connection {
	struct gfarm_iobuffer *recvbuffer;
	struct gfarm_iobuffer *sendbuffer;

	struct xxx_iobuffer_ops *iob_ops;
	void *cookie;
	int fd;
};

/*
 * switch to new iobuffer operation,
 * and (possibly) switch to new cookie/fd
 */
void xxx_connection_set(struct xxx_connection *conn,
	struct xxx_iobuffer_ops *ops, void *cookie, int fd)
{
	conn->iob_ops = ops;
	conn->cookie = cookie;
	conn->fd = fd;

	gfarm_iobuffer_set_read(conn->recvbuffer, ops->blocking_read,
	    cookie, fd);
	gfarm_iobuffer_set_write(conn->sendbuffer, ops->blocking_write,
	    cookie, fd);
}

char *
xxx_connection_new(struct xxx_iobuffer_ops *ops, void *cookie, int fd,
	struct xxx_connection **connp)
{
	struct xxx_connection *conn;

	GFARM_MALLOC(conn);
	if (conn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	conn->recvbuffer = gfarm_iobuffer_alloc(XXX_BUFSIZE);
	if (conn->recvbuffer == NULL) {
		free(conn);
		return (GFARM_ERR_NO_MEMORY);
	}
	conn->sendbuffer = gfarm_iobuffer_alloc(XXX_BUFSIZE);
	if (conn->sendbuffer == NULL) {
		gfarm_iobuffer_free(conn->recvbuffer);
		free(conn);
		return (GFARM_ERR_NO_MEMORY);
	}
	xxx_connection_set(conn, ops, cookie, fd);

	*connp = conn;
	return (NULL);
}

char *
xxx_connection_free(struct xxx_connection *conn)
{
	char *e, *e_save;

	e_save = xxx_proto_flush(conn);
	gfarm_iobuffer_free(conn->sendbuffer);
	gfarm_iobuffer_free(conn->recvbuffer);

	e = (*conn->iob_ops->close)(conn->cookie, conn->fd);
	if (e_save == NULL)
		e_save = e;

	free(conn);
	return (e);
}

void *
xxx_connection_cookie(struct xxx_connection *conn)
{
	return (conn->cookie);
}

int
xxx_connection_fd(struct xxx_connection *conn)
{
	return (conn->fd);
}


char *
xxx_connection_export_credential(struct xxx_connection *conn)
{
	return ((*conn->iob_ops->export_credential)(conn->cookie));
}

char *
xxx_connection_delete_credential(struct xxx_connection *conn)
{
	return ((*conn->iob_ops->delete_credential)(conn->cookie));
}

char *
xxx_connection_env_for_credential(struct xxx_connection *conn)
{
	return ((*conn->iob_ops->env_for_credential)(conn->cookie));
}


void
gfarm_iobuffer_set_nonblocking_read_xxx(struct gfarm_iobuffer *b, 
	struct xxx_connection *conn)
{
	gfarm_iobuffer_set_read(b, conn->iob_ops->nonblocking_read,
	    conn->cookie, conn->fd);
}

void
gfarm_iobuffer_set_nonblocking_write_xxx(struct gfarm_iobuffer *b,
	struct xxx_connection *conn)
{
	gfarm_iobuffer_set_write(b, conn->iob_ops->nonblocking_write,
	    conn->cookie, conn->fd);
}

char *
xxx_proto_flush(struct xxx_connection *conn)
{
	gfarm_iobuffer_flush_write(conn->sendbuffer);
	return (gfarm_iobuffer_get_error(conn->sendbuffer));
}

char *
xxx_proto_purge(struct xxx_connection *conn, int just, int len)
{
	if (gfarm_iobuffer_purge_read_x(conn->recvbuffer, len, just) != len)
		return (GFARM_ERR_UNEXPECTED_EOF);
	return (NULL);
}

char *
xxx_proto_vsend(struct xxx_connection *conn, char **formatp, va_list *app)
{
	char *format = *formatp;
	gfarm_uint8_t c;
	gfarm_int16_t h;
	gfarm_int32_t i, n;
	file_offset_t o;
	gfarm_uint32_t ov[2];
#if FILE_OFFSET_T_IS_FLOAT
	int minus;
#endif
	char *s;

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
		case 'o':
			/*
			 * note that because actual type of file_offset_t
			 * may be diffenent (int64_t or double), we must
			 * not pass this as is via network.
			 */
			o = va_arg(*app, file_offset_t);
#if FILE_OFFSET_T_IS_FLOAT
			minus = o < 0;
			if (minus)
				o = -o;
			ov[0] = o / POWER2_32;
			ov[1] = o - ov[0] * POWER2_32;
			if (minus) {
				ov[0] = ~ov[0];
				ov[1] = ~ov[1];
				if (++ov[1] == 0)
					++ov[0];
			}
#else
			ov[0] = o >> 32;
			ov[1] = o;
#endif
			ov[0] = htonl(ov[0]);
			ov[1] = htonl(ov[1]);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    ov, sizeof(ov));
			break;
		case 's':
			s = va_arg(*app, char *);
			n = strlen(s);
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
			s = va_arg(*app, char *);
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    &i, sizeof(i));
			gfarm_iobuffer_put_write(conn->sendbuffer,
			    s, n);
			break;
		default:
			goto finish;
		}
	}
 finish:
	*formatp = format;
	return (gfarm_iobuffer_get_error(conn->sendbuffer));
}

char *
xxx_proto_vrecv(struct xxx_connection *conn, int just, int *eofp,
	char **formatp, va_list *app)
{
	char *format = *formatp;
	gfarm_int8_t *cp;
	gfarm_int16_t *hp;
	gfarm_int32_t *ip, i;
	file_offset_t *op;
	gfarm_uint32_t ov[2];
#if FILE_OFFSET_T_IS_FLOAT
	int minus;
#endif
	char **sp, *s;
	size_t *szp, sz;
	char *e;
	size_t size;
	int overflow = 0;

	e = xxx_proto_flush(conn);
	if (e != NULL)
		return (e);

	*eofp = 1;

	for (; *format; format++) {
		switch (*format) {
		case 'c':
			cp = va_arg(*app, gfarm_int8_t *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    cp, sizeof(*cp), just) != sizeof(*cp))
				return (NULL);
			break;
		case 'h':
			hp = va_arg(*app, gfarm_int16_t *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    hp, sizeof(*hp), just) != sizeof(*hp))
				return (NULL);
			*hp = ntohs(*hp);
			break;
		case 'i':
			ip = va_arg(*app, gfarm_int32_t *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    ip, sizeof(*ip), just) != sizeof(*ip))
				return (NULL);
			*ip = ntohl(*ip);
			break;
		case 'o':
			/*
			 * note that because actual type of file_offset_t
			 * may be diffenent (int64_t or double), we must
			 * not pass this as is via network.
			 */
			op = va_arg(*app, file_offset_t *);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    ov, sizeof(ov), just) != sizeof(ov))
				return (NULL);
			ov[0] = ntohl(ov[0]);
			ov[1] = ntohl(ov[1]);
#if FILE_OFFSET_T_IS_FLOAT
			minus = ov[0] & 0x80000000;
			if (minus) {
				ov[0] = ~ov[0];
				ov[1] = ~ov[1];
				if (++ov[1] == 0)
					++ov[0];
			}
			*op = ov[0] * POWER2_32 + ov[1];
			if (minus)
				*op = -*op;
#else
			*op = ((file_offset_t)ov[0] << 32) | ov[1];
#endif
			break;
		case 's':
			sp = va_arg(*app, char **);
			if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
			    &i, sizeof(i), just) != sizeof(i))
				return (NULL);
			i = ntohl(i);
			size = gfarm_size_add(&overflow, i, 1);
			if (!overflow)
				GFARM_MALLOC_ARRAY(*sp, size);
			if (!overflow && *sp != NULL) {
				/* caller should check whether *sp == NULL */
				if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
				    *sp, i, just) != i)
					return (NULL);
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
				return (NULL);
			i = ntohl(i);
			*szp = i;
			if (i <= sz) {
				if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
				    s, i, just) != i)
					return (NULL);
			} else {
				if (gfarm_iobuffer_get_read_x(conn->recvbuffer,
				    s, sz, just) != sz)
					return (NULL);
				/* abandon (i - sz) bytes */
				if (gfarm_iobuffer_purge_read_x(
				    conn->recvbuffer, i - sz, just) != i - sz)
					return (NULL);
			}
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

char *
xxx_proto_send(struct xxx_connection *conn, char *format, ...)
{
	va_list ap;
	char *e;

	va_start(ap, format);
	e = xxx_proto_vsend(conn, &format, &ap);
	va_end(ap);

	if (e != NULL)
		return (e);
	if (*format != '\0')
		return ("xxx_proto_send: invalid format character");
	return (NULL);
}

char *
xxx_proto_recv(struct xxx_connection *conn,
	int just, int *eofp, char *format, ...)
{
	va_list ap;
	char *e;

	va_start(ap, format);
	e = xxx_proto_vrecv(conn, just, eofp, &format, &ap);
	va_end(ap);

	if (e != NULL)
		return (e);
	if (*eofp)
		return (NULL);
	if (*format != '\0')
		return ("xxx_proto_recv: invalid format character");
	return (NULL);
}

/*
 * do RPC request
 */
char *
xxx_proto_vrpc_request(struct xxx_connection *conn, gfarm_int32_t command,
		       char **formatp, va_list *app)
{
	char *e;

	/*
	 * send request
	 */
	e = xxx_proto_send(conn, "i", command);
	if (e != NULL)
		return (e);
	e = xxx_proto_vsend(conn, formatp, app);
	if (e != NULL)
		return (e);
	return (NULL);
}

/*
 * get RPC result
 */
char *
xxx_proto_vrpc_result(struct xxx_connection *conn,
	int just, gfarm_int32_t *errorp, char **formatp, va_list *app)
{
	char *e;
	int eof;

	/*
	 * receive response
	 */
	e = xxx_proto_recv(conn, just, &eof, "i", errorp);
	if (e != NULL)
		return (e);
	if (eof)
		return (GFARM_ERR_UNEXPECTED_EOF); /* rpc status missing */
	if (*errorp != 0)
		return (NULL); /* should examine error in this case */
	e = xxx_proto_vrecv(conn, just, &eof, formatp, app);
	if (e != NULL)
		return (e);
	if (eof)
		return (GFARM_ERR_UNEXPECTED_EOF); /* rpc return value missing */
	return (NULL);
}

/*
 * do RPC with "request-args/result-args" format string.
 */
char *
xxx_proto_vrpc(struct xxx_connection *conn, int just, gfarm_int32_t command,
	gfarm_int32_t *errorp, char **formatp, va_list *app)
{
	char *e;

	/*
	 * send request
	 */
	e = xxx_proto_vrpc_request(conn, command, formatp, app);
	if (e != NULL)
		return (e);

	if (**formatp != '/')
		return ("xxx_proto_vrpc: missing result in format string");
	(*formatp)++;

	e = xxx_proto_vrpc_result(conn, just, errorp, formatp, app);
	if (e != NULL)
		return (e);

	if (*errorp != 0)
		return (NULL); /* should examine error in this case */

	if (**formatp != '\0')
		return ("xxx_proto_vrpc: invalid format character");
	return (NULL);
}

/*
 * low level interface, this does not wait to receive desired length.
 */
int
xxx_recv_partial(struct xxx_connection *conn, int just, void *data, int length)
{
	return (gfarm_iobuffer_get_read_partial_x(
			conn->recvbuffer, data, length, just));
}

/*
 * lowest level interface,
 * this does not wait to receive desired length, and
 * this does not honor iobuffer.
 */
char *
xxx_read_direct(struct xxx_connection *conn, void *data, int length,
	int *resultp)
{
	int rv = (*conn->iob_ops->blocking_read)(conn->recvbuffer,
	    conn->cookie, conn->fd, data, length);

	if (rv == -1) {
		*resultp = 0;
		return (gfarm_iobuffer_get_error(conn->recvbuffer));
	}
	*resultp = rv;
	return (NULL);
}

char *
xxx_write_direct(struct xxx_connection *conn, void *data, int length,
	int *resultp)
{
	int rv = (*conn->iob_ops->blocking_write)(conn->sendbuffer,
	    conn->cookie, conn->fd, data, length);

	if (rv == -1) {
		*resultp = 0;
		return (gfarm_iobuffer_get_error(conn->sendbuffer));
	}
	*resultp = rv;
	return (NULL);
}
