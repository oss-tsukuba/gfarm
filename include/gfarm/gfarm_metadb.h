/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

/* ---------------------------------------- */

/*
 * GFarm host table:
 * (hostname) -> (architecture, ncpu)
 */

struct gfarm_host_info {
	char *hostname;
	int nhostaliases;
	char **hostaliases;
	char *architecture;
	int ncpu;
};
#define GFARM_HOST_INFO_NCPU_NOT_SET	(-1)

void gfarm_host_info_free(struct gfarm_host_info *);
char *gfarm_host_info_get(const char *, struct gfarm_host_info *);
char *gfarm_host_info_set(const char *, const struct gfarm_host_info *);
char *gfarm_host_info_replace(const char *, const struct gfarm_host_info *);
char *gfarm_host_info_remove(const char *);
char *gfarm_host_info_remove_hostaliases(const char *);
void gfarm_host_info_free_all(int, struct gfarm_host_info *);
char *gfarm_host_info_get_all(int *, struct gfarm_host_info **);
char *gfarm_host_info_get_by_name_alias(const char *,
	struct gfarm_host_info *);
char *gfarm_host_info_get_allhost_by_architecture(const char *,
	int *, struct gfarm_host_info **);

/* convenience function */
char *gfarm_host_info_get_architecture_by_host(const char *);

/* ---------------------------------------- */

/*
 * GFarm filesystem directory / i-node information:
 * (pathname) -> (status)
 */

struct gfarm_path_info {
	char *pathname;
	/*
	 * In meta DB, the following fields differ from reality:
	 *	status.st_size: always 0
	 *	status.st_nsections: 0, if the path is not fragmented file.
	 */
	struct gfs_stat status;
};

void gfarm_path_info_free(struct gfarm_path_info *);
char *gfarm_path_info_get(const char *, struct gfarm_path_info *);
char *gfarm_path_info_set(const char *, const struct gfarm_path_info *);
char *gfarm_path_info_replace(const char *, const struct gfarm_path_info *);
char *gfarm_path_info_remove(const char *);

/* XXX - this is for a stopgap implementation of gfs_opendir() */
char *gfarm_path_info_get_all_foreach(
	void (*)(void *, struct gfarm_path_info *), void *);

void gfarm_path_free_all(int, char **);
char *gfarm_path_get_all_children(const char *, int *, char ***);

/* extended attribute */
struct gfarm_path_info_xattr {
	char *pathname;
	char *xattr;
};

void gfarm_path_info_xattr_free(struct gfarm_path_info_xattr *);
char *gfarm_path_info_xattr_get(const char *, struct gfarm_path_info_xattr *);
char *gfarm_path_info_xattr_set(const struct gfarm_path_info_xattr *);
char *gfarm_path_info_xattr_replace(const struct gfarm_path_info_xattr *);
char *gfarm_path_info_xattr_remove(const char *);

/* ---------------------------------------- */

/*
 * GFarmFile section database:
 * (pathname, section) -> (section_info)
 *
 * the `section' means:
 *	serial number for fragments
 *	architecture for programs
 */

struct gfarm_file_section_info {
	char *pathname;
	char *section;
	file_offset_t filesize;
	char *checksum_type;
	char *checksum;
};

void gfarm_file_section_info_free(struct gfarm_file_section_info *);
char *gfarm_file_section_info_get(
	const char *, const char *, struct gfarm_file_section_info *);
char *gfarm_file_section_info_set(
	const char *, const char *, const struct gfarm_file_section_info *);
char *gfarm_file_section_info_replace(
	const char *, const char *, const struct gfarm_file_section_info *);
char *gfarm_file_section_info_remove(const char *, const char *);
void gfarm_file_section_info_free_all(int, struct gfarm_file_section_info *);
char *gfarm_file_section_info_get_all_by_file(
	const char *, int *, struct gfarm_file_section_info **);
char *gfarm_file_section_info_get_sorted_all_serial_by_file(
	const char *, int *, struct gfarm_file_section_info **);
char *gfarm_file_section_info_remove_all_by_file(const char *);

/* convenience function */
int gfarm_file_section_info_does_exist(const char *, const char *);

/* ---------------------------------------- */

/*
 * GFarm file_section delivery map:
 * (GFarmFile, section, hostname) -> ()
 */

struct gfarm_file_section_copy_info {
	char *pathname;
	char *section;
	char *hostname;
};

void gfarm_file_section_copy_info_free(struct gfarm_file_section_copy_info *);
char *gfarm_file_section_copy_info_get(
	const char *, const char *, const char *,
	struct gfarm_file_section_copy_info *);
char *gfarm_file_section_copy_info_set(
	const char *, const char *, const char *,
	const struct gfarm_file_section_copy_info *);
char *gfarm_file_section_copy_info_remove(
	const char *, const char *, const char *);
void gfarm_file_section_copy_info_free_all(
	int, struct gfarm_file_section_copy_info *);
char *gfarm_file_section_copy_info_get_all_by_file(const char *, int *,
	struct gfarm_file_section_copy_info **);
char *gfarm_file_section_copy_info_remove_all_by_file(const char *);
char *gfarm_file_section_copy_info_get_all_by_section(const char *,
	const char *, int *, struct gfarm_file_section_copy_info **);

char *gfarm_file_section_copy_info_remove_all_by_section(
	const char *, const char *);
char *gfarm_file_section_copy_info_get_all_by_host(
	const char *, int *, struct gfarm_file_section_copy_info **);
char *gfarm_file_section_copy_info_remove_all_by_host(const char *);

/* convenience function */
int gfarm_file_section_copy_info_does_exist(
	const char *, const char *, const char *);

/* ---------------------------------------- */

#if 0 /* GFarmFile history isn't actually used yet */

/*
 * GFarmFile history database:
 * (pathname) -> (history)
 */

struct gfarm_file_history {
	char *program;		/* program which created the file */
	char **input_files;	/* input_files passed to the program */
	char *parameter;	/* parameter string passed to the program */
};

void gfarm_file_history_free(struct gfarm_file_history *);
char *gfarm_file_history_get(const char *, struct gfarm_file_history *);
char *gfarm_file_history_set(const char *, const struct gfarm_file_history *);

void gfarm_file_history_free_allfile(int, char **);
/* get GFarmFiles which were created by the program */
char *gfarm_file_history_get_allfile_by_program(const char *, int *, char ***);
/* get GFarmFiles which were created from the file as a input */
char *gfarm_file_history_get_allfile_by_file(const char *, int *, char ***);

#endif /* GFarmFile history isn't actually used yet */
