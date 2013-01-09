/*
 * $Id$
 */

struct gfarm_hostspec;
struct sockaddr;

gfarm_error_t gfarm_hostspec_af_inet4_new(gfarm_uint32_t, gfarm_uint32_t,
    struct gfarm_hostspec **);
void gfarm_hostspec_free(struct gfarm_hostspec *);

gfarm_error_t gfarm_hostspec_parse(char *, struct gfarm_hostspec **);
void gfarm_hostspec_free(struct gfarm_hostspec *);
int gfarm_hostspec_match(struct gfarm_hostspec *, const char *,
	struct sockaddr *);

/* 41 "IPv6 address" + 1 '/' + 41 + 1 '\0' */
#define GFARM_HOSTSPEC_STRLEN	84
void gfarm_hostspec_to_string(struct gfarm_hostspec *, char *, size_t);

gfarm_error_t gfarm_sockaddr_to_name(struct sockaddr *, char **);

/* "[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]" */
#define GFARM_SOCKADDR_STRLEN	42
void gfarm_sockaddr_to_string(struct sockaddr *, char *, size_t);
