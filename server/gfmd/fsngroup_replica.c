/*
 * $Id$
 */
#if 0
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

/* for host_addr_lookup() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <pthread.h>

#include <gfarm/gfarm.h>

#include "internal_host_info.h"

#include "gfutil.h"
#include "hash.h"
#include "thrsubr.h"

#include "metadb_common.h"	/* gfarm_host_info_free_except_hostname() */
#include "gfp_xdr.h"
#include "gfm_proto.h" /* GFM_PROTO_SCHED_FLAG_* */
#include "gfs_proto.h" /* GFS_PROTOCOL_VERSION */
#include "auth.h"
#include "config.h"

#include "callout.h"
#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "host.h"
#include "mdhost.h"
#include "user.h"
#include "peer.h"
#include "inode.h"
#include "abstract_host.h"
#include "dead_file_copy.h"
#include "back_channel.h"
#include "fsngroup.h"
#include "fsngroup_replica.h"
#else  /* --------------------------------- */

#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "host.h"
#include "inode.h"

#endif

/*****************************************************************************/

struct file_copy {
	struct file_copy *host_next;
	struct host *host;
	int flags;
};

/*****************************************************************************/
/*
 * Exported APIs:
 */

char *
gfarm_server_fsngroup_find_replicainfo_by_inode(struct inode *inode)
{
	/*
	 * Not yet.
	 */
	(void)inode;
	return (NULL);	/* will be free'd in file_opening_free(). */
}

void
gfarm_server_fsngroup_replicate_file(struct inode *inode,
	struct host *src_host, char *info, struct file_copy *exclusions)
{
	struct host **ehosts = NULL;
	size_t nehosts = 0;

	(void)inode;

	/*
	 * Convert a linked-list into an array to increase search speed.
	 */
	if (exclusions != NULL) {
		struct file_copy *orig = exclusions;

		do {
			nehosts++;
			orig = orig->host_next;
		} while (orig != NULL);

		ehosts = (struct host **)alloca(
			sizeof(struct hosts *) * nehosts);
		if (ehosts == NULL) {
			gflog_error(GFARM_MSG_UNFIXED,
				"gfarm_server_fsngroup_replicate_file(): "
				"Insufficient memory to allocate "
				"%zu of sturct host *.",
				nehosts);
			return;
		}

		orig = exclusions;
		nehosts = 0;

		do {
			ehosts[nehosts++] = orig->host;
			orig = orig->host_next;
		} while (orig != NULL);
	}

	/*
	 * Not yet.
	 */
	gflog_info(GFARM_MSG_UNFIXED,
		"gfarm_server_fsngroup_replicate_file(): "
		"replicate inode %llu@%s to fsngroup '%s'.",
		(long long)inode_get_number(inode),
		host_name(src_host),
		info);

	/*
	 * What to do are:
	 *
	 *	1) Parse info and create (fsngroup, amount) tupples.
	 *	2) For each the tupple:
	 *		a) Create a set (hosts:fsngroup - ehosts)
	 *		b) For each a host in the set:
	 *			Create a replica.
	 */
}
