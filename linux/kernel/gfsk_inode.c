#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>

#include "gfsk_fs.h"

static int
gfsk_inode_eq(struct inode *inode, void *data)
{
	u64 gen =  *(u64 *) data;
	struct gfarm_inode *gi = get_gfarm_inode(inode);

	if (!gi->i_gen || gi->i_gen == gen)
		return (1);
	return (0);
}
static int
gfsk_inode_set(struct inode *inode, void *data)
{
	u64 gen =  *(u64 *) data;
	struct gfarm_inode *gi = get_gfarm_inode(inode);
	gi->i_gen = gen;
	gi->i_direntsize = 0;
	return (0);
}

struct inode *
gfsk_get_inode(struct super_block *sb, int mode, unsigned long ino, u64 gen)
{
	struct inode *inode;

	inode = iget5_locked(sb, ino, gfsk_inode_eq, gfsk_inode_set, &gen);
	if (inode == NULL)
		return (NULL);

	if (!(inode->i_state & I_NEW)) {
		return (inode);
	}
	if (ino > 0) {
		inode->i_ino = ino;
	}
	if (gen > 0) {
		struct gfarm_inode *gi = get_gfarm_inode(inode);
		inode->i_generation = gen;
		gi->i_gen = gen;
	}
	inode->i_mode = mode;
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

int
gfsk_make_node(struct inode *dir, struct dentry *dentry, int mode,
		unsigned long ino, u64 gen)
{
	struct inode *inode = gfsk_get_inode(dir->i_sb, mode, ino, gen);
	int error = -ENOSPC;

	if (inode) {
		if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return (error);
}
