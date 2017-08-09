#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <string.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm.h>
#include <gfarm/gfarm_config.h>
#include "context.h"
#include "config.h"
#include "gfsk_fs.h"
#include "gfsk_libgfarm.h"
#include "gfsk_genfile.h"
#include "gfsk_ccib.h"

const struct inode_operations gfarm_file_inode_operations = {
	.getattr	= gfsk_getattr,
	.setattr	= gfsk_setattr,
};

/**********************************************************************/

static int
gfsk_check_flags(int flags)
{
	/* O_ASYNC = FASYNC = 020000 */
	if (flags & FASYNC)
		return (-EINVAL);
	/* See nfs_check_flags() @ fs/nfs/file.c */
	if ((flags & (O_APPEND | O_DIRECT)) == (O_APPEND | O_DIRECT))
		return (-EINVAL);

	return (0);
}

static int
gfsk_file_open(struct inode *inode, struct file *file)
{
	int retval;
	GFSK_CTX_DECLARE_INODE(inode);

	GFSK_CTX_SET();
	retval = gfsk_check_flags(file->f_flags);
	if (retval == 0) {
		retval = gfarm_openfile(inode, file);
		if (retval == 0 && (file->f_flags & O_DIRECT)) {
			invalidate_inode_pages2(inode->i_mapping);
		} else if (retval == -ENOENT) {
			gflog_error(GFARM_MSG_1004910,
				"%s: ENOENT inode=%p file=%p",
				__func__, inode, file);
			if (inode)
				gfsk_iflag_set(inode, GFSK_INODE_STALE);
		}
	}
	GFSK_CTX_UNSET();
	return (retval);
}

static int
gfsk_file_flush(struct file *file, fl_owner_t id)
{
	struct gfarm_inode *gi = get_gfarm_inode(file->f_dentry->d_inode);

	if (gi->i_wopencnt == 1 &&
		(file->f_flags & (O_WRONLY|O_RDWR))) {
		vfs_fsync(file, file->f_path.dentry, 1);
	}
	return (0);
}
static int
gfsk_file_close(struct inode *inode, struct file *file)
{
	int retval;
	GFSK_CTX_DECLARE_INODE(inode);

	GFSK_CTX_SET();

	retval = gfarm_closefile(inode, file);
	if (!file->f_dentry->d_inode)
		gflog_fatal(GFARM_MSG_1004911,
			"%s: no inode inode=%p file=%p",
			__func__, inode, file);
	GFSK_CTX_UNSET();
	return (retval);
}

static ssize_t
gfsk_aio_read(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct inode *inode = iocb->ki_filp->f_path.dentry->d_inode;
	GFSK_CTX_DECLARE_INODE(inode);
	GFSK_CTX_SET();

	gfsk_check_cache_pages(inode);
	GFSK_CTX_UNSET();
	return (generic_file_aio_read(iocb, iov, nr_segs, pos));
}

