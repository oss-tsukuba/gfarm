#include <gfarm/gfarm_config.h>

#include <pthread.h>

#ifdef HAVE_TLS_1_3

#define IN_TLS_CORE
#undef TLS_TEST

#include "tls_headers.h"
#include "tls_instances.h"
#include "tls_funcs.h"
#include "io_fd.h"

#include "thrsubr.h"

#include "config_openssl.h"
#include "io_fd.h" /* for gfp_xdr_set_socket() */

/*
 * Gfarm iobuffer iops
 */

struct gfp_io_tls {
	struct tls_session_ctx_struct *ctx;

	/* for exclusion between gfmd async protocol senders and receivers */
	pthread_mutex_t mutex;
};

static const char mutex_what[] = "gfp_io_tls::mutex";

/* destructor */

static gfarm_error_t
gfp_io_tls_free(struct gfp_io_tls *io, const char *diag)
{
	gfarm_error_t e;
	struct tls_session_ctx_struct *ctx = io->ctx;

	/*
	 * just in case. (theoretically this mutex_lock call is unnecessary)
	 * because upper layer must guarantee that other threads do not
	 * access this io at the same time.
	 */
	gfarm_mutex_lock(&io->mutex, diag, mutex_what);

	e = tls_session_shutdown(ctx);
	tls_session_destroy_ctx(ctx);

	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	gfarm_mutex_destroy(&io->mutex, diag, mutex_what);
	free(io);

	return (e);
}

/*
 * close
 */
static gfarm_error_t
tls_iobufop_close(void *cookie, int fd)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	int st = -1;
	struct gfp_io_tls *io = cookie;
	static const char diag[] = "tls_iobufop_close";

	ret = gfp_io_tls_free(io, diag);

	errno = 0;
	st = close(fd);
	if (st != 0) {
		ret = gfarm_errno_to_error(errno);
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
	int st = -1;
	struct gfp_io_tls *io = cookie;
	struct tls_session_ctx_struct *ctx = io->ctx;
	static const char diag[] = "tls_iobufop_shutdown";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);
	ret = tls_session_shutdown(ctx);
	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	errno = 0;
	st = shutdown(fd, SHUT_RDWR);
	if (st != 0) {
		ret = gfarm_errno_to_error(errno);
	}

	return (ret);
}

static int
tls_iobufop_recv_is_ready(void *cookie)
{
	gfarm_error_t e;
	int ret = -1;
	struct gfp_io_tls *io = cookie;
	struct tls_session_ctx_struct *ctx = io->ctx;
	static const char diag[] = "tls_iobufop_recv_is_ready";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);
	e = tls_session_get_pending_read_bytes_n(ctx, &ret);
	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	return (e == GFARM_ERR_NO_ERROR && ret > 0);
}

/*
 * read(2) with timeout
 */
