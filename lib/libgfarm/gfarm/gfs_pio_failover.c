/*
 * $Id$
 */

#include <string.h>
#include <stdlib.h>	/* for free */

#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <openssl/evp.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "queue.h"

#include "config.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "gfs_client.h"
#include "gfs_io.h"
#include "gfs_pio.h"
#include "filesystem.h"
#include "gfs_failover.h"
#include "gfs_file_list.h"
#include "gfs_misc.h"


static struct gfs_connection *
get_storage_context(struct gfs_file_section_context *vc)
{
	if (vc == NULL)
		return (NULL);
	return (vc->storage_context);
}

int
gfm_client_connection_should_failover(struct gfm_connection *gfm_server,
	gfarm_error_t e)
{
#ifndef __KERNEL__	/* failover */
	struct gfarm_filesystem *fs;

	if (gfm_server == NULL || !gfm_client_is_connection_error(e))
		return (0);
	fs = gfarm_filesystem_get_by_connection(gfm_server);
	return (!gfarm_filesystem_in_failover_process(fs));
#else /* __KERNEL__ */
	return (0);
#endif /* __KERNEL__ */
}
int
gfs_pio_should_failover(GFS_File gf, gfarm_error_t e)
{
#ifndef __KERNEL__	/* failover */
	struct gfarm_filesystem *fs;

	if (gf == NULL)
		return (0);
	fs = gfarm_filesystem_get_by_connection(gfs_pio_metadb(gf));
	if (gfarm_filesystem_in_failover_process(fs))
		return (0);
	return (gfarm_filesystem_failover_detected(fs) ||
	    e == GFARM_ERR_GFMD_FAILED_OVER);
#else /* __KERNEL__ */
	return (0);
#endif /* __KERNEL__ */
}

static gfarm_error_t
gfs_pio_reopen(struct gfarm_filesystem *fs, GFS_File gf)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int fd, type;
	gfarm_ino_t ino;
	char *real_url = NULL;

	/* avoid failover in gfm_open_fd_with_ino() */
	gfarm_filesystem_set_failover_detected(fs, 0);

	/* increment ref count of gfm_server */
	if ((e = gfm_open_fd_with_ino(gf->url,
	    gf->open_flags & (~GFARM_FILE_TRUNC),
	    &gfm_server, &fd, &type, &real_url, &ino)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003380,
		    "reopen operation on file descriptor for URL (%s) "
		    "failed: %s",
		    gf->url,
		    gfarm_error_string(e));
		free(real_url);
		return (e);
	} else if (gfm_server != gf->gfm_server ||
	    type != GFS_DT_REG || ino != gf->ino) {
		e = GFARM_ERR_STALE_FILE_HANDLE;
	} else {
		gf->fd = fd;
		/* storage_context is null in scheduling */
		if (get_storage_context(gf->view_context) != NULL)
			e = (*gf->ops->view_reopen)(gf);
	}

	if (e == GFARM_ERR_NO_ERROR) {
		if (real_url != NULL) {
			free(gf->url);
			gf->url = real_url;
		}
	} else {
		if (real_url != NULL) {
			free(real_url);
		}
		(void)gfm_close_fd(gfm_server, fd); /* ignore result */
		gf->fd = GFARM_DESCRIPTOR_INVALID;
		gf->error = e;
		gflog_debug(GFARM_MSG_1003381,
		    "reopen operation on pio for URL (%s) failed: %s",
		    gf->url,
		    gfarm_error_string(e));
	}
	/* decrement ref count of gfm_server. then we'll use gf->gfm_server */
	gfm_client_connection_free(gfm_server);

	return (e);
}

struct reset_and_reopen_info {
	struct gfm_connection *gfm_server;
	int must_retry;
};

