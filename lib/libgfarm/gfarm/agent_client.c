/*
 * $Id$
 */

#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h> /* sprintf() */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <gfarm/gfarm.h>

#include "xxx_proto.h"
#include "io_fd.h"
#include "config.h"
#include "agent_proto.h"
#include "agent_client.h"

struct agent_connection {
	struct xxx_connection *conn;
};

static char *
agent_client_connection0(
	struct sockaddr_un *peer_addr, struct agent_connection *agent_server)
{
	char *e;
	int sock;

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock == -1)
		return (gfarm_errno_to_error(errno));
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	if (connect(sock, (struct sockaddr *)peer_addr, sizeof(*peer_addr)) < 0) {
		e = gfarm_errno_to_error(errno);
		close(sock);
		return (e);
	}
	e = xxx_fd_connection_new(sock, &agent_server->conn);
	if (e != NULL) {
		close(sock);
		return (e);
	}
	return (NULL);
}

char *
agent_client_connect(struct sockaddr_un *peer_addr,
	struct agent_connection **agent_serverp)
{
	struct agent_connection *agent_server =
		malloc(sizeof(struct agent_connection));
	char *e;

	if (agent_server == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = agent_client_connection0(peer_addr, agent_server);
	if (e != NULL) {
		free(agent_server);
		return (e);
	}
	*agent_serverp = agent_server;
	return (NULL);
}

char *
agent_client_disconnect(struct agent_connection *agent_server)
{
	char *e = xxx_connection_free(agent_server->conn);

	free(agent_server);
	return (e);
}

/*
 * agent_client RPC
 */

char *
agent_client_rpc(struct agent_connection *agent_server, int just, int command,
	       char *format, ...)
{
	va_list ap;
	char *e;
	int error;

	va_start(ap, format);
	e = xxx_proto_vrpc(agent_server->conn, just,
			   command, &error, &format, &ap);
	va_end(ap);

	if (e != NULL)
		return (e);
	if (error != 0)
		return (gfarm_errno_to_error(error));
	return (NULL);
}

char *
agent_client_path_info_get(struct agent_connection *agent_server,
	const char *path, struct gfarm_path_info *info)
{
	return (agent_client_rpc(
			agent_server, 0, AGENT_PROTO_PATH_INFO_GET,
			"s/siissoiiiiiii", path,
			&info->pathname, 
			&info->status.st_ino, &info->status.st_mode,
			&info->status.st_user, &info->status.st_group,
			&info->status.st_size, &info->status.st_nsections,
			&info->status.st_atimespec.tv_sec,
			&info->status.st_atimespec.tv_nsec, 
			&info->status.st_mtimespec.tv_sec,
			&info->status.st_mtimespec.tv_nsec, 
			&info->status.st_ctimespec.tv_sec,
			&info->status.st_ctimespec.tv_nsec));
}

char *
agent_client_path_info_set(struct agent_connection *agent_server,
	char *path, struct gfarm_path_info *info)
{
	return (agent_client_rpc(agent_server, 0, AGENT_PROTO_PATH_INFO_SET,
			"siissoiiiiiii/", path,
			info->status.st_ino, info->status.st_mode,
			info->status.st_user, info->status.st_group,
			info->status.st_size, info->status.st_nsections,
			info->status.st_atimespec.tv_sec,
			info->status.st_atimespec.tv_nsec, 
			info->status.st_mtimespec.tv_sec,
			info->status.st_mtimespec.tv_nsec, 
			info->status.st_ctimespec.tv_sec,
			info->status.st_ctimespec.tv_nsec));
}

char *
agent_client_path_info_replace(struct agent_connection *agent_server,
	char *path, struct gfarm_path_info *info)
{
	return (agent_client_rpc(agent_server, 0, AGENT_PROTO_PATH_INFO_REPLACE,
			"siissoiiiiiii/", path,
			info->status.st_ino, info->status.st_mode,
			info->status.st_user, info->status.st_group,
			info->status.st_size, info->status.st_nsections,
			info->status.st_atimespec.tv_sec,
			info->status.st_atimespec.tv_nsec, 
			info->status.st_mtimespec.tv_sec,
			info->status.st_mtimespec.tv_nsec, 
			info->status.st_ctimespec.tv_sec,
			info->status.st_ctimespec.tv_nsec));
}

char *
agent_client_path_info_remove(struct agent_connection *agent_server,
	const char *path)
{
	return (agent_client_rpc(agent_server, 0, AGENT_PROTO_PATH_INFO_REMOVE,
				 "s/", path));
}

char *
agent_client_realpath_canonical(struct agent_connection *agent_server,
	const char *path, char **abspathp)
{
	return (agent_client_rpc(agent_server, 0,
			AGENT_PROTO_REALPATH_CANONICAL, "s/s",
			path, abspathp));
}

char *
agent_client_get_ino(struct agent_connection *agent_server,
	const char *path, gfarm_int32_t *inop)
{
	return (agent_client_rpc(agent_server, 0, AGENT_PROTO_GET_INO, "s/i",
				 path, inop));
}

char *
agent_client_opendir(struct agent_connection *agent_server,
	const char *path, GFS_Dir *dirp)
{
	gfarm_int32_t p;
	char *e;

	e = agent_client_rpc(agent_server, 0, AGENT_PROTO_OPENDIR, "s/i",
			     path, &p);
	if (e == NULL)
		*dirp = (GFS_Dir)p;
	return (e);
}

char *
agent_client_readdir(struct agent_connection *agent_server,
	GFS_Dir dir, struct gfs_dirent **entry)
{
	gfarm_int32_t p = (gfarm_int32_t)dir;
	char *e, *name;
	static struct gfs_dirent de;

	e = agent_client_rpc(
		agent_server, 0, AGENT_PROTO_READDIR,
		"i/ihccs", p,
		&de.d_fileno, &de.d_reclen,
		&de.d_type, &de.d_namlen, &name);
	if (e == NULL) {
		if (*name == '\0')
			*entry = NULL;
		else {
			strcpy(de.d_name, name);
			*entry = &de;
		}
		free(name);
	}
	return (e);
}

char *
agent_client_closedir(struct agent_connection *agent_server, GFS_Dir dir)
{
	gfarm_int32_t p = (gfarm_int32_t)dir;

	return (agent_client_rpc(
			agent_server, 0, AGENT_PROTO_CLOSEDIR,
			"i/", p));
}

char *
agent_client_dirname(struct agent_connection *agent_server, GFS_Dir dir)
{
	gfarm_int32_t p = (gfarm_int32_t)dir;
	char *e, *n;
	static char name[GFS_MAXNAMLEN];

	e = agent_client_rpc(
		agent_server, 0, AGENT_PROTO_DIRNAME,
		"i/s", p, &n);
	if (e == NULL) {
		strcpy(name, n);
		free(n);
		return (name);
	}
	else
		return (NULL);
}

char *
agent_client_uncachedir(struct agent_connection *agent_server)
{
	return (agent_client_rpc(agent_server, 0, AGENT_PROTO_UNCACHEDIR, "/"));
}
