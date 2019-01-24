void inode_init(void);
void inode_initial_entry(void);
void dir_entry_init(void);
void dir_entry_initial_entry(void);
void symlink_init(void);
void xattr_init(void);
void xattr_init_cache_all(void);

gfarm_uint64_t inode_total_num(void);

struct inode;

struct host;
struct user;
struct group;
struct process;
struct file_copy;
struct dead_file_copy;
struct dirset;
struct gfs_stat;
struct inode_trace_log_info;
struct replica_spec;

void inode_for_each_file_copies(
	struct inode *,
	void (*)(struct inode *, struct file_copy *, void *),
	void *);
void inode_for_each_file_opening(
	struct inode *,
	void (*)(int, struct host *, void *),
	void *);

struct host *file_copy_host(struct file_copy *);
int file_copy_is_valid(struct file_copy *);
int file_copy_is_being_removed(struct file_copy *);

int inode_is_dir(struct inode *);
int inode_is_file(struct inode *);
int inode_is_symlink(struct inode *);
gfarm_ino_t inode_get_number(struct inode *);
gfarm_int64_t inode_get_gen(struct inode *);
void inode_set_gen_in_cache(struct inode *, gfarm_uint64_t);
gfarm_int64_t inode_get_nlink(struct inode *);
void inode_set_nlink_in_cache(struct inode *, gfarm_uint64_t);
struct user *inode_get_user(struct inode *);
void inode_set_user_by_name_in_cache(struct inode *, const char *);
struct group *inode_get_group(struct inode *);
void inode_set_group_by_name_in_cache(struct inode *, const char *);
int inode_has_no_replica(struct inode *);
gfarm_int64_t inode_get_ncopy(struct inode *);
gfarm_int64_t inode_get_ncopy_with_dead_host(struct inode *);
struct hostset;
int inode_count_replicas_within_scope(
	struct inode *, int, int, gfarm_time_t, struct hostset *);

gfarm_mode_t inode_get_mode(struct inode *);
gfarm_error_t inode_set_mode(struct inode *, gfarm_mode_t);
void inode_set_mode_in_cache(struct inode *, gfarm_mode_t);
gfarm_off_t inode_get_size(struct inode *);
void inode_set_size(struct inode *, struct dirset *, gfarm_off_t);
void inode_set_size_in_cache(struct inode *, gfarm_off_t);
gfarm_error_t inode_set_owner(struct inode *, struct user *, struct group *);
struct gfarm_timespec *inode_get_atime(struct inode *);
struct gfarm_timespec *inode_get_mtime(struct inode *);
struct gfarm_timespec *inode_get_ctime(struct inode *);
extern void (*inode_set_relatime)(struct inode *, struct gfarm_timespec *);
extern void (*inode_set_atime)(struct inode *, struct gfarm_timespec *);
void inode_set_atime_in_cache(struct inode *, struct gfarm_timespec *);
void inode_set_mtime(struct inode *, struct gfarm_timespec *);
void inode_set_mtime_in_cache(struct inode *, struct gfarm_timespec *);
void inode_set_ctime(struct inode *, struct gfarm_timespec *);
void inode_set_ctime_in_cache(struct inode *, struct gfarm_timespec *);
void inode_accessed(struct inode *);
void inode_modified(struct inode *);
void inode_status_changed(struct inode *);
char *inode_get_symlink(struct inode *);
int inode_dir_is_empty(struct inode *);
int inode_desired_dead_file_copy(gfarm_ino_t);
gfarm_error_t inode_add_or_modify_in_cache(struct gfs_stat *, struct inode **);
void inode_modify(struct inode *, struct gfs_stat *);
gfarm_error_t symlink_add(gfarm_ino_t, char *);
void inode_clear_symlink(struct inode *);
void inode_free(struct inode *);
struct inode *inode_alloc_num(gfarm_ino_t);

struct peer;
int inode_new_generation_is_pending(struct inode *);
void inode_new_generation_by_fd_start(struct inode *, struct peer *);
gfarm_error_t inode_new_generation_by_cookie_start(
	struct inode *, struct peer *, gfarm_uint64_t);
gfarm_error_t inode_new_generation_by_fd_finish(
	struct inode *, struct peer *, gfarm_error_t);
gfarm_error_t inode_new_generation_by_cookie_finish(struct inode *,
	gfarm_off_t, gfarm_uint64_t, struct peer *, gfarm_error_t);
gfarm_error_t inode_new_generation_wait(struct inode *, struct peer *,
	gfarm_error_t (*)(struct peer *, void *, int *), void *);


gfarm_error_t inode_access(struct inode *, struct user *, int);

gfarm_ino_t inode_root_number();
gfarm_ino_t inode_table_current_size();
struct inode *inode_lookup(gfarm_ino_t);
void inode_lookup_all(void *, void (*callback)(void *, struct inode *));

gfarm_error_t inode_lookup_root(struct process *, int, struct inode **);
gfarm_error_t inode_lookup_parent(struct inode *, struct process *, int,
	struct dirset **, struct inode **);
