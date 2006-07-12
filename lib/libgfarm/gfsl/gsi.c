#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>

#include "gssapi.h"

#include "gfevent.h"
#include "gfutil.h"

#include "tcputil.h"

#include "gfsl_config.h"
#include "gfarm_gsi.h"
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
#include "gfarm_auth.h"
#endif


static char **gssCrackStatus(OM_uint32 statValue, int statType);

static int gssInitiateSecurityContextSwitch(struct gfarmGssInitiateSecurityContextState *state);
static int gssInitiateSecurityContextNext(struct gfarmGssInitiateSecurityContextState *state);
static void gfarmGssInitiateSecurityContextSendToken(int events,
						     int fd,
						     void *closure,
						     const struct timeval *t);
static void gfarmGssInitiateSecurityContextReceiveToken(int events,
							int fd,
							void *closure,
							const struct timeval *t);



static char **
gssCrackStatus(statValue, statType)
     OM_uint32 statValue;
     int statType;
{
    OM_uint32 msgCtx;
    OM_uint32 minStat;
    gss_buffer_desc stStr;
    char **ret;
    int i = 0;
    char *dP = NULL;

    GFARM_MALLOC_ARRAY(ret, 1);
    ret[0] = NULL;
    while (1) {
	msgCtx = 0;
	(void)gss_display_status(&minStat,
				 statValue,
				 statType,
				 GSS_C_NO_OID,
				 &msgCtx,
				 &stStr);
	GFARM_REALLOC_ARRAY(ret, ret, i + 2);
	GFARM_MALLOC_ARRAY(ret[i], (int)stStr.length + 1);
	dP = ret[i];
	dP[(int)stStr.length] = '\0';
	i++;
	(void)memcpy((void *)dP, (void *)stStr.value, (int)stStr.length);
	(void)gss_release_buffer(&minStat, (gss_buffer_t)&stStr);
	if (msgCtx == 0) {
	    break;
	}
    }
    ret[i] = NULL;

    return ret;
}


void
gfarmGssFreeCrackedStatus(strPtr)
     char **strPtr;
{
    char **cpS = strPtr;
    while (*cpS != NULL) {
	(void)free(*cpS++);
    }
    (void)free(strPtr);
}


char **
gfarmGssCrackMajorStatus(majStat)
     OM_uint32 majStat;
{
    return gssCrackStatus(majStat, GSS_C_GSS_CODE);
}


char **
gfarmGssCrackMinorStatus(minStat)
     OM_uint32 minStat;
{
    return gssCrackStatus(minStat, GSS_C_MECH_CODE);
}


void
gfarmGssPrintMajorStatus(majStat)
     OM_uint32 majStat;
{
    char **list = gfarmGssCrackMajorStatus(majStat);
    char **lP = list;
    if (*lP != NULL) {
	while (*lP != NULL) {
	    gflog_error("\t : %s", *lP++);
	}
    } else {
	gflog_error("GSS Major Status Error: UNKNOWN");
    }
    gfarmGssFreeCrackedStatus(list);
}


void
gfarmGssPrintMinorStatus(minStat)
     OM_uint32 minStat;
{
    char **list = gfarmGssCrackMinorStatus(minStat); 
    char **lP = list;
    if (*lP != NULL) {
	while (*lP != NULL)
	    gflog_error("\t : %s", *lP++);
    } else {
	gflog_error("GSS Minor Status Error: UNKNOWN\n");
    }
    gfarmGssFreeCrackedStatus(list);
}


int
gfarmGssImportName(namePtr, nameValue, nameLength, nameType, majStatPtr, minStatPtr)
     gss_name_t *namePtr;
     void *nameValue;
     size_t nameLength;
     gss_OID nameType;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    OM_uint32 majStat = 0;
    OM_uint32 minStat = 0;
    int ret = -1;
    gss_buffer_desc buf;

