/*
 * iobuffer operation / GFSL / GSS API of Globus GSI or Kerberos
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <gssapi.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h> /* for definition of gfarm_off_t */

#include "gfutil.h"
#include "thrsubr.h"

#include "gfsl_secure_session.h"
#include "gss.h"

#include "context.h"
#include "liberror.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "config.h"

/*
 * for "gsi" or "kerberos" method
 */

struct io_gfsl {
	struct gfarm_gss *gss;
	gfarmSecSession *session;
	gss_cred_id_t cred_to_be_freed; /* cred which will be freed at close */
	char *initiator_dn;

	/* for read */
	char *buffer;
	int p, residual;

	/* for exclusion between gfmd async protocol senders and receivers */
	pthread_mutex_t mutex;
};

static const char mutex_what[] = "io_gfsl::mutex";

/*
 * only blocking i/o is available.
 */

static int
gfarm_iobuffer_read_secsession_x(struct gfarm_iobuffer *b, void *cookie, int fd,
	void *data, int length, int do_timeout)
{
	struct io_gfsl *io = cookie;
	int rv;
	int msec = do_timeout ? gfarm_ctxp->network_receive_timeout * 1000
		: GFARM_GSS_TIMEOUT_INFINITE;
	static const char diag[] = "gfarm_iobuffer_read_secsession_x";

	if (io->buffer == NULL) {
		int flag = fcntl(fd, F_GETFL, NULL);

		/* temporary drop O_NONBLOCK flag to prevent EAGAIN */
		if (flag & O_NONBLOCK)
			fcntl(fd, F_SETFL, flag & ~O_NONBLOCK);

		gfarm_mutex_lock(&io->mutex, diag, mutex_what);
		rv = io->gss->gfarmSecSessionReceiveInt8(io->session,
		    &io->buffer, &io->residual, msec);
		gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

		if (flag & O_NONBLOCK)
			fcntl(fd, F_SETFL, flag);

		if (rv <= 0) {
			/* XXX - interpret io->session->gssLastStat */
			/* XXX - set GFARM_ERR_BROKEN_PIPE to reconnect */
			gfarm_iobuffer_set_error(b, GFARM_ERR_BROKEN_PIPE);
			return (rv);
		}
		io->p = 0;
	}

	if (io->residual <= length) {
		rv = io->residual;
		memcpy(data, &io->buffer[io->p], rv);
		free(io->buffer);
		io->buffer = NULL;
		io->p = io->residual = 0;
	} else {
		rv = length;
		memcpy(data, &io->buffer[io->p], rv);
		io->p += rv; io->residual -= rv;
	}
	return (rv);
}

int
gfarm_iobuffer_read_timeout_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	return (gfarm_iobuffer_read_secsession_x(
	    b, cookie, fd, data, length, 1));
}

int
gfarm_iobuffer_read_notimeout_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	return (gfarm_iobuffer_read_secsession_x(
	    b, cookie, fd, data, length, 0));
}

static int
gfarm_iobuffer_write_secsession_x(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length, int do_timeout)
{
	struct io_gfsl *io = cookie;
	int rv, flag = fcntl(fd, F_GETFL, NULL);
	int msec;
	static const char diag[] = "gfarm_iobuffer_write_secsession_x";

	if (do_timeout && gfarm_ctxp->network_send_timeout != 0)
		msec = gfarm_ctxp->network_send_timeout * 1000;
	else
		msec = GFARM_GSS_TIMEOUT_INFINITE;

	/* temporary drop O_NONBLOCK flag to prevent EAGAIN */
	if (flag & O_NONBLOCK)
		fcntl(fd, F_SETFL, flag & ~O_NONBLOCK);

	gfarm_mutex_lock(&io->mutex, diag, mutex_what);
	rv = io->gss->gfarmSecSessionSendInt8(io->session, data, length, msec);
	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);

	if (flag & O_NONBLOCK)
		fcntl(fd, F_SETFL, flag);

	if (rv <= 0) {
		/* XXX - interpret io->session->gssLastStat */
		/* XXX - set GFARM_ERR_BROKEN_PIPE to reconnect */
		gfarm_iobuffer_set_error(b, GFARM_ERR_BROKEN_PIPE);
	}

	return (rv);
}

int
gfarm_iobuffer_write_timeout_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	return (gfarm_iobuffer_write_secsession_x(
	    b, cookie, fd, data, length, 1));
}

int
gfarm_iobuffer_write_notimeout_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	return (gfarm_iobuffer_write_secsession_x(
	    b, cookie, fd, data, length, 0));
}

static void
free_secsession(struct io_gfsl *io, const char *diag)
{
	OM_uint32 e_major, e_minor;

	/*
	 * just in case. (theoretically this mutex_lock call is unnecessary)
	 * because upper layer must guarantee that other threads do not
	 * access this io at the same time.
	 */
	gfarm_mutex_lock(&io->mutex, diag, mutex_what);

	io->gss->gfarmSecSessionTerminate(io->session);

	if (io->cred_to_be_freed != GSS_C_NO_CREDENTIAL &&
	    io->gss->gfarmGssDeleteCredential(&io->cred_to_be_freed,
	    &e_major, &e_minor) < 0 &&
	    gflog_auth_get_verbose()) {
		gflog_error(GFARM_MSG_1000725,
		    "Can't free my credential because of:");
		io->gss->gfarmGssPrintMajorStatus(e_major);
		io->gss->gfarmGssPrintMinorStatus(e_minor);
	}

	gfarm_mutex_unlock(&io->mutex, diag, mutex_what);
	gfarm_mutex_destroy(&io->mutex, diag, mutex_what);

	free(io->initiator_dn);
	free(io->buffer);
	free(io);
}

