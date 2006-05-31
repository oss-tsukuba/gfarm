/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <gfarm/gfarm.h>
#include "metadb_access.h"
#include "gfs_misc.h"
#include "agent_client.h"
#include "agent_wrap.h"

extern char *gfarm_client_initialize(int *, char ***);
extern char *gfarm_client_terminate(void);

static int gfarm_agent_enabled = 1;
static struct agent_connection *agent_server;
static pid_t agent_pid;

static char *gfarm_agent_name;
static int gfarm_agent_port;
static char *gfarm_agent_sock_path;
static enum agent_type gfarm_agent_type;

char GFARM_AGENT_ERR_NO_AGENT[] = "no agent connection";

void
gfarm_agent_disable(void)
{
	gfarm_agent_enabled = 0;
}

static char *
gfarm_agent_check(void)
{
	if (!gfarm_agent_enabled)
		return (GFARM_AGENT_ERR_NO_AGENT);

	if (agent_server != NULL) {
		if (agent_pid == getpid())
			return (NULL);
		else
			gfarm_agent_disconnect();
	}
	return (gfarm_agent_connect());
}

char *
gfarm_agent_type_set(enum agent_type type)
{
	if (gfarm_agent_type == NO_AGENT)
		gfarm_agent_type = type;
	else if (gfarm_agent_type != type)
		return ("agent connection type already set");
	return (NULL);
}

enum agent_type
gfarm_agent_type_get()
{
	return (gfarm_agent_type);
}

void
gfarm_agent_name_clear()
{
	if (gfarm_agent_name != NULL)
		free(gfarm_agent_name);
	gfarm_agent_name = NULL;	
}

char *
gfarm_agent_name_set(char *name)
{
	if (gfarm_agent_name != NULL)
		return (NULL);

	gfarm_agent_name = strdup(name);
	if (gfarm_agent_name == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (gfarm_agent_type_set(INET));
}

void
gfarm_agent_port_clear()
{
	if (gfarm_agent_port != 0)
		gfarm_agent_port = 0;
}

char *
gfarm_agent_port_set(char *port)
{
	if (gfarm_agent_port != 0)
		return (NULL);

	gfarm_agent_port = atoi(port);

	return (gfarm_agent_type_set(INET));
}

int
gfarm_agent_port_get()
{
	return (gfarm_agent_port);
}

void
gfarm_agent_sock_path_clear()
{
	if (gfarm_agent_sock_path != NULL)
		free(gfarm_agent_sock_path);
	gfarm_agent_sock_path = NULL;
}

char *
gfarm_agent_sock_path_set(char *path)
{
	if (gfarm_agent_sock_path != NULL)
		return (NULL);

	gfarm_agent_sock_path = strdup(path);
	if (gfarm_agent_sock_path == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (gfarm_agent_type_set(UNIX_DOMAIN));
}

static char *
gfarm_agent_eval_env()
{
	char *env, *e = NULL;

	env = getenv("GFARM_AGENT_SOCK");
	if (env != NULL)
		e = gfarm_agent_sock_path_set(env);
	if (e != NULL)
		return (e);

	env = getenv("GFARM_AGENT_HOST");
	if (env != NULL)
		e = gfarm_agent_name_set(env);
	if (e != NULL)
		return (e);

	env = getenv("GFARM_AGENT_PORT");
	if (env != NULL)
		e = gfarm_agent_port_set(env);

	return (e);
}

char *
gfarm_agent_connect()
{
	struct sockaddr_un unix_addr;
	struct sockaddr inet_addr;
	char *e, *if_hostname;
	size_t len;

	if (agent_server != NULL)
		return ("already connected");

	e = gfarm_agent_eval_env();
	if (e != NULL)
		return (e);
	agent_pid = getpid();

	switch (gfarm_agent_type) {
	case UNIX_DOMAIN:
		len = strlen(gfarm_agent_sock_path);
		if (len >= sizeof(unix_addr.sun_path))
			return ("$GFARM_AGENT_SOCK: too long pathname");

		memset(&unix_addr, 0, sizeof(unix_addr));
		unix_addr.sun_family = AF_UNIX;
		strcpy(unix_addr.sun_path, gfarm_agent_sock_path);
		e = agent_client_connect_unix(&unix_addr, &agent_server);
		break;
	case INET:
		e = gfarm_host_address_get(
			gfarm_agent_name, gfarm_agent_port,
			&inet_addr, &if_hostname);
		if (e != NULL)
			return (e);
		e = agent_client_connect_inet(
			if_hostname, &inet_addr, &agent_server);
		free(if_hostname);
		break;
	default:
		e = GFARM_AGENT_ERR_NO_AGENT;
	}
	return (e);
}

char *
gfarm_agent_disconnect()
{
	char *e;

	if (agent_server == NULL)
		return ("already disconnected");

	e = agent_client_disconnect(agent_server);
	/* reset agent_server even in error case */
	agent_server = NULL;
	agent_pid = 0;

	return (e);
}

char *
gfarm_initialize(int *argc, char ***argv)
{
	if (gfarm_agent_enabled)
		(void)gfarm_agent_connect();
	return (gfarm_client_initialize(argc, argv));
}

char *
gfarm_terminate(void)
{
	char *e;

	if (gfarm_agent_check() == NULL)
		(void)gfarm_agent_disconnect();
	e = gfarm_client_terminate();
	(void)gfarm_metadb_terminate();

	return (e);
}

char *
gfarm_path_info_get(const char *path, struct gfarm_path_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_path_info_get(agent_server, path, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_i_path_info_get(path, info));
}

char *
gfarm_path_info_set(char *path, struct gfarm_path_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_path_info_set(agent_server, path, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_i_path_info_set(path, info));
}

char *
gfarm_path_info_replace(char *path, struct gfarm_path_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_path_info_replace(
				agent_server, path, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_i_path_info_replace(path, info));
}

char *
gfarm_path_info_remove(const char *path)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_path_info_remove(agent_server, path);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_i_path_info_remove(path));
}

