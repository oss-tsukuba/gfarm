#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/time.h>

#include "gssapi.h"

#include <gfarm/gfarm_config.h>
#include "gfarm_secure_session.h"
#include "tcputil.h"
#include "misc.h"

/* #define SS_DEBUG */

/*
 * Initial credential and its name.
 */
static gss_cred_id_t initiatorInitialCred = GSS_C_NO_CREDENTIAL;
static gss_cred_id_t acceptorInitialCred = GSS_C_NO_CREDENTIAL;
static char *initiatorInitialCredName = NULL;
static char *acceptorInitialCredName = NULL;

static int initiatorInitialized = 0;
static int acceptorInitialized = 0;

/*
 * Communication option read from configuration file.
 */
static gfarmSecSessionOption acceptorSsOpt = GFARM_SS_DEFAULT_OPTION;
static gfarmSecSessionOption initiatorSsOpt = GFARM_SS_DEFAULT_OPTION;

static int			canonicSecSessionOpt(int which, 
				      gfarmSecSessionOption *reqPtr,
				      gfarmSecSessionOption *canPtr);
static gfarmSecSession *	allocSecSession(int which);
static void			destroySecSession(gfarmSecSession *ssPtr);
static int			secSessionReadConfigFile(char *configFile,
				      gfarmSecSessionOption *ssOptPtr);
static int			negotiateConfigParam(int fd,
				      gss_ctx_id_t sCtx,
				      int which,
				      gfarmSecSessionOption *canPtr,
				      gss_qop_t *qOpPtr,
				      unsigned int *maxTransPtr,
				      unsigned int *configPtr,
				      OM_uint32 *majStatPtr,
				      OM_uint32 *minStatPtr);
static gfarmSecSession *	secSessionInitiate(int fd,
				      gss_cred_id_t cred,
				      gfarmSecSessionOption *ssOptPtr,
				      OM_uint32 *majStatPtr,
				      OM_uint32 *minStatPtr,
				      int needClose);


#ifdef SS_DEBUG
static void	dumpConfParam(gss_qop_t qOp, unsigned int max, unsigned int conf);
static void	dumpSsOpt(gfarmSecSessionOption *ssOptPtr);


static void
dumpConfParam(qOp, max, conf)
     gss_qop_t qOp;
     unsigned int max;
     unsigned int conf;
{
    fprintf(stderr, "\tQOP:\t%d\n", (int)qOp);
    fprintf(stderr, "\tMAX:\t%d\n", max);
    fprintf(stderr, "\tOPT:\t(%d)", conf);
    if (conf & GFARM_SS_USE_ENCRYPTION) {
	fprintf(stderr, " encrypt");
    }
    if (conf & GFARM_SS_USE_COMPRESSION) {
	fprintf(stderr, " compress");
    }
    fprintf(stderr, "\n");
}


static void
dumpSsOpt(ssOptPtr)
     gfarmSecSessionOption *ssOptPtr;
{
    fprintf(stderr, "\n\tQOP:\t");
    if (ssOptPtr->optMask & GFARM_SS_OPT_QOP_MASK) {
	fprintf(stderr, "%d%s\n", (int)ssOptPtr->qOpReq,
		(ssOptPtr->qOpForce == 1) ? " (force)" : "");
    } else {
	fprintf(stderr, "none\n");
    }

    fprintf(stderr, "\tMAX:\t");
    if (ssOptPtr->optMask & GFARM_SS_OPT_MAXT_MASK) {
	fprintf(stderr, "%d%s\n", (int)ssOptPtr->maxTransSizeReq,
		(ssOptPtr->maxTransSizeForce == 1) ? " (force)" : "");
    } else {
	fprintf(stderr, "none\n");
    }

    fprintf(stderr, "\tOPT:\t");
    if (ssOptPtr->optMask & GFARM_SS_OPT_CONF_MASK) {
	fprintf(stderr, "(%d)", ssOptPtr->configReq);
	if (ssOptPtr->configReq & GFARM_SS_USE_ENCRYPTION) {
	    fprintf(stderr, " encrypt");
	}
	if (ssOptPtr->configReq & GFARM_SS_USE_COMPRESSION) {
	    fprintf(stderr, " compress");
	}
	if (ssOptPtr->configForce == 1) {
	    fprintf(stderr, " (force)");
	}
	fprintf(stderr, "\n\n");
    } else {
	fprintf(stderr, "none\n\n");
    }
}
#endif /* SS_DEBUG */


static int
secSessionReadConfigFile(configFile, ssOptPtr)
     char *configFile;
     gfarmSecSessionOption *ssOptPtr;
{
    FILE *fd = NULL;
    char lineBuf[65536];
    char *token[64];
    int nToken;
    gfarmSecSessionOption ssTmp = GFARM_SS_DEFAULT_OPTION;
    int i;

    if (configFile == NULL || configFile[0] == '\0') {
	return -1;
    }

    if ((fd = fopen(configFile, "r")) == NULL) {
	/*
	 * use default option.
	 */
	goto Done;
    }

    while (fgets(lineBuf, 65536, fd) != NULL) {
	nToken = gfarmGetToken(lineBuf, token, 64);
	if (nToken <= 0) {
	    continue;
	}
	if (token[0][0] == '#') {
	    continue;
	}

	if (strcasecmp(token[0], "qop:") == 0) {
	    for (i = 1; i < nToken; i++) {
		if (strcasecmp(token[i], "force") == 0) {
		    ssTmp.qOpForce = 1;
		} else if (strcasecmp(token[i], "default") == 0 ||
			   strcasecmp(token[i], "gfarm") == 0) {
		    ssTmp.qOpReq = GFARM_GSS_DEFAULT_QOP;
#if defined(USE_GLOBUS) && defined(GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG)
		} else if (strcasecmp(token[i], "globus") == 0) {
		    ssTmp.qOpReq = GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG;
#endif /* USE_GLOBUS */
		} else if (strcasecmp(token[i], "gssapi") == 0) {
		    ssTmp.qOpReq = GSS_C_QOP_DEFAULT;
		}
	    }
	} else if (strcasecmp(token[0], "maxtrans:") == 0) {
	    for (i = 1; i < nToken; i++) {
		if (strcasecmp(token[i], "force") == 0) {
		    ssTmp.maxTransSizeForce = 1;
		} else if (strcasecmp(token[i], "default") == 0) {
		    ssTmp.maxTransSizeReq = GFARM_GSS_DEFAULT_MAX_MESSAGE_REQUEST_SIZE;
		} else {
		    int val;
		    if (gfarmGetInt(token[i], &val) == 1) {
			if (val < 0) {
			    val = GFARM_GSS_DEFAULT_MAX_MESSAGE_REQUEST_SIZE;
			}
			ssTmp.maxTransSizeReq = (unsigned int)val;
		    }
		}
	    }
	} else if (strcasecmp(token[0], "options:") == 0) {
	    unsigned int opt = GFARM_SS_USE_ENCRYPTION;
	    for (i = 1; i < nToken; i++) {
		if (strcasecmp(token[i], "force") == 0) {
		    ssTmp.configForce = 1;
		} else if (strcasecmp(token[i], "encrypt") == 0) {
		    opt |= GFARM_SS_USE_ENCRYPTION;
		} else if (strcasecmp(token[i], "compress") == 0) {
		    opt |= GFARM_SS_USE_COMPRESSION;
		} else if (strcasecmp(token[i], "default") == 0) {
		    opt = GFARM_SS_USE_ENCRYPTION;
		}
	    }
	    ssTmp.configReq = opt;
	}
    }
    (void)fclose(fd);

    Done:
    if (ssOptPtr != NULL) {
	memcpy((void *)ssOptPtr, (void *)&ssTmp, sizeof(gfarmSecSessionOption));
	ssOptPtr->optMask = GFARM_SS_OPT_ALL_MASK;
    }
    return 1;
}