#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
    if (nameType == GSS_C_NT_USER_NAME) {
	char *user;
	gfarmAuthEntry *aePtr;

	GFARM_MALLOC_ARRAY(user, nameLength + 1);
	if (user == NULL) {
	    gflog_auth_error("gfarmGssImportName(): no memory");
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    goto Done;
	}
	memcpy(user, nameValue, nameLength);
	user[nameLength] = '\0';
	aePtr = gfarmAuthGetLocalUserEntry(user);
	if (aePtr == NULL) {
	    gflog_auth_error("%s: ERROR: cannot convert "
			     "this user name to X.509 Distinguish name", user);
	    free(user);
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    goto Done;
	}
	free(user);
	assert(aePtr->authType == GFARM_AUTH_USER);
	nameValue = aePtr->distName;
	nameLength = strlen(aePtr->distName);
	nameType = GSS_C_NO_OID; /* mechanism specific */
    }
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */
    buf.length = nameLength;
    buf.value = nameValue;
    majStat = gss_import_name(&minStat, &buf, nameType, namePtr);
    if (majStat == GSS_S_COMPLETE) {
	ret = 1; /* OK */
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


int
gfarmGssImportNameOfHostBasedService(namePtr, service, hostname, majStatPtr, minStatPtr)
     gss_name_t *namePtr;
     char *service;
     char *hostname;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat;
    int ret = -1;
    size_t nameLength = strlen(service) + 1 + strlen(hostname);
    char *nameString;

    GFARM_MALLOC_ARRAY(nameString, nameLength + 1);
    if (nameString == NULL) {
	gflog_auth_error("gfarmGssImportNameOfHostBasedService(): "
			 "no memory");
	majStat = GSS_S_FAILURE;
	minStat = GFSL_DEFAULT_MINOR_ERROR;
    } else {
	sprintf(nameString, "%s@%s", service, hostname);
	if (gfarmGssImportName(namePtr, nameString, nameLength,
			       GSS_C_NT_HOSTBASED_SERVICE,
			       &majStat, &minStat) > 0) {
	    ret = 1;
	}
	free(nameString);
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
gfarmGssImportNameOfHost(namePtr, hostname, majStatPtr, minStatPtr)
     gss_name_t *namePtr;
     char *hostname;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    return gfarmGssImportNameOfHostBasedService(namePtr, "host", hostname,
						majStatPtr, minStatPtr);
}


int
gfarmGssDeleteName(namePtr, majStatPtr, minStatPtr)
     gss_name_t *namePtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat;

    majStat = gss_release_name(&minStat, namePtr);

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return majStat == GSS_S_COMPLETE ? 1 : -1;
}


#if 0 /* gss_duplicate_name() is not implemented at least in globus-2 yet. */
int
gfarmGssDuplicateName(outputNamePtr, inputName, majStatPtr, minStatPtr)
     gss_name_t *outputNamePtr;
     const gss_name_t inputName;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat;

    majStat = gss_duplicate_name(&minStat, inputName, outputNamePtr);

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return majStat == GSS_S_COMPLETE ? 1 : -1;
}
#endif


int
gfarmGssNewCredentialName(outputNamePtr, cred, majStatPtr, minStatPtr)
     gss_name_t *outputNamePtr;
     gss_cred_id_t cred;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat;

    majStat = gss_inquire_cred(&minStat, cred, outputNamePtr,
			       NULL,	/* lifetime */
			       NULL,	/* usage */
			       NULL	/* supported mech */);
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return majStat == GSS_S_COMPLETE ? 1 : -1;
}


