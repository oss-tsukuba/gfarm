#include <pthread.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

#include <gssapi.h>

#define GFARM_USE_GSSAPI
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "thrsubr.h"

#include "gfsl_secure_session.h"
#include "gss.h"

#include "context.h"
#include "liberror.h"
#include "gfpath.h"
#include "auth.h"
#include "auth_gss.h"

struct gfarm_auth_common_gss_static {
	gss_cred_id_t client_cred;

	/* gfarm_gsi_client_cred_name() or gfarm_kerberos_client_cred_name() */
	pthread_mutex_t client_cred_init_mutex;
	int client_cred_initialized;
	char *client_dn;

	/* client credential had a problem? (e.g. expired) */
	int client_cred_failed;
};

gfarm_error_t
gfarm_auth_common_gss_static_init(struct gfarm_auth_common_gss_static **sp,
	const char *diag)
{
	struct gfarm_auth_common_gss_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->client_cred = GSS_C_NO_CREDENTIAL;

	gfarm_mutex_init(&s->client_cred_init_mutex,
	    diag, "client_cred_initialize");
	s->client_cred_initialized = 0;
	s->client_dn = NULL;

	s->client_cred_failed = 0;

	*sp = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_auth_common_gss_static_term(struct gfarm_auth_common_gss_static *s,
	const char *diag)
{
	if (s == NULL)
		return;

	gfarm_mutex_destroy(&s->client_cred_init_mutex,
	    diag, "client_cred_initialize");
	free(s->client_dn);
	free(s);
}

char *
gfarm_gss_client_cred_name(struct gfarm_gss *gss,
	struct gfarm_auth_common_gss_static *s)
{
	gss_cred_id_t cred = s->client_cred;
	gss_name_t name;
	OM_uint32 e_major, e_minor;
	char *client_dn;
	static const char diag[] = "gfarm_gss_client_cred_name";

	gfarm_mutex_lock(&s->client_cred_init_mutex, diag, gss->protocol);
	if (s->client_cred_initialized) {
		client_dn = s->client_dn;
		gfarm_mutex_unlock(&s->client_cred_init_mutex,
		    diag, gss->protocol);
		return (client_dn);
	}

	if (gss->gfarmGssNewCredentialName(&name, cred, &e_major, &e_minor)
	    < 0) {
		s->client_dn = NULL;
		gflog_auth_notice(GFARM_MSG_1005294,
		    "gfarm_gss_client_cred_name() for %s: "
		    "cannot acquire initiator credential",
		    gss->protocol);
		if (gflog_auth_get_verbose()) {
			gss->gfarmGssPrintMajorStatus(e_major);
			gss->gfarmGssPrintMinorStatus(e_minor);
		}
	} else {
		s->client_dn = gss->gfarmGssNewDisplayName(
		    name, &e_major, &e_minor, NULL);
		if (s->client_dn == NULL && gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1005295,
			    "cannot convert initiator credential for %s "
			    "to string", gss->protocol);
			gss->gfarmGssPrintMajorStatus(e_major);
			gss->gfarmGssPrintMinorStatus(e_minor);
		}
		gss->gfarmGssDeleteName(&name, NULL, NULL);
		s->client_cred_initialized = 1;
	}
	client_dn = s->client_dn;
	gfarm_mutex_unlock(&s->client_cred_init_mutex, diag, gss->protocol);
	return (client_dn);
}

static int
gfarm_auth_gss_client_credential_available(struct gfarm_gss *gss)
{
	if (gss->gfarmGssAcquireCredential(
	    NULL, GSS_C_NO_NAME, GSS_C_INITIATE, NULL, NULL, NULL) < 0) {
		return (0);
	}
	return (1);
}

void
gfarm_auth_gss_client_cred_set(struct gfarm_auth_common_gss_static *s,
	gss_cred_id_t cred)
{
	s->client_cred = cred;
}

gss_cred_id_t
gfarm_auth_gss_client_cred_get(struct gfarm_auth_common_gss_static *s)
{
	return (s->client_cred);
}

