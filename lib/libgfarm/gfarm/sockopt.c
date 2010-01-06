/*
 * $Id$
 */

#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>	/* TCP_NODELAY */
#include <netdb.h>		/* getprotobyname() */
#include <errno.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include "gfutil.h"
#include "liberror.h"
#include "hostspec.h"
#include "param.h"
#include "sockopt.h"

struct gfarm_sockopt_info {
	char *proto;
	int level, option;
};

struct gfarm_sockopt_info gfarm_sockopt_info_debug =
    { NULL, SOL_SOCKET,	SO_DEBUG };
struct gfarm_sockopt_info gfarm_sockopt_info_keepalive =
    { NULL, SOL_SOCKET,	SO_KEEPALIVE };
struct gfarm_sockopt_info gfarm_sockopt_info_sndbuf =
    { NULL, SOL_SOCKET,	SO_SNDBUF };
struct gfarm_sockopt_info gfarm_sockopt_info_rcvbuf =
    { NULL, SOL_SOCKET,	SO_RCVBUF };
struct gfarm_sockopt_info gfarm_sockopt_info_tcp_nodelay =
    { "tcp", 0,		TCP_NODELAY };

struct gfarm_param_type gfarm_sockopt_type_table[] = {
    { "debug",		1, &gfarm_sockopt_info_debug },
    { "keepalive",	1, &gfarm_sockopt_info_keepalive },
    { "sndbuf",		0, &gfarm_sockopt_info_sndbuf },
    { "rcvbuf",		0, &gfarm_sockopt_info_rcvbuf },
    { "tcp_nodelay",	1, &gfarm_sockopt_info_tcp_nodelay },
};

#define NSOCKOPTS GFARM_ARRAY_LENGTH(gfarm_sockopt_type_table)

struct gfarm_param_config *gfarm_sockopt_config_list = NULL;
struct gfarm_param_config **gfarm_sockopt_config_last =
    &gfarm_sockopt_config_list;

struct gfarm_param_config *gfarm_sockopt_listener_config_list = NULL;
struct gfarm_param_config **gfarm_sockopt_listener_config_last =
    &gfarm_sockopt_listener_config_list;

static void
sockopt_initialize(void)
{
	int i;
	struct gfarm_param_type *type;
	struct gfarm_sockopt_info *info;
	struct protoent *proto;

	for (i = 0; i < NSOCKOPTS; i++) {
		type = &gfarm_sockopt_type_table[i];
		info = type->extension;
		if (info->proto != NULL) {
			proto = getprotobyname(info->proto);
			if (proto == NULL)
				gflog_fatal(GFARM_MSG_UNFIXED,
				    "getprotobyname(%s) failed",
				    info->proto);
			info->level = proto->p_proto;
		}
	}
}

gfarm_error_t
gfarm_sockopt_initialize(void)
{
	static pthread_once_t initialized = PTHREAD_ONCE_INIT;

	pthread_once(&initialized, sockopt_initialize);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_sockopt_config_add_internal(struct gfarm_param_config ***lastp,
	char *config, struct gfarm_hostspec *hsp)
{
	gfarm_error_t e;
	int param_type_index;
	long value;

	e = gfarm_sockopt_initialize();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfarm_param_config_parse_long(NSOCKOPTS, gfarm_sockopt_type_table,
	    config, &param_type_index, &value);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		return (GFARM_ERRMSG_UNKNOWN_SOCKET_OPTION);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfarm_param_config_add_long(lastp,
	    param_type_index, value, hsp));
}

gfarm_error_t
gfarm_sockopt_config_add(char *option, struct gfarm_hostspec *hsp)
{
	return (gfarm_sockopt_config_add_internal(
	    &gfarm_sockopt_config_last, option, hsp));
}

gfarm_error_t
gfarm_sockopt_listener_config_add(char *option)
{
	return (gfarm_sockopt_config_add_internal(
	    &gfarm_sockopt_listener_config_last, option, NULL));
}

static gfarm_error_t
gfarm_sockopt_set(void *closure, int param_type_index, long value)
{
	int fd = *(int *)closure, v = value;
	struct gfarm_param_type *type =
	    &gfarm_sockopt_type_table[param_type_index];
	struct gfarm_sockopt_info *info = type->extension;

	if (setsockopt(fd, info->level, info->option, &v, sizeof(v)) == -1)
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_sockopt_apply_by_name_addr(int fd, const char *name,
	struct sockaddr *addr)
{
	return (gfarm_param_apply_long_by_name_addr(gfarm_sockopt_config_list,
	    name, addr, gfarm_sockopt_set, &fd));
}

gfarm_error_t
gfarm_sockopt_apply_listener(int fd)
{
	return (gfarm_param_apply_long(gfarm_sockopt_listener_config_list,
	    gfarm_sockopt_set, &fd));
}
