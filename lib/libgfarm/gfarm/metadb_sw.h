/*
 * Metadata access switch for internal implementation
 *
 * $Id$
 */

struct gfarm_metadb_internal_ops {
	char *(*initialize)(void);
	char *(*terminate)(void);

	char *(*host_info_get)(const char *, struct gfarm_host_info *);
	char *(*host_info_remove_hostaliases)(const char *);
	char *(*host_info_set)(char *, struct gfarm_host_info *);
	char *(*host_info_replace)(char *, struct gfarm_host_info *);
	char *(*host_info_remove)(const char *hostname);
	char *(*host_info_get_all)(int *, struct gfarm_host_info **);
	char *(*host_info_get_by_name_alias)(const char *,
		struct gfarm_host_info *);
	char *(*host_info_get_allhost_by_architecture)(const char *,
		int *, struct gfarm_host_info **);

	char *(*path_info_get)(const char *, struct gfarm_path_info *);
	char *(*path_info_set)(char *, struct gfarm_path_info *);
	char *(*path_info_replace)(char *, struct gfarm_path_info *);
	char *(*path_info_remove)(const char *);
	char *(*path_info_get_all_foreach)(
		void (*)(void *, struct gfarm_path_info *), void *);

	char *(*file_section_info_get)(
		const char *, const char *, struct gfarm_file_section_info *);
	char *(*file_section_info_set)(
		char *, char *, struct gfarm_file_section_info *);
	char *(*file_section_info_replace)(
		char *, char *, struct gfarm_file_section_info *);
	char *(*file_section_info_remove)(const char *, const char *);
	char *(*file_section_info_get_all_by_file)(
		const char *, int *, struct gfarm_file_section_info **);

	char *(*file_section_copy_info_get)(
		const char *, const char *, const char *,
		struct gfarm_file_section_copy_info *);
	char *(*file_section_copy_info_set)(
		char *, char *, char *, struct gfarm_file_section_copy_info *);
	char *(*file_section_copy_info_remove)(
		const char *, const char *, const char *);
	char *(*file_section_copy_info_get_all_by_file)(const char *, int *,
		struct gfarm_file_section_copy_info **);
	char *(*file_section_copy_info_get_all_by_section)(const char *,
		const char *, int *, struct gfarm_file_section_copy_info **);
	char *(*file_section_copy_info_get_all_by_host)(
		const char *, int *, struct gfarm_file_section_copy_info **);
};

extern const struct gfarm_metadb_internal_ops gfarm_ldap_metadb_ops;
extern const struct gfarm_metadb_internal_ops gfarm_pgsql_metadb_ops;

int gfarm_does_own_metadb_connection(void);

struct gfarm_base_generic_info_ops {
	size_t info_size;
	void (*free)(void *info);
	void (*clear)(void *info);
	int (*validate)(void *info);
};

extern const struct gfarm_base_generic_info_ops
	gfarm_base_host_info_ops,
	gfarm_base_path_info_ops,
	gfarm_base_file_section_info_ops,
	gfarm_base_file_section_copy_info_ops;

