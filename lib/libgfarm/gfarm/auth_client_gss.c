#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/time.h>

#include <gssapi.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "gfevent.h"

#include "gfsl_secure_session.h"
#include "gfarm_auth.h"

#include "gss.h"

#include "liberror.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "io_gfsl.h"
#include "auth.h"
#include "auth_gss.h"

#include "gfm_proto.h" /* for GFM_SERVICE_TAG, XXX layering violation */
#include "gfs_proto.h" /* for GFS_SERVICE_TAG, XXX layering violation */

/*
 * client side authentication
 */

static gfarm_error_t
gfarm_gss_acquire_initiator_credential(struct gfarm_gss *gss,
	const char *hostname,
	enum gfarm_auth_id_type self_type,
	gss_cred_id_t *output_cred, gss_name_t *output_name)
{
	char *service_tag;
	gss_name_t desired_name = GSS_C_NO_NAME;
	gss_name_t initiator_name = GSS_C_NO_NAME;
	gss_cred_id_t cred;
	gfarm_error_t e;
	int ret;
	gfarm_OM_uint32 e_major, e_minor;

	switch (self_type) {
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		/*
		 * If spool_server_cred_service is specified,
		 * a service certificate is used.
		 */
		if (self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST) {
			service_tag = GFS_SERVICE_TAG;
		} else if (self_type == GFARM_AUTH_ID_TYPE_METADATA_HOST) {
			service_tag = GFM_SERVICE_TAG;
		} else {
			assert(0);
		}
		e = gfarm_gss_cred_name_for_server(gss, service_tag, hostname,
		    &desired_name);
		if (e != GFARM_ERR_NO_ERROR) {
			char *cred_service =
			    gfarm_auth_server_cred_service_get(service_tag);

			gflog_auth_error(GFARM_MSG_1000698,
			    "Service credential configuration for %s: %s",
			    cred_service, gfarm_error_string(e));
			return (e);
		}
		break;
	case GFARM_AUTH_ID_TYPE_USER: /* from client */
		break;
	default:
		break;
	}

	/*
	 * always re-acquire my credential, otherwise we cannot deal
	 * with credential expiration.
	 */
	ret = gss->gfarmGssAcquireCredential(
		&cred, desired_name, GSS_C_INITIATE,
		&e_major, &e_minor, &initiator_name);
	if (desired_name != GSS_C_NO_NAME)
		gss->gfarmGssDeleteName(&desired_name, NULL, NULL);
	if (ret < 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000699,
			    "Can't acquire my credential because of:");
			gss->gfarmGssPrintMajorStatus(e_major);
			gss->gfarmGssPrintMinorStatus(e_minor);
		}
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
		gflog_debug(GFARM_MSG_1001466,
		    "acquirement of client credential failed");
		gfarm_auth_set_gss_cred_failed(gss);
		return (GFARM_ERR_INVALID_CREDENTIAL);
