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

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"
#include "tcputil.h"

int testBufSize = 4096;

void	doServer(int fd, char *host, int port);
void	doClient(char *host, int port, gss_cred_id_t deleCred, int deleCheck);


void
doServer(fd, hostname, port)
     int fd;
     char *hostname;
     int port;
{
    OM_uint32 majStat, minStat;
    char *rBuf = NULL;
    int tBufSz = -1;
    int n = -1;
    int rSz = -1;
    int dCheck = 0;
    long *tmpBuf;
    gfarmAuthEntry *aePtr = NULL;

    gfarmSecSession *initialSession =
    	gfarmSecSessionAccept(fd, GSS_C_NO_CREDENTIAL, NULL, &majStat,
			     &minStat);
    int x;

    if (initialSession == NULL) {
	fprintf(stderr, "Can't create acceptor session because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }
    aePtr = gfarmSecSessionGetInitiatorInfo(initialSession);
    fprintf(stderr, "Initiator: '%s' -> '%s'\n",
	    aePtr->distName,
	    (aePtr->authType == GFARM_AUTH_USER) ?
	    aePtr->authData.userAuth.localName :
	    aePtr->authData.hostAuth.FQDN);

    if (gfarmSecSessionDedicate(initialSession) < 0) {
	fprintf(stderr, "Can't dedicate to '%s'.\n",
		(aePtr->authType == GFARM_AUTH_USER) ?
		aePtr->authData.userAuth.localName :
		aePtr->authData.hostAuth.FQDN);
	goto Done;
    }

    /*
     * Now, we can communicate securely.
     */

    x = gfarmSecSessionReceiveLongs(initialSession, (long **)&tmpBuf, &n);
    if (x != 1) {
	fprintf(stderr, "can't receive test buffer size because of:\n");
	gfarmSecSessionPrintStatus(initialSession);
	goto Done;
    }
    tBufSz = *tmpBuf;
    (void)free(tmpBuf);
    fprintf(stderr, "Receive buffer size: %d\n", tBufSz);

    if (gfarmSecSessionReceiveBytes(initialSession, &rBuf, &rSz) <= 0) {
	fprintf(stderr, "test buffer receive failed because of:\n");
	gfarmSecSessionPrintStatus(initialSession);
	goto Done;
    }

    if (tBufSz != rSz) {
	fprintf(stderr,
		"test buffer size differ.\n"
		"\tOriginal: %10d\n"
		"\tReplyed:  %10d\n",
		tBufSz, rSz);
	goto Done;
    }

    if (gfarmSecSessionSendBytes(initialSession, rBuf, rSz) != rSz) {
	fprintf(stderr, "test buffer send failed because of:\n");
	gfarmSecSessionPrintStatus(initialSession);
	goto Done;
    }
    (void)free(rBuf);

    if (gfarmSecSessionReceiveLongs(initialSession, (long **)&tmpBuf, &n) != 1) {
	fprintf(stderr, "can't receive delegation check flag because of:\n");
	gfarmSecSessionPrintStatus(initialSession);
	goto Done;
    }
    dCheck = (int)*tmpBuf;
    if (dCheck == 1) {
	gss_cred_id_t deleCred = gfarmSecSessionGetDelegatedCredential(initialSession);
	if (deleCred != GSS_C_NO_CREDENTIAL) {
	    fprintf(stderr, "\nDelegation check.\n");
	    doClient(hostname, port, deleCred, 0);
	}
    }

    Done:
    gfarmSecSessionTerminate(initialSession);
    if (dCheck == 1) {
	gfarmSecSessionFinalizeBoth();
    }
    return;
}


static void	randomizeIt(char *buf, int len);
static void
randomizeIt(buf, len)
     char *buf;
     int len;
{
    int i;

    srand(time(NULL));

    for (i = 0; i < len; i++) {
	buf[i] = rand() % 256;
    }
}


void
doClient(hostname, port, deleCred, deleCheck)
     char *hostname;
     int port;
     gss_cred_id_t deleCred;
     int deleCheck;
{
    char *sBuf = NULL;
    char *rBuf = NULL;
    int rSz = -1;
    OM_uint32 majStat;
    OM_uint32 minStat;
    gfarmSecSession *ss =
    	gfarmSecSessionInitiateByName(hostname, port, deleCred, NULL,
				      &majStat, &minStat);

    if (ss == NULL) {
	fprintf(stderr, "Can't create initiator session because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	return;
    }

    /*
     * Now, we can communicate securely.
     */
    sBuf = (char *)malloc(sizeof(char *) * testBufSize);
    if (sBuf == NULL) {
	fprintf(stderr, "can't allocate test buffer.\n");
	goto Done;
    }
    randomizeIt(sBuf, testBufSize);

    if (gfarmSecSessionSendLongs(ss, (long *)&testBufSize, 1) != 1) {
	fprintf(stderr, "can't send test buffer size because of:\n");
	gfarmSecSessionPrintStatus(ss);
	goto Done;
    }
    fprintf(stderr, "Send buffer size: %d\n", testBufSize);

    if (gfarmSecSessionSendBytes(ss, sBuf, testBufSize) != testBufSize) {
	fprintf(stderr, "test buffer send failed because of:\n");
	gfarmSecSessionPrintStatus(ss);
	goto Done;
    }

    if (gfarmSecSessionReceiveBytes(ss, &rBuf, &rSz) <= 0) {
	fprintf(stderr, "test buffer receive failed because of:\n");
	gfarmSecSessionPrintStatus(ss);
	goto Done;
    }

    if (testBufSize != rSz) {
	fprintf(stderr,
		"test buffer size differ.\n"
		"\tOriginal: %10d\n"
		"\tReplyed:  %10d\n",
		testBufSize, rSz);
	goto Done;
    }

    if (memcmp((void *)sBuf, (void *)rBuf, testBufSize) != 0) {
	fprintf(stderr, "test buffer check failed.\n");
	goto Done;
    } else {
	fprintf(stderr, "test buffer check OK.\n");
    }

    if (gfarmSecSessionSendLongs(ss, (long *)&deleCheck, 1) != 1) {
	fprintf(stderr, "can't send delegation check flag.\n");
	goto Done;
    }

    Done:
    if (sBuf != NULL) {
	(void)free(sBuf);
    }
    if (rBuf != NULL) {
	(void)free(rBuf);
    }
    gfarmSecSessionTerminate(ss);

    return;
}
