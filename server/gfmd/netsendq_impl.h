/*
 * Only the following files are allowed to include this header.
 *	- dead_file_copy.c
 *	- file_replication.c
 *
 * Any other source code shouldn't use this file.
 */

struct netsendq_entry;
struct netsendq_type {
	void *(*send)(void *);
	void (*finalize)(struct netsendq_entry *);
	int window_size;
	int flags;	/* NETSENDQ_FLAG_* */
	int type_index; /* NETSENDQ_TYPE_GF?_* */
};

#if 0
enum netsendq_entry_state {
	netsendq_entry_send_pending, /* only on netsendq::pendingsq[type] */
	netsendq_entry_send_ready,	/*   on netsendq::readyq */
	netsendq_entry_inflight,	/*   already sent */
	netsendq_entry_finalize_pending, /*  on netsendq_manager::finalizeq */
	netsendq_entry_finalizing,
	netsendq_entry_out_of_queue /* special state per netsendq_type */
};
#endif

struct netsendq_entry {
	const struct netsendq_type *sendq_type; /* this pointer is immutable */

	/*
	 * either
	 * - for same abstract_host, netsendq::pendingqs[type] entries.
	 * or
	 * - for same netsendq_manager, netsendq_manager::finalizeq entries.
	 * or
	 * - on any other type-specific queue.
	 */
	GFARM_HCIRCLEQ_ENTRY(netsendq_entry) workq_entries;

	/*
	 * for same abstract_host, readyq entries.
	 */
	GFARM_STAILQ_ENTRY(netsendq_entry) readyq_entries;

	struct abstract_host *abhost; /* this pointer is immutable */

	gfarm_error_t result; /* available if on netsendq_finalizer queue */

	pthread_mutex_t entry_mutex;
	int sending, finalize_pending; /* protected by entry_mutex */
};

void netsendq_entry_init(struct netsendq_entry *, struct netsendq_type *);
void netsendq_entry_destroy(struct netsendq_entry *);
