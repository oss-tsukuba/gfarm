/*
 * $Id$
 */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

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
#include "process.h"
#include "inode.h"
#include "dir.h"
#include "abstract_host.h"
#include "abstract_host_impl.h"
#include "dead_file_copy.h"
#include "back_channel.h"
#include "relay.h"
#include "fsngroup.h"
#include "fsngroup_replica.h"
#include "repattr.h"

/*
 * Make us sure our having the giant_lock acquired.
 */
void
fsngroup_replicate_file(struct inode *inode,
	struct host *src_host, const char *repattr,
	int n_exceptions, struct host **exceptions)
{
	gfarm_error_t e;
	size_t i, nreps = 0;
	gfarm_repattr_t *reps = NULL;
	int n_ghosts;
	struct host **ghosts;
	static const char diag[] = "fsngroup_replicate_file()";

	assert(repattr != NULL && src_host != NULL);

	gflog_debug(GFARM_MSG_UNFIXED,
		"gfarm_server_fsngroup_replicate_file(): "
		"replicate inode %llu@%s to fsngroup '%s'.",
		(long long)inode_get_number(inode),
		host_name(src_host),
		repattr);

	e = gfarm_repattr_parse(repattr, &reps, &nreps);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
		return;
	}
	if (nreps == 0) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: can't parse a repattr: '%s'.", diag, repattr);
		/* fall through */
	}

	for (i = 0; i < nreps; i++) {
		e = fsngroup_get_hosts(gfarm_repattr_group(reps[i]),
		    &n_ghosts, &ghosts);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "%s: fsngroup_get_hosts(%s): %s",
			    diag, (char *)gfarm_repattr_group(reps[i]),
			    gfarm_error_string(e));
			continue;
		}
		inode_schedule_replication(inode, src_host,
		    &n_ghosts, ghosts, &n_exceptions, exceptions,
		    gfarm_repattr_amount(reps[i]), diag);

		free(ghosts);
	}

	if (reps != NULL) {
		for (i = 0; i < nreps; i++)
			gfarm_repattr_free(reps[i]);
	}
	free(reps);
}
