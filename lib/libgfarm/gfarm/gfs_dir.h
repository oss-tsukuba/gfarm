/*
 * This is a private header.
 *
 * Only gfs_dir.c and agent_wrap.c are allowed to include this header file.
 * Every other modules shouldn't include this.
 */

struct gfs_dir_ops {
	char *(*close)(GFS_Dir);
	char *(*read)(GFS_Dir, struct gfs_dirent **);
	char *(*seek)(GFS_Dir, file_offset_t);
	char *(*tell)(GFS_Dir, file_offset_t *);
	char *(*dirname)(GFS_Dir);
};

/*
 * This is an abstract base class.
 * All instances of struct gfs_dir_* should have this as its first member.
 */
struct gfs_dir {
	struct gfs_dir_ops *ops;
};
