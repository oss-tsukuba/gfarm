/*
 * iobuffer operation: GSI communication: GFSL / Globus GSS API / OpenSSL
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <gssapi.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h> /* for definition of gfarm_off_t */

#include "gfutil.h"

#include "gfarm_secure_session.h"

#include "context.h"
#include "liberror.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "config.h"

/*
 * for "gsi" method
 */

struct io_gfsl {
	gfarmSecSession *session;
	gss_cred_id_t cred_to_be_freed; /* cred which will be freed at close */
	gfarmExportedCredential *exported_credential;

	/* for read */
	char *buffer;
	int p, residual;
};

/*
 * only blocking i/o is available.
 */

static int
gfarm_iobuffer_read_session_x(struct gfarm_iobuffer *b, void *cookie, int fd,
	void *data, int length, int do_timeout)
{
	struct io_gfsl *io = cookie;
	int rv;
	int msec = do_timeout ? gfarm_ctxp->network_receive_timeout * 1000
		: GFARM_GSS_TIMEOUT_INFINITE;

	if (io->buffer == NULL) {
		int flag = fcntl(fd, F_GETFL, NULL);

		/* temporary drop O_NONBLOCK flag to prevent EAGAIN */
		if (flag & O_NONBLOCK)
			fcntl(fd, F_SETFL, flag & ~O_NONBLOCK);

		rv = gfarmSecSessionReceiveInt8(io->session,
		    &io->buffer, &io->residual, msec);

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
	return (gfarm_iobuffer_read_session_x(b, cookie, fd, data, length, 1));
}

int
gfarm_iobuffer_read_notimeout_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	return (gfarm_iobuffer_read_session_x(b, cookie, fd, data, length, 0));
}

int
gfarm_iobuffer_write_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	struct io_gfsl *io = cookie;
	int rv, flag = fcntl(fd, F_GETFL, NULL);

	/* temporary drop O_NONBLOCK flag to prevent EAGAIN */
	if (flag & O_NONBLOCK)
		fcntl(fd, F_SETFL, flag & ~O_NONBLOCK);

	rv = gfarmSecSessionSendInt8(io->session, data, length);

	if (flag & O_NONBLOCK)
		fcntl(fd, F_SETFL, flag);

	if (rv <= 0) {
		/* XXX - interpret io->session->gssLastStat */
		/* XXX - set GFARM_ERR_BROKEN_PIPE to reconnect */
		gfarm_iobuffer_set_error(b, GFARM_ERR_BROKEN_PIPE);
	}

	return (rv);
}

static void
free_secsession(struct io_gfsl *io)
{
	OM_uint32 e_major, e_minor;

	gfarmSecSessionTerminate(io->session);

	if (io->cred_to_be_freed != GSS_C_NO_CREDENTIAL &&
	    gfarmGssDeleteCredential(&io->cred_to_be_freed,
	    &e_major, &e_minor) < 0 &&
	    gflog_auth_get_verbose()) {
		gflog_error(GFARM_MSG_1000725,
		    "Can't free my credential because of:");
		gfarmGssPrintMajorStatus(e_major);
		gfarmGssPrintMinorStatus(e_minor);
	}

	if (io->buffer != NULL)
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

	free_secsession(cookie);
	rv = close(fd);
	if (rv == -1)
		e = gfarm_errno_to_error(errno);
	return (e);
}

gfarm_error_t
gfp_iobuffer_export_credential_secsession_op(void *cookie)
{
	struct io_gfsl *io = cookie;
	OM_uint32 e_major;
	gss_cred_id_t cred;

	cred = gfarmSecSessionGetDelegatedCredential(io->session);
	if (cred == GSS_C_NO_CREDENTIAL)
		return (GFARM_ERRMSG_GSI_DELEGATED_CREDENTIAL_NOT_EXIST);
	io->exported_credential = gfarmGssExportCredential(cred, &e_major);
	if (io->exported_credential == NULL)
		return (GFARM_ERRMSG_GSI_DELEGATED_CREDENTIAL_CANNOT_EXPORT);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_iobuffer_delete_credential_secsession_op(void *cookie, int sighandler)
{
	struct io_gfsl *io = cookie;

	if (io->exported_credential == NULL)
		return (GFARM_ERR_NO_ERROR);
	gfarmGssDeleteExportedCredential(io->exported_credential, sighandler);
	io->exported_credential = NULL;
	return (GFARM_ERR_NO_ERROR);
}

char *
gfp_iobuffer_env_for_credential_secsession_op(void *cookie)
{
	struct io_gfsl *io = cookie;

	if (io->exported_credential == NULL)
		return (NULL);
	return (gfarmGssEnvForExportedCredential(io->exported_credential));
}

struct gfp_iobuffer_ops gfp_xdr_secsession_iobuffer_ops = {
	gfp_iobuffer_close_secsession_op,
	gfp_iobuffer_export_credential_secsession_op,
	gfp_iobuffer_delete_credential_secsession_op,
	gfp_iobuffer_env_for_credential_secsession_op,
	gfarm_iobuffer_read_timeout_secsession_op,
	gfarm_iobuffer_read_notimeout_secsession_op,
	gfarm_iobuffer_write_secsession_op
};

gfarm_error_t
gfp_xdr_set_secsession(struct gfp_xdr *conn,
	gfarmSecSession *secsession, gss_cred_id_t cred_to_be_freed)
{
	struct io_gfsl *io;

	GFARM_MALLOC(io);
	if (io == NULL) {
		gflog_debug(GFARM_MSG_1001480,
			"allocation of 'io_gfsl' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	io->session = secsession;
	io->cred_to_be_freed = cred_to_be_freed;
	io->exported_credential = NULL;
	io->buffer = NULL;
	io->p = io->residual = 0;
	gfp_xdr_set(conn, &gfp_xdr_secsession_iobuffer_ops,
	    io, secsession->fd);
	return (GFARM_ERR_NO_ERROR);
}

/* free resources which were allocated by gfp_xdr_set_secsession() */
void
gfp_xdr_reset_secsession(struct gfp_xdr *conn)
{
	struct io_gfsl *io = gfp_xdr_cookie(conn);

	if (io != NULL)
		free_secsession(io);
	gfp_xdr_set(conn, &gfp_xdr_secsession_iobuffer_ops, NULL, -1);
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
 * for "gsi_auth" method
 */

static struct gfp_iobuffer_ops gfp_xdr_insecure_gsi_session_iobuffer_ops = {
	gfp_iobuffer_close_secsession_op,
	gfp_iobuffer_export_credential_secsession_op,
	gfp_iobuffer_delete_credential_secsession_op,
	gfp_iobuffer_env_for_credential_secsession_op,
	/* NOTE: the following assumes that these functions don't use cookie */
	gfarm_iobuffer_blocking_read_timeout_fd_op,
	gfarm_iobuffer_blocking_read_notimeout_fd_op,
	gfarm_iobuffer_blocking_write_socket_op
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
