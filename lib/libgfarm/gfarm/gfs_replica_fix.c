#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfm_proto.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_replica_fix.h"

struct gfm_replica_fix_closure {
	gfarm_uint64_t iflags, oflags;
	gfarm_uint32_t timeout;
};

static gfarm_error_t
gfm_replica_fix_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_replica_fix_closure *c = closure;
	gfarm_error_t e = gfm_client_replica_fix_request(
	    gfm_server, c->iflags, c->timeout);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_UNFIXED,
		    "replica_fix request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_replica_fix_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_replica_fix_closure *c = closure;
	gfarm_error_t e;

	if ((c->iflags & GFM_PROTO_REPLICA_FIX_IFLAGS_TO_WAIT) != 0)
		e = gfm_client_replica_fix_result_notimeout(
		    gfm_server, &c->oflags);
	else
		e = gfm_client_replica_fix_result_timeout(
		    gfm_server, &c->oflags);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "replica_fix result; %s",
		    gfarm_error_string(e));
#endif
	return (e);
}

gfarm_error_t
gfs_replica_fix(
	const char *file, gfarm_uint64_t iflags, gfarm_int32_t timeout,
	gfarm_uint64_t *oflagsp)
{
	gfarm_error_t e;
	struct gfm_replica_fix_closure closure;

	closure.iflags = iflags;
	closure.timeout = timeout;;
	closure.oflags = 0;
	e = gfm_inode_op_modifiable(file,
	    GFARM_FILE_LOOKUP|GFARM_FILE_REPLICA_SPEC,
	    gfm_replica_fix_request,
	    gfm_replica_fix_result,
	    gfm_inode_success_op_connection_free,
	    NULL, NULL,
	    &closure);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (oflagsp != NULL)
		*oflagsp = closure.oflags;
	return (GFARM_ERR_NO_ERROR);
}
