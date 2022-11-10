#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include <sasl/sasl.h>

#include <gfarm/gfarm.h>

#include "context.h"
#include "gfp_xdr.h"
#include "io_tls.h"
#include "auth.h"

/*
 * server side authentication
 */

static gfarm_error_t sasl_server_initialized = GFARM_ERR_INTERNAL_ERROR;

static gfarm_error_t
gfarm_authorize_sasl_common(struct gfp_xdr *conn,
	char *service_tag, char *hostname, enum gfarm_auth_method auth_method,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_type *, char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep,
	const char *diag)
{
	gfarm_error_t e;
	int save_errno, eof, r, count;
	char self_hsbuf[NI_MAXHOST + NI_MAXSERV];
	char peer_hsbuf[NI_MAXHOST + NI_MAXSERV];
	sasl_conn_t *sasl_conn;
	char *chosen_mechanism = NULL;
	gfarm_int32_t has_initial_response, result;
	const char *user_id, *data = NULL;
	unsigned len;
	char *response = NULL, *global_username = NULL;
	size_t rsz = 0;
	enum gfarm_auth_id_type peer_type = GFARM_AUTH_ID_TYPE_USER;

	/* sanity check, shouldn't happen */
	if (sasl_server_initialized != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED, "sasl_server_initialized: %s",
		    gfarm_error_string(sasl_server_initialized));
		return (sasl_server_initialized);
	}

	save_errno = gfarm_sasl_addr_string(gfp_xdr_fd(conn),
	    self_hsbuf, sizeof(self_hsbuf),
	    peer_hsbuf, sizeof(peer_hsbuf), diag);
	if (save_errno != 0)
		return (gfarm_errno_to_error(save_errno));

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_ACCEPT);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_UNFIXED,
		    "failed to establish SSL connection");
		/* is this case graceful? */
		return (e);
	}

	/*
	 * negotiation protocol phase 1:
	 *
	 * client: i:server_authentication_result
	 */
	e = gfp_xdr_recv(conn, 1, &eof, "i", &result);
	if (e != GFARM_ERR_NO_ERROR || eof) {
		if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
			e = GFARM_ERR_UNEXPECTED_EOF;
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (e);
	}
	if (result != GFARM_ERR_NO_ERROR) {
		/* server cert is invalid? raise alert */
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s: does not accept my certificate: %s",
		    hostname, gfarm_error_string(result));
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	r = sasl_server_new("gfarm", gfarm_host_get_self_name(), NULL,
	    self_hsbuf, peer_hsbuf, NULL, 0, &sasl_conn);
	if (r != SASL_OK) {
		gflog_notice(GFARM_MSG_UNFIXED, "sasl_server_new(): %s",
		    sasl_errstring(r, NULL, NULL));
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	/*
	 * mechanism negotiation
	 */
	if (gfarm_ctxp->sasl_mechanisms != NULL) {
		data = gfarm_ctxp->sasl_mechanisms;
		len = strlen(data);
	} else {
		r = sasl_listmech(sasl_conn, NULL, NULL, " ", NULL,
		    &data, &len, &count);
		if (r != SASL_OK) {
			gflog_error(GFARM_MSG_UNFIXED, "sasl_listmech(): %s",
			    sasl_errstring(r, NULL, NULL));
			data = "";
			len = 0;
		}
	}
	if (data == NULL) {
		/* mechanism_candidates == "" means error */
		e = gfp_xdr_send(conn, "s", "");
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_NO_MEMORY);
	}
	if (gflog_auth_get_verbose()) {
		gflog_info(GFARM_MSG_UNFIXED, "SASL: propose mechanisms <%s>",
		    data);
	}

	/*
	 * negotiation protocol phase 2:
	 *
	 * server: s:mechanism_candidates
	 * client: s:chosen_mechanism
	 * client: i:does_client_initial_response_exist?
	 * client:(s:client_initial_response) ... optional
	 */
	e = gfp_xdr_send(conn, "s", data);
	data = NULL;
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (e);
	}

	e = gfp_xdr_recv(conn, 1, &eof, "s", &chosen_mechanism);
	if (e != GFARM_ERR_NO_ERROR || eof ||
	    (chosen_mechanism != NULL && chosen_mechanism[0] == '\0')) {
		/* chosen_mechanism == "" means error */
		if (e == GFARM_ERR_NO_ERROR && EOF) /* i.e. eof */
			e = GFARM_ERR_UNEXPECTED_EOF;
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (e);
	}
	e = gfp_xdr_recv(conn, 1, &eof, "i", &has_initial_response);
	if (e != GFARM_ERR_NO_ERROR || eof) {
		if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
			e = GFARM_ERR_UNEXPECTED_EOF;
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (e);
	}
	if (has_initial_response) {
		e = gfp_xdr_recv(conn, 1, &eof, "B", &rsz, &response);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_UNEXPECTED_EOF;
			free(chosen_mechanism);
			sasl_dispose(&sasl_conn);
			gfp_xdr_tls_reset(conn); /* is this case graceful? */
			return (e);
		}
	}

	if (gfarm_ctxp->sasl_mechanisms != NULL &&
	    strcasecmp(chosen_mechanism, gfarm_ctxp->sasl_mechanisms) != 0) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: SASL mechanism does not match. \"%s\" vs \"%s\"",
		    hostname, gfarm_ctxp->sasl_mechanisms, chosen_mechanism);
		/* XXX FIXME is this graceful? */
		e = gfp_xdr_send(conn, "i",
		    (gfarm_int32_t)GFARM_AUTH_SASL_STEP_ERROR);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		free(response);
		free(chosen_mechanism);
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	r = sasl_server_start(sasl_conn, chosen_mechanism, response, rsz,
	    &data, &len);
	free(response);
	free(chosen_mechanism);
	chosen_mechanism = response = NULL;
	if (r != SASL_OK && r != SASL_CONTINUE) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: SASL negotiation: %s", hostname,
		    sasl_errstring(r, NULL, NULL));
		e = gfp_xdr_send(conn, "i",
		    (gfarm_int32_t)GFARM_AUTH_SASL_STEP_ERROR);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	while (r == SASL_CONTINUE) {
		e = gfp_xdr_send(conn, "i",
		    (gfarm_int32_t)GFARM_AUTH_SASL_STEP_CONTINUE);
		if (e == GFARM_ERR_NO_ERROR) {
			if (data) {
				e = gfp_xdr_send(conn, "b", (size_t)len, data);
			} else {
				e = gfp_xdr_send(conn, "b", (size_t)0, "");
			}
		}
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		if (e != GFARM_ERR_NO_ERROR) {
			sasl_dispose(&sasl_conn);
			gfp_xdr_tls_reset(conn); /* is this case graceful? */
			return (e);
		}

		e = gfp_xdr_recv(conn, 1, &eof, "B", &rsz, &response);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_UNEXPECTED_EOF;
			sasl_dispose(&sasl_conn);
			gfp_xdr_tls_reset(conn); /* is this case graceful? */
			return (e);
		}
		r = sasl_server_step(sasl_conn, response, rsz, &data, &len);
		free(response);
		response = NULL;
		if (r != SASL_OK && r != SASL_CONTINUE) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: SASL negotiation: %s", peer_hsbuf,
			    sasl_errstring(r, NULL, NULL));
			e = gfp_xdr_send(conn, "i",
			    (gfarm_int32_t)GFARM_AUTH_SASL_STEP_ERROR);
			if (e == GFARM_ERR_NO_ERROR)
				e = gfp_xdr_flush(conn);
			sasl_dispose(&sasl_conn);
			gfp_xdr_tls_reset(conn); /* is this case graceful? */
			return (GFARM_ERR_AUTHENTICATION);
		}
	}

	if (r != SASL_OK) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: SASL: incorrect authentication: %s", hostname,
		    sasl_errstring(r, NULL, NULL));
		e = gfp_xdr_send(conn, "i",
		    (gfarm_int32_t)GFARM_AUTH_SASL_STEP_ERROR);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	r = sasl_getprop(sasl_conn, SASL_USERNAME,
	    (const void **)&user_id);

	if (r != SASL_OK) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: SASL: SASL_USERNAME: %s", hostname,
		    sasl_errstring(r, NULL, NULL));
		e = gfp_xdr_send(conn, "i",
		    (gfarm_int32_t)GFARM_AUTH_SASL_STEP_ERROR);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	e = (*auth_uid_to_global_user)(closure, auth_method,
	    user_id, &peer_type, &global_username);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s@%s: unregistered user: %s", user_id, hostname,
		    sasl_errstring(r, NULL, NULL));
		e = gfp_xdr_send(conn, "i",
		    (gfarm_int32_t)GFARM_AUTH_SASL_STEP_ERROR);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	sasl_dispose(&sasl_conn); /* user_id is freed here */

	gflog_notice(GFARM_MSG_UNFIXED,
	    "(%s@%s) authenticated: auth=%s type:user",
	    global_username, hostname,
	    auth_method == GFARM_AUTH_METHOD_SASL ? "sasl" :
	    auth_method == GFARM_AUTH_METHOD_SASL_AUTH ? "sasl_auth" :
	    "sasl_unexpected");

	e = gfp_xdr_send(conn, "i", (gfarm_int32_t)GFARM_AUTH_SASL_STEP_DONE);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		free(global_username);
		return (e);
	}

	*peer_typep = GFARM_AUTH_ID_TYPE_USER;
	*global_usernamep = global_username;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * "sasl" method
 */

