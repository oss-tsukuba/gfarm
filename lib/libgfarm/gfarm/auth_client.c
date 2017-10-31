#include <sys/types.h> /* fd_set */
#include <sys/time.h>
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pwd.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "gfevent.h"

#include "context.h"
#include "liberror.h"
#include "gfp_xdr.h"
#include "auth.h"

#define staticp	(gfarm_ctxp->auth_client_static)

struct gfarm_auth_client_static {
	enum gfarm_auth_id_type gfarm_auth_type;
};

gfarm_error_t
gfarm_auth_client_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_auth_client_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->gfarm_auth_type = GFARM_AUTH_ID_TYPE_USER;

	ctxp->auth_client_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_auth_client_static_term(struct gfarm_context *ctxp)
{
	free(ctxp->auth_client_static);
}

/*
 * currently 31 is enough,
 * but it is possible that future server replies more methods.
 */
#define GFARM_AUTH_METHODS_BUFFER_SIZE	256

static const struct gfarm_auth_client_method {
	enum gfarm_auth_method method;
	gfarm_error_t (*request)(struct gfp_xdr *,
		const char *, const char *, enum gfarm_auth_id_type,
		const char *, struct passwd *);
	gfarm_error_t (*request_multiplexed)(struct gfarm_eventqueue *,
		struct gfp_xdr *, const char *, const char *,
		enum gfarm_auth_id_type, const char *, struct passwd *,
		void (*)(void *), void *, void **);
	gfarm_error_t (*result_multiplexed)(void *);
} gfarm_auth_trial_table[] = {
	/*
	 * This table entry should be prefered order
	 */
	{ GFARM_AUTH_METHOD_SHAREDSECRET,
	  gfarm_auth_request_sharedsecret,
	  gfarm_auth_request_sharedsecret_multiplexed,
	  gfarm_auth_result_sharedsecret_multiplexed },
#ifdef HAVE_GSI
	{ GFARM_AUTH_METHOD_GSI_AUTH,
	  gfarm_auth_request_gsi_auth,
	  gfarm_auth_request_gsi_auth_multiplexed,
	  gfarm_auth_result_gsi_auth_multiplexed },
	{ GFARM_AUTH_METHOD_GSI,
	  gfarm_auth_request_gsi,
	  gfarm_auth_request_gsi_multiplexed,
	  gfarm_auth_result_gsi_multiplexed },
#endif
	{ GFARM_AUTH_METHOD_NONE,	  NULL, NULL, NULL }	/* sentinel */
};

