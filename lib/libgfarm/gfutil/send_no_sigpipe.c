#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gfarm/gflog.h>

#ifndef __KERNEL__	/* gfarm_sigpipe_ignore  ???*/
static int sigpipe_is_ignored = 0;
static struct sigaction old_sigpipe;

void
gfarm_sigpipe_ignore(void)
{
	struct sigaction new_act;

	memset(&new_act, 0, sizeof(new_act));
	new_act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &new_act, &old_sigpipe) == -1) {
		/* this shouldn't happen */
		gflog_error_errno(GFARM_MSG_1005241,
		    "signal(SIGPIPE, SIG_IGN)");
	} else {
		sigpipe_is_ignored = 1;
	}
}

void
gfarm_sigpipe_restore(void)
{
	if (sigaction(SIGPIPE, &old_sigpipe, NULL) == -1) {
		/* this shouldn't happen */
		gflog_error_errno(GFARM_MSG_1005242,
		    "restoring old SIGPIPE handler");
	}
}

#endif /* __KERNEL__ */

ssize_t
gfarm_send_no_sigpipe(int fd, const void *data, size_t length)
{
#ifdef MSG_NOSIGNAL
	return (send(fd, data, length, MSG_NOSIGNAL));
#else /* !defined(MSG_NOSIGNAL) */
	int save_errno;

	if (sigpipe_is_ignored) {
		return (write(fd, data, length));
	} else {
		/*
		 * This code assumes that SIGPIPE is posted synchronously
		 * in write(2) operation, instead of asynchronously.
		 */
		ssize_t rv;
		int old_is_set;
		struct sigaction sigpipe_ignore, sigpipe_old;

		memset(&sigpipe_ignore, 0, sizeof(sigpipe_ignore));
		sigpipe_ignore.sa_handler = SIG_IGN;
		if (sigaction(SIGPIPE, &sigpipe_ignore, &sigpipe_old) == -1)
			old_is_set = 0;
		else
			old_is_set = 1;
		rv = write(fd, data, length);
		if (old_is_set) {
			save_errno = errno;
			sigaction(SIGPIPE, &sigpipe_old, NULL);
			errno = save_errno;
		}
		return (rv);
	}
#endif /* !defined(MSG_NOSIGNAL) */
}

