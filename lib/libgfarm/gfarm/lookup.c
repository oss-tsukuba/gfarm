#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>

#define GFARM_INTERNAL_USE /* GFARM_FILE_LOOKUP, gfs_mode_to_type(), etc. */
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"

#include "context.h"
#include "config.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "lookup.h"
#include "filesystem.h"
#include "gfs_failover.h"

static gfarm_error_t
gfarm_get_hostname_by_url0(const char **pathp,
	char **hostnamep, int *portp)
{
	const char *p, *path = pathp ? *pathp : NULL;
	char *ep, *hostname;
	unsigned long port;
	int nohost = 1, noport = 1;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	hostname = NULL;
	port = 0;
#endif
	if (path == NULL || !gfarm_is_url(path))
		goto finish;

	path += GFARM_URL_PREFIX_LENGTH;
	if (path[0] != '/' || path[1] != '/') {
		gflog_debug(GFARM_MSG_1001254,
			"Host missing in url (%s): %s", *pathp,
			gfarm_error_string(
				GFARM_ERR_GFARM_URL_HOST_IS_MISSING));
		goto finish;
	}
	path += 2; /* skip "//" */
	for (p = path;
	    *p != '\0' &&
	    (isalnum(*(unsigned char *)p) || *p == '-' || *p == '.');
	    p++)
		;
	if (p == path) {
		gflog_debug(GFARM_MSG_1001255,
			"Host missing in url (%s): %s", *pathp,
			gfarm_error_string(
				GFARM_ERR_GFARM_URL_HOST_IS_MISSING));
		goto finish;
	}
	GFARM_MALLOC_ARRAY(hostname, p - path + 1);
	if (hostname == NULL) {
		gflog_debug(GFARM_MSG_1002312,
		    "allocating gfm server name for '%s': "
		    "no memory", *pathp);
		return (GFARM_ERR_NO_MEMORY);
	}
	memcpy(hostname, path, p - path);
	hostname[p - path] = '\0';
	nohost = 0;

	if (*p != ':') {
		gflog_debug(GFARM_MSG_1001256,
		    "Port missing in url (%s): %s", *pathp,
		    gfarm_error_string(GFARM_ERR_GFARM_URL_PORT_IS_MISSING));
		if (*p != '\0' && *p != '/') {
			free(hostname);
			return (GFARM_ERR_GFARM_URL_PORT_IS_MISSING);
		}
		path = p;
		goto finish;
	}
	p++; /* skip ":" */
	if (*p == '\0' || *p == '/') {
		gflog_debug(GFARM_MSG_1001257,
		    "Port missing in url (%s): %s", *pathp,
		    gfarm_error_string(GFARM_ERR_GFARM_URL_PORT_IS_MISSING));
		path = p;
		goto finish;
	}
	errno = 0;
	port = strtoul(p, &ep, 10);
	if (p == ep || (*ep != '\0' && *ep != '/')) {
		free(hostname);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "Port invalid in url (%s): %s", *pathp,
		    gfarm_error_string(GFARM_ERR_GFARM_URL_PORT_IS_INVALID));
		return (GFARM_ERR_GFARM_URL_PORT_IS_INVALID);
	}
	path = ep;
	if (errno == ERANGE || port == ULONG_MAX ||
	    port <= 0 || port >= 65536) {
		free(hostname);
		gflog_debug(GFARM_MSG_1001258,
		    "Port invalid in url (%s): %s", *pathp,
		    gfarm_error_string(GFARM_ERR_GFARM_URL_PORT_IS_INVALID));
		return (GFARM_ERR_GFARM_URL_PORT_IS_INVALID);
	}
	noport = 0;

 finish:
	if (hostnamep != NULL) {
		if (nohost) {
			*hostnamep = strdup(gfarm_ctxp->metadb_server_name);
			if (*hostnamep == NULL)
				return (GFARM_ERR_NO_MEMORY);
		} else
			*hostnamep = hostname;
	} else if (nohost == 0)
		free(hostname);
	if (portp != NULL) {
		if (noport)
			*portp = gfarm_ctxp->metadb_server_port;
		else
			*portp = port;
	}
	if (pathp != NULL)
		*pathp = path;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_get_hostname_by_url(const char *path,
	char **hostnamep, int *portp)
{
	return (gfarm_get_hostname_by_url0(&path, hostnamep, portp));
}

gfarm_error_t
gfarm_url_parse_metadb(const char **pathp,
	struct gfm_connection **gfm_serverp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	struct gfarm_filesystem *fs;
	char *hostname;
	int port;
	char *user = NULL;

	if ((e = gfarm_get_hostname_by_url0(pathp, &hostname, &port))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002585,
		    "gfarm_get_hostname_by_url0 failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	if (gfm_serverp == NULL) {
		e = GFARM_ERR_NO_ERROR;
		goto end;
	} else if ((e = gfarm_get_global_username_by_host_for_connection_cache(
	    hostname, port, &user)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002587,
		    "gfarm_get_global_username_by_host_for_connection_cache: "
		    "%s", gfarm_error_string(e));
		goto end;
	}

	/*
	 * If failover was detected, all opened files must be failed
	 * over and reset fds before opening new fd not to conflict
	 * old fd and new fd in gfsd.
	 *
	 * this path is called from inode operations including
	 * gfs_pio_open(), gfs_pio_create().
	 */
	fs = gfarm_filesystem_get(hostname, port);
	if (gfarm_filesystem_failover_detected(fs) &&
	    !gfarm_filesystem_in_failover_process(fs)) {
		if ((e = gfm_client_connection_failover_pre_connect(
		    hostname, port, user)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "gfm_client_connection_failover_acquired: %s",
			    gfarm_error_string(e));
			goto end;
		}
	}

	if ((e = gfm_client_connection_and_process_acquire(
		    hostname, port, user, &gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_client_connection_and_process_acquire: %s",
		    gfarm_error_string(e));
	} else
		*gfm_serverp = gfm_server; /* gfm_serverp is not NULL */
end:
	free(hostname);
	free(user);

	return (e);
}

gfarm_error_t
gfm_client_connection_and_process_acquire_by_path(const char *path,
	struct gfm_connection **gfm_serverp)
{
	return (gfarm_url_parse_metadb(&path, gfm_serverp));
}

int
gfm_is_mounted(struct gfm_connection *gfm_server)
{
	struct gfm_connection *gfm_root;
	int rv;

	if (gfm_client_connection_and_process_acquire_by_path("/",
	    &gfm_root) == GFARM_ERR_NO_ERROR) {
		rv = gfm_server == gfm_root;
		gfm_client_connection_free(gfm_root);
		return (rv);
	}
	return (0);
}

#define SKIP_SLASH(p) { while (*(p) == '/') (p)++; }

static gfarm_error_t
gfm_lookup_dir_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, const char *path,
	const char **basep, int *is_lastp)
{
	gfarm_error_t e;
	int beginning = 1;
	int len;

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
				e = gfm_client_open_root_request(gfm_server,
				    ctx, GFARM_FILE_LOOKUP);
				if (e != GFARM_ERR_NO_ERROR)
					break;
			}
			e = GFARM_ERR_NO_ERROR;
			if (is_lastp != NULL)
				*is_lastp = 1;
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
			    ctx, GFARM_FILE_LOOKUP);
			if (e != GFARM_ERR_NO_ERROR)
				break;
			beginning = 0;
		}
		e = gfm_client_open_request(gfm_server, ctx, path, len,
		    GFARM_FILE_LOOKUP);
		if (e != GFARM_ERR_NO_ERROR)
			break;
		path += len;
		SKIP_SLASH(path);
		if ((e = gfm_client_verify_type_request(gfm_server, ctx,
		    GFS_DT_DIR)) != GFARM_ERR_NO_ERROR)
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
gfm_lookup_dir_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, const char *path,
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
				e = gfm_client_open_root_result(
				    gfm_server, ctx);
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
			e = gfm_client_open_root_result(gfm_server, ctx);
			if (e != GFARM_ERR_NO_ERROR)
				break;
			beginning = 0;
		}
		e = gfm_client_open_result(gfm_server, ctx,
		    &inum, &gen, &mode);
		if (e != GFARM_ERR_NO_ERROR)
			break;
		path += len;
		SKIP_SLASH(path);
		if ((e = gfm_client_verify_type_result(gfm_server, ctx))
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
	void *closure, int type, const char *path, gfarm_ino_t ino)
{
	return (gfm_inode_success_op_connection_free(gfm_server, closure, type,
		path, ino));
}


