#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <pwd.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "xxx_proto.h"
#include "gfutil.h"
#include "hostspec.h"
#include "auth.h"

#ifdef HAVE_GSI
#include "io_gfsl.h" /* XXX - gfarm_authorize_gsi() */
#endif

static char *gfarm_authorize_panic(struct xxx_connection *, int, char *,
	char **);
static char *gfarm_authorize_sharedsecret(struct xxx_connection *, int, char *,
        char **);

char *(*gfarm_authorization_table[])(struct xxx_connection *, int, char *,
	char **) = {
	/*
	 * This table entry should be ordered by enum gfarm_auth_method.
	 */
	gfarm_authorize_panic,		/* GFARM_AUTH_METHOD_NONE */
	gfarm_authorize_sharedsecret,	/* GFARM_AUTH_METHOD_SHAREDSECRET */
#ifdef HAVE_GSI
	gfarm_authorize_gsi,		/* GFARM_AUTH_METHOD_GSI */
#else
	gfarm_authorize_panic,		/* GFARM_AUTH_METHOD_GSI */
#endif
};

static char *
gfarm_authorize_panic(struct xxx_connection *conn, int switch_to,
	char *hostname, char **global_usernamep)
{
	gflog_fatal("gfarm_authorize", "authorization assertion failed");
	return (GFARM_ERR_PROTOCOL);
}

