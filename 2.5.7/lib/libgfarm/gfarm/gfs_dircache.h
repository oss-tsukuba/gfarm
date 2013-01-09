gfarm_error_t gfs_stat_cached_internal(const char *, struct gfs_stat *);
gfarm_error_t gfs_lstat_cached_internal(const char *, struct gfs_stat *);
gfarm_error_t gfs_opendir_caching_internal(const char *, GFS_Dir *);
gfarm_error_t gfs_getxattr_cached_internal(const char *, const char *,
	void *, size_t *);
gfarm_error_t gfs_lgetxattr_cached_internal(const char *, const char *,
	void *, size_t *);
