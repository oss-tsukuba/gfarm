/*
 * $Id$
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "config.h"
#include "host.h"
#include "gfm_client.h"
#include "gfs_client.h"
#include "lookup.h"
#include "schedule.h"
#include "gfs_misc.h"
#include "gfs_failover.h"

/*#define V2_4 1*/

struct gfm_replicate_file_from_to_closure {
	const char *srchost;
	const char *dsthost;
	int flags;
};

static gfarm_error_t
gfm_replicate_file_from_to_request(struct gfm_connection *gfm_server,
	void *closure)
{
	struct gfm_replicate_file_from_to_closure *c = closure;
	gfarm_error_t e = gfm_client_replicate_file_from_to_request(
	    gfm_server, c->srchost, c->dsthost, c->flags);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1001386,
		    "replicate_file_from_to request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_replicate_file_from_to_result(struct gfm_connection *gfm_server,
	void *closure)
{
	gfarm_error_t e = gfm_client_replicate_file_from_to_result(gfm_server);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1001387,
		    "replicate_file_from_to result; %s",
		    gfarm_error_string(e));
#endif
	return (e);
}

gfarm_error_t
gfs_replicate_file_from_to_request(
	const char *file, const char *srchost, const char *dsthost, int flags)
{
	gfarm_error_t e;
	struct gfm_replicate_file_from_to_closure closure;

	if ((flags & GFS_REPLICATE_FILE_WAIT) != 0)
		return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);

	closure.srchost = srchost;
	closure.dsthost = dsthost;
	closure.flags = (flags & ~GFS_REPLICATE_FILE_MIGRATE);
	e = gfm_inode_op_modifiable(file, GFARM_FILE_LOOKUP,
	    gfm_replicate_file_from_to_request,
	    gfm_replicate_file_from_to_result,
	    gfm_inode_success_op_connection_free,
	    NULL, NULL,
	    &closure);

	/*
	 * XXX GFS_REPLICATE_FILE_MIGRATE is not implemented by gfmd for now.
	 * So, we do it by client side.
	 */
	if (e == GFARM_ERR_NO_ERROR &&
	    (flags & GFS_REPLICATE_FILE_MIGRATE) != 0)
		e = gfs_replica_remove_by_file(file, srchost);
	return (e);
}

