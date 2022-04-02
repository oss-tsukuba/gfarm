#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include <gfarm/gfarm.h>

#include <sasl/sasl.h>

#include "gfutil.h"
#include "gfevent.h"

#include "context.h"
#include "gfp_xdr.h"
#include "io_tls.h"
#include "auth.h"

#define staticp	(gfarm_ctxp->auth_sasl_client_static)

struct gfarm_auth_sasl_client_static {
	gfarm_error_t sasl_client_initialized;
	sasl_secret_t *sasl_secret;
};

gfarm_error_t
gfarm_auth_request_sasl_common(struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, const char *diag)
{
	gfarm_error_t e;
	int save_errno, eof, r;
	char self_hsbuf[NI_MAXHOST + NI_MAXSERV];
	char peer_hsbuf[NI_MAXHOST + NI_MAXSERV];
	sasl_conn_t *sasl_conn;
	char *mechanism_candidates = NULL;
	const char *chosen_mechanism = NULL;
	gfarm_int32_t error, step_type;
	const char *data = NULL;
	unsigned len;

	/* sanity check, shouldn't happen */
	if (staticp->sasl_client_initialized != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED, "sasl_client_initialized: %s",
		    gfarm_error_string(staticp->sasl_client_initialized));
		return (staticp->sasl_client_initialized);
	}

	save_errno = gfarm_sasl_addr_string(gfp_xdr_fd(conn),
	    self_hsbuf, sizeof(self_hsbuf),
	    peer_hsbuf, sizeof(peer_hsbuf), diag);
	if (save_errno != 0)
		return (gfarm_errno_to_error(save_errno));

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_INITIATE);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		return (e);
	}

	/*
	 * negotiation protocol phase 1:
	 *
	 * client: i:server_authentication_result
	 */
	error = gfarm_tls_server_cert_is_ok(conn, service_tag, hostname);

	if (error == GFARM_ERR_NO_ERROR) {
		r = sasl_client_new("gfarm", hostname, self_hsbuf, peer_hsbuf,
		    NULL, 0, &sasl_conn);
		if (r != SASL_OK) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "%s: sasl_client_new(): %s",
			    hostname, sasl_errstring(r, NULL, NULL));
			error = GFARM_ERR_AUTHENTICATION;
		}
	}

	e = gfp_xdr_send(conn, "i", error);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (error != GFARM_ERR_NO_ERROR || e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (error != GFARM_ERR_NO_ERROR ?
		    GFARM_ERR_AUTHENTICATION : e);
	}


	/*
	 * negotiation protocol phase 2:
	 *
	 * server: s:mechanisms
	 * client: s:chosen_mechanism
	 * client: i:does_client_initial_response_exist?
	 * client:(b:client_initial_response) ... optional
	 */
	e = gfp_xdr_recv(conn, 1, &eof, "s", &mechanism_candidates);
	if (e != GFARM_ERR_NO_ERROR || eof) {
		if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
			e = GFARM_ERR_UNEXPECTED_EOF;
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (e);
	}

	if (gfarm_ctxp->sasl_mechanisms != NULL &&
	    strstr(mechanism_candidates, gfarm_ctxp->sasl_mechanisms)
	    == NULL) {
		/* XXX FIXME: this is NOT graceful*/
		free(mechanism_candidates);
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	r = sasl_client_start(sasl_conn,
	    gfarm_ctxp->sasl_mechanisms != NULL ?
	    gfarm_ctxp->sasl_mechanisms : mechanism_candidates,
	    NULL, &data, &len, &chosen_mechanism);
	free(mechanism_candidates);
	if (r != SASL_OK && r != SASL_CONTINUE) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: sasl_client_start(): %s",
			    hostname, sasl_errstring(r, NULL, NULL));
		}
		sasl_dispose(&sasl_conn);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}

	if (gflog_auth_get_verbose()) {
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: SASL using mechanism %s", hostname, chosen_mechanism);
	}

	if (data != NULL) {
		e = gfp_xdr_send(conn, "sib",
		    chosen_mechanism, (gfarm_int32_t)1, (size_t)len, data);
	} else {
		e = gfp_xdr_send(conn, "si",
		    chosen_mechanism, (gfarm_int32_t)0);
	}
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);

	for (;;) {
		char *response = NULL;
		size_t rsz;

		e = gfp_xdr_recv(conn, 1, &eof, "i", &step_type);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_UNEXPECTED_EOF;
			sasl_dispose(&sasl_conn);
			gfp_xdr_tls_reset(conn); /* is this case graceful? */
			return (e);
		}
		if (step_type != GFARM_AUTH_SASL_STEP_CONTINUE)
			break;
		e = gfp_xdr_recv(conn, 1, &eof, "B", &rsz, &response);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_UNEXPECTED_EOF;
			sasl_dispose(&sasl_conn);
			gfp_xdr_tls_reset(conn); /* is this case graceful? */
			return (e);
		}
		r = sasl_client_step(sasl_conn, response, rsz, NULL,
		    &data, &len);
		if (data == NULL)
			len = 0; /* defensive programming */
		free(response);
		if (r != SASL_OK && r != SASL_CONTINUE) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: sasl_client_step(): %s",
			    hostname, sasl_errstring(r, NULL, NULL));
			sasl_dispose(&sasl_conn);
			gfp_xdr_tls_reset(conn); /* is this case graceful? */
			return (GFARM_ERR_AUTHENTICATION);
		}
		e = gfp_xdr_send(conn, "b", (size_t)len, data);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		if (e != GFARM_ERR_NO_ERROR) {
			sasl_dispose(&sasl_conn);
			gfp_xdr_tls_reset(conn); /* is this case graceful? */
			return (e);
		}
	}

	sasl_dispose(&sasl_conn);
	if (step_type == GFARM_AUTH_SASL_STEP_DONE) {
		return (GFARM_ERR_NO_ERROR);
	} else {
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_AUTHENTICATION);
	}
}

