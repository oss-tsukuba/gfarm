#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <gssapi.h>

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "gfutil.h"
#include "xxx_proto.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"

#define GFSL_CONF_USERMAP "/etc/grid-security/grid-mapfile"

gss_cred_id_t gfarm_gsi_get_delegated_cred();	/* XXX */

char *
gfarm_gsi_initialize(void)
{
	static int initialized;
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (initialized)
		return (NULL);

	if (geteuid() == 0) { /* XXX - kluge */
		rv = gfarmSecSessionInitializeBoth(NULL, NULL,
		    GFSL_CONF_USERMAP, &e_major, &e_minor);
	} else {
		rv = gfarmSecSessionInitializeInitiator(NULL, &e_major,
		    &e_minor);
	}
	if (rv <= 0) {
		if (gfarm_authentication_verbose) {
			gflog_error(
				"can't initialize as initiator because of:",
				NULL);
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		if (geteuid() == 0) { /* XXX - kluge */
			gfarmSecSessionFinalizeBoth();
		} else {
			gfarmSecSessionFinalizeInitiator();
		}
		return (GFARM_ERR_UNKNOWN); /* XXX */
	}
	initialized = 1;
	return (NULL);
}

char *
gfarm_auth_request_gsi(struct xxx_connection *conn)
{
	int fd = xxx_connection_fd(conn);
	char *e;
	OM_uint32 e_major;
	OM_uint32 e_minor;
	gfarmSecSession *session;
	gfarm_int32_t error; /* enum gfarm_auth_error */
	int eof;

	e = gfarm_gsi_initialize();
	if (e != NULL)
		return (e);

	session = gfarmSecSessionInitiate(fd,
	    gfarm_gsi_get_delegated_cred(), NULL, &e_major, &e_minor);
	if (session == NULL) {
		if (gfarm_authentication_verbose) {
			gflog_error("Can't initiate session because of:",
				    NULL);
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		return (GFARM_ERR_AUTHENTICATION);
	}
	xxx_connection_set_secsession(conn, session);

	e = xxx_proto_recv(conn, 1, &eof, "i", &error);
	if (e != NULL || eof || error != GFARM_AUTH_ERROR_NO_ERROR) {
		gfarmSecSessionTerminate(session);
		xxx_connection_reset_secsession(conn);
		xxx_connection_set_fd(conn, fd);
		return (e != NULL ? e :
		    eof ? GFARM_ERR_PROTOCOL : GFARM_ERR_AUTHENTICATION);
	}
	return (NULL);
}
