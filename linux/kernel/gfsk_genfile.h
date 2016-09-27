#ifndef _GFSK_GENFILE_H
#define _GFSK_GENFILE_H

/* routines for general file (including regular file, dir, etc) */

extern void gfsk_invalidate_attr(struct inode *inode);
extern int gfsk_update_attributes(struct file *file, struct inode *inode,
		struct dentry *dentry);


extern int gfsk_getattr(struct vfsmount *mnt, struct dentry *dentry,
		struct kstat *stat);
extern int gfsk_setattr(struct dentry *dentry, struct iattr *attr);

extern void gfsk_set_cache_invalidate(struct inode *inode);
extern void gfsk_set_cache_updatetime(struct inode *inode);
extern void gfsk_check_cache_pages(struct inode *inode);
extern void gfsk_invalidate_pages(struct inode *inode);

#endif /* _GFSK_GENFILE_H */
