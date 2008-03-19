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
#include "auth_gsi.h"

#include "gfs_proto.h" /* for GFSD_USERNAME, XXX layering violation */



/*
 * server side authentication
 */

static gfarm_error_t
gfarm_authorize_gsi_common(struct gfp_xdr *conn, int switch_to,
	char *service_tag, char *hostname, char *auth_method_name,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	int fd = gfp_xdr_fd(conn);
	gfarm_error_t e, e2;
	char *global_username, *aux = NULL;
	OM_uint32 e_major, e_minor;
	gfarmSecSession *session;
	gfarmAuthEntry *userinfo;
	gfarm_int32_t error = GFARM_AUTH_ERROR_NO_ERROR; /* gfarm_auth_error */
	enum gfarm_auth_cred_type cred_type =
	    gfarm_auth_server_cred_type_get(service_tag);
	char *cred_service = gfarm_auth_server_cred_service_get(service_tag);
	char *cred_name = gfarm_auth_server_cred_name_get(service_tag);
	gss_cred_id_t cred;

	e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error("authorize_gsi: protocol drain: %s",
		    gfarm_error_string(e));
		return (e);
	}

	e = gfarm_gsi_server_initialize();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error("authorize_gsi: GSI initialize: %s",
		    gfarm_error_string(e));
		return (e);
	}

	if (cred_type == GFARM_AUTH_CRED_TYPE_DEFAULT &&
	    cred_service == NULL && cred_name == NULL) {
		cred = GSS_C_NO_CREDENTIAL;
	} else {
		gss_name_t desired_name = GSS_C_NO_NAME;
		int rv;

		/*
		 * It is desired to try gfarm_host_get_canonical_self_name()
		 * before calling gfarm_host_get_self_name(), but it is not
		 * possible for now, because currently there is no LDAP
		 * connection in server side.
		 * XXX FIXME
		 * This can be done in gfarm_gsi_cred_config_convert_to_name()
		 * with gfarm v2.
		 */
		e = gfarm_gsi_cred_config_convert_to_name(
		    cred_type, cred_service, cred_name,
		    gfarm_host_get_self_name(),
		    &desired_name);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_auth_error(
			    "Server credential configuration for %s:%s: %s",
			    service_tag, hostname, gfarm_error_string(e));
			return (e);
		}
		rv = gfarmGssAcquireCredential(&cred,
		    desired_name, GSS_C_BOTH,
		    &e_major, &e_minor, NULL);
		if (desired_name != GSS_C_NO_NAME)
			gfarmGssDeleteName(&desired_name, NULL, NULL);
		if (rv < 0) {
			if (gflog_auth_get_verbose()) {
				gflog_error(
				    "Can't get server credential for %s",
				    service_tag);
				gfarmGssPrintMajorStatus(e_major);
				gfarmGssPrintMinorStatus(e_minor);
				gflog_error("GSI authentication error: %s",
				    hostname);
			}
			return (GFARM_ERR_AUTHENTICATION);
		}
	}

	session = gfarmSecSessionAccept(fd, cred, NULL, &e_major, &e_minor);
	if (cred != GSS_C_NO_CREDENTIAL) {
		OM_uint32 e_major2, e_minor2;
		
		if (gfarmGssDeleteCredential(&cred, &e_major2, &e_minor2) < 0
		    && gflog_auth_get_verbose()) {
			gflog_warning("Can't release credential because of:");
			gfarmGssPrintMajorStatus(e_major2);
			gfarmGssPrintMinorStatus(e_minor2);
		}
	}
	if (session == NULL) {
		if (gflog_auth_get_verbose()) {
			gflog_error("Can't accept session because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
			gflog_error("GSI authentication error: %s", hostname);
		}
		return (GFARM_ERR_AUTHENTICATION);
	}
	/* XXX NOTYET determine *peer_typep == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	userinfo = gfarmSecSessionGetInitiatorInfo(session);
#if 0
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
		    "cannot map DN into global username: %s",
		    userinfo->distName);
	}
#else
	/*
	 * DN is used as a global user name for now since a gfmd
	 * cannot call an RPC to itself.  It should be mapped to the
	 * global user name by a caller when gfarm_authorize()
	 * successfully returned.
	 */
	global_username = strdup(userinfo->distName);
	if (global_username == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		error = GFARM_AUTH_ERROR_DENIED;
		gflog_error("authorize_gsi: DN=\"%s\": no memory",
		    userinfo->distName);
	}
	else
		e = GFARM_ERR_NO_ERROR;
#endif
	if (e == GFARM_ERR_NO_ERROR) {
		/* assert(error == GFARM_AUTH_ERROR_NO_ERROR); */

		/* succeed, do logging */
		gflog_notice(
		    "(%s@%s) authenticated: auth=%s local_user=%s DN=\"%s\"",
		    global_username, hostname,
		    auth_method_name,
		    gfarmAuthGetAuthEntryType(userinfo) == GFARM_AUTH_USER ?
		    userinfo->authData.userAuth.localName : "@host@",
		    userinfo->distName);

		if (switch_to) {
			GFARM_MALLOC_ARRAY(aux, strlen(global_username) + 1 +
			    strlen(hostname) + 1);
			if (aux == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				error = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
				gflog_error("(%s@%s) authorize_gsi: %s",
				    global_username, hostname,
				    gfarm_error_string(e));
			}
		}
	}

	gfp_xdr_set_secsession(conn, session, GSS_C_NO_CREDENTIAL);
	e2 = gfp_xdr_send(conn, "i", error);
	if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_error("(%s@%s) authorize_gsi: send reply: %s",
		    global_username, hostname, gfarm_error_string(e2));
	} else if ((e2 = gfp_xdr_flush(conn)) != GFARM_ERR_NO_ERROR) {
		gflog_error("(%s@%s) authorize_gsi: completion: %s",
		    global_username, hostname, gfarm_error_string(e2));
	}

	if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR) {
		if (global_username != NULL)
			free(global_username);
		if (aux != NULL)
			free(aux);
		gfp_xdr_reset_secsession(conn);
		gfp_xdr_set_socket(conn, fd);
		return (e);
	}

	if (switch_to &&
	    gfarmAuthGetAuthEntryType(userinfo) == GFARM_AUTH_USER) {
		sprintf(aux, "%s@%s", global_username, hostname);
		gflog_set_auxiliary_info(aux);

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

	/* determine *peer_typep == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	if (peer_typep != NULL) {
		if (gfarmAuthGetAuthEntryType(userinfo) == GFARM_AUTH_HOST)
		     *peer_typep = GFARM_AUTH_ID_TYPE_SPOOL_HOST;
		else
		     *peer_typep = GFARM_AUTH_ID_TYPE_USER;
	}
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
gfarm_authorize_gsi(struct gfp_xdr *conn,
	int switch_to, char *service_tag, char *hostname,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	return (gfarm_authorize_gsi_common(conn,
	    switch_to, service_tag, hostname, "gsi",
	    peer_typep, global_usernamep));
}

/*
 * "gsi_auth" method
 */

gfarm_error_t
gfarm_authorize_gsi_auth(struct gfp_xdr *conn,
	int switch_to, char *service_tag, char *hostname,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e = gfarm_authorize_gsi_common(conn,
	    switch_to, service_tag, hostname, "gsi_auth",
	    peer_typep, global_usernamep);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}
