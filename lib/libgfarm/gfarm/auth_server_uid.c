#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "auth.h"
#include "gfm_client.h"

static gfarm_error_t gfarm_auth_uid_to_global_username_panic(void *,
	const char *, char **);

gfarm_error_t (*gfarm_auth_uid_to_global_username_table[])(void *,
	const char *, char **) = {
/*
 * This table entry should be ordered by enum gfarm_auth_method.
 */
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_NONE*/
 gfarm_auth_uid_to_global_username_panic,
					/* GFARM_AUTH_METHOD_SHAREDSECRET_V2 */
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_GSI_OLD*/
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_GSI_V2*/
 gfarm_auth_uid_to_global_username_panic, /*GFARM_AUTH_METHOD_GSI_AUTH_V2*/
 gfarm_auth_uid_to_global_username_sharedsecret,
					/* GFARM_AUTH_METHOD_SHAREDSECRET */
#ifdef HAVE_GSI
 gfarm_auth_uid_to_global_username_gsi,		/*GFARM_AUTH_METHOD_GSI*/
 gfarm_auth_uid_to_global_username_gsi,		/*GFARM_AUTH_METHOD_GSI_AUTH*/
#else
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_GSI*/
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_GSI_AUTH*/
#endif
};

gfarm_error_t
gfarm_auth_uid_to_global_username_panic(void *closure,
	const char *auth_user_id, char **global_usernamep)
{
	gflog_fatal(GFARM_MSG_1000055,
	    "gfarm_auth_uid_to_global_username_panic: "
	    "authorization assertion failed");
	return (GFARM_ERR_PROTOCOL);
}

gfarm_error_t
gfarm_auth_uid_to_global_username_sharedsecret(void *closure,
	const char *auth_user_id, char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	gfarm_error_t e, e2;
	char *global_username;
	struct gfarm_user_info ui;

	/*
	 * In sharedsecret, auth_user_id is a Gfarm global username,
	 * and we have to verity it, at least.
	 */
	e = gfm_client_user_info_get_by_names(gfm_server,
	    1, &auth_user_id, &e2, &ui);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001078,
			"getting user info by names failed (%s): %s",
			auth_user_id,
			gfarm_error_string(e));
		return (e);
	}
	if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001079,
			"getting user info by names failed (%s): %s",
			auth_user_id,
			gfarm_error_string(e2));
		return (e2);
	}

	gfarm_user_info_free(&ui);
	if (global_usernamep == NULL)
		return (GFARM_ERR_NO_ERROR);
	global_username = strdup(auth_user_id);
	if (global_username == NULL) {
		gflog_debug(GFARM_MSG_1001080,
			"allocation of string 'global_username' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	*global_usernamep = global_username;
	return (GFARM_ERR_NO_ERROR);
}

/* only called in case of gfarm_auth_id_type == GFARM_AUTH_ID_TYPE_USER */
gfarm_error_t
gfarm_auth_uid_to_global_username(void *closure,
	enum gfarm_auth_method auth_method,
	const char *auth_user_id,
	char **global_usernamep)
{
	if (auth_method < GFARM_AUTH_METHOD_NONE ||
	    auth_method >=
	    GFARM_ARRAY_LENGTH(gfarm_auth_uid_to_global_username_table)) {
		gflog_error(GFARM_MSG_1000056,
		    "gfarm_auth_uid_to_global_username: method=%d/%d",
		    auth_method,
		    (int)GFARM_ARRAY_LENGTH(
		    gfarm_auth_uid_to_global_username_table));
		return (gfarm_auth_uid_to_global_username_panic(closure,
		    auth_user_id, global_usernamep));
	}
	return (gfarm_auth_uid_to_global_username_table[auth_method](
	    closure, auth_user_id, global_usernamep));
}