static ssize_t
gfsk_aio_write(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_path.dentry->d_inode;
	GFSK_CTX_DECLARE_INODE(inode);
	GFSK_CTX_SET();

	gfsk_check_cache_pages(inode);

	gfarm_priv_wrote(file);
	if (file->f_flags & O_APPEND) {
		int	retval = 0;
		struct page *page, *tpage = NULL;
		int count = iov_length(iov, nr_segs);
		struct iov_iter it;
		int len, wrote, wrotelen = 0, poff, copied;
		char *kaddr, *taddr;
		ssize_t bytes;
		pgoff_t index;

		if (!(page = alloc_page(GFP_KERNEL))) {
			GFSK_CTX_UNSET();
			return (-ENOMEM);
		}

		iov_iter_init(&it, iov, nr_segs, count, 0);
		for ( ; (bytes = iov_iter_count(&it)) > 0; ) {
			if (bytes > PAGE_CACHE_SIZE)
				bytes = PAGE_CACHE_SIZE;
			pagefault_disable();
			len = iov_iter_copy_from_user_atomic(page, &it,
					0, bytes);
			pagefault_enable();

			kaddr = page_address(page);
			retval = gfarm_append(file, kaddr, len, &wrote, &pos);
			if (retval < 0) {
				break;
			}

			for (copied = 0; copied < wrote; copied += len) {
				index = (pos + copied) >> PAGE_CACHE_SHIFT;
				tpage = grab_cache_page_write_begin(
						file->f_mapping, index, 0);
				if (!tpage) {
					break;
				}
				taddr = kmap_atomic(tpage, KM_USER0);
				poff = (pos + copied) & (PAGE_CACHE_SIZE - 1);
				len = PAGE_CACHE_SIZE - poff;
				if (len > wrote - copied)
					len = wrote - copied;
				memcpy(kaddr + copied, taddr + poff, len);
				kunmap_atomic(taddr, KM_USER0);
				unlock_page(tpage);
				page_cache_release(tpage);
			}
			if (!copied)
				break;
			iov_iter_advance(&it, wrote);
			wrotelen += wrote;
			iocb->ki_pos = pos + wrote;
		}
		GFSK_CTX_UNSET();
		__free_page(page);
		return (wrotelen > 0 ? wrotelen : retval);
	}
	GFSK_CTX_UNSET();
	return (generic_file_aio_write(iocb, iov, nr_segs, pos));
}

static int
gfsk_writepage_sync(struct file *file, struct inode *inode, struct page *page,
	unsigned long pageoffset, unsigned int len)
{
	int retval;
	char	*buf;
	loff_t pos = page_offset(page) + pageoffset;
	int wrotelen;

	buf = kmap(page) + pageoffset;
	retval = gfarm_write(file, pos, buf, len, &wrotelen);
	if (retval == 0) {
		SetPageUptodate(page);
		gfsk_set_cache_updatetime(file->f_path.dentry->d_inode);
	}
	kunmap(page);

	return (retval);
}
/*
 * We are called with the page locked and we unlock it when done.
 */
static int
gfsk_readpage(struct file *file, struct page *page)
{
	int retval;
	char	*buf;
	loff_t pos = page_offset(page);
	size_t len = PAGE_CACHE_SIZE;
	int readlen;
	GFSK_CTX_DECLARE_FILE(file);
	GFSK_CTX_SET();

	page_cache_get(page);
	buf = kmap(page);
	retval = gfarm_read(file, pos, buf, len, &readlen);
	if (retval == 0) {
		if (readlen < len)
			memset(buf + readlen, 0, len - readlen);
		flush_dcache_page(page);
		SetPageUptodate(page);
		gfsk_set_cache_updatetime(file->f_path.dentry->d_inode);
	}
	kunmap(page);
	unlock_page(page);
	page_cache_release(page);

	GFSK_CTX_UNSET();
	return (retval);
}
int
gfsk_find_pages(struct gfcc_ctx *ctx, int npblk, struct gfcc_pblk *pblk,
		struct gfcc_pages *pages, int *npagep)
{
	struct gfcc_obj *obj = &pages->cp_obj;
	struct inode *inode;
	struct address_space *mapping;
	struct page *page;
	int i, j, n;
	pgoff_t index;

	pages->cp_npage = 0;
	if (!gfsk_fsp)
		gflog_fatal(GFARM_MSG_1004912, "no fsp");

