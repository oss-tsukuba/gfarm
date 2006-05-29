#include <stddef.h>

#include <gfarm/gfarm.h>

gfarm_error_t
gfs_rmdir(const char *path)
{
	gfarm_error_t e;
	struct gfs_stat st;
	int is_dir;

	e = gfs_stat(path, &st);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	is_dir = GFARM_S_ISDIR(st.st_mode);
	gfs_stat_free(&st);
	if (!is_dir)
		return (GFARM_ERR_NOT_A_DIRECTORY);

	/* XXX FIXME there is race condition here */

	return (gfs_remove(path));
}
