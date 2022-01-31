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
gfarm_error_t
gfarm_authorize_tls_sharedsecret(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e;

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_ACCEPT);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_UNFIXED,
		    "failed to establish SSL connection");
		/* is this case graceful? */
		return (e);
	}
	e = gfarm_authorize_sharedsecret_common(
	    conn, service_tag, hostname, auth_uid_to_global_user,
	    closure, "tls_sharedsecret", peer_typep, global_usernamep);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
	}
	return (e);
}

/*
 * auth_server_tls_client_certificate
 */
gfarm_error_t gfarm_authorize_tls_client_certificate(
	struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e, e2;
	int eof;
	gfarm_int32_t req, arg, peer_type, result;
	char *global_username = NULL;

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_ACCEPT|
	    GFP_XDR_TLS_CLIENT_AUTHENTICATION |
	    (gfarm_ctxp->tls_proxy_certificate ?
	     GFP_XDR_TLS_CLIENT_USE_PROXY_CERTIFICATE : 0));
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_UNFIXED,
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
		gflog_warning(GFARM_MSG_UNFIXED,
		    "my certificate is not accepted: %s",
		    gfarm_error_string(arg));
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	} else if (req == GFARM_AUTH_TLS_CLIENT_CERTIFICATE_CLIENT_TYPE) {
		peer_type = arg;
	} else {
		/* unknown protocol */
		gflog_warning(GFARM_MSG_UNFIXED,
		    "unknown authentication request: 0x%x (0x%x)",
		(int)req, (int)arg);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_PROTOCOL);
	}


	if (peer_type != GFARM_AUTH_ID_TYPE_USER &&
	    peer_type != GFARM_AUTH_ID_TYPE_SPOOL_HOST &&
	    peer_type != GFARM_AUTH_ID_TYPE_METADATA_HOST) {
		result = GFARM_AUTH_ERROR_NOT_SUPPORTED;
	} else {
		e = (*auth_uid_to_global_user)(closure,
		    GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE, peer_type,
		    peer_type == GFARM_AUTH_ID_TYPE_USER ?
		    gfp_xdr_tls_peer_dn_gsi(conn) :
		    gfp_xdr_tls_peer_dn_common_name(conn),
		    &global_username);
		if (e == GFARM_ERR_NO_ERROR) {
			switch (peer_type) {
			case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
				free(global_username); /* hostname in DN */
				global_username = strdup(GFSD_USERNAME);
				if (global_username == NULL)
					e = GFARM_ERR_NO_MEMORY;
				break;
			case GFARM_AUTH_ID_TYPE_METADATA_HOST:
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

		gflog_notice(GFARM_MSG_UNFIXED,
		    "(%s@%s) authenticated: auth=%s type:%s DN=\"%s\"",
		    global_username, hostname,
		    "tls_client_certificate",
		    peer_type == GFARM_AUTH_ID_TYPE_USER ? "user" :
		    peer_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST ? "gfsd" :
		    peer_type == GFARM_AUTH_ID_TYPE_METADATA_HOST ? "gfmd" :
		    "unknown",
		    gfp_xdr_tls_peer_dn_gsi(conn));

		*peer_typep = peer_type;
		*global_usernamep = global_username;
	} else {
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		free(global_username);
	}

	return (e);
}