	inode = gfsk_find_inode(gfsk_fsp->gf_sb, S_IFREG, obj->co_ino,
			obj->co_gen);
	if (!inode) {
		gflog_debug(GFARM_MSG_1004913, "no ino=%ld", obj->co_ino);
		return (-ENOENT);
	}
	if (!(mapping = inode->i_mapping)) {
		iput(inode);
		gflog_info(GFARM_MSG_1004914, "no mapping=%ld", obj->co_ino);
		return (-ENOENT);
	}
	gfsk_check_cache_pages(inode);
	*npagep = n = 0;
	for (i = 0; i < npblk; i++) {
		index = pblk[i].cs_index;
		for (j = pblk[i].cs_npage; j > 0; j--, index++) {
			if (!(page = find_get_page(mapping, index)))
				break;
			if (!PageUptodate(page)) {
				page_cache_release(page);
				break;
			}
			pages->cp_pages[n++] = page;
		}
		if (j) {
			gflog_debug(GFARM_MSG_1004915,
				"no %s page, ino=%lu index=%ld, "
				"requested from=%lu, found %d",
				page ? "uptodate" : "", obj->co_ino, index,
				(unsigned long) pblk[0].cs_index, n);
			break;
		}
	}
	*npagep = pages->cp_npage = n;
	iput(inode);
	return (0);
}

void
gfsk_validate_pages(struct gfcc_pages *pages, int nvalid, int invalid)
{
	struct page *page;
	int i, j, npage;

	npage = pages->cp_npage;
	if (pages->cp_inode) {
		if (nvalid)
			gfsk_set_cache_updatetime(pages->cp_inode);
	}
	for (i = 0; i < npage; i++) {
		page = pages->cp_pages[i];
		if (nvalid-- > 0) {
			SetPageUptodate(page);
		} else if (!invalid) {
			pages->cp_npage = npage - i;
			break;
		}
		unlock_page(page);
		page_cache_release(page);
	}
	if (invalid) {
		pages->cp_npage = 0;
	} else {
		if (i) {
			pages->cp_npage = npage - i;
			for (j = 0; i < npage; )
				pages->cp_pages[j++] = pages->cp_pages[i++];
		}
	}
}
void
gfsk_release_pages(struct gfcc_pages *pages)
{
	struct page *page;
	int i, npage;

	npage = pages->cp_npage;
	for (i = 0; i < npage; i++) {
		page = pages->cp_pages[i];
		page_cache_release(page);
	}
	pages->cp_npage = 0;
}
int
gfsk_cache_pages(struct address_space *mapping, struct list_head *head,
	unsigned nr_pages, struct gfcc_pages *pages)
{
	int n = 0;
	struct page *page, *next;

	list_for_each_entry_safe_reverse(page, next, head, lru) {
		list_del(&page->lru);
		if (add_to_page_cache_lru(page, mapping,
				page->index, GFP_KERNEL)) {
			page_cache_release(page);
		} else {
			pages->cp_pages[n++] = page;
			if (n == GFCC_PAGES_MAX)
				break;
		}
	}
	pages->cp_npage = n;
	return (n);
}

static gfarm_off_t
gfsk_read_page_cb(char *buf, gfarm_off_t off, int size, void *arg)
{
	struct gfcc_pages *pages = (struct gfcc_pages *)arg;
	int i, j, npage, res;
	int len, eof;
	loff_t req;
	char *addr;

	if ((res = ((unsigned long)buf & (PAGE_SIZE - 1)))) {
		buf += res;
		off += res;
		size -= res;
	}
	eof =  (off + size >= pages->cp_inode->i_size);
	req = -1;
	npage = pages->cp_npage;
	for (i = j = 0; i < npage; i++) {
		struct page *page = pages->cp_pages[i];
		loff_t pos = page_offset(page);
		loff_t lpos = pos + PAGE_CACHE_SIZE;
		if (lpos > pages->cp_inode->i_size)
			lpos = pages->cp_inode->i_size;
		if (lpos > off + size) {
			if (req == -1)
				req = pos;
			if (i != j)
				for (; i < npage;) {
					pages->cp_pages[j++] =
						pages->cp_pages[i++];
				}
			break;
		}
		if (pos < off) {
			if (req == -1)
				req = pos;
			pages->cp_pages[j++] = page;
			continue;
		}
		len = size - (pos - off);
		if (len > PAGE_CACHE_SIZE)
			len = PAGE_CACHE_SIZE;
		addr = kmap(page);
		memcpy(addr, buf + (pos - off), len);
		if (len < PAGE_CACHE_SIZE) {
			memset(addr + len, 0, PAGE_CACHE_SIZE - len);
		}
		kunmap(page);
		SetPageUptodate(page);
		unlock_page(page);
		page_cache_release(page);
		pages->cp_npage--;
	}
	gflog_debug(GFARM_MSG_1004916, "req=%lx npage=%d",
			(unsigned long)req, pages->cp_npage);
	return (req);
}
static int
gfsk_readpages(struct file *file, struct address_space *mapping,
	struct list_head *head, unsigned nr_pages)
{
	int err;
	struct inode *inode = mapping->host;
	int npage = nr_pages;
	struct page *page;
	loff_t	off, eoff;
	struct gfcc_pages *pages;
	GFSK_CTX_DECLARE_INODE(inode);
	GFSK_CTX_SET();

