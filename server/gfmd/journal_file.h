/*
 * $Id$
 */

#define JOURNAL_FILE_HEADER_SIZE		4096
#define GFARM_JOURNAL_FILE_MAGIC		"GfMj"
#define GFARM_JOURNAL_RECORD_MAGIC		"GfMr"
#define GFARM_JOURNAL_MAGIC_SIZE		4
#define GFARM_JOURNAL_VERSION			0x00000001
#define GFARM_JOURNAL_VERSION_SIZE		4

#define GFARM_JOURNAL_RDONLY	1
#define GFARM_JOURNAL_RDWR	2

enum journal_operation {
	GFM_JOURNAL_BEGIN = 1,
	GFM_JOURNAL_END,

	GFM_JOURNAL_HOST_ADD,
	GFM_JOURNAL_HOST_MODIFY,
	GFM_JOURNAL_HOST_REMOVE,

	GFM_JOURNAL_USER_ADD,
	GFM_JOURNAL_USER_MODIFY,
	GFM_JOURNAL_USER_REMOVE,

	GFM_JOURNAL_GROUP_ADD,
	GFM_JOURNAL_GROUP_MODIFY,
	GFM_JOURNAL_GROUP_REMOVE,

	GFM_JOURNAL_INODE_ADD,
	GFM_JOURNAL_INODE_MODIFY,
	GFM_JOURNAL_INODE_GEN_MODIFY,
	GFM_JOURNAL_INODE_NLINK_MODIFY,
	GFM_JOURNAL_INODE_SIZE_MODIFY,
	GFM_JOURNAL_INODE_MODE_MODIFY,
	GFM_JOURNAL_INODE_USER_MODIFY,
	GFM_JOURNAL_INODE_GROUP_MODIFY,
	GFM_JOURNAL_INODE_ATIME_MODIFY,
	GFM_JOURNAL_INODE_MTIME_MODIFY,
	GFM_JOURNAL_INODE_CTIME_MODIFY,

	GFM_JOURNAL_INODE_CKSUM_ADD,
	GFM_JOURNAL_INODE_CKSUM_MODIFY,
	GFM_JOURNAL_INODE_CKSUM_REMOVE,

	GFM_JOURNAL_FILECOPY_ADD,
	GFM_JOURNAL_FILECOPY_REMOVE,

	GFM_JOURNAL_DEADFILECOPY_ADD,
	GFM_JOURNAL_DEADFILECOPY_REMOVE,

	GFM_JOURNAL_DIRENTRY_ADD,
	GFM_JOURNAL_DIRENTRY_REMOVE,

	GFM_JOURNAL_SYMLINK_ADD,
	GFM_JOURNAL_SYMLINK_REMOVE,

	GFM_JOURNAL_XATTR_ADD,
	GFM_JOURNAL_XATTR_MODIFY,
	GFM_JOURNAL_XATTR_REMOVE,
	GFM_JOURNAL_XATTR_REMOVEALL,

	GFM_JOURNAL_QUOTA_ADD,
	GFM_JOURNAL_QUOTA_MODIFY,
	GFM_JOURNAL_QUOTA_REMOVE,

	GFM_JOURNAL_MDHOST_ADD,
	GFM_JOURNAL_MDHOST_MODIFY,
	GFM_JOURNAL_MDHOST_REMOVE,

	GFM_JOURNAL_FSNGROUP_MODIFY,

	GFM_JOURNAL_NOP,
	GFM_JOURNAL_OPERATION_MAX
};

struct gfp_xdr;
struct journal_file;
struct journal_file_reader;
struct journal_file_writer;

typedef gfarm_error_t (*journal_size_add_op_t)(enum journal_operation,
	size_t *, void *);
typedef gfarm_error_t (*journal_send_op_t)(enum journal_operation,
	void *);
typedef gfarm_error_t (*journal_post_read_op_t)(void *, gfarm_uint64_t,
	enum journal_operation, void *, void *, size_t, int *);
