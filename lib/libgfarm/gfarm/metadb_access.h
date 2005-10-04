/*
 * Metadata server operations for LDAP
 *
 * $Id$
 */

char *gfarm_metadb_initialize(void);
char *gfarm_metadb_terminate(void);
void gfarm_metadb_share_connection(void);

char *gfarm_metadb_host_info_get(const char *, struct gfarm_host_info *);
char *gfarm_metadb_host_info_remove_hostaliases(const char *);
char *gfarm_metadb_host_info_set(char *, struct gfarm_host_info *);
char *gfarm_metadb_host_info_replace(char *, struct gfarm_host_info *);
char *gfarm_metadb_host_info_remove(const char *hostname);
char *gfarm_metadb_host_info_get_all(int *, struct gfarm_host_info **);
char *gfarm_metadb_host_info_get_by_name_alias(const char *,
	struct gfarm_host_info *);
char *gfarm_metadb_host_info_get_allhost_by_architecture(const char *,
	int *, struct gfarm_host_info **);

char *gfarm_metadb_path_info_get(const char *, struct gfarm_path_info *);
char *gfarm_metadb_path_info_set(char *, struct gfarm_path_info *);
char *gfarm_metadb_path_info_replace(char *, struct gfarm_path_info *);
char *gfarm_metadb_path_info_remove(const char *);
char *gfarm_metadb_path_info_get_all_foreach(
	void (*)(void *, struct gfarm_path_info *), void *);

char *gfarm_metadb_file_section_info_get(
	const char *, const char *, struct gfarm_file_section_info *);
char *gfarm_metadb_file_section_info_set(
	char *, char *, struct gfarm_file_section_info *);
char *gfarm_metadb_file_section_info_replace(
	char *, char *, struct gfarm_file_section_info *);
char *gfarm_metadb_file_section_info_remove(const char *, const char *);
char *gfarm_metadb_file_section_info_get_all_by_file(
	const char *, int *, struct gfarm_file_section_info **);

char *gfarm_metadb_file_section_copy_info_get(
	const char *, const char *, const char *,
	struct gfarm_file_section_copy_info *);
char *gfarm_metadb_file_section_copy_info_set(
	char *, char *, char *, struct gfarm_file_section_copy_info *);
char *gfarm_metadb_file_section_copy_info_remove(
	const char *, const char *, const char *);
char *gfarm_metadb_file_section_copy_info_get_all_by_file(const char *, int *,
	struct gfarm_file_section_copy_info **);
char *gfarm_metadb_file_section_copy_info_get_all_by_section(const char *,
	const char *, int *, struct gfarm_file_section_copy_info **);
char *gfarm_metadb_file_section_copy_info_get_all_by_host(
	const char *, int *, struct gfarm_file_section_copy_info **);
