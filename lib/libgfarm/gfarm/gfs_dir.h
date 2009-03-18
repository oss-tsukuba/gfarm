struct gfs_dir_ops {
	gfarm_error_t (*closedir)(GFS_Dir);
	gfarm_error_t (*readdir)(GFS_Dir, struct gfs_dirent **);
	gfarm_error_t (*seekdir)(GFS_Dir, gfarm_off_t);
	gfarm_error_t (*telldir)(GFS_Dir, gfarm_off_t *);
};

struct gfs_dir {
	struct gfs_dir_ops *ops;
};

gfarm_error_t gfs_seekdir_unimpl(GFS_Dir, gfarm_off_t);
gfarm_error_t gfs_telldir_unimpl(GFS_Dir, gfarm_off_t *);
