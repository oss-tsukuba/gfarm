#include <stdlib.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfm_proto.h"
#include "gfm_client.h"
#include "lookup.h"

struct gfm_replica_info_get_closure {
	int inflags;

	gfarm_int32_t n;
	char **hosts;
	gfarm_uint64_t *gens;
	gfarm_int32_t *outflags;
};

static gfarm_error_t
gfm_replica_info_get_request(struct gfm_connection *gfm_server,
	void *closure)
{
	struct gfm_replica_info_get_closure *c = closure;
	gfarm_error_t e = gfm_client_replica_info_get_request(
	    gfm_server, c->inflags);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1001384,
		    "replica_info_get request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_replica_info_get_result(struct gfm_connection *gfm_server,
	void *closure)
{
	struct gfm_replica_info_get_closure *c = closure;
	gfarm_error_t e = gfm_client_replica_info_get_result(gfm_server,
	    &c->n, &c->hosts, &c->gens, &c->outflags);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1001385,
		    "replica_info_get result; %s",
		    gfarm_error_string(e));
#endif
	return (e);
}

struct gfs_replica_info {
	gfarm_int32_t n;
	char **hosts;
	gfarm_uint64_t *gens;
	gfarm_int32_t *flags;
};

gfarm_error_t
gfs_replica_info_by_name(const char *file, int flags,
	struct gfs_replica_info **replica_infop)
{
	gfarm_error_t e;
	struct gfs_replica_info *replica_info;
	struct gfm_replica_info_get_closure closure;

	closure.inflags = flags;
	e = gfm_inode_op_readonly(file, GFARM_FILE_LOOKUP,
	    gfm_replica_info_get_request,
	    gfm_replica_info_get_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC(replica_info);
	if (replica_info == NULL)
		return (GFARM_ERR_NO_MEMORY);
	replica_info->n = closure.n;
	replica_info->hosts = closure.hosts;
	replica_info->gens = closure.gens;
	replica_info->flags = closure.outflags;
	*replica_infop = replica_info;
	return (GFARM_ERR_NO_ERROR);
}

int
gfs_replica_info_number(struct gfs_replica_info *ri)
{
	return (ri->n);
}

const char *
gfs_replica_info_nth_host(struct gfs_replica_info *ri, int n)
{
	if (n < 0 || n >= ri->n)
		return (NULL);
	return (ri->hosts[n]);
}

gfarm_uint64_t
gfs_replica_info_nth_gen(struct gfs_replica_info *ri, int n)
{
	if (n < 0 || n >= ri->n)
		return (~(gfarm_uint64_t)0);
	return (ri->gens[n]);
}

int
gfs_replica_info_nth_is_incomplete(struct gfs_replica_info *ri, int n)
{
	if (n < 0 || n >= ri->n)
		return (1);
	return ((ri->flags[n] & GFM_PROTO_REPLICA_FLAG_INCOMPLETE) != 0);
}

int
gfs_replica_info_nth_is_dead_host(struct gfs_replica_info *ri, int n)
{
	if (n < 0 || n >= ri->n)
		return (1);
	return ((ri->flags[n] & GFM_PROTO_REPLICA_FLAG_DEAD_HOST) != 0);
}

int
gfs_replica_info_nth_is_dead_copy(struct gfs_replica_info *ri, int n)
{
	if (n < 0 || n >= ri->n)
		return (1);
	return ((ri->flags[n] & GFM_PROTO_REPLICA_FLAG_DEAD_COPY) != 0);
}

void
gfs_replica_info_free(struct gfs_replica_info *ri)
{
	int i;

	for (i = 0; i < ri->n; i++)
		free(ri->hosts[i]);
	free(ri->hosts);
	free(ri->gens);
	free(ri->flags);
	free(ri);
}