char *
gfs_realpath_canonical(const char *path, char **abspathp)
{
	char *e, cwd[PATH_MAX + 1];

	/* cwd is maintained by a client */
	if (path[0] != '/') {
		e = gfs_getcwd(cwd, sizeof(cwd));
		if (e != NULL)
			return (e);
		if (strlen(cwd) + 1 + strlen(path) + 1 > sizeof(cwd))
			return (GFARM_ERR_NUMERICAL_RESULT_OUT_OF_RANGE);
		strcat(cwd, "/");
		strcat(cwd, path);
		path = cwd;
	}
	if (gfarm_agent_check() == NULL) {
		e = agent_client_realpath_canonical(
			agent_server, path, abspathp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfs_i_realpath_canonical(path, abspathp));
}

char *
gfs_get_ino(const char *canonic_path, long *inop)
{
	char *e;
	gfarm_int32_t ip;

	if (gfarm_agent_check() == NULL) {
		e = agent_client_get_ino(agent_server, canonic_path, &ip);
		if (e == NULL)
			*inop = (long)ip;
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfs_i_get_ino(canonic_path, inop));
}

char *
gfs_opendir(const char *path, GFS_Dir *dirp)
{
	struct gfarm_path_info pi;
	char *e, cwd[PATH_MAX + 1], *canonic_path;

	path = gfarm_url_prefix_skip(path);

	/* cwd is maintained by a client */
	if (path[0] != '/') {
		e = gfs_getcwd(cwd, sizeof(cwd));
		if (e != NULL)
			return (e);
		if (strlen(cwd) + 1 + strlen(path) + 1 > sizeof(cwd))
			return (GFARM_ERR_NUMERICAL_RESULT_OUT_OF_RANGE);
		strcat(cwd, "/");
		strcat(cwd, path);
		path = cwd;
	}

	/* access control */
	e = gfarm_canonical_path(path, &canonic_path);
	if (e == NULL) {
		e = gfarm_path_info_get(canonic_path, &pi);
		if (e == NULL) {
			e = gfarm_path_info_access(&pi, R_OK);
			gfarm_path_info_free(&pi);
		}
		free(canonic_path);
	}
	if (e != NULL)
		return (e);

	if (gfarm_agent_check() == NULL) {
		e = agent_client_opendir(agent_server, path, dirp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfs_i_opendir(path, dirp));
}

char *
gfs_readdir(GFS_Dir dir, struct gfs_dirent **entry)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_readdir(agent_server, dir, entry);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfs_i_readdir(dir, entry));
}

char *
gfs_closedir(GFS_Dir dir)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_closedir(agent_server, dir);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfs_i_closedir(dir));
}

char *
gfs_dirname(GFS_Dir dir)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_dirname(agent_server, dir);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfs_i_dirname(dir));
}

char *
gfs_seekdir(GFS_Dir dir, file_offset_t off)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_seekdir(agent_server, dir, off);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfs_i_seekdir(dir, off));
}

char *
gfs_telldir(GFS_Dir dir, file_offset_t *offp)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_telldir(agent_server, dir, offp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfs_i_telldir(dir, offp));
}

