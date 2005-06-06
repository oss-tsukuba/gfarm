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
agent_client_rpc_request(struct agent_connection *agent_server, int command,
			 char *format, ...)
{
	va_list ap;
	char *e;

	va_start(ap, format);
	e = xxx_proto_vrpc_request(agent_server->conn, command, &format, &ap);
	va_end(ap);
	return (e);
}

char *
agent_client_rpc_result(struct agent_connection *agent_server, int just,
			char *format, ...)
{
	va_list ap;
	char *e;
	int error;

	va_start(ap, format);
	e = xxx_proto_vrpc_result(agent_server->conn, just,
				  &error, &format, &ap);
	va_end(ap);

	if (e != NULL)
		return (e);
	if (error != 0)
		return (gfarm_errno_to_error(error));
	return (NULL);
}

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
	char *e;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_PATH_INFO_GET, "s/", path);
	if (e != NULL)
		return (e);
	return (xxx_proto_recv_path_info(agent_server->conn, info));
}

char *
agent_client_path_info_set(struct agent_connection *agent_server,
	char *path, struct gfarm_path_info *info)
{
	char *e;

	e = agent_client_rpc_request(agent_server,
		AGENT_PROTO_PATH_INFO_SET, "s", path);
	if (e != NULL)
		return (e);
	e = xxx_proto_send_path_info_for_set(agent_server->conn, info);
	if (e != NULL)
		return (e);
	return (agent_client_rpc_result(agent_server, 0, ""));
}

char *
agent_client_path_info_replace(struct agent_connection *agent_server,
	char *path, struct gfarm_path_info *info)
{
	char *e;

	e = agent_client_rpc_request(agent_server,
		AGENT_PROTO_PATH_INFO_REPLACE, "s", path);
	if (e != NULL)
		return (e);
	e = xxx_proto_send_path_info_for_set(agent_server->conn, info);
	if (e != NULL)
		return (e);
	return (agent_client_rpc_result(agent_server, 0, ""));
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

/* host info */

char *
agent_client_host_info_get(struct agent_connection *agent_server,
	const char *host, struct gfarm_host_info *info)
{
	char *e;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_HOST_INFO_GET, "s/", host);
	if (e != NULL)
		return (e);
	return (xxx_proto_recv_host_info(agent_server->conn, info));
}

char *
agent_client_host_info_remove_hostaliases(
	struct agent_connection *agent_server, const char *hostname)
{
	return (agent_client_rpc(agent_server, 0,
			AGENT_PROTO_HOST_INFO_REMOVE_HOSTALIASES, "s/",
			hostname));
}

char *
agent_client_host_info_set(struct agent_connection *agent_server,
	char *hostname, struct gfarm_host_info *info)
{
	char *e;

	e = agent_client_rpc_request(agent_server,
		AGENT_PROTO_HOST_INFO_SET, "s", hostname);
	if (e != NULL)
		return (e);
	e = xxx_proto_send_host_info_for_set(agent_server->conn, info);
	if (e != NULL)
		return (e);
	return (agent_client_rpc_result(agent_server, 0, ""));
}

char *
agent_client_host_info_replace(struct agent_connection *agent_server,
	char *hostname, struct gfarm_host_info *info)
{
	char *e;

	e = agent_client_rpc_request(agent_server,
		AGENT_PROTO_HOST_INFO_REPLACE, "s", hostname);
	if (e != NULL)
		return (e);
	e = xxx_proto_send_host_info_for_set(agent_server->conn, info);
	if (e != NULL)
		return (e);
	return (agent_client_rpc_result(agent_server, 0, ""));
}

char *
agent_client_host_info_remove(struct agent_connection *agent_server,
	const char *hostname)
{
	return (agent_client_rpc(agent_server, 0,
			AGENT_PROTO_HOST_INFO_REMOVE, "s/", hostname));
}

char *
agent_client_host_info_get_all(struct agent_connection *agent_server,
	int *np, struct gfarm_host_info **infosp)
{
	struct gfarm_host_info *infos;
	char *e;
	int i;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_HOST_INFO_GET_ALL, "/i", np);
	if (e != NULL)
		return (e);
	infos = malloc(sizeof(struct gfarm_host_info) * *np);
	if (infos == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < *np; ++i) {
		e = xxx_proto_recv_host_info(agent_server->conn, &infos[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_host_info_free(&infos[i]);
			free(infos);
			return (e);
		}
	}
	*infosp = infos;
	return (NULL);
}

char *
agent_client_host_info_get_by_name_alias(struct agent_connection *agent_server,
	const char *name_alias, struct gfarm_host_info *info)
{
	char *e;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_HOST_INFO_GET_BY_NAME_ALIAS, "s/", name_alias);
	if (e != NULL)
		return (e);
	return (xxx_proto_recv_host_info(agent_server->conn, info));
}

char *
agent_client_host_info_get_allhost_by_architecture(
	struct agent_connection *agent_server,
	const char *architecture, int *np, struct gfarm_host_info **infosp)
{
	struct gfarm_host_info *infos;
	char *e;
	int i;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_HOST_INFO_GET_ALLHOST_BY_ARCHITECTURE,
		"s/i", architecture, np);
	if (e != NULL)
		return (e);
	infos = malloc(sizeof(struct gfarm_host_info) * *np);
	if (infos == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < *np; ++i) {
		e = xxx_proto_recv_host_info(agent_server->conn, &infos[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_host_info_free(&infos[i]);
			free(infos);
			return (e);
		}
	}
	*infosp = infos;
	return (NULL);
}