#endif
	}
	if (output_cred != NULL)
		*output_cred = cred;
	else
		gss->gfarmGssDeleteCredential(&cred, NULL, NULL);
	if (output_name != NULL)
		*output_name = initiator_name;
	else
		gss->gfarmGssDeleteName(&initiator_name, NULL, NULL);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_auth_request_gss(struct gfp_xdr *conn, struct gfarm_gss *gss,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd)
{
	int fd = gfp_xdr_fd(conn);
	gfarm_error_t e;
	gss_name_t acceptor_name = GSS_C_NO_NAME;
	gss_name_t initiator_name = GSS_C_NO_NAME;
	char *initiator_dn = NULL;
	gss_cred_id_t cred;
	gfarm_OM_uint32 e_major;
	gfarm_OM_uint32 e_minor;
	gfarmSecSession *session;
	gfarm_int32_t error; /* enum gfarm_auth_error */
	int eof, gss_errno = 0, cred_acquired = 0;

	e = gfarm_gss_client_initialize(gss);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001465,
			"initialization of client failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	e = gfarm_gss_cred_name_for_server(gss, service_tag, hostname,
	     &acceptor_name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_auth_error(GFARM_MSG_1000697,
		    "Server credential configuration for %s:%s: %s",
		    service_tag, hostname, gfarm_error_string(e));
		return (e);
	}
	cred = gss->client_cred_get();
	if (cred == GSS_C_NO_CREDENTIAL) { /* if not delegated */
		e = gfarm_gss_acquire_initiator_credential(gss, hostname,
		    self_type, &cred, &initiator_name);
		/* error message already displayed */
		cred_acquired = 1;
	} else {
		if (gss->gfarmGssNewCredentialName(&initiator_name, cred,
		    &e_major, &e_minor) < 0) {
			gfarm_auth_set_gss_cred_failed(gss);
			e = GFARM_ERR_INVALID_CREDENTIAL;
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_1004245,
				    "cannot obtain initiator name");
				gss->gfarmGssPrintMajorStatus(e_major);
				gss->gfarmGssPrintMinorStatus(e_minor);
			}
		}
	}
	if (e == GFARM_ERR_NO_ERROR) {
		initiator_dn = gss->gfarmGssNewDisplayName(initiator_name,
		    &e_major, &e_minor, NULL);
		if (initiator_dn == NULL) {
			gfarm_auth_set_gss_cred_failed(gss);
			e = GFARM_ERR_INVALID_CREDENTIAL;
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_1004246,
				    "cannot obtain initiator dn");
				gss->gfarmGssPrintMajorStatus(e_major);
				gss->gfarmGssPrintMinorStatus(e_minor);
			}
		}
	}
	if (initiator_name != GSS_C_NO_NAME)
		gss->gfarmGssDeleteName(&initiator_name, NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		if (acceptor_name != GSS_C_NO_NAME)
			gss->gfarmGssDeleteName(&acceptor_name, NULL, NULL);
		return (e);
	}
	/* XXX NOTYET deal with self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	session = gss->gfarmSecSessionInitiate(fd, acceptor_name, cred,
	    GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG, NULL,
	    &gss_errno, &e_major, &e_minor);
	if (acceptor_name != GSS_C_NO_NAME)
		gss->gfarmGssDeleteName(&acceptor_name, NULL, NULL);
	if (session == NULL) {
		if (gflog_auth_get_verbose()) {
			gflog_notice(GFARM_MSG_1000700,
			    "Can't initiate session because of:");
			if (gss_errno != 0) {
				gflog_info(GFARM_MSG_1004002, "%s",
				    strerror(gss_errno));
			} else {
				gss->gfarmGssPrintMajorStatus(e_major);
				gss->gfarmGssPrintMinorStatus(e_minor);
			}
		}
		if (cred_acquired &&
		    gss->gfarmGssDeleteCredential(&cred, &e_major, &e_minor)
		    < 0 &&
		    gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000701,
			    "Can't free my credential because of:");
			gss->gfarmGssPrintMajorStatus(e_major);
			gss->gfarmGssPrintMinorStatus(e_minor);
		}
		free(initiator_dn);
		if (gss_errno != 0)
			return (gfarm_errno_to_error(gss_errno));
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
		gflog_debug(GFARM_MSG_1001467,
			"initiation of session failed: %s",
			gfarm_error_string(GFARM_ERR_OPERATION_NOT_PERMITTED));
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
#endif
	}
	gfp_xdr_set_secsession(conn, gss, session,
	    cred_acquired ? cred : GSS_C_NO_CREDENTIAL, initiator_dn);

	e = gfp_xdr_recv(conn, 1, &eof, "i", &error);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001468,
		    "receiving response failed: %s", gfarm_error_string(e));
	} else if (eof) {
		gflog_debug(GFARM_MSG_1001469,
		    "Unexpected EOF when receiving response");
		e = GFARM_ERR_PROTOCOL;
	} else if (error != GFARM_AUTH_ERROR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001470,
		    "Authentication failed: %d", error);
		e = GFARM_ERR_AUTHENTICATION;
	} else {
		return (GFARM_ERR_NO_ERROR);
	}
	gfp_xdr_reset_secsession(conn);
	gfp_xdr_set_socket(conn, fd);
	return (e);
}

/*
 * multiplexed version of gfarm_auth_request_gss() for parallel authentication
 */