gfarm_error_t
gfarm_authorize_sasl(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_type *, char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	return (gfarm_authorize_sasl_common(conn,
	    service_tag, hostname, GFARM_AUTH_METHOD_SASL,
	    auth_uid_to_global_user, closure,
	    peer_typep, global_usernamep,
	    "gfarm_authorize_sasl"));
}

/*
 * "sasl_auth" method
 */

gfarm_error_t
gfarm_authorize_sasl_auth(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_type *, char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	gfarm_error_t e = gfarm_authorize_sasl_common(conn,
	    service_tag, hostname, GFARM_AUTH_METHOD_SASL_AUTH,
	    auth_uid_to_global_user, closure,
	    peer_typep, global_usernamep,
	    "gfarm_authorize_sasl_auth");

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_from_tls_to_fd(conn);
	return (e);
}

void
gfarm_sasl_server_init(void)
{
	static sasl_callback_t callbacks[] = {
		{ SASL_CB_LOG, (int(*)(void))gfarm_sasl_log, NULL },
		{ SASL_CB_LIST_END, NULL, NULL },
	};
	int r = sasl_server_init(callbacks, "gfarm");

	if (r != SASL_OK) {
		gflog_notice(GFARM_MSG_UNFIXED, "sasl_server_init(): %s",
		    sasl_errstring(r, NULL, NULL));
		sasl_server_initialized = GFARM_ERR_UNKNOWN;
		return;
	}
	sasl_server_initialized = GFARM_ERR_NO_ERROR;
}

int
gfarm_auth_server_method_sasl_available(void)
{
	return (sasl_server_initialized == GFARM_ERR_NO_ERROR);
}