gfarm_error_t
gfm_name2_success_op_connection_free(struct gfm_connection *gfm_server,
	void *closure)
{
	gfm_client_connection_free(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}


gfarm_error_t
gfm_inode_success_op_connection_free(struct gfm_connection *gfm_server,
	void *closure, int type, const char *path, gfarm_ino_t ino)
{
	gfm_client_connection_free(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_alloc_link_destination(struct gfm_connection *gfm_server,
	char *link, char **nextpathp, char *rest, int is_last)
{
	char *p, *p0, *n = *nextpathp;
	int i, len, blen, linklen, is_rel;

	linklen = link == NULL ? 0 : strlen(link);
	if (linklen == 0) {
		gflog_debug(GFARM_MSG_1002588,
		    "symlink is empty : %s", n);
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	}

	blen = strlen(rest);
	if (blen > 0) {
		i = blen;
		while (i > 0 && rest[i - 1] == '/')
			--i;
		if (i < blen) {
			rest[i] = '\0';
			blen = i;
		}
	}

	is_rel = link[0] != '/' &&
	    (linklen < GFARM_URL_PREFIX_LENGTH || !gfarm_is_url(link));
	len = linklen + (is_last ? 0 : blen + 1) +
		(is_rel ? strlen(n) + 1 : 0);
	GFARM_MALLOC_ARRAY(p, len + 1);
	p0 = p;

	if (is_rel) {
		/* add relative path */
		i = rest - n;
		if (!is_last) {
			while (i > 0 && n[i - 1] == '/')
				--i;
			while (i > 0 && n[i - 1] != '/')
				--i;
		}
		while (i > 0 && n[i - 1] == '/')
			--i;
		if (i > 0)
			memcpy(p, n, i);
		p += i;
		*p++ = '/';
	}

	strcpy(p, link);
	p += linklen;
	if (!is_last) {
		*p++ = '/';
		strcpy(p, rest);
		p += blen;
	}
	*p = 0;
	free(n);
	*nextpathp = p0;
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
	struct gfp_xdr_context *ctx,
	const char *path, int flags, char **restp, int *do_verifyp)
{
	gfarm_error_t e;
	int is_last;
	int pflags = (flags & GFARM_FILE_PROTOCOL_MASK);
	int is_open_last = (flags & GFARM_FILE_OPEN_LAST_COMPONENT) != 0;

	if ((e = gfm_lookup_dir_request(gfm_server, ctx, path,
		(const char **)restp, &is_last)) != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1002589,
		    "lookup_dir(%s) request: %s", path,
		    gfarm_error_string(e));
		return (e);
	}
	if (!is_open_last)
		return (GFARM_ERR_NO_ERROR);

	*do_verifyp = ((flags & GFARM_FILE_SYMLINK_NO_FOLLOW) == 0) || !is_last;
	if (GFARM_IS_PATH_ROOT(path)) {
		if ((e = gfm_client_open_root_request(
			    gfm_server, ctx, pflags))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002590,
			    "open_root(flags=%d) "
			    "request failed: %s",
			    pflags, gfarm_error_string(e));
			return (e);
		}
	} else {
		if ((e = gfm_client_open_request(
			gfm_server, ctx, *restp, strlen(*restp), pflags))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002591,
			    "open(name=%s,flags=%d) "
			    "request failed: %s", *restp, pflags,
			    gfarm_error_string(e));
			return (e);
		}
		if (*do_verifyp && (e = gfm_client_verify_type_not_request(
		    gfm_server, ctx, GFS_DT_LNK)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002592,
			    "verify_type_not_request failed: %s",
			    gfarm_error_string(e));
			return (e);
		}
	}

	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_inode_or_name_op_lookup_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx,
	const char *path, int flags, int do_verify, char **restp,
	int *typep, int *retry_countp, int *is_lastp, int *is_retryp,
	gfarm_ino_t *inop)
{
	gfarm_error_t e;
	int is_open_last = (flags & GFARM_FILE_OPEN_LAST_COMPONENT) != 0;

	if ((e = gfm_lookup_dir_result(gfm_server, ctx, path,
	    (const char **)restp, is_lastp)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002593,
		    "lookup_dir(path=%s) result failed: %s",
		    path, gfarm_error_string(e));
		return (e);
	}

	if (!is_open_last)
		return (GFARM_ERR_NO_ERROR);

	if (GFARM_IS_PATH_ROOT(path)) {/* "/" is special */
		if ((e = gfm_client_open_root_result(gfm_server, ctx))
			!= GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002594,
			    "open_root result failed: %s",
			    gfarm_error_string(e));
			return (e);
		}
		*typep = GFS_DT_DIR;
	} else {
		gfarm_ino_t inum;
		gfarm_uint64_t gen;
		gfarm_mode_t mode;

		if ((e = gfm_client_open_result(gfm_server, ctx,
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
		if (do_verify &&
		    (e = gfm_client_verify_type_not_result(gfm_server, ctx))
		    != GFARM_ERR_NO_ERROR) {
			if (e != GFARM_ERR_IS_A_SYMBOLIC_LINK) {
				gflog_debug(GFARM_MSG_1002595,
				    "verify_type_not result failed: %s",
				    gfarm_error_string(e));
			}
			return (e);
		}
		*typep = gfs_mode_to_type(mode);
		if (inop)
			*inop = inum;
	}

	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_inode_or_name_op_on_error_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, struct gfp_xdr_xid_record **xidrp)
{
	gfarm_error_t e;
	struct gfp_xdr_xid_record *xidr;

	if ((e = gfm_client_compound_on_error_request(gfm_server, ctx,
		GFARM_ERR_IS_A_SYMBOLIC_LINK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002596,
		    "compound_on_error request failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	xidr = gfm_client_context_get_pos(gfm_server, ctx);
	if ((e = gfm_client_readlink_request(gfm_server, ctx))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002597,
		    "readlink request failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	*xidrp = xidr;
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfm_inode_or_name_op_on_error_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx,
	int is_last, char *rest, char **nextpathp, int *is_retryp)
{
	gfarm_error_t e;
	char *link;

	if ((e = gfm_client_readlink_result(gfm_server, ctx, &link))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002598,
		    "readlink result failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if ((e = gfm_alloc_link_destination(gfm_server, link, nextpathp,
	    rest, is_last)) == GFARM_ERR_NO_ERROR)
		*is_retryp = 1;
	free(link);
	return (e);
}

static gfarm_error_t
gfm_inode_or_name_op_end_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, struct gfp_xdr_xid_record **xidrp)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_end_request(gfm_server, ctx))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	*xidrp = gfm_client_context_get_pos(gfm_server, ctx);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfm_inode_or_name_op0(const char *url, int flags,
	gfm_inode_request_op_t inode_request_op,
	gfm_name_request_op_t name_request_op,
	gfm_result_op_t result_op, gfm_success_op_t success_op,
	gfm_cleanup_op_t cleanup_op, void *closure,
	struct gfm_connection **gfm_serverp)
{
	gfarm_error_t e, e2;
	struct gfm_connection *gfm_server = NULL;
	struct gfp_xdr_context *ctx = NULL;
	struct gfp_xdr_xid_record *on_error_pos = NULL;
	int type;
	int retry_count = 0, nlinks = 0;
	char *path;
	char *rest, *nextpath;
	int do_verify, is_last, is_retry;
	int is_success = 0;
	int is_open_last = (flags & GFARM_FILE_OPEN_LAST_COMPONENT) != 0;
	gfarm_ino_t ino;

	nextpath = trim_tailing_file_separator(url);
	if (nextpath == NULL) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1002599,
		    "trim_tailing_file_separator failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	for (;;) {
		path = nextpath;
		if (gfm_server == NULL || gfarm_is_url(path)) {
			if (gfm_server) {
				gfm_client_connection_unlock(gfm_server);
				if (ctx != NULL) {
					gfm_client_context_free(gfm_server,
					    ctx);
					ctx = NULL;
				}
				gfm_client_connection_free(gfm_server);
				gfm_server = NULL;
			}
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
			gfm_client_connection_lock(gfm_server);
		}
		if (!is_open_last && GFARM_IS_PATH_ROOT(path)) {
			e = GFARM_ERR_PATH_IS_ROOT;
			gflog_debug(GFARM_MSG_1002600,
			    "inode_or_name_op_lookup_request : %s",
			    gfarm_error_string(e));
			break;
		}

		if (ctx != NULL) {
			gfm_client_context_free(gfm_server, ctx);
			ctx = NULL;
		}
		if ((e = gfm_client_context_alloc(gfm_server, &ctx)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "gfm_client_context_alloc: %s",
			    gfarm_error_string(e));
			break;
		}

		if ((e = gfm_client_compound_begin_request(gfm_server, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1002601,
			    "compound_begin(%s) request: %s", path,
			    gfarm_error_string(e));
			break;
		} else if ((e = gfm_inode_or_name_op_lookup_request(
		    gfm_server, ctx, path, flags, &rest, &do_verify))
		    != GFARM_ERR_NO_ERROR)
			break;

		if (is_open_last)
			e = (*inode_request_op)(gfm_server, ctx, closure);
		else
			e = (*name_request_op)(gfm_server, ctx, closure, rest);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002602,
			    "request_op failed: %s",
			    gfarm_error_string(e));
			break;
		}
		if ((e = gfm_inode_or_name_op_on_error_request(
		    gfm_server, ctx, &on_error_pos)) != GFARM_ERR_NO_ERROR)
			break;
		if ((e = gfm_client_compound_end_request(gfm_server, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002603,
			    "compound_end request failed: %s",
			    gfarm_error_string(e));
			break;
		}

		is_retry = 0;
		if ((e = gfm_client_compound_begin_result(gfm_server, ctx))
			!= GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1002604,
			    "compound_begin result: %s",
			    gfarm_error_string(e));
			break;
		} else if ((e = gfm_inode_or_name_op_lookup_result(gfm_server,
		    ctx, path, flags, do_verify, &rest, &type, &retry_count,
		    &is_last, &is_retry, &ino)) != GFARM_ERR_NO_ERROR) {
			if (is_retry)
				continue;
			gfm_client_context_free_until(gfm_server, ctx,
			    on_error_pos);
			if ((e2 = gfm_client_compound_on_error_result(
			    gfm_server, ctx)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "compound_on_error result failed: %s",
				    gfarm_error_string(e2));
				return (e2);
			}
			if (e != GFARM_ERR_IS_A_SYMBOLIC_LINK)
				break;
			if ((e = gfm_inode_or_name_op_on_error_result(
			    gfm_server, ctx, is_last, rest,
			    &nextpath, &is_retry)) != GFARM_ERR_NO_ERROR)
				break;
			if (++nlinks <= GFARM_SYMLINK_LEVEL_MAX)
				continue;
			e = GFARM_ERR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINK;
			gflog_debug(GFARM_MSG_1002605,
			    "maybe loop: %s",
			    gfarm_error_string(e));
			break;
		}

		if (is_open_last) {
			if ((e = (*result_op)(gfm_server, ctx, closure))
				!= GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002606,
				    "result_op failed: %s",
				    gfarm_error_string(e));
				break;
			}
		} else if (!GFARM_IS_PATH_ROOT(path) &&
		    (e = (*result_op)(gfm_server, ctx, closure))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002607,
			    "result_op failed: %s",
			    gfarm_error_string(e));
			break;
		}
		if ((e = gfm_client_compound_end_result(gfm_server, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002608,
			    "compound_end result failed: %s",
			    gfarm_error_string(e));
			if (cleanup_op)
				(*cleanup_op)(gfm_server, closure);
			break;
		}
		is_success = 1;
		break;
	}
	if (ctx != NULL)
		gfm_client_context_free(gfm_server, ctx);

	if (gfm_server) {
		*gfm_serverp = gfm_server;
		gfm_client_connection_unlock(gfm_server);
	}
	if (is_success) {
		e = (*success_op)(gfm_server, closure, type, path, ino);
		if (nextpath)
			free(nextpath);
		return (e);
	}

	if (nextpath)
		free(nextpath);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001267,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}