static int
canonicSecSessionOpt(which, reqPtr, canPtr)
     int which;
     gfarmSecSessionOption *reqPtr;
     gfarmSecSessionOption *canPtr;
{
    gfarmSecSessionOption *dPtr = (which == GFARM_SS_INITIATOR) ?
    	&initiatorSsOpt : &acceptorSsOpt;

    if (canPtr == NULL) {
	return -1;
    }
    if (reqPtr == NULL) {
	(void)memcpy((void *)canPtr, (void *)dPtr, sizeof(gfarmSecSessionOption));
	return 1;
    }

#define isMasked(op, msk) ((op)->optMask & msk) == msk
#define isUseReq(req, mbr) (req)->mbr == 1 || ((req)->mbr == 0 && dPtr->mbr == 0)
#define cpIt(dst, src, mbr) (dst)->mbr = (src)->mbr
#define useReq(mbr) cpIt(canPtr, reqPtr, mbr)
#define useDef(mbr) cpIt(canPtr, dPtr, mbr)

    canPtr->optMask |= GFARM_SS_OPT_QOP_MASK;
    if (isMasked(reqPtr, GFARM_SS_OPT_QOP_MASK)) {
	if (isUseReq(reqPtr, qOpForce)) {
	    useReq(qOpReq);
	    useReq(qOpForce);
	} else {
	    goto useQOPDef;
	}
    } else {
	useQOPDef:
	useDef(qOpReq);
	useDef(qOpForce);
    }

    canPtr->optMask |= GFARM_SS_OPT_MAXT_MASK;
    if (isMasked(reqPtr, GFARM_SS_OPT_MAXT_MASK)) {
	if (isUseReq(reqPtr, maxTransSizeForce)) {
	    useReq(maxTransSizeReq);
	    useReq(maxTransSizeForce);
	} else {
	    goto useMaxTDef;
	}
    } else {
	useMaxTDef:
	useDef(maxTransSizeReq);
	useDef(maxTransSizeForce);
    }

    canPtr->optMask |= GFARM_SS_OPT_CONF_MASK;
    if (isMasked(reqPtr, GFARM_SS_OPT_CONF_MASK)) {
	if (isUseReq(reqPtr, configForce)) {
	    useReq(configReq);
	    useReq(configForce);
	} else {
	    goto useConfigDef;
	}
    } else {
	useConfigDef:
	useDef(configReq);
	useDef(configForce);
    }

    return 1;
#undef isMasked
#undef isUseReq
#undef cpIt
#undef useReq
#undef useDef
}


static int
negotiateConfigParam(fd, sCtx, which, canPtr, qOpPtr, maxTransPtr, configPtr, majStatPtr, minStatPtr)
     int fd;
     gss_ctx_id_t sCtx;
     int which;
     gfarmSecSessionOption *canPtr;
     gss_qop_t *qOpPtr;
     unsigned int *maxTransPtr;
     unsigned int *configPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
#define SS_CONF_ACK	1
#define SS_CONF_NACK	0
#define SS_CONF_NEGO	2
    int ret = -1;
    gss_qop_t retQOP = GFARM_GSS_DEFAULT_QOP;
    unsigned int retMaxT = GFARM_GSS_DEFAULT_MAX_MESSAGE_REQUEST_SIZE;
    unsigned int retConf = GFARM_SS_USE_ENCRYPTION;
    char NACK = SS_CONF_NACK;
    char ACK = SS_CONF_ACK;
    char NEGO = SS_CONF_NEGO;
    char stat;

    int negoPhase = 0;
    int dQOP = (int)(GFARM_GSS_DEFAULT_QOP);
    unsigned int dMax = GFARM_GSS_DEFAULT_MAX_MESSAGE_REQUEST_SIZE;
    unsigned int dConf = GFARM_SS_USE_ENCRYPTION;

#define SendCmd(p) { if (gfarmWriteBytes(fd, (char *)&(p), 1) != 1) { break; }}
#define SendACK(dum) SendCmd(ACK)
#define SendNACK(dum) SendCmd(NACK)
#define SendNEGO(dum) SendCmd(NEGO)

#define ReadCmd(p) { if (gfarmReadBytes(fd, (char *)&(p), 1) != 1) { break; }}

