#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

#include "gfsk_fs.h"

static int
gfsk__set_page_dirty_no_writeback(struct page *page)
{
	if (!PageDirty(page))
		SetPageDirty(page);
	return (0);
}

const struct address_space_operations gfarm_file_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.set_page_dirty = gfsk__set_page_dirty_no_writeback,
};

const struct file_operations gfarm_file_operations = {
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	/*.fsync		= simple_sync_file, */
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.llseek		= generic_file_llseek,
};

const struct inode_operations gfarm_file_inode_operations = {
	.getattr	= simple_getattr,
};
