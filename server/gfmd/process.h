typedef gfarm_int32_t gfarm_pid_t;
struct process;

struct inode;
struct host;

struct process *process_lookup(gfarm_pid_t);
void process_add_ref(struct process *);
void process_del_ref(struct process *);

struct user *process_get_user(struct process *);
struct inode *process_get_cwd(struct process *);
gfarm_error_t process_set_cwd(struct process *, struct inode *);

struct file_opening {
	/*
	 * end marker:
	 * {fo->opening_prev, fo->opening_next} == &fo->inode->openings
	 */
	struct file_opening *opening_prev, *opening_next;

	struct inode *inode;
	struct host *spool_host;
	int flag;
};

gfarm_error_t process_open_file(struct process *, struct inode *,
	gfarm_int32_t, struct host *, gfarm_int32_t *);
gfarm_error_t process_close_file_read(struct process *, struct host *,
	int, struct gfarm_timespec *);
gfarm_error_t process_close_file_write(struct process *, struct host *,
	int, struct gfarm_timespec *, struct gfarm_timespec *);

struct peer;
gfarm_error_t gfm_server_process_alloc(struct peer *, int);
gfarm_error_t gfm_server_process_free(struct peer *, int);
gfarm_error_t gfm_server_process_set(struct peer *, int);
