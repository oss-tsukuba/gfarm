#include <stddef.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"

#include "context.h"
#include "gfs_profile.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_failover.h"

#define staticp	(gfarm_ctxp->gfs_stat_static)

struct gfarm_gfs_stat_static {
	double stat_time;
};

gfarm_error_t
gfarm_gfs_stat_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_gfs_stat_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->stat_time = 0;

	ctxp->gfs_stat_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_gfs_stat_static_term(struct gfarm_context *ctxp)
{
	free(ctxp->gfs_stat_static);
}

struct gfm_stat_closure {
	struct gfs_stat *st;
};

static gfarm_error_t
gfm_stat_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	gfarm_error_t e = gfm_client_fstat_request(gfm_server, ctx);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000130,
		    "fstat request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_stat_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	struct gfm_stat_closure *c = closure;
	gfarm_error_t e = gfm_client_fstat_result(gfm_server, ctx, c->st);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000131,
		    "fstat result; %s", gfarm_error_string(e));
#endif
	if (e == GFARM_ERR_NO_ERROR &&
	    GFARM_S_IS_SUGID_PROGRAM(c->st->st_mode) &&
	    !gfm_is_mounted(gfm_server)) {
		/*
		 * for safety of gfarm2fs "suid" option.
		 * We have to check gfm_server here instead of using
		 * gfm_client_connection_and_process_acquire_by_path(path,),
		 * because we have to follow a symolic link to check it.
		 */
		c->st->st_mode &= ~(GFARM_S_ISUID|GFARM_S_ISGID);
	}
	return (e);
}

gfarm_error_t
gfs_stat(const char *path, struct gfs_stat *s)
{
	gfarm_timerval_t t1, t2;
	struct gfm_stat_closure closure;
	gfarm_error_t e;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.st = s;
	e = gfm_inode_op_readonly(path, GFARM_FILE_LOOKUP,
	    gfm_stat_request,
	    gfm_stat_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->stat_time += gfarm_timerval_sub(&t2, &t1));

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001377,
			"gfm_inode_op(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}

	return (e);
}

gfarm_error_t
gfs_lstat(const char *path, struct gfs_stat *s)
{
	gfarm_timerval_t t1, t2;
	struct gfm_stat_closure closure;
	gfarm_error_t e;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.st = s;
	e = gfm_inode_op_no_follow_readonly(path, GFARM_FILE_LOOKUP,
	    gfm_stat_request,
	    gfm_stat_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->stat_time += gfarm_timerval_sub(&t2, &t1));

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002666,
			"gfm_inode_op_no_follow(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}

	return (e);
}

gfarm_error_t
gfs_fstat(GFS_File gf, struct gfs_stat *s)
{
	gfarm_timerval_t t1, t2;
	struct gfm_stat_closure closure;
	gfarm_error_t e;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.st = s;

	e = gfm_client_compound_file_op_readonly(gf,
	    gfm_stat_request, gfm_stat_result, NULL, &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->stat_time += gfarm_timerval_sub(&t2, &t1));

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_client_compound_file_op_readonly() failed: %s",
		    gfarm_error_string(e));
	}

	return (e);
}

void
gfs_stat_display_timers(void)
{
	gflog_info(GFARM_MSG_1000132,
	    "gfs_stat        : %g sec", staticp->stat_time);
}
