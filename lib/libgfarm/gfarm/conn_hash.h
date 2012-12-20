struct gfp_conn_hash_id {
	char *hostname;
	int port;
	char *username;
};

gfarm_error_t gfp_conn_hash_table_init(struct gfarm_hash_table **, int);
void gfp_conn_hash_table_dispose(struct gfarm_hash_table *);
gfarm_error_t gfp_conn_hash_enter(struct gfarm_hash_table **, int, size_t,
	const char *, int, const char *, struct gfarm_hash_entry **, int *);
gfarm_error_t gfp_conn_hash_enter_noalloc(struct gfarm_hash_table **, int,
	size_t, const char *, int, const char *, struct gfarm_hash_entry **,
	int *);
gfarm_error_t gfp_conn_hash_id_enter(struct gfarm_hash_table **, int, size_t,
	struct gfp_conn_hash_id *, struct gfarm_hash_entry **, int *);
gfarm_error_t gfp_conn_hash_id_enter_noalloc(struct gfarm_hash_table **, int,
	size_t, struct gfp_conn_hash_id *, struct gfarm_hash_entry **, int *);
gfarm_error_t gfp_conn_hash_lookup(struct gfarm_hash_table **, int,
	const char *, int, const char *, struct gfarm_hash_entry **);
void gfp_conn_hash_purge(struct gfarm_hash_table *, struct gfarm_hash_entry *);
void gfp_conn_hash_iterator_purge(struct gfarm_hash_iterator *);
const char *gfp_conn_hash_hostname(struct gfarm_hash_entry *);
const char *gfp_conn_hash_username(struct gfarm_hash_entry *);
int gfp_conn_hash_port(struct gfarm_hash_entry *);
gfarm_error_t gfp_conn_hash_lookup(struct gfarm_hash_table **, int,
	const char *, int, const char *, struct gfarm_hash_entry **);
void gfp_conn_hash_purge(struct gfarm_hash_table *, struct gfarm_hash_entry *);
void gfp_conn_hash_iterator_purge(struct gfarm_hash_iterator *);
