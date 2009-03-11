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
 * File/Directory operations
 */

#if 0 /* not yet on Gfarm v2 */
typedef struct gfs_desc *GFS_Desc;

gfarm_error_t gfs_desc_create(const char *, int, gfarm_mode_t, GFS_Desc *);
gfarm_error_t gfs_desc_open(const char *, int, GFS_Desc *);
gfarm_error_t gfs_desc_close(GFS_Desc);
int gfs_desc_fileno(GFS_Desc);
#endif /* not yet on Gfarm v2 */

#define GFARM_FILE_RDONLY		0
#define GFARM_FILE_WRONLY		1
#define GFARM_FILE_RDWR			2
#define GFARM_FILE_ACCMODE		3	/* RD/WR/RDWR mode mask */
#ifdef GFARM_INTERNAL_USE /* internal use only */
#define GFARM_FILE_LOOKUP		3
#define GFARM_FILE_CREATE		0x00000200
#endif
#define GFARM_FILE_TRUNC		0x00000400
#if 0 /* not yet on Gfarm v2 */
#define GFARM_FILE_APPEND		0x00000800
#define GFARM_FILE_EXCLUSIVE		0x00001000
/* the followings are just hints */
#define GFARM_FILE_SEQUENTIAL		0x01000000
#define GFARM_FILE_REPLICATE		0x02000000
#define GFARM_FILE_NOT_REPLICATE	0x04000000
#define GFARM_FILE_NOT_RETRY		0x08000000
#endif
#define GFARM_FILE_UNBUFFERED		0x10000000
#define GFARM_FILE_CREATE_REPLICA	0x20000000
#ifdef GFARM_INTERNAL_USE /* internal use only */
#define GFARM_FILE_BEQUEATHED		0x40000000
#define GFARM_FILE_CKSUM_INVALIDATED	0x80000000

#if 0 /* not yet on Gfarm v2 */
#define GFARM_FILE_USER_MODE	(GFARM_FILE_ACCMODE|GFARM_FILE_TRUNC| \
		GFARM_FILE_APPEND|GFARM_FILE_EXCLUSIVE|GFARM_FILE_SEQUENTIAL| \
		GFARM_FILE_REPLICATE|GFARM_FILE_NOT_REPLICATE| \
		GFARM_FILE_UNBUFFERED)
#else
#define GFARM_FILE_USER_MODE	(GFARM_FILE_ACCMODE|GFARM_FILE_TRUNC| \
	GFARM_FILE_UNBUFFERED|GFARM_FILE_CREATE_REPLICA)
#endif /* not yet on Gfarm v2 */
#endif /* GFARM_INTERNAL_USE */

#if 0 /* not yet on Gfarm v2 */
gfarm_error_t gfs_desc_seek(GFS_Desc, gfarm_off_t, int, gfarm_off_t *);
#endif /* not yet on Gfarm v2 */

#define GFARM_SEEK_SET	0
#define GFARM_SEEK_CUR	1
#define GFARM_SEEK_END	2

#if 0 /* not yet on Gfarm v2 */
gfarm_error_t gfs_desc_chown(GFS_Desc, char *, char *);
gfarm_error_t gfs_desc_chmod(GFS_Desc, gfarm_mode_t);
gfarm_error_t gfs_desc_utimes(GFS_Desc, const struct gfarm_timespec *);

gfarm_error_t gfs_desc_stat(GFS_Desc, struct gfs_stat *);
#endif /* not yet on Gfarm v2 */

void gfs_stat_free(struct gfs_stat *);
gfarm_error_t gfs_stat_copy(struct gfs_stat *, const struct gfs_stat *);

void gfs_client_connection_gc(void);

/*
 * File operations
 */

typedef struct gfs_file *GFS_File;

gfarm_error_t gfs_pio_open(const char *, int, GFS_File *);
gfarm_error_t gfs_pio_create(const char *, int, gfarm_mode_t mode, GFS_File *);

#if 0 /* not yet on Gfarm v2 */
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
#endif

gfarm_error_t gfs_pio_close(GFS_File);

int gfs_pio_eof(GFS_File);
gfarm_error_t gfs_pio_error(GFS_File);
void gfs_pio_clearerr(GFS_File);
int gfs_pio_fileno(GFS_File);
#if 0 /* not yet on Gfarm v2 */
gfarm_error_t gfs_pio_get_nfragment(GFS_File, int *);
#endif

