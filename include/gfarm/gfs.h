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
#define	GFARM_S_ISUID	0004000		/* set user id on execution */
#define	GFARM_S_ISGID	0002000		/* set group id on execution */
#define	GFARM_S_ISTXT	0001000		/* sticky bit */
#define	GFARM_S_IFMT	0170000		/* type of file mask */
#define	GFARM_S_IFDIR	0040000		/* directory */
#define	GFARM_S_IFREG	0100000		/* regular file */
#define	GFARM_S_IFLNK	0120000		/* symbolic link */
#define	GFARM_S_ISDIR(m) (((m) & GFARM_S_IFMT) == GFARM_S_IFDIR)
#define	GFARM_S_ISREG(m) (((m) & GFARM_S_IFMT) == GFARM_S_IFREG)
#define	GFARM_S_ISLNK(m) (((m) & GFARM_S_IFMT) == GFARM_S_IFLNK)

#define	GFARM_S_IS_PROGRAM(m) \
	(GFARM_S_ISREG(m) && ((m) & 0111) != 0)

#define GFARM_S_IS_SUGID_PROGRAM(mode) \
	(((mode) & (GFARM_S_ISUID|GFARM_S_ISGID)) != 0 && \
	 GFARM_S_IS_PROGRAM(mode))

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

struct gfs_stat_cksum {
	char *type, *cksum;
	size_t len;
	int flags;
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
#ifdef GFARM_INTERNAL_USE /* internal use only, but passed via protocol */
#define GFARM_FILE_LOOKUP		3
#define GFARM_FILE_CREATE		0x00000200
#endif
#define GFARM_FILE_TRUNC		0x00000400
#define GFARM_FILE_APPEND		0x00000800
#define GFARM_FILE_EXCLUSIVE		0x00001000
#ifdef GFARM_INTERNAL_USE /* internal use only, but passed via protocol */
#define GFARM_FILE_REPLICA_SPEC		0x00010000
#endif
#ifdef GFARM_INTERNAL_USE /* internal use only, never passed via protocol */
#define GFARM_FILE_GFSD_ACCESS_REVOKED	0x00400000 /* used by gfmd only */
#define GFARM_FILE_SYMLINK_NO_FOLLOW	0x00400000 /* used by libgfarm only */
#define GFARM_FILE_TRUNC_PENDING	0x00800000 /* used by gfmd only */
#define GFARM_FILE_OPEN_LAST_COMPONENT	0x00800000 /* used by libgfarm only */
#endif
#if 0
/* the followings are just hints */
#define GFARM_FILE_SEQUENTIAL		0x01000000
#define GFARM_FILE_REPLICATE		0x02000000
#define GFARM_FILE_NOT_REPLICATE	0x04000000
#define GFARM_FILE_NOT_RETRY		0x08000000
#endif
#define GFARM_FILE_UNBUFFERED		0x10000000
#define GFARM_FILE_CREATE_REPLICA	0x20000000
#ifdef GFARM_INTERNAL_USE /* internal use only, but passed via protocol */
#define GFARM_FILE_BEQUEATHED		0x40000000
#define GFARM_FILE_CKSUM_INVALIDATED	0x80000000

#if 0 /* not yet on Gfarm v2 */
#define GFARM_FILE_USER_MODE	(GFARM_FILE_ACCMODE|GFARM_FILE_TRUNC| \
		GFARM_FILE_APPEND|GFARM_FILE_EXCLUSIVE|GFARM_FILE_SEQUENTIAL| \
		GFARM_FILE_REPLICATE|GFARM_FILE_NOT_REPLICATE| \
		GFARM_FILE_UNBUFFERED)
#else
#define GFARM_FILE_USER_MODE	(GFARM_FILE_ACCMODE|GFARM_FILE_TRUNC| \
	GFARM_FILE_APPEND|GFARM_FILE_EXCLUSIVE| \
	GFARM_FILE_UNBUFFERED|GFARM_FILE_CREATE_REPLICA| \
	GFARM_FILE_REPLICA_SPEC)