gfarm_error_t
gfarm_auth_request_sharedsecret(struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd)
{
	/*
	 * too weak authentication.
	 * assumes shared home directory.
	 */
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	char *home;
	unsigned int expire;
	char shared_key[GFARM_AUTH_SHARED_KEY_LEN];
	char challenge[GFARM_AUTH_CHALLENGE_LEN];
	char response[GFARM_AUTH_RESPONSE_LEN];
	size_t len;
	gfarm_int32_t error, error_ignore; /* enum gfarm_auth_error */
	int eof, key_create = GFARM_AUTH_SHARED_KEY_CREATE;
	int try = 0;

	/* XXX NOTYET deal with self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	home = pwd ? pwd->pw_dir : gfarm_get_local_homedir();
	if (user == NULL || home == NULL)
		return (GFARM_ERRMSG_AUTH_REQUEST_SHAREDSECRET_IMPLEMENTATION_ERROR);

	e = gfp_xdr_send(conn, "s", user);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001025,
			"sending user (%s) failed"
			"when requesting sharedsecret: %s",
			user, gfarm_error_string(e));
		return (e);
	}

	do {
		e = gfarm_auth_shared_key_get(&expire, shared_key, home, pwd,
		    key_create, 0);
		key_create = GFARM_AUTH_SHARED_KEY_CREATE_FORCE;
		if (e != GFARM_ERR_NO_ERROR) {
			e_save = e;
			gflog_auth_error(GFARM_MSG_1000019,
			    "while accessing %s: %s",
			    GFARM_AUTH_SHARED_KEY_PRINTNAME,
			    gfarm_error_string(e));
			break;
		}
		e = gfp_xdr_send(conn, "i", GFARM_AUTH_SHAREDSECRET_MD5);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001026,
				"sending auth shared secret md5 failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		e = gfp_xdr_recv(conn, 0, &eof, "i", &error);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001027,
				"receiving auth shared secret md5 "
				"response failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		if (eof) {
			gflog_debug(GFARM_MSG_1001028,
			    "receiving auth shared secret md5 response: %s",
			    gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
			return (GFARM_ERR_UNEXPECTED_EOF);
		}
		if (error != GFARM_AUTH_ERROR_NO_ERROR)
			break;

		e = gfp_xdr_recv(conn, 0, &eof, "b",
		    sizeof(challenge), &len, challenge);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001029,
				"receiving challenge response failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		if (eof) {
			gflog_debug(GFARM_MSG_1001030,
			    "receiving challenge response: %s",
			    gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
			return (GFARM_ERR_UNEXPECTED_EOF);
		}
		e = gfarm_auth_sharedsecret_response_data(
		    shared_key, challenge, response);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1004515,
			    "calculating challenge-response: %s",
			    gfarm_error_string(e));
			/*
			 * this response must fail, we just want to prevent
			 * uninitialized memory access here.
			 */
			memset(response, 0, sizeof response);
		}
		e = gfp_xdr_send(conn, "ib",
		    expire, sizeof(response), response);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001031,
				"sending expire %u failed: %s",
				expire,
				gfarm_error_string(e));
			return (e);
		}
		e = gfp_xdr_recv(conn, 1, &eof, "i", &error);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001032,
				"receiving expire response failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		if (eof) {
			gflog_debug(GFARM_MSG_1001033,
			    "receiving expire response: %s",
			    gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
			return (GFARM_ERR_UNEXPECTED_EOF);
		}
		if (error == GFARM_AUTH_ERROR_NO_ERROR)
			return (GFARM_ERR_NO_ERROR); /* success */
	} while (++try < GFARM_AUTH_RETRY_MAX &&
	    error == GFARM_AUTH_ERROR_EXPIRED);

	e = gfp_xdr_send(conn, "i", GFARM_AUTH_SHAREDSECRET_GIVEUP);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001034,
			"sending giveup failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	e = gfp_xdr_recv(conn, 0, &eof, "i", &error_ignore);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001035,
			"receiving giveup response failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (eof) {
		gflog_debug(GFARM_MSG_1001036,
		    "receiving giveup response: %s",
		    gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
		return (GFARM_ERR_UNEXPECTED_EOF);
	}

	if (e_save != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001037,
			"access to %s failed: %s",
			GFARM_AUTH_SHARED_KEY_PRINTNAME,
			gfarm_error_string(e_save));
		return (e_save);
	}
	switch (error) {
	case GFARM_AUTH_ERROR_NOT_SUPPORTED:
		gflog_debug(GFARM_MSG_1001038,
			"Protocol not supported");
		return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
	case GFARM_AUTH_ERROR_EXPIRED:
		gflog_debug(GFARM_MSG_1001039,
			"Authentication token expired");
		return (GFARM_ERR_EXPIRED);
	case GFARM_AUTH_ERROR_TEMPORARY_FAILURE:
		gflog_debug(GFARM_MSG_1003719,
		    "gfarm_auth_request_sharedsecre: temporary failure");
		/*
		 * an error which satisfies IS_CONNECTION_ERROR(),
		 * to make the caller retry
		 */
		return (GFARM_ERR_CONNECTION_ABORTED);
	default:
		gflog_debug(GFARM_MSG_1003720,
		    "Authentication failed: %d", (int)error);
		return (GFARM_ERR_AUTHENTICATION);
	}
}

