/*
 * $Id$
 */

struct agent_connection;

char *agent_client_connect_unix(
	struct sockaddr_un *, struct agent_connection **);
char *agent_client_connect_inet(
	const char *, struct sockaddr *, struct agent_connection **);
char *agent_client_disconnect(struct agent_connection *);

/* agent_client RPC */

char *agent_client_path_info_get(
	struct agent_connection *, const char *, struct gfarm_path_info *);
char *agent_client_path_info_set(
	struct agent_connection *, const char *,
	const struct gfarm_path_info *);
char *agent_client_path_info_replace(
	struct agent_connection *, const char *,
	const struct gfarm_path_info *);
char *agent_client_path_info_remove(struct agent_connection *, const char *);
char *agent_client_realpath_canonical(
	struct agent_connection *, const char *, char **);
char *agent_client_get_ino(
	struct agent_connection *, const char *, gfarm_uint32_t *);
char *agent_client_opendir(struct agent_connection *, const char *,
	gfarm_int32_t *);
char *agent_client_readdir(
	struct agent_connection *, gfarm_int32_t, struct gfs_dirent **);
char *agent_client_closedir(struct agent_connection *, gfarm_int32_t);
char *agent_client_dirname(struct agent_connection *, gfarm_int32_t);
char *agent_client_seekdir(struct agent_connection *,
	gfarm_int32_t, file_offset_t);
char *agent_client_telldir(struct agent_connection *,
	gfarm_int32_t, file_offset_t*);
char *agent_client_uncachedir(struct agent_connection *);

char *agent_client_host_info_get(
	struct agent_connection *, const char *, struct gfarm_host_info *);
char *agent_client_host_info_remove_hostaliases(
	struct agent_connection *, const char *);
char *agent_client_host_info_set(
	struct agent_connection *, const char *,
	const struct gfarm_host_info *);
char *agent_client_host_info_replace(
	struct agent_connection *, const char *,
	const struct gfarm_host_info *);
char *agent_client_host_info_remove(struct agent_connection *, const char *);
char *agent_client_host_info_get_all(
	struct agent_connection *, int *, struct gfarm_host_info **);
char *agent_client_host_info_get_by_name_alias(
	struct agent_connection *, const char *, struct gfarm_host_info *);
char *agent_client_host_info_get_allhost_by_architecture(
	struct agent_connection *,
	const char *, int *, struct gfarm_host_info **);

char *agent_client_path_info_xattr_get(
	struct agent_connection *,
	const char *, struct gfarm_path_info_xattr *);
char *agent_client_path_info_xattr_set(
	struct agent_connection *, const struct gfarm_path_info_xattr *);
char *agent_client_path_info_xattr_replace(
	struct agent_connection *, const struct gfarm_path_info_xattr *);
char *agent_client_path_info_xattr_remove(
	struct agent_connection *, const char *);

char *agent_client_file_section_info_get(
	struct agent_connection *, const char *, const char *,
	struct gfarm_file_section_info *);
char *agent_client_file_section_info_set(
	struct agent_connection *, const char *, const char *,
	const struct gfarm_file_section_info *);
char *agent_client_file_section_info_replace(
	struct agent_connection *, const char *, const char *,
	const struct gfarm_file_section_info *);
char *agent_client_file_section_info_remove(
	struct agent_connection *, const char *, const char *);
char *agent_client_file_section_info_get_all_by_file(
	struct agent_connection *, const char *, int *,
	struct gfarm_file_section_info **);

char *agent_client_file_section_copy_info_get(
	struct agent_connection *, const char *, const char *, const char *,
	struct gfarm_file_section_copy_info *);
char *agent_client_file_section_copy_info_set(
	struct agent_connection *, const char *, const char *, const char *,
	const struct gfarm_file_section_copy_info *);
char *agent_client_file_section_copy_info_remove(
	struct agent_connection *, const char *, const char *, const char *);
char *agent_client_file_section_copy_info_get_all_by_file(
	struct agent_connection *, const char *, int *,
	struct gfarm_file_section_copy_info **);
char *agent_client_file_section_copy_info_get_all_by_section(
	struct agent_connection *, const char *, const char *, int *,
	struct gfarm_file_section_copy_info **);
char *agent_client_file_section_copy_info_get_all_by_host(
	struct agent_connection *, const char *, int *,
	struct gfarm_file_section_copy_info **);