/*
 * multiplexed version of gfarm_auth_request_sasl()
 * for parallel authentication
 */

struct gfarm_auth_request_sasl_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *readable, *writable;
	struct gfp_xdr *conn;
	const char *service_tag, *hostname;
	int auth_timeout;
	void (*continuation)(void *);
	void *closure;
	const char *diag;

	sasl_conn_t *sasl_conn;
	const char *chosen_mechanism; /* sasl_conn owns this data */
	const char *data; /* sasl_conn owns this data */
	unsigned len;

	/* results */
	gfarm_error_t error;
};

static void
gfarm_auth_request_sasl_step(int events, int fd, void *closure,
	const struct timeval *t)
{
	gfarm_error_t e;
	struct gfarm_auth_request_sasl_state *state = closure;
	int rv, r, eof;
	gfarm_int32_t step_type;
	char *response;
	size_t rsz;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: receiving step_type: %s",
		    state->diag, gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}

	assert(events == GFARM_EVENT_READ);
	e = gfp_xdr_recv(state->conn, 1, &eof, "i", &step_type);
	if (e == GFARM_ERR_NO_ERROR && eof)
		e = GFARM_ERR_UNEXPECTED_EOF;

	if (e != GFARM_ERR_NO_ERROR) {
		state->error = e;
	} else if (step_type == GFARM_AUTH_SASL_STEP_DONE) {
		/* leave state->error as is. i.e. GFARM_ERR_NO_ERROR */
	} else if (step_type != GFARM_AUTH_SASL_STEP_CONTINUE) {
		state->error = GFARM_ERR_AUTHENTICATION;
	} else if ((e = gfp_xdr_recv(state->conn, 1, &eof, "B",
	    &rsz, &response)) != GFARM_ERR_NO_ERROR || eof) {
		if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
			e = GFARM_ERR_UNEXPECTED_EOF;
		state->error = e;
	} else {
		r = sasl_client_step(state->sasl_conn, response, rsz, NULL,
		    &state->data, &state->len);
		if (state->data == NULL)
			state->len = 0; /* defensive programming */
		free(response);
		if (r != SASL_OK && r != SASL_CONTINUE) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: sasl_client_step(): %s",
			    state->hostname, sasl_errstring(r, NULL, NULL));
			state->error = GFARM_ERR_AUTHENTICATION;
		} else if ((e = gfp_xdr_send(state->conn, "b",
		    (size_t)state->len, state->data)) != GFARM_ERR_NO_ERROR ||
		    (e = gfp_xdr_flush(state->conn)) != GFARM_ERR_NO_ERROR) {
			state->error = e;
		} else {
			struct timeval timeout;

			timeout.tv_sec = state->auth_timeout;
			timeout.tv_usec = 0;
			if ((rv = gfarm_eventqueue_add_event(state->q,
			    state->readable, &timeout)) == 0) {
				/*
				 * go to gfarm_auth_request_sasl_step()
				 */
				return;
			}
			state->error = gfarm_errno_to_error(rv);
		}
	}

	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_sasl_send_chosen_mechanism(int events, int fd,
	void *closure, const struct timeval *t)
{
	gfarm_error_t e;
	struct gfarm_auth_request_sasl_state *state = closure;
	int rv;

	/*
	 * negotiation protocol phase 2:
	 *
	 * client: s:chosen_mechanism
	 * client: i:does_client_initial_response_exist?
	 * client:(b:client_initial_response) ... optional
	 */

	if (state->data != NULL) {
		e = gfp_xdr_send(state->conn, "sib",
		    state->chosen_mechanism, (gfarm_int32_t)1,
		    (size_t)state->len, state->data);
	} else {
		e = gfp_xdr_send(state->conn, "si",
		    state->chosen_mechanism, (gfarm_int32_t)0);
	}
	if (e != GFARM_ERR_NO_ERROR ||
	    (e = gfp_xdr_flush(state->conn)) != GFARM_ERR_NO_ERROR) {
		state->error = e;
	} else {
		struct timeval timeout;

		gfarm_fd_event_set_callback(state->readable,
		    gfarm_auth_request_sasl_step, state);
		timeout.tv_sec = state->auth_timeout; timeout.tv_usec = 0;
		if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, &timeout)) == 0) {
			/*
			 * go to gfarm_auth_request_sasl_step()
			 */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_sasl_receive_mechanisms(int events, int fd, void *closure,
	const struct timeval *t)
{
	gfarm_error_t e;
	struct gfarm_auth_request_sasl_state *state = closure;
	int rv, r, eof;
	char *mechanism_candidates = NULL;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: receiving mechanisms: %s",
		    state->diag, gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}

	/*
	 * negotiation protocol phase 2:
	 *
	 * server: s:mechanisms
	 */

	assert(events == GFARM_EVENT_READ);
	e = gfp_xdr_recv(state->conn, 1, &eof, "s", &mechanism_candidates);
	if (e == GFARM_ERR_NO_ERROR && eof)
		e = GFARM_ERR_UNEXPECTED_EOF;

	if (e != GFARM_ERR_NO_ERROR) {
		state->error = e;
	} else if (gfarm_ctxp->sasl_mechanisms != NULL &&
	    strstr(mechanism_candidates, gfarm_ctxp->sasl_mechanisms)
	    == NULL) {
		/* XXX FIXME: this is NOT graceful*/
		free(mechanism_candidates);
		state->error = GFARM_ERR_AUTHENTICATION;
	} else {
		gfarm_fd_event_set_callback(state->writable,
		    gfarm_auth_request_sasl_send_chosen_mechanism, state);

		r = sasl_client_start(state->sasl_conn,
		    gfarm_ctxp->sasl_mechanisms != NULL ?
		    gfarm_ctxp->sasl_mechanisms : mechanism_candidates,
		    NULL, &state->data, &state->len,
		    &state->chosen_mechanism);
		free(mechanism_candidates);
		if (r != SASL_OK && r != SASL_CONTINUE) {
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "%s: sasl_client_start(): %s",
				    state->hostname,
				    sasl_errstring(r, NULL, NULL));
			}
			state->error = GFARM_ERR_AUTHENTICATION;
		} else if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->writable, NULL)) != 0) {
			state->error = gfarm_errno_to_error(rv);
		} else {
			/*
			 * go to
			 * gfarm_auth_request_sasl_send_chosen_mechanism()
			 */
			return;
		}
	}

	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_sasl_send_server_auth_result(int events, int fd,
	void *closure, const struct timeval *t)
{
	gfarm_error_t e;
	struct gfarm_auth_request_sasl_state *state = closure;
	int rv;
	gfarm_int32_t error;

	/*
	 * negotiation protocol phase 1:
	 *
	 * client: i:server_authentication_result
	 */
	error = gfarm_tls_server_cert_is_ok(
	    state->conn, state->service_tag, state->hostname);

	if (error == GFARM_ERR_NO_ERROR) {
		int save_errno, r;
		char self_hsbuf[NI_MAXHOST + NI_MAXSERV];
		char peer_hsbuf[NI_MAXHOST + NI_MAXSERV];

		save_errno = gfarm_sasl_addr_string(gfp_xdr_fd(state->conn),
		    self_hsbuf, sizeof(self_hsbuf),
		    peer_hsbuf, sizeof(peer_hsbuf), state->diag);
		if (save_errno != 0) {
			error = gfarm_errno_to_error(save_errno);
		} else if ((r = sasl_client_new("gfarm", state->hostname,
		    self_hsbuf, peer_hsbuf, NULL, 0, &state->sasl_conn))
		    != SASL_OK) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "%s: sasl_client_new(): %s",
			    state->hostname, sasl_errstring(r, NULL, NULL));
			error = GFARM_ERR_AUTHENTICATION;
		}
	}

	e = gfp_xdr_send(state->conn, "i", error);
	if (e != GFARM_ERR_NO_ERROR ||
	    (e = gfp_xdr_flush(state->conn)) != GFARM_ERR_NO_ERROR) {
		state->error = e;
	} else if (error != GFARM_ERR_NO_ERROR) {
		state->error = GFARM_ERR_AUTHENTICATION;
	} else {
		struct timeval timeout;

		timeout.tv_sec = state->auth_timeout; timeout.tv_usec = 0;
		if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, &timeout)) == 0) {
			/*
			 * go to gfarm_auth_request_sasl_receive_mechanisms()
			 */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfarm_auth_request_sasl_common_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp, const char *diag)
{
	gfarm_error_t e;
	struct gfarm_auth_request_sasl_state *state;
	int rv;

	/* sanity check, shouldn't happen */
	if (staticp->sasl_client_initialized != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED, "sasl_client_initialized: %s",
		    gfarm_error_string(staticp->sasl_client_initialized));
		return (staticp->sasl_client_initialized);
	}

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_INITIATE);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		return (e);
	}

	GFARM_MALLOC(state);
	if (state == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: auth state allocation: %s",
		    diag, gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	state->writable = gfarm_fd_event_alloc(
	    GFARM_EVENT_WRITE, gfp_xdr_fd(conn), NULL, NULL,
	    gfarm_auth_request_sasl_send_server_auth_result, state);
	if (state->writable == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: writable event allocation: %s",
		    diag, gfarm_error_string(GFARM_ERR_NO_MEMORY));
		free(state);
		return (GFARM_ERR_NO_MEMORY);
	}
	/*
	 * We cannt use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, gfp_xdr_fd(conn),
	    gfp_xdr_recv_is_ready_call, conn,
	    gfarm_auth_request_sasl_receive_mechanisms, state);
	if (state->readable == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: readable event allocation: %s",
		    diag, gfarm_error_string(GFARM_ERR_NO_MEMORY));
		free(state);
		return (GFARM_ERR_NO_MEMORY);
	}
	/* go to gfarm_auth_request_sasl_send_server_auth_result() */
	rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
	if (rv != 0) {
		e = gfarm_errno_to_error(rv);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: gfarm_eventqueue_add_event: %s",
		    diag, gfarm_error_string(e));
		free(state);
		return (GFARM_ERR_NO_MEMORY);
	}

	state->q = q;
	state->conn = conn;
	state->service_tag = service_tag;
	state->hostname = hostname;
	state->auth_timeout = auth_timeout;
	state->continuation = continuation;
	state->closure = closure;
	state->diag = diag;
	state->sasl_conn = NULL;
	state->error = GFARM_ERR_NO_ERROR;
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_auth_result_sasl_common_multiplexed(void *sp)
{
	struct gfarm_auth_request_sasl_state *state = sp;
	gfarm_error_t e = state->error;

	if (state->sasl_conn != NULL)
		sasl_dispose(&state->sasl_conn);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		gfp_xdr_tls_reset(state->conn);
	}
	gfarm_event_free(state->readable);
	gfarm_event_free(state->writable);
	free(state);
	return (e);
}