gfarm_error_t inode_lookup_for_open(struct inode *, const char *,
	struct process *, int, struct dirset **, struct inode **);
gfarm_error_t inode_create_file(struct inode *, char *,
	struct process *, int, gfarm_mode_t, int,
	struct inode **, int *);
gfarm_error_t inode_create_dir(struct inode *, char *,
	struct process *, gfarm_mode_t);
gfarm_error_t inode_create_symlink(struct inode *, char *,
	struct process *, char *, struct inode_trace_log_info *);
gfarm_error_t inode_create_link(struct inode *, char *,
	struct process *, struct inode *);
gfarm_error_t inode_rename(struct inode *, const char *,
	struct inode *, const char *,
	struct process *, struct peer *, struct inode_trace_log_info *,
	struct inode_trace_log_info *, int *, int *, const char *);
gfarm_error_t inode_unlink(struct inode *, const char *, struct process *,
	struct inode_trace_log_info *, int *);

struct file_opening;

void inode_dead_file_copy_added(gfarm_ino_t, gfarm_int64_t, struct host *);
gfarm_error_t inode_add_replica(struct inode *, struct host *, int);
gfarm_error_t inode_add_file_copy_in_cache(struct inode *, struct host *);
void inode_remove_replica_completed(gfarm_ino_t, gfarm_int64_t, struct host *);
gfarm_error_t inode_remove_replica_lost(struct inode *, struct host *,
	gfarm_int64_t);
gfarm_error_t inode_remove_replica_protected(struct inode *, struct host *,
	struct replica_spec *, struct dirset *);
gfarm_error_t inode_remove_replica_orphan(struct inode *, struct host *);
void inode_remove_replica_incomplete(struct inode *, struct host *,
	gfarm_int64_t);
gfarm_error_t inode_remove_replica_in_cache(struct inode *, struct host *);
void inode_remove_replica_in_cache_for_invalid_host(gfarm_ino_t);
int inode_is_updated(struct inode *, struct gfarm_timespec *);
gfarm_error_t dir_entry_add(gfarm_ino_t, char *, int, gfarm_ino_t);

struct hostset;
gfarm_error_t inode_schedule_replication_within_scope(
	struct inode *, struct dirset *, int,
	int, struct host **, int *,
	int *, struct hostset *,
	int *, struct hostset *, gfarm_time_t,
	int *, struct hostset *, const char *, int *);
gfarm_error_t inode_schedule_replication(
	struct inode *, struct dirset *, int, int, const char *,
	int, struct host **,
	int *, struct hostset *, gfarm_time_t,
	int *, struct hostset *, const char *, int *);

gfarm_error_t inode_open(struct file_opening *, struct dirset *);
struct dirset *inode_get_tdirset(struct inode *);
void inode_close(struct file_opening *, char **, const char *);
void inode_close_read(struct file_opening *, struct gfarm_timespec *, char **,
	const char *);
gfarm_error_t inode_fhclose_read(struct inode *, struct gfarm_timespec *);
void inode_add_ref_spool_writers(struct inode *);
void inode_del_ref_spool_writers(struct inode *);
void inode_check_pending_replication(struct file_opening *);
int inode_file_update(struct file_opening *,
	gfarm_off_t, struct gfarm_timespec *, struct gfarm_timespec *, int,
	gfarm_int64_t *, gfarm_int64_t *, char **, const char *);
gfarm_error_t inode_file_handle_update(struct inode *,
	gfarm_off_t, struct gfarm_timespec *, struct gfarm_timespec *,
	struct host *, gfarm_int64_t *, gfarm_int64_t *, int *, char **,
	const char *);

void inode_cksum_remove_in_cache(struct inode *);
gfarm_error_t inode_cksum_set_in_cache(struct inode *,
	const char *, size_t, const char *);
gfarm_error_t inode_cksum_set(struct inode *,
	const char *, size_t, const char *, gfarm_int32_t, int, int *);
gfarm_error_t inode_cksum_set_if_not_writing(struct inode *,
	const char *, size_t, const char *, gfarm_int32_t);

gfarm_error_t file_opening_cksum_set(struct file_opening *,
	const char *, size_t, const char *,
	gfarm_int32_t, struct gfarm_timespec *);
gfarm_error_t file_opening_cksum_get(struct file_opening *,
	char **, size_t *, char **, gfarm_int32_t *);
gfarm_error_t inode_cksum_get_on_host(struct inode *, struct host *,
	char **, size_t *, char **, gfarm_int32_t *);

int inode_is_opened_for_writing(struct inode *);
int inode_is_opened_on(struct inode *, struct host *);
gfarm_uint64_t inode_get_open_status_by_host(struct inode *, struct host *);
struct file_copy * inode_get_file_copy(struct inode *, struct host *);
int inode_has_file_copy(struct inode *, struct host *);
int inode_has_replica(struct inode *, struct host *);
gfarm_error_t inode_getdirpath(struct inode *, struct process *, char **);
struct host *inode_schedule_host_for_read(struct inode *, struct host *);
struct host *inode_schedule_host_for_write(struct inode *, struct host *);
struct host *inode_writing_spool_host(struct inode *);
gfarm_error_t inode_schedule_confirm_for_write(struct file_opening *,
	struct host *, int *);
