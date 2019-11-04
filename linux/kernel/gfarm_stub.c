#include <sys/time.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "quota_info.h"
#include "gfm_client.h"
#include "context.h"
#include "auth.h"

struct gfs_connection;
struct gfs_file_list;

gfarm_error_t
gfarm_auth_server_cred_type_set_by_string(char *service_tag, char *name)
{
	gflog_debug(GFARM_MSG_1004802, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);

}
gfarm_error_t
gfarm_auth_server_cred_service_set(char *service_tag, char *service)
{
	gflog_debug(GFARM_MSG_1004803, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_server_cred_name_set(char *service_tag, char *name)
{
	gflog_debug(GFARM_MSG_1004804, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_enable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp)
{
	gflog_debug(GFARM_MSG_1004805, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_disable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp)
{
	gflog_debug(GFARM_MSG_1004806, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_method_parse(char *name, enum gfarm_auth_method *methodp)
{
	gflog_debug(GFARM_MSG_1004807, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
void
gfarm_auth_random(void *buffer, size_t length)
{
	gflog_debug(GFARM_MSG_1004808, "Not supported yet");
}
gfarm_error_t
gfarm_auth_config_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_1004809, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_common_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_1004810, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_client_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_1004811, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_gfs_xattr_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_1004812, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
void
gfarm_log_backtrace_symbols(void)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}


gfarm_error_t
gfs_getxattr(const char *path, const char *name, void *value, size_t *size)
{
	gflog_debug(GFARM_MSG_1004813, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfs_lgetxattr(const char *path, const char *name, void *value, size_t *size)
{
	gflog_debug(GFARM_MSG_1004814, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}

char GFS_SERVICE_TAG[] = "gfarm-data";

/*
gfs_client_connect_result_multiplexed
gfs_client_connection_alloc_and_auth
gfs_client_connect_request_multiplexed
gfs_pio_open_local_section
-------------------------------
*/
