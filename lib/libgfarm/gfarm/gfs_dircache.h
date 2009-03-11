gfarm_error_t gfs_stat_cached_internal(const char *, struct gfs_stat *);
gfarm_error_t gfs_opendir_caching_internal(const char *, GFS_DirCaching *);
gfarm_error_t gfs_readdir_caching_internal(GFS_DirCaching, struct gfs_dirent **);
gfarm_error_t gfs_closedir_caching_internal(GFS_DirCaching);
