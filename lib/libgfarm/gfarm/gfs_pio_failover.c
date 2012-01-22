/*
 * $Id$
 */

#include <string.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <openssl/evp.h>

#include <gfarm/gfarm.h>

#include "queue.h"

#include "config.h"
#include "gfm_client.h"
#include "gfs_client.h"
#include "gfs_io.h"
#include "gfs_pio.h"
#include "gfs_misc.h"
#include "filesystem.h"
#include "gfs_failover.h"
#include "gfs_file_list.h"


int
gfs_pio_should_failover(GFS_File gf, gfarm_error_t e)
{
	return (gfarm_filesystem_has_multiple_servers(
		    gfarm_filesystem_get_by_connection(gfs_pio_metadb(gf)))
		&& gfm_client_is_connection_error(e));
}

static gfarm_error_t
gfs_pio_reopen(GFS_File gf)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int fd, type;
	gfarm_ino_t ino;
	char *real_url = NULL;

	if ((e = gfm_open_fd_with_ino(gf->url, gf->open_flags,
	    &gfm_server, &fd, &type, &real_url, &ino)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003380,
		    "reopen operation on file descriptor for URL (%s) "
		    "failed: %s",
		    gf->url,
		    gfarm_error_string(e));
		free(real_url);
		return (e);
	} else if (type != GFS_DT_REG || ino != gf->ino) {
		e = GFARM_ERR_STALE_FILE_HANDLE;
	} else {
		gf->fd = fd;
		if (gf->view_context)
			e = (*gf->ops->view_reopen)(gf);
	}
	if (real_url) {
		free(gf->url);
		gf->url = real_url;
	}

	if (e != GFARM_ERR_NO_ERROR) {
		(void)gfm_close_fd(gfm_server, fd); /* ignore result */
		gf->fd = -1;
		gfm_client_connection_free(gfm_server);
		gf->error = e;
		gflog_debug(GFARM_MSG_1003381,
		    "reopen operation on pio for URL (%s) failed: %s",
		    gf->url,
		    gfarm_error_string(e));
	}

	return (e);
}

static int
close_on_server(GFS_File gf, void *closure)
{
	gfarm_error_t e;
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *sc;

	gfm_client_connection_free(gf->gfm_server);

	if (vc == NULL)
		return (1);

	sc = vc->storage_context;

	if ((e = gfs_client_close(sc, gf->fd)) != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003382,
		    "gfs_client_close: %s", gfarm_error_string(e));
	if (e == GFARM_ERR_GFMD_FAILED_OVER)
		e = GFARM_ERR_NO_ERROR;
	if (e != GFARM_ERR_NO_ERROR)
		gf->error = e;
	gf->fd = -1;

	return (1);
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
	struct gfs_file_section_context *vc;
	struct gfs_connection *sc;
	int fc = gfm_client_connection_failover_count(gfm_server);

	/* increment ref count of gfm_server */
	gf->gfm_server = gfm_server;

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

	vc = gf->view_context;

	if (vc) {
		sc = vc->storage_context;

		/* pid will be 0 if gfarm_client_process_reset() resulted
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
	    (e = gfs_pio_reopen(gf)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003387,
		    "gfs_pio_reopen: %s", gfarm_error_string(e));
	}

	return (1);
}

static int
reset_and_reopen_all(struct gfm_connection *gfm_server)
{
	struct reset_and_reopen_info ri;

	ri.gfm_server = gfm_server;
	ri.must_retry = 0;

	gfs_pio_file_list_foreach(gfm_client_connection_file_list(gfm_server),
	    reset_and_reopen, &ri);

	return (ri.must_retry == 0);
}

static int
set_error(GFS_File gf, void *closure)
{
	gf->error = *(gfarm_error_t *)closure;
	return (1);
}

#define NUM_FAILOVER_RETRY 3

gfarm_error_t
gfs_pio_failover(GFS_File gf)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server = gfs_pio_metadb(gf);
	struct gfs_file_list *gfl = NULL;
	char *host, *user;
	int port, fc, i, ok = 0;

	if ((host = strdup(gfm_client_hostname(gfm_server))) ==  NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1003388,
		    "%s", gfarm_error_string(e));
		goto error_all;
	}
	if ((user = strdup(gfm_client_username(gfm_server))) ==  NULL) {
		free(host);
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1003389,
		    "%s", gfarm_error_string(e));
		goto error_all;
	}

	port = gfm_client_port(gfm_server);
	fc = gfm_client_connection_failover_count(gfm_server);
	gfl = gfm_client_connection_detach_file_list(gfm_server);
	/* force to create new connection in next connection acquirement. */
	gfm_client_purge_from_cache(gfm_server);

	for (i = 0; i < NUM_FAILOVER_RETRY; ++i) {
		/* close fd without accessing to gfmd from client
		 * and release gfm_connection.
		 */
		gfs_pio_file_list_foreach(gfl, close_on_server, NULL);

		/* reconnect to gfmd */
		if ((e = gfm_client_connection_and_process_acquire(
		    host, port, user, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003390,
			    "gfm_client_connection_acquire failed : %s",
			    gfarm_error_string(e));
			goto error_all;
		}
		gfm_client_connection_set_failover_count(gfm_server, fc + 1);
		gfm_client_connection_set_file_list(gfm_server, gfl);
		/* reset processes and reopen files */
		ok = reset_and_reopen_all(gfm_server);
		gfm_client_connection_free(gfm_server);
		if (ok)
			break;
	}

	if (ok) {
		gflog_notice(GFARM_MSG_1003391,
		    "connection to metadb server was failed over successfully");
	} else {
		e = GFARM_ERR_OPERATION_TIMED_OUT;
		gf->error = e;
		gflog_debug(GFARM_MSG_1003392,
		    "falied to fail over: %s", gfarm_error_string(e));
	}

	free(host);
	free(user);

	return (gf->error);

error_all:
	if (gfl == NULL)
		gfl = gfm_client_connection_file_list(gfm_server);
	gfs_pio_file_list_foreach(gfl, set_error, &e);

	return (e);
}