struct inode_or_name_op_info {
	const char *url;
	int flags;
	gfm_inode_request_op_t inode_request_op;
	gfm_name_request_op_t name_request_op;
	gfm_result_op_t result_op;
	gfm_success_op_t success_op;
	gfm_cleanup_op_t cleanup_op;
	gfm_must_be_warned_op_t must_be_warned_op;
	void *closure;
};

static gfarm_error_t
inode_or_name_op_rpc(struct gfm_connection **gfm_serverp, void *closure)
{
	gfarm_error_t e;
	struct inode_or_name_op_info *ii = closure;

	e = gfm_inode_or_name_op0(ii->url, ii->flags,
	    ii->inode_request_op, ii->name_request_op,
	    ii->result_op, ii->success_op, ii->cleanup_op,
	    ii->closure, gfm_serverp);
	return (e);
}

static gfarm_error_t
inode_or_name_op_post_failover(struct gfm_connection *gfm_server,
	void *closure)
{
	if (gfm_server)
		gfm_client_connection_free(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}

static void
inode_or_name_op_exit(struct gfm_connection *gfm_server, gfarm_error_t e,
	void *closure)
{
	if (e != GFARM_ERR_NO_ERROR) {
		/* do not release connection when no error occurred. */
		(void)inode_or_name_op_post_failover(gfm_server, closure);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_inode_or_name_op: %s",
		    gfarm_error_string(e));
	}
}

