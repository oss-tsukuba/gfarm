#include <assert.h>
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

static gfarm_error_t gfarm_auth_uid_to_global_username_sharedsecret(
	void *, const char *, enum gfarm_auth_id_type *, char **);
#ifdef HAVE_GSI
static gfarm_error_t gfarm_auth_uid_to_global_username_gsi(
	void *, const char *, enum gfarm_auth_id_type *, char **);
#endif
#ifdef HAVE_KERBEROS
static gfarm_error_t gfarm_auth_uid_to_global_username_kerberos(
	void *, const char *, enum gfarm_auth_id_type *, char **);
#endif
#ifdef HAVE_TLS_1_3
static gfarm_error_t gfarm_auth_uid_to_global_username_tls_client_certificate(
	void *, const char *, enum gfarm_auth_id_type *, char **);
#endif

gfarm_error_t (*gfarm_auth_uid_to_global_username_table[])(
	void *, const char *, enum gfarm_auth_id_type *, char **) = {
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
 gfarm_auth_uid_to_global_username_kerberos,/*GFARM_AUTH_METHOD_KERBEROS*/
 gfarm_auth_uid_to_global_username_kerberos,/*GFARM_AUTH_METHOD_KERBEROS_AUTH*/
#else
 gfarm_auth_uid_to_global_username_panic,	/*GFARM_AUTH_METHOD_KERBEROS*/
 gfarm_auth_uid_to_global_username_panic,   /*GFARM_AUTH_METHOD_KERBEROS_AUTH*/
#endif
};

/*
 * this function is never called,
 * because such auth_method is rejected by gfarm_auth_method_get_available()
 * and the execution path of GFARM_AUTH_ERROR_DENIED
 * in gfarm_authorize_wo_setuid() will be chosen.
 * thus, calling gflog_fatal() is OK.
 */
