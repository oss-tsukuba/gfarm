enum gfarm_auth_error {
	GFARM_AUTH_ERROR_NO_ERROR,
	GFARM_AUTH_ERROR_DENIED,
	GFARM_AUTH_ERROR_NOT_SUPPORTED,
	GFARM_AUTH_ERROR_INVALID_CREDENTIAL,
	GFARM_AUTH_ERROR_EXPIRED,
	GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE,
	GFARM_AUTH_ERROR_TEMPORARY_FAILURE, /* e.g. gfmd failover */
};

enum gfarm_auth_id_type {
	GFARM_AUTH_ID_TYPE_UNKNOWN,
	GFARM_AUTH_ID_TYPE_USER,
	GFARM_AUTH_ID_TYPE_SPOOL_HOST,
	GFARM_AUTH_ID_TYPE_METADATA_HOST,
};

enum gfarm_auth_method {
	GFARM_AUTH_METHOD_NONE, /* never used */
	GFARM_AUTH_METHOD_SHAREDSECRET,
	GFARM_AUTH_METHOD_GSI_OLD, /* not supported since 2003/07/09 */
	GFARM_AUTH_METHOD_GSI,
	GFARM_AUTH_METHOD_GSI_AUTH,
	GFARM_AUTH_METHOD_TLS_SHAREDSECRET,
	GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE,
	GFARM_AUTH_METHOD_KERBEROS,
	GFARM_AUTH_METHOD_KERBEROS_AUTH,
	GFARM_AUTH_METHOD_SASL,
	GFARM_AUTH_METHOD_SASL_AUTH,

	GFARM_AUTH_METHOD_NUMBER
};

enum gfarm_auth_cred_type {
	GFARM_AUTH_CRED_TYPE_DEFAULT,
	GFARM_AUTH_CRED_TYPE_NO_NAME,
	GFARM_AUTH_CRED_TYPE_MECHANISM_SPECIFIC,
	GFARM_AUTH_CRED_TYPE_HOST,
	GFARM_AUTH_CRED_TYPE_USER,
	GFARM_AUTH_CRED_TYPE_SELF
};

#define GFARM_AUTH_METHOD_ALL	GFARM_AUTH_METHOD_NONE

/* try next authentication method? */
#define GFARM_AUTH_ERR_TRY_NEXT_METHOD(e) ( \
	(e) == GFARM_ERR_PROTOCOL_NOT_SUPPORTED || \
	(e) == GFARM_ERR_EXPIRED || \
	(e) == GFARM_ERR_PERMISSION_DENIED || \
	(e) == GFARM_ERR_HOSTNAME_MISMATCH || \
	(e) == GFARM_ERR_AUTHENTICATION)

/*
 * GFARM_AUTH_METHOD_SHAREDSECRET dependent constants.
 * 	note that this is too weak authentication for the Internet.
 */

void gfarm_auth_random(void *, size_t);
gfarm_error_t gfarm_auth_sharedsecret_response_data(char *, char *, char *);

void gfarm_auth_root_squash_support_disable(void);
struct passwd;
gfarm_error_t gfarm_auth_shared_key_get(unsigned int *, char *,
	char *, struct passwd *, int, int);
#define GFARM_AUTH_SHARED_KEY_GET		0
#define GFARM_AUTH_SHARED_KEY_CREATE		1
#define GFARM_AUTH_SHARED_KEY_CREATE_FORCE	2

/* request */
enum gfarm_auth_sharedsecret_request {
	GFARM_AUTH_SHAREDSECRET_GIVEUP,
	GFARM_AUTH_SHAREDSECRET_MD5
};

/* key */
#define GFARM_AUTH_RETRY_MAX		2
#define GFARM_AUTH_SHARED_KEY_LEN	32
#define GFARM_AUTH_CHALLENGE_LEN	32
#define GFARM_AUTH_RESPONSE_LEN		16	/* length of MD5 */

