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

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include "liberror.h"
#include "gfutil.h"
#include "gfp_xdr.h"
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

static gfarm_error_t
gfarm_authorize_gsi_common(struct gfp_xdr *conn,
	int switch_to, char *hostname, char *auth_method_name,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	int fd = gfp_xdr_fd(conn);
	gfarm_error_t e, e2;
	char *global_username;
	OM_uint32 e_major, e_minor;
	gfarmSecSession *session;
	gfarmAuthEntry *userinfo;
	gfarm_int32_t error = GFARM_AUTH_ERROR_NO_ERROR; /* gfarm_auth_error */

	e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error("authorize_gsi: protocol drain ",
		    gfarm_error_string(e));
		return (e);
	}

	e = gfarm_gsi_server_initialize();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error("authorize_gsi: GSI initialize",
		    gfarm_error_string(e));
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
	/* XXX NOTYET determine *peer_typep == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	userinfo = gfarmSecSessionGetInitiatorInfo(session);
#if 0 /* XXX - global name should be distinguished by DN (distinguish name) */
	e = gfarm_local_to_global_username(
	    userinfo->authData.userAuth.localName, &global_username);
	if (e != GFARM_ERR_NO_ERROR) {
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
	 *
	 * XXX - local_user_map file is used for mapping from DN to a
	 *       global user name, instead of accessing to a meta db.
	 */
	e = gfarm_local_to_global_username(
		userinfo->distName, &global_username);
	if (e == GFARM_ERR_NO_ERROR &&
	    strcmp(userinfo->distName, global_username) == 0) {
		free(global_username);
		e = gfarm_local_to_global_username(
			userinfo->authData.userAuth.localName,
			&global_username);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		global_username = NULL;
		error = GFARM_AUTH_ERROR_DENIED;
		gflog_error("authorize_gsi: "
		    "cannot map DN into global username",
		    userinfo->distName);
	}
#endif
	if (e == GFARM_ERR_NO_ERROR) {
		/* assert(error == GFARM_AUTH_ERROR_NO_ERROR); */

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
			gflog_error("authorize_gsi", gfarm_error_string(e));
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

	gfp_xdr_set_secsession(conn, session);
	e2 = gfp_xdr_send(conn, "i", error);
	if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_error("authorize_gsi: send reply",
		    gfarm_error_string(e2));
	} else if ((e2 = gfp_xdr_flush(conn)) != GFARM_ERR_NO_ERROR) {
		gflog_error("authorize_gsi: completion",
		    gfarm_error_string(e2));
	}

	if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR) {
		if (global_username != NULL)
			free(global_username);
		gfp_xdr_reset_secsession(conn);
		gfp_xdr_set_fd(conn, fd);
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

	/* XXX NOTYET determine *peer_typep == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	if (peer_typep != NULL)
		*peer_typep = GFARM_AUTH_ID_TYPE_USER;
	if (global_usernamep != NULL)
		*global_usernamep = global_username;
	else
		free(global_username);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * "gsi" method
 */
gfarm_error_t
gfarm_authorize_gsi(struct gfp_xdr *conn, int switch_to, char *hostname,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	return (gfarm_authorize_gsi_common(conn, switch_to, hostname, "gsi",
	    peer_typep, global_usernamep));
}

/*
 * "gsi_auth" method
 */

gfarm_error_t
gfarm_authorize_gsi_auth(struct gfp_xdr *conn,
	int switch_to, char *hostname,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e = gfarm_authorize_gsi_common(conn, switch_to, hostname,
	    "gsi_auth", peer_typep, global_usernamep);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}
