struct process;

struct inode;
struct host;

struct process *process_lookup(gfarm_pid_t);
void process_attach_peer(struct process *, struct peer *);
void process_detach_peer(struct process *, struct peer *);

struct user *process_get_user(struct process *);

gfarm_error_t process_verify_fd(struct process *, int);
gfarm_error_t process_get_file_inode(struct process *, int,
	struct inode **);
gfarm_error_t process_get_file_writable(struct process *, struct peer *, int);

gfarm_error_t process_get_dir_offset(struct process *, struct peer *, int,
	gfarm_off_t *);
gfarm_error_t process_set_dir_offset(struct process *, struct peer *, int,
	gfarm_off_t);
gfarm_error_t process_get_dir_key(struct process *, struct peer *, int,
	char **, int *);
gfarm_error_t process_set_dir_key(struct process *, struct peer *, int,
	char *, int);
gfarm_error_t process_clear_dir_key(struct process *, struct peer *, int);

struct file_opening {
	/*
	 * end marker:
	 * {fo->opening_prev, fo->opening_next} == &fo->inode->openings
	 */
	struct file_opening *opening_prev, *opening_next;

	struct inode *inode;
	int flag;

	struct peer *opener;
	union {
		struct opening_file {
			struct peer *spool_opener;
			struct host *spool_host;
		} f;
		struct opening_dir {
			gfarm_off_t offset;
			char *key;
		} d;
	} u;
};

/*
 * a client opened a file:
 *		file_opening:opener == client_peer
 *		file_opening:u.f.spool_opener == NULL
 * then, a gfsd reopened it:
 *		file_opening:opener == client_peer
 *		file_opening:u.f.spool_opener == gfsd_peer
 * then
 *	(a) client closed the file:
 *		file_opening:opener == NULL
 *		file_opening:u.f.spool_opener == gfsd_peer
 *	(b) gfsd closed the file:
 *		file_opening:opener == client_peer
 *		file_opening:u.f.spool_opener == NULL
 *
 * a gfsd opened a file:
 *		file_opening:opener == gfsd_peer
 *		file_opening:u.f.spool_opener == gfsd_peer
 */

gfarm_error_t process_open_file(struct process *, struct inode *,
	gfarm_int32_t, int, struct peer *, struct host *, gfarm_int32_t *);
gfarm_error_t process_reopen_file(struct process *,
	struct peer *, struct host *, int,
	gfarm_ino_t *, gfarm_uint64_t *, gfarm_int32_t *,
	gfarm_int32_t *, gfarm_int32_t *);
gfarm_error_t process_close_file(struct process *, struct peer *, int);
gfarm_error_t process_close_file_read(struct process *, struct peer *, int,
	struct gfarm_timespec *);
gfarm_error_t process_close_file_write(struct process *, struct peer *, int,
	gfarm_off_t, struct gfarm_timespec *, struct gfarm_timespec *);

gfarm_error_t process_cksum_set(struct process *, struct peer *, int,
	const char *, size_t, const char *,
	gfarm_int32_t, struct gfarm_timespec *);
gfarm_error_t process_cksum_get(struct process *, struct peer *, int,
	char **, size_t *, char **, gfarm_int32_t *);

struct peer;
gfarm_error_t gfm_server_process_alloc(struct peer *, int, int);
gfarm_error_t gfm_server_process_alloc_child(struct peer *, int, int);
gfarm_error_t gfm_server_process_free(struct peer *, int, int);
gfarm_error_t gfm_server_process_set(struct peer *, int, int);

gfarm_error_t gfm_server_bequeath_fd(struct peer *, int, int);
gfarm_error_t gfm_server_inherit_fd(struct peer *, int, int);

gfarm_error_t process_replica_adding(struct process *, struct peer *,
	struct host *, char *, int, gfarm_ino_t *, gfarm_uint64_t *,
	gfarm_int64_t *, gfarm_int32_t *);
gfarm_error_t process_replica_added(struct process *, struct peer *,
	struct host *, int, int, gfarm_int64_t, gfarm_int32_t);
