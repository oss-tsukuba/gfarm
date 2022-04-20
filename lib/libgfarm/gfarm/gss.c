#include <stdlib.h>
#include <dlfcn.h>

#include <gssapi.h>

#include <gfarm/gfarm.h>

#include "gfarm_auth.h"
#include "gfsl_secure_session.h"

#include "auth.h"
#include "auth_gss.h"
#include "gfarm_gss.h"
#include "gss.h"

static struct gfarm_gss *
gfarm_gss_dlopen(const char *libname, const char *proto,
	gss_cred_id_t (*client_cred_get)(void))
{
	void *lib = dlopen(libname, RTLD_LAZY|RTLD_LOCAL);
	struct gfarm_gss *gss;

	if (lib == NULL)
		return (NULL);
	GFARM_MALLOC(gss);
	if (gss == NULL) {
		dlclose(lib);
		return (NULL);
	}

	gss->protocol = proto;
	gss->client_cred_get = client_cred_get;

#define SYM(s) \
	gss->s = dlsym(lib, #s); \
	if (gss->s == NULL) { \
		gflog_warning(GFARM_MSG_UNFIXED, "%s: symbol %s not found", \
		    libname, #s); \
		break; \
	}

	do {
		SYM(gfarmGssPrintMajorStatus)
		SYM(gfarmGssPrintMinorStatus)

		SYM(gfarmGssImportName)
		SYM(gfarmGssImportNameOfHostBasedService)
		SYM(gfarmGssImportNameOfHost)
		SYM(gfarmGssDeleteName)
		SYM(gfarmGssNewCredentialName)
		SYM(gfarmGssNewDisplayName)

		SYM(gfarmGssAcquireCredential)
		SYM(gfarmGssDeleteCredential)

		SYM(gfarmSecSessionInitializeInitiator)
		SYM(gfarmSecSessionInitializeBoth)
		SYM(gfarmSecSessionFinalizeInitiator)
		SYM(gfarmSecSessionFinalizeBoth)

		SYM(gfarmSecSessionReceiveInt8)
		SYM(gfarmSecSessionSendInt8)

		SYM(gfarmSecSessionInitiate)
		SYM(gfarmSecSessionInitiateRequest)
		SYM(gfarmSecSessionInitiateResult)
		SYM(gfarmSecSessionAccept)
		SYM(gfarmSecSessionGetInitiatorInfo)
		SYM(gfarmSecSessionTerminate)

		SYM(gfarmAuthGetDistName)
		SYM(gfarmAuthGetLocalName)
		SYM(gfarmAuthGetAuthEntryType)
		SYM(gfarmAuthGetAuthEntryType)

		return (gss);
	} while (0);

	free(gss);
	dlclose(lib);

	return (NULL);
}

#ifdef HAVE_GSI

static struct gfarm_gss *libgfsl_gsi;

static void
libgfsl_gsi_initialize(void)
{
	libgfsl_gsi = gfarm_gss_dlopen(LIBGFSL_GSI, "gsi",
	    gfarm_gsi_client_cred_get);
}


struct gfarm_gss *
gfarm_gss_gsi(void)
{
	static pthread_once_t initialized;

	pthread_once(&initialized, libgfsl_gsi_initialize);
	return (libgfsl_gsi);
}

#endif /* HAVE_GSI */

#ifdef HAVE_KERBEROS

static struct gfarm_gss *libgfsl_kerberos;

static void
libgfsl_kerberos_initialize(void)
{
	libgfsl_kerberos = gfarm_gss_dlopen(LIBGFSL_KERBEROS, "kerberos",
	    gfarm_kerberos_client_cred_get);
}

struct gfarm_gss *
gfarm_gss_kerberos(void)
{
	static pthread_once_t initialized;

	pthread_once(&initialized, libgfsl_kerberos_initialize);
	return (libgfsl_kerberos);
}

#endif /* HAVE_KERBEROS */
