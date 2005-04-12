/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <gfarm/gfarm.h>
#include "agent_client.h"
#include "agent_wrap.h"

extern char *gfarm_client_initialize(int *, char ***);
extern char *gfarm_client_terminate(void);

static int gfarm_agent_enabled = 1;
static struct agent_connection *agent_server;
static pid_t agent_pid;

char GFARM_AGENT_ERR_NO_AGENT[] = "no agent connection";

void
gfarm_agent_disable(void)
{
	gfarm_agent_enabled = 0;
}

char *gfarm_agent_connect(char *);
char *gfarm_agent_disconnect(void);

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
	return (gfarm_agent_connect(NULL));
}

char *
gfarm_agent_connect(char *path)
{
	struct sockaddr_un peer_addr;

	if (agent_server != NULL)
		return ("already connected");

	if (path == NULL)
		path = getenv("GFARM_AGENT_SOCK");
	if (path == NULL)
		return (GFARM_AGENT_ERR_NO_AGENT);

	memset(&peer_addr, 0, sizeof(peer_addr));
	peer_addr.sun_family = AF_UNIX;
	strcpy(peer_addr.sun_path, path);

	agent_pid = getpid();
	return (agent_client_connect(&peer_addr, &agent_server));
}

char *
gfarm_agent_disconnect()
{
	char *e;

	if (agent_server == NULL)
		return ("already disconnected");

	e = agent_client_disconnect(agent_server);
	if (e == NULL) {
		agent_server = NULL;
		agent_pid = 0;
	}
	return (e);
}

char *
gfarm_initialize(int *argc, char ***argv)
{
	if (gfarm_agent_enabled)
		(void)gfarm_agent_connect(NULL);
	return (gfarm_client_initialize(argc, argv));
}

char *
gfarm_terminate(void)
{
	gfarm_agent_disconnect();
	return (gfarm_client_terminate());
}

char *
gfarm_path_info_get(const char *path, struct gfarm_path_info *info)
{
	if (gfarm_agent_check() == NULL)
		return (agent_client_path_info_get(agent_server, path, info));
	else
		return (gfarm_i_path_info_get(path, info));
}

char *
gfarm_path_info_set(char *path, struct gfarm_path_info *info)
{
	if (gfarm_agent_check() == NULL)
		return (agent_client_path_info_set(agent_server, path, info));
	else
		return (gfarm_i_path_info_set(path, info));
}

char *
gfarm_path_info_replace(char *path, struct gfarm_path_info *info)
{
	if (gfarm_agent_check() == NULL)
		return (agent_client_path_info_replace(
				agent_server, path, info));
	else
		return (gfarm_i_path_info_replace(path, info));
}

char *
gfarm_path_info_remove(const char *path)
{
	if (gfarm_agent_check() == NULL)
		return (agent_client_path_info_remove(agent_server, path));
	else
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
	if (gfarm_agent_check() == NULL)
		return (agent_client_realpath_canonical(
				agent_server, path, abspathp));
	else
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
		return (e);
	}
	else
		return (gfs_i_get_ino(canonic_path, inop));
}

char *
gfs_opendir(const char *path, GFS_Dir *dirp)
{
	char *e, cwd[PATH_MAX + 1];

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
	if (gfarm_agent_check() == NULL)
		return (agent_client_opendir(agent_server, path, dirp));
	else
		return (gfs_i_opendir(path, dirp));
}

char *
gfs_readdir(GFS_Dir dir, struct gfs_dirent **entry)
{
	if (gfarm_agent_check() == NULL)
		return (agent_client_readdir(agent_server, dir, entry));
	else
		return (gfs_i_readdir(dir, entry));
}

char *
gfs_closedir(GFS_Dir dir)
{
	if (gfarm_agent_check() == NULL)
		return (agent_client_closedir(agent_server, dir));
	else
		return (gfs_i_closedir(dir));
}

char *
gfs_dirname(GFS_Dir dir)
{
	if (gfarm_agent_check() == NULL)
		return (agent_client_dirname(agent_server, dir));
	else
		return (gfs_i_dirname(dir));
}

void
gfs_uncachedir(void)
{
	if (gfarm_agent_check() == NULL)
		agent_client_uncachedir(agent_server);
	else
		gfs_i_uncachedir();
}
