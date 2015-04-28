#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_io.h"
#include "gfs_failover.h"

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
	/* remember opened url */
	char *url;
	/* remember opened inode num */
	gfarm_ino_t ino;
};

static struct gfm_connection *
dirplus_metadb(struct gfs_failover_file *super)
{
	return (((struct gfs_dirplus *)super)->gfm_server);
}

static void
dirplus_set_metadb(struct gfs_failover_file *super,
	struct gfm_connection *gfm_server)
{
	((struct gfs_dirplus *)super)->gfm_server = gfm_server;
}

static gfarm_int32_t
dirplus_fileno(struct gfs_failover_file *super)
{
	return (((struct gfs_dirplus *)super)->fd);
}

static void
dirplus_set_fileno(struct gfs_failover_file *super, gfarm_int32_t fd)
{
	((struct gfs_dirplus *)super)->fd = fd;
}

static const char *
dirplus_url(struct gfs_failover_file *super)
{
	return (((struct gfs_dirplus *)super)->url);
}

static gfarm_ino_t
dirplus_ino(struct gfs_failover_file *super)
{
	return (((struct gfs_dirplus *)super)->ino);
}

static struct gfs_failover_file_ops failover_file_ops = {
	GFS_DT_DIR,
	dirplus_metadb,
	dirplus_set_metadb,
	dirplus_fileno,
	dirplus_set_fileno,
	dirplus_url,
	dirplus_ino,
};

static gfarm_error_t
gfs_dirplus_alloc(struct gfm_connection *gfm_server, gfarm_int32_t fd,
	char *url, gfarm_ino_t ino, GFS_DirPlus *dirp)
{
	GFS_DirPlus dir;

	GFARM_MALLOC(dir);
	if (dir == NULL) {
		gflog_debug(GFARM_MSG_1001277,
			"allocation of dir failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	dir->gfm_server = gfm_server;
	dir->fd = fd;
	dir->n = dir->index = 0;
	dir->url = url;
	dir->ino = ino;

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
	char *url;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;

	if ((e = gfm_open_fd(path, GFARM_FILE_RDONLY, &gfm_server,
	    &fd, &type, &url, &ino, &gen, NULL)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001278,
			"gfm_open_fd(%s) failed: %s",
			path,
			gfarm_error_string(e));
		return (e);
	}

	if (type != GFS_DT_DIR)
		e = GFARM_ERR_NOT_A_DIRECTORY;
	else if ((e = gfs_dirplus_alloc(gfm_server, fd, url, ino, dirp)) ==
	    GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);

	if (e == GFARM_ERR_NOT_A_DIRECTORY)
		gflog_debug(GFARM_MSG_1001279,
			"Not a directory (%s): %s",
			path,
			gfarm_error_string(e));
	else if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1001280,
			"allocation of dirplus for path (%s) failed: %s",
			path,
			gfarm_error_string(e));

	(void)gfm_close_fd(gfm_server, fd, NULL); /* ignore result */
	gfm_client_connection_free(gfm_server);
	return (e);
}

static gfarm_error_t
gfm_getdirentsplus_request(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_getdirentsplus_request(
	    gfm_server, DIRENTSPLUS_BUFCOUNT);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000090, "getdirentsplus request: %s",
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
		gflog_warning(GFARM_MSG_1000091, "getdirentsplus result: %s",
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
		e = gfm_client_compound_fd_op_readonly(
		    (struct gfs_failover_file *)dir,
		    &failover_file_ops,
		    gfm_getdirentsplus_request,
		    gfm_getdirentsplus_result,
		    NULL,
		    dir);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003937,
			    "gfm_client_compound_readonly_fd_op: %s",
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
	dir->index++;

	if (GFARM_S_IS_SUGID_PROGRAM((*status)->st_mode) &&
	    !gfm_is_mounted(dir->gfm_server)) {
		/* for safety of gfarm2fs "suid" option. */
		(*status)->st_mode &= ~(GFARM_S_ISUID|GFARM_S_ISGID);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_closedirplus(GFS_DirPlus dir)
{
	gfarm_error_t e;

	if ((e = gfm_close_fd(dir->gfm_server, dir->fd, NULL))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003938,
		    "gfm_close_fd: %s",
		    gfarm_error_string(e));
	gfm_client_connection_free(dir->gfm_server);
	gfs_dirplus_clear(dir);
	free(dir->url);
	free(dir);
	/* ignore result */
	return (GFARM_ERR_NO_ERROR);
}
