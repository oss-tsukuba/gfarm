#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/namei.h>
#include <asm/string.h>
#include <gfarm/gfarm.h>
#include "gfsk_fs.h"
#include "gfsk_libgfarm.h"
#include "gfsk_genfile.h"

static int gfsk_dir_update_node(struct file *dir, struct gfs_dirent *entry,
		struct gfs_stat *status);
void
gfsk_invalidate_dir_pages(struct inode *inode)
{
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	if (gi->i_direntsize > 0) {
		gfsk_invalidate_pages(inode);
		gi->i_direntsize = 0;
	}
	gfsk_invalidate_attr(inode);
}

static int
check_invalidate_dir_pages(struct inode *inode, struct file *file)
{
	int retval;
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	struct timespec mtime = inode->i_mtime;
	struct inode *tmp;

	if (gi->i_direntsize == 0)
		return (0);

	retval = gfarm_stat(file->f_path.dentry, &tmp);
	if (retval)
		return (retval);
	if (mtime.tv_sec != tmp->i_mtime.tv_sec ||
		mtime.tv_nsec != tmp->i_mtime.tv_nsec) {
		gfsk_invalidate_dir_pages(inode);
	}
	iput(tmp);
	return (0);
}

static struct dentry *
gfsk_lookup(struct inode *dir, struct dentry *dentry,
		struct nameidata *nd)
{
	int retval;
	struct inode *inode;
	struct dentry *newent = NULL;
	GFSK_CTX_DECLARE_INODE(dir);

	if (dentry->d_name.len > GFS_MAXNAMLEN)
		return (ERR_PTR(-ENAMETOOLONG));

	GFSK_CTX_SET();
	retval = gfarm_stat(dentry, &inode);
	if (!retval || retval == -ENOENT) {
		retval = 0;
		newent = d_splice_alias(inode, dentry);
	}
	GFSK_CTX_UNSET();
	if (!retval)
		return (newent);
	else
		return (ERR_PTR(retval));
}

static int
gfsk_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int retval;
	struct inode *inode;
	GFSK_CTX_DECLARE_INODE(dir);

	GFSK_CTX_SET();
	retval = gfarm_mkdir(dir, dentry, mode);
	if (retval)
		goto quit;

	gfsk_invalidate_dir_pages(dir);
	retval = gfarm_stat(dentry, &inode);
	if (retval)
		goto quit;
	d_instantiate(dentry, inode);	/* inode count is handed */
quit:
	GFSK_CTX_UNSET();
	return (retval);
}

static int
gfsk_rmdir(struct inode *dir, struct dentry *dentry)
{
	int retval;
	GFSK_CTX_DECLARE_INODE(dir);

	GFSK_CTX_SET();
	retval = gfarm_rmdir(dir, dentry);
	if (!retval) {
		gfsk_invalidate_dir_pages(dir);
		clear_nlink(dentry->d_inode);
	}
	GFSK_CTX_UNSET();
	return (retval);
}

static int
gfsk_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	int retval, flags;
	struct inode *inode;
	GFSK_CTX_DECLARE_INODE(dir);

	GFSK_CTX_SET();

	/* see open_to_namei_flags() */
	flags = nd->intent.open.flags;
	if (flags & O_ACCMODE)
		flags--;
	retval = gfarm_createfile(dir, dentry, flags, mode,
					nd->intent.open.file, &inode);
	if (retval)
		goto quit;
	gfsk_invalidate_dir_pages(dir);
	retval = gfarm_fstat(nd->intent.open.file, inode);
	if (retval) {
		gfarm_closefile(inode, nd->intent.open.file);
		goto quit;
	}
	d_instantiate(dentry, inode);	/* inode count is handed */
quit:
	GFSK_CTX_UNSET();
	return (retval);
}

static int
gfsk_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry) {
	int retval;
	GFSK_CTX_DECLARE_INODE(old_dir);

	GFSK_CTX_SET();
	retval = gfarm_rename(old_dir, old_dentry, new_dir, new_dentry);
	if (!retval) {
		gfsk_invalidate_dir_pages(old_dir);
		if (old_dir != new_dir) {
			gfsk_invalidate_dir_pages(new_dir);
		}
		retval = simple_rename(old_dir, old_dentry, new_dir,
			new_dentry);
	}
	GFSK_CTX_UNSET();
	return (retval);
}


