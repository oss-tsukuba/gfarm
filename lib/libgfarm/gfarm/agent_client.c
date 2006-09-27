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
#include "sockopt.h"
#include "agent_proto.h"
#include "agent_client.h"
#include "agent_wrap.h"

#ifndef	va_copy
#define	va_copy(dst, src)	((dst) = (src))
#endif

struct agent_connection {
	struct xxx_connection *conn;
};

static char *
agent_client_connection0_unix(
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
	e = xxx_socket_connection_new(sock, &agent_server->conn);
	if (e != NULL) {
		close(sock);
		return (e);
	}
	return (NULL);
}

char *
agent_client_connect_unix(struct sockaddr_un *peer_addr,
	struct agent_connection **agent_serverp)
{
	struct agent_connection *agent_server;
	char *e;

	GFARM_MALLOC(agent_server);
	if (agent_server == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = agent_client_connection0_unix(peer_addr, agent_server);
	if (e != NULL) {
		free(agent_server);
		return (e);
	}
	*agent_serverp = agent_server;
	return (NULL);
}

static char *
agent_client_connection0_inet(const char *if_hostname,
	struct sockaddr *peer_addr, struct agent_connection *agent_server)
{
	char *e;
	int sock;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		return (gfarm_errno_to_error(errno));
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	/* XXX - how to report setsockopt(2) failure ? */
	gfarm_sockopt_apply_by_name_addr(sock, if_hostname, peer_addr);
	if (connect(sock, peer_addr, sizeof(*peer_addr)) < 0) {
		e = gfarm_errno_to_error(errno);
		close(sock);
		return (e);
	}
	e = xxx_socket_connection_new(sock, &agent_server->conn);
	if (e != NULL) {
		close(sock);
		return (e);
	}
	return (NULL);
}

char *
agent_client_connect_inet(const char *hostname,
	struct sockaddr *peer_addr, struct agent_connection **agent_serverp)
{
	struct agent_connection *agent_server;
	char *e;

	GFARM_MALLOC(agent_server);
	if (agent_server == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = agent_client_connection0_inet(hostname, peer_addr, agent_server);
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
	va_list ap, save_ap;
	char *e, *save_format;

	va_start(ap, format);
	save_format = format;
	va_copy(save_ap, ap);
retry:
	e = xxx_proto_vrpc_request(agent_server->conn, command, &format, &ap);
	if (e == GFARM_ERR_BROKEN_PIPE || e == GFARM_ERR_UNEXPECTED_EOF) {
		gfarm_agent_disconnect();
		e = gfarm_agent_connect();
		if (e == NULL) {
			format = save_format;
			va_end(ap);
			va_copy(ap, save_ap);
			goto retry;
		}
	}
	va_end(ap);
	va_end(save_ap);
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
	va_list ap, save_ap;
	char *e, *save_format;
	int error;

	va_start(ap, format);
	save_format = format;
	va_copy(save_ap, ap);
retry:
	e = xxx_proto_vrpc(agent_server->conn, just,
			   command, &error, &format, &ap);
	if (e == GFARM_ERR_BROKEN_PIPE || e == GFARM_ERR_UNEXPECTED_EOF) {
		gfarm_agent_disconnect();
		e = gfarm_agent_connect();
		if (e == NULL) {
			format = save_format;
			va_end(ap);
			va_copy(ap, save_ap);
			goto retry;
		}
	}
	va_end(ap);
	va_end(save_ap);

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
	const char *path, gfarm_int32_t *dirdescp)
{
	return (agent_client_rpc(agent_server, 0, AGENT_PROTO_OPENDIR, "s/i",
	    path, dirdescp));
}

char *
agent_client_readdir(struct agent_connection *agent_server,
	gfarm_int32_t dirdesc, struct gfs_dirent **entry)
{
	char *e, *name;
	static struct gfs_dirent de;
	gfarm_uint32_t ino;

	e = agent_client_rpc(
		agent_server, 0, AGENT_PROTO_READDIR,
		"i/ihccs", dirdesc,
		&ino, &de.d_reclen,
		&de.d_type, &de.d_namlen, &name);
	if (e == NULL) {
		de.d_fileno = ino;
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
agent_client_closedir(struct agent_connection *agent_server,
	gfarm_int32_t dirdesc)
{
	return (agent_client_rpc(
			agent_server, 0, AGENT_PROTO_CLOSEDIR,
			"i/", dirdesc));
}

char *
agent_client_dirname(struct agent_connection *agent_server,
	gfarm_int32_t dirdesc)
{
	char *e, *n;
	static char name[GFS_MAXNAMLEN];

	e = agent_client_rpc(
		agent_server, 0, AGENT_PROTO_DIRNAME,
		"i/s", dirdesc, &n);
	if (e == NULL) {
		strcpy(name, n);
		free(n);
		return (name);
	}
	else
		return (NULL);
}

char *
agent_client_seekdir(struct agent_connection *agent_server,
	gfarm_int32_t dirdesc, file_offset_t off)
{
	return (agent_client_rpc(agent_server, 0, AGENT_PROTO_SEEKDIR, "io/",
	    dirdesc, off));
}

char *
agent_client_telldir(struct agent_connection *agent_server,
	gfarm_int32_t dirdesc, file_offset_t *offp)
{
	return (agent_client_rpc(agent_server, 0, AGENT_PROTO_TELLDIR, "i/o",
	    dirdesc, offp));
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
	GFARM_MALLOC_ARRAY(infos, *np);
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
	GFARM_MALLOC_ARRAY(infos, *np);
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
	GFARM_MALLOC_ARRAY(infos, *np);
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
	GFARM_MALLOC_ARRAY(infos, *np);
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
	GFARM_MALLOC_ARRAY(infos, *np);
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
	GFARM_MALLOC_ARRAY(infos, *np);
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
