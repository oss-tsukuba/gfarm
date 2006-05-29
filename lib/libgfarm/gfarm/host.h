int gfarm_canonical_hostname_is_local(const char *);
int gfarm_host_is_local(const char *);

struct in_addr;
gfarm_error_t gfarm_get_ip_addresses(int *, struct in_addr **);

struct sockaddr;
struct gfarm_hostspec;
#if 0 /* XXX for now */
gfarm_error_t gfarm_host_address_use(struct gfarm_hostspec *);

struct gfarm_host_info;
gfarm_error_t gfarm_host_info_address_get(const char *, int,
	struct gfarm_host_info *, struct sockaddr *, char **);
#endif /* for now */

gfarm_error_t gfarm_set_client_architecture(char *, struct gfarm_hostspec *);

int gfarm_addr_is_same_net(struct sockaddr *,
	struct sockaddr *, struct sockaddr *, int, int *);
gfarm_error_t gfarm_addr_range_get(struct sockaddr *,
	struct sockaddr *, struct sockaddr *);
