gfarm_error_t inode_init(void);
gfarm_error_t dir_entry_init(void);
gfarm_error_t file_copy_init(void);
gfarm_error_t dead_file_copy_init(void);

struct inode;

/*
 * although external representation of gfarm_ino_t is 64bit,
 * we use 32bit number for in-core version gfmd.
 */
typedef gfarm_int32_t internal_ino_t;

struct host;
struct user;
struct group;
struct process;

int inode_is_dir(struct inode *);
int inode_is_file(struct inode *);
internal_ino_t inode_get_number(struct inode *);
struct user *inode_get_user(struct inode *);

gfarm_mode_t inode_get_mode(struct inode *);
gfarm_error_t inode_set_mode(struct inode *, gfarm_mode_t);
gfarm_error_t inode_set_owner(struct inode *, struct user *, struct group *);
struct gfarm_timespec *inode_get_atime(struct inode *);

gfarm_error_t inode_access(struct inode *, struct user *, int);

struct inode *inode_lookup(internal_ino_t);
gfarm_error_t inode_lookup_by_name(char *, struct process *, struct inode **);
gfarm_error_t inode_create_file(char *, struct process *, gfarm_mode_t,
	struct inode **, int *);
gfarm_error_t inode_unlink(char *, struct process *);

gfarm_error_t inode_add_replica(struct inode *, struct host *);
gfarm_error_t inode_remove_replica(struct inode *, struct host *);

struct file_opening;

void inode_open(struct file_opening *);
void inode_close_read(struct file_opening *, struct gfarm_timespec *);
void inode_close_write(struct file_opening *,
	struct gfarm_timespec *, struct gfarm_timespec *);

int inode_has_replica(struct inode *, struct host *);
struct host *inode_schedule_host_for_write(struct inode *, struct host *);
