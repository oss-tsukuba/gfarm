#ifndef _GFARM_GSI_H_
#define _GFARM_GSI_H_

/* only available on GFARM_GSS_EXPORT_CRED_ENABLED case */
typedef struct gfarmExportedCredential gfarmExportedCredential;

/*
 * Prototype
 */
extern char *	gfarmGssGetCredentialName(gss_cred_id_t cred);
extern char **	gfarmGssCrackMajorStatus(OM_uint32 majStat);
extern char **	gfarmGssCrackMinorStatus(OM_uint32 minStat);
extern void	gfarmGssFreeCrackedStatus(char **strPtr);
extern void	gfarmGssPrintMajorStatus(OM_uint32 majStat);
extern void	gfarmGssPrintMinorStatus(OM_uint32 minStat);

extern int	gfarmGssSendToken(int fd, gss_buffer_t gsBuf);
extern int	gfarmGssReceiveToken(int fd, gss_buffer_t gsBuf);

extern int	gfarmGssAcquireCredential(gss_cred_id_t *credPtr,
					  gss_cred_usage_t credUsage,
					  OM_uint32 *majStatPtr,
  					  OM_uint32 *minStatPtr,
					  char **credNamePtr);
extern int	gfarmGssAcceptSecurityContext(int fd,
					      gss_cred_id_t cred,
					      gss_ctx_id_t *scPtr,
					      OM_uint32 *majStatPtr,
     					      OM_uint32 *minStatPtr,
					      char **remoteNamePtr,
					      gss_cred_id_t *remoteCredPtr);
extern int	gfarmGssInitiateSecurityContext(int fd,
						gss_cred_id_t cred,
						OM_uint32 reqFlag,
						gss_ctx_id_t *scPtr,
						OM_uint32 *majStatPtr,
						OM_uint32 *minSstatPtr,
						char **remoteNamePtr);

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
			     char *buf, int n, int chunkSz,
			     OM_uint32 *statPtr);
extern int	gfarmGssReceive(int fd, gss_ctx_id_t sCtx,
				char **bufPtr, int *lenPtr,
				OM_uint32 *statPtr);

/* only available on GFARM_GSS_EXPORT_CRED_ENABLED case */
extern gfarmExportedCredential *
		gfarmGssExportCredential(gss_cred_id_t cred,
					 OM_uint32 *statPtr);
extern char *	gfarmGssEnvForExportedCredential(gfarmExportedCredential *exportedCred);
extern void	gfarmGssDeleteExportedCredential(gfarmExportedCredential *exportedCred);

#endif /* _GFARM_GSI_H_ */
