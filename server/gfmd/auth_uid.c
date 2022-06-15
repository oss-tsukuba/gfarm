#include <gfarm/gfarm_config.h>

#include <stdlib.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "auth.h"

#include "subr.h"
#include "host.h"
#include "mdhost.h"
#include "user.h"
#include "group.h"
#include "gfmd.h"

/*
 * if auth_method is gsi*, kerberos* or tls_client_certificate,
 * gfarm_auth_id_type is GFARM_AUTH_ID_TYPE_{USER,SPOOL_HOST,METADATA_HOST},
 * otherwise gfarm_auth_id_type must be GFARM_AUTH_ID_TYPE_USER.
 *
 * if auth_method is gfsl (i.e. gsi* or kerberos*),
 * *gfarm_auth_id_typep is an output parameter, otherwise an input parameter.
 */
gfarm_error_t
auth_uid_to_global_username(void *closure,
	enum gfarm_auth_method auth_method,
	const char *auth_user_id,
	enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	gfarm_error_t e;
	enum gfarm_auth_id_type auth_user_id_type = *auth_user_id_typep;
	char *global_username;
	struct user *u;
	static const char diag[] = "auth_uid_to_global_username";

	if (GFARM_IS_AUTH_TLS_CLIENT_CERTIFICATE(auth_method) ||
	    GFARM_IS_AUTH_KERBEROS(auth_method)) {
		char *hostname;
		struct host *h;
		struct mdhost *m;

		switch (auth_user_id_type) {
		case GFARM_AUTH_ID_TYPE_UNKNOWN:
			e = GFARM_ERR_INTERNAL_ERROR;
			break;
		case GFARM_AUTH_ID_TYPE_USER:
			break;
		case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
			if (GFARM_IS_AUTH_TLS_CLIENT_CERTIFICATE(
			    auth_method)) {
				e = gfarm_x509_cn_get_hostname(
				    auth_user_id_type, auth_user_id,
				    &hostname);
			} else {
				e = gfarm_kerberos_principal_get_hostname(
				    auth_user_id_type, auth_user_id,
				    &hostname);
			}
			if (e != GFARM_ERR_NO_ERROR)
				return (GFARM_ERR_AUTHENTICATION);
			giant_lock();
			h = host_lookup(hostname);
			giant_unlock();
			if (h == NULL) {
				gflog_info(GFARM_MSG_UNFIXED,
				    "unknown gfsd <%s>", hostname);
				free(hostname);
				return (GFARM_ERR_AUTHENTICATION);
			}
			if (global_usernamep == NULL)
				free(hostname);
			else
				*global_usernamep = hostname;
			return (GFARM_ERR_NO_ERROR);
		case GFARM_AUTH_ID_TYPE_METADATA_HOST:
			if (GFARM_IS_AUTH_TLS_CLIENT_CERTIFICATE(
			    auth_method)) {
				e = gfarm_x509_cn_get_hostname(
				    auth_user_id_type, auth_user_id,
				    &hostname);
			} else {
				e = gfarm_kerberos_principal_get_hostname(
				    auth_user_id_type, auth_user_id,
				    &hostname);
			}
			if (e != GFARM_ERR_NO_ERROR)
				return (GFARM_ERR_AUTHENTICATION);
			giant_lock();
			m = mdhost_lookup(hostname);
			giant_unlock();
			if (m == NULL) {
				gflog_info(GFARM_MSG_UNFIXED,
				    "unknown gfmd <%s>", hostname);
				free(hostname);
				return (GFARM_ERR_AUTHENTICATION);
			}
			if (global_usernamep == NULL)
				free(hostname);
			else
				*global_usernamep = hostname;
			return (GFARM_ERR_NO_ERROR);
		default:
			gflog_debug(GFARM_MSG_UNFIXED,
			    "auth_uid_to_global_username(id_type:%d, id:%s): "
			    "unexpected call",
			    auth_user_id_type, auth_user_id);
			return (GFARM_ERR_AUTHENTICATION);
		}
	}

	if (GFARM_IS_AUTH_GSS(auth_method) ||
	    GFARM_IS_AUTH_TLS_CLIENT_CERTIFICATE(auth_method)) {
		char *hostname = NULL;
		struct host *h;
		struct mdhost *m;

		giant_lock();
		u = user_lookup_gsi_dn(auth_user_id);
		if (u != NULL) {
			giant_unlock();
			/* auth_user_id is a DN */
			*auth_user_id_typep = GFARM_AUTH_ID_TYPE_USER;
			if (global_usernamep == NULL)
				return (GFARM_ERR_NO_ERROR);
			global_username = strdup_log(user_name(u), diag);
			if (global_username == NULL)
				return (GFARM_ERR_NO_MEMORY);
			*global_usernamep = global_username;
			return (GFARM_ERR_NO_ERROR);

		}
		if ((e = gfarm_x509_cn_get_hostname(
		    GFARM_AUTH_ID_TYPE_SPOOL_HOST, auth_user_id, &hostname))
		    != GFARM_ERR_NO_ERROR &&
		    (h = host_lookup(hostname)) != NULL) {
			giant_unlock();
			*auth_user_id_typep = GFARM_AUTH_ID_TYPE_SPOOL_HOST;
			if (global_usernamep == NULL)
				free(hostname);
			else
				*global_usernamep = hostname;
			return (GFARM_ERR_NO_ERROR);
		}
		if (hostname != NULL) {
			free(hostname);
			hostname = NULL;
		}
		if ((e = gfarm_x509_cn_get_hostname(
		    GFARM_AUTH_ID_TYPE_METADATA_HOST, auth_user_id, &hostname))
		    != GFARM_ERR_NO_ERROR &&
		    (m = mdhost_lookup(hostname)) != NULL) {
			giant_unlock();
			*auth_user_id_typep = GFARM_AUTH_ID_TYPE_METADATA_HOST;
			if (global_usernamep == NULL)
				free(hostname);
			else
				*global_usernamep = hostname;
			return (GFARM_ERR_NO_ERROR);
		}
		giant_unlock();
		return (GFARM_ERR_AUTHENTICATION);
	}

	if (auth_user_id_type != GFARM_AUTH_ID_TYPE_USER)
		return (GFARM_ERR_AUTHENTICATION);

	giant_lock();
	if (GFARM_IS_AUTH_GSS(auth_method) ||
	    GFARM_IS_AUTH_TLS_CLIENT_CERTIFICATE(auth_method)) {
		/* auth_user_id is a DN */
		u = user_lookup_gsi_dn(auth_user_id);
	} else { /* auth_user_id is a gfarm global user name */
		u = user_lookup(auth_user_id);
	}
	giant_unlock();

	if (u == NULL) {
		/*
		 * do not return GFARM_ERR_NO_SUCH_USER
		 * to prevent information leak
		 */
		gflog_info(GFARM_MSG_UNFIXED,
		    "unknown user id <%s>", auth_user_id);
		return (GFARM_ERR_AUTHENTICATION);
	}
	if (global_usernamep == NULL)
		return (GFARM_ERR_NO_ERROR);
	global_username = strdup_log(user_name(u), diag);
	if (global_username == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*global_usernamep = global_username;
	return (GFARM_ERR_NO_ERROR);
}
