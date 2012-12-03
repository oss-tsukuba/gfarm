/*
 * $Id$
 */

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>	/* TCP_NODELAY */
#include <netdb.h>		/* getprotobyname() */
#include <errno.h>
#include <string.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"

#include "context.h"
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

#define staticp	(gfarm_ctxp->sockopt_static)

struct gfarm_sockopt_static {
	struct gfarm_param_config *config_list;
	struct gfarm_param_config **config_last;

	struct gfarm_param_config *listener_config_list;
	struct gfarm_param_config **listener_config_last;
};

gfarm_error_t
gfarm_sockopt_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_sockopt_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->config_list = NULL;
	s->config_last = &s->config_list;
	s->listener_config_list = NULL;
	s->listener_config_last = &s->listener_config_list;

	ctxp->sockopt_static = s;
	return (GFARM_ERR_NO_ERROR);
}
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
				gflog_fatal(GFARM_MSG_1000008,
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

static gfarm_error_t
gfarm_sockopt_config_get_index(char *config, int *indexp, int *valuep)
{
	gfarm_error_t e;
	int param_type_index;
	long value;

	e = gfarm_sockopt_initialize();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000899,
			"Initialization of socket option failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	e = gfarm_param_config_parse_long(NSOCKOPTS, gfarm_sockopt_type_table,
	    config, &param_type_index, &value);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		gflog_debug(GFARM_MSG_1000900,
			"Unknown socket option (%s)",
			config);
		return (GFARM_ERRMSG_UNKNOWN_SOCKET_OPTION);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000901,
			"gfarm_param_config_parse_long(%s) failed: %s",
			config,
			gfarm_error_string(e));
		return (e);
	}
	if (indexp != NULL)
		*indexp = param_type_index;
	if (valuep != NULL)
		*valuep = value;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_sockopt_config_add_internal(struct gfarm_param_config ***lastp,
	char *config, struct gfarm_hostspec *hsp)
{
	int index, value;
	gfarm_error_t e;

	e = gfarm_sockopt_config_get_index(config, &index, &value);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (gfarm_param_config_add_long(lastp, index, value, hsp));
}

gfarm_error_t
gfarm_sockopt_set_option(int fd, char *config)
{
	int index, v;
	struct gfarm_param_type *type;
	struct gfarm_sockopt_info *info;
	gfarm_error_t e;

	e = gfarm_sockopt_config_get_index(config, &index, &v);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	type = &gfarm_sockopt_type_table[index];
	info = type->extension;
	if (setsockopt(fd, info->level, info->option, &v, sizeof(v)) == -1) {
		int save_errno = errno;

		gflog_debug(GFARM_MSG_1003366,
			"setsocketopt(%d) to (%d) failed: %s",
			index, v, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_sockopt_config_add(char *option, struct gfarm_hostspec *hsp)
{
	return (gfarm_sockopt_config_add_internal(&staticp->config_last,
	    option, hsp));
}

gfarm_error_t
gfarm_sockopt_listener_config_add(char *option)
{
	return (gfarm_sockopt_config_add_internal(
	    &staticp->listener_config_last, option, NULL));
}

static gfarm_error_t
gfarm_sockopt_set(void *closure, int param_type_index, long value)
{
	int fd = *(int *)closure, v = value;
	struct gfarm_param_type *type =
	    &gfarm_sockopt_type_table[param_type_index];
	struct gfarm_sockopt_info *info = type->extension;

	if (setsockopt(fd, info->level, info->option, &v, sizeof(v)) == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1000902,
			"setsocketopt(%d) to (%ld) failed: %s",
			param_type_index, value, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_sockopt_apply_by_name_addr(int fd, const char *name,
	struct sockaddr *addr)
{
	return (gfarm_param_apply_long_by_name_addr(staticp->config_list,
	    name, addr, gfarm_sockopt_set, &fd));
}

gfarm_error_t
gfarm_sockopt_apply_listener(int fd)
{
	return (gfarm_param_apply_long(staticp->listener_config_list,
	    gfarm_sockopt_set, &fd));
}
