#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfm_client.h"
#include "gfs_io.h"

/*
 * gfs_opendirplus()/readdirplus()/closedirplus()
 */

#define DIRENTSPLUS_BUFCOUNT	256

struct gfs_dirplus {
	int fd;
	struct gfs_dirent buffer[DIRENTSPLUS_BUFCOUNT];
	struct gfs_stat stbuf[DIRENTSPLUS_BUFCOUNT];
	int n, index;
};

static gfarm_error_t
gfs_dirplus_alloc(gfarm_int32_t fd, GFS_DirPlus *dirp)
{
	GFS_DirPlus dir;

	GFARM_MALLOC(dir);
	if (dir == NULL)
		return (GFARM_ERR_NO_MEMORY);

	dir->fd = fd;
	dir->n = dir->index = 0;

	*dirp = dir;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfs_dirplus_clear(GFS_DirPlus dir)
{
	int i, n = dir->n;

	for (i = 0; i < n; i++)
		gfs_stat_free(&dir->stbuf[i]);
	dir->n = dir->index = 0;
}

gfarm_error_t
gfs_opendirplus(const char *path, GFS_DirPlus *dirp)
{
	gfarm_error_t e;
	int fd, type;

	if ((e = gfm_open_fd(path, GFARM_FILE_RDONLY, &fd, &type))
	    != GFARM_ERR_NO_ERROR)
 		;
	else if (type != GFS_DT_DIR) {
		(void)gfm_close_fd(fd); /* ignore this result */
		e = GFARM_ERR_NOT_A_DIRECTORY;
	} else if ((e = gfs_dirplus_alloc(fd, dirp)) != GFARM_ERR_NO_ERROR)
		(void)gfm_close_fd(fd); /* ignore this result */

	return (e);
}

/*
 * both (*entryp) and (*status) shouldn't be freed.
 */
gfarm_error_t
gfs_readdirplus(GFS_DirPlus dir,
	struct gfs_dirent **entry, struct gfs_stat **status)
{
	gfarm_error_t e;

	if (dir->index >= dir->n) {
		gfs_dirplus_clear(dir);
		if ((e = gfm_client_compound_begin_request(
		    gfarm_metadb_server)) != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_put_fd_request(gfarm_metadb_server,
		    dir->fd)) != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_getdirentsplus_request(
		    gfarm_metadb_server, DIRENTSPLUS_BUFCOUNT))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("get_dirents request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(
		    gfarm_metadb_server)) != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(
		    gfarm_metadb_server)) != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_put_fd_result(gfarm_metadb_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_getdirentsplus_result(
		    gfarm_metadb_server, &dir->n, dir->buffer, dir->stbuf))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("get_dirents result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(
		    gfarm_metadb_server)) != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));

		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if (dir->n == 0) {
			*entry = NULL;
			*status = NULL;
			return (GFARM_ERR_NO_ERROR);
		}
		dir->index = 0;
	}
	*entry = &dir->buffer[dir->index];
	*status = &dir->stbuf[dir->index];
	dir->index++;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_closedirplus(GFS_DirPlus dir)
{
	gfarm_error_t e = gfm_close_fd(dir->fd);

	gfs_dirplus_clear(dir);
	free(dir);
	return (e);
}

