#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>

#include "gssapi.h"

#include "gfsl_config.h"
#include "gfarm_gsi.h"
#include "gfarm_hash.h"
#include "tcputil.h"


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


char **
gfarmGssCrackStatus(majStat)
     OM_uint32 majStat;
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
				 majStat,
				 GSS_C_GSS_CODE,
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


void
gfarmGssPrintStatus(fd, majStat)
     FILE *fd;
     OM_uint32 majStat;
{
    char **list = gfarmGssCrackStatus(majStat);
    char **lP = list;
    if (*lP != NULL) {
	while (*lP != NULL) {
	    fprintf(fd, "%s\n", *lP++);
	}
    } else {
	fprintf(fd, "UNKNOWN\n");
    }
    fflush(fd);
    gfarmGssFreeCrackedStatus(list);
}


int
gfarmGssAcquireCredential(credPtr, credUsage, statPtr, credNamePtr)
     gss_cred_id_t *credPtr;
     gss_cred_usage_t credUsage;
     OM_uint32 *statPtr;
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
    if (statPtr != NULL) {
	*statPtr = majStat;
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
    int iLen = (int)(gsBuf->length);
    if (WriteLongs(fd, (long *)&iLen, 1) != 1) {
	return -1;
    }
    if (WriteBytes(fd, (char *)(gsBuf->value), iLen) != iLen) {
	return -1;
    }
    return iLen;
}


int
gfarmGssReceiveToken(fd, gsBuf)
     int fd;
     gss_buffer_t gsBuf;
{
    int iLen;
    char *buf = NULL;

    if (gsBuf->value != NULL) {
	OM_uint32 minStat;
	(void)gss_release_buffer(&minStat, gsBuf);
    }
    gsBuf->length = 0;
    gsBuf->value = NULL;

    if (ReadLongs(fd, (long *)&iLen, 1) != 1) {
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

    if (ReadBytes(fd, buf, iLen) != iLen) {
	(void)free(buf);
	return -1;
    }

    gsBuf->length = (size_t)iLen;
    gsBuf->value = (void *)buf;
    return iLen;
}


int
gfarmGssAcceptSecurityContext(fd, cred, scPtr, statPtr, remoteNamePtr, remoteCredPtr)
     int fd;
     gss_cred_id_t cred;
     gss_ctx_id_t *scPtr;
     OM_uint32 *statPtr;
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

    if (statPtr != NULL) {
	*statPtr = majStat;
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
gfarmGssInitiateSecurityContext(fd, cred, reqFlag, scPtr, statPtr, remoteNamePtr)
     int fd;
     gss_cred_id_t cred;
     OM_uint32 reqFlag;
     gss_ctx_id_t *scPtr;
     OM_uint32 *statPtr;
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
	    (void)gss_release_buffer(&minStat, otPtr);
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

    if (statPtr != NULL) {
	*statPtr = majStat;
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
gfarmGssConfigureMessageSize(sCtx, doEncrypt, qopReq, reqOutSz, maxInSzPtr, statPtr)
     gss_ctx_id_t sCtx;
     int doEncrypt;
     gss_qop_t qopReq;
     unsigned int reqOutSz;
     unsigned int *maxInSzPtr;
     OM_uint32 *statPtr;
{
    int ret = -1;

    OM_uint32 majStat;
    OM_uint32 minStat;

    OM_uint32 oReqSz = (OM_uint32)reqOutSz;
    OM_uint32 oMaxSz = oReqSz;

    majStat = gss_wrap_size_limit(&minStat, sCtx, doEncrypt, qopReq,
				  oReqSz, &oMaxSz);
    if (statPtr != NULL) {
	*statPtr = majStat;
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
     char *buf;
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

    if (WriteLongs(fd, (long *)&n, 1) != 1) {
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
     char **bufPtr;
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

    int n;
    int sum = 0;
    int rem;
    int len;
    char *buf = NULL;
    int i;

    /*
     * Receive a length of a PLAIN TEXT.
     *	XXXXX FIX ME:
     *		Generally it is wrong idea receiving a plain text
     *		length in plain text communication. Should be
     *		encrypted.
     */

    i = ReadLongs(fd, (long *)&n, 1);
    if (i == 0) {
	ret = 0;
	n = 0;
	majStat = GSS_S_COMPLETE;
	goto Done;
    } else if (i != 1) {
	majStat = GSS_S_CALL_INACCESSIBLE_READ;
	goto Done;
    }
    buf = (char *)malloc(sizeof(char) * n);
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

