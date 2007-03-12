#include <gfarm/gfarm_config.h> /* file_offset_t */

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
void gfs_hook_terminate(void);

int gfs_hook_open_flags_gfarmize(int);

int gfs_hook_num_gfs_files(void);
void gfs_hook_reserve_fd(void);
void gfs_hook_release_fd(void);
int gfs_hook_insert_gfs_file(struct gfs_file *);
int gfs_hook_insert_gfs_dir(struct gfs_dir *, char *);
unsigned char gfs_hook_gfs_file_type(int);
char *gfs_hook_clear_gfs_file(int);
void gfs_hook_unset_calc_digest_all(void);
char *gfs_hook_flush_all(void);
char *gfs_hook_close_all(void);

void *gfs_hook_is_open(int);
void gfs_hook_set_suspended_gfs_dirent(int, struct gfs_dirent *,file_offset_t);
struct gfs_dirent *gfs_hook_get_suspended_gfs_dirent(int, file_offset_t *);
struct gfs_stat *gfs_hook_get_gfs_stat(int);
char *gfs_hook_get_gfs_url(int);
int gfs_hook_set_cwd_is_gfarm(int);
int gfs_hook_get_cwd_is_gfarm(void);
int gfs_hook_is_url(const char *, char **);
char *gfs_hook_get_prefix(char *, size_t);
int __syscall_close(int);

enum gfs_hook_file_view {
	local_view,
	index_view,
	global_view,
	section_view
};

enum gfs_hook_file_view gfs_hook_get_current_view(void);
int gfs_hook_get_current_index(void);
int gfs_hook_get_current_nfrags(void);
char *gfs_hook_get_current_section(void);

struct dirent;
struct dirent64;
struct stat;
struct stat64;
int gfs_hook_syscall_open(const char *, int, mode_t);
int gfs_hook_syscall_getdents(int, struct dirent *, size_t);
int gfs_hook_syscall_getdents64(int, struct dirent64 *, size_t);
ssize_t gfs_hook_syscall_pread(int, void *, size_t, off_t);
ssize_t gfs_hook_syscall_pwrite(int, const void *, size_t, off_t);
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

#define GFS_DEV		((dev_t)-1)
#define GFS_BLKSIZE	8192
#define GFS_NLINK_DIR	32000	/* dummy value of st_nlink for directories */
#define STAT_BLKSIZ	512	/* for st_blocks */
