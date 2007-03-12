#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/time.h>

#include <gssapi.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "gfevent.h"

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include "liberror.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"
#include "auth_gsi.h"

/*
 * client side authentication
 */

gfarm_error_t
gfarm_auth_request_gsi(struct gfp_xdr *conn,
	char *service_tag, char *hostname, enum gfarm_auth_id_type self_type)
{
	int fd = gfp_xdr_fd(conn);
	gfarm_error_t e;
	enum gfarm_auth_cred_type serv_type =
	    gfarm_auth_server_cred_type_get(service_tag);
	char *serv_service = gfarm_auth_server_cred_service_get(service_tag);
	char *serv_name = gfarm_auth_server_cred_name_get(service_tag);
	gss_name_t acceptor_name = GSS_C_NO_NAME;
	gss_cred_id_t cred;
	OM_uint32 e_major;
	OM_uint32 e_minor;
	gfarmSecSession *session;
	gfarm_int32_t error; /* enum gfarm_auth_error */
	int eof, cred_acquired = 0;

	e = gfarm_gsi_client_initialize();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfarm_gsi_cred_config_convert_to_name(
	    serv_type != GFARM_AUTH_CRED_TYPE_DEFAULT ?
	    serv_type : GFARM_AUTH_CRED_TYPE_HOST,
	    serv_service, serv_name,
	    hostname,
	    &acceptor_name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_auth_error(
		    "Server credential configuration for %s:%s: %s",
		    service_tag, hostname, e);
		return (e);
	}
	cred = gfarm_gsi_get_delegated_cred();
	if (cred == GSS_C_NO_CREDENTIAL) { /* if not delegated */
		/*
		 * always re-acquire my credential, otherwise we cannot deal
		 * with credential expiration.
		 */
		if (gfarmGssAcquireCredential(&cred,
		    GSS_C_NO_NAME, GSS_C_INITIATE,
		    &e_major, &e_minor, NULL) < 0) {
			if (gflog_auth_get_verbose()) {
				gflog_error("Can't acquire my credentail "
				    "because of:");
				gfarmGssPrintMajorStatus(e_major);
				gfarmGssPrintMinorStatus(e_minor);
			}
			if (acceptor_name != GSS_C_NO_NAME)
				gfarmGssDeleteName(&acceptor_name, NULL, NULL);
#if 0
			return (GFARM_ERR_AUTHENTICATION);
#else
			/*
			 * We don't return GFARM_ERR_AUTHENTICATION or
			 * GFARM_ERR_EXPIRED here for now,
			 * to prevent the caller -- gfarm_auth_request()
			 * -- from trying next auth_method, because current
			 * server side implmenetation doesn't allow us to
			 * continue gracefully in this case.
			 * So, just kill this connection.
			 */
			return (GFARM_ERR_CANNOT_ACQUIRE_CLIENT_CRED);
#endif
		}
		cred_acquired = 1;
	}
	/* XXX NOTYET deal with self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	session = gfarmSecSessionInitiate(fd, acceptor_name, cred,
	    GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG, NULL, &e_major, &e_minor);
	if (acceptor_name != GSS_C_NO_NAME)
		gfarmGssDeleteName(&acceptor_name, NULL, NULL);
	if (session == NULL) {
		if (gflog_auth_get_verbose()) {
			gflog_error("Can't initiate session because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		if (cred_acquired &&
		    gfarmGssDeleteCredential(&cred, &e_major, &e_minor) < 0 &&
		    gflog_auth_get_verbose()) {
			gflog_error("Can't free my credential because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
#if 0
		/* XXX e_major/e_minor should be used */
		return (GFARM_ERR_AUTHENTICATION);
#else
		/*
		 * We don't return GFARM_ERR_AUTHENTICATION for now,
		 * to prevent the caller -- gfarm_auth_request()
		 * -- from trying next auth_method, because currently
		 * GFSL protocol doesn't guarantee to gracefully continue
		 * further communication on error cases.
		 */
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
#endif
	}
	gfp_xdr_set_secsession(conn, session,
	    cred_acquired ? cred : GSS_C_NO_CREDENTIAL);

	e = gfp_xdr_recv(conn, 1, &eof, "i", &error);
	if (e != GFARM_ERR_NO_ERROR || eof ||
	    error != GFARM_AUTH_ERROR_NO_ERROR) {
		gfp_xdr_reset_secsession(conn);
		gfp_xdr_set_socket(conn, fd);
		return (e != GFARM_ERR_NO_ERROR ? e :
		    eof ? GFARM_ERR_PROTOCOL : GFARM_ERR_AUTHENTICATION);
	}
	return (GFARM_ERR_NO_ERROR);
}

