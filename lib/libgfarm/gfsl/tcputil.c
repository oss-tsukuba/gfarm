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
#include "tcputil.h"

#define MAX_BACKLOG	10

static int	isNonBlock(int fd);

static int
isNonBlock(fd)
     int fd;
{
    int stat = fcntl(fd, F_GETFL, 0);
    if (stat < 0) {
	perror("fcntl");
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
	perror("socket");
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
	    perror("connect");
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
	perror("socket");
	return -1;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short)port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(int)) != 0) {
	perror("setsockopt");
	close(sock);
	return -1;
    }
    
    if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
	perror("bind");
	close(sock);
	return -1;
    }

    if (listen(sock, MAX_BACKLOG) != 0) {
	perror("listen");
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
    int slen = sizeof(sin);

    if (getpeername(sock, (struct sockaddr *)&sin, &slen) != 0) {
	perror("getpeername");
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
    int slen = sizeof(sin);
    
    if (getsockname(sock, (struct sockaddr *)&sin, &slen) != 0) {
	perror("getsockname");
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
	    perror("select");
	    return sel;
	}
    }
    return sel;
}	


int
gfarmReadBytes(fd, buf, len)
     int fd;
     char *buf;
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
	    perror("read");
	    return sum;
	} else if (cur == 0) {
	    break;
	}
	sum += cur;
    } while (sum < len);
    return sum;
}


int
gfarmReadShorts(fd, buf, len)
     int fd;
     short *buf;
     int len;
{
    int i;
    int n;
    short s;

    for (i = 0; i < len; i++) {
	n = gfarmReadBytes(fd, (char *)&s, sizeof(short));
	if (sizeof(short) != n) {
	    return i;
	}
	buf[i] = ntohs(s);
    }

    return i;
}


int
gfarmReadLongs(fd, buf, len)
     int fd;
     long *buf;
     int len;
{
    int i;
    int n;
    long l;

    for (i = 0; i < len; i++) {
	n = gfarmReadBytes(fd, (char *)&l, sizeof(long));
	if (sizeof(long) != n) {
	    return i;
	}
	buf[i] = ntohl(l);
    }

    return i;
}


int
gfarmWriteBytes(fd, buf, len)
     int fd;
     char *buf;
     int len;
{
    int sum = 0;
    int cur = 0;

    do {
	cur = write(fd, buf + sum, len - sum);
	if (cur < 0) {
	    perror("write");
	    return sum;
	}
	sum += cur;
    } while (sum < len);
    return sum;
}


int
gfarmWriteShorts(fd, buf, len)
     int fd;
     short *buf;
     int len;
{
    int i;
    int n;
    short s;
    
    for (i = 0; i < len; i++) {
	s = htons(buf[i]);
	n = gfarmWriteBytes(fd, (char *)&s, sizeof(short));
	if (sizeof(short) != n) {
	    return i;
	}
    }
    return i;
}


int
gfarmWriteLongs(fd, buf, len)
     int fd;
     long *buf;
     int len;
{
    int i;
    int n;
    long l;
    
    for (i = 0; i < len; i++) {
	l = htonl(buf[i]);
	n = gfarmWriteBytes(fd, (char *)&l, sizeof(long));
	if (sizeof(long) != n) {
	    return i;
	}
    }
    return i;
}

