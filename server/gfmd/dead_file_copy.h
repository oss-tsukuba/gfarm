struct dead_file_copy;
struct dead_file_copy_list;
struct host;

struct dead_file_copy *dead_file_copy_new(
	gfarm_ino_t, gfarm_uint64_t, struct host *,
	struct dead_file_copy_list **);
gfarm_ino_t dead_file_copy_get_ino(struct dead_file_copy *);
gfarm_uint64_t dead_file_copy_get_gen(struct dead_file_copy *);
struct host *dead_file_copy_get_host(struct dead_file_copy *);

int dead_file_copy_is_removable_default(struct dead_file_copy *);
extern int (*dead_file_copy_is_removable)(struct dead_file_copy *);

void dead_file_copy_list_add(struct dead_file_copy_list **,
	struct dead_file_copy *);
int dead_file_copy_list_free_check(struct dead_file_copy_list *);

void dead_file_copy_schedule_removal(struct dead_file_copy *);

void dead_file_copy_host_becomes_up(struct host *);
void dead_file_copy_host_removed(struct host *);
struct dead_file_copy_list;
void dead_file_copy_inode_status_changed(struct dead_file_copy_list *);

void dead_file_copy_mark_kept(struct dead_file_copy *);

int dead_file_copy_count_by_inode(struct dead_file_copy_list *,
	gfarm_uint64_t, int);
gfarm_error_t dead_file_copy_info_by_inode(struct dead_file_copy_list *,
	gfarm_uint64_t, int, int *, char **, gfarm_int64_t *, gfarm_int32_t *);
int dead_file_copy_existing(struct dead_file_copy_list *,
	gfarm_uint64_t, struct host *);

void dead_file_copy_init_load(void);
void dead_file_copy_init(int);

struct netsendq_type;
extern struct netsendq_type gfs_proto_fhremove_queue;
