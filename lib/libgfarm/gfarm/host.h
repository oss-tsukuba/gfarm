int gfarm_canonical_hostname_is_local(const char *);
int gfarm_host_is_local(char *);

struct in_addr;
char *gfarm_get_ip_addresses(int *, struct in_addr **);

struct sockaddr;
struct gfarm_hostspec;
char *gfarm_host_address_use(struct gfarm_hostspec *);
char *gfarm_set_client_architecture(char *, struct gfarm_hostspec *);
char *gfarm_host_address_get_bare(
	const char *, int, struct sockaddr *, char **);

struct gfarm_host_info;
char *gfarm_host_info_address_get(const char *, int, struct gfarm_host_info *,
	struct sockaddr *, char **);

int gfarm_addr_is_same_net(struct sockaddr *,
	struct sockaddr *, struct sockaddr *, int, int *);
char *gfarm_addr_range_get(struct sockaddr *,
	struct sockaddr *, struct sockaddr *);

char *gfarm_hosts_in_domain(int *, char ***, char *);
