struct dead_file_copy;
struct host;

struct dead_file_copy *dead_file_copy_new(
	gfarm_ino_t, gfarm_uint64_t, struct host *);
gfarm_ino_t dead_file_copy_get_ino(struct dead_file_copy *);
gfarm_uint64_t dead_file_copy_get_gen(struct dead_file_copy *);
struct host *dead_file_copy_get_host(struct dead_file_copy *);

int dead_file_copy_is_removable_default(struct dead_file_copy *);
extern int (*dead_file_copy_is_removable)(struct dead_file_copy *);

void removal_pendingq_enqueue(struct dead_file_copy *);
struct dead_file_copy *removal_pendingq_dequeue(void);
void removal_finishedq_enqueue(struct dead_file_copy *, gfarm_int32_t);
void host_busyq_enqueue(struct dead_file_copy *);

void dead_file_copy_scan_deferred_all(void);
void dead_file_copy_host_becomes_down(struct host *);
void dead_file_copy_host_becomes_up(struct host *);
void dead_file_copy_host_removed(struct host *);
void dead_file_copy_replica_status_changed(gfarm_ino_t, struct host *);
void dead_file_copy_inode_status_changed(gfarm_ino_t);

void dead_file_copy_mark_kept(struct dead_file_copy *);
void dead_file_copy_mark_deferred(struct dead_file_copy *);
gfarm_error_t dead_file_copy_mark_lost(
	gfarm_ino_t, gfarm_uint64_t, struct host *);

int dead_file_copy_count_by_inode(gfarm_ino_t, gfarm_uint64_t, int);
gfarm_error_t dead_file_copy_info_by_inode(gfarm_ino_t, gfarm_uint64_t, int,
	int *, char **, gfarm_int64_t *, gfarm_int32_t *);
int dead_file_copy_existing(gfarm_ino_t, gfarm_uint64_t, struct host *);

void dead_file_copy_init_load(void);
void dead_file_copy_init(int);
