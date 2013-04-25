#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/statfs.h>
#include <asm/uaccess.h>
#include <asm/string.h>
#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include "queue.h"
#include "gfs_pio.h"
#include "context.h"
#include "gfsk_fs.h"
#include "gfsk_libgfarm.h"
#include "ug_idmap.h"

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

	gflog_debug(GFARM_MSG_UNFIXED,
		"gfarm_statfs: type=0x%lx, bsize=%ld, blocks=%llu, "
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
static void
ktime2gftime(struct timespec *ktime, struct gfarm_timespec *gftime)
{
	gftime->tv_sec = (gfarm_time_t)ktime->tv_sec;
	gftime->tv_nsec = ktime->tv_nsec; /* XXX: long -> int */
}

static int
timeequal(struct gfarm_timespec *gftime, struct timespec *ktime) {
	int ret = ((gftime->tv_sec == ktime->tv_sec)
			&& ((long) gftime->tv_nsec == ktime->tv_nsec));
	if (!ret)
		gflog_debug(GFARM_MSG_UNFIXED, "time=%ld.%ld, %ld.%d",
			ktime->tv_sec, ktime->tv_nsec,
			gftime->tv_sec, gftime->tv_nsec);
	return (ret);
}

static struct gfsk_file_private *
gfarm_priv_get(struct file *file) {
	return (struct gfsk_file_private *)file->private_data;
}

static struct gfsk_file_private *
gfarm_priv_alloc(struct file *file)
{
	struct gfsk_file_private *priv;
	if ((priv = gfarm_priv_get(file))) {
		return (priv);
	}
	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (priv != NULL) {
		memset(priv, 0, sizeof(*priv));
		file->private_data = priv;
		priv->f_file = file;
		INIT_LIST_HEAD(&priv->f_openlist);
		mutex_init(&priv->f_lock);
	}
	return (priv);
}

struct file *
gfsk_open_file_get(struct inode *inode)
{
	struct file *file = NULL;
	struct gfarm_inode *gi = get_gfarm_inode(inode);

	mutex_lock(&gfsk_fsp->gf_lock);
	if (!list_empty(&gi->i_openfile)) {
		struct gfsk_file_private *priv;
		priv = list_first_entry(&gi->i_openfile,
				struct gfsk_file_private, f_openlist);
		file = priv->f_file;
		get_file(file);
	}
	mutex_unlock(&gfsk_fsp->gf_lock);
	return (file);
}

static void
gfarm_priv_add_openfile(struct inode *inode, struct file *file)
{
	/* connect write open files for failover */
	if (file->f_flags & (O_WRONLY | O_RDWR)) {
		struct gfarm_inode *gi;
		struct gfsk_file_private *priv = gfarm_priv_get(file);
		if (list_empty(&priv->f_openlist)) {
			gi = get_gfarm_inode(inode);
			mutex_lock(&gfsk_fsp->gf_lock);
			list_add_tail(&priv->f_openlist, &gi->i_openfile);
			gi->i_wopencnt++;
			mutex_unlock(&gfsk_fsp->gf_lock);
			return;
		}
	}
}

static void
gfarm_priv_free(struct file *file)
{
	struct gfsk_file_private *priv = gfarm_priv_get(file);
	if (priv != NULL) {
		if (file->f_flags & (O_WRONLY | O_RDWR)) {
			struct gfarm_inode *gi;
			if (file->f_path.dentry != NULL) {
				gi = get_gfarm_inode(
					file->f_path.dentry->d_inode);
			} else {
				gi = NULL;
			}
			mutex_lock(&gfsk_fsp->gf_lock);
			if (!list_empty(&priv->f_openlist)) {
				list_del(&priv->f_openlist);
				if (gi != NULL) {
					gi->i_wopencnt--;
				}
			}
			mutex_unlock(&gfsk_fsp->gf_lock);
		}
		mutex_destroy(&priv->f_lock);
		kfree(priv);
		file->private_data = NULL;
	}
}

static void
gfarm_stat_set(struct inode *inode, struct gfs_stat *stp)
{
	struct gfarm_inode *gi;
	int ret;

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
	inode->i_generation = stp->st_gen;
	gftime2ktime(&stp->st_atimespec, &inode->i_atime);
	gftime2ktime(&stp->st_ctimespec, &inode->i_ctime);
	if (!timeequal(&stp->st_mtimespec, &inode->i_mtime)) {
		gfsk_invalidate_dir_pages(inode);
	}
	gftime2ktime(&stp->st_mtimespec, &inode->i_mtime);
	inode->i_mode = stp->st_mode;

	gi = get_gfarm_inode(inode);
	gi->i_ncopy = stp->st_ncopy;
	gi->i_gen = stp->st_gen;
	gi->i_actime = get_jiffies_64() + gfsk_fsp->gf_actime;
}
struct inode *
gfarm_stat2inode(struct super_block *sb, struct gfs_stat *stp)
{
	struct inode *inode;

	inode = gfsk_get_inode(sb, stp->st_mode, stp->st_ino, stp->st_gen, 0);
	if (!inode)
		return (NULL);
	gfarm_stat_set(inode, stp);
	return (inode);
}
int
gfarm_fstat(struct file *file, struct inode *inode)
{
	gfarm_error_t ge;
	struct gfsk_file_private *priv;
	struct gfs_stat gstat;

	priv = (struct gfsk_file_private *)file->private_data;
	if (S_ISREG(inode->i_mode) && priv && priv->f_u.filp) {
		ge = gfs_pio_stat(priv->f_u.filp, &gstat);
		if (ge == GFARM_ERR_NO_ERROR)
			gfarm_stat_set(inode, &gstat);
		else
			gflog_debug(GFARM_MSG_UNFIXED,
				"%s: gfs_pio_stat fail, %d",
				__func__, ge);
		return (GFARM_ERROR_TO_ERRNO(ge));
	} else
		return (gfarm_stat(file->f_dentry, NULL));
}

int
gfarm_stat(struct dentry *dentry, struct inode **inodep)
{
	char *path;
	gfarm_error_t ge;
	struct gfs_stat gstat;
	struct inode *inode;

	if (!dentry) {
		gflog_fatal(GFARM_MSG_UNFIXED, "dentry NULL");
	}
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
		inode = gfarm_stat2inode(dentry->d_sb, &gstat);
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
gfarm_setattr(struct dentry *dentry, struct iattr *attr, struct file *file)
{
	char *path;
	gfarm_error_t ge = GFARM_ERR_NO_ERROR;
	int err = 0;
	struct inode *inode = dentry->d_inode;

	if (attr->ia_valid & ATTR_SIZE) {
		if (!S_ISREG(dentry->d_inode->i_mode)) {
			return (-EINVAL);
		}
		if ((err = inode_newsize_ok(inode, attr->ia_size))) {
			return (err);
		}
		if (!file) {
			err = gfarm_truncate(inode, attr->ia_size);
			if (err)
				return (err);
		} else {
			struct gfsk_file_private *priv = gfarm_priv_get(file);
			if (priv && priv->f_u.filp) {
				ge = gfs_pio_truncate(priv->f_u.filp,
							attr->ia_size);
			} else
				ge = GFARM_ERR_INVALID_ARGUMENT;
		}
	}
	if (ge)
		goto ret;
	path = gfsk_make_path(dentry);
	if (path == NULL) {
		ge = GFARM_ERR_NO_MEMORY;
		goto ret;
	}
	if (attr->ia_valid & ATTR_MODE) {
		if ((ge = gfs_chmod(path, attr->ia_mode)) != GFARM_ERR_NO_ERROR)
			goto out;
	}
	if (attr->ia_valid & (ATTR_MTIME|ATTR_ATIME)) {
		struct gfarm_timespec	ts[2];

		if (attr->ia_valid & ATTR_ATIME) {
			ktime2gftime(&attr->ia_atime, &ts[0]);
		} else {
			ts[0].tv_nsec = GFARM_UTIME_OMIT;
		}
		if (attr->ia_valid & ATTR_MTIME) {
			ktime2gftime(&attr->ia_mtime, &ts[1]);
		} else {
			ts[1].tv_nsec = GFARM_UTIME_OMIT;
		}
		if ((ge = gfs_utimes(path, (const struct gfarm_timespec *)ts)))
			goto out;
	}
	if (attr->ia_valid & (ATTR_UID|ATTR_GID)) {
		char *user, *group;
		char userbuf[UG_IDMAP_NAMESZ], groupbuf[UG_IDMAP_NAMESZ];

		if (attr->ia_valid & ATTR_UID) {
			user = userbuf;
			err = ug_map_uid_to_name(attr->ia_uid, user,
				UG_IDMAP_NAMESZ);
		} else
			user = NULL;
		if (attr->ia_valid & ATTR_GID) {
			group = groupbuf;
			err = ug_map_gid_to_name(attr->ia_uid, group,
				UG_IDMAP_NAMESZ);
		} else
			group = NULL;
		if (err) {
			ge = gfarm_errno_to_error(err);
			goto out;
		} else if ((ge = gfs_chown(path, user, group)))
			goto out;
	}
	if (file)
		gfarm_fstat(file, inode);
	else
		gfarm_stat(dentry, NULL);
out:
	kfree(path);
ret:
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

int
gfarm_link(struct dentry *old_dentry, struct inode *new_dir,
		struct dentry *new_dentry)
{
	char *src, *dst;
	gfarm_error_t ge = GFARM_ERR_NO_MEMORY;

	src = gfsk_make_path(old_dentry);
	if (src != NULL) {
		dst = gfsk_make_path(new_dentry);
		if (dst != NULL) {
			ge = gfs_link(src, dst);
			kfree(dst);
		}
		kfree(src);
	}
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	char *src, *dst;
	gfarm_error_t ge = GFARM_ERR_NO_MEMORY;

	src = gfsk_make_path(old_dentry);
	if (src != NULL) {
		dst = gfsk_make_path(new_dentry);
		if (dst != NULL) {
			ge = gfs_rename(src, dst);
			kfree(dst);
		}
		kfree(src);
	}
	return (GFARM_ERROR_TO_ERRNO(ge));
}

/****************************************************************/
int
gfarm_opendir(struct inode *inode, struct file *file)
{
	char *path;
	gfarm_error_t ge = GFARM_ERR_NO_MEMORY;
	struct dentry *dentry = file->f_path.dentry;
	struct gfsk_file_private *priv;

	path = gfsk_make_path(dentry);
	if (path != NULL && (priv = gfarm_priv_alloc(file))) {
		ge = gfs_opendirplus(path, &priv->f_u.dirp);
		if (ge != GFARM_ERR_NO_ERROR) {
			gfarm_priv_free(file);
		}
	}
	kfree(path);
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_closedir(struct inode *inode, struct file *file)
{
	gfarm_error_t ge;
	struct gfsk_file_private *priv = gfarm_priv_get(file);

	if (priv && priv->f_u.dirp) {
		ge = gfs_closedirplus(priv->f_u.dirp);
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	gfarm_priv_free(file);
	return (GFARM_ERROR_TO_ERRNO(ge));
}

static int
gfsk_open_flags_gfarmize(int open_flags)
{
	int gfs_flags;

	switch (open_flags & O_ACCMODE) {
	case O_RDONLY:
		gfs_flags = GFARM_FILE_RDONLY;
		break;
	case O_WRONLY:
		gfs_flags = GFARM_FILE_WRONLY;
		break;
	case O_RDWR:
		gfs_flags = GFARM_FILE_RDWR;
		break;
	default:
		return (0);
	}

	if ((open_flags & O_CREAT) != 0)
		gfs_flags |= GFARM_FILE_CREATE;
	if ((open_flags & O_TRUNC) != 0)
		gfs_flags |= GFARM_FILE_TRUNC;
#if 0 /* not yet */
	if ((open_flags & O_APPEND) != 0)
		gfs_flags |= GFARM_FILE_APPEND;
	if ((open_flags & O_EXCL) != 0)
		gfs_flags |= GFARM_FILE_EXCLUSIVE;
#endif
	return (gfs_flags);
}

int
gfarm_createfile(struct inode *inode, struct dentry *dentry, int flag,
		int mode, struct file *file, struct inode **childp)
{
	char *path;
	gfarm_error_t ge = GFARM_ERR_NO_MEMORY;
	struct gfsk_file_private *priv;

	*childp = NULL;
	path = gfsk_make_path(dentry);
	if (path != NULL && (priv = gfarm_priv_alloc(file))) {
		int flags = (gfsk_open_flags_gfarmize(flag)
			| GFARM_FILE_UNBUFFERED);
		struct inode *child;
		gfarm_ino_t ino;
		gfarm_uint64_t gen;

		ge = gfs_pio_create_igen(path, (flags & ~GFARM_FILE_CREATE),
			(mode & GFARM_S_ALLPERM), &priv->f_u.filp, &ino, &gen);
		if (ge == GFARM_ERR_NO_ERROR) {
			child = gfsk_get_inode(inode->i_sb, mode|S_IFREG,
						ino, gen, 1);
			if (child) {
				gfarm_priv_add_openfile(child, file);
				*childp = child;
			} else {
				ge = GFARM_ERR_NO_MEMORY;
				gfs_pio_close(priv->f_u.filp);
				gfarm_priv_free(file);
			}
		} else {
			gfarm_priv_free(file);
			gflog_debug(GFARM_MSG_UNFIXED,
				"%s: gfs_pio_create_igen %s fail, %d",
				__func__, path, ge);
		}
	}
	kfree(path);
	return (GFARM_ERROR_TO_ERRNO(ge));
}
int
gfarm_unlink(struct inode *inode, struct dentry *dentry)
{
	char *path;
	gfarm_error_t ge = GFARM_ERR_NO_MEMORY;

	path = gfsk_make_path(dentry);
	if (path != NULL) {
		ge = gfs_unlink(path);
		kfree(path);
	}
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_openfile(struct inode *inode, struct file *file)
{
	char *path;
	gfarm_error_t ge = GFARM_ERR_NO_MEMORY;
	struct dentry *dentry = file->f_path.dentry;
	struct gfsk_file_private *priv;

	if (gfarm_priv_get(file)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"%s: already open inode=%p file=%p flag=%x",
			__func__, inode, file, file->f_flags);
		return (0);
	}
	path = gfsk_make_path(dentry);
	if (path != NULL && (priv = gfarm_priv_alloc(file))) {
		int flags = gfsk_open_flags_gfarmize(file->f_flags)
			| GFARM_FILE_UNBUFFERED;
		ge = gfs_pio_open(path, (flags & ~GFARM_FILE_CREATE),
			&priv->f_u.filp);
		if (ge == GFARM_ERR_NO_ERROR) {
			gfarm_priv_add_openfile(inode, file);
		} else {
			gfarm_priv_free(file);
		}
	}
	kfree(path);
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_closefile(struct inode *inode, struct file *file)
{
	gfarm_error_t ge;
	struct gfsk_file_private *priv = gfarm_priv_get(file);

	if (priv && priv->f_u.filp) {
		ge = gfs_pio_close(priv->f_u.filp);
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	gfarm_priv_free(file);

	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_read(struct file *file, loff_t off, char *buff, ssize_t size,
	int *readlen)
{
	gfarm_error_t ge;
	struct gfsk_file_private *priv = gfarm_priv_get(file);

	if (priv && priv->f_u.filp) {
		ge = gfs_pio_pread(priv->f_u.filp, buff, size,
				(gfarm_off_t)off, readlen);
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_write(struct file *file, loff_t off, const char *buff, ssize_t size,
	int *writelen)
{
	gfarm_error_t ge;
	struct gfsk_file_private *priv = gfarm_priv_get(file);

	if (priv && priv->f_u.filp) {
		ge = gfs_pio_pwrite(priv->f_u.filp, (void *)buff, size,
				(gfarm_off_t)off, writelen);
		if (ge == GFARM_ERR_NO_ERROR) {
			struct inode *inode =  file->f_dentry->d_inode;

			if (off + *writelen > inode->i_size)
				inode->i_size = off + *writelen;
			inode->i_mtime = CURRENT_TIME;
		}
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	return (GFARM_ERROR_TO_ERRNO(ge));
}
int
gfarm_append(struct file *file, const char *buff, ssize_t size,
			int *writelen, loff_t *offp)
{
	gfarm_error_t ge;
	struct gfsk_file_private *priv = gfarm_priv_get(file);
	gfarm_off_t fsize;

	if (priv && priv->f_u.filp) {
		ge = gfs_pio_append(priv->f_u.filp, (void *)buff, size,
			writelen, (gfarm_off_t *)offp, &fsize);
		if (ge == GFARM_ERR_NO_ERROR) {
			struct inode *inode =  file->f_dentry->d_inode;
			inode->i_size = fsize;
			inode->i_mtime = CURRENT_TIME;
		}
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfsk_get_localfd(struct file *file, int *fdp)
{
	gfarm_error_t ge;
	struct gfsk_file_private *priv;

	priv = (struct gfsk_file_private *)file->private_data;
	if (priv && priv->f_u.filp) {
		ge = gfs_pio_view_fd(priv->f_u.filp, fdp);
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	return (GFARM_ERROR_TO_ERRNO(ge));
}
int
gfarm_seek(struct file *file, loff_t off, int whence, loff_t *newoff)
{
	gfarm_error_t ge = GFARM_ERR_NO_ERROR;
	struct gfsk_file_private *priv = gfarm_priv_get(file);

	/* NOTE: don't call gfs_pio_seek(), it's meaningless
	 * because kernel module doesn't use gf->buffer.
	 */
	if (priv && priv->f_u.filp) {
		switch (whence) {
		case SEEK_SET:
			*newoff = off;
			break;
		case SEEK_CUR:
			*newoff = file->f_pos + off;
			break;
		case SEEK_END:
			*newoff = file->f_path.dentry->d_inode->i_size + off;
			break;
		default:
			ge = GFARM_ERR_INVALID_ARGUMENT;
			break;
		}
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_truncate(struct inode *inode, loff_t size)
{
	struct dentry *dentry;
	char *path;
	gfarm_error_t ge;
	GFS_File gf;

	dentry = list_first_entry(&inode->i_dentry, struct dentry, d_alias);
	path = gfsk_make_path(dentry);
	if (path == NULL)
		return (GFARM_ERROR_TO_ERRNO(GFARM_ERR_NO_ERROR));

	ge = gfs_pio_open(path, GFARM_FILE_WRONLY|GFARM_FILE_UNBUFFERED, &gf);
	if (ge == GFARM_ERR_NO_ERROR) {
		ge = gfs_pio_truncate(gf, size);
		gfs_pio_close(gf);
	}
	kfree(path);
	return (GFARM_ERROR_TO_ERRNO(ge));
}

int
gfarm_fsync(struct file *file, int datasync)
{
	gfarm_error_t ge = GFARM_ERR_NO_ERROR;
	struct gfsk_file_private *priv = gfarm_priv_get(file);

	if (priv && priv->f_u.filp) {
		if (datasync) {
			/* sync data only, not meta */
			ge = gfs_pio_datasync(priv->f_u.filp);
		} else {
			/* sync data and meta */
			ge = gfs_pio_sync(priv->f_u.filp);
		}
	} else
		ge = GFARM_ERR_INVALID_ARGUMENT;
	return (GFARM_ERROR_TO_ERRNO(ge));
}
