/* convert symbol to string literal */
#define STRING(symbol)	#symbol
#define S(symbol)	STRING(symbol)

#ifdef DEBUG
#include <stdio.h>
extern int gfarm_node, gfarm_nnode;
#define _gfs_hook_debug(x) x
#define _gfs_hook_debug_v(x)
#else
#define _gfs_hook_debug(x)
#define _gfs_hook_debug_v(x)
#endif

struct gfs_file;
struct gfs_dir;
struct gfs_dirent;

char *gfs_hook_initialize(void);

int gfs_hook_open_flags_gfarmize(int);

int gfs_hook_insert_gfs_file(struct gfs_file *);
int gfs_hook_insert_gfs_dir(struct gfs_dir *, char *);
unsigned char gfs_hook_gfs_file_type(int);
char *gfs_hook_clear_gfs_file(int);

void *gfs_hook_is_open(int);
char *gfs_hook_add_creating_file(struct gfs_file *);
struct gfs_file *gfs_hook_is_now_creating(const char *);
void gfs_hook_delete_creating_file(struct gfs_file *);
void gfs_hook_inc_readcount(int);
int gfs_hook_is_read(int);
void gfs_hook_set_suspended_gfs_dirent(int, struct gfs_dirent *);
struct gfs_dirent *gfs_hook_get_suspended_gfs_dirent(int);
struct gfs_stat *gfs_hook_get_gfs_stat(int);
char *gfs_hook_get_gfs_canonical_path(int);
int gfs_hook_set_cwd_is_gfarm(int);
int gfs_hook_get_cwd_is_gfarm();
int gfs_hook_is_url(const char *, char **);
char *gfs_hook_get_prefix(char *, size_t);
int __syscall_close(int);

enum gfs_hook_file_view {
	local_view,
	index_view,
	global_view,
	section_view
};

enum gfs_hook_file_view gfs_hook_get_current_view();
int gfs_hook_get_current_index();
int gfs_hook_get_current_nfrags();
char *gfs_hook_get_current_section();

struct dirent;
struct dirent64;
struct stat;
struct stat64;
int gfs_hook_syscall_open(const char *, int, mode_t);
int gfs_hook_syscall_getdents(int, struct dirent *, size_t);
int gfs_hook_syscall_getdents64(int, struct dirent64 *, size_t);
int gfs_hook_syscall_xstat(int, const char *, struct stat *);
int gfs_hook_syscall_lxstat(int, const char *, struct stat *);
int gfs_hook_syscall_fxstat(int, int, struct stat *);
int gfs_hook_syscall_xstat64(int, const char *, struct stat64 *);
int gfs_hook_syscall_lxstat64(int, const char *, struct stat64 *);
int gfs_hook_syscall_fxstat64(int, int, struct stat64 *);
char *gfs_hook_syscall_getcwd(char *, size_t);

struct _gfs_file_descriptor;
struct _gfs_file_descriptor *gfs_hook_dup_descriptor(int);
void gfs_hook_set_descriptor(int, struct _gfs_file_descriptor *);

#ifdef _LARGEFILE64_SOURCE
off64_t gfs_hook_syscall_lseek64(int, off64_t, int);
#endif

#define GFS_DEV	(dev_t)-1
