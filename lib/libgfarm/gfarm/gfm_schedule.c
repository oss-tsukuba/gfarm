#include <stddef.h>

#include <gfarm/gfarm.h>

#include "gfm_client.h"
#include "gfm_schedule.h"
#include "gfs_failover.h"
#include "lookup.h"

struct gfm_schedule_file_closure {
	/* input parameter */
	const char *domain;

	/* output parameters */
	int nhosts;
	struct gfarm_host_sched_info *infos;
};

static gfarm_error_t
gfm_schedule_file_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_schedule_file_closure *c = closure;
	gfarm_error_t e = gfm_client_schedule_file_request(gfm_server,
	    c->domain);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000106, "schedule_file request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_schedule_file_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_schedule_file_closure *c = closure;
	gfarm_error_t e = gfm_client_schedule_file_result(gfm_server,
	    &c->nhosts, &c->infos);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000107, "schedule_file result: %s",
		    gfarm_error_string(e));
#endif
	return (e);
}

static void
gfm_schedule_file_cleanup(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_schedule_file_closure *c = closure;

	gfarm_host_sched_info_free(c->nhosts, c->infos);
}

gfarm_error_t
gfm_schedule_file(struct gfs_file *gf, int *nhostsp,
	struct gfarm_host_sched_info **infosp)
{
	gfarm_error_t e;
	struct gfm_schedule_file_closure closure;

	closure.domain = "";
	e = gfm_client_compound_file_op_readonly(gf,
	    gfm_schedule_file_request,
	    gfm_schedule_file_result,
	    gfm_schedule_file_cleanup,
	    &closure);
	if (e == GFARM_ERR_NO_ERROR) {
		*nhostsp = closure.nhosts;
		*infosp = closure.infos;
	} else {
		gflog_debug(GFARM_MSG_1001349,
			"gfm_client_compound_fd_op() failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfarm_schedule_hosts_domain_by_file(const char *path, int openflags,
	const char *domain,
	int *nhostsp, struct gfarm_host_sched_info **infosp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	struct gfm_schedule_file_closure closure;

	if ((e = gfm_client_connection_and_process_acquire_by_path(
	    path, &gfm_server)) != GFARM_ERR_NO_ERROR)
		return (e);

	closure.domain = domain;
	e = gfm_inode_op(path, openflags,
	    gfm_schedule_file_request,
	    gfm_schedule_file_result,
	    gfm_inode_success_op_connection_free,
	    gfm_schedule_file_cleanup,
	    &closure);
	if (e == GFARM_ERR_NO_ERROR) {
		*nhostsp = closure.nhosts;
		*infosp = closure.infos;
	} else {
		gflog_debug(GFARM_MSG_1002416,
		    "gfarm_schedule_hosts_domain_by_file(%s, 0x%x, %s): %s",
		    path, openflags, domain, gfarm_error_string(e));
	}
	gfm_client_connection_free(gfm_server);
	return (e);
}

gfarm_error_t
gfarm_schedule_hosts_domain_all(const char *path, const char *domain,
	int *nhostsp, struct gfarm_host_sched_info **infosp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;

	if ((e = gfm_client_connection_and_process_acquire_by_path(
	    path, &gfm_server)) != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfm_client_schedule_host_domain(gfm_server, domain,
	    nhostsp, infosp);
	gfm_client_connection_free(gfm_server);
	return (e);
}
