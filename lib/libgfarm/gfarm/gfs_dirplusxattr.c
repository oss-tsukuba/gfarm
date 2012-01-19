#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_io.h"
#include "gfs_dirplusxattr.h"

/*
 * gfs_opendirplusxattr()/readdirplusxattr()/closedirplusxattr()
 */

#define DIRENTSPLUSXATTR_BUFCOUNT	256

struct gfs_dirplusxattr {
	struct gfm_connection *gfm_server;
	int fd;
	struct gfs_dirent buffer[DIRENTSPLUSXATTR_BUFCOUNT];
	struct gfs_stat stbuf[DIRENTSPLUSXATTR_BUFCOUNT];
	int nattrbuf[DIRENTSPLUSXATTR_BUFCOUNT];
	char **attrnamebuf[DIRENTSPLUSXATTR_BUFCOUNT];
	void **attrvaluebuf[DIRENTSPLUSXATTR_BUFCOUNT];
	size_t *attrsizebuf[DIRENTSPLUSXATTR_BUFCOUNT];
	int n, index;
};

static gfarm_error_t
gfs_dirplusxattr_alloc(struct gfm_connection *gfm_server, gfarm_int32_t fd,
	GFS_DirPlusXAttr *dirp)
{
	GFS_DirPlusXAttr dir;

	GFARM_MALLOC(dir);
	if (dir == NULL) {
		gflog_debug(GFARM_MSG_1002458,
			"allocation of dir failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	dir->gfm_server = gfm_server;
	dir->fd = fd;
	dir->n = dir->index = 0;

	*dirp = dir;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfs_dirplusxattr_clear(GFS_DirPlusXAttr dir)
{
	int i, j, n = dir->n, nattrs;
	char **attrs;
	void **values;
	size_t *sizes;

	for (i = 0; i < n; i++) {
		gfs_stat_free(&dir->stbuf[i]);
		nattrs = dir->nattrbuf[i];
		attrs = dir->attrnamebuf[i];
		values = dir->attrvaluebuf[i];
		sizes = dir->attrsizebuf[i];
		for (j = 0; j < nattrs; j++) {
			free(attrs[j]);
			free(values[j]);
		}
		free(attrs);
		free(values);
		free(sizes);
	}
	dir->n = dir->index = 0;
}

gfarm_error_t
gfs_opendirplusxattr(const char *path, GFS_DirPlusXAttr *dirp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int fd, type;

	if ((e = gfm_open_fd(path, GFARM_FILE_RDONLY, &gfm_server, &fd, &type))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002459,
			"gfm_open_fd(%s) failed: %s",
			path,
			gfarm_error_string(e));
		return (e);
	}

	if (type != GFS_DT_DIR)
		e = GFARM_ERR_NOT_A_DIRECTORY;
	else if ((e = gfs_dirplusxattr_alloc(gfm_server, fd, dirp)) ==
	    GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);

	if (e == GFARM_ERR_NOT_A_DIRECTORY)
		gflog_debug(GFARM_MSG_1002460,
			"Not a directory (%s): %s",
			path,
			gfarm_error_string(e));
	else if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002461,
			"allocation of dirplusxattr for path (%s) failed: %s",
			path,
			gfarm_error_string(e));

	(void)gfm_close_fd(gfm_server, fd); /* ignore result */
	gfm_client_connection_free(gfm_server);
	return (e);
}

static gfarm_error_t
gfm_getdirentsplusxattr_request(struct gfm_connection *gfm_server,
	void *closure)
{
	gfarm_error_t e = gfm_client_getdirentsplusxattr_request(
	    gfm_server, DIRENTSPLUSXATTR_BUFCOUNT,
	    gfarm_xattr_caching_patterns(),
	    gfarm_xattr_caching_patterns_number());

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1002462,
		    "getdirentsplusxattr request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_getdirentsplusxattr_result(struct gfm_connection *gfm_server, void *closure)
{
	GFS_DirPlusXAttr dir = closure;
	gfarm_error_t e = gfm_client_getdirentsplusxattr_result(gfm_server,
	    &dir->n, dir->buffer, dir->stbuf, dir->nattrbuf,
	    dir->attrnamebuf, dir->attrvaluebuf, dir->attrsizebuf);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1002463,
		    "getdirentsplusxattr result: %s",
		    gfarm_error_string(e));
	return (e);
}

/*
 * both (*entryp) and (*status) shouldn't be freed.
 */
gfarm_error_t
gfs_readdirplusxattr(GFS_DirPlusXAttr dir,
	struct gfs_dirent **entry, struct gfs_stat **status, int *nattrsp,
	char ***attrnamesp, void ***attrvaluesp, size_t **attrsizesp)
{
	gfarm_error_t e;

	if (dir->index >= dir->n) {
		gfs_dirplusxattr_clear(dir);
		e = gfm_client_compound_fd_op(dir->gfm_server, dir->fd,
		    gfm_getdirentsplusxattr_request,
		    gfm_getdirentsplusxattr_result,
		    NULL,
		    dir);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002464,
				"gfs_client_compound_fd_op() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		if (dir->n == 0) {
			*entry = NULL;
			*status = NULL;
			return (GFARM_ERR_NO_ERROR);
		}
		dir->index = 0;
	}
	*entry = &dir->buffer[dir->index];
	*status = &dir->stbuf[dir->index];
	*nattrsp = dir->nattrbuf[dir->index];
	*attrnamesp = dir->attrnamebuf[dir->index];
	*attrvaluesp = dir->attrvaluebuf[dir->index];
	*attrsizesp = dir->attrsizebuf[dir->index];
	dir->index++;

	if (GFARM_S_IS_SUGID_PROGRAM((*status)->st_mode) &&
	    !gfm_is_mounted(dir->gfm_server)) {
		/* for safety of gfarm2fs "suid" option. */
		(*status)->st_mode &= ~(GFARM_S_ISUID|GFARM_S_ISGID);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_closedirplusxattr(GFS_DirPlusXAttr dir)
{
	gfarm_error_t e;

	if ((e = gfm_close_fd(dir->gfm_server, dir->fd)) != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_close_fd: %s",
		    gfarm_error_string(e));
	gfm_client_connection_free(dir->gfm_server);
	gfs_dirplusxattr_clear(dir);
	free(dir);
	/* ignore result */
	return (GFARM_ERR_NO_ERROR);
}
