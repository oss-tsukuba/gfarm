#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <gfarm/gfarm.h>
#include "gfsk_fs.h"
#include "gfsk_libgfarm.h"
#include "gfsk_genfile.h"

void
gfsk_invalidate_attr(struct inode *inode)
{
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	gi->i_actime = 0;
}
int
gfsk_update_attributes(struct file *file, struct inode *inode,
		struct dentry *dentry)
{
	int retval = 0;
	struct file *ofile = file;
	struct gfarm_inode *gi = get_gfarm_inode(inode);

	gflog_verbose(GFARM_MSG_1004919, "ino=%ld actime=%ld",
			 inode->i_ino, gi->i_actime - get_jiffies_64());
	if (gi->i_actime < get_jiffies_64()) {
		if (!file)
			file = gfsk_open_file_get(inode);
		if (file)
			retval = gfarm_fstat(file, inode);
		else if (dentry)
			retval = gfarm_stat(dentry, NULL);
		else if ((dentry = d_find_alias(inode))) {
			retval = gfarm_stat(dentry, NULL);
			dput(dentry);
		} else {
			retval = -EINVAL;
			gflog_warning(GFARM_MSG_1004920, "no dentry ino=%ld",
					 inode->i_ino);
		}
		if (file && !ofile)
			fput(file);
		if (retval == -ENOENT)
			gfsk_iflag_set(inode, GFSK_INODE_STALE);
		else if (!retval && gfsk_iflag_isset(inode, GFSK_INODE_STALE)) {
			retval = -ENOENT;
		}
	}
	return (retval);
}

int
gfsk_getattr(struct vfsmount *mnt, struct dentry *dentry,
		struct kstat *stat)
{
	int retval;
	struct inode *inode = dentry->d_inode;
	GFSK_CTX_DECLARE_INODE(inode);

	GFSK_CTX_SET();
	retval = gfsk_update_attributes(NULL, inode, dentry);
	if (!retval && stat) {
		generic_fillattr(inode, stat);
		stat->blksize = 4096;
		stat->blocks = (stat->size + stat->blksize - 1)
					/ stat->blksize;
	} else if (retval == -ENOENT && inode) {
#if 1
		d_delete(dentry);
#else
		gfsk_invalidate_attr(inode);
		if (S_ISREG(inode->i_mode))
			gfsk_invalidate_pages(inode);
		else if (S_ISDIR(inode->i_mode))
			gfsk_invalidate_dir_pages(inode);
		gflog_info(GFARM_MSG_1004921, "gfarm:may deleted. "
			"ino=%lu, ref=%d",
			inode->i_ino, atomic_read(&dentry->d_count));
		d_delete(dentry);
#endif
	}
	GFSK_CTX_UNSET();
	return (retval);
}

int
gfsk_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int retval;
	GFSK_CTX_DECLARE_INODE(inode);

	GFSK_CTX_SET();
	retval = inode_change_ok(inode, iattr);
	if (retval)
		goto quit;

	retval = gfarm_setattr(dentry, iattr,  (iattr->ia_valid & ATTR_FILE) ?
			 iattr->ia_file : NULL);
	if (!retval && (iattr->ia_valid & ATTR_SIZE)) {
		retval = vmtruncate(inode, iattr->ia_size);
	}
quit:
	GFSK_CTX_UNSET();
	return (retval);
}

void
gfsk_set_cache_invalidate(struct inode *inode)
{
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	gi->i_pctime = 0;
}

void
gfsk_set_cache_updatetime(struct inode *inode)
{
# if 0
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	uint64_t jiffies = get_jiffies_64();
	gflog_debug(GFARM_MSG_1004922, "gfsk_set_cache_updatetime. ino=%lu, "
			"pctime=%llu, jiffies=%llu",
			inode->i_ino, gi->i_pctime, jiffies);
	if ((gi->i_pctime == 0) || (jiffies < gi->i_pctime)) {
		/* expand cache valid period */
		gi->i_pctime = jiffies + gfsk_fsp->gf_pctime;
		gflog_debug(GFARM_MSG_1004923,
			"gfsk_set_cache_updatetime updated."
			" ino=%lu, pctime=%llu", inode->i_ino, gi->i_pctime);
	} else {
		/* don't update pctime since it's not valid anymore */
	}
#endif
}

void
gfsk_check_cache_pages(struct inode *inode)
{
	if (gfsk_update_attributes(NULL, inode, NULL))
		gfsk_invalidate_pages(inode);
}

void
gfsk_invalidate_pages(struct inode *inode)
{
	invalidate_inode_pages2(inode->i_mapping);
	gflog_debug(GFARM_MSG_1004924,
		"gfsk_invalidate_pages done. ino=%lu", inode->i_ino);
	gfsk_set_cache_invalidate(inode);
}