/*
 * "sasl" method
 */

gfarm_error_t
gfarm_auth_request_sasl(struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd)
{
	return (gfarm_auth_request_sasl_common(conn,
	    service_tag, hostname, self_type, user, pwd,
	    "gfarm_auth_request_sasl"));
}

gfarm_error_t
gfarm_auth_request_sasl_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	return (gfarm_auth_request_sasl_common_multiplexed(q, conn,
	    service_tag, hostname, self_type, user, pwd, auth_timeout,
	    continuation, closure, statepp,
	    "gfarm_auth_request_sasl_multiplexed"));
}

gfarm_error_t
gfarm_auth_result_sasl_multiplexed(void *sp)
{
	return (gfarm_auth_result_sasl_common_multiplexed(sp));
}

/*
 * "sasl_auth" method
 */

gfarm_error_t
gfarm_auth_request_sasl_auth(struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd)
{
	gfarm_error_t e = gfarm_auth_request_sasl_common(conn,
	    service_tag, hostname, self_type, user, pwd,
	    "gfarm_auth_request_sasl_auth");

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_from_tls_to_fd(conn);
	return (e);
}

gfarm_error_t
gfarm_auth_request_sasl_auth_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	return (gfarm_auth_request_sasl_common_multiplexed(q, conn,
	    service_tag, hostname, self_type, user, pwd, auth_timeout,
	    continuation, closure, statepp,
	    "gfarm_auth_request_sasl_auth_multiplexed"));
}

