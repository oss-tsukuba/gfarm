/*
 * $Id$
 */

/*
 * basic types
 */

typedef gfarm_int64_t gfarm_time_t;

struct gfarm_timespec {
	gfarm_time_t tv_sec;
	gfarm_int32_t tv_nsec;
};

typedef gfarm_int64_t gfarm_off_t;
typedef gfarm_uint64_t gfarm_ino_t;

typedef gfarm_uint32_t gfarm_mode_t;
#define	GFARM_S_ALLPERM	0007777		/* all access permissions */
#define	GFARM_S_IFMT	0170000		/* type of file mask */
#define	GFARM_S_IFDIR	0040000		/* directory */
#define	GFARM_S_IFREG	0100000		/* regular file */
#if 0
#define	GFARM_S_IFLNK	0120000		/* symbolic link */
#endif
#define	GFARM_S_ISDIR(m) (((m) & GFARM_S_IFMT) == GFARM_S_IFDIR)
#define	GFARM_S_ISREG(m) (((m) & GFARM_S_IFMT) == GFARM_S_IFREG)
#if 0
#define	GFARM_S_ISLNK(m) (((m) & GFARM_S_IFMT) == GFARM_S_IFLNK)
#endif

#define	GFARM_S_IS_PROGRAM(m) \
	(GFARM_S_ISREG(m) && ((m) & 0111) != 0)


struct gfs_stat {
	gfarm_ino_t st_ino;
	gfarm_uint64_t st_gen;
	gfarm_mode_t st_mode;
	gfarm_uint64_t st_nlink;
	char *st_user;
	char *st_group;
	gfarm_off_t st_size;
	gfarm_uint64_t st_ncopy;
	struct gfarm_timespec st_atimespec;
	struct gfarm_timespec st_mtimespec;
	struct gfarm_timespec st_ctimespec;
};

/*
 * gfs_file ops
 */
typedef struct gfs_file *GFS_File;

gfarm_error_t gfs_pio_open(const char *, int, GFS_File *);
gfarm_error_t gfs_pio_create(const char *, int, gfarm_mode_t mode, GFS_File *);

#define GFARM_FILE_RDONLY		0
#define GFARM_FILE_WRONLY		1
#define GFARM_FILE_RDWR			2
#define GFARM_FILE_ACCMODE		3	/* RD/WR/RDWR mode mask */
/* #define GFARM_FILE_CREATE		0x00000200 */ /* internal use only */
#define GFARM_FILE_TRUNC		0x00000400
#define GFARM_FILE_APPEND		0x00000800
#define GFARM_FILE_EXCLUSIVE		0x00001000
/* the followings are just hints */
#define GFARM_FILE_SEQUENTIAL		0x01000000
#define GFARM_FILE_REPLICATE		0x02000000
#define GFARM_FILE_NOT_REPLICATE	0x04000000
#define GFARM_FILE_NOT_RETRY		0x08000000
#define GFARM_FILE_UNBUFFERED		0x10000000

char *gfs_pio_truncate(GFS_File, gfarm_off_t);

gfarm_error_t gfs_pio_set_local(int, int);
gfarm_error_t gfs_pio_set_local_check(void);
gfarm_error_t gfs_pio_get_node_rank(int *);
gfarm_error_t gfs_pio_get_node_size(int *);

gfarm_error_t gfs_pio_set_view_local(GFS_File, int);
gfarm_error_t gfs_pio_set_view_index(GFS_File, int, int, char *, int);
gfarm_error_t gfs_pio_set_view_section(GFS_File, const char *, char *, int);
gfarm_error_t gfs_pio_set_view_global(GFS_File, int);
/* as total fragment number */
#define GFARM_FILE_DONTCARE		(-1)

gfarm_error_t gfs_pio_close(GFS_File);

int gfs_pio_eof(GFS_File);
gfarm_error_t gfs_pio_error(GFS_File);
void gfs_pio_clearerr(GFS_File);
int gfs_pio_fileno(GFS_File);
gfarm_error_t gfs_pio_get_nfragment(GFS_File, int *);

