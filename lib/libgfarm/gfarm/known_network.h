struct sockaddr;
struct gfarm_hostspec;

void gfarm_known_network_list_dump(void);
gfarm_error_t gfarm_known_network_list_add(struct gfarm_hostspec *);
gfarm_error_t gfarm_known_network_list_add_local_host(void);
gfarm_error_t gfarm_addr_network_get(struct sockaddr *,
	struct gfarm_hostspec **);