	if (!file && !(file = gfsk_open_file_get(inode))) {
		pr_err("gfsk_readpages: no file");
		goto end_ret;
	}
	if (!(pages = gfcc_pages_alloc(0))) {
		pr_err("gfsk_readpages: no pages");
		goto end_ret;
	}
	pages->cp_inode = inode;	/* i_count ?? */
	npage = gfsk_cache_pages(mapping, head, npage, pages);
	gflog_debug(GFARM_MSG_1004917, "index=%lx npage=%d",
		pages->cp_pages[0]->index, npage);
	if (npage > 0) {
		page = pages->cp_pages[0];
		off = (loff_t)page->index << PAGE_CACHE_SHIFT;
		page = pages->cp_pages[npage - 1];
		eoff = (loff_t)(page->index + 1) << PAGE_CACHE_SHIFT;
		err = gfarm_read_page(file, off, eoff - off, 0,
				gfsk_read_page_cb, pages);
		npage = pages->cp_npage;
	}
	if (npage > 0 && gfsk_fsp->gf_cc_ctxp) {
		struct gfcc_ibaddr ibaddr;
		struct gfarm_inode *gi = get_gfarm_inode(inode);

		gflog_debug(GFARM_MSG_1004918, "cc req index=%lx npage=%d",
			pages->cp_pages[0]->index, npage);
		pages->cp_obj.co_ino = inode->i_ino;
		pages->cp_obj.co_gen = gi->i_gen;
		page = pages->cp_pages[0];
		pages->cp_obj.co_off = (loff_t)page->index << PAGE_CACHE_SHIFT;
		pages->cp_obj.co_len = eoff - pages->cp_obj.co_off;
		if (!gfarm_cc_find_host(&pages->cp_obj, &ibaddr))
			npage = gfsk_cc_read(gfsk_fsp->gf_cc_ctxp,
				&ibaddr, pages, gfsk_fsp->gf_ra_async);
		if (gfsk_fsp->gf_ra_async && !npage) {
			goto end_ret;
		}
	}
	if (npage > 0) {
		page = pages->cp_pages[0];
		off = (loff_t)page->index << PAGE_CACHE_SHIFT;
		err = gfarm_read_page(file, off, eoff - off, 1,
				gfsk_read_page_cb, pages);
		npage = pages->cp_npage;
	}
	if (npage) {
		gfsk_validate_pages(pages, 0, 1);
	}
	gfcc_pages_free(pages);
	gfsk_set_cache_updatetime(inode);
end_ret:
	GFSK_CTX_UNSET();
	return (npage < 0 ? -EIO : 0);
}

static int
gfsk_writepage(struct page *page, struct writeback_control *wbc)
{
	int retval;
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	GFSK_CTX_DECLARE_INODE(inode);
	struct file *file;

	GFSK_CTX_SET();

	page_cache_get(page);

	if (!(file = gfsk_open_file_get(inode))) {
		pr_err("gfsk_writepage: no file");
		retval = -EIO;
	} else {
		loff_t size = file->f_path.dentry->d_inode->i_size;
		loff_t pageoff = page_offset(page);
		unsigned int len;
		if (pageoff + PAGE_CACHE_SIZE <= size) {
			len = PAGE_CACHE_SIZE;
		} else {
			len = size - pageoff;
		}
		retval = gfsk_writepage_sync(file, inode, page, 0, len);
		fput(file);
	}
	unlock_page(page);
	page_cache_release(page);

	GFSK_CTX_UNSET();
	return (retval);
}

