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
#include "gfutil.h"
#include "xxx_proto.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"

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

static char *
gfarm_authorize_gsi_common(struct xxx_connection *conn,
	int switch_to, char *hostname, char *auth_method_name,
	char **global_usernamep)
{
	int fd = xxx_connection_fd(conn);
	char *e, *e2, *global_username;
	OM_uint32 e_major, e_minor;
	gfarmSecSession *session;
	gfarmAuthEntry *userinfo;
	gfarm_int32_t error = GFARM_AUTH_ERROR_NO_ERROR; /* gfarm_auth_error */

	e = xxx_proto_flush(conn);
	if (e != NULL) {
		gflog_error("authorize_gsi: protocol drain ", e);
		return (e);
	}

	e = gfarm_gsi_initialize();
	if (e != NULL) {
		gflog_error("authorize_gsi: GSI initialize", e);
		return (e);
	}

	session = gfarmSecSessionAccept(fd, GSS_C_NO_CREDENTIAL, NULL,
	    &e_major, &e_minor);
	if (session == NULL) {
		if (gfarm_authentication_verbose) {
			gflog_error("Can't accept session because of:", NULL);
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
			gflog_error("GSI authentication error", hostname);
		}
		return (GFARM_ERR_AUTHENTICATION);
		
	}
	userinfo = gfarmSecSessionGetInitiatorInfo(session);
#if 1 /* XXX - global name should be distinguished by DN (distinguish name) */
	e = gfarm_local_to_global_username(
	    userinfo->authData.userAuth.localName, &global_username);
	if (e != NULL) {
		global_username = NULL;
		error = GFARM_AUTH_ERROR_DENIED;
		gflog_error("authorize_gsi: "
		    "cannot map global username into local username",
		    userinfo->authData.userAuth.localName);
	}
#else
	/*
	 * We DO need GSI authentication to access user database in this case,
	 * otherwise GSI authentcation depends on weak protocol.
	 */
	e = gfarm_DN_to_global_username(userinfo->distName, &global_username);
	if (e != NULL) {
		global_username = NULL;
		error = GFARM_AUTH_ERROR_DENIED;
		gflog_error("authorize_gsi: "
		    "cannot map DN into global username",
		    userinfo->distName);
	}
#endif
	if (e == NULL) { /* assert(error == GFARM_AUTH_ERROR_NO_ERROR); */

		/* succeed, do logging */

		static char method_prefix[] = "auth=";
		static char user_prefix[] = " local_user=";
		static char dnb[] = " DN=\"";
		static char dne[] = "\"";
		char *aux = malloc(strlen(global_username) + 1 +
		    strlen(hostname) + 1);
		char *msg = malloc(sizeof(method_prefix) - 1 +
		    strlen(auth_method_name) + sizeof(user_prefix) - 1 +
		    strlen(userinfo->authData.userAuth.localName) +
		    sizeof(dnb)-1 + strlen(userinfo->distName) + sizeof(dne));

		if (aux == NULL || msg == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			error = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
			if (aux != NULL)
				free(aux);
			if (msg != NULL)
				free(msg);
			gflog_error("authorize_gsi", e);
		} else {
			sprintf(aux, "%s@%s", global_username, hostname);
			gflog_set_auxiliary_info(aux);

			sprintf(msg, "%s%s%s%s%s%s%s",
			    method_prefix, auth_method_name,
			    user_prefix, userinfo->authData.userAuth.localName,
			    dnb, userinfo->distName, dne);
			gflog_notice("authenticated", msg);
			free(msg);

			if (!switch_to) {
				gflog_set_auxiliary_info(NULL);
				free(aux);
			}
		}
	}

	xxx_connection_set_secsession(conn, session);
	e2 = xxx_proto_send(conn, "i", error);
	if (e2 != NULL) {
		gflog_error("authorize_gsi: send reply", e2);
	} else if ((e2 = xxx_proto_flush(conn)) != NULL) {
		gflog_error("authorize_gsi: completion", e2);
	}

	if (e != NULL || e2 != NULL) {
		if (global_username != NULL)
			free(global_username);
		xxx_connection_reset_secsession(conn);
		xxx_connection_set_fd(conn, fd);
		return (e);
	}

	if (switch_to) {
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
	}

	if (global_usernamep != NULL)
		*global_usernamep = global_username;
	else
		free(global_username);
	return (NULL);
}

/*
 * "gsi" method
 */
char *
gfarm_authorize_gsi(struct xxx_connection *conn, int switch_to, char *hostname,
	char **global_usernamep)
{
	return (gfarm_authorize_gsi_common(conn, switch_to, hostname, "gsi",
	    global_usernamep));
}

/*
 * "gsi_auth" method
 */

char *
gfarm_authorize_gsi_auth(struct xxx_connection *conn,
	int switch_to, char *hostname,
	char **global_usernamep)
{
	char *e = gfarm_authorize_gsi_common(conn, switch_to, hostname,
	    "gsi_auth", global_usernamep);
	int fd = xxx_connection_fd(conn);

	if (e == NULL) {
		xxx_connection_reset_secsession(conn);
		xxx_connection_set_fd(conn, fd);
	}
	return (e);
}
