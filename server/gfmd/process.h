/*
 * NOTE:
 * only slave gfmd knows this bit representation.
 * clients and master gfmd should NOT assume any bit pattern,
 * except GFARM_DESCRIPTOR_INVALID is an invalid descriptor.
 */
#define FD_BIT_SLAVE		0x80000000
#define FD_IS_SLAVE_ONLY(fd)	(((fd) & FD_BIT_SLAVE) != 0)

#define FLAG_IS_SLAVE_ONLY(flag) (((flag) & GFARM_FILE_SLAVE_ONLY) != 0)

struct process;

struct inode;
struct host;
struct file_replication;
struct replication_info;

struct process *process_lookup(gfarm_pid_t);

gfarm_error_t process_new_generation_wait(struct peer *, int,
	gfarm_error_t (*)(struct peer *, void *, int *), void *);
gfarm_error_t process_new_generation_done(struct process *, struct peer *,
	int, gfarm_int32_t);

void process_attach_peer(struct process *, struct peer *);
void process_detach_peer(struct process *, struct peer *);

struct user *process_get_user(struct process *);

gfarm_error_t process_verify_fd(struct process *, int);
gfarm_error_t process_record_replica_spec(struct process *, int, int, char *);
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
gfarm_error_t process_get_path_for_trace_log(struct process *,
	int, char **);
gfarm_error_t process_set_path_for_trace_log(struct process *,
	int, char *);

struct file_opening {
	/*
	 * end marker:
	 * 	{fo->opening_prev, fo->opening_next}
	 *	== &fo->inode->u.c.state->openings
	 */
	struct file_opening *opening_prev, *opening_next;

	struct inode *inode;
	int flag;

	struct peer *opener;
	union {
		struct opening_file {
			struct peer *spool_opener;
			struct host *spool_host;
			int desired_replica_number;
			char *repattr;

			/* only used by client initiated replication */
			struct replication_info *replica_source;
		} f;
		struct opening_dir {
			gfarm_off_t offset;
			char *key;

			/* the followings are only used if FD_IS_SLAVE_ONLY */
			gfarm_uint64_t igen;
		} d;
	} u;

	char *path_for_trace_log; /* XXX FIXME not maintained if "." or ".." */
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
gfarm_error_t process_open_slave_file(struct process *, struct inode *,
	gfarm_int32_t, int, struct peer *, struct host *, gfarm_int32_t *);
gfarm_error_t process_schedule_file(struct process *,
	struct peer *, int, gfarm_int32_t *, struct host ***);
gfarm_error_t process_reopen_file(struct process *,
	struct peer *, struct host *, int,
	gfarm_ino_t *, gfarm_uint64_t *, gfarm_int32_t *,
	gfarm_int32_t *, gfarm_int32_t *);
gfarm_error_t process_close_file(struct process *, struct peer *, int, char **);
gfarm_error_t process_close_file_read(struct process *, struct peer *, int,
	struct gfarm_timespec *);
gfarm_error_t process_close_file_write(struct process *, struct peer *, int,
	gfarm_off_t, struct gfarm_timespec *, struct gfarm_timespec *,
	gfarm_int32_t *, gfarm_ino_t *, gfarm_int64_t *, gfarm_int64_t *,
	char **);

gfarm_error_t process_cksum_set(struct process *, struct peer *, int,
	const char *, size_t, const char *,
	gfarm_int32_t, struct gfarm_timespec *);
gfarm_error_t process_cksum_get(struct process *, struct peer *, int,
	char **, size_t *, char **, gfarm_int32_t *);
gfarm_error_t process_get_file_opening(struct process *, int,
	struct file_opening **);
gfarm_error_t process_get_slave_file_opening(struct process *, int,
	struct file_opening **);

struct peer;
gfarm_error_t gfm_server_process_alloc(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
#ifdef NOT_USED
gfarm_error_t gfm_server_process_alloc_child(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
#endif
gfarm_error_t gfm_server_process_free(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_process_set(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);

gfarm_error_t gfm_server_bequeath_fd(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_inherit_fd(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);

gfarm_error_t process_prepare_to_replicate(struct process *, struct peer *,
	struct host *, struct host *, int, gfarm_int32_t,
	struct file_replication **, struct inode **);
gfarm_error_t process_replica_adding(struct process *, struct peer *,
	struct host *, struct host *, int, struct inode **);
gfarm_error_t process_replica_added(struct process *, struct peer *,
	struct host *, int, int, gfarm_int64_t, gfarm_int32_t, gfarm_off_t);
