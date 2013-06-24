#ifndef GFSK_LIBGFARM_H
#define GFSK_LIBGFARM_H

extern int gfarm_statfs(struct dentry *dentry, struct kstatfs *buf);

extern int gfarm_stat(struct dentry *dentry, struct inode **inodepp);
extern struct inode *gfarm_stat2inode(struct super_block *sb,
		struct gfs_stat *stp);

int gfarm_fstat(struct file *file, struct inode *inode);

extern int gfarm_mkdir(struct inode *dir, struct dentry * dentry, int mode);
extern int gfarm_rmdir(struct inode *dir, struct dentry *dentry);
extern int gfarm_link(struct dentry *old_dentry, struct inode *new_dir,
		struct dentry *new_dentry);
extern int gfarm_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry);
extern int gfarm_setattr(struct dentry *dentry, struct iattr *attr,
		struct file *file);


extern int gfarm_opendir(struct inode *inode, struct file *file);
extern int gfarm_closedir(struct inode *inode, struct file *file);

extern int gfarm_createfile(struct inode *inode, struct dentry *dentry,
		int flags, int mode, struct file *file, struct inode **inodep);
extern int gfarm_unlink(struct inode *inode, struct dentry *dentry);

extern int gfarm_openfile(struct inode *inode, struct file *file);
extern int gfarm_closefile(struct inode *inode, struct file *file);

extern int gfarm_read(struct file *file, loff_t off, char *buff,
		ssize_t size, int *readlen);
extern int gfarm_write(struct file *file, loff_t off, const char *buff,
		ssize_t size, int *writelen);
extern int gfarm_seek(struct file *file, loff_t off, int whence,
		loff_t *newoff);
extern int gfarm_append(struct file *file, const char *buff, ssize_t size,
		int *writelen, loff_t *offp);
extern int gfarm_truncate(struct inode *inode, loff_t size);
extern int gfarm_fsync(struct file *file, int datasync);
extern int gfsk_get_localfd(struct file *file, int *fdp);
extern struct file *gfsk_open_file_get(struct inode *inode);

struct gfsk_file_private {
	/* NOTE: attached to file->private_data at gfsk_opendir etc. */
	union {
		GFS_DirPlus dirp;
		GFS_File filp;
	} f_u;
	struct {
		void *entry;
	} dirinfo;
	struct file *f_file;
	struct list_head f_openlist;
	struct mutex f_lock;
};

#define UNKNOWN_UID	((uid_t)-1)
#define UNKNOWN_GID	((gid_t)-1)

#define GFARM_ERROR_TO_ERRNO(ge)	\
	((ge)  == GFARM_ERR_NO_ERROR ? 0 : -gfarm_error_to_errno(ge))

#endif /* GFSK_LIBGFARM_H */