#define SendParam(x) {if (gfarmWriteLongs(fd, (long *)&(x), 1) != 1) {break;}}
#define ReadParam(x) {if (gfarmReadLongs(fd, (long *)&(x), 1) != 1) {break;}}

    if (sCtx == GSS_C_NO_CONTEXT) {
	return ret;
    }

    switch (which) {
	case GFARM_SS_ACCEPTOR: {
	    int iQOP, iQOPF;
	    unsigned int iMax;
	    int iMaxF;
	    int iConf, iConfF;

	    /* QOP negotiation. */
	    ReadParam(iQOP);
	    ReadParam(iQOPF);
	    if (canPtr->qOpReq != iQOP) {
		if (canPtr->qOpForce == 1 && iQOPF == 1) {
		    /*
		     * Both sides want to rule the world.
		     * No 2nd thought sigh.
		     */
		    SendNACK(0);
		    break;
		} else if (iQOPF == 1) {
		    /*
		     * Use the initiator's QOP
		     */
		    dQOP = iQOP;
		    goto setQOP;
		} else {
		    /*
		     * (canPtr->qOpForce == 1 ||
		     *  Both force flags == 0)
		     * Propose the acceptor's QOP
		     */
		    SendNEGO(2);
		    SendParam(canPtr->qOpReq);
		    ReadCmd(stat);
		    if (stat != ACK) {
			break;
		    } else {
			dQOP = canPtr->qOpReq;
			goto setQOP;
		    }
		}
	    } else {
		dQOP = iQOP;
		setQOP:
		SendACK(1);
		retQOP = (gss_qop_t)dQOP;
		negoPhase++;
	    }

	    /* Max transmission size negotiation. */
	    ReadParam(iMax);
	    ReadParam(iMaxF);
	    if (canPtr->maxTransSizeReq != iMax) {
		if (canPtr->maxTransSizeForce == 1 && iMaxF == 1) {
		    SendNACK(0);
		    break;
		} else if (iMaxF == 1) {
		    dMax = iMax;
		    goto setMax;
		} else {
		    int tmpMax;
		    if (canPtr->maxTransSizeForce == 1) {
			tmpMax = canPtr->maxTransSizeReq;
		    } else {
			tmpMax = (canPtr->maxTransSizeReq < iMax) ?
				canPtr->maxTransSizeReq : iMax;
		    }
		    SendNEGO(2);
		    SendParam(tmpMax);
		    ReadCmd(stat);
		    if (stat != ACK) {
			break;
		    } else {
			dMax = tmpMax;
			goto setMax;
		    }
		}
	    } else {
		dMax = iMax;
		setMax:
		SendACK(1);
		retMaxT = dMax;
		negoPhase++;
	    }

	    /* Configuration flag negotiation. */
	    ReadParam(iConf);
	    ReadParam(iConfF);
	    if (canPtr->configReq != iConf) {
		if (canPtr->configForce == 1 && iConfF == 1) {
		    SendNACK(0);
		    break;
		} else if (iConfF == 1) {
		    dConf = iConf;
		    goto setConf;
		} else {
		    SendNEGO(2);
		    SendParam(canPtr->configReq);
		    ReadCmd(stat);
		    if (stat != ACK) {
			break;
		    } else {
			dConf = canPtr->configReq;
			goto setConf;
		    }
		}
	    } else {
		dConf = iConf;
		setConf:
		SendACK(1);
		retConf = dConf;
		negoPhase++;
	    }

	    /* End of initiator side negotiation. */
	    break;
	}

	case GFARM_SS_INITIATOR: {
	    /* QOP */
	    SendParam(canPtr->qOpReq);
	    SendParam(canPtr->qOpForce);
	    ReadCmd(stat);
	    switch (stat) {
		case SS_CONF_ACK: {
		    retQOP = canPtr->qOpReq;
		    break;
		}
		case SS_CONF_NACK: {
		    goto Done;
		}
		case SS_CONF_NEGO: {
		    ReadParam(retQOP);
		    SendACK(1);
		    break;
		}
	    }
	    negoPhase++;

	    /* Max transmission size */
	    SendParam(canPtr->maxTransSizeReq);
	    SendParam(canPtr->maxTransSizeForce);
	    ReadCmd(stat);
	    switch (stat) {
		case SS_CONF_ACK: {
		    retMaxT = canPtr->maxTransSizeReq;
		    break;
		}
		case SS_CONF_NACK: {
		    goto Done;
		}
		case SS_CONF_NEGO: {
		    ReadParam(retMaxT);
		    SendACK(1);
		    break;
		}
	    }
	    negoPhase++;

	    /* Configuration flag */
	    SendParam(canPtr->configReq);
	    SendParam(canPtr->configForce);
	    ReadCmd(stat);
	    switch (stat) {
		case SS_CONF_ACK: {
		    retConf = canPtr->configReq;
		    break;
		}
		case SS_CONF_NACK: {
		    goto Done;
		}
		case SS_CONF_NEGO: {
		    ReadParam(retConf);
		    SendACK(1);
		    break;
		}
	    }
	    negoPhase++;
	    
	    break;
	}
    }

    Done:
    if (negoPhase >= 3) {
	OM_uint32 majStat, minStat;
	unsigned int maxMsgSize;
	int doEncrypt = GFARM_GSS_ENCRYPTION_ENABLED &
    			(isBitSet(retConf,
				  GFARM_SS_USE_ENCRYPTION) ? 1 : 0);
	if (gfarmGssConfigureMessageSize(sCtx, doEncrypt,
					 retQOP, retMaxT, &maxMsgSize,
					 &majStat, &minStat) < 0) {
	    if (majStatPtr != NULL) {
		*majStatPtr = majStat;
	    }
	    if (minStatPtr != NULL) {
		*minStatPtr = minStat;
	    }
	    return ret;
	}

	if (majStatPtr != NULL) {
	    *majStatPtr = majStat;
	}
	if (minStatPtr != NULL) {
	    *minStatPtr = minStat;
	}
	if (qOpPtr != NULL) {
	    *qOpPtr = retQOP;
	}
	if (maxTransPtr != NULL) {
	    *maxTransPtr = maxMsgSize;
	}
	if (configPtr != NULL) {
	    *configPtr = retConf;
	}

	ret = 1;
    }
    return ret;

#undef SS_CONF_ACK
#undef SS_CONF_NACK
#undef SS_CONF_NEGO
#undef SendCmd
#undef SendACK
#undef SendNACK
#undef SendNEGO
#undef ReadCmd
#undef SendParam
#undef ReadParam
}


static gfarmSecSession *
allocSecSession(which)
     int which;
{
    gfarmSecSession *ret = (gfarmSecSession *)malloc(sizeof(gfarmSecSession));
    if (ret == NULL) {
	return NULL;
    }
    (void)memset((void *)ret, 0, sizeof(gfarmSecSession));
    switch (which) {
	case GFARM_SS_INITIATOR: {
	    break;
	}
	case GFARM_SS_ACCEPTOR: {
	    ret->iOaInfo.acceptor.deleCred = GSS_C_NO_CREDENTIAL;
	    break;
	}
	default: {
	    (void)free(ret);
	    ret = NULL;
	    return NULL;
	}
    }

    ret->iOa = which;
    ret->cred = GSS_C_NO_CREDENTIAL;
    ret->sCtx = GSS_C_NO_CONTEXT;
    ret->gssLastStat = GSS_S_COMPLETE;
    return ret;
}


