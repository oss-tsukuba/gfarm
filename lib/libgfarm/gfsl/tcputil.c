#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <errno.h>

#ifdef USE_GLOBUS_LIBC_HOOK
#include "globus_libc.h"
#endif /* USE_GLOBUS_LIBC_HOOK */

#include <gfarm/gfarm_config.h>

#include <gfarm/error.h>

#include "gfutil.h"

#include "tcputil.h"

#define MAX_BACKLOG	10

#define	GFARM_OCTETS_PER_32BIT	4	/* 32/8 */
#define	GFARM_OCTETS_PER_16BIT	2	/* 16/8 */

static int	isNonBlock(int fd);

static int
isNonBlock(fd)
     int fd;
{
    int stat = fcntl(fd, F_GETFL, 0);
    if (stat < 0) {
	gflog_error("fcntl: %s", strerror(errno));
	return 0;
    } else {
	if (stat & O_NONBLOCK) {
	    return 1;
	} else {
	    return 0;
	}
    }
}


int
gfarmTCPConnectPortByHost(host, port)
     char *host;
     int port;
{
    struct hostent *h;
    struct sockaddr_in sin;
    int sock;

    /* XXX - thread unsafe */
    h = gethostbyname(host);
    if (h == NULL) {
	gflog_warning("gethostbyname: %s: %s", host, hstrerror(h_errno));
	return (-1);
    }
    if (h->h_addrtype != AF_INET) {
	gflog_warning("gethostbyname: %s: unsupported address family", host);
	return (-1);
    }
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = h->h_addrtype;
    memcpy(&sin.sin_addr, h->h_addr, sizeof(sin.sin_addr));
    sin.sin_port = htons(port);

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	gflog_error("socket: %s", strerror(errno));
	return (-1);
    }
    fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

    /* XXX - set socket options */

    ReConnect:
    errno = 0;
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
	int saved_errno = errno;

	if (saved_errno == EINTR) {
	    goto ReConnect;
	} else if (saved_errno == EINPROGRESS) {
	    if (isNonBlock(sock) == 0) {
		goto Error;
	    } else {
		/* XXX - should wait explicitly instead of sleep blindly */
		sleep(1);
		return (sock);
	    }
	} else {
	    Error:
	    close(sock);
	    gflog_error("connect: %s", strerror(saved_errno));
	    return (-1);
	}
    }
    return (sock);
}

int
gfarmTCPBindPort(port)
     int port;
{
    struct sockaddr_in sin;
    int sock;
    int one = 1;

    memset((void *)&sin, 0, sizeof(struct sockaddr_in));

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	gflog_error("socket: %s", strerror(errno));
	return -1;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short)port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(int)) != 0) {
	gflog_error("setsockopt: %s", strerror(errno));
	close(sock);
	return -1;
    }
    
    if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
	gflog_error("bind: %s", strerror(errno));
	close(sock);
	return -1;
    }

    if (listen(sock, MAX_BACKLOG) != 0) {
	gflog_error("listen: %s", strerror(errno));
	close(sock);
	return -1;
    }

    return sock;
}


int
gfarmGetPeernameOfSocket(sock, portPtr, hostPtr)
     int sock;
     int *portPtr;
     char **hostPtr;
{
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    struct hostent *hptr;

    if (getpeername(sock, (struct sockaddr *)&sin, &slen) != 0) {
	gflog_error("getpeername: %s", strerror(errno));
	return (-1);
    }
    if (portPtr != NULL)
	*portPtr = (int)ntohs(sin.sin_port);
    if (hostPtr != NULL &&
	(hptr = gethostbyaddr(&sin.sin_addr, sizeof(sin.sin_addr), AF_INET))
	!= NULL)
	*hostPtr = strdup(hptr->h_name);
    return (0);
}


int
gfarmGetNameOfSocket(sock, portPtr)
     int sock;
     int *portPtr;
{
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    
    if (getsockname(sock, (struct sockaddr *)&sin, &slen) != 0) {
	gflog_error("getsockname: %s", strerror(errno));
	return (-1);
    }
    if (portPtr != NULL)
	*portPtr = (int)ntohs(sin.sin_port);
    return (0);
}


int
gfarmWaitReadable(fd)
     int fd;
{
    fd_set rFd;
    int sel;

    FD_ZERO(&rFd);
    FD_SET(fd, &rFd);

    SelectAgain:
    errno = 0;
    sel = select(fd + 1, &rFd, NULL, NULL, NULL);
    if (sel < 0) {
	if (errno == EINTR) {
	    goto SelectAgain;
	} else {
	    gflog_error("select: %s", strerror(errno));
	    return sel;
	}
    }
    return sel;
}	


int
gfarmReadInt8(fd, buf, len)
     int fd;
     gfarm_int8_t *buf;
     int len;
{
    int sum = 0;
    int cur = 0;
    int sel;

    do {
	sel = gfarmWaitReadable(fd);
	if (sel <= 0) {
	    return sum;
	}
	cur = read(fd, buf + sum, len - sum);
	if (cur < 0) {
	    gflog_error("read: %s", strerror(errno));
	    return sum;
	} else if (cur == 0) {
	    break;
	}
	sum += cur;
    } while (sum < len);
    return sum;
}


int
gfarmReadInt16(fd, buf, len)
     int fd;
     gfarm_int16_t *buf;
     int len;
{
    int i;
    int n;
    gfarm_int16_t s;

    for (i = 0; i < len; i++) {
	n = gfarmReadInt8(fd, (gfarm_int8_t *)&s, GFARM_OCTETS_PER_16BIT);
	if (n != GFARM_OCTETS_PER_16BIT) {
	    return i;
	}
	buf[i] = ntohs(s);
    }

    return i;
}


int
gfarmReadInt32(fd, buf, len)
     int fd;
     gfarm_int32_t *buf;
     int len;
{
    int i;
    int n;
    gfarm_int32_t l;

    for (i = 0; i < len; i++) {
	n = gfarmReadInt8(fd, (gfarm_int8_t *)&l, GFARM_OCTETS_PER_32BIT);
	if (n != GFARM_OCTETS_PER_32BIT) {
	    return i;
	}
	buf[i] = ntohl(l);
    }

    return i;
}


int
gfarmWriteInt8(fd, buf, len)
     int fd;
     gfarm_int8_t *buf;
     int len;
{
    int sum = 0;
    int cur = 0;

    do {
	cur = gfarm_send_no_sigpipe(fd, buf + sum, len - sum);
	if (cur < 0) {
	    gflog_error("write: %s", strerror(errno));
	    return sum;
	}
	sum += cur;
    } while (sum < len);
    return sum;
}


int
gfarmWriteInt16(fd, buf, len)
     int fd;
     gfarm_int16_t *buf;
     int len;
{
    int i;
    int n;
    gfarm_int16_t s;
    
    for (i = 0; i < len; i++) {
	s = htons(buf[i]);
	n = gfarmWriteInt8(fd, (gfarm_int8_t *)&s, GFARM_OCTETS_PER_16BIT);
	if (n != GFARM_OCTETS_PER_16BIT) {
	    return i;
	}
    }
    return i;
}


int
gfarmWriteInt32(fd, buf, len)
     int fd;
     gfarm_int32_t *buf;
     int len;
{
    int i;
    int n;
    gfarm_int32_t l;
    
    for (i = 0; i < len; i++) {
	l = htonl(buf[i]);
	n = gfarmWriteInt8(fd, (gfarm_int8_t *)&l, GFARM_OCTETS_PER_32BIT);
	if (n != GFARM_OCTETS_PER_32BIT) {
	    return i;
	}
    }
    return i;
}

