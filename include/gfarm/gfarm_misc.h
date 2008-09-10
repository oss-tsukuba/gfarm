/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

/*
 * basic types
 */
typedef unsigned char	gfarm_uint8_t;
typedef unsigned short	gfarm_uint16_t;
typedef unsigned int	gfarm_uint32_t;
typedef char		gfarm_int8_t;
typedef short		gfarm_int16_t;
typedef int		gfarm_int32_t;

typedef gfarm_int64_t gfarm_pid_t; /* XXX - need better place */

/*
 * username handling
 */

/* the return value of the following functions should be free(3)ed */
gfarm_error_t gfarm_global_to_local_username(char *, char **);
gfarm_error_t gfarm_local_to_global_username(char *, char **);

/*
 * the return value of the following gfarm_get_*() funtions should not be
 * trusted (maybe forged) on client side, because any user can forge
 * the name by simply setting $USER.
 */
gfarm_error_t gfarm_set_global_username(char *);
gfarm_error_t gfarm_set_local_username(char *);
gfarm_error_t gfarm_set_local_homedir(char *);
char *gfarm_get_global_username(void);
char *gfarm_get_local_username(void);
char *gfarm_get_local_homedir(void);
gfarm_error_t gfarm_set_local_user_for_this_local_account(void);
gfarm_error_t gfarm_set_global_user_for_this_local_account(void);

/*
 * gfarm.conf
 */

/* the following functions are for client, */
/* server/daemon process shouldn't call follows: */
gfarm_error_t gfarm_initialize(int *, char ***);
gfarm_error_t gfarm_terminate(void);
gfarm_error_t gfarm_config_read(void);

/* the following function is for server. */
gfarm_error_t gfarm_server_initialize(void);
gfarm_error_t gfarm_server_terminate(void);
gfarm_error_t gfarm_server_config_read(void);
void gfarm_config_set_filename(char *);

/*
 * GFarm URL and pathname handling
 */

gfarm_error_t gfarm_canonical_path(const char *, char **);
gfarm_error_t gfarm_canonical_path_for_creation(const char *, char **);
gfarm_error_t gfarm_url_make_path(const char *, char **);
gfarm_error_t gfarm_url_make_path_for_creation(const char *, char **);
int gfarm_is_url(const char *);
gfarm_error_t gfarm_path_canonical_to_url(const char *, char **);

const char *gfarm_url_prefix_skip(const char *);
gfarm_error_t gfarm_url_prefix_add(const char *);
const char *gfarm_path_dir_skip(const char *);

extern char GFARM_URL_PREFIX[];
#define GFARM_URL_PREFIX_LENGTH 6

/*
 * Pool Host Scheduling
 * XXX - will be separated to <gfarm_schedule.h>?
 */
void gfarm_schedule_search_mode_use_loadavg(void);
gfarm_error_t gfarm_schedule_search_idle_hosts(int, char **, int, char **);
gfarm_error_t gfarm_schedule_search_idle_acyclic_hosts(int, char **, int *, char **);
gfarm_error_t gfarm_schedule_search_idle_by_all(int, char **);
gfarm_error_t gfarm_schedule_search_idle_by_domainname(const char *, int, char **);
gfarm_error_t gfarm_schedule_search_idle_acyclic_by_domainname(const char *, int *,
	char **);
gfarm_error_t gfarm_schedule_search_idle_by_program(char *, int, char **);
gfarm_error_t gfarm_url_hosts_schedule(const char *, char *, int *, char ***);
gfarm_error_t gfarm_url_hosts_schedule_by_program(char *, char *, char *,
	int *, char ***);
gfarm_error_t gfarm_file_section_host_schedule(char *, char *, char **);
gfarm_error_t gfarm_file_section_host_schedule_by_program(
	char *, char *, char *, char **);
gfarm_error_t gfarm_file_section_host_schedule_with_priority_to_local(char *, char *,
	char **);

/*
 * MetaDB utility
 */
gfarm_error_t gfarm_url_fragment_cleanup(char *, int, char **);
gfarm_error_t gfarm_url_fragment_number(const char *, int *);

/*
 * helper functions for import
 */

gfarm_error_t gfarm_import_fragment_config_read(char *,
	int *, char ***, gfarm_int64_t **, int *);
gfarm_int64_t *gfarm_import_fragment_size_alloc(gfarm_int64_t, int);
gfarm_error_t gfarm_hostlist_read(char *, int *, char ***, int *);

/*
 * hostspec
 */
int gfarm_host_is_in_domain(const char *, const char *);

/*
 * host
 */
struct gfarm_host_info;
gfarm_error_t gfarm_host_info_get_by_if_hostname(const char *,
	struct gfarm_host_info *);
char *gfarm_host_get_self_name(void);
gfarm_error_t gfarm_host_get_canonical_name(const char *, char **, int *);
gfarm_error_t gfarm_host_get_canonical_names(int, char **, char ***, int **);
gfarm_error_t gfarm_host_get_canonical_self_name(char **, int *);
gfarm_error_t gfarm_host_get_self_architecture(char **);
struct sockaddr;
gfarm_error_t gfarm_host_address_get(const char *,
	int, struct sockaddr *, char **);

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

gfarm_error_t gfarm_fixedstrings_dup(int, char **, char **);
void gfarm_strings_free_deeply(int, char **);
int gfarm_strarray_length(char **);
char **gfarm_strarray_dup(char **);
void gfarm_strarray_free(char **);
int gfarm_attach_debugger(void);
