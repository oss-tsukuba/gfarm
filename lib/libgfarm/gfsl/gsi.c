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

#include "gfutil.h"
#include "gfevent.h"

#include "tcputil.h"

#include "gfsl_config.h"
#include "gfarm_gsi.h"


static char *	gssName2Str(gss_name_t name);


static char *
gssName2Str(name)
     gss_name_t name;
{
    OM_uint32 minStat;
    char *ret = NULL;
    gss_name_t nameT = name;

    if (nameT != GSS_C_NO_NAME) {
	gss_buffer_desc nameBuf;
	(void)gss_display_name(&minStat, nameT, &nameBuf, NULL);
	ret = (char *)malloc(sizeof(char) * (nameBuf.length + 1));
	if (ret == NULL) {
	    goto done;
	}
	ret[nameBuf.length] = '\0';
	(void)memcpy((void *)ret, (void *)nameBuf.value, nameBuf.length);
	(void)gss_release_buffer(&minStat, (gss_buffer_t)&nameBuf);
    }
    done:
    return ret;
}


char *
gfarmGssGetCredentialName(cred)
     gss_cred_id_t cred;
{
    char *ret = NULL;
    OM_uint32 minStat = GSS_S_COMPLETE;
    gss_name_t credName = GSS_C_NO_NAME;
    (void)gss_inquire_cred(&minStat,
			   cred,
			   &credName,
			   NULL,	/* lifetime */
			   NULL,	/* usage */
			   NULL		/* supported mech */);
    if (credName != GSS_C_NO_NAME) {
	ret = gssName2Str(credName);
	(void)gss_release_name(&minStat, &credName);
    }
    return ret;
}


static char **
gssCrackStatus(statValue, statType)
     OM_uint32 statValue;
     int statType;
{
    OM_uint32 msgCtx;
    OM_uint32 minStat;
    gss_buffer_desc stStr;
    char **ret = (char **)malloc(sizeof(char *) * 1);
    int i = 0;
    char *dP = NULL;
    ret[0] = NULL;

    while (1) {
	msgCtx = 0;
	(void)gss_display_status(&minStat,
				 statValue,
				 statType,
				 GSS_C_NO_OID,
				 &msgCtx,
				 &stStr);
	ret = (char **)realloc(ret, sizeof(char *) * (i + 2));
	ret[i] = (char *)malloc(sizeof(char) * ((int)stStr.length + 1));
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
	    gflog_error("", *lP++);
	}
    } else {
	gflog_error("GSS Major Status Error:", " UNKNOWN\n");
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
	    gflog_error("", *lP++);
    } else {
	gflog_error("GSS Minor Status Error:", " UNKNOWN\n");
    }
    gfarmGssFreeCrackedStatus(list);
}


