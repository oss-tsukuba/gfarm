typedef struct gfs_dirplusxattr *GFS_DirPlusXAttr;

gfarm_error_t gfs_opendirplusxattr(const char *, GFS_DirPlusXAttr *);
gfarm_error_t gfs_readdirplusxattr(GFS_DirPlusXAttr,
	struct gfs_dirent **, struct gfs_stat **, int *,
	char ***, void ***, size_t **);
gfarm_error_t gfs_seekdirplusxattr(GFS_DirPlusXAttr, gfarm_off_t);
gfarm_error_t gfs_telldirplusxattr(GFS_DirPlusXAttr, gfarm_off_t *);

gfarm_error_t gfs_closedirplusxattr(GFS_DirPlusXAttr);
