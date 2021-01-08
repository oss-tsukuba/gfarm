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
gfarm_authorize_tls_sharedsecret(struct gfp_xdr *conn, int switch_to,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e;

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_ACCEPT);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		return (e);
	}
	e = gfarm_authorize_sharedsecret_common(
	    conn, switch_to, service_tag, hostname, auth_uid_to_global_user,
	    closure, "tls_sharedsecret", peer_typep, global_usernamep);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		gfp_xdr_tls_reset(conn);
	}
	return (e);
}

/*
 * auth_server_tls_client_certificate
 */
gfarm_error_t gfarm_authorize_tls_client_certificate(
	struct gfp_xdr *conn, int switch_to,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e, e2;
	int eof;
	gfarm_int32_t peer_type, result;
	char *reserved, *global_username = NULL;

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_ACCEPT);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		return (e);
	}

	/*
	 * 2nd parameter is reserved for future use
	 * (e.g. Server Name Indication)
	 */
	e = gfp_xdr_recv(conn, 1, &eof, "is", &peer_type, &reserved);
	if (e != GFARM_ERR_NO_ERROR || eof) {
		/* this is not gfarceful, but OK because of a network error */
		if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
			e = GFARM_ERR_UNEXPECTED_EOF;
		return (e);
	}
	if (peer_type != GFARM_AUTH_ID_TYPE_USER &&
	    peer_type != GFARM_AUTH_ID_TYPE_SPOOL_HOST &&
	    peer_type != GFARM_AUTH_ID_TYPE_METADATA_HOST) {
		result = GFARM_AUTH_ERROR_NOT_SUPPORTED;
	} else {
		e = (*auth_uid_to_global_user)(closure,
		    GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE, peer_type,
		    gfp_xdr_tls_initiator_dn_oneline(conn), &global_username);
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

		if (result == GFARM_AUTH_ERROR_NO_ERROR && switch_to) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "(%s@%s): user privilege switching is "
			    "not supported in auth tls_client_certificate",
			    global_username, hostname);
			result = GFARM_AUTH_ERROR_NOT_SUPPORTED;
		}
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
		    gfp_xdr_tls_initiator_dn_oneline(conn));

		*peer_typep = peer_type;
		*global_usernamep = global_username;
	} else {
		/* is this case graceful? */
		gfp_xdr_tls_reset(conn);
		free(global_username);
	}

	return (e);
}
