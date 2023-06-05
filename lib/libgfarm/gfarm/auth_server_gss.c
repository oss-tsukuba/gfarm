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

#include "gss.h"

#include "liberror.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"
#include "auth_gss.h"
#include "gfarm_gss.h"

#include "gfm_proto.h" /* for GFMD_USERNAME, XXX layering violation */
#include "gfs_proto.h" /* for GFSD_USERNAME, XXX layering violation */

/*
 * server side authentication
 */

/*
 * "gsi" method
 */
gfarm_error_t
gfarm_authorize_gss(struct gfp_xdr *conn, struct gfarm_gss *gss,
	char *service_tag, char *hostname, int send_self_type,
	enum gfarm_auth_method auth_method,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	int eof, gsi_errno = 0, fd = gfp_xdr_fd(conn);
	gfarm_error_t e, e2;
	char *global_username = NULL, *aux = NULL;
	gfarm_OM_uint32 e_major, e_minor;
	gfarmSecSession *session;
	char *distname;
	gfarm_int32_t error = GFARM_AUTH_ERROR_NO_ERROR; /* gfarm_auth_error */
	enum gfarm_auth_cred_type cred_type =
	    gfarm_auth_server_cred_type_get(service_tag);
	char *cred_service = gfarm_auth_server_cred_service_get(service_tag);
	char *cred_name = gfarm_auth_server_cred_name_get(service_tag);
	gss_cred_id_t cred;
	enum gfarm_auth_id_role peer_role;
	gfarm_int32_t req, arg;

	e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000712,
		    "authorize_gss: %s: protocol drain: %s",
		    hostname, gfarm_error_string(e));
		return (e);
	}

	e = gfarm_gss_server_initialize(gss);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000713,
		    "authorize_gss: %s: GSI initialize: %s",
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

	gfp_xdr_set_secsession(conn, gss, session, GSS_C_NO_CREDENTIAL, NULL);

	/* peer_role is not sent in GSI due to protocol compatibility */
	if (!send_self_type) {
		peer_role = GFARM_AUTH_ID_ROLE_UNKNOWN;
	} else {
		e = gfp_xdr_recv(conn, 1, &eof, "ii", &req, &arg);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			/*
			 * this is not gfarceful,
			 * but OK because of a network error
			 */
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_UNEXPECTED_EOF;
			return (e);
		}
		if (req == GFARM_AUTH_GSS_GIVEUP) {
			/* this shouldn't happen for now */
			gflog_warning(GFARM_MSG_UNFIXED,
			    "client does not accept me: %s",
			    gfarm_error_string(arg));
			/* is this case graceful? */
			gfp_xdr_reset_secsession(conn);
			return (GFARM_ERR_AUTHENTICATION);
		} else if (req == GFARM_AUTH_GSS_CLIENT_ROLE) {
			peer_role = arg;
		} else {
			/* unknown protocol */
			gflog_warning(GFARM_MSG_UNFIXED,
			    "unknown authentication request: 0x%x (0x%x)",
			(int)req, (int)arg);
			gfp_xdr_reset_secsession(conn);
			return (GFARM_ERR_PROTOCOL);
		}
		if (peer_role != GFARM_AUTH_ID_ROLE_USER &&
		    peer_role != GFARM_AUTH_ID_ROLE_SPOOL_HOST &&
		    peer_role != GFARM_AUTH_ID_ROLE_METADATA_HOST) {
			e = GFARM_ERR_PROTOCOL;
			error = GFARM_AUTH_ERROR_NOT_SUPPORTED;
		}
	}

	if (gss->gfarmSecSessionGetInitiatorDistName(session, &distname) < 0) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "GFSL: unexpected session state");
		distname = "<not-known>";
		error = GFARM_AUTH_ERROR_NOT_SUPPORTED;
		e = GFARM_ERR_INTERNAL_ERROR;
	} else if (error == GFARM_AUTH_ERROR_NO_ERROR) {
		e = (*auth_uid_to_global_user)(closure, auth_method,
		    distname, &peer_role, &global_username);
		switch (e) {
		case GFARM_ERR_NO_ERROR:
			switch (peer_role) {
			case GFARM_AUTH_ID_ROLE_UNKNOWN:
				e = GFARM_ERR_INTERNAL_ERROR;
				error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
				gflog_error(GFARM_MSG_UNFIXED,
				    "authorize_gss: \"%s\" @ %s: "
				    "GSS authentication: peer type unknown",
				    distname, hostname);
				break;
			case GFARM_AUTH_ID_ROLE_USER:
				break;
			case GFARM_AUTH_ID_ROLE_SPOOL_HOST:
				free(global_username); /* hostname in DN */
				global_username = strdup(GFSD_USERNAME);
				if (global_username == NULL) {
					e = GFARM_ERR_NO_MEMORY;
					error =
					 GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
				}
				break;
			case GFARM_AUTH_ID_ROLE_METADATA_HOST:
				free(global_username); /* hostname in DN */
				global_username = strdup(GFMD_USERNAME);
				if (global_username == NULL) {
					e = GFARM_ERR_NO_MEMORY;
					error =
					 GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
				}
				break;
			}
			break;
		case GFARM_ERR_NO_MEMORY:
			error = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
			gflog_error(GFARM_MSG_1003393,
			    "authorize_gss: \"%s\" @ %s: GSS authentication: "
			    "no memory", distname, hostname);
			break;
		case GFARM_ERR_NO_SUCH_OBJECT:
			error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
			gflog_notice(GFARM_MSG_1003394,
			    "authorize_gss: \"%s\" @ %s: authentication: "
			    "%s%s", distname, hostname, gfarm_error_string(e),
			    e == GFARM_ERR_AUTHENTICATION ?
			    " (possibly unregistered user/host)" : "");
		default:
			gflog_error(GFARM_MSG_UNFIXED,
			    "authorize_gss: \"%s\" @ %s: error: %s",
			    distname, hostname, gfarm_error_string(e));
			e = GFARM_ERR_AUTHENTICATION;
			error = GFARM_AUTH_ERROR_DENIED;
			break;
		}
	}

	if (e == GFARM_ERR_NO_ERROR) {
		/* assert(error == GFARM_AUTH_ERROR_NO_ERROR); */

		/* succeed, do logging */
		gflog_notice(GFARM_MSG_UNFIXED,
		    "(%s@%s) authenticated: auth=%s type=%s  DN=\"%s\"",
		    global_username, hostname,
		    gfarm_auth_method_name(auth_method),
		    gfarm_auth_id_role_name(peer_role), distname);
	}

	e2 = gfp_xdr_send(conn, "i", error);
	if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000723,
		    "(%s@%s) authorize_gss: send reply: %s",
		    global_username, hostname, gfarm_error_string(e2));
	} else if ((e2 = gfp_xdr_flush(conn)) != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000724,
		    "(%s@%s) authorize_gss: completion: %s",
		    global_username, hostname, gfarm_error_string(e2));
	}

	if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR) {
		if (global_username != NULL)
			free(global_username);
		if (aux != NULL)
			free(aux);
		gfp_xdr_reset_secsession(conn);
		gflog_debug(GFARM_MSG_1001477,
			"Authorization failed: %s",
			gfarm_error_string(e != GFARM_ERR_NO_ERROR ? e : e2));
		return (e != GFARM_ERR_NO_ERROR ? e : e2);
	}

	/* determine *peer_rolep == GFARM_AUTH_ID_ROLE_SPOOL_HOST */
	if (peer_rolep != NULL)
		*peer_rolep = peer_role;
	if (global_usernamep != NULL)
		*global_usernamep = global_username;
	else
		free(global_username);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * "gsi_auth" method
 */

gfarm_error_t
gfarm_authorize_gss_auth(struct gfp_xdr *conn, struct gfarm_gss *gss,
	char *service_tag, char *hostname, int send_self_type,
	enum gfarm_auth_method auth_method,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	gfarm_error_t e = gfarm_authorize_gss(conn, gss,
	    service_tag, hostname, send_self_type, auth_method,
	    auth_uid_to_global_user, closure,
	    peer_rolep, global_usernamep);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}