static int
reset_and_reopen(GFS_File gf, void *closure)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct reset_and_reopen_info *ri = closure;
	struct gfm_connection *gfm_server = ri->gfm_server;
	struct gfm_connection *gfm_server1;
	struct gfs_connection *sc;
	struct gfarm_filesystem *fs =
	    gfarm_filesystem_get_by_connection(gfm_server);
	int fc = gfarm_filesystem_failover_count(fs);

	if ((e = gfm_client_connection_acquire(gfm_client_hostname(gfm_server),
	    gfm_client_port(gfm_server), gfm_client_username(gfm_server),
	    &gfm_server1)) != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1003383,
		    "gfm_client_connection_acquire: %s",
		    gfarm_error_string(e));
		return (1);
	}

	if (gfm_server != gfm_server1) {
		gfm_client_connection_free(gfm_server1);
		gflog_debug(GFARM_MSG_1003384,
		    "reconnected to other gfmd or gfmd restarted");
		ri->must_retry = 1;
		return (0);
	}

	/* if old gfm_connection is alive, fd must be closed */
	(void)gfm_close_fd(gf->gfm_server, gf->fd);
	gf->fd = GFARM_DESCRIPTOR_INVALID;
	gfm_client_connection_free(gf->gfm_server);
	/* ref count of gfm_server is incremented above */
	gf->gfm_server = gfm_server;

	if ((sc = get_storage_context(gf->view_context)) != NULL) {
		/*
		 * pid will be 0 if gfarm_client_process_reset() resulted
		 * in failure at reset_and_reopen() previously called with
		 * the same gfs_connection.
		 */
		if (gfs_client_pid(sc) == 0) {
			gf->error = GFARM_ERR_CONNECTION_ABORTED;
			gflog_debug(GFARM_MSG_1003385,
			    "%s", gfarm_error_string(gf->error));
			return (1);
		}

		/* reset pid */
		if (fc > gfs_client_connection_failover_count(sc)) {
			gfs_client_connection_set_failover_count(sc, fc);
			/*
			 * gfs_file just in scheduling is not related to
			 * gfs_server.
			 * In that case, gfarm_client_process_reset() is
			 * called in gfs_pio_open_section().
			 *
			 * all fd will be closed in gfsd by
			 * gfarm_client_process_reset().
			 */
			e = gfarm_client_process_reset(sc, gfm_server);
			if (e != GFARM_ERR_NO_ERROR) {
				gf->error = e;
				gflog_debug(GFARM_MSG_1003386,
				    "gfarm_client_process_reset: %s",
				    gfarm_error_string(e));
				return (1);
			}
		}
	}

	/* reopen file */
	if (gfs_pio_error(gf) != GFARM_ERR_STALE_FILE_HANDLE &&
	    (e = gfs_pio_reopen(fs, gf)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003387,
		    "gfs_pio_reopen: %s", gfarm_error_string(e));
	}

	return (1);
}

static int
reset_and_reopen_all(struct gfm_connection *gfm_server,
	struct gfs_file_list *gfl)
{
	struct reset_and_reopen_info ri;

	ri.gfm_server = gfm_server;
	ri.must_retry = 0;

	gfs_pio_file_list_foreach(gfl, reset_and_reopen, &ri);

	return (ri.must_retry == 0);
}

static int
set_error(GFS_File gf, void *closure)
{
	gf->error = *(gfarm_error_t *)closure;
	return (1);
}

#define NUM_FAILOVER_RETRY 3

