#include <stddef.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>

#include "gfnetdb.h"

#include "sockutil.h"

gfarm_error_t
gfarm_connect_wait(int s, int timeout_seconds)
{
	fd_set wset;
	struct timeval timeout;
	int rv, error;
	socklen_t error_size;

	FD_ZERO(&wset);
	FD_SET(s, &wset);
	timeout.tv_sec = timeout_seconds;
	timeout.tv_usec = 0;

	/* XXX shouldn't use select(2), since wset may overflow. */
	rv = select(s + 1, NULL, &wset, NULL, &timeout);
	if (rv == 0)
		return (gfarm_errno_to_error(ETIMEDOUT));
	if (rv < 0)
		return (gfarm_errno_to_error(errno));

	error_size = sizeof(error);
	rv = getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &error_size);
	if (rv == -1)
		return (gfarm_errno_to_error(errno));
	if (error != 0)
		return (gfarm_errno_to_error(error));
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
	if (gfarm_getaddrinfo(source_ip, NULL, &shints, &sres) != 0)
		return (GFARM_ERR_UNKNOWN_HOST);

	rv = bind(sock, sres->ai_addr, sres->ai_addrlen);
	save_errno = errno;
	gfarm_freeaddrinfo(sres);
	if (rv == -1)
		return (gfarm_errno_to_error(save_errno));
	return (GFARM_ERR_NO_ERROR);
}
