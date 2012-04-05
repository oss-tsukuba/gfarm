#ifndef GFSK_LIBGFARM_H
#define GFSK_LIBGFARM_H

extern int gfarm_statfs(struct dentry *dentry, struct kstatfs *buf);

extern struct inode *gfarm_stat_set(struct super_block *sb,
	struct gfs_stat *stp);
extern int gfarm_stat(struct dentry *dentry, struct inode **inodepp);

extern int gfarm_mkdir(struct inode *dir, struct dentry * dentry, int mode);
extern int gfarm_rmdir(struct inode *dir, struct dentry *dentry);

extern int gfarm_opendir(struct inode *inode, struct file *file);
extern int gfarm_closedir(struct inode *inode, struct file *file);

struct gfsk_file_private {
	/* NOTE: attached to file->private_data at gfsk_opendir */
	GFS_DirPlus dirp;
};

#define UNKNOWN_UID	((uid_t)-1)
#define UNKNOWN_GID	((gid_t)-1)

#define GFARM_ERROR_TO_ERRNO(ge)	\
	((ge)  == GFARM_ERR_NO_ERROR ? 0 : -gfarm_error_to_errno(ge))

#endif /* GFSK_LIBGFARM_H */
