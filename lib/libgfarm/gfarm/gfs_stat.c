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
#include "gfm_proto.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_pio.h"
#include "gfs_misc.h"
#include "gfs_failover.h"

#define staticp	(gfarm_ctxp->gfs_stat_static)

struct gfarm_gfs_stat_static {
	double stat_time;
	unsigned long long stat_count;
};

gfarm_error_t
gfarm_gfs_stat_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_gfs_stat_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->stat_time = 0;
	s->stat_count = 0;

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
gfm_stat_request(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_fstat_request(gfm_server);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000130,
		    "fstat request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_stat_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_stat_closure *c = closure;
	gfarm_error_t e = gfm_client_fstat_result(gfm_server, c->st);

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
	gfs_profile(staticp->stat_count++);

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
	gfs_profile(staticp->stat_count++);

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
	gfs_profile(staticp->stat_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003976,
		    "gfm_client_compound_file_op_readonly() failed: %s",
		    gfarm_error_string(e));
	}

	return (e);
}

struct gfm_stat_cksum_closure {
	struct gfs_stat_cksum *st;
};

static gfarm_error_t
gfm_stat_cksum_request(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_cksum_get_request(gfm_server);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003744,
		    "cksum_get request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_stat_cksum_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_stat_cksum_closure *c = closure;
	struct gfs_stat_cksum *st = c->st;
	size_t size;
	gfarm_error_t e;

	st->cksum = malloc(GFM_PROTO_CKSUM_MAXLEN);
	if (st->cksum == NULL)
		size = 0;
	else
		size = GFM_PROTO_CKSUM_MAXLEN;
	e = gfm_client_cksum_get_result(gfm_server,
		&st->type, size, &st->len, st->cksum, &st->flags);
#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003745,
		    "cksum_get result; %s", gfarm_error_string(e));
#endif
	if (size > st->len)
		st->cksum[st->len] = '\0';
	return (e);
}

gfarm_error_t
gfs_stat_cksum(const char *path, struct gfs_stat_cksum *s)
{
	struct gfm_stat_cksum_closure closure;
	gfarm_error_t e;

	closure.st = s;
	e = gfm_inode_op_readonly(path, GFARM_FILE_LOOKUP,
	    gfm_stat_cksum_request,
	    gfm_stat_cksum_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003746,
		    "gfm_stat_cksum(%s): %s", path, gfarm_error_string(e));

	return (e);
}

gfarm_error_t
gfs_fstat_cksum(GFS_File gf, struct gfs_stat_cksum *s)
{
	struct gfm_stat_cksum_closure closure;
	gfarm_error_t e;

	closure.st = s;
	e = gfm_client_compound_file_op_readonly(gf,
	    gfm_stat_cksum_request, gfm_stat_cksum_result, NULL, &closure);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003747, "gfm_fstat_cksum(%s): %s",
		    gfs_pio_url(gf), gfarm_error_string(e));

	return (e);
}

static gfarm_error_t
gfm_stat_cksum_set_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_stat_cksum_closure *c = closure;
	struct gfs_stat_cksum *st = c->st;
	gfarm_error_t e = gfm_client_cksum_set_request(gfm_server,
	    st->type, st->len, st->cksum, 0, 0, 0);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1003748,
		    "cksum_set request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_stat_cksum_set_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_cksum_set_result(gfm_server);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003749,
		    "cksum_set result; %s", gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_fstat_cksum_set(GFS_File gf, struct gfs_stat_cksum *s)
{
	struct gfm_stat_cksum_closure closure;
	gfarm_error_t e;

	closure.st = s;
	e = gfm_client_compound_file_op_modifiable(gf,
	    gfm_stat_cksum_set_request, gfm_stat_cksum_set_result, NULL,
	    NULL /* GFM_PROTO_CKSUM_SET never returns ALREADY_EXISTS */,
	    &closure);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003750, "gfm_fstat_cksum_set(%s): %s",
		    gfs_pio_url(gf), gfarm_error_string(e));

	return (e);
}

gfarm_error_t
gfs_stat_cksum_free(struct gfs_stat_cksum *s)
{
	if (s != NULL) {
		free(s->type);
		free(s->cksum);
	}
	return (GFARM_ERR_NO_ERROR);
}

void
gfs_stat_display_timers(void)
{
	gflog_info(GFARM_MSG_1000132,
	    "gfs_stat time  : %g sec", staticp->stat_time);
	gflog_info(GFARM_MSG_1003836,
	    "gfs_stat count : %llu", staticp->stat_count);
}
