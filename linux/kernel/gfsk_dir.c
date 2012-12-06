#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/namei.h>
#include <asm/string.h>
#include <gfarm/gfarm.h>
#include "gfsk_fs.h"
#include "gfsk_libgfarm.h"

void invalidate_dir_pages(struct inode *inode)
{
	struct gfarm_inode *gi = get_gfarm_inode(inode);

	if ((inode->i_mode & S_IFMT) != S_IFDIR)
		return;
	invalidate_inode_pages2(inode->i_mapping);
	gflog_debug(0, "invalidate_inode_pages2 done. ino=%lu", inode->i_ino);
	gi->i_direntsize = 0;
}

static int check_invalidate_dir_pages(struct inode *inode, struct file *file)
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
	gflog_debug(0, "mtime=%ld.%ld, %ld.%ld",
		mtime.tv_sec, mtime.tv_nsec,
		tmp->i_mtime.tv_sec, tmp->i_mtime.tv_nsec);
	if (mtime.tv_sec != tmp->i_mtime.tv_sec ||
		mtime.tv_nsec != tmp->i_mtime.tv_nsec) {
		invalidate_dir_pages(inode);
	}
	iput(tmp);
	return (0);
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

	invalidate_dir_pages(dir);
	retval = gfarm_stat(dentry, &inode);
	if (retval)
		goto quit;
	d_instantiate(dentry, inode);	/* inode count is handed */
quit:
	GFSK_CTX_UNSET();
	return (retval);
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
gfsk_rmdir(struct inode *dir, struct dentry *dentry)
{
	int retval;
	GFSK_CTX_DECLARE_INODE(dir);

	GFSK_CTX_SET();
	retval = gfarm_rmdir(dir, dentry);
	if (!retval) {
		invalidate_dir_pages(dir);
		retval = simple_rmdir(dir, dentry);
	}
	GFSK_CTX_UNSET();
	return (retval);
}

static int
gfsk_getattr(struct vfsmount *mnt, struct dentry *dentry,
		struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	int retval;
	GFSK_CTX_DECLARE_INODE(inode);

	GFSK_CTX_SET();
	retval = gfarm_stat(dentry, NULL);
	if (!retval) {
		generic_fillattr(inode, stat);
		stat->blksize = 4096;
		stat->blocks = (stat->size + stat->blksize - 1)
					/ stat->blksize;
	}
	GFSK_CTX_UNSET();
	return (retval);
}

const struct inode_operations gfarm_dir_inode_operations = {
	.lookup = gfsk_lookup,
	.mkdir = gfsk_mkdir,
	.rmdir = gfsk_rmdir,
	.getattr = gfsk_getattr
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
	GFSK_CTX_UNSET();
	return (retval);
}

static loff_t
gfsk_dir_lseek(struct file *file, loff_t offset, int origin)
{
	return (-EOPNOTSUPP);
}

static unsigned char get_dtype(unsigned char gtype)
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
gfsk_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	struct page *page;
	GFSK_CTX_DECLARE_FILE(filp);
	char *p, *end;
	struct gfs_dirent *entry;
	struct gfsk_file_private *priv;
	int over;

	if (filp->f_pos > 0 && gi->i_direntsize > 0 &&
			filp->f_pos >= gi->i_direntsize)
		return (0);
	priv = (struct gfsk_file_private *)filp->private_data;
	if (!priv || !priv->dirp)
		return (-EINVAL);

	GFSK_CTX_SET();
	while (1) {
		/* NOTE: 4th arg of read_mapping_page() is
		   1st arg of gfsk_dir_readpage */
		page = read_mapping_page(inode->i_mapping,
			filp->f_pos / PAGE_SIZE, filp);
		if (IS_ERR(page)) {
			GFSK_CTX_UNSET();
			break;
		}
		kmap(page);
		p = page_address(page);
		end = (p + PAGE_SIZE - sizeof(*entry));
		p += (filp->f_pos % PAGE_SIZE);
		while (p <= end) {
			entry = (struct gfs_dirent *)p;
			over = filldir(dirent, entry->d_name, entry->d_namlen,
				filp->f_pos, entry->d_fileno,
				get_dtype(entry->d_type));
			if (over)
				break;
			filp->f_pos += sizeof(*entry);
			if (filp->f_pos >= gi->i_direntsize) {
				over = 1;
				break;
			}
			p += sizeof(*entry);
		}
		kunmap(page);
		if (over)
			break;
		filp->f_pos = (filp->f_pos + (PAGE_SIZE - 1)) & PAGE_MASK;
	}

	GFSK_CTX_UNSET();
	return (0);
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

	inode = gfarm_stat_set(sb, status);

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

static int gfsk_dir_readpage(struct file *dir, struct page *page)
{
	int retval;
	struct gfs_dirent *entry;
	struct gfs_stat *status;
	gfarm_error_t ge = GFARM_ERR_NO_ERROR;
	struct gfarm_inode *gi = get_gfarm_inode(dir->f_path.dentry->d_inode);
	struct gfsk_file_private *priv;
	char *p, *end;
	pgoff_t pgindex = 0;
	int gap;

	priv = (struct gfsk_file_private *)dir->private_data;
	if (!priv || !priv->dirp)
		return (-EINVAL);

	kmap(page);
	p = page_address(page);
	end = (p + PAGE_SIZE);
	gi->i_direntsize = 0;
	gap = 0;
	while (1) {
		ge = gfs_readdirplus(priv->dirp, &entry, &status);
		if ((ge != GFARM_ERR_NO_ERROR) || (entry == NULL))
			break;
		if ((end - p) < sizeof(*entry)) {
			gap = (end - p);
			memset(p, 0, gap);
			kunmap(page);
			SetPageUptodate(page);
			unlock_page(page);
			pgindex++;
			page = find_or_create_page(dir->f_mapping,
					pgindex, GFP_TEMPORARY);
			if (page == NULL)
				return (-ENOMEM);
			kmap(page);
			p = page_address(page);
			end = (p + PAGE_SIZE);
		}
		retval = gfsk_dir_update_node(dir, entry, status);
		if (retval)
			return (retval);
		memcpy(p, entry, sizeof(*entry));
		if (gap > 0) {
			gi->i_direntsize += gap;
			gap = 0;
		}
		gi->i_direntsize += sizeof(*entry);
		p += sizeof(*entry);
	}
	kunmap(page);
	SetPageUptodate(page);
	unlock_page(page);

	return (GFARM_ERROR_TO_ERRNO(ge));
}

const struct address_space_operations gfarm_dir_aops = {
	.readpage	= gfsk_dir_readpage,
};
