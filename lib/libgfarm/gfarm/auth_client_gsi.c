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

#define GFSL_CONF_USERMAP "/etc/grid-security/grid-mapfile"

gss_cred_id_t gfarm_gsi_get_delegated_cred();	/* XXX */

static int gsi_initialized;
static int gsi_server_initialized;

static char *gsi_client_cred_name;

gfarm_error_t
gfarm_gsi_client_initialize(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (gsi_initialized)
		return (GFARM_ERR_NO_ERROR);

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
		return (GFARM_ERRMSG_GSI_CREDENTIAL_INITIALIZATION_FAILED);
	}
	gsi_initialized = 1;
	gsi_server_initialized = 0;
	gsi_client_cred_name = gfarmSecSessionGetInitiatorCredName();
	return (GFARM_ERR_NO_ERROR);
}

char *
gfarm_gsi_client_cred_name(void)
{
	return (gsi_client_cred_name);
}

gfarm_error_t
gfarm_gsi_server_initialize(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (gsi_initialized) {
		if (gsi_server_initialized)
			return (GFARM_ERR_NO_ERROR);
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
		return (GFARM_ERRMSG_GSI_INITIALIZATION_FAILED); /* XXX */
	}
	gsi_initialized = 1;
	gsi_server_initialized = 1;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_auth_request_gsi(struct gfp_xdr *conn, enum gfarm_auth_id_type self_type)
{
	int fd = gfp_xdr_fd(conn);
	gfarm_error_t e;
	OM_uint32 e_major;
	OM_uint32 e_minor;
	gfarmSecSession *session;
	gfarm_int32_t error; /* enum gfarm_auth_error */
	int eof;

	e = gfarm_gsi_client_initialize();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/* XXX NOTYET deal with self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
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
	gfp_xdr_set_secsession(conn, session);

	e = gfp_xdr_recv(conn, 1, &eof, "i", &error);
	if (e != GFARM_ERR_NO_ERROR || eof ||
	    error != GFARM_AUTH_ERROR_NO_ERROR) {
		gfp_xdr_reset_secsession(conn);
		gfp_xdr_set_fd(conn, fd);
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
		gfp_xdr_set_fd(state->conn, fd);
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
			gfp_xdr_set_secsession(state->conn,
			    state->session);
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
	struct gfp_xdr *conn, enum gfarm_auth_id_type self_type,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	gfarm_error_t e;
	struct gfarm_auth_request_gsi_state *state;
	OM_uint32 e_major, e_minor;

	e = gfarm_gsi_client_initialize();
	if (e != GFARM_ERR_NO_ERROR)
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
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, gfp_xdr_fd(conn),
	    gfarm_auth_request_gsi_receive_result, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto error_free_state;
	}

	/* XXX NOTYET deal with self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	state->gfsl_state = gfarmSecSessionInitiateRequest(q,
	    gfp_xdr_fd(conn), gfarm_gsi_get_delegated_cred(), NULL,
	    gfarm_auth_request_gsi_wait_result, state,
	    &e_major, &e_minor);
	if (state->gfsl_state == NULL) {
		/* XXX e_major/e_minor should be used */
		e = GFARM_ERRMSG_CANNOT_INITIATE_GSI_CONNECTION;
		goto error_free_readable;
	}

	state->q = q;
	state->conn = conn;
	state->continuation = continuation;
	state->closure = closure;
	state->error = GFARM_ERR_NO_ERROR;
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

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

	gfarm_event_free(state->readable);
	free(state);
	return (e);
}

/*
 * "gsi_auth" method
 */

gfarm_error_t
gfarm_auth_request_gsi_auth(struct gfp_xdr *conn,
	enum gfarm_auth_id_type self_type)
{
	gfarm_error_t e = gfarm_auth_request_gsi(conn, self_type);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}

gfarm_error_t
gfarm_auth_request_gsi_auth_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn, enum gfarm_auth_id_type self_type,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	return (gfarm_auth_request_gsi_multiplexed(q, conn, self_type,
	    continuation, closure, statepp));
}

gfarm_error_t
gfarm_auth_result_gsi_auth_multiplexed(void *sp)
{
	struct gfarm_auth_request_gsi_state *state = sp;
	gfarm_error_t e = gfarm_auth_result_gsi_multiplexed(sp);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(state->conn);
	return (e);
}