gfarm_error_t
gfarm_auth_request(struct gfp_xdr *conn,
	const char *service_tag, const char *name, struct sockaddr *addr,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd, enum gfarm_auth_method *auth_methodp)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i, eof;
	gfarm_int32_t methods, server_methods; /* bitset */
	gfarm_int32_t method; /* enum gfarm_auth_method */
	gfarm_int32_t error; /* enum gfarm_auth_error */
	size_t nmethods;
	unsigned char methods_buffer[GFARM_AUTH_METHODS_BUFFER_SIZE];

	assert(GFARM_AUTH_METHOD_NUMBER <= sizeof(gfarm_int32_t) * CHAR_BIT);

	methods = gfarm_auth_method_get_enabled_by_name_addr(name, addr);
	if (methods == 0) {
		gflog_debug(GFARM_MSG_1001041,
			"Auth method not available for host %s",
		    name);
		return (GFARM_ERRMSG_AUTH_METHOD_NOT_AVAILABLE_FOR_THE_HOST);
	}
	methods &= gfarm_auth_method_get_available();
	if (methods == 0) {
		gflog_debug(GFARM_MSG_1001042,
			"No usable auth method configured");
		return (GFARM_ERRMSG_USABLE_AUTH_METHOD_IS_NOT_CONFIGURED);
	}

	e = gfp_xdr_recv(conn, 0, &eof, "b", sizeof(methods_buffer),
	    &nmethods, methods_buffer);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001043,
			"receiving methods response failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (eof) {
		gflog_debug(GFARM_MSG_1001044,
		    "receiving methods response: %s",
		    gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
		return (GFARM_ERR_UNEXPECTED_EOF);
	}

	server_methods = 0;
	for (i = 0; i < nmethods; i++) {
		if (methods_buffer[i] <= GFARM_AUTH_METHOD_NONE ||
		    methods_buffer[i] >= GFARM_AUTH_METHOD_NUMBER)
			continue;
		server_methods |= 1 << methods_buffer[i];
	}

	for (i = 0;; i++) {
		method = gfarm_auth_trial_table[i].method;
		if (method != GFARM_AUTH_METHOD_NONE &&
		    (methods & server_methods & (1 << method)) == 0)
			continue;
		e = gfp_xdr_send(conn, "i", method);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001045,
				"sending method (%d) failed: %s",
				method,
				gfarm_error_string(e));
			return (e);
		}
		e = gfp_xdr_recv(conn, 1, &eof, "i", &error);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001046,
				"receiving method (%d) response failed: %s",
				method,
				gfarm_error_string(e));
			return (e);
		}
		if (eof || error != GFARM_AUTH_ERROR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001047,
			    "receiving method (%d) response (error = %u): %s",
			    method, error,
			    eof ?
			    gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF) :
			    gfarm_error_string(GFARM_ERR_PROTOCOL));
			/* GFARM_ERR_PROTOCOL shouldn't happen */
			return (eof ?
			    GFARM_ERR_UNEXPECTED_EOF : GFARM_ERR_PROTOCOL);
		}
		if (method == GFARM_AUTH_METHOD_NONE) {
			/* give up */
			if (server_methods == 0) {
				gflog_debug(GFARM_MSG_1001048,
					"Method permission denied");
				return (GFARM_ERR_PERMISSION_DENIED);
			}
			if ((methods & server_methods) == 0) {
				gflog_debug(GFARM_MSG_1001049,
					"Method protocol not supported");
				return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
			}
			if (e_save != GFARM_ERR_NO_ERROR)
				gflog_debug(GFARM_MSG_1001050,
					"Method error: %s",
					gfarm_error_string(e_save));
			else
				gflog_debug(GFARM_MSG_1001051,
					"Auth request implementation error");
			return (e_save != GFARM_ERR_NO_ERROR ? e_save :
			    GFARM_ERRMSG_AUTH_REQUEST_IMPLEMENTATION_ERROR);
		}
		e = (*gfarm_auth_trial_table[i].request)(conn,
		    service_tag, name, self_type, user, pwd);
		if (e == GFARM_ERR_NO_ERROR) {
			if (auth_methodp != NULL)
				*auth_methodp = method;
			return (GFARM_ERR_NO_ERROR); /* success */
		}
		if (e != GFARM_ERR_PROTOCOL_NOT_SUPPORTED &&
		    e != GFARM_ERR_EXPIRED &&
		    e != GFARM_ERR_PERMISSION_DENIED &&
		    e != GFARM_ERR_AUTHENTICATION) {
			gflog_debug(GFARM_MSG_1001052,
				"Method protocol error: %s",
				gfarm_error_string(e));
			/* protocol error */
			return (e);
		}
		e_save = e;
	}
}

/*
 * multiplexed version of gfarm_auth_request_sharedsecret()
 * for parallel authentication
 */

struct gfarm_auth_request_sharedsecret_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *readable, *writable;
	struct gfp_xdr *conn;
	void (*continuation)(void *);
	void *closure;

	char *home;
	struct passwd *pwd;

	/* for loop */
	int try;
	unsigned int expire;
	char shared_key[GFARM_AUTH_SHARED_KEY_LEN];

	/* results */
	gfarm_error_t error, error_save;
	gfarm_int32_t proto_error; /* enum gfarm_auth_error */
};

