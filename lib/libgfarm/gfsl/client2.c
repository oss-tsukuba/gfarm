#include <sys/types.h>
#include <pwd.h>
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

static unsigned long int addr = 0L;
static char *hostname = NULL;

static int
ParseArgs(argc, argv)
     int argc;
     char *argv[];
{
    int c, tmp;

    while ((c = getopt(argc, argv, "h:"  COMMON_OPTIONS)) != -1) {
	switch (c) {
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
    gfarmSecSession *ss0 = NULL;
    gfarmSecSession *ss1 = NULL;
    char buf[4096];
    int z = 0;
    int ret = 1;
    int len;

    if (ParseArgs(argc, argv) != 0) {
	goto Done;
    }

    gflog_auth_set_verbose(1);
    if (gfarmSecSessionInitializeInitiator(NULL, NULL, &majStat, &minStat) <= 0) {
	fprintf(stderr, "can't initialize as initiator because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }

    if (!acceptorSpecified) {
	acceptorNameString = malloc(sizeof("host@") + strlen(hostname));
	if (acceptorNameString == NULL) {
	    fprintf(stderr, "no memory\n");
	    goto Done;
	}
	sprintf(acceptorNameString, "host@%s", hostname);
	acceptorNameType = GSS_C_NT_HOSTBASED_SERVICE;
    }

    ss0 = gfarmSecSessionInitiateByAddr(addr, port, acceptorNameString, acceptorNameType, GSS_C_NO_CREDENTIAL, NULL, &majStat, &minStat);
    if (ss0 == NULL) {
	fprintf(stderr, "Can't initiate session 0 because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }
    ss1 = gfarmSecSessionInitiateByAddr(addr, port, acceptorNameString, acceptorNameType, GSS_C_NO_CREDENTIAL, NULL, &majStat, &minStat);
    if (ss1 == NULL) {
	fprintf(stderr, "Can't initiate session 1 because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }

    while (fgets(buf, 4096, stdin) != NULL) {
	len = strlen(buf);
	if (buf[len - 1] == '\n') {
	    len--;
	}
	if (z % 2 == 0) {
	    (void)gfarmSecSessionSendInt8(ss0, buf, len);
	} else {
	    (void)gfarmSecSessionSendInt8(ss1, buf, len);
	}
	z++;
    }
    ret = 0;

    Done:
    if (ss0 != NULL) {
	gfarmSecSessionTerminate(ss0);
    }
    if (ss1 != NULL) {
	gfarmSecSessionTerminate(ss1);
    }
    gfarmSecSessionFinalizeInitiator();

    return ret;
}