gfarm_error_t
gfarm_auth_result_sasl_auth_multiplexed(void *sp)
{
	struct gfarm_auth_request_sasl_state *state = sp;
	/* sp will be free'ed in gfarm_auth_result_sasl_multiplexed().
	 * state->conn should be saved before calling it. */
	struct gfp_xdr *conn = state->conn;
	gfarm_error_t e = gfarm_auth_result_sasl_common_multiplexed(sp);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_from_tls_to_fd(conn);
	return (e);
}

static int
sasl_getrealm(void *context, int id, const char **availrealms,
	const char **resultp)
{
	const char *r;

	/* sanity check */
	if (resultp == NULL)
		return (SASL_BADPARAM);

	switch (id) {
	case SASL_CB_GETREALM:
		if (gflog_auth_get_verbose()) {
			gflog_info(GFARM_MSG_UNFIXED,
			    "SASL: available realms:");
			while (*availrealms) {
				gflog_info(GFARM_MSG_UNFIXED,
				    "SASL: available realm <%s>",
				    *availrealms);
				availrealms++;
			}
		}
		r = gfarm_ctxp->sasl_realm;
		if (r == NULL) {
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "sasl_realm: not set");
			}
			return (SASL_FAIL);
		}
		break;
	default:
		return (SASL_BADPARAM);
	}

	*resultp = r;

	return (SASL_OK);
}

