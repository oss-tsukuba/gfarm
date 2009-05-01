#include <stddef.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>

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
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}
