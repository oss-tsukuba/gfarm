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
	struct gfm_connection *gfm_server;
	int fd;
	struct gfs_dirent buffer[DIRENTSPLUS_BUFCOUNT];
	struct gfs_stat stbuf[DIRENTSPLUS_BUFCOUNT];
	int n, index;
};

static gfarm_error_t
gfs_dirplus_alloc(struct gfm_connection *gfm_server, gfarm_int32_t fd,
	GFS_DirPlus *dirp)
{
	GFS_DirPlus dir;

	GFARM_MALLOC(dir);
	if (dir == NULL)
		return (GFARM_ERR_NO_MEMORY);

	dir->gfm_server = gfm_server;
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
	struct gfm_connection *gfm_server;
	int fd, type;

	if ((e = gfm_open_fd(path, GFARM_FILE_RDONLY, &gfm_server, &fd, &type))
	    != GFARM_ERR_NO_ERROR)
		return (e);

	if (type != GFS_DT_DIR)
		e = GFARM_ERR_NOT_A_DIRECTORY;
	else if ((e = gfs_dirplus_alloc(gfm_server, fd, dirp)) ==
	    GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);

	(void)gfm_close_fd(gfm_server, fd); /* ignore result */
	gfm_client_connection_free(gfm_server);
	return (e);
}

static gfarm_error_t
gfm_getdirentsplus_request(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_getdirentsplus_request(
	    gfm_server, DIRENTSPLUS_BUFCOUNT);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_UNFIXED, "getdirentsplus request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_getdirentsplus_result(struct gfm_connection *gfm_server, void *closure)
{
	GFS_DirPlus dir = closure;
	gfarm_error_t e = gfm_client_getdirentsplus_result(gfm_server,
	    &dir->n, dir->buffer, dir->stbuf);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_UNFIXED, "getdirentsplus result: %s",
		    gfarm_error_string(e));
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
		e = gfm_client_compound_fd_op(dir->gfm_server, dir->fd,
		    gfm_getdirentsplus_request,
		    gfm_getdirentsplus_result,
		    NULL, 
		    dir);
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
	gfarm_error_t e = gfm_close_fd(dir->gfm_server, dir->fd);

	gfm_client_connection_free(dir->gfm_server);
	gfs_dirplus_clear(dir);
	free(dir);
	return (e);
}