char *
gfarmGssNewDisplayName(inputName, majStatPtr, minStatPtr, outputNameTypePtr)
     const gss_name_t inputName;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     gss_OID *outputNameTypePtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat, minStat2;
    char *ret = NULL;
    gss_buffer_desc buf;
    gss_OID outputNameType;

    if (inputName == GSS_C_NO_NAME) {
	gflog_auth_error("gfarmGssNewDisplayName(): GSS_C_NO_NAME is passed");
	majStat = GSS_S_FAILURE;
	minStat = GFSL_DEFAULT_MINOR_ERROR;
    } else if ((majStat = gss_display_name(&minStat, inputName,
					   &buf, &outputNameType))
	       == GSS_S_COMPLETE) {
	GFARM_MALLOC_ARRAY(ret, buf.length + 1);
	if (ret == NULL) {
	    gflog_auth_error("gfarmGssNewDisplayName(): no memory");
	    majStat = GSS_S_FAILURE;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	} else {
	    ret[buf.length] = '\0';
	    memcpy(ret, buf.value, buf.length);
	    (void)gss_release_buffer(&minStat2, &buf);
	    if (outputNameTypePtr != NULL) {
		*outputNameTypePtr = outputNameType;
	    }
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
gfarmGssAcquireCredential(credPtr, desiredName, credUsage, majStatPtr, minStatPtr, credNamePtr)
     gss_cred_id_t *credPtr;
     const gss_name_t desiredName;
     gss_cred_usage_t credUsage;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     gss_name_t *credNamePtr;
{
    OM_uint32 majStat = 0;
    OM_uint32 minStat = 0;
    int ret = -1;
    gss_cred_id_t cred;
    
    *credPtr = GSS_C_NO_CREDENTIAL;

    majStat = gss_acquire_cred(&minStat,
			       desiredName,
			       GSS_C_INDEFINITE,
			       GSS_C_NO_OID_SET,
			       credUsage,
			       &cred,
			       NULL,
			       NULL);
#if GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS
    if (majStat != GSS_S_COMPLETE) {
	OM_uint32 majStat2, majStat3;
	OM_uint32 minStat2, minStat3;

	/*
	 * to workaround a problem that any proxy credential cannot be
	 * acquired by using "/C=.../O=.../CN=John Smith" as its name.
	 * Globus requires "/C=.../O=.../CN=John Smith/CN=proxy".
	 */
	majStat2 = gss_acquire_cred(&minStat2,
				    GSS_C_NO_NAME,
				    GSS_C_INDEFINITE,
				    GSS_C_NO_OID_SET,
				    credUsage,
				    &cred,
				    NULL,
				    NULL);
	if (majStat2 == GSS_S_COMPLETE) {
	    gss_name_t credName;

	    if (gfarmGssNewCredentialName(&credName, cred, NULL, NULL) > 0) {
		int equal;

		majStat3 = gss_compare_name(&minStat3, desiredName, credName,
					    &equal);
		if (majStat3 == GSS_S_COMPLETE && equal) {
		    majStat = majStat2;
		    minStat = minStat2;
		}
		gfarmGssDeleteName(&credName, NULL, NULL);
	    }
	    if (majStat != GSS_S_COMPLETE) {
		gfarmGssDeleteCredential(&cred, NULL, NULL);
	    }
	}
    }
#endif /* GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS */

    /*
     * Check validness.
     */
    if (majStat == GSS_S_COMPLETE) {
	if (credNamePtr == NULL) {
	    ret = 1;
	} else if (gfarmGssNewCredentialName(credNamePtr, cred,
					     &majStat, &minStat) > 0) {
	    /* Only valid when the name is got. */
	    ret = 1;
	}
	if (ret > 0 && credPtr != NULL) {
	    *credPtr = cred;
	} else {
	    gfarmGssDeleteCredential(&cred, NULL, NULL);
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
gfarmGssDeleteCredential(credPtr, majStatPtr, minStatPtr)
     gss_cred_id_t *credPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    OM_uint32 majStat = 0;
    OM_uint32 minStat = 0;
    int ret = -1;

    majStat = gss_release_cred(&minStat, credPtr);
    if (majStat == GSS_S_COMPLETE) {
	ret = 1; /* valid */
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
gfarmGssSendToken(fd, gsBuf)
     int fd;
     gss_buffer_t gsBuf;
{
    gfarm_int32_t iLen = (gfarm_int32_t)(gsBuf->length);

    if (gfarmWriteInt32(fd, &iLen, 1) != 1) {
	return -1;
    }
    if (gfarmWriteInt8(fd, (gfarm_int8_t *)(gsBuf->value), iLen) != iLen) {
	return -1;
    }
    return iLen;
}


int
gfarmGssReceiveToken(fd, gsBuf)
     int fd;
     gss_buffer_t gsBuf;
{
    gfarm_int32_t iLen;
    gfarm_int8_t *buf = NULL;
    char *p;

    if (gsBuf->value != NULL) {
	OM_uint32 minStat;
	(void)gss_release_buffer(&minStat, gsBuf);
    }
    gsBuf->length = 0;
    gsBuf->value = NULL;

    if (gfarmReadInt32(fd, &iLen, 1) != 1) {
	return -1;
    }

    /*
     * XXXXX FIXME:
     * 	GSSAPI has no API for allocating a gss_buffer_t. It is not
     *	recommended to allocate it with malloc().
     */
    GFARM_MALLOC_ARRAY(p, iLen);
    if (p == NULL) {
	return -1;
    }
    buf = (gfarm_int8_t *)p;

    if (gfarmReadInt8(fd, buf, iLen) != iLen) {
	(void)free(buf);
	return -1;
    }

    gsBuf->length = (size_t)iLen;
    gsBuf->value = (void *)buf;
    return iLen;
}


int
gfarmGssAcceptSecurityContext(fd, cred, scPtr, majStatPtr, minStatPtr, remoteNamePtr, remoteCredPtr)
     int fd;
     gss_cred_id_t cred;
     gss_ctx_id_t *scPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     gss_name_t *remoteNamePtr;
     gss_cred_id_t *remoteCredPtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat, minStat2;
    OM_uint32 retFlag = GFARM_GSS_DEFAULT_SECURITY_ACCEPT_FLAG;
    gss_name_t initiatorName = GSS_C_NO_NAME;
    gss_cred_id_t remCred = GSS_C_NO_CREDENTIAL;

    gss_buffer_desc inputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t itPtr = &inputToken;
    
    gss_buffer_desc outputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t otPtr = &outputToken;

    gss_OID mechType = GSS_C_NO_OID;
    OM_uint32 timeRet;

    int tknStat;

    *scPtr = GSS_C_NO_CONTEXT;

    do {
	tknStat = gfarmGssReceiveToken(fd, itPtr);
	if (tknStat <= 0) {
	    gflog_auth_error("gfarmGssAcceptSecurityContext(): "
			     "failed to receive response");
	    majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_READ;
	    minStat = GFSL_DEFAULT_MINOR_ERROR;
	    break;
	}

	majStat = gss_accept_sec_context(&minStat,
					 scPtr,
					 cred,
					 itPtr,
					 GSS_C_NO_CHANNEL_BINDINGS,
					 &initiatorName,
					 &mechType,
					 otPtr,
					 &retFlag,
					 &timeRet,
					 &remCred);

	if (itPtr->length > 0) {
	    (void)gss_release_buffer(&minStat2, itPtr);
	}

	if (otPtr->length > 0) {
	    tknStat = gfarmGssSendToken(fd, otPtr);
	    (void)gss_release_buffer(&minStat2, otPtr);
	    if (tknStat <= 0) {
		gflog_auth_error("gfarmGssAcceptSecurityContext(): "
				 "failed to send response");
		majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
		minStat = GFSL_DEFAULT_MINOR_ERROR;
	    }
	}

	if (GSS_ERROR(majStat)) {
	    break;
	}

    } while (majStat & GSS_S_CONTINUE_NEEDED);

    if (itPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, itPtr);
    }
    if (otPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, otPtr);
    }

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }

    if (majStat == GSS_S_COMPLETE && remoteNamePtr != NULL) {
	*remoteNamePtr = initiatorName;
    } else if (initiatorName != GSS_C_NO_NAME) {
	(void)gss_release_name(&minStat2, &initiatorName);
    }

    if (majStat == GSS_S_COMPLETE && remoteCredPtr != NULL) {
	*remoteCredPtr = remCred;
    } else if (remCred != GSS_C_NO_CREDENTIAL) {
	(void)gss_release_cred(&minStat2, &remCred);
    }
    
    if (majStat != GSS_S_COMPLETE && *scPtr != GSS_C_NO_CONTEXT) {
	(void)gss_delete_sec_context(&minStat2, scPtr, GSS_C_NO_BUFFER);
    }

    return majStat == GSS_S_COMPLETE ? 1 : -1;
}


int
gfarmGssInitiateSecurityContext(fd, acceptorName, cred, reqFlag, scPtr, majStatPtr, minStatPtr, remoteNamePtr)
     int fd;
     const gss_name_t acceptorName;
     gss_cred_id_t cred;
     OM_uint32 reqFlag;
     gss_ctx_id_t *scPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     gss_name_t *remoteNamePtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat, minStat2;
    OM_uint32 retFlag = 0;

    gss_buffer_desc inputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t itPtr = &inputToken;
    
    gss_buffer_desc outputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t otPtr = &outputToken;

    gss_OID *actualMechType = NULL;
    OM_uint32 timeRet;

    int tknStat;

    *scPtr = GSS_C_NO_CONTEXT;

    /*
     * Implementation specification:
     * In gfarm, an initiator must reveal own identity to an acceptor.
     */
    if ((reqFlag & GSS_C_ANON_FLAG) == GSS_C_ANON_FLAG) {
	/* It is a bit safer to deny the request than to silently ignore it */
	gflog_auth_error("gfarmGssInitiateSecurityContext(): "
	    "GSS_C_ANON_FLAG is not allowed");
	majStat = GSS_S_UNAVAILABLE;
	minStat = GFSL_DEFAULT_MINOR_ERROR;
	goto Done;
    }

    while (1) {
	majStat = gss_init_sec_context(&minStat,
				       cred,
				       scPtr,
				       acceptorName,
				       GSS_C_NO_OID,
				       reqFlag,
				       0,
				       GSS_C_NO_CHANNEL_BINDINGS,
				       itPtr,
				       actualMechType,
				       otPtr,
				       &retFlag,
				       &timeRet);
	
	if (itPtr->length > 0) {
	    (void)gss_release_buffer(&minStat2, itPtr);
	}

	if (otPtr->length > 0) {
	    tknStat = gfarmGssSendToken(fd, otPtr);
	    (void)gss_release_buffer(&minStat2, otPtr);
	    if (tknStat <= 0) {
		gflog_auth_error("gfarmGssInitiateSecurityContext(): "
				 "failed to send response");
		majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
		minStat = GFSL_DEFAULT_MINOR_ERROR;
	    }
	}

	if (GSS_ERROR(majStat)) {
	    break;
	}
    
	if (majStat & GSS_S_CONTINUE_NEEDED) {
	    tknStat = gfarmGssReceiveToken(fd, itPtr);
	    if (tknStat <= 0) {
		gflog_auth_error("gfarmGssInitiateSecurityContext(): "
				 "failed to receive response");
		majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_READ;
		minStat = GFSL_DEFAULT_MINOR_ERROR;
		break;
	    }
	} else {
	    break;
	}
    }

    if (itPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, itPtr);
    }
    if (otPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, otPtr);
    }

    if (majStat == GSS_S_COMPLETE && remoteNamePtr != NULL) {
	majStat = gss_inquire_context(&minStat,
				      *scPtr,
				      NULL,
				      remoteNamePtr,
				      NULL,
				      NULL,
				      NULL,
				      NULL,
				      NULL);
    }

    Done:
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }

    if (majStat != GSS_S_COMPLETE && *scPtr != GSS_C_NO_CONTEXT) {
	(void)gss_delete_sec_context(&minStat2, scPtr, GSS_C_NO_BUFFER);
    }

    return majStat == GSS_S_COMPLETE ? 1 : -1;
}