int
gfarmGssAcquireCredential(credPtr, credUsage, majStatPtr, minStatPtr, credNamePtr)
     gss_cred_id_t *credPtr;
     gss_cred_usage_t credUsage;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     char **credNamePtr;
{
    OM_uint32 majStat = 0;
    OM_uint32 minStat = 0;
    int ret = -1;
    
    *credPtr = GSS_C_NO_CREDENTIAL;

    majStat = gss_acquire_cred(&minStat,
			       GSS_C_NO_NAME,
			       GSS_C_INDEFINITE,
			       GSS_C_NO_OID_SET,
			       credUsage,
			       credPtr,
			       NULL,
			       NULL);
    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }	

    /*
     * Check validness.
     */
    if (majStat == GSS_S_COMPLETE) {
	char *name = gfarmGssGetCredentialName(*credPtr);
	if (name != NULL) {
	    /* Only valid when the name is got. */
	    ret = 1;
	}
	if (credNamePtr != NULL) {
	    *credNamePtr = name;
	} else {
	    (void)free(name);
	}
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
    buf = (void *)malloc(sizeof(char) * iLen);
    if (buf == NULL) {
	return -1;
    }

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
     char **remoteNamePtr;
     gss_cred_id_t *remoteCredPtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat;
    OM_uint32 retFlag = 0;
    gss_name_t initiatorName = GSS_C_NO_NAME;
    gss_cred_id_t remCred = GSS_C_NO_CREDENTIAL;

    gss_buffer_desc inputToken = GSS_C_EMPTY_BUFFER;

    gss_buffer_t itPtr = &inputToken;
    
    gss_buffer_desc outputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t otPtr = &outputToken;

    gss_OID mechType = GSS_C_NO_OID;
    OM_uint32 timeRet;

    OM_uint32 minStat2;
    int tknStat;

    int ret = -1;

    if (remoteCredPtr != NULL) {
	*remoteCredPtr = GSS_C_NO_CREDENTIAL;
    }
    *scPtr = GSS_C_NO_CONTEXT;

    do {
	tknStat = gfarmGssReceiveToken(fd, itPtr);
	if (tknStat <= 0) {
	    majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_READ;
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
		majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
	    }
	}

	if (GSS_ERROR(majStat)) {
	    if (*scPtr != GSS_C_NO_CONTEXT) {
		(void)gss_delete_sec_context(&minStat2, scPtr, GSS_C_NO_BUFFER);
		break;
	    }
	}

    } while (majStat & GSS_S_CONTINUE_NEEDED);

    if (itPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, itPtr);
    }
    if (otPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, otPtr);
    }

    if (majStat == GSS_S_COMPLETE &&
	(retFlag & GSS_C_ANON_FLAG) == 0) {
	if (initiatorName != GSS_C_NO_NAME) {
	    char *name = gssName2Str(initiatorName);
	    if (name != NULL) {
#if GLOBUS_BUG
		/*
		 * From the RFC 2744, delegated credential is only
		 * valid when GSS_C_DELEG_FLAG is true, however, this
		 * is not satisfied at least in the Globus-2.2.x
		 * GSSAPI library.
		 * Although this was satisfied in the Globus-1.x once.
		 */
#else /* !GLOBUS_BUG */
		if ((retFlag & GSS_C_DELEG_FLAG) == GSS_C_DELEG_FLAG) {
#endif /* !GLOBUS_BUG */
		    /*
		     * If a credential is delegeted from the initiator,
		     * Check a name of the delegated credential.
		     */
		    if (remCred != GSS_C_NO_CREDENTIAL) {
			char *cName = gfarmGssGetCredentialName(remCred);
			if (cName != NULL) {
			    if (strcmp(name, cName) == 0) {
				/*
				 * Only valid if the name of sec-context and
				 * the name of delegated credential are
				 * identical.
				 */
				ret = 1;
				if (remoteCredPtr != NULL) {
				    *remoteCredPtr = remCred;
				}
			    }
			    (void)free(cName);
			}
		    }
#if GLOBUS_BUG
		    else {
			/*
			 * Only valid when the sec-context name is got.
			 */
			ret = 1;
		    }
#else /* !GLOBUS_BUG */
		} else {
		    /*
		     * Only valid when the sec-context name is got.
		     */
		    ret = 1;
		}
#endif /* !GLOBUS_BUG */
	    }
	    if (remoteNamePtr != NULL) {
		*remoteNamePtr = name;
	    } else {
		(void)free(name);
	    }
	}
	if (ret != 1) {
	    majStat = GSS_S_UNAUTHORIZED;
	}
    }

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }
    
    if (initiatorName != GSS_C_NO_NAME) {
	(void)gss_release_name(&minStat2, &initiatorName);
    }
	
    if (ret == -1) {
	if (*scPtr != GSS_C_NO_CONTEXT) {
	    (void)gss_delete_sec_context(&minStat2, scPtr, GSS_C_NO_BUFFER);
	}
    }

    return ret;
}


