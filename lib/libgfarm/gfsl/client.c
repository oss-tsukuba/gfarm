#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <gssapi.h>
#include <limits.h>

#include "gfutil.h"

#include "tcputil.h"
#include "gfsl_config.h"
#include "gfarm_gsi.h"
#include "gfarm_secure_session.h"
#include "misc.h"

#include "scarg.h"

extern void	doClient(char *host, int port, char *acceptorNameString, gss_OID acceptorNameType, gss_cred_id_t deleCred, int deleCheck);

static unsigned long int addr = 0L;
static char *hostname = NULL;

extern int testBufSize;

static int
ParseArgs(argc, argv)
     int argc;
     char *argv[];
{
    int c, tmp;

    while ((c = getopt(argc, argv, "s:h:" COMMON_OPTIONS)) != -1) {
	switch (c) {
	case 's':
	    if (gfarmGetInt(optarg, &tmp) < 0) {
		fprintf(stderr, "illegal buffer size.\n");
		return -1;
	    }
	    if (tmp <= 0) {
		fprintf(stderr, "buffer size must be > 0.\n");
		return -1;
	    }
	    testBufSize = tmp;
	    break;
	case 'h':
	    addr = gfarmIPGetAddressOfHost(optarg);
	    if (addr == ~0L || addr == 0L) {
		fprintf(stderr, "Invalid hostname.\n");
		return -1;
	    }
	    hostname = optarg;
	    break;
	default:
	    if (HandleCommonOptions(c, optarg) != 0)
		return -1;
	    break;
	}
    }

    if (optind < argc) {
	fprintf(stderr, "unknown extra argument %s\n", argv[optind]);
	return -1;
    }

    if (addr == 0L) {
	fprintf(stderr, "hostname is not specified.\n");
	return -1;
    }
    if (port == 0) {
	fprintf(stderr, "port # is not specified.\n");
	return -1;
    }

    return 0;
}


int
main(argc, argv)
     int argc;
     char *argv[];
{
    OM_uint32 majStat;
    OM_uint32 minStat;

    if (ParseArgs(argc, argv) != 0) {
	return 1;
    }

    gflog_auth_set_verbose(1);
    if (gfarmSecSessionInitializeInitiator(NULL, NULL, &majStat, &minStat) <= 0) {
	fprintf(stderr, "can't initialize as initiator because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	gfarmSecSessionFinalizeInitiator();
	return 1;
    }

    if (!acceptorSpecified) {
	acceptorNameString = malloc(sizeof("host@") + strlen(hostname));
	if (acceptorNameString == NULL) {
	    fprintf(stderr, "no memory\n");
	    return 1;
	}
	sprintf(acceptorNameString, "host@%s", hostname);
	acceptorNameType = GSS_C_NT_HOSTBASED_SERVICE;
    }

    doClient(hostname, port, acceptorNameString, acceptorNameType,
	     GSS_C_NO_CREDENTIAL, 1);
    gfarmSecSessionFinalizeInitiator();

    return 0;
}