#endif /* not yet on Gfarm v2 */
#define GFARM_FILE_PROTOCOL_MASK	(GFARM_FILE_USER_MODE|\
	GFARM_FILE_BEQUEATHED|GFARM_FILE_CKSUM_INVALIDATED)
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
gfarm_error_t gfs_pio_fhopen(gfarm_ino_t, gfarm_uint64_t, int, GFS_File *);
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
gfarm_error_t gfs_pio_cksum(GFS_File, const char *, struct gfs_stat_cksum *);

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
#define GFS_DT_LNK	10
int gfs_mode_to_type(gfarm_mode_t);

typedef struct gfs_dir *GFS_Dir;

gfarm_error_t gfs_opendir(const char *, GFS_Dir *);
gfarm_error_t gfs_fhopendir(gfarm_ino_t, gfarm_uint64_t, GFS_Dir *);
gfarm_error_t gfs_closedir(GFS_Dir);
gfarm_error_t gfs_seekdir(GFS_Dir, gfarm_off_t);
gfarm_error_t gfs_telldir(GFS_Dir, gfarm_off_t *);
gfarm_error_t gfs_readdir(GFS_Dir, struct gfs_dirent **);
gfarm_error_t gfs_fgetdirpath(GFS_Dir, char **);

typedef struct gfs_dirplus *GFS_DirPlus;

gfarm_error_t gfs_opendirplus(const char *, GFS_DirPlus *);
gfarm_error_t gfs_closedirplus(GFS_DirPlus);
gfarm_error_t gfs_readdirplus(GFS_DirPlus,
	struct gfs_dirent **, struct gfs_stat **);

gfarm_error_t gfs_realpath(const char *, char **);

/*
 * Symbolic link operations
 */
gfarm_error_t gfs_symlink(const char *, const char *);
gfarm_error_t gfs_readlink(const char *, char **);

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
gfarm_error_t gfs_utimes(const char *, const struct gfarm_timespec *);
gfarm_error_t gfs_lchown(const char *, const char *, const char *);
gfarm_error_t gfs_lchmod(const char *, gfarm_mode_t);
gfarm_error_t gfs_lutimes(const char *, const struct gfarm_timespec *);
gfarm_error_t gfs_link(const char *, const char *);
gfarm_error_t gfs_rename(const char *, const char *);

gfarm_error_t gfs_stat(const char *, struct gfs_stat *);
gfarm_error_t gfs_lstat(const char *, struct gfs_stat *);
gfarm_error_t gfs_fstat(GFS_File, struct gfs_stat *);
#if 0
gfarm_error_t gfs_stat_section(const char *, const char *, struct gfs_stat *);
gfarm_error_t gfs_stat_index(char *, int, struct gfs_stat *);
#endif
gfarm_error_t gfs_stat_cksum(const char *, struct gfs_stat_cksum *);
gfarm_error_t gfs_fstat_cksum(GFS_File, struct gfs_stat_cksum *);
gfarm_error_t gfs_stat_cksum_free(struct gfs_stat_cksum *);
gfarm_error_t gfs_fstat_cksum_set(GFS_File, struct gfs_stat_cksum *);

gfarm_error_t gfs_access(const char *, int);
#define GFS_F_OK	0
#define GFS_X_OK	1
#define GFS_W_OK	2
#define GFS_R_OK	4

/* 5th argument (flags) of gfs_setxattr() and gfs_fsetxattr() */
#define GFS_XATTR_CREATE    0x1     /* set value, fail if attr already exists */
#define GFS_XATTR_REPLACE   0x2     /* set value, fail if attr does not exist */

gfarm_error_t gfs_setxattr(const char *path, const char *name,
	const void *value, size_t size, int flags);
gfarm_error_t gfs_lsetxattr(const char *path, const char *name,
	const void *value, size_t size, int flags);
gfarm_error_t gfs_getxattr(const char *path, const char *name,
	void *value, size_t *size);
gfarm_error_t gfs_lgetxattr(const char *path, const char *name,
	void *value, size_t *size);
gfarm_error_t gfs_listxattr(const char *path, char *list, size_t *size);
gfarm_error_t gfs_llistxattr(const char *path, char *list, size_t *size);
gfarm_error_t gfs_removexattr(const char *path, const char *name);
gfarm_error_t gfs_lremovexattr(const char *path, const char *name);

