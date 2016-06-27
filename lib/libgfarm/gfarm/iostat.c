#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <gfarm/gfarm.h>
#include <gfarm/gfarm_iostat.h>
/* #define GFARM_NOCONTEXT */
#ifndef GFARM_NOCONTEXT
#include "context.h"
#endif
#include "iostat.h"

struct gfarm_iostat_static {
	struct gfarm_iostat_head	*stat_hp;
	struct gfarm_iostat_items	*stat_sip;
	struct gfarm_iostat_items	*stat_local_ip;
	gfarm_off_t		stat_size;
};

#define DEF_CACHE_LINESIZE 64
static unsigned int cache_linesize = DEF_CACHE_LINESIZE;

#define is_statfile_valid(hp, sip)	\
	(staticp && (hp = staticp->stat_hp) && (sip = staticp->stat_sip))

#ifdef GFARM_NOCONTEXT
struct gfarm_iostat_static iostat_static, *staticp;
#else
#define staticp (gfarm_ctxp->iostat_static)

gfarm_error_t
gfarm_iostat_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_iostat_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	memset(s, 0, sizeof(*s));
	ctxp->iostat_static = s;
#ifdef _SC_LEVEL1_DCACHE_LINESIZE
	cache_linesize = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
	if (cache_linesize & 0x1f)
		cache_linesize = DEF_CACHE_LINESIZE;
	else if (cache_linesize > 0x100)
		cache_linesize = 0x100;
#endif
	return (GFARM_ERR_NO_ERROR);
}
void
gfarm_iostat_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_iostat_static *s = ctxp->iostat_static;

	if (s == NULL)
		return;
	ctxp->iostat_static = NULL;

	free(s);
}
#endif


gfarm_error_t
gfarm_iostat_mmap(char *path, struct gfarm_iostat_spec *specp,
		unsigned int nitem, unsigned int row)
{
	int fd;
	gfarm_error_t e;
	size_t	size, off;
	void	*addr;
	struct gfarm_iostat_head head, *hp = &head;
	int	ncol;
	int	scol;

	scol = sizeof(((struct gfarm_iostat_items *)0)->s_valid);
	ncol = (((nitem + 1) * scol + (cache_linesize - 1))
		& ~(cache_linesize - 1)) / scol;

	off = sizeof(struct gfarm_iostat_head)
		+ sizeof(struct gfarm_iostat_spec) * nitem;
	off = (off + (cache_linesize - 1)) & ~(cache_linesize - 1);

	memset(hp, 0, sizeof(*hp));
	hp->s_magic = GFARM_IOSTAT_MAGIC;
	hp->s_nitem = nitem;
	hp->s_row = row;
	hp->s_rowcur = 0;
	hp->s_rowmax = 0;
	hp->s_start_sec = hp->s_update_sec = time(0);
	hp->s_item_off = off;
	hp->s_ncolumn = ncol;
	hp->s_item_size = scol * ncol;

	size = off + hp->s_item_size * row;
	strncpy(hp->s_name, basename(path), GFARM_IOSTAT_NAME_MAX);

	if ((fd = open(path, O_CREAT|O_TRUNC|O_RDWR, 0644)) < 0) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1003591,
			"gfarm_iostat_mmap(%s) open failed: %s",
			path, gfarm_error_string(e));
		return (e);
	}
	if (ftruncate(fd, size) < 0) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1003592,
			"gfarm_iostat_mmap(%s) truncate %zu failed: %s",
			path, size, gfarm_error_string(e));
		close(fd);
		return (e);
	}
	if (write(fd, hp, sizeof(*hp)) < 0) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1003593,
			"gfarm_iostat_mmap(%s) write head failed: %s",
			path, gfarm_error_string(e));
		close(fd);
		return (e);
	}
	if (write(fd, specp, sizeof(*specp) * nitem) < 0) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1003594,
			"gfarm_iostat_mmap(%s) write spec failed: %s",
			path, gfarm_error_string(e));
		close(fd);
		return (e);
	}

	if ((addr = mmap(NULL, size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0))
		== MAP_FAILED) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1003595,
			"gfarm_iostat_mmap(%s) mmap %zu failed: %s",
			path, size, gfarm_error_string(e));
		close(fd);
		return (e);
	}
	close(fd);
#ifdef GFARM_NOCONTEXT
	if (!staticp)
		staticp = &iostat_static;
