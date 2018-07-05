#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gflog.h>

#include "gfutil.h"
#include "gfnetdb.h"

#include "sockopt.h"
#include "sockutil.h"

static gfarm_error_t
gfarm_bind_source_ip(int proto_family, int sock, const char *source_ip)
{
	struct addrinfo shints, *sres;
	int rv, save_errno;

	memset(&shints, 0, sizeof(shints));
	shints.ai_family = proto_family;
	shints.ai_socktype = SOCK_STREAM;
	shints.ai_flags = AI_PASSIVE;
	if (gfarm_getaddrinfo(source_ip, NULL, &shints, &sres) != 0) {
		gflog_debug(GFARM_MSG_1001461,
			"gfarm_getaddrinfo(%s) failed: %s",
			source_ip,
			gfarm_error_string(GFARM_ERR_UNKNOWN_HOST));
		return (GFARM_ERR_UNKNOWN_HOST);
	}

	rv = bind(sock, sres->ai_addr, sres->ai_addrlen);
	save_errno = errno;
	gfarm_freeaddrinfo(sres);
	if (rv == -1) {
		gflog_debug(GFARM_MSG_1001462,
			"bind() failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_nonblocking_connect(int proto_family, int sock_type, int protocol,
	struct sockaddr *addr, socklen_t addrlen,
	const char *canonical_hostname,
	const char *source_ip, void (*descriptor_gc)(void),
	int *connection_in_progress_p, int *sockp)
{
	gfarm_error_t e;
	int sock, rv, save_errno, connection_in_progress;

	sock = socket(proto_family, sock_type, protocol);
	if (sock == -1 && (errno == ENFILE || errno == EMFILE)) {
		(*descriptor_gc)();
		sock = socket(proto_family, sock_type, protocol);
	}
	if (sock == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "creation of socket(%d, %d, %d) failed: %s",
		    proto_family, sock_type, protocol, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */
	fcntl(sock, F_SETFL, O_NONBLOCK); /* this should never fail */

	/* XXX - how to report setsockopt(2) failure ? */
	gfarm_sockopt_apply_by_name_addr(sock, canonical_hostname, addr);

	if (source_ip != NULL) {
		e = gfarm_bind_source_ip(proto_family, sock, source_ip);
		if (e != GFARM_ERR_NO_ERROR) {
			close(sock);
			gflog_debug(GFARM_MSG_1001095,
			    "bind(%s) failed: %s",
			    source_ip, gfarm_error_string(e));
			return (e);
		}
	}

	rv = connect(sock, addr, addrlen);
	if (rv < 0) {
		if (errno != EINPROGRESS) {
			save_errno = errno;
			close(sock);
			gflog_debug(GFARM_MSG_1001096, "connect failed: %s",
			    strerror(save_errno));
			return (gfarm_errno_to_error(save_errno));
		}
		/* returns GFARM_ERR_NO_ERROR, if EINPROGRESS */
		connection_in_progress = 1;
	} else {
		connection_in_progress = 0;
	}
	fcntl(sock, F_SETFL, 0); /* clear O_NONBLOCK, this should never fail */

	if (connection_in_progress_p != NULL)
		*connection_in_progress_p = connection_in_progress;
	*sockp = sock;
	return (GFARM_ERR_NO_ERROR);
}
