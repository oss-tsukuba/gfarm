#include <stdio.h>	/* config.h needs FILE */
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfm_client.h"
#include "lookup.h"

struct gfm_utimes_closure {
	struct gfarm_timespec atime, mtime;
};

static gfarm_error_t
gfm_utimes_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	struct gfm_utimes_closure *c = closure;
	gfarm_error_t e = gfm_client_futimes_request(gfm_server, ctx,
	    c->atime.tv_sec, c->atime.tv_nsec,
	    c->mtime.tv_sec, c->mtime.tv_nsec);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000158,
		    "futimes request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_utimes_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	gfarm_error_t e = gfm_client_futimes_result(gfm_server, ctx);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000159,
		    "futimes result: %s", gfarm_error_string(e));
#endif
	return (e);
}

static gfarm_error_t
gfs_utimes_common(const char *path, const struct gfarm_timespec *tsp,
	gfarm_error_t (*inode_op)(const char *, int,
	    gfm_inode_request_op_t, gfm_result_op_t,
	    gfm_success_op_t, gfm_cleanup_op_t,
	    gfm_must_be_warned_op_t, void *))
{
	struct gfm_utimes_closure closure;
	struct timeval now;

	if (tsp == NULL || tsp[0].tv_nsec == GFARM_UTIME_NOW
			|| tsp[1].tv_nsec == GFARM_UTIME_NOW)
		gettimeofday(&now, NULL);
	if (tsp == NULL) {
		closure.atime.tv_sec  = closure.mtime.tv_sec  =
		    now.tv_sec;
		closure.atime.tv_nsec = closure.mtime.tv_nsec =
		    now.tv_usec * 1000;
	} else {
		
		if (tsp[0].tv_nsec == GFARM_UTIME_NOW) {
			closure.atime.tv_sec  = now.tv_sec;
			closure.atime.tv_nsec = now.tv_usec * 1000;
		} else 
			closure.atime = tsp[0];
		if (tsp[1].tv_nsec == GFARM_UTIME_NOW) {
			closure.mtime.tv_sec  = now.tv_sec;
			closure.mtime.tv_nsec = now.tv_usec * 1000;
		} else 
			closure.mtime = tsp[1];
	}

	return ((*inode_op)(path, GFARM_FILE_LOOKUP,
	    gfm_utimes_request,
	    gfm_utimes_result,
	    gfm_inode_success_op_connection_free,
	    NULL, NULL,
	    &closure));
}

gfarm_error_t
gfs_utimes(const char *path, const struct gfarm_timespec *tsp)
{
	return (gfs_utimes_common(path, tsp, gfm_inode_op_modifiable));
}

gfarm_error_t
gfs_lutimes(const char *path, const struct gfarm_timespec *tsp)
{
	return (gfs_utimes_common(path, tsp,
	    gfm_inode_op_no_follow_modifiable));
}
