#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>	/* for kmem_cache_destroy */
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/parser.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/string.h>

#include <gfarm/gfarm.h>

#include "gfsk.h"
#include "gfsk_fs.h"
#include "gfsk_devif.h"
#include "ug_idmap.h"
#include "gfsk_libgfarm.h"

/* static int gflog_level = GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG; */
static int gflog_level = LOG_DEBUG;

module_param(gflog_level, int, 0);
MODULE_PARM_DESC(gflog_level, "log level (0: emerge, ..., 7: debug)");

module_param(ug_timeout_sec, uint, 0);
MODULE_PARM_DESC(ug_timeout_sec, "TimeOut sec for ug_idmapd");


static struct kmem_cache *gfarm_inode_cachep;

static struct inode *
gfsk_alloc_inode(struct super_block *sb)
{
	struct inode *inode;

	inode = kmem_cache_alloc(gfarm_inode_cachep, GFP_KERNEL);
	if (!inode)
		return (NULL);

	return (inode);
}

static void
gfsk_destroy_inode(struct inode *inode)
{
	kmem_cache_free(gfarm_inode_cachep, inode);
}

static void
gfsk_clear_inode(struct inode *inode)
{
}

static int
gfsk_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int ret;
	GFSK_CTX_DECLARE_DENTRY(dentry);

	GFSK_CTX_SET();
	ret = gfarm_statfs(dentry, buf);
	GFSK_CTX_UNSET();
	return (ret);
}

#if 0
static void
gfsk_umount_begin(struct super_block *sb)
{
	GFSK_CTX_DECLARE_SB(sb);

	GFSK_CTX_SET();
	gflog_debug(GFARM_MSG_1004987, "gfsk_umount_begin() called");
	gfskdev_abort_conn(gfsk_fsp->gf_dc);
	GFSK_CTX_UNSET();
}
#endif

static void
gfsk_put_super(struct super_block *sb)
{
	gflog_debug(GFARM_MSG_1004988, "gfsk_put_super() called");
}

static const struct super_operations gfarm_ops = {
	.alloc_inode = gfsk_alloc_inode,
	.destroy_inode  = gfsk_destroy_inode,
	.clear_inode	= gfsk_clear_inode,
	.drop_inode	= generic_delete_inode,
	.statfs = gfsk_statfs,
	.put_super = gfsk_put_super,
	.show_options = generic_show_options
};

static int
gfarm_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode = NULL;
	struct dentry *root;
	int err;

	err = gfsk_client_mount(sb, data);
	if (err) {
		gfsk_client_fini();
		return (err);
	}
	save_mount_options(sb, data);

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = GFARM_MAGIC;
	sb->s_op = &gfarm_ops;
	sb->s_time_gran = 1;

	inode = gfsk_get_inode(sb, S_IFDIR | 0777, GFARM_ROOT_INO,
			GFARM_ROOT_IGEN, 1);
	if (!inode) {
		return (-ENOMEM);
	}

	root = d_alloc_root(inode);
	sb->s_root = root;
	if (!root) {
		err = -ENOMEM;
		iput(inode);
	}

	return (err);
}

static int
gfarm_get_sb(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data, struct vfsmount *mnt)
{
	int ret;
	GFSK_CTX_DECLARE_ZERO;

	if (!data)
		return (-EINVAL);

	GFSK_CTX_SET();
	ret = get_sb_nodev(fs_type, flags, data, gfarm_fill_super, mnt);
	GFSK_CTX_UNSET();
	return (ret);
}

static void
gfarm_kill_sb(struct super_block *sb)
{
	GFSK_CTX_DECLARE_SB(sb);

	GFSK_CTX_SET();
	gflog_debug(GFARM_MSG_1004995, "gfarm_kill_sb() start");
	gfsk_client_fini();
	generic_shutdown_super(sb);
	gflog_debug(GFARM_MSG_1004996, "gfarm_kill_sb() end");
	GFSK_CTX_UNSET();
}

static struct file_system_type gfarm_fs_type = {
	.name = "gfarm",
	.get_sb = gfarm_get_sb,
	.kill_sb = gfarm_kill_sb,
	.fs_flags = FS_BINARY_MOUNTDATA,
};
static void
gfarm_inode_init_once(void *foo)
{
	struct inode *inode = foo;

	inode_init_once(inode);
}

static int __init
init_gfarm_fs(void)
{
	int ret;

	gflog_set_priority_level(gflog_level);
	gflog_info(GFARM_MSG_1004997, "init_gfarm_fs() start");

	ret = gfskdev_init();

	if (ret) {
		gflog_error(GFARM_MSG_1004998,
			"gfsk_dev_init() failed. ret=%d", ret);
		return (ret);
	}
	ret = ug_idmap_init();
	if (ret) {
		gflog_error(GFARM_MSG_1004999,
			"ug_idmap_init() failed. ret=%d", ret);
		goto quit1;
	}
	ret = register_filesystem(&gfarm_fs_type);
	if (ret) {
		gflog_error(GFARM_MSG_1005000,
			"register_filesystem() failed. ret=%d", ret);
		goto quit2;
	}
	gfarm_inode_cachep = kmem_cache_create("gfarm_inode",
					      sizeof(struct gfarm_inode),
					      0, SLAB_HWCACHE_ALIGN,
					      gfarm_inode_init_once);
	if (!gfarm_inode_cachep) {
		gflog_error(GFARM_MSG_1005001,
			"kmem_cache_create(gfarm_inode) failed.");
		goto quit3;
	}
	gflog_info(GFARM_MSG_1005006, "init_gfarm_fs() end");
	return (0);
quit3:
	unregister_filesystem(&gfarm_fs_type);
quit2:
	ug_idmap_exit();
quit1:
	gfskdev_fini();
	return (ret);
}

static void __exit
exit_gfarm_fs(void)
{
	gflog_info(GFARM_MSG_1005007, "exit_gfarm_fs() start");
	if (gfarm_inode_cachep)
		kmem_cache_destroy(gfarm_inode_cachep);
	unregister_filesystem(&gfarm_fs_type);
	ug_idmap_exit();
	gfskdev_fini();
	gflog_info(GFARM_MSG_1005008, "exit_gfarm_fs() end");
}

module_init(init_gfarm_fs)
module_exit(exit_gfarm_fs)
