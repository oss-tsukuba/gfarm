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

#include "gfevent.h"
#include "gfutil.h"

#include "tcputil.h"
#include "gfarm_secure_session.h"
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
				      char *acceptorNameString,
				      gss_OID acceptorNameType,
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
	gflog_auth_error("gfarm:secSessionReadConfigFile()",
			 "GFSL configuration file isn't specified");
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
	gflog_auth_error("gfarmSecSession:canonicSecSessionOpt()",
			 "invalid argument");
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
    int ret = -1;
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
	gflog_auth_error("gfarmSecSession:negotiateConfigParam()",
			 "no context");
	goto Done;
    }

    switch (which) {
	gfarm_int32_t param[NUM_NEGO_PARAM];

	case GFARM_SS_ACCEPTOR: {
	    int iQOP, iQOPF;
	    unsigned int iMax;
	    int iMaxF;
	    int iConf, iConfF;
	   
	    if (gfarmReadInt32(fd, param, NUM_NEGO_PARAM) != NUM_NEGO_PARAM) {
		gflog_auth_error("gfarmSecSession:negotiateConfigParam()",
				 "negotiation failure with the initiator");
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
		retMaxT = iMax;
	    } else if (iMaxF == 1) {
		/*
		 * Use the initiator's.
		 */
		retMaxT = canPtr->maxTransSizeReq;
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
		gflog_auth_error("gfarmSecSession:negotiateConfigParam()",
				 "initiator disappered");
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
		gflog_auth_error("gfarmSecSession:negotiateConfigParam()",
				 "acceptor disappered");
		goto Done;
	    }

	    if (gfarmReadInt32(fd, param, NUM_NEGO_PARAM) != NUM_NEGO_PARAM) {
		gflog_auth_error("gfarmSecSession:negotiateConfigParam()",
				 "negotiation failure with the acceptor");
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
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return ret;
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
		if (ssPtr->iOaInfo.acceptor.exportedName != NULL) {
		    free(ssPtr->iOaInfo.acceptor.exportedName);
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
					  NULL,
					  GSS_C_NO_OID,
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
		gflog_auth_error("gfarmSecSessionInitializeAcceptor()",
				 "no memory");
		majStat = GSS_S_FAILURE;
		minStat = GFSL_DEFAULT_MINOR_ERROR;
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
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    goto Done;
	}

	/*
	 * Authorization init.
	 */
	if (gfarmAuthInitialize(usermapFile) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
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

char *
gfarmSecSessionGetInitiatorCredName()
{
    if (initiatorInitialized)
	return (initiatorInitialCredName);
    else
	return (NULL);
}

int
gfarmSecSessionInitializeInitiator(configFile, usermapFile, majStatPtr, minStatPtr)
     char *configFile;
     char *usermapFile;
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
					  NULL,
					  GSS_C_NO_OID,
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
		gflog_auth_error("gfarmSecSessionInitializeInitiator()",
				 "no memory");
		majStat = GSS_S_FAILURE;
		minStat = GFSL_DEFAULT_MINOR_ERROR;
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
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
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
	 */
	if (gfarmAuthInitialize(usermapFile) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    goto Done;
	}
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */

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
					  NULL,
					  GSS_C_NO_OID,
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
		gflog_auth_error("gfarmSecSessionInitializeBoth()",
				 "no memory");
		majStat = GSS_S_FAILURE;
		minStat = GFSL_DEFAULT_MINOR_ERROR;
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
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
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
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    ret = -1;
	    goto Done;
	}

	/*
	 * Authorization init.
	 */
	if (gfarmAuthInitialize(usermapFile) < 0) {
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
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
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;
    gss_ctx_id_t sCtx = GSS_C_NO_CONTEXT;
    char *initiatorDistName = NULL;
    char *initiatorExportedName = NULL;
    gss_cred_id_t deleCred = GSS_C_NO_CREDENTIAL;
    char *credName = NULL;
    gfarm_int32_t acknack = GFARM_SS_AUTH_NACK;

    gss_qop_t qOp;
    unsigned int maxTransSize;
    unsigned int config;

    if (acceptorInitialized == 0) {
	gflog_auth_error("gfarmSecSessionAccept()", "not initialized");
	goto Fail;
    }

    ret = allocSecSession(GFARM_SS_ACCEPTOR);
    if (ret == NULL) {
	gflog_auth_error("gfarmSecSessionAccept()", "no memory");
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
				      &initiatorExportedName,
				      &deleCred) < 0) {
	goto Fail;
    }
    if (initiatorDistName == NULL || initiatorDistName[0] == '\0') {
	gflog_auth_error("gfarmSecSessionAccept()", "no DN from initiator");
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 2: Authorization and send ACK/NACK
     */
    entry = gfarmAuthGetUserEntry(initiatorDistName);
    if (entry == NULL) {
	gflog_auth_error(initiatorDistName, "not registered in mapfile");
	majStat = GSS_S_UNAUTHORIZED;
	/* Send NACK. */
	acknack = GFARM_SS_AUTH_NACK;
	(void)gfarmWriteInt32(fd, &acknack, 1);
	goto Fail;
    } else {
	int type = gfarmAuthGetAuthEntryType(entry);
	if (type == GFARM_AUTH_USER) {
	    /* Send ACK. */
	    acknack = GFARM_SS_AUTH_ACK;
	    (void)gfarmWriteInt32(fd, &acknack, 1);
	} else if (type == GFARM_AUTH_HOST) {
	    /* check peer name is actually allowed */
	    if (strcmp(peerName, entry->authData.hostAuth.FQDN) == 0) {
		/* Send ACK. */
		acknack = GFARM_SS_AUTH_ACK;
		(void)gfarmWriteInt32(fd, &acknack, 1);
	    } else {
		gflog_auth_error(initiatorDistName, "hostname doesn't match");
		/* Send NACK. */
		majStat = GSS_S_UNAUTHORIZED;
		acknack = GFARM_SS_AUTH_NACK;
		(void)gfarmWriteInt32(fd, &acknack, 1);
		goto Fail;
	    }
	}
    }

    /*
     * Phase 3: Negotiate configuration parameters
     * with the initiator.
     */
    if (negotiateConfigParam(fd, sCtx, GFARM_SS_ACCEPTOR, &canOpt,
			     &qOp, &maxTransSize, &config,
			     &majStat, &minStat) < 0) {
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
    ret->iOaInfo.acceptor.exportedName = initiatorExportedName;
    ret->iOaInfo.acceptor.deleCred = deleCred;
    ret->qOp = qOp;
    ret->maxTransSize = maxTransSize;
    ret->config = config;
    ret->gssLastStat = majStat;
    goto Done;

    Fail:
    if (initiatorExportedName != NULL) {
	free(initiatorExportedName);
	initiatorExportedName = NULL;
    }
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
secSessionInitiate(fd, acceptorNameString, acceptorNameType, cred, ssOptPtr, majStatPtr, minStatPtr, needClose)
     int fd;
     char *acceptorNameString;
     gss_OID acceptorNameType;
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
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;
    gss_ctx_id_t sCtx = GSS_C_NO_CONTEXT;
    char *acceptorDistName = NULL;
    char *credName = NULL;
    gfarm_int32_t acknack = GFARM_SS_AUTH_NACK;

    gss_qop_t qOp;
    unsigned int maxTransSize;
    unsigned int config;

    if (initiatorInitialized == 0) {
	gflog_auth_error("gfarm:secSessionInitiate()", "not initialized");
	goto Fail;
    }

    ret = allocSecSession(GFARM_SS_INITIATOR);
    if (ret == NULL) {
	gflog_auth_error("gfarm:secSessionInitiate()", "no memory");
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
    if (gfarmGssInitiateSecurityContext(fd,
					acceptorNameString, acceptorNameType,
					cred,
					GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG,
					&sCtx,
					&majStat,
					&minStat,
					&acceptorDistName) < 0) {
	goto Fail;
    }
    if (acceptorDistName == NULL || acceptorDistName[0] == '\0') {
	gflog_auth_error("gfarm:secSessionInitiate()", "no DN from acceptor");
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 2: Receive authorization acknowledgement.
     */
    if (gfarmReadInt32(fd, &acknack, 1) != 1) {
	gflog_auth_error(acceptorDistName,
			 "acceptor does not answer authentication result");
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }
    if (acknack == GFARM_SS_AUTH_NACK) {
	gflog_auth_error(acceptorDistName, "session refused by acceptor");
	majStat = GSS_S_UNAUTHORIZED;
	goto Fail;
    }

    /*
     * Phase 3: Negotiate configuration parameters
     * with the acceptor.
     */
    if (negotiateConfigParam(fd, sCtx, GFARM_SS_INITIATOR, &canOpt,
			     &qOp, &maxTransSize, &config,
			     &majStat, &minStat) < 0) {
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
gfarmSecSessionInitiate(fd, acceptorNameString, acceptorNameType, cred, ssOptPtr, majStatPtr, minStatPtr)
     int fd;
     char *acceptorNameString;
     gss_OID acceptorNameType;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    return secSessionInitiate(fd, acceptorNameString, acceptorNameType, cred,
			      ssOptPtr, majStatPtr, minStatPtr, 0);
}


gfarmSecSession *
gfarmSecSessionInitiateByAddr(rAddr, port, acceptorNameString, acceptorNameType, cred, ssOptPtr, majStatPtr, minStatPtr)
     unsigned long rAddr;
     int port;
     char *acceptorNameString;
     gss_OID acceptorNameType;
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
	if (minStatPtr != NULL) {
	    *minStatPtr = GFSL_DEFAULT_MINOR_ERROR;
	}
	return NULL;
    }
    return secSessionInitiate(fd, acceptorNameString, acceptorNameType, cred,
			      ssOptPtr, majStatPtr, minStatPtr, 1);
}


gfarmSecSession *
gfarmSecSessionInitiateByName(hostname, port, acceptorNameString, acceptorNameType, cred, ssOptPtr, majStatPtr, minStatPtr)
     char *hostname;
     int port;
     char *acceptorNameString;
     gss_OID acceptorNameType;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    unsigned long rAddr = gfarmIPGetAddressOfHost(hostname);
    if (rAddr == ~0L || rAddr == 0L) {
	if (majStatPtr != NULL) {
	    *majStatPtr = GSS_S_FAILURE;
	}
	if (minStatPtr != NULL) {
	    *minStatPtr = GFSL_DEFAULT_MINOR_ERROR;
	}
	return NULL;
    }
    return gfarmSecSessionInitiateByAddr(rAddr, port,
					 acceptorNameString, acceptorNameType, cred,
					 ssOptPtr, majStatPtr, minStatPtr);
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

char *
gfarmSecSessionGetInitiatorExportedName(gfarmSecSession *ssPtr)
{
    if (ssPtr->iOa == GFARM_SS_INITIATOR) {
	return NULL;
    }
    return ssPtr->iOaInfo.acceptor.exportedName;
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
#if ! GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
	/*
	 * If GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS is true,
	 * this information is need to initiate a conneciton to
	 * an acceptor which name is GSS_C_NT_USER_NAME.
	 */
	gfarmAuthMakeThisAlone(aePtr);
#endif /* ! GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
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
gfarmSecSessionSendInt8(ssPtr, buf, n)
     gfarmSecSession *ssPtr;
     gfarm_int8_t *buf;
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
gfarmSecSessionReceiveInt8(ssPtr, bufPtr, lenPtr)
     gfarmSecSession *ssPtr;
     gfarm_int8_t **bufPtr;
     int *lenPtr;
{
    return gfarmGssReceive(ssPtr->fd,
			   ssPtr->sCtx,
			   bufPtr,
			   lenPtr,
			   &(ssPtr->gssLastStat));
}


int
gfarmSecSessionSendInt32(ssPtr, buf, n)
     gfarmSecSession *ssPtr;
     gfarm_int32_t *buf;
     int n;
{
    gfarm_int32_t *lBuf = malloc(GFARM_OCTETS_PER_32BIT * n);
    int i;
    int ret = -1;

    if (lBuf == NULL) {
	ssPtr->gssLastStat = GSS_S_FAILURE;
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
gfarmSecSessionReceiveInt32(ssPtr, bufPtr, lenPtr)
     gfarmSecSession *ssPtr;
     gfarm_int32_t **bufPtr;
     int *lenPtr;
{
    gfarm_int8_t *lBuf = NULL;
    gfarm_int8_t *lbPtr = NULL;
    gfarm_int32_t *retBuf = NULL;
    gfarm_int32_t tmp;
    int len = 0;
    int i;
    int n;
    int ret = gfarmSecSessionReceiveInt8(ssPtr, &lBuf, &len);
    
    if (ret <= 0) {
	goto Done;
    }
    n = len % GFARM_OCTETS_PER_32BIT;
    if (n != 0) {
	goto Done;
    }
    n = len / GFARM_OCTETS_PER_32BIT;

    retBuf = malloc(GFARM_OCTETS_PER_32BIT * n);
    if (retBuf == NULL) {
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
gfarmSecSessionSendInt16(ssPtr, buf, n)
     gfarmSecSession *ssPtr;
     gfarm_int16_t *buf;
     int n;
{
    gfarm_int16_t *lBuf = malloc(GFARM_OCTETS_PER_16BIT * n);
    int i;
    int ret = -1;

    if (lBuf == NULL) {
	ssPtr->gssLastStat = GSS_S_FAILURE;
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
gfarmSecSessionReceiveInt16(ssPtr, bufPtr, lenPtr)
     gfarmSecSession *ssPtr;
     gfarm_int16_t **bufPtr;
     int *lenPtr;
{
    char *lBuf = NULL;
    char *lbPtr = NULL;
    gfarm_int16_t *retBuf = NULL;
    gfarm_int16_t tmp;
    int len = 0;
    int i;
    int n;
    int ret = gfarmSecSessionReceiveInt8(ssPtr, &lBuf, &len);
    
    if (ret <= 0) {
	goto Done;
    }
    n = len % GFARM_OCTETS_PER_16BIT;
    if (n != 0) {
	goto Done;
    }
    n = len / GFARM_OCTETS_PER_16BIT;

    retBuf = malloc(GFARM_OCTETS_PER_16BIT * n);
    if (retBuf == NULL) {
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
negotiateConfigParamInitiatorReceive(events, fd, closure, t)
     int events;
     int fd;
     void *closure;
     const struct timeval *t;
{
    struct negotiateConfigParamInitiatorState *state = closure;
    gfarm_int32_t param[NUM_NEGO_PARAM];

    if ((events & GFARM_EVENT_TIMEOUT) != 0) {
	assert(events == GFARM_EVENT_TIMEOUT);
	gflog_auth_error("gfarmSecSession:"
			 "negotiateConfigParamInitiatorReceive()",
			 "timed out");
	state->majStat = GSS_S_UNAVAILABLE; /* timeout */
    } else {
	assert(events == GFARM_EVENT_READ);
	if (gfarmReadInt32(fd, param, NUM_NEGO_PARAM) != NUM_NEGO_PARAM) {
	    gflog_auth_error("gfarmSecSession:"
			     "negotiateConfigParamInitiatorReceive()",
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
negotiateConfigParamInitiatorSend(events, fd, closure, t)
     int events;
     int fd;
     void *closure;
     const struct timeval *t;
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
	gflog_auth_error("gfarmSecSession:negotiateConfigParamInitiatorSend()",
			 "acceptor disappered");
    } else {
	timeout.tv_sec = GFARM_GSS_AUTH_TIMEOUT; timeout.tv_usec = 0;
	rv = gfarm_eventqueue_add_event(state->q, state->readable, &timeout);
	if (rv == 0) {
	    /* go to negotiateConfigParamInitiatorReceive() */
	    return;
	}
	gflog_auth_error("gfarmSecSession:negotiateConfigParamInitiatorSend()",
			 strerror(rv));
	/* XXX convert rv to state->{majStat,minStat} */
	state->majStat = GSS_S_FAILURE;
    }

    /* failure */
    if (state->continuation != NULL)
	(*state->continuation)(state->closure);
}

static struct negotiateConfigParamInitiatorState *
negotiateConfigParamInitiatorRequest(q, fd, sCtx, canPtr, continuation, closure, majStatPtr, minStatPtr)
     struct gfarm_eventqueue *q;
     int fd;
     gss_ctx_id_t sCtx;
     gfarmSecSessionOption *canPtr;
     void (*continuation)(void *);
     void *closure;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    struct negotiateConfigParamInitiatorState *state = NULL;
    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;
    int rv;

    if (sCtx == GSS_C_NO_CONTEXT) {
	gflog_auth_error("gfarmSecSession:"
			 "negotiateConfigParamInitiatorRequest()",
			 "no context");
    } else if ((state = malloc(sizeof(*state))) == NULL) {
	gflog_auth_error("gfarmSecSession:"
			 "negotiateConfigParamInitiatorRequest()",
			 "no memory");
    } else {
	state->writable = gfarm_fd_event_alloc(
	    GFARM_EVENT_WRITE, fd,
	    negotiateConfigParamInitiatorSend, state);
	if (state->writable == NULL) {
	    gflog_auth_error("gfarmSecSession:"
			     "negotiateConfigParamInitiatorRequest",
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
		gflog_auth_error("gfarmSecSession:"
				 "negotiateConfigParamInitiatorRequest",
				 "no memory");
	    } else {
		/* go to negotiateConfigParamInitiatorSend() */
		rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
		if (rv != 0) {
		    gflog_auth_error("gfarmSecSession:"
				     "negotiateConfigParamInitiatorRequest",
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
negotiateConfigParamInitiatorResult(state, qOpPtr, maxTransPtr, configPtr, majStatPtr, minStatPtr)
     struct negotiateConfigParamInitiatorState *state;
     gss_qop_t *qOpPtr;
     unsigned int *maxTransPtr;
     unsigned int *configPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
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

    unsigned long int rAddr;
    int rPort;
    char *peerName;
    struct gfarm_event *readable;
    struct gfarmGssInitiateSecurityContextState *secCtxState;
    struct negotiateConfigParamInitiatorState *negoCfgState;

    gss_ctx_id_t sCtx;
    char *acceptorDistName;
    char *credName;

    gss_qop_t qOp;
    unsigned int maxTransSize;
    unsigned int config;

    /* results */
    OM_uint32 majStat;
    OM_uint32 minStat;
    gfarmSecSession *session;
};

static void
secSessionInitiateCleanup(closure)
     void *closure;
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
secSessionInitiateReceiveAuthorizationAck(events, fd, closure, t)
     int events;
     int fd;
     void *closure;
     const struct timeval *t;
{
    struct gfarmSecSessionInitiateState *state = closure;
    gfarm_int32_t acknack;

    if ((events & GFARM_EVENT_TIMEOUT) != 0) {
	assert(events == GFARM_EVENT_TIMEOUT);
	gflog_auth_error(state->acceptorDistName,
			 "gfarm:secSessionInitiateReceiveAuthorizationAck(): "
			 "timed out");
	state->majStat = GSS_S_UNAVAILABLE; /* timeout */
    } else {
	assert(events == GFARM_EVENT_READ);
	/*
	 * Phase 2: Receive authorization acknowledgement.
	 */
	if (gfarmReadInt32(fd, &acknack, 1) != 1) {
	    gflog_auth_error(state->acceptorDistName,
			     "acceptor does not answer authentication result");
	    state->majStat = GSS_S_UNAUTHORIZED;
	} else if (acknack == GFARM_SS_AUTH_NACK) {
	    state->majStat = GSS_S_UNAUTHORIZED;
		gflog_auth_error(state->acceptorDistName,
				 "session refused by acceptor");
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
secSessionInitiateWaitAuthorizationAck(closure)
     void *closure;
{
    struct gfarmSecSessionInitiateState *state = closure;
    struct timeval timeout;
    int rv;

    if (gfarmGssInitiateSecurityContextResult(
	state->secCtxState, &state->sCtx, &state->majStat, &state->minStat,
	&state->acceptorDistName) >= 0) {
	if (state->acceptorDistName == NULL ||
	    state->acceptorDistName[0] == '\0') {
	    gflog_auth_error("gfarm:secSessionInitiateWaitAuthorizationAck()",
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
	    gflog_auth_error("gfarm:secSessionInitiateWaitAuthorizationAck()",
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
secSessionInitiateRequest(q, fd, acceptorNameString, acceptorNameType, cred, ssOptPtr, continuation, closure, needClose, majStatPtr, minStatPtr)
     struct gfarm_eventqueue *q;
     int fd;
     char *acceptorNameString;
     gss_OID acceptorNameType;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     void (*continuation)(void *);
     void *closure;
     int needClose;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    struct gfarmSecSessionInitiateState *state = NULL;
    gfarmSecSession *ret = NULL;
    gfarmSecSessionOption canOpt = GFARM_SS_DEFAULT_OPTION;
    OM_uint32 majStat = GSS_S_FAILURE;
    OM_uint32 minStat = GFSL_DEFAULT_MINOR_ERROR;

    if (initiatorInitialized == 0) {
	gflog_auth_error("gfarm:secSessionInitiateRequest()",
			 "not initialized");
    } else if ((state = malloc(sizeof(*state))) == NULL) {
	gflog_auth_error("gfarm:secSessionInitiateRequest()", "no memory");
    } else if ((ret = allocSecSession(GFARM_SS_INITIATOR)) == NULL) {
	gflog_auth_error("gfarm:secSessionInitiateRequest()", "no memory");
    } else if (canonicSecSessionOpt(GFARM_SS_INITIATOR, ssOptPtr, &canOpt)< 0){
	/* failure */;
    } else {
	state->q = q;
	state->fd = fd;
	state->cred = cred;
	state->continuation = continuation;
	state->closure = closure;
	state->needClose = needClose;

	state->canOpt = canOpt;

	state->rAddr = INADDR_ANY;
	state->rPort = 0;
	state->peerName = NULL;

	state->sCtx = GSS_C_NO_CONTEXT;
	state->acceptorDistName = NULL;
	state->credName = NULL;

	state->majStat = GSS_S_COMPLETE;
	state->minStat = GFSL_DEFAULT_MINOR_ERROR;
	state->session = ret;

	/*
	 * Get a peer information.
	 */
	state->rAddr = gfarmIPGetPeernameOfSocket(fd, &state->rPort);
	if (state->rAddr != 0 && state->rPort != 0) {
	    state->peerName = gfarmIPGetHostOfAddress(state->rAddr);
	}

	/*
	 * Check the credential.
	 */
	if (cred == GSS_C_NO_CREDENTIAL) {
	    state->cred = initiatorInitialCred;
	    state->credName = strdup(initiatorInitialCredName);
	} else {
	    state->credName = gfarmGssGetCredentialName(cred);
	}

	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, fd,
	    secSessionInitiateReceiveAuthorizationAck, state);

	if (state->readable == NULL) {
	    gflog_auth_error("gfarm:secSessionInitiateRequest()", "no memory");
	} else {
	    /*
	     * Phase 1: Initiate a security context.
	     */
	    state->secCtxState = gfarmGssInitiateSecurityContextRequest(q,
		fd, acceptorNameString, acceptorNameType, cred,
		GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG,
		secSessionInitiateWaitAuthorizationAck, state,
		majStatPtr, minStatPtr);
	    if (state->secCtxState != NULL)
		return (state); /* success */

	    gflog_auth_error("gfarm:secSessionInitiateRequest()", "no memory");
	    majStat = *majStatPtr;
	    minStat = *minStatPtr;
	}

	/* failure */
	if (state->readable != NULL) {
	    gfarm_event_free(state->readable);
	}
	if (state->peerName != NULL) {
	    free(state->peerName);
	}
	if (state->credName != NULL) {
	    free(state->credName);
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
secSessionInitiateResult(state, majStatPtr, minStatPtr)
     struct gfarmSecSessionInitiateState *state;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    gfarmSecSession *ret = state->session;

    if (GSS_ERROR(state->majStat)) {
	destroySecSession(ret);
	ret = NULL;
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
	ret->rAddr = state->rAddr;
	ret->rPort = state->rPort;
	ret->peerName = state->peerName;
	ret->cred = state->cred;
	ret->credName = state->credName;
	ret->sCtx = state->sCtx;
	ret->iOa = GFARM_SS_INITIATOR;
	ret->iOaInfo.initiator.reqFlag = GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG;
	ret->iOaInfo.initiator.acceptorDistName =
	    strdup(state->acceptorDistName);
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
gfarmSecSessionInitiateRequest(q, fd, acceptorNameString, acceptorNameType, cred, ssOptPtr, continuation, closure, majStatPtr, minStatPtr)
     struct gfarm_eventqueue *q;
     int fd;
     char *acceptorNameString;
     gss_OID acceptorNameType;
     gss_cred_id_t cred;
     gfarmSecSessionOption *ssOptPtr;
     void (*continuation)(void *);
     void *closure;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    return secSessionInitiateRequest(q,
				     fd, acceptorNameString, acceptorNameType,
				     cred, ssOptPtr,
				     continuation, closure,
				     majStatPtr, minStatPtr, 0);
}

gfarmSecSession *
gfarmSecSessionInitiateResult(state, majStatPtr, minStatPtr)
     struct gfarmSecSessionInitiateState *state;
     OM_uint32 *majStatPtr, *minStatPtr;
{
    return secSessionInitiateResult(state, majStatPtr, minStatPtr);
}