static void
gfarm_auth_request_sharedsecret_receive_fin(int events, int fd,
	void *closure, const struct timeval *t)
{
	struct gfarm_auth_request_sharedsecret_state *state = closure;
	int eof;
	gfarm_int32_t error_ignore; /* enum gfarm_auth_error */

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_1001053,
			"receiving fin failed: %s",
			gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	state->error = gfp_xdr_recv(state->conn, 0, &eof, "i",
	    &error_ignore);
	if (state->error == GFARM_ERR_NO_ERROR && eof) {
		state->error = GFARM_ERR_UNEXPECTED_EOF;
		gflog_debug(GFARM_MSG_1001054,
			"receiving fin %s",
			gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
	}
	if (state->error != GFARM_ERR_NO_ERROR)
		;
	else if (state->error_save != GFARM_ERR_NO_ERROR) {
		state->error = state->error_save;
	} else {
		switch (state->proto_error) {
		case GFARM_AUTH_ERROR_NOT_SUPPORTED:
			state->error = GFARM_ERR_PROTOCOL_NOT_SUPPORTED;
			gflog_debug(GFARM_MSG_1001055,
				"Protocol not supported");
			break;
		case GFARM_AUTH_ERROR_EXPIRED:
			state->error = GFARM_ERR_EXPIRED;
			gflog_debug(GFARM_MSG_1001056,
				"Authentication token expired");
			break;
		case GFARM_AUTH_ERROR_TEMPORARY_FAILURE:
			/*
			 * an error which satisfies IS_CONNECTION_ERROR(),
			 * to make the caller retry
			 */
			state->error = GFARM_ERR_CONNECTION_ABORTED;
			gflog_debug(GFARM_MSG_1003721,
			    "gfarm_auth_request_sharedsecret_multiplexed: "
			    "temporary failure");
			break;
		default:
			state->error = GFARM_ERR_AUTHENTICATION;
			gflog_debug(GFARM_MSG_1003722,
			    "Authentication failed: %d",
			    (int)state->proto_error);
			break;
		}
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_sharedsecret_send_giveup(int events, int fd,
	void *closure, const struct timeval *t)
{
	struct gfarm_auth_request_sharedsecret_state *state = closure;
	int rv;
	struct timeval timeout;

	state->error = gfp_xdr_send(state->conn, "i",
	    GFARM_AUTH_SHAREDSECRET_GIVEUP);
	if (state->error == GFARM_ERR_NO_ERROR &&
	    (state->error = gfp_xdr_flush(state->conn)) == GFARM_ERR_NO_ERROR) {
		gfarm_fd_event_set_callback(state->readable,
		    gfarm_auth_request_sharedsecret_receive_fin, state);
		timeout.tv_sec = GFARM_AUTH_TIMEOUT; timeout.tv_usec = 0;
		if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, &timeout)) == 0) {
			/* go to
			 * gfarm_auth_request_sharedsecret_receive_fin() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void gfarm_auth_request_sharedsecret_send_keytype(int events, int fd,
	void *closure, const struct timeval *t);

static void
gfarm_auth_request_sharedsecret_receive_result(int events, int fd,
	void *closure, const struct timeval *t)
{
	struct gfarm_auth_request_sharedsecret_state *state = closure;
	int rv, eof;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_1001058,
			"receiving result failed: %s",
			gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	state->error = gfp_xdr_recv(state->conn, 1, &eof, "i",
	    &state->proto_error);
	if (state->error == GFARM_ERR_NO_ERROR && eof)
		state->error = GFARM_ERR_UNEXPECTED_EOF;
	if (state->error == GFARM_ERR_NO_ERROR) {
		if (state->proto_error != GFARM_AUTH_ERROR_NO_ERROR) {
			gfarm_fd_event_set_callback(state->writable,
			    (++state->try < GFARM_AUTH_RETRY_MAX &&
			    state->proto_error == GFARM_AUTH_ERROR_EXPIRED) ?
			    gfarm_auth_request_sharedsecret_send_keytype :
			    gfarm_auth_request_sharedsecret_send_giveup,
			    state);
			rv = gfarm_eventqueue_add_event(state->q,
			    state->writable, NULL);
			if (rv == 0)
				return;
			state->error = gfarm_errno_to_error(rv);
		}
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_sharedsecret_receive_challenge(int events, int fd,
	void *closure, const struct timeval *t)
{
	gfarm_error_t e;
	struct gfarm_auth_request_sharedsecret_state *state = closure;
	int rv, eof;
	char challenge[GFARM_AUTH_CHALLENGE_LEN];
	char response[GFARM_AUTH_RESPONSE_LEN];
	size_t len;
	struct timeval timeout;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_1001059,
			"receiving challenge failed: %s",
			gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	state->error = gfp_xdr_recv(state->conn, 0, &eof, "b",
	    sizeof(challenge), &len, challenge);
	if (state->error == GFARM_ERR_NO_ERROR && eof)
		state->error = GFARM_ERR_UNEXPECTED_EOF;
	if (state->error == GFARM_ERR_NO_ERROR) {
		/* XXX It's better to check writable event here */
		e = gfarm_auth_sharedsecret_response_data(
		    state->shared_key, challenge, response);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1004516,
			    "calculating challenge-response: %s",
			    gfarm_error_string(e));
			/*
			 * this response must fail, we just want to prevent
			 * uninitialized memory access here.
			 */
			memset(response, 0, sizeof response);
		}
		state->error = gfp_xdr_send(state->conn, "ib",
		    state->expire, sizeof(response), response);
		if (state->error == GFARM_ERR_NO_ERROR &&
		    (state->error = gfp_xdr_flush(state->conn)) ==
		    GFARM_ERR_NO_ERROR) {
			gfarm_fd_event_set_callback(state->readable,
			   gfarm_auth_request_sharedsecret_receive_result,
			   state);
			timeout.tv_sec = GFARM_AUTH_TIMEOUT;
			timeout.tv_usec = 0;
			rv = gfarm_eventqueue_add_event(state->q,
			    state->readable, &timeout);
			if (rv == 0) {
				/* go to gfarm_..._receive_result() */
				return;
			}
			state->error = gfarm_errno_to_error(rv);
		}
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_sharedsecret_receive_keytype(int events, int fd,
	void *closure, const struct timeval *t)
{
	struct gfarm_auth_request_sharedsecret_state *state = closure;
	int rv, eof;
	struct timeval timeout;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_1001060,
			"receiving keytype failed: %s",
			gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	/* We need just==1 here, because we may wait an event next. */
	state->error = gfp_xdr_recv(state->conn, 1, &eof, "i",
	    &state->proto_error);
	if (state->error == GFARM_ERR_NO_ERROR && eof)
		state->error = GFARM_ERR_UNEXPECTED_EOF;
	if (state->error == GFARM_ERR_NO_ERROR) {
		if (state->proto_error != GFARM_AUTH_ERROR_NO_ERROR) {
			gfarm_fd_event_set_callback(state->writable,
			    gfarm_auth_request_sharedsecret_send_giveup,
			    state);
			rv = gfarm_eventqueue_add_event(state->q,
			    state->writable, NULL);
		} else {
			gfarm_fd_event_set_callback(state->readable,
			   gfarm_auth_request_sharedsecret_receive_challenge,
			   state);
			timeout.tv_sec = GFARM_AUTH_TIMEOUT;
			timeout.tv_usec = 0;
			rv = gfarm_eventqueue_add_event(state->q,
			    state->readable, &timeout);
		}
		if (rv == 0) {
			/*
			 * go to
			 * gfarm_auth_request_sharedsecret_send_giveup()
			 * or
			 *gfarm_auth_request_sharedsecret_receive_challenge()
			 */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_sharedsecret_send_keytype(int events, int fd,
	void *closure, const struct timeval *t)
{
	struct gfarm_auth_request_sharedsecret_state *state = closure;
	int rv;
	struct timeval timeout;

	state->error_save = gfarm_auth_shared_key_get(
	    &state->expire, state->shared_key, state->home, state->pwd,
	    state->try == 0 ?
	    GFARM_AUTH_SHARED_KEY_CREATE :
	    GFARM_AUTH_SHARED_KEY_CREATE_FORCE, 0);
	if (state->error_save != GFARM_ERR_NO_ERROR) {
		gflog_auth_error(GFARM_MSG_1000020, "while accessing %s: %s",
		    GFARM_AUTH_SHARED_KEY_PRINTNAME,
		    gfarm_error_string(state->error_save));
		gfarm_auth_request_sharedsecret_send_giveup(events, fd,
		    closure, t);
		return;
	}
	state->error = gfp_xdr_send(state->conn, "i",
	    GFARM_AUTH_SHAREDSECRET_MD5);
	if (state->error == GFARM_ERR_NO_ERROR &&
	    (state->error = gfp_xdr_flush(state->conn)) == GFARM_ERR_NO_ERROR) {
		timeout.tv_sec = GFARM_AUTH_TIMEOUT; timeout.tv_usec = 0;
		if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, &timeout)) == 0) {
			/* go to gfarm_auth_request_sharedsecret
			 *	_receive_keytype() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfarm_auth_request_sharedsecret_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	gfarm_error_t e;
	char *home;
	struct gfarm_auth_request_sharedsecret_state *state;
	int rv, sock = gfp_xdr_fd(conn);

	/* XXX NOTYET deal with self_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST */
	home = pwd ? pwd->pw_dir : gfarm_get_local_homedir();
	if (user == NULL || home == NULL) /* not properly initialized */
		return (GFARM_ERRMSG_AUTH_REQUEST_SHAREDSECRET_MULTIPLEXED_IMPLEMENTATION_ERROR);

	/* XXX It's better to check writable event here */
	e = gfp_xdr_send(conn, "s", user);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001061,
			"sending user %s failed: %s",
			user,
			gfarm_error_string(e));
		return (e);
	}

	GFARM_MALLOC(state);
	if (state == NULL) {
		gflog_debug(GFARM_MSG_1001062,
			"allocation of 'state' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	state->writable = gfarm_fd_event_alloc(
	    GFARM_EVENT_WRITE, sock,
	    gfarm_auth_request_sharedsecret_send_keytype, state);
	if (state->writable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001063,
			"allocation of 'writable' failed: %s",
			gfarm_error_string(e));
		goto error_free_state;
	}
	/*
	 * We cannt use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, sock,
	    gfarm_auth_request_sharedsecret_receive_keytype, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001064,
			"allocation of 'readable' failed: %s",
			gfarm_error_string(e));
		goto error_free_writable;
	}
	/* go to gfarm_auth_request_sharedsecret_send_user() */
	rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
	if (rv != 0) {
		e = gfarm_errno_to_error(rv);
		gflog_debug(GFARM_MSG_1001065,
			"addition of event failed: %s",
			gfarm_error_string(e));
		goto error_free_readable;
	}

	state->q = q;
	state->conn = conn;
	state->continuation = continuation;
	state->closure = closure;
	state->home = home;
	state->pwd = pwd;
	state->try = 0;
	state->error = state->error_save = GFARM_ERR_NO_ERROR;
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

error_free_readable:
	gfarm_event_free(state->readable);
error_free_writable:
	gfarm_event_free(state->writable);
error_free_state:
	free(state);
	return (e);
}

gfarm_error_t
gfarm_auth_result_sharedsecret_multiplexed(void *sp)
{
	struct gfarm_auth_request_sharedsecret_state *state = sp;
	gfarm_error_t e = state->error;

	gfarm_event_free(state->readable);
	gfarm_event_free(state->writable);
	free(state);
	return (e);
}

/*
 * multiplexed version of gfs_auth_request() for parallel authentication
 */

struct gfarm_auth_request_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *readable, *writable;
	struct gfp_xdr *conn;
	const char *service_tag;
	const char *name;
	char *user;
	struct passwd *pwd;
	struct sockaddr *addr;
	enum gfarm_auth_id_type self_type;
	void (*continuation)(void *);
	void *closure;

	gfarm_int32_t methods, server_methods; /* bitset */

	/* loop state */
	int auth_method_index;
	void *method_state;
	gfarm_error_t last_error;

	/* results */
	gfarm_error_t error;
};

