#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <inttypes.h>	/* more portable than <stdint.h> on UNIX variants */
#include <sys/socket.h>
#include <netinet/in.h> /* ntoh[ls]()/hton[ls]() on glibc */
#include <netdb.h>

#include <gfarm/gfarm.h>

#include "gfnetdb.h"

#include "host_address.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "context.h"
#include "auth.h" /* for gfarm_auth_random() */
#include "hostspec.h" /* GFARM_SOCKADDR_STRLEN */

#include "subr.h"
#include "watcher.h"

#include "host.h"
#include "back_channel.h"
#include "gfmd.h"

static int
gfs_client_failover_notify_request(int sock, int retry_count, const char *xid,
	const char *hostname)
{
	char buffer[GFS_UDP_RPC_SIZE_MAX], *p = buffer;
	gfarm_uint32_t u32;
	ssize_t namelen, rv;
	int save_errno;

	if (debug_mode)
		gflog_info(GFARM_MSG_1004061,
		    "failover notifiy: sending to %s", hostname);
	u32 = htonl(GFS_UDP_RPC_MAGIC);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(GFS_UDP_RPC_TYPE_REQUEST);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(retry_count);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	memcpy(p, xid, GFS_UDP_RPC_XID_SIZE);
	p += GFS_UDP_RPC_XID_SIZE;

	u32 = htonl(GFS_UDP_PROTO_FAILOVER_NOTIFY);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	namelen = strlen(gfarm_ctxp->metadb_server_name);
	if (namelen > GFARM_MAXHOSTNAMELEN) { /* sanity */
		gflog_error(GFARM_MSG_1004062,
		    "metadb_server_name %s: too long name (%zd > %d)",
		    gfarm_ctxp->metadb_server_name,
		    namelen, GFARM_MAXHOSTNAMELEN);
		return (EMSGSIZE);
	}
	u32 = htonl(namelen);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	memcpy(p, gfarm_ctxp->metadb_server_name, namelen); p += namelen;

	u32 = htonl(gfmd_port); /* not 16bit, but 32bit */
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	rv = write(sock, buffer, p - buffer);
	if (rv == -1) {
		save_errno = errno;
		gflog_notice(GFARM_MSG_1004063,
		    "failover_notify_reqeust(%s): %s",
		    hostname, strerror(save_errno));
		return (save_errno);
	} else if (rv != p - buffer) {
		gflog_error(GFARM_MSG_1004064,
		    "failover_notify_reqeust(%s): short write: %zd != %zd",
		    hostname, rv, p - buffer);
		return (ERANGE);
	} else {
		return (0);
	}
}

