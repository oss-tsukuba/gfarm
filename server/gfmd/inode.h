gfarm_error_t inode_init(void);
gfarm_error_t dir_entry_init(void);
gfarm_error_t file_copy_init(void);
gfarm_error_t dead_file_copy_init(void);

struct inode;

struct host;
struct user;
struct group;
struct process;

int inode_is_dir(struct inode *);
int inode_is_file(struct inode *);
gfarm_ino_t inode_get_number(struct inode *);
gfarm_int64_t inode_get_gen(struct inode *);
gfarm_int64_t inode_get_nlink(struct inode *);
struct user *inode_get_user(struct inode *);
struct group *inode_get_group(struct inode *);
gfarm_off_t inode_get_size(struct inode *);
gfarm_int64_t inode_get_ncopy(struct inode *);

gfarm_mode_t inode_get_mode(struct inode *);
gfarm_error_t inode_set_mode(struct inode *, gfarm_mode_t);
gfarm_error_t inode_set_owner(struct inode *, struct user *, struct group *);
struct gfarm_timespec *inode_get_atime(struct inode *);
struct gfarm_timespec *inode_get_mtime(struct inode *);
struct gfarm_timespec *inode_get_ctime(struct inode *);
void inode_set_atime(struct inode *, struct gfarm_timespec *);
void inode_set_mtime(struct inode *, struct gfarm_timespec *);
void inode_accessed(struct inode *);
void inode_modified(struct inode *);
void inode_status_changed(struct inode *);

gfarm_error_t inode_access(struct inode *, struct user *, int);

struct inode *inode_lookup(gfarm_ino_t);
gfarm_error_t inode_lookup_by_name(char *, struct process *, int,
	struct inode **);
gfarm_error_t inode_create_file(char *, struct process *, int, gfarm_mode_t,
	struct inode **, int *);
gfarm_error_t inode_unlink(char *, struct process *);

gfarm_error_t inode_add_replica(struct inode *, struct host *);
gfarm_error_t inode_remove_replica(struct inode *, struct host *);

struct file_opening;

void inode_open(struct file_opening *);
void inode_close_read(struct file_opening *, struct gfarm_timespec *);
void inode_close_write(struct file_opening *,
	gfarm_off_t, struct gfarm_timespec *, struct gfarm_timespec *);

int inode_has_replica(struct inode *, struct host *);
struct host *inode_schedule_host_for_write(struct inode *, struct host *);
