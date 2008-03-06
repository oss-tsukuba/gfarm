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
gfarmTCPConnectPort(addr, port)
     unsigned long addr;
     int port;
{
    struct sockaddr_in sin;
    int sock;

    memset((void *)&sin, 0, sizeof(struct sockaddr_in));

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	gflog_error("socket: %s", strerror(errno));
	return -1;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(addr);

    ReConnect:
    errno = 0;
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
	if (errno == EINTR) {
	    goto ReConnect;
	} else if (errno == EINPROGRESS) {
	    if (isNonBlock(sock) == 0) {
		goto Error;
	    } else {
		sleep(1);
		return sock;
	    }
	} else {
	    Error:
	    gflog_error("connect: %s", strerror(errno));
	    return -1;
	}
    }
    return sock;
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


unsigned long int
gfarmIPGetAddressOfHost(host)
     char *host;
{
    struct hostent *h = gethostbyname(host);
    if (h != NULL) {
	return ntohl(*(unsigned long *)(h->h_addr));
    } else {
	/*
	 * maybe host is XXX.XXX.XXX.XXX format.
	 */
	return ntohl(inet_addr(host));
    }
}


char *
gfarmIPGetHostOfAddress(addr)
     unsigned long int addr;
{
    /*
     * addr must be Host Byte Order.
     */
    char *ret = NULL;
    unsigned long caddr = htonl(addr);
    struct hostent *h = NULL;

#ifndef USE_GLOBUS_LIBC_HOOK
    h = gethostbyaddr((void *)&caddr, sizeof(unsigned long int), AF_INET);
#else
    {
	struct hostent gH;
	char gBuf[4096];
	int gh_errno = 0;
	h = globus_libc_gethostbyaddr_r((void *)&caddr, sizeof(unsigned long int),
					AF_INET, &gH, gBuf, 4096, &gh_errno);
    }
#endif /* USE_GLOBUS_LIBC_HOOK */

    if (h != NULL) {
	ret = strdup(h->h_name);
    } else {
	char hostBuf[4096];
#ifdef HAVE_SNPRINTF
	snprintf(hostBuf, sizeof hostBuf, "%d.%d.%d.%d",
		(int)((addr & 0xff000000) >> 24),
		(int)((addr & 0x00ff0000) >> 16),
		(int)((addr & 0x0000ff00) >> 8),
		(int)(addr & 0x000000ff));
#else
	sprintf(hostBuf, "%d.%d.%d.%d",
		(int)((addr & 0xff000000) >> 24),
		(int)((addr & 0x00ff0000) >> 16),
		(int)((addr & 0x0000ff00) >> 8),
		(int)(addr & 0x000000ff));
#endif
	ret = strdup(hostBuf);
    }
    return ret;
}


unsigned long int
gfarmIPGetPeernameOfSocket(sock, portPtr)
     int sock;
     int *portPtr;
{
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);

    if (getpeername(sock, (struct sockaddr *)&sin, &slen) != 0) {
	gflog_error("getpeername: %s", strerror(errno));
	if (portPtr != NULL) {
	    *portPtr = 0;
	}
	return 0;
    }
    if (portPtr != NULL) {
	*portPtr = (int)ntohs(sin.sin_port);
    }
    return ntohl(sin.sin_addr.s_addr);
}


unsigned long int
gfarmIPGetNameOfSocket(sock, portPtr)
     int sock;
     int *portPtr;
{
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    
    if (getsockname(sock, (struct sockaddr *)&sin, &slen) != 0) {
	gflog_error("getsockname: %s", strerror(errno));
	if (portPtr != NULL) {
	    *portPtr = 0;
	}
	return 0;
    }
    if (portPtr != NULL) {
	*portPtr = (int)ntohs(sin.sin_port);
    }
    return ntohl(sin.sin_addr.s_addr);
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

