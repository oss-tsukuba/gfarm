#ifndef _GFARM_GFSL_CONFIG_H_
#define _GFARM_GFSL_CONFIG_H_

#define USE_GLOBUS

#if 0 /* defined(USE_GLOBUS) */ /* Now, Globus GSSAPI supports encryption */
/*
 * Globus GSSAPI does not support confidentiality security service.
 */
# define GFARM_GSS_ENCRYPTION_ENABLED	0
#else
# define GFARM_GSS_ENCRYPTION_ENABLED	1
#endif /* USE_GLOBUS */

#if GFARM_GSS_ENCRYPTION_ENABLED
#ifndef GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG
#define GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG	1
#endif /* GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG */
#define GFARM_GSS_DEFAULT_QOP	GSS_C_QOP_GLOBUS_GSSAPI_SSLEAY_BIG
#define GFARM_GSS_C_CONF_FLAG	0
#else
#define GFARM_GSS_DEFAULT_QOP	GSS_C_QOP_DEFAULT
#define GFARM_GSS_C_CONF_FLAG	GSS_C_CONF_FLAG
#endif /* USE_GLOBUS */

#define GFARM_GSS_DEFAULT_SECURITY_SETUP_FLAG \
	(OM_uint32)(GSS_C_DELEG_FLAG | 		/* delegation */ \
		    GSS_C_MUTUAL_FLAG |		/* mutual authentication */ \
		    GSS_C_REPLAY_FLAG |		/* reply message detection */ \
		    GSS_C_SEQUENCE_FLAG |	/* out of sequence detection */ \
		    GFARM_GSS_C_CONF_FLAG |	/* confidentiality service */ \
		    GSS_C_INTEG_FLAG		/* integrity check service */ \
		    )

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

#endif /* _GFARM_GFSL_CONFIG_H_ */

