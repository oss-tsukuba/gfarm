#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "xxx_proto.h"

#ifdef HAVE_GSI
#include "io_gfsl.h" /* XXX - gfarm_auth_request_gsi() */
#endif

#include "auth.h"

/*
 * currently 31 is enough,
 * but it is possible that future server replies more methods.
 */
#define GFARM_AUTH_METHODS_BUFFER_SIZE	256

char *gfarm_auth_request_sharedsecret(struct xxx_connection *);

struct {
	enum gfarm_auth_method method;
	char *(*request)(struct xxx_connection *);
} gfarm_auth_trial_table[] = {
	/*
	 * This table entry should be prefered order
	 */
	{ GFARM_AUTH_METHOD_SHAREDSECRET, gfarm_auth_request_sharedsecret },
#ifdef HAVE_GSI
	{ GFARM_AUTH_METHOD_GSI,	  gfarm_auth_request_gsi },
#endif
	{ GFARM_AUTH_METHOD_NONE,	  NULL }	/* sentinel */
};

char *
gfarm_auth_request_sharedsecret(struct xxx_connection *conn)
{
	/*
	 * too weak authentication.
	 * assumes shared home directory.
	 */
	char *e, *user, *home;
	unsigned int expire;
	char shared_key[GFARM_AUTH_SHARED_KEY_LEN];
	char challenge[GFARM_AUTH_CHALLENGE_LEN];
	char response[GFARM_AUTH_RESPONSE_LEN];
	size_t len;
	gfarm_int32_t error, error_ignore; /* enum gfarm_auth_error */
	int eof, key_create = GFARM_AUTH_SHARED_KEY_CREATE;
	int try = 0;

	user = gfarm_get_global_username();
	home = gfarm_get_local_homedir();
	if (user == NULL || home == NULL)
		return (
		    "gfarm_auth_request_sharedsecret(): programming error, "
		    "gfarm library isn't properly initialized");

	e = xxx_proto_send(conn, "s", user);
	if (e != NULL)
		return (e);

	do {
		e = xxx_proto_send(conn, "i", GFARM_AUTH_SHAREDSECRET_MD5);
		if (e != NULL)
			return (e);
		e = xxx_proto_recv(conn, 0, &eof, "i", &error);
		if (e != NULL)
			return (e);
		if (eof)
			return (GFARM_ERR_PROTOCOL);
		if (error != GFARM_AUTH_ERROR_NO_ERROR)
			break;

		e = gfarm_auth_shared_key_get(&expire, shared_key, home,
		    key_create);
		key_create = GFARM_AUTH_SHARED_KEY_CREATE_FORCE;
		if (e != NULL)
			return (e);
		e = xxx_proto_recv(conn, 0, &eof, "b",
		    sizeof(challenge), &len, challenge);
		if (e != NULL)
			return (e);
		if (eof)
			return (GFARM_ERR_PROTOCOL);
		gfarm_auth_sharedsecret_response_data(shared_key, challenge,
						response);
		e = xxx_proto_send(conn, "ib",
		    expire, sizeof(response), response);
		if (e != NULL)
			return (e);
		e = xxx_proto_recv(conn, 1, &eof, "i", &error);
		if (e != NULL)
			return (e);
		if (eof)
			return (GFARM_ERR_PROTOCOL);
		if (error == GFARM_AUTH_ERROR_NO_ERROR)
			return (NULL); /* success */
	} while (++try < GFARM_AUTH_RETRY_MAX &&
	    error == GFARM_AUTH_ERROR_EXPIRED);

	e = xxx_proto_send(conn, "i", GFARM_AUTH_SHAREDSECRET_GIVEUP);
	if (e != NULL)
		return (e);
	e = xxx_proto_recv(conn, 0, &eof, "i", &error_ignore);
	if (e != NULL)
		return (e);
	if (eof)
		return (GFARM_ERR_PROTOCOL);

	switch (error) {
	case GFARM_AUTH_ERROR_NOT_SUPPORTED:
		return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
	case GFARM_AUTH_ERROR_EXPIRED:
		return (GFARM_ERR_EXPIRED);
	default:
		return (GFARM_ERR_AUTHENTICATION);
	}
}

char *
gfarm_auth_request(struct xxx_connection *conn,
	char *name, struct sockaddr *addr)
{
	char *e, *e_save = NULL;
	int i, eof;
	gfarm_int32_t methods, server_methods; /* bitset */
	gfarm_int32_t method; /* enum gfarm_auth_method */
	gfarm_int32_t error; /* enum gfarm_auth_error */
	size_t nmethods;
	unsigned char methods_buffer[GFARM_AUTH_METHODS_BUFFER_SIZE];

	assert(GFARM_AUTH_METHOD_NUMBER <= sizeof(gfarm_int32_t) * CHAR_BIT);

	methods = gfarm_auth_method_get_enabled_by_name_addr(name, addr);
	if (methods == 0)
		return ("gfarm auth method isn't available for the host");
	methods &= gfarm_auth_method_get_available();
	if (methods == 0)
		return ("usable auth-method isn't configured");

	e = xxx_proto_recv(conn, 0, &eof, "b", sizeof(methods_buffer),
	    &nmethods, methods_buffer);
	if (e != NULL)
		return (e);
	if (eof)
		return (GFARM_ERR_PROTOCOL);

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
		e = xxx_proto_send(conn, "i", method);
		if (e != NULL)
			return (e);
		e = xxx_proto_recv(conn, 1, &eof, "i", &error);
		if (error != GFARM_AUTH_ERROR_NO_ERROR)
			return (GFARM_ERR_PROTOCOL);
		if (method == GFARM_AUTH_METHOD_NONE) {
			/* give up */
			if (server_methods == 0)
				return (GFARM_ERR_PERMISSION_DENIED);
			if ((methods & server_methods) == 0)
				return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
			return (e_save != NULL ? e_save :
			    "gfarm_auth_request: implementation error");
		}
		e = (*gfarm_auth_trial_table[i].request)(conn);
		if (e == NULL)
			return (NULL); /* success */
		if (e != GFARM_ERR_PROTOCOL_NOT_SUPPORTED &&
		    e != GFARM_ERR_EXPIRED &&
		    e != GFARM_ERR_AUTHENTICATION) {
			/* protocol error */
			return (e);
		}
		e_save = e;
	}
}