static gfarm_error_t
acquire_valid_connection(struct gfm_connection *gfm_server, const char *host,
	int port, const char *user, struct gfarm_filesystem *fs)
{
	gfarm_error_t e;
	int fc = gfarm_filesystem_failover_count(fs);
	int acquired = 0;

	while (fc > gfm_client_connection_failover_count(gfm_server)) {
		gfm_client_purge_from_cache(gfm_server);
		if (acquired)
			gfm_client_connection_free(gfm_server);

		if ((e = gfm_client_connection_and_process_acquire(
		    host, port, user, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "gfm_client_connection_and_process_acquire: %s",
			    gfarm_error_string(e));
			return (e);
		}
		acquired = 1;
	}
	if (acquired)
		gfm_client_connection_free(gfm_server);
	gfarm_filesystem_set_failover_detected(fs, 0);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
failover0(struct gfm_connection *gfm_server, const char *host0, int port,
	const char *user0)
{
	gfarm_error_t e;
	struct gfarm_filesystem *fs;
	struct gfs_file_list *gfl = NULL;
	char *host = NULL, *user = NULL;
	int fc, i, ok = 0;

	if (gfm_server) {
		fs = gfarm_filesystem_get_by_connection(gfm_server);
		if (gfarm_filesystem_in_failover_process(fs)) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "gfmd connection failover process called "
			    "recursively");
			goto error_all;
		}
		gfarm_filesystem_set_in_failover_process(fs, 1);
		fc = gfarm_filesystem_failover_count(fs);
		if ((host = strdup(gfm_client_hostname(gfm_server))) ==  NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1003388,
			    "%s", gfarm_error_string(e));
			goto error_all;
		}
		if ((user = strdup(gfm_client_username(gfm_server))) ==  NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1003389,
			    "%s", gfarm_error_string(e));
			goto error_all;
		}
		port = gfm_client_port(gfm_server);

		if (fc > gfm_client_connection_failover_count(gfm_server)) {
			/* already failover occurred */
			e = acquire_valid_connection(gfm_server, host, port,
			    user, fs);
			goto end;
		}
	} else {
		fs = gfarm_filesystem_get(host0, port);
		assert(fs != NULL);
		if (gfarm_filesystem_in_failover_process(fs)) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "gfmd connection failover process called "
			    "recursively");
			goto error_all;
		}
		gfarm_filesystem_set_in_failover_process(fs, 1);
		fc = gfarm_filesystem_failover_count(fs);
		if ((host = strdup(host0)) ==  NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s", gfarm_error_string(e));
			goto error_all;
		}
		if ((user = strdup(user0)) ==  NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s", gfarm_error_string(e));
			goto error_all;
		}
	}
	gfl = gfarm_filesystem_opened_file_list(fs);

	/*
	 * failover_count must be incremented before acquire connection
	 * because failover_count is set at creating new connection
	 * and we use failover_count to indicate whether or not the acquired
	 * connection is new connection.
	 */
	gfarm_filesystem_set_failover_count(fs, fc + 1);

	for (i = 0; i < NUM_FAILOVER_RETRY; ++i) {
		/* reconnect to gfmd */
		if ((e = gfm_client_connection_and_process_acquire(
		    host, port, user, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003390,
			    "gfm_client_connection_acquire failed : %s",
			    gfarm_error_string(e));
			continue;
		}
		if (gfm_client_connection_failover_count(gfm_server)
		    != fc + 1) {
			/*
			 * When this function is called from
			 * gfm_client_connection_failover_pre_connect(),
			 * the acquired connection is possibly an old
			 * connection.
			 */
			gfm_client_purge_from_cache(gfm_server);
			gfm_client_connection_free(gfm_server);
			--i;
			continue;
		}

		/*
		 * close fd, release gfm_connection and set invalid fd,
		 * reset processes and reopen files
		*/
		ok = reset_and_reopen_all(gfm_server, gfl);
		gfm_client_connection_free(gfm_server);
		if (ok)
			break;
	}

	if (ok) {
		gfarm_filesystem_set_failover_detected(fs, 0);
		e = GFARM_ERR_NO_ERROR;
		gflog_notice(GFARM_MSG_1003391,
		    "connection to metadb server was failed over successfully");
	} else {
		gfarm_filesystem_set_failover_detected(fs, 1);
		e = GFARM_ERR_OPERATION_TIMED_OUT;
		gflog_debug(GFARM_MSG_1003392,
		    "falied to fail over: %s", gfarm_error_string(e));
	}
end:
	free(host);
	free(user);
	gfarm_filesystem_set_in_failover_process(fs, 0);

	return (e);

error_all:

	free(host);
	free(user);

	if (gfl != NULL)
		gfs_pio_file_list_foreach(gfl, set_error, &e);
	gfarm_filesystem_set_in_failover_process(fs, 0);

	return (e);
}

static gfarm_error_t
failover(struct gfm_connection *gfm_server)
{
	return (failover0(gfm_server, NULL, 0, NULL));
}

gfarm_error_t
gfm_client_connection_failover_pre_connect(const char *host, int port,
	const char *user)
{
	return (failover0(NULL, host, port, user));
}

