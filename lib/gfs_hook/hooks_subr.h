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

char *gfs_hook_initialize(void);

int gfs_hook_insert_gfs_file(struct gfs_file *);
int gfs_hook_clear_gfs_file(int);

int gfs_hook_insert_filedes(int, struct gfs_file *);
void gfs_hook_inc_refcount(int);

struct gfs_file *gfs_hook_is_open(int);
int gfs_hook_is_url(const char *, char **, char **);
int __syscall_close(int);

enum gfs_hook_file_view {
	local_view,
	index_view,
	global_view
} _gfs_hook_default_view;

extern int _gfs_hook_index;
extern int _gfs_hook_num_fragments;

struct stat;
struct stat64;
int gfs_hook_syscall_open(const char *, int, mode_t);
int gfs_hook_syscall_xstat(int, const char *, struct stat *);
int gfs_hook_syscall_lxstat(int, const char *, struct stat *);
int gfs_hook_syscall_fxstat(int, int, struct stat *);
int gfs_hook_syscall_xstat64(int, const char *, struct stat64 *);
int gfs_hook_syscall_lxstat64(int, const char *, struct stat64 *);
int gfs_hook_syscall_fxstat64(int, int, struct stat64 *);
