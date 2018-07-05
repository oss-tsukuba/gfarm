struct gfarm_host_address {
	int sa_family;
	socklen_t sa_addrlen;
	struct sockaddr sa_addr;
};

#ifdef GFARM_HOST_ADDRESS_INTERNAL
/* should be same order with struct gfarm_host_address */
struct gfarm_host_address_ipv4 {
	int sa_family, sa_addrlen;
	struct sockaddr_in sa_addr;
};

/* should be same order with struct gfarm_host_address */
struct gfarm_host_address_ipv6 {
	int sa_family, sa_addrlen;
	struct sockaddr_in6 sa_addr;
};
#endif

void gfarm_host_address_free(int, struct gfarm_host_address **);
gfarm_error_t gfarm_host_address_get(const char *, int,
	int *, struct gfarm_host_address ***);
gfarm_error_t gfarm_passive_address_get(const char *, int,
	int *, struct gfarm_host_address ***);

gfarm_error_t gfarm_sockaddr_get_port(struct sockaddr *, socklen_t, int *);
