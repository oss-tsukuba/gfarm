#ifndef _GFARM_SECURE_SESSION_H_
#define _GFARM_SECURE_SESSION_H_

#include "gfsl_config.h"
#include "gfarm_gsi.h"
#include "gfarm_auth.h"

/*
 * Session information struct
 */
typedef struct gfarmSecSession {
    /*
     * Generic TCP transport information.
     */
    int fd;			/* File descriptor for the
				   session. */
    int needClose;		/* Need to close above fd or not
				   when terminate this session. */
    int rPort;			/* Port # of the peer. */
    char *peerName;		/* FQDN of the peer. Heap alloc'd. */

    /*
     * Credential information.
     */
    gss_cred_id_t cred;		/* The credential used for the
				   session. */
    gss_name_t credName;	/* Name of the credential.
				   Need to release. */
    gss_ctx_id_t sCtx;		/* A secure context for the session. */

    /*
     * Role specific information.
     */
    int iOa;			/* An initiator or an acceptor. */
#define GFARM_SS_INITIATOR	1
#define GFARM_SS_ACCEPTOR	2
    union {
	/*
	 * Initiator side session infomation.
	 */
	struct initiatorSessionInfo {
	    OM_uint32 reqFlag;		/* Security context
					   initiation flag. */
	    gss_name_t acceptorName;	/* Need to release */
	} initiator;

	/*
	 * Acceptor side session information.
	 */
	struct acceptorSessionInfo {
	    gfarmAuthEntry *mappedUser;	/* Authenticated
					   user information. */
	    gss_name_t initiatorName;	/* Need to release */
	    gss_cred_id_t deleCred;	/* A credential
					   delegated from
					   the initiator. */
	} acceptor;
    } iOaInfo;

    /*
     * Session configuration information.
     */
    gss_qop_t qOp;			/* Quality Of Protection
					 * for GSSAPI layer. */
    unsigned int maxTransSize;		/* Maximum transmission size
					   of the session in bytes. */
    unsigned int config;		/* Or'd value of belows. */
#define GFARM_SS_USE_ENCRYPTION		0x1
#define GFARM_SS_USE_COMPRESSION	0x2
#define GFARM_SS_USE_SYSTEMCONF		0x80000000

    /*
     * Poll status.
     */
    int pollEvent;			/* Or'd value of belows. */
#define GFARM_SS_POLL_NONE		0x0
#define GFARM_SS_POLL_READABLE		0x1
#define GFARM_SS_POLL_WRITABLE		0x2
#define GFARM_SS_POLL_ERROR		0x4

    /*
     * Session status.
     */
    OM_uint32 gssLastStat;		/* The last status of GSSAPI
					   invocation. */
} gfarmSecSession;

#define isBitSet(A, B) (((A) & (B)) == (B))


/*
 * GSSAPI and other transmission configuration information struct.
 */
typedef struct {
    unsigned int optMask;	/* Mask of which options are 
				   specified. Or'd belows: */
#define GFARM_SS_OPT_QOP_MASK	0x1
#define GFARM_SS_OPT_MAXT_MASK	0x2
#define GFARM_SS_OPT_CONF_MASK	0x4
#define GFARM_SS_OPT_ALL_MASK	0x7
    gss_qop_t qOpReq;		/* Requested QOP. */
    int qOpForce;		/* If 1 use the requested QOP forcible
				   otherwise negothiate with peer and
				   falldown to acceptable level. */

    unsigned int maxTransSizeReq;
    				/* Requested maximum transmission
				   size. */
    int maxTransSizeForce;	/* Force use above or not. */

    unsigned int configReq;	/* Any other configuration. */
    int configForce;		/* Force use above or not. */
} gfarmSecSessionOption;

#define GFARM_SS_DEFAULT_OPTION	\
{ \
  GFARM_SS_OPT_ALL_MASK, \
  GFARM_GSS_DEFAULT_QOP, \
  0, \
  GFARM_GSS_DEFAULT_MAX_MESSAGE_REQUEST_SIZE, \
  0, \
  GFARM_SS_USE_ENCRYPTION, \
  0 \
}

/* Authorization ACK/NACK */
#define GFARM_SS_AUTH_ACK	1
#define GFARM_SS_AUTH_NACK	0

/* Prototypes */

extern char **	gfarmSecSessionCrackStatus(gfarmSecSession *ssPtr);
extern void	gfarmSecSessionFreeCrackedStatus(char **strPtr);
extern void	gfarmSecSessionPrintStatus(gfarmSecSession *ssPtr);

extern int	gfarmSecSessionInitializeAcceptor(char *configFile,
						  char *usermapFile,
						  OM_uint32 *majStatPtr,
  						  OM_uint32 *minStatPtr);
extern int	gfarmSecSessionInitializeInitiator(char *configFile,
						   char *usermapFile,
						   OM_uint32 *majStatPtr,
						   OM_uint32 *minStatPtr);
extern int	gfarmSecSessionInitializeBoth(char *iConfigFile,
					      char *aConfigFile,
					      char *usermapFile,
					      OM_uint32 *majstatPtr,
					      OM_uint32 *minstatPtr);

