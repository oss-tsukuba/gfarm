/*
 * GFS_File and GFS_Dir are derived classes of struct gfs_desc.
 */

struct gfs_desc;
struct gfs_file; /* GFS_File */
struct gfs_dir; /* GFS_Dir */

struct gfs_desc_ops {
	gfarm_error_t (*close)(struct gfs_desc *);

	/* down cast */
	gfarm_error_t (*desc_to_file)(struct gfs_desc *, struct gfs_file **);
	gfarm_error_t (*desc_to_dir)(struct gfs_desc *, struct gfs_dir **);
};

struct gfs_desc {
	struct gfs_desc_ops *ops;
};

gfarm_error_t gfs_desc_create(const char *, int, gfarm_mode_t,
	struct gfs_desc **);
gfarm_error_t gfs_desc_open(const char *, int, struct gfs_desc **);
gfarm_error_t gfs_desc_close(struct gfs_desc *);

/* constructor of subclasses */
gfarm_error_t gfs_file_alloc(gfarm_int32_t, int, struct gfs_desc **);
gfarm_error_t gfs_dir_alloc(gfarm_int32_t, int, struct gfs_desc **);

/* XXX should have metadata server as an argument */
gfarm_error_t gfm_close_fd(int);
