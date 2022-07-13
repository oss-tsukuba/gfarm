#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>

#include <gfarm/gfarm.h>

#include "gfevent.h"

#include "context.h"
#include "gfp_xdr.h"
#include "io_tls.h"
#include "auth.h"

static gfarm_error_t
server_cert_is_ok(struct gfp_xdr *conn, const char *service_tag,
	const char *hostname)
{
	gfarm_error_t e;
	char *peer_cn, *peer_hostname = NULL;

	if ((peer_cn = gfp_xdr_tls_peer_dn_common_name(conn)) == NULL) {
		/* this shouldn't happen, raise alert */
		gflog_auth_warning(GFARM_MSG_UNFIXED,
		    "%s: missing hostname in TLS server certificate",
		    hostname);
		return (GFARM_ERR_HOSTNAME_MISMATCH);
	} else if ((e = gfarm_x509_cn_get_service_hostname(
	    service_tag, peer_cn, &peer_hostname)) != GFARM_ERR_NO_ERROR) {
		/* server cert is invalid? raise alert */
		gflog_auth_warning(GFARM_MSG_UNFIXED,
		    "%s: '%s' service is expected "
		    "in TLS server certificate <%s>",
		    hostname, service_tag, peer_cn);
		return (GFARM_ERR_HOSTNAME_MISMATCH);
	}

	if (strcasecmp(peer_hostname, hostname) != 0) {
		/* server cert is invalid? raise alert */
		gflog_auth_warning(GFARM_MSG_UNFIXED,
		    "%s: %s service - host '%s' is expected but '%s' "
		    "in TLS server certificate <%s>",
		    hostname, service_tag, hostname, peer_hostname, peer_cn);
		e = GFARM_ERR_HOSTNAME_MISMATCH;
	} else {
		e = GFARM_ERR_NO_ERROR;
	}
	free(peer_hostname);

	return (e);
}

/*
 * auth_client_tls_sharedsecret
 */
gfarm_error_t
gfarm_auth_request_tls_sharedsecret(struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd)
{
	gfarm_error_t e;
	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_INITIATE);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		return (e);
	}

	e = server_cert_is_ok(conn, service_tag, hostname);

	e = gfarm_auth_request_sharedsecret_common(
	    conn, service_tag, hostname, self_type, user, pwd,
	    e == GFARM_ERR_NO_ERROR);

	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		gfp_xdr_tls_reset(conn);
		return (e);
	}
	return (e);
}

struct gfarm_auth_request_tls_sharedsecret_state {
	struct gfp_xdr *conn;
	void *sharedsecret_state;
};

gfarm_error_t
gfarm_auth_request_tls_sharedsecret_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	gfarm_error_t e;
	struct gfarm_auth_request_tls_sharedsecret_state *state;

	GFARM_MALLOC(state);
	if (state == NULL) {
		/* XXX this is NOT graceful */
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfarm_auth_request_tls_sharedsecret_multiplexed: %s",
		    gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	state->conn = conn;

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn), GFP_XDR_TLS_INITIATE);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		free(state);
		return (e);
	}

	e = server_cert_is_ok(conn, service_tag, hostname);

	e = gfarm_auth_request_sharedsecret_common_multiplexed(
	    q, conn, service_tag, hostname, self_type, user, pwd,
	    e == GFARM_ERR_NO_ERROR, auth_timeout,
	    continuation, closure, &state->sharedsecret_state);

	if (e == GFARM_ERR_NO_ERROR) {
		*statepp = state;
	} else {
		/* is this case graceful? */
		gfp_xdr_tls_reset(conn);
		free(state);
	}
	return (e);
}

gfarm_error_t
gfarm_auth_result_tls_sharedsecret_multiplexed(void *sp)
{
	struct gfarm_auth_request_tls_sharedsecret_state *state = sp;
	gfarm_error_t e;

	e = gfarm_auth_result_sharedsecret_multiplexed(
	    state->sharedsecret_state);

	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		gfp_xdr_tls_reset(state->conn);
	}
	free(state);
	return (e);
}

/*
 * auth_client_tls_client_certificate
 */

gfarm_error_t
gfarm_auth_request_tls_client_certificate(struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd)
{
	gfarm_error_t e;
	int eof;
	gfarm_int32_t req; /* enum gfarm_auth_tls_client_certificate_request */
	gfarm_int32_t arg; /* gfarm_error_t or enum gfarm_auth_id_type */
	gfarm_int32_t result; /* enum gfarm_auth_error */

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn),
	    GFP_XDR_TLS_INITIATE | GFP_XDR_TLS_CLIENT_AUTHENTICATION |
	    (gfarm_ctxp->tls_proxy_certificate ?
	     GFP_XDR_TLS_CLIENT_USE_PROXY_CERTIFICATE : 0));
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		return (e);
	}

	e = server_cert_is_ok(conn, service_tag, hostname);
	if (e == GFARM_ERR_NO_ERROR) {
		req = GFARM_AUTH_TLS_CLIENT_CERTIFICATE_CLIENT_TYPE;
		arg = self_type;
	} else {
		req = GFARM_AUTH_TLS_CLIENT_CERTIFICATE_GIVEUP;
		arg = e;
	}

	e = gfp_xdr_send(conn, "ii", req, arg);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		/* this is not gfarceful, but OK because of a network error */
		gflog_debug(GFARM_MSG_UNFIXED,
		    "sending self_type failed: %s", gfarm_error_string(e));
	}
	if (req != GFARM_AUTH_TLS_CLIENT_CERTIFICATE_CLIENT_TYPE) {
		/* giveup, due to server cert problem */
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_HOSTNAME_MISMATCH);
	}

	if ((e = gfp_xdr_recv(conn, 1, &eof, "i", &result))
	    != GFARM_ERR_NO_ERROR || eof) {
		if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
			e = GFARM_ERR_UNEXPECTED_EOF;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "receiving tls_client_certificate result failed: %s",
		    gfarm_error_string(e));
	} else if (result != GFARM_AUTH_ERROR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "Authentication failed: %d", (int)result);
		e = GFARM_ERR_AUTHENTICATION;
	} else {
		/* e == GFARM_ERR_NO_ERROR */
	}

	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (e);
	}
	return (e);
}

