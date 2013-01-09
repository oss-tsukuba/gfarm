struct gfs_dir_ops {
	gfarm_error_t (*closedir)(GFS_Dir);
	gfarm_error_t (*readdir)(GFS_Dir, struct gfs_dirent **);
	gfarm_error_t (*seekdir)(GFS_Dir, gfarm_off_t);
	gfarm_error_t (*telldir)(GFS_Dir, gfarm_off_t *);
};

struct gfs_dir {
	struct gfs_dir_ops *ops;
};

struct gfm_seekdir_closure {
	gfarm_off_t offset;
	gfarm_int32_t whence;
};
gfarm_error_t gfm_seekdir_request(struct gfm_connection *, void *);
gfarm_error_t gfm_seekdir_result(struct gfm_connection *, void *);