extern void	gfarmSecSessionFinalizeAcceptor(void);
extern void	gfarmSecSessionFinalizeInitiator(void);
extern void	gfarmSecSessionFinalizeBoth(void);

extern gfarmSecSession *	gfarmSecSessionAccept(int fd,
						      gss_cred_id_t cred,
						      gfarmSecSessionOption *ssOptPtr,
						      int *gsiErrNoPtr,
						      OM_uint32 *majStatPtr,
						      OM_uint32 *minStatPtr);
extern gfarmSecSession *	gfarmSecSessionInitiate(int fd,
							const gss_name_t acceptorName,
							gss_cred_id_t cred,
							OM_uint32 reqFlag,
							gfarmSecSessionOption *ssOptPtr,
							int *gsiErrNoPtr,
							OM_uint32 *majStatPtr,
							OM_uint32 *minStatPtr);
extern gfarmSecSession *	gfarmSecSessionInitiateByAddr(unsigned long rAddr,
							int port,
							const gss_name_t acceptorName,
							gss_cred_id_t cred,
							OM_uint32 reqFlag,
							gfarmSecSessionOption *ssOptPtr,
							OM_uint32 *majStatPtr,
							OM_uint32 *minStatPtr);
extern gfarmSecSession *	gfarmSecSessionInitiateByName(char *hostname,
							int port,
							const gss_name_t acceptorName,
							gss_cred_id_t cred,
							OM_uint32 reqFlag,
							gfarmSecSessionOption *ssOptPtr,
							int *gsiErrNoPtr,
							OM_uint32 *majStatPtr,
							OM_uint32 *minStatPtr);
extern void			gfarmSecSessionTerminate(gfarmSecSession *ssPtr);

extern gss_cred_id_t		gfarmSecSessionGetDelegatedCredential(gfarmSecSession *ssPtr);

extern int			gfarmSecSessionGetInitiatorName(
							gfarmSecSession *ssPtr,
							gss_name_t *namePtr);

extern gfarmAuthEntry *		gfarmSecSessionGetInitiatorInfo(gfarmSecSession *ssPtr);

extern int			gfarmSecSessionDedicate(gfarmSecSession *ssPtr);

extern int			gfarmSecSessionSendInt32(gfarmSecSession *ssPtr,
							 gfarm_int32_t *buf,
							 int n);
extern int			gfarmSecSessionReceiveInt32(gfarmSecSession *ssPtr,
							    gfarm_int32_t **bufPtr,
							    int *lenPtr,
							    int timeoutMsec);

extern int			gfarmSecSessionSendInt16(gfarmSecSession *ssPtr,
							 gfarm_int16_t *buf,
							  int n);
extern int			gfarmSecSessionReceiveInt16(gfarmSecSession *ssPtr,
							    gfarm_int16_t **bufPtr,
							    int *lenPtr,
							    int timeoutMsec);

extern int			gfarmSecSessionSendInt8(gfarmSecSession *ssPtr,
							gfarm_int8_t *buf,
							int n);
extern int			gfarmSecSessionReceiveInt8(gfarmSecSession *ssPtr,
							   gfarm_int8_t **bufPtr,
							   int *lenPtr,
							   int timeoutMsec);

extern int			gfarmSecSessionPoll(gfarmSecSession *ssList[],
						    int n,
						    struct timeval *toPtr);
/*
 * gfarmSecSessionPoll() convinience macro(s).
 */
#define gfarmSecSessionCheckPollReadable(s) (isBitSet(s->pollEvent, GFARM_SS_POLL_READABLE))
#define gfarmSecSessionCheckPollWritable(s) (isBitSet(s->pollEvent, GFARM_SS_POLL_WRITABLE))
#define gfarmSecSessionCheckPollError(s) (isBitSet(s->pollEvent, GFARM_SS_POLL_WRITABLE))

#define gfarmSecSessionSetPollEvent(s, e) { s->pollEvent = (e); }
#define gfarmSecSessionClearPollEvent(s) gfarmSecSessionSetPollEvent(s, GFARM_SS_POLL_NONE)

#define gfarmSecSessionAddPollEvent(s, m) { s->pollEvent |= (m); }
#define gfarmSecSessionDeletePollEvent(s, d) { s->pollEvent &= ~(d); }

/*
 * multiplexed version
 */
struct gfarm_eventqueue;
struct gfarmSecSessionInitiateState;

extern struct gfarmSecSessionInitiateState *gfarmSecSessionInitiateRequest(
							struct gfarm_eventqueue *q,
							int fd,
							const gss_name_t acceptorName,
							gss_cred_id_t cred,
							OM_uint32 reqFlag,
							gfarmSecSessionOption *ssOptPtr,
							void (*continuation)(void *),
							void *closure,
							OM_uint32 *majStatPtr,
							OM_uint32 *minStatPtr);
extern gfarmSecSession *gfarmSecSessionInitiateResult(struct gfarmSecSessionInitiateState *state,
						      OM_uint32 *majStatPtr,
						      OM_uint32 *minStatPtr);

#endif /* _GFARM_SECURE_SESSION_H_ */
