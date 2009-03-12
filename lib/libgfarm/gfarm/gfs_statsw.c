#include <stddef.h>
#include <gfarm/gfarm.h>

#include "gfs_dircache.h"

struct gfs_statsw {
	gfarm_error_t (*opendir)(const char *, GFS_DirCaching *);
	gfarm_error_t (*readdir)(GFS_DirCaching, struct gfs_dirent **);
	gfarm_error_t (*closedir)(GFS_DirCaching);

	gfarm_error_t (*stat)(const char *, struct gfs_stat *);
};

/*
 * for gfs_statsw_uncached
 */

static struct gfs_statsw gfs_statsw_uncached = {
	(gfarm_error_t (*)(const char *, GFS_DirCaching *))gfs_opendir,
	(gfarm_error_t (*)(GFS_DirCaching, struct gfs_dirent **))gfs_readdir,
	(gfarm_error_t (*)(GFS_DirCaching))gfs_closedir,

	gfs_stat,
};

/*
 * for gfs_statsw_cached
 */

static struct gfs_statsw gfs_statsw_cached = {
	gfs_opendir_caching_internal,
	gfs_readdir_caching_internal,
	gfs_closedir_caching_internal,

	gfs_stat_cached_internal,
};

/*
 * for gfs_statsw
 */
static struct gfs_statsw *gfs_statsw = &gfs_statsw_cached;

gfarm_error_t
gfs_opendir_caching(const char *path, GFS_DirCaching *dirp)
{
	return ((*gfs_statsw->opendir)(path, dirp));
}

gfarm_error_t
gfs_readdir_caching(GFS_DirCaching dir, struct gfs_dirent **entry)
{
	return ((*gfs_statsw->readdir)(dir, entry));
}

gfarm_error_t
gfs_closedir_caching(GFS_DirCaching dir)
{
	return ((*gfs_statsw->closedir)(dir));
}

gfarm_error_t
gfs_stat_cached(const char *path, struct gfs_stat *st)
{
	return ((*gfs_statsw->stat)(path, st));
}

gfarm_error_t
gfs_lstat_cached(const char *path, struct gfs_stat *st)
{
	return ((*gfs_statsw->stat)(path, st)); /* XXX FIXME */
}

void
gfs_stat_cache_enable(int enable)
{
	gfs_statsw = enable ? &gfs_statsw_cached : &gfs_statsw_uncached;
}