static gfarm_error_t
gfs_client_failover_notify_result(int sock, int retry_count, const char *xid,
	const char *hostname)
{
	unsigned char buffer[GFS_UDP_RPC_SIZE_MAX], *p = buffer;
	gfarm_uint32_t u32, got_rpc_magic;
	gfarm_uint32_t got_rpc_type, got_retry_count, got_request_type;
	gfarm_error_t got_rpc_result;
	ssize_t rv;
	int save_errno, xid_match;

	rv = read(sock, buffer, sizeof buffer);
	if (rv == -1) {
		save_errno = errno;
		gflog_info(GFARM_MSG_1004065,
		    "failover_notify_result(%s): %s",
		    hostname, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	if (rv == GFS_UDP_PROTO_OLD_LOADAV_REPLY_SIZE) {
		gflog_notice(GFARM_MSG_1004066,
		    "failover_notify_result(%s): gfsd version is obsolete ",
		    hostname);
		return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
	}
	if (rv != GFS_UDP_PROTO_FAILOVER_NOTIFY_REPLY_SIZE) {
		gflog_notice(GFARM_MSG_1004067,
		    "failover_notify_result(%s): unknown reply size: %zd",
		    hostname, rv);
		return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
	}

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_rpc_magic = ntohl(u32);

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_rpc_type = ntohl(u32);

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_retry_count = ntohl(u32);

	xid_match = memcmp(p, xid, GFS_UDP_RPC_XID_SIZE) == 0;
	p += GFS_UDP_RPC_XID_SIZE;

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_request_type = ntohl(u32);

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_rpc_result = ntohl(u32);

	assert(p - buffer == GFS_UDP_PROTO_FAILOVER_NOTIFY_REPLY_SIZE);

	if (got_rpc_magic != GFS_UDP_RPC_MAGIC ||
	    got_rpc_type != GFS_UDP_RPC_TYPE_REPLY ||
	    got_retry_count > retry_count ||
	    !xid_match ||
	    got_request_type != GFS_UDP_PROTO_FAILOVER_NOTIFY) {
		gflog_notice(GFARM_MSG_1004068,
		    "failover_notify_result(%s): rpc not supported: "
		    "type=0x%08x, retry_count=0x%08x (should be <= 0x%08x), "
		    "xid=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x "
		    "(should be %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x), "
		    "request=0x%08x",
		    hostname, got_rpc_type, got_retry_count, retry_count,
		    buffer[8], buffer[9], buffer[10], buffer[11],
		    buffer[12], buffer[13], buffer[14], buffer[15],
		    xid[0], xid[1], xid[2], xid[3], xid[4],
		    xid[5], xid[6], xid[7], got_request_type);
		return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
	}
	if (got_rpc_result != GFARM_ERR_NO_ERROR)
		gflog_info(GFARM_MSG_1004069,
		    "failover_notify_result(%s): result: %s (0x%x)",
		    hostname,
		    gfarm_error_string(got_rpc_result), got_rpc_result);

	return (got_rpc_result);
}

static int
udp_connect_to(const char *hostname, int port, struct gfarm_host_address *addr)
{
	int save_errno, error, sock;
	char addr_string[NI_MAXHOST], serv_string[NI_MAXSERV];

	if (addr->sa_family != AF_INET &&
	    addr->sa_family != AF_INET6)
		return (-1);
	if ((addr->sa_family == AF_INET &&
	     addr->sa_addrlen != sizeof(struct sockaddr_in)) ||
	    (addr->sa_family == AF_INET6 &&
	     addr->sa_addrlen != sizeof(struct sockaddr_in6))) {
		gflog_error(GFARM_MSG_1004080,
		    "failover_notify: resolved hostname `%s': "
		    "unexpected address length %d, should be %zd",
		    hostname,
		    (int)addr->sa_addrlen,
		    addr->sa_family == AF_INET ?
		    sizeof(struct sockaddr_in) :
		    addr->sa_family == AF_INET6 ?
		    sizeof(struct sockaddr_in6) : 0);
		return (-1);
	}
	/*
	 * use different socket for each peer,
	 * to identify error code
	 */
	sock = socket(addr->sa_family, SOCK_DGRAM, 0);
	if (sock == -1) {
		gflog_error(GFARM_MSG_1004077,
		    "failover_notify: socket(%s): %s",
		    hostname, strerror(errno));
		return (-1);
	}
	/*
	 * workaround linux UDP behavior
	 * that select(2)/poll(2)/epoll(2) returns
	 * that the socket is readable, but it may be not.
	 *
	 * from http://stackoverflow.com/questions/4381430/
	 * What are the WONTFIX bugs on GNU/Linux and how to
	 * work around them?
	 *
	 * The Linux UDP select bug: select (and related interfaces)
	 * flag a UDP socket file descriptor ready
	 * for reading as soon as a packet has been received,
	 * without confirming the checksum. On subsequent
	 * recv/read/etc., if the checksum was invalid, the
	 * call will block.
	 * Working around this requires always setting UDP sockets to
	 * non-blocking mode and dealing with
	 * the EWOULDBLOCK condition.
	 */
	if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1)
		gflog_warning(GFARM_MSG_1004078,
		    "failover_notify(%s): set nonblock: %s",
		    hostname, strerror(errno));

	if (connect(sock, &addr->sa_addr, addr->sa_addrlen) == -1) {
		save_errno = errno;
		error = gfarm_getnameinfo(
		    &addr->sa_addr, addr->sa_addrlen,
		    addr_string, sizeof(addr_string),
		    serv_string, sizeof(serv_string),
		    NI_NUMERICHOST | NI_NUMERICSERV);
		if (error != 0) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "failover_notify: "
			    "UDP connect(%s:%d): %s (%s)",
			    hostname, port, strerror(save_errno),
			    gai_strerror(error));
		} else {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "failover_notify: "
			    "UDP connect(%s/[%s]:%s): %s",
			    hostname, addr_string, serv_string,
			    strerror(save_errno));
		}
		close(sock);
		return (-1);
	}
	return (sock);
}

struct failover_notify_closure {
	struct host *fsnode;
	struct gfarm_host_address **addrs;
	int naddrs, addrs_index;

