#include <stddef.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gflog.h>

#include "gfnetdb.h"

#include "sockutil.h"

gfarm_error_t
gfarm_connect_wait(int s, int timeout_seconds)
{
	fd_set wset;
	struct timeval timeout;
	int rv, error, save_errno;
	socklen_t error_size;

	for (;;) {
		FD_ZERO(&wset);
		FD_SET(s, &wset);
		timeout.tv_sec = timeout_seconds;
		timeout.tv_usec = 0;

		/* XXX shouldn't use select(2), since wset may overflow. */
		rv = select(s + 1, NULL, &wset, NULL, &timeout);
		if (rv == 0)
			return (gfarm_errno_to_error(ETIMEDOUT));
		if (rv < 0) {
			if (errno == EINTR)
				continue;
			save_errno = errno;
			gflog_debug(GFARM_MSG_1001458, "select() failed: %s",
				strerror(save_errno));
			return (gfarm_errno_to_error(save_errno));
		}
		break;
	}

	error_size = sizeof(error);
	rv = getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &error_size);
	if (rv == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1001459, "getsocket() failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	if (error != 0) {
		gflog_debug(GFARM_MSG_1001460,
			"error occurred at socket: %s",
			gfarm_error_string(gfarm_errno_to_error(error)));
		return (gfarm_errno_to_error(error));
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_bind_source_ip(int sock, const char *source_ip)
{
	struct addrinfo shints, *sres;
	int rv, save_errno;

	memset(&shints, 0, sizeof(shints));
	shints.ai_family = AF_INET;
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
