/*
 * $Id$
 */

#ifdef ENABLE_JOURNAL

struct db_ops;
extern struct db_ops db_journal_ops;
struct journal_file_reader;
enum journal_operation;

gfarm_uint64_t db_journal_next_seqnum(void);
void db_journal_set_apply_ops(const struct db_ops *);
gfarm_error_t db_journal_read(struct journal_file_reader *, void *,
	gfarm_error_t (*)(void *, gfarm_uint64_t, enum journal_operation,
	void *, size_t), int *);
gfarm_error_t db_journal_add_reader(struct journal_file_reader **);
gfarm_error_t db_journal_fetch(struct journal_file_reader *, gfarm_uint64_t,
	char **, int *, gfarm_uint64_t *, gfarm_uint64_t *, const char *);
void *db_journal_store_thread(void *);

#endif
