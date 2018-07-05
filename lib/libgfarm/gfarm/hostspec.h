/*
 * $Id$
 */

struct gfarm_hostspec;
struct gfarm_ifinfo;
struct sockaddr;

gfarm_error_t gfarm_hostspec_af_inet4_new(gfarm_uint32_t, gfarm_uint32_t,
	struct gfarm_hostspec **);
gfarm_error_t gfarm_hostspec_af_inet6_new(
	const unsigned char *, const unsigned char *,
	struct gfarm_hostspec **);
gfarm_error_t gfarm_hostspec_ifinfo_new(struct gfarm_ifinfo *,
    struct gfarm_hostspec **);
void gfarm_hostspec_free(struct gfarm_hostspec *);

gfarm_error_t gfarm_hostspec_parse(const char *, struct gfarm_hostspec **);
void gfarm_hostspec_free(struct gfarm_hostspec *);
int gfarm_hostspec_match(struct gfarm_hostspec *, const char *,
	struct sockaddr *);

#define GFARM_IN6_ADDR_LEN	16	/* octets */

/* 47 "IPv6 address" + 1 '/' + 47 + 1 '\0' */
#define GFARM_HOSTSPEC_STRLEN	96
void gfarm_hostspec_to_string(struct gfarm_hostspec *, char *, size_t);

gfarm_error_t gfarm_sockaddr_to_log_string(
	struct sockaddr *, int, char **);
