/*
 * $Id$
 *
 * This defines internal structure of gfs_pio module.
 *
 * Only gfs_pio_section.c, gfs_pio_{local,remote}.c, gfs_pio.c and context.c
 * are allowed to include this header file.
 * Every other modules shouldn't include this.
 */

struct stat;

#define	GFS_FILE_IS_PROGRAM(gf) (GFARM_S_IS_PROGRAM(gf->pi.status.st_mode))

struct gfs_pio_ops {
	gfarm_error_t (*view_close)(GFS_File);
	int (*view_fd)(GFS_File);

	gfarm_error_t (*view_pread)(GFS_File,
		char *, size_t, gfarm_off_t, size_t *);
	gfarm_error_t (*view_pwrite)(GFS_File,
		const char *, size_t, gfarm_off_t, size_t *);
	gfarm_error_t (*view_ftruncate)(GFS_File, gfarm_off_t);
	gfarm_error_t (*view_fsync)(GFS_File, int);
	gfarm_error_t (*view_fstat)(GFS_File, struct gfs_stat *);
	gfarm_error_t (*view_reopen)(GFS_File);
};

struct gfm_connection;
struct gfs_file {
	struct gfs_pio_ops *ops;
	void *view_context;

	struct gfm_connection *gfm_server;
	int fd;

	int mode;
#define GFS_FILE_MODE_READ		0x00000001
#define GFS_FILE_MODE_WRITE		0x00000002
#define GFS_FILE_MODE_NSEGMENTS_FIXED	0x01000000
#define GFS_FILE_MODE_CALC_DIGEST	0x02000000 /* keep updating md_ctx */
#define GFS_FILE_MODE_BUFFER_DIRTY	0x40000000

	/* remember parameter of open/set_view */
	int open_flags;
	/* remember opened url */
	char *url;
	/* remember opened inode num */
	gfarm_ino_t ino;
#if 0 /* not yet in gfarm v2 */
	int view_flags;
#endif /* not yet in gfarm v2 */

	gfarm_error_t error; /* GFARM_ERRMSG_GFS_PIO_IS_EOF, if end of file */

	gfarm_off_t io_offset;

/*
 * bufsize should be equal to or less than
 * GFS_PROTO_MAX_IOSIZE defined in gfs_proto.h.
 */
	char *buffer;
	int bufsize;
	int p;
	int length;

	gfarm_off_t offset;

	/* opening files */
	GFARM_HCIRCLEQ_ENTRY(gfs_file) hcircleq;
};

gfarm_error_t gfs_pio_set_view_default(GFS_File);
#if 0 /* not yet in gfarm v2 */
gfarm_error_t gfs_pio_set_view_global(GFS_File, int);
#endif /* not yet in gfarm v2 */
struct gfs_connection;
gfarm_error_t gfs_pio_open_local_section(GFS_File, struct gfs_connection *);
gfarm_error_t gfs_pio_open_remote_section(GFS_File, struct gfs_connection *);
gfarm_error_t gfs_pio_internal_set_view_section(GFS_File, char *);
gfarm_error_t gfs_pio_reconnect(GFS_File);

struct gfs_connection;

struct gfs_storage_ops {
	gfarm_error_t (*storage_close)(GFS_File);
	int (*storage_fd)(GFS_File);

	gfarm_error_t (*storage_pread)(GFS_File,
		char *, size_t, gfarm_off_t, size_t *);
	gfarm_error_t (*storage_pwrite)(GFS_File,
		const char *, size_t, gfarm_off_t, size_t *);
	gfarm_error_t (*storage_ftruncate)(GFS_File, gfarm_off_t);
	gfarm_error_t (*storage_fsync)(GFS_File, int);
	gfarm_error_t (*storage_fstat)(GFS_File, struct gfs_stat *);
	gfarm_error_t (*storage_reopen)(GFS_File);
};

#define GFS_DEFAULT_DIGEST_NAME	"md5"
#define GFS_DEFAULT_DIGEST_MODE	EVP_md5()

struct gfs_file_section_context {
	struct gfs_storage_ops *ops;
	void *storage_context;

#if 0 /* not yet in gfarm v2 */
	char *section;
	char *canonical_hostname;
#endif /* not yet in gfarm v2 */
	int fd; /* this isn't used for remote case, but only local case */
	pid_t pid;

#ifdef EVP_MD_CTX_FLAG_ONESHOT /* for kernel mode */
	/* for checksum, maintained only if GFS_FILE_MODE_CALC_DIGEST */
	EVP_MD_CTX md_ctx;
#endif
};

/*
 *       offset
 *         v
 *  buffer |*******************************---------|
 *         ^        ^                     ^
 *         0        p                   length
 *
 * the following conditions are valid usually, but not always:
 *
 * on write:
 *	io_offset == offset
 * on sequential write:
 *	io_offset == offset && p == length
 * on read:
 *	io_offset == offset + length
 */
