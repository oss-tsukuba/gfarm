/*
 * $Id$
 */

struct gfarm_hostspec;
struct sockaddr;

gfarm_error_t gfarm_hostspec_parse(char *, struct gfarm_hostspec **);
void gfarm_hostspec_free(struct gfarm_hostspec *);
int gfarm_hostspec_match(struct gfarm_hostspec *, const char *,
	struct sockaddr *);

gfarm_error_t gfarm_sockaddr_to_name(struct sockaddr *, char **);

/* "[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]" */
#define GFARM_SOCKADDR_STRLEN	42
void gfarm_sockaddr_to_string(struct sockaddr *, char *, size_t);