static void
destroySecSession(ssPtr)
     gfarmSecSession *ssPtr;
{
    if (ssPtr != NULL) {
	OM_uint32 minStat;

	if (ssPtr->peerName != NULL) {
	    (void)free(ssPtr->peerName);
	}
	if (ssPtr->credName != NULL) {
	    (void)free(ssPtr->credName);
	}

	switch (ssPtr->iOa) {
	    case GFARM_SS_INITIATOR: {
		if (ssPtr->iOaInfo.initiator.acceptorDistName != NULL) {
		    (void)free(ssPtr->iOaInfo.initiator.acceptorDistName);
		}
		break;
	    }
	    case GFARM_SS_ACCEPTOR: {
		if (ssPtr->iOaInfo.acceptor.deleCred != GSS_C_NO_CREDENTIAL) {
		    (void)gss_release_cred(&minStat,
					   &(ssPtr->iOaInfo.acceptor.deleCred));
		}
		if (ssPtr->iOaInfo.acceptor.mappedUser != NULL) {
		    /*
		     * ssPtr->iOaInfo.acceptor.mappedUser may be NULL,
		     * if gfarmSecSessionAccept() aborts during initialization.
		     */
		    ssPtr->iOaInfo.acceptor.mappedUser->sesRefCount--;
		    if (ssPtr->iOaInfo.acceptor.mappedUser->sesRefCount < 0) {
			ssPtr->iOaInfo.acceptor.mappedUser->sesRefCount = 0;
		    }
		    if (ssPtr->iOaInfo.acceptor.mappedUser->sesRefCount == 0 &&
			ssPtr->iOaInfo.acceptor.mappedUser->orphaned == 1) {
			gfarmAuthDestroyUserEntry(ssPtr->iOaInfo.acceptor.mappedUser);
		    }
		}
		break;
	    }
	    default: {
		break;
	    }
	}

	if (ssPtr->sCtx != GSS_C_NO_CONTEXT) {
	    (void)gss_delete_sec_context(&minStat, &(ssPtr->sCtx), GSS_C_NO_BUFFER);
	}

	if (ssPtr->needClose == 1) {
	    (void)close(ssPtr->fd);
	}

	(void)free(ssPtr);
    }
}


char **
gfarmSecSessionCrackStatus(ssPtr)
     gfarmSecSession *ssPtr;
{
    return gfarmGssCrackMajorStatus(ssPtr->gssLastStat);
}


void
gfarmSecSessionFreeCrackedStatus(strPtr)
     char **strPtr;
{
    gfarmGssFreeCrackedStatus(strPtr);
}


void
gfarmSecSessionPrintStatus(ssPtr)
     gfarmSecSession *ssPtr;
{
    gfarmGssPrintMajorStatus(ssPtr->gssLastStat);
}


int
gfarmSecSessionInitializeAcceptor(configFile, usermapFile, majStatPtr, minStatPtr)
     char *configFile;
     char *usermapFile;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    int ret = 1;
    OM_uint32 majStat = GSS_S_COMPLETE;
    OM_uint32 minStat = GSS_S_COMPLETE;

    if (acceptorInitialized == 0) {
	char confFile[PATH_MAX];

	/*
	 * Get a credential.
	 */
	if (acceptorInitialCred == GSS_C_NO_CREDENTIAL &&
	    acceptorInitialCredName == NULL) {
	    if (gfarmGssAcquireCredential(&acceptorInitialCred,
					  GSS_C_ACCEPT,
					  &majStat,
					  &minStat,
					  &acceptorInitialCredName) < 0) {
		ret = -1;
		goto Done;
	    }
	}

	/*
	 * Read config file.
	 */
	if (configFile == NULL || configFile[0] == '\0') {
	    char *confDir = gfarmGetEtcDir();
	    if (confDir == NULL) {
		majStat = GSS_S_FAILURE;
		ret = -1;
		goto Done;
	    }
#ifdef HAVE_SNPRINTF
	    snprintf(confFile, sizeof confFile, "%s/%s", confDir, GFARM_DEFAULT_ACCEPTOR_CONFIG_FILE);
#else
	    sprintf(confFile, "%s/%s", confDir, GFARM_DEFAULT_ACCEPTOR_CONFIG_FILE);
#endif
	    (void)free(confDir);
	    configFile = confFile;
	}
	if (secSessionReadConfigFile(configFile, &acceptorSsOpt) < 0) {
	    majStat = GSS_S_FAILURE;
	    ret = -1;
	    goto Done;
	}

	/*
	 * Authorization init.
	 */
	if (gfarmAuthInitialize(usermapFile) < 0) {
	    majStat = GSS_S_FAILURE;
	    ret = -1;
	    goto Done;
	}

	Done:
	if (ret == -1) {
	    if (acceptorInitialCred != GSS_C_NO_CREDENTIAL) {
		OM_uint32 minStat;
		(void)gss_release_cred(&minStat, &acceptorInitialCred);
		acceptorInitialCred = GSS_C_NO_CREDENTIAL;
	    }
	    if (acceptorInitialCredName != NULL) {
		(void)free(acceptorInitialCredName);
		acceptorInitialCredName = NULL;
	    }
	} else {
	    acceptorInitialized = 1;
	}
    }

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
}


int
gfarmSecSessionInitializeInitiator(configFile, majStatPtr, minStatPtr)
     char *configFile;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    int ret = 1;
    OM_uint32 majStat = GSS_S_COMPLETE;
    OM_uint32 minStat = GSS_S_COMPLETE;

    if (initiatorInitialized == 0) {
	char confFile[PATH_MAX];

	/*
	 * Get a credential.
	 */
	if (initiatorInitialCred == GSS_C_NO_CREDENTIAL &&
	    initiatorInitialCredName == NULL) {
	    if (gfarmGssAcquireCredential(&initiatorInitialCred,
					  GSS_C_INITIATE,
					  &majStat,
  					  &minStat,
					  &initiatorInitialCredName) < 0) {
		ret = -1;
		goto Done;
	    }
	}

	/*
	 * Read config file.
	 */
	if (configFile == NULL || configFile[0] == '\0') {
	    char *confDir = gfarmGetEtcDir();
	    if (confDir == NULL) {
		majStat = GSS_S_FAILURE;
		ret = -1;
		goto Done;
	    }
#ifdef HAVE_SNPRINTF
	    snprintf(confFile, sizeof confFile, "%s/%s", confDir, GFARM_DEFAULT_INITIATOR_CONFIG_FILE);
#else
	    sprintf(confFile, "%s/%s", confDir, GFARM_DEFAULT_INITIATOR_CONFIG_FILE);
#endif
	    (void)free(confDir);
	    configFile = confFile;
	}
	if (secSessionReadConfigFile(configFile, &initiatorSsOpt) < 0) {
	    majStat = GSS_S_FAILURE;
	    ret = -1;
	    goto Done;
	}

	Done:
	if (ret == -1) {
	    if (initiatorInitialCred != GSS_C_NO_CREDENTIAL) {
		OM_uint32 minStat;
		(void)gss_release_cred(&minStat, &initiatorInitialCred);
		initiatorInitialCred = GSS_C_NO_CREDENTIAL;
	    }
	    if (initiatorInitialCredName != NULL) {
		(void)free(initiatorInitialCredName);
		initiatorInitialCredName = NULL;
	    }
	} else {
	    initiatorInitialized = 1;
	}
    }

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
}


