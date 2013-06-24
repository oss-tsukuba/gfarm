#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>

#include <gfarm/gfarm.h>
#include "gfsk_fs.h"
#include "gfsk_genfile.h"

struct inode_data {
	u64	ino;
	u64	gen;
	int	mode;
	int	new;
};
static int
gfsk_inode_eq(struct inode *inode, void *data)
{
	struct inode_data *idata =  (struct inode_data *) data;
	struct gfarm_inode *gi = get_gfarm_inode(inode);

	if (inode->i_ino == idata->ino
	&& ((inode->i_mode & S_IFMT) == (idata->mode & S_IFMT))) {
		if (gi->i_gen == idata->gen)
			return (1);
		if (!gi->i_gen || !idata->new) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"%s:ino=%lld gen=%lld:%lld"
				", but asume same. new=%d\n", __func__,
				 idata->ino, gi->i_gen, idata->gen, idata->new);
			return (1);
		} else
			gflog_info(GFARM_MSG_UNFIXED,
				"%s:ino=%lld gen=%lld:%lld"
				", so different. new=%d\n", __func__,
				 idata->ino, gi->i_gen, idata->gen, idata->new);
	}
	return (0);
}

static int
gfsk_inode_set(struct inode *inode, void *data)
{
	struct inode_data *idata =  (struct inode_data *) data;
	struct gfarm_inode *gi = get_gfarm_inode(inode);

	inode->i_ino = idata->ino;
	inode->i_mode = idata->mode;
	inode->i_generation = idata->gen;
	gi->i_gen = idata->gen;
	gi->i_direntsize = 0;
	INIT_LIST_HEAD(&gi->i_openfile);
	gi->i_wopencnt = 0;
	gfsk_set_cache_invalidate(inode);
	return (0);
}

struct inode *
gfsk_get_inode(struct super_block *sb, int mode, unsigned long ino, u64 gen,
	int new)
{
	struct inode *inode;
	struct inode_data data = {ino, gen, mode, new};

	inode = iget5_locked(sb, ino, gfsk_inode_eq, gfsk_inode_set, &data);
	if (inode == NULL)
		return (NULL);

	if (!(inode->i_state & I_NEW)) {
		return (inode);
	}
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	switch (mode & S_IFMT) {
	default:
		init_special_inode(inode, mode, 0);
		break;
	case S_IFREG:
		inode->i_op = &gfarm_file_inode_operations;
		inode->i_fop = &gfarm_file_operations;
		inode->i_mapping->a_ops = &gfarm_file_aops;
		break;
	case S_IFDIR:
		inode->i_op = &gfarm_dir_inode_operations;
		inode->i_fop = &gfarm_dir_operations;
		inode->i_mapping->a_ops = &gfarm_dir_aops;
		/* directory inodes start off with
		 * i_nlink == 2 (for "." entry) */
		inc_nlink(inode);
		break;
	case S_IFLNK:
		inode->i_op = &page_symlink_inode_operations;
		break;
	}
	unlock_new_inode(inode);
	return (inode);
}