#define GFARM_AUTH_SHARED_KEY_BASENAME	".gfarm_shared_key"
#define GFARM_AUTH_SHARED_KEY_PRINTNAME	"~/" GFARM_AUTH_SHARED_KEY_BASENAME

/* GSS */
struct gfarm_gss;
gfarm_error_t gfarm_gss_server_initialize(struct gfarm_gss *);
gfarm_error_t gfarm_gss_client_initialize(struct gfarm_gss *);

/* request (GSI does not use this due to protocol compatibility) */
enum gfarm_auth_gss_request {
	GFARM_AUTH_GSS_GIVEUP,
	GFARM_AUTH_GSS_CLIENT_TYPE
};

#define GFARM_IS_AUTH_GSS(auth) \
	((auth) == GFARM_AUTH_METHOD_GSI || \
	 (auth) == GFARM_AUTH_METHOD_GSI_AUTH || \
	 (auth) == GFARM_AUTH_METHOD_KERBEROS || \
	 (auth) == GFARM_AUTH_METHOD_KERBEROS_AUTH)
#define GFARM_IS_AUTH_GSI(auth) \
	((auth) == GFARM_AUTH_METHOD_GSI || \
	 (auth) == GFARM_AUTH_METHOD_GSI_AUTH)
#define GFARM_IS_AUTH_KERBEROS(auth) \
	((auth) == GFARM_AUTH_METHOD_KERBEROS || \
	 (auth) == GFARM_AUTH_METHOD_KERBEROS_AUTH)

/*
 * GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE dependent constants.
 */
/* request */
enum gfarm_auth_tls_client_certificate_request {
	GFARM_AUTH_TLS_CLIENT_CERTIFICATE_GIVEUP,
	GFARM_AUTH_TLS_CLIENT_CERTIFICATE_CLIENT_TYPE
};

#define GFARM_IS_AUTH_TLS_CLIENT_CERTIFICATE(auth) \
	((auth) == GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE)

/*
 * GFARM_AUTH_METHOD_SASL* dependent constants.
 */
enum gfarm_auth_sasl_step_type {
	GFARM_AUTH_SASL_STEP_ERROR,
	GFARM_AUTH_SASL_STEP_DONE,
	GFARM_AUTH_SASL_STEP_CONTINUE,
};

/* auth_client */

struct gfp_xdr;
struct sockaddr;
struct gfarm_eventqueue;
struct gfarm_auth_request_state;

gfarm_error_t gfarm_authorize_log_connected(struct gfp_xdr *, char *, char *);
gfarm_error_t gfarm_auth_request(struct gfp_xdr *,
	const char *, const char *, struct sockaddr *,
	enum gfarm_auth_id_type, const char *, struct passwd *,
	enum gfarm_auth_method *);
gfarm_error_t gfarm_auth_request_multiplexed(struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, struct sockaddr *,
	enum gfarm_auth_id_type, const char *, struct passwd *, int,
	void (*)(void *), void *, struct gfarm_auth_request_state **);
gfarm_error_t gfarm_auth_result_multiplexed(struct gfarm_auth_request_state *,
	enum gfarm_auth_method *);
gfarm_error_t gfarm_authorize_wo_setuid(struct gfp_xdr *, char *,
	char *, struct sockaddr *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **, enum gfarm_auth_method *);

/* default implementation of 6th argument of gfarm_authorize() */
gfarm_error_t gfarm_auth_uid_to_global_username(void *,
	enum gfarm_auth_method, const char *,
	enum gfarm_auth_id_type *, char **);
gfarm_error_t gfarm_auth_uid_to_global_username_panic(void *,
	const char *, enum gfarm_auth_id_type *, char **);

/* helper for auth_uid_to_global_username for host-based authentication case */
gfarm_error_t gfarm_x509_get_cn(const char *, char **);
gfarm_error_t gfarm_x509_cn_get_hostname(
	enum gfarm_auth_id_type, const char *, char **);