static int
gfsk_write_begin(struct file *file, struct address_space *mapping,
	loff_t pos, unsigned len, unsigned flags,
	struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	*pagep = grab_cache_page_write_begin(mapping, index, flags);
	if (!*pagep)
		return (-ENOMEM);
	return (0);
}

static int
gfsk_write_end(struct file *file, struct address_space *mapping,
	loff_t pos, unsigned len, unsigned copied,
	struct page *page, void *fsdata)
{
	int status = 0;
	unsigned offset = pos & (PAGE_CACHE_SIZE - 1);
	GFSK_CTX_DECLARE_FILE(file);
	GFSK_CTX_SET();

	if (copied)
		status = gfsk_writepage_sync(file, mapping->host, page,
			offset, copied);
	if (!status)
		status = copied;
	unlock_page(page);
	page_cache_release(page);

	GFSK_CTX_UNSET();
	return (status);
}

static loff_t
gfsk_llseek(struct file *file, loff_t offset, int origin)
{
	int retval;
	GFSK_CTX_DECLARE_FILE(file);
	loff_t newoff;

	GFSK_CTX_SET();
	retval = gfarm_seek(file, offset, origin, &newoff);
	GFSK_CTX_UNSET();
	if (retval == 0) {
		file->f_pos = newoff;
		return (newoff);
	} else {
		return (-1);
	}
}

static int
gfsk_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	int retval;
	GFSK_CTX_DECLARE_FILE(file);

	GFSK_CTX_SET();
	retval = gfarm_fsync(file, datasync);
	GFSK_CTX_UNSET();
	return (retval);
}

struct gfsk_local_vmops_struct {
	struct vm_operations_struct m_vmops;
	struct vm_area_struct	*m_vma;
	const struct vm_operations_struct *m_hisvmops;
	struct file		*m_myfile;
	struct list_head	m_locallist;
};

void
gfsk_local_map_fini(struct gfsk_fs_context *fsp)
{
	struct gfsk_local_vmops_struct *lmp, *next;

	mutex_lock(&fsp->gf_lock);
	list_for_each_entry_safe(lmp, next, &fsp->gf_locallist, m_locallist) {
		list_del(&lmp->m_locallist);
		mutex_unlock(&fsp->gf_lock);

		lmp->m_vma->vm_ops = lmp->m_hisvmops;
		fput(lmp->m_myfile);
		kfree(lmp);

		mutex_lock(&fsp->gf_lock);
	}
	mutex_unlock(&fsp->gf_lock);
}

static void
gfsk_local_vma_close(struct vm_area_struct *vma)
{
	struct file *file;
	struct gfsk_local_vmops_struct  *lmp =
		(struct gfsk_local_vmops_struct *) vma->vm_ops;

	if (!vma->vm_file) {
		pr_warning("gfsk_local_vma_close:no vm_file");
	} else if (!lmp) {
		pr_warning("gfsk_local_vma_close:no vm_ops");
	} else if ((file = lmp->m_myfile)) {
		GFSK_CTX_DECLARE_FILE(file);
		GFSK_CTX_SET();

		mutex_lock(&gfsk_fsp->gf_lock);
		list_del(&lmp->m_locallist);
		mutex_unlock(&gfsk_fsp->gf_lock);

		GFSK_CTX_UNSET();
		vma->vm_ops = lmp->m_hisvmops;
		if (lmp->m_hisvmops && lmp->m_hisvmops->close) {
			lmp->m_hisvmops->close(vma);
		}
		pr_debug("gfsk_local_vma_close:local-file count=%ld %s",
			file_count(vma->vm_file), file->f_dentry->d_name.name);
		pr_debug("gfsk_local_vma_close:file count=%ld %s",
			file_count(file), file->f_dentry->d_name.name);

		kfree(lmp);
		fput(file);	/* ref in lmp */
	}
}

