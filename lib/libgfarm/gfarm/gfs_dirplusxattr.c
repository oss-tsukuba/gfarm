#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_io.h"
#include "gfs_dir.h" /* gfm_seekdir_{request,result}() */
#include "gfs_dirplusxattr.h"
#include "gfs_failover.h"

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
	gfarm_off_t seek_pos;
	/* remember opened url */
	char *url;
	/* remember opened inode num */
	gfarm_ino_t ino;
};

static struct gfm_connection *
dirplusxattr_metadb(struct gfs_failover_file *super)
{
	return (((struct gfs_dirplusxattr *)super)->gfm_server);
}

static void
dirplusxattr_set_metadb(struct gfs_failover_file *super,
	struct gfm_connection *gfm_server)
{
	((struct gfs_dirplusxattr *)super)->gfm_server = gfm_server;
}

static gfarm_int32_t
dirplusxattr_fileno(struct gfs_failover_file *super)
{
	return (((struct gfs_dirplusxattr *)super)->fd);
}

static void
dirplusxattr_set_fileno(struct gfs_failover_file *super, gfarm_int32_t fd)
{
	((struct gfs_dirplusxattr *)super)->fd = fd;
}

static const char *
dirplusxattr_url(struct gfs_failover_file *super)
{
	return (((struct gfs_dirplusxattr *)super)->url);
}

static gfarm_ino_t
dirplusxattr_ino(struct gfs_failover_file *super)
{
	return (((struct gfs_dirplusxattr *)super)->ino);
}

static struct gfs_failover_file_ops failover_file_ops = {
	GFS_DT_DIR,
	dirplusxattr_metadb,
	dirplusxattr_set_metadb,
	dirplusxattr_fileno,
	dirplusxattr_set_fileno,
	dirplusxattr_url,
	dirplusxattr_ino,
};

static gfarm_error_t
gfs_dirplusxattr_alloc(struct gfm_connection *gfm_server, gfarm_int32_t fd,
	char *url, gfarm_ino_t ino, GFS_DirPlusXAttr *dirp)
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
	dir->seek_pos = 0;
	dir->url = url;
	dir->ino = ino;

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
	char *url;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;

	if ((e = gfm_open_fd(path, GFARM_FILE_RDONLY, &gfm_server,
	    &fd, &type, &url, &ino, &gen, NULL)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002459,
			"gfm_open_fd(%s) failed: %s",
			path,
			gfarm_error_string(e));
		return (e);
	}

	if (type != GFS_DT_DIR)
		e = GFARM_ERR_NOT_A_DIRECTORY;
	else if ((e = gfs_dirplusxattr_alloc(gfm_server, fd, url, ino, dirp)) ==
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

	(void)gfm_close_fd(gfm_server, fd, NULL); /* ignore result */
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
	int n;

	if (dir->index >= dir->n) {
		n = dir->n;
		gfs_dirplusxattr_clear(dir);
		e = gfm_client_compound_fd_op_readonly(
		    (struct gfs_failover_file *)dir,
		    &failover_file_ops,
		    gfm_getdirentsplusxattr_request,
		    gfm_getdirentsplusxattr_result,
		    NULL,
		    dir);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003939,
			    "gfm_client_compound_readonly_fd_op: %s",
			    gfarm_error_string(e));
			return (e);
		}

		dir->seek_pos += n;
		if (dir->n == 0) {
			*entry = NULL;
			*status = NULL;
			return (GFARM_ERR_NO_ERROR);
		}
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
gfs_seekdirplusxattr(GFS_DirPlusXAttr dir, gfarm_off_t off)
{
	gfarm_error_t e;
	struct gfm_seekdir_closure closure;

	if (dir->seek_pos <= off && off <= dir->seek_pos + dir->n) {
		dir->index = off - dir->seek_pos;
		return (GFARM_ERR_NO_ERROR);
	}

	closure.offset = off;
	closure.whence = 0;
	e = gfm_client_compound_fd_op_readonly(
	    (struct gfs_failover_file *)dir, &failover_file_ops,
	    gfm_seekdir_request, gfm_seekdir_result, NULL, &closure);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003420,
		    "gfm_client_compound_fd_op_readonly(seek): %s",
		    gfarm_error_string(e));
	}
	gfs_dirplusxattr_clear(dir);
	dir->seek_pos = closure.offset;
	return (e);
}

gfarm_error_t
gfs_telldirplusxattr(GFS_DirPlusXAttr dir, gfarm_off_t *offp)
{
	*offp = dir->seek_pos + dir->index;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_closedirplusxattr(GFS_DirPlusXAttr dir)
{
	gfarm_error_t e;

	if ((e = gfm_close_fd(dir->gfm_server, dir->fd, NULL))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003940,
		    "gfm_close_fd: %s",
		    gfarm_error_string(e));
	gfm_client_connection_free(dir->gfm_server);
	gfs_dirplusxattr_clear(dir);
	free(dir->url);
	free(dir);
	/* ignore result */
	return (GFARM_ERR_NO_ERROR);
}
