#include <sys/types.h>
#include <pwd.h>
#include <string.h>

#include <gssapi.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include "auth.h"
#include "auth_gsi.h"

#define GFSL_CONF_USERMAP "/etc/grid-security/grid-mapfile"

static int gsi_initialized;
static int gsi_server_initialized;

static char *gsi_client_cred_name;

char *
gfarm_gsi_client_initialize(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (gsi_initialized)
		return (NULL);

	rv = gfarmSecSessionInitializeInitiator(NULL, GFSL_CONF_USERMAP,
	    &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(
				"can't initialize as initiator because of:",
				NULL);
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarmSecSessionFinalizeInitiator();
		return ("GSI credential initialization failed"); /* XXX */
	}
	gsi_initialized = 1;
	gsi_server_initialized = 0;
	gsi_client_cred_name = gfarmSecSessionGetInitiatorCredName();
	return (NULL);
}

char *
gfarm_gsi_client_cred_name(void)
{
	return (gsi_client_cred_name);
}

char *
gfarm_gsi_server_initialize(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (gsi_initialized) {
		if (gsi_server_initialized)
			return (NULL);
		gfarmSecSessionFinalizeInitiator();
		gsi_initialized = 0;
	}

	rv = gfarmSecSessionInitializeBoth(NULL, NULL,
	    GFSL_CONF_USERMAP, &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(
				"can't initialize GSI as both because of:",
				NULL);
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarmSecSessionFinalizeBoth();
		return ("GSI initialization failed"); /* XXX */
	}
	gsi_initialized = 1;
	gsi_server_initialized = 1;
	return (NULL);
}

/*
 * Delegated credential
 */

static gss_cred_id_t delegated_cred = GSS_C_NO_CREDENTIAL;

void
gfarm_gsi_set_delegated_cred(gss_cred_id_t cred)
{
	delegated_cred = cred;
}

gss_cred_id_t
gfarm_gsi_get_delegated_cred()
{
	return (delegated_cred);
}

/*
 * converter from credential configuration to [GSSNameType, GSSName].
 *
 * The results of
 * (type, service, name) -> [NameType, Name] -> gss_name_t -> gss_cred_id_t
 * are:
 * (DEFAULT, NULL, NULL) is not allowed. caller must check this at first.
 * (NO_NAME, NULL, NULL) -> [GSS_C_NO_OID, NULL] -> GSS_C_NO_NAME
 * (MECHANISM_SPECIFIC, NULL, name) -> [GSS_C_NO_OID, name]
 * (HOST, service, host) ->[GSS_C_NT_HOSTBASED_SERVICE, service + "@" + host]
 *		if (service == NULL) service = "host"
 *		if (host == NULL) host = canonical_self_name
 * (USER, NULL, username) -> [GSS_C_NT_USER_NAME, username]
 *		if (username == NULL) username = self_local_username
 *
 * when a server acquires a credential of itself:
 *	(DEFAULT, NULL, NULL) -> N/A -> N/A -> GSS_C_NO_CREDENTIAL
 * when a client authenticates a server:
 *	(DEFAULT, NULL, NULL) is equivalent to (HOST, NULL, NULL)
 *	(HOST, service, NULL) is equivalent to (HOST, service, peer_host)
 */

char *
gfarm_gsi_cred_config_convert_to_name_and_type(
	enum gfarm_auth_cred_type type, char *service, char *name,
	char *hostname,
	char **name_stringp, gss_OID *name_typep, int *need_freep)
{
	char *gss_name_string;

	switch (type) {
	case GFARM_AUTH_CRED_TYPE_DEFAULT:
		if (name != NULL)
			return ("cred_type is not set, but cred_name is set");
		if (service != NULL)
			return ("cred_type is not set, but cred_service is set"
			    );
		return ("internal error: missing GSS_C_NO_CREDENTIAL check");
	case GFARM_AUTH_CRED_TYPE_NO_NAME:
		if (name != NULL)
			return ("cred_type is \"no-name\", "
			    "but cred_name is set");
		if (service != NULL)
			return ("cred_type is \"no-name\", "
			    "but cred_service is set");
		*name_stringp = NULL;
		*name_typep = GSS_C_NO_OID;
		*need_freep = 0;
		break;
	case GFARM_AUTH_CRED_TYPE_MECHANISM_SPECIFIC:
		if (name == NULL)
			return ("cred_type is \"mechanism-specific\", "
			    "but cred_name is not set");
		if (service != NULL)
			return ("cred_type is \"mechanism-specific\", "
			    "but cred_service is set");
		*name_stringp = name;
		*name_typep = GSS_C_NO_OID;
		*need_freep = 0;
		break;
	case GFARM_AUTH_CRED_TYPE_HOST:
		if (service == NULL)
			service = "host";
		if (name == NULL)
			name = hostname;
		if (name == NULL &&
		    gfarm_host_get_canonical_self_name(&name) != NULL)
			name = gfarm_host_get_self_name();
		gss_name_string = malloc(strlen(service) + strlen(name) + 2);
		if (gss_name_string == NULL)
			return (GFARM_ERR_NO_MEMORY);
		sprintf(gss_name_string, "%s@%s", service, name);
		*name_stringp = gss_name_string;
		*name_typep = GSS_C_NT_HOSTBASED_SERVICE;
		*need_freep = 1;
		break;
	case GFARM_AUTH_CRED_TYPE_USER:
		if (service != NULL)
			return ("cred_type is \"user\", "
			    "but cred_service is set");
		/*
		 * XXX FIXME: `name' must be converted from global_username
		 * to local_username, but there is no such function for now.
		 */
		*name_stringp = name != NULL ? name :
		    gfarm_get_local_username();
		*name_typep = GSS_C_NT_USER_NAME;
		*need_freep = 0;
		break;
	default:
		return ("internal error - invalid cred_type");
	}
	return (NULL);
}