int
gfarmSecSessionInitializeBoth(iConfigFile, aConfigFile, usermapFile, majStatPtr, minStatPtr)
     char *iConfigFile;
     char *aConfigFile;
     char *usermapFile;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    int ret = 1;
    OM_uint32 majStat = GSS_S_COMPLETE;
    OM_uint32 minStat = GSS_S_COMPLETE;

    /*
     * If the process's effective user is root (getuid()==0):
     *		acceptor:	host credential (GSS_C_BOTH)
     *		initiator:	host credential (GSS_C_BOTH)
     *
     * Otherwise (uid!=0):
     *		acceptor:	user credential (GSS_C_BOTH)
     *		initiator:	user credential (GSS_C_BOTH)
     *
     * XXXXX FIXME:
     * 	This implementation depends on gssapi_ssleay of Globus. With
     *	other GSSAPI implementation, a major change could be needed.
     */

    if (initiatorInitialized == 0 && acceptorInitialized == 0) {
	char confFile[PATH_MAX];
	char *confDir = NULL;

	if (acceptorInitialCred == GSS_C_NO_CREDENTIAL &&
	    acceptorInitialCredName == NULL) {
	    if (gfarmGssAcquireCredential(&acceptorInitialCred,
					  GSS_C_BOTH,
					  &majStat,
					  &minStat,
					  &acceptorInitialCredName) < 0) {
		ret = -1;
		goto Done;
	    }
	    initiatorInitialCred = acceptorInitialCred;
	    initiatorInitialCredName = strdup(acceptorInitialCredName);
	}

	/*
	 * Read config file.
	 */
	if ((aConfigFile == NULL || aConfigFile[0] == '\0') ||
	    (iConfigFile == NULL || iConfigFile[0] == '\0')) {
	    confDir = gfarmGetEtcDir();
	    if (confDir == NULL) {
		majStat = GSS_S_FAILURE;
		ret = -1;
		goto Done;
	    }
	}

	/*
	 * Acceptor's configuration
	 */
	if (aConfigFile == NULL || aConfigFile[0] == '\0') {
#ifdef HAVE_SNPRINTF
	    snprintf(confFile, sizeof confFile, "%s/%s", confDir, GFARM_DEFAULT_ACCEPTOR_CONFIG_FILE);
#else
	    sprintf(confFile, "%s/%s", confDir, GFARM_DEFAULT_ACCEPTOR_CONFIG_FILE);
#endif
	    aConfigFile = confFile;
	}
	if (secSessionReadConfigFile(aConfigFile, &acceptorSsOpt) < 0) {
	    majStat = GSS_S_FAILURE;
	    ret = -1;
	    goto Done;
	}

	/*
	 * Initiator's configuration
	 */
	if (iConfigFile == NULL || iConfigFile[0] == '\0') {
#ifdef HAVE_SNPRINTF
	    snprintf(confFile, sizeof confFile, "%s/%s", confDir, GFARM_DEFAULT_INITIATOR_CONFIG_FILE);
#else
	    sprintf(confFile, "%s/%s", confDir, GFARM_DEFAULT_INITIATOR_CONFIG_FILE);
#endif
	    iConfigFile = confFile;
	}
	if (secSessionReadConfigFile(iConfigFile, &initiatorSsOpt) < 0) {
	    majStat = GSS_S_FAILURE;
	    ret = -1;
	    goto Done;
	}

	/*
	 * Authorization init.
	 */
	if (gfarmAuthInitialize(usermapFile) < 0) {
	    majStat = GSS_S_FAILURE;
	    ret = -1;
	    goto Done;
	}

	Done:
	if (confDir != NULL) {
	    (void)free(confDir);
	}
	if (ret == -1) {
	    if (acceptorInitialCred != GSS_C_NO_CREDENTIAL) {
		OM_uint32 minStat;
		(void)gss_release_cred(&minStat, &acceptorInitialCred);
		acceptorInitialCred = GSS_C_NO_CREDENTIAL;
	    }
	    if (acceptorInitialCredName != NULL) {
		(void)free(acceptorInitialCredName);
		acceptorInitialCredName = NULL;
	    }
	    if (initiatorInitialCredName != NULL) {
		(void)free(initiatorInitialCredName);
		initiatorInitialCredName = NULL;
	    }
	    initiatorInitialCred = GSS_C_NO_CREDENTIAL;
	} else {
	    initiatorInitialized = 1;
	    acceptorInitialized = 1;
	}
    }

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
}


void
gfarmSecSessionFinalizeInitiator()
{
    if (initiatorInitialized == 1) {
	if (initiatorInitialCred != GSS_C_NO_CREDENTIAL) {
	    OM_uint32 minStat;
	    (void)gss_release_cred(&minStat, &initiatorInitialCred);
	    initiatorInitialCred = GSS_C_NO_CREDENTIAL;
	}
	if (initiatorInitialCredName != NULL) {
	    (void)free(initiatorInitialCredName);
	    initiatorInitialCredName = NULL;
	}
	initiatorInitialized = 0;
    }
}


void
gfarmSecSessionFinalizeAcceptor()
{
    if (acceptorInitialized == 1) {
	if (acceptorInitialCred != GSS_C_NO_CREDENTIAL) {
	    OM_uint32 minStat;
	    (void)gss_release_cred(&minStat, &acceptorInitialCred);
	    acceptorInitialCred = GSS_C_NO_CREDENTIAL;
	}
	if (acceptorInitialCredName != NULL) {
	    (void)free(acceptorInitialCredName);
	    acceptorInitialCredName = NULL;
	}
	gfarmAuthFinalize();
	acceptorInitialized = 0;
    }
}


