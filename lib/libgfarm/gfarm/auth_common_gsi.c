#include <pthread.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include <time.h>

#include <gssapi.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "thrsubr.h"

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include "liberror.h"
#include "gfpath.h"
#include "auth.h"
#include "auth_gsi.h"

static pthread_mutex_t gsi_initialize_mutex = PTHREAD_MUTEX_INITIALIZER;
static int gsi_initialized;
static int gsi_server_initialized;
static const char gsi_initialize_diag[] = "gsi_initialize_mutex";

void
gfarm_gsi_initialize_mutex_lock(const char *diag)
{
	gfarm_mutex_lock(&gsi_initialize_mutex, diag, gsi_initialize_diag);
}

void
gfarm_gsi_initialize_mutex_unlock(const char *diag)
{
	gfarm_mutex_unlock(&gsi_initialize_mutex, diag, gsi_initialize_diag);
}

static void
gfarm_gsi_client_finalize_unlocked(void)
{
	gfarmSecSessionFinalizeInitiator();
	gsi_initialized = 0;
}

void
gfarm_gsi_client_finalize(void)
{
	static const char diag[] = "gfarm_gsi_client_finalize";

	gfarm_gsi_initialize_mutex_lock(diag);
	if (gsi_initialized)
		gfarm_gsi_client_finalize_unlocked();
	gfarm_gsi_initialize_mutex_unlock(diag);
}

gfarm_error_t
gfarm_gsi_client_initialize(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;
	static const char diag[] = "gfarm_gsi_client_initialize";

	gfarm_gsi_initialize_mutex_lock(diag);
	if (gsi_initialized) {
		gfarm_gsi_initialize_mutex_unlock(diag);
		return (GFARM_ERR_NO_ERROR);
	}

	rv = gfarmSecSessionInitializeInitiator(NULL, GRID_MAPFILE,
	    &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000706,
			    "can't initialize as initiator because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarm_gsi_client_finalize_unlocked();
		gfarm_gsi_initialize_mutex_unlock(diag);

		return (GFARM_ERRMSG_GSI_CREDENTIAL_INITIALIZATION_FAILED);
	}
	gsi_initialized = 1;
	gsi_server_initialized = 0;
	gfarm_gsi_initialize_mutex_unlock(diag);
	return (GFARM_ERR_NO_ERROR);
}

char *
gfarm_gsi_client_cred_name(void)
{
	gss_cred_id_t cred = gfarm_gsi_get_delegated_cred();
	gss_name_t name;
	OM_uint32 e_major, e_minor;
	char *dn;
	static pthread_mutex_t client_cred_initialize_mutex =
	    PTHREAD_MUTEX_INITIALIZER;
	static int initialized = 0;
	static char *client_dn;
	static const char diag[] = "gfarm_gsi_client_cred_name";
	static const char mutex_name[] = "client_cred_initialize_mutex";

	gfarm_mutex_lock(&client_cred_initialize_mutex, diag, mutex_name);
	if (initialized) {
		dn = client_dn;
		gfarm_mutex_unlock(&client_cred_initialize_mutex,
		    diag, mutex_name);
		return (dn);
	}

	if (cred == GSS_C_NO_CREDENTIAL &&
	    gfarmSecSessionGetInitiatorInitialCredential(&cred) < 0) {
		client_dn = NULL;
		gflog_auth_notice(GFARM_MSG_1000707,
		    "gfarm_gsi_client_cred_name(): "
		    "not initialized as an initiator");
	} else if (gfarmGssNewCredentialName(&name, cred, &e_major, &e_minor)
	    < 0) {
		client_dn = NULL;
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000708,
			    "cannot convert initiator credential "
			    "to name");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
	} else {
		client_dn = gfarmGssNewDisplayName(
		    name, &e_major, &e_minor, NULL);
		if (client_dn == NULL && gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000709,
			    "cannot convert initiator credential "
			    "to string");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarmGssDeleteName(&name, NULL, NULL);
	}
	initialized = 1;
	dn = client_dn;
	gfarm_mutex_unlock(&client_cred_initialize_mutex, diag, mutex_name);
	return (dn);
}

static void
gfarm_gsi_server_finalize_unlocked(void)
{
	gfarmSecSessionFinalizeBoth();
	gsi_initialized = 0;
	gsi_server_initialized = 0;
}

static pthread_cond_t gsi_server_init_count_cond = PTHREAD_COND_INITIALIZER;
static int gsi_server_init_count = 0;