static int
sasl_getsimple(void *context, int id, const char **resultp, unsigned *lenp)
{
	const char *r;

	/* sanity check */
	if (resultp == NULL)
		return (SASL_BADPARAM);

	/* use same user name as authcid and authzid for now */
	switch (id) {
	case SASL_CB_AUTHNAME: /* authcid: authentication id */
		r = gfarm_ctxp->sasl_user;
		if (r == NULL) {
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "sasl_authname: not set");
			}
			return (SASL_FAIL);
		}
		break;
	case SASL_CB_USER: /* authzid: authorization id */
		r = gfarm_ctxp->sasl_user;
		if (r == NULL) {
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "sasl_user: not set");
			}
			return (SASL_FAIL);
		}
		break;
	default:
		return (SASL_BADPARAM);
	}

	*resultp = r;
	if (lenp != NULL)
		*lenp = strlen(r);

	return (SASL_OK);
}

static int
sasl_getsecret(
	sasl_conn_t *conn, void *context, int id, sasl_secret_t **resultp)
{
	char *rs;
	size_t sz, len;
	sasl_secret_t *r = NULL;
	int overflow = 0;

	/* sanity check */
	if (conn == NULL || resultp == NULL)
		return (SASL_BADPARAM);

	switch (id) {
	case SASL_CB_PASS:
		rs = gfarm_ctxp->sasl_password;
		if (rs == NULL) {
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "sasl_password: not set");
			}
			return (SASL_FAIL);
		}
		break;
	default:
		return (SASL_BADPARAM);
	}

	len = strlen(rs);
	/* sizeof(*r) includes storage for terminating '\0' */
	sz = gfarm_size_add(&overflow, sizeof(*r), len);
	if (!overflow)
		r = malloc(sz);
	if (overflow || r == NULL)
		return (SASL_NOMEM);
	r->len = len;
	strcpy((char *)r->data, rs);

	*resultp = r;

	return (SASL_OK);
}

