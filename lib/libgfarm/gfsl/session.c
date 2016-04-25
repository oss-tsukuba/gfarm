#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>

#include <gssapi.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/time.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>

#include "gfevent.h"
#include "gfutil.h"
#include "thrsubr.h"

#include "tcputil.h"
#include "gfarm_secure_session.h"
#include "misc.h"

/* #define SS_DEBUG */

static int initiatorInitialized = 0;
static int acceptorInitialized = 0;

static pthread_mutex_t initiator_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t acceptor_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char initiatorDiag[] = "initiator";
static const char acceptorDiag[] = "acceptor";

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
				      int *gsiErrNoPtr,
				      OM_uint32 *majStatPtr,
				      OM_uint32 *minStatPtr);
static gfarmSecSession *	secSessionInitiate(int fd,
				      const gss_name_t acceptorName,
				      gss_cred_id_t cred,
				      OM_uint32 reqFlag,
				      gfarmSecSessionOption *ssOptPtr,
				      int *gsiErrNoPtr,
				      OM_uint32 *majStatPtr,
				      OM_uint32 *minStatPtr,
				      int needClose);

static void negotiateConfigParamInitiatorReceive(int events,
						 int fd,
						 void *closure,
						 const struct timeval *t);
static void negotiateConfigParamInitiatorSend(int events,
					      int fd,
					      void *closure,
					      const struct timeval *t);
static struct negotiateConfigParamInitiatorState *
	negotiateConfigParamInitiatorRequest(struct gfarm_eventqueue *q,
					     int fd,
					     gss_ctx_id_t sCtx,
					     gfarmSecSessionOption *canPtr,
					     void (*continuation)(void *),
					     void *closure,
					     OM_uint32 *majStatPtr,
					     OM_uint32 *minStatPtr);
static int negotiateConfigParamInitiatorResult(struct negotiateConfigParamInitiatorState *state,
					       gss_qop_t *qOpPtr,
					       unsigned int *maxTransPtr,
					       unsigned int *configPtr,
					       OM_uint32 *majStatPtr,
					       OM_uint32 *minStatPtr);
static void secSessionInitiateCleanup(void *closure);
static void secSessionInitiateReceiveAuthorizationAck(int events,
						      int fd,
						      void *closure,
						      const struct timeval *t);
static void secSessionInitiateWaitAuthorizationAck(void *closure);
static struct gfarmSecSessionInitiateState *
	secSessionInitiateRequest(struct gfarm_eventqueue *q,
				  int fd,
				  const gss_name_t acceptorName,
				  gss_cred_id_t cred,
				  OM_uint32 reqFlag,
				  gfarmSecSessionOption *ssOptPtr,
				  void (*continuation)(void *),
				  void *closure,
				  OM_uint32 *majStatPtr,
				  OM_uint32 *minStatPtr,
				  int needClose);
static gfarmSecSession *secSessionInitiateResult(struct gfarmSecSessionInitiateState *state,
						 OM_uint32 *majStatPtr,
						 OM_uint32 *minStatPtr);


#ifdef SS_DEBUG
static void	dumpConfParam(gss_qop_t qOp, unsigned int max, unsigned int conf);
static void	dumpSsOpt(gfarmSecSessionOption *ssOptPtr);


static void
dumpConfParam(gss_qop_t qOp, unsigned int max, unsigned int conf)
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
dumpSsOpt(gfarmSecSessionOption *ssOptPtr)
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

