#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/time.h>

#include <gssapi.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "gfevent.h"

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include "xxx_proto.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"
#include "auth_gsi.h"

/*
 * client side authentication
 */

char *
gfarm_auth_request_gsi(struct xxx_connection *conn,
	char *service_tag, char *hostname)
{
	int fd = xxx_connection_fd(conn);
	enum gfarm_auth_cred_type serv_type =
	    gfarm_auth_server_cred_type_get(service_tag);
	char *serv_service = gfarm_auth_server_cred_service_get(service_tag);
	char *serv_name = gfarm_auth_server_cred_name_get(service_tag);
	char *e;
	char *acceptor_name;
	gss_OID acceptor_name_type;
	int need_free;
	OM_uint32 e_major;
	OM_uint32 e_minor;
	gfarmSecSession *session;
	gfarm_int32_t error; /* enum gfarm_auth_error */
	int eof;

	e = gfarm_gsi_client_initialize();
	if (e != NULL)
		return (e);

	e = gfarm_gsi_cred_config_convert_to_name_and_type(
	    serv_type != GFARM_AUTH_CRED_TYPE_DEFAULT ?
	    serv_type : GFARM_AUTH_CRED_TYPE_HOST,
	    serv_service, serv_name,
	    hostname,
	    &acceptor_name, &acceptor_name_type, &need_free);
	if (e != NULL) {
		gflog_auth_error("Server credential configuration for",
		    service_tag);
		gflog_auth_error(e, hostname);
		return (e);
	}
	session = gfarmSecSessionInitiate(fd,
	    acceptor_name, acceptor_name_type,
	    gfarm_gsi_get_delegated_cred(), NULL, &e_major, &e_minor);
	if (need_free)
		free(acceptor_name);
	if (session == NULL) {
		if (gflog_auth_get_verbose()) {
			gflog_error("Can't initiate session because of:",
				    NULL);
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		return (GFARM_ERR_AUTHENTICATION);
	}
	xxx_connection_set_secsession(conn, session);

	e = xxx_proto_recv(conn, 1, &eof, "i", &error);
	if (e != NULL || eof || error != GFARM_AUTH_ERROR_NO_ERROR) {
		xxx_connection_reset_secsession(conn);
		xxx_connection_set_fd(conn, fd);
		return (e != NULL ? e :
		    eof ? GFARM_ERR_PROTOCOL : GFARM_ERR_AUTHENTICATION);
	}
	return (NULL);
}

/*
 * multiplexed version of gfarm_auth_request_gsi() for parallel authentication
 */

struct gfarm_auth_request_gsi_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *readable;
	struct xxx_connection *conn;
	void (*continuation)(void *);
	void *closure;

	struct gfarmSecSessionInitiateState *gfsl_state;
	gfarmSecSession *session;

	/* results */
	char *error;
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
		state->error = GFARM_ERR_CONNECTION_TIMED_OUT;
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	state->error = xxx_proto_recv(state->conn, 1, &eof, "i", &error);
	if (state->error == NULL && eof)
		state->error = GFARM_ERR_PROTOCOL;
	if (state->error == NULL && error != GFARM_AUTH_ERROR_NO_ERROR)
		state->error = GFARM_ERR_AUTHENTICATION;
	if (state->error != NULL) {
		xxx_connection_reset_secsession(state->conn);
		xxx_connection_set_fd(state->conn, fd);
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
		/* XXX e_major/e_minor should be used */
		state->error = GFARM_ERR_AUTHENTICATION;
	} else {
		timeout.tv_sec = GFARM_AUTH_TIMEOUT; timeout.tv_usec = 0;
		rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, &timeout);
		if (rv == 0) {
			xxx_connection_set_secsession(state->conn,
			    state->session);
			/* go to gfarm_auth_request_gsi_receive_result() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

char *
gfarm_auth_request_gsi_multiplexed(struct gfarm_eventqueue *q,
	struct xxx_connection *conn,
	char *service_tag, char *hostname,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	char *e;
	struct gfarm_auth_request_gsi_state *state;
	enum gfarm_auth_cred_type serv_type =
	    gfarm_auth_server_cred_type_get(service_tag);
	char *serv_service = gfarm_auth_server_cred_service_get(service_tag);
	char *serv_name = gfarm_auth_server_cred_name_get(service_tag);
	char *acceptor_name;
	gss_OID acceptor_name_type;
	int need_free;
	OM_uint32 e_major, e_minor;

	e = gfarm_gsi_client_initialize();
	if (e != NULL)
		return (e);

	state = malloc(sizeof(*state));
	if (state == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, xxx_connection_fd(conn),
	    gfarm_auth_request_gsi_receive_result, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto error_free_state;
	}

	e = gfarm_gsi_cred_config_convert_to_name_and_type(
	    serv_type != GFARM_AUTH_CRED_TYPE_DEFAULT ?
	    serv_type : GFARM_AUTH_CRED_TYPE_HOST,
	    serv_service, serv_name,
	    hostname,
	    &acceptor_name, &acceptor_name_type, &need_free);
	if (e != NULL) {
		gflog_auth_error("Server credential configuration for",
		    service_tag);
		gflog_auth_error(e, hostname);
		goto error_free_readable;
	}
	state->gfsl_state = gfarmSecSessionInitiateRequest(q,
	    xxx_connection_fd(conn), acceptor_name, acceptor_name_type,
	    gfarm_gsi_get_delegated_cred(), NULL,
	    gfarm_auth_request_gsi_wait_result, state,
	    &e_major, &e_minor);
	if (need_free)
		free(acceptor_name);
	if (state->gfsl_state == NULL) {
		/* XXX e_major/e_minor should be used */
		e = "cannote initiate GSI connection";
		goto error_free_readable;
	}

	state->q = q;
	state->conn = conn;
	state->continuation = continuation;
	state->closure = closure;
	state->error = NULL;
	*statepp = state;
	return (NULL);

error_free_readable:
	gfarm_event_free(state->readable);
error_free_state:
	free(state);
	return (e);
}

char *
gfarm_auth_result_gsi_multiplexed(void *sp)
{
	struct gfarm_auth_request_gsi_state *state = sp;
	char *e = state->error;

	gfarm_event_free(state->readable);
	free(state);
	return (e);
}

/*
 * "gsi_auth" method
 */

char *
gfarm_auth_request_gsi_auth(struct xxx_connection *conn,
	char *service_tag, char *hostname)
{
	char *e = gfarm_auth_request_gsi(conn, service_tag, hostname);

	if (e == NULL)
		xxx_connection_downgrade_to_insecure_session(conn);
	return (e);
}

char *
gfarm_auth_request_gsi_auth_multiplexed(struct gfarm_eventqueue *q,
	struct xxx_connection *conn,
	char *service_tag, char *hostname,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	return (gfarm_auth_request_gsi_multiplexed(q, conn,
	    service_tag, hostname,
	    continuation, closure, statepp));
}

char *
gfarm_auth_result_gsi_auth_multiplexed(void *sp)
{
	struct gfarm_auth_request_gsi_state *state = sp;
	/* sp will be free'ed in gfarm_auth_result_gsi_multiplexed().
	 * state->conn should be saved before calling it. */
	struct xxx_connection *conn = state->conn;
	char *e = gfarm_auth_result_gsi_multiplexed(sp);

	if (e == NULL)
		xxx_connection_downgrade_to_insecure_session(conn);
	return (e);
}