struct gfarm_auth_request_tls_client_certificate_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *readable;
	struct gfp_xdr *conn;
	int auth_timeout;
	void (*continuation)(void *);
	void *closure;

	/* results */
	gfarm_error_t error;
	gfarm_int32_t result; /* enum gfarm_auth_error */
};

static void
gfarm_auth_request_tls_client_certificate_receive_result(int events, int fd,
	void *closure, const struct timeval *t)
{
	struct gfarm_auth_request_tls_client_certificate_state *state =
	    closure;
	int eof;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "receiving tls_client_certificate result failed: %s",
		    gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	state->error = gfp_xdr_recv(state->conn, 1, &eof, "i", &state->result);
	if (state->error != GFARM_ERR_NO_ERROR || eof) {
		if (state->error == GFARM_ERR_NO_ERROR) /* i.e. eof */
			state->error = GFARM_ERR_UNEXPECTED_EOF;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "receiving tls_client_certificate result failed: %s",
		    gfarm_error_string(state->error));
	} else if (state->result != GFARM_AUTH_ERROR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "Authentication failed: %d", (int)state->result);
		state->error = GFARM_ERR_AUTHENTICATION;
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfarm_auth_request_tls_client_certificate_multiplexed(
	struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	gfarm_error_t e;
	gfarm_int32_t req; /* enum gfarm_auth_tls_client_certificate_request */
	gfarm_int32_t arg; /* gfarm_error_t or enum gfarm_auth_id_type */
	struct gfarm_auth_request_tls_client_certificate_state *state;
	int rv;

	GFARM_MALLOC(state);
	if (state == NULL) {
		/* XXX this is NOT graceful */
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfarm_auth_request_tls_client_certificate_multiplexed: "
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	e = gfp_xdr_tls_alloc(conn, gfp_xdr_fd(conn),
	    GFP_XDR_TLS_INITIATE | GFP_XDR_TLS_CLIENT_AUTHENTICATION |
	    (gfarm_ctxp->tls_proxy_certificate ?
	     GFP_XDR_TLS_CLIENT_USE_PROXY_CERTIFICATE : 0));
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		free(state);
		return (e);
	}

	e = server_cert_is_ok(conn, service_tag, hostname);
	if (e == GFARM_ERR_NO_ERROR) {
		req = GFARM_AUTH_TLS_CLIENT_CERTIFICATE_CLIENT_TYPE;
		arg = self_type;
	} else {
		req = GFARM_AUTH_TLS_CLIENT_CERTIFICATE_GIVEUP;
		arg = e;
	}

	e = gfp_xdr_send(conn, "ii", req, arg);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		/* this is not gfarceful, but OK because of a network error */
		gflog_debug(GFARM_MSG_UNFIXED,
		    "sending self_type failed: %s", gfarm_error_string(e));
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		free(state);
		return (e);
	}
	if (req != GFARM_AUTH_TLS_CLIENT_CERTIFICATE_CLIENT_TYPE) {
		/* giveup, due to server cert problem */
		free(state);
		gfp_xdr_tls_reset(conn); /* is this case graceful? */
		return (GFARM_ERR_HOSTNAME_MISMATCH);
	}

	/*
	 * We cannt use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, gfp_xdr_fd(conn),
	    gfp_xdr_recv_is_ready_call, conn,
	    gfarm_auth_request_tls_client_certificate_receive_result, state);
	if (state->readable == NULL) {
		/* XXX this is NOT graceful */
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "allocation of 'readable' failed: %s",
		    gfarm_error_string(e));
	} else if ((rv = gfarm_eventqueue_add_event(q, state->readable, NULL))
	    != 0) {
		e = gfarm_errno_to_error(rv);
		/* XXX this is NOT graceful */
		gflog_debug(GFARM_MSG_UNFIXED, "addition of event failed: %s",
		    strerror(rv));
	} else {
		state->q = q;
		state->conn = conn;
		state->auth_timeout = auth_timeout;
		state->continuation = continuation;
		state->closure = closure;
		state->error = GFARM_ERR_NO_ERROR;
		state->result = GFARM_AUTH_ERROR_NO_ERROR;
		*statepp = state;
		return (GFARM_ERR_NO_ERROR);
	}

	if (state->readable != NULL)
		gfarm_event_free(state->readable);

	/* is this case graceful? */
	gfp_xdr_tls_reset(conn);
	free(state);
	return (e);
}

gfarm_error_t
gfarm_auth_result_tls_client_certificate_multiplexed(void *sp)
{
	struct gfarm_auth_request_tls_client_certificate_state *state = sp;
	gfarm_error_t e = state->error;

	gfarm_event_free(state->readable);
	if (e != GFARM_ERR_NO_ERROR) {
		/* is this case graceful? */
		gfp_xdr_tls_reset(state->conn);
	}
	free(state);
	return (e);
}