void
gfarmSecSessionFinalizeBoth()
{
    if (initiatorInitialized == 1 && acceptorInitialized == 1) {
	if (acceptorInitialCred != GSS_C_NO_CREDENTIAL) {
	    OM_uint32 minStat;
	    (void)gss_release_cred(&minStat, &acceptorInitialCred);
	    acceptorInitialCred = GSS_C_NO_CREDENTIAL;
	}
	if (acceptorInitialCredName != NULL) {
	    (void)free(acceptorInitialCredName);
	    acceptorInitialCredName = NULL;
	}
	if (initiatorInitialCredName != NULL) {
	    (void)free(initiatorInitialCredName);
	    initiatorInitialCredName = NULL;
	    initiatorInitialCred = GSS_C_NO_CREDENTIAL;
	}
	gfarmAuthFinalize();
	acceptorInitialized = 0;
	initiatorInitialized = 0;
    }
}


gfarmSecSession *
gfarmSecSessionAccept(fd, cred, ssOptPtr, majStatPtr, minStatPtr)
     int fd;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    gfarmSecSession *ret = NULL;
    gfarmSecSessionOption canOpt = GFARM_SS_DEFAULT_OPTION;
    gfarmAuthEntry *entry = NULL;

    unsigned long int rAddr = INADDR_ANY;
    int rPort = 0;
    char *peerName = NULL;

    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GSS_S_COMPLETE;
    gss_ctx_id_t sCtx = GSS_C_NO_CONTEXT;
    char *initiatorDistName = NULL;
    gss_cred_id_t deleCred = GSS_C_NO_CREDENTIAL;
    char *credName = NULL;
    int acknack = GFARM_SS_AUTH_NACK;

    gss_qop_t qOp;
    unsigned int maxTransSize;
    unsigned int config;

    if (acceptorInitialized == 0) {
	goto Fail;
    }

    ret = allocSecSession(GFARM_SS_ACCEPTOR);
    if (ret == NULL) {
	goto Fail;
    }

    if (canonicSecSessionOpt(GFARM_SS_ACCEPTOR, ssOptPtr, &canOpt) < 0) {
	goto Fail;
    }

    /*
     * Get a peer information.
     */
    rAddr = gfarmIPGetPeernameOfSocket(fd, &rPort);
    if (rAddr != 0 && rPort != 0) {
	peerName = gfarmIPGetHostOfAddress(rAddr);
    }

    /*
     * Check the credential.
     */
    if (cred == GSS_C_NO_CREDENTIAL) {
	cred = acceptorInitialCred;
	credName = strdup(acceptorInitialCredName);
    } else {
	credName = gfarmGssGetCredentialName(cred);
    }

    /*
     * Phase 1: Accept a security context.
     */
    if (gfarmGssAcceptSecurityContext(fd, cred, &sCtx,
				      &majStat,
				      &minStat,
				      &initiatorDistName,
				      &deleCred) < 0) {
	goto Fail;
    }
    if (initiatorDistName == NULL || initiatorDistName[0] == '\0') {
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 2: Authorization and send ACK/NACK
     */
    entry = gfarmAuthGetUserEntry(initiatorDistName);
    if (entry == NULL) {
	majStat = GSS_S_UNAUTHORIZED;
	/* Send NACK. */
	acknack = GFARM_SS_AUTH_NACK;
	(void)gfarmWriteLongs(fd, (long *)&acknack, 1);
	goto Fail;
    } else {
	int type = gfarmAuthGetAuthEntryType(entry);
	if (type == GFARM_AUTH_USER) {
	    /* Send ACK. */
	    acknack = GFARM_SS_AUTH_ACK;
	    (void)gfarmWriteLongs(fd, (long *)&acknack, 1);
	} else if (type == GFARM_AUTH_HOST) {
	    /* check peer name is actually allowed */
	    if (strcmp(peerName, entry->authData.hostAuth.FQDN) == 0) {
		/* Send ACK. */
		acknack = GFARM_SS_AUTH_ACK;
		(void)gfarmWriteLongs(fd, (long *)&acknack, 1);
	    } else {
		/* Send NACK. */
		acknack = GFARM_SS_AUTH_NACK;
		(void)gfarmWriteLongs(fd, (long *)&acknack, 1);
		goto Fail;
	    }
	}
    }

    /*
     * Phase 3: Negotiate configuration parameters
     * with the initiator.
     */
    if (negotiateConfigParam(fd, sCtx, GFARM_SS_ACCEPTOR, &canOpt,
			     &qOp, &maxTransSize, &config, &majStat,
			     &minStat) < 0) {
	goto Fail;
    }
#if 0
    fprintf(stderr, "Acceptor config:\n");
    dumpConfParam(qOp, maxTransSize, config);
#endif

    /*
     * Success: Fill all members of session struct out.
     */
    ret->fd = fd;
    ret->rAddr = rAddr;
    ret->rPort = rPort;
    ret->peerName = peerName;
    ret->cred = cred;
    ret->credName = credName;
    ret->sCtx = sCtx;
    ret->iOa = GFARM_SS_ACCEPTOR;
    ret->iOaInfo.acceptor.mappedUser = entry;
    ret->iOaInfo.acceptor.mappedUser->sesRefCount++;
    ret->iOaInfo.acceptor.deleCred = deleCred;
    ret->qOp = qOp;
    ret->maxTransSize = maxTransSize;
    ret->config = config;
    ret->gssLastStat = majStat;
    goto Done;

    Fail:
    if (ret != NULL) {
	destroySecSession(ret);
	ret = NULL;
    }

    Done:
    if (initiatorDistName != NULL) {
	(void)free(initiatorDistName);
    }
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
}


