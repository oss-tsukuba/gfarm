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
#include "gfarm_secure_session.h"

#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "iobuffer.h"
#include "xxx_proto.h"
#include "io_gfsl.h"

struct io_gfsl {
	gfarmSecSession *session;
	gfarmExportedCredential *exported_credential;

	/* for read */
	char *buffer;
	int p, residual;
};

/*
 * only blocking i/o is available.
 */

int
gfarm_iobuffer_read_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	struct io_gfsl *io = cookie;
	int rv;

	if (io->buffer == NULL) {
		int flag = fcntl(fd, F_GETFL, NULL);

		/* temporary drop O_NONBLOCK flag to prevent EAGAIN */
		if (flag & O_NONBLOCK)
			fcntl(fd, F_SETFL, flag & ~O_NONBLOCK);

		rv = gfarmSecSessionReceiveBytes(io->session,
		    &io->buffer, &io->residual);

		if (flag & O_NONBLOCK)
			fcntl(fd, F_SETFL, flag);

		if (rv <= 0) {
			/* XXX - interpret io->session->gssLastStat */
			gfarm_iobuffer_set_error(b, GFARM_ERR_UNKNOWN);
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
gfarm_iobuffer_write_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	struct io_gfsl *io = cookie;
	int rv, flag = fcntl(fd, F_GETFL, NULL);

	/* temporary drop O_NONBLOCK flag to prevent EAGAIN */
	if (flag & O_NONBLOCK)
		fcntl(fd, F_SETFL, flag & ~O_NONBLOCK);

	rv = gfarmSecSessionSendBytes(io->session, data, length);

	if (flag & O_NONBLOCK)
		fcntl(fd, F_SETFL, flag);

	if (rv <= 0) {
		/* XXX - interpret io->session->gssLastStat */
		gfarm_iobuffer_set_error(b, GFARM_ERR_UNKNOWN);
	}

	return (rv);
}

static void
free_secsession(struct io_gfsl *io)
{
	gfarmSecSessionTerminate(io->session);
	if (io->buffer != NULL)
		free(io->buffer);
	free(io);
}

/*
 * xxx_connection operation
 */

char *
xxx_iobuffer_close_secsession_op(void *cookie, int fd)
{
	int rv;
	char *e = NULL;

	free_secsession(cookie);
	rv = close(fd);
	if (rv == -1)
		e = gfarm_errno_to_error(errno);
	return (e);
}

char *
xxx_iobuffer_export_credential_secsession_op(void *cookie)
{
	struct io_gfsl *io = cookie;
	OM_uint32 e_major;
	gss_cred_id_t cred;
       
	cred = gfarmSecSessionGetDelegatedCredential(io->session);
	if (cred == GSS_C_NO_CREDENTIAL)
		return ("GSI delegated credential doesn't exist");
	io->exported_credential = gfarmGssExportCredential(cred, &e_major);
	if (io->exported_credential == NULL)
		return ("cannot export GSI delegated credential");
	return (NULL);
}

char *
xxx_iobuffer_delete_credential_secsession_op(void *cookie)
{
	struct io_gfsl *io = cookie;

	if (io->exported_credential == NULL)
		return (NULL);
	gfarmGssDeleteExportedCredential(io->exported_credential);
	io->exported_credential = NULL;
	return (NULL);
}

char *
xxx_iobuffer_env_for_credential_secsession_op(void *cookie)
{
	struct io_gfsl *io = cookie;

	if (io->exported_credential == NULL)
		return (NULL);
	return (gfarmGssEnvForExportedCredential(io->exported_credential));
}

struct xxx_iobuffer_ops xxx_secsession_iobuffer_ops = {
	xxx_iobuffer_close_secsession_op,
	xxx_iobuffer_export_credential_secsession_op,
	xxx_iobuffer_delete_credential_secsession_op,
	xxx_iobuffer_env_for_credential_secsession_op,
	gfarm_iobuffer_read_secsession_op,
	gfarm_iobuffer_write_secsession_op,
	gfarm_iobuffer_read_secsession_op,
	gfarm_iobuffer_write_secsession_op
};

char *
xxx_connection_set_secsession(struct xxx_connection *conn,
	gfarmSecSession *secsession)
{
	struct io_gfsl *io = malloc(sizeof(struct io_gfsl));

	if (io == NULL)
		return (GFARM_ERR_NO_MEMORY);
	io->session = secsession;
	io->exported_credential = NULL;
	io->buffer = NULL;
	io->p = io->residual = 0;
	xxx_connection_set(conn, &xxx_secsession_iobuffer_ops,
	    io, secsession->fd);
	return (NULL);
}

/* free resources which were allocated by xxx_connection_set_secsession() */
void
xxx_connection_reset_secsession(struct xxx_connection *conn)
{
	struct io_gfsl *io = xxx_connection_cookie(conn);

	if (io != NULL)
		free_secsession(io);
	xxx_connection_set(conn, &xxx_secsession_iobuffer_ops, NULL, -1);
}

/*
 * an option for gfarm_iobuffer_set_write_close()
 */
void
gfarm_iobuffer_write_close_secsession_op(struct gfarm_iobuffer *b,
	void *cookie, int fd)
{
	struct io_gfsl *io = cookie;
	char *e = xxx_iobuffer_close_secsession_op(io, fd);

	if (e != NULL && gfarm_iobuffer_get_error(b) == 0)
		gfarm_iobuffer_set_error(b, e);
}
