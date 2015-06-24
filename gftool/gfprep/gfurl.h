/*
 * $Id$
 */

struct gfurl_stat {
	gfarm_int32_t nlink;
	gfarm_int32_t mode;
	gfarm_int64_t size;
	struct gfarm_timespec mtime;
};

struct gfurl_functions {
	gfarm_error_t (*lstat)(const char *, struct gfurl_stat *);
	gfarm_error_t (*exist)(const char *);
	gfarm_error_t (*lutimens)(const char *, struct gfarm_timespec *,
				  struct gfarm_timespec *);
	gfarm_error_t (*chmod)(const char *, int);
	gfarm_error_t (*mkdir)(const char *, int, int);
	gfarm_error_t (*rmdir)(const char *);
	gfarm_error_t (*unlink)(const char *);
	gfarm_error_t (*readlink)(const char *, char **);
	gfarm_error_t (*symlink)(const char *, char *);
};

struct gfurl;
typedef struct gfurl *GFURL;

#define GFURL_SCHEME_UNKNOWN 0
#define GFURL_SCHEME_LOCAL   1
#define GFURL_SCHEME_GFARM   2
#define GFURL_SCHEME_HPSS    3

/* Local */
extern const char GFURL_LOCAL_PREFIX[];
#define GFURL_LOCAL_PREFIX_LENGTH 5 /* file: */
int gfurl_path_is_local(const char *);
extern const struct gfurl_functions gfurl_func_local;

/* Gfarm */
int gfurl_path_is_gfarm(const char *);
extern const struct gfurl_functions gfurl_func_gfarm;

/* HPSS */
extern const char GFURL_HPSS_PREFIX[];
#define GFURL_HPSS_PREFIX_LENGTH 5 /* hpss: */
int gfurl_path_is_hpss(const char *);
extern const struct gfurl_functions gfurl_func_hpss;
int gfurl_hpss_is_available(void);

/* Common */

/* need <stdarg.h> */
int gfurl_vasprintf(char **, const char *, va_list) GFLOG_PRINTF_ARG(2, 0);
int gfurl_asprintf(char **, const char *, ...) GFLOG_PRINTF_ARG(2, 3);
char *gfurl_asprintf2(const char *, ...) GFLOG_PRINTF_ARG(1, 2);

char *gfurl_path_dirname(const char *);
char *gfurl_path_basename(const char *);
char *gfurl_path_combine(const char *, const char *);

GFURL gfurl_init(const char *);
GFURL gfurl_dup(GFURL);
void gfurl_free(GFURL);

const char *gfurl_url(GFURL);
const char *gfurl_epath(GFURL);

GFURL gfurl_parent(GFURL);
GFURL gfurl_child(GFURL, const char *);
int gfurl_is_rootdir(GFURL);

int gfurl_is_same_gfmd(GFURL, GFURL);
int gfurl_is_same_dir(GFURL, GFURL);

int gfurl_is_local(GFURL);
int gfurl_is_gfarm(GFURL);
int gfurl_is_hpss(GFURL);

gfarm_error_t gfurl_lstat(GFURL, struct gfurl_stat *);
gfarm_error_t gfurl_exist(GFURL);
int gfurl_stat_file_type(struct gfurl_stat *);
gfarm_error_t gfurl_lutimens(GFURL, struct gfarm_timespec *,
	struct gfarm_timespec *);
gfarm_error_t gfurl_set_mtime(GFURL, struct gfarm_timespec *);
gfarm_error_t gfurl_chmod(GFURL, int);
gfarm_error_t gfurl_mkdir(GFURL, int, int);
gfarm_error_t gfurl_rmdir(GFURL);
gfarm_error_t gfurl_unlink(GFURL);
gfarm_error_t gfurl_readlink(GFURL, char **);
gfarm_error_t gfurl_symlink(GFURL, char *);