static int
tls_iobufop_timeout_read(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	struct gfp_io_tls *io = cookie;
	struct tls_session_ctx_struct *ctx = io->ctx;
	static const char diag[] = "tls_iobufop_timeout_read";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe = tls_session_timeout_read(ctx, fd, buf, len,
					gfarm_ctxp->network_receive_timeout *
						1000 * 1000,
					&ret);
		if (unlikely(gfe != GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

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
	struct gfp_io_tls *io = cookie;
	struct tls_session_ctx_struct *ctx = io->ctx;
	static const char diag[] = "tls_iobufop_full_blocking_read";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe = tls_session_timeout_read(ctx, fd, buf, len,
					-1, &ret);
		if (unlikely(gfe != GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	return (ret);
}

/*
 * write(2) with timeout
 */
static int
tls_iobufop_timeout_write(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	struct gfp_io_tls *io = cookie;
	struct tls_session_ctx_struct *ctx = io->ctx;
	static const char diag[] = "tls_iobufop_timeout_write";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe;
		int timeout = gfarm_ctxp->network_send_timeout;

		if (timeout == 0)
			timeout = -1; /* inifinite */
		else
			timeout *= 1000 * 1000;
		gfe = tls_session_timeout_write(ctx, fd, buf, len, timeout,
						&ret);
		if (unlikely(gfe != GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	return (ret);
}

/*
 * full-blocking write(2)
 */
static int
tls_iobufop_full_blocking_write(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *buf, int len)
{
	int ret = -1;
	struct gfp_io_tls *io = cookie;
	struct tls_session_ctx_struct *ctx = io->ctx;
	static const char diag[] = "tls_iobufop_full_blocking_write";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);

	if (likely(ctx != NULL && b != NULL)) {
		gfarm_error_t gfe = tls_session_timeout_write(
					ctx, fd, buf, len, -1, &ret);
		if (unlikely(gfe != GFARM_ERR_NO_ERROR)) {
			gfarm_iobuffer_set_error(b, gfe);
		}
	} else {
		ret = -1;
		if (b != NULL) {
			gfarm_iobuffer_set_error(b,
				GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	return (ret);
}


/*
 * iobuffer ops table
 */
static struct gfp_iobuffer_ops gfp_xdr_tls_iobuf_ops = {
	tls_iobufop_close,
	tls_iobufop_shutdown,
	tls_iobufop_recv_is_ready,
	tls_iobufop_timeout_read,
	tls_iobufop_full_blocking_read,
	tls_iobufop_timeout_write,
	tls_iobufop_full_blocking_write,
};

/*
 * Gfarm internal APIs
 */

static int
gfp_xdr_tls_session_create_ctx(int flags, struct tls_session_ctx_struct **ctxp)
{
	bool do_mutual_auth =
		(flags & GFP_XDR_TLS_CLIENT_AUTHENTICATION) != 0;
	enum tls_role role =
		(GFP_XDR_TLS_ROLE_IS_INITIATOR(flags)) ?
		TLS_ROLE_INITIATOR : TLS_ROLE_ACCEPTOR;
	bool use_proxy_cert =
		flags & GFP_XDR_TLS_CLIENT_USE_PROXY_CERTIFICATE;

	return (tls_session_create_ctx(ctxp,
	    role, do_mutual_auth, use_proxy_cert));
}

/*
 * An SSL_CTX/SSL constructor
 */
gfarm_error_t
gfp_xdr_tls_alloc(struct gfp_xdr *conn,	int fd, int flags)
{
	gfarm_error_t ret;

	struct gfp_io_tls *io;
	struct tls_session_ctx_struct *ctx = NULL;
	static const char diag[] = "gfp_xdr_tls_alloc";

	GFARM_MALLOC(io);
	if (io == NULL) {
		/* It's OK to make all authentcation fail in this case */
		return (GFARM_ERR_NO_MEMORY);
	}
	gfarm_mutex_init(&io->mutex, diag, mutex_what);

	/*
	 * helgrind reports data race in openssl11-libs-1.1.1k on CentOS 7.
	 * e.g.
	 * OPENSSL_LH_retrieve() called from SSL_CTX_new()
	 * for different sessions.
	 *
	 * enabling the following gfarm_openssl_global_{lock,unlock}()
	 * reduces the problem.
	 * XXX but this may be overblocking
	 */
#if 0
	gfarm_openssl_global_lock(diag);
#endif
	ret = gfp_xdr_tls_session_create_ctx(flags, &ctx);
#if 0
	gfarm_openssl_global_unlock(diag);
#endif

	/*
	 * to make TLS authentication graceful,
	 * always call tls_session_establish()
	 * even if ret != GFARM_ERR_NO_ERROR or ctx == NULL.
	 * tls_session_establish() can handle such case.
	 */
	io->ctx = ctx;

	ret = tls_session_establish(ctx, fd, conn, ret);

	if (likely(ret == GFARM_ERR_NO_ERROR)) {
		gfp_xdr_set(conn, &gfp_xdr_tls_iobuf_ops, io, fd);
	} else {
		if (ctx != NULL)
			tls_session_destroy_ctx(ctx);
		gfarm_mutex_destroy(&io->mutex, diag, mutex_what);
		free(io);
	}

	return (ret);
}

/*
 * An SSL destructor
 */
void
gfp_xdr_tls_reset(struct gfp_xdr *conn)
{
	int fd = gfp_xdr_fd(conn);
	struct gfp_io_tls *io = gfp_xdr_cookie(conn);
	static const char diag[] = "gfp_xdr_tls_reset";

	(void)gfp_io_tls_free(io, diag);

	gfp_xdr_set_socket(conn, fd);
}

char *
gfp_xdr_tls_peer_dn_rfc2253(struct gfp_xdr *conn)
{
	struct gfp_io_tls *io = gfp_xdr_cookie(conn);
	struct tls_session_ctx_struct *ctx = io->ctx;
	char *dn;
	const char diag[] = "gfp_xdr_tls_peer_dn_rfc2253";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);
	dn = tls_session_peer_subjectdn_rfc2253(ctx);
	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	return (dn);
}

char *
gfp_xdr_tls_peer_dn_gsi(struct gfp_xdr *conn)
{
	struct gfp_io_tls *io = gfp_xdr_cookie(conn);
	struct tls_session_ctx_struct *ctx = io->ctx;
	char *dn;
	const char diag[] = "gfp_xdr_tls_peer_dn_gsi";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);
	dn = tls_session_peer_subjectdn_gsi(ctx);
	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	return (dn);
}

char *
gfp_xdr_tls_peer_dn_common_name(struct gfp_xdr *conn)
{
	struct gfp_io_tls *io = gfp_xdr_cookie(conn);
	struct tls_session_ctx_struct *ctx = io->ctx;
	char *dn;
	const char diag[] = "gfp_xdr_tls_peer_dn_common_name";

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);
	dn = tls_session_peer_cn(ctx);
	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	return (dn);
}

/*
 * for "sasl_auth" method
 */

static struct gfp_iobuffer_ops gfp_xdr_insecure_tls_session_iobuffer_ops = {
	tls_iobufop_close,
	tls_iobufop_shutdown,
	tls_iobufop_recv_is_ready,
	/* NOTE: the following assumes that these functions don't use cookie */
	gfarm_iobuffer_blocking_read_timeout_fd_op,
	gfarm_iobuffer_blocking_read_notimeout_fd_op,
	gfarm_iobuffer_blocking_write_timeout_socket_op,
	gfarm_iobuffer_blocking_write_notimeout_socket_op,
};

/*
 * downgrade from a TLS connection which is created by gfp_xdr_tls_alloc()
 * to a fd connection.
 */

void
gfp_xdr_downgrade_from_tls_to_fd(struct gfp_xdr *conn)
{
	gfp_xdr_set(conn, &gfp_xdr_insecure_tls_session_iobuffer_ops,
	    gfp_xdr_cookie(conn), gfp_xdr_fd(conn));
}

#endif /* HAVE_TLS_1_3 */
