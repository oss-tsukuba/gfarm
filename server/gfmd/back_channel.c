/*
 * $Id$
 */

#include <stdlib.h>
#include <stdarg.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "gfutil.h"
#include "gfp_xdr.h"
#include "gfs_proto.h"
#include "auth.h"
#include "host.h"
#include "peer.h"
#include "subr.h"
#include "back_channel.h"

static gfarm_error_t
gfs_client_rpc_back_channel(struct peer *peer, int just, int command,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int errcode;
	struct gfp_xdr *conn = peer_get_conn(peer);

	if (conn == NULL)
		return (GFARM_ERR_SOCKET_IS_NOT_CONNECTED);

	va_start(ap, format);
	e = gfp_xdr_vrpc(conn, just, command, &errcode, &format, &ap);
	va_end(ap);
	if (IS_CONNECTION_ERROR(e)) {
		/* back channel is disconnected */
		gflog_warning("back channel disconnected: %s",
			      gfarm_error_string(e));
		host_peer_disconnect(peer_get_host(peer));
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_fhremove(struct peer *peer, gfarm_ino_t ino, gfarm_uint64_t gen)
{
	return (gfs_client_rpc_back_channel(
			peer, 0, GFS_PROTO_FHREMOVE, "ll/", ino, gen));
}

gfarm_error_t
remover(void *h)
{
	struct host *host = h;
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e2;

	while (host_is_up(host)) {
		e = host_remove_replica(host);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	e2 = host_remove_replica_dump(host);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfm_server_switch_back_channel(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (from_client)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (host_peer(peer_get_host(peer)) != peer)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		e = GFARM_ERR_NO_ERROR;
	e2 = gfm_server_put_reply(peer, "switch_back_channel", e, "");

	if (e == GFARM_ERR_NO_ERROR && e2 == GFARM_ERR_NO_ERROR)
		e2 = remover(peer_get_host(peer));
	return (e2);
}