static char *
gfarm_auth_sharedsecret_response(struct xxx_connection *conn, char *homedir)
{
	char *e;
	gfarm_uint32_t request, expire, expire_expected;
	size_t len;
	gfarm_int32_t error = GFARM_AUTH_ERROR_EXPIRED; /* gfarm_auth_error */
	int eof, try = 0;
	char challenge[GFARM_AUTH_CHALLENGE_LEN];
	char response[GFARM_AUTH_RESPONSE_LEN];
	char shared_key_expected[GFARM_AUTH_SHARED_KEY_LEN];
	char response_expected[GFARM_AUTH_RESPONSE_LEN];

	/* NOTE: `homedir' may be NULL, if invalid username is requested. */

	for (;;) {
		++try;
		e = xxx_proto_recv(conn, 0, &eof, "i", &request);
		if (e != NULL) {
			gflog_error("auth_sharedsecret_request", e);
			return (e);
		}
		if (eof) {
			gflog_error("auth_sharedsecret-request",
			    "unexpected EOF");
			return (GFARM_ERR_PROTOCOL);
		}
		if (request != GFARM_AUTH_SHAREDSECRET_GIVEUP &&
		    request != GFARM_AUTH_SHAREDSECRET_MD5) {
			error = GFARM_AUTH_ERROR_NOT_SUPPORTED;
			e = xxx_proto_send(conn, "i", error);
			if (e != NULL)
				return (e);
			continue;
		}
		e = xxx_proto_send(conn, "i", GFARM_AUTH_ERROR_NO_ERROR);
		if (e != NULL)
			return (e);
		if (request == GFARM_AUTH_SHAREDSECRET_GIVEUP) {
			e = xxx_proto_flush(conn);
			if (e != NULL)
				return (e);
			if (try > 1) {
				switch (error) {
				case GFARM_AUTH_ERROR_EXPIRED:
					return (GFARM_ERR_EXPIRED);
				case GFARM_AUTH_ERROR_NOT_SUPPORTED:
					return (
					    GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
				}
			}
			return (GFARM_ERR_AUTHENTICATION);
		}

		gfarm_auth_random(challenge, sizeof(challenge));
		e = xxx_proto_send(conn, "b", sizeof(challenge), challenge);
		if (e != NULL)
			return (e);
		e = xxx_proto_recv(conn, 0, &eof, "ib",
		    &expire, sizeof(response), &len, response);
		if (e != NULL) {
			gflog_error("auth_sharedsecret_response", e);
			return (e);
		}
		if (eof) {
			gflog_error("auth_sharedsecret_response",
				"unexpected EOF");
			return (GFARM_ERR_PROTOCOL);
		}
		/*
		 * Note that gfarm_auth_shared_key_get() should be called
		 * after the above xxx_proto_recv(), otherwise
		 * client (re)generated shared key may not be accessible.
		 */
		if (homedir == NULL) {
			error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
		} else if ((e = gfarm_auth_shared_key_get(&expire_expected,
					shared_key_expected, homedir,
					GFARM_AUTH_SHARED_KEY_GET)) != NULL &&
			   e != GFARM_ERR_EXPIRED) {
			error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
			gflog_error("auth_sharedsecret: read-key", e);
		} else if (time(0) >= expire) {
			/* may reach here if (e == GFARM_ERR_EXPIRED) */
			error = GFARM_AUTH_ERROR_EXPIRED;
			gflog_warning("auth_sharedsecret", "key expired");
		} else {
			/* may also reach here if (e == GFARM_ERR_EXPIRED) */
			gfarm_auth_sharedsecret_response_data(
			    shared_key_expected, challenge,
			    response_expected);
			if (expire != expire_expected) {
				error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
				gflog_error("auth_sharedsecret",
				    "expire time mismatch");
			} else if (memcmp(response, response_expected,
			    sizeof(response)) != 0) {
				error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
				gflog_error("auth_sharedsecret",
				    "response mismatch");
			} else {
				error = GFARM_AUTH_ERROR_NO_ERROR;
			}
		}
		e = xxx_proto_send(conn, "i", error);
		if (e != NULL)
			return (e);
		if (error == GFARM_AUTH_ERROR_NO_ERROR) {
			e = xxx_proto_flush(conn);
			if (e != NULL)
				return (e);
			return (NULL); /* success */
		}
	}
}

static char *
gfarm_authorize_sharedsecret(struct xxx_connection *conn, int switch_to,
	char *hostname, char **global_usernamep)
{
	char *e, *global_username, *local_username, *aux, *msg;
	int eof;
	struct passwd *pwd;
	uid_t o_uid;
	gid_t o_gid;
	static char method[] = "auth=sharedsecret local_user=";

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	o_uid = o_gid = 0;
#endif
	e = xxx_proto_recv(conn, 0, &eof, "s", &global_username);
	if (e != NULL) {
		gflog_error("authorize_sharedsecret", "reading username");
		return (e);
	}
	if (eof) {
		gflog_error("authorize_sharedsecret", "unexpected EOF");
		return (GFARM_ERR_PROTOCOL);
	}

	aux = malloc(strlen(global_username) + 1 + strlen(hostname) + 1);
	if (aux == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(aux, "%s@%s", global_username, hostname);
	gflog_set_auxiliary_info(aux);

	e = gfarm_global_to_local_username(global_username, &local_username);
	if (e != NULL) {
		pwd = NULL;
		gflog_error("authorize_sharedsecret",
		    "cannot map global username into local username");
	} else {
		pwd = getpwnam(local_username);
		if (pwd == NULL)
			gflog_error(local_username, "authorize_sharedsecret: "
			    "local account doesn't exist");
	}

	if (pwd != NULL) {
		/*
		 * first, switch to the user's privilege
		 * to read ~/.gfarm_shared_key.
		 *
		 * NOTE: reading this file with root privilege may not work,
		 *	if home directory is NFS mounted and root access for
		 *	the home directory partition is not permitted.
		 *
		 * Do not switch the user ID of the current process here
		 * even in the switch_to case, because it is necessary to
		 * switch back to the original user ID when
		 * gfarm_auth_sharedsecret fails.
		 */
		o_gid = getegid();
		o_uid = geteuid();
		initgroups(pwd->pw_name, pwd->pw_gid);
		setegid(pwd->pw_gid);
		seteuid(pwd->pw_uid);
	}
	e = gfarm_auth_sharedsecret_response(conn,
		pwd == NULL ? NULL : pwd->pw_dir);
	if (pwd != NULL) {
		setegid(o_gid);
		seteuid(o_uid);
	}
	/* if (pwd == NULL), (e != NULL) must be true */
	if (e != NULL) {
		free(gflog_get_auxiliary_info());
		gflog_set_auxiliary_info(NULL);
		free(global_username);
		if (local_username != NULL)
			free(local_username);
		return (e);
	}

	msg = malloc(sizeof(method) + strlen(local_username));
	if (msg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(msg, "%s%s", method, local_username);
	gflog_notice("authenticated", msg);
	free(msg);

	if (switch_to) {
		/*
		 * because the name returned by getlogin() is
		 * an attribute of a session on 4.4BSD derived OSs,
		 * we should create new session before calling
		 * setlogin().
		 */
		setsid();
#ifdef HAVE_SETLOGIN
		setlogin(pwd->pw_name);
#endif
		setgid(pwd->pw_gid);
		setuid(pwd->pw_uid);

		/* assert(local_username != NULL); */
		gfarm_set_global_username(global_username);
		gfarm_set_local_username(local_username);
		gfarm_set_local_homedir(pwd->pw_dir);
	} else {
		free(gflog_get_auxiliary_info());
		gflog_set_auxiliary_info(NULL);
		if (global_usernamep == NULL) /* see below */
			free(global_username);
	}
	/*
	 * global_username may continue to be refered,
	 * if (switch_to) as gflog_set_auxiliary_info()
	 * else if (global_usernamep != NULL) as *global_usernamep
	 */
	free(local_username);
	if (global_usernamep != NULL)
		*global_usernamep = global_username;
	return (NULL);
}

/*
 * the `switch_to' flag has the following side effects:
 *      - the privilege of this program will switch to the authenticated user.
 *      - gflog_set_auxiliary_info("user@hostname") will be called.
 *        thus, the caller of gfarm_authorize() must call the following later:
 *              char *aux = gflog_get_auxiliary_info();
 *              gflog_get_auxiliary_info(NULL);
 *              free(aux);
 *      - gfarm_get_local_username(), gfarm_get_local_homedir() and
 *        gfarm_get_global_username() become available.
 *
 * note that the user's account is not always necessary on this host,
 * if the `switch_to' flag isn't set. but also note that some
 * authentication methods (e.g. gfarm-sharedsecret-auth) require the user's
 * local account anyway even if the `switch_to' isn't set.
 */
char *
gfarm_authorize(struct xxx_connection *conn, int switch_to,
		char **global_usernamep)
{
	char *e, *name, *log_header;
	gfarm_int32_t methods; /* bitset of enum gfarm_auth_method */
	gfarm_int32_t method; /* enum gfarm_auth_method */
	gfarm_int32_t error; /* enum gfarm_auth_error */
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);
	int rv = getpeername(xxx_connection_fd(conn), &addr, &addrlen);
	int i, eof, try = 0;
	size_t nmethods;
	unsigned char methods_buffer[CHAR_BIT * sizeof(gfarm_int32_t)];
	static char name_header[] = "authorize: ";
	char namebuf[sizeof(name_header) + GFARM_SOCKADDR_STRLEN];

	assert(GFARM_ARRAY_LENGTH(gfarm_authorization_table) ==
	    GFARM_AUTH_METHOD_NUMBER);

	if (rv == -1)
		return (gfarm_errno_to_error(errno));
	e = gfarm_sockaddr_to_name(&addr, &name);
	if (e == NULL) {
		log_header = name;
	} else {
		strcpy(namebuf, name_header);
		gfarm_sockaddr_to_string(&addr,
		    namebuf + sizeof(name_header) - 1, GFARM_SOCKADDR_STRLEN);
		gflog_warning(namebuf, e);
		log_header = namebuf;
		name = NULL;
	}

	methods = gfarm_auth_method_get_enabled_by_name_addr(name, &addr);
	if (methods == 0) {
		gflog_error(log_header, "access refused");
	} else {
		methods &= gfarm_auth_method_get_available();
		if (methods == 0)
			gflog_error(log_header, "auth-method not configured");
	}

	nmethods = 0;
	for (i = GFARM_AUTH_METHOD_NONE + 1; i < GFARM_AUTH_METHOD_NUMBER &&
	    i < CHAR_BIT * sizeof(gfarm_int32_t); i++) {
		if ((methods & (1 << i)) != 0)
			methods_buffer[nmethods++] = i;
	}
	e = xxx_proto_send(conn, "b", nmethods, methods_buffer);
	if (e != NULL)
		return (e);
	for (;;) {
		++try;
		e = xxx_proto_recv(conn, 0, &eof, "i", &method);
		if (e != NULL)
			return (e);
		if (eof)
			return (GFARM_ERR_PROTOCOL);
		if (method == GFARM_AUTH_METHOD_NONE)
			error = GFARM_AUTH_ERROR_NO_ERROR;
		else if (method >= GFARM_AUTH_METHOD_NUMBER)
			error = GFARM_AUTH_ERROR_NOT_SUPPORTED;
		else if (method <= GFARM_AUTH_METHOD_NONE ||
		    ((1 << method) & methods) == 0)
			error = GFARM_AUTH_ERROR_DENIED;
		else
			error = GFARM_AUTH_ERROR_NO_ERROR;
		e = xxx_proto_send(conn, "i", error);
		if (e != NULL)
			return (e);
		e = xxx_proto_flush(conn);
		if (e != NULL)
			return (e);
		if (method == GFARM_AUTH_METHOD_NONE) {
			/* client gave up */
			if (methods == 0)
				return (GFARM_ERR_PERMISSION_DENIED);
			if (try == 1) {
				/*
				 * there is no usable auth-method
				 * between client and server.
				 */
				gflog_error(log_header,
				    "auth-method not match");
				return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
			}
			return (GFARM_ERR_AUTHENTICATION);
		}
		if (error != GFARM_AUTH_ERROR_NO_ERROR) {
			gflog_error(log_header, "incorrect auth-method reply");
			return (GFARM_ERR_PROTOCOL);
		}

		e = (*gfarm_authorization_table[method])(conn, switch_to,
			log_header, global_usernamep);
		if (e != GFARM_ERR_PROTOCOL_NOT_SUPPORTED &&
		    e != GFARM_ERR_EXPIRED &&
		    e != GFARM_ERR_AUTHENTICATION) {
			/* success if (e == NULL), or protocol error */
			return (e);
		}
	}
}
