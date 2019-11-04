#include <pthread.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "id_table.h"
#include "thrsubr.h"

#include "liberror.h"
#include "gfp_xdr.h"

/*
 * asynchronous RPC related functions
 */

#define XID_TYPE_BIT		0xc0000000
#define XID_TYPE_REQUEST	0x00000000
#define XID_TYPE_RESULT		0x80000000

static const char async_peer_diag[] = "gfp_xdr_async_peer";

struct gfp_xdr_async_peer {
	struct gfarm_id_table *idtab;
	pthread_mutex_t mutex;
};

struct gfp_xdr_async_callback {
	result_callback_t result_callback;
	disconnect_callback_t disconnect_callback;
	void *closure;
};

static struct gfarm_id_table_entry_ops gfp_xdr_async_xid_table_ops = {
	sizeof(struct gfp_xdr_async_callback)
};

/*
 * asynchronous protocol client side functions
 *
 * currently, all clients of asynchronous protocl are servers too.
 */

gfarm_error_t
gfp_xdr_async_peer_new(gfp_xdr_async_peer_t *async_serverp)
{
	struct gfp_xdr_async_peer *async_server;
	static const char diag[] = "gfp_xdr_async_peer_new";

	GFARM_MALLOC(async_server);
	if (async_server == NULL)
		return (GFARM_ERR_NO_MEMORY);
	gfarm_mutex_init(&async_server->mutex, diag, async_peer_diag);
	async_server->idtab =
	    gfarm_id_table_alloc(&gfp_xdr_async_xid_table_ops);
	if (async_server->idtab == NULL) {
		gfarm_mutex_destroy(&async_server->mutex,
		    diag, async_peer_diag);
		free(async_server);
		return (GFARM_ERR_NO_MEMORY);
	}
	*async_serverp = async_server;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfp_xdr_async_xid_free(void *peer, gfp_xdr_xid_t xid, void *xdata)
{
	struct gfp_xdr_async_callback *cb = xdata;

	(*cb->disconnect_callback)(peer, cb->closure);
}

void
gfp_xdr_async_peer_free(gfp_xdr_async_peer_t async_server, void *peer)
{
	static const char diag[] = "gfp_xdr_async_peer_free";

	gfarm_id_table_free(async_server->idtab, gfp_xdr_async_xid_free, peer);
	gfarm_mutex_destroy(&async_server->mutex, diag, async_peer_diag);
	free(async_server);
}

gfarm_error_t
gfp_xdr_callback_async_result(gfp_xdr_async_peer_t async_server,
	void *peer, gfp_xdr_xid_t xid, size_t size, gfarm_int32_t *resultp)
{
	struct gfp_xdr_async_callback *cb;
	result_callback_t result_callback;
	void *closure;
	static const char diag[] = "gfp_xdr_callback_async_result";

	gfarm_mutex_lock(&async_server->mutex, diag, async_peer_diag);
	cb = gfarm_id_lookup(async_server->idtab, xid);
	if (cb == NULL) {
		gfarm_mutex_unlock(&async_server->mutex, diag, async_peer_diag);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	result_callback = cb->result_callback;
	closure = cb->closure;
	gfarm_id_free(async_server->idtab, xid);
	gfarm_mutex_unlock(&async_server->mutex, diag, async_peer_diag);

	*resultp = (*result_callback)(peer, closure, size);
	return (GFARM_ERR_NO_ERROR);
}

static void
gfp_xdr_send_async_request_error(gfp_xdr_async_peer_t async_server,
	gfarm_int32_t xid, const char *diag)
{
	gfarm_mutex_lock(&async_server->mutex, diag, async_peer_diag);
	gfarm_id_free(async_server->idtab, xid);
	gfarm_mutex_unlock(&async_server->mutex, diag, async_peer_diag);
}

static gfarm_error_t
gfp_xdr_send_async_request_header(struct gfp_xdr *server,
	gfp_xdr_async_peer_t async_server, size_t size,
	result_callback_t result_callback,
	disconnect_callback_t disconnect_callback,
	void *closure,
	gfarm_int32_t *xidp)
{
	gfarm_error_t e;
	gfarm_int32_t xid, xid_and_type;
	struct gfp_xdr_async_callback *cb;
	static const char diag[] = "gfp_xdr_send_async_request_header";

	gfarm_mutex_lock(&async_server->mutex, diag, async_peer_diag);
	cb = gfarm_id_alloc(async_server->idtab, &xid);
	if (cb == NULL) {
		gfarm_mutex_unlock(&async_server->mutex, diag, async_peer_diag);
		return (GFARM_ERR_NO_MEMORY);
	}
	cb->result_callback = result_callback;
	cb->disconnect_callback = disconnect_callback;
	cb->closure = closure;
	gfarm_mutex_unlock(&async_server->mutex, diag, async_peer_diag);

	xid_and_type = (xid | XID_TYPE_REQUEST);
#define ASYNC_REQUEST_HEADER_SIZE	(4+4)	/* size of "ii" */
	e = gfp_xdr_send(server, "ii", xid_and_type, (gfarm_int32_t)size);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_send_async_request_error(async_server, xid, diag);
		return (e);
	}
	*xidp = xid;
	return (GFARM_ERR_NO_ERROR);
}

/* this does gfp_xdr_flush() too, for freeing xid in an error case */
static gfarm_error_t
gfp_xdr_vsend_async_request_internal(struct gfp_xdr *server,
	gfp_xdr_async_peer_t async_server,
	result_callback_t result_callback,
	disconnect_callback_t disconnect_callback,
	void *closure,
	int nonblock,
	gfarm_int32_t command, const char *format, va_list *app)
{
	gfarm_error_t e;
	size_t size = 0;
	va_list ap;
	const char *fmt;
	gfarm_int32_t xid;

	e = gfp_xdr_send_size_add(&size, "i", command);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	va_copy(ap, *app);
	fmt = format;
	e = gfp_xdr_vsend_size_add(&size, &fmt, &ap);
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (nonblock && (e = gfp_xdr_sendbuffer_check_size(server,
	    size + ASYNC_REQUEST_HEADER_SIZE)) != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfp_xdr_send_async_request_header(server, async_server, size,
	    result_callback, disconnect_callback, closure, &xid);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfp_xdr_vrpc_request(server, command, &format, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_send_async_request_error(async_server, xid,
		    "gfp_xdr_vrpc_request");
		return (e);
	}
	if (*format != '\0')
		gflog_fatal(GFARM_MSG_1001016, "gfp_xdr_vsend_async_request: "
		    "invalid format character: %c(%x)", *format, *format);

	e = gfp_xdr_flush(server);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_send_async_request_error(async_server, xid,
		    "gfp_xdr_flush");
		return (e);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_vsend_async_nonblocking_request(struct gfp_xdr *server,
	gfp_xdr_async_peer_t async_server,
	result_callback_t result_callback,
	disconnect_callback_t disconnect_callback,
	void *closure,
	gfarm_int32_t command, const char *format, va_list *app)
{
	return (gfp_xdr_vsend_async_request_internal(server,
	    async_server, result_callback, disconnect_callback, closure, 1,
	    command, format, app));
}

gfarm_error_t
gfp_xdr_vsend_async_request(struct gfp_xdr *server,
	gfp_xdr_async_peer_t async_server,
	result_callback_t result_callback,
	disconnect_callback_t disconnect_callback,
	void *closure,
	gfarm_int32_t command, const char *format, va_list *app)
{
	return (gfp_xdr_vsend_async_request_internal(server,
	    async_server, result_callback, disconnect_callback, closure, 0,
	    command, format, app));
}

/*
 * used by both client and server side
 */

gfarm_error_t
gfp_xdr_recv_async_header(struct gfp_xdr *conn, int just,
	enum gfp_xdr_msg_type *typep, gfp_xdr_xid_t *xidp, size_t *sizep)
{
	gfarm_error_t e;
	gfarm_uint32_t xid;
	gfarm_uint32_t size;
	int eof;

	e = gfp_xdr_recv(conn, just, &eof, "ii", &xid, &size);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (eof)
		return (GFARM_ERR_UNEXPECTED_EOF);
	*typep = (xid & XID_TYPE_BIT) == XID_TYPE_REQUEST ?
	    GFP_XDR_TYPE_REQUEST : GFP_XDR_TYPE_RESULT;
	*xidp = (xid & ~XID_TYPE_BIT);
	*sizep = size;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * server side functions
 */

static gfarm_error_t
gfp_xdr_send_async_result_header(struct gfp_xdr *server,
	gfarm_int32_t xid, size_t size)
{
	xid = (xid | XID_TYPE_RESULT);
	return (gfp_xdr_send(server, "ii", xid, (gfarm_int32_t)size));
}

/*
 * used by both synchronous and asynchronous protocol.
 * if sizep == NULL, it's a synchronous protocol, otherwise asynchronous.
 * Note that this function assumes that async_header is already received.
 */
gfarm_error_t
gfp_xdr_recv_request_command(struct gfp_xdr *client, int just, size_t *sizep,
	gfarm_int32_t *commandp)
{
	gfarm_error_t e;
	int eof;

	/*
	 * always do timeout, because:
	 * - this is called when epoll(2) says it's ready to receive
	 * or
	 * - this is after async protocol header
	 */
	e = gfp_xdr_recv_sized(client, just, 1, sizep, &eof, "i", commandp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (eof)
		return (GFARM_ERR_UNEXPECTED_EOF);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * used by both synchronous and asynchronous protocol.
 * if sizep == NULL, it's a synchronous protocol, otherwise asynchronous.
 */
gfarm_error_t
gfp_xdr_vrecv_request_parameters(struct gfp_xdr *client, int just,
	size_t *sizep, const char *format, va_list *app)
{
	gfarm_error_t e;
	int eof;

	/* always do timeout here, because request type is already received */
	e = gfp_xdr_vrecv_sized(client, just, 1, sizep, &eof, &format, app);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (eof)
		return (GFARM_ERR_UNEXPECTED_EOF);
	if (*format != '\0') {
		gflog_debug(GFARM_MSG_1001017,
		    "gfp_xdr_vrecv_request_parameters: "
		    "invalid format character: %c(%x)", *format, *format);
		return (GFARM_ERRMSG_GFP_XDR_VRPC_INVALID_FORMAT_CHARACTER);
	}
	if (sizep != NULL && *sizep != 0) {
		gflog_debug(GFARM_MSG_1001018,
		    "gfp_xdr_vrecv_request_parameters: residual %d bytes",
		    (int)*sizep);
		return (GFARM_ERR_PROTOCOL);
	}
	return (GFARM_ERR_NO_ERROR);
}

/* the caller should call gfp_xdr_flush() after this function */
gfarm_error_t
gfp_xdr_vsend_result(struct gfp_xdr *client,
	gfarm_int32_t ecode, const char *format, va_list *app)
{
	gfarm_error_t e;

	e = gfp_xdr_send(client, "i", ecode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (ecode == GFARM_ERR_NO_ERROR) {
		e = gfp_xdr_vsend(client, &format, app);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if (*format != '\0') {
			gflog_debug(GFARM_MSG_1001019, "gfp_xdr_vsend_result: "
			    "invalid format character: %c(%x)",
			    *format, *format);
			e = GFARM_ERRMSG_GFP_XDR_VRPC_INVALID_FORMAT_CHARACTER;
		}
	}
	return (e);
}

/* used by asynchronous protocol */
gfarm_error_t
gfp_xdr_vsend_async_result(struct gfp_xdr *client, gfp_xdr_xid_t xid,
	gfarm_int32_t ecode, const char *format, va_list *app)
{
	gfarm_error_t e;
	size_t size = 0;

	e = gfp_xdr_send_size_add(&size, "i", ecode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (ecode == GFARM_ERR_NO_ERROR) {
		va_list ap;
		const char *fmt;

		va_copy(ap, *app);
		fmt = format;
		e = gfp_xdr_vsend_size_add(&size, &fmt, &ap);
		va_end(ap);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	e = gfp_xdr_send_async_result_header(client, xid, size);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfp_xdr_vsend_result(client, ecode, format, app));
}
