#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "auth.h"
#include "gfs_proto.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "metadb_server.h"

static gfarm_error_t gfarm_auth_uid_to_global_username_panic(
	void *, enum gfarm_auth_id_type, const char *, char **);
static gfarm_error_t gfarm_auth_uid_to_global_username_sharedsecret(
	void *, enum gfarm_auth_id_type, const char *, char **);
#if defined(HAVE_GSI) || defined(HAVE_KERBEROS)
static gfarm_error_t gfarm_auth_uid_to_global_username_gsi(
	void *, enum gfarm_auth_id_type, const char *, char **);
#endif
#ifdef HAVE_TLS_1_3
static gfarm_error_t gfarm_auth_uid_to_global_username_tls_client_certificate(
	void *, enum gfarm_auth_id_type, const char *, char **);
#endif

gfarm_error_t (*gfarm_auth_uid_to_global_username_table[])(
	void *, enum gfarm_auth_id_type, const char *, char **) = {
/*
 * This table entry should be ordered by enum gfarm_auth_method.
 */
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_NONE*/
 gfarm_auth_uid_to_global_username_sharedsecret,
					    /*GFARM_AUTH_METHOD_SHAREDSECRET */
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_GSI_OLD*/
#ifdef HAVE_GSI
 gfarm_auth_uid_to_global_username_gsi,		/*GFARM_AUTH_METHOD_GSI*/
 gfarm_auth_uid_to_global_username_gsi,		/*GFARM_AUTH_METHOD_GSI_AUTH*/
#else
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_GSI*/
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_GSI_AUTH*/
#endif
#ifdef HAVE_TLS_1_3
 gfarm_auth_uid_to_global_username_sharedsecret,
					/*GFARM_AUTH_METHOD_TLS_SHAREDSECRET*/
 gfarm_auth_uid_to_global_username_tls_client_certificate,
				   /*GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE*/
#else
 gfarm_auth_uid_to_global_username_panic,/*GFARM_AUTH_METHOD_TLS_SHAREDSECRET*/
 gfarm_auth_uid_to_global_username_panic,
				   /*GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE*/
#endif
#ifdef HAVE_KERBEROS
 gfarm_auth_uid_to_global_username_gsi,		/*GFARM_AUTH_METHOD_KERBEROS*/
 gfarm_auth_uid_to_global_username_gsi,	    /*GFARM_AUTH_METHOD_KERBEROS_AUTH*/
#else
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_KERBEROS*/
 gfarm_auth_uid_to_global_username_panic,   /*GFARM_AUTH_METHOD_KERBEROS_AUTH*/
#endif
#if defined(HAVE_CYRUS_SASL) && defined(HAVE_TLS_1_3)
 gfarm_auth_uid_to_global_username_sharedsecret, /*GFARM_AUTH_METHOD_SASL*/
 gfarm_auth_uid_to_global_username_sharedsecret,/*GFARM_AUTH_METHOD_SASL_AUTH*/
#else
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_KERBEROS*/
 gfarm_auth_uid_to_global_username_panic,   /*GFARM_AUTH_METHOD_KERBEROS_AUTH*/
#endif
};

static gfarm_error_t
gfarm_auth_uid_to_global_username_panic(void *closure,
	enum gfarm_auth_id_type auth_user_id_type, const char *auth_user_id,
	char **global_usernamep)
{
	gflog_fatal(GFARM_MSG_1000055,
	    "gfarm_auth_uid_to_global_username_panic: "
	    "authorization assertion failed");
	return (GFARM_ERR_PROTOCOL);
}

#if defined(HAVE_GSS) || defined(HAVE_TLS_1_3)

static gfarm_error_t
gfarm_auth_uid_to_global_username_by_dn(void *closure,
	const char *auth_user_id, char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	gfarm_error_t e;
	char *global_username;
	struct gfarm_user_info ui;

	/* auth_user_id is a DN */
	e = gfm_client_user_info_get_by_gsi_dn(gfm_server,
		auth_user_id, &ui);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001478,
			"getting user info by gsi dn (%s) failed: %s",
			auth_user_id,
			gfarm_error_string(e));
		return (e);
	}
	if (global_usernamep == NULL) {
		gfarm_user_info_free(&ui);
		return (GFARM_ERR_NO_ERROR);
	}
	global_username = strdup(ui.username);
	gfarm_user_info_free(&ui);
	if (global_username == NULL) {
		gflog_debug(GFARM_MSG_1001479,
			"allocation of 'global_username' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	*global_usernamep = global_username;
	return (GFARM_ERR_NO_ERROR);
}

#endif /* defined(HAVE_GSI) || defined(HAVE_TLS_1_3) */

#if defined(HAVE_GSI) || defined(HAVE_KERBEROS)

static gfarm_error_t
gfarm_auth_uid_to_global_username_gsi(void *closure,
	enum gfarm_auth_id_type auth_user_id_type, const char *auth_user_id,
	char **global_usernamep)
{
	if (auth_user_id_type != GFARM_AUTH_ID_TYPE_USER) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "auth_uid_to_global_username(id_type:%d, id:%s): "
		    "unexpected call", auth_user_id_type, auth_user_id);
		return (GFARM_ERR_NO_SUCH_USER);
	}

	return (gfarm_auth_uid_to_global_username_by_dn(
	    closure, auth_user_id, global_usernamep));
}

