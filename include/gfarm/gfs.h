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
#define	GAFRM_S_IFLNK	0120000		/* symbolic link */
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

char *gfs_pio_open(char *, int, GFS_File *);
char *gfs_pio_create(char *, int, gfarm_mode_t mode, GFS_File *);

#define GFARM_FILE_RDONLY		0
#define GFARM_FILE_WRONLY		1
#define GFARM_FILE_RDWR			2
#define GFARM_FILE_ACCMODE		3	/* RD/WR/RDWR mode mask */
#define GFARM_FILE_CREATE		0x00000200
#define GFARM_FILE_TRUNC		0x00000400
/* the followings are just hints */
#define GFARM_FILE_SEQUENTIAL		0x01000000
#define GFARM_FILE_REPLICATE		0x02000000
#define GFARM_FILE_NOT_REPLICATE	0x04000000

char *gfs_pio_set_local(int, int);
char *gfs_pio_set_local_check(void);
char *gfs_pio_get_node_rank(int *);
char *gfs_pio_get_node_size(int *);

char *gfs_pio_set_view_local(GFS_File, int);
char *gfs_pio_set_view_index(GFS_File, int, int, char *, int);
char *gfs_pio_set_view_section(GFS_File, char *, char *, int);
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
char *gfs_pio_puts(GFS_File, char *);
char *gfs_pio_getline(GFS_File, char *, size_t, int *);
char *gfs_pio_putline(GFS_File, char *);

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
char *gfs_unlink_replicas_on_host(const char *, const char *, int);
char *gfs_mkdir(const char *, gfarm_mode_t);
char *gfs_rmdir(const char *);
char *gfs_chdir(const char *);
char *gfs_getcwd(char *, int);
char *gfs_chown(const char *, char *, char *);
char *gfs_chmod(const char *, gfarm_mode_t);
char *gfs_utimes(const char *, const struct gfarm_timespec *);
char *gfs_rename(const char *, const char *);

char *gfs_stat(char *, struct gfs_stat *);
char *gfs_stat_section(char *, char *, struct gfs_stat *);
char *gfs_stat_index(char *, int, struct gfs_stat *);
char *gfs_fstat(GFS_File, struct gfs_stat *);
void gfs_stat_free(struct gfs_stat *);
char *gfs_access(char *, int);

#define	GFS_MAXNAMLEN	255
struct gfs_dirent {
	int d_fileno;
	unsigned char d_type;
	unsigned char d_namlen;
	char d_name[GFS_MAXNAMLEN + 1];
};
/* File types */
#define	GFS_DT_UNKNOWN		 0
#define	GFS_DT_DIR		 4
#define	GFS_DT_REG		 8

typedef struct gfs_dir *GFS_Dir;
char *gfs_opendir(char *, GFS_Dir *);
char *gfs_readdir(GFS_Dir, struct gfs_dirent **);
char *gfs_closedir(GFS_Dir);

void gfs_uncachedir(void);

/*
 * execution
 */

char *gfs_execve(const char *, char *const [], char *const []);

/*
 * meta operations
 */
char *gfarm_url_section_replicate_from_to(char *, char *, char *, char *);
char *gfarm_url_section_replicate_to(char *, char *, char *);
char *gfarm_url_program_register(char *, char *, char *, int);
char *gfarm_url_program_deliver(const char *, int, char **, char ***);
char *gfarm_url_fragments_replicate(char *, int, char **);
char *gfarm_url_fragments_replicate_to_domainname(char *, const char *);