static void
gfarm_auth_request_next_method(struct gfarm_auth_request_state *state)
{
	int rv;

	if (state->last_error == GFARM_ERR_NO_ERROR ||
	    (state->last_error != GFARM_ERR_PROTOCOL_NOT_SUPPORTED &&
	     state->last_error != GFARM_ERR_EXPIRED &&
	     state->last_error != GFARM_ERR_PERMISSION_DENIED &&
	     state->last_error != GFARM_ERR_AUTHENTICATION)) {
		state->error = state->last_error;
	} else {
		if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->writable, NULL)) == 0) {
			++state->auth_method_index;
			/* go to gfarm_auth_request_loop_ask_method() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_dispatch_result(void *closure)
{
	struct gfarm_auth_request_state *state = closure;

	state->last_error = (*gfarm_auth_trial_table[state->auth_method_index].
	    result_multiplexed)(state->method_state);
	gfarm_auth_request_next_method(state);
}

static void
gfarm_auth_request_dispatch_method(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfarm_auth_request_state *state = closure;
	int eof;
	gfarm_int32_t error;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_1001066,
			"dispatch method failed: %s",
			gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	state->error = gfp_xdr_recv(state->conn, 1, &eof, "i", &error);
	if (state->error == GFARM_ERR_NO_ERROR && eof)
		state->error = GFARM_ERR_UNEXPECTED_EOF;
	if (state->error == GFARM_ERR_NO_ERROR &&
	    error != GFARM_AUTH_ERROR_NO_ERROR)
		state->error = GFARM_ERR_PROTOCOL;
	if (state->error == GFARM_ERR_NO_ERROR) {
		if (gfarm_auth_trial_table[state->auth_method_index].method
		    != GFARM_AUTH_METHOD_NONE) {
			state->last_error =
			    (*gfarm_auth_trial_table[state->auth_method_index].
			    request_multiplexed)(state->q, state->conn,
			    state->service_tag, state->name, state->self_type,
			    state->user, state->pwd,
			    gfarm_auth_request_dispatch_result, state,
			    &state->method_state);
			if (state->last_error == GFARM_ERR_NO_ERROR) {
				/*
				 * call gfarm_auth_request_$method, then
				 * go to gfarm_auth_request_dispatch_result()
				 */
				return;
			}
			gfarm_auth_request_next_method(state);
			return;
		}
		/* give up */
		if (state->server_methods == 0) {
			state->error = GFARM_ERR_PERMISSION_DENIED;
		} else if ((state->methods & state->server_methods) == 0) {
			state->error = GFARM_ERR_PROTOCOL_NOT_SUPPORTED;
		} else {
			state->error =
			    state->last_error != GFARM_ERR_NO_ERROR ?
			    state->last_error :
			    GFARM_ERRMSG_AUTH_REQUEST_MULTIPLEXED_MPLEMENTATION_ERROR;
		}
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_loop_ask_method(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfarm_auth_request_state *state = closure;
	gfarm_int32_t method; /* enum gfarm_auth_method */
	int rv;
	struct timeval timeout;

	method = gfarm_auth_trial_table[state->auth_method_index].method;
	while (method != GFARM_AUTH_METHOD_NONE &&
	    (state->methods & state->server_methods & (1 << method)) == 0) {
		method =
		    gfarm_auth_trial_table[++state->auth_method_index].method;
	}
	state->error = gfp_xdr_send(state->conn, "i", method);
	if (state->error == GFARM_ERR_NO_ERROR &&
	    (state->error = gfp_xdr_flush(state->conn)) == GFARM_ERR_NO_ERROR) {
		gfarm_fd_event_set_callback(state->readable,
		    gfarm_auth_request_dispatch_method, state);
		timeout.tv_sec = GFARM_AUTH_TIMEOUT; timeout.tv_usec = 0;
		if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, &timeout)) == 0) {
			/* go to gfarm_auth_request_dispatch_method() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfarm_auth_request_receive_server_methods(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfarm_auth_request_state *state = closure;
	int rv, i, eof;
	size_t nmethods;
	unsigned char methods_buffer[GFARM_AUTH_METHODS_BUFFER_SIZE];

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_1001067,
			"receiving server methods failed: %s",
			gfarm_error_string(state->error));
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
		return;
	}
	assert(events == GFARM_EVENT_READ);
	state->error = gfp_xdr_recv(state->conn, 0, &eof, "b",
	    sizeof(methods_buffer), &nmethods, methods_buffer);
	if (state->error == GFARM_ERR_NO_ERROR && eof)
		state->error = GFARM_ERR_UNEXPECTED_EOF;
	if (state->error == GFARM_ERR_NO_ERROR) {
		state->server_methods = 0;
		for (i = 0; i < nmethods; i++) {
			if (methods_buffer[i] <= GFARM_AUTH_METHOD_NONE ||
			    methods_buffer[i] >= GFARM_AUTH_METHOD_NUMBER)
				continue;
			state->server_methods |= 1 << methods_buffer[i];
		}
		if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->writable, NULL)) == 0) {
			state->last_error = GFARM_ERR_NO_ERROR;
			state->auth_method_index = 0;
			/* go to gfarm_auth_request_loop_ask_method() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfarm_auth_request_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *name, struct sockaddr *addr,
	enum gfarm_auth_id_type self_type, const char *user,
	struct passwd *pwd,
	void (*continuation)(void *), void *closure,
	struct gfarm_auth_request_state **statepp)
{
	gfarm_error_t e;
	int rv, sock = gfp_xdr_fd(conn);
	struct gfarm_auth_request_state *state;
	gfarm_int32_t methods; /* bitset */
	struct timeval timeout;

	assert(GFARM_AUTH_METHOD_NUMBER <= sizeof(gfarm_int32_t) * CHAR_BIT);

	methods = gfarm_auth_method_get_enabled_by_name_addr(name, addr);
	if (methods == 0) {
		gflog_debug(GFARM_MSG_1001068,
			"Auth method not available for host %s",
			name);
		return (GFARM_ERRMSG_AUTH_METHOD_NOT_AVAILABLE_FOR_THE_HOST);
	}
	methods &= gfarm_auth_method_get_available();
	if (methods == 0) {
		gflog_debug(GFARM_MSG_1001069,
			"No usable auth method configured");
		return (GFARM_ERRMSG_USABLE_AUTH_METHOD_IS_NOT_CONFIGURED);
	}

	GFARM_MALLOC(state);
	if (state == NULL) {
		gflog_debug(GFARM_MSG_1001070,
			"allocation of 'state' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	state->writable = gfarm_fd_event_alloc(
	    GFARM_EVENT_WRITE, sock,
	    gfarm_auth_request_loop_ask_method, state);
	if (state->writable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001071,
			"allocation of 'writable' failed: %s",
			gfarm_error_string(e));
		goto error_free_state;
	}
	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, sock,
	    gfarm_auth_request_receive_server_methods, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001072,
			"allocation of 'readable' failed: %s",
			gfarm_error_string(e));
		goto error_free_writable;
	}
	/* go to gfarm_auth_request_receive_server_methods() */
	timeout.tv_sec = GFARM_AUTH_TIMEOUT; timeout.tv_usec = 0;
	rv = gfarm_eventqueue_add_event(q, state->readable, &timeout);
	if (rv != 0) {
		e = gfarm_errno_to_error(rv);
		gflog_debug(GFARM_MSG_1001073,
			"addition of event failed: %s",
			gfarm_error_string(e));
		goto error_free_readable;
	}

	state->user = strdup(user);
	if (state->user == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1002561,
			"failed to alloc `user`: %s",
			gfarm_error_string(e));
		goto error_free_readable;
	}

	state->q = q;
	state->conn = conn;
	state->service_tag = service_tag;
	state->name = name;
	state->pwd = pwd;
	state->addr = addr;
	state->self_type = self_type;
	state->methods = methods;
	state->continuation = continuation;
	state->closure = closure;
	state->error = GFARM_ERR_NO_ERROR;
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

error_free_readable:
	gfarm_event_free(state->readable);
error_free_writable:
	gfarm_event_free(state->writable);
error_free_state:
	free(state);
	return (e);
}

gfarm_error_t
gfarm_auth_result_multiplexed(struct gfarm_auth_request_state *state,
	enum gfarm_auth_method *auth_methodp)
{
	gfarm_error_t e = state->error;

	if (e == GFARM_ERR_NO_ERROR) {
		if (auth_methodp != NULL)
			*auth_methodp = gfarm_auth_trial_table[
			    state->auth_method_index].method;
	}
	gfarm_event_free(state->readable);
	gfarm_event_free(state->writable);
	free(state->user);
	free(state);
	return (e);
}

gfarm_error_t
gfarm_set_auth_id_type(enum gfarm_auth_id_type type)
{
	staticp->gfarm_auth_type = type;
	return (GFARM_ERR_NO_ERROR);
}

enum gfarm_auth_id_type
gfarm_get_auth_id_type(void)
{
	return (staticp->gfarm_auth_type);
}
