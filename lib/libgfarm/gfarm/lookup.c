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
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	char *ep, *gfm_server_name = NULL /* , *gfm_server_user */ ; 
	unsigned long gfm_server_port;
	const char *p, *path = *pathp;

	if (!gfarm_is_url(path)) {
		if (gfm_serverp == NULL)
			e = GFARM_ERR_NO_ERROR;
		else
			e = gfm_client_connection_and_process_acquire(
			    gfarm_metadb_server_name, gfarm_metadb_server_port,
			    &gfm_server);
	} else {
		path += GFARM_URL_PREFIX_LENGTH;
		if (path[0] != '/' || path[1] != '/') {
			gflog_debug(GFARM_MSG_1001254,
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
			gflog_debug(GFARM_MSG_1001255,
				"Host missing in url (%s): %s",
				*pathp,
				gfarm_error_string(
					GFARM_ERR_GFARM_URL_HOST_IS_MISSING));
			return (GFARM_ERR_GFARM_URL_HOST_IS_MISSING);
		}
		if (*p != ':') {
			gflog_debug(GFARM_MSG_1001256,
				"Port missing in url (%s): %s",
				*pathp,
				gfarm_error_string(
					GFARM_ERR_GFARM_URL_PORT_IS_MISSING));
			return (GFARM_ERR_GFARM_URL_PORT_IS_MISSING);
		}
		if (gfm_serverp != NULL) {
			GFARM_MALLOC_ARRAY(gfm_server_name, p - path + 1);
			if (gfm_server_name == NULL) {
				gflog_debug(GFARM_MSG_1002312,
				    "allocating gfm server name for '%s': "
				    "no memory", *pathp);
				return (GFARM_ERR_NO_MEMORY);
			}
			memcpy(gfm_server_name, path, p - path);
			gfm_server_name[p - path] = '\0';
		}
		p++; /* skip ":" */
		errno = 0;
		gfm_server_port = strtoul(p, &ep, 10);
		if (*p == '\0' || (*ep != '\0' && *ep != '/')) {
			if (gfm_serverp != NULL)
				free(gfm_server_name);
			gflog_debug(GFARM_MSG_1001257,
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
			gflog_debug(GFARM_MSG_1001258,
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
		gflog_debug(GFARM_MSG_1001259,
			"error occurred during process: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (gfm_serverp != NULL)
		*gfm_serverp = gfm_server;
	*pathp = path;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_connection_and_process_acquire_by_path(const char *path,
	struct gfm_connection **gfm_serverp)
{
	return (gfarm_url_parse_metadb(&path, gfm_serverp));
}

#define SKIP_SLASH(p) { while (*(p) == '/') (p)++; }

gfarm_error_t
gfm_lookup_dir_request(struct gfm_connection *gfm_server, const char *path,
	const char **basep)
{
	gfarm_error_t e;
	int beginning = 1;
	int len;

	/* XXX FIX ME: current directory is always "/" on v2 for now */

	SKIP_SLASH(path);

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
			SKIP_SLASH(path);
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
		path += len;
		SKIP_SLASH(path);
		if ((e = gfm_client_verify_type_request(gfm_server, GFS_DT_DIR))
		    != GFARM_ERR_NO_ERROR)
			break;
	}

	*basep = path;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001260,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfm_lookup_dir_result(struct gfm_connection *gfm_server, const char *path,
	const char **restp, int *is_lastp)
{
	gfarm_error_t e;
	int beginning = 1;
	int len;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_mode_t mode;

	/* XXX FIX ME: current directory is always "/" on v2 for now */
	if (is_lastp != NULL)
		*is_lastp = 0;

	SKIP_SLASH(path);

	for (;;) {
		len = strcspn(path, "/");
		if (path[len] != '/') {
			assert(path[len] == '\0');
			if (beginning) {
				if (len == 0) {
					path = "/";
					e = GFARM_ERR_NO_ERROR;
					if (is_lastp != NULL)
						*is_lastp = 1;
					break;
				}
				e = gfm_client_open_root_result(gfm_server);
				if (e != GFARM_ERR_NO_ERROR)
					break;
			}
			e = GFARM_ERR_NO_ERROR;
			if (is_lastp != NULL)
				*is_lastp = 1;
			break;
		}
		if (len == 1 && *path == '.') {
			path += 2;
			SKIP_SLASH(path);
			continue;
		}
		if (len == 0) {
			path++;
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
		path += len;
		SKIP_SLASH(path);
		if ((e = gfm_client_verify_type_result(gfm_server))
			!= GFARM_ERR_NO_ERROR)
			break;
	}

	*restp = path;

	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1001261,
			"error occurred during process: %s",
			gfarm_error_string(e));
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
gfm_inode_success_op_connection_free(struct gfm_connection *gfm_server,
	void *closure, int type, const char *path)
{
	gfm_client_connection_free(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_alloc_link_destination(struct gfm_connection *gfm_server,
	char *link, char **nextpathp, char *rest, int is_last)
{
	char *p = *nextpathp;
	int len, blen, linklen;

	blen = strlen(rest);
	if (blen > 0) {
		char *ss = rest + blen - 1;
		if (*ss == '/') {
			while (*(--ss) == '/')
				;
			*(++ss) = 0;
			blen = strlen(rest);
		}
	}
	linklen = strlen(link);
	len = linklen + (is_last ? 0 : blen + 1);
	GFARM_MALLOC_ARRAY(p, len + 1);
	strcpy(p, link);
	if (!is_last) {
		p[linklen] = '/';
		strcpy(p + linklen + 1, rest);
	}
	p[len] = 0;
	if (*nextpathp)
		free(*nextpathp);
	*nextpathp = p;
	return (GFARM_ERR_NO_ERROR);
}


static char *
trim_tailing_file_separator(const char *path)
{
	char *npath;
	int len;

	npath = strdup(path);
	if (npath == NULL)
		return (NULL);
	if (!GFARM_IS_PATH_ROOT(npath) && strchr(npath, '/') != NULL) {
		char *p = npath + strlen(npath);
		while (*(--p) == '/' && p > npath)
			;
		*(++p) = '\0';
	}
	len = strlen(npath);
	if (len == 1 && npath[0] == '.')
		npath[0] = '/';
	return (npath);
}


static gfarm_error_t
gfm_inode_or_name_op_lookup_request(struct gfm_connection *gfm_server,
	const char *path, int flags, char **restp)
{
	gfarm_error_t e;
	int pflags = (flags & GFARM_FILE_PROTOCOL_MASK);
	int is_open_last = (flags & GFARM_FILE_OPEN_LAST_COMPONENT) != 0;

	if (!is_open_last && GFARM_IS_PATH_ROOT(path)) {
		e = GFARM_ERR_PATH_IS_ROOT;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "inode_or_name_op_lookup_request : %s",
		    gfarm_error_string(e));
		return (e);
	}
	if ((e = gfm_lookup_dir_request(gfm_server, path,
		(const char **)restp)) != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "lookup_dir(%s) request: %s", path,
		    gfarm_error_string(e));
		return (e);
	}
	if (!is_open_last)
		return (GFARM_ERR_NO_ERROR);

	if (GFARM_IS_PATH_ROOT(path)) {
		if ((e = gfm_client_open_root_request(
			    gfm_server, pflags))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "open_root(flags=%d) "
			    "request failed: %s",
			    pflags, gfarm_error_string(e));
			return (e);
		}
	} else {
		if ((e = gfm_client_open_request(
			gfm_server, *restp, strlen(*restp), pflags))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "open(name=%s,flags=%d) "
			    "request failed: %s", *restp, pflags,
			    gfarm_error_string(e));
			return (e);
		}
		if ((e = gfm_client_verify_type_not_request(
			    gfm_server, GFS_DT_LNK))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "verify_type_not_request failed: %s",
			    gfarm_error_string(e));
			return (e);
		}
	}

	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_inode_or_name_op_lookup_result(struct gfm_connection *gfm_server,
	const char *path, int flags, char **restp, int *is_lastp,
	int *typep, int *retry_countp, int *is_retryp)
{
	gfarm_error_t e;
	int is_open_last = (flags & GFARM_FILE_OPEN_LAST_COMPONENT) != 0;

	if ((e = gfm_lookup_dir_result(gfm_server, path,
			(const char **)restp, is_lastp))
		!= GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "lookup_dir(path=%s) result failed: %s",
		    path, gfarm_error_string(e));
		return (e);
	}

	if (!is_open_last)
		return (GFARM_ERR_NO_ERROR);

	if (GFARM_IS_PATH_ROOT(path)) {/* "/" is special */
		if ((e = gfm_client_open_root_result(
				gfm_server))
			!= GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "open_root result failed: %s",
			    gfarm_error_string(e));
			return (e);
		}
		*typep = GFS_DT_DIR;
	} else {
		gfarm_ino_t inum;
		gfarm_uint64_t gen;
		gfarm_mode_t mode;

		if ((e = gfm_client_open_result(gfm_server,
		    &inum, &gen, &mode)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1000078,
			    "gfm_client_open_result(%s) result: %s",
			    path, gfarm_error_string(e));
			if (gfm_client_is_connection_error(e) &&
			    ++(*retry_countp) <= 1) {
				*is_retryp = 1;
			}
			return (e);
		}
		if ((e = gfm_client_verify_type_not_result(gfm_server))
			!= GFARM_ERR_NO_ERROR) {
			if (e != GFARM_ERR_IS_A_SYMBOLIC_LINK) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "verify_type_not result"
				    "failed: %s",
				    gfarm_error_string(e));
			}
			return (e);
		}
		*typep = gfs_mode_to_type(mode);
	}

	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_inode_or_name_op_on_error_request(struct gfm_connection *gfm_server)
{
	gfarm_error_t e;
	if ((e = gfm_client_compound_on_error_request(gfm_server,
		GFARM_ERR_IS_A_SYMBOLIC_LINK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "compound_on_error request failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if ((e = gfm_client_readlink_request(gfm_server))
		!= GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "readlink request failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if ((e = gfm_client_get_fd_request(gfm_server))
		!= GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "get_fd request failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_inode_or_name_op_on_error_result(struct gfm_connection *gfm_server,
	int flags, int is_last, int *is_symlinkp,
	char *rest, char **nextpathp, int *is_retryp, int *fdp)
{
	gfarm_error_t e;
	char *link;

	*is_symlinkp = ((flags & GFARM_FILE_SYMLINK_NO_FOLLOW) != 0) && is_last;

	if ((e = gfm_client_readlink_result(gfm_server, &link))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "readlink result failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (!*is_symlinkp &&
	    (e = gfm_alloc_link_destination(
	    gfm_server, link, nextpathp, rest, is_last))
	    == GFARM_ERR_NO_ERROR)
		*is_retryp = 1;
	free(link);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfm_client_get_fd_result(gfm_server, fdp))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "get_fd result failed: %s",
		    gfarm_error_string(e));
	}
	return (e);
}


static gfarm_error_t
gfm_inode_or_name_op(const char *url, int flags,
	gfarm_error_t (*inode_request_op)(
		struct gfm_connection*, void *),
	gfarm_error_t (*inode_success_op)(
		struct gfm_connection *, void *, int, const char *),
	gfarm_error_t (*name_request_op)(
		struct gfm_connection*, void *, const char *),
	gfarm_error_t (*name_success_op)(
		struct gfm_connection *, void *),
	gfarm_error_t (*result_op)(
		struct gfm_connection *, void *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	void *closure)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server = NULL;
	int type;
	int retry_count = 0, nlinks = 0;
	char *path;
	char *rest, *nextpath;
	int is_last, is_retry, lookup;
	int is_success = 0, is_symlink = 0;
	int is_open_last = (flags & GFARM_FILE_OPEN_LAST_COMPONENT) != 0;
	gfarm_int32_t fd = -1, tmpfd;

	nextpath = trim_tailing_file_separator(url);
	if (nextpath == NULL) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "trim_tailing_file_separator failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	for (;;) {
		lookup = fd < 0;

		path = nextpath;
		if (gfm_server == NULL ||
		    (lookup && gfarm_is_url(path))) {
			if (gfm_server)
				gfm_client_connection_free(gfm_server);
			gfm_server = NULL;
			if ((e = gfarm_url_parse_metadb(
			    (const char **)&path, &gfm_server))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001266,
				    "gfarm_url_parse_metadb(%s) failed: %s",
				    url, gfarm_error_string(e));
				break;
			}
			if (path[0] == '\0')
				path = "/";
		}

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "compound_begin(%s) request: %s", path,
			    gfarm_error_string(e));
			break;
		} else if (!lookup) {
			if ((e = gfm_client_put_fd_request(gfm_server, fd))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001266,
				    "put_fd(%d) request failed: %s",
				    (int)fd, gfarm_error_string(e));
				break;
			}
		} else {
			if ((e = gfm_inode_or_name_op_lookup_request(
			    gfm_server, path, flags, &rest))
			    != GFARM_ERR_NO_ERROR) {
				break;
			}
		}

		if (is_open_last)
			e = (*inode_request_op)(gfm_server, closure);
		else
			e = (*name_request_op)(gfm_server, closure, rest);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "request_op failed: %s",
			    gfarm_error_string(e));
			break;
		}

		if ((e = gfm_inode_or_name_op_on_error_request(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			break;
		}
		if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "compound_end request failed: %s",
			    gfarm_error_string(e));
			break;
		}

		is_retry = 0;
		if ((e = gfm_client_compound_begin_result(gfm_server))
			!= GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "compound_begin result: %s",
			    gfarm_error_string(e));
			break;
		} else if (!lookup) {
			if ((e = gfm_client_put_fd_result(gfm_server))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "put_fd(%d) result failed: %s",
				    (int)fd, gfarm_error_string(e));
				break;
			}
		} else {
			if ((e = gfm_inode_or_name_op_lookup_result(gfm_server,
			    path, flags, &rest, &is_last, &type,
			    &retry_count, &is_retry)) != GFARM_ERR_NO_ERROR) {
				if (is_retry)
					continue;
				if (e != GFARM_ERR_IS_A_SYMBOLIC_LINK)
					break;
				if ((e = gfm_inode_or_name_op_on_error_result(
				    gfm_server, flags, is_last,
				    &is_symlink, rest, &nextpath, &is_retry,
				    &tmpfd))
				    != GFARM_ERR_NO_ERROR)
					break;
				if (is_symlink) {
					fd = tmpfd;
					type = GFS_DT_LNK;
				}
				if (++nlinks <= GFARM_SYMLINK_LEVEL_MAX)
					continue;
				e = GFARM_ERR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINK;
				gflog_debug(GFARM_MSG_UNFIXED,
				    "maybe loop: %s",
				    gfarm_error_string(e));
				break;
			}
		}

		if (is_open_last) {
			if ((e = (*result_op)(gfm_server, closure))
				!= GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "result_op failed: %s",
				    gfarm_error_string(e));
				break;
			}
		} else if (!GFARM_IS_PATH_ROOT(path) &&
			    (e = (*result_op)(gfm_server, closure))
			    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "result_op failed: %s",
			    gfarm_error_string(e));
			break;
		}
		if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "compound_end result failed: %s",
			    gfarm_error_string(e));
			if (cleanup_op)
				(*cleanup_op)(gfm_server, closure);
			break;
		}
		is_success = 1;
		break;
	}

	if (is_success) {
		e = is_open_last ?
			(*inode_success_op)(gfm_server, closure, type, path) :
				/* needs 3rd,4th arguments for gfm_inode_op */
			(*name_success_op)(gfm_server, closure);
		if (nextpath)
			free(nextpath);
		return (e);
	}

	if (nextpath)
		free(nextpath);
	if (gfm_server)
		gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001267,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}


