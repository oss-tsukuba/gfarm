int gfarm_canonical_hostname_is_local(char *);
int gfarm_host_is_local(char *);

struct in_addr;
char *gfarm_get_ip_addresses(int *, struct in_addr **);

struct sockaddr;
struct gfarm_hostspec;
char *gfarm_host_address_use(struct gfarm_hostspec *);