#endif
	staticp->stat_hp = (struct gfarm_iostat_head *)addr;
	staticp->stat_sip = (struct gfarm_iostat_items *)((char*)addr + off);
	staticp->stat_size = size;

	return (GFARM_ERR_NO_ERROR);
}
void
gfarm_iostat_sync(void)
{
	if (staticp && staticp->stat_hp)
		msync(staticp->stat_hp, staticp->stat_size, MS_SYNC);
}
static inline struct gfarm_iostat_items *
gfarm_iostat_find_row(struct gfarm_iostat_head *hp,
	struct gfarm_iostat_items *sip, gfarm_uint64_t id, int i, int upto,
	int *ind)
{
	struct gfarm_iostat_items *ip;
	int isize = hp->s_item_size;
	for (; i < upto; i++) {
		ip = (struct gfarm_iostat_items *)((char *)sip + i * isize);
		if (ip->s_valid == id) {
			*ind = i;
			return (ip);
		}
	}
#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	*ind = 0;
#endif
	return (NULL);
}
void
gfarm_iostat_clear_id(gfarm_uint64_t id, unsigned int hint)
{
	int i;
	struct gfarm_iostat_items *ip;
	struct gfarm_iostat_head *hp; struct gfarm_iostat_items *sip;

	if (!is_statfile_valid(hp, sip))
		return;

	ip = gfarm_iostat_find_row(hp, sip, id, hint, hp->s_rowcur, &i);
	if (!ip && hint)
		ip = gfarm_iostat_find_row(hp, sip, id, 0, hint, &i);

	if (!ip) {
		gflog_error(GFARM_MSG_1003596,
			"gfarm_iostat_clear_id(%s) id(%u) not found",
			hp->s_name, (unsigned int)id);
		return;
	}
	ip->s_valid = 0;
	hp->s_update_sec = time(0);
	if (i == hp->s_rowcur - 1) {
		int isize = hp->s_item_size;
		hp->s_rowcur--;
		for (; i >= 0; i--) {
			ip = (struct gfarm_iostat_items *)((char *)sip
						 + i * isize);
			if (ip->s_valid) {
				hp->s_rowcur = i + 1;
				break;
			}
		}
	}
}
void
gfarm_iostat_clear_ip(struct gfarm_iostat_items *ip)
{
	int i;
	struct gfarm_iostat_head *hp; struct gfarm_iostat_items *sip;

	if (!is_statfile_valid(hp, sip))
		return;
	if (!ip) {
		gflog_debug(GFARM_MSG_1003597, "not initialized");
		return;
	}

	i = ((char *)ip - (char *)sip) / hp->s_item_size;
	if (i < 0 || i >= hp->s_row) {
		gflog_error(GFARM_MSG_1003598,
			"gfarm_iostat_clear_ip(%s) i(%d) not found",
			hp->s_name, i);
		return;
	}
	ip->s_valid = 0;
	hp->s_update_sec = time(0);
	if (i == hp->s_rowcur - 1) {
		int isize = hp->s_item_size;
		hp->s_rowcur--;
		for (; i >= 0; i--) {
			ip = (struct gfarm_iostat_items *)((char *)sip
						 + i * isize);
			if (ip->s_valid) {
				hp->s_rowcur = i + 1;
				break;
			}
		}
	}
}

struct gfarm_iostat_items*
gfarm_iostat_find_space(unsigned int hint)
{
	int i;
	struct gfarm_iostat_items *ip;
	struct gfarm_iostat_head *hp; struct gfarm_iostat_items *sip;

	if (!is_statfile_valid(hp, sip))
		return (NULL);

	ip = gfarm_iostat_find_row(hp, sip, 0, hint, hp->s_rowcur, &i);
	if (ip)
		gflog_debug(GFARM_MSG_1003599, "found 1 %d into %p", i, ip);
	if (!ip && hint)
		ip = gfarm_iostat_find_row(hp, sip, 0, 0, hint, &i);
	if (ip)
		gflog_debug(GFARM_MSG_1003600, "found 2 %d into %p", i, ip);
	if (!ip) {
		ip = gfarm_iostat_find_row(hp, sip, 0, hp->s_rowcur,
			hp->s_row, &i);
		if (ip) {
			hp->s_rowcur = i + 1;
			if (i >= hp->s_rowmax)
				hp->s_rowmax = i + 1;
		} else {
			gflog_error(GFARM_MSG_1003601,
				"gfarm_iostat_find_space(%s) small row %d",
				hp->s_name, hp->s_row);
		}
	}
	hp->s_update_sec = time(0);

	return (ip);
}
struct gfarm_iostat_items*
gfarm_iostat_get_ip(unsigned int i)
{
	struct gfarm_iostat_items *ip;
	struct gfarm_iostat_head *hp; struct gfarm_iostat_items *sip;

	if (!is_statfile_valid(hp, sip))
		return (NULL);

	if (i >= hp->s_row) {
		gflog_error(GFARM_MSG_1003602,
			"gfarm_iostat_get_ip(%s) too big id %d",
			hp->s_name, i);
		return (NULL);
	}
	ip = (struct gfarm_iostat_items *)((char *)sip + i * hp->s_item_size);
	ip->s_valid = i;
	if (i >= hp->s_rowcur) {
		hp->s_rowcur = i + 1;
		if (i >= hp->s_rowmax)
			hp->s_rowmax = i + 1;
	}
	hp->s_update_sec = time(0);

	return (ip);
}
void
gfarm_iostat_set_id(struct gfarm_iostat_items *ip, gfarm_uint64_t id)
{
	if (ip)
		ip->s_valid = id;
}
void
gfarm_iostat_set_local_ip(struct gfarm_iostat_items *ip)
{
	struct gfarm_iostat_head *hp; struct gfarm_iostat_items *sip;

	if (!is_statfile_valid(hp, sip))
		return;

	staticp->stat_local_ip = ip;
}
void
gfarm_iostat_stat_add(struct gfarm_iostat_items *ip, unsigned int cat, int val)
{
	struct gfarm_iostat_head *hp; struct gfarm_iostat_items *sip;

	if (!is_statfile_valid(hp, sip))
		return;
	if (!ip) {
		gflog_debug(GFARM_MSG_1003603, "not initialized");
		return;
	}
	if (!ip->s_valid) {
		gflog_error(GFARM_MSG_1003604,
			"gfarm_iostat_stat_add(%s) invalid ip %p",
			hp->s_name, ip);
		return;
	}
	if (cat >= hp->s_nitem) {
		gflog_error(GFARM_MSG_1003605,
			"gfarm_iostat_stat_add(%s) too big cat %d",
			hp->s_name, cat);
		return;
	}
	ip->s_vals[cat] += val;
}
void
gfarm_iostat_local_add(unsigned int cat, int val)
{
	struct gfarm_iostat_head *hp; struct gfarm_iostat_items *sip, *ip;

	if (!is_statfile_valid(hp, sip))
		return;
	if (!(ip = staticp->stat_local_ip)) {
		gflog_debug(GFARM_MSG_1003606, "not initialized");
		return;
	}
	gfarm_iostat_stat_add(ip, cat, val);
}
