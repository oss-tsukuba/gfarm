#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "gssapi.h"

#include "gfsl_config.h"
#include "gfarm_gsi.h"
#include "gfarm_secure_session.h"
#include "tcputil.h"
#include "misc.h"


static int port = 0;
static unsigned long int addr = 0L;
static char *hostname = NULL;

static int
ParseArgs(argc, argv)
     int argc;
     char *argv[];
{
    while (*argv != NULL) {
	if (strcmp(*argv, "-p") == 0) {
	    if (*(argv + 1) != NULL) {
		int tmp;
		if (gfarmGetInt(*(++argv), &tmp) < 0) {
		    fprintf(stderr, "illegal port number.\n");
		    return -1;
		}
		if (tmp <= 0) {
		    fprintf(stderr, "port number must be > 0.\n");
		    return -1;
		} else if (tmp > 65535) {
		    fprintf(stderr, "port number must be < 65536.\n");
		    return -1;
		}
		port = tmp;
	    }
	} else if (strcmp(*argv, "-h") == 0) {
	    if (*(argv + 1) != NULL) {
		unsigned long int tmp;
		argv++;
		tmp = gfarmIPGetAddressOfHost(*argv);
		if (tmp == ~0L || tmp == 0L) {
		    fprintf(stderr, "Invalid hostname.\n");
		    return -1;
		}
		addr = tmp;
		hostname = *argv;
	    }
	}

	argv++;
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

    if (ParseArgs(argc - 1, argv + 1) != 0) {
	goto Done;
    }

    if (gfarmSecSessionInitializeInitiator(NULL, &majStat, &minStat) <= 0) {
	fprintf(stderr, "can't initialize as initiator because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }

    ss0 = gfarmSecSessionInitiateByAddr(addr, port, GSS_C_NO_CREDENTIAL, NULL, &majStat, &minStat);
    if (ss0 == NULL) {
	fprintf(stderr, "Can't initiate session 0 because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }
    ss1 = gfarmSecSessionInitiateByAddr(addr, port, GSS_C_NO_CREDENTIAL, NULL, &majStat, &minStat);
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
	    (void)gfarmSecSessionSendBytes(ss0, buf, len);
	} else {
	    (void)gfarmSecSessionSendBytes(ss1, buf, len);
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