static int
inode_or_name_op_must_be_warned(gfarm_error_t e, void *closure)
{
	struct inode_or_name_op_info *ii = closure;

	return (ii->must_be_warned_op ? ii->must_be_warned_op(e, ii->closure) :
	    0);
}

static gfarm_error_t
gfm_inode_or_name_op(const char *url, int flags,
	gfm_inode_request_op_t inode_request_op,
	gfm_name_request_op_t name_request_op,
	gfm_result_op_t result_op, gfm_success_op_t success_op,
	gfm_cleanup_op_t cleanup_op, gfm_must_be_warned_op_t must_be_warned_op,
	void *closure)
{
	struct inode_or_name_op_info ii = {
		url, flags,
		inode_request_op, name_request_op, result_op,
		success_op, cleanup_op, must_be_warned_op, closure,
	};

	return (gfm_client_rpc_with_failover(inode_or_name_op_rpc,
	    inode_or_name_op_post_failover, inode_or_name_op_exit,
	    inode_or_name_op_must_be_warned, &ii));
}

static gfarm_error_t
gfm_inode_op(const char *url, int flags,
	gfm_inode_request_op_t request_op, gfm_result_op_t result_op,
	gfm_success_op_t success_op, gfm_cleanup_op_t cleanup_op,
	gfm_must_be_warned_op_t must_be_warned_op, void *closure)
{
	gfarm_error_t e = gfm_inode_or_name_op(url,
	    flags | GFARM_FILE_OPEN_LAST_COMPONENT,
	    request_op, NULL, result_op, success_op, cleanup_op,
	    must_be_warned_op, closure);

	return (e == GFARM_ERR_PATH_IS_ROOT ?
		GFARM_ERR_OPERATION_NOT_PERMITTED : e);
}

gfarm_error_t
gfm_inode_op_readonly(const char *url, int flags,
	gfm_inode_request_op_t request_op, gfm_result_op_t result_op,
	gfm_success_op_t success_op, gfm_cleanup_op_t cleanup_op, void *closure)
{
	return (gfm_inode_op(url, flags, request_op, result_op, success_op,
	    cleanup_op, NULL, closure));
}

gfarm_error_t
gfm_inode_op_modifiable(const char *url, int flags,
	gfm_inode_request_op_t request_op, gfm_result_op_t result_op,
	gfm_success_op_t success_op, gfm_cleanup_op_t cleanup_op,
	gfm_must_be_warned_op_t must_be_warned_op, void *closure)
{
	return (gfm_inode_op(url, flags, request_op, result_op, success_op,
	    cleanup_op, must_be_warned_op, closure));
}

static gfarm_error_t
gfm_inode_op_no_follow(const char *url, int flags,
	gfm_inode_request_op_t request_op, gfm_result_op_t result_op,
	gfm_success_op_t success_op, gfm_cleanup_op_t cleanup_op,
	gfm_must_be_warned_op_t must_be_warned_op, void *closure)
{
	return (gfm_inode_op(url, flags | GFARM_FILE_SYMLINK_NO_FOLLOW,
	    request_op, result_op, success_op, cleanup_op, must_be_warned_op,
	    closure));
}

gfarm_error_t
gfm_inode_op_no_follow_readonly(const char *url, int flags,
	gfm_inode_request_op_t request_op, gfm_result_op_t result_op,
	gfm_success_op_t success_op, gfm_cleanup_op_t cleanup_op, void *closure)
{
	return (gfm_inode_op_no_follow(url, flags, request_op, result_op,
	    success_op, cleanup_op, NULL, closure));
}

gfarm_error_t
gfm_inode_op_no_follow_modifiable(const char *url, int flags,
	gfm_inode_request_op_t request_op, gfm_result_op_t result_op,
	gfm_success_op_t success_op, gfm_cleanup_op_t cleanup_op,
	gfm_must_be_warned_op_t must_be_warned_op, void *closure)
{
	return (gfm_inode_op_no_follow(url, flags, request_op, result_op,
	    success_op, cleanup_op, must_be_warned_op, closure));
}