gfarm_error_t gfarm_x509_cn_get_service_hostname(
	const char *, const char *, char **);

gfarm_error_t gfarm_kerberos_principal_get_hostname(
	enum gfarm_auth_id_type, const char *, char **);

/* client side configuration */
gfarm_error_t gfarm_set_auth_id_type(enum gfarm_auth_id_type);
enum gfarm_auth_id_type gfarm_get_auth_id_type(void);

/* auth_config */
struct gfarm_hostspec;

const char *gfarm_auth_id_type_name(enum gfarm_auth_id_type);

char gfarm_auth_method_mnemonic(enum gfarm_auth_method);
char *gfarm_auth_method_name(enum gfarm_auth_method);
gfarm_error_t gfarm_auth_method_parse(char *, enum gfarm_auth_method *);
enum gfarm_auth_config_position {
	GFARM_AUTH_CONFIG_AT_HEAD,
	GFARM_AUTH_CONFIG_AT_TAIL,
	GFARM_AUTH_CONFIG_AT_MARK
};
gfarm_error_t gfarm_auth_enable(
	enum gfarm_auth_method, struct gfarm_hostspec *,
	enum gfarm_auth_config_position);
gfarm_error_t gfarm_auth_disable(
	enum gfarm_auth_method, struct gfarm_hostspec *,
	enum gfarm_auth_config_position);
void gfarm_auth_config_set_mark(void);

/* this i/f have to be changed, if we support more than 31 auth methods */
gfarm_int32_t gfarm_auth_method_get_enabled_by_name_addr(
	const char *, struct sockaddr *);
gfarm_int32_t gfarm_auth_client_method_get_available(enum gfarm_auth_id_type);
gfarm_int32_t gfarm_auth_server_method_get_available(void);

gfarm_error_t gfarm_auth_cred_type_parse(char *, enum gfarm_auth_cred_type *);
enum gfarm_auth_cred_type gfarm_auth_server_cred_type_get(const char *);
char *gfarm_auth_server_cred_service_get(const char *);
char *gfarm_auth_server_cred_name_get(const char *);
gfarm_error_t gfarm_auth_server_cred_type_set_by_string(char *, char *);
gfarm_error_t gfarm_auth_server_cred_type_set(char *,
	enum gfarm_auth_cred_type);
gfarm_error_t gfarm_auth_server_cred_service_set(char *, char *);
gfarm_error_t gfarm_auth_server_cred_name_set(char *, char *);

char *gfarm_auth_config_string_dup(void);