gfarm_error_t gfs_fsetxattr(GFS_File gf, const char *name,
	const void *value, size_t size, int flags);
gfarm_error_t gfs_fgetxattr(GFS_File gf, const char *name,
	void *value, size_t *size);
gfarm_error_t gfs_fremovexattr(GFS_File gf, const char *name);

gfarm_error_t gfs_setxmlattr(const char *path, const char *name,
		const void *value, size_t size, int flags);
gfarm_error_t gfs_lsetxmlattr(const char *path, const char *name,
		const void *value, size_t size, int flags);
gfarm_error_t gfs_getxmlattr(const char *path, const char *name,
		void *value, size_t *size);
gfarm_error_t gfs_lgetxmlattr(const char *path, const char *name,
		void *value, size_t *size);
gfarm_error_t gfs_listxmlattr(const char *path, char *list, size_t *size);
gfarm_error_t gfs_llistxmlattr(const char *path, char *list, size_t *size);
gfarm_error_t gfs_removexmlattr(const char *path, const char *name);
gfarm_error_t gfs_lremovexmlattr(const char *path, const char *name);

struct gfs_xmlattr_ctx;
gfarm_error_t gfs_findxmlattr(const char *path, const char *expr,
	int depth, struct gfs_xmlattr_ctx **ctxpp);
gfarm_error_t gfs_getxmlent(struct gfs_xmlattr_ctx *ctxp,
	char **fpathp, char **namep);
gfarm_error_t gfs_closexmlattr(struct gfs_xmlattr_ctx *ctxp);

gfarm_error_t gfs_replicate_to(char *, char *, int);
gfarm_error_t gfs_replicate_from_to(char *, char *, int, char *, int);
gfarm_error_t gfs_migrate_to(char *, char *, int);
gfarm_error_t gfs_migrate_from_to(char *, char *, int, char *, int);

#define GFS_REPLICATE_FILE_FORCE			1	/* no BUSY */
#ifdef GFARM_INTERNAL_USE /* internal use only */
#define GFS_REPLICATE_FILE_WAIT				2
#define GFS_REPLICATE_FILE_MIGRATE			4
#endif
gfarm_error_t gfs_replicate_file_from_to_request(
	const char *, const char *, const char *, int);
gfarm_error_t gfs_replicate_file_to_request(const char *, const char *, int);
gfarm_error_t gfs_replicate_file_from_to(
	const char *, const char *, const char *, int);
gfarm_error_t gfs_replicate_file_to(const char *, const char *, int);

#define GFS_REPLICA_INFO_INCLUDING_DEAD_HOST		1
#define GFS_REPLICA_INFO_INCLUDING_INCOMPLETE_COPY	2
#define GFS_REPLICA_INFO_INCLUDING_DEAD_COPY		4
struct gfs_replica_info;
gfarm_error_t gfs_replica_info_by_name(const char *, int,
	struct gfs_replica_info **);
int gfs_replica_info_number(struct gfs_replica_info *);
const char *gfs_replica_info_nth_host(struct gfs_replica_info *, int);
gfarm_uint64_t gfs_replica_info_nth_gen(struct gfs_replica_info *, int);
int gfs_replica_info_nth_is_incomplete(struct gfs_replica_info *, int);
int gfs_replica_info_nth_is_dead_host(struct gfs_replica_info *, int);
int gfs_replica_info_nth_is_dead_copy(struct gfs_replica_info *, int);
void gfs_replica_info_free(struct gfs_replica_info *);

gfarm_error_t gfs_replica_list_by_name(const char *, int *, char ***);
gfarm_error_t gfs_replica_remove_by_file(const char *, const char *);
gfarm_error_t gfs_replicate_to_local(GFS_File, char *, int);
#if 0
gfarm_error_t gfs_execve(const char *, char *const *, char *const *);
#endif
gfarm_error_t gfs_statfs(gfarm_off_t *, gfarm_off_t *, gfarm_off_t *);
gfarm_error_t gfs_statfsnode_by_path(const char *, char *, int,
	gfarm_int32_t *, gfarm_off_t *, gfarm_off_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *, gfarm_off_t *);
