#include <sys/time.h>
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "quota_info.h"
#include "gfm_client.h"
#include "context.h"
#include "liberror.h"
#include "hostspec.h"
#include "auth.h"

struct gfs_connection;
struct gfs_file_list;

gfarm_error_t
gfarm_auth_server_cred_type_set_by_string(char *service_tag, char *name)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);

}
gfarm_error_t
gfarm_auth_server_cred_service_set(char *service_tag, char *service)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_server_cred_name_set(char *service_tag, char *name)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_enable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_disable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_method_parse(char *name, enum gfarm_auth_method *methodp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
void
gfarm_auth_random(void *buffer, size_t length)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}
gfarm_error_t
gfarm_auth_config_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_common_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_auth_client_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_schedule_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_gfs_pio_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_gfs_pio_section_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_gfs_xattr_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
void
gfarm_log_backtrace_symbols(void)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}
void
gfarm_quota_info_free(struct gfarm_quota_info *qi)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}
gfarm_error_t
gfarm_client_process_reset(struct gfs_connection *gfs_server,
struct gfm_connection *gfm_server)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfs_client_static_init(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
void
gfs_client_static_term(struct gfarm_context *ctxp)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}
void
gfs_client_terminate(void)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}
gfarm_error_t
gfs_client_close(struct gfs_connection *gfs_server, gfarm_int32_t fd)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_pid_t
gfs_client_pid(struct gfs_connection *gfs_server)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (0);
}
int
gfs_client_connection_failover_count(struct gfs_connection *gfs_server)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (0);
}

void
gfs_client_connection_set_failover_count(
	struct gfs_connection *gfs_server, int count)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}

gfarm_error_t
gfs_pio_error(GFS_File gf)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (0);
}
void
gfs_pio_file_list_foreach(struct gfs_file_list *gfl,
	int (*func)(struct gfs_file *, void *), void *closure)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}

gfarm_error_t
gfs_getxattr(const char *path, const char *name, void *value, size_t *size)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfs_lgetxattr(const char *path, const char *name, void *value, size_t *size)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
struct gfs_file_list *
gfs_pio_file_list_alloc(void)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (NULL);
}
void
gfs_pio_file_list_free(struct gfs_file_list *gfl)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
}

struct gfm_connection *
gfs_pio_metadb(GFS_File gf)
{
	gflog_debug(GFARM_MSG_UNFIXED, "Not supported yet");
	return (NULL);
}