	struct watcher_event *result_event;
	int socket;
	int retry_count;
	char xid[GFS_UDP_RPC_XID_SIZE];
};

static struct failover_notify_closure *
failover_notify_closure_alloc(struct host *h,
	int naddrs, struct gfarm_host_address **addrs)
{
	struct failover_notify_closure *fnc;

	GFARM_MALLOC(fnc);
	if (fnc == NULL) {
		gflog_error(GFARM_MSG_1004070,
		    "failover_notify(%s): no memory", host_name(h));
		return (NULL);
	}

	fnc->fsnode = h;
	fnc->addrs = addrs;
	fnc->naddrs = naddrs;
	fnc->addrs_index = 0;

	fnc->result_event = NULL;
	fnc->socket = -1;
	fnc->retry_count = 0;
	/* failover_notify_start() will initialize fnc->xid */

	return (fnc);
}

static void
failover_notify_finish(struct failover_notify_closure *fnc)
{
	if (debug_mode)
		gflog_info(GFARM_MSG_1004072,
		    "failover notifiy: finish for %s",
		    host_name(fnc->fsnode));

	if (fnc->result_event != NULL)
		watcher_event_free(fnc->result_event);
	if (fnc->socket != -1)
		close(fnc->socket);
	gfarm_host_address_free(fnc->naddrs, fnc->addrs);
	free(fnc);
}

static void failover_notify_start(struct failover_notify_closure *);

static void
failover_notify_next_addr(struct failover_notify_closure *fnc)
{

	assert(fnc->result_event != NULL);
	watcher_event_free(fnc->result_event);
	fnc->result_event = NULL;

	assert(fnc->socket != -1);
	close(fnc->socket);
	fnc->socket = -1;

	fnc->addrs_index++;
	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "failover notifiy(%s): next address %d/%d",
		    host_name(fnc->fsnode), fnc->addrs_index, fnc->naddrs);
	failover_notify_start(fnc);
}

static void *failover_notify_result(void *);

static void
failover_notify_got_reply(struct failover_notify_closure *fnc)
{
	gfarm_error_t e;

	if (debug_mode)
		gflog_info(GFARM_MSG_1004073,
		    "failover notifiy: result for %s", host_name(fnc->fsnode));

	e = gfs_client_failover_notify_result(fnc->socket,
	    fnc->retry_count, fnc->xid, host_name(fnc->fsnode));
	if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE /*EWOULDBLOCK*/ ||
	    e == GFARM_ERR_PROTOCOL_NOT_SUPPORTED /* forged packet? */) {
		/* wait again */
		watcher_add_event_with_timeout(back_channel_watcher(),
		    fnc->result_event,
		    gfs_client_datagram_timeouts[fnc->retry_count],
		    back_channel_recv_thrpool(),
		    failover_notify_result, fnc);
	} else {
		failover_notify_finish(fnc);
	}
}

static void
failover_notify_timeout(struct failover_notify_closure *fnc)
{
	int error, gai_err;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	if (debug_mode)
		gflog_info(GFARM_MSG_1004074,
		    "failover notifiy: timeout for %s",
		     host_name(fnc->fsnode));

	if (++fnc->retry_count >= gfs_client_datagram_ntimeouts) {
		gai_err = gfarm_getnameinfo(
		    &fnc->addrs[fnc->addrs_index]->sa_addr,
		    fnc->addrs[fnc->addrs_index]->sa_addrlen,
		    hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
		    NI_NUMERICHOST | NI_NUMERICSERV);
		if (gai_err != 0) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "failover_notify(%s): "
			    "retry_count exceeds %d times - timedout (%s)",
			    host_name(fnc->fsnode), fnc->retry_count,
			    gai_strerror(gai_err));
		} else {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "failover_notify(%s/[%s]:%s): "
			    "retry_count exceeds %d times - timedout",
			    host_name(fnc->fsnode), hbuf, sbuf,
			    fnc->retry_count);
		}

		failover_notify_next_addr(fnc);
	} else if ((error = gfs_client_failover_notify_request(
	    fnc->socket, fnc->retry_count, fnc->xid,
	    host_name(fnc->fsnode))) != 0 &&
	    (error != EWOULDBLOCK ||
	     fnc->retry_count >= gfs_client_datagram_ntimeouts - 1)) {
		gai_err = gfarm_getnameinfo(
		    &fnc->addrs[fnc->addrs_index]->sa_addr,
		    fnc->addrs[fnc->addrs_index]->sa_addrlen,
		    hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
		    NI_NUMERICHOST | NI_NUMERICSERV);
		if (gai_err != 0) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "failover_notify(%s): "
			    "%d time(s) retried, but failed: %s (%s)",
			    host_name(fnc->fsnode), fnc->retry_count,
			    strerror(error), gai_strerror(gai_err));
		} else {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "failover_notify(%s/[%s]:%s): "
			    "%d time(s) retried, but failed: %s",
			    host_name(fnc->fsnode), hbuf, sbuf,
			    fnc->retry_count, strerror(error));
		}

		failover_notify_next_addr(fnc);
	} else {
		/* do retry in case of a retry or EWOULDBLOCK */
		watcher_add_event_with_timeout(back_channel_watcher(),
		    fnc->result_event,
		    gfs_client_datagram_timeouts[fnc->retry_count],
		    back_channel_recv_thrpool(),
		    failover_notify_result, fnc);
	}
}

