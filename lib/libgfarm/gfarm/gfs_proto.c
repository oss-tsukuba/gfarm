#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>

#include <gfarm/gfarm.h>

#include "gfnetdb.h"

#include "gfs_proto.h"

char GFS_SERVICE_TAG[] = "gfarm-data";

gfarm_error_t
gfs_sockaddr_to_local_addr(struct sockaddr *addr, socklen_t addrlen, int port,
	struct sockaddr_un *local_addr, char **dirp)
{
	int rv;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV], dir_buf[PATH_MAX], *dir;

	/* port number in `addr' and sbuf are not actually used */
	rv = gfarm_getnameinfo(addr, addrlen, hbuf, sizeof hbuf,
	    sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV);
	if (rv != 0) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfs_local_socket_addr_init: %s",
		    gai_strerror(rv));
		return (GFARM_ERR_INTERNAL_ERROR);
	}
	memset(local_addr, 0, sizeof(*local_addr));
	local_addr->sun_family = AF_UNIX;
	snprintf(local_addr->sun_path, sizeof local_addr->sun_path,
	    GFSD_LOCAL_SOCKET_NAME, hbuf, port);

	if (dirp != NULL) {
		snprintf(dir_buf, sizeof dir_buf,
		    GFSD_LOCAL_SOCKET_DIR, hbuf, port);
		dir = strdup(dir_buf);
		if (dir == NULL)
			return (GFARM_ERR_NO_MEMORY);
		*dirp = dir;
	}

	return (GFARM_ERR_NO_ERROR);
}
