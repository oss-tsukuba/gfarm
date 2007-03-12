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

#include "tcputil.h"

#include "gfarm_secure_session.h"
#include "gfarm_auth.h"

#include "scarg.h"

gfarm_int32_t testBufSize = 4096;

void	doServer(int fd, char *host, int port, gss_cred_id_t myCred,
		 gss_name_t acceptorName);
void	doClient(char *host, int port, gss_name_t acceptorName,
		 gss_cred_id_t deleCred, gfarm_int32_t deleCheck);


void
doServer(fd, hostname, port, myCred, acceptorName)
     int fd;
     char *hostname;
     int port;
     gss_cred_id_t myCred;
     gss_name_t acceptorName;
{
    OM_uint32 majStat, minStat;
    char *rBuf = NULL;
    int tBufSz = -1;
    int n = -1;
    int rSz = -1;
    int dCheck = 0;
    gfarm_int32_t *tmpBuf;
    gfarmAuthEntry *aePtr = NULL;
    char *name;

    gfarmSecSession *initialSession =
    	gfarmSecSessionAccept(fd, myCred, NULL, &majStat, &minStat);
    int x;

    if (initialSession == NULL) {
	fprintf(stderr, "Can't create acceptor session because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	goto Done;
    }
    name = newStringOfCredential(initialSession->cred);
    fprintf(stderr, "Accept => Acceptor: '%s'\n", name);
    free(name);
    aePtr = gfarmSecSessionGetInitiatorInfo(initialSession);
    fprintf(stderr, "Accept => Initiator: '%s' -> '%s'\n",
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

    x = gfarmSecSessionReceiveInt32(initialSession, &tmpBuf, &n);
    if (x != 1) {
	fprintf(stderr, "can't receive test buffer size because of:\n");
	gfarmSecSessionPrintStatus(initialSession);
	goto Done;
    }
    tBufSz = *tmpBuf;
    (void)free(tmpBuf);
    fprintf(stderr, "Receive buffer size: %d\n", tBufSz);

    if (gfarmSecSessionReceiveInt8(initialSession, &rBuf, &rSz) <= 0) {
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

    if (gfarmSecSessionSendInt8(initialSession, rBuf, rSz) != rSz) {
	fprintf(stderr, "test buffer send failed because of:\n");
	gfarmSecSessionPrintStatus(initialSession);
	goto Done;
    }
    (void)free(rBuf);

    if (gfarmSecSessionReceiveInt32(initialSession, &tmpBuf, &n) != 1) {
	fprintf(stderr, "can't receive delegation check flag because of:\n");
	gfarmSecSessionPrintStatus(initialSession);
	goto Done;
    }
    dCheck = *tmpBuf;
    if (dCheck == 1) {
	gss_cred_id_t deleCred = gfarmSecSessionGetDelegatedCredential(initialSession);
	if (deleCred != GSS_C_NO_CREDENTIAL) {
	    fprintf(stderr, "\nDelegation check.\n");
	    doClient(hostname, port, acceptorName, deleCred, 0);
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
doClient(hostname, port, acceptorName, deleCred, deleCheck)
     char *hostname;
     int port;
     gss_name_t acceptorName;
     gss_cred_id_t deleCred;
     gfarm_int32_t deleCheck;
{
    char *sBuf = NULL;
    char *rBuf = NULL;
    char *name;
    int rSz = -1;
    OM_uint32 majStat;
    OM_uint32 minStat;
    gfarmSecSession *ss =
    	gfarmSecSessionInitiateByName(hostname, port, acceptorName, deleCred,
				      GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG,
				      NULL, &majStat, &minStat);

    if (ss == NULL) {
	fprintf(stderr, "Can't create initiator session because of:\n");
	gfarmGssPrintMajorStatus(majStat);
	gfarmGssPrintMinorStatus(minStat);
	return;
    }

    name = newStringOfCredential(ss->cred);
    fprintf(stderr, "Initiate => Initiator: '%s'\n", name);
    free(name);
    name = newStringOfName(ss->iOaInfo.initiator.acceptorName);
    fprintf(stderr, "Initiate => Acceptor: '%s'\n", name);
    free(name);

    /*
     * Now, we can communicate securely.
     */
    GFARM_MALLOC_ARRAY(sBuf, testBufSize);
    if (sBuf == NULL) {
	fprintf(stderr, "can't allocate test buffer.\n");
	goto Done;
    }
    randomizeIt(sBuf, testBufSize);

    if (gfarmSecSessionSendInt32(ss, &testBufSize, 1) != 1) {
	fprintf(stderr, "can't send test buffer size because of:\n");
	gfarmSecSessionPrintStatus(ss);
	goto Done;
    }
    fprintf(stderr, "Send buffer size: %ld\n", (long)testBufSize);

    if (gfarmSecSessionSendInt8(ss, sBuf, testBufSize) != testBufSize) {
	fprintf(stderr, "test buffer send failed because of:\n");
	gfarmSecSessionPrintStatus(ss);
	goto Done;
    }

    if (gfarmSecSessionReceiveInt8(ss, &rBuf, &rSz) <= 0) {
	fprintf(stderr, "test buffer receive failed because of:\n");
	gfarmSecSessionPrintStatus(ss);
	goto Done;
    }

    if (testBufSize != rSz) {
	fprintf(stderr,
		"test buffer size differ.\n"
		"\tOriginal: %10ld\n"
		"\tReplyed:  %10d\n",
		(long)testBufSize, rSz);
	goto Done;
    }

    if (memcmp((void *)sBuf, (void *)rBuf, testBufSize) != 0) {
	fprintf(stderr, "test buffer check failed.\n");
	goto Done;
    } else {
	fprintf(stderr, "test buffer check OK.\n");
    }

    if (gfarmSecSessionSendInt32(ss, &deleCheck, 1) != 1) {
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
