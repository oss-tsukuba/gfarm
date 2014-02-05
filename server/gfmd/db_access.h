/*
 * Metadata storage operations
 *
 * $Id$
 */

gfarm_error_t db_initialize(void);
gfarm_error_t db_terminate(void);
void *db_thread(void *);
int db_getfreenum(void);

gfarm_error_t db_begin(const char *);
gfarm_error_t db_end(const char *);

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
gfarm_error_t db_inode_gen_modify(gfarm_ino_t, gfarm_uint64_t);
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

gfarm_error_t db_symlink_add(gfarm_ino_t, const char *);
gfarm_error_t db_symlink_remove(gfarm_ino_t);
gfarm_error_t db_symlink_load(void *, void (*)(void *, gfarm_ino_t, char *));

struct db_waitctx;
struct xattr_info;
gfarm_error_t db_xattr_add(int, gfarm_ino_t, char *, void *, size_t,
	struct db_waitctx *);
gfarm_error_t db_xattr_modify(int, gfarm_ino_t, char *, void *, size_t,
	struct db_waitctx *);
gfarm_error_t db_xattr_remove(int, gfarm_ino_t, const char *);
gfarm_error_t db_xattr_removeall(int, gfarm_ino_t);
gfarm_error_t db_xattr_get(int, gfarm_ino_t, char *, void **, size_t *,
	struct db_waitctx *);
gfarm_error_t db_xattr_load(void *closure,
	void (*callback)(void *, struct xattr_info *));
gfarm_error_t db_xmlattr_find(gfarm_ino_t, const char *,
	gfarm_error_t (*foundcallback)(void *, int, void *), void *,
	void (*callback)(gfarm_error_t, void *), void *);

struct quota;
gfarm_error_t db_quota_user_set(struct quota *, const char *);
gfarm_error_t db_quota_group_set(struct quota *, const char *);
gfarm_error_t db_quota_user_remove(const char *);
gfarm_error_t db_quota_group_remove(const char *);
struct gfarm_quota_info;
gfarm_error_t db_quota_user_load(void *,
	void (*)(void *, struct gfarm_quota_info *));
gfarm_error_t db_quota_group_load(void *,
	void (*)(void *, struct gfarm_quota_info *));
struct db_seqnum_arg;
gfarm_error_t db_seqnum_add(char *, gfarm_uint64_t);
gfarm_error_t db_seqnum_modify(char *, gfarm_uint64_t);
gfarm_error_t db_seqnum_remove(char *);
gfarm_error_t db_seqnum_load(void *,
	void (*)(void *, struct db_seqnum_arg *));
pthread_mutex_t *get_db_access_mutex(void);

struct gfarm_metadb_server;
gfarm_error_t db_mdhost_add(const struct gfarm_metadb_server *);
void *db_mdhost_dup(const struct gfarm_metadb_server *, size_t);
struct db_mdhost_modify_arg *db_mdhost_modify_arg_alloc(
	const struct gfarm_metadb_server *, int);
gfarm_error_t db_mdhost_modify(const struct gfarm_metadb_server *, int);
gfarm_error_t db_mdhost_remove(const char *);
gfarm_error_t db_mdhost_load(void *, void (*)(void *,
	struct gfarm_metadb_server *));

/* allocation for storage operations arguments */
struct db_host_modify_arg;
struct db_user_modify_arg;
struct db_group_modify_arg;
struct db_inode_string_modify_arg;
struct db_inode_cksum_arg;
struct db_filecopy_arg;
struct db_deadfilecopy_arg;
struct db_direntry_arg;
struct db_symlink_arg;
struct db_xattr_arg;
struct db_quota_arg;
struct db_quota_remove_arg;
struct db_mdhost_modify_arg;

void *db_host_dup(const struct gfarm_host_info *, size_t);
void *db_user_dup(const struct gfarm_user_info *, size_t);
void *db_group_dup(const struct gfarm_group_info *, size_t);
struct gfs_stat *db_inode_dup(const struct gfs_stat *, size_t);
struct db_host_modify_arg *db_host_modify_arg_alloc(
	const struct gfarm_host_info *, int, int, const char **, int,
	const char **);
struct db_user_modify_arg *db_user_modify_arg_alloc(
	const struct gfarm_user_info *, int);
struct db_group_modify_arg *db_group_modify_arg_alloc(
	const struct gfarm_group_info *, int, int, const char **, int,
	const char **);
struct db_inode_string_modify_arg *db_inode_string_modify_arg_alloc(
	gfarm_ino_t, const char *);
struct db_inode_cksum_arg *db_inode_cksum_arg_alloc(gfarm_ino_t,
	const char *, size_t, const char *);
struct db_filecopy_arg *db_filecopy_arg_alloc(gfarm_ino_t, const char *);
struct db_deadfilecopy_arg *db_deadfilecopy_arg_alloc(gfarm_ino_t,
	gfarm_uint64_t, const char *);
struct db_direntry_arg *db_direntry_arg_alloc(gfarm_ino_t, const char *,
	int, gfarm_ino_t);
struct db_symlink_arg *db_symlink_arg_alloc(gfarm_ino_t, const char *);
struct db_xattr_arg *db_xattr_arg_alloc(int, gfarm_ino_t, const char *,
	void *, size_t);
struct db_quota_arg *db_quota_arg_alloc(const struct quota *, const char *,
	int);
struct db_quota_remove_arg *db_quota_remove_arg_alloc(const char *, int);

/* external interface to select metadb backend type */

struct db_ops;
gfarm_error_t db_use(const struct db_ops *);

extern const struct db_ops db_none_ops, db_ldap_ops, db_pgsql_ops;
extern const struct db_ops *store_ops;


struct db_waitctx {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	gfarm_error_t e;
};

void db_waitctx_init(struct db_waitctx *);
void db_waitctx_fini(struct db_waitctx *);
gfarm_error_t dbq_waitret(struct db_waitctx *);


/* exported for a use from a private extension */
/* The official gfmd source code shouldn't use these interface */
typedef gfarm_error_t (*dbq_entry_func_t)(gfarm_uint64_t, void *);
gfarm_error_t gfarm_dbq_enter(dbq_entry_func_t, void *);
gfarm_error_t gfarm_dbq_enter_for_waitret(
	dbq_entry_func_t, void *, struct db_waitctx *);
const struct db_ops *db_get_ops(void);