gfarm_error_t
gfm_inode_op(const char *url, int flags,
	gfarm_error_t (*request_op)(struct gfm_connection *, void *),
	gfarm_error_t (*result_op)(struct gfm_connection *, void *),
	gfarm_error_t (*success_op)(
	    struct gfm_connection *, void *, int, const char *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	void *closure)
{
	gfarm_error_t e = gfm_inode_or_name_op(url,
	    flags | GFARM_FILE_OPEN_LAST_COMPONENT,
	    request_op, success_op, NULL, NULL,
	    result_op, cleanup_op, closure);
	return (e == GFARM_ERR_PATH_IS_ROOT ?
		GFARM_ERR_OPERATION_NOT_PERMITTED : e);
}


gfarm_error_t
gfm_inode_op_no_follow(const char *url, int flags,
	gfarm_error_t (*request_op)(struct gfm_connection *, void *),
	gfarm_error_t (*result_op)(struct gfm_connection *, void *),
	gfarm_error_t (*success_op)(
	    struct gfm_connection *, void *, int, const char *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	void *closure)
{
	return (gfm_inode_op(url, flags | GFARM_FILE_SYMLINK_NO_FOLLOW,
		request_op, result_op, success_op, cleanup_op, closure));
}


gfarm_error_t
gfm_name_op(const char *url, gfarm_error_t root_error_code,
	gfarm_error_t (*request_op)(struct gfm_connection *, void *,
		const char *),
	gfarm_error_t (*result_op)(struct gfm_connection *, void *),
	gfarm_error_t (*success_op)(struct gfm_connection *, void *),
	void *closure)
{
	gfarm_error_t e = gfm_inode_or_name_op(url, 0,
	    NULL, NULL, request_op, success_op, result_op,
	    NULL, closure);
	return (e == GFARM_ERR_PATH_IS_ROOT ? root_error_code : e);
}


gfarm_error_t
gfm_name2_op(const char *src, const char *dst, int flags,
	gfarm_error_t (*inode_request_op)(struct gfm_connection *,
		void *, const char *),
	gfarm_error_t (*name_request_op)(struct gfm_connection *,
		void *, const char *, const char *),
	gfarm_error_t (*result_op)(struct gfm_connection *, void *),
	gfarm_error_t (*success_op)(struct gfm_connection *, void *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	void *closure)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR, se, de;
	struct gfm_connection *sconn = NULL, *dconn = NULL;
	const char *spath = NULL, *dpath = NULL;
	char *srest, *drest, *snextpath, *dnextpath;
	int type, s_is_last, d_is_last;
	int sretry, dretry, slookup, dlookup;
	int retry_count = 0, same_mds = 0, is_success = 0;
	int snlinks = 0, dnlinks = 0;
	int s_is_symlink = 0, d_is_symlink = 0;
	int is_open_last = (flags & GFARM_FILE_OPEN_LAST_COMPONENT) != 0;
	gfarm_int32_t tmpfd, sfd = -1, dfd = -1;

	snextpath = trim_tailing_file_separator(src);
	dnextpath = trim_tailing_file_separator(dst);
	if (snextpath == NULL || dnextpath == NULL) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "trim_tailing_file_separator failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	for (;;) {
		/*
		[same_mds == 1, slookup == 1, dlookup == 1]

			compound_begin(sconn)
			inode_or_name_op_lookup(sconn, flags=flags)
			get_fd(sconn)
			save_fd(sconn)
			inode_or_name_op_lookup(sconn, flags=0)
			get_fd(sconn)
			op(sconn)
			inode_or_name_op_on_error(sconn)
			compound_end

		[same_mds == 1, slookup == 0, dlookup == 1]

			compound_begin(sconn)
			put_fd(sconn)
			save_fd(sconn)
			inode_or_name_op_lookup(sconn, flags=0)
			get_fd(sconn)
			op(sconn)
			inode_or_name_op_on_error(sconn)
			compound_end

		[same_mds == 1, slookup == 1, dlookup == 0]

			compound_begin(sconn)
			inode_or_name_op_lookup(sconn, flags=flags)
			get_fd(sconn)
			save_fd(sconn)
			put_fd(sconn, flags=0)
			op(sconn)
			inode_or_name_op_on_error(sconn)
			compound_end

		[same_mds == 1, slookup == 0, dlookup == 0]

			compound_begin(sconn)
			put_fd(sconn)
			save_fd(sconn)
			put_fd(sconn)
			op(sconn)
			compound_end

		[same_mds == 0, slookup == 1, dlookup == 1]

			compound_begin(sconn)
			inode_or_name_op_lookup(sconn, flags=flags)
			get_fd(sconn)
			inode_or_name_op_on_error(sconn)
			compound_end

			compound_begin(dconn)
			inode_or_name_op_lookup(dconn, flags=0)
			get_fd(dconn)
			inode_or_name_op_on_error(dconn)
			compound_end

		[same_mds == 0, slookup == 0, dlookup == 1]

			compound_begin(dconn)
			inode_or_name_op_lookup(dconn, flags=0)
			get_fd(dconn)
			inode_or_name_op_on_error(dconn)
			compound_end

		[same_mds == 0, slookup == 1, dlookup == 0]

			compound_begin(sconn)
			inode_or_name_op_lookup(sconn, flags=flags)
			get_fd(sconn)
			inode_or_name_op_on_error(sconn)
			compound_end

		[same_mds == 0, slookup == 0, dlookup == 0]

			ERR_CROSS_DEVICE_LINK
		*/

		slookup = sfd < 0;
		dlookup = dfd < 0;
		spath = snextpath;
		dpath = dnextpath;

		if (!slookup && !dlookup && !same_mds) {
			e = GFARM_ERR_CROSS_DEVICE_LINK;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s (%s, %s)",
			    gfarm_error_string(e),
			    spath, dpath);
			break;
		}
		if (sconn == NULL ||
		    (slookup && gfarm_is_url(spath))) {
			if (sconn)
				gfm_client_connection_free(sconn);
			sconn = NULL;
			if ((e = gfarm_url_parse_metadb(&spath, &sconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "gfarm_url_parse_metadb(%s) failed: %s",
				    src, gfarm_error_string(e));
				break;
			}
			if (spath[0] == '\0')
				spath = "/";
		}
		if (dconn == NULL ||
		    (dlookup && gfarm_is_url(dpath))) {
			if (dconn && dconn != sconn)
				gfm_client_connection_free(dconn);
			dconn = NULL;
			if ((e = gfarm_url_parse_metadb(&dpath, &dconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "gfarm_url_parse_metadb(%s) failed: %s",
				    dst, gfarm_error_string(e));
				break;
			}
			if (dpath[0] == '\0')
				dpath = "/";
		}

		same_mds = sconn == dconn;

		if ((slookup || same_mds) &&
		    (e = gfm_client_compound_begin_request(sconn))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "compound_begin request: %s",
			    gfarm_error_string(e));
			break;
		}
		if (dlookup && !same_mds &&
		    (e = gfm_client_compound_begin_request(dconn))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "compound_begin request: %s",
			    gfarm_error_string(e));
			break;
		}
		if (slookup) {
			if ((e = gfm_inode_or_name_op_lookup_request(sconn,
				spath, flags, &srest))
			    != GFARM_ERR_NO_ERROR) {
				break;
			}
			if ((e = gfm_client_get_fd_request(sconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "get_fd request: %s",
				    gfarm_error_string(e));
				break;
			}
		} else if (same_mds) {
			if ((e = gfm_client_put_fd_request(
				sconn, sfd))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "put_fd(%d) request: %s", sfd,
				    gfarm_error_string(e));
				break;
			}
		}
		if (same_mds) {
			if ((e = gfm_client_save_fd_request(sconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "save_fd request: %s",
				    gfarm_error_string(e));
				break;
			}
		}
		if (dlookup) {
			if ((e = gfm_inode_or_name_op_lookup_request(dconn,
				dpath, 0, &drest))
			    != GFARM_ERR_NO_ERROR) {
				break;
			}
			if ((e = gfm_client_get_fd_request(dconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "get_fd request: %s",
				    gfarm_error_string(e));
				break;
			}
		} else if (same_mds) {
			if ((e = gfm_client_put_fd_request(dconn, dfd))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "put_fd(%d) request: %s", dfd,
				    gfarm_error_string(e));
				break;
			}
		}
		if (same_mds) {
			if (is_open_last)
				e = (*inode_request_op)(sconn, closure, drest);
			else
				e = (*name_request_op)(sconn, closure, srest,
					drest);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "request_op failed: %s",
				    gfarm_error_string(e));
				break;
			}
		}
		if (slookup || same_mds) {
			if ((e = gfm_inode_or_name_op_on_error_request(sconn))
			    != GFARM_ERR_NO_ERROR) {
				break;
			}
			if ((e = gfm_client_compound_end_request(sconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "compound_end request failed: %s",
				    gfarm_error_string(e));
				break;
			}
		}
		if (dlookup && !same_mds) {
			if ((e = gfm_inode_or_name_op_on_error_request(dconn))
			    != GFARM_ERR_NO_ERROR)
				break;
			if ((e = gfm_client_compound_end_request(dconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "compound_end request failed: %s",
				    gfarm_error_string(e));
				break;
			}
		}

		if ((slookup || same_mds) &&
		    (e = gfm_client_compound_begin_result(sconn))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "compound_begin result: %s",
			    gfarm_error_string(e));
			break;
		}
		if (dlookup && !same_mds &&
		    (e = gfm_client_compound_begin_result(dconn))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "compound_begin result: %s",
			    gfarm_error_string(e));
			break;
		}
		se = de = GFARM_ERR_NO_ERROR;
		sretry = dretry = 0;
		if (slookup) {
			if ((se = gfm_inode_or_name_op_lookup_result(sconn,
			    spath, flags, &srest, &s_is_last, &type,
			    &retry_count, &sretry)) != GFARM_ERR_NO_ERROR) {
				if (sretry)
					continue;
				if (se == GFARM_ERR_IS_A_SYMBOLIC_LINK) {
					if (sconn != dconn)
						goto dst_lookup_result;
					goto on_error_result;
				}
				break;
			}
			if ((e = gfm_client_get_fd_result(sconn, &sfd))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "get_fd result: %s",
				    gfarm_error_string(e));
				break;
			}
		} else if (same_mds) {
			if ((e = gfm_client_put_fd_result(sconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "put_fd(%d) result: %s", sfd,
				    gfarm_error_string(e));
				break;
			}
		}
		if (same_mds) {
			if ((e = gfm_client_save_fd_result(sconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "save_fd result: %s",
				    gfarm_error_string(e));
				break;
			}
		}

dst_lookup_result:
		if (dlookup) {
			if ((de = gfm_inode_or_name_op_lookup_result(dconn,
			    dpath, 0, &drest, &d_is_last, &type,
			    &retry_count, &dretry)) != GFARM_ERR_NO_ERROR) {
				if (dretry)
					continue;
				if (de == GFARM_ERR_IS_A_SYMBOLIC_LINK)
					goto on_error_result;
				break;
			}
			if ((e = gfm_client_get_fd_result(dconn, &dfd))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "get_fd result: %s",
				    gfarm_error_string(e));
				break;
			}
		} else if (same_mds) {
			if ((e = gfm_client_put_fd_result(dconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "put_fd(%d) result: %s", dfd,
				    gfarm_error_string(e));
				break;
			}
		}
		if (same_mds &&
		    (e = (*result_op)(sconn, closure))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "result_op failed: %s",
			    gfarm_error_string(e));
			break;
		}

on_error_result:
		if (slookup &&
		    se == GFARM_ERR_IS_A_SYMBOLIC_LINK &&
		    (e = gfm_inode_or_name_op_on_error_result(sconn,
			flags, s_is_last, &s_is_symlink,
			srest, &snextpath, &sretry, &tmpfd))
		    != GFARM_ERR_NO_ERROR) {
			break;
		}
		if (dlookup &&
		    de == GFARM_ERR_IS_A_SYMBOLIC_LINK &&
		    (e = gfm_inode_or_name_op_on_error_result(dconn,
			0, d_is_last, &d_is_symlink,
			drest, &dnextpath, &dretry, &tmpfd))
		    != GFARM_ERR_NO_ERROR) {
			break;
		}
		if (((same_mds && se == GFARM_ERR_NO_ERROR &&
			de == GFARM_ERR_NO_ERROR) ||
		    (!same_mds && slookup && se == GFARM_ERR_NO_ERROR)) &&
		    (e = gfm_client_compound_end_result(sconn))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "compound_end result failed: %s",
			    gfarm_error_string(e));
			if (sconn == dconn && cleanup_op)
				(*cleanup_op)(sconn, closure);
			break;
		}
		if (dlookup && !same_mds &&
		    de == GFARM_ERR_NO_ERROR &&
		    (e = gfm_client_compound_end_result(dconn))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "compound_end result failed: %s",
			    gfarm_error_string(e));
			break;
		}
		if ((sretry && ++snlinks > GFARM_SYMLINK_LEVEL_MAX) ||
		    (dretry && ++dnlinks > GFARM_SYMLINK_LEVEL_MAX)) {
			e = GFARM_ERR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINK;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "maybe loop: %s",
			    gfarm_error_string(e));
			break;
		}
		if (slookup && s_is_symlink)
			sfd = tmpfd;
		if (same_mds && !sretry && !dretry &&
		    (!slookup || !s_is_symlink)) {
			is_success = 1;
			break;
		}
	}

	if (snextpath)
		free(snextpath);
	if (dnextpath)
		free(dnextpath);

	if (is_success)
		return (*success_op)(sconn, closure);

	if (sconn)
		gfm_client_connection_free(sconn);
	if (dconn && !same_mds)
		gfm_client_connection_free(dconn);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}
