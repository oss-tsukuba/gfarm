/*
 * $Id$
 */

#define DB_SEQNUM_MASTER_NAME	""

struct db_ops;
extern struct db_ops db_journal_ops;
struct journal_file_reader;
enum journal_operation;

gfarm_uint64_t db_journal_next_seqnum(void);
gfarm_uint64_t db_journal_get_current_seqnum(void);
void db_journal_set_apply_ops(const struct db_ops *);
void db_journal_set_fail_store_op(void (*)(void));
gfarm_error_t db_journal_read(struct journal_file_reader *, void *,
	gfarm_error_t (*)(void *, gfarm_uint64_t, enum journal_operation,
	void *, void *, size_t, int *), void *, int *);
void db_journal_wait_for_apply_thread(void);
gfarm_error_t db_journal_reader_reopen_if_needed(const char *,
	struct journal_file_reader **, gfarm_uint64_t, int *);
gfarm_error_t db_journal_fetch(struct journal_file_reader *, gfarm_uint64_t,
	char **, int *, gfarm_uint64_t *, gfarm_uint64_t *, int *,
	const char *);
gfarm_error_t db_journal_recvq_enter(gfarm_uint64_t, gfarm_uint64_t, int,
	unsigned char *);
void db_journal_cancel_recvq();
void db_journal_set_sync_op(gfarm_error_t (*func)(gfarm_uint64_t));
gfarm_error_t db_journal_file_writer_sync(void);
void db_journal_wait_until_readable(void);

void *db_journal_store_thread(void *);
void *db_journal_recvq_thread(void *);
void *db_journal_apply_thread(void *);
void db_journal_reset_slave_transaction_nesting(void);
void db_journal_init_seqnum(void);
struct peer;
gfarm_error_t db_journal_init(void (*)(struct peer *));
gfarm_error_t db_journal_init_status(void); /* currently only for regress */
