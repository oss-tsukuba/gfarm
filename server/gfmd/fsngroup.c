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

#else
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "auth.h"

#include "peer.h"

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
	return (GFARM_ERR_NO_ERROR);
}
