/*
 * $Id$
 */

struct gfarm_hostspec;

char *gfarm_sockopt_config_add(char *, struct gfarm_hostspec *);
char *gfarm_sockopt_apply_by_name_addr(int, const char *, struct sockaddr *);
char *gfarm_sockopt_listener_config_add(char *);
char *gfarm_sockopt_apply_listener(int);
