#include <stddef.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <string.h>
#include <poll.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gflog.h>

#include "gfutil.h"
#include "fdutil.h"
#include "assert.h"

int
gfarm_socket_get_errno(int fd)
{
	int error = -1;
	socklen_t error_size = sizeof(error);
	int rv = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size);

	if (rv == -1) /* Solaris, see UNP by rstevens */
		return (errno);
	return (error);
}

/* if timeout_seconds == -1, wait indefinitely */
int
gfarm_fd_wait(int sock, int to_read, int to_write, int timeout_seconds,
	const char *diag)
{
	int rv, save_errno, timeout_milliseconds;
	struct pollfd pfd;
	int poll_mode;
	struct timeval timeout, now, duration;

	if (timeout_seconds == -1) {
		timeout_milliseconds = -1;
	} else {
		gettimeofday(&timeout, NULL);
		timeout.tv_sec += timeout_seconds;
		timeout_milliseconds =
		    timeout_seconds * GFARM_SECOND_BY_MILLISEC;
	}

	poll_mode = (to_read ? POLLIN : 0) | (to_write ? POLLOUT : 0);

	pfd.fd = sock;
	pfd.events = poll_mode;
	for (;;) {
		rv = poll(&pfd, 1, timeout_milliseconds);
		if (rv == 0)
			return (ETIMEDOUT);
		if (rv > 0) {
			if ((pfd.revents & (POLLERR|POLLHUP|POLLNVAL)) == 0 &&
			    (pfd.revents & poll_mode) != 0)
				return (0);

			save_errno = gfarm_socket_get_errno(pfd.fd);
			if (save_errno != 0)
				return (save_errno);

			/* shouldn't happen */
			gflog_info(GFARM_MSG_UNFIXED,
			    "%s: unexpected poll event 0x%x",
			    diag, pfd.revents);
			return (ECONNREFUSED); /* XXX */
		}
		if (errno != EINTR) {
			/* shouldn't happen */
			save_errno = errno;
			gflog_info_errno(GFARM_MSG_UNFIXED,
			    "%s: poll()", diag);
			return (save_errno);
		}
		if (timeout_seconds != -1) {
			gettimeofday(&now, NULL);
			if (gfarm_timeval_cmp(&timeout, &now) < 0)
				return (ETIMEDOUT);
			duration = timeout;
			gfarm_timeval_sub(&duration, &now);
			timeout_milliseconds =
			    duration.tv_sec * GFARM_SECOND_BY_MILLISEC +
			    duration.tv_usec / GFARM_MILLISEC_BY_MICROSEC;
		}
	}
	assert(0);
}
