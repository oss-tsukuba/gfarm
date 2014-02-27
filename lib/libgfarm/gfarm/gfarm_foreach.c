/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"

static gfarm_error_t
gfarm_foreach_directory_hierarchy_internal(
	gfarm_error_t (*op_file)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*op_dir1)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*op_dir2)(char *, struct gfs_stat *, void *),
	char *file, void *arg, struct gfs_stat *st);

static gfarm_error_t
gfarm_foreach_directory(
	gfarm_error_t (*op_file)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*op_dir1)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*op_dir2)(char *, struct gfs_stat *, void *),
	char *file, void *arg, int (*filter)(struct gfs_stat *))
{
	char *path, *slash;
	const char *f;
	int file_len;
	GFS_DirPlus dir;
	struct gfs_dirent *dent;
	struct gfs_stat *stent;
	gfarm_error_t e, e2, e_save = GFARM_ERR_NO_ERROR;

	/* add '/' if necessary */
	f = gfarm_url_prefix_hostname_port_skip(file);
	if (*f == '\0' || *gfarm_path_dir_skip(f))
		slash = "/";
	else
		slash = "";
	file_len = strlen(file) + strlen(slash);

	e = gfs_opendirplus(file, &dir);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	while ((e = gfs_readdirplus(dir, &dent, &stent))
	    == GFARM_ERR_NO_ERROR && dent != NULL) {
		char *d = dent->d_name;

		if (d[0] == '.' && (d[1] == '\0' || (d[1] == '.'
		    && d[2] == '\0')))
			continue;
		if (!filter(stent))
			continue;

		GFARM_MALLOC_ARRAY(path, file_len + strlen(d) + 1);
		if (path == NULL) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1003588,
			    "%s%s%s: no memory", file, slash, d);
			continue;
		}
		sprintf(path, "%s%s%s", file, slash, d);
		e = gfarm_foreach_directory_hierarchy_internal(
		    op_file, op_dir1, op_dir2, path, arg, stent);
		free(path);
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	e2 = gfs_closedirplus(dir);
	if (e == GFARM_ERR_NO_ERROR)
		e = e_save;
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;
	return (e);
}

static int
file_filter(struct gfs_stat *st)
{
	return (!GFARM_S_ISDIR(st->st_mode));
}

static int
dir_filter(struct gfs_stat *st)
{
	return (GFARM_S_ISDIR(st->st_mode));
}

static gfarm_error_t
gfarm_foreach_directory_hierarchy_internal(
	gfarm_error_t (*op_file)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*op_dir1)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*op_dir2)(char *, struct gfs_stat *, void *),
	char *file, void *arg, struct gfs_stat *st)
{
	gfarm_error_t e, e2;

	if (GFARM_S_ISDIR(st->st_mode)) {
		if (op_dir1 != NULL) {
			e = op_dir1(file, st, arg);
			if (e != GFARM_ERR_NO_ERROR)
				goto error;
		}
		e = gfarm_foreach_directory(op_file, op_dir1, op_dir2,
		    file, arg, file_filter);
		e2 = gfarm_foreach_directory(op_file, op_dir1, op_dir2,
		    file, arg, dir_filter);
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
		if (e != GFARM_ERR_NO_ERROR)
			goto error;
		if (op_dir2 != NULL)
			e = op_dir2(file, st, arg);
	} else if (op_file != NULL) /* not only file but also symlink */
		e = op_file(file, st, arg);
	else
		e = GFARM_ERR_NO_ERROR;
error:
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001413,
			"Error in foreach directory hierarchy: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfarm_foreach_directory_hierarchy(
	gfarm_error_t (*op_file)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*op_dir1)(char *, struct gfs_stat *, void *),
	gfarm_error_t (*op_dir2)(char *, struct gfs_stat *, void *),
	char *file, void *arg)
{
	struct gfs_stat st;
	gfarm_error_t e = gfs_lstat(file, &st);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfarm_foreach_directory_hierarchy_internal(
	    op_file, op_dir1, op_dir2, file, arg, &st);
	gfs_stat_free(&st);
	return (e);
}
