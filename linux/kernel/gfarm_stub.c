#include <sys/time.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "quota_info.h"
#include "gfm_client.h"
#include "context.h"
#include "auth.h"
#include "gfsk_ccib.h"

struct gfs_connection;
struct gfs_file_list;

gfarm_error_t
gfarm_cc_register(struct gfcc_ibaddr *ibaddr)
{
	gflog_debug(GFARM_MSG_1004801, "Not supported yet");
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfarm_cc_find_host(struct gfcc_obj *obj, struct gfcc_ibaddr *ibaddr)
{
	int err;
	err = gfsk_cc_find_host(obj, ibaddr);
	return (gfarm_errno_to_error(-err));
}

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
	dump_stack();
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

void
gfarm_iostat_local_add(unsigned int cat, int val)
{
}

#include <openssl/evp.h>
int
gfarm_msgdigest_name_verify(const char *gfarm_name)
{
	gflog_error(GFARM_MSG_1004815, "Not supported");
	return (0);
}
size_t
gfarm_msgdigest_to_string(
	char *md_string, unsigned char *md_value, size_t md_len)
{
	gflog_error(GFARM_MSG_1004816, "Not supported");
	return (0);
}
EVP_MD_CTX *
gfarm_msgdigest_alloc_by_name(const char *md_type_name, int *cause_p)
{
	gflog_error(GFARM_MSG_1004817, "Not supported");
	if (md_type_name == NULL || md_type_name[0] == '\0') {
		if (cause_p != NULL)
			*cause_p = 0;
		return (NULL);
	}
	if (cause_p != NULL)
		*cause_p = EOPNOTSUPP;
	return (NULL);
}
size_t
gfarm_msgdigest_free(EVP_MD_CTX *md_ctx, unsigned char *md_value)
{
	gflog_error(GFARM_MSG_1004818, "Not supported");
	return (0);
}
size_t
gfarm_msgdigest_to_string_and_free(EVP_MD_CTX *md_ctx, char *md_string)
{
	gflog_error(GFARM_MSG_1004819, "Not supported");
	return (0);
}


/*
gfs_client_connect_result_multiplexed
gfs_client_connection_alloc_and_auth
gfs_client_connect_request_multiplexed
gfs_pio_open_local_section
*/