static gfarmSecSession *
secSessionInitiate(fd, cred, ssOptPtr, majStatPtr, minStatPtr, needClose)
     int fd;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     int needClose;
{
    gfarmSecSession *ret = NULL;
    gfarmSecSessionOption canOpt = GFARM_SS_DEFAULT_OPTION;

    unsigned long int rAddr = INADDR_ANY;
    int rPort = 0;
    char *peerName = NULL;

    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GSS_S_COMPLETE;
    gss_ctx_id_t sCtx = GSS_C_NO_CONTEXT;
    char *acceptorDistName = NULL;
    char *credName = NULL;
    int acknack = GFARM_SS_AUTH_NACK;

    gss_qop_t qOp;
    unsigned int maxTransSize;
    unsigned int config;

    if (initiatorInitialized == 0) {
	goto Fail;
    }

    ret = allocSecSession(GFARM_SS_INITIATOR);
    if (ret == NULL) {
	goto Fail;
    }

    if (canonicSecSessionOpt(GFARM_SS_INITIATOR, ssOptPtr, &canOpt) < 0) {
	goto Fail;
    }

    /*
     * Get a peer information.
     */
    rAddr = gfarmIPGetPeernameOfSocket(fd, &rPort);
    if (rAddr != 0 && rPort != 0) {
	peerName = gfarmIPGetHostOfAddress(rAddr);
    }

    /*
     * Check the credential.
     */
    if (cred == GSS_C_NO_CREDENTIAL) {
	cred = initiatorInitialCred;
	credName = strdup(initiatorInitialCredName);
    } else {
	credName = gfarmGssGetCredentialName(cred);
    }

    /*
     * Phase 1: Initiate a security context.
     */
    if (gfarmGssInitiateSecurityContext(fd, cred,
					GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG,
					&sCtx,
					&majStat,
					&minStat,
					&acceptorDistName) < 0) {
	goto Fail;
    }
    if (acceptorDistName == NULL || acceptorDistName[0] == '\0') {
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 2: Receive authorization acknowledgement.
     */
    if (gfarmReadLongs(fd, (long *)&acknack, 1) != 1) {
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }
    if (acknack == GFARM_SS_AUTH_NACK) {
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 3: Negotiate configuration parameters
     * with the acceptor.
     */
    if (negotiateConfigParam(fd, sCtx, GFARM_SS_INITIATOR, &canOpt,
			     &qOp, &maxTransSize, &config, &majStat,
			     &minStat) < 0) {
	goto Fail;
    }
#if 0
    fprintf(stderr, "Initiator config:\n");
    dumpConfParam(qOp, maxTransSize, config);
#endif

    /*
     * Success: Fill all members of session struct out.
     */
    ret->fd = fd;
    ret->needClose = needClose;
    ret->rAddr = rAddr;
    ret->rPort = rPort;
    ret->peerName = peerName;
    ret->cred = cred;
    ret->credName = credName;
    ret->sCtx = sCtx;
    ret->iOa = GFARM_SS_INITIATOR;
    ret->iOaInfo.initiator.reqFlag = GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG;
    ret->iOaInfo.initiator.acceptorDistName = strdup(acceptorDistName);
    ret->qOp = qOp;
    ret->maxTransSize = maxTransSize;
    ret->config = config;
    ret->gssLastStat = majStat;
    goto Done;

    Fail:
    if (ret != NULL) {
	destroySecSession(ret);
	ret = NULL;
    }

    Done:
    if (acceptorDistName != NULL) {
	(void)free(acceptorDistName);
    }
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
}


gfarmSecSession *
gfarmSecSessionInitiate(fd, cred, ssOptPtr, majStatPtr, minStatPtr)
     int fd;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    return secSessionInitiate(fd, cred, ssOptPtr, majStatPtr, minStatPtr, 0);
}


gfarmSecSession *
gfarmSecSessionInitiateByAddr(rAddr, port, cred, ssOptPtr, majStatPtr, minStatPtr)
     unsigned long rAddr;
     int port;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    int fd = gfarmTCPConnectPort(rAddr, port);
    if (fd < 0) {
	if (majStatPtr != NULL) {
	    *majStatPtr = GSS_S_FAILURE;
	}
	return NULL;
    }
    return secSessionInitiate(fd, cred, ssOptPtr, majStatPtr, minStatPtr, 1);
}


gfarmSecSession *
gfarmSecSessionInitiateByName(hostname, port, cred, ssOptPtr, majStatPtr, minStatPtr)
     char *hostname;
     int port;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    unsigned long rAddr = gfarmIPGetAddressOfHost(hostname);
    if (rAddr == ~0L || rAddr == 0L) {
	if (majStatPtr != NULL) {
	    *majStatPtr = GSS_S_FAILURE;
	    *minStatPtr = GSS_S_COMPLETE;
	}
	return NULL;
    }
    return gfarmSecSessionInitiateByAddr(rAddr, port, cred, ssOptPtr,
					 majStatPtr, minStatPtr);
}


void
gfarmSecSessionTerminate(ssPtr)
     gfarmSecSession *ssPtr;
{
    destroySecSession(ssPtr);
}


gss_cred_id_t
gfarmSecSessionGetDelegatedCredential(ssPtr)
     gfarmSecSession *ssPtr;
{
    if (ssPtr->iOa == GFARM_SS_INITIATOR) {
	return GSS_C_NO_CREDENTIAL;
    }
    return ssPtr->iOaInfo.acceptor.deleCred;
}


gfarmAuthEntry *
gfarmSecSessionGetInitiatorInfo(ssPtr)
     gfarmSecSession *ssPtr;
{
    if (ssPtr->iOa == GFARM_SS_INITIATOR) {
	return NULL;
    }
    return ssPtr->iOaInfo.acceptor.mappedUser;
}


int
gfarmSecSessionDedicate(ssPtr)
     gfarmSecSession *ssPtr;
{
    int ret = -1;
    gfarmAuthEntry *aePtr = gfarmSecSessionGetInitiatorInfo(ssPtr);
    if (aePtr != NULL) {
	gid_t gid = getgid();
	uid_t uid = getuid();
	gfarmAuthMakeThisAlone(aePtr);
	if (uid == 0 && aePtr->authType == GFARM_AUTH_USER) {
	    if (aePtr->authData.userAuth.gid != gid) {
		if ((ret = setgid(aePtr->authData.userAuth.gid)) < 0) {
		    ssPtr->gssLastStat = GSS_S_FAILURE;
		    goto Done;
		}
	    }
	    if (aePtr->authData.userAuth.uid != uid) {
		if ((ret = setuid(aePtr->authData.userAuth.uid)) < 0) {
		    ssPtr->gssLastStat = GSS_S_FAILURE;
		    goto Done;
		}
	    }
	}
	ret = 1;
    } else {
	ssPtr->gssLastStat = GSS_S_FAILURE;
    }

    Done:
    return ret;
}


int
gfarmSecSessionSendBytes(ssPtr, buf, n)
     gfarmSecSession *ssPtr;
     char *buf;
     int n;
{
    int doEncrypt = GFARM_GSS_ENCRYPTION_ENABLED &
    		    (isBitSet(ssPtr->config,
			      GFARM_SS_USE_ENCRYPTION) ? 1 : 0);
    return gfarmGssSend(ssPtr->fd,
			ssPtr->sCtx,
			doEncrypt,
			ssPtr->qOp,
			buf,
			n,
			ssPtr->maxTransSize,
			&(ssPtr->gssLastStat));
}