gfarm_error_t gfs_statfsnode(char *, int,
	gfarm_int32_t *, gfarm_off_t *, gfarm_off_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *, gfarm_off_t *);

/*
 * ACL operations
 */

typedef gfarm_uint32_t		gfarm_acl_tag_t;
typedef gfarm_uint32_t		gfarm_acl_perm_t;
typedef gfarm_uint32_t		gfarm_acl_type_t;

typedef struct gfarm_acl	*gfarm_acl_t;
typedef struct gfarm_acl_entry	*gfarm_acl_entry_t;
typedef gfarm_acl_perm_t	*gfarm_acl_permset_t;

/* gfarm_perm_t flags */
#define GFARM_ACL_READ			(0x04)
#define GFARM_ACL_WRITE			(0x02)
#define GFARM_ACL_EXECUTE		(0x01)

/* gfarm_acl_tag_t values */
#define GFARM_ACL_UNDEFINED_TAG		(0x00)
#define GFARM_ACL_USER_OBJ		(0x01)
#define GFARM_ACL_USER			(0x02)
#define GFARM_ACL_GROUP_OBJ		(0x04)
#define GFARM_ACL_GROUP			(0x08)
#define GFARM_ACL_MASK			(0x10)
#define GFARM_ACL_OTHER			(0x20)

/* constatns for entry_id of gfs_acl_get_entry() */
#define GFARM_ACL_FIRST_ENTRY		0
#define GFARM_ACL_NEXT_ENTRY		1

/* gfs_acl_check() errors */
#define GFARM_ACL_NO_ERROR		(0x0000)
#define GFARM_ACL_MULTI_ERROR		(0x1000) /* multiple unique objects */
#define GFARM_ACL_DUPLICATE_ERROR	(0x2000) /* duplicate Ids in entries */
#define GFARM_ACL_MISS_ERROR		(0x3000) /* missing required entry */
#define GFARM_ACL_ENTRY_ERROR 		(0x4000) /* wrong entry type */

/* gfarm_acl_type_t values */
#define GFARM_ACL_TYPE_ACCESS		(0x8000)
#define GFARM_ACL_TYPE_DEFAULT		(0x4000)

/* gfs_acl_to_any_text() options */
#define GFARM_ACL_TEXT_SOME_EFFECTIVE	0x01
#define GFARM_ACL_TEXT_ALL_EFFECTIVE	0x02
#define GFARM_ACL_TEXT_SMART_INDENT	0x04
/* ID is not defined in Gfarm v2 : #define GFARM_ACL_TEXT_NUMERIC_IDS 0x08 */
#define GFARM_ACL_TEXT_ABBREVIATE	0x10

/* gfarm extended atrribute representation */
static const char GFARM_ACL_EA_ACCESS[] = "gfarm.acl_access";
static const char GFARM_ACL_EA_DEFAULT[] = "gfarm.acl_default";
/* version */
#define GFARM_ACL_EA_VERSION		(0x0001)

gfarm_error_t gfs_acl_init(int, gfarm_acl_t *);
gfarm_error_t gfs_acl_dup(gfarm_acl_t *, gfarm_acl_t);
gfarm_error_t gfs_acl_free(gfarm_acl_t);
gfarm_error_t gfs_acl_create_entry(gfarm_acl_t *, gfarm_acl_entry_t *);
gfarm_error_t gfs_acl_delete_entry(gfarm_acl_t, gfarm_acl_entry_t);
gfarm_error_t gfs_acl_get_entry(gfarm_acl_t, int, gfarm_acl_entry_t *);
gfarm_error_t gfs_acl_valid(gfarm_acl_t);
gfarm_error_t gfs_acl_calc_mask(gfarm_acl_t *);
gfarm_error_t gfs_acl_get_permset(gfarm_acl_entry_t, gfarm_acl_permset_t *);
gfarm_error_t gfs_acl_set_permset(gfarm_acl_entry_t, gfarm_acl_permset_t);
gfarm_error_t gfs_acl_add_perm(gfarm_acl_permset_t, gfarm_acl_perm_t);
gfarm_error_t gfs_acl_clear_perms(gfarm_acl_permset_t);
gfarm_error_t gfs_acl_delete_perm(gfarm_acl_permset_t, gfarm_acl_perm_t);
gfarm_error_t gfs_acl_get_qualifier(gfarm_acl_entry_t, char **);
gfarm_error_t gfs_acl_set_qualifier(gfarm_acl_entry_t, const char *);
gfarm_error_t gfs_acl_get_tag_type(gfarm_acl_entry_t, gfarm_acl_tag_t *);
gfarm_error_t gfs_acl_set_tag_type(gfarm_acl_entry_t, gfarm_acl_tag_t);
gfarm_error_t gfs_acl_delete_def_file(const char *);
gfarm_error_t gfs_acl_get_file(const char *, gfarm_acl_type_t, gfarm_acl_t *);
gfarm_error_t gfs_acl_get_file_cached(const char *, gfarm_acl_type_t,
				      gfarm_acl_t *);