gfarm_error_t
gfm_client_connection_failover(struct gfm_connection *gfm_server)
{
	return (failover(gfm_server));
}

gfarm_error_t
gfs_pio_failover(GFS_File gf)
{
	gfarm_error_t e = failover(gf->gfm_server);

	if (e != GFARM_ERR_NO_ERROR)
		gf->error = e;
	return (e);
}

gfarm_error_t
gfm_client_rpc_with_failover(
	gfarm_error_t (*rpc_op)(struct gfm_connection **, void *),
	gfarm_error_t (*post_failover_op)(struct gfm_connection *, void *),
	void (*exit_op)(struct gfm_connection *, gfarm_error_t, void *),
	int (*must_be_warned_op)(gfarm_error_t, void *),
	void *closure)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int nretry = 1, post_nretry = 1;

retry:
	gfm_server = NULL;
	e = rpc_op(&gfm_server, closure);
	if (nretry > 0 && gfm_client_connection_should_failover(
	    gfm_server, e)) {
		if ((e = failover(gfm_server)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "failover: %s", gfarm_error_string(e));
		} else if (post_failover_op &&
		    (e = post_failover_op(gfm_server, closure))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "post_failover_op: %s", gfarm_error_string(e));
			if (gfm_client_is_connection_error(e) &&
			    post_nretry > 0) {
				/*
				 * following cases:
				 * - acquired conneciton in failover() is
				 *   created before failover().
				 * - connection error occurred after failover().
				 */
				post_nretry--;
				goto retry;
			}
		} else {
			nretry--;
			goto retry;
		}
	} else if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_client_rpc_with_failover: rpc_op: %s",
		    gfarm_error_string(e));
		if (nretry == 0 && must_be_warned_op &&
		    must_be_warned_op(e, closure))
			gflog_warning(GFARM_MSG_UNFIXED,
			    "error ocurred at retry for the operation after "
			    "connection to metadb server was failed over, "
			    "so the operation possibly succeeded in the server."
			    " error='%s'",
			    gfarm_error_string(e));
	}
	if (exit_op)
		exit_op(gfm_server, e, closure);

	return (e);
}

struct compound_fd_op_info {
	struct gfs_failover_file *file;
	struct gfs_failover_file_ops *ops;
	gfarm_error_t (*request_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *);
	gfarm_error_t (*result_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *);
	void (*cleanup_op)(struct gfm_connection *, void *);
	int (*must_be_warned_op)(gfarm_error_t, void *);
	void *closure;
};

static gfarm_error_t
compound_fd_op_rpc(struct gfm_connection **gfm_serverp, void *closure)
{
	struct compound_fd_op_info *ci = closure;

	*gfm_serverp = ci->ops->get_connection(ci->file);
	return (gfm_client_compound_fd_op(*gfm_serverp,
	    ci->ops->get_fileno(ci->file), ci->request_op, ci->result_op,
	    ci->cleanup_op, ci->closure));
}

static gfarm_error_t
compound_fd_op_post_failover(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e;
	int fd, type;
	char *url;
	gfarm_ino_t ino;
	struct compound_fd_op_info *ci = closure;
	void *file = ci->file;
	struct gfs_failover_file_ops *ops = ci->ops;
	const char *url0 = ops->get_url(file);

	/* reopen */
	if ((e = gfm_open_fd_with_ino(url0, GFARM_FILE_RDONLY,
	    &gfm_server, &fd, &type, &url, &ino)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_open_fd_with_ino(%s) failed: %s",
		    url0, gfarm_error_string(e));
		/*
		 * any error except connection error means that the file
		 * cannot be accessed.
		 */
		return (gfm_client_is_connection_error(e) ? e :
			GFARM_ERR_STALE_FILE_HANDLE);
	}
	free(url);

	if (type != ops->type || ino != ops->get_ino(file))
		e = GFARM_ERR_STALE_FILE_HANDLE;
	else {
		/* set new fd and connection */
		ops->set_fileno(file, fd);
		ops->set_connection(file, gfm_server);
	}

	if (e != GFARM_ERR_NO_ERROR)
		gfm_client_connection_free(gfm_server);

	return (e);
}