#endif /* defined(HAVE_GSI) || defined(HAVE_KERBEROS) */

gfarm_error_t
gfarm_x509_cn_get_service_hostname(
	const char *service_tag, const char *cn, char **hostnamep)
{
	const char *serv_service;
	char *hostname, *s;
	size_t serv_service_len;

	serv_service = gfarm_auth_server_cred_service_get(service_tag);
	if (serv_service == NULL)
		serv_service = "host"; /* default configuration */
	serv_service_len = strlen(serv_service);
	if (strncmp(cn, serv_service, serv_service_len) == 0 &&
	    cn[serv_service_len] == '/') {
		/* has service in CN. e.g. "CN=gfsd/gfsd1.example.org" */
		hostname = strdup(cn + serv_service_len + 1);
	} else if (strcmp(serv_service, "host") == 0) {
		/* "host/" prefix in CN can be omitted */
		hostname = strdup(cn);
	} else {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "service:%s, cn=\"%s\": service not match",
		    serv_service, cn);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	if (hostname == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "service:%s, cn=\"%s\": no memory", serv_service, cn);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((s = strchr(hostname, '/')) != NULL)
		*s = '\0';

	*hostnamep = hostname;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_x509_cn_get_hostname(
	enum gfarm_auth_id_type auth_id_type, const char *cn,
	char **hostnamep)
{
	const char *service_tag;

	switch (auth_id_type) {
	case GFARM_AUTH_ID_TYPE_USER:
		return (GFARM_ERR_INVALID_ARGUMENT);
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
		service_tag = GFS_SERVICE_TAG;
		break;
	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		service_tag = GFM_SERVICE_TAG;
		break;
	default:
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	return (gfarm_x509_cn_get_service_hostname(
	    service_tag, cn, hostnamep));
}

#ifdef HAVE_TLS_1_3

static gfarm_error_t
gfarm_auth_uid_to_global_username_tls_client_certificate(void *closure,
	enum gfarm_auth_id_type auth_user_id_type, const char *auth_user_id,
	char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	gfarm_error_t e, e2;
	char *hostname;
	const char *chost;
	struct gfarm_host_info host;
	struct gfarm_metadb_server mdhost;

	switch (auth_user_id_type) {
	case GFARM_AUTH_ID_TYPE_USER:
		return (gfarm_auth_uid_to_global_username_by_dn(
		    closure, auth_user_id, global_usernamep));
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
		e = gfarm_x509_cn_get_hostname(auth_user_id_type, auth_user_id,
		    &hostname);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		chost = hostname;
		e = gfm_client_host_info_get_by_names(gfm_server, 1, &chost,
		    &e2, &host);
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
		if (e == GFARM_ERR_NO_ERROR)
			gfarm_host_info_free(&host);
		break;
	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		e = gfarm_x509_cn_get_hostname(auth_user_id_type, auth_user_id,
		    &hostname);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		e = gfm_client_metadb_server_get(gfm_server, hostname,
		    &mdhost);
		if (e == GFARM_ERR_NO_ERROR)
			gfarm_metadb_server_free(&mdhost);
		break;
	default:
		gflog_warning(GFARM_MSG_UNFIXED,
		    "auth_uid_to_global_username(id_type:%d, id:%s): "
		    "unexpected call", auth_user_id_type, auth_user_id);
		return (GFARM_ERR_NO_SUCH_USER);
	}
	if (e != GFARM_ERR_NO_ERROR)
		free(hostname);
	else if (global_usernamep == NULL)
		free(hostname);
	else
		*global_usernamep = hostname;
	return (e);
}

#endif /* HAVE_TLS_1_3 */

static gfarm_error_t
gfarm_auth_uid_to_global_username_sharedsecret(void *closure,
	enum gfarm_auth_id_type auth_user_id_type, const char *auth_user_id,
	char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	gfarm_error_t e, e2;
	char *global_username;
	struct gfarm_user_info ui;

	if (auth_user_id_type != GFARM_AUTH_ID_TYPE_USER) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "auth_uid_to_global_username(id_type:%d, id:%s): "
		    "unexpected call", auth_user_id_type, auth_user_id);
		return (GFARM_ERR_NO_SUCH_USER);
	}

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
	enum gfarm_auth_id_type auth_user_id_type,
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
		    auth_user_id_type, auth_user_id, global_usernamep));
	}
	return (gfarm_auth_uid_to_global_username_table[auth_method](
	    closure, auth_user_id_type, auth_user_id, global_usernamep));
}
