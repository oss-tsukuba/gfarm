/*
 * Non-standard errors. Gfarm library internal use only.
 * Compatibility shouldn't be assumed,
 * so, these errors shouldn't be used via network protocol.
 *
 */
enum gfarm_errmsg {
	GFARM_ERRMSG_BEGIN = 4096, /* must be > GFARM_ERR_NUMBER */
	GFARM_ERRMSG_INCONSISTENT_RECOVERABLE = GFARM_ERRMSG_BEGIN,
	GFARM_ERRMSG_TOO_MANY_ERROR_DOMAIN,

	GFARM_ERRMSG_UNKNOWN_KEYWORD,
	GFARM_ERRMSG_UNKNOWN_AUTH_METHOD,

	/* refered only from gfarm/hostspec.c */
	GFARM_ERRMSG_HOSTNAME_OR_IP_EXPECTED,
	GFARM_ERRMSG_INVALID_CHAR_IN_HOSTNAME,
	GFARM_ERRMSG_INVALID_CHAR_IN_IP,
	GFARM_ERRMSG_TOO_BIG_BYTE_IN_IP,
	GFARM_ERRMSG_IP_ADDRESS_EXPECTED,
	GFARM_ERRMSG_IP_ADDRESS_TOO_SHORT,
	GFARM_ERRMSG_TOO_BIG_NETMASK,
	GFARM_ERRMSG_INVALID_NAME_RECORD,
	GFARM_ERRMSG_REVERSE_LOOKUP_NAME_IS_NOT_RESOLVABLE,
	GFARM_ERRMSG_REVERSE_LOOKUP_NAME_DOES_NOT_MATCH,

	/* refered only from gfarm/param.c */
	GFARM_ERRMSG_VALUE_IS_NOT_SPECIFIED,
	GFARM_ERRMSG_VALUE_IS_EMPTY,
	GFARM_ERRMSG_VALUE_IS_NOT_ALLOWED_FOR_BOOLEAN,
	GFARM_ERRMSG_INVALID_CHAR_IN_VALUE,
	GFARM_ERRMSG_UNKNOWN_PARAMETER,

	/* refered only from gfarm/sockopt.c */
	GFARM_ERRMSG_GETPROTOBYNAME_FAILED,
	GFARM_ERRMSG_UNKNOWN_SOCKET_OPTION,

	/* refered only from gfarm/config.c */
	GFARM_ERRMSG_MISSING_ARGUMENT,
	GFARM_ERRMSG_TOO_MANY_ARGUMENTS,
	GFARM_ERRMSG_INTEGER_EXPECTED,
	GFARM_ERRMSG_FLOATING_POINT_NUMBER_EXPECTED,
	GFARM_ERRMSG_INVALID_CHARACTER,
	GFARM_ERRMSG_ENABLED_OR_DISABLED_EXPECTED,
	GFARM_ERRMSG_INVALID_SYSLOG_PRIORITY_LEVEL,
	GFARM_ERRMSG_LOCAL_USER_REDEFIEND,
	GFARM_ERRMSG_GLOBAL_USER_REDEFIEND,
	GFARM_ERRMSG_LOCAL_GROUP_REDEFIEND,
	GFARM_ERRMSG_GLOBAL_GROUP_REDEFIEND,
	GFARM_ERRMSG_MISSING_LOCAL_USER,
	GFARM_ERRMSG_DB_ACCESS_TYPE_IS_NONE,
	GFARM_ERRMSG_BACKEND_ALREADY_LDAP,
	GFARM_ERRMSG_BACKEND_ALREADY_POSTGRESQL,
	GFARM_ERRMSG_BACKEND_ALREADY_LOCALFS,
	GFARM_ERRMSG_UNTERMINATED_SINGLE_QUOTE,
	GFARM_ERRMSG_UNTERMINATED_DOUBLE_QUOTE,
	GFARM_ERRMSG_INCOMPLETE_ESCAPE,
	GFARM_ERRMSG_MISSING_1ST_AUTH_COMMAND_ARGUMENT,
	GFARM_ERRMSG_MISSING_2ND_AUTH_METHOD_ARGUMENT,
	GFARM_ERRMSG_MISSING_3RD_HOST_SPEC_ARGUMENT,
	GFARM_ERRMSG_UNKNOWN_AUTH_SUBCOMMAND,
	GFARM_ERRMSG_MISSING_NETPARAM_OPTION_ARGUMENT,
	GFARM_ERRMSG_MISSING_SOCKOPT_OPTION_ARGUMENT,
	GFARM_ERRMSG_MISSING_ADDRESS_ARGUMENT,
	GFARM_ERRMSG_MISSING_USER_MAP_FILE_ARGUMENT,
	GFARM_ERRMSG_MISSING_GROUP_MAP_FILE_ARGUMENT,
	GFARM_ERRMSG_MISSING_1ST_ARCHITECTURE_ARGUMENT,
	GFARM_ERRMSG_MISSING_2ND_HOST_SPEC_ARGUMENT,
	GFARM_ERRMSG_CANNOT_OPEN_CONFIG,

