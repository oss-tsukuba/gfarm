/* convert symbol to string literal */
#define STRING(symbol)	#symbol
#define S(symbol)	STRING(symbol)

#ifdef DEBUG
#include <stdio.h>
#define _gfs_hook_debug(x) x
#define _gfs_hook_debug_v(x)
#else
#define _gfs_hook_debug(x)
#define _gfs_hook_debug_v(x)
#endif

char *gfs_hook_initialize(void);

int gfs_hook_insert_gfs_file(GFS_File);
int gfs_hook_clear_gfs_file(int);

int gfs_hook_insert_filedes(int, GFS_File);
void gfs_hook_inc_refcount(int);

GFS_File gfs_hook_is_open(int);
int gfs_hook_is_url(const char *, char **, char **);
int __syscall_close(int);

enum gfs_hook_file_view {
	local_view,
	index_view,
	global_view
} _gfs_hook_default_view;

extern int _gfs_hook_index;
extern int _gfs_hook_num_fragments;
