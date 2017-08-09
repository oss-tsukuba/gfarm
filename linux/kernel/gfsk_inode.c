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
	int	maybe;
};
static int
gfsk_inode_eq(struct inode *inode, void *data)
{
	struct inode_data *idata =  (struct inode_data *) data;
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	int valid = 0;

	if (gfsk_iflag_isset(inode, GFSK_INODE_STALE))
		valid = 0;
	else if (inode->i_ino == idata->ino) {
		if (gi->i_gen == idata->gen)
			valid = 1;
		else if (((inode->i_mode & S_IFMT) != (idata->mode & S_IFMT)))
			valid = 0;
		else if (!S_ISREG(inode->i_mode))
			valid = 0;
		else if (!gi->i_gen || !idata->new) {
			gflog_debug(GFARM_MSG_1004958,
				"%s:ino=%lld gen=my %lld:%lld"
				", but asume same. new=%d\n", __func__,
				 idata->ino, gi->i_gen, idata->gen, idata->new);
			valid = 1;
			if (idata->gen > gi->i_gen)
				gfsk_iflag_set(inode, GFSK_INODE_MAYSTALE);
		} else if (idata->maybe && (idata->gen <= gi->i_gen)) {
			gflog_debug(GFARM_MSG_1004959,
				"%s:ino=%lld gen=my %lld:%lld"
				", but maybe same. new=%d\n", __func__,
				 idata->ino, gi->i_gen, idata->gen, idata->new);
			valid = 1;
		} else if (list_empty(&inode->i_dentry)) {
			gflog_info(GFARM_MSG_1004960,
				"%s:ino=%lld gen=my %lld:%lld"
				", but set same. new=%d\n", __func__,
				 idata->ino, gi->i_gen, idata->gen, idata->new);
			valid = 1;
			gfsk_iflag_set(inode, GFSK_INODE_MAYSTALE);
		} else {
			gflog_info(GFARM_MSG_1004961,
				"%s:ino=%lld gen=my %lld:%lld"
				", so different. new=%d\n", __func__,
				 idata->ino, gi->i_gen, idata->gen, idata->new);
			valid = 0;
		}
		if (!valid) {
			gfsk_iflag_set(inode, GFSK_INODE_STALE);
		}
	}
	return (valid);
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
	INIT_LIST_HEAD(&gi->i_openfile);
	gi->i_wopencnt = 0;
	gi->i_iflag = 0;
	gfsk_set_cache_invalidate(inode);
	return (0);
}

struct inode *
gfsk_get_inode(struct super_block *sb, int mode, unsigned long ino, u64 gen,
	int new)
{
	struct inode *inode;
	struct gfarm_inode *gi;
	struct inode_data data = {ino, gen, mode, new, 0};

	inode = iget5_locked(sb, ino, gfsk_inode_eq, gfsk_inode_set, &data);
	if (inode == NULL)
		return (NULL);

	gi = get_gfarm_inode(inode);
	if (!(inode->i_state & I_NEW)) {
		if (gfsk_iflag_isset(inode, GFSK_INODE_MAYSTALE)) {
			gfsk_iflag_unset(inode, GFSK_INODE_MAYSTALE);
			gfsk_invalidate_pages(inode);
			inode->i_generation = gen;
			gi->i_gen = gen;
		}
		return (inode);
	}
	mutex_init(&gi->i_mutex);
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
struct inode *
gfsk_find_inode(struct super_block *sb, int mode, unsigned long ino, u64 gen)
{
	struct inode *inode;
	struct inode_data data = {ino, gen, mode, 1, 1};
	inode = ilookup5(sb, ino, gfsk_inode_eq, &data);
	return (inode);
}
