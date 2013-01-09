#include <stddef.h>

#include <gfarm/gfarm.h>

gfarm_error_t
gfs_rmdir(const char *path)
{
	gfarm_error_t e;
	struct gfs_stat st;
	int is_dir;

	e = gfs_lstat(path, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001394,
			"gfs_lstat(%s) failed: %s",
			path,
			gfarm_error_string(e));
		return (e);
	}
	is_dir = GFARM_S_ISDIR(st.st_mode);
	gfs_stat_free(&st);
	if (!is_dir) {
		gflog_debug(GFARM_MSG_1001395,
			"Not a directory(%s): %s",
			path,
			gfarm_error_string(GFARM_ERR_NOT_A_DIRECTORY));
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}

	/* XXX FIXME there is race condition here */

	return (gfs_remove(path));
}
