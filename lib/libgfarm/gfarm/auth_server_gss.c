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
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"

#include "gfsl_secure_session.h"
#include "gfarm_auth.h"

#include "liberror.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"
#include "auth_gss.h"
#include "gfarm_gss.h"
#include "gss.h"

#include "gfs_proto.h" /* for GFSD_USERNAME, XXX layering violation */

/*
 * server side authentication
 */

static gfarm_error_t
gfarm_authorize_gss_common0(struct gfp_xdr *conn, struct gfarm_gss *gss,
	char *service_tag, char *hostname, enum gfarm_auth_method auth_method,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	int gsi_errno = 0, fd = gfp_xdr_fd(conn);
	gfarm_error_t e, e2;
	char *global_username = NULL, *aux = NULL;
	gfarm_OM_uint32 e_major, e_minor;
	gfarmSecSession *session;
	gfarmAuthEntry *userinfo;
	char *distname, *localname;
	gfarm_int32_t error = GFARM_AUTH_ERROR_NO_ERROR; /* gfarm_auth_error */
	enum gfarm_auth_cred_type cred_type =
	    gfarm_auth_server_cred_type_get(service_tag);
	char *cred_service = gfarm_auth_server_cred_service_get(service_tag);
	char *cred_name = gfarm_auth_server_cred_name_get(service_tag);
	gss_cred_id_t cred;
	enum gfarm_auth_id_type peer_type = GFARM_AUTH_ID_TYPE_UNKNOWN;

	e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000712,
		    "authorize_gsi: %s: protocol drain: %s",
		    hostname, gfarm_error_string(e));
		return (e);
	}

	e = gfarm_gss_server_initialize(gss);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000713,
		    "authorize_gsi: %s: GSI initialize: %s",
		    hostname, gfarm_error_string(e));
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
		e = gfarm_gss_cred_config_convert_to_name(gss,
		    cred_type, cred_service, cred_name,
		    gfarm_host_get_self_name(),
		    &desired_name);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_auth_error(GFARM_MSG_1000714, "%s: "
			    "Server credential configuration for %s:%s: %s",
			    hostname,
			    service_tag, hostname, gfarm_error_string(e));
			return (e);
		}
		rv = gss->gfarmGssAcquireCredential(&cred,
		    desired_name, GSS_C_ACCEPT,
		    &e_major, &e_minor, NULL);
		if (desired_name != GSS_C_NO_NAME)
			gss->gfarmGssDeleteName(&desired_name, NULL, NULL);
		if (rv < 0) {
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_1000715,
				    "%s: Can't get server credential for %s",
				    hostname, service_tag);
				gss->gfarmGssPrintMajorStatus(e_major);
				gss->gfarmGssPrintMinorStatus(e_minor);
				gflog_error(GFARM_MSG_1000716,
				    "GSI authentication error: %s",
				    hostname);
			}
			return (GFARM_ERR_AUTHENTICATION);
		}
	}

	session = gss->gfarmSecSessionAccept(fd, cred, NULL,
	    &gsi_errno, &e_major, &e_minor);
	if (cred != GSS_C_NO_CREDENTIAL) {
		gfarm_OM_uint32 e_major2, e_minor2;

		if (gss->gfarmGssDeleteCredential(&cred,
		    &e_major2, &e_minor2) < 0 && gflog_auth_get_verbose()) {
			gflog_warning(GFARM_MSG_1000717,
			    "Can't release credential because of:");
			gss->gfarmGssPrintMajorStatus(e_major2);
			gss->gfarmGssPrintMinorStatus(e_minor2);
		}
	}
	if (session == NULL) {
		if (gflog_auth_get_verbose()) {
			gflog_info(GFARM_MSG_1000718,
			    "%s: Can't accept session because of:",
			    hostname);
			if (gsi_errno != 0) {
				gflog_info(GFARM_MSG_1004003, "%s",
				    strerror(gsi_errno));
			} else {
				gss->gfarmGssPrintMajorStatus(e_major);
				gss->gfarmGssPrintMinorStatus(e_minor);
			}
			gflog_info(GFARM_MSG_1000719,
			    "GSI authentication error: %s", hostname);
		}
		/*
		 * The expiration of CA and CRL cannot be
		 * checked in gfarm_gss_server_initialize() and it
		 * cannot be investigated by e_major and e_minor of
		 * gfarmSecSessionAccept().  However, do not call
		 * gfarm_gsi_server_finalize() here, which causes the
		 * data race.  Instead, deliver SIGHUP to gfmd.
		 */
		if (gsi_errno != 0)
			return (gfarm_errno_to_error(gsi_errno));
		return (GFARM_ERR_AUTHENTICATION);
	}

	userinfo = gss->gfarmSecSessionGetInitiatorInfo(session);
	distname = gss->gfarmAuthGetDistName(userinfo);
	localname = gss->gfarmAuthGetLocalName(userinfo);
	switch (gss->gfarmAuthGetAuthEntryType(userinfo)) {
	case GFARM_AUTH_HOST:
		peer_type = GFARM_AUTH_ID_TYPE_SPOOL_HOST;
		if ((global_username = strdup(GFSD_USERNAME)) != NULL) {
			e = GFARM_ERR_NO_ERROR;
		} else {
			e = GFARM_ERR_NO_MEMORY;
			error = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
			gflog_error(GFARM_MSG_1003393,
			    "authorize_gsi: \"%s\" @ %s: host authentication: "
			    "no memory", distname, hostname);
		}
		break;
	case GFARM_AUTH_USER:
		peer_type = GFARM_AUTH_ID_TYPE_USER;
		e = (*auth_uid_to_global_user)(closure, auth_method,
		    peer_type, distname, &global_username);
		if (e != GFARM_ERR_NO_ERROR) {
			error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
			gflog_notice(GFARM_MSG_1003394,
			    "authorize_gsi: \"%s\" @ %s: user authentication: "
			    "%s%s", distname, hostname, gfarm_error_string(e),
			    e == GFARM_ERR_AUTHENTICATION ?
			    " (possibly unregistered user)" : "");
		}
		break;
	default:
		gflog_error(GFARM_MSG_1000720,
		    "authorize_gsi: \"%s\" @ %s: auth entry type=%d", distname,
		    hostname, gss->gfarmAuthGetAuthEntryType(userinfo));
		e = GFARM_ERR_AUTHENTICATION;
		error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
		break;
	}

	if (e == GFARM_ERR_NO_ERROR) {
		/* assert(error == GFARM_AUTH_ERROR_NO_ERROR); */

		/* succeed, do logging */
		gflog_notice(GFARM_MSG_1000721,
		    "(%s@%s) authenticated: auth=%s local_user=%s DN=\"%s\"",
		    global_username, hostname,
		    gfarm_auth_method_name(auth_method),
		    gss->gfarmAuthGetAuthEntryType(userinfo)
		    == GFARM_AUTH_USER ? localname : "@host@", distname);

	}

	gfp_xdr_set_secsession(conn, gss, session, GSS_C_NO_CREDENTIAL, NULL);
	e2 = gfp_xdr_send(conn, "i", error);
	if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000723,
		    "(%s@%s) authorize_gsi: send reply: %s",
		    global_username, hostname, gfarm_error_string(e2));
	} else if ((e2 = gfp_xdr_flush(conn)) != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000724,
		    "(%s@%s) authorize_gsi: completion: %s",
		    global_username, hostname, gfarm_error_string(e2));
	}

	if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR) {
		if (global_username != NULL)
			free(global_username);
		if (aux != NULL)
			free(aux);
		gfp_xdr_reset_secsession(conn);
		gfp_xdr_set_socket(conn, fd);
		gflog_debug(GFARM_MSG_1001477,
			"Authorization failed: %s",
			gfarm_error_string(e != GFARM_ERR_NO_ERROR ? e : e2));
		return (e != GFARM_ERR_NO_ERROR ? e : e2);
	}

	/* determine *peer_typep == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	if (peer_typep != NULL)
		*peer_typep = peer_type;
	if (global_usernamep != NULL)
		*global_usernamep = global_username;
	else
		free(global_username);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_authorize_gss_common(struct gfp_xdr *conn, struct gfarm_gss *gss,
	char *service_tag, char *hostname, enum gfarm_auth_method auth_method,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e;

	e = gfarm_authorize_gss_common0(conn, gss,
	    service_tag, hostname, auth_method, auth_uid_to_global_user,
	    closure, peer_typep, global_usernamep);
	return (e);
}

/*
 * "gsi" method
 */
gfarm_error_t
gfarm_authorize_gss(struct gfp_xdr *conn, struct gfarm_gss *gss,
	char *service_tag, char *hostname, enum gfarm_auth_method auth_method,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	return (gfarm_authorize_gss_common(conn, gss,
	    service_tag, hostname, auth_method,
	    auth_uid_to_global_user, closure,
	    peer_typep, global_usernamep));
}

/*
 * "gsi_auth" method
 */

gfarm_error_t
gfarm_authorize_gss_auth(struct gfp_xdr *conn, struct gfarm_gss *gss,
	char *service_tag, char *hostname, enum gfarm_auth_method auth_method,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e = gfarm_authorize_gss_common(conn, gss,
	    service_tag, hostname, auth_method,
	    auth_uid_to_global_user, closure,
	    peer_typep, global_usernamep);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}
