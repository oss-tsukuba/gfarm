/*
 * $Id$
 */

struct sockaddr;
struct gfarm_hostspec;
struct gfarm_param_config;

struct gfarm_param_type {
	char *name;
	int boolean;
	void *extension;
};

char *gfarm_param_config_parse_long(int, struct gfarm_param_type *, char *,
	int *, long *);
char *gfarm_param_config_add_long(struct gfarm_param_config ***,
	int, long, struct gfarm_hostspec *);
char *gfarm_param_apply_long_by_name_addr(struct gfarm_param_config *,
	const char *, struct sockaddr *, char *(*)(void *, int, long), void *);
char *gfarm_param_apply_long(struct gfarm_param_config *,
	char *(*)(void *, int, long), void *);
char *gfarm_param_get_long_by_name_addr(struct gfarm_param_config *, int,
	char *, struct sockaddr *, long *);
char *gfarm_param_get_long(struct gfarm_param_config *, int, long *);

/*
 * netparam
 */

struct gfarm_netparam_info;

char *gfarm_netparam_config_add_long(char *, struct gfarm_hostspec *);
char *gfarm_netparam_config_get_long(struct gfarm_netparam_info *,
	char *, struct sockaddr *, long *);

extern struct gfarm_netparam_info gfarm_netparam_parallel_streams;
extern struct gfarm_netparam_info gfarm_netparam_stripe_unit_size;
extern struct gfarm_netparam_info gfarm_netparam_rate_limit;