gfarm_error_t gfs_pio_seek(GFS_File, gfarm_off_t, int, gfarm_off_t *);
gfarm_error_t gfs_pio_read(GFS_File, void *, int, int *);
gfarm_error_t gfs_pio_write(GFS_File, const void *, int, int *);
gfarm_error_t gfs_pio_flush(GFS_File);
gfarm_error_t gfs_pio_sync(GFS_File);
gfarm_error_t gfs_pio_datasync(GFS_File);
gfarm_error_t gfs_pio_truncate(GFS_File, gfarm_off_t);

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

gfarm_error_t gfs_pio_stat(GFS_File, struct gfs_stat *);

/*
 * Directory operations
 */

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
gfarm_error_t gfs_closedir(GFS_Dir);
gfarm_error_t gfs_seekdir(GFS_Dir, gfarm_off_t);
gfarm_error_t gfs_telldir(GFS_Dir, gfarm_off_t *);
gfarm_error_t gfs_readdir(GFS_Dir, struct gfs_dirent **);

typedef struct gfs_dirplus *GFS_DirPlus;

gfarm_error_t gfs_opendirplus(const char *, GFS_DirPlus *);
gfarm_error_t gfs_closedirplus(GFS_DirPlus);
gfarm_error_t gfs_readdirplus(GFS_DirPlus,
	struct gfs_dirent **, struct gfs_stat **);

gfarm_error_t gfs_realpath(const char *, char **);

/*
 * Meta operations
 */

gfarm_error_t gfs_remove(const char *); /* XXX shouldn't be exported? */
gfarm_error_t gfs_unlink(const char *);
#if 0 /* not yet on Gfarm v2 */
gfarm_error_t gfs_unlink_section(const char *, const char *);
gfarm_error_t gfs_unlink_section_replica(const char *, const char *,
	int, char **, int);
gfarm_error_t gfs_unlink_replicas_on_host(const char *,	const char *, int);
#endif
gfarm_error_t gfs_mkdir(const char *, gfarm_mode_t);
gfarm_error_t gfs_rmdir(const char *);
gfarm_error_t gfs_chdir(const char *);
gfarm_error_t gfs_getcwd(char *, int);
gfarm_error_t gfs_chown(const char *, const char *, const char *);
gfarm_error_t gfs_chmod(const char *, gfarm_mode_t);
gfarm_error_t gfs_link(const char *, const char *);
gfarm_error_t gfs_utimes(const char *, const struct gfarm_timespec *);
gfarm_error_t gfs_rename(const char *, const char *);

gfarm_error_t gfs_stat(const char *, struct gfs_stat *);
gfarm_error_t gfs_fstat(GFS_File, struct gfs_stat *);
#if 0
gfarm_error_t gfs_stat_section(const char *, const char *, struct gfs_stat *);
gfarm_error_t gfs_stat_index(char *, int, struct gfs_stat *);
#endif

gfarm_error_t gfs_access(const char *, int);
#define GFS_F_OK	0
#define GFS_X_OK	1
#define GFS_W_OK	2
#define GFS_R_OK	4

gfarm_error_t gfs_replicate_to(char *, char *, int);
gfarm_error_t gfs_replicate_from_to(char *, char *, int, char *, int);
gfarm_error_t gfs_migrate_to(char *, char *, int);
gfarm_error_t gfs_migrate_from_to(char *, char *, int, char *, int);
gfarm_error_t gfs_replica_list_by_name(const char *, int *, char ***);
gfarm_error_t gfs_replica_remove_by_file(const char *, const char *);
gfarm_error_t gfs_replicate_to_local(GFS_File, char *, int);
#if 0
gfarm_error_t gfs_execve(const char *, char *const *, char *const *);
#endif
gfarm_error_t gfs_statfs(gfarm_off_t *, gfarm_off_t *, gfarm_off_t *);

/*
 * Client-side Metadata cache (preliminary version)
 */

void gfs_stat_cache_enable(int); /* enabled by default */
gfarm_error_t gfs_stat_cache_init(void);
void gfs_stat_cache_clear(void);
void gfs_stat_cache_expire(void);
void gfs_stat_cache_expiration_set(long); /* per milli-second */
gfarm_error_t gfs_stat_cache_purge(const char *);
gfarm_error_t gfs_stat_cached(const char *, struct gfs_stat *);
gfarm_error_t gfs_stat_caching(const char *, struct gfs_stat *);

typedef struct gfs_dir_caching *GFS_DirCaching;
gfarm_error_t gfs_opendir_caching(const char *, GFS_DirCaching *);
gfarm_error_t gfs_readdir_caching(GFS_DirCaching, struct gfs_dirent **);
gfarm_error_t gfs_closedir_caching(GFS_DirCaching);