struct gfarm_auth_request_gss_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *readable;
	struct gfp_xdr *conn;
	struct gfarm_gss *gss;
	int auth_timeout;
	void (*continuation)(void *);
	void *closure;

	gss_name_t acceptor_name;
	gss_cred_id_t cred;
	char *initiator_dn;
	int cred_acquired;
	struct gfarmSecSessionInitiateState *gfsl_state;
	gfarmSecSession *session;

	/* results */
	gfarm_error_t error;
};

static void
gfarm_auth_request_gss_receive_result(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfarm_auth_request_gss_state *state = closure;
	int eof;
	gfarm_int32_t error; /* enum gfarm_auth_error */

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_1001471,
			"receiving gss auth result failed: %s",
			gfarm_error_string(state->error));
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
gfarm_auth_request_gss_wait_result(void *closure)
{
	struct gfarm_auth_request_gss_state *state = closure;
	OM_uint32 e_major, e_minor;
	int rv;
	struct timeval timeout;

	state->session = state->gss->gfarmSecSessionInitiateResult(
	    state->gfsl_state,
	    &e_major, &e_minor);
	if (state->session == NULL) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000702,
			    "Can't initiate session because of:");
			state->gss->gfarmGssPrintMajorStatus(e_major);
			state->gss->gfarmGssPrintMinorStatus(e_minor);
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
		timeout.tv_sec = state->auth_timeout; timeout.tv_usec = 0;
		rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, &timeout);
		if (rv == 0) {
			gfp_xdr_set_secsession(state->conn, state->gss,
			    state->session,
			    state->cred_acquired ?
			    state->cred : GSS_C_NO_CREDENTIAL,
			    state->initiator_dn);
			/* go to gfarm_auth_request_gss_receive_result() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfarm_auth_request_gss_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn, struct gfarm_gss *gss,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	gfarm_error_t e;
	struct gfarm_auth_request_gss_state *state;
	gss_name_t initiator_name = GSS_C_NO_NAME;
	gfarm_OM_uint32 e_major, e_minor;

	e = gfarm_gss_client_initialize(gss);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001472,
			"initialization of gss client failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	GFARM_MALLOC(state);
	if (state == NULL) {
		gflog_debug(GFARM_MSG_1001473,
			"allocation of 'state' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, gfp_xdr_fd(conn),
	    gfp_xdr_recv_is_ready_call, conn,
	    gfarm_auth_request_gss_receive_result, state);
	if (state->readable == NULL) {
		gflog_debug(GFARM_MSG_1001474,
			"allocation of 'readable' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		e = GFARM_ERR_NO_MEMORY;
		goto error_free_state;
	}

	e = gfarm_gss_cred_name_for_server(gss, service_tag, hostname,
	    &state->acceptor_name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_auth_error(GFARM_MSG_1000703,
		    "Server credential configuration for %s:%s: %s",
		    service_tag, hostname, gfarm_error_string(e));
		goto error_free_readable;
	}

	state->cred_acquired = 0;
	state->cred = gss->client_cred_get();
	if (state->cred == GSS_C_NO_CREDENTIAL) { /* if not delegated */
		e = gfarm_gss_acquire_initiator_credential(gss, hostname,
		    self_type, &state->cred, &initiator_name);
		/* error message already displayed */
		state->cred_acquired = 1;
	} else {
		if (gss->gfarmGssNewCredentialName(&initiator_name,
		    state->cred, &e_major, &e_minor) < 0) {
			gfarm_auth_set_gss_cred_failed(gss);
			e = GFARM_ERR_INVALID_CREDENTIAL;
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_1004247,
				    "cannot obtain initiator name");
				gss->gfarmGssPrintMajorStatus(e_major);
				gss->gfarmGssPrintMinorStatus(e_minor);
			}
		}
	}
	state->initiator_dn = NULL;
	if (e == GFARM_ERR_NO_ERROR) {
		state->initiator_dn = gss->gfarmGssNewDisplayName(
		    initiator_name, &e_major, &e_minor, NULL);
		if (state->initiator_dn == NULL) {
			gfarm_auth_set_gss_cred_failed(gss);
			e = GFARM_ERR_INVALID_CREDENTIAL;
			if (gflog_auth_get_verbose()) {
				gflog_error(GFARM_MSG_1004248,
				    "cannot obtain initiator dn");
				gss->gfarmGssPrintMajorStatus(e_major);
				gss->gfarmGssPrintMinorStatus(e_minor);
			}
		}
	}
	if (initiator_name != GSS_C_NO_NAME)
		gss->gfarmGssDeleteName(&initiator_name, NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		goto error_free_cred;

	/* XXX NOTYET deal with self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	state->gfsl_state = gss->gfarmSecSessionInitiateRequest(q,
	    gfp_xdr_fd(conn), state->acceptor_name, state->cred,
	    GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG, NULL,
	    gfarm_auth_request_gss_wait_result, state,
	    &e_major, &e_minor);
	if (state->gfsl_state == NULL) {
		/* XXX e_major/e_minor should be used */
		gflog_debug(GFARM_MSG_1001476,
			"initiation of gss connection failed");
		e = GFARM_ERRMSG_CANNOT_INITIATE_GSS_CONNECTION;
		goto error_free_cred;
	}

	state->q = q;
	state->conn = conn;
	state->gss = gss;
	state->auth_timeout = auth_timeout;
	state->continuation = continuation;
	state->closure = closure;
	state->error = GFARM_ERR_NO_ERROR;
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

