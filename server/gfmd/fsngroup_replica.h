/*
 * $Id$
 */
struct process;
struct inode;
struct host;

/**
 *	Select specified # of hosts for replication scheduling.
 *
 *	@param	[in]	hosts		An array of the candidate hosts
 *	@param	[in]	nhosts		A # of the hosts
 *	@param	[out]	indices		An indices for the seleted hosts
 *	@param	[in]	maxindices	A max # of the indices
 *
 *	@return	A # of selected hosts
 */
typedef size_t (*fsngroup_sched_proc_t)(
	const char * const hosts[], size_t nhosts,
	size_t *indices, size_t maxindices);

void gfarm_server_process_record_replication_attribute(
	struct process *, int,
	struct inode *, struct inode *);
void gfarm_server_fsngroup_replicate_file(
	struct inode *, struct host *, const char *, char **, size_t,
	fsngroup_sched_proc_t);
