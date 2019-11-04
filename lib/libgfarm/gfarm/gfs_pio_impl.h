/*
 * $Id: gfs_pio.h 9051 2014-04-29 12:01:56Z tatebe $
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
	gfarm_error_t (*view_write)(GFS_File,
		const char *, size_t, size_t *, gfarm_off_t *, gfarm_off_t *);
	gfarm_error_t (*view_cksum)(GFS_File,
		const char *, struct gfs_stat_cksum *);
	gfarm_error_t (*view_recvfile)(GFS_File, gfarm_off_t,
		int, gfarm_off_t, gfarm_off_t, gfarm_off_t *);
	gfarm_error_t (*view_sendfile)(GFS_File, gfarm_off_t,
		int, gfarm_off_t, gfarm_off_t, gfarm_off_t *);
};

struct gfm_connection;
struct gfs_file {
	struct gfs_pio_ops *ops;
	void *view_context;

	/* XXX should be a per view_context variable to support global view */
	gfarm_uint64_t scheduled_age;

	struct gfm_connection *gfm_server;
	int fd;

	int mode;
#define GFS_FILE_MODE_READ		0x00000001
#define GFS_FILE_MODE_WRITE		0x00000002
#define GFS_FILE_MODE_NSEGMENTS_FIXED	0x01000000
#define GFS_FILE_MODE_DIGEST_CALC	0x02000000 /* keep updating md_ctx */
#define GFS_FILE_MODE_DIGEST_AVAIL	0x04000000 /* metadata has cksum */
#define GFS_FILE_MODE_DIGEST_FINISH	0x08000000 /* EVP_DigestFinal() done */
#define GFS_FILE_MODE_BUFFER_DIRTY	0x40000000
#define GFS_FILE_MODE_MODIFIED		0x80000000

	/* remember parameter of open/set_view */
	int open_flags;
	/* remember opened url */
	char *url;
	/* remember opened inode num */
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
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

	/*
	 * checksum calculation (md: message digest)
	 *
	 * if (md.cksum_type != NULL)
	 *	md_ctx was initialized, and EVP_DigestFinal() has to be called,
	 *	unless GFS_FILE_MODE_DIGEST_FINISH bit is set.
	 *
	 * switch (mode &
	 *	(GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH)) {
	 * case  0:
	 *	do not calculate digest, or the digest was invalidated
	 * case  GFS_FILE_MODE_DIGEST_CALC:
	 *	digest calculation is ongoing
	 * case (GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH):
	 *	digest calculation is completed
	 * case  GFS_FILE_MODE_DIGEST_FINISH:
	 *	digest calculation is completed, but the digest was invalidated
	 * }
	 */
	gfarm_off_t md_offset;
	struct gfs_pio_internal_cksum_info md;
	EVP_MD_CTX *md_ctx;

	/* opening files */
	GFARM_HCIRCLEQ_ENTRY(gfs_file) hcircleq;
};


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
	gfarm_error_t (*storage_write)(GFS_File,
		const char *, size_t, size_t *, gfarm_off_t *, gfarm_off_t *);
	gfarm_error_t (*storage_cksum)(GFS_File,
		const char *, char *, size_t, size_t *);
	gfarm_error_t (*storage_recvfile)(GFS_File, gfarm_off_t,
		int, gfarm_off_t, gfarm_off_t, EVP_MD_CTX *, gfarm_off_t *);
	gfarm_error_t (*storage_sendfile)(GFS_File, gfarm_off_t,
		int, gfarm_off_t, gfarm_off_t, EVP_MD_CTX *, gfarm_off_t *);
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

int gfs_pio_md_init(const char *, EVP_MD_CTX **, char *);
gfarm_error_t gfs_pio_md_finish(GFS_File);
gfarm_error_t gfs_pio_reopen_fd(GFS_File,
	struct gfm_connection **, int *, int *,
	char **, gfarm_ino_t *, gfarm_uint64_t *);

extern struct gfs_storage_ops gfs_pio_local_storage_ops;