void
gfarmGssDeleteSecurityContext(scPtr)
     gss_ctx_id_t *scPtr;
{
    OM_uint32 minStat;
    if (scPtr == NULL || *scPtr == GSS_C_NO_CONTEXT) {
	return;
    }
    (void)gss_delete_sec_context(&minStat, scPtr, GSS_C_NO_BUFFER);
}


int
gfarmGssConfigureMessageSize(sCtx, doEncrypt, qopReq, reqOutSz, maxInSzPtr, majStatPtr, minStatPtr)
     gss_ctx_id_t sCtx;
     int doEncrypt;
     gss_qop_t qopReq;
     unsigned int reqOutSz;
     unsigned int *maxInSzPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    int ret = -1;

    OM_uint32 majStat;
    OM_uint32 minStat;

    OM_uint32 oReqSz = (OM_uint32)reqOutSz;
    OM_uint32 oMaxSz = oReqSz;

    majStat = gss_wrap_size_limit(&minStat, sCtx, doEncrypt, qopReq,
				  oReqSz, &oMaxSz);
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    if (majStat == GSS_S_COMPLETE) {
	ret = 1;
	if (maxInSzPtr != NULL) {
	    *maxInSzPtr = (int)oMaxSz;
	}
    }

    return ret;
}


int
gfarmGssSend(fd, sCtx, doEncrypt, qopReq, buf, n, chunkSz, statPtr)
     int fd;
     gss_ctx_id_t sCtx;
     int doEncrypt;
     gss_qop_t qopReq;
     gfarm_int8_t *buf;
     int n;
     int chunkSz;
     OM_uint32 *statPtr;
{
    int ret = -1;
    OM_uint32 majStat;
    OM_uint32 minStat;
    
    gss_buffer_desc inputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t itPtr = &inputToken;
    gss_buffer_desc outputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t otPtr = &outputToken;

    int sum = 0;
    int rem = n;
    int len;
    gfarm_int32_t n_buf = n;

    /*
     * Send a length of a PLAIN TEXT.
     *	XXXXX FIX ME:
     *		Generally it is wrong idea sending a plain text length
     *		in plain text communication. Should be encrypted.
     */
    
    if (buf == NULL || n <= 0) {
	ret = 0;
	majStat = GSS_S_COMPLETE;
	goto Done;
    }

    if (gfarmWriteInt32(fd, &n_buf, 1) != 1) {
	majStat = GSS_S_CALL_INACCESSIBLE_WRITE;
	goto Done;
    }
    do {
	inputToken.value = (void *)((char *)(buf + sum));
	len = (rem > chunkSz ? chunkSz : rem);
	inputToken.length = (size_t)len;

	majStat = gss_wrap(&minStat, sCtx, doEncrypt, qopReq,
			   (const gss_buffer_t)itPtr,
			   NULL,
			   otPtr);
	if (majStat == GSS_S_COMPLETE) {
	    if (otPtr->length > 0) {
		if (gfarmGssSendToken(fd, otPtr) <= 0) {
		    majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
		    goto Done;
		}
		(void)gss_release_buffer(&minStat, otPtr);
		otPtr->length = 0;
		otPtr->value = NULL;
		rem -= len;
		sum += len;
	    } else {
		majStat = GSS_S_DEFECTIVE_TOKEN;
		goto Done;
	    }
	} else {
	    break;
	}
    } while (rem > 0);
    if (rem <= 0) {
	ret = n;
    }

    Done:
    if (otPtr->length > 0) {
	(void)gss_release_buffer(&minStat, otPtr);
    }
    if (statPtr != NULL) {
	*statPtr = majStat;
    }
    
    return ret;
}


