struct gfm_connection;

gfarm_error_t gfarm_host_info_get_by_name_alias(
	struct gfm_connection *, const char *, struct gfarm_host_info *);

#if 0 /* XXX for now */
int gfarm_canonical_hostname_is_local(struct gfm_connection *, const char *);
#endif

int gfarm_host_is_local(struct gfm_connection *, const char *);

struct in_addr;
gfarm_error_t gfarm_get_ip_addresses(int *, struct in_addr **);

struct sockaddr;

#if 0 /* XXX for now */
struct gfarm_hostspec;

gfarm_error_t gfarm_host_address_use(struct gfarm_hostspec *);
#endif /* for now */

struct gfarm_host_info;
gfarm_error_t gfarm_host_info_address_get(struct gfm_connection *,
	const char *, int,
	struct gfarm_host_info *, struct sockaddr *, char **);

#if 0 /* XXX for now */
gfarm_error_t gfarm_set_client_architecture(char *, struct gfarm_hostspec *);
#endif /* for now */

int gfarm_addr_is_same_net(struct sockaddr *,
	struct sockaddr *, struct sockaddr *, int, int *);
gfarm_error_t gfarm_addr_range_get(struct sockaddr *,
	struct sockaddr *, struct sockaddr *);
