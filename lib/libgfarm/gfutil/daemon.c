#include <gfarm/gfarm_config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "gfutil.h"

#include <gfarm/gflog.h>

#ifndef HAVE_DAEMON
int
gfarm_daemon(int not_chdir, int not_close)
{
	int save_errno;
	switch (fork()) {
	case -1:
		save_errno = errno;
		gflog_debug(GFARM_MSG_1000766, "fork() failed: %s",
			strerror(save_errno));
		errno = save_errno;
		return (-1);
	case 0:
		break;
	default:
		exit(0);
	}
	setsid();
	if (!not_chdir)
		chdir("/");
	if (!not_close) {
		int fd = open("/dev/null", O_RDWR, 0);

		if (fd != -1) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
	}
	return (0);
}
#endif /* !HAVE_DAEMON */