gfarm_error_t gfs_pio_flush(GFS_File);

gfarm_error_t gfs_pio_seek(GFS_File, gfarm_off_t, int, gfarm_off_t *);
gfarm_error_t gfs_pio_read(GFS_File, void *, int, int *);
gfarm_error_t gfs_pio_write(GFS_File, const void *, int, int *);
gfarm_error_t gfs_pio_sync(GFS_File);
gfarm_error_t gfs_pio_datasync(GFS_File);

int gfs_pio_getc(GFS_File);
int gfs_pio_ungetc(GFS_File, int);
gfarm_error_t gfs_pio_putc(GFS_File, int);
gfarm_error_t gfs_pio_puts(GFS_File, const char *);
gfarm_error_t gfs_pio_gets(GFS_File, char *, size_t);
gfarm_error_t gfs_pio_getline(GFS_File, char *, size_t, int *);
gfarm_error_t gfs_pio_putline(GFS_File, const char *);
gfarm_error_t gfs_pio_readline(GFS_File, char **, size_t *, size_t *);
gfarm_error_t gfs_pio_readdelim(GFS_File, char **, size_t *, size_t *,
	const char *, size_t);

/*
 *  For legacy code
 */
gfarm_error_t gfs_pio_set_fragment_info_local(char *, char *, char *);

/*
 * Meta operations
 */

gfarm_error_t gfs_unlink(const char *);
gfarm_error_t gfs_unlink_section(const char *, const char *);
gfarm_error_t gfs_unlink_section_replica(const char *, const char *,
	int, char **, int);
gfarm_error_t gfs_unlink_replicas_on_host(const char *,	const char *, int);
gfarm_error_t gfs_mkdir(const char *, gfarm_mode_t);
gfarm_error_t gfs_rmdir(const char *);
gfarm_error_t gfs_chdir(const char *);
gfarm_error_t gfs_getcwd(char *, int);
gfarm_error_t gfs_chown(const char *, char *, char *);
gfarm_error_t gfs_chmod(const char *, gfarm_mode_t);
gfarm_error_t gfs_fchmod(GFS_File, gfarm_mode_t);
gfarm_error_t gfs_utimes(const char *, const struct gfarm_timespec *);
gfarm_error_t gfs_rename(const char *, const char *);

gfarm_error_t gfs_stat(const char *, struct gfs_stat *);
gfarm_error_t gfs_stat_section(const char *, const char *, struct gfs_stat *);
gfarm_error_t gfs_stat_index(char *, int, struct gfs_stat *);
gfarm_error_t gfs_fstat(GFS_File, struct gfs_stat *);
void gfs_stat_free(struct gfs_stat *);

gfarm_error_t gfs_access(const char *, int);
#define GFS_F_OK	0
#define GFS_X_OK	1
#define GFS_W_OK	2
#define GFS_R_OK	4

#define	GFS_MAXNAMLEN	255
struct gfs_dirent {
	gfarm_ino_t d_fileno;
	unsigned short d_reclen;
	unsigned char d_type;
	unsigned char d_namlen;
	char d_name[GFS_MAXNAMLEN + 1];
};

/* File types */
#define	GFS_DT_UNKNOWN	 0 /* gfs_hook.c depends on it that this is 0 */
#define	GFS_DT_DIR	 4
#define	GFS_DT_REG	 8

typedef struct gfs_dir *GFS_Dir;
gfarm_error_t gfs_opendir(const char *, GFS_Dir *);
gfarm_error_t gfs_readdir(GFS_Dir, struct gfs_dirent **);
gfarm_error_t gfs_closedir(GFS_Dir);
gfarm_error_t gfs_dirname(GFS_Dir);
gfarm_error_t gfs_seekdir(GFS_Dir, gfarm_off_t);
gfarm_error_t gfs_telldir(GFS_Dir, gfarm_off_t *);
gfarm_error_t gfs_realpath(const char *, char **);
gfarm_error_t gfs_realpath(const char *, char **);

/*
 * execution
 */

gfarm_error_t gfs_execve(const char *, char *const [], char *const []);