void
gfs_uncachedir(void)
{
	if (gfarm_agent_check() == NULL)
		agent_client_uncachedir(agent_server);
		/* XXX - no way to know reconnection failure */
	else
		gfs_i_uncachedir();
}

/* host info */

void
gfarm_host_info_free(struct gfarm_host_info *info)
{
	gfarm_cache_host_info_free(info);
}

char *
gfarm_host_info_get(const char *hostname, struct gfarm_host_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_host_info_get(
				agent_server, hostname, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_cache_host_info_get(hostname, info));
}

char *
gfarm_host_info_remove_hostaliases(const char *hostname)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_host_info_remove_hostaliases(
				agent_server, hostname);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_cache_host_info_remove_hostaliases(hostname));
}

char *
gfarm_host_info_set(char *hostname, struct gfarm_host_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_host_info_set(
				agent_server, hostname, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_cache_host_info_set(hostname, info));
}

char *
gfarm_host_info_replace(char *hostname, struct gfarm_host_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_host_info_replace(
				agent_server, hostname, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_cache_host_info_replace(hostname, info));
}

char *
gfarm_host_info_remove(const char *hostname)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_host_info_remove(agent_server, hostname);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_cache_host_info_remove(hostname));
}

void
gfarm_host_info_free_all(int n, struct gfarm_host_info *infos)
{
	gfarm_cache_host_info_free_all(n, infos);
}

char *
gfarm_host_info_get_all(int *np, struct gfarm_host_info **infosp)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_host_info_get_all(
				agent_server, np, infosp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_cache_host_info_get_all(np, infosp));
}

char *
gfarm_host_info_get_by_name_alias(
	const char *alias, struct gfarm_host_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_host_info_get_by_name_alias(
				agent_server, alias, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_cache_host_info_get_by_name_alias(alias, info));
}

char *
gfarm_host_info_get_allhost_by_architecture(const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_host_info_get_allhost_by_architecture(
				agent_server, architecture, np, infosp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_cache_host_info_get_allhost_by_architecture(
			architecture, np, infosp));
}

char *
gfarm_host_info_get_architecture_by_host(const char *hostname)
{
	char *error, *arch;
	struct gfarm_host_info info;

	error = gfarm_host_info_get(hostname, &info);
	if (error != NULL)
		return (NULL);

	arch = strdup(info.architecture);
	gfarm_host_info_free(&info);
	if (arch == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (arch);
}

/* file section info */

char *
gfarm_file_section_info_get(
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_info_get(
				agent_server, pathname, section, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_info_get(pathname, section, info));
}

char *
gfarm_file_section_info_set(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_info_set(
				agent_server, pathname, section, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_info_set(pathname, section, info));
}

char *
gfarm_file_section_info_replace(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_info_replace(
				agent_server, pathname, section, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_info_replace(
			pathname, section, info));
}

char *
gfarm_file_section_info_remove(const char *pathname, const char *section)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_info_remove(
				agent_server, pathname, section);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_info_remove(pathname, section));
}

char *
gfarm_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_info_get_all_by_file(
				agent_server, pathname, np, infosp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_info_get_all_by_file(
			pathname, np, infosp));
}

static int
gfarm_file_section_info_compare_serial(const void *d, const void *s)
{
	const struct gfarm_file_section_info *df = d, *sf = s;

	return (atoi(df->section) - atoi(sf->section));
}

char *
gfarm_file_section_info_get_sorted_all_serial_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	int n;
	struct gfarm_file_section_info *infos;
	char *error = gfarm_file_section_info_get_all_by_file(
		pathname, &n, &infos);

	if (error != NULL)
		return (error);

	qsort(infos, n, sizeof(infos[0]),
	      gfarm_file_section_info_compare_serial);
	*np = n;
	*infosp = infos;
	return (NULL);
}

char *
gfarm_file_section_info_remove_all_by_file(const char *pathname)
{
	char *error, *error_save;
	int i, n;
	struct gfarm_file_section_info *infos;

	error = gfarm_file_section_info_get_all_by_file(pathname,
	    &n, &infos);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (NULL);
		return (error);
	}

	/*
	 * remove GfarmFileSection's
	 */
	error_save = NULL;
	for (i = 0; i < n; i++) {
		error = gfarm_file_section_info_remove(pathname,
		    infos[i].section);
		if (error != NULL && error != GFARM_ERR_NO_SUCH_OBJECT)
			error_save = error;
	}
	gfarm_file_section_info_free_all(n, infos);

	/* XXX - do not remove parent GFarmPath here */

	return (error_save);
}

