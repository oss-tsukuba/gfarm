#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/statfs.h>
#include <asm/uaccess.h>
#include <asm/string.h>

#include <gfarm/gfarm.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>

#include "gfsk_fs.h"
#include "gfsk_libgfarm.h"

#define GFARM_ERROR_TO_ERRNO(ge)	\
	((ge)  == GFARM_ERR_NO_ERROR ? 0 : -gfarm_error_to_errno(ge))
int
gfarm_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	gfarm_error_t ge;
	gfarm_off_t used, avail, files;

	ge = gfs_statfs(&used, &avail, &files);
	if (ge != GFARM_ERR_NO_ERROR)
		goto quit;

	memset(buf, 0, sizeof(*buf));
	buf->f_type = GFARM_MAGIC;
	buf->f_bsize = 1024;
	buf->f_blocks = used + avail;
	buf->f_bfree = avail;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = INT_MAX;
	buf->f_ffree = buf->f_files - files;
	buf->f_fsid.val[0] = 0;
	buf->f_fsid.val[1] = 0;
	buf->f_namelen = GFS_MAXNAMLEN;

	gflog_debug(0, "gfarm_statfs: type=0x%lx, bsize=%ld, blocks=%llu, "
		"bfree=%llu, bavail=%llu, files=%llu, ffree=%llu, "
		"fsid=0x%x:%x, namelen=%ld\n",
		buf->f_type, buf->f_bsize, buf->f_blocks, buf->f_bfree,
		buf->f_bavail, buf->f_files, buf->f_ffree,
		buf->f_fsid.val[0], buf->f_fsid.val[1], buf->f_namelen);
quit:
	return (GFARM_ERROR_TO_ERRNO(ge));
}

static char *
gfsk_make_path(struct dentry *dentry)
{
	int len = 1;
	struct dentry *ent = dentry;
	char *path, *p;

	if (IS_ROOT(ent)) {
		path = kmalloc(2, GFP_KERNEL);
		if (path != NULL) {
			path[0] = '/';
			path[1] = '\0';
		}
		return (path);
	}

	/* NOTE: IS_ROOT(dentry) is true when dentry is mount root */
	while (!IS_ROOT(ent)) {
		len += ent->d_name.len + 1;
		ent = ent->d_parent;
	}
	path = kmalloc(len, GFP_KERNEL);
	if (path == NULL) {
		return (NULL);
	}

	p = path + len - 1;
	*p = '\0';
	ent = dentry;
	while (!IS_ROOT(ent)) {
		p -= ent->d_name.len;
		memcpy(p, ent->d_name.name, ent->d_name.len);
		p--;
		*p = '/';
		ent = ent->d_parent;
	}
	return (path);
}

static void
gftime2ktime(struct gfarm_timespec *gftime, struct timespec *ktime)
{
	ktime->tv_sec = (__kernel_time_t) gftime->tv_sec;
	ktime->tv_nsec = (long) gftime->tv_nsec; /* XXX: int -> long */
}

static int
timeequal(struct gfarm_timespec *gftime, struct timespec *ktime) {
	int ret = ((gftime->tv_sec == ktime->tv_sec)
			&& ((long) gftime->tv_nsec == ktime->tv_nsec));
	if (!ret)
		gflog_debug(0, "time=%ld.%ld, %ld.%d",
			ktime->tv_sec, ktime->tv_nsec,
			gftime->tv_sec, gftime->tv_nsec);
	return (ret);
}

struct inode *
gfarm_stat_set(struct super_block *sb, struct gfs_stat *stp)
{
	struct inode *inode;
	struct gfarm_inode *gi;
	int ret;

	inode = gfsk_get_inode(sb, stp->st_mode, stp->st_ino, stp->st_gen);
	if (!inode)
		return (NULL);
	inode->i_nlink = stp->st_nlink; /* NOTE: uint <== gfarm_uint64_t */
	ret = ug_map_name_to_uid(stp->st_user, strlen(stp->st_user),
		&inode->i_uid);
	if (ret)
		inode->i_uid = UNKNOWN_UID;
	ret = ug_map_name_to_gid(stp->st_group, strlen(stp->st_group),
		&inode->i_gid);
	if (ret)
		inode->i_gid = UNKNOWN_GID;
	inode->i_size = stp->st_size;
	gftime2ktime(&stp->st_atimespec, &inode->i_atime);
	gftime2ktime(&stp->st_ctimespec, &inode->i_ctime);
	if (!timeequal(&stp->st_mtimespec, &inode->i_mtime)) {
		invalidate_dir_pages(inode);
	}
	gftime2ktime(&stp->st_mtimespec, &inode->i_mtime);
	inode->i_mode = stp->st_mode;

	gi = get_gfarm_inode(inode);
	gi->i_ncopy = stp->st_ncopy;
	gi->i_gen = stp->st_gen;
	return (inode);
}

int
gfarm_stat(struct dentry *dentry, struct inode **inodep)
{
	char *path;
	gfarm_error_t ge;
	struct gfs_stat gstat;
	struct inode *inode;

	if (inodep)
		*inodep = NULL;
	path = gfsk_make_path(dentry);
	if (path == NULL) {
		ge = GFARM_ERR_NO_MEMORY;
		goto quit;
	}
	ge = gfs_stat(path, &gstat);
	kfree(path);
	if (ge == GFARM_ERR_NO_ERROR) {
		inode = gfarm_stat_set(dentry->d_sb, &gstat);
		if (!inode) {
			ge = GFARM_ERR_NO_MEMORY;
			goto quit;
		}
		if (inodep)
			*inodep = inode;
		else
			iput(inode);
	}
quit:
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_mkdir(struct inode *dir, struct dentry * dentry, int mode)
{
	char *path;
	gfarm_error_t ge;

	path = gfsk_make_path(dentry);
	if (path != NULL) {
		/* NOTE: mode must be 0755 etc, not set S_IFDIR etc */
		ge = gfs_mkdir(path, mode);
		kfree(path);
	} else {
		ge = GFARM_ERR_NO_MEMORY;
	}
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_rmdir(struct inode *dir, struct dentry *dentry)
{
	char *path;
	gfarm_error_t ge;

	path = gfsk_make_path(dentry);
	if (path != NULL) {
		ge = gfs_rmdir(path);
		kfree(path);
	} else {
		ge = GFARM_ERR_NO_MEMORY;
	}
	return (GFARM_ERROR_TO_ERRNO(ge));
}

/****************************************************************/

int
gfarm_opendir(struct inode *inode, struct file *file)
{
	char *path;
	gfarm_error_t ge;
	struct dentry *dentry = file->f_path.dentry;
	struct gfsk_file_private *priv;

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return (-GFARM_ERR_NO_MEMORY);
	file->private_data = priv;
	path = gfsk_make_path(dentry);
	if (path != NULL) {
		memset(priv, 0, sizeof(*priv));
		ge = gfs_opendirplus(path, &priv->dirp);
		kfree(path);
	} else {
		ge = GFARM_ERR_NO_MEMORY;
	}
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_closedir(struct inode *inode, struct file *file)
{
	gfarm_error_t ge;
	struct gfsk_file_private *priv;

	priv = (struct gfsk_file_private *)file->private_data;
	if (priv && priv->dirp) {
		ge = gfs_closedirplus(priv->dirp);
		kfree(priv);
		file->private_data = NULL;
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	return (GFARM_ERROR_TO_ERRNO(ge));
}
