#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

#define GFARM_INTERNAL_USE /* GFARM_FILE_LOOKUP, gfs_mode_to_type(), etc. */
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"

#include "config.h"
#include "gfm_client.h"
#include "lookup.h"

gfarm_error_t
gfarm_url_parse_metadb(const char **pathp,
	struct gfm_connection **gfm_serverp)
{
	const char gfarm_prefix[] = "gfarm:";
#define GFARM_PREFIX_LEN	(sizeof(gfarm_prefix) - 1)
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	char *ep, *gfm_server_name = NULL /* , *gfm_server_user */ ; 
	unsigned long gfm_server_port;
	const char *p, *path = *pathp;

	if (memcmp(path, gfarm_prefix, GFARM_PREFIX_LEN) != 0) {
		if (gfm_serverp == NULL)
			e = GFARM_ERR_NO_ERROR;
		else
			e = gfm_client_connection_and_process_acquire(
			    gfarm_metadb_server_name, gfarm_metadb_server_port,
			    &gfm_server);
	} else {
		path += GFARM_PREFIX_LEN;
		if (path[0] != '/' || path[1] != '/') {
			gflog_debug(GFARM_MSG_UNFIXED,
				"Host missing in url (%s): %s",
				*pathp,
				gfarm_error_string(
					GFARM_ERR_GFARM_URL_HOST_IS_MISSING));
			return (GFARM_ERR_GFARM_URL_HOST_IS_MISSING);
		}
		path += 2; /* skip "//" */
		for (p = path;
		    *p != '\0' &&
		    (isalnum(*(unsigned char *)p) || *p == '-' || *p == '.');
		    p++)
			;
		if (p == path) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"Host missing in url (%s): %s",
				*pathp,
				gfarm_error_string(
					GFARM_ERR_GFARM_URL_HOST_IS_MISSING));
			return (GFARM_ERR_GFARM_URL_HOST_IS_MISSING);
		}
		if (*p != ':') {
			gflog_debug(GFARM_MSG_UNFIXED,
				"Port missing in url (%s): %s",
				*pathp,
				gfarm_error_string(
					GFARM_ERR_GFARM_URL_PORT_IS_MISSING));
			return (GFARM_ERR_GFARM_URL_PORT_IS_MISSING);
		}
		if (gfm_serverp != NULL) {
			GFARM_MALLOC_ARRAY(gfm_server_name, p - path + 1);
			memcpy(gfm_server_name, path, p - path);
			gfm_server_name[p - path] = '\0';
		}
		p++; /* skip ":" */
		errno = 0;
		gfm_server_port = strtoul(p, &ep, 10);
		if (*p == '\0' || (*ep != '\0' && *ep != '/')) {
			if (gfm_serverp != NULL)
				free(gfm_server_name);
			gflog_debug(GFARM_MSG_UNFIXED,
				"Port missing in url (%s): %s",
				*pathp,
				gfarm_error_string(
					GFARM_ERR_GFARM_URL_PORT_IS_MISSING));
			return (GFARM_ERR_GFARM_URL_PORT_IS_MISSING);
		}
		path = ep;
		if (errno == ERANGE || gfm_server_port == ULONG_MAX ||
		    gfm_server_port <= 0 || gfm_server_port >= 65536) {
			if (gfm_serverp != NULL)
				free(gfm_server_name);
			gflog_debug(GFARM_MSG_UNFIXED,
				"Port invalid in url (%s): %s",
				*pathp,
				gfarm_error_string(
					GFARM_ERR_GFARM_URL_PORT_IS_INVALID));
			return (GFARM_ERR_GFARM_URL_PORT_IS_INVALID);
		}
		if (gfm_serverp == NULL) {
			e = GFARM_ERR_NO_ERROR;
		} else {
			e = gfm_client_connection_and_process_acquire(
			    gfm_server_name, gfm_server_port,
			    &gfm_server);
			free(gfm_server_name);
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (gfm_serverp != NULL)
		*gfm_serverp = gfm_server;
	*pathp = path;
	return (GFARM_ERR_NO_ERROR);
}

const char GFARM_PATH_ROOT[] = "/";