static int
compound_fd_op_must_be_warned(gfarm_error_t e, void *closure)
{
	struct compound_fd_op_info *ci = closure;

	return (ci->must_be_warned_op ? ci->must_be_warned_op(e, ci->closure) :
	    0);
}

static gfarm_error_t
compound_fd_op(struct gfs_failover_file *file,
	struct gfs_failover_file_ops *ops,
	gfarm_error_t (*request_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	gfarm_error_t (*result_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	int (*must_be_warned_op)(gfarm_error_t, void *),
	void *closure)
{
	gfarm_error_t e;
	struct compound_fd_op_info ci = {
		file, ops,
		request_op, result_op, cleanup_op, must_be_warned_op,
		closure
	};

	e = gfm_client_rpc_with_failover(compound_fd_op_rpc,
	    compound_fd_op_post_failover, NULL,
	    compound_fd_op_must_be_warned, &ci);

	return (e);
}

gfarm_error_t
gfm_client_compound_fd_op_readonly(struct gfs_failover_file *file,
	struct gfs_failover_file_ops *ops,
	gfarm_error_t (*request_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	gfarm_error_t (*result_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	void *closure)
{
	gfarm_error_t e = compound_fd_op(file, ops, request_op, result_op,
	    cleanup_op, NULL, closure);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "compound_fd_op: %s", gfarm_error_string(e));
	return (e);
}

struct compound_file_op_info {
	GFS_File gf;
	gfarm_error_t (*request_op)(struct gfm_connection *,
		struct gfp_xdr_context *, void *);
	gfarm_error_t (*result_op)(struct gfm_connection *,
		struct gfp_xdr_context *, void *);
	void (*cleanup_op)(struct gfm_connection *, void *);
	int (*must_be_warned_op)(gfarm_error_t, void *);
	void *closure;
};

static gfarm_error_t
compound_file_op_rpc(struct gfm_connection **gfm_serverp, void *closure)
{
	struct compound_file_op_info *ci = closure;

	*gfm_serverp = ci->gf->gfm_server;
	return (gfm_client_compound_fd_op(*gfm_serverp,
	    ci->gf->fd, ci->request_op, ci->result_op, ci->cleanup_op,
	    ci->closure));
}

static int
compound_file_op_must_be_warned_op(gfarm_error_t e, void *closure)
{
	struct compound_file_op_info *ci = closure;

	return (ci->must_be_warned_op ? ci->must_be_warned_op(e, ci->closure) :
	    0);
}

static gfarm_error_t
compound_file_op(GFS_File gf,
	gfarm_error_t (*request_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	gfarm_error_t (*result_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	int (*must_be_warned_op)(gfarm_error_t, void *),
	void *closure)
{
	gfarm_error_t e;
	struct compound_file_op_info ci = {
		gf,
		request_op, result_op, cleanup_op, must_be_warned_op,
		closure
	};

	e = gfm_client_rpc_with_failover(compound_file_op_rpc, NULL,
	    NULL, compound_file_op_must_be_warned_op, &ci);
	return (e);
}

gfarm_error_t
gfm_client_compound_file_op_readonly(GFS_File gf,
	gfarm_error_t (*request_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	gfarm_error_t (*result_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	void *closure)
{
	gfarm_error_t e = compound_file_op(gf, request_op, result_op,
	    cleanup_op, NULL, closure);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "compound_file_op: %s", gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfm_client_compound_file_op_modifiable(GFS_File gf,
	gfarm_error_t (*request_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	gfarm_error_t (*result_op)(struct gfm_connection *,
	    struct gfp_xdr_context *, void *),
	void (*cleanup_op)(struct gfm_connection *, void *),
	int (*must_be_warned_op)(gfarm_error_t, void *),
	void *closure)
{
	gfarm_error_t e = compound_file_op(gf, request_op, result_op,
	    cleanup_op, must_be_warned_op, closure);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "compound_file_op: %s", gfarm_error_string(e));
	return (e);
}
