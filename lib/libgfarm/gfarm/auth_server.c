#include <pthread.h>
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
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "thrsubr.h"
#include "gfutil.h"

#include "context.h"
#include "liberror.h"
#include "hostspec.h"
#include "auth.h"
#include "gfp_xdr.h"

#include "gfs_proto.h" /* for GFSD_USERNAME, XXX layering violation */
#include "gfm_proto.h" /* for GFSM_USERNAME, XXX layering violation */

static int gfarm_auth_server_method_is_always_available(void);
static int gfarm_auth_server_method_is_never_available(void);
static gfarm_error_t gfarm_authorize_panic(struct gfp_xdr *,
	char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_role *,
	    char **), void *,
	enum gfarm_auth_id_role *, char **);

struct gfarm_auth_server_method {
	int (*is_available)(void);
	gfarm_error_t (*authorize)(struct gfp_xdr *, char *, char *,
		gfarm_error_t (*)(void *,
		    enum gfarm_auth_method, const char *,
		    enum gfarm_auth_id_role *, char **), void *,
		    enum gfarm_auth_id_role *, char **);
};

/*
 * This table entries should be ordered by enum gfarm_auth_method.
 */
static const struct gfarm_auth_server_method gfarm_auth_server_table[] = {
	{ /* GFARM_AUTH_METHOD_NONE */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
	{ /* GFARM_AUTH_METHOD_SHAREDSECRET */
		gfarm_auth_server_method_is_always_available,
		gfarm_authorize_sharedsecret,
	},
	{ /* GFARM_AUTH_METHOD_GSI_OLD */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
#ifdef HAVE_GSI
	{ /* GFARM_AUTH_METHOD_GSI */
		gfarm_auth_server_method_is_gsi_available,
		gfarm_authorize_gsi,
	},
	{ /* GFARM_AUTH_METHOD_GSI_AUTH */
		gfarm_auth_server_method_is_gsi_available,
		gfarm_authorize_gsi_auth,
	},
#else
	{ /* GFARM_AUTH_METHOD_GSI */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
	{ /* GFARM_AUTH_METHOD_GSI_AUTH */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
#endif
#ifdef HAVE_TLS_1_3
	{ /* GFARM_AUTH_METHOD_TLS_SHAREDSECRET */
		gfarm_auth_server_method_is_tls_sharedsecret_available,
		gfarm_authorize_tls_sharedsecret,
	},
	{ /* GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE */
		gfarm_auth_server_method_is_tls_client_certificate_available,
		gfarm_authorize_tls_client_certificate,
	},
#else
	{ /* GFARM_AUTH_METHOD_TLS_SHAREDSECRET */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
	{ /* GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
#endif
#ifdef HAVE_KERBEROS
	{ /* GFARM_AUTH_METHOD_KERBEROS */
		gfarm_auth_server_method_is_kerberos_available,
		gfarm_authorize_kerberos,
	},
	{ /* GFARM_AUTH_METHOD_KERBEROS_AUTH */
		gfarm_auth_server_method_is_kerberos_available,
		gfarm_authorize_kerberos_auth,
	},
#else
	{ /* GFARM_AUTH_METHOD_KERBEROS */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
	{ /* GFARM_AUTH_METHOD_KERBEROS_AUTH */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
#endif
#if defined(HAVE_CYRUS_SASL) && defined(HAVE_TLS_1_3)
	{ /* GFARM_AUTH_METHOD_SASL */
		gfarm_auth_server_method_is_sasl_available,
		gfarm_authorize_sasl,
	},
	{ /* GFARM_AUTH_METHOD_SASL_AUTH */
		gfarm_auth_server_method_is_sasl_available,
		gfarm_authorize_sasl_auth,
	},
#else
	{ /* GFARM_AUTH_METHOD_SASL */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
	{ /* GFARM_AUTH_METHOD_SASL_AUTH */
		gfarm_auth_server_method_is_never_available,
		gfarm_authorize_panic,
	},
#endif
};

gfarm_int32_t
gfarm_auth_server_method_get_available(void)
{
	int i;
	gfarm_int32_t methods;

	assert(GFARM_AUTH_METHOD_NUMBER <= sizeof(gfarm_int32_t) * CHAR_BIT);
	assert(GFARM_ARRAY_LENGTH(gfarm_auth_server_table) ==
	    GFARM_AUTH_METHOD_NUMBER);

	methods = 0;
	for (i = GFARM_AUTH_METHOD_NONE + 1; i < GFARM_AUTH_METHOD_NUMBER;
	    i++) {
		if (gfarm_auth_server_table[i].is_available())
			methods |= 1 << i;
	}
	return (methods);
}

static int
gfarm_auth_server_method_is_always_available(void)
{
	return (1);
}

static int
gfarm_auth_server_method_is_never_available(void)
{
	return (0);
}

static gfarm_error_t
gfarm_authorize_panic(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	gflog_fatal(GFARM_MSG_1000021,
	    "gfarm_authorize: authorization assertion failed");
	return (GFARM_ERR_PROTOCOL);
}

/*
 * gfsd_shared_key_cache: cache of ~_gfarmfs/.gfarm_shared_key
 * to reduce gfarm_privilege_lock contention in gfmd
 */

static const char gfsd_shared_key_cache_mutex_diag[] =
	"gfsd_shared_key_cache_mutex";
static pthread_mutex_t gfsd_shared_key_cache_mutex =
	GFARM_MUTEX_INITIALIZER(gfsd_shared_key_cache_mutex);
static int gfsd_shared_key_cache_avaiable = 0;
static gfarm_uint32_t gfsd_shared_key_cache_expire;
static char gfsd_shared_key_cache[GFARM_AUTH_SHARED_KEY_LEN];

static gfarm_error_t
gfsd_shared_key_cache_get(
	gfarm_uint32_t *expire_expected, char *shared_key_expected)
{
	gfarm_error_t e;
	static const char diag[] = "gfsd_shared_key_cache_get";

	gfarm_mutex_lock(&gfsd_shared_key_cache_mutex,
	    diag, gfsd_shared_key_cache_mutex_diag);

	if (!gfsd_shared_key_cache_avaiable) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if (time(0) >= gfsd_shared_key_cache_expire) {
		e = GFARM_ERR_EXPIRED;
	} else {
		e = GFARM_ERR_NO_ERROR;
		*expire_expected = gfsd_shared_key_cache_expire;
		memcpy(shared_key_expected, gfsd_shared_key_cache,
		    sizeof(gfsd_shared_key_cache));
	}

	gfarm_mutex_unlock(&gfsd_shared_key_cache_mutex,
	    diag, gfsd_shared_key_cache_mutex_diag);

	return (e);
}

static void
gfsd_shared_key_cache_set(gfarm_uint32_t expire, const char *shared_key)
{
	static const char diag[] = "gfsd_shared_key_cache_set";

	gfarm_mutex_lock(&gfsd_shared_key_cache_mutex,
	    diag, gfsd_shared_key_cache_mutex_diag);

	gfsd_shared_key_cache_avaiable = 1;
	gfsd_shared_key_cache_expire = expire;
	memcpy(gfsd_shared_key_cache, shared_key,
	    sizeof(gfsd_shared_key_cache));

	gfarm_mutex_unlock(&gfsd_shared_key_cache_mutex,
	    diag, gfsd_shared_key_cache_mutex_diag);
}

/*
 * sharedsecret authorization
 */

static gfarm_error_t
gfarm_auth_sharedsecret_giveup_response(struct gfp_xdr *conn,
	const char *hostname, const char *global_username,
	int try, gfarm_int32_t error /* gfarm_auth_error */)
{
	gfarm_error_t e;

	e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1003571,
		    "(%s@%s) auth_sharedsecret: cut: %s",
		    global_username, hostname, gfarm_error_string(e));
	} else if (try <= 1) {
		e = GFARM_ERR_AUTHENTICATION;
		gflog_info(GFARM_MSG_1003572,
		    "(%s@%s) auth_sharedsecret: scaned: %s",
		    global_username, hostname, gfarm_error_string(e));
	} else {
		switch (error) {
		case GFARM_AUTH_ERROR_EXPIRED:
			e = GFARM_ERR_EXPIRED;
			break;
		case GFARM_AUTH_ERROR_NOT_SUPPORTED:
			e = GFARM_ERR_PROTOCOL_NOT_SUPPORTED;
			break;
		default:
			e = GFARM_ERR_AUTHENTICATION;
			break;
		}
		gflog_info(GFARM_MSG_1003573,
		    "(%s@%s) auth_sharedsecret: gives up: %s",
		    global_username, hostname, gfarm_error_string(e));
	}
	return (e);
}

static enum gfarm_auth_error
gfarm_auth_sharedsecret_compare(
	const char *hostname, const char *global_username,
	gfarm_uint32_t expire, char *challenge,
	char *response, size_t response_size,
	gfarm_uint32_t expire_expected, char *shared_key_expected,
	const char *diag)
{
	gfarm_error_t e;
	char response_expected[GFARM_AUTH_RESPONSE_LEN];
	enum gfarm_auth_error error;

	/* may also reach here even if shared_key_expected is expired */
	e = gfarm_auth_sharedsecret_response_data(
	    shared_key_expected, challenge, response_expected);
	if (expire != expire_expected) {
		error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
		gflog_info(GFARM_MSG_1004489,
		    "(%s@%s) auth_sharedsecret: expire time mismatch%s",
		    global_username, hostname, diag);
	} else if (e != GFARM_ERR_NO_ERROR) {
		error = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
		gflog_error(GFARM_MSG_1004517,
		    "(%s@%s) auth_sharedsecret: "
		    "calculating challenge-response: %s",
		    global_username, hostname, gfarm_error_string(e));
	} else if (memcmp(response, response_expected, response_size) != 0) {
		error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
		gflog_info(GFARM_MSG_1004490,
		    "(%s@%s) auth_sharedsecret: key mismatch%s",
		    global_username, hostname, diag);
	} else { /* success */
		error = GFARM_AUTH_ERROR_NO_ERROR;
	}
	return (error);
}

static enum gfarm_auth_error
gfarm_auth_sharedsecret_check(
	const char *hostname, const char *global_username, struct passwd *pwd,
	gfarm_uint32_t expire, char *challenge,
	char *response, size_t response_size)
{
	gfarm_error_t e;
	gfarm_uint32_t expire_expected;
	char shared_key_expected[GFARM_AUTH_SHARED_KEY_LEN];
	enum gfarm_auth_error error;
	int gfsd_shared_key_cache_needs_update = 0;

	if (strcmp(global_username, GFSD_USERNAME) == 0) {
		e = gfsd_shared_key_cache_get(
		    &expire_expected, shared_key_expected);
		if (e == GFARM_ERR_NO_ERROR) {
			error = gfarm_auth_sharedsecret_compare(
			    hostname, global_username,
			    expire, challenge, response, response_size,
			    expire_expected, shared_key_expected,
			    " with cache");
			if (error == GFARM_AUTH_ERROR_NO_ERROR)
				return (error);
		}
		gfsd_shared_key_cache_needs_update = 1;
	}

	e = gfarm_auth_shared_key_get(&expire_expected, shared_key_expected,
	    pwd->pw_dir, pwd, GFARM_AUTH_SHARED_KEY_GET, 0);
	if (e == GFARM_ERR_NO_ERROR && gfsd_shared_key_cache_needs_update) {
		gfsd_shared_key_cache_set(
		    expire_expected, shared_key_expected);
	}

	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_EXPIRED) {
		error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
		gflog_info(GFARM_MSG_1003576,
		    "(%s@%s) auth_sharedsecret: .gfarm_shared_key: %s",
		    global_username, hostname, gfarm_error_string(e));
	} else if (time(0) >= expire) {
		/* may reach here if (e == GFARM_ERR_EXPIRED) */
		error = GFARM_AUTH_ERROR_EXPIRED;
		gflog_info(GFARM_MSG_1003577,
		    "(%s@%s) auth_sharedsecret: key expired",
		    global_username, hostname);
	} else {
		/* may also reach here if (e == GFARM_ERR_EXPIRED) */
		error = gfarm_auth_sharedsecret_compare(
		    hostname, global_username,
		    expire, challenge, response, response_size,
		    expire_expected, shared_key_expected, "");
	}
	return (error);
}

static gfarm_error_t
gfarm_auth_sharedsecret_md5_response(struct gfp_xdr *conn,
	const char *hostname, const char *global_username,
	struct passwd *pwd, gfarm_int32_t *errorp)
{
	int eof;
	size_t len;
	gfarm_uint32_t expire;
	char challenge[GFARM_AUTH_CHALLENGE_LEN];
	char response[GFARM_AUTH_RESPONSE_LEN];
	gfarm_int32_t error; /* gfarm_auth_error */
	gfarm_error_t e;

	gfarm_auth_random(challenge, sizeof(challenge));
	e = gfp_xdr_send(conn, "b", sizeof(challenge), challenge);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1003574,
		    "(%s@%s) auth_sharedsecret: challenge: %s",
		    global_username, hostname, gfarm_error_string(e));
		return (e);
	}
	e = gfp_xdr_recv(conn, 0, &eof, "ib",
	    &expire, sizeof(response), &len, response);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1003575,
		    "(%s@%s) auth_sharedsecret: response: %s",
		    global_username, hostname, gfarm_error_string(e));
		return (e);
	}
	if (eof) {
		gflog_info(GFARM_MSG_1000027, "(%s@%s) auth_sharedsecret: "
		    "unexpected EOF in response", global_username, hostname);
		return (GFARM_ERR_PROTOCOL);
	}
	/*
	 * Note that gfarm_auth_shared_key_get() should be called
	 * after the above gfp_xdr_recv(), otherwise
	 * client (re)generated shared key may not be accessible.
	 */
	if (pwd == NULL) {
		/* *errorp should have a valid value only in this case */
		error = *errorp;
		gflog_debug(GFARM_MSG_1003723, "Password is null (%d)",
		    (int)error);
		/* already logged at gfarm_authorize_sharedsecret() */
	} else {
		error = gfarm_auth_sharedsecret_check(
		    hostname, global_username, pwd,
		    expire, challenge, response, sizeof(response));
	}
	*errorp = error;
	e = gfp_xdr_send(conn, "i", error);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1003579,
		    "(%s@%s) auth_sharedsecret: send result: %s",
		    global_username, hostname, gfarm_error_string(e));
		return (e);
	}
	if (error == GFARM_AUTH_ERROR_NO_ERROR) {
		e = gfp_xdr_flush(conn);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_1003580,
			    "(%s@%s) auth_sharedsecret: completion: %s",
			    global_username, hostname, gfarm_error_string(e));
			return (e);
		}
		return (GFARM_ERR_NO_ERROR); /* success */
	}
	return (GFARM_ERRMSG_AUTH_SHAREDSECRET_MD5_CONTINUE);
}