static void
gsi_server_init_count_add(int i, const char *diag)
{
	static const char name[] = "init_count";

	gfarm_gsi_initialize_mutex_lock(diag);
	gsi_server_init_count += i;
	gfarm_cond_signal(&gsi_server_init_count_cond, diag, name);
	gfarm_gsi_initialize_mutex_unlock(diag);
}

void
gfarm_gsi_server_init_count_increment(void)
{
	static const char diag[] = "gsi_server_init_count_increment";

	gsi_server_init_count_add(1, diag);
}

void
gfarm_gsi_server_init_count_decrement(void)
{
	static const char diag[] = "gsi_server_init_count_decrement";

	gsi_server_init_count_add(-1, diag);
}

#define POSSIBLE_DEADLOCK_TIMEOUT	3600 /* 60 min */
#define POSSIBLE_DEADLOCK_COUNT		16

void
gfarm_gsi_server_finalize(void)
{
	static const char diag[] = "gfarm_gsi_server_finalize";
	static const char name[] = "init_count";
	struct timespec ts;
	int rc;

	gfarm_gsi_initialize_mutex_lock(diag);
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += POSSIBLE_DEADLOCK_TIMEOUT;
	while (gsi_server_init_count > 0) {
		gflog_info(GFARM_MSG_1003751, "%s: wait (%d)", diag,
		    gsi_server_init_count);
		rc = gfarm_cond_timedwait(&gsi_server_init_count_cond,
		    &gsi_initialize_mutex, &ts, diag, name);
		if (rc == 0 ||
		    gsi_server_init_count > POSSIBLE_DEADLOCK_COUNT) {
			/* backtrace may cause deadlock */
			gflog_set_fatal_action(GFLOG_FATAL_ACTION_ABORT);
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "%s: possible deadlock detected, die", diag);
		}
	}
	if (gsi_initialized && gsi_server_initialized)
		gfarm_gsi_server_finalize_unlocked();
	gfarm_gsi_initialize_mutex_unlock(diag);
}

gfarm_error_t
gfarm_gsi_server_initialize_unlocked(void)
{
	OM_uint32 e_major;
	OM_uint32 e_minor;
	int rv;

	if (gsi_initialized) {
		if (gsi_server_initialized)
			return (GFARM_ERR_NO_ERROR);
		else
			gfarm_gsi_client_finalize_unlocked();
	}

	rv = gfarmSecSessionInitializeBoth(NULL, NULL, GRID_MAPFILE,
	    &e_major, &e_minor);
	if (rv <= 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000710,
			    "can't initialize GSI as both because of:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		gfarm_gsi_server_finalize_unlocked();
		return (GFARM_ERRMSG_GSI_INITIALIZATION_FAILED);
	}
	gsi_initialized = 1;
	gsi_server_initialized = 1;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_gsi_server_initialize(void)
{
	static const char diag[] = "gfarm_gsi_server_initialize";
	gfarm_error_t e;

	gfarm_gsi_initialize_mutex_lock(diag);
	e = gfarm_gsi_server_initialize_unlocked();
	gfarm_gsi_initialize_mutex_unlock(diag);
	return (e);
}

/*
 * Delegated credential
 */

/*
 * XXX - thread-unsafe interface.  this assumes a single thread server
 * like gfsd and gfarm_gridftp_dsi.  this is not for gfmd.
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

gfarm_error_t
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
			return (GFARM_ERRMSG_CRED_TYPE_USER_CRED_INVALID_CRED_SERVICE);
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
			return (GFARM_ERRMSG_CRED_TYPE_SELF_CRED_INVALID_CRED_NAME);
		if (service != NULL)
			return (GFARM_ERRMSG_CRED_TYPE_SELF_CRED_INVALID_CRED_SERVICE);
		if (gfarmSecSessionGetInitiatorInitialCredential(&cred) < 0 ||
		    cred == GSS_C_NO_CREDENTIAL)
			return (GFARM_ERRMSG_CRED_TYPE_SELF_NOT_INITIALIZED_AS_AN_INITIATOR);
		rv = gfarmGssNewCredentialName(namep, cred, &e_major,&e_minor);
		break;
	default:
		return (GFARM_ERRMSG_INVALID_CRED_TYPE);
	}
	if (rv < 0) {
		if (gflog_auth_get_verbose()) {
			gflog_error(GFARM_MSG_1000711, "gfarmGssImportName(): "
			    "invalid credential configuration:");
			gfarmGssPrintMajorStatus(e_major);
			gfarmGssPrintMinorStatus(e_minor);
		}
		return (GFARM_ERRMSG_INVALID_CREDENTIAL_CONFIGURATION);
	}
	return (GFARM_ERR_NO_ERROR);
}
