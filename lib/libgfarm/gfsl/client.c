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

extern void	doClient(char *host, int port, gss_cred_id_t deleCred, int deleCheck);

static int port = 0;
static unsigned long int addr = 0L;
static char *hostname = NULL;

extern int testBufSize;

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
	} else if (strcmp(*argv, "-s") == 0) {
	    if (*(argv + 1) != NULL) {
		int tmp;
		if (gfarmGetInt(*(++argv), &tmp) < 0) {
		    fprintf(stderr, "illegal buffer size.\n");
		    return -1;
		}
		if (tmp <= 0) {
		    fprintf(stderr, "buffer size must be > 0.\n");
		    return -1;
		}
		testBufSize = tmp;
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

    if (ParseArgs(argc - 1, argv + 1) != 0) {
	return 1;
    }

    if (gfarmSecSessionInitializeInitiator(NULL, &majStat) <= 0) {
	fprintf(stderr, "can't initialize as initiator because of:\n");
	gfarmGssPrintStatus(stderr, majStat);
	gfarmSecSessionFinalizeInitiator();
	return 1;
    }

    doClient(hostname, port, GSS_C_NO_CREDENTIAL, 1);
    gfarmSecSessionFinalizeInitiator();

    return 0;
}

