enum gfarm_auth_error {
	GFARM_AUTH_ERROR_NO_ERROR,
	GFARM_AUTH_ERROR_DENIED,
	GFARM_AUTH_ERROR_NOT_SUPPORTED,
	GFARM_AUTH_ERROR_INVALID_CREDENTIAL,
	GFARM_AUTH_ERROR_EXPIRED
};

enum gfarm_auth_method {
	GFARM_AUTH_METHOD_NONE, /* never used */
	GFARM_AUTH_METHOD_SIMPLE,
	GFARM_AUTH_METHOD_GSI,

	GFARM_AUTH_METHOD_NUMBER
};

#define GFARM_AUTH_METHOD_ALL	GFARM_AUTH_METHOD_NONE

/*
 * GFARM_AUTH_METHOD_SIMPLE dependent constants.
 * 	note that this is too weak authentication for the Internet.
 */

void gfarm_auth_random(void *, size_t);
void gfarm_auth_simple_response_data(char *, char *, char *);

char *gfarm_auth_shared_key_get(unsigned int *, char *, char *, int);
#define GFARM_AUTH_SHARED_KEY_GET		0
#define GFARM_AUTH_SHARED_KEY_CREATE		1
#define GFARM_AUTH_SHARED_KEY_CREATE_FORCE	2

/* request */
enum gfarm_auth_simple_request {
	GFARM_AUTH_SIMPLE_GIVEUP,
	GFARM_AUTH_SIMPLE_MD5
};

/* key */
#define GFARM_AUTH_RETRY_MAX		2
#define GFARM_AUTH_SHARED_KEY_LEN	32
#define GFARM_AUTH_CHALLENGE_LEN	32
#define GFARM_AUTH_RESPONSE_LEN		16	/* length of MD5 */

struct xxx_connection;
struct sockaddr;

char *gfarm_auth_request(struct xxx_connection *, char *, struct sockaddr *);
char *gfarm_authorize(struct xxx_connection *, int, char **);

/* auth config */
struct gfarm_hostspec;

char *gfarm_auth_method_parse(char *, enum gfarm_auth_method *);
char *gfarm_auth_enable(enum gfarm_auth_method, struct gfarm_hostspec *);
char *gfarm_auth_disable(enum gfarm_auth_method, struct gfarm_hostspec *);

/* this i/f have to be changed, if we support more than 31 auth methods */
gfarm_int32_t gfarm_auth_method_get_enabled_by_name_addr(
	char *, struct sockaddr *);
gfarm_int32_t gfarm_auth_method_get_available(void);
