#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h>

#include "gssapi.h"

#include "gfsl_config.h"
#include "gfarm_gsi.h"
#include "gfarm_auth.h"
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
		break;
	    }
	    case 'm': case 'M': {
		t = 1024 * 1024;
		buf[lC] = '\0';
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

static int
ParseArgs(argc, argv)
     int argc;
     char *argv[];
{
    while (*argv != NULL) {
	if (strcmp(*argv, "-p") == 0) {
	    if (argv[1] != NULL &&
		*argv[1] != '\0') {
		int tmp;
		argv++;
		if (getInt(*argv, &tmp) < 0) {
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
	    } else {
		fprintf(stderr, "a port number must be specified.\n");
		return -1;
	    }
	}

	argv++;
    }
    
    return 0;
}


int
main(argc, argv)
     int argc;
     char *argv[];
{
    int ret = 1;
    int bindFd = -1;
    struct sockaddr_in remote;
    int remLen = sizeof(struct sockaddr_in);
    int fd0 = -1;
    int fd1 = -1;
    OM_uint32 majStat;
    gfarmSecSession *ss0 = NULL;
    gfarmSecSession *ss1 = NULL;
    int sel;
    char *buf;
    int n;
    int i;

    gfarmSecSession *ssList[2];

    if (ParseArgs(argc - 1, argv + 1) != 0) {
	goto Done;
    }

    if (gfarmSecSessionInitializeAcceptor(NULL, NULL, &majStat) <= 0) {
	fprintf(stderr, "can't initialize as acceptor because of:\n");
	gfarmGssPrintStatus(stderr, majStat);
	goto Done;
    }

    /*
     * Create a channel.
     */
    bindFd = BindPort(port);
    if (bindFd < 0) {
	goto Done;
    }
    (void)GetNameOfSocket(bindFd, &port);
    fprintf(stderr, "Accepting port: %d\n", port);

    fd0 = accept(bindFd, (struct sockaddr *)&remote, &remLen);  
    if (fd0 < 0) {
	perror("accept");
	goto Done;
    }
    ss0 = gfarmSecSessionAccept(fd0, GSS_C_NO_CREDENTIAL, NULL, &majStat);
    if (ss0 == NULL) {
	fprintf(stderr, "Can't create acceptor session because of:\n");
	gfarmGssPrintStatus(stderr, majStat);
	goto Done;
    }

    fd1 = accept(bindFd, (struct sockaddr *)&remote, &remLen);  
    if (fd1 < 0) {
	perror("accept");
	goto Done;
    }
    ss1 = gfarmSecSessionAccept(fd1, GSS_C_NO_CREDENTIAL, NULL, &majStat);
    if (ss1 == NULL) {
	fprintf(stderr, "Can't create acceptor session because of:\n");
	gfarmGssPrintStatus(stderr, majStat);
	goto Done;
    }

    if (gfarmSecSessionGetInitiatorInfo(ss0) !=
	gfarmSecSessionGetInitiatorInfo(ss1)) {
	fprintf(stderr, "1st initiator and 2nd initiator differ.\n");
	goto Done;
    }

    gfarmSecSessionDedicate(ss0);

    ssList[0] = ss0;
    ssList[1] = ss1;

    while (1) {
	gfarmSecSessionSetPollEvent(ss0, GFARM_SS_POLL_READABLE);
	gfarmSecSessionSetPollEvent(ss1, GFARM_SS_POLL_READABLE);

	sel = gfarmSecSessionPoll(ssList, 2, NULL);
	if (sel == 0) {
	    continue;
	} else if (sel > 0) {
	    if (gfarmSecSessionCheckPollReadable(ss0)) {
		i = gfarmSecSessionReceiveBytes(ss0, &buf, &n);
		if (i == 0) {
		    break;
		} else if (i < 0) {
		    fprintf(stderr, "1st session receive failed because of:\n");
		    gfarmSecSessionPrintStatus(stderr, ss0);
		    break;
		} else {
		    fprintf(stderr, "0: got %5d '", n);
		    write(2, buf, n);
		    fprintf(stderr, "'\n");
		    (void)free(buf);
		}
	    }
	    if (gfarmSecSessionCheckPollReadable(ss1)) {
		i = gfarmSecSessionReceiveBytes(ss1, &buf, &n);
		if (i == 0) {
		    break;
		} else if (i < 0) {
		    fprintf(stderr, "2nd session receive failed because of:\n");
		    gfarmSecSessionPrintStatus(stderr, ss0);
		    break;
		} else {
		    fprintf(stderr, "1: got %5d '", n);
		    write(2, buf, n);
		    fprintf(stderr, "'\n");
		    (void)free(buf);
		}
	    }
	} else {
	    break;
	}
    }
    ret = 0;

    Done:
    (void)close(bindFd);
    if (ss0 != NULL) {
	(void)close(fd0);
	gfarmSecSessionTerminate(ss0);
    }
    if (ss1 != NULL) {
	(void)close(fd1);
	gfarmSecSessionTerminate(ss1);
    }
    gfarmSecSessionFinalizeAcceptor();
    return ret;
}
