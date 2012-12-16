/*
 * $Id$
 */
struct process;
struct inode;
struct host;

void fsngroup_replicate_file(
	struct inode *, struct host *, const char *,
	int, struct host **);
