enum gfarm_auth_error {
	GFARM_AUTH_ERROR_NO_ERROR,
	GFARM_AUTH_ERROR_DENIED,
	GFARM_AUTH_ERROR_NOT_SUPPORTED,
	GFARM_AUTH_ERROR_INVALID_CREDENTIAL,
	GFARM_AUTH_ERROR_EXPIRED,
	GFARM_AUTH_ERROR_RESOURCE_UNAVAILABLE,
};

enum gfarm_auth_method {
	GFARM_AUTH_METHOD_NONE, /* never used */
	GFARM_AUTH_METHOD_SHAREDSECRET,
	GFARM_AUTH_METHOD_GSI_OLD, /* not supported anymore */
	GFARM_AUTH_METHOD_GSI,

	GFARM_AUTH_METHOD_NUMBER
};

#define GFARM_AUTH_METHOD_ALL	GFARM_AUTH_METHOD_NONE

#define GFARM_AUTH_TIMEOUT	60	/* seconds */

/*
 * GFARM_AUTH_METHOD_SHAREDSECRET dependent constants.
 * 	note that this is too weak authentication for the Internet.
 */

void gfarm_random_initialize(void);
void gfarm_auth_random(void *, size_t);
void gfarm_auth_sharedsecret_response_data(char *, char *, char *);

char *gfarm_auth_shared_key_get(unsigned int *, char *, char *, int);
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

/* auth_client */

struct xxx_connection;
struct sockaddr;
struct gfarm_eventqueue;
struct gfarm_auth_request_state;

char *gfarm_authorize_log_connected(struct xxx_connection *, char *, char *);
char *gfarm_auth_request(struct xxx_connection *, char *, struct sockaddr *,
	enum gfarm_auth_method *);
char *gfarm_auth_request_multiplexed(struct gfarm_eventqueue *,
	struct xxx_connection *, char *, struct sockaddr *,
	void (*)(void *), void *,
	struct gfarm_auth_request_state **);
char *gfarm_auth_result_multiplexed(struct gfarm_auth_request_state *,
	enum gfarm_auth_method *);
char *gfarm_authorize(struct xxx_connection *, int, char **, char **,
	enum gfarm_auth_method *);

/* auth config */
struct gfarm_hostspec;

char gfarm_auth_method_mnemonic(enum gfarm_auth_method);
char *gfarm_auth_method_name(enum gfarm_auth_method);
char *gfarm_auth_method_parse(char *, enum gfarm_auth_method *);
char *gfarm_auth_enable(enum gfarm_auth_method, struct gfarm_hostspec *);
char *gfarm_auth_disable(enum gfarm_auth_method, struct gfarm_hostspec *);

/* this i/f have to be changed, if we support more than 31 auth methods */
gfarm_int32_t gfarm_auth_method_get_enabled_by_name_addr(
	char *, struct sockaddr *);
gfarm_int32_t gfarm_auth_method_get_available(void);

/* auth_client_sharedsecret */
char *gfarm_auth_request_sharedsecret(struct xxx_connection *);
char *gfarm_auth_request_sharedsecret_multiplexed(struct gfarm_eventqueue *,
	struct xxx_connection *, void (*)(void *), void *,
	void **);
char *gfarm_auth_result_sharedsecret_multiplexed(void *);

/* auth_client_gsi */
char *gfarm_auth_request_gsi(struct xxx_connection *);
char *gfarm_auth_request_gsi_multiplexed(struct gfarm_eventqueue *,
	struct xxx_connection *, void (*)(void *), void *,
	void **);
char *gfarm_auth_result_gsi_multiplexed(void *);

/* auth_server_sharedsecret */
char *gfarm_authorize_sharedsecret(struct xxx_connection *, int, char *,
        char **);

char *gfarm_authorize_gsi(struct xxx_connection *, int, char *, char **);
/* auth_server_gsi */