/*
 * gfp_xdr operation
 */

gfarm_error_t
gfp_iobuffer_close_secsession_op(void *cookie, int fd)
{
	int rv;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "gfp_iobuffer_close_secsession_op";

	free_secsession(cookie, diag);
	rv = close(fd);
	if (rv == -1)
		e = gfarm_errno_to_error(errno);
	return (e);
}

gfarm_error_t
gfp_iobuffer_shutdown_secsession_op(void *cookie, int fd)
{
	int rv;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	rv = shutdown(fd, SHUT_RDWR);
	if (rv == -1)
		e = gfarm_errno_to_error(errno);
	return (e);
}

int
gfp_iobuffer_recv_is_ready_secssion_op(void *cookie)
{
	/* never holds penidng read in its internal buffer (probably) */
	return (0);
}

struct gfp_iobuffer_ops gfp_xdr_secsession_iobuffer_ops = {
	gfp_iobuffer_close_secsession_op,
	gfp_iobuffer_shutdown_secsession_op,
	gfp_iobuffer_recv_is_ready_secssion_op,
	gfarm_iobuffer_read_timeout_secsession_op,
	gfarm_iobuffer_read_notimeout_secsession_op,
	gfarm_iobuffer_write_timeout_secsession_op,
	gfarm_iobuffer_write_notimeout_secsession_op,
};

gfarm_error_t
gfp_xdr_set_secsession(struct gfp_xdr *conn, struct gfarm_gss *gss,
	gfarmSecSession *secsession, gss_cred_id_t cred_to_be_freed, char *dn)
{
	struct io_gfsl *io;
	static const char diag[] = "gfp_xdr_set_secsession";

	GFARM_MALLOC(io);
	if (io == NULL) {
		gflog_debug(GFARM_MSG_1001480,
			"allocation of 'io_gfsl' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	io->gss = gss;
	io->session = secsession;
	io->cred_to_be_freed = cred_to_be_freed;
	io->initiator_dn = dn;
	io->buffer = NULL;
	io->p = io->residual = 0;
	gfarm_mutex_init(&io->mutex, diag, mutex_what);
	gfp_xdr_set(conn, &gfp_xdr_secsession_iobuffer_ops,
	    io, secsession->fd);
	return (GFARM_ERR_NO_ERROR);
}

/* free resources which were allocated by gfp_xdr_set_secsession() */
void
gfp_xdr_reset_secsession(struct gfp_xdr *conn)
{
	int fd = gfp_xdr_fd(conn);
	struct io_gfsl *io = gfp_xdr_cookie(conn);
	static const char diag[] = "gfp_xdr_reset_secsession";

	if (io != NULL)
		free_secsession(io, diag);

	gfp_xdr_set_socket(conn, fd);
}

char *
gfp_xdr_secsession_initiator_dn(struct gfp_xdr *conn)
{
	struct io_gfsl *io = gfp_xdr_cookie(conn);

	if (io != NULL)
		return (io->initiator_dn);
	return (NULL);
}

/*
 * an option for gfarm_iobuffer_set_write_close()
 */
#if 0 /* currently not used */
void
gfarm_iobuffer_write_close_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd)
{
	struct io_gfsl *io = cookie;
	gfarm_error_t e = gfp_iobuffer_close_secsession_op(io, fd);

	if (e != GFARM_ERR_NO_ERROR && gfarm_iobuffer_get_error(b) == 0)
		gfarm_iobuffer_set_error(b, e);
}
#endif /* currently not used */

/*
 * for "gsi_auth" or "kerberos_auth" method
 */

static struct gfp_iobuffer_ops gfp_xdr_insecure_gsi_session_iobuffer_ops = {
	gfp_iobuffer_close_secsession_op,
	gfp_iobuffer_shutdown_secsession_op,
	gfp_iobuffer_recv_is_ready_secssion_op,
	/* NOTE: the following assumes that these functions don't use cookie */
	gfarm_iobuffer_blocking_read_timeout_fd_op,
	gfarm_iobuffer_blocking_read_notimeout_fd_op,
	gfarm_iobuffer_blocking_write_timeout_socket_op,
	gfarm_iobuffer_blocking_write_notimeout_socket_op,
};

/*
 * downgrade
 * from a "gsi" connection which is created by gfp_xdr_set_secsession()
 * to a "gsi_auth" connection.
 */

void
gfp_xdr_downgrade_to_insecure_session(struct gfp_xdr *conn)
{
	gfp_xdr_set(conn, &gfp_xdr_insecure_gsi_session_iobuffer_ops,
	    gfp_xdr_cookie(conn), gfp_xdr_fd(conn));
}