/*
 * multiplexed version of gfarm_auth_request_gsi() for parallel authentication
 */

struct gfarm_auth_request_gsi_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *readable;
	struct gfp_xdr *conn;
	void (*continuation)(void *);
	void *closure;

	gss_name_t acceptor_name;
	gss_cred_id_t cred;
	int cred_acquired;
	struct gfarmSecSessionInitiateState *gfsl_state;
	gfarmSecSession *session;

	/* results */
	gfarm_error_t error;
};

static void
gfarm_auth_request_gsi_receive_result(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfarm_auth_request_gsi_state *state = closure;
	int eof;
	gfarm_int32_t error; /* enum gfarm_auth_error */

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	state->error = gfp_xdr_recv(state->conn, 1, &eof, "i", &error);
	if (state->error == GFARM_ERR_NO_ERROR && eof)
		state->error = GFARM_ERR_PROTOCOL;
	if (state->error == GFARM_ERR_NO_ERROR &&
	    error != GFARM_AUTH_ERROR_NO_ERROR)
		state->error = GFARM_ERR_AUTHENTICATION;
	if (state->error != GFARM_ERR_NO_ERROR) {
		gfp_xdr_reset_secsession(state->conn);
		gfp_xdr_set_socket(state->conn, fd);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_gsi_wait_result(void *closure)
{
	struct gfarm_auth_request_gsi_state *state = closure;
	OM_uint32 e_major, e_minor;
	int rv;
	struct timeval timeout;

	state->session = gfarmSecSessionInitiateResult(state->gfsl_state,
	    &e_major, &e_minor);
	if (state->session == NULL) {
		if (gflog_auth_get_verbose()) {
			gflog_error("Can't initiate session because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
#if 0
		/* XXX e_major/e_minor should be used */
		state->error = GFARM_ERR_AUTHENTICATION;
#else
		/*
		 * We don't return GFARM_ERR_AUTHENTICATION for now,
		 * to prevent the caller -- gfarm_auth_request_next_method()
		 * -- from trying next auth_method, because currently
		 * GFSL protocol doesn't guarantee to gracefully continue
		 * further communication on error cases.
		 */
		state->error = GFARM_ERR_OPERATION_NOT_PERMITTED;
#endif
	} else {
		timeout.tv_sec = GFARM_AUTH_TIMEOUT; timeout.tv_usec = 0;
		rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, &timeout);
		if (rv == 0) {
			gfp_xdr_set_secsession(state->conn,
			    state->session,
			    state->cred_acquired ?
			    state->cred : GSS_C_NO_CREDENTIAL);
			/* go to gfarm_auth_request_gsi_receive_result() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfarm_auth_request_gsi_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	char *service_tag, char *hostname, enum gfarm_auth_id_type self_type,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	gfarm_error_t e;
	struct gfarm_auth_request_gsi_state *state;
	enum gfarm_auth_cred_type serv_type =
	    gfarm_auth_server_cred_type_get(service_tag);
	char *serv_service = gfarm_auth_server_cred_service_get(service_tag);
	char *serv_name = gfarm_auth_server_cred_name_get(service_tag);
	OM_uint32 e_major, e_minor;

	e = gfarm_gsi_client_initialize();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC(state);
	if (state == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, gfp_xdr_fd(conn),
	    gfarm_auth_request_gsi_receive_result, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto error_free_state;
	}

	e = gfarm_gsi_cred_config_convert_to_name(
	    serv_type != GFARM_AUTH_CRED_TYPE_DEFAULT ?
	    serv_type : GFARM_AUTH_CRED_TYPE_HOST,
	    serv_service, serv_name,
	    hostname,
	    &state->acceptor_name);
	if (e != NULL) {
		gflog_auth_error(
		    "Server credential configuration for %s:%s: %s",
		    service_tag, hostname, e);
		goto error_free_readable;
	}

	state->cred_acquired = 0;
	state->cred = gfarm_gsi_get_delegated_cred();
	if (state->cred == GSS_C_NO_CREDENTIAL) { /* if not delegated */
		/*
		 * always re-acquire my credential, otherwise we cannot deal
		 * with credential expiration.
		 */
		if (gfarmGssAcquireCredential(&state->cred,
		    GSS_C_NO_NAME, GSS_C_INITIATE,
		    &e_major, &e_minor, NULL) < 0) {
			if (gflog_auth_get_verbose()) {
				gflog_error("Can't acquire my credentail "
				    "because of:");
				gfarmGssPrintMajorStatus(e_major);
				gfarmGssPrintMinorStatus(e_minor);
			}
#if 0
			e = GFARM_ERR_AUTHENTICATION;
#else
			/*
			 * We don't return GFARM_ERR_AUTHENTICATION or
			 * GFARM_ERR_EXPIRED here for now, to prevent
			 * the caller -- gfarm_auth_request_next_method()
			 * -- from trying next auth_method, because current
			 * server side implmenetation doesn't allow us to
			 * continue gracefully in this case.
			 * So, just kill this connection.
			 */
			e = GFARM_ERR_CANNOT_ACQUIRE_CLIENT_CRED;
#endif
			goto error_free_acceptor_name;
		}
		state->cred_acquired = 1;
	}

	/* XXX NOTYET deal with self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	state->gfsl_state = gfarmSecSessionInitiateRequest(q,
	    gfp_xdr_fd(conn), state->acceptor_name, state->cred,
	    GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG, NULL,
	    gfarm_auth_request_gsi_wait_result, state,
	    &e_major, &e_minor);
	if (state->gfsl_state == NULL) {
		/* XXX e_major/e_minor should be used */
		e = GFARM_ERRMSG_CANNOT_INITIATE_GSI_CONNECTION;
		goto error_free_cred;
	}

	state->q = q;
	state->conn = conn;
	state->continuation = continuation;
	state->closure = closure;
	state->error = GFARM_ERR_NO_ERROR;
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

error_free_cred:
	if (state->cred_acquired &&
	    gfarmGssDeleteCredential(&state->cred, &e_major, &e_minor) < 0 &&
	    gflog_auth_get_verbose()) {
		gflog_error("Can't free my credential because of:");
		gfarmGssPrintMajorStatus(e_major);
		gfarmGssPrintMinorStatus(e_minor);
	}
error_free_acceptor_name:
	if (state->acceptor_name != GSS_C_NO_NAME)
		gfarmGssDeleteName(&state->acceptor_name, NULL, NULL);
error_free_readable:
	gfarm_event_free(state->readable);
error_free_state:
	free(state);
	return (e);
}

gfarm_error_t
gfarm_auth_result_gsi_multiplexed(void *sp)
{
	struct gfarm_auth_request_gsi_state *state = sp;
	gfarm_error_t e = state->error;

	if (state->acceptor_name != GSS_C_NO_NAME)
		gfarmGssDeleteName(&state->acceptor_name, NULL, NULL);
	gfarm_event_free(state->readable);
	free(state);
	return (e);
}

/*
 * "gsi_auth" method
 */

gfarm_error_t
gfarm_auth_request_gsi_auth(struct gfp_xdr *conn,
	char *service_tag, char *hostname, enum gfarm_auth_id_type self_type)
{
	gfarm_error_t e = gfarm_auth_request_gsi(conn,
	    service_tag, hostname, self_type);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}

gfarm_error_t
gfarm_auth_request_gsi_auth_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	char *service_tag, char *hostname, enum gfarm_auth_id_type self_type,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	return (gfarm_auth_request_gsi_multiplexed(q, conn,
	    service_tag, hostname, self_type,
	    continuation, closure, statepp));
}

gfarm_error_t
gfarm_auth_result_gsi_auth_multiplexed(void *sp)
{
	struct gfarm_auth_request_gsi_state *state = sp;
	/* sp will be free'ed in gfarm_auth_result_gsi_multiplexed().
	 * state->conn should be saved before calling it. */
	struct struct gfp_xdr *conn = state->conn;
	gfarm_error_t e = gfarm_auth_result_gsi_multiplexed(sp);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}