int
gfarmGssReceive(fd, sCtx, bufPtr, lenPtr, statPtr)
     int fd;
     gss_ctx_id_t sCtx;
     gfarm_int8_t **bufPtr;
     int *lenPtr;
     OM_uint32 *statPtr;
{
    int ret = -1;
    OM_uint32 majStat;
    OM_uint32 minStat;

    gss_buffer_desc inputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t itPtr = &inputToken;
    gss_buffer_desc outputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t otPtr = &outputToken;

    gfarm_int32_t n;
    int sum = 0;
    int rem;
    int len;
    gfarm_int8_t *buf = NULL;
    int i;

    /*
     * Receive a length of a PLAIN TEXT.
     *	XXXXX FIX ME:
     *		Generally it is wrong idea receiving a plain text
     *		length in plain text communication. Should be
     *		encrypted.
     */

    i = gfarmReadInt32(fd, &n, 1);
    if (i == 0) {
	ret = 0;
	n = 0;
	majStat = GSS_S_COMPLETE;
	goto Done;
    } else if (i != 1) {
	majStat = GSS_S_CALL_INACCESSIBLE_READ;
	goto Done;
    }
    GFARM_MALLOC_ARRAY(buf, n);
    if (buf == NULL) {
	majStat = GSS_S_FAILURE;
	goto Done;
    }

    rem = n;
    do {
	if (gfarmGssReceiveToken(fd, itPtr) <= 0) {
	    majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_READ;
	    goto Done;
	}
	majStat = gss_unwrap(&minStat, sCtx,
			     (const gss_buffer_t)itPtr,
			     otPtr,
			     NULL, NULL);
	(void)gss_release_buffer(&minStat, itPtr);
	itPtr->length = 0;
	itPtr->value = NULL;
	if (majStat == GSS_S_COMPLETE) {
	    if (otPtr->length > 0) {
		(void)memcpy((void *)(buf + sum), (void *)otPtr->value, otPtr->length);
		len = (int)(otPtr->length);
		rem -= len;
		sum += len;
		(void)gss_release_buffer(&minStat, otPtr);
		otPtr->length = 0;
		otPtr->value = NULL;
	    } else {
		majStat = GSS_S_DEFECTIVE_TOKEN;
		goto Done;
	    }
	} else {
	    break;
	}
    } while (rem > 0);
    if (rem <= 0) {
	ret = n;
    }

    Done:
    if (otPtr->length > 0) {
	(void)gss_release_buffer(&minStat, otPtr);
    }
    if (itPtr->length > 0) {
	(void)gss_release_buffer(&minStat, itPtr);
    }
    if (statPtr != NULL) {
	*statPtr = majStat;
    }
    if (ret == -1) {
	*bufPtr = NULL;
	*lenPtr = -1;
    } else {
	*bufPtr = buf;
	*lenPtr = n;
    }

    return ret;
}

