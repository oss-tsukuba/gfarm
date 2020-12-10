#include <gfarm/gfarm_config.h>

#ifdef HAVE_TLS_1_3



#define IN_TLS_CORE
#undef TLS_TEST

#include "tls_headers.h"
#include "tls_funcs.h"


	
static void
tls_runtime_init_once(void)
{
	/*
	 * XXX FIXME:
	 *	Are option flags sufficient enough or too much?
	 *	I'm not sure about it, hope it would be a OK.
	 */
	if (likely(OPENSSL_init_ssl(
			OPENSSL_INIT_LOAD_SSL_STRINGS |
			OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
			OPENSSL_INIT_NO_ADD_ALL_CIPHERS |
			OPENSSL_INIT_NO_ADD_ALL_DIGESTS |
			OPENSSL_INIT_ENGINE_ALL_BUILTIN,
			NULL) == 1)) {
		is_tls_runtime_initd = true;
	}
}

static int
tty_passwd_callback(char *buf, int maxlen, int rwflag, void *u)
{
	int ret = 0;
	tls_passwd_cb_arg_t arg = (tls_passwd_cb_arg_t)u;

	(void)rwflag;

	if (likely(arg != NULL)) {
		char p[4096];
		bool has_passwd_cache = is_valid_string(arg->pw_buf_);
		bool do_passwd =
			(has_passwd_cache == false &&
			 arg->pw_buf_ != NULL &&
			 arg->pw_buf_maxlen_ > 0) ?
			true : false;

		if (unlikely(do_passwd == true)) {
			/*
			 * Set a prompt
			 */
			if (is_valid_string(arg->filename_) == true) {
				(void)snprintf(p, sizeof(p),
					"Passphrase for \"%s\": ",
					arg->filename_);
			} else {
				(void)snprintf(p, sizeof(p),
					"Passphrase: ");
			}
		}
		
		tty_lock();
		{
			if (unlikely(do_passwd == true)) {
				if (tty_get_passwd(arg->pw_buf_,
					arg->pw_buf_maxlen_, p, &ret) ==
					GFARM_ERR_NO_ERROR) {
					goto copy_cache;
				}
			} else if (likely(has_passwd_cache == true)) {
			copy_cache:
				ret = snprintf(buf, maxlen, "%s",
						arg->pw_buf_);
			}
		}
		tty_unlock();
	}

	return (ret);
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
			tls_session_ctx_destroy(ctx);
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

	ret = tls_session_ctx_create(&ctx, role, do_mutual_auth);
	if (likely(ret == GFARM_ERR_NO_ERROR && ctx != NULL)) {
		ret = tls_session_establish(ctx, fd);
		if (likely(ret == GFARM_ERR_NO_ERROR)) {
			gfp_xdr_set(conn, &gfp_xdr_tls_iobuf_ops,
				ctx, fd);
		} else {
			tls_session_ctx_destroy(ctx);
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

	tls_session_ctx_destroy(ctx);
}

char *
gfp_xdr_tls_initiator_dn(struct gfp_xdr *conn)
{
	tls_session_ctx_t ctx = gfp_xdr_cookie(conn);

	return (ctx->peer_dn_);
}



#endif /* HAVE_TLS_1_3 */