gfarm_error_t gfs_acl_set_file(const char *, gfarm_acl_type_t, gfarm_acl_t);
gfarm_error_t gfs_acl_to_text(gfarm_acl_t, char **, size_t *);
gfarm_error_t gfs_acl_from_text(const char *, gfarm_acl_t *);

gfarm_error_t gfs_acl_get_perm(gfarm_acl_permset_t, gfarm_acl_perm_t, int *);
const char *gfs_acl_error(int);
gfarm_error_t gfs_acl_check(gfarm_acl_t, int *, int *);
int gfs_acl_entries(gfarm_acl_t);
gfarm_error_t gfs_acl_equiv_mode(gfarm_acl_t, gfarm_mode_t *, int *);
int gfs_acl_cmp(gfarm_acl_t, gfarm_acl_t);
gfarm_error_t gfs_acl_from_mode(gfarm_mode_t, gfarm_acl_t *);
gfarm_error_t gfs_acl_to_any_text(gfarm_acl_t, const char *, char, int,
				  char **);

gfarm_error_t gfs_acl_to_xattr_value(gfarm_acl_t, void **, size_t *);
gfarm_error_t gfs_acl_from_xattr_value(const void *, size_t, gfarm_acl_t *);
gfarm_error_t gfs_acl_sort(gfarm_acl_t);
gfarm_error_t gfs_acl_from_text_with_default(const char *, gfarm_acl_t *,
					     gfarm_acl_t *);
#ifdef GFARM_INTERNAL_USE /* internal use only */
gfarm_error_t gfs_acl_delete_mode(gfarm_acl_t);
#endif

/*
 * Key names of extended attribute for gfarm_root.*
 */
#define GFARM_ROOT_EA_PREFIX		"gfarm_root."
#define GFARM_ROOT_EA_PREFIX_LEN	(11)
#define GFARM_ROOT_EA_USER		GFARM_ROOT_EA_PREFIX"user"
#define GFARM_ROOT_EA_GROUP		GFARM_ROOT_EA_PREFIX"group"

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
gfarm_error_t gfs_lstat_cached(const char *, struct gfs_stat *);
gfarm_error_t gfs_lstat_caching(const char *, struct gfs_stat *);
gfarm_error_t gfs_getxattr_cached(const char *path, const char *name,
	void *value, size_t *size);
gfarm_error_t gfs_getxattr_caching(const char *path, const char *name,
	void *value, size_t *size);
gfarm_error_t gfs_lgetxattr_cached(const char *path, const char *name,
	void *value, size_t *size);
gfarm_error_t gfs_lgetxattr_caching(const char *path, const char *name,
	void *value, size_t *size);
gfarm_error_t gfs_lgetxattr_cached(const char *path, const char *name,
	void *value, size_t *size);
gfarm_error_t gfs_lgetxattr_caching(const char *path, const char *name,
	void *value, size_t *size);

int gfarm_xattr_caching(const char *);
gfarm_error_t gfarm_xattr_caching_pattern_add(const char *);

gfarm_error_t gfs_opendir_caching(const char *, GFS_Dir *);