static gfarm_error_t
gfarm_auth_sharedsecret_response(struct gfp_xdr *conn,
	const char *hostname, const char *global_username, struct passwd *pwd,
	enum gfarm_auth_error pwd_error)
{
	gfarm_error_t e;
	gfarm_uint32_t request;
	gfarm_int32_t error = GFARM_AUTH_ERROR_EXPIRED; /* gfarm_auth_error */
	int eof, try = 0;

	/* NOTE: `pwd' may be NULL. pwd_error shows the reason in that case */

	for (;;) {
		++try;
		e = gfp_xdr_recv(conn, 0, &eof, "i", &request);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_1003581,
			    "(%s@%s) auth_sharedsecret_response: %s",
			    global_username, hostname, gfarm_error_string(e));
			return (e);
		}
		if (eof) {
			gflog_info(GFARM_MSG_1003582,
			    "(%s@%s) auth_sharedsecret_response: "
			    "unexpected EOF", global_username, hostname);
			return (GFARM_ERR_PROTOCOL);
		}
		switch (request) {
		case GFARM_AUTH_SHAREDSECRET_MD5:
		case GFARM_AUTH_SHAREDSECRET_GIVEUP:
			e = gfp_xdr_send(conn, "i", GFARM_AUTH_ERROR_NO_ERROR);
			break;
		default:
			error = GFARM_AUTH_ERROR_NOT_SUPPORTED;
			e = gfp_xdr_send(conn, "i", error);
			break;
		}
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_1003583,
			    "(%s@%s) auth_sharedsecret: key query: %s",
			    global_username, hostname, gfarm_error_string(e));
			return (e);
		}
		switch (request) {
		case GFARM_AUTH_SHAREDSECRET_GIVEUP:
			return (gfarm_auth_sharedsecret_giveup_response(
			    conn, hostname, global_username, try, error));
		case GFARM_AUTH_SHAREDSECRET_MD5:
			if (pwd == NULL)
				error = pwd_error;
			e = gfarm_auth_sharedsecret_md5_response(
			    conn, hostname, global_username, pwd, &error);
			if (e != GFARM_ERRMSG_AUTH_SHAREDSECRET_MD5_CONTINUE)
				return (e);
		default:
			e = gfp_xdr_flush(conn);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_info(GFARM_MSG_1003584,
				    "(%s@%s) auth_sharedsecret: request "
				    "response: %s", global_username, hostname,
				    gfarm_error_string(e));
				return (e);
			}
			break;
		}
	}
}