gfarm_error_t
gfarm_auth_uid_to_global_username_panic(void *closure,
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
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

#if defined(HAVE_GSI) || defined(HAVE_KERBEROS) || defined(HAVE_TLS_1_3)

#define DEFAULT_HOSTBASED_SERVICE	"host"	/* default service */

const char *
gfarm_auth_server_cred_service_get_or_default(const char *service_tag)
{
	const char *serv_service;

	serv_service = gfarm_auth_server_cred_service_get(service_tag);
	if (serv_service != NULL)
		return (serv_service);
	return (DEFAULT_HOSTBASED_SERVICE);
}

#endif /* defined(HAVE_GSI) || defined(HAVE_KERBEROS) */

#if defined(HAVE_GSI) || defined(HAVE_TLS_1_3)

gfarm_error_t
gfarm_x509_cn_get_service_hostname(
	const char *service_tag, const char *cn, char **hostnamep)
{
	const char *serv_service;
	char *hostname, *s;
	size_t serv_service_len;

	serv_service =
	    gfarm_auth_server_cred_service_get_or_default(service_tag);
	serv_service_len = strlen(serv_service);
	if (strncmp(cn, serv_service, serv_service_len) == 0 &&
	    cn[serv_service_len] == '/') {
		/* has service in CN. e.g. "CN=gfsd/gfsd1.example.org" */
		hostname = strdup(cn + serv_service_len + 1);
	} else if (strcmp(serv_service, DEFAULT_HOSTBASED_SERVICE) == 0 &&
	    strchr(cn, '/') == NULL) {
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

#endif /* defined(HAVE_GSI) || defined(HAVE_TLS_1_3) */

#ifdef HAVE_GSI

gfarm_error_t
gfarm_x509_get_cn(const char *x509_dn, char **cnp)
{
	static const char prefix[] = "/CN=";
	size_t len;
	const char *cn = strstr(x509_dn, prefix);
	const char *s, *t;
	char *c;

	if (cn == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	cn += sizeof(prefix) - 1;
	for (s = cn; *s != '\0'; s++) {
		if (*s == '/') {
			/*
			 * Is this next RDN (e.g. "/emailAddress=...") ?
			 * Or part of this RDN (e.g. "/hostname" part of
			 * " "CN=gfsd/hostname") ?
			 */
			for (t = s + 1;; t++) {
				if (*t == '\0' || *t == '/') { /* this RDN */
					s = t;
					break;
				}
				if (*t == '=' || *t == '+') { /* next RDN */
					break;
				}
			}
			break; /* `s' points the end of CN */
		}
	}
	len = s - cn;
	GFARM_MALLOC_ARRAY(c, len + 1);
	if (c == NULL)
		return (GFARM_ERR_NO_MEMORY);
	memcpy(c, cn, len);
	c[len] = '\0';
	*cnp = c;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_auth_uid_to_global_username_gsi(void *closure,
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	gfarm_error_t e, e2;
	enum gfarm_auth_id_type auth_user_id_type = *auth_user_id_typep;
	char *cn, *hostname;

	if (auth_user_id_type != GFARM_AUTH_ID_TYPE_UNKNOWN) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "auth_uid_to_global_username(id_type:%d, id:%s): "
		    "unexpected call", auth_user_id_type, auth_user_id);
		return (GFARM_ERR_NO_SUCH_USER);
	}

	/* GFARM_AUTH_ID_TYPE_USER? */
	e = gfarm_auth_uid_to_global_username_by_dn(
	    closure, auth_user_id, global_usernamep);
	if (e == GFARM_ERR_NO_ERROR) {
		*auth_user_id_typep = GFARM_AUTH_ID_TYPE_USER;
		return (GFARM_ERR_NO_ERROR);
	}

	e = gfarm_x509_get_cn(auth_user_id, &cn);
	if (e != GFARM_ERR_NO_ERROR) {
		return (e);
	}

	/* gfsd? */
	e = gfarm_x509_cn_get_hostname(GFARM_AUTH_ID_TYPE_SPOOL_HOST, cn,
	    &hostname);
	if (e == GFARM_ERR_NO_ERROR) {
		struct gfarm_host_info host;
		const char *chost = hostname;

		e = gfm_client_host_info_get_by_names(gfm_server, 1, &chost,
		    &e2, &host);
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
		if (e == GFARM_ERR_NO_ERROR) {
			gfarm_host_info_free(&host);
			*auth_user_id_typep = GFARM_AUTH_ID_TYPE_SPOOL_HOST;
			if (global_usernamep == NULL)
				free(hostname);
			else
				*global_usernamep = hostname;
			return (e);
		}
	}

	/* gfmd? */
	e = gfarm_x509_cn_get_hostname(GFARM_AUTH_ID_TYPE_METADATA_HOST, cn,
	    &hostname);
	if (e == GFARM_ERR_NO_ERROR) {
		struct gfarm_metadb_server mdhost;

		e = gfm_client_metadb_server_get(gfm_server, hostname,
		    &mdhost);
		if (e == GFARM_ERR_NO_ERROR) {
			gfarm_metadb_server_free(&mdhost);
			*auth_user_id_typep = GFARM_AUTH_ID_TYPE_METADATA_HOST;
			if (global_usernamep == NULL)
				free(hostname);
			else
				*global_usernamep = hostname;
			return (e);
		}
	}
	free(hostname);
	return (e);
}

#endif /* HAVE_GSI */

#ifdef HAVE_KERBEROS

gfarm_error_t
gfarm_kerberos_principal_get_service_hostname(
	const char *service_tag, const char *principal, char **hostnamep)
{
	const char *serv_service;
	char *hostname, *at;
	size_t serv_service_len, hlen;

	serv_service =
	    gfarm_auth_server_cred_service_get_or_default(service_tag);
	serv_service_len = strlen(serv_service);
	if (strncmp(principal, serv_service, serv_service_len) == 0 &&
	    principal[serv_service_len] == '/') {
		/*
		 * has service in principal name.
		 * e.g. "gfsd/gfsd1.example.org@example.org"
		 */
		principal += serv_service_len + 1;
		at = strchr(principal, '@');
		if (at == NULL)
			hlen = strlen(principal);
		else
			hlen = at - principal;
		GFARM_MALLOC_ARRAY(hostname, hlen);
		if (hostname == NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "service:%s, principal=\"%s\": no memory",
			    serv_service, principal);
			return (GFARM_ERR_NO_MEMORY);
		}
		memcpy(hostname, principal, hlen);
		hostname[hlen] = '\0';
		hostname = strdup(principal + serv_service_len + 1);
	} else {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "service:%s, principal=\"%s\": service not match",
		    serv_service, principal);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}

	*hostnamep = hostname;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_kerberos_principal_get_hostname(
	enum gfarm_auth_id_type auth_id_type, const char *principal,
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

	return (gfarm_kerberos_principal_get_service_hostname(
	    service_tag, principal, hostnamep));
}

static gfarm_error_t
gfarm_auth_uid_to_global_username_kerberos(void *closure,
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	enum gfarm_auth_id_type auth_user_id_type = *auth_user_id_typep;
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
		e = gfarm_kerberos_principal_get_hostname(auth_user_id_type,
		    auth_user_id, &hostname);
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
		e = gfarm_kerberos_principal_get_hostname(auth_user_id_type,
		    auth_user_id, &hostname);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		e = gfm_client_metadb_server_get(gfm_server, hostname,
		    &mdhost);
		if (e == GFARM_ERR_NO_ERROR)
			gfarm_metadb_server_free(&mdhost);
		break;
	default:
		gflog_warning(GFARM_MSG_UNFIXED,
		    "auth_uid_to_global_username_kerberos(id_type:%d, id:%s): "
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
#endif /* HAVE_KERBEROS */

#ifdef HAVE_TLS_1_3

static gfarm_error_t
gfarm_auth_uid_to_global_username_tls_client_certificate(void *closure,
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	enum gfarm_auth_id_type auth_user_id_type = *auth_user_id_typep;
	gfarm_error_t e, e2;
	char *hostname;
	const char *chost;
	struct gfarm_host_info host;
	struct gfarm_metadb_server mdhost;

	switch (auth_user_id_type) {
	case GFARM_AUTH_ID_TYPE_USER:
		/* auth_user_id is Distinguished Name */
		return (gfarm_auth_uid_to_global_username_by_dn(
		    closure, auth_user_id, global_usernamep));
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
		/* auth_user_id is Common Name */
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
		/* auth_user_id is Common Name */
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
	const char *auth_user_id, enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	enum gfarm_auth_id_type auth_user_id_type = *auth_user_id_typep;
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

/*
 * if auth_method is gsi*, kerberos* or tls_client_certificate,
 * gfarm_auth_id_type is GFARM_AUTH_ID_TYPE_{USER,SPOOL_HOST,METADATA_HOST},
 * otherwise gfarm_auth_id_type must be GFARM_AUTH_ID_TYPE_USER.
 *
 * if auth_method is gsi*,
 * *gfarm_auth_id_typep is an output parameter, otherwise an input parameter.
 */
gfarm_error_t
gfarm_auth_uid_to_global_username(void *closure,
	enum gfarm_auth_method auth_method,
	const char *auth_user_id,
	enum gfarm_auth_id_type *auth_user_id_typep,
	char **global_usernamep)
{
	assert(GFARM_ARRAY_LENGTH(gfarm_auth_uid_to_global_username_table)
	    == GFARM_AUTH_METHOD_NUMBER);

	if (auth_method < GFARM_AUTH_METHOD_NONE ||
	    auth_method >=
	    GFARM_ARRAY_LENGTH(gfarm_auth_uid_to_global_username_table)) {
		gflog_error(GFARM_MSG_1000056,
		    "gfarm_auth_uid_to_global_username: method=%d/%d",
		    auth_method,
		    (int)GFARM_ARRAY_LENGTH(
		    gfarm_auth_uid_to_global_username_table));
		return (gfarm_auth_uid_to_global_username_panic(closure,
		    auth_user_id, auth_user_id_typep, global_usernamep));
	}
	return (gfarm_auth_uid_to_global_username_table[auth_method](
	    closure, auth_user_id, auth_user_id_typep, global_usernamep));
}
