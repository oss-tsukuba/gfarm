/*
 * $Id$
 */

#ifdef ENABLE_JOURNAL

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
gfarm_error_t db_journal_reader_reopen(struct journal_file_reader **,
	gfarm_uint64_t);
gfarm_error_t db_journal_fetch(struct journal_file_reader *, gfarm_uint64_t,
	char **, int *, gfarm_uint64_t *, gfarm_uint64_t *, int *,
	const char *);
gfarm_error_t db_journal_recvq_enter(gfarm_uint64_t, gfarm_uint64_t, int,
	unsigned char *, const char *);

void *db_journal_store_thread(void *);
void *db_journal_recvq_thread(void *);
void *db_journal_apply_thread(void *);
void db_journal_boot_apply(void);

#endif