/* auth_client_sharedsecret */
gfarm_error_t gfarm_auth_request_sharedsecret_common(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *, int);
gfarm_error_t gfarm_auth_request_sharedsecret(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_sharedsecret_common_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, int,
	void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_request_sharedsecret_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_sharedsecret_multiplexed(void *);

/* auth_client_gss */
gfarm_error_t gfarm_auth_request_gss(struct gfp_xdr *, struct gfarm_gss *,
	const char *, const char *, int, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_gss_multiplexed(struct gfarm_eventqueue *,
	struct gfp_xdr *, struct gfarm_gss *,
	const char *, const char *, int, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_gss_multiplexed(void *);

/* auth_client_gss_auth */
gfarm_error_t gfarm_auth_request_gss_auth(struct gfp_xdr *, struct gfarm_gss *,
	const char *, const char *, int, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_gss_auth_multiplexed(
	struct gfarm_eventqueue *, struct gfp_xdr *, struct gfarm_gss *,
	const char *, const char *, int, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_gss_auth_multiplexed(void *);

/* auth_client_gsi */
gfarm_error_t gfarm_auth_request_gsi(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_gsi_multiplexed(struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_gsi_multiplexed(void *);

char *gfarm_gsi_client_cred_name(void);

/* auth_client_gsi_auth */
gfarm_error_t gfarm_auth_request_gsi_auth(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_gsi_auth_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_gsi_auth_multiplexed(void *);

/* auth_client_kerberos */
gfarm_error_t gfarm_auth_request_kerberos(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_kerberos_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_kerberos_multiplexed(void *);

char *gfarm_kerberos_client_cred_name(void);

/* auth_client_kerberos_auth */
gfarm_error_t gfarm_auth_request_kerberos_auth(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_kerberos_auth_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_kerberos_auth_multiplexed(void *);

/* auth_client_sasl */
gfarm_error_t gfarm_auth_request_sasl(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_sasl_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_sasl_multiplexed(void *);

/* auth_client_sasl_auth */
gfarm_error_t gfarm_auth_request_sasl_auth(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_sasl_auth_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_sasl_auth_multiplexed(void *);

int gfarm_auth_client_method_sasl_available(void);
int gfarm_sasl_log(void *, int, const char *);
int gfarm_sasl_addr_string(int, char *, size_t, char *, size_t, const char *);

/* auth_client_tls_sharedsecret */
gfarm_error_t gfarm_auth_request_tls_sharedsecret(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_tls_sharedsecret_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_tls_sharedsecret_multiplexed(void *);

/* auth_client_tls_client_certificate */

gfarm_error_t gfarm_auth_request_tls_client_certificate(struct gfp_xdr *,
	const char *, const char *, enum gfarm_auth_id_type, const char *,
	struct passwd *);
gfarm_error_t gfarm_auth_request_tls_client_certificate_multiplexed(
	struct gfarm_eventqueue *,
	struct gfp_xdr *, const char *, const char *, enum gfarm_auth_id_type,
	const char *, struct passwd *, int, void (*)(void *), void *, void **);
gfarm_error_t gfarm_auth_result_tls_client_certificate_multiplexed(void *);

gfarm_error_t gfarm_tls_server_cert_is_ok(
	struct gfp_xdr *, const char *, const char *);

/* auth_server_sharedsecret */
gfarm_error_t gfarm_authorize_sharedsecret_common(struct gfp_xdr *,
	char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	const char *, enum gfarm_auth_id_type *, char **);
gfarm_error_t gfarm_authorize_sharedsecret(struct gfp_xdr *,
	char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_gss */
gfarm_error_t gfarm_authorize_gss(struct gfp_xdr *, struct gfarm_gss *,
	char *, char *, int, enum gfarm_auth_method auth_method,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_gss_auth */
gfarm_error_t gfarm_authorize_gss_auth(struct gfp_xdr *, struct gfarm_gss *,
	char *, char *, int, enum gfarm_auth_method auth_method,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_gsi */
gfarm_error_t gfarm_authorize_gsi(struct gfp_xdr *, char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_gsi_auth */
gfarm_error_t gfarm_authorize_gsi_auth(struct gfp_xdr *, char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_kerberos */
gfarm_error_t gfarm_authorize_kerberos(struct gfp_xdr *, char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_kerberos_auth */
gfarm_error_t gfarm_authorize_kerberos_auth(struct gfp_xdr *, char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_sasl */
gfarm_error_t gfarm_authorize_sasl(struct gfp_xdr *, char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_sasl_auth */
gfarm_error_t gfarm_authorize_sasl_auth(struct gfp_xdr *, char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

void gfarm_sasl_server_init(void);
int gfarm_auth_server_method_sasl_available(void);

/* auth_server_tls_sharedsecret */
gfarm_error_t gfarm_authorize_tls_sharedsecret(struct gfp_xdr *,
	char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);

/* auth_server_tls_client_certificate */
gfarm_error_t gfarm_authorize_tls_client_certificate(struct gfp_xdr *,
	char *, char *,
	gfarm_error_t (*)(void *,
	    enum gfarm_auth_method, const char *, enum gfarm_auth_id_type *,
	    char **), void *,
	enum gfarm_auth_id_type *, char **);


/* gss_auth_error */
void gfarm_auth_set_gss_cred_failed(struct gfarm_gss *);
gfarm_error_t gfarm_auth_check_gss_cred_failed(void);
