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
	gfarm_base_gfs_stat_ops;

void gfarm_base_generic_info_free_all(int, void *,
	const struct gfarm_base_generic_info_ops *);