gfarm_error_t
gfs_replicate_file_to_request(
	const char *file, const char *dsthost, int flags)
{
	char *srchost;
	int srcport;
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(file, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfarm_schedule_file(gf, &srchost, &srcport);
	e2 = gfs_pio_close(gf);
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfs_replicate_file_from_to_request(file, srchost,
		    dsthost, flags);
		free(srchost);
	}

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfs_replicate_file_from_to(
	const char *file, const char *srchost, const char *dsthost, int flags)
{
	return (gfs_replicate_file_from_to_request(file, srchost, dsthost,
	   flags /* | GFS_REPLICATE_FILE_WAIT */));
}

gfarm_error_t
gfs_replicate_file_to(const char *file, const char *dsthost, int flags)
{
	return (gfs_replicate_file_to_request(file, dsthost,
	   flags /* | GFS_REPLICATE_FILE_WAIT */));
}


/* XXX FIXME */
static gfarm_error_t
gfs_replicate_from_to_internal(GFS_File gf, char *srchost, int srcport,
	char *dsthost, int dstport)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server = gfs_pio_metadb(gf);
	struct gfs_connection *gfs_server;
	int gfsd_nretries = GFS_CONN_RETRY_COUNT;
	int failover_nretries = GFS_FAILOVER_RETRY_COUNT;

retry:
	gfm_server = gfs_pio_metadb(gf);
	gfm_client_connection_addref(gfm_server);
	e = gfs_client_connection_and_process_acquire(
	    &gfm_server, dsthost, dstport, &gfs_server, NULL);
	gfm_client_connection_delref(gfm_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001388,
			"acquirement of client connection failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	e = gfs_client_replica_add_from(gfs_server, srchost, srcport,
	    gfs_pio_fileno(gf));
	gfs_client_connection_free(gfs_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003982,
		    "gfs_client_replica_add_from: %s",
		    gfarm_error_string(e));
		if (gfs_client_is_connection_error(e) &&
		    --gfsd_nretries >= 0)
			goto retry;
		if (gfs_pio_should_failover(gf, e) &&
		    --failover_nretries >= 0) {
			if ((e = gfs_pio_failover(gf)) == GFARM_ERR_NO_ERROR)
				goto retry;
			gflog_debug(GFARM_MSG_1003983,
			    "gfs_pio_failover: %s",
			    gfarm_error_string(e));
		}
	}
	if ((e == GFARM_ERR_ALREADY_EXISTS || e == GFARM_ERR_FILE_BUSY) &&
	    (gfsd_nretries != GFS_CONN_RETRY_COUNT ||
	     failover_nretries != GFS_FAILOVER_RETRY_COUNT)) {
		gflog_notice(GFARM_MSG_1003453,
		    "error ocurred at retry for the operation after "
		    "connection to %s, "
		    "so the operation possibly succeeded in the server."
		    " error='%s'",
		    (gfsd_nretries != GFS_CONN_RETRY_COUNT &&
		     failover_nretries != GFS_FAILOVER_RETRY_COUNT) ?
		    "gfsd was disconnected and connection to "
		    "gfmd was failed over" :
		    gfsd_nretries != GFS_CONN_RETRY_COUNT ?
		    "gfsd was disconnected" :
		    "gfmd was failed over" ,
		    gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfs_replicate_to_internal(char *file, char *dsthost, int dstport, int migrate)
{
	char *srchost;
	int srcport;
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(file, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001389,
			"gfs_pio_open(%s) failed: %s",
			file,
			gfarm_error_string(e));
		return (e);
	}

	e = gfarm_schedule_file(gf, &srchost, &srcport);
	if (e == GFARM_ERR_NO_ERROR) {

#ifndef V2_4
		e = gfs_replicate_from_to_internal(gf, srchost, srcport,
			dsthost, dstport);
#else
		e = gfs_replicate_file_from_to(file, srchost, dsthost,
		    GFS_REPLICATE_FILE_FORCE
		    /* | GFS_REPLICATE_FILE_WAIT */ /* XXX NOTYET */);
#endif
		if (e == GFARM_ERR_NO_ERROR && migrate)
			e = gfs_replica_remove_by_file(file, srchost);
		free(srchost);
	}
	e2 = gfs_pio_close(gf);

	if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001390,
			"error occurred in gfs_replicate_to_internal(): %s",
			gfarm_error_string(e != GFARM_ERR_NO_ERROR ? e : e2));
	}

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfs_replicate_to_local(GFS_File gf, char *srchost, int srcport)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server = gfs_pio_metadb(gf);
	char *self;
	int port, nretries = GFS_FAILOVER_RETRY_COUNT;

	for (;;) {
		e = gfm_host_get_canonical_self_name(gfm_server, &self, &port);
		if (e == GFARM_ERR_NO_ERROR)
			break;
		if (!gfm_client_connection_should_failover(
		    gfs_pio_metadb(gf), e) || nretries-- <= 0)
			break;
		if ((e = gfs_pio_failover(gf)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003984,
			    "gfs_pio_failover: %s", gfarm_error_string(e));
			break;
		}
	}
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfs_replicate_from_to_internal(gf, srchost, srcport,
		    self, port);
	}

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001391,
			"error occurred in gfs_replicate_to_local(): %s",
			gfarm_error_string(e));
	}

	return (e);
}

gfarm_error_t
gfs_replicate_to(char *file, char *dsthost, int dstport)
{
	return (gfs_replicate_to_internal(file, dsthost, dstport, 0));
}

gfarm_error_t
gfs_migrate_to(char *file, char *dsthost, int dstport)
{
	return (gfs_replicate_to_internal(file, dsthost, dstport, 1));
}

gfarm_error_t
gfs_replicate_from_to(char *file, char *srchost, int srcport,
	char *dsthost, int dstport)
{
#ifndef V2_4
	gfarm_error_t e, e2;
	GFS_File gf;

	if (srchost == NULL)
		return (gfs_replicate_to(file, dsthost, dstport));

	e = gfs_pio_open(file, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001392,
			"gfs_pio_open(%s) failed: %s",
			file,
			gfarm_error_string(e));
		return (e);
	}

	e = gfs_replicate_from_to_internal(gf, srchost, srcport,
		dsthost, dstport);
	e2 = gfs_pio_close(gf);

	if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001393,
			"replication failed (%s) from (%s:%d) to (%s:%d): %s",
			file, srchost, srcport, dsthost, dstport,
			gfarm_error_string(e != GFARM_ERR_NO_ERROR ? e : e2));
	}

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
#else
	return (gfs_replicate_file_from_to(file, srchost, dsthost,
	    GFS_REPLICATE_FILE_FORCE
	    /* | GFS_REPLICATE_FILE_WAIT */ /* XXX NOTYET */));

#endif
}

gfarm_error_t
gfs_migrate_from_to(char *file, char *srchost, int srcport,
	char *dsthost, int dstport)
{
	gfarm_error_t e;

	e = gfs_replicate_from_to(file, srchost, srcport, dsthost, dstport);
	return (e != GFARM_ERR_NO_ERROR ? e :
		gfs_replica_remove_by_file(file, srchost));
}