error_free_cred:
	if (state->cred_acquired &&
	    gss->gfarmGssDeleteCredential(&state->cred, &e_major, &e_minor)
	    < 0 &&
	    gflog_auth_get_verbose()) {
		gflog_error(GFARM_MSG_1000705,
		    "Can't free my credential because of:");
		gss->gfarmGssPrintMajorStatus(e_major);
		gss->gfarmGssPrintMinorStatus(e_minor);
	}
	free(state->initiator_dn);
	if (state->acceptor_name != GSS_C_NO_NAME)
		gss->gfarmGssDeleteName(&state->acceptor_name, NULL, NULL);
error_free_readable:
	gfarm_event_free(state->readable);
error_free_state:
	free(state);
	return (e);
}

gfarm_error_t
gfarm_auth_result_gss_multiplexed(void *sp)
{
	struct gfarm_auth_request_gss_state *state = sp;
	gfarm_error_t e = state->error;

	if (state->acceptor_name != GSS_C_NO_NAME)
		state->gss->gfarmGssDeleteName(
		    &state->acceptor_name, NULL, NULL);
	gfarm_event_free(state->readable);
	free(state);
	return (e);
}

/*
 * "gsi_auth" and "kerberos_auth" method
 */

gfarm_error_t
gfarm_auth_request_gss_auth(struct gfp_xdr *conn, struct gfarm_gss *gss,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd)
{
	gfarm_error_t e = gfarm_auth_request_gss(conn, gss,
	    service_tag, hostname, self_type, user, pwd);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}

gfarm_error_t
gfarm_auth_request_gss_auth_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn, struct gfarm_gss *gss,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	return (gfarm_auth_request_gss_multiplexed(q, conn, gss,
	    service_tag, hostname, self_type, user, pwd, auth_timeout,
	    continuation, closure, statepp));
}

gfarm_error_t
gfarm_auth_result_gss_auth_multiplexed(void *sp)
{
	struct gfarm_auth_request_gss_state *state = sp;
	/* sp will be free'ed in gfarm_auth_result_gss_multiplexed().
	 * state->conn should be saved before calling it. */
	struct gfp_xdr *conn = state->conn;
	gfarm_error_t e = gfarm_auth_result_gss_multiplexed(sp);

	if (e == GFARM_ERR_NO_ERROR)
		gfp_xdr_downgrade_to_insecure_session(conn);
	return (e);
}
