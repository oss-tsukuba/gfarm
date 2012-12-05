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
#include "abstract_host_impl.h"
#include "dead_file_copy.h"
#include "back_channel.h"
#include "relay.h"
#include "fsngroup.h"

#else  /* ------------------------ */

#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "auth.h"
#include "gfm_proto.h"

#include "db_access.h"
#include "fsngroup.h"
#include "host.h"
#include "peer.h"
#include "rpcsubr.h"
#include "subr.h"
#include "user.h"

#endif

gfarm_error_t
gfm_server_fsngroup_get_all(
	struct peer *peer,
	int from_client, int skip)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_fsngroup_get_by_names(
	struct peer *peer,
	int from_client, int skip)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_fsngroup_modify(
	struct peer *peer,
	int from_client, int skip)
{
	gfarm_error_t e = GFARM_ERR_UNKNOWN;
	static const char diag[] =
		macro_stringify(GFM_PROTO_FSNGROUP_MODIFY);
	char *hostname = NULL;		/* need to be free'd always */
	char *fsngroupname = NULL;	/* need to be free'd always */

	e = gfm_server_get_request(peer, diag,
		"ss", &hostname, &fsngroupname);
	if (e != GFARM_ERR_NO_ERROR)
		goto bailout;
	if (skip) {
		e = GFARM_ERR_NO_ERROR;
		goto bailout;
	}

	{
		struct host *h = NULL;
		struct user *user = peer_get_user(peer);

		if (!from_client || user == NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			goto reply;
		}

		giant_lock();

		if ((h = host_lookup(hostname)) == NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"host does not exists");
			e = GFARM_ERR_NO_SUCH_OBJECT;
			goto unlock;
		}
		if (!user_is_admin(user)) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			goto unlock;
		}
		if ((e = db_fsngroup_modify(hostname, fsngroupname)) !=
			GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"db_fsngroup_modify failed: %s",
				gfarm_error_string(e));
			goto unlock;
		}
		host_fsngroup_modify(h, fsngroupname);

unlock:
		giant_unlock();
	}
reply:
	e = gfm_server_put_reply(peer, diag, e, "");

bailout:
	if (hostname != NULL)
		free((void *)hostname);
	if (fsngroupname != NULL)
		free((void *)fsngroupname);

	return (e);
}