static void *
failover_notify_result(void *arg)
{
	struct failover_notify_closure *fnc = arg;

	watcher_event_ack(fnc->result_event);

	if (watcher_event_is_readable(fnc->result_event))
		failover_notify_got_reply(fnc);
	else
		failover_notify_timeout(fnc);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

static void
failover_notify_send(struct failover_notify_closure *fnc)
{
	int error;

	/* do retry in EWOULDBLOCK case */
	if ((error = gfs_client_failover_notify_request(fnc->socket,
	    fnc->retry_count, fnc->xid, host_name(fnc->fsnode))) != 0 &&
	    error != EWOULDBLOCK) {
		free(fnc);
		return;
	}

	watcher_add_event_with_timeout(back_channel_watcher(),
	    fnc->result_event, gfs_client_datagram_timeouts[fnc->retry_count],
	    back_channel_recv_thrpool(), failover_notify_result, fnc);
}

static void
failover_notify_start(struct failover_notify_closure *fnc)
{
	gfarm_error_t e;

	for (;;) {
		const char *hostname = host_name(fnc->fsnode);

		if (fnc->addrs_index >= fnc->naddrs) {
			gflog_warning(GFARM_MSG_1004082,
			    "failover_notify(%s): UDP connect failed",
			    hostname);
			failover_notify_finish(fnc);
			return;
		}
		fnc->socket = udp_connect_to(hostname, host_port(fnc->fsnode),
		    fnc->addrs[fnc->addrs_index]);
		if (fnc->socket == -1) {
			fnc->addrs_index++;
			continue;
		}
		e = watcher_fd_readable_or_timeout_event_alloc(fnc->socket,
		    &fnc->result_event);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1004071,
			    "failover_notify(%s): "
			    "watcher_fd_readable_or_timeout_event_alloc: %s",
			    hostname, gfarm_error_string(e));
			/* fatal, do not try next address */
			failover_notify_finish(fnc);
			return;
		}

		fnc->retry_count = 0;
		/* use gfarm_auth_random() for security */
		gfarm_auth_random(fnc->xid, sizeof(fnc->xid));

		failover_notify_send(fnc);
		return;
	}
}

static int
record_fsnode(struct host *h, void *junk, void *rec)
{
	struct host **hp = rec;

	*hp = h;
	return (1); /* record all fsnode */
}

void
failover_notify(void)
{
	gfarm_error_t e;
	struct host **fsnodes;
	size_t i, nfsnodes;
	void *tmp;
	int naddrs;
	struct gfarm_host_address **addrs;
	struct failover_notify_closure *fnc;

	giant_lock();
	e = host_iterate(record_fsnode, NULL, sizeof(*fsnodes),
	    &nfsnodes, &tmp);
	giant_unlock();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004083,
		    "failover_notify: filesystem node allocation: %s",
		    gfarm_error_string(e));
		return;
	}
	fsnodes = tmp;

	for (i = 0; i < nfsnodes; i++) {
		if ((e = gfarm_host_address_get(
		    host_name(fsnodes[i]), host_port(fsnodes[i]),
		    &naddrs, &addrs)) != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1004079,
			    "failover_notify: resolving hostname `%s': %s",
			    host_name(fsnodes[i]), gfarm_error_string(e));
		} else if ((fnc = failover_notify_closure_alloc(
		    fsnodes[i], naddrs, addrs)) == NULL) {
			gfarm_host_address_free(naddrs, addrs);
		} else
			failover_notify_start(fnc);
	}
	free(fsnodes);
}
