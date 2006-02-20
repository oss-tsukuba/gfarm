/*
 * $Id$
 */

void gfarm_agent_disable(void);

enum agent_type { NO_AGENT, UNIX_DOMAIN, INET };

char *gfarm_agent_type_set(enum agent_type);
char *gfarm_agent_name_set(char *);
char *gfarm_agent_port_set(char *);
char *gfarm_agent_sock_path_set(char *);

char *gfarm_agent_connect(void);
char *gfarm_agent_disconnect(void);

/* for direct access without agent */

char *gfarm_i_path_info_get(const char *, struct gfarm_path_info *);
char *gfarm_i_path_info_set(char *, struct gfarm_path_info *);
char *gfarm_i_path_info_replace(char *,	struct gfarm_path_info *);
char *gfarm_i_path_info_remove(const char *);
char *gfs_i_realpath_canonical(const char *, char **);
char *gfs_i_get_ino(const char *, long *);
char *gfs_i_opendir(const char *, GFS_Dir *);
char *gfs_i_readdir(GFS_Dir, struct gfs_dirent **);
char *gfs_i_closedir(GFS_Dir);
char *gfs_i_dirname(GFS_Dir);
char *gfs_i_seekdir(GFS_Dir, file_offset_t);
char *gfs_i_telldir(GFS_Dir, file_offset_t *);
void gfs_i_uncachedir(void);

/* hostcache */

void gfarm_cache_host_info_free(struct gfarm_host_info *);
char *gfarm_cache_host_info_get(const char *, struct gfarm_host_info *);
char *gfarm_cache_host_info_get_by_name_alias(
	const char *, struct gfarm_host_info *);
char *gfarm_cache_host_info_remove_hostaliases(const char *);
char *gfarm_cache_host_info_set(char *, struct gfarm_host_info *);
char *gfarm_cache_host_info_replace(char *, struct gfarm_host_info *);
char *gfarm_cache_host_info_remove(const char *);
void gfarm_cache_host_info_free_all(int, struct gfarm_host_info *);
char *gfarm_cache_host_info_get_all(int *, struct gfarm_host_info **);
char *gfarm_cache_host_info_get_allhost_by_architecture(const char *,
	int *, struct gfarm_host_info **);
