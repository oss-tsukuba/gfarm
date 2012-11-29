#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "context.h"
#include "config.h"

struct gfarm_context *gfarm_ctxp;

gfarm_error_t
gfarm_context_init(void)
{
	struct gfarm_context *ctxp;
	gfarm_error_t e;
#	define BUFSIZE_MAX 2048
	gfarm_error_t (*inits[])(struct gfarm_context *) = {
		gfarm_config_static_init,
		gfarm_sockopt_static_init,
		gfm_client_static_init,
		gfs_client_static_init,
		gfarm_host_static_init,
		gfarm_auth_config_static_init,
		gfarm_auth_common_static_init,
#ifdef HAVE_GSI
		gfarm_auth_common_gsi_static_init,
#endif
		gfarm_auth_client_static_init,
		gfarm_schedule_static_init,
		gfarm_gfs_pio_static_init,
		gfarm_gfs_pio_section_static_init,
		gfarm_gfs_stat_static_init,
		gfarm_gfs_unlink_static_init,
		gfarm_gfs_xattr_static_init,
		gfarm_filesystem_static_init
	};
	int i;

	GFARM_MALLOC(ctxp);
	if (ctxp == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/* config.c */
	ctxp->metadb_server_name = NULL;
	ctxp->metadb_server_port = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->metadb_admin_user = NULL;
	ctxp->metadb_admin_user_gsi_dn = NULL;

	ctxp->log_level = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->no_file_system_node_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->gfmd_reconnection_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->attr_cache_limit = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->attr_cache_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_cache_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_concurrency = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_concurrency_per_net = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_idle_load = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_busy_load = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_virtual_load = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_candidates_ratio = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_rtt_thresh_ratio = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_rtt_thresh_diff = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->gfsd_connection_cache = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->gfmd_connection_cache = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->client_file_bufsize = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->client_parallel_copy = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->network_receive_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->file_trace = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->on_demand_replication = 0;
	if ((ctxp->getpw_r_bufsz = sysconf(_SC_GETPW_R_SIZE_MAX)) == -1)
		ctxp->getpw_r_bufsz = BUFSIZE_MAX;

	/* gfs_profile.c */
	ctxp->profile = 0;

	for (i = 0; i < GFARM_ARRAY_LENGTH(inits); i++) {
		e = inits[i](ctxp);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	if (e == GFARM_ERR_NO_ERROR)
		gfarm_ctxp = ctxp;
	else
		free(ctxp);

	return (e);
}

static void
gfarm_context_string_free(char **var)
{
	if (*var != NULL) {
		free(*var);
		*var = NULL;
	}
}

void
gfarm_context_free(void)
{
	gfarm_context_string_free(&gfarm_ctxp->metadb_server_name);
	gfarm_context_string_free(&gfarm_ctxp->metadb_admin_user);
	gfarm_context_string_free(&gfarm_ctxp->metadb_admin_user_gsi_dn);
}
