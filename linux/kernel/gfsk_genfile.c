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

	if (gi->i_actime < get_jiffies_64()) {
		if (!file)
			file = gfsk_open_file_get(inode);
		if (file)
			retval = gfarm_fstat(file, inode);
		else
			retval = gfarm_stat(dentry, NULL);
		if (file && !ofile)
			fput(file);
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
}

int
gfsk_is_cache_valid(struct inode *inode)
{
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	u64 jiffies = get_jiffies_64();
	gflog_debug(GFARM_MSG_UNFIXED, "gfsk_is_cache_valid done. ino=%lu, "
		"jiffies=%llu, pctime=%llu",
		inode->i_ino, jiffies, gi->i_pctime);
	return ((0 < gi->i_pctime) && (jiffies < gi->i_pctime));
}

void
gfsk_invalidate_pages(struct inode *inode)
{
	invalidate_inode_pages2(inode->i_mapping);
	gflog_debug(GFARM_MSG_1004924,
		"gfsk_invalidate_pages done. ino=%lu", inode->i_ino);
	gfsk_set_cache_invalidate(inode);
}