/* file section info */

char *
agent_client_file_section_info_get(
	struct agent_connection *agent_server,
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	char *e;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_FILE_SECTION_INFO_GET, "ss/", pathname, section);
	if (e != NULL)
		return (e);
	return (xxx_proto_recv_file_section_info(agent_server->conn, info));
}

char *
agent_client_file_section_info_set(
	struct agent_connection *agent_server,
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	char *e;

	e = agent_client_rpc_request(agent_server,
		AGENT_PROTO_FILE_SECTION_INFO_SET, "ss", pathname, section);
	if (e != NULL)
		return (e);
	e = xxx_proto_send_file_section_info_for_set(agent_server->conn, info);
	if (e != NULL)
		return (e);
	return (agent_client_rpc_result(agent_server, 0, ""));
}

char *
agent_client_file_section_info_replace(
	struct agent_connection *agent_server,
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	char *e;

	e = agent_client_rpc_request(agent_server,
		AGENT_PROTO_FILE_SECTION_INFO_REPLACE, "ss", pathname, section);
	if (e != NULL)
		return (e);
	e = xxx_proto_send_file_section_info_for_set(agent_server->conn, info);
	if (e != NULL)
		return (e);
	return (agent_client_rpc_result(agent_server, 0, ""));
}

char *
agent_client_file_section_info_remove(struct agent_connection *agent_server,
	const char *pathname, const char *section)
{
	return (agent_client_rpc(agent_server, 0,
			AGENT_PROTO_FILE_SECTION_INFO_REMOVE, "ss/",
			pathname, section));
}

char *
agent_client_file_section_info_get_all_by_file(
	struct agent_connection *agent_server,
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	struct gfarm_file_section_info *infos;
	char *e;
	int i;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_FILE_SECTION_INFO_GET_ALL_BY_FILE,
		"s/i", pathname, np);
	if (e != NULL)
		return (e);
	infos = malloc(sizeof(struct gfarm_file_section_info) * *np);
	if (infos == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < *np; ++i) {
		e = xxx_proto_recv_file_section_info(
			agent_server->conn, &infos[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_file_section_info_free(&infos[i]);
			free(infos);
			return (e);
		}
	}
	*infosp = infos;
	return (NULL);
}

/* file section copy info */

char *
agent_client_file_section_copy_info_get(
	struct agent_connection *agent_server,
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	char *e;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_FILE_SECTION_COPY_INFO_GET,
		"sss/", pathname, section, hostname);
	if (e != NULL)
		return (e);
	return (xxx_proto_recv_file_section_copy_info(
			agent_server->conn, info));
}

char *
agent_client_file_section_copy_info_set(
	struct agent_connection *agent_server,
	char *pathname, char *section, char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	return (agent_client_rpc(agent_server, 0,
			AGENT_PROTO_FILE_SECTION_COPY_INFO_SET,
			"sss/", pathname, section, hostname));
}

char *
agent_client_file_section_copy_info_remove(
	struct agent_connection *agent_server,
	const char *pathname, const char *section, const char *hostname)
{
	return (agent_client_rpc(agent_server, 0,
			AGENT_PROTO_FILE_SECTION_COPY_INFO_REMOVE, "sss/",
			pathname, section, hostname));
}

char *
agent_client_file_section_copy_info_get_all_by_file(
	struct agent_connection *agent_server,
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	struct gfarm_file_section_copy_info *infos;
	char *e;
	int i;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_FILE_SECTION_COPY_INFO_GET_ALL_BY_FILE,
		"s/i", pathname, np);
	if (e != NULL)
		return (e);
	infos = malloc(sizeof(struct gfarm_file_section_copy_info) * *np);
	if (infos == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < *np; ++i) {
		e = xxx_proto_recv_file_section_copy_info(
			agent_server->conn, &infos[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_file_section_copy_info_free(&infos[i]);
			free(infos);
			return (e);
		}
	}
	*infosp = infos;
	return (NULL);
}

char *
agent_client_file_section_copy_info_get_all_by_section(
	struct agent_connection *agent_server,
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	struct gfarm_file_section_copy_info *infos;
	char *e;
	int i;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_FILE_SECTION_COPY_INFO_GET_ALL_BY_SECTION,
		"ss/i", pathname, section, np);
	if (e != NULL)
		return (e);
	infos = malloc(sizeof(struct gfarm_file_section_copy_info) * *np);
	if (infos == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < *np; ++i) {
		e = xxx_proto_recv_file_section_copy_info(
			agent_server->conn, &infos[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_file_section_copy_info_free(&infos[i]);
			free(infos);
			return (e);
		}
	}
	*infosp = infos;
	return (NULL);
}

char *
agent_client_file_section_copy_info_get_all_by_host(
	struct agent_connection *agent_server,
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	struct gfarm_file_section_copy_info *infos;
	char *e;
	int i;

	e = agent_client_rpc(agent_server, 0,
		AGENT_PROTO_FILE_SECTION_COPY_INFO_GET_ALL_BY_HOST,
		"s/i", hostname, np);
	if (e != NULL)
		return (e);
	infos = malloc(sizeof(struct gfarm_file_section_copy_info) * *np);
	if (infos == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < *np; ++i) {
		e = xxx_proto_recv_file_section_copy_info(
			agent_server->conn, &infos[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_file_section_copy_info_free(&infos[i]);
			free(infos);
			return (e);
		}
	}
	*infosp = infos;
	return (NULL);
}
