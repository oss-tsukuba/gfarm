#include <assert.h>
#include <stdio.h>	/* config.h needs FILE */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#include <openssl/evp.h>

#define GFARM_INTERNAL_USE /* GFARM_FILE_LOOKUP */
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_proto.h"
#include "gfm_client.h"
#include "lookup.h"
#define GFARM_USE_GFS_PIO_INTERNAL_CKSUM_INFO
#include "gfs_io.h"

static gfarm_error_t
gfm_open_flag_check(int flag)
{
	if (flag & ~GFARM_FILE_USER_OPEN_FLAGS)
		return (GFARM_ERR_INVALID_ARGUMENT);
	if ((flag & GFARM_FILE_ACCMODE) == GFARM_FILE_LOOKUP)
		return (GFARM_ERR_INVALID_ARGUMENT);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * gfm_create_fd()
 */

struct gfm_create_fd_closure {
	/* input */
	int flags;
	gfarm_mode_t mode_to_create;

	/* work */
	gfarm_mode_t mode_created;
	int fd;

	/* output */
	struct gfm_connection **gfm_serverp;
	int *fdp;
	int *typep;
	gfarm_ino_t *inump;
	gfarm_uint64_t *igenp; /* currentyly only for gfarm_file_trace */
	char **urlp;
	struct gfs_pio_internal_cksum_info *cksum_info; /* may be NULL */
};

struct gfm_close_fd_closure {
	gfarm_uint64_t *igenp;
	struct gfs_pio_internal_cksum_info *cksum_info;
};

static gfarm_error_t
gfm_create_fd_request(struct gfm_connection *gfm_server, void *closure,
	const char *base)
{
	struct gfm_create_fd_closure *c = closure;
	gfarm_error_t e;

	if ((e = gfm_client_create_request(gfm_server, base,
	    c->flags, c->mode_to_create)) != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000080, "create(%s) request: %s",
		    base, gfarm_error_string(e));
	} else if (c->cksum_info != NULL &&
	    (e = gfm_client_fstat_request(gfm_server)) != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003920, "create(%s) request: %s",
		    base, gfarm_error_string(e));
	} else if (c->cksum_info != NULL &&
	    (e = gfm_client_cksum_get_request(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003921, "create(%s) request: %s",
		    base, gfarm_error_string(e));
	} else if ((e = gfm_client_get_fd_request(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000081, "get_fd(%s) request: %s",
		    base, gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_create_fd_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_create_fd_closure *c = closure;
	gfarm_error_t e;
	struct gfs_pio_internal_cksum_info *ci = c->cksum_info;
	struct gfs_stat st;

	if (ci != NULL) {
		ci->cksum_type = NULL;
		ci->cksum_set_flags = 0;
		memset(&st, 0, sizeof(st));
	}

	if ((e = gfm_client_create_result(gfm_server,
	    c->inump, c->igenp, &c->mode_created))
	    != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
		gflog_debug(GFARM_MSG_1000082,
		    "create() result: %s", gfarm_error_string(e));
#endif
	} else if (ci != NULL &&
	    (e = gfm_client_fstat_result(gfm_server, &st))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003922,
		    "create() result: %s", gfarm_error_string(e));
	} else if (ci != NULL &&
	    (e = gfm_client_cksum_get_result(gfm_server,
	    &ci->cksum_type, sizeof(ci->cksum), &ci->cksum_len, ci->cksum,
	    &ci->cksum_get_flags)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003923,
		    "create() result: %s", gfarm_error_string(e));
	} else if ((e = gfm_client_get_fd_result(gfm_server, &c->fd))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000083,
		    "get_fd() result: %s", gfarm_error_string(e));
	}

	if (ci != NULL) {
		if (e == GFARM_ERR_NO_ERROR) {
			ci->filesize = st.st_size;
		} else {
			free(ci->cksum_type);
			ci->cksum_type = NULL;
		}
		gfs_stat_free(&st);
	}

	return (e);
}

static gfarm_error_t
gfm_create_fd_success(struct gfm_connection *gfm_server, void *closure,
	int type, const char *url, gfarm_ino_t ino, gfarm_uint64_t gen)
{
	struct gfm_create_fd_closure *c = closure;

	*c->gfm_serverp = gfm_server;
	*c->fdp = c->fd;
	if (c->typep != NULL)
		*c->typep = gfs_mode_to_type(c->mode_created);
	if (c->urlp) {
		if ((*c->urlp = strdup(url)) == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	return (GFARM_ERR_NO_ERROR);
}

/* cksum_info may be NULL */
gfarm_error_t
gfm_create_fd(const char *path, int flags, gfarm_mode_t mode,
	struct gfm_connection **gfm_serverp, int *fdp, int *typep,
	gfarm_ino_t *inump, gfarm_uint64_t *igenp, char **urlp,
	struct gfs_pio_internal_cksum_info *cksum_info)
{
	gfarm_error_t e;
	struct gfm_create_fd_closure closure;

	if ((e = gfm_open_flag_check(flags)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001268,
			"gfm_open_flag_check(0x%x) failed: %s",
			flags,
			gfarm_error_string(e));
		return (e);
	}

	closure.flags = flags & GFARM_FILE_USER_MODE;
	closure.mode_to_create = mode;
	closure.gfm_serverp = gfm_serverp;
	closure.fdp = fdp;
	closure.typep = typep;
	closure.inump = inump;
	closure.igenp = igenp;
	closure.urlp = urlp;
	closure.cksum_info = cksum_info;

	return (gfm_name_op_modifiable(path, GFARM_ERR_IS_A_DIRECTORY,
	    gfm_create_fd_request,
	    gfm_create_fd_result,
	    gfm_create_fd_success,
	    NULL, &closure));
}

/*
 * gfm_open_fd()
 */

struct gfm_open_fd_closure {
	int fd;
	struct gfm_connection **gfm_serverp;
	int *fdp;
	int *typep;
	gfarm_ino_t *inump;
	gfarm_uint64_t *igenp;
	char **urlp;
	struct gfs_pio_internal_cksum_info *cksum_info; /* may be NULL */
};

static gfarm_error_t
gfm_open_fd_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_open_fd_closure *c = closure;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (c->cksum_info != NULL &&
	    (e = gfm_client_fstat_request(gfm_server)) != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003924, "fstat() request: %s",
		    gfarm_error_string(e));
	} else if (c->cksum_info != NULL &&
	    (e = gfm_client_cksum_get_request(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003925, "cksum_get() request: %s",
		    gfarm_error_string(e));
	} else if ((e = gfm_client_get_fd_request(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000084,
		    "get_fd request; %s", gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_open_fd_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_open_fd_closure *c = closure;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct gfs_pio_internal_cksum_info *ci = c->cksum_info;
	struct gfs_stat st;

	if (ci != NULL) {
		ci->cksum_type = NULL;
		ci->cksum_set_flags = 0;
		memset(&st, 0, sizeof(st));
	}

	if (ci != NULL &&
	    (e = gfm_client_fstat_result(gfm_server, &st))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003926,
		    "create() result: %s", gfarm_error_string(e));
	} else if (ci != NULL &&
	    (e = gfm_client_cksum_get_result(gfm_server,
	    &ci->cksum_type, sizeof(ci->cksum), &ci->cksum_len, ci->cksum,
	    &ci->cksum_get_flags)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003927,
		    "create() result: %s", gfarm_error_string(e));
	} else if ((e = gfm_client_get_fd_result(gfm_server, &c->fd))
	    != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
		gflog_debug(GFARM_MSG_1000085,
		    "get_fd result; %s", gfarm_error_string(e));
#endif
	}

	if (ci != NULL) {
		if (e == GFARM_ERR_NO_ERROR) {
			ci->filesize = st.st_size;
		} else {
			free(ci->cksum_type);
			ci->cksum_type = NULL;
		}
		gfs_stat_free(&st);
	}

	return (e);
}

static gfarm_error_t
gfm_open_fd_success(struct gfm_connection *gfm_server, void *closure, int type,
	const char *path, gfarm_ino_t ino, gfarm_uint64_t igen)
{
	struct gfm_open_fd_closure *c = closure;

	*c->gfm_serverp = gfm_server;
	*c->fdp = c->fd;
	if (c->typep != NULL)
		*c->typep = type;
	if (c->inump)
		*c->inump = ino;
	if (c->igenp)
		*c->igenp = igen;
	if (c->urlp)
		*c->urlp = strdup(path);
	return (GFARM_ERR_NO_ERROR);
}

/* cksum_info may be NULL */
gfarm_error_t
gfm_open_fd(const char *path, int flags,
	struct gfm_connection **gfm_serverp, int *fdp, int *typep,
	char **urlp, gfarm_ino_t *inump, gfarm_uint64_t *igenp,
	struct gfs_pio_internal_cksum_info *cksum_info)
{
	gfarm_error_t e;
	struct gfm_open_fd_closure closure;

	/* GFARM_FILE_EXCLUSIVE is ignored by gfmd again. */
	flags &= ~GFARM_FILE_EXCLUSIVE;

	if ((e = gfm_open_flag_check(flags)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001269,
			"gfm_open_flag_check(0x%x) failed: %s",
			flags,
			gfarm_error_string(e));
		return (e);
	}

	closure.gfm_serverp = gfm_serverp;
	closure.fdp = fdp;
	closure.typep = typep;
	closure.inump = inump;
	closure.igenp = igenp;
	closure.urlp = urlp;
	closure.cksum_info = cksum_info;
	return (gfm_inode_op_modifiable(path, flags & GFARM_FILE_USER_MODE,
	    gfm_open_fd_request,
	    gfm_open_fd_result,
	    gfm_open_fd_success,
	    NULL, NULL,
	    &closure));
}

/*
 * gfm_fhopen_fd()
 */

gfarm_error_t
gfm_fhopen_fd(gfarm_ino_t inum, gfarm_uint64_t gen, int flags,
	struct gfm_connection **gfm_serverp, int *fdp, int *typep,
	struct gfs_pio_internal_cksum_info *ci)
{
	struct gfm_connection *gfm_server;
	gfarm_error_t e;
	gfarm_mode_t mode;
	int fd;
	struct gfs_stat st;

	if (ci != NULL) {
		ci->cksum_type = NULL;
		ci->cksum_set_flags = 0;
		memset(&st, 0, sizeof(st));
	}

	if ((e = gfm_client_connection_and_process_acquire_by_path(
		     GFARM_PATH_ROOT, &gfm_server)) != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003726, "process_acquire: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if ((e = gfm_client_compound_begin_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003727, "compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_fhopen_request(gfm_server, inum, gen, flags))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003728, "fhopen request: %s",
		    gfarm_error_string(e));
	else if (ci != NULL &&
	    (e = gfm_client_fstat_request(gfm_server)) != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003928, "fstat request: %s",
		    gfarm_error_string(e));
	else if (ci != NULL &&
	    (e = gfm_client_cksum_get_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003929, "cksum_get request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_get_fd_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003729, "get_fd request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003730, "compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003731, "compound_begin result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_fhopen_result(gfm_server, &mode))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003732, "fhopen result: %s",
		    gfarm_error_string(e));
	else if (ci != NULL &&
	    (e = gfm_client_fstat_result(gfm_server, &st))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003930, "fstat result: %s",
		    gfarm_error_string(e));
	else if (ci != NULL &&
	    (e = gfm_client_cksum_get_result(gfm_server,
	    &ci->cksum_type, sizeof(ci->cksum), &ci->cksum_len, ci->cksum,
	    &ci->cksum_get_flags)) != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003931, "cksum_get result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_get_fd_result(gfm_server, &fd))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003733, "get_fd result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003734, "compound_end result: %s",
		    gfarm_error_string(e));
	}

	if (ci != NULL) {
		if (e == GFARM_ERR_NO_ERROR) {
			ci->filesize = st.st_size;
		} else {
			free(ci->cksum_type);
			ci->cksum_type = NULL;
		}
		gfs_stat_free(&st);
	}

	if (e == GFARM_ERR_NO_ERROR) {
		*fdp = fd;
		*typep = gfs_mode_to_type(mode);
		*gfm_serverp = gfm_server;
	} else
		gfm_client_connection_free(gfm_server);

	return (e);
}

/*
 * gfm_close_fd()
 */

static gfarm_error_t
gfm_close_request(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct gfm_close_fd_closure	*clp = closure;
	struct gfs_pio_internal_cksum_info *ci = clp ? clp->cksum_info : NULL;
	gfarm_uint64_t *igenp = clp ? clp->igenp : NULL;

	if (ci != NULL &&
	    (e = gfm_client_cksum_set_request(gfm_server, ci->cksum_type,
	    ci->cksum_len, ci->cksum, ci->cksum_set_flags, 0, 0))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003932, "cksum_set() request: %s",
		    gfarm_error_string(e));
	} else if (!igenp && (e = gfm_client_close_request(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000086,
		    "close request: %s", gfarm_error_string(e));
	} else if (igenp && (e = gfm_client_close_getgen_request(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1004607,
		    "close request: %s", gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_close_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct gfm_close_fd_closure	*clp = closure;
	struct gfs_pio_internal_cksum_info *ci = clp ? clp->cksum_info : NULL;
	gfarm_uint64_t *igenp = clp ? clp->igenp : NULL;

	if (ci != NULL &&
	    (e = gfm_client_cksum_set_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
		gflog_debug(GFARM_MSG_1003933,
		    "cksum_set() result: %s", gfarm_error_string(e));
#endif
	} else if (!igenp && (e = gfm_client_close_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
#if 1 /* DEBUG */
		if (e != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1000087,
			    "close result: %s", gfarm_error_string(e));
#endif
	} else if (igenp && (e = gfm_client_close_getgen_result(gfm_server,
		igenp)) != GFARM_ERR_NO_ERROR) {
		if (e != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1004608,
			    "close result: %s", gfarm_error_string(e));
	}
	return (e);
}

/*
 * cksum_info may be NULL
 *
 * NOTE:
 * gfm_server is not freed by this function.
 * callers of this function should free it.
 */
gfarm_error_t
gfm_close_fd(struct gfm_connection *gfm_server, int fd,
	gfarm_uint64_t *igenp, struct gfs_pio_internal_cksum_info *cksum_info)
{
	gfarm_error_t e;
	struct gfm_close_fd_closure close_info;

	close_info.igenp = igenp;
	close_info.cksum_info = cksum_info;
	if ((e = gfm_client_compound_fd_op(gfm_server, fd,
	    gfm_close_request, gfm_close_result, NULL, &close_info))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003934,
		    "gfm_close_fd fd=%d: %s", fd, gfarm_error_string(e));
	return (e);
}
