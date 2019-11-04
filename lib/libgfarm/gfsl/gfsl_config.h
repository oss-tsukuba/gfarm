#ifndef _GFARM_GFSL_CONFIG_H_
#define _GFARM_GFSL_CONFIG_H_

#define USE_GLOBUS

#if defined(USE_GLOBUS) && GLOBUS_FAKE_GSS_C_NT_USER
/* currently Globus doesn't actually support GSS_C_NT_USER_NAME */
#define GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS 1
#else
#define GFARM_FAKE_GSS_C_NT_USER_NAME_FOR_GLOBUS 0
#endif /*  defined(USE_GLOBUS) && GLOBUS_FAKE_GSS_C_NT_USER */

#ifdef USE_GLOBUS
/* draft-engert-ggf-gss-extensions @ IETF & draft-ggf-gss-extensions @ GGF */
# define GFARM_GSS_EXPORT_CRED_ENABLED	1
#else
# define GFARM_GSS_EXPORT_CRED_ENABLED	0
#endif /* USE_GLOBUS */

#if 0 /* defined(USE_GLOBUS) */ /* Now, Globus GSSAPI supports encryption */
/*
 * Exportable version of Globus GSSAPI did not support confidentiality
 * security service.
 */
# define GFARM_GSS_ENCRYPTION_ENABLED	0
#else
# define GFARM_GSS_ENCRYPTION_ENABLED	1
#endif /* USE_GLOBUS */

#if GFARM_GSS_ENCRYPTION_ENABLED
# define GFARM_GSS_C_CONF_FLAG	GSS_C_CONF_FLAG
#else
# define GFARM_GSS_C_CONF_FLAG	0
# ifdef USE_GLOBUS
#  ifndef GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG
#  define GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG	1
#  endif /* GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG */
#  define GFARM_GSS_DEFAULT_QOP	GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG
# endif /* USE_GLOBUS */
#endif /* GFARM_GSS_ENCRYPTION_ENABLED */

#ifndef GFARM_GSS_DEFAULT_QOP
#define GFARM_GSS_DEFAULT_QOP	GSS_C_QOP_DEFAULT
#endif

#ifdef USE_GLOBUS
/* GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG is deprecated since GT 4.0 */
#ifndef GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG
#define GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG 0
#endif
/* allow to delegate a limited proxy */
#define GFARM_GSS_C_GLOBUS_INIT_FLAG GSS_C_GLOBUS_LIMITED_DELEG_PROXY_FLAG
/* accept level N limited proxy certificate */
#define GFARM_GSS_C_GLOBUS_ACCEPT_FLAG GSS_C_GLOBUS_LIMITED_PROXY_MANY_FLAG
#else
#define GFARM_GSS_C_GLOBUS_INIT_FLAG 0
#define GFARM_GSS_C_GLOBUS_ACCEPT_FLAG 0
#endif /* USE_GLOBUS */

#define GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG \
	(OM_uint32)(GSS_C_DELEG_FLAG | 		/* delegation */ \
		    GSS_C_MUTUAL_FLAG |		/* mutual authentication */ \
		    GSS_C_REPLAY_FLAG |		/* reply message detection */ \
		    GSS_C_SEQUENCE_FLAG |	/* out of sequence detection */ \
		    GFARM_GSS_C_CONF_FLAG |	/* confidentiality service */ \
		    GSS_C_INTEG_FLAG |		/* integrity check service */ \
		    GFARM_GSS_C_GLOBUS_INIT_FLAG /* globus optional flag */ \
		    )

#define GFARM_GSS_DEFAULT_SECURITY_ACCEPT_FLAG \
	(OM_uint32)(GFARM_GSS_C_GLOBUS_ACCEPT_FLAG) /* globus optional flag */

#define GFARM_GSS_DEFAULT_MAX_MESSAGE_REQUEST_SIZE 8 * 1024 * 1024

#define GFARM_DEFAULT_INITIATOR_CONFIG_FILE	"gfarm-initiator.conf"
#define GFARM_DEFAULT_ACCEPTOR_CONFIG_FILE	"gfarm-acceptor.conf"

#define GFARM_DEFAULT_INSTALL_ETC_DIR		"etc"

#ifndef GFARM_INSTALL_ETC_DIR
#define GFARM_INSTALL_ETC_DIR	"/etc"
#endif /* GFARM_INSTALL_ETC_DIR */

#ifndef GFARM_INSTALL_DIR_ENV
#define GFARM_INSTALL_DIR_ENV	"GFARM_HOME"
#endif /* GFARM_INSTALL_DIR_ENV */

#define GFARM_DEFAULT_USERMAP_FILE		"gfarm-usermap"

/* XXX - This depends on globus implementation - GLOBUS_SUCCESS (== 0) */
#define GFSL_DEFAULT_MINOR_ERROR 0

#define GFARM_GSS_AUTH_TIMEOUT	60	/* seconds */
#define GFARM_GSS_AUTH_TIMEOUT_MSEC	(GFARM_GSS_AUTH_TIMEOUT * 1000)
#define GFARM_GSS_TIMEOUT_INFINITE	-1

#endif /* _GFARM_GFSL_CONFIG_H_ */

