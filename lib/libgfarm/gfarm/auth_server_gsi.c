#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>
#include <sys/types.h>
#include <time.h>
#include <pwd.h>

#include <gssapi.h>

#include <stdio.h>
#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "xxx_proto.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"
#include "gfutil.h"

/*
 * Delegated credential
 */

static gss_cred_id_t delegated_cred = GSS_C_NO_CREDENTIAL;

void
gfarm_gsi_set_delegated_cred(gss_cred_id_t cred)
{
	delegated_cred = cred;
}

gss_cred_id_t
gfarm_gsi_get_delegated_cred()
{
	return (delegated_cred);
}

/*
 *
 */

char *
gfarm_authorize_gsi(struct xxx_connection *conn, int switch_to,
		    char **global_usernamep)
{
	int fd = xxx_connection_fd(conn);
	char *e, *e2, *global_username;
	OM_uint32 e_major;
	gfarmSecSession *session;
	gfarmAuthEntry *userinfo;

	e = xxx_proto_flush(conn);
	if (e != NULL)
		return (e);

	e = gfarm_gsi_initialize();
	if (e != NULL)
		return (e);

	session = gfarmSecSessionAccept(fd, GSS_C_NO_CREDENTIAL, NULL,
	    &e_major);
	if (session == NULL) {
#if 1 /* XXX for debugging */
		fprintf(stderr, "Can't accept session because of:\n");
		gfarmGssPrintStatus(stderr, e_major);
#endif
		return (GFARM_ERR_AUTHENTICATION);
	}
	xxx_connection_set_secsession(conn, session);

	userinfo = gfarmSecSessionGetInitiatorInfo(session);

#if 1 /* XXX - global name should be distinguished by DN (distinguish name) */
	e = gfarm_local_to_global_username(
	    userinfo->authData.userAuth.localName, &global_username);
	if (e != NULL) {
		e2 = xxx_proto_send(conn, "i", GFARM_AUTH_ERROR_DENIED);
		if (e2 == NULL)
			e2 = xxx_proto_flush(conn);
		gfarmSecSessionTerminate(session);
		xxx_connection_reset_secsession(conn);
		xxx_connection_set_fd(conn, fd);
		gflog_error("authorize_gsi: "
		    "cannot map global username into local username",
		    userinfo->authData.userAuth.localName);
		return (e);
	}
#else
	e = gfarm_DN_to_global_username(userinfo->distName, &global_username);
	if (e != NULL) {
		e2 = xxx_proto_send(conn, "i", GFARM_AUTH_ERROR_DENIED);
		if (e2 == NULL)
			e2 = xxx_proto_flush(conn);
		gfarmSecSessionTerminate(session);
		xxx_connection_reset_secsession(conn);
		xxx_connection_set_fd(conn, fd);
		gflog_error(
		    "authorize_gsi: cannot map DN into global username",
		    userinfo->distName);
		return (e != NULL ? e : GFARM_ERR_AUTHENTICATION);
	}
#endif
	e = xxx_proto_send(conn, "i", GFARM_AUTH_ERROR_NO_ERROR);
	if (e == NULL)
		e = xxx_proto_flush(conn);
	if (e != NULL) {
		gfarmSecSessionTerminate(session);
		xxx_connection_set_fd(conn, fd);
		return (e);
	}

	if (switch_to) {
#if 1
		char *syslog_name;

		syslog_name = strdup(userinfo->distName);
		gflog_set_auxiliary_info(syslog_name);
#else
		gflog_set_auxiliary_info(global_username);
#endif
		/*
		 * because the name returned by getlogin() is
		 * an attribute of a session on 4.4BSD derived OSs,
		 * we should create new session before calling
		 * setlogin().
		 */
		setsid();
#ifdef HAVE_SETLOGIN
		setlogin(userinfo->authData.userAuth.localName);
#endif
		initgroups(userinfo->authData.userAuth.localName,
			   userinfo->authData.userAuth.gid);
		gfarmSecSessionDedicate(session);

		gfarm_set_global_username(global_username);
		gfarm_set_local_username(
		    userinfo->authData.userAuth.localName);
		gfarm_set_local_homedir(
		    userinfo->authData.userAuth.homeDir);

		/* set the delegated credential. */
		gfarm_gsi_set_delegated_cred(
		    gfarmSecSessionGetDelegatedCredential(session));
	} else {
		if (global_usernamep == NULL) /* see below */
			free(global_username);
	}

	/*
	 * global_username may continue to be refered,
	 * if (switch_to) as gflog_set_auxiliary_info()
	 * else if (global_usernamep != NULL) as *global_usernamep
	 */
	if (global_usernamep != NULL)
		*global_usernamep = global_username;
	return (NULL);
}
