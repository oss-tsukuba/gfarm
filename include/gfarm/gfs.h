/*
 * $Id$
 */

/*
 * basic types
 */

struct gfarm_timespec {
	unsigned int tv_sec;
	unsigned int tv_nsec;
};

typedef unsigned int gfarm_mode_t;
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

#define	GFARM_S_IS_FRAGMENTED_FILE(m) \
	(GFARM_S_ISREG(m) && ((m) & 0111) == 0)
#define	GFARM_S_IS_PROGRAM(m) \
	(GFARM_S_ISREG(m) && ((m) & 0111) != 0)


struct gfs_stat {
	long st_ino;
	gfarm_mode_t st_mode;
	char *st_user;
	char *st_group;
	struct gfarm_timespec st_atimespec;
	struct gfarm_timespec st_mtimespec;
	struct gfarm_timespec st_ctimespec;
	file_offset_t st_size;
	int st_nsections;
};

/*
 * gfs_file ops
 */
typedef struct gfs_file *GFS_File;

char *gfs_pio_open(const char *, int, GFS_File *);
char *gfs_pio_create(const char *, int, gfarm_mode_t mode, GFS_File *);

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

char *gfs_pio_truncate(GFS_File, file_offset_t);

char *gfs_pio_set_local(int, int);
char *gfs_pio_set_local_check(void);
char *gfs_pio_get_node_rank(int *);
char *gfs_pio_get_node_size(int *);

char *gfs_pio_set_view_local(GFS_File, int);
char *gfs_pio_set_view_index(GFS_File, int, int, char *, int);
char *gfs_pio_set_view_section(GFS_File, const char *, char *, int);
char *gfs_pio_set_view_global(GFS_File, int);
/* as total fragment number */
#define GFARM_FILE_DONTCARE		(-1)

char *gfs_pio_close(GFS_File);

int gfs_pio_eof(GFS_File);
char *gfs_pio_error(GFS_File);
void gfs_pio_clearerr(GFS_File);
int gfs_pio_fileno(GFS_File);
char *gfs_pio_get_nfragment(GFS_File, int *);

char *gfs_pio_flush(GFS_File);

char *gfs_pio_seek(GFS_File, file_offset_t, int, file_offset_t *);
char *gfs_pio_read(GFS_File, void *, int, int *);
char *gfs_pio_write(GFS_File, const void *, int, int *);

int gfs_pio_getc(GFS_File);
int gfs_pio_ungetc(GFS_File, int);
char *gfs_pio_putc(GFS_File, int);
char *gfs_pio_puts(GFS_File, const char *);
char *gfs_pio_gets(GFS_File, char *, size_t);
char *gfs_pio_getline(GFS_File, char *, size_t, int *);
char *gfs_pio_putline(GFS_File, const char *);
char *gfs_pio_readline(GFS_File, char **, size_t *, size_t *);
char *gfs_pio_readdelim(GFS_File, char **, size_t *, size_t *,
	const char *, size_t);

/*
 *  For legacy code
 */
char *gfs_pio_set_fragment_info_local(char *, char *, char *);

/*
 * Meta operations
 */

char *gfs_unlink(const char *);
char *gfs_unlink_section_replica(const char *, const char *,
	int, char **, int);
char *gfs_unlink_replica(const char *, int, char **, int);
char *gfs_mkdir(const char *, gfarm_mode_t);
char *gfs_rmdir(const char *);
char *gfs_chdir_canonical(const char *);
char *gfs_chdir(const char *);
char *gfs_getcwd(char *, int);
char *gfs_chown(const char *, char *, char *);
char *gfs_chmod(const char *, gfarm_mode_t);
char *gfs_fchmod(GFS_File, gfarm_mode_t);
char *gfs_utimes(const char *, const struct gfarm_timespec *);
char *gfs_rename(const char *, const char *);

char *gfs_stat(const char *, struct gfs_stat *);
char *gfs_stat_section(const char *, const char *, struct gfs_stat *);
char *gfs_stat_index(char *, int, struct gfs_stat *);
char *gfs_fstat(GFS_File, struct gfs_stat *);
void gfs_stat_free(struct gfs_stat *);
char *gfs_access(const char *, int);

#define	GFS_MAXNAMLEN	255
struct gfs_dirent {
	long d_fileno;
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
char *gfs_opendir(const char *, GFS_Dir *);
char *gfs_readdir(GFS_Dir, struct gfs_dirent **);
char *gfs_closedir(GFS_Dir);
char *gfs_dirname(GFS_Dir);
char *gfs_realpath(const char *, char **);

void gfs_uncachedir(void);

/*
 * execution
 */

char *gfs_execve(const char *, char *const [], char *const []);

/*
 * meta operations
 */
char *gfarm_url_section_replicate_from_to(const char *, char *, char *, char *);
char *gfarm_url_section_replicate_to(const char *, char *, char *);
char *gfarm_url_program_register(const char *, char *, char *, int);
char *gfarm_url_program_deliver(const char *, int, char **, char ***);
char *gfarm_url_execfile_replicate_to_local(const char *, char **);
char *gfarm_url_program_get_local_path(const char *, char **);
char *gfarm_url_fragments_replicate(const char *, int, char **);
char *gfarm_url_fragments_replicate_to_domainname(const char *, const char *);
char *gfarm_url_section_migrate_from_to(const char *, char *, char *, char *);
char *gfarm_url_section_migrate_to(const char *, char *, char *);
char *gfarm_url_fragments_migrate(const char *, int, char **);
char *gfarm_url_fragments_migrate_to_domainname(const char *, const char *);