int
gfarmGssInitiateSecurityContext(fd, cred, reqFlag, scPtr, majStatPtr, minStatPtr,remoteNamePtr)
     int fd;
     gss_cred_id_t cred;
     OM_uint32 reqFlag;
     gss_ctx_id_t *scPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     char **remoteNamePtr;
{
    OM_uint32 majStat;
    OM_uint32 minStat;
    OM_uint32 retFlag = 0;
    gss_name_t acceptorName = GSS_C_NO_NAME;

    gss_buffer_desc inputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t itPtr = &inputToken;
    
    gss_buffer_desc outputToken = GSS_C_EMPTY_BUFFER;
    gss_buffer_t otPtr = &outputToken;

    gss_OID *actualMechType = NULL;
    OM_uint32 timeRet;

    OM_uint32 minStat2;
    int tknStat;

    int ret = -1;

    *scPtr = GSS_C_NO_CONTEXT;

    /*
     * Implementation specification:
     * In gfarm, an initiator must reveal own identity to an acceptor.
     */
    reqFlag &= ~GSS_C_ANON_FLAG;

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
		majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
	    }
	}

	if (GSS_ERROR(majStat)) {
	    if (*scPtr != GSS_C_NO_CONTEXT) {
		(void)gss_delete_sec_context(&minStat2, scPtr, GSS_C_NO_BUFFER);
		break;
	    }
	}
    
	if (majStat & GSS_S_CONTINUE_NEEDED) {
	    tknStat = gfarmGssReceiveToken(fd, itPtr);
	    if (tknStat <= 0) {
		majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_READ;
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

    if (majStat == GSS_S_COMPLETE) {
	acceptorName = GSS_C_NO_NAME;
	(void)gss_inquire_context(&minStat2,
				  *scPtr,
				  NULL,
				  &acceptorName,
				  NULL,
				  NULL,
				  NULL,
				  NULL,
				  NULL);
	if (acceptorName != GSS_C_NO_NAME) {
	    char *name = gssName2Str(acceptorName);
	    if (name != NULL) {
		/* Only valid when the name is got. */
		ret = 1;
	    }
	    if (remoteNamePtr != NULL) {
		*remoteNamePtr = name;
	    } else {
		(void)free(name);
	    }
	    (void)gss_release_name(&minStat2, &acceptorName);
	}
	if (ret != 1) {
	    majStat = GSS_S_UNAUTHORIZED;
	}
    }

    if (majStatPtr != NULL) {
	*majStatPtr = majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = minStat;
    }

    if (ret == -1) {
	if (*scPtr != GSS_C_NO_CONTEXT) {
	    (void)gss_delete_sec_context(&minStat2, scPtr, GSS_C_NO_BUFFER);
	}
    }

    return ret;
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
    buf = (char *)malloc(sizeof(*buf) * n);
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
	env = malloc(sizeof(env_name) + strlen(filename));
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

    exportedCred = malloc(sizeof(*exportedCred));
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

static void
gssInitiateSecurityContextSwitch(state)
     struct gfarmGssInitiateSecurityContextState *state;
{
    OM_uint32 minStat2;
    struct timeval timeout;

    if (GSS_ERROR(state->majStat)) {
	if (state->sc != GSS_C_NO_CONTEXT) {
	    (void)gss_delete_sec_context(&minStat2, &state->sc,
					 GSS_C_NO_BUFFER);
	    return;
	}
    }

    if (state->majStat & GSS_S_CONTINUE_NEEDED) {
	timeout.tv_sec = GFARM_GSS_AUTH_TIMEOUT;
	timeout.tv_usec = 0;
	if (gfarm_eventqueue_add_event(state->q, state->readable, &timeout) == 0) {
	    /* go to gfarmGssInitiateSecurityContextRecieveToken() */
	    return;
	}
	state->majStat = GSS_S_FAILURE;
	state->minStat = GFSL_DEFAULT_MINOR_ERROR;
    } else {
	state->completed = 1;
    }
}

static void
gssInitiateSecurityContextNext(state)
     struct gfarmGssInitiateSecurityContextState *state;
{
    OM_uint32 minStat2;

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
	if (gfarm_eventqueue_add_event(state->q, state->writable, NULL) == 0) {
	    /* go to gfarmGssInitiateSecurityContextSendToken() */
	    return;
	}
	state->majStat = GSS_S_FAILURE;
	state->minStat = GFSL_DEFAULT_MINOR_ERROR;
    }

    gssInitiateSecurityContextSwitch(state);
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
	state->majStat = GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_WRITE;
    }

    gssInitiateSecurityContextSwitch(state);
    if (GSS_ERROR(state->majStat) || state->completed) {
	if (state->continuation != NULL)
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
	    state->majStat =
		GSS_S_DEFECTIVE_TOKEN|GSS_S_CALL_INACCESSIBLE_READ;
	} else {
	    gssInitiateSecurityContextNext(state);
	    if (!GSS_ERROR(state->majStat) && !state->completed) {
		/* possibly go to gfarmGssInitiateSecurityContextSendToken() */
		return;
	    }
	}
    }
    assert(GSS_ERROR(state->majStat) || state->completed);
    if (state->continuation != NULL)
	(*state->continuation)(state->closure);
}