gfarm_error_t
gfm_client_connection_and_process_acquire_by_path(const char *path,
	struct gfm_connection **gfm_serverp)
{
	return (gfarm_url_parse_metadb(&path, gfm_serverp));
}

gfarm_error_t
gfm_lookup_dir_request(struct gfm_connection *gfm_server, const char *path,
	const char **basep)
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
					path = "/";
					e = GFARM_ERR_NO_ERROR;
					break;
				}
				e = gfm_client_open_root_request(gfm_server,
				    GFARM_FILE_LOOKUP);
				if (e != GFARM_ERR_NO_ERROR)
					break;
			}
			e = GFARM_ERR_NO_ERROR;
			break;
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
				break;
			beginning = 0;
		}
		e = gfm_client_open_request(gfm_server, path, len,
		    GFARM_FILE_LOOKUP);
		if (e != GFARM_ERR_NO_ERROR)
			break;
		path += len + 1;
	}

	if (e == GFARM_ERR_NO_ERROR)
		*basep = path;
	else {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfm_lookup_dir_result(struct gfm_connection *gfm_server, const char *path,
	const char **basep)
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
					path = "/";
					e = GFARM_ERR_NO_ERROR;
					break;
				}
				e = gfm_client_open_root_result(gfm_server);
				if (e != GFARM_ERR_NO_ERROR)
					break;
			}
			e = GFARM_ERR_NO_ERROR;
			break;
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
				break;
			beginning = 0;
		}
		e = gfm_client_open_result(gfm_server, &inum, &gen, &mode);
		if (e != GFARM_ERR_NO_ERROR)
			break;
		path += len + 1;
	}

	if (e == GFARM_ERR_NO_ERROR)
		*basep = path;
	else {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}