typedef gfarm_error_t (*journal_read_op_t)(void *, struct gfp_xdr *,
	enum journal_operation, void **);
typedef void (*journal_free_op_t)(void *, enum journal_operation, void *);

off_t journal_file_tail(struct journal_file *);
off_t journal_file_size(struct journal_file *);
void journal_file_mutex_lock(struct journal_file *, const char *);
void journal_file_mutex_unlock(struct journal_file *, const char *);
void journal_file_nonfull_cond_signal(struct journal_file_reader *,
	const char *);
gfarm_error_t journal_file_open(const char *, size_t,
	gfarm_uint64_t, struct journal_file **, int);
void journal_file_close(struct journal_file *);
struct journal_file_writer *journal_file_writer(struct journal_file *);
struct journal_file_reader *journal_file_main_reader(struct journal_file *);
gfarm_uint64_t journal_file_get_inital_max_seqnum(struct journal_file *jf);
gfarm_error_t journal_file_write(struct journal_file *,
	gfarm_uint64_t, enum journal_operation, void *,
	journal_size_add_op_t, journal_send_op_t);
gfarm_error_t journal_file_write_raw(struct journal_file *, int,
	unsigned char *, gfarm_uint64_t *, int *);
gfarm_error_t journal_file_read(struct journal_file_reader *, void *,
	journal_read_op_t, journal_post_read_op_t, journal_free_op_t,
	void *, int *);
gfarm_error_t journal_file_read_serialized(struct journal_file_reader *,
	char **, gfarm_uint32_t *, gfarm_uint64_t *, int *);
void journal_file_wait_for_read_completion(struct journal_file_reader *);
void journal_file_wait_until_readable(struct journal_file *);
void journal_file_wait_until_empty(struct journal_file *);
int journal_file_is_closed(struct journal_file *);
int journal_file_is_waiting_until_nonempty(struct journal_file *);

gfarm_error_t journal_file_writer_sync(struct journal_file_writer *writer);
gfarm_error_t journal_file_writer_flush(struct journal_file_writer *);
struct gfp_xdr *journal_file_writer_xdr(struct journal_file_writer *);
off_t journal_file_writer_pos(struct journal_file_writer *);

struct gfp_xdr *journal_file_reader_xdr(struct journal_file_reader *);
void journal_file_reader_committed_pos(struct journal_file_reader *, off_t *,
	gfarm_uint64_t *);
void journal_file_reader_committed_pos_unlocked(struct journal_file_reader *,
	off_t *, gfarm_uint64_t *);
void journal_file_reader_commit_pos(struct journal_file_reader *);
off_t journal_file_reader_fd_pos(struct journal_file_reader *reader);
int journal_file_reader_is_expired(struct journal_file_reader *);
void journal_file_reader_disable_block_writer(struct journal_file_reader *);
void journal_file_reader_invalidate(struct journal_file_reader *);
void journal_file_reader_close(struct journal_file_reader *);
gfarm_error_t journal_file_reader_reopen_if_needed(struct journal_file *,
	const char *, struct journal_file_reader **, gfarm_uint64_t, int *);

const char *journal_operation_name(enum journal_operation);

#define GFLOG_DEBUG_WITH_OPE(logid, msg, e, ope) \
	gflog_debug(logid, "ope=%s " msg " : %s", \
		journal_operation_name(ope), gfarm_error_string(e))
#define GFLOG_ERROR_WITH_OPE(logid, msg, e, ope) \
	gflog_error(logid, "ope=%s " msg " : %s", \
		journal_operation_name(ope), gfarm_error_string(e))
#define GFLOG_ERROR_WITH_SN(logid, msg, e, seqnum, ope) \
	gflog_error(logid, "seqnum=%llu ope=%s " msg " : %s", \
		(unsigned long long)seqnum, \
		journal_operation_name(ope), gfarm_error_string(e))
