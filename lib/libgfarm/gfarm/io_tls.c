#include <gfarm/gfarm_config.h>

#ifdef HAVE_TLS_1_3



#define IN_TLS_CORE
#undef TLS_TEST

#include "tls_headers.h"
#include "tls_funcs.h"



static void
tls_runtime_init_once(void)
{
	tls_runtime_init_once_body();
}

static int 
tls_verify_callback(int ok, X509_STORE_CTX *sctx) {
	return tls_verify_callback_body(ok, sctx);
}

static int
tty_passwd_callback(char *buf, int maxlen, int rwflag, void *u)
{
	return tty_passwd_callback_body(buf, maxlen, rwflag, u);
}



/*
 * Gfarm iobuffer iops
 */

/*
 * close
 */
static gfarm_error_t
tls_iobufop_close(void *cookie, int fd)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	tls_session_ctx_t ctx = (tls_session_ctx_t)cookie;

	if (likely(ctx != NULL)) { 
		ret = tls_session_shutdown(ctx, fd, true);
		if (likely(ret == GFARM_ERR_NO_ERROR)) {
			tls_session_destroy_ctx(ctx);
		}
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

/*
 * shutdown
 */
static gfarm_error_t
tls_iobufop_shutdown(void *cookie, int fd)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	tls_session_ctx_t ctx = (tls_session_ctx_t)cookie;

	if (likely(ctx != NULL)) { 
		ret = tls_session_shutdown(ctx, fd, false);
	} else {
		ret = GFARM_ERR_INVALID_ARGUMENT;
	}

	return (ret);
}

/*
 * read(2) with timeout
 */
static int
tls_iobufop_timeout_read(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	tls_session_ctx_t ctx = (tls_session_ctx_t)cookie;

	if (likely(ctx != NULL && b != NULL && gfarm_ctxp != NULL)) {
		gfarm_error_t gfe = tls_session_timeout_read(ctx, fd, buf, len,
					gfarm_ctxp->network_receive_timeout *
						1000 * 1000,
					&ret);
		if (likely(gfe == GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	return (ret);
}

/*
 * full-blocking read(2)
 */
static int
tls_iobufop_full_blocking_read(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	tls_session_ctx_t ctx = (tls_session_ctx_t)cookie;

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe = tls_session_timeout_read(ctx, fd, buf, len,
					-1, &ret);
		if (likely(gfe == GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	return (ret);
}

/*
 * write(2)
 */
static int
tls_iobufop_write(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	tls_session_ctx_t ctx = (tls_session_ctx_t)cookie;

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe = tls_session_write(ctx, buf, len, &ret);
		if (likely(gfe == GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	return (ret);
}

/*
 * iobuffer ops table
 */
static struct gfp_iobuffer_ops gfp_xdr_tls_iobuf_ops = {
	tls_iobufop_close,
	tls_iobufop_shutdown,
	NULL,
	NULL,
	NULL,
	tls_iobufop_timeout_read,
	tls_iobufop_full_blocking_read,
	tls_iobufop_write
};



/*
 * Gfarm internal APIs
 */

/*
 * An SSL_CTX/SSL constructor
 */
gfarm_error_t
gfp_xdr_tls_alloc(struct gfp_xdr *conn,	int fd,
	int flags, const char *service, const char *name)
{
	gfarm_error_t ret;

	/* just for now */
	(void)service;
	(void)name;

	tls_session_ctx_t ctx = NULL;
	bool do_mutual_auth =
		((flags & GFP_XDR_TLS_CLIENT_AUTHENTICATION) != 0) ?
		true : false;
	tls_role_t role =
		(GFP_XDR_TLS_ROLE_IS_INITIATOR(flags)) ?
		TLS_ROLE_INITIATOR : TLS_ROLE_ACCEPTOR;

	ret = tls_session_create_ctx(&ctx, role, do_mutual_auth);
	if (likely(ret == GFARM_ERR_NO_ERROR && ctx != NULL)) {
		ret = tls_session_establish(ctx, fd);
		if (likely(ret == GFARM_ERR_NO_ERROR)) {
			gfp_xdr_set(conn, &gfp_xdr_tls_iobuf_ops,
				ctx, fd);
		} else {
			tls_session_destroy_ctx(ctx);
		}
	}

	return (ret);
}

/*
 * An SSL destructor
 */
void
gfp_xdr_tls_reset(struct gfp_xdr *conn)
{
	tls_session_ctx_t ctx = gfp_xdr_cookie(conn);

	tls_session_destroy_ctx(ctx);
}

char *
gfp_xdr_tls_initiator_dn_oneline(struct gfp_xdr *conn)
{
	tls_session_ctx_t ctx = gfp_xdr_cookie(conn);

	return (ctx->peer_dn_);
}

char *
gfp_xdr_tls_initiator_dn_rfc2253(struct gfp_xdr *conn)
{
	tls_session_ctx_t ctx = gfp_xdr_cookie(conn);

	return (ctx->peer_dn_);
}



#endif /* HAVE_TLS_1_3 */