void
gfarm_auth_gss_client_cred_set_failed(struct gfarm_gss *gss,
	struct gfarm_auth_common_gss_static *s)
{
	s->client_cred_failed = 1;
}

/* to prevent to connect servers with expired client credential */
gfarm_error_t
gfarm_auth_gss_client_cred_check_failed(struct gfarm_gss *gss,
	struct gfarm_auth_common_gss_static *s)
{
	if (s->client_cred_failed) {
		if (!gfarm_auth_gss_client_credential_available(gss)) {
			return (GFARM_ERR_INVALID_CREDENTIAL);
		}
		s->client_cred_failed = 0;
	}
	return (GFARM_ERR_NO_ERROR);
}

/* NOTE: it's OK to pass NULL as gss */
gfarm_error_t
gfarm_auth_client_method_gss_protocol_available(struct gfarm_gss *gss,
	struct gfarm_auth_common_gss_static *s)
{
	if (gss == NULL)
		return (GFARM_ERR_PROTOCOL_NOT_AVAILABLE);
	if (gfarm_auth_gss_client_credential_available(gss))
		return (GFARM_ERR_NO_ERROR);

	gss->gfarm_ops->client_cred_set_failed();

	return (GFARM_ERR_INVALID_CREDENTIAL);
}

gfarm_error_t
gfarm_gss_client_initialize(struct gfarm_gss *gss)
{
	OM_uint32 e_major, e_minor;
	int rv;

	rv = gss->gfarmSecSessionInitializeInitiator(NULL, &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000706,
			    "can't initialize as initiator because of:");
			gss->gfarmGssPrintMajorStatus(e_major);
			gss->gfarmGssPrintMinorStatus(e_minor);
		}
		gss->gfarmSecSessionFinalizeInitiator();

		return (GFARM_ERRMSG_GSS_CREDENTIAL_INITIALIZATION_FAILED);
	}
	return (GFARM_ERR_NO_ERROR);
}

static void
gfarm_gss_server_finalize(struct gfarm_gss *gss)
{
	gss->gfarmSecSessionFinalizeBoth();
}

gfarm_error_t
gfarm_gss_server_initialize(struct gfarm_gss *gss)
{
	gfarm_OM_uint32 e_major, e_minor;
	int rv;

	rv = gss->gfarmSecSessionInitializeBoth(NULL, NULL,
	    &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000710,
			    "can't initialize GSS as both because of:");
			gss->gfarmGssPrintMajorStatus(e_major);
			gss->gfarmGssPrintMinorStatus(e_minor);
		}
		gfarm_gss_server_finalize(gss);

		return (GFARM_ERRMSG_GSS_INITIALIZATION_FAILED);
	}
	return (GFARM_ERR_NO_ERROR);
}

/*
 * converter from credential configuration to [GSSNameType, GSSName].
 *
 * The results of
 * (type, service, name) -> gss_name_t [NameType, Name] -> gss_cred_id_t
 * are:
 * (DEFAULT, NULL, NULL) is not allowed. caller must check this at first.
 * (NO_NAME, NULL, NULL) -> GSS_C_NO_NAME
 * (MECHANISM_SPECIFIC, NULL, name) -> [GSS_C_NO_OID, name]
 * (HOST, service, host) ->[GSS_C_NT_HOSTBASED_SERVICE, service + "@" + host]
 *		if (service == NULL) service = "host"
 * (USER, NULL, username) -> [GSS_C_NT_USER_NAME, username]
 *		if (username == NULL) username = self_local_username
 * (SELF, NULL, NULL) -> the name of initial initiator credential
 *
 * when a server acquires a credential of itself:
 *	(DEFAULT, NULL, NULL) -> N/A -> GSS_C_NO_CREDENTIAL
 * when a client authenticates a server:
 *	(DEFAULT, NULL, NULL) is equivalent to (HOST, NULL, NULL)
 *	(HOST, service, NULL) is equivalent to (HOST, service, peer_host)
 */

