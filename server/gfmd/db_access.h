/*
 * Metadata storage operations
 *
 * $Id$
 */

gfarm_error_t db_initialize(void);
gfarm_error_t db_terminate(void);
void *db_thread(void *);

#if 0 /* XXX for now */
gfarm_error_t db_begin(void);
gfarm_error_t db_end(void);
#endif

struct gfarm_host_info;
gfarm_error_t db_host_add(const struct gfarm_host_info *);
gfarm_error_t db_host_modify(const struct gfarm_host_info *,
	int, int, const char **, int, const char **);
gfarm_error_t db_host_remove(const char *);
gfarm_error_t db_host_load(void *, void (*)(void *, struct gfarm_host_info *));

#define DB_HOST_MOD_ARCHITECTURE	1
#define DB_HOST_MOD_NCPU		2
#define DB_HOST_MOD_FLAGS		4

struct gfarm_user_info;
gfarm_error_t db_user_add(const struct gfarm_user_info *);
gfarm_error_t db_user_modify(const struct gfarm_user_info *, int);
gfarm_error_t db_user_remove(const char *);
gfarm_error_t db_user_load(void *, void (*)(void *, struct gfarm_user_info *));

#define DB_USER_MOD_REALNAME		1
#define DB_USER_MOD_HOMEDIR		2
#define DB_USER_MOD_GSI_DN		4

struct gfarm_group_info;
gfarm_error_t db_group_add(const struct gfarm_group_info *);
gfarm_error_t db_group_modify(const struct gfarm_group_info *, int,
	int, const char **, int, const char **);
gfarm_error_t db_group_remove(const char *);
gfarm_error_t db_group_load(void *,
	void (*)(void *, struct gfarm_group_info *));

struct gfs_stat;
gfarm_error_t db_inode_add(const struct gfs_stat *);
gfarm_error_t db_inode_modify(const struct gfs_stat *);
gfarm_error_t db_inode_nlink_modify(gfarm_ino_t, gfarm_uint64_t);
gfarm_error_t db_inode_size_modify(gfarm_ino_t, gfarm_off_t);
gfarm_error_t db_inode_mode_modify(gfarm_ino_t, gfarm_mode_t);
gfarm_error_t db_inode_user_modify(gfarm_ino_t, const char *);
gfarm_error_t db_inode_group_modify(gfarm_ino_t, const char *);
gfarm_error_t db_inode_atime_modify(gfarm_ino_t, struct gfarm_timespec *);
gfarm_error_t db_inode_mtime_modify(gfarm_ino_t, struct gfarm_timespec *);
gfarm_error_t db_inode_ctime_modify(gfarm_ino_t, struct gfarm_timespec *);
/* db_inode_remove: never remove any inode to keep inode->i_gen */
gfarm_error_t db_inode_load(void *, void (*)(void *, struct gfs_stat *));

gfarm_error_t db_inode_cksum_add(gfarm_ino_t,
	const char *, size_t, const char *);
gfarm_error_t db_inode_cksum_modify(gfarm_ino_t,
	const char *, size_t, const char *);
gfarm_error_t db_inode_cksum_remove(gfarm_ino_t);
gfarm_error_t db_inode_cksum_load(void *,
	void (*)(void *, gfarm_ino_t, char *, size_t, char *));

gfarm_error_t db_filecopy_add(gfarm_ino_t, const char *);
gfarm_error_t db_filecopy_remove(gfarm_ino_t, const char *);
gfarm_error_t db_filecopy_load(void *, void (*)(void *, gfarm_ino_t, char *));

gfarm_error_t db_deadfilecopy_add(gfarm_ino_t, gfarm_uint64_t, const char *);
gfarm_error_t db_deadfilecopy_remove(gfarm_ino_t, gfarm_uint64_t,
	const char *);
gfarm_error_t db_deadfilecopy_load(void *,
	void (*)(void *, gfarm_ino_t, gfarm_uint64_t, char *));

gfarm_error_t db_direntry_add(gfarm_ino_t, const char *, int, gfarm_ino_t);
gfarm_error_t db_direntry_remove(gfarm_ino_t, const char *, int);
gfarm_error_t db_direntry_load(void *,
	void (*)(void *, gfarm_ino_t, char *, int, gfarm_ino_t));


/* external interface to select metadb backend type */

struct db_ops;
gfarm_error_t db_use(const struct db_ops *);

extern const struct db_ops db_none_ops, db_ldap_ops, db_pgsql_ops;
