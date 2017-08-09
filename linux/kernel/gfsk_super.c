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
#include "gfsk_ccib.h"
#include "gfsk_proc.h"

/* static int gflog_level = GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG; */
static int gflog_level = LOG_INFO;

module_param(gflog_level, int, 0);
MODULE_PARM_DESC(gflog_level, "log level (0: emerge, ..., 7: debug)");

module_param(ug_timeout_sec, uint, 0);
MODULE_PARM_DESC(ug_timeout_sec, "TimeOut sec for ug_idmapd");


static struct kmem_cache *gfarm_inode_cachep;
static struct kmem_cache *gfarm_pages_cachep;

static struct inode *
gfsk_alloc_inode(struct super_block *sb)
{
	struct inode *inode;
	struct gfarm_inode *gi;

	inode = kmem_cache_alloc(gfarm_inode_cachep, GFP_KERNEL);
	if (!inode)
		return (NULL);
	gi = get_gfarm_inode(inode);

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

gflog_debug(GFARM_MSG_1004989, "%d\n", 0);
	err = gfsk_client_mount(sb, data);
gflog_debug(GFARM_MSG_1004990, "%d\n", err);
	if (err) {
		goto client_err;
	}

	gfsk_fsp->gf_bdi.name = "gfarm";
	if ((int)gfsk_fsp->gf_bdi.ra_pages == -1)
		gfsk_fsp->gf_bdi.ra_pages = default_backing_dev_info.ra_pages;
	gfsk_fsp->gf_bdi.unplug_io_fn = default_unplug_io_fn;
	if ((err = bdi_init(&gfsk_fsp->gf_bdi))) {
		gflog_error(GFARM_MSG_1004991, "bdi_init fail %d", err);
		goto end_err;
	}
	if ((err = bdi_register_dev(&gfsk_fsp->gf_bdi, sb->s_dev))) {
		gflog_error(GFARM_MSG_1004992, "bdi_register fail %d", err);
		goto end_err;
	}
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = GFARM_MAGIC;
	sb->s_op = &gfarm_ops;
	sb->s_time_gran = 1;

	inode = gfsk_get_inode(sb, S_IFDIR | 0777, GFARM_ROOT_INO,
			GFARM_ROOT_IGEN, 1);
	if (!inode) {
		err = -ENOMEM;
		gflog_debug(GFARM_MSG_1004993, "%d\n", err);
		goto end_err;
	}

	root = d_alloc_root(inode);
	sb->s_root = root;
	if (!root) {
		iput(inode);
		err = -ENOMEM;
		gflog_debug(GFARM_MSG_1004994, "%d\n", err);
		goto end_err;
	}
	return (0);
end_err:
	bdi_unregister(&gfsk_fsp->gf_bdi);
	bdi_destroy(&gfsk_fsp->gf_bdi);
client_err:
	gfsk_client_fini();
	return (err);
}

static int
gfarm_get_sb(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data, struct vfsmount *mnt)
{
	int ret;
	struct gfsk_mount_arg arg;
	GFSK_CTX_DECLARE_ZERO;

	if (!data)
		return (-EINVAL);

	GFSK_CTX_SET();
	arg.data = data;
	arg.mnt = mnt;
	ret = get_sb_nodev(fs_type, flags, &arg, gfarm_fill_super, mnt);
	GFSK_CTX_UNSET();
	return (ret);
}

static void
gfarm_kill_sb(struct super_block *sb)
{
	GFSK_CTX_DECLARE_SB(sb);

	GFSK_CTX_SET();
	if (gfsk_fsp && gfsk_fsp->gf_bdi.dev) {
		bdi_unregister(&gfsk_fsp->gf_bdi);
		bdi_destroy(&gfsk_fsp->gf_bdi);
	}
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
struct gfcc_pages *
gfcc_pages_alloc(int nowait)
{
	struct gfcc_pages *pages;
	pages = kmem_cache_alloc(gfarm_pages_cachep,
			nowait ? GFP_ATOMIC : GFP_KERNEL);
	return (pages);
}
void
gfcc_pages_free(struct gfcc_pages *pages)
{
	if (pages)
		kmem_cache_free(gfarm_pages_cachep, pages);
}
static void
gfarm_inode_init_once(void *foo)
{
	struct inode *inode = foo;

	inode_init_once(inode);
}

static int
gfarm_proc_loglevel_read(char *buf, int size, loff_t off,
		struct gfsk_procop *op)
{
	int n;
	if (off)
		return (0);
	n = snprintf(buf, size, "%d\n", gflog_get_priority_level());
	return (n);
}
static int
gfarm_proc_loglevel_write(char *buf, int size, loff_t off,
		struct gfsk_procop *op)
{
	int lv;
	if (sscanf(buf, "%d", &lv) == 1) {
		gflog_set_priority_level(lv);
		return (size);
	}
	return (-EINVAL);
}
static struct gfsk_procop procop_loglevel = {
	"loglevel", 0644, gfarm_proc_loglevel_read, gfarm_proc_loglevel_write};

static int __init
init_gfarm_fs(void)
{
	int ret;

	gflog_set_priority_level(gflog_level);
	gflog_set_message_verbose(2);
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
	gfarm_pages_cachep = kmem_cache_create("gfarm_readpages",
					      sizeof(struct gfcc_pages),
					      0, SLAB_HWCACHE_ALIGN,
					      NULL);
	if (!gfarm_pages_cachep) {
		gflog_error(GFARM_MSG_1005002,
			"kmem_cache_create(gfarm_pages) failed.");
		goto quit4;
	}
	if ((ret = gfarm_proc_init())) {
		gflog_error(GFARM_MSG_1005003,
			"gfarm_proc_init() failed. ret=%d", ret);
		goto quit5;
	}
	if ((ret = gfarm_procop_create(NULL, &procop_loglevel))) {
		gflog_error(GFARM_MSG_1005004,
			"gfarm_procop_create() failed. ret=%d", ret);
		goto quit5;
	}
	if ((ret = gfcc_ib_init())) {
		gflog_error(GFARM_MSG_1005005,
			"gfarm_ib_init() failed. ret=%d, ignore", ret);
		ret = 0;
	}
	gflog_info(GFARM_MSG_1005006, "init_gfarm_fs() end");
	return (0);
quit5:
	kmem_cache_destroy(gfarm_pages_cachep);
quit4:
	kmem_cache_destroy(gfarm_inode_cachep);
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

	gfcc_ib_fini();
	gfarm_procop_remove(NULL, &procop_loglevel);
	gfarm_proc_fini();
	if (gfarm_inode_cachep)
		kmem_cache_destroy(gfarm_inode_cachep);
	if (gfarm_pages_cachep)
		kmem_cache_destroy(gfarm_pages_cachep);
	unregister_filesystem(&gfarm_fs_type);
	ug_idmap_exit();
	gfskdev_fini();
	gflog_info(GFARM_MSG_1005008, "exit_gfarm_fs() end");
}

module_init(init_gfarm_fs)
module_exit(exit_gfarm_fs)
