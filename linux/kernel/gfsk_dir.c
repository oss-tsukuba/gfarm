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
static int gfsk_d_delete(struct dentry *);
static int gfsk_d_revalidate(struct dentry *, struct nameidata *);

static struct dentry_operations gfsk_delete_dentry_operations = {
	.d_delete =  gfsk_d_delete,
	.d_revalidate =  gfsk_d_revalidate,
};
static struct dentry_operations gfsk_dentry_operations = {
	.d_revalidate =  gfsk_d_revalidate,
};
unsigned long
gfsk_gettv(struct timespec *ts)
{
	struct timespec t;

	if (!ts) {
		getnstimeofday(&t);
		ts = &t;
	}
	return (timespec_to_ktime(*ts).tv64);
}
void
gfsk_d_op_set(struct dentry *dentry)
{
	dentry->d_op = gfsk_fsp->gf_d_delete ?
		&gfsk_delete_dentry_operations : &gfsk_dentry_operations;
}
static int
gfsk_d_delete(struct dentry *dentry)
{
	if ((!dentry->d_inode)
	|| (gfsk_iflag_isset(dentry->d_inode, GFSK_INODE_STALE))
	|| (!(dentry->d_sb->s_flags & MS_ACTIVE))
	){
		return (1);
	}
	return (0);
}
static int
gfsk_d_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	int valid;
	struct inode *inode;
	unsigned long pmtime;
	GFSK_CTX_DECLARE_SB(dentry->d_sb);

	GFSK_CTX_SET();
	gfsk_d_op_set(dentry);
	if (IS_ROOT(dentry))
		valid = 1;
	else if (gfsk_update_attributes(NULL, dentry->d_parent->d_inode,
			dentry->d_parent))
			valid = -1;
	else {
		pmtime = gfsk_gettv(&dentry->d_parent->d_inode->i_mtime);
		if (pmtime <= dentry->d_time)
			valid = 2;
		else if (!(inode = dentry->d_inode))
			valid = -2;
		else if (!gfsk_update_attributes(NULL, inode, dentry))
			valid = 3;
		else
			valid = -3;
	}
	if (valid < 0)
		gflog_debug(GFARM_MSG_1004901, "%s/%s %s  valid=%d",
			dentry->d_parent->d_name.name, dentry->d_name.name,
			dentry->d_inode ? "" : "(Neg)", valid);

	GFSK_CTX_UNSET();
	return (valid > 0 ? 1 : 0);
}
void
gfsk_invalidate_dir_pages(struct inode *inode)
{
	gfsk_invalidate_attr(inode);
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
	gfsk_d_op_set(dentry);
	retval = gfarm_stat(dentry, &inode);
	if (!retval || retval == -ENOENT) {
		retval = 0;
		newent = d_splice_alias(inode, dentry);
		if (newent)
			dentry = newent;
		dentry->d_time = gfsk_gettv(NULL);	/* for ENOENT */
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
	gfsk_d_op_set(dentry);
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
	gfsk_d_op_set(dentry);
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
	dentry->d_time = gfsk_gettv(NULL);
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
	gfsk_d_op_set(new_dentry);
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
	gfsk_d_op_set(dentry);
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
# if 0
		gfsk_invalidate_attr(dentry->d_inode);
#endif
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

	if (err)
		gflog_debug(GFARM_MSG_1004902,
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
		gflog_fatal(GFARM_MSG_1004903,
			"%s: no inode inode=%p file=%p",
			__func__, inode, file);

	GFSK_CTX_UNSET();
	return (retval);
}

static loff_t
gfsk_dir_lseek(struct file *file, loff_t offset, int origin)
{
	switch (origin) {
	case SEEK_SET:
		if (offset == 0) {
			file->f_pos = offset;
			return (offset);
		}
		/* fall through */
	default:
		return (-EOPNOTSUPP);
	}
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
gfsk_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	GFSK_CTX_DECLARE_FILE(file);
	gfarm_error_t ge = GFARM_ERR_NO_ERROR;
	struct gfs_dirent *entry;
	struct gfs_stat *status;
	struct gfsk_file_private *priv;
	int over = 0, count = 0, retval = 0;

	if (file->f_pos == (loff_t) -1) {
		return (0);
	}
	GFSK_CTX_SET();
	priv = gfarm_priv_get(file);
	if (!priv || !priv->f_u.dirp) {
		gfarm_priv_put(file);
		GFSK_CTX_UNSET();
		return (-EINVAL);
	}
	gfs_seekdirplus(priv->f_u.dirp, file->f_pos);
	while (!over) {
		ge = gfs_readdirplus(priv->f_u.dirp, &entry, &status);
		if (ge != GFARM_ERR_NO_ERROR) {
			if (!count)
				retval = GFARM_ERROR_TO_ERRNO(ge);
			break;
		}
		if (entry == NULL) {
			file->f_pos = (loff_t) -1;
			break;
		}
		(void)gfsk_dir_update_node(file, entry, status);
		over = filldir(dirent, entry->d_name, entry->d_namlen,
			file->f_pos, entry->d_fileno, get_dtype(entry->d_type));
		if (over) {
			break;
		}
		file->f_pos += 1;
		count++;
	}
	gfarm_priv_put(file);
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

	if ((inode = gfsk_find_inode(sb, status->st_mode, status->st_ino,
		status->st_gen))) {
		iput(inode);
		return (0);
	}
	inode = gfarm_stat2inode(sb, status, 1);

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
