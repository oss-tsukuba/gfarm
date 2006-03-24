#ifndef _GFARM_GSI_H_
#define _GFARM_GSI_H_

/* only available on GFARM_GSS_EXPORT_CRED_ENABLED case */
typedef struct gfarmExportedCredential gfarmExportedCredential;

/*
 * Prototype
 */
extern char **	gfarmGssCrackMajorStatus(OM_uint32 majStat);
extern char **	gfarmGssCrackMinorStatus(OM_uint32 minStat);
extern void	gfarmGssFreeCrackedStatus(char **strPtr);
extern void	gfarmGssPrintMajorStatus(OM_uint32 majStat);
extern void	gfarmGssPrintMinorStatus(OM_uint32 minStat);

extern int	gfarmGssSendToken(int fd, gss_buffer_t gsBuf);
extern int	gfarmGssReceiveToken(int fd, gss_buffer_t gsBuf);

extern int	gfarmGssImportName(gss_name_t *namePtr,
				   void *nameValue,
				   size_t nameLength,
				   gss_OID nameType,
				   OM_uint32 *majStatPtr,
				   OM_uint32 *minStatPtr);
extern int	gfarmGssImportNameOfHostBasedService(gss_name_t *namePtr,
						     char *service,
						     char *hostname,
						     OM_uint32 *majStatPtr,
						     OM_uint32 *minStatPtr);
extern int	gfarmGssImportNameOfHost(gss_name_t *namePtr, char *hostname,
					 OM_uint32 *majStatPtr,
					 OM_uint32 *minStatPtr);
extern int	gfarmGssDeleteName(gss_name_t *namePtr,
				   OM_uint32 *majStatPtr,
				   OM_uint32 *minStatPtr);
extern int	gfarmGssNewCredentialName(gss_name_t *outputNamePtr,
					  gss_cred_id_t cred,
					  OM_uint32 *majStatPtr,
					  OM_uint32 *minStatPtr);
extern char *	gfarmGssNewDisplayName(const gss_name_t inputName,
				       OM_uint32 *majStatPtr,
				       OM_uint32 *minStatPtr,
				       gss_OID *outputNameTypePtr);

extern int	gfarmGssAcquireCredential(gss_cred_id_t *credPtr,
					  const gss_name_t desiredName,
					  gss_cred_usage_t credUsage,
					  OM_uint32 *majStatPtr,
  					  OM_uint32 *minStatPtr,
					  gss_name_t *credNamePtr);
extern int	gfarmGssDeleteCredential(gss_cred_id_t *credPtr,
					 OM_uint32 *majStatPtr,
					 OM_uint32 *minStatPtr);

extern int	gfarmGssAcceptSecurityContext(int fd,
					      gss_cred_id_t cred,
					      gss_ctx_id_t *scPtr,
					      OM_uint32 *majStatPtr,
     					      OM_uint32 *minStatPtr,
					      gss_name_t *remoteNamePtr,
					      gss_cred_id_t *remoteCredPtr);
extern int	gfarmGssInitiateSecurityContext(int fd,
						const gss_name_t acceptorName,
						gss_cred_id_t cred,
						OM_uint32 reqFlag,
						gss_ctx_id_t *scPtr,
						OM_uint32 *majStatPtr,
						OM_uint32 *minSstatPtr,
						gss_name_t *remoteNamePtr);

extern void	gfarmGssDeleteSecurityContext(gss_ctx_id_t *scPtr);

extern int	gfarmGssConfigureMessageSize(gss_ctx_id_t sCtx,
					     int doEncrypt,
					     gss_qop_t qopReq,
					     unsigned int reqOutSz,
					     unsigned int *maxInSzPtr,
					     OM_uint32 *majStatPtr,
					     OM_uint32 *minStatPtr);

extern int	gfarmGssSend(int fd, gss_ctx_id_t sCtx,
			     int doEncrypt,
			     gss_qop_t qopReq,
			     gfarm_int8_t *buf, int n, int chunkSz,
			     OM_uint32 *statPtr);
extern int	gfarmGssReceive(int fd, gss_ctx_id_t sCtx,
				gfarm_int8_t **bufPtr, int *lenPtr,
				OM_uint32 *statPtr);

/* multiplexed version */

struct gfarm_eventqueue;
struct gfarmGssInitiateSecurityContextState;

extern struct gfarmGssInitiateSecurityContextState *
		gfarmGssInitiateSecurityContextRequest(
			struct gfarm_eventqueue *q,
			int fd,
			const gss_name_t acceptorName,
			gss_cred_id_t cred, OM_uint32 reqFlag,
			void (*continuation)(void *), void *closure,
			OM_uint32 *majStatPtr, OM_uint32 *minStatPtr);
extern int	gfarmGssInitiateSecurityContextResult(
			struct gfarmGssInitiateSecurityContextState *state,
			gss_ctx_id_t *scPtr, 
			OM_uint32 *majStatPtr, OM_uint32 *minStatPtr,
			gss_name_t *remoteNamePtr);

/* only available on GFARM_GSS_EXPORT_CRED_ENABLED case */
extern gfarmExportedCredential *
		gfarmGssExportCredential(gss_cred_id_t cred,
					 OM_uint32 *statPtr);
extern char *	gfarmGssEnvForExportedCredential(
			gfarmExportedCredential *exportedCred);
extern void	gfarmGssDeleteExportedCredential(
			gfarmExportedCredential *exportedCred,
			int sigHandler);

#endif /* _GFARM_GSI_H_ */