struct gfarmGssInitiateSecurityContextState *
gfarmGssInitiateSecurityContextRequest(q, fd, cred, reqFlag, continuation, closure, majStatPtr, minStatPtr)
     struct gfarm_eventqueue *q;
     int fd;
     gss_cred_id_t cred;
     OM_uint32 reqFlag;
     void (*continuation)(void *);
     void *closure;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
{
    struct gfarmGssInitiateSecurityContextState *state;

    state = malloc(sizeof(*state));
    if (state == NULL) {
	if (majStatPtr != NULL) {
	    *majStatPtr = GSS_S_FAILURE;
	}
	if (minStatPtr != NULL) {
	    *minStatPtr = GFSL_DEFAULT_MINOR_ERROR;
	}
	return (NULL);
    }

    state->completed = 0;
    state->majStat = GSS_S_COMPLETE;
    state->minStat = GFSL_DEFAULT_MINOR_ERROR;

    state->writable =
	gfarm_fd_event_alloc(GFARM_EVENT_WRITE, fd,
			     gfarmGssInitiateSecurityContextSendToken,
			     state);
    if (state->writable == NULL) {
	state->majStat = GSS_S_FAILURE;
	goto freeState;
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
	state->majStat = GSS_S_FAILURE;
	goto freeWritable;
    }

    state->q = q;
    state->fd = fd;
    state->cred = cred;
    state->reqFlag = reqFlag;
    state->continuation = continuation;
    state->closure = closure;

    state->retFlag = 0;
    state->acceptorName = GSS_C_NO_NAME;

    /* GSS_C_EMPTY_BUFFER */
    state->inputToken.length = 0; state->inputToken.value = NULL;
    state->itPtr = &state->inputToken;

    /* GSS_C_EMPTY_BUFFER */
    state->outputToken.length = 0; state->outputToken.value = NULL;
    state->otPtr = &state->outputToken;

    state->actualMechType = NULL;

    state->sc = GSS_C_NO_CONTEXT;

    /*
     * Implementation specification:
     * In gfarm, an initiator must reveal own identity to an acceptor.
     */
    state->reqFlag &= ~GSS_C_ANON_FLAG;

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
freeWritable:
    gfarm_event_free(state->writable);
freeState:
    if (majStatPtr != NULL) {
	*majStatPtr = state->majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = state->minStat;
    }
    free(state);
    return (NULL);
}

int
gfarmGssInitiateSecurityContextResult(state, scPtr, majStatPtr, minStatPtr, remoteNamePtr)
     struct gfarmGssInitiateSecurityContextState *state;
     gss_ctx_id_t *scPtr;
     OM_uint32 *majStatPtr;
     OM_uint32 *minStatPtr;
     char **remoteNamePtr;
{
    int ret = -1;
    OM_uint32 minStat2;

    assert(GSS_ERROR(state->majStat) || state->completed);

    if (state->itPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, state->itPtr);
    }
    if (state->otPtr->length > 0) {
	(void)gss_release_buffer(&minStat2, state->otPtr);
    }

    if (state->majStat == GSS_S_COMPLETE) {
	state->acceptorName = GSS_C_NO_NAME;
	(void)gss_inquire_context(&minStat2,
				  state->sc,
				  NULL,
				  &state->acceptorName,
				  NULL,
				  NULL,
				  NULL,
				  NULL,
				  NULL);
	if (state->acceptorName != GSS_C_NO_NAME) {
	    char *name = gssName2Str(state->acceptorName);
	    if (name != NULL) {
		/* Only valid when the name is got. */
		ret = 1;
	    }
	    if (remoteNamePtr != NULL) {
		*remoteNamePtr = name;
	    } else {
		(void)free(name);
	    }
	    (void)gss_release_name(&minStat2, &state->acceptorName);
	}
	if (ret != 1) {
	    state->majStat = GSS_S_UNAUTHORIZED;
	}
    }

    gfarm_event_free(state->readable);
    gfarm_event_free(state->writable);

    if (majStatPtr != NULL) {
	*majStatPtr = state->majStat;
    }
    if (minStatPtr != NULL) {
	*minStatPtr = state->minStat;
    }

    if (ret == -1) {
	if (state->sc != GSS_C_NO_CONTEXT) {
	    (void)gss_delete_sec_context(&minStat2, &state->sc,
					 GSS_C_NO_BUFFER);
	}
    }

    *scPtr = state->sc;

    free(state);
    return ret;
}