static gfarm_error_t
close_fd2_locked(struct gfm_connection *conn, int fd1, int fd2)
{
	gfarm_error_t e;
	struct gfp_xdr_context *ctx = NULL;

	if (fd1 == GFARM_DESCRIPTOR_INVALID && fd2 == GFARM_DESCRIPTOR_INVALID)
		return (GFARM_ERR_INVALID_ARGUMENT);

	if ((e = gfm_client_context_alloc(conn, &ctx)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_client_context_alloc: %s",
		    gfarm_error_string(e));
		return (e);
	}

	if ((e = gfm_client_compound_begin_request(conn, ctx))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002609,
		    "compound_begin request: %s", gfarm_error_string(e));
		gfm_client_context_free(conn, ctx);
		return (e);
	}
	if (fd1 != GFARM_DESCRIPTOR_INVALID) {
		if ((e = gfm_client_put_fd_request(conn, ctx, fd1))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002610,
			    "put_fd request: %s", gfarm_error_string(e));
			gfm_client_context_free(conn, ctx);
			return (e);
		}
		if ((e = gfm_client_close_request(conn, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002611,
			    "close request: %s", gfarm_error_string(e));
			gfm_client_context_free(conn, ctx);
			return (e);
		}
	}
	if (fd2 != GFARM_DESCRIPTOR_INVALID) {
		if ((e = gfm_client_put_fd_request(conn, ctx, fd2))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002612,
			    "put_fd request: %s", gfarm_error_string(e));
			gfm_client_context_free(conn, ctx);
			return (e);
		}
		if ((e = gfm_client_close_request(conn, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002613,
			    "close request: %s", gfarm_error_string(e));
			gfm_client_context_free(conn, ctx);
			return (e);
		}
	}
	if ((e = gfm_client_compound_end_request(conn, ctx))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002614,
		    "compound_end request: %s", gfarm_error_string(e));
		gfm_client_context_free(conn, ctx);
		return (e);
	}
	if ((e = gfm_client_compound_begin_result(conn, ctx))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002615,
		    "compound_begin result: %s", gfarm_error_string(e));
		gfm_client_context_free(conn, ctx);
		return (e);
	}
	if (fd1 != GFARM_DESCRIPTOR_INVALID) {
		if ((e = gfm_client_put_fd_result(conn, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002616,
			    "put_fd result: %s", gfarm_error_string(e));
			gfm_client_context_free(conn, ctx);
			return (e);
		}
		if ((e = gfm_client_close_result(conn, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002617,
			    "close result: %s", gfarm_error_string(e));
			gfm_client_context_free(conn, ctx);
			return (e);
		}
	}
	if (fd2 != GFARM_DESCRIPTOR_INVALID) {
		if ((e = gfm_client_put_fd_result(conn, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002618,
			    "put_fd result: %s", gfarm_error_string(e));
			gfm_client_context_free(conn, ctx);
			return (e);
		}
		if ((e = gfm_client_close_result(conn, ctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002619,
			    "close result: %s", gfarm_error_string(e));
			gfm_client_context_free(conn, ctx);
			return (e);
		}
	}
	if ((e = gfm_client_compound_end_result(conn, ctx))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002620,
		    "compound_end result: %s", gfarm_error_string(e));

	gfm_client_context_free(conn, ctx);
	return (e);
}
static gfarm_error_t
close_fd2(struct gfm_connection *conn, int fd1, int fd2)
{
	gfarm_error_t e;
	gfm_client_connection_lock(conn);
	e = close_fd2_locked(conn, fd1, fd2);
	gfm_client_connection_unlock(conn);
	return (e);
}

static gfarm_error_t
gfm_name_op(const char *url, gfarm_error_t root_error_code,
	gfm_name_request_op_t request_op, gfm_result_op_t result_op,
	gfm_success_op_t success_op, gfm_must_be_warned_op_t must_be_warned_op,
	void *closure)
{
	gfarm_error_t e = gfm_inode_or_name_op(url, 0,
	    NULL, request_op, result_op, success_op, NULL, must_be_warned_op,
	    closure);

	return (e == GFARM_ERR_PATH_IS_ROOT ? root_error_code : e);
}

gfarm_error_t
gfm_name_op_modifiable(const char *url, gfarm_error_t root_error_code,
	gfm_name_request_op_t request_op, gfm_result_op_t result_op,
	gfm_success_op_t success_op, gfm_must_be_warned_op_t must_be_warned_op,
	void *closure)
{
	return (gfm_name_op(url, root_error_code, request_op, result_op,
	    success_op, must_be_warned_op, closure));
}

static gfarm_error_t
gfm_name2_op0(const char *src, const char *dst, int flags,
	gfm_name2_inode_request_op_t inode_request_op,
	gfm_name2_request_op_t name_request_op,
	gfm_result_op_t result_op, gfm_name2_success_op_t success_op,
	gfm_cleanup_op_t cleanup_op, void *closure,
	struct gfm_connection **sconnp, struct gfm_connection **dconnp,
	int *is_dst_errp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR, se, de, e2, e3;
	struct gfm_connection *sconn = NULL, *dconn = NULL;
	struct gfp_xdr_context *sctx = NULL, *dctx = NULL;
	struct gfp_xdr_xid_record *s_end_pos = NULL, *s_on_error_pos = NULL;
	struct gfp_xdr_xid_record *d_end_pos = NULL, *d_on_error_pos = NULL;
	const char *spath = NULL, *dpath = NULL;
	char *srest, *drest, *snextpath, *dnextpath;
	int type, s_is_last, d_is_last, s_do_verify, d_do_verify;
	int sretry, dretry, slookup, dlookup;
	int retry_count = 0, same_mds = 0, is_success = 0, op_called = 0;
	int snlinks = 0, dnlinks = 0;
	int is_open_last = (flags & GFARM_FILE_OPEN_LAST_COMPONENT) != 0;
	gfarm_int32_t sfd = GFARM_DESCRIPTOR_INVALID;
	gfarm_int32_t dfd = GFARM_DESCRIPTOR_INVALID;
	gfarm_ino_t ino;

	snextpath = trim_tailing_file_separator(src);
	dnextpath = trim_tailing_file_separator(dst);
	if (snextpath == NULL || dnextpath == NULL) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1002621,
		    "trim_tailing_file_separator failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	for (;;) {
		/*
		 * [same_mds == 1, slookup == 1, dlookup == 1]
		 *
		 * 	compound_begin(sconn)
		 * 	inode_or_name_op_lookup(sconn, spath, flags=flags)
		 * 	get_fd(sconn)
		 * 	save_fd(sconn)
		 * 	inode_or_name_op_lookup(sconn, dpath, flags=0)
		 * 	get_fd(sconn)
		 * 	op(sconn)
		 *	close
		 *	restore
		 *	close
		 * 	inode_or_name_op_on_error(sconn)
		 * 	compound_end
		 *
		 * [same_mds == 1, slookup == 0, dlookup == 1]
		 *
		 * 	compound_begin(sconn)
		 * 	put_fd(sconn)
		 * 	save_fd(sconn)
		 * 	inode_or_name_op_lookup(sconn, dpath, flags=0)
		 * 	get_fd(sconn)
		 * 	op(sconn)
		 *	close
		 *	restore
		 *	close
		 * 	inode_or_name_op_on_error(sconn)
		 * 	compound_end
		 *
		 * [same_mds == 1, slookup == 1, dlookup == 0]
		 *
		 * 	compound_begin(sconn)
		 * 	inode_or_name_op_lookup(sconn, spath, flags=flags)
		 * 	get_fd(sconn)
		 * 	save_fd(sconn)
		 * 	put_fd(sconn, flags=0)
		 * 	op(sconn)
		 *	close
		 *	restore
		 *	close
		 * 	inode_or_name_op_on_error(sconn)
		 * 	compound_end
		 *
		 * [same_mds == 1, slookup == 0, dlookup == 0]
		 *
		 * 	compound_begin(sconn)
		 * 	put_fd(sconn)
		 * 	save_fd(sconn)
		 * 	put_fd(sconn)
		 * 	op(sconn)
		 *	close
		 *	restore
		 *	close
		 * 	compound_end
		 *
		 * [same_mds == 0, slookup == 1, dlookup == 1]
		 *
		 * 	compound_begin(sconn)
		 * 	inode_or_name_op_lookup(sconn, spath, flags=flags)
		 * 	get_fd(sconn)
		 * 	inode_or_name_op_on_error(sconn)
		 * 	compound_end
		 *
		 * 	compound_begin(dconn)
		 * 	inode_or_name_op_lookup(dconn, dpath, flags=0)
		 * 	get_fd(dconn)
		 * 	inode_or_name_op_on_error(dconn)
		 * 	compound_end
		 *
		 * [same_mds == 0, slookup == 0, dlookup == 1]
		 *
		 * 	compound_begin(dconn)
		 * 	inode_or_name_op_lookup(dconn, dpath, flags=0)
		 * 	get_fd(dconn)
		 * 	inode_or_name_op_on_error(dconn)
		 * 	compound_end
		 *
		 * [same_mds == 0, slookup == 1, dlookup == 0]
		 *
		 * 	compound_begin(sconn)
		 * 	inode_or_name_op_lookup(sconn, spath, flags=flags)
		 * 	get_fd(sconn)
		 * 	inode_or_name_op_on_error(sconn)
		 * 	compound_end
		 *
		 * [same_mds == 0, slookup == 0, dlookup == 0]
		 *
		 * 	ERR_CROSS_DEVICE_LINK
		 *
		 */

		slookup = sfd == GFARM_DESCRIPTOR_INVALID;
		dlookup = dfd == GFARM_DESCRIPTOR_INVALID;
		spath = snextpath;
		dpath = dnextpath;

		if (!slookup && !dlookup && !same_mds) {
			e = GFARM_ERR_CROSS_DEVICE_LINK;
			gflog_debug(GFARM_MSG_1002622,
			    "%s (%s, %s)",
			    gfarm_error_string(e),
			    spath, dpath);
			break;
		}
		if (sconn == NULL || (slookup && gfarm_is_url(spath))) {
			if (sconn) {
				gfm_client_connection_unlock(sconn);
				if (sctx != NULL) {
					gfm_client_context_free(sconn, sctx);
					sctx = NULL;
				}
				gfm_client_connection_free(sconn);
				sconn = NULL;
			}
			if ((e = gfarm_url_parse_metadb(&spath, &sconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002623,
				    "gfarm_url_parse_metadb(%s) failed: %s",
				    src, gfarm_error_string(e));
				break;
			}
			if (spath[0] == '\0')
				spath = "/";
		}
		if (dconn == NULL || (dlookup && gfarm_is_url(dpath))) {
			if (dconn) {
				gfm_client_connection_unlock(dconn);
				if (dctx != NULL) {
					gfm_client_context_free(dconn, dctx);
					dctx = NULL;
				}
				gfm_client_connection_free(dconn);
				dconn = NULL;
			}
			if ((e = gfarm_url_parse_metadb(&dpath, &dconn))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002624,
				    "gfarm_url_parse_metadb(%s) failed: %s",
				    dst, gfarm_error_string(e));
				break;
			}
			if (dpath[0] == '\0')
				dpath = "/";
		}
		if ((!is_open_last && GFARM_IS_PATH_ROOT(spath)) ||
		    GFARM_IS_PATH_ROOT(dpath)) {
			e = GFARM_ERR_PATH_IS_ROOT;
			gflog_debug(GFARM_MSG_1002625,
			    "inode_or_name_op_lookup_request : %s",
			    gfarm_error_string(e));
			break;
		}

		if ((long) sconn < (long) dconn) {
			gfm_client_connection_lock(sconn);
			gfm_client_connection_lock(dconn);
		} else {
			gfm_client_connection_lock(dconn);
			gfm_client_connection_lock(sconn);
		}

		same_mds = sconn == dconn;

		if ((slookup || same_mds)) {
			if (sctx != NULL) {
				gfm_client_context_free(sconn, sctx);
				sctx = NULL;
			}
			if ((e = gfm_client_context_alloc(sconn, &sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_UNFIXED,
				    "gfm_client_context_alloc: %s",
				    gfarm_error_string(e));
				break;
			}
			if ((e = gfm_client_compound_begin_request(
			    sconn, sctx)) != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1002626,
				    "compound_begin request: %s",
				    gfarm_error_string(e));
				break;
			}
		}
		if (dlookup && !same_mds) {
			if (dctx != NULL) {
				gfm_client_context_free(dconn, dctx);
				dctx = NULL;
			}
			if ((e = gfm_client_context_alloc(dconn, &dctx))
			     != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_UNFIXED,
				    "gfm_client_context_alloc: %s",
				    gfarm_error_string(e));
				*is_dst_errp = 1;
				break;
			}
			if ((e = gfm_client_compound_begin_request(
			    dconn, dctx)) != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1002627,
				    "compound_begin request: %s",
				    gfarm_error_string(e));
				*is_dst_errp = 1;
				break;
			}
		}
		if (slookup) {
			if ((e = gfm_inode_or_name_op_lookup_request(
			    sconn, sctx,
			    spath, flags, &srest, &s_do_verify))
			    != GFARM_ERR_NO_ERROR)
				break;
			if ((e = gfm_client_get_fd_request(
			    sconn, sctx)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002628,
				    "get_fd request: %s",
				    gfarm_error_string(e));
				break;
			}
		} else if (same_mds) {
			if ((e = gfm_client_put_fd_request(
			    sconn, sctx, sfd)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002629,
				    "put_fd(%d) request: %s", sfd,
				    gfarm_error_string(e));
				break;
			}
		}
		if (same_mds) {
			if ((e = gfm_client_save_fd_request(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002630,
				    "save_fd request: %s",
				    gfarm_error_string(e));
				break;
			}
		}
		if (dlookup) {
			if ((e = gfm_inode_or_name_op_lookup_request(
				dconn, same_mds ? sctx : dctx,
				dpath, 0, &drest, &d_do_verify))
			    != GFARM_ERR_NO_ERROR) {
				*is_dst_errp = !same_mds;
				break;
			}
			if ((e = gfm_client_get_fd_request(
			    dconn, same_mds ? sctx : dctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002631,
				    "get_fd request: %s",
				    gfarm_error_string(e));
				*is_dst_errp = !same_mds;
				break;
			}
		} else if (same_mds) {
			/* same_mds => use sctx instead of dctx */
			if ((e = gfm_client_put_fd_request(dconn, sctx, dfd))
			    != GFARM_ERR_NO_ERROR) {
				/* sconn == dconn */
				gflog_debug(GFARM_MSG_1002632,
				    "put_fd(%d) request: %s", dfd,
				    gfarm_error_string(e));
				break;
			}
		}
		if (same_mds) {
			if (is_open_last)
				e = (*inode_request_op)(sconn, sctx,
				    closure, drest);
			else
				e = (*name_request_op)(sconn, sctx,
				    closure, srest, drest);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002633,
				    "request_op failed: %s",
				    gfarm_error_string(e));
				break;
			}
			if ((e = gfm_client_close_request(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002634,
				    "close request: %s",
				    gfarm_error_string(e));
				break;
			}
			if ((e = gfm_client_restore_fd_request(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002635,
				    "restore request: %s",
				    gfarm_error_string(e));
				break;
			}
			if ((e = gfm_client_close_request(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002636,
				    "close request: %s",
				    gfarm_error_string(e));
				break;
			}
		}
		if (slookup || same_mds) {
			if ((e = gfm_inode_or_name_op_on_error_request(
			    sconn, sctx, &s_on_error_pos))
			    != GFARM_ERR_NO_ERROR) {
				break;
			}
			if ((e = gfm_inode_or_name_op_end_request(
			    sconn, sctx, &s_end_pos)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002637,
				    "compound_end request failed: %s",
				    gfarm_error_string(e));
				break;
			}
		}
		if (dlookup && !same_mds) {
			if ((e = gfm_inode_or_name_op_on_error_request(
			    dconn, dctx, &d_on_error_pos))
			    != GFARM_ERR_NO_ERROR) {
				*is_dst_errp = 1;
				break;
			}
			if ((e = gfm_inode_or_name_op_end_request(
			    dconn, dctx, &d_end_pos)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002638,
				    "compound_end request failed: %s",
				    gfarm_error_string(e));
				*is_dst_errp = 1;
				break;
			}
		}
		if ((slookup || same_mds) &&
		    (e = gfm_client_compound_begin_result(sconn, sctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1002639,
			    "compound_begin result: %s",
			    gfarm_error_string(e));
			break;
		}
		if (dlookup && !same_mds &&
		    (e = gfm_client_compound_begin_result(dconn, dctx))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1002640,
			    "compound_begin result: %s",
			    gfarm_error_string(e));
			*is_dst_errp = 1;
			break;
		}
		se = de = GFARM_ERR_NO_ERROR;
		sretry = dretry = 0;
		if (slookup) {
			if ((se = gfm_inode_or_name_op_lookup_result(
			    sconn, sctx,
			    spath, flags, s_do_verify, &srest, &type,
			    &retry_count, &s_is_last, &sretry, &ino))
			    != GFARM_ERR_NO_ERROR) {
				if (sretry)
					continue;
				if (se == GFARM_ERR_IS_A_SYMBOLIC_LINK) {
					if (sconn != dconn)
						goto dst_lookup_result;
					goto on_error_result;
				}
				e = se;
				break;
			}
			if ((e = gfm_client_get_fd_result(sconn, sctx,
			    &sfd)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002641,
				    "get_fd result: %s",
				    gfarm_error_string(e));
				break;
			}
		} else if (same_mds) {
			if ((e = gfm_client_put_fd_result(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002642,
				    "put_fd(%d) result: %s", sfd,
				    gfarm_error_string(e));
				break;
			}
		}
		if (same_mds) {
			if ((e = gfm_client_save_fd_result(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002643,
				    "save_fd result: %s",
				    gfarm_error_string(e));
				break;
			}
		}

dst_lookup_result:
		if (dlookup) {
			if ((de = gfm_inode_or_name_op_lookup_result(
			    dconn, same_mds ? sctx : dctx,
			    dpath, 0, d_do_verify, &drest, &type,
			    &retry_count, &d_is_last, &dretry, &ino))
			    != GFARM_ERR_NO_ERROR) {
				if (dretry)
					continue;
				if (de == GFARM_ERR_IS_A_SYMBOLIC_LINK)
					goto on_error_result;
				e = de;
				*is_dst_errp = !same_mds;
				break;
			}
			if ((e = gfm_client_get_fd_result(dconn,
			    same_mds ? sctx : dctx, &dfd))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002644,
				    "get_fd result: %s",
				    gfarm_error_string(e));
				*is_dst_errp = !same_mds;
				break;
			}
		} else if (same_mds) {
			/* same_mds => use sctx instead of dctx */
			if ((e = gfm_client_put_fd_result(dconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002645,
				    "put_fd(%d) result: %s", dfd,
				    gfarm_error_string(e));
				/* sconn == dconn */
				break;
			}
		}
		if (same_mds) {
			if ((e = (*result_op)(sconn, sctx, closure))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002646,
				    "result_op failed: %s",
				    gfarm_error_string(e));
				break;
			}
			op_called = 1;
			if ((e = gfm_client_close_result(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002647,
				    "close result: %s",
				    gfarm_error_string(e));
				break;
			}
			dfd = GFARM_DESCRIPTOR_INVALID;
			if ((e = gfm_client_restore_fd_result(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002648,
				    "restore_fd result: %s",
				    gfarm_error_string(e));
				break;
			}
			if ((e = gfm_client_close_result(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002649,
				    "close result: %s",
				    gfarm_error_string(e));
				break;
			}
			sfd = GFARM_DESCRIPTOR_INVALID;
		}

on_error_result:
		if (slookup) {
			gfm_client_context_free_until(sconn, sctx,
			    s_on_error_pos);
			if ((e3 = gfm_client_compound_on_error_result(
			    sconn, sctx)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "compound_on_error result failed: %s",
				    gfarm_error_string(e3));
				e = e3;
				break;
			}
			if (se == GFARM_ERR_IS_A_SYMBOLIC_LINK &&
			    (e = gfm_inode_or_name_op_on_error_result(
			    sconn, sctx,
			    s_is_last, srest, &snextpath, &sretry))
			    != GFARM_ERR_NO_ERROR) {
				break;
			}
		}
		if (dlookup) {
			if (!same_mds || !slookup) {
				gfm_client_context_free_until(dconn,
				    same_mds ? sctx : dctx,
				    same_mds ? s_on_error_pos:d_on_error_pos);
				if ((e3 = gfm_client_compound_on_error_result(
				    dconn, same_mds ? sctx : dctx))
				    != GFARM_ERR_NO_ERROR) {
					gflog_debug(GFARM_MSG_UNFIXED,
					    "compound_on_error result failed: "
					    "%s", gfarm_error_string(e3));
					e = e3;
					*is_dst_errp = !same_mds;
					break;
				}
			}
			if (de == GFARM_ERR_IS_A_SYMBOLIC_LINK &&
			    (e = gfm_inode_or_name_op_on_error_result(
			    dconn, same_mds ? sctx : dctx,
			    d_is_last, drest, &dnextpath, &dretry))
			    != GFARM_ERR_NO_ERROR) {
				*is_dst_errp = !same_mds;
				break;
			}
		}
		if ((same_mds && se == GFARM_ERR_NO_ERROR &&
		     de == GFARM_ERR_NO_ERROR) ||
		    (!same_mds && slookup && se == GFARM_ERR_NO_ERROR)) {
			gfm_client_context_free_until(sconn, sctx, s_end_pos);
			if ((e = gfm_client_compound_end_result(sconn, sctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002650,
				    "compound_end result failed: %s",
				    gfarm_error_string(e));
				if (sconn == dconn && cleanup_op)
					(*cleanup_op)(sconn, closure);
				*is_dst_errp = !same_mds;
				break;
			}
		}
		if (dlookup && !same_mds &&
		    de == GFARM_ERR_NO_ERROR) {
			gfm_client_context_free_until(dconn, dctx, d_end_pos);
			if ((e = gfm_client_compound_end_result(dconn, dctx))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002651,
				    "compound_end result failed: %s",
				    gfarm_error_string(e));
				*is_dst_errp = 1;
				break;
			}
		}
		if (op_called) {
			is_success = 1;
			break;
		}
		if ((sretry && ++snlinks > GFARM_SYMLINK_LEVEL_MAX) ||
		    (dretry && ++dnlinks > GFARM_SYMLINK_LEVEL_MAX)) {
			e = GFARM_ERR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINK;
			gflog_debug(GFARM_MSG_1002652,
			    "maybe loop: %s",
			    gfarm_error_string(e));
			break;
		}
	}
	if (sctx != NULL) {
		gfm_client_context_free(sconn, sctx);
		sctx = NULL;
	}
	if (dctx != NULL) {
		gfm_client_context_free(dconn, dctx);
		dctx = NULL;
	}

	if (snextpath)
		free(snextpath);
	if (dnextpath)
		free(dnextpath);
	if (sconn) {
		gfm_client_connection_unlock(sconn);
		*sconnp = sconn;
	}
	if (dconn) {
		gfm_client_connection_unlock(dconn);
	}
	if (dconn && !same_mds)
		*dconnp = dconn;

	if (is_success)
		return (*success_op)(sconn, closure);

	if (same_mds) {
		/* ignore result */
		if (sconn != NULL &&
		    (e2 = close_fd2(sconn, sfd, dfd)) != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_UNFIXED,
			    "close_fd2: %s",
			    gfarm_error_string(e2));
	} else {
		/* ignore result */
		if (sconn != NULL &&
		    (e2 = close_fd2(sconn, sfd, GFARM_DESCRIPTOR_INVALID)) !=
		    GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_UNFIXED,
			    "close_fd2: %s",
			    gfarm_error_string(e2));
		if (dconn != NULL &&
		    (e2 = close_fd2(dconn, dfd, GFARM_DESCRIPTOR_INVALID)) !=
		    GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_UNFIXED,
			    "close_fd2: %s",
			    gfarm_error_string(e2));
	}

	/*
	 * NOTE: the opened descriptor unexternalized is automatically closed
	 * by gfmd
	 */

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002653,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}
	return (e);
}

struct name2_op_info {
	const char *src, *dst;
	int flags;
	struct gfm_connection *sconn, *dconn;
	gfm_name2_inode_request_op_t inode_request_op;
	gfm_name2_request_op_t name_request_op;
	gfm_result_op_t result_op;
	gfm_name2_success_op_t success_op;
	gfm_cleanup_op_t cleanup_op;
	gfm_must_be_warned_op_t must_be_warned_op;
	void *closure;
};

static gfarm_error_t
name2_op_rpc(struct gfm_connection **gfm_serverp, void *closure)
{
	gfarm_error_t e;
	struct name2_op_info *ni = closure;
	struct gfm_connection *sconn = NULL, *dconn = NULL;
	int is_dst_err = 0;

	e = gfm_name2_op0(ni->src, ni->dst, ni->flags, ni->inode_request_op,
	    ni->name_request_op, ni->result_op, ni->success_op,
	    ni->cleanup_op, ni->closure, &sconn, &dconn, &is_dst_err);
	*gfm_serverp = is_dst_err && (sconn != dconn) ? dconn : sconn;
	ni->sconn = sconn;
	ni->dconn = dconn;

	return (e);
}

static gfarm_error_t
name2_op_post_failover(struct gfm_connection *gfm_serverp, void *closure)
{
	struct name2_op_info *ni = closure;

	if (ni->sconn) {
		gfm_client_connection_free(ni->sconn);
		ni->sconn = NULL;
	}
	if (ni->dconn && ni->sconn != ni->dconn) {
		gfm_client_connection_free(ni->dconn);
		ni->dconn = NULL;
	}

	return (GFARM_ERR_NO_ERROR);
}

static void
name2_op_exit(struct gfm_connection *gfm_serverp, gfarm_error_t e,
	void *closure)
{
	if (e != GFARM_ERR_NO_ERROR) {
		/* do not release connection when no error occurred. */
		(void)name2_op_post_failover(gfm_serverp, closure);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_name2_op: %s", gfarm_error_string(e));
	}
}

static int
name2_must_be_warned_op(gfarm_error_t e, void *closure)
{
	struct name2_op_info *ni = closure;

	return (ni->must_be_warned_op ? ni->must_be_warned_op(e, ni->closure) :
	    0);
}

static gfarm_error_t
gfm_name2_op(const char *src, const char *dst, int flags,
	gfm_name2_inode_request_op_t inode_request_op,
	gfm_name2_request_op_t name_request_op,
	gfm_result_op_t result_op, gfm_name2_success_op_t success_op,
	gfm_cleanup_op_t cleanup_op, gfm_must_be_warned_op_t must_be_warned_op,
	void *closure)
{
	struct name2_op_info ni = {
		src, dst, flags, NULL, NULL,
		inode_request_op, name_request_op, result_op,
		success_op, cleanup_op, must_be_warned_op, closure,
	};

	return (gfm_client_rpc_with_failover(name2_op_rpc,
	    name2_op_post_failover, name2_op_exit, name2_must_be_warned_op,
	    &ni));
}

gfarm_error_t
gfm_name2_op_modifiable(const char *src, const char *dst, int flags,
	gfm_name2_inode_request_op_t inode_request_op,
	gfm_name2_request_op_t name_request_op,
	gfm_result_op_t result_op, gfm_name2_success_op_t success_op,
	gfm_cleanup_op_t cleanup_op, gfm_must_be_warned_op_t must_be_warned_op,
	void *closure)
{
	return (gfm_name2_op(src, dst, flags, inode_request_op,
	    name_request_op, result_op, success_op, cleanup_op,
	    must_be_warned_op, closure));
}
