#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/time.h>

#include <gssapi.h>

#include "gfutil.h"
#include "gfevent.h"

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "xxx_proto.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"

#define GFSL_CONF_USERMAP "/etc/grid-security/grid-mapfile"

gss_cred_id_t gfarm_gsi_get_delegated_cred();	/* XXX */

static int gsi_initialized;
static int gsi_server_initialized;

static char *gsi_client_cred_name;

char *
gfarm_gsi_client_initialize(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (gsi_initialized)
		return (NULL);

	rv = gfarmSecSessionInitializeInitiator(NULL, &e_major, &e_minor);
	if (rv <= 0) {
		if (gfarm_authentication_verbose) {
			gflog_error(
				"can't initialize as initiator because of:",
				NULL);
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarmSecSessionFinalizeInitiator();
		return ("GSI credential initialization failed"); /* XXX */
	}
	gsi_initialized = 1;
	gsi_server_initialized = 0;
	gsi_client_cred_name = gfarmSecSessionGetInitiatorCredName();
	return (NULL);
}

char *
gfarm_gsi_client_cred_name(void)
{
	return (gsi_client_cred_name);
}

char *
gfarm_gsi_server_initialize(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (gsi_initialized) {
		if (gsi_server_initialized)
			return (NULL);
		gfarmSecSessionFinalizeInitiator();
		gsi_initialized = 0;
	}

	rv = gfarmSecSessionInitializeBoth(NULL, NULL,
	    GFSL_CONF_USERMAP, &e_major, &e_minor);
	if (rv <= 0) {
		if (gfarm_authentication_verbose) {
			gflog_error(
				"can't initialize GSI as both because of:",
				NULL);
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarmSecSessionFinalizeBoth();
		return ("GSI initialization failed"); /* XXX */
	}
	gsi_initialized = 1;
	gsi_server_initialized = 1;
	return (NULL);
}

char *
gfarm_auth_request_gsi(struct xxx_connection *conn)
{
	int fd = xxx_connection_fd(conn);
	char *e;
	OM_uint32 e_major;
	OM_uint32 e_minor;
	gfarmSecSession *session;
	gfarm_int32_t error; /* enum gfarm_auth_error */
	int eof;

	e = gfarm_gsi_client_initialize();
	if (e != NULL)
		return (e);

	session = gfarmSecSessionInitiate(fd,
	    gfarm_gsi_get_delegated_cred(), NULL, &e_major, &e_minor);
	if (session == NULL) {
		if (gfarm_authentication_verbose) {
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
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	char *e;
	struct gfarm_auth_request_gsi_state *state;
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

	state->gfsl_state = gfarmSecSessionInitiateRequest(q,
	    xxx_connection_fd(conn), gfarm_gsi_get_delegated_cred(), NULL,
	    gfarm_auth_request_gsi_wait_result, state,
	    &e_major, &e_minor);
	if (state->gfsl_state < 0) {
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
gfarm_auth_request_gsi_auth(struct xxx_connection *conn)
{
	char *e = gfarm_auth_request_gsi(conn);

	if (e == NULL)
		xxx_connection_downgrade_to_insecure_session(conn);
	return (e);
}

char *
gfarm_auth_request_gsi_auth_multiplexed(struct gfarm_eventqueue *q,
	struct xxx_connection *conn,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	return (gfarm_auth_request_gsi_multiplexed(q, conn,
	    continuation, closure, statepp));
}

char *
gfarm_auth_result_gsi_auth_multiplexed(void *sp)
{
	struct gfarm_auth_request_gsi_state *state = sp;
	char *e = gfarm_auth_result_gsi_multiplexed(sp);

	if (e == NULL)
		xxx_connection_downgrade_to_insecure_session(state->conn);
	return (e);
}
