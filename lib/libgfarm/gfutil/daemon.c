#include <gfarm/gfarm_config.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include "gfutil.h"

#ifndef HAVE_DAEMON
int
gfarm_daemon(int not_chdir, int not_close)
{
	switch (fork()) {
	case -1:
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
