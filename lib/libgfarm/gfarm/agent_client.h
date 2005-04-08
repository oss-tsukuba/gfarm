/*
 * $Id$
 */

struct agent_connection;

char *agent_client_connect(struct sockaddr_un *, struct agent_connection **);
char *agent_client_disconnect(struct agent_connection *);

/* agent_client RPC */

char *agent_client_rpc(struct agent_connection *, int, int, char *, ...);

char *agent_client_path_info_get(
	struct agent_connection *, const char *, struct gfarm_path_info *);
char *agent_client_path_info_set(
	struct agent_connection *, char *, struct gfarm_path_info *);
char *agent_client_path_info_replace(
	struct agent_connection *, char *, struct gfarm_path_info *);
char *agent_client_path_info_remove(struct agent_connection *, const char *);
char *agent_client_realpath_canonical(
	struct agent_connection *, const char *, char **);
char *agent_client_get_ino(
	struct agent_connection *, const char *, gfarm_int32_t *);
char *agent_client_opendir(struct agent_connection *, const char *, GFS_Dir *);
char *agent_client_readdir(
	struct agent_connection *, GFS_Dir, struct gfs_dirent **);
char *agent_client_closedir(struct agent_connection *, GFS_Dir);
char *agent_client_dirname(struct agent_connection *, GFS_Dir);
char *agent_client_uncachedir(struct agent_connection *);