int
gfarmSecSessionReceiveBytes(ssPtr, bufPtr, lenPtr)
     gfarmSecSession *ssPtr;
     char **bufPtr;
     int *lenPtr;
{
    return gfarmGssReceive(ssPtr->fd,
			   ssPtr->sCtx,
			   bufPtr,
			   lenPtr,
			   &(ssPtr->gssLastStat));
}


int
gfarmSecSessionSendLongs(ssPtr, buf, n)
     gfarmSecSession *ssPtr;
     long *buf;
     int n;
{
    long *lBuf = (long *)malloc(sizeof(long) * n);
    int i;
    int ret = -1;

    if (lBuf == NULL) {
	ssPtr->gssLastStat = GSS_S_FAILURE;
	return ret;
    }
    for (i = 0; i < n; i++) {
	lBuf[i] = htonl(buf[i]);
    }
    
    ret = gfarmSecSessionSendBytes(ssPtr, (char *)lBuf, n * sizeof(long));
    (void)free(lBuf);
    if (ret > 0) {
	ret /= sizeof(long);
    }
    return ret;
}


int
gfarmSecSessionReceiveLongs(ssPtr, bufPtr, lenPtr)
     gfarmSecSession *ssPtr;
     long **bufPtr;
     int *lenPtr;
{
    char *lBuf = NULL;
    char *lbPtr = NULL;
    long *retBuf = NULL;
    long tmp;
    int len = 0;
    int i;
    int n;
    int ret = gfarmSecSessionReceiveBytes(ssPtr, &lBuf, &len);
    
    if (ret <= 0) {
	goto Done;
    }
    n = len % sizeof(long);
    if (n != 0) {
	goto Done;
    }
    n = len / sizeof(long);

    retBuf = (long *)malloc(sizeof(long) * n);
    if (retBuf == NULL) {
	goto Done;
    }

    lbPtr = lBuf;
    for (i = 0; i < n; i++) {
	memcpy((void *)&tmp, (void *)lbPtr, sizeof(long));
	retBuf[i] = ntohl(tmp);
	lbPtr += sizeof(long);
    }
    ret = n;
    if (lenPtr != NULL) {
	*lenPtr = n;
    }
    if (bufPtr != NULL) {
	*bufPtr = retBuf;
    }

    Done:
    if (lBuf != NULL) {
	(void)free(lBuf);
    }
    return ret;
}


int
gfarmSecSessionSendShorts(ssPtr, buf, n)
     gfarmSecSession *ssPtr;
     short *buf;
     int n;
{
    short *lBuf = (short *)malloc(sizeof(short) * n);
    int i;
    int ret = -1;

    if (lBuf == NULL) {
	ssPtr->gssLastStat = GSS_S_FAILURE;
	return ret;
    }
    for (i = 0; i < n; i++) {
	lBuf[i] = htons(buf[i]);
    }
    
    ret = gfarmSecSessionSendBytes(ssPtr, (char *)lBuf, n * sizeof(short));
    (void)free(lBuf);
    if (ret > 0) {
	ret /= sizeof(short);
    }
    return ret;
}


int
gfarmSecSessionReceiveShorts(ssPtr, bufPtr, lenPtr)
     gfarmSecSession *ssPtr;
     short **bufPtr;
     int *lenPtr;
{
    char *lBuf = NULL;
    char *lbPtr = NULL;
    short *retBuf = NULL;
    short tmp;
    int len = 0;
    int i;
    int n;
    int ret = gfarmSecSessionReceiveBytes(ssPtr, &lBuf, &len);
    
    if (ret <= 0) {
	goto Done;
    }
    n = len % sizeof(short);
    if (n != 0) {
	goto Done;
    }
    n = len / sizeof(short);

    retBuf = (short *)malloc(sizeof(short) * n);
    if (retBuf == NULL) {
	goto Done;
    }

    lbPtr = lBuf;
    for (i = 0; i < n; i++) {
	memcpy((void *)&tmp, (void *)lbPtr, sizeof(short));
	retBuf[i] = ntohs(tmp);
	lbPtr += sizeof(short);
    }
    ret = n;
    if (lenPtr != NULL) {
	*lenPtr = n;
    }
    if (bufPtr != NULL) {
	*bufPtr = retBuf;
    }

    Done:
    if (lBuf != NULL) {
	(void)free(lBuf);
    }
    return ret;
}


int
gfarmSecSessionPoll(ssList, n, toPtr)
     gfarmSecSession *ssList[];
     int n;
     struct timeval *toPtr;
{
    int nFds = -INT_MAX;
    int i;
    fd_set rFds;
    fd_set wFds;
    fd_set eFds;
    gfarmSecSession *ssPtr;
    int fdChk = 0;
    int ret;

    FD_ZERO(&rFds);
    FD_ZERO(&wFds);
    FD_ZERO(&eFds);

    for (i = 0; i < n; i++) {
	fdChk = 0;
	ssPtr = ssList[i];
	if (gfarmSecSessionCheckPollReadable(ssPtr)) {
	    FD_SET(ssPtr->fd, &rFds);
	    fdChk++;
	}
	if (gfarmSecSessionCheckPollWritable(ssPtr)) {
	    FD_SET(ssPtr->fd, &wFds);
	    fdChk++;
	}
	if (gfarmSecSessionCheckPollError(ssPtr)) {
	    FD_SET(ssPtr->fd, &eFds);
	    fdChk++;
	}
	if (fdChk > 0) {
	    if (nFds < ssPtr->fd) {
		nFds = ssPtr->fd;
	    }
	}
    }
    if (nFds < 0) {
	return 0;
    }
    nFds++;

    doSel:
    ret = select(nFds, &rFds, &wFds, &eFds, toPtr);
    if (ret == 0) {
	for (i = 0; i < n; i++) {
	    gfarmSecSessionClearPollEvent(ssList[i]);
	}
    } else if (ret < 0) {
	if (errno == EINTR) {
	    goto doSel;
	}
    } else {
	for (i = 0; i < n; i++) {
	    ssPtr = ssList[i];
	    gfarmSecSessionClearPollEvent(ssPtr);
	    if (FD_ISSET(ssPtr->fd, &rFds)) {
		ssPtr->pollEvent |= GFARM_SS_POLL_READABLE;
	    }
	    if (FD_ISSET(ssPtr->fd, &wFds)) {
		ssPtr->pollEvent |= GFARM_SS_POLL_WRITABLE;
	    }
	    if (FD_ISSET(ssPtr->fd, &eFds)) {
		ssPtr->pollEvent |= GFARM_SS_POLL_ERROR;
	    }
	}
    }
    return ret;
}