static int
gfsk_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	int retval;
	GFSK_CTX_DECLARE_DENTRY(old_dentry);

	GFSK_CTX_SET();
	retval = gfarm_link(old_dentry, dir, dentry);
	if (!retval) {
		struct inode *new_inode;
		gfsk_invalidate_attr(old_dentry->d_inode);
		if (old_dentry->d_parent->d_inode != dir) {
			gfsk_invalidate_dir_pages(
				old_dentry->d_parent->d_inode);
		}
		gfsk_invalidate_dir_pages(dir);
		retval = gfarm_stat(dentry, &new_inode);
		if (!retval)
			d_instantiate(dentry, new_inode);
	}
	GFSK_CTX_UNSET();
	return (retval);
}

static int
gfsk_unlink(struct inode *dir, struct dentry *dentry)
{
	int retval;
	GFSK_CTX_DECLARE_INODE(dir);

	GFSK_CTX_SET();
	retval = gfarm_unlink(dir, dentry);
	if (retval == 0) {
		gfsk_invalidate_attr(dentry->d_inode);
		gfsk_invalidate_dir_pages(dir);
	}
	GFSK_CTX_UNSET();
	return (retval);
}
int
gfsk_dirperm(struct inode *inode, int mask)
{
	int err;

	err = generic_permission(inode, mask, NULL);

	if (err || !err)
		gflog_debug(GFARM_MSG_UNFIXED,
		"%s:%d:mask=o%d inode->ino=%lu i_flags=0x%x mode=0%o , ret=%d",
		__func__, __LINE__, mask, inode->i_ino, inode->i_flags,
			inode->i_mode, -err);

	return (err);
}
const struct inode_operations gfarm_dir_inode_operations = {
	.create		= gfsk_create,
	.lookup		= gfsk_lookup,
	.link		= gfsk_link,
	.unlink		= gfsk_unlink,
	.mkdir		= gfsk_mkdir,
	.rmdir		= gfsk_rmdir,
	.rename		= gfsk_rename,
	.getattr	= gfsk_getattr,
	.setattr	= gfsk_setattr,
	.permission	= gfsk_dirperm,
};

/**********************************************************************/

static int
gfsk_dir_open(struct inode *inode, struct file *file)
{
	int retval;
	GFSK_CTX_DECLARE_INODE(inode);

	GFSK_CTX_SET();
	retval = gfarm_opendir(inode, file);
	if (!retval) {
		retval = check_invalidate_dir_pages(inode, file);
	}
	GFSK_CTX_UNSET();
	return (retval);
}

static int
gfsk_dir_close(struct inode *inode, struct file *file)
{
	int retval;
	GFSK_CTX_DECLARE_INODE(inode);

	GFSK_CTX_SET();
	retval = gfarm_closedir(inode, file);
	if (!file->f_dentry->d_inode)
		gflog_fatal(GFARM_MSG_UNFIXED,
			"%s: no inode inode=%p file=%p",
			__func__, inode, file);

	GFSK_CTX_UNSET();
	return (retval);
}

static loff_t
gfsk_dir_lseek(struct file *file, loff_t offset, int origin)
{
	return (-EOPNOTSUPP);
}

static unsigned char
get_dtype(unsigned char gtype)
{
	/* NOTE: GFS_DT_XXX = DT_XXX actually */
	switch (gtype) {
	case GFS_DT_DIR:
		return (DT_DIR);
	case GFS_DT_REG:
		return (DT_REG);
	case GFS_DT_LNK:
		return (DT_LNK);
	case GFS_DT_UNKNOWN:
	default:
		return (DT_UNKNOWN);
	}

}