/* thread safe */
static int
secSessionReadConfigFile(char *configFile, gfarmSecSessionOption *ssOptPtr)
{
    FILE *fd = NULL;
    char lineBuf[65536];
    char *token[64];
    int nToken;
    gfarmSecSessionOption ssTmp = GFARM_SS_DEFAULT_OPTION;
    int i;
    static const char diag[] = "secSessionReadConfigFile";

    if (configFile == NULL || configFile[0] == '\0') {
	gflog_debug(GFARM_MSG_1004488, "%s: no configuration file", diag);
	goto Done;
    }
    gfarm_privilege_lock(diag);
    fd = fopen(configFile, "r");
    gfarm_privilege_unlock(diag);
    if (fd == NULL) {
	/*
	 * use default option.
	 */
	gflog_debug(GFARM_MSG_1000815, "fopen(%s) failed", configFile);
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
canonicSecSessionOpt(int which, gfarmSecSessionOption *reqPtr,
    gfarmSecSessionOption *canPtr)
{
    gfarmSecSessionOption *dPtr = (which == GFARM_SS_INITIATOR) ?
    	&initiatorSsOpt : &acceptorSsOpt;

    if (canPtr == NULL) {
	gflog_auth_error(GFARM_MSG_1000660,
	    "gfarmSecSession:canonicSecSessionOpt(): "
			 "invalid argument");
	errno = EINVAL;
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
negotiateConfigParam(int fd, gss_ctx_id_t sCtx, int which,
    gfarmSecSessionOption *canPtr, gss_qop_t *qOpPtr,
    unsigned int *maxTransPtr, unsigned int *configPtr,
    int *gsiErrNoPtr, OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    int ret = -1;
    int gsiErrNo = 0;
    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;

    gss_qop_t retQOP = GFARM_GSS_DEFAULT_QOP;
    unsigned int retMaxT = GFARM_GSS_DEFAULT_MAX_MESSAGE_REQUEST_SIZE;
    unsigned int retConf = GFARM_SS_USE_ENCRYPTION;

#define NEGO_PARAM_QOP			0
#define NEGO_PARAM_QOP_FORCE		1
#define NEGO_PARAM_MAX_TRANS_SIZE	2
#define NEGO_PARAM_MAX_TRANS_SIZE_FORCE	3
#define NEGO_PARAM_OTHER_CONFIG		4
#define NEGO_PARAM_OTHER_CONFIG_FORCE	5
#define NUM_NEGO_PARAM			6
  
    if (sCtx == GSS_C_NO_CONTEXT) {
	gflog_auth_error(GFARM_MSG_1000661,
	    "gfarmSecSession:negotiateConfigParam(): "
			 "no context");
	gsiErrNo = EINVAL;
	goto Done;
    }

    switch (which) {
	gfarm_int32_t param[NUM_NEGO_PARAM];

	case GFARM_SS_ACCEPTOR: {
	    int iQOP, iQOPF;
	    unsigned int iMax;
	    int iMaxF;
	    int iConf, iConfF;
	   
	    if (gfarmReadInt32(fd, param, NUM_NEGO_PARAM,
		GFARM_GSS_AUTH_TIMEOUT_MSEC) != NUM_NEGO_PARAM) {
		gsiErrNo = errno;
		gflog_auth_info(GFARM_MSG_1003849,
		    "gfarmSecSession:negotiateConfigParam(): "
		    "negotiation failure with the initiator: %s",
		    strerror(errno));
		goto Done;
	    }
	    iQOP = param[NEGO_PARAM_QOP];
	    iQOPF = param[NEGO_PARAM_QOP_FORCE];
	    iMax = param[NEGO_PARAM_MAX_TRANS_SIZE];
	    iMaxF = param[NEGO_PARAM_MAX_TRANS_SIZE_FORCE];
	    iConf = param[NEGO_PARAM_OTHER_CONFIG];
	    iConfF = param[NEGO_PARAM_OTHER_CONFIG_FORCE];

	    /* 
	     * Give precedence to the acceptor on QOP.
	     */
	    if (canPtr->qOpForce == 0 && iQOPF == 1) {
		/*
		 * Use the initiator's.
		 */
		retQOP = (gss_qop_t)iQOP;
	    } else {
		/*
		 * Use the acceptor's.
		 */
		retQOP = canPtr->qOpReq;
	    }
	    param[NEGO_PARAM_QOP] = retQOP;

	    /* 
	     * maximum transmission size
	     */
	    if (canPtr->maxTransSizeForce == 1) {
		/*
		 * Use the acceptor's.
		 */
		retMaxT = canPtr->maxTransSizeReq;
	    } else if (iMaxF == 1) {
		/*
		 * Use the initiator's.
		 */
		retMaxT = iMax;
	    } else { 
		/*
		 * Both force flags are off.
		 * Use larger one.
		 */
		retMaxT = (canPtr->maxTransSizeReq >= iMax) ?
				canPtr->maxTransSizeReq : iMax;
	    }
	    param[NEGO_PARAM_MAX_TRANS_SIZE] = retMaxT;

	    /* 
	     * other configuration flags
	     */
	    /* compression, systemconf -  Give precedence to the acceptor. */
	    if (canPtr->configForce == 0 && iConfF == 1) {
		/*
		 * Use the initiator's.
		 */
		retConf = canPtr->configReq;
	    } else {
		/*
		 * Use the acceptor's.
		 */
		retConf = iConf;
	    }
	    /* encryption - Take logical or value ignoring both force flags. */
	    retConf &= ~GFARM_SS_USE_ENCRYPTION;
	    retConf |= (canPtr->configReq | iConf) & GFARM_SS_USE_ENCRYPTION;

	    param[NEGO_PARAM_OTHER_CONFIG] = retConf;

	    if (gfarmWriteInt32(fd, param, NUM_NEGO_PARAM) != NUM_NEGO_PARAM) {
		gsiErrNo = errno;
		gflog_auth_info(GFARM_MSG_1003850,
		    "gfarmSecSession:negotiateConfigParam(): "
		    "initiator disappered: %s", strerror(errno));
		goto Done;
	    }

	    /* End of acceptor side negotiation. */
	    break;
	}

	case GFARM_SS_INITIATOR: {
	    param[NEGO_PARAM_QOP] = canPtr->qOpReq;
	    param[NEGO_PARAM_QOP_FORCE] = canPtr->qOpForce;
	    param[NEGO_PARAM_MAX_TRANS_SIZE] = canPtr->maxTransSizeReq;
	    param[NEGO_PARAM_MAX_TRANS_SIZE_FORCE] = canPtr->maxTransSizeForce;
	    param[NEGO_PARAM_OTHER_CONFIG] = canPtr->configReq;
	    param[NEGO_PARAM_OTHER_CONFIG_FORCE] = canPtr->configForce;

	    if (gfarmWriteInt32(fd, param, NUM_NEGO_PARAM) != NUM_NEGO_PARAM) {
		gsiErrNo = errno;
		gflog_auth_error(GFARM_MSG_1003851,
		    "gfarmSecSession:negotiateConfigParam(): "
		    "acceptor disappered: %s", strerror(errno));
		goto Done;
	    }

	    if (gfarmReadInt32(fd, param, NUM_NEGO_PARAM,
		GFARM_GSS_AUTH_TIMEOUT_MSEC) != NUM_NEGO_PARAM) {
		gsiErrNo = errno;
		gflog_auth_error(GFARM_MSG_1000665,
		    "gfarmSecSession:negotiateConfigParam(): "
		    "negotiation failure with the acceptor: %s",
		    strerror(errno));
		goto Done;
	    }

	    retQOP = param[NEGO_PARAM_QOP];
	    retMaxT = param[NEGO_PARAM_MAX_TRANS_SIZE];
	    retConf = param[NEGO_PARAM_OTHER_CONFIG];

	    /* End of initiator side negotiation. */
	    break;
	}
    }

    {
	unsigned int maxMsgSize;
	int doEncrypt = GFARM_GSS_ENCRYPTION_ENABLED &
    			(isBitSet(retConf,
				  GFARM_SS_USE_ENCRYPTION) ? 1 : 0);

	if (gfarmGssConfigureMessageSize(sCtx, doEncrypt,
					 retQOP, retMaxT, &maxMsgSize,
					 &majStat, &minStat) >= 0) {
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
    }
    Done:
    if (gsiErrNoPtr != NULL) {
	*gsiErrNoPtr = gsiErrNo;
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
allocSecSession(int which)
{
    gfarmSecSession *ret;

    GFARM_MALLOC(ret);
    if (ret == NULL) {
	gflog_debug(GFARM_MSG_1000816,
		"allocation of gfarmSecSession failed");
	errno = ENOMEM;
	return NULL;
    }
    (void)memset((void *)ret, 0, sizeof(gfarmSecSession));
    switch (which) {
	case GFARM_SS_INITIATOR: {
	    ret->iOaInfo.initiator.acceptorName = GSS_C_NO_NAME;
	    break;
	}
	case GFARM_SS_ACCEPTOR: {
	    ret->iOaInfo.acceptor.initiatorName = GSS_C_NO_NAME;
	    ret->iOaInfo.acceptor.deleCred = GSS_C_NO_CREDENTIAL;
	    break;
	}
	default: {
	    (void)free(ret);
	    errno = EINVAL;
	    ret = NULL;
	    gflog_debug(GFARM_MSG_1000817, "Invalid session type (%d)", which);
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
destroySecSession(gfarmSecSession *ssPtr)
{
    if (ssPtr != NULL) {
	OM_uint32 minStat;

	free(ssPtr->peerName);
	switch (ssPtr->iOa) {
	    case GFARM_SS_INITIATOR:
		if (ssPtr->iOaInfo.initiator.acceptorName != GSS_C_NO_NAME)
		    gfarmGssDeleteName(&ssPtr->iOaInfo.initiator.acceptorName,
				       NULL, NULL);
		break;
	    case GFARM_SS_ACCEPTOR:
		if (ssPtr->iOaInfo.acceptor.initiatorName != GSS_C_NO_NAME)
		    gfarmGssDeleteName(&ssPtr->iOaInfo.acceptor.initiatorName,
				       NULL, NULL);
		if (ssPtr->iOaInfo.acceptor.deleCred != GSS_C_NO_CREDENTIAL)
		    (void)gss_release_cred(&minStat,
				&(ssPtr->iOaInfo.acceptor.deleCred));
		if (ssPtr->iOaInfo.acceptor.mappedUser != NULL) {
		    /*
		     * ssPtr->iOaInfo.acceptor.mappedUser may be NULL,
		     * if gfarmSecSessionAccept() aborts during initialization.
		     */
		    gfarmAuthFreeUserEntry(ssPtr->iOaInfo.acceptor.mappedUser);
		}
		break;
	    default:
		break;
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
gfarmSecSessionCrackStatus(gfarmSecSession *ssPtr)
{
    return gfarmGssCrackMajorStatus(ssPtr->gssLastStat);
}


void
gfarmSecSessionFreeCrackedStatus(char **strPtr)
{
    gfarmGssFreeCrackedStatus(strPtr);
}


void
gfarmSecSessionPrintStatus(gfarmSecSession *ssPtr)
{
    gfarmGssPrintMajorStatus(ssPtr->gssLastStat);
}


int
gfarmSecSessionInitializeAcceptor(char *configFile, char *usermapFile,
    OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    int ret = 1;
    OM_uint32 majStat = GSS_S_COMPLETE;
    OM_uint32 minStat = GSS_S_COMPLETE;
    static const char diag[] = "gfarmSecSessionInitializeAcceptor()";

    gfarm_mutex_lock(&acceptor_mutex, diag, acceptorDiag);
    if (acceptorInitialized == 0) {
	char *confFile = NULL;
	/*
	 * Read config file.
	 */
	if (configFile == NULL || configFile[0] == '\0') {
	    confFile = gfarmGetDefaultConfigFile(
		GFARM_DEFAULT_ACCEPTOR_CONFIG_FILE);
	    configFile = confFile;
	}
	if (secSessionReadConfigFile(configFile, &acceptorSsOpt) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    gflog_debug(GFARM_MSG_1000818, "failed to read config file (%s)",
		configFile);
	    goto Done;
	}

	/*
	 * Authorization init.
	 */
	if (gfarmAuthInitialize(usermapFile) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    gflog_debug(GFARM_MSG_1000819, "gfarmAuthInitialize(%s) failed",
		usermapFile);
	    goto Done;
	}

	Done:
	free(confFile);
	if (ret == 1) {
	    acceptorInitialized = 1;
	}
    }
    gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
}

int
gfarmSecSessionInitializeInitiator(char *configFile, char *usermapFile,
    OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    int ret = 1;
    OM_uint32 majStat = GSS_S_COMPLETE;
    OM_uint32 minStat = GSS_S_COMPLETE;
    static const char diag[] = "gfarmSecSessionInitializeInitiator()";

    gfarm_mutex_lock(&initiator_mutex, diag, initiatorDiag);
    if (initiatorInitialized == 0) {
	char *confFile = NULL;
	/*
	 * Read config file.
	 */
	if (configFile == NULL || configFile[0] == '\0') {
	    confFile = gfarmGetDefaultConfigFile(
		GFARM_DEFAULT_INITIATOR_CONFIG_FILE);
	    configFile = confFile;
	}
	if (secSessionReadConfigFile(configFile, &initiatorSsOpt) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    gflog_debug(GFARM_MSG_1000820, "failed to read config file (%s)",
		configFile);
	    goto Done;
	}

#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	/*
	 * If GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS is true,
	 * this information is need to initiate a conneciton to
	 * an acceptor which name is GSS_C_NT_USER_NAME.
	 */

	/*
	 * Authorization init.
	 * It isn't fatal for an initiator, if this fails.
	 */
	(void)gfarmAuthInitialize(usermapFile);
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */

	Done:
	free(confFile);
	if (ret == 1) {
	    initiatorInitialized = 1;
	}
    }
    gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return (ret);
}


int
gfarmSecSessionInitializeBoth(char *iConfigFile, char *aConfigFile,
    char *usermapFile, OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    int ret = 1;
    OM_uint32 majStat = GSS_S_COMPLETE;
    OM_uint32 minStat = GSS_S_COMPLETE;
    static const char diag[] = "gfarmSecSessionInitializeBoth()";

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

    gfarm_mutex_lock(&initiator_mutex, diag, initiatorDiag);
    gfarm_mutex_lock(&acceptor_mutex, diag, acceptorDiag);
    if (initiatorInitialized == 0 && acceptorInitialized == 0) {
	char *confFile = NULL;

	/*
	 * Acceptor's configuration
	 */
	if (aConfigFile == NULL || aConfigFile[0] == '\0') {
	    confFile = gfarmGetDefaultConfigFile(
		GFARM_DEFAULT_ACCEPTOR_CONFIG_FILE);
	    aConfigFile = confFile;
	}
	if (secSessionReadConfigFile(aConfigFile, &acceptorSsOpt) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    gflog_debug(GFARM_MSG_1000821,
		"failed to read acceptor's config file (%s)", aConfigFile);
	    goto Done;
	}
	free(confFile);
	/*
	 * Initiator's configuration
	 */
	if (iConfigFile == NULL || iConfigFile[0] == '\0') {
	    confFile = gfarmGetDefaultConfigFile(
		GFARM_DEFAULT_INITIATOR_CONFIG_FILE);
	    iConfigFile = confFile;
	}
	if (secSessionReadConfigFile(iConfigFile, &initiatorSsOpt) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    gflog_debug(GFARM_MSG_1000822,
		"failed to read initiator's config file (%s)", iConfigFile);
	    goto Done;
	}

	/*
	 * Authorization init.
	 */
	if (gfarmAuthInitialize(usermapFile) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    gflog_debug(GFARM_MSG_1000823, "gfarmAuthInitialize(%s) failed",
		usermapFile);
	    goto Done;
	}

	Done:
	free(confFile);
	if (ret == 1) {
	    initiatorInitialized = 1;
	    acceptorInitialized = 1;
	}
    }
    gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);
    gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
}


void
gfarmSecSessionFinalizeInitiator(void)
{
    static const char diag[] = "gfarmSecSessionFinalizeInitiator()";

    gfarm_mutex_lock(&initiator_mutex, diag, initiatorDiag);
    if (initiatorInitialized == 1) {
	initiatorInitialized = 0;
    }
    gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);
}


void
gfarmSecSessionFinalizeAcceptor(void)
{
    static const char diag[] = "gfarmSecSessionFinalizeAcceptor()";

    gfarm_mutex_lock(&acceptor_mutex, diag, acceptorDiag);
    if (acceptorInitialized == 1) {
	gfarmAuthFinalize();
	acceptorInitialized = 0;
    }
    gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);
}


void
gfarmSecSessionFinalizeBoth(void)
{
    static const char diag[] = "gfarmSecSessionFinalizeBoth()";

    gfarm_mutex_lock(&initiator_mutex, diag, initiatorDiag);
    gfarm_mutex_lock(&acceptor_mutex, diag, acceptorDiag);
    if (initiatorInitialized == 1 && acceptorInitialized == 1) {
	gfarmAuthFinalize();
	acceptorInitialized = 0;
	initiatorInitialized = 0;
    }
    gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);
    gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);
}

gfarmSecSession *
gfarmSecSessionAccept(int fd, gss_cred_id_t cred,
    gfarmSecSessionOption *ssOptPtr, int *gsiErrNoPtr, OM_uint32 *majStatPtr,
    OM_uint32 *minStatPtr)
{
    gfarmSecSession *ret = NULL;
    gfarmSecSessionOption canOpt = GFARM_SS_DEFAULT_OPTION;
    gfarmAuthEntry *entry = NULL;

    int rPort = 0;
    char *peerName = NULL;

    int gsiErrNo = 0;
    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;
    gss_ctx_id_t sCtx = GSS_C_NO_CONTEXT;
    gss_name_t initiatorName = GSS_C_NO_NAME;
    gss_OID initiatorNameType;
    char *initiatorDistName = NULL;
    gss_cred_id_t deleCred = GSS_C_NO_CREDENTIAL;
    gfarm_int32_t acknack = GFARM_SS_AUTH_NACK;

    gss_qop_t qOp;
    unsigned int maxTransSize;
    unsigned int config;

    static const char diag[] = "gfarmSecSessionAccept()";

    gfarm_mutex_lock(&acceptor_mutex, diag, acceptorDiag);
    if (acceptorInitialized == 0) {
	gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);
	gflog_auth_error(GFARM_MSG_1000669,
	    "gfarmSecSessionAccept(): not initialized");
	gsiErrNo = EINVAL;
	goto Fail;
    }

    ret = allocSecSession(GFARM_SS_ACCEPTOR);
    if (ret == NULL) {
	gsiErrNo = errno;
	gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);
	gflog_auth_error(GFARM_MSG_1003852,
	    "gfarmSecSessionAccept(): %s", strerror(gsiErrNo));
	goto Fail;
    }

    if (canonicSecSessionOpt(GFARM_SS_ACCEPTOR, ssOptPtr, &canOpt) < 0) {
	gsiErrNo = errno;
	gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);
	gflog_debug(GFARM_MSG_1003853,
		"canonicSecSessionOpt() failed: %s", strerror(gsiErrNo));
	goto Fail;
    }

    /*
     * Get a peer information.
     */
    if (gfarmGetPeernameOfSocket(fd, &rPort, &peerName)) {
	gsiErrNo = errno;
	gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);
	gflog_debug(GFARM_MSG_1003854,
		"gfarmGetPeernameOfSocket() failed: %s", strerror(gsiErrNo));
	goto Fail;
    }
    gfarm_mutex_unlock(&acceptor_mutex, diag, acceptorDiag);

    /*
     * Phase 1: Accept a security context.
     */
    if (gfarmGssAcceptSecurityContext(fd, cred, &sCtx,
				      &gsiErrNo, &majStat, &minStat,
				      &initiatorName, &deleCred) < 0) {
	gflog_debug(GFARM_MSG_1000826,
		"gfarmGssAcceptSecurityContext failed(%u)(%u)",
		majStat, minStat);
	goto Fail;
    }
    if (initiatorName == GSS_C_NO_NAME ||
	(initiatorDistName =
	 gfarmGssNewDisplayName(initiatorName, &majStat, &minStat,
				&initiatorNameType)) == NULL ||
	initiatorDistName[0] == '\0' ||
	initiatorNameType == GSS_C_NT_ANONYMOUS) {
	gflog_auth_error(GFARM_MSG_1000671,
	    "gfarmSecSessionAccept(): no DN from initiator");
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 2: Authorization and send ACK/NACK
     */
    entry = gfarmAuthGetUserEntry(initiatorDistName);
    if (entry == NULL) {
	gflog_auth_notice(GFARM_MSG_1000672,
	    "%s: not registered in mapfile", initiatorDistName);
	majStat = GSS_S_UNAUTHORIZED;
	goto SendNack;
    } else {
	int type = gfarmAuthGetAuthEntryType(entry);

	if (type == GFARM_AUTH_USER) {
	    /* Send ACK. */
	    acknack = GFARM_SS_AUTH_ACK;
	    (void)gfarmWriteInt32(fd, &acknack, 1);
	} else if (type == GFARM_AUTH_HOST) {
	    /* check peer name is actually allowed */
	    if (strcmp(peerName, gfarmAuthGetFQDN(entry)) == 0) {
		/* Send ACK. */
		acknack = GFARM_SS_AUTH_ACK;
		(void)gfarmWriteInt32(fd, &acknack, 1);
	    } else {
		gflog_auth_error(GFARM_MSG_1000673,
		    "%s: hostname doesn't match: "
		    "peer %s, expected %s", initiatorDistName, peerName,
		    gfarmAuthGetFQDN(entry));
		majStat = GSS_S_UNAUTHORIZED;
		gfarmAuthFreeUserEntry(entry);
		goto SendNack;
	    }
	}
    }

    /*
     * Phase 3: Negotiate configuration parameters
     * with the initiator.
     */
    if (negotiateConfigParam(fd, sCtx, GFARM_SS_ACCEPTOR, &canOpt,
			     &qOp, &maxTransSize, &config,
			     &gsiErrNo, &majStat, &minStat) < 0) {
	gflog_debug(GFARM_MSG_1003855,
		"negotiateConfigParam() failed (%s)(%u)(%u)",
		strerror(gsiErrNo), majStat, minStat);
	gfarmAuthFreeUserEntry(entry);
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
    ret->rPort = rPort;
    ret->peerName = peerName;
    ret->cred = cred;
    ret->sCtx = sCtx;
    ret->iOa = GFARM_SS_ACCEPTOR;
    ret->iOaInfo.acceptor.mappedUser = entry;
    ret->iOaInfo.acceptor.initiatorName = initiatorName;
    ret->iOaInfo.acceptor.deleCred = deleCred;
    ret->qOp = qOp;
    ret->maxTransSize = maxTransSize;
    ret->config = config;
    ret->gssLastStat = majStat;
    goto Done;

SendNack:
    /* Send NACK. */
    acknack = GFARM_SS_AUTH_NACK;
    gfarmWriteInt32(fd, &acknack, 1);
Fail:
    if (ret != NULL) {
	destroySecSession(ret);
	ret = NULL;
    }
    if (peerName != NULL) {
	free(peerName);
    }
    if (sCtx != GSS_C_NO_CONTEXT) {
	gfarmGssDeleteSecurityContext(&sCtx);
    }
    if (initiatorName != GSS_C_NO_NAME) {
	gfarmGssDeleteName(&initiatorName, NULL, NULL);
    }
    if (deleCred != GSS_C_NO_CREDENTIAL) {
	gfarmGssDeleteCredential(&deleCred, NULL, NULL);
    }

    Done:
    if (initiatorDistName != NULL) {
	(void)free(initiatorDistName);
    }
    if (gsiErrNoPtr != NULL) {
	*gsiErrNoPtr = gsiErrNo;
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
secSessionInitiate(int fd, const gss_name_t acceptorName,
    gss_cred_id_t cred, OM_uint32 reqFlag, gfarmSecSessionOption *ssOptPtr,
    int *gsiErrNoPtr, OM_uint32 *majStatPtr, OM_uint32 *minStatPtr,
    int needClose)
{
    gfarmSecSession *ret = NULL;
    gfarmSecSessionOption canOpt = GFARM_SS_DEFAULT_OPTION;

    int rPort = 0;
    char *peerName = NULL;

    int gsiErrNo = 0;
    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;
    gss_ctx_id_t sCtx = GSS_C_NO_CONTEXT;
    gss_name_t acceptorNameResult = GSS_C_NO_NAME;
    char *acceptorDistName = NULL;
    gss_OID acceptorNameType = GSS_C_NO_OID;
    gfarm_int32_t acknack = GFARM_SS_AUTH_NACK;

    gss_qop_t qOp;
    unsigned int maxTransSize;
    unsigned int config;

    static const char diag[] = "gfsl/secSessionInitiate()";

    gfarm_mutex_lock(&initiator_mutex, diag, initiatorDiag);
    if (initiatorInitialized == 0) {
	gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);
	gflog_auth_error(GFARM_MSG_1000674,
	    "gfarm:secSessionInitiate(): not initialized");
	gsiErrNo = EINVAL;
	goto Fail;
    }

    ret = allocSecSession(GFARM_SS_INITIATOR);
    if (ret == NULL) {
	gsiErrNo = errno;
	gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);
	gflog_auth_error(GFARM_MSG_1003856,
	    "gfarm:secSessionInitiate(): %s", strerror(gsiErrNo));
	goto Fail;
    }

    if (canonicSecSessionOpt(GFARM_SS_INITIATOR, ssOptPtr, &canOpt) < 0) {
	gsiErrNo = errno;
	gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);
	gflog_debug(GFARM_MSG_1003857,
		"canonicSecSessionOpt() failed: %s", strerror(gsiErrNo));
	goto Fail;
    }

    /*
     * Get a peer information.
     */
    if (gfarmGetPeernameOfSocket(fd, &rPort, &peerName)) {
	gsiErrNo = errno;
	gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);
	gflog_debug(GFARM_MSG_1003858,
		"gfarmGetPeernameOfSocket() failed: %s", strerror(gsiErrNo));
	goto Fail;
    }
    gfarm_mutex_unlock(&initiator_mutex, diag, initiatorDiag);

    /*
     * Phase 1: Initiate a security context.
     */
    if (gfarmGssInitiateSecurityContext(fd, acceptorName, cred, reqFlag, &sCtx,
					&gsiErrNo, &majStat, &minStat,
					&acceptorNameResult) < 0) {
	gflog_debug(GFARM_MSG_1003859,
		"gfarmGssInitiateSecurityContext() failed (%s)(%u)(%u)",
		strerror(gsiErrNo), majStat, minStat);
	goto Fail;
    }
    if (acceptorNameResult == GSS_C_NO_NAME ||
	(acceptorDistName =
	 gfarmGssNewDisplayName(acceptorNameResult, &majStat, &minStat,
				&acceptorNameType)) == NULL ||
	acceptorDistName[0] == '\0' ||
	acceptorNameType == GSS_C_NT_ANONYMOUS) {
	gflog_auth_error(GFARM_MSG_1000676,
	    "gfarm:secSessionInitiate(): no DN from acceptor");
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 2: Receive authorization acknowledgement.
     */
    if (gfarmReadInt32(fd, &acknack, 1, GFARM_GSS_AUTH_TIMEOUT_MSEC) != 1) {
	gsiErrNo = errno;
	gflog_auth_error(GFARM_MSG_1003860,
	    "%s: acceptor does not answer authentication result: %s",
	    acceptorDistName, strerror(gsiErrNo));
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }
    if (acknack == GFARM_SS_AUTH_NACK) {
	gflog_auth_notice(GFARM_MSG_1000678,
	    "%s: session refused by acceptor", acceptorDistName);
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 3: Negotiate configuration parameters
     * with the acceptor.
     */
    if (negotiateConfigParam(fd, sCtx, GFARM_SS_INITIATOR, &canOpt,
			     &qOp, &maxTransSize, &config,
			     &gsiErrNo, &majStat, &minStat) < 0) {
	gflog_debug(GFARM_MSG_1003861,
		"negotiateConfigParam() failed (%s)(%u)(%u)",
		strerror(gsiErrNo), majStat, minStat);
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
    ret->rPort = rPort;
    ret->peerName = peerName;
    ret->cred = cred;
    ret->sCtx = sCtx;
    ret->iOa = GFARM_SS_INITIATOR;
    ret->iOaInfo.initiator.reqFlag = reqFlag;
    ret->iOaInfo.initiator.acceptorName = acceptorNameResult;
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
    if (peerName != NULL) {
	free(peerName);
    }
    if (sCtx != GSS_C_NO_CONTEXT) {
	gfarmGssDeleteSecurityContext(&sCtx);
    }
    if (acceptorNameResult != GSS_C_NO_NAME) {
	gfarmGssDeleteName(&acceptorNameResult, NULL, NULL);
    }

    Done:
    if (acceptorDistName != NULL) {
	(void)free(acceptorDistName);
    }
    if (gsiErrNoPtr != NULL) {
	*gsiErrNoPtr = gsiErrNo;
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
gfarmSecSessionInitiate(int fd, const gss_name_t acceptorName,
    gss_cred_id_t cred, OM_uint32 reqFlag, gfarmSecSessionOption *ssOptPtr,
    int *gsiErrNoPtr, OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    return secSessionInitiate(fd, acceptorName, cred, reqFlag, ssOptPtr,
			      gsiErrNoPtr, majStatPtr, minStatPtr, 0);
}


gfarmSecSession *
gfarmSecSessionInitiateByName(char *hostname, int port,
    const gss_name_t acceptorName, gss_cred_id_t cred, OM_uint32 reqFlag,
    gfarmSecSessionOption *ssOptPtr, int *gsiErrNoPtr, OM_uint32 *majStatPtr,
    OM_uint32 *minStatPtr)
{
    int fd = gfarmTCPConnectPortByHost(hostname, port);

    if (fd < 0) {
	if (majStatPtr != NULL) {
	    *majStatPtr = GSS_S_FAILURE;
	}
	if (minStatPtr != NULL) {
	    *minStatPtr = GFSL_DEFAULT_MINOR_ERROR;
	}
	gflog_debug(GFARM_MSG_1000832,
		"gfarmTCPConnectPortByHost(%s:%d) failed",
		hostname, port);
	return NULL;
    }
    return secSessionInitiate(fd, acceptorName, cred, reqFlag,
			      ssOptPtr, gsiErrNoPtr, majStatPtr, minStatPtr, 1);
}


void
gfarmSecSessionTerminate(gfarmSecSession *ssPtr)
{
    destroySecSession(ssPtr);
}


gss_cred_id_t
gfarmSecSessionGetDelegatedCredential(gfarmSecSession *ssPtr)
{
    if (ssPtr->iOa == GFARM_SS_INITIATOR) {
	return GSS_C_NO_CREDENTIAL;
    }
    return ssPtr->iOaInfo.acceptor.deleCred;
}

int
gfarmSecSessionGetInitiatorName(gfarmSecSession *ssPtr, gss_name_t *namePtr)
{
    if (ssPtr->iOa == GFARM_SS_INITIATOR) {
	return -1;
    }
    *namePtr = ssPtr->iOaInfo.acceptor.initiatorName;
    return 1;
}

gfarmAuthEntry *
gfarmSecSessionGetInitiatorInfo(gfarmSecSession *ssPtr)
{
    if (ssPtr->iOa == GFARM_SS_INITIATOR) {
	return NULL;
    }
    return ssPtr->iOaInfo.acceptor.mappedUser;
}

int
gfarmSecSessionDedicate(gfarmSecSession *ssPtr)
{
    int ret = -1;
    gfarmAuthEntry *aePtr = gfarmSecSessionGetInitiatorInfo(ssPtr);

    if (aePtr != NULL) {
	gid_t gid = getgid();
	uid_t uid = getuid();

#if ! GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	/*
	 * If GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS is true,
	 * this information is need to initiate a conneciton to
	 * an acceptor which name is GSS_C_NT_USER_NAME.
	 */
	gfarmAuthMakeThisAlone(aePtr);
#endif /* ! GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
	if (uid == 0 && gfarmAuthGetAuthEntryType(aePtr) == GFARM_AUTH_USER) {
	    if (gfarmAuthGetGid(aePtr) != gid) {
		if ((ret = setgid(gfarmAuthGetGid(aePtr))) < 0) {
		    ssPtr->gssLastStat = GSS_S_FAILURE;
		    gflog_debug(GFARM_MSG_1000833, "setgid() failed");
		    goto Done;
		}
	    }
	    if (gfarmAuthGetUid(aePtr) != uid) {
		if ((ret = setuid(gfarmAuthGetUid(aePtr))) < 0) {
		    ssPtr->gssLastStat = GSS_S_FAILURE;
		    gflog_debug(GFARM_MSG_1000834, "setuid() failed");
		    goto Done;
		}
	    }
	}
	ret = 1;
    } else {
	ssPtr->gssLastStat = GSS_S_FAILURE;
	gflog_debug(GFARM_MSG_1000835, "gfarmAuthEntry is NULL");
    }

    Done:
    return ret;
}

int
gfarmSecSessionSendInt8(gfarmSecSession *ssPtr, gfarm_int8_t *buf, int n)
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
gfarmSecSessionReceiveInt8(gfarmSecSession *ssPtr, gfarm_int8_t **bufPtr,
    int *lenPtr, int timeoutMsec)
{
    return gfarmGssReceive(ssPtr->fd,
			   ssPtr->sCtx,
			   bufPtr,
			   lenPtr,
			   &(ssPtr->gssLastStat),
			   timeoutMsec);
}


int
gfarmSecSessionSendInt32(gfarmSecSession *ssPtr, gfarm_int32_t *buf, int n)
{
    gfarm_int32_t *lBuf;
    int i;
    int ret = -1;

    GFARM_MALLOC_ARRAY(lBuf, n);
    if (lBuf == NULL) {
	ssPtr->gssLastStat = GSS_S_FAILURE;
	gflog_debug(GFARM_MSG_1000836, "allocation of buffer failed");
	return ret;
    }
    for (i = 0; i < n; i++) {
	lBuf[i] = htonl(buf[i]);
    }
    
    ret = gfarmSecSessionSendInt8(ssPtr, (gfarm_int8_t *)lBuf,
				  n * GFARM_OCTETS_PER_32BIT);
    (void)free(lBuf);
    if (ret > 0) {
	ret /= GFARM_OCTETS_PER_32BIT;
    }
    return ret;
}


int
gfarmSecSessionReceiveInt32(gfarmSecSession *ssPtr, gfarm_int32_t **bufPtr,
    int *lenPtr, int timeoutMsec)
{
    gfarm_int8_t *lBuf = NULL;
    gfarm_int8_t *lbPtr = NULL;
    gfarm_int32_t *retBuf = NULL;
    gfarm_int32_t tmp;
    int len = 0;
    int i;
    int n;
    int ret = gfarmSecSessionReceiveInt8(ssPtr, &lBuf, &len, timeoutMsec);
    
    if (ret <= 0) {
	goto Done;
    }
    n = len % GFARM_OCTETS_PER_32BIT;
    if (n != 0) {
	goto Done;
    }
    n = len / GFARM_OCTETS_PER_32BIT;

    GFARM_MALLOC_ARRAY(retBuf, n);
    if (retBuf == NULL) {
	gflog_debug(GFARM_MSG_1000837, "allocation of 'retBuf' failed");
	goto Done;
    }

    lbPtr = lBuf;
    for (i = 0; i < n; i++) {
	memcpy((void *)&tmp, (void *)lbPtr, GFARM_OCTETS_PER_32BIT);
	retBuf[i] = ntohl(tmp);
	lbPtr += GFARM_OCTETS_PER_32BIT;
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
gfarmSecSessionSendInt16(gfarmSecSession *ssPtr, gfarm_int16_t *buf, int n)
{
    gfarm_int16_t *lBuf;
    int i;
    int ret = -1;

    GFARM_MALLOC_ARRAY(lBuf, n);
    if (lBuf == NULL) {
	ssPtr->gssLastStat = GSS_S_FAILURE;
	gflog_debug(GFARM_MSG_1000838, "allocation of buffer failed");
	return ret;
    }
    for (i = 0; i < n; i++) {
	lBuf[i] = htons(buf[i]);
    }
    
    ret = gfarmSecSessionSendInt8(ssPtr, (gfarm_int8_t *)lBuf,
				   n * GFARM_OCTETS_PER_16BIT);
    (void)free(lBuf);
    if (ret > 0) {
	ret /= GFARM_OCTETS_PER_16BIT;
    }
    return ret;
}


int
gfarmSecSessionReceiveInt16(gfarmSecSession *ssPtr, gfarm_int16_t **bufPtr,
    int *lenPtr, int timeoutMsec)
{
    char *lBuf = NULL;
    char *lbPtr = NULL;
    gfarm_int16_t *retBuf = NULL;
    gfarm_int16_t tmp;
    int len = 0;
    int i;
    int n;
    int ret = gfarmSecSessionReceiveInt8(ssPtr, &lBuf, &len, timeoutMsec);
    
    if (ret <= 0) {
	goto Done;
    }
    n = len % GFARM_OCTETS_PER_16BIT;
    if (n != 0) {
	goto Done;
    }
    n = len / GFARM_OCTETS_PER_16BIT;

    GFARM_MALLOC_ARRAY(retBuf, n);
    if (retBuf == NULL) {
	gflog_debug(GFARM_MSG_1000839, "allocation of 'retBuf' failed");
	goto Done;
    }

    lbPtr = lBuf;
    for (i = 0; i < n; i++) {
	memcpy((void *)&tmp, (void *)lbPtr, GFARM_OCTETS_PER_16BIT);
	retBuf[i] = ntohs(tmp);
	lbPtr += GFARM_OCTETS_PER_16BIT;
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

/*
 * This function is only used by a test pgrogram.
 */

int
gfarmSecSessionPoll(gfarmSecSession **ssList, int n, struct timeval *toPtr)
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

/*
 * multiplexed version of negotiateConfigParam(,, GFARM_SS_INITIATOR, ...)
 */

struct negotiateConfigParamInitiatorState {
    struct gfarm_eventqueue *q;
    struct gfarm_event *writable, *readable;
    int fd;
    gss_ctx_id_t sCtx;
    gfarmSecSessionOption *canPtr;
    void (*continuation)(void *);
    void *closure;

    gss_qop_t retQOP;
    unsigned int retMaxT;
    unsigned int retConf;

    /* results */
    OM_uint32 majStat;
    OM_uint32 minStat;
};

static void
negotiateConfigParamInitiatorReceive(int events, int fd, void *closure,
    const struct timeval *t)
{
    struct negotiateConfigParamInitiatorState *state = closure;
    gfarm_int32_t param[NUM_NEGO_PARAM];

    if ((events & GFARM_EVENT_TIMEOUT) != 0) {
	assert(events == GFARM_EVENT_TIMEOUT);
	gflog_auth_error(GFARM_MSG_1000679, "gfarmSecSession: "
			 "negotiateConfigParamInitiatorReceive(): timed out");
	state->majStat = GSS_S_UNAVAILABLE; /* timeout */
    } else {
	assert(events == GFARM_EVENT_READ);
	if (gfarmReadInt32(fd, param, NUM_NEGO_PARAM,
	    GFARM_GSS_AUTH_TIMEOUT_MSEC) != NUM_NEGO_PARAM) {
	    gflog_auth_error(GFARM_MSG_1000680, "gfarmSecSession: "
			     "negotiateConfigParamInitiatorReceive(): "
			     "negotiation failure with the acceptor");
	    state->majStat = GSS_S_FAILURE;
	} else {
	    state->retQOP = param[NEGO_PARAM_QOP];
	    state->retMaxT = param[NEGO_PARAM_MAX_TRANS_SIZE];
	    state->retConf = param[NEGO_PARAM_OTHER_CONFIG];
	    /* End of initiator side negotiation. */
	}
    }

    /* finished */
    if (state->continuation != NULL)
	(*state->continuation)(state->closure);
}

static void
negotiateConfigParamInitiatorSend(int events, int fd, void *closure,
    const struct timeval *t)
{
    struct negotiateConfigParamInitiatorState *state = closure;
    gfarmSecSessionOption *canPtr = state->canPtr;
    gfarm_int32_t param[NUM_NEGO_PARAM];
    struct timeval timeout;
    int rv;

    param[NEGO_PARAM_QOP] = canPtr->qOpReq;
    param[NEGO_PARAM_QOP_FORCE] = canPtr->qOpForce;
    param[NEGO_PARAM_MAX_TRANS_SIZE] = canPtr->maxTransSizeReq;
    param[NEGO_PARAM_MAX_TRANS_SIZE_FORCE] = canPtr->maxTransSizeForce;
    param[NEGO_PARAM_OTHER_CONFIG] = canPtr->configReq;
    param[NEGO_PARAM_OTHER_CONFIG_FORCE] = canPtr->configForce;

    if (gfarmWriteInt32(fd, param, NUM_NEGO_PARAM) != NUM_NEGO_PARAM) {
	gflog_auth_error(GFARM_MSG_1000681,
	    "gfarmSecSession:negotiateConfigParamInitiatorSend(): "
	    "acceptor disappered");
    } else {
	timeout.tv_sec = GFARM_GSS_AUTH_TIMEOUT; timeout.tv_usec = 0;
	rv = gfarm_eventqueue_add_event(state->q, state->readable, &timeout);
	if (rv == 0) {
	    /* go to negotiateConfigParamInitiatorReceive() */
	    return;
	}
	gflog_auth_error(GFARM_MSG_1000682,
	    "gfarmSecSession:negotiateConfigParamInitiatorSend(): %s",
	    strerror(rv));
	/* XXX convert rv to state->{majStat,minStat} */
	state->majStat = GSS_S_FAILURE;
    }

    /* failure */
    if (state->continuation != NULL)
	(*state->continuation)(state->closure);
}

static struct negotiateConfigParamInitiatorState *
negotiateConfigParamInitiatorRequest(struct gfarm_eventqueue *q, int fd,
    gss_ctx_id_t sCtx, gfarmSecSessionOption *canPtr,
    void (*continuation) (void *),
    void *closure, OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    struct negotiateConfigParamInitiatorState *state = NULL;
    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;
    int rv;

    if (sCtx == GSS_C_NO_CONTEXT) {
	gflog_auth_error(GFARM_MSG_1000683, "gfarmSecSession: "
			 "negotiateConfigParamInitiatorRequest(): "
			 "no context");
    } else if (GFARM_MALLOC(state) == NULL) {
	gflog_auth_error(GFARM_MSG_1000684, "gfarmSecSession: "
			 "negotiateConfigParamInitiatorRequest(): "
			 "no memory");
    } else {
	state->writable = gfarm_fd_event_alloc(
	    GFARM_EVENT_WRITE, fd,
	    negotiateConfigParamInitiatorSend, state);
	if (state->writable == NULL) {
	    gflog_auth_error(GFARM_MSG_1000685, "gfarmSecSession: "
			     "negotiateConfigParamInitiatorRequest: "
			     "no memory");
	} else {
	    /*
	     * We cannot use two independent events (i.e. a fd_event with
	     * GFARM_EVENT_READ flag and a timer_event) here, because
	     * it's possible that both event handlers are called at once.
	     */
	    state->readable = gfarm_fd_event_alloc(
		GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, fd,
		negotiateConfigParamInitiatorReceive, state);
	    if (state->readable == NULL) {
		gflog_auth_error(GFARM_MSG_1000686, "gfarmSecSession: "
				 "negotiateConfigParamInitiatorRequest: "
				 "no memory");
	    } else {
		/* go to negotiateConfigParamInitiatorSend() */
		rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
		if (rv != 0) {
		    gflog_auth_error(GFARM_MSG_1000687, "gfarmSecSession: "
		        "negotiateConfigParamInitiatorRequest: %s",
			strerror(rv));
		} else {
		    state->q = q;
		    state->fd = fd;
		    state->sCtx = sCtx;
		    state->canPtr = canPtr;
		    state->continuation = continuation;
		    state->closure = closure;

		    state->retQOP = GFARM_GSS_DEFAULT_QOP;
		    state->retMaxT = GFARM_GSS_DEFAULT_MAX_MESSAGE_REQUEST_SIZE;
		    state->retConf = GFARM_SS_USE_ENCRYPTION;

		    state->majStat = GSS_S_COMPLETE;
		    state->minStat = GFSL_DEFAULT_MINOR_ERROR;

		    majStat = GSS_S_COMPLETE;
		    goto Done;
		}
		/* XXX convert rv to {majStat,minStat} */
		gfarm_event_free(state->readable);
	    }
	    gfarm_event_free(state->writable);
	}
	free(state);
	state = NULL;
    }
    Done:
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return (state);
}

static int
negotiateConfigParamInitiatorResult(
    struct negotiateConfigParamInitiatorState *state, gss_qop_t *qOpPtr,
    unsigned int *maxTransPtr, unsigned int *configPtr, OM_uint32 *majStatPtr,
    OM_uint32 *minStatPtr)
{
    int ret = -1;
    OM_uint32 majStat = state->majStat;
    OM_uint32 minStat = state->minStat;

    if (majStat == GSS_S_COMPLETE) {
	unsigned int maxMsgSize;
	int doEncrypt = GFARM_GSS_ENCRYPTION_ENABLED &
    			(isBitSet(state->retConf,
				  GFARM_SS_USE_ENCRYPTION) ? 1 : 0);

	if (gfarmGssConfigureMessageSize(state->sCtx, doEncrypt,
					 state->retQOP, state->retMaxT,
					 &maxMsgSize,
					 &majStat, &minStat) >= 0) {
	    if (qOpPtr != NULL) {
		*qOpPtr = state->retQOP;
	    }
	    if (maxTransPtr != NULL) {
		*maxTransPtr = maxMsgSize;
	    }
	    if (configPtr != NULL) {
		*configPtr = state->retConf;
	    }
	    ret = 1;
	}
    }
    gfarm_event_free(state->readable);
    gfarm_event_free(state->writable);
    free(state);
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
}

/*
 * multiplexed version of secSessionInitiate()
 */

struct gfarmSecSessionInitiateState {
    /* request */
    struct gfarm_eventqueue *q;
    int fd;
    gss_cred_id_t cred;
    void (*continuation)(void *);
    void *closure;
    int needClose;

    /* local variables */

    gfarmSecSessionOption canOpt;

    int rPort;
    char *peerName;
    struct gfarm_event *readable;
    struct gfarmGssInitiateSecurityContextState *secCtxState;
    struct negotiateConfigParamInitiatorState *negoCfgState;

    gss_ctx_id_t sCtx;
    OM_uint32 reqFlag;
    gss_name_t acceptorNameResult;
    char *acceptorDistName;
    gss_OID acceptorNameType;

    gss_qop_t qOp;
    unsigned int maxTransSize;
    unsigned int config;

    /* results */
    OM_uint32 majStat;
    OM_uint32 minStat;
    gfarmSecSession *session;
};

static void
secSessionInitiateCleanup(void *closure)
{
    struct gfarmSecSessionInitiateState *state = closure;

    if (negotiateConfigParamInitiatorResult(state->negoCfgState,
					    &state->qOp,
					    &state->maxTransSize,
					    &state->config,
					    &state->majStat,
					    &state->minStat) >= 0) {
	state->majStat = GSS_S_COMPLETE;
	state->minStat = GFSL_DEFAULT_MINOR_ERROR;
    }
    if (state->continuation != NULL)
	(*state->continuation)(state->closure);
}

static void
secSessionInitiateReceiveAuthorizationAck(int events, int fd, void *closure,
    const struct timeval *t)
{
    struct gfarmSecSessionInitiateState *state = closure;
    gfarm_int32_t acknack;

    if ((events & GFARM_EVENT_TIMEOUT) != 0) {
	assert(events == GFARM_EVENT_TIMEOUT);
	gflog_auth_error(GFARM_MSG_1000688, "%s: gfarm:"
	    "secSessionInitiateReceiveAuthorizationAck(): timed out",
	    state->acceptorDistName);
	state->majStat = GSS_S_UNAVAILABLE; /* timeout */
    } else {
	assert(events == GFARM_EVENT_READ);
	/*
	 * Phase 2: Receive authorization acknowledgement.
	 */
	if (gfarmReadInt32(fd, &acknack, 1, GFARM_GSS_AUTH_TIMEOUT_MSEC) != 1) {
	    gflog_auth_error(GFARM_MSG_1000689,
	        "%s: acceptor does not answer authentication result",
		state->acceptorDistName);
	    state->majStat = GSS_S_UNAUTHORIZED;
	} else if (acknack == GFARM_SS_AUTH_NACK) {
	    state->majStat = GSS_S_UNAUTHORIZED;
		gflog_auth_error(GFARM_MSG_1000690,
		    "%s: session refused by acceptor",
		    state->acceptorDistName);
	} else {
	    /*
	     * Phase 3: Negotiate configuration parameters
	     * with the acceptor.
	     */
	    state->negoCfgState = negotiateConfigParamInitiatorRequest(
		state->q, fd, state->sCtx, &state->canOpt,
		secSessionInitiateCleanup, state,
		&state->majStat, &state->minStat);
	    if (state->negoCfgState != NULL) {
		/*
		 * call negotiateConfigParamInitiator*(),
		 * then go to secSessionInitiateCleanup()
		 */
		return;
	    }
	}
    }

    /* failure */
    if (state->continuation != NULL)
	(*state->continuation)(state->closure);
}

static void
secSessionInitiateWaitAuthorizationAck(void *closure)
{
    struct gfarmSecSessionInitiateState *state = closure;
    struct timeval timeout;
    int rv;

    if (gfarmGssInitiateSecurityContextResult(
	state->secCtxState, &state->sCtx, &state->majStat, &state->minStat,
	&state->acceptorNameResult) >= 0) {
	if (state->acceptorNameResult == GSS_C_NO_NAME ||
	    (state->acceptorDistName =
	     gfarmGssNewDisplayName(state->acceptorNameResult,
				    &state->majStat, &state->minStat,
				    &state->acceptorNameType)) == NULL ||
	    state->acceptorDistName[0] == '\0' ||
	    state->acceptorNameType == GSS_C_NT_ANONYMOUS) {
	    gflog_auth_error(GFARM_MSG_1000691,
		"gfarm:secSessionInitiateWaitAuthorizationAck(): "
			     "no DN from acceptor");
	    state->majStat = GSS_S_UNAUTHORIZED; /* failure */
	} else {
	    timeout.tv_sec = GFARM_GSS_AUTH_TIMEOUT; timeout.tv_usec = 0;
	    rv = gfarm_eventqueue_add_event(state->q,
					    state->readable, &timeout);
	    if (rv == 0) {
		/* go to secSessionInitiateReceiveAuthorizationAck() */
		return;
	    }
	    gflog_auth_error(GFARM_MSG_1000692,
		"gfarm:secSessionInitiateWaitAuthorizationAck(): %s",
		strerror(rv));
	    /* XXX convert rv to state->{majStat,minStat} */
	    state->majStat = GSS_S_FAILURE;
	}
    }
    /* failure */
    if (state->continuation != NULL)
	(*state->continuation)(state->closure);
}

static struct gfarmSecSessionInitiateState *
secSessionInitiateRequest(struct gfarm_eventqueue *q, int fd,
    const gss_name_t acceptorName, gss_cred_id_t cred, OM_uint32 reqFlag,
    gfarmSecSessionOption *ssOptPtr, void (*continuation) (void *),
    void *closure, OM_uint32 *majStatPtr, OM_uint32 *minStatPtr, int needClose)
{
    struct gfarmSecSessionInitiateState *state = NULL;
    gfarmSecSession *ret = NULL;
    gfarmSecSessionOption canOpt = GFARM_SS_DEFAULT_OPTION;
    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;

    if (initiatorInitialized == 0) {
	gflog_auth_error(GFARM_MSG_1000693,
	    "gfarm:secSessionInitiateRequest(): "
			 "not initialized");
    } else if (GFARM_MALLOC(state) == NULL) {
	gflog_auth_error(GFARM_MSG_1000694,
	    "gfarm:secSessionInitiateRequest(): no memory");
    } else if ((ret = allocSecSession(GFARM_SS_INITIATOR)) == NULL) {
	gflog_auth_error(GFARM_MSG_1000695,
	    "gfarm:secSessionInitiateRequest(): no memory");
    } else if (canonicSecSessionOpt(GFARM_SS_INITIATOR, ssOptPtr, &canOpt)< 0){
	/* failure */;
	gflog_debug(GFARM_MSG_1000840, "canonicSecSessionOpt() failed");
    } else {
	state->q = q;
	state->fd = fd;
	state->cred = cred;
	state->continuation = continuation;
	state->closure = closure;
	state->needClose = needClose;

	state->canOpt = canOpt;

	state->rPort = 0;
	state->peerName = NULL;

	state->sCtx = GSS_C_NO_CONTEXT;
	state->reqFlag = reqFlag;
	state->acceptorNameResult = GSS_C_NO_NAME;
	state->acceptorDistName = NULL;

	state->majStat = GSS_S_COMPLETE;
	state->minStat = GFSL_DEFAULT_MINOR_ERROR;
	state->session = ret;

	/*
	 * Get a peer information.
	 */
	(void)gfarmGetPeernameOfSocket(fd, &state->rPort, &state->peerName);

	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, fd,
	    secSessionInitiateReceiveAuthorizationAck, state);
	if (state->readable != NULL) {
	    /*
	     * Phase 1: Initiate a security context.
	     */
	    state->secCtxState = gfarmGssInitiateSecurityContextRequest(q,
		fd, acceptorName, state->cred, reqFlag,
		secSessionInitiateWaitAuthorizationAck, state,
		&majStat, &minStat);
	    if (state->secCtxState != NULL) {
		if (majStatPtr != NULL) {
		    *majStatPtr = GSS_S_COMPLETE;
		}
		if (minStatPtr != NULL) {
		    *minStatPtr = GFSL_DEFAULT_MINOR_ERROR;
		}
		return (state); /* success */
	    }
	    gflog_auth_error(GFARM_MSG_1000696,
		"gfarm:secSessionInitiateRequest(): no memory");
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    gfarm_event_free(state->readable);
	} else {
	    gflog_debug(GFARM_MSG_1000841, "gfarm_fd_event_alloc() failed");
	}

	/* failure */
	if (state->peerName != NULL) {
	    free(state->peerName);
	}
    }

    if (ret != NULL) {
	destroySecSession(ret);
    }
    if (state != NULL) {
	free(state);
    }
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return (NULL); /* failure */
}

static gfarmSecSession *
secSessionInitiateResult(struct gfarmSecSessionInitiateState *state,
    OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    gfarmSecSession *ret = state->session;

    if (GSS_ERROR(state->majStat)) {
	if (ret != NULL) {
	    destroySecSession(ret);
	    ret = NULL;
	}
	if (state->peerName != NULL) {
	    free(state->peerName);
	}
	if (state->sCtx != GSS_C_NO_CONTEXT) {
	    gfarmGssDeleteSecurityContext(&state->sCtx);
	}
	if (state->acceptorNameResult != GSS_C_NO_NAME) {
	    gfarmGssDeleteName(&state->acceptorNameResult, NULL, NULL);
	}
    } else {
#if 0
	fprintf(stderr, "Initiator config:\n");
	dumpConfParam(state->qOp, state->maxTransSize, state->config);
#endif
	/*
	 * Success: Fill all members of session struct out.
	 */
	ret->fd = state->fd;
	ret->needClose = state->needClose;
	ret->rPort = state->rPort;
	ret->peerName = state->peerName;
	ret->cred = state->cred;
	ret->sCtx = state->sCtx;
	ret->iOa = GFARM_SS_INITIATOR;
	ret->iOaInfo.initiator.reqFlag = state->reqFlag;
	ret->iOaInfo.initiator.acceptorName = state->acceptorNameResult;
	ret->qOp = state->qOp;
	ret->maxTransSize = state->maxTransSize;
	ret->config = state->config;
	ret->gssLastStat = state->majStat;
    }

    if (state->acceptorDistName != NULL) {
	(void)free(state->acceptorDistName);
    }
    gfarm_event_free(state->readable);

    if (majStatPtr != NULL) {
	*majStatPtr = state->majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = state->minStat;
    }
    free(state);
    return (ret);
}

/*
 * multiplexed version of gfarmSecSessionInitiate()
 */

struct gfarmSecSessionInitiateState *
gfarmSecSessionInitiateRequest(struct gfarm_eventqueue *q, int fd,
    const gss_name_t acceptorName, gss_cred_id_t cred, OM_uint32 reqFlag,
    gfarmSecSessionOption *ssOptPtr, void (*continuation) (void *),
    void *closure, OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    return secSessionInitiateRequest(q, fd, acceptorName, cred, reqFlag,
				     ssOptPtr,
				     continuation, closure,
				     majStatPtr, minStatPtr, 0);
}

gfarmSecSession *
gfarmSecSessionInitiateResult(struct gfarmSecSessionInitiateState *state,
    OM_uint32 *majStatPtr, OM_uint32 *minStatPtr)
{
    return secSessionInitiateResult(state, majStatPtr, minStatPtr);
}
