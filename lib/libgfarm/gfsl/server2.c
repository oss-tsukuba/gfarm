#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <gssapi.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h>

#include <gfarm/gfarm_config.h>

#include "gfutil.h"

#include "tcputil.h"
#include "gfsl_config.h"
#include "gfarm_gsi.h"
#include "gfarm_auth.h"
#include "gfarm_secure_session.h"
#include "misc.h"

#include "scarg.h"

static int
ParseArgs(argc, argv)
     int argc;
     char *argv[];
{
    int c;

    while ((c = getopt(argc, argv, COMMON_OPTIONS)) != -1) {
	if (HandleCommonOptions(c, optarg) != 0)
	    return -1;
    }
    if (optind < argc) {
	fprintf(stderr, "unknown extra argument %s\n", argv[optind]);
	return -1;
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
    socklen_t remLen = sizeof(struct sockaddr_in);
    int fd0 = -1;
    int fd1 = -1;
    OM_uint32 majStat, minStat;
    gss_cred_id_t myCred;
    gfarmSecSession *ss0 = NULL;
    gfarmSecSession *ss1 = NULL;
    int sel;
    char *buf;
    int n;
    int i;

    gfarmSecSession *ssList[2];

    gflog_auth_set_verbose(1);
    if (gfarmSecSessionInitializeAcceptor(NULL, NULL,
					  &majStat, &minStat) <= 0) {
	fprintf(stderr, "can't initialize as acceptor because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }

    if (ParseArgs(argc, argv) != 0) {
	goto Done;
    }

    if (!acceptorSpecified) {
	myCred = GSS_C_NO_CREDENTIAL;
    } else {
	gss_name_t credName;
	char *credString;

	if (gfarmGssAcquireCredential(&myCred,
				      acceptorName, GSS_C_ACCEPT,
				      &majStat, &minStat, &credName) <= 0) {
	    fprintf(stderr, "can't acquire credential because of:\n");
	    gfarmGssPrintMajorStatus(majStat);
	    gfarmGssPrintMinorStatus(minStat);
	    return 1;
	}
	credString = newStringOfName(credName);
	fprintf(stderr, "Acceptor Credential: '%s'\n", credString);
	free(credString);
	gfarmGssDeleteName(&credName, NULL, NULL);
    }

    /*
     * Create a channel.
     */
    bindFd = gfarmTCPBindPort(port);
    if (bindFd < 0) {
	goto Done;
    }
    (void)gfarmIPGetNameOfSocket(bindFd, &port);
    fprintf(stderr, "Accepting port: %d\n", port);

    fd0 = accept(bindFd, (struct sockaddr *)&remote, &remLen);  
    if (fd0 < 0) {
	perror("accept");
	goto Done;
    }
    ss0 = gfarmSecSessionAccept(fd0, myCred, NULL, &majStat, &minStat);
    if (ss0 == NULL) {
	fprintf(stderr, "Can't create acceptor session because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }

    fd1 = accept(bindFd, (struct sockaddr *)&remote, &remLen);  
    if (fd1 < 0) {
	perror("accept");
	goto Done;
    }
    ss1 = gfarmSecSessionAccept(fd1, myCred, NULL, &majStat, &minStat);
    if (ss1 == NULL) {
	fprintf(stderr, "Can't create acceptor session because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
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
		i = gfarmSecSessionReceiveInt8(ss0, &buf, &n);
		if (i == 0) {
		    break;
		} else if (i < 0) {
		    fprintf(stderr, "1st session receive failed because of:\n");
		    gfarmSecSessionPrintStatus(ss0);
		    break;
		} else {
		    fprintf(stderr, "0: got %5d '", n);
		    write(2, buf, n);
		    fprintf(stderr, "'\n");
		    (void)free(buf);
		}
	    }
	    if (gfarmSecSessionCheckPollReadable(ss1)) {
		i = gfarmSecSessionReceiveInt8(ss1, &buf, &n);
		if (i == 0) {
		    break;
		} else if (i < 0) {
		    fprintf(stderr, "2nd session receive failed because of:\n");
		    gfarmSecSessionPrintStatus(ss0);
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