	/* refered only from gfarm/gfp_xdr.c */
	GFARM_ERRMSG_GFP_XDR_SEND_INVALID_FORMAT_CHARACTER,
	GFARM_ERRMSG_GFP_XDR_RECV_INVALID_FORMAT_CHARACTER,
	GFARM_ERRMSG_GFP_XDR_VRPC_MISSING_RESULT_IN_FORMAT_STRING,
	GFARM_ERRMSG_GFP_XDR_VRPC_INVALID_FORMAT_CHARACTER,

	/* refered only from gfarm/auth_common.c */
	GFARM_ERRMSG_SHAREDSECRET_INVALID_EXPIRE_FIELD,
	GFARM_ERRMSG_SHAREDSECRET_INVALID_KEY_FIELD,
	GFARM_ERRMSG_SHAREDSECRET_KEY_FILE_NOT_EXIST,

	/* refered only from gfarm/auth_client.c */
	GFARM_ERRMSG_AUTH_METHOD_NOT_AVAILABLE_FOR_THE_HOST,
	GFARM_ERRMSG_USABLE_AUTH_METHOD_IS_NOT_CONFIGURED,
	GFARM_ERRMSG_AUTH_REQUEST_SHAREDSECRET_IMPLEMENTATION_ERROR,
	GFARM_ERRMSG_AUTH_REQUEST_IMPLEMENTATION_ERROR,
	GFARM_ERRMSG_AUTH_REQUEST_SHAREDSECRET_MULTIPLEXED_IMPLEMENTATION_ERROR,
	GFARM_ERRMSG_AUTH_REQUEST_MULTIPLEXED_MPLEMENTATION_ERROR,

	/* refered only from gfarm/auth_server.c */
	GFARM_ERRMSG_AUTH_SHAREDSECRET_MD5_CONTINUE,

	/* refered only from gfarm/auth_client_gss.c */
	GFARM_ERRMSG_GSS_CREDENTIAL_INITIALIZATION_FAILED,
	GFARM_ERRMSG_GSS_INITIALIZATION_FAILED,
	GFARM_ERRMSG_CANNOT_INITIATE_GSS_CONNECTION,
	GFARM_ERRMSG_CANNOT_ACQUIRE_CLIENT_CRED,

	/* refered only from gfarm/auth_config.c */
	GFARM_ERRMSG_UNKNOWN_CREDENTIAL_TYPE,

	/* refered only from gfarm/io_gfsl.c */
	GFARM_ERRMSG_GSI_DELEGATED_CREDENTIAL_NOT_EXIST,	/* obsolete */
	GFARM_ERRMSG_GSI_DELEGATED_CREDENTIAL_CANNOT_EXPORT,	/* obsolete */

	/* refered only from gfarm/gfs_client.c */
	GFARM_ERRMSG_GFSD_ABORTED,
	GFARM_ERRMSG_GFSD_DESCRIPTOR_ILLEGAL,
	GFARM_ERRMSG_GFSD_REPLY_UNKNOWN,
	GFARM_ERRMSG_GFS_PROTO_PREAD_PROTOCOL,
	GFARM_ERRMSG_GFS_PROTO_PWRITE_PROTOCOL,

	/* refered from gfarm/gfs_pio.c and related modules */
	GFARM_ERRMSG_GFS_PIO_IS_EOF,

	/* refered only from gfarm/glob.c */
	GFARM_ERRMSG_GFS_GLOB_NOT_PROPERLY_INITIALIZED,