#if GFARM_GSS_EXPORT_CRED_ENABLED

struct gfarmExportedCredential {
    char *env;
    char *filename;
};

gfarmExportedCredential *
gfarmGssExportCredential(cred, statPtr)
     gss_cred_id_t cred;
     OM_uint32 *statPtr;
{
    gfarmExportedCredential *exportedCred = NULL;
    OM_uint32 majStat = 0;
    OM_uint32 minStat = 0;
    gss_buffer_desc buf = GSS_C_EMPTY_BUFFER;
    char *exported, *filename, *env;
    static char exported_name[] = "X509_USER_DELEG_PROXY=";
    static char env_name[] = "X509_USER_PROXY=";
    static char file_prefix[] = "FILE:";

    majStat = gss_export_cred(&minStat, cred, GSS_C_NO_OID, 1, &buf);
    if (GSS_ERROR(majStat))
	goto Done;

    exported = (char *)buf.value;
    for (filename = exported; *filename != '\0'; filename++)
	if (!isalnum(*(unsigned char *)filename) && *filename != '_')
	    break;
    if (*filename != '=') { /* not an environment variable */
	majStat = GSS_S_UNAVAILABLE;
	goto Done;
    }
    filename++;
    if (memcmp(exported, exported_name, sizeof(exported_name) - 1) == 0) {
	GFARM_MALLOC_ARRAY(env, sizeof(env_name) + strlen(filename));
	if (env == NULL) {
	    majStat = GSS_S_FAILURE;
	    goto Done;
	}
	memcpy(env, env_name, sizeof(env_name) - 1);
	strcpy(env + sizeof(env_name) - 1, filename);
	filename = env + sizeof(env_name) - 1;
    } else {
	env = strdup(exported);
	if (env == NULL) {
	    majStat = GSS_S_FAILURE;
	    goto Done;
	}
	filename = env + (filename - exported);
    }
    if (memcmp(filename, file_prefix, sizeof(file_prefix) - 1) == 0)
	filename += sizeof(file_prefix) - 1;

    GFARM_MALLOC(exportedCred);
    if (exportedCred == NULL) {
	free(env);
	majStat = GSS_S_FAILURE;
	goto Done;
    }
    exportedCred->env = env;
    exportedCred->filename = access(filename, R_OK) == 0 ? filename : NULL;

    Done:
    gss_release_buffer(&minStat, &buf);
    if (statPtr != NULL)
	*statPtr = majStat;
    return exportedCred;
}

char *
gfarmGssEnvForExportedCredential(exportedCred)
    gfarmExportedCredential *exportedCred;
{
    return exportedCred->env;
}

void
gfarmGssDeleteExportedCredential(exportedCred)
    gfarmExportedCredential *exportedCred;
{
    if (exportedCred->filename != NULL)
	unlink(exportedCred->filename);
    free(exportedCred->env);
    free(exportedCred);
}

#endif /* GFARM_GSS_EXPORT_CRED_ENABLED */

/*
 * multiplexed version of gfarmGssInitiateSecurityContext()
 */

struct gfarmGssInitiateSecurityContextState {
    struct gfarm_eventqueue *q;
    struct gfarm_event *readable, *writable;
    int fd;
    gss_cred_id_t cred;
    OM_uint32 reqFlag;
    void (*continuation)(void *);
    void *closure;

