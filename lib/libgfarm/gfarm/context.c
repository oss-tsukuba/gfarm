#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "context.h"
#include "config.h"

#ifndef __KERNEL__	/* gfarm_ctxp */
struct gfarm_context *gfarm_ctxp;
#endif /* __KERNEL__ */

struct gfarm_context_module_entry {
	gfarm_error_t (*init)(struct gfarm_context *);
	void          (*term)(struct gfarm_context *);
};

static const struct gfarm_context_module_entry module_entries[] = {
	{
		gfarm_config_static_init,
		gfarm_config_static_term
	},
	{
		gfarm_sockopt_static_init,
		gfarm_sockopt_static_term
	},
	{
		gfm_client_static_init,
		gfm_client_static_term
	},
	{
		gfs_client_static_init,
		gfs_client_static_term
	},
	{
		gfarm_host_static_init,
		gfarm_host_static_term
	},
#ifndef __KERNEL__	/* auth */
	{
		gfarm_auth_config_static_init,
		gfarm_auth_config_static_term
	},
	{
		gfarm_auth_common_static_init,
		gfarm_auth_common_static_term
	},
#ifdef HAVE_GSI
	{
		gfarm_auth_common_gsi_static_init,
		gfarm_auth_common_gsi_static_term
	},
#endif
	{
		gfarm_auth_client_static_init,
		gfarm_auth_client_static_term
	},
#endif /* __KERNEL__ */
	{
		gfarm_schedule_static_init,
		gfarm_schedule_static_term
	},
	{
		gfarm_gfs_pio_static_init,
		gfarm_gfs_pio_static_term
	},
	{
		gfarm_gfs_pio_section_static_init,
		gfarm_gfs_pio_section_static_term
	},
	{
		gfarm_gfs_pio_local_static_init,
		gfarm_gfs_pio_local_static_term
	},
	{
		gfarm_gfs_pio_remote_static_init,
		gfarm_gfs_pio_remote_static_term
	},
	{
		gfarm_gfs_stat_static_init,
		gfarm_gfs_stat_static_term
	},
	{
		gfarm_gfs_unlink_static_init,
		gfarm_gfs_unlink_static_term
	},
#ifndef __KERNEL__	/* xattr */
	{
		gfarm_gfs_xattr_static_init,
		gfarm_gfs_xattr_static_term
	},
	{
		gfarm_iostat_static_init,
		gfarm_iostat_static_term
	},
#ifdef HAVE_INFINIBAND
	{
		gfs_ib_rdma_static_init,
		gfs_ib_rdma_static_term
	},
#endif /* HAVE_INFINIBAND */
#endif /* __KERNEL__ */
	{
		gfarm_filesystem_static_init,
		gfarm_filesystem_static_term
	},
};

gfarm_error_t
gfarm_context_init(void)
{
	struct gfarm_context *ctxp;
	gfarm_error_t e;
#	define BUFSIZE_MAX 2048
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
	ctxp->gfmd_authentication_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->gfmd_reconnection_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->gfsd_connection_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->attr_cache_limit = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->attr_cache_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->page_cache_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_cache_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_concurrency = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_concurrency_per_net = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_idle_load = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_busy_load = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_virtual_load = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_candidates_ratio = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_rtt_thresh_ratio = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_rtt_thresh_diff = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->schedule_write_target_domain = NULL;
	ctxp->schedule_write_local_priority = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->direct_local_access = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->replication_at_write_open = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->gfsd_connection_cache = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->gfmd_connection_cache = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->client_digest_check = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->client_file_bufsize = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->client_parallel_copy = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->client_parallel_max = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->network_receive_timeout = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->file_trace = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->ib_rdma = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->rdma_min_size = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->rdma_port = 0;
	ctxp->rdma_device = NULL;
	ctxp->rdma_mr_reg_mode = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->rdma_mr_reg_static_min_size = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->rdma_mr_reg_static_max_size = GFARM_CONFIG_MISC_DEFAULT;
	ctxp->on_demand_replication = 0;
	ctxp->call_rpc_instead_syscall = 0;
	ctxp->fatal_action = GFARM_CONFIG_MISC_DEFAULT;
	if ((ctxp->getpw_r_bufsz = sysconf(_SC_GETPW_R_SIZE_MAX)) == -1)
		ctxp->getpw_r_bufsz = BUFSIZE_MAX;

	/* gfs_profile.c */
	ctxp->profile = GFARM_CONFIG_MISC_DEFAULT;

	for (i = 0; i < GFARM_ARRAY_LENGTH(module_entries); i++) {
		e = (module_entries[i].init)(ctxp);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	if (e == GFARM_ERR_NO_ERROR)
		gfarm_ctxp = ctxp;
	else
		free(ctxp);

	return (e);
}

void
gfarm_context_term(void)
{
	int i;

	if (gfarm_ctxp == NULL)
		return;

	for (i = 0; i < GFARM_ARRAY_LENGTH(module_entries); i++)
		(module_entries[i].term)(gfarm_ctxp);
	free(gfarm_ctxp->metadb_server_name);
	free(gfarm_ctxp->metadb_admin_user);
	free(gfarm_ctxp->metadb_admin_user_gsi_dn);
	free(gfarm_ctxp->schedule_write_target_domain);
	free(gfarm_ctxp);

	gfarm_ctxp = NULL;
}