int
gfarm_file_section_info_does_exist(
	const char *pathname,
	const char *section)
{
	struct gfarm_file_section_info info;

	if (gfarm_file_section_info_get(pathname, section, &info)
	    != NULL)
		return (0);
	gfarm_file_section_info_free(&info);
	return (1);
}

/* file section copy info */

char *
gfarm_file_section_copy_info_get(
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_copy_info_get(agent_server,
				pathname, section, hostname, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_copy_info_get(
			pathname, section, hostname, info));
}

char *
gfarm_file_section_copy_info_set(
	char *pathname,
	char *section,
	char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_copy_info_set(agent_server,
				pathname, section, hostname, info);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_copy_info_set(
			pathname, section, hostname, info));
}

char *
gfarm_file_section_copy_info_remove(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_copy_info_remove(
				agent_server, pathname, section, hostname);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_copy_info_remove(
			pathname, section, hostname));
}

char *
gfarm_file_section_copy_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_copy_info_get_all_by_file(
				agent_server, pathname, np, infosp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_copy_info_get_all_by_file(
			pathname, np, infosp));
}

char *
gfarm_file_section_copy_info_remove_all_by_file(
	const char *pathname)
{
	char *error, *error_save;
	int i, n;
	struct gfarm_file_section_copy_info *infos;

	error = gfarm_file_section_copy_info_get_all_by_file(pathname,
	    &n, &infos);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (NULL);
		return (error);
	}

	/*
	 * remove GFarmFileSectionCopies
	 */
	error_save = NULL;
	for (i = 0; i < n; i++) {
		error = gfarm_file_section_copy_info_remove(pathname,
		    infos[i].section, infos[i].hostname);
		if (error != NULL && error != GFARM_ERR_NO_SUCH_OBJECT)
			error_save = error;
	}
	gfarm_file_section_copy_info_free_all(n, infos);

	return (error_save);
}

char *
gfarm_file_section_copy_info_get_all_by_section(
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	if (gfarm_agent_check() == NULL) {
		char *e;
		e = agent_client_file_section_copy_info_get_all_by_section(
			agent_server, pathname, section, np, infosp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_copy_info_get_all_by_section(
			pathname, section, np, infosp));
}

char *
gfarm_file_section_copy_info_remove_all_by_section(
	const char *pathname,
	const char *section)
{
	char *error, *error_save;
	int i, n;
	struct gfarm_file_section_copy_info *infos;

	error = gfarm_file_section_copy_info_get_all_by_section(
	    pathname, section,
	    &n, &infos);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (NULL);
		return (error);
	}

	/*
	 * remove GfarmFileSectionCopies
	 */
	error_save = NULL;
	for (i = 0; i < n; i++) {
		error = gfarm_file_section_copy_info_remove(pathname,
		    section, infos[i].hostname);
		if (error != NULL && error != GFARM_ERR_NO_SUCH_OBJECT)
			error_save = error;
	}
	gfarm_file_section_copy_info_free_all(n, infos);

	return (error_save);
}

char *
gfarm_file_section_copy_info_get_all_by_host(
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	if (gfarm_agent_check() == NULL) {
		char *e = agent_client_file_section_copy_info_get_all_by_host(
				agent_server, hostname, np, infosp);
		if (e != GFARM_ERR_CONNECTION_REFUSED)
			return (e);
		/* reconnection failed, connect to metadb directly */
	}
	return (gfarm_metadb_file_section_copy_info_get_all_by_host(
			hostname, np, infosp));
}

char *
gfarm_file_section_copy_info_remove_all_by_host(
	const char *hostname)
{
	char *error, *error_save;
	int i, n;
	struct gfarm_file_section_copy_info *infos;

	error = gfarm_file_section_copy_info_get_all_by_host(hostname,
	    &n, &infos);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (NULL);
		return (error);
	}

	/*
	 * remove GfarmFileSectionCopy's
	 */
	error_save = NULL;
	for (i = 0; i < n; i++) {
		error = gfarm_file_section_copy_info_remove(
		    infos[i].pathname, infos[i].section,
		    hostname);
		if (error != NULL && error != GFARM_ERR_NO_SUCH_OBJECT)
			error_save = error;
	}
	gfarm_file_section_copy_info_free_all(n, infos);

	return (error_save);
}

int
gfarm_file_section_copy_info_does_exist(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	struct gfarm_file_section_copy_info info;

	if (gfarm_file_section_copy_info_get(pathname, section,
	    hostname, &info) != NULL)
		return (0);
	gfarm_file_section_copy_info_free(&info);
	return (1);
}
