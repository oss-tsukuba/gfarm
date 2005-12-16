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

#include "gfpath.h"
#include "auth.h"
#include "auth_gsi.h"

static int gsi_initialized;
static int gsi_server_initialized;

char *
gfarm_gsi_client_initialize(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (gsi_initialized)
		return (NULL);

	rv = gfarmSecSessionInitializeInitiator(NULL, GRID_MAPFILE,
	    &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(
			    "can't initialize as initiator because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarmSecSessionFinalizeInitiator();
		return ("GSI credential initialization failed"); /* XXX */
	}
	gsi_initialized = 1;
	gsi_server_initialized = 0;
	return (NULL);
}

char *
gfarm_gsi_client_cred_name(void)
{
	gss_cred_id_t cred;
	gss_name_t name;
	OM_uint32 e_major, e_minor;
	static int initialized = 0;
	static char *dn;

	if (initialized)
		return (dn);
	
	if (gfarmSecSessionGetInitiatorInitialCredential(&cred) < 0) {
		dn = NULL;
		gflog_auth_error("gfarm_gsi_client_cred_name(): "
		    "not initialized as an initiator");
	} else if (gfarmGssNewCredentialName(&name, cred, &e_major, &e_minor)
	    < 0) {
		dn = NULL;
		if (gflog_auth_get_verbose()) {
			gflog_error("cannot convert initiator credential "
			    "to name");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
	} else {
		dn = gfarmGssNewDisplayName(name, &e_major, &e_minor, NULL);
		if (dn == NULL && gflog_auth_get_verbose()) {
			gflog_error("cannot convert initiator credential "
			    "to string");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarmGssDeleteName(&name, NULL, NULL);
	}
	initialized = 1;
	return (dn);
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

	rv = gfarmSecSessionInitializeBoth(NULL, NULL, GRID_MAPFILE,
	    &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(
			    "can't initialize GSI as both because of:");
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

char *
gfarm_gsi_cred_config_convert_to_name(
	enum gfarm_auth_cred_type type, char *service, char *name,
	char *hostname,
	gss_name_t *namep)
{
	int rv;
	OM_uint32 e_major;
	OM_uint32 e_minor;
	gss_cred_id_t cred;

	switch (type) {
	case GFARM_AUTH_CRED_TYPE_DEFAULT:
		/* special. equivalent to GSS_C_NO_CREDENTIAL */
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
		*namep = GSS_C_NO_NAME;
		return (NULL);
	case GFARM_AUTH_CRED_TYPE_MECHANISM_SPECIFIC:
		if (name == NULL)
			return ("cred_type is \"mechanism-specific\", "
			    "but cred_name is not set");
		if (service != NULL)
			return ("cred_type is \"mechanism-specific\", "
			    "but cred_service is set");
		rv = gfarmGssImportName(namep, name, strlen(name),
		    GSS_C_NO_OID, &e_major, &e_minor);
		break;
	case GFARM_AUTH_CRED_TYPE_HOST:
		if (name == NULL)
			name = hostname;
		if (service == NULL) {
			rv = gfarmGssImportNameOfHost(namep, name,
			    &e_major, &e_minor);
		} else {
			rv = gfarmGssImportNameOfHostBasedService(namep,
			    service, name, &e_major, &e_minor);
		}
		break;
	case GFARM_AUTH_CRED_TYPE_USER:
		if (service != NULL)
			return ("cred_type is \"user\", "
			    "but cred_service is set");
		/*
		 * XXX FIXME: `name' must be converted from global_username
		 * to local_username, but there is no such function for now.
		 */
		if (name == NULL)
			name = gfarm_get_local_username();
		rv = gfarmGssImportName(namep, name, strlen(name),
		    GSS_C_NT_USER_NAME, &e_major, &e_minor);
		break;
	case GFARM_AUTH_CRED_TYPE_SELF:
		/* special. there is no corresponding name_type in GSSAPI */
		if (name != NULL)
			return ("cred_type is \"self\", but cred_name is set");
		if (service != NULL)
			return ("cred_type is \"self\", "
			    "but cred_service is set");
		if (gfarmSecSessionGetInitiatorInitialCredential(&cred) < 0 ||
		    cred == GSS_C_NO_CREDENTIAL)
			return ("cred_type is \"self\", "
			    "but not initialized as an initiator");
		rv = gfarmGssNewCredentialName(namep, cred, &e_major,&e_minor);
		break;
	default:
		return ("internal error - invalid cred_type");
	}
	if (rv < 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error("gfarmGssImportName(): "
			    "invalid credential configuration:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		return ("invalid credential configuration");
	}
	return (NULL);
}
