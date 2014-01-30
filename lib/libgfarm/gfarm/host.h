struct gfm_connection;
struct gfarm_host_info;
struct gfarm_hostspec;

gfarm_error_t gfm_host_info_get_by_name_alias(
	struct gfm_connection *, const char *, struct gfarm_host_info *);
gfarm_error_t gfm_host_get_canonical_name(struct gfm_connection *,
	const char *, char **, int *);
gfarm_error_t gfm_host_get_canonical_self_name(struct gfm_connection *,
	char **, int *);

#if 0 /* XXX for now */
gfarm_error_t gfarm_set_client_architecture(char *, struct gfarm_hostspec *);
gfarm_error_t gfarm_host_get_self_architecture(struct gfm_connection *,
	char **);
#endif /* for now */

int gfm_canonical_hostname_is_local(struct gfm_connection *, const char *);
int gfm_host_is_local(struct gfm_connection *, const char *);


struct in_addr;
gfarm_error_t gfarm_get_ip_addresses(int *, struct in_addr **);

struct sockaddr;

#if 0 /* XXX for now */
gfarm_error_t gfarm_host_address_use(struct gfarm_hostspec *);
#endif /* for now */

gfarm_error_t gfm_host_info_address_get(struct gfm_connection *,
	const char *, int,
	struct gfarm_host_info *, struct sockaddr *, char **);

gfarm_error_t gfm_host_address_get(struct gfm_connection *, const char *,
	int, struct sockaddr *, char **);

void gfarm_known_network_list_dump(void);
gfarm_error_t gfarm_known_network_list_add(struct gfarm_hostspec *);
gfarm_error_t gfarm_known_network_list_add_local_host(void);
gfarm_error_t gfarm_addr_network_get(struct sockaddr *,
	struct gfarm_hostspec **);
