char *gfp_conn_hash_hostname(struct gfarm_hash_entry *);
char *gfp_conn_hash_username(struct gfarm_hash_entry *);
char *gfp_conn_hash_enter(struct gfarm_hash_table **, int, size_t,
	const char *, int, const char *,
	struct gfarm_hash_entry **, int *);
char *gfp_conn_hash_lookup(struct gfarm_hash_table **, int,
	const char *, int, const char *, struct gfarm_hash_entry **);
void gfp_conn_hash_purge(struct gfarm_hash_table *, struct gfarm_hash_entry *);
void gfp_conn_hash_iterator_purge(struct gfarm_hash_iterator *);
