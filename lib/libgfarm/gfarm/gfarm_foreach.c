/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"

char *
gfarm_foreach_directory_hierarchy(
	char *(*op_file)(char *, struct gfs_stat *, void *),
	char *(*op_dir1)(char *, struct gfs_stat *, void *),
	char *(*op_dir2)(char *, struct gfs_stat *, void *),
	char *file, void *arg)
{
	char *path, *slash, *e, *e_save = NULL;
	int file_len;
	struct gfs_stat st;
	GFS_Dir dir;
	struct gfs_dirent *dent;

	e = gfs_stat(file, &st);
	if (e != NULL && e != GFARM_ERR_NO_FRAGMENT_INFORMATION)
		return (e);
	if (e != NULL)
		e_save = e;

	/* add '/' if necessary */
	if (*gfarm_path_dir_skip(gfarm_url_prefix_skip(file)))
		slash = "/";
	else
		slash = "";
	file_len = strlen(file) + strlen(slash);

	if (GFARM_S_ISDIR(st.st_mode)) {
		if (op_dir1 != NULL) {
			e = op_dir1(file, &st, arg);
			if (e != NULL)
				goto free_st;
		}
		e = gfs_opendir(file, &dir);
		if (e != NULL)
			goto free_st;

		while ((e = gfs_readdir(dir, &dent)) == NULL && dent != NULL) {
			char *d = dent->d_name;

			if (d[0] == '.' && (d[1] == '\0' ||
					    (d[1] == '.' && d[2] == '\0')))
				continue;

			GFARM_MALLOC_ARRAY(path, file_len + strlen(d) + 1);
			if (path == NULL) {
				if (e_save == NULL)
					e_save = GFARM_ERR_NO_MEMORY;
				continue;
			}
			sprintf(path, "%s%s%s", file, slash, d);
			e = gfarm_foreach_directory_hierarchy(
				op_file, op_dir1, op_dir2, path, arg);
			free(path);
			if (e_save == NULL)
				e_save = e;
		}
		e = gfs_closedir(dir);
		if (e_save == NULL)
			e_save = e;
		if (op_dir2 != NULL)
			e = op_dir2(file, &st, arg);
	}
	else if (GFARM_S_ISREG(st.st_mode) && op_file != NULL)
		e = op_file(file, &st, arg);
free_st:
	gfs_stat_free(&st);
	return (e_save == NULL ? e : e_save);
}
