#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <gfarm/gfarm.h>

#include "queue.h"
#include "id_table.h"

#include "liberror.h"
#include "gfp_xdr.h"

struct gfp_xdr_xid_record {
	gfp_xdr_xid_t xid;

	GFARM_HCIRCLEQ_ENTRY(gfp_xdr_xid_record) next_xid;
};

struct gfp_xdr_context {
	GFARM_HCIRCLEQ_HEAD(gfp_xdr_xid_record) list;
};

struct gfp_xdr_async_server {
	struct gfarm_id_table *idtab;
};

static struct gfarm_id_table_entry_ops gfp_xdr_async_server_xid_table_ops = {
	sizeof(struct gfp_xdr_xid_record)
};

gfarm_error_t
gfp_xdr_client_new(struct gfp_iobuffer_ops *ops, void *cookie, int fd,
	int flags, struct gfp_xdr **connp)
{
	static gfarm_error_t e;
	struct gfp_xdr *conn;
	struct gfp_xdr_async_server *async_server;

	e = gfp_xdr_new(ops, cookie, fd, flags, &conn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC(async_server);
	if (async_server == NULL) {
		gfp_xdr_free(conn);
		return (GFARM_ERR_NO_MEMORY);
	}
	async_server->idtab =
	    gfarm_id_table_alloc(&gfp_xdr_async_server_xid_table_ops);
	if (async_server->idtab == NULL) {
		free(async_server);
		gfp_xdr_free(conn);
		return (GFARM_ERR_NO_MEMORY);
	}
	gfp_xdr_set_async(conn, async_server);
	
	*connp = conn;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfp_xdr_async_server_xid_free(void *peer, gfp_xdr_xid_t xid, void *xdata)
{
	/* NOP: each idtab entries must be freed by gfp_xdr_context_free */
}

void
xdr_xdr_client_free(struct gfp_xdr *conn)
{
	struct gfp_xdr_async_server *async_server = gfp_xdr_async(conn);
	
	gfarm_id_table_free(async_server->idtab,
	    gfp_xdr_async_server_xid_free, NULL);
	free(async_server);
	gfp_xdr_free(conn);
}

static void
gfp_xdr_client_request_free(struct gfp_xdr_async_server *async_server,
	gfp_xdr_xid_t xid)
{
	gfarm_id_free(async_server->idtab, xid);
}

/*
 * put RPC request
 */
gfarm_error_t
gfp_xdr_vrpc_raw_request_begin(struct gfp_xdr *conn,
	struct gfp_xdr_xid_record **xidrp, int *size_posp,
	gfarm_int32_t command,
	const char **formatp, va_list *app)
{
	gfarm_error_t e;
	struct gfp_xdr_async_server *async_server = gfp_xdr_async(conn);
	struct gfp_xdr_xid_record *xidr;
	int size_pos;
	gfp_xdr_xid_t xid;

	assert(async_server != NULL);
	xidr = gfarm_id_alloc(async_server->idtab, &xid);
	if (xidr == NULL) {
		return (GFARM_ERR_NO_MEMORY);
	}
	xidr->xid = xid;

	e = gfp_xdr_vrpc_send_request_begin(conn, xid, &size_pos,
	    command, formatp, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_client_request_free(async_server, xid);
		gflog_debug(GFARM_MSG_1001009,
		    "sending command (%d) failed: %s",
		    command, gfarm_error_string(e));
		return (e);
	}

	*xidrp = xidr;
	*size_posp = size_pos;
	return (e);
}

gfarm_error_t
gfp_xdr_rpc_raw_request_end(struct gfp_xdr *conn,
	struct gfp_xdr_xid_record *xidr, int size_pos)
{
	gfp_xdr_rpc_send_end(conn, size_pos);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_vrpc_raw_request(struct gfp_xdr *conn,
	struct gfp_xdr_xid_record **xidrp, gfarm_int32_t command,
	const char **formatp, va_list *app)
{
	int size_pos;
	gfarm_error_t e = gfp_xdr_vrpc_raw_request_begin(conn,
	    xidrp, &size_pos, command, formatp, app);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_rpc_raw_request_end(conn, *xidrp, size_pos);
	return (e);
}

/*
 * get RPC result
 */
gfarm_error_t
gfp_xdr_vrpc_raw_result_begin(
	struct gfp_xdr *conn, int just, int do_timeout,
	struct gfp_xdr_xid_record *xidr, size_t *sizep,
	gfarm_int32_t *errcodep, const char **formatp, va_list *app)
{
	gfarm_error_t e;
	int eof;
	struct gfp_xdr_async_server *async_server = gfp_xdr_async(conn);
	enum gfp_xdr_msg_type type;
	gfp_xdr_xid_t xid;
	size_t size;

	assert(xidr->xid != -1);

	e = gfp_xdr_recv_async_header(conn, just, do_timeout,
	    &type, &xid, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "client RPC result header: %s", gfarm_error_string(e));
		gfp_xdr_client_request_free(async_server, xid);
		return (e);
	}
	/*
	 * XXX:
	 * if multithreaded, xid mismatch => should yield CPU to the xid holder
	 */
	if (type != GFP_XDR_TYPE_RESULT || xid != xidr->xid) {
		/* XXX should be gflog_debug(), but for DEBUGGING */
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "conn %p: "
		    "client rpc type:%u / xid:%u, but expected:%d - mismatch",
		    conn, (int)type, (int)xid, (int)xidr->xid);
		gfp_xdr_client_request_free(async_server, xid);
		return (GFARM_ERR_PROTOCOL);
	}
	gfp_xdr_client_request_free(async_server, xid);

	/*
	 * receive response.
	 * always do timeout here, because error code is already received
	 */
	if ((e = gfp_xdr_recv_sized(conn, just, 1, &size, &eof, "i", errcodep))
	    != GFARM_ERR_NO_ERROR) {
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
	if (*errcodep != GFARM_ERR_NO_ERROR) { /* no result argument */
		*sizep = size;
		return (GFARM_ERR_NO_ERROR);
	}

	/* always do timeout here, because error code is already received */
	e = gfp_xdr_vrecv_sized_x(conn, just, 1, &size, &eof, formatp, app);
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
		gflog_debug(GFARM_MSG_UNFIXED,
		    "invalid format character: %c(%x)", **formatp, **formatp);
		return (GFARM_ERRMSG_GFP_XDR_VRPC_INVALID_FORMAT_CHARACTER);
	}
	*sizep = size;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_xdr_rpc_raw_result_begin(
	struct gfp_xdr *conn, int just, int do_timeout,
	struct gfp_xdr_xid_record *xidr, size_t *sizep,
	gfarm_int32_t *errcodep, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrpc_raw_result_begin(conn, just, do_timeout,
	    xidr, sizep, errcodep, &format, &ap);
	va_end(ap);
	return (e);
}

static gfarm_error_t
gfp_xdr_rpc_raw_result_end_common(
	struct gfp_xdr *conn, int just,
	struct gfp_xdr_xid_record *xidr,
	size_t size, const char *diag, int warn)
{
	gfarm_error_t e;

	/*
	 * NOTE: currently xidr is not used. if you change this,
	 * gfp_xdr_rpc_result_end() should be modified.
	 */

	if (size == 0)
		return (GFARM_ERR_NO_ERROR);

	if (warn)
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s: client rpc residual:%u (%x)",
		    diag, (int)size, (int)size);
	if ((e = gfp_xdr_purge(conn, just, size)) != GFARM_ERR_NO_ERROR)
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: client rpc result: skipping: %s",
		    diag, gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfp_xdr_rpc_raw_result_end(
	struct gfp_xdr *conn, int just,
	struct gfp_xdr_xid_record *xidr,
	size_t size)
{
	static const char diag[] = "gfp_xdr_rpc_raw_result_end";

	return (gfp_xdr_rpc_raw_result_end_common(
	    conn, just, xidr, size, diag, 1));
}

static gfarm_error_t
gfp_xdr_rpc_raw_result_skip(
	struct gfp_xdr *conn, int just, int do_timeout,
	struct gfp_xdr_xid_record *xidr)
{
	gfarm_error_t e;
	gfarm_int32_t errcode;
	size_t size;
#ifdef RPC_DEBUG
	gfp_xdr_xid_t xid = xidr->xid;
#endif
	static const char diag[] = "gfp_xdr_rpc_raw_result_skip";

	e = gfp_xdr_rpc_raw_result_begin(conn, just, do_timeout,
	    xidr, &size, &errcode, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
#ifdef RPC_DEBUG
	/* for debugging */
	gflog_info(GFARM_MSG_UNFIXED,
	    "gfp_xdr_rpc_raw_result_skip: xid %d: skipped: %s",
	    (int)xid, gfarm_error_string(errcode));
#endif
	e = gfp_xdr_rpc_raw_result_end_common(conn, just, xidr, size, diag, 0);
	return (e);
}

gfarm_error_t
gfp_xdr_vrpc_raw_result(
	struct gfp_xdr *conn, int just, int do_timeout,
	struct gfp_xdr_xid_record *xidr, gfarm_int32_t *errcodep,
	const char **formatp, va_list *app)
{
	size_t size;
	gfarm_error_t e = gfp_xdr_vrpc_raw_result_begin(conn, just, do_timeout,
	    xidr, &size, errcodep, formatp, app);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfp_xdr_rpc_raw_result_end(conn, just, xidr, size);
	return (e);
}

/*
 * do RPC with "request-args/result-args" format string.
 */
gfarm_error_t
gfp_xdr_vrpc(struct gfp_xdr *conn, int just, int do_timeout,
	gfarm_int32_t command, gfarm_int32_t *errcodep,
	const char **formatp, va_list *app)
{
	gfarm_error_t e;
	struct gfp_xdr_xid_record *xidr;

	/*
	 * send request
	 */
	e = gfp_xdr_vrpc_raw_request(conn, &xidr, command, formatp, app);
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfp_xdr_flush(conn);
	} else {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfp_xdr_vrpc(%d) requestfailed: %s",
		    (int)command, gfarm_error_string(e));
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

	return (gfp_xdr_vrpc_raw_result(conn, just, do_timeout,
	    xidr, errcodep, formatp, app));
}

/*
 * functions which use gfp_xdr_context
 */

gfarm_error_t
gfp_xdr_context_alloc(struct gfp_xdr *conn, struct gfp_xdr_context **ctxp)
{
	struct gfp_xdr_context *ctx;

	/* `conn' is not used for now */
	GFARM_MALLOC(ctx);
	if (ctx == NULL)
		return (GFARM_ERR_NO_MEMORY);

	GFARM_HCIRCLEQ_INIT(ctx->list, next_xid);
	ctx->list.xid = -1;

	*ctxp = ctx;
	return (GFARM_ERR_NO_ERROR);
}

void
gfp_xdr_context_free(struct gfp_xdr *conn, struct gfp_xdr_context *ctx)
{
	gfarm_error_t e;
	struct gfp_xdr_xid_record *entry, *tmp;

	GFARM_HCIRCLEQ_FOREACH_SAFE(entry, ctx->list, next_xid, tmp) {
		e = gfp_xdr_rpc_raw_result_skip(conn, 0, 1, entry);
		(void)e; /* communication error will be handled at next RPC */
	}
}

void
gfp_xdr_context_free_until(struct gfp_xdr *conn,
	struct gfp_xdr_context *ctx, struct gfp_xdr_xid_record *xidr)
{
	gfarm_error_t e;
	struct gfp_xdr_xid_record *entry, *tmp;

	GFARM_HCIRCLEQ_FOREACH_SAFE(entry, ctx->list, next_xid, tmp) {
		if (entry == xidr)
			break;
		GFARM_HCIRCLEQ_REMOVE(entry, next_xid);
		e = gfp_xdr_rpc_raw_result_skip(conn, 0, 1, entry);
		(void)e; /* communication error will be handled at next RPC */
	}
}

struct gfp_xdr_xid_record *
gfp_xdr_context_get_pos(struct gfp_xdr *conn, struct gfp_xdr_context *ctx)
{
	if (GFARM_HCIRCLEQ_EMPTY(ctx->list, next_xid))
		return (NULL);
	else
		return (GFARM_HCIRCLEQ_LAST(ctx->list, next_xid));
}

#ifdef GFM_PROTO_OPEN_DEBUG
gfarm_uint32_t
gfp_xdr_client_first_xid(struct gfp_xdr_context *ctx)
{
	return (GFARM_HCIRCLEQ_FIRST(ctx->list, next_xid)->xid);
}

gfarm_uint32_t
gfp_xdr_client_last_xid(struct gfp_xdr_context *ctx)
{
	return (GFARM_HCIRCLEQ_LAST(ctx->list, next_xid)->xid);
}
#endif

gfarm_error_t
gfp_xdr_vrpc_request_begin(struct gfp_xdr *conn,
	struct gfp_xdr_context *ctx, int *size_posp,
	gfarm_int32_t command, const char **formatp, va_list *app)
{
	struct gfp_xdr_xid_record *xidr;
	gfarm_error_t e;

	e = gfp_xdr_vrpc_raw_request_begin(conn,
	    &xidr, size_posp, command, formatp, app);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_HCIRCLEQ_INSERT_TAIL(ctx->list, xidr, next_xid);
	return (e);
}

gfarm_error_t
gfp_xdr_rpc_request_end(struct gfp_xdr *conn,
	struct gfp_xdr_context *ctx, int size_pos)
{
	return (gfp_xdr_rpc_raw_request_end(conn,
	    GFARM_HCIRCLEQ_LAST(ctx->list, next_xid), size_pos));
}

gfarm_error_t
gfp_xdr_vrpc_result_begin(
	struct gfp_xdr *conn, int just, int do_timeout,
	struct gfp_xdr_context *ctx, size_t *sizep,
	gfarm_int32_t *errcodep, const char **formatp, va_list *app)
{
	struct gfp_xdr_xid_record *xidr;

	xidr = GFARM_HCIRCLEQ_FIRST(ctx->list, next_xid);
	/* remove xidr, regardless of the RPC result */
	GFARM_HCIRCLEQ_REMOVE_HEAD(ctx->list, next_xid);

	return (gfp_xdr_vrpc_raw_result_begin(conn, just, do_timeout,
	    xidr, sizep, errcodep, formatp, app));
}

gfarm_error_t
gfp_xdr_rpc_result_end(struct gfp_xdr *conn, int just,
	struct gfp_xdr_context *ctx, size_t size)
{
	return (gfp_xdr_rpc_raw_result_end(conn, just,
	    /* currently the xidr parameter is not actually used */ NULL,
	    size));
}

gfarm_error_t
gfp_xdr_vrpc_request(struct gfp_xdr *conn,
	struct gfp_xdr_context *ctx, gfarm_int32_t command,
	const char **formatp, va_list *app)
{
	int size_pos;
	gfarm_error_t e = gfp_xdr_vrpc_request_begin(conn,
	    ctx, &size_pos, command, formatp, app);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_rpc_request_end(conn, ctx, size_pos);
	return (e);
}

gfarm_error_t
gfp_xdr_vrpc_result(
	struct gfp_xdr *conn, int just, int do_timeout,
	struct gfp_xdr_context *ctx,
	gfarm_int32_t *errcodep, const char **formatp, va_list *app)
{
	size_t size;
	gfarm_error_t e = gfp_xdr_vrpc_result_begin(conn, just, do_timeout,
	    ctx, &size, errcodep, formatp, app);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_rpc_result_end(conn, just, ctx, size);
	return (e);
}
