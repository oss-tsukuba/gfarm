#include <assert.h>
#include <string.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "lookup.h"

gfarm_error_t
gfm_lookup_dir_request(struct gfm_connection *gfm_server,
	const char *path, const char **basep)
{
	gfarm_error_t e;
	int beginning = 1;
	int len;

	if (*path == '/')
		path++;
#if 0 /* XXX FIX ME: current directory is always "/" on v2 for now */
	else
		return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
#endif

	for (;;) {
		len = strcspn(path, "/");
		if (path[len] != '/') {
			assert(path[len] == '\0');
			if (beginning) {
				if (len == 0) {
					*basep = "/";
					return (GFARM_ERR_NO_ERROR);
				}
				e = gfm_client_open_root_request(gfm_server,
				    GFARM_FILE_LOOKUP);
				if (e != GFARM_ERR_NO_ERROR)
					return (e);
			}
			*basep = path;
			return (GFARM_ERR_NO_ERROR);
		}
		if (len == 0) {
			path++;
			continue;
		}
		if (len == 1 && *path == '.') {
			path += 2;
			continue;
		}
		if (beginning) {
			e = gfm_client_open_root_request(gfm_server,
			    GFARM_FILE_LOOKUP);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			beginning = 0;
		}
		e = gfm_client_open_request(gfm_server, path, len,
		    GFARM_FILE_LOOKUP);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		path += len + 1;
	}
	    
}

gfarm_error_t
gfm_lookup_dir_result(struct gfm_connection *gfm_server,
	const char *path, const char **basep)
{
	gfarm_error_t e;
	int beginning = 1;
	int len;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_mode_t mode;

	if (*path == '/')
		path++;
#if 0 /* XXX FIX ME: current directory is always "/" on v2 for now */
	else
		return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
#endif

	for (;;) {
		len = strcspn(path, "/");
		if (path[len] != '/') {
			assert(path[len] == '\0');
			if (beginning) {
				if (len == 0) {
					*basep = "/";
					return (GFARM_ERR_NO_ERROR);
				}
				e = gfm_client_open_root_result(gfm_server);
				if (e != GFARM_ERR_NO_ERROR)
					return (e);
			}
			*basep = path;
			return (GFARM_ERR_NO_ERROR);
		}
		if (len == 0) {
			path++;
			continue;
		}
		if (len == 1 && *path == '.') {
			path += 2;
			continue;
		}
		if (beginning) {
			e = gfm_client_open_root_result(gfm_server);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			beginning = 0;
		}
		e = gfm_client_open_result(gfm_server, &inum, &gen, &mode);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		path += len + 1;
	}
	    
}


gfarm_error_t
gfm_tmp_open_request(struct gfm_connection *gfm_server,
	const char *path, int flags)
{
	gfarm_error_t e;
	const char *base;

	e = gfm_lookup_dir_request(gfm_server, path, &base);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (base[0] == '/' && base[1] == '\0') { /* "/" is special */
		return (gfm_client_open_root_request(gfm_server,
		    flags));
	} else {
		return (gfm_client_open_request(gfm_server,
		    base, strlen(base), flags));
	}
}

gfarm_error_t
gfm_tmp_open_result(struct gfm_connection *gfm_server,
	const char *path, int *is_dirp)
{
	gfarm_error_t e;
	const char *base;
	int is_dir;

	e = gfm_lookup_dir_result(gfm_server, path, &base);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (base[0] == '/' && base[1] == '\0') { /* "/" is special */
		e = gfm_client_open_root_result(gfm_server);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		is_dir = 1;
	} else {
		gfarm_ino_t inum;
		gfarm_uint64_t gen;
		gfarm_mode_t mode;

		e = gfm_client_open_result(gfm_server,
		    &inum, &gen, &mode);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		is_dir = GFARM_S_ISDIR(mode);
	}
	if (is_dirp != NULL)
		*is_dirp = is_dir;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_open_request(struct gfm_connection *gfm_server,
	const char *path, int flags)
{
	gfarm_error_t e;

	e = gfm_tmp_open_request(gfm_server, path, flags);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfm_client_get_fd_request(gfm_server));
}

gfarm_error_t
gfm_open_result(struct gfm_connection *gfm_server,
	const char *path, gfarm_int32_t *fdp, int *is_dirp)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	int is_dir;

	e = gfm_tmp_open_result(gfm_server, path, &is_dir);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfm_client_get_fd_result(gfm_server, &fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (fdp != NULL)
		*fdp = fd;
	if (is_dirp != NULL)
		*is_dirp = is_dir;
	return (GFARM_ERR_NO_ERROR);
}