static void
gfsk_remote_vma_close(struct vm_area_struct *vma)
{
	gfsk_file_flush(vma->vm_file, 0);
};

struct vm_operations_struct gfsk_rmote_vm_ops = {
	.fault		= filemap_fault,
	.close		= gfsk_remote_vma_close,
};

static int
gfsk_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int	err;
	int	fd;
	GFSK_CTX_DECLARE_FILE(file);
	GFSK_CTX_SET();
	if ((err =  gfsk_get_localfd(file, &fd))) {
		goto out;
	}
	if (fd >= 0) {
		struct file *local = NULL;
		struct gfsk_local_vmops_struct *lmp;

		if (!(lmp = kzalloc(sizeof(*lmp), GFP_KERNEL))) {
			err = -ENOMEM;
			goto out;
		}
		if ((err = gfsk_fd2file(fd, &local)) < 0) {
			pr_err("gfsk_file_mmap:no local %s:%s",
				file->f_dentry->d_name.name, strerror(-err));
			goto local_out;
		}
		if (!local->f_op || !local->f_op->mmap) {
			pr_err("gfsk_file_mmap:no local f_op %s",
				file->f_dentry->d_name.name);
			err = -EINVAL;
			goto local_out;
		}
		vma->vm_file = local;	/* ref to vm_file */
		GFSK_CTX_CLEAR();
		err = local->f_op->mmap(local, vma);
		GFSK_CTX_RESTORE();
		if (err) {
			pr_err("gfsk_file_mmap:local mmap fail %s",
				file->f_dentry->d_name.name);
			vma->vm_file = file;
			goto local_out;
		}
		lmp->m_myfile = file; 	/* ref from vm_file */
		lmp->m_vma = vma;
		lmp->m_hisvmops = vma->vm_ops;
		if (vma->vm_ops)
			lmp->m_vmops = *vma->vm_ops;
		lmp->m_vmops.close = gfsk_local_vma_close;
		vma->vm_ops = &lmp->m_vmops;

		mutex_lock(&gfsk_fsp->gf_lock);
		list_add(&lmp->m_locallist, &gfsk_fsp->gf_locallist);
		mutex_unlock(&gfsk_fsp->gf_lock);
		file_accessed(file);
		pr_debug("gfsk_file_mmap:local mmap %s",
			file->f_dentry->d_name.name);
local_out:
		if (err) {
			kfree(lmp);
			if (local)
				fput(local);
			goto out;
		}
	} else {
		file_accessed(file);
		vma->vm_ops = &gfsk_rmote_vm_ops;
		vma->vm_flags |= VM_CAN_NONLINEAR;
		err = 0;
	}
out:
	GFSK_CTX_UNSET();
	return (err);
}

const struct address_space_operations gfarm_file_aops = {
	.readpage	= gfsk_readpage,
	.readpages	= gfsk_readpages,
	.writepage	= gfsk_writepage,
	.write_begin	= gfsk_write_begin,
	.write_end	= gfsk_write_end,
	.set_page_dirty = __set_page_dirty_nobuffers,
};

const struct file_operations gfarm_file_operations = {
	.open		= gfsk_file_open,
	.flush		= gfsk_file_flush,
	.release	= gfsk_file_close,
	.llseek		= gfsk_llseek,
	.read		= do_sync_read,
	.aio_read	= gfsk_aio_read,
	.write		= do_sync_write,
	.aio_write	= gfsk_aio_write,
	.fsync		= gfsk_fsync,
	.mmap		= gfsk_file_mmap,
	.flock		= NULL /* use system default handling */
};
