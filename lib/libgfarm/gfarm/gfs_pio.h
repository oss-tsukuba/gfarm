/*
 * $Id$
 */

struct stat;

#define	GFS_FILE_IS_PROGRAM(gf) (GFARM_S_IS_PROGRAM(gf->pi.status.st_mode))

extern char GFS_FILE_ERROR_EOF[];

struct gfs_pio_ops {
	char *(*view_close)(GFS_File);

	char *(*view_write)(GFS_File, const char *, size_t, size_t *);
	char *(*view_read)(GFS_File, char *, size_t, size_t *);
	char *(*view_seek)(GFS_File, file_offset_t, int, file_offset_t *);
	char *(*view_ftruncate)(GFS_File, file_offset_t);
	char *(*view_fsync)(GFS_File, int);
	int (*view_fd)(GFS_File);
	char *(*view_stat)(GFS_File, struct gfs_stat *);
	char *(*view_chmod)(GFS_File, gfarm_mode_t);
};

struct gfs_file {
	struct gfs_pio_ops *ops;
	void *view_context;

	int mode;
#define GFS_FILE_MODE_READ		0x00000001
#define GFS_FILE_MODE_WRITE		0x00000002
#define GFS_FILE_MODE_NSEGMENTS_FIXED	0x01000000
#define GFS_FILE_MODE_CALC_DIGEST	0x02000000 /* keep updating md_ctx */
#define GFS_FILE_MODE_UPDATE_METADATA	0x04000000 /* need to update */
#define GFS_FILE_MODE_FILE_CREATED	0x08000000 /* path_info created */
#define GFS_FILE_MODE_BUFFER_DIRTY	0x40000000

	/* remember parameter of open/set_view */
	int open_flags;
	int view_flags;

	char *error; /* GFS_FILE_ERROR_EOF, if end of file */

	file_offset_t io_offset;

#define GFS_FILE_BUFSIZE 65536
	char *buffer;
	int p;
	int length;

	file_offset_t offset;

	struct gfarm_path_info pi;
};

char *gfs_check_section_busy_by_finfo(struct gfarm_file_section_info *);
char *gfs_check_section_busy(char *, char *);
char *gfs_check_section_checksum_unknown_by_finfo(
	struct gfarm_file_section_info *);
char *gfs_unlink_section_internal(const char *, const char *);
char *gfs_unlink_every_other_replicas(
	const char *, const char *, const char *);

char *gfs_pio_set_view_default(GFS_File);
char *gfs_pio_set_view_global(GFS_File, int);
char *gfs_pio_open_local_section(GFS_File, int);
char *gfs_pio_open_remote_section(GFS_File, char *, int);

struct gfs_connection;

char *gfs_pio_local_mkdir_parent_canonical_path(char *);
char *gfs_pio_remote_mkdir_parent_canonical_path(
	struct gfs_connection *, char *);

struct gfs_storage_ops {
	char *(*storage_close)(GFS_File);
	char *(*storage_write)(GFS_File, const char *, size_t, size_t *);
	char *(*storage_read)(GFS_File, char *, size_t, size_t *);
	char *(*storage_seek)(GFS_File, file_offset_t, int, file_offset_t *);
	char *(*storage_ftruncate)(GFS_File, file_offset_t);
	char *(*storage_fsync)(GFS_File, int);
	char *(*storage_fstat)(GFS_File, struct stat *);
	char *(*storage_calculate_digest)(GFS_File, char *, size_t,
	    size_t *, unsigned char *, file_offset_t *);
	int (*storage_fd)(GFS_File);
};

#define GFS_DEFAULT_DIGEST_NAME	"md5"
#define GFS_DEFAULT_DIGEST_MODE	EVP_md5()

struct gfs_file_section_context {
	struct gfs_storage_ops *ops;
	void *storage_context;

	char *section;
	char *canonical_hostname;
	int fd; /* socket (for remote) or file (for local) descriptor */
	pid_t pid;

	/* for checksum, maintained only if GFS_FILE_MODE_CALC_DIGEST */
	EVP_MD_CTX md_ctx;
};

/*
 *       offset
 *         v
 *  buffer |*******************************---------|
 *         ^        ^                     ^
 *         0        p                   length
 *
 * on write:
 *	io_offset == offset
 * on sequential write:
 *	io_offset == offset && p == length
 * on read:
 *	io_offset == offset + length
 *
 * switching from writing to reading:
 *	no problem, flush the buffer if `p' beyonds `length'.
 * switching from reading to writing:
 *	usually, seek is needed.
 */

extern GFS_File gf_stdout, gf_stderr;
extern int gf_profile;
extern int gf_on_demand_replication;
extern int gf_hook_default_global;
#define gfs_profile(x) if (gf_profile == 1) { x; }

extern double gfs_pio_set_view_section_time;
extern double gfs_unlink_time;
extern double gfs_stat_time;

void gfs_display_timers();
