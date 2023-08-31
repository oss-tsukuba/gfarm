#include <pthread.h>
#include <stdlib.h>
#include <dlfcn.h>

#include <gssapi.h>

#include <gfarm/gfarm.h>

#include "gfsl_secure_session.h"

#include "gss.h"

struct gfarm_gss *
gfarm_gss_dlopen(const char *libname, const char *proto,
	struct gfarm_auth_gss_ops *gfarm_ops)
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

	gss->gfarm_ops = gfarm_ops;
	gss->protocol = proto;

#define SYM(s) \
	gss->s = dlsym(lib, #s); \
	if (gss->s == NULL) { \
		gflog_warning(GFARM_MSG_1005291, "%s: symbol %s not found", \
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

		SYM(gfarmSecSessionAccept)
		SYM(gfarmSecSessionInitiate)
		SYM(gfarmSecSessionInitiateRequest)
		SYM(gfarmSecSessionInitiateResult)
		SYM(gfarmSecSessionTerminate)

		SYM(gfarmSecSessionReceiveInt8)
		SYM(gfarmSecSessionSendInt8)

		SYM(gfarmSecSessionGetInitiatorDistName)

		return (gss);
	} while (0);

	free(gss);
	dlclose(lib);

	return (NULL);
}
