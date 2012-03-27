/*
 * $Id$
 */
struct process;
struct inode;
struct host;

void gfarm_server_process_record_replication_attribute(
	struct process *, int,
	struct inode *, struct inode *);
void gfarm_server_fsngroup_replicate_file(
	struct inode *, struct host *, const char *, char **, size_t);