gfarm_error_t
gfarm_auth_client_sasl_static_init(struct gfarm_context *ctxp)
{
	static sasl_callback_t callbacks[] = {
		{ SASL_CB_LOG, (int(*)(void))gfarm_sasl_log, NULL },
		{ SASL_CB_GETREALM, (int(*)(void))sasl_getrealm, NULL },
		{ SASL_CB_USER, (int(*)(void))sasl_getsimple, NULL },
		{ SASL_CB_AUTHNAME, (int(*)(void))sasl_getsimple, NULL },
		{ SASL_CB_PASS, (int(*)(void))sasl_getsecret, NULL },
		{ SASL_CB_LIST_END, NULL, NULL },
	};
	struct gfarm_auth_sasl_client_static *s;
	int r;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	ctxp->auth_sasl_client_static = s;
	r = sasl_client_init(callbacks);

	s->sasl_secret = NULL;

	if (r != SASL_OK) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "sasl_client_init(): %s",
			    sasl_errstring(r, NULL, NULL));
		}
		s->sasl_client_initialized = GFARM_ERR_UNKNOWN;
		/* SASL won't work, but this is not a fatal error */
		return (GFARM_ERR_NO_ERROR);
	}

	s->sasl_client_initialized = GFARM_ERR_NO_ERROR;

	return (GFARM_ERR_NO_ERROR);
}

int
gfarm_auth_client_method_sasl_available(void)
{
	return (staticp->sasl_client_initialized == GFARM_ERR_NO_ERROR);
}

void
gfarm_auth_client_sasl_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_auth_sasl_client_static *s =
	    ctxp->auth_sasl_client_static;

	free(s);
	ctxp->auth_sasl_client_static = NULL;
}
