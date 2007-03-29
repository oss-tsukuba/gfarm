/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

#define GFARM_VERSION		"1.4.99"
#define GFARM_MAJOR_VERSION	1
#define GFARM_MINOR_VERSION	4
#define GFARM_PATCH_VERSION	99	/* 99 means development version */
#define GFARM_MICRO_VERSION	0

#define GFARM_REVISION	"$Revision$"

const char *gfarm_version_string(void);
int gfarm_major_version(void);
int gfarm_minor_version(void);
int gfarm_patch_version(void);
int gfarm_micro_version(void);
const char *gfarm_revision_string(void);

/*
 * basic types
 */
typedef unsigned char	gfarm_uint8_t;
typedef unsigned short	gfarm_uint16_t;
typedef unsigned int	gfarm_uint32_t;
typedef char		gfarm_int8_t;
typedef short		gfarm_int16_t;
typedef int		gfarm_int32_t;

/*
 * username handling
 */

/* the return value of the following functions should be free(3)ed */
char *gfarm_global_to_local_username(char *, char **);
char *gfarm_local_to_global_username(char *, char **);

/*
 * the return value of the following gfarm_get_*() funtions should not be
 * trusted (maybe forged) on client side, because any user can forge
 * the name by simply setting $USER.
 */
char *gfarm_set_global_username(char *);
char *gfarm_get_global_username(void);
char *gfarm_set_local_username(char *);
char *gfarm_get_local_username(void);
char *gfarm_set_local_homedir(char *);
char *gfarm_get_local_homedir(void);
char *gfarm_set_local_user_for_this_local_account(void);
char *gfarm_set_global_user_for_this_local_account(void);

/*
 * gfarm.conf
 */

/* the following functions are for client, */
/* server/daemon process shouldn't call follows: */
char *gfarm_initialize(int *, char ***);
int gfarm_initialized(void);
char *gfarm_terminate(void);
char *gfarm_config_read(void);

/* the following function is for server. */
char *gfarm_server_initialize(void);
char *gfarm_server_terminate(void);
char *gfarm_server_config_read(void);
void gfarm_config_set_filename(char *);

char *gfarm_strtoken(char **, char **);

/*
 * GFarm URL and pathname handling
 */

char *gfarm_canonical_path(const char *, char **);
char *gfarm_canonical_path_for_creation(const char *, char **);
char *gfarm_url_make_path(const char *, char **);
char *gfarm_url_make_path_for_creation(const char *, char **);
int gfarm_is_url(const char *);
char *gfarm_path_canonical_to_url(const char *, char **);

#if 0
char *gfarm_url_make_localized_path(char *, char **);
char *gfarm_url_make_localized_file_fragment_path(char *, int, char **);
#endif
char *gfarm_path_section(const char *, const char *, char **);
char *gfarm_full_path_file_section(char *, char *, char *, char **);
char *gfarm_path_localize(char *, char **);
char *gfarm_path_localize_file_section(char *, char *, char **);
char *gfarm_path_localize_file_fragment(char *, int, char **);
const char *gfarm_url_prefix_skip(const char *);
char *gfarm_url_prefix_add(const char *);
const char *gfarm_path_dir_skip(const char *);

extern char GFARM_URL_PREFIX[];
#define GFARM_URL_PREFIX_LENGTH 6

/*
 * Pool Host Scheduling
 * XXX - will be separated to <gfarm_schedule.h>?
 */
void gfarm_schedule_search_mode_use_loadavg(void);
char *gfarm_schedule_search_idle_hosts(int, char **, int, char **);
char *gfarm_schedule_search_idle_hosts_to_write(int, char **, int, char **);
char *gfarm_schedule_search_idle_acyclic_hosts_to_write(
	int, char **, int *, char **);
char *gfarm_schedule_search_idle_acyclic_hosts(int, char **, int *, char **);
char *gfarm_schedule_search_idle_by_all(int, char **);
char *gfarm_schedule_search_idle_by_all_to_write(int, char **);
char *gfarm_schedule_search_idle_by_domainname(const char *, int, char **);
char *gfarm_schedule_search_idle_by_domainname_to_write(
	const char *, int, char **);
char *gfarm_schedule_search_idle_acyclic_by_domainname(const char *, int *,
	char **);
char *gfarm_schedule_search_idle_acyclic_by_domainname_to_write(
	const char *, int *, char **);
char *gfarm_schedule_search_idle_by_program(char *, int, char **);
char *gfarm_schedule_search_idle_by_program_to_write(char *, int, char **);
char *gfarm_url_hosts_schedule(const char *, char *, int *, char ***);
char *gfarm_url_hosts_schedule_by_program(char *, char *, char *,
	int *, char ***);
char *gfarm_file_section_host_schedule(char *, char *, char **);
char *gfarm_file_section_host_schedule_to_write(char *, char *, char **);
char *gfarm_file_section_host_schedule_by_program(char *, char *, char *,
	char **);
char *gfarm_file_section_host_schedule_by_program_to_write(
	char *, char *, char *, char **);
char *gfarm_file_section_host_schedule_with_priority_to_local(char *, char *,
	char **);
char *gfarm_file_section_host_schedule_with_priority_to_local_to_write(
	char *, char *, char **);

/*
 * MetaDB utility
 */
char *gfarm_url_fragment_cleanup(char *, int, char **);
char *gfarm_url_fragment_number(const char *, int *);

/*
 * helper functions for import
 */

char *gfarm_import_fragment_config_read(char *,
	int *, char ***, file_offset_t **, int *);
file_offset_t *gfarm_import_fragment_size_alloc(file_offset_t, int);
char *gfarm_hostlist_read(char *, int *, char ***, int *);

/*
 * hostspec
 */
int gfarm_host_is_in_domain(const char *, const char *);

/*
 * host
 */
struct gfarm_host_info;
char *gfarm_host_info_get_by_if_hostname(const char *,
	struct gfarm_host_info *);
char *gfarm_host_get_self_name(void);
char *gfarm_host_get_canonical_name(const char *, char **);
char *gfarm_host_get_canonical_names(int, char **, char ***);
char *gfarm_host_get_canonical_self_name(char **);
char *gfarm_host_get_self_architecture(char **);
struct sockaddr;
char *gfarm_host_address_get(const char *, int, struct sockaddr *, char **);

/*
 * Miscellaneous
 */
#define GFARM_INT32STRLEN 11	/* max strlen(sprintf(s, "%d", int32)) */
#define GFARM_INT64STRLEN 22	/* max strlen(sprintf(s, "%lld", int64)) */

#define GFARM_ARRAY_LENGTH(array)	(sizeof(array)/sizeof(array[0]))

#define GFARM_MALLOC(p)		((p) = malloc(sizeof(*(p))))
#define GFARM_CALLOC_ARRAY(p,n)	((p) = gfarm_calloc_array((n), sizeof(*(p))))
#define GFARM_MALLOC_ARRAY(p,n)	((p) = gfarm_malloc_array((n), sizeof(*(p))))
#define GFARM_REALLOC_ARRAY(d,s,n)	((d) = gfarm_realloc_array((s), (n), sizeof(*(d))))

void *gfarm_calloc_array(size_t, size_t);
void *gfarm_malloc_array(size_t, size_t);
void *gfarm_realloc_array(void *, size_t, size_t);

char *gfarm_fixedstrings_dup(int, char **, char *const *);
void gfarm_strings_free_deeply(int, char **);
int gfarm_strarray_length(char *const *);
char **gfarm_strarray_dup(char *const *);
void gfarm_strarray_free(char **);
int gfarm_attach_debugger(void);

void gfarm_set_record_atime(int);