    int completed;
    OM_uint32 majStat;
    OM_uint32 minStat;

    gss_ctx_id_t sc;
    gss_name_t acceptorName;
    OM_uint32 retFlag;

    gss_buffer_desc inputToken;
    gss_buffer_t itPtr;

    gss_buffer_desc outputToken;
    gss_buffer_t otPtr;

    gss_OID *actualMechType;
    OM_uint32 timeRet;
};

/* this function returns 1, if an event is added */
static int
gssInitiateSecurityContextSwitch(state)
     struct gfarmGssInitiateSecurityContextState *state;
{
    int rv;
    struct timeval timeout;

    if (GSS_ERROR(state->majStat)) {
	return 0;
    }

    if (state->majStat & GSS_S_CONTINUE_NEEDED) {
	timeout.tv_sec = GFARM_GSS_AUTH_TIMEOUT;
	timeout.tv_usec = 0;
	rv = gfarm_eventqueue_add_event(state->q, state->readable, &timeout);
	if (rv == 0) {
	    /* go to gfarmGssInitiateSecurityContextReceiveToken() */
	    return 1;
	}
	gflog_auth_error("gfarm:gssInitiateSecurityContextSwitch(): %s",
			 strerror(rv));
	state->majStat = GSS_S_FAILURE;
	state->minStat = GFSL_DEFAULT_MINOR_ERROR;
    } else {
	state->completed = 1;
    }
    return 0;
}

/* this function returns 1, if an event is added */
static int
gssInitiateSecurityContextNext(state)
     struct gfarmGssInitiateSecurityContextState *state;
{
    OM_uint32 minStat2;
    int rv;

    state->majStat = gss_init_sec_context(&state->minStat,
					  state->cred,
					  &state->sc,
					  state->acceptorName,
					  GSS_C_NO_OID,
					  state->reqFlag,
					  0,
					  GSS_C_NO_CHANNEL_BINDINGS,
					  state->itPtr,
					  state->actualMechType,
					  state->otPtr,
					  &state->retFlag,
					  &state->timeRet);

    if (state->itPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, state->itPtr);
    }

    if (state->otPtr->length > 0) {
	rv = gfarm_eventqueue_add_event(state->q, state->writable, NULL);
	if (rv == 0) {
	    /* go to gfarmGssInitiateSecurityContextSendToken() */
	    return 1;
	}
	gflog_auth_error("gfarm:gssInitiateSecurityContextNext(): %s",
			 strerror(rv));
	state->majStat = GSS_S_FAILURE;
	state->minStat = GFSL_DEFAULT_MINOR_ERROR;
    }

    return gssInitiateSecurityContextSwitch(state);
}

static void
gfarmGssInitiateSecurityContextSendToken(events, fd, closure, t)
     int events;
     int fd;
     void *closure;
     const struct timeval *t;
{
    struct gfarmGssInitiateSecurityContextState *state = closure;
    int tknStat;
    OM_uint32 minStat2;

    tknStat = gfarmGssSendToken(fd, state->otPtr);
    (void)gss_release_buffer(&minStat2, state->otPtr);
    if (tknStat <= 0) {
	gflog_auth_error("gfarmGssInitiateSecurityContextSendToken(): "
			 "failed to send response");
	state->majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
	state->minStat = GFSL_DEFAULT_MINOR_ERROR;
    }

    if (gssInitiateSecurityContextSwitch(state)) {
	return;
    }
    if (state->continuation != NULL) {
	(*state->continuation)(state->closure);
    }
}

static void
gfarmGssInitiateSecurityContextReceiveToken(events, fd, closure, t)
     int events;
     int fd;
     void *closure;
     const struct timeval *t;
{
    struct gfarmGssInitiateSecurityContextState *state = closure;
    int tknStat;

    if ((events & GFARM_EVENT_TIMEOUT) != 0) {
	assert(events == GFARM_EVENT_TIMEOUT);
	state->majStat = GSS_S_UNAVAILABLE; /* failure: timeout */
    } else {
	assert(events == GFARM_EVENT_READ);
	tknStat = gfarmGssReceiveToken(fd, state->itPtr);
	if (tknStat <= 0) {
	    gflog_auth_error("gfarmGssInitiateSecurityContextReceiveToken(): "
			     "failed to receive response");
	    state->majStat= GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_READ;
	    state->minStat= GFSL_DEFAULT_MINOR_ERROR;
	} else if (gssInitiateSecurityContextNext(state)) {
	    return;
	}
    }
    assert(GSS_ERROR(state->majStat) || state->completed);
    if (state->continuation != NULL)
	(*state->continuation)(state->closure);
}