gfarm_error_t
gfarm_gss_cred_config_convert_to_name(struct gfarm_gss *gss,
	enum gfarm_auth_cred_type type, char *service, char *name,
	char *hostname,
	gss_name_t *namep)
{
	int rv;
	OM_uint32 e_major;
	OM_uint32 e_minor;

	switch (type) {
	case GFARM_AUTH_CRED_TYPE_DEFAULT:
		/* special. equivalent to GSS_C_NO_CREDENTIAL */
		if (name != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_DEFAULT_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_DEFAULT_INVALID_CRED_SERVICE);
		return (GFARM_ERRMSG_CRED_TYPE_DEFAULT_INTERNAL_ERROR);
	case GFARM_AUTH_CRED_TYPE_NO_NAME:
		if (name != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_NO_NAME_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_NO_NAME_INVALID_CRED_SERVICE);
		*namep = GSS_C_NO_NAME;
		return (GFARM_ERR_NO_ERROR);
	case GFARM_AUTH_CRED_TYPE_MECHANISM_SPECIFIC:
		if (name == NULL)
			return (GFARM_ERRMSG_CRED_TYPE_MECHANISM_SPECIFIC_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_MECHANISM_SPECIFIC_INVALID_CRED_SERVICE);
		rv = gss->gfarmGssImportName(namep, name, strlen(name),
		    GSS_C_NO_OID, &e_major, &e_minor);
		break;
	case GFARM_AUTH_CRED_TYPE_HOST:
		if (name == NULL)
			name = hostname;
		if (service == NULL) {
			rv = gss->gfarmGssImportNameOfHost(namep, name,
			    &e_major, &e_minor);
		} else {
			rv = gss->gfarmGssImportNameOfHostBasedService(namep,
			    service, name, &e_major, &e_minor);
		}
		break;
	case GFARM_AUTH_CRED_TYPE_USER:
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_USER_CRED_INVALID_CRED_SERVICE);
		/*
		 * XXX FIXME: `name' must be converted from global_username
		 * to local_username, but there is no such function for now.
		 */
		if (name == NULL)
			name = gfarm_get_local_username();
		rv = gss->gfarmGssImportName(namep, name, strlen(name),
		    GSS_C_NT_USER_NAME, &e_major, &e_minor);
		break;
	case GFARM_AUTH_CRED_TYPE_SELF:
		/* special. there is no corresponding name_type in GSSAPI */
		if (name != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_SELF_CRED_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_SELF_CRED_INVALID_CRED_SERVICE);
		rv = gss->gfarmGssAcquireCredential(NULL, GSS_C_NO_NAME,
			GSS_C_INITIATE, &e_major, &e_minor, namep);
		break;
	default:
		return (GFARM_ERRMSG_INVALID_CRED_TYPE);
	}
	if (rv < 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000711, "gfarmGssImportName(): "
			    "invalid credential configuration:");
			gss->gfarmGssPrintMajorStatus(e_major);
			gss->gfarmGssPrintMinorStatus(e_minor);
		}
		return (GFARM_ERRMSG_INVALID_CREDENTIAL_CONFIGURATION);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_gss_cred_name_for_server(struct gfarm_gss *gss,
	const char *service_tag, const char *hostname, gss_name_t *namep)
{
	enum gfarm_auth_cred_type cred_type =
	    gfarm_auth_server_cred_type_get(service_tag);
	char *cred_service =
	    gfarm_auth_server_cred_service_get(service_tag);
	char *cred_name =
	    gfarm_auth_server_cred_name_get(service_tag);

	return (gfarm_gss_cred_config_convert_to_name(
	    gss,
	    cred_type != GFARM_AUTH_CRED_TYPE_DEFAULT ?
	    cred_type : GFARM_AUTH_CRED_TYPE_HOST,
	    cred_service,
	    cred_name,
	    (char *)hostname, namep));
}
