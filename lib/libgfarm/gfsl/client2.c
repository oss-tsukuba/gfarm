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


static int
getInt(char *str, int *val)
{
    char *ePtr = NULL;
    int ret = -1;
    int t = 1;
    char *buf = NULL;
    int tmp;
    int base = 10;
    int len;
    int neg = 1;

    switch ((int)str[0]) {
	case '-': {
	    neg = -1;
	    str++;
	    break;
	}
	case '+': {
	    str++;
	    break;
	}
    }
    if (strncmp(str, "0x", 2) == 0) {
	base = 16;
	str += 2;
    }

    buf = strdup(str);
    if (buf == NULL) {
	return -1;
    }
    len = strlen(buf);
    if (len == 0) {
	return -1;
    }

    if (base == 10) {
	int lC = len - 1;
	switch ((int)(buf[lC])) {
	    case 'k': case 'K': {
		t = 1024;
		buf[lC] = '\0';
		len--;
		break;
	    }
	    case 'm': case 'M': {
		t = 1024 * 1024;
		buf[lC] = '\0';
		len--;
		break;
	    }
	}
    }

    tmp = (int)strtol(buf, &ePtr, base);
    if (ePtr == (buf + len)) {
	ret = 1;
	*val = tmp * t * neg;
    }

    (void)free(buf);
    return ret;
}


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
		if (getInt(*(++argv), &tmp) < 0) {
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
		tmp = GetIPAddressOfHost(*argv);
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
    gfarmSecSession *ss0 = NULL;
    gfarmSecSession *ss1 = NULL;
    char buf[4096];
    int z = 0;
    int ret = 1;
    int len;

    if (ParseArgs(argc - 1, argv + 1) != 0) {
	goto Done;
    }

    if (gfarmSecSessionInitializeInitiator(NULL, &majStat) <= 0) {
	fprintf(stderr, "can't initialize as initiator because of:\n");
	gfarmGssPrintStatus(stderr, majStat);
	goto Done;
    }

    ss0 = gfarmSecSessionInitiateByAddr(addr, port, GSS_C_NO_CREDENTIAL, NULL, &majStat);
    if (ss0 == NULL) {
	fprintf(stderr, "Can't initiate session 0 because of:\n");
	gfarmGssPrintStatus(stderr, majStat);
	goto Done;
    }
    ss1 = gfarmSecSessionInitiateByAddr(addr, port, GSS_C_NO_CREDENTIAL, NULL, &majStat);
    if (ss1 == NULL) {
	fprintf(stderr, "Can't initiate session 1 because of:\n");
	gfarmGssPrintStatus(stderr, majStat);
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