static int
gfsk_full_readpage(struct file *dir, struct page **reqpage)
{
	int retval;
	struct gfs_dirent *entry;
	struct gfs_stat *status;
	gfarm_error_t ge = GFARM_ERR_NO_ERROR;
	struct inode *inode = dir->f_path.dentry->d_inode;
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	struct gfsk_file_private *priv;
	char *p, *end;
	pgoff_t pgindex;
	struct page *page = NULL;
	int gap;

	priv = (struct gfsk_file_private *)dir->private_data;
	if (!priv || !priv->f_u.dirp)
		return (-EINVAL);

	p = 0;
	end = 0;
	gi->i_direntsize = 0;
	gap = 0;
	pgindex = 0;
	while (1) {
		ge = gfs_readdirplus(priv->f_u.dirp, &entry, &status);
		if ((ge != GFARM_ERR_NO_ERROR) || (entry == NULL))
			break;
		if ((end - p) < sizeof(*entry)) {
			gap = (end - p);
			memset(p, 0, gap);
			if (page) {
				if (reqpage && *reqpage == page) {
					*reqpage = NULL;
					reqpage = NULL;
				}
				kunmap(page);
				SetPageUptodate(page);
				unlock_page(page);
				pgindex++;
			}
			if (reqpage && (*reqpage)->index == pgindex)
				page = *reqpage;
			else
				page = find_or_create_page(dir->f_mapping,
					pgindex, GFP_TEMPORARY);
			if (page == NULL)
				return (-ENOMEM);
			kmap(page);
			p = page_address(page);
			end = (p + PAGE_SIZE);
		}
		retval = gfsk_dir_update_node(dir, entry, status);
		/******** don't care to make child
		if (retval)
			return (retval);
		*********************************/
		memcpy(p, entry, sizeof(*entry));
		if (gap > 0) {
			gi->i_direntsize += gap;
			gap = 0;
		}
		gi->i_direntsize += sizeof(*entry);
		p += sizeof(*entry);
	}
	if (page) {
		kunmap(page);
		SetPageUptodate(page);
		unlock_page(page);
		if (reqpage && *reqpage == page)
			*reqpage = NULL;
	}

	if (ge == GFARM_ERR_NO_ERROR) {
		gfsk_set_cache_updatetime(inode);
	}
	return (GFARM_ERROR_TO_ERRNO(ge));
}
static int
gfsk_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	GFSK_CTX_DECLARE_FILE(filp);
	gfarm_error_t ge = GFARM_ERR_NO_ERROR;
	struct gfs_dirent *entry;
	struct gfs_stat *status;
	struct gfsk_file_private *priv;
	int over = 0, count = 0, retval = 0;

	priv = (struct gfsk_file_private *)filp->private_data;
	if (!priv || !priv->f_u.dirp)
		return (-EINVAL);

	GFSK_CTX_SET();
	if ((entry = priv->dirinfo.entry)) {
		priv->dirinfo.entry = NULL;
		over = filldir(dirent, entry->d_name, entry->d_namlen,
			filp->f_pos, entry->d_fileno, get_dtype(entry->d_type));
		if (!over) {
			filp->f_pos += sizeof(*entry);
			count++;
		}
	}
	while (!over) {
		ge = gfs_readdirplus(priv->f_u.dirp, &entry, &status);
		if (ge != GFARM_ERR_NO_ERROR) {
			if (!count)
				retval = GFARM_ERROR_TO_ERRNO(ge);
			break;
		}
		if (entry == NULL)
			break;
		(void)gfsk_dir_update_node(filp, entry, status);
		over = filldir(dirent, entry->d_name, entry->d_namlen,
			filp->f_pos, entry->d_fileno, get_dtype(entry->d_type));
		if (over) {
			priv->dirinfo.entry = entry;
			break;
		}
		filp->f_pos += sizeof(*entry);
		count++;
	}
	GFSK_CTX_UNSET();
	return (retval);
}

const struct file_operations gfarm_dir_operations = {
	.open = gfsk_dir_open,
	.release = gfsk_dir_close,
	.llseek = gfsk_dir_lseek,
	.read = generic_read_dir,
	.readdir = gfsk_readdir
};

static int
gfsk_dir_update_node(struct file *dir, struct gfs_dirent *entry,
		struct gfs_stat *status)
{
	struct super_block *sb = dir->f_path.dentry->d_sb;
	struct inode *inode;
	struct dentry *dentry;

	inode = gfarm_stat2inode(sb, status);

	if (inode == NULL)
		return (-ENOMEM);
	dentry = lookup_one_len(entry->d_name, dir->f_path.dentry,
						entry->d_namlen);
	if (IS_ERR(dentry)) {
		iput(inode);
		return (PTR_ERR(dentry));
	} else {
		if (!dentry->d_inode)
			d_instantiate(dentry, inode);
		else
			iput(inode);
		dput(dentry);
	}
	return (0);
}

static int
gfsk_dir_readpage(struct file *dir, struct page *page)
{
	loff_t pos = page_offset(page);
	struct inode *inode = dir->f_path.dentry->d_inode;
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	int retval;
	GFSK_CTX_DECLARE_FILE(dir);

	GFSK_CTX_SET();

	gflog_error(GFARM_MSG_UNFIXED, "%s:called!! size=%lld pos=%lld",
		__func__, gi->i_direntsize, pos);

	retval = gfsk_full_readpage(dir, &page);
	GFSK_CTX_UNSET();
	if (page)
		unlock_page(page);
	return (retval);
}

const struct address_space_operations gfarm_dir_aops = {
	.readpage	= gfsk_dir_readpage,
};