	/* OBSOLETE: should be able to be removed, but kept for safety */
	GFARM_ERRMSG_NO_FILESYSTEM_NODE_OBSOLETE,

	/* refered only from gfarm/auth_common_gss.c */
	GFARM_ERRMSG_CRED_TYPE_DEFAULT_INVALID_CRED_NAME,
	GFARM_ERRMSG_CRED_TYPE_DEFAULT_INVALID_CRED_SERVICE,
	GFARM_ERRMSG_CRED_TYPE_DEFAULT_INTERNAL_ERROR,
	GFARM_ERRMSG_CRED_TYPE_NO_NAME_INVALID_CRED_NAME,
	GFARM_ERRMSG_CRED_TYPE_NO_NAME_INVALID_CRED_SERVICE,
	GFARM_ERRMSG_CRED_TYPE_MECHANISM_SPECIFIC_INVALID_CRED_NAME,
	GFARM_ERRMSG_CRED_TYPE_MECHANISM_SPECIFIC_INVALID_CRED_SERVICE,
	GFARM_ERRMSG_CRED_TYPE_USER_CRED_INVALID_CRED_SERVICE,
	GFARM_ERRMSG_CRED_TYPE_SELF_CRED_INVALID_CRED_NAME,
	GFARM_ERRMSG_CRED_TYPE_SELF_CRED_INVALID_CRED_SERVICE,
	GFARM_ERRMSG_CRED_TYPE_SELF_NOT_INITIALIZED_AS_AN_INITIATOR,
	GFARM_ERRMSG_INVALID_CRED_TYPE,
	GFARM_ERRMSG_INVALID_CREDENTIAL_CONFIGURATION,

	/* refered only from gfarm/import_help.c */
	GFARM_ERRMSG_HOSTNAME_EXPECTED,
	GFARM_ERRMSG_EMPTY_FILE,

	/* refered only from gfarm/config.c */
	GFARM_ERRMSG_INVALID_DIGEST_TYPE,

	/*
	 * refered only from gfarm/tls_funcs.h
	 *
	 * deprecated, but DO NOT REMOVE
	 * because these may be transfered via network in gfarm-2.8.0
	 */
	GFARM_ERRMSG_OLD_TLS_PRIVATE_KEY_FILE_PERMISSION_TOO_WIDELY_OPEN,
	GFARM_ERRMSG_OLD_TLS_PRIVATE_KEY_FILE_ABOUT_TO_BE_OPENED_BY_OTHERS,
	GFARM_ERRMSG_OLD_TLS_PRIVATE_KEY_READ_FAILURE,
	GFARM_ERRMSG_OLD_TLS_INVALID_CIPHER,
	GFARM_ERRMSG_OLD_TLS_CERT_VERIFIY_FAILURE,

	GFARM_ERRMSG_END
};

/*
 * Dynamically assigned error code for foreign systems (e.g. UNIX, LDAP, ...).
 * Compatibility shouldn't be assumed,
 * so, these errors shouldn't be used via network protocol.
 * All values will become > GFARM_ERR_FOREIGN_BEGIN
 * This range can be allocated by gfarm_error_domain_alloc().
 */
#define GFARM_ERR_FOREIGN_BEGIN	65536

/*
 * Statically assigned error code range for private implementations.
 * Since the range is statically assigned, it can be used via network protocol.
 * The range must be >= GFARM_ERR_PRIVATE_BEGIN
 * This range can be allocated by gfarm_error_range_alloc().
 */
#define GFARM_ERR_PRIVATE_BEGIN	0x70000000

struct gfarm_error_domain;

gfarm_error_t gfarm_error_domain_alloc(int, int,
	const char *(*)(void *, int), void *,
	struct gfarm_error_domain **);
gfarm_error_t gfarm_error_range_alloc(int, int,
	const char *(*)(void *, int), void *,
	struct gfarm_error_domain **);
gfarm_error_t gfarm_error_domain_add_map(struct gfarm_error_domain *,
	int, gfarm_error_t);
gfarm_error_t gfarm_error_domain_add_message(struct gfarm_error_domain *,
	int, const char *);
gfarm_error_t gfarm_error_domain_map(struct gfarm_error_domain *, int);