/* Note that this does COMPOUND_BEGIN request too. */
gfarm_error_t
gfm_tmp_lookup_parent_request(struct gfm_connection *gfm_server,
	const char *path, const char **basep)
{
	gfarm_error_t e;
	const char *base;

	if ((e = gfm_client_compound_begin_request(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000069,
		    "compound_begin(%s) request: %s", path,
		    gfarm_error_string(e));
	} else if ((e = gfm_lookup_dir_request(gfm_server, path, &base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000070,
		    "lookup_dir(%s) request: %s", path,
		    gfarm_error_string(e));
	} else {
		*basep = base;
	}
	return (e);
}

gfarm_error_t
gfm_tmp_lookup_parent_result(struct gfm_connection *gfm_server,
	const char *path, const char **basep)
{
	gfarm_error_t e;
	const char *base;

	if ((e = gfm_client_compound_begin_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000071,
		    "compound_begin(%s) result: %s", path,
		    gfarm_error_string(e));
	} else if ((e = gfm_lookup_dir_result(gfm_server, path, &base))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else {
		*basep = base;
	}
	return (e);
}


/* Note that this does COMPOUND_BEGIN request too. */
gfarm_error_t
gfm_tmp_open_request(struct gfm_connection *gfm_server,
	const char *path, int flags)
{
	gfarm_error_t e;
	const char *base;

	if ((e = gfm_tmp_lookup_parent_request(gfm_server, path, &base))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else if (base[0] == '/' && base[1] == '\0') { /* "/" is special */
		e = gfm_client_open_root_request(gfm_server, flags);
	} else {
		e = gfm_client_open_request(gfm_server,
		    base, strlen(base), flags);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfm_client_open_request(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfm_tmp_open_result(struct gfm_connection *gfm_server,
	const char *path, int *typep)
{
	gfarm_error_t e;
	const char *base;

	if ((e = gfm_tmp_lookup_parent_result(gfm_server, path, &base))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else if (base[0] == '/' && base[1] == '\0') { /* "/" is special */
		if ((e = gfm_client_open_root_result(gfm_server))
		    == GFARM_ERR_NO_ERROR && typep != NULL)
			*typep = GFS_DT_DIR;
	} else {
		gfarm_ino_t inum;
		gfarm_uint64_t gen;
		gfarm_mode_t mode;

		if ((e = gfm_client_open_result(gfm_server,
		    &inum, &gen, &mode)) == GFARM_ERR_NO_ERROR &&
		    typep != NULL)
			*typep = gfs_mode_to_type(mode);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}



gfarm_error_t
gfm_name_success_op_connection_free(struct gfm_connection *gfm_server,
	void *closure)
{
	gfm_client_connection_free(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_name_op(const char *url, gfarm_error_t root_error_code,
	gfarm_error_t (*request_op)(
		struct gfm_connection *, void *, const char *),
	gfarm_error_t (*result_op)(struct gfm_connection *, void *),
	gfarm_error_t (*success_op)(struct gfm_connection *, void *),
	void *closure)
{
	gfarm_error_t e, e_save;
	int retry = 0;
	struct gfm_connection *gfm_server;
	const char *path, *base;

	for (;;) {
		e_save = GFARM_ERR_NO_ERROR;
		path = url;

		if ((e = gfarm_url_parse_metadb(&path, &gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"gfarm_url_parse_metadb(%s) failed: %s",
				url,
				gfarm_error_string(e));
			return (e);
		}

		if ((e = gfm_tmp_lookup_parent_request(gfm_server, path,
		    &base)) != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000072,
			    "tmp_lookup_parent(%s) request: %s",
			    path, gfarm_error_string(e));
		} else if (base[0] == '/' && base[1] == '\0') {
			/* "/" is special */
			e_save = root_error_code;
		} else {
			e = (*request_op)(gfm_server, closure, base);
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000073,
			    "compound_end request: %s",
			    gfarm_error_string(e));

		} else if ((e = gfm_tmp_lookup_parent_result(gfm_server, path,
		    &base)) != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
#if 0 /* DEBUG */
			gflog_debug(GFARM_MSG_1000074,
			    "tmp_lookup_parent(%s) result: %s", path,
			    gfarm_error_string(e));
#endif
		} else if (base[0] == '/' && base[1] == '\0') {
			/* "/" is special */
			e_save = root_error_code;
		} else {
			e = (*result_op)(gfm_server, closure);
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000075,
			    "compound_end result: %s",
			    gfarm_error_string(e));
			break;
		}
		if (e_save != GFARM_ERR_NO_ERROR)
			break;

		return ((*success_op)(gfm_server, closure));
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	if (e != GFARM_ERR_NO_ERROR || e_save != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(
				e_save != GFARM_ERR_NO_ERROR ? e_save : e));
	}

	return (e_save != GFARM_ERR_NO_ERROR ? e_save : e);
}



gfarm_error_t
gfm_inode_success_op_connection_free(struct gfm_connection *gfm_server,
	void *closure, int type)
{
	gfm_client_connection_free(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_inode_op(const char *url, int flags,
	gfarm_error_t (*request_op)(struct gfm_connection *, void *),
	gfarm_error_t (*result_op)(struct gfm_connection *, void *),
	gfarm_error_t (*success_op)(struct gfm_connection *, void *, int),
	void (*cleanup_op)(struct gfm_connection *, void *),
	void *closure)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	const char *path;
	int type;

	for (;;) {
		path = url;

		if ((e = gfarm_url_parse_metadb(&path, &gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"gfarm_url_parse_metadb(%s) failed: %s",
				url,
				gfarm_error_string(e));
			return (e);
		}

		if ((e = gfm_tmp_open_request(gfm_server, path, flags))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000076,
			    "tmp_open(%s) request: %s", path,
			    gfarm_error_string(e));
		} else if ((e = (*request_op)(gfm_server, closure))
		    != GFARM_ERR_NO_ERROR) {
			;
		} else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000077,
			    "compound_end request: %s",
			    gfarm_error_string(e));

		} else if ((e = gfm_tmp_open_result(gfm_server, path, &type))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
#if 0 /* DEBUG */
			gflog_debug(GFARM_MSG_1000078,
			    "tmp_open(%s) result: %s", path,
			    gfarm_error_string(e));
#endif
		} else if ((e = (*result_op)(gfm_server, closure))
		    != GFARM_ERR_NO_ERROR) {
			;
		} else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000079,
			    "compound_end result: %s",
			    gfarm_error_string(e));
			if (cleanup_op != NULL)
				(*cleanup_op)(gfm_server, closure);
		} else {
			return ((*success_op)(gfm_server, closure, type));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}
