#include <assert.h>
#include <stdio.h>	/* config.h needs FILE */
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"
#include "gfs_io.h"

static gfarm_error_t
gfs_desc_open_common(gfarm_int32_t fd, int flags, int is_dir,
	struct gfs_desc **gdp)
{
	if (is_dir) {
		return (gfs_dir_alloc(fd, flags, gdp));
	} else {
		return (gfs_file_alloc(fd, flags, gdp));
	}
}

static gfarm_error_t
gfs_desc_flag_check(int flag)
{
	if (flag & ~GFARM_FILE_USER_MODE)
		return (GFARM_ERR_INVALID_ARGUMENT);
	if ((flag & GFARM_FILE_ACCMODE) == GFARM_FILE_LOOKUP)
		return (GFARM_ERR_INVALID_ARGUMENT);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfm_create_request(struct gfm_connection *gfm_server,
	const char *path, int flags, gfarm_mode_t mode)
{
	gfarm_error_t e;
	const char *base;

	e = gfm_lookup_dir_request(gfm_server, path, &base);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (base[0] == '/' && base[1] == '\0') { /* "/" is special */
		e = GFARM_ERR_IS_A_DIRECTORY;
	} else {
		e = gfm_client_create_request(gfm_server,
		    base, flags, mode);
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfm_client_get_fd_request(gfm_server));
}

static gfarm_error_t
gfm_create_result(struct gfm_connection *gfm_server,
	const char *path, gfarm_int32_t *fdp, int *is_dirp)
{
	gfarm_error_t e;
	const char *base;
	int is_dir;

	e = gfm_lookup_dir_result(gfm_server, path, &base);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (base[0] == '/' && base[1] == '\0') { /* "/" is special */
		/* shouldn't come here */
		assert(0);
		return (GFARM_ERR_IS_A_DIRECTORY);
	} else {
		gfarm_ino_t inum;
		gfarm_uint64_t gen;
		gfarm_mode_t mode;

		e = gfm_client_create_result(gfm_server,
		    &inum, &gen, &mode);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		is_dir = GFARM_S_ISDIR(mode);
	}
	e = gfm_client_get_fd_result(gfm_server, fdp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (is_dirp != NULL)
		*is_dirp = is_dir;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_desc_create(const char *url, int flags, gfarm_mode_t mode,
	struct gfs_desc **gdp)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	int is_dir = 0;

	if ((e = gfs_desc_flag_check(flags)) != GFARM_ERR_NO_ERROR)
		;
	else if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_create_request(gfarm_metadb_server,
	    url, flags, mode)) != GFARM_ERR_NO_ERROR)
		gflog_warning("create request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_create_result(gfarm_metadb_server, url,
	    &fd, &is_dir)) != GFARM_ERR_NO_ERROR)
		gflog_warning("create_base request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));

	if (e == GFARM_ERR_NO_ERROR)
		e = gfs_desc_open_common(fd, flags, is_dir, gdp);
	
	return (e);
}

gfarm_error_t
gfs_desc_open(const char *url, int flags,
	struct gfs_desc **gdp)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	int is_dir = 0;

#if 0 /* not yet in gfarm v2 */
	/* GFARM_FILE_EXCLUSIVE is a NOP with gfs_desc_open(). */
	flags &= ~GFARM_FILE_EXCLUSIVE;
#endif /* not yet in gfarm v2 */

	if ((e = gfs_desc_flag_check(flags)) != GFARM_ERR_NO_ERROR)
		;
	else if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_open_request(gfarm_metadb_server, url, flags))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("open path request; %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_open_result(gfarm_metadb_server, url,
	    &fd, &is_dir)) != GFARM_ERR_NO_ERROR)
		gflog_warning("open path result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));

	if (e == GFARM_ERR_NO_ERROR)
		e = gfs_desc_open_common(fd, flags, is_dir, gdp);
	
	return (e);
}

gfarm_error_t
gfs_desc_close(struct gfs_desc *gd)
{
	return ((*gd->ops->close)(gd));
}

gfarm_error_t
gfm_close_fd(int fd)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_request(gfarm_metadb_server, fd))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("put_fd request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_close_request(gfarm_metadb_server)
	    ) != GFARM_ERR_NO_ERROR)
		gflog_warning("close request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("put_fd result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_close_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("close result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));

	return (e);
}