struct gfarmGssInitiateSecurityContextState *
gfarmGssInitiateSecurityContextRequest(q, fd, acceptorName, cred, reqFlag, continuation, closure, majStatPtr, minStatPtr)
     struct gfarm_eventqueue *q;
     int fd;
     const gss_name_t acceptorName;
     gss_cred_id_t cred;
     OM_uint32 reqFlag;
     void (*continuation)(void *);
     void *closure;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat;
    struct gfarmGssInitiateSecurityContextState *state;

    /*
     * Implementation specification:
     * In gfarm, an initiator must reveal own identity to an acceptor.
     */
    if ((reqFlag & GSS_C_ANON_FLAG) == GSS_C_ANON_FLAG) {
	/* It is a bit safer to deny the request than to silently ignore it */
	gflog_auth_error("gfarmGssInitiateSecurityContextRequest(): "
	    "GSS_C_ANON_FLAG is not allowed");
	majStat = GSS_S_UNAVAILABLE;
	minStat = GFSL_DEFAULT_MINOR_ERROR;
	goto ReturnStat;
    }

    GFARM_MALLOC(state);
    if (state == NULL) {
	gflog_auth_error("gfarmGssInitiateSecurityContextRequest(): "
			 "no memory");
	majStat = GSS_S_FAILURE;
	minStat = GFSL_DEFAULT_MINOR_ERROR;
	goto ReturnStat;
    }

    state->completed = 0;
    state->majStat = GSS_S_COMPLETE;
    state->minStat = GFSL_DEFAULT_MINOR_ERROR;

    state->writable =
	gfarm_fd_event_alloc(GFARM_EVENT_WRITE, fd,
			     gfarmGssInitiateSecurityContextSendToken,
			     state);
    if (state->writable == NULL) {
	gflog_auth_error("gfarmGssInitiateSecurityContextRequest(): "
			 "no memory");
	state->majStat = GSS_S_FAILURE;
	goto FreeState;
    }
    /*
     * We cannot use two independent events (i.e. a fd_event with
     * GFARM_EVENT_READ flag and a timer_event) here, because
     * it's possible that both event handlers are called at once.
     */
    state->readable =
	gfarm_fd_event_alloc(GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, fd,
			     gfarmGssInitiateSecurityContextReceiveToken,
			     state);
    if (state->readable == NULL) {
	gflog_auth_error("gfarmGssInitiateSecurityContextRequest(): "
			 "no memory");
	state->majStat = GSS_S_FAILURE;
	goto FreeWritable;
    }

    state->q = q;
    state->fd = fd;
    state->acceptorName = acceptorName;
    state->cred = cred;
    state->reqFlag = reqFlag;
    state->continuation = continuation;
    state->closure = closure;

    state->retFlag = 0;

    /* GSS_C_EMPTY_BUFFER */
    state->inputToken.length = 0; state->inputToken.value = NULL;
    state->itPtr = &state->inputToken;

    /* GSS_C_EMPTY_BUFFER */
    state->outputToken.length = 0; state->outputToken.value = NULL;
    state->otPtr = &state->outputToken;

    state->actualMechType = NULL;

    state->sc = GSS_C_NO_CONTEXT;

    gssInitiateSecurityContextNext(state);
    assert(!state->completed);
    if (!GSS_ERROR(state->majStat)) {
	if (majStatPtr != NULL) {
	    *majStatPtr = GSS_S_COMPLETE;
	}
	if (minStatPtr != NULL) {
	    *minStatPtr = GFSL_DEFAULT_MINOR_ERROR;
	}
	return (state);
    }

    gfarm_event_free(state->readable);

    FreeWritable:
    gfarm_event_free(state->writable);

    FreeState:
    majStat = state->majStat;
    minStat = state->minStat;
    free(state);

    ReturnStat:
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    return (NULL);
}

int
gfarmGssInitiateSecurityContextResult(state, scPtr, majStatPtr, minStatPtr, remoteNamePtr)
     struct gfarmGssInitiateSecurityContextState *state;
     gss_ctx_id_t *scPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     gss_name_t *remoteNamePtr;
{
    int ret;
    OM_uint32 minStat2;

    assert(GSS_ERROR(state->majStat) || state->completed);

    if (state->itPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, state->itPtr);
    }
    if (state->otPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, state->otPtr);
    }

    if (state->majStat == GSS_S_COMPLETE && remoteNamePtr != NULL) {
	state->majStat = gss_inquire_context(&state->minStat,
					     state->sc,
					     NULL,
					     remoteNamePtr,
					     NULL,
					     NULL,
					     NULL,
					     NULL,
					     NULL);
    }

    gfarm_event_free(state->readable);
    gfarm_event_free(state->writable);

    if (majStatPtr != NULL) {
	*majStatPtr = state->majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = state->minStat;
    }

    if (state->majStat == GSS_S_COMPLETE) {
	*scPtr = state->sc;
    } else if (state->sc != GSS_C_NO_CONTEXT) {
	(void)gss_delete_sec_context(&minStat2, &state->sc, GSS_C_NO_BUFFER);
    }

    ret = state->majStat == GSS_S_COMPLETE ? 1 : -1;
    free(state);
    return ret;
}
