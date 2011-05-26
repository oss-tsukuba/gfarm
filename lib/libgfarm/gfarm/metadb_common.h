struct gfarm_base_generic_info_ops {
	size_t info_size;
	void (*free)(void *info);
	void (*clear)(void *info);
	int (*validate)(void *info);
};

extern const struct gfarm_base_generic_info_ops
	gfarm_base_host_info_ops,
	gfarm_base_user_info_ops,
	gfarm_base_group_info_ops,
	gfarm_base_gfs_stat_ops,
	gfarm_base_xattr_info_ops,
	gfarm_base_quota_info_ops,
	gfarm_base_metadb_server_ops,
	gfarm_base_metadb_cluster_ops;

void gfarm_base_generic_info_free_all(int, void *,
	const struct gfarm_base_generic_info_ops *);


void gfarm_host_info_free_except_hostname(struct gfarm_host_info *);

struct gfarm_metadb_server;
void gfarm_metadb_server_free(struct gfarm_metadb_server *);
void gfarm_metadb_server_free_all(int, struct gfarm_metadb_server *);