gfarm_error_t
gfarm_authorize_sharedsecret_common(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	const char *auth_method_name,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	gfarm_error_t e;
	char *global_username, *local_username, *buf = NULL;
	int eof;
	enum gfarm_auth_id_role peer_role;
	enum gfarm_auth_error error = GFARM_AUTH_ERROR_DENIED; /* to be safe */
	struct passwd pwbuf, *pwd;

	e = gfp_xdr_recv(conn, 0, &eof, "s", &global_username);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: authorize %s: reading username",
		    hostname, auth_method_name);
		return (e);
	}
	if (eof) {
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: authorize %s: unexpected EOF",
		    hostname, auth_method_name);
		return (GFARM_ERR_PROTOCOL);
	}

	if (strcmp(global_username, GFSD_USERNAME) == 0) {
		peer_role = GFARM_AUTH_ID_ROLE_SPOOL_HOST;
	} else if (strcmp(global_username, GFMD_USERNAME) == 0) {
		peer_role = GFARM_AUTH_ID_ROLE_METADATA_HOST;
	} else {
		/*
		 * actually, a protocol-level uid is a gfarm global username
		 * in sharedsecret case.
		 * so, the purpose of (*auth_uid_to_global_user)() is
		 * to verify whether the user does exist or not in this case.
		 */
		peer_role = GFARM_AUTH_ID_ROLE_USER;
		e = (*auth_uid_to_global_user)(closure,
		    GFARM_AUTH_METHOD_SHAREDSECRET,
		    global_username, &peer_role, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "(%s@%s) authorize %s: "
			    "the global username isn't registered in gfmd: %s",
			    global_username, hostname, auth_method_name,
			    gfarm_error_string(e));
			if (e == GFARM_ERR_NO_MEMORY)
				error = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
			else if (e == GFARM_ERR_PROTOCOL)
				error = GFARM_AUTH_ERROR_NOT_SUPPORTED;
			else if (IS_CONNECTION_ERROR(e))
				error = GFARM_AUTH_ERROR_TEMPORARY_FAILURE;
			else
				error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
		}
	}
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfarm_global_to_local_username_by_url(GFARM_PATH_ROOT,
		    global_username, &local_username);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "(%s@%s) authorize %s: "
			    "cannot map global username into local username: "
			    "%s",
			    global_username, hostname, auth_method_name,
			    gfarm_error_string(e));
			/* no memory, or configuration error */
			error = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		local_username = NULL;
		pwd = NULL;
		/* `error' must be already set */
	} else {
		GFARM_MALLOC_ARRAY(buf, gfarm_ctxp->getpw_r_bufsz);
		if (buf == NULL) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "(%s@%s) %s: authorize %s: %s",
			    global_username, hostname, auth_method_name,
			    local_username, gfarm_error_string(e));
			pwd = NULL;
			error = GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE;
		} else if (getpwnam_r(local_username, &pwbuf, buf,
		    gfarm_ctxp->getpw_r_bufsz, &pwd) != 0 || pwd == NULL) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "(%s@%s) %s: authorize %s: "
			    "local account doesn't exist",
			    global_username, hostname, auth_method_name,
			    local_username);
			pwd = NULL;
			error = GFARM_AUTH_ERROR_INVALID_CREDENTIAL;
		}
	}

	/* pwd may be NULL */
	e = gfarm_auth_sharedsecret_response(conn,
	    hostname, global_username, pwd, error);

	/* if (pwd == NULL), must be (e != GFARM_ERR_NO_ERROR) here */
	if (e != GFARM_ERR_NO_ERROR) {
		if (local_username != NULL)
			free(local_username);
		free(global_username);
		if (buf != NULL)
			free(buf);
		return (e);
	}
	assert(local_username != NULL);

	/* succeed, do logging */
	gflog_notice(GFARM_MSG_UNFIXED,
	    "(%s@%s) authenticated: auth=%s local_user=%s",
	    global_username, hostname, auth_method_name, local_username);

	free(local_username);
	if (peer_rolep != NULL)
		*peer_rolep = peer_role;
	if (global_usernamep != NULL)
		*global_usernamep = global_username;
	else
		free(global_username);
	if (buf != NULL)
		free(buf);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_authorize_sharedsecret(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	return (gfarm_authorize_sharedsecret_common(conn,
	    service_tag, hostname, auth_uid_to_global_user, closure,
	    "sharedsecret", peer_rolep, global_usernamep));
}