struct peer;

/* this interface is made as a hook for a private extension */
extern gfarm_error_t (*inode_schedule_file)(struct file_opening *,
	struct peer *, gfarm_int32_t *, struct host ***);

struct file_replicating;
void replication_info(void);
gfarm_error_t file_replicating_new(
	struct inode *, struct host *, struct dead_file_copy *,
	struct dirset *, struct file_replicating **);
void file_replicating_free(struct file_replicating *);
void file_replicating_free_by_error_before_request(struct file_replicating *);
gfarm_int64_t file_replicating_get_gen(struct file_replicating *);
gfarm_error_t inode_replicated(struct file_replicating *,
	gfarm_int32_t, gfarm_int32_t, gfarm_off_t,
	int, gfarm_int32_t, char *, size_t, char *, gfarm_int32_t);
gfarm_error_t inode_prepare_to_replicate(struct inode *, struct user *,
	struct host *, struct host *, gfarm_int32_t,
	struct file_replicating **);
void inode_replication_get_cksum_mode(struct inode *, struct host *,
	char **, size_t *, char **, gfarm_int32_t *);
gfarm_error_t inode_replication_request(struct host *, struct host *,
	struct inode *, struct file_replicating *, const char *);

gfarm_error_t inode_replica_hostset(
	struct inode *, gfarm_int32_t *, struct hostset **,
	gfarm_int32_t *, struct hostset **);
gfarm_error_t inode_replica_hosts_valid(struct inode *,
	gfarm_int32_t *, struct host ***);

gfarm_error_t inode_replica_list_by_name(struct inode *,
	gfarm_int32_t *, char ***);
gfarm_error_t inode_replica_list_by_name_with_dead_host(struct inode *,
	gfarm_int32_t *, char ***);
gfarm_error_t inode_replica_info_get(struct inode *, gfarm_int32_t,
	gfarm_int32_t *, char ***, gfarm_int64_t **, gfarm_int32_t **);

gfarm_error_t inode_xattr_add(struct inode *, int, const char *,
	void *, size_t);
gfarm_error_t inode_xattr_modify(struct inode *, int, const char *,
	void *, size_t);
gfarm_error_t inode_xattr_get_cache(struct inode *, int, const char *,
	void **, size_t *);
gfarm_error_t inode_xattr_cache_is_same(struct inode *, int, const char *,
	const void *, size_t);
int inode_xattr_has_attr(struct inode *, int, const char *);
int inode_xattr_has_xmlattrs(struct inode *);
gfarm_error_t inode_xattr_remove(struct inode *, int, const char *);
gfarm_error_t inode_xattr_list(struct inode *, int, char **, size_t *);
void inode_xattrs_clear(struct inode *);

struct xattr_list {
	char *name;
	void *value;
	size_t size;
};
void inode_xattr_list_free(struct xattr_list *, size_t);
gfarm_error_t inode_xattr_list_get_cached_by_patterns(gfarm_ino_t,
	char **, int, struct dirset *, struct xattr_list **, size_t *);

gfarm_error_t inode_xattr_to_uint(const void *, size_t, unsigned int *, int *);
int inode_has_desired_number(struct inode *, int *);
int inode_has_repattr(struct inode *, char **);
int inode_get_replica_spec(struct inode *, char **, int *);
int inode_search_replica_spec(struct inode *, char **, int *);

struct dirset *inode_search_tdirset(struct inode *);

enum inode_scan_choice {
	INODE_SCAN_CONTINUE,
	INODE_SCAN_INTERRUPT,
	INODE_SCAN_RELEASE_GIANT_LOCK
};
int inode_foreach_in_subtree_interruptible(struct inode *, void *,
	enum inode_scan_choice (*)(void *, struct inode *), int (*)(void *));

int inode_is_ok_to_set_dirset(struct inode *);
void inode_subtree_fixup_tdirset(struct inode *, struct dirset *);

void inode_remove_orphan(void);
void inode_free_orphan(void);
void inode_check_and_repair(void);

gfarm_error_t inode_create_file_in_lost_found(
	struct host *, gfarm_ino_t, gfarm_uint64_t, gfarm_off_t,
	struct gfarm_timespec *, struct inode **);

/* debug */
void dir_dump(gfarm_ino_t);
void rootdir_dump(void);


/* exported for a use from a private extension */
gfarm_error_t inode_schedule_file_default(struct file_opening *,
	struct peer *, gfarm_int32_t *, struct host ***);

struct inode_trace_log_info {
	gfarm_ino_t inum;
	gfarm_uint64_t igen;
	gfarm_mode_t imode;
};
