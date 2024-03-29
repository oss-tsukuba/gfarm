#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "context.h"
#include "gfp_xdr.h"
#include "io_tls.h"
#include "auth.h"
#include "gfm_proto.h"
#include "gfs_proto.h"

/*
 * auth_server_tls_sharedsecret
 */

int
gfarm_auth_server_method_is_tls_sharedsecret_available(void)
{
	return (1);
}

gfarm_error_t
gfarm_authorize_tls_sharedsecret(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	gfarm_error_t e;

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_ACCEPT);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1005316,
		    "failed to establish SSL connection");
		/* is this case graceful? */
		return (e);
	}
	e = gfarm_authorize_sharedsecret_common(
	    conn, service_tag, hostname, auth_uid_to_global_user,
	    closure, "tls_sharedsecret", peer_rolep, global_usernamep);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
	}
	return (e);
}

/*
 * auth_server_tls_client_certificate
 */

int
gfarm_auth_server_method_is_tls_client_certificate_available(void)
{
	return (1);
}

gfarm_error_t gfarm_authorize_tls_client_certificate(
	struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	gfarm_error_t e, e2;
	int eof;
	gfarm_int32_t req, arg, result;
	enum gfarm_auth_id_role peer_role;
	char *global_username = NULL;

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_ACCEPT|
	    GFP_XDR_TLS_CLIENT_AUTHENTICATION |
	    (gfarm_ctxp->tls_proxy_certificate ?
	     GFP_XDR_TLS_CLIENT_USE_PROXY_CERTIFICATE : 0));
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1005317,
		    "failed to establish SSL connection");
		/* is this case graceful? */
		return (e);
	}

	e = gfp_xdr_recv(conn, 1, &eof, "ii", &req, &arg);
	if (e != GFARM_ERR_NO_ERROR || eof) {
		/* this is not gfarceful, but OK because of a network error */
		if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
			e = GFARM_ERR_UNEXPECTED_EOF;
		return (e);
	}
	if (req == GFARM_AUTH_TLS_CLIENT_CERTIFICATE_GIVEUP) {
		/* server cert is invalid? raise alert */
		gflog_warning(GFARM_MSG_1005318,
		    "%s: does not accept my certificate: %s",
		    hostname, gfarm_error_string(arg));
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	} else if (req == GFARM_AUTH_TLS_CLIENT_CERTIFICATE_CLIENT_ROLE) {
		peer_role = arg;
	} else {
		/* unknown protocol */
		gflog_warning(GFARM_MSG_1005319,
		    "unknown authentication request: 0x%x (0x%x)",
		(int)req, (int)arg);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_PROTOCOL);
	}


	if (peer_role != GFARM_AUTH_ID_ROLE_USER &&
	    peer_role != GFARM_AUTH_ID_ROLE_SPOOL_HOST &&
	    peer_role != GFARM_AUTH_ID_ROLE_METADATA_HOST) {
		result = GFARM_AUTH_ERROR_NOT_SUPPORTED;
	} else {
		e = (*auth_uid_to_global_user)(closure,
		    GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE,
		    peer_role == GFARM_AUTH_ID_ROLE_USER ?
		    gfp_xdr_tls_peer_dn_gsi(conn) :
		    gfp_xdr_tls_peer_dn_common_name(conn),
		    &peer_role, &global_username);
		if (e == GFARM_ERR_NO_ERROR) {
			switch (peer_role) {
			case GFARM_AUTH_ID_ROLE_UNKNOWN:
				e = GFARM_ERR_INTERNAL_ERROR;
				gflog_error(GFARM_MSG_1005320,
				    "authorize_tls_client_certificate: "
				    "\"%s\" @ %s: peer type unknown",
				    gfp_xdr_tls_peer_dn_gsi(conn), hostname);
				break;
			case GFARM_AUTH_ID_ROLE_USER:
				break;
			case GFARM_AUTH_ID_ROLE_SPOOL_HOST:
				free(global_username); /* hostname in DN */
				global_username = strdup(GFSD_USERNAME);
				if (global_username == NULL)
					e = GFARM_ERR_NO_MEMORY;
				break;
			case GFARM_AUTH_ID_ROLE_METADATA_HOST:
				free(global_username); /* hostname in DN */
				global_username = strdup(GFMD_USERNAME);
				if (global_username == NULL)
					e = GFARM_ERR_NO_MEMORY;
				break;
			}
		}
		if (e == GFARM_ERR_NO_ERROR)
			result = GFARM_AUTH_ERROR_NO_ERROR;
		else if (e == GFARM_ERR_NO_MEMORY)
			result = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
		else if (e == GFARM_ERR_INTERNAL_ERROR)
			result = GFARM_AUTH_ERROR_INVALID_CREDENTIAL; /* ? */
		else
			result = GFARM_AUTH_ERROR_DENIED;
	}
	e2 = gfp_xdr_send(conn, "i", result);
	if (e2 == GFARM_ERR_NO_ERROR)
		e2 = gfp_xdr_flush(conn);
	if (e2 != GFARM_ERR_NO_ERROR)
		e = e2;
	else if (result != GFARM_AUTH_ERROR_NO_ERROR)
		e = GFARM_ERR_AUTHENTICATION;

	if (e == GFARM_ERR_NO_ERROR) {
		/* succeed, do logging */

		gflog_notice(GFARM_MSG_1005321,
		    "(%s@%s) authenticated: auth=%s type:%s DN=\"%s\"",
		    global_username, hostname,
		    "tls_client_certificate",
		    peer_role == GFARM_AUTH_ID_ROLE_USER ? "user" :
		    peer_role == GFARM_AUTH_ID_ROLE_SPOOL_HOST ? "gfsd" :
		    peer_role == GFARM_AUTH_ID_ROLE_METADATA_HOST ? "gfmd" :
		    "unknown",
		    gfp_xdr_tls_peer_dn_gsi(conn));

		*peer_rolep = peer_role;
		*global_usernamep = global_username;
	} else {
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		free(global_username);
	}

	return (e);
}