/*
 * note that the user's account is not always necessary on this host,
 * but also note that some authentication methods (e.g. "sharedsecret")
 * require the user's local account.
 */
gfarm_error_t
gfarm_authorize_wo_setuid(struct gfp_xdr *conn, char *service_tag,
	char *hostname, struct sockaddr *addr,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep,
	enum gfarm_auth_method *auth_methodp)
{
	gfarm_error_t e;
	gfarm_int32_t methods; /* bitset of enum gfarm_auth_method */
	gfarm_int32_t method; /* enum gfarm_auth_method */
	gfarm_int32_t error; /* enum gfarm_auth_error */
	int i, eof, try = 0;
	size_t nmethods;
	unsigned char methods_buffer[CHAR_BIT * sizeof(gfarm_int32_t)];

	assert(GFARM_ARRAY_LENGTH(gfarm_auth_server_table) ==
	    GFARM_AUTH_METHOD_NUMBER);

	methods = gfarm_auth_method_get_enabled_by_name_addr(hostname, addr);
	if (methods == 0) {
		gflog_info(GFARM_MSG_1000046,
		    "%s: refusing access", hostname);
	} else {
		methods &= gfarm_auth_server_method_get_available();
		if (methods == 0)
			gflog_error(GFARM_MSG_1000047,
			    "%s: auth-method not configured",
			    hostname);
	}

	nmethods = 0;
	for (i = GFARM_AUTH_METHOD_NONE + 1; i < GFARM_AUTH_METHOD_NUMBER &&
	    i < CHAR_BIT * sizeof(gfarm_int32_t); i++) {
		if ((methods & (1 << i)) != 0)
			methods_buffer[nmethods++] = i;
	}
	e = gfp_xdr_send(conn, "b", nmethods, methods_buffer);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000048,
		    "%s: %s", hostname, gfarm_error_string(e));
		return (e);
	}
	for (;;) {
		++try;
		e = gfp_xdr_recv(conn, 0, &eof, "i", &method);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_1000049,
			    "%s: %s", hostname, gfarm_error_string(e));
			return (e);
		}
		if (eof) {
			if (try <= 1)
				gflog_notice(GFARM_MSG_1000050,
				    "%s: port scan, or please try to increase "
				    "listen_backlog setting", hostname);
			else
				gflog_info(GFARM_MSG_1000051,
				    "%s: client disappeared", hostname);
			return (GFARM_ERR_PROTOCOL);
		}
		if (method == GFARM_AUTH_METHOD_NONE)
			error = GFARM_AUTH_ERROR_NO_ERROR;
		else if (method >= GFARM_AUTH_METHOD_NUMBER)
			error = GFARM_AUTH_ERROR_NOT_SUPPORTED;
		else if (method <= GFARM_AUTH_METHOD_NONE ||
		    ((1 << method) & methods) == 0)
			error = GFARM_AUTH_ERROR_DENIED;
		else if (gfarm_auth_server_table[method].authorize ==
		    gfarm_authorize_panic)
			error = GFARM_AUTH_ERROR_NOT_SUPPORTED;
		else
			error = GFARM_AUTH_ERROR_NO_ERROR;
		e = gfp_xdr_send(conn, "i", error);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(conn);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_1000052,
			    "%s: %s", hostname, gfarm_error_string(e));
			return (e);
		}
		if (error != GFARM_AUTH_ERROR_NO_ERROR) {
			gflog_notice(GFARM_MSG_1003369,
			    "%s: incorrect auth-method request %d: %d",
			    hostname, (int)method, (int)error);
			return (GFARM_ERR_PROTOCOL);
		}
		if (method == GFARM_AUTH_METHOD_NONE) {
			/* client gave up */
			if (methods == 0) {
				gflog_debug(GFARM_MSG_1001075,
					"Method permission denied: %s",
					gfarm_error_string(
						GFARM_ERR_PERMISSION_DENIED));
				e = GFARM_ERR_PERMISSION_DENIED;
			} else if (try <= 1) {
				/*
				 * there is no usable auth-method
				 * between client and server.
				 */
				gflog_info(GFARM_MSG_1000054,
				    "%s: authentication method "
				    "doesn't match", hostname);
				e = GFARM_ERR_PROTOCOL_NOT_SUPPORTED;
			} else {
				gflog_debug(GFARM_MSG_1001076,
					"Authentication failed: %s",
					gfarm_error_string(
						GFARM_ERR_AUTHENTICATION));
				e = GFARM_ERR_AUTHENTICATION;
			}
			return (e);
		}

		e = (*gfarm_auth_server_table[method].authorize)(conn,
		    service_tag, hostname, auth_uid_to_global_user, closure,
		    peer_rolep, global_usernamep);
		if (!GFARM_AUTH_ERR_TRY_NEXT_METHOD(e)) {
			/* protocol error, or success */
			if (e == GFARM_ERR_NO_ERROR) {
				if (auth_methodp != NULL)
					*auth_methodp = method;
			} else {
				gflog_debug(GFARM_MSG_1001077,
					"Authentication failed host=(%s): %s",
					hostname,
					gfarm_error_string(e));
			}
			return (e);
		}
	}
}
