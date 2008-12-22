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

#include "gfnetdb.h"
#include "gfutil.h"

#include "tcputil.h"

#define MAX_BACKLOG	10

#define	GFARM_OCTETS_PER_32BIT	4	/* 32/8 */
#define	GFARM_OCTETS_PER_16BIT	2	/* 16/8 */

int
gfarmTCPConnectPortByHost(host, port)
     char *host;
     int port;
{
    struct addrinfo hints, *res, *res0;
    int sock, error, rv;
    char sbuf[NI_MAXSERV];

    snprintf(sbuf, sizeof(sbuf), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    error = gfarm_getaddrinfo(host, sbuf, &hints, &res0);
    if (error != 0) {
	gflog_warning("getaddrinfo: %s: %s", host, gai_strerror(error));
	return (-1);
    }
    sock = -1;
    for (res = res0; res != NULL; res = res->ai_next) {
	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0) {
	    gflog_error("socket(%d, %d, %d): %s\n", (int)res->ai_family,
		(int)res->ai_socktype, (int)res->ai_protocol, strerror(errno));
	    continue;
	}

	while ((rv = connect(sock, res->ai_addr, res->ai_addrlen)) < 0 &&
	       errno == EINTR)
		;
	if (rv < 0) {
	    gflog_error("connect: %s\n", strerror(errno));
	    close(sock);
	    sock = -1;
	    continue;
	}

	/* connected */

	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	/* XXX - set socket options */

	break;
    }
    gfarm_freeaddrinfo(res0);
    return (sock);
}

int
gfarmTCPBindPort(port)
     int port;
{
    struct addrinfo hints, *res;
    int sock, e;
    int one = 1;
    char sbuf[NI_MAXSERV];

    snprintf(sbuf, sizeof(sbuf), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    e = gfarm_getaddrinfo(NULL, sbuf, &hints, &res);
    if (e) {
	glog_error("getaddrinfo(port = %u): %s\n", port, gai_strerror(e));
	return -1;
    }
    if (res == NULL)
	return -1;

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
	gflog_error("socket: %s", strerror(errno));
    } else if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(int)) != 0) {
	gflog_error("setsockopt: %s", strerror(errno));
	close(sock);
	sock = -1;
    } else if (bind(sock, res->ai_addr, res->ai_addrlen) < 0) {
	gflog_error("bind: %s", strerror(errno));
	close(sock);
	sock = -1;
    } else if (listen(sock, MAX_BACKLOG) != 0) {
	gflog_error("listen: %s", strerror(errno));
	close(sock);
	sock = -1;
    }
    gfarm_freeaddrinfo(res);

    return sock;
}


int
gfarmGetPeernameOfSocket(sock, portPtr, hostPtr)
     int sock;
     int *portPtr;
     char **hostPtr;
{
    struct sockaddr sin;
    socklen_t slen = sizeof(sin);
    char hbuf[NI_MAXHOST];

    if (getpeername(sock, (struct sockaddr *)&sin, &slen) != 0) {
	gflog_error("getpeername: %s", strerror(errno));
	return (-1);
    }
    if (hostPtr != NULL) {
	if (gfarm_getnameinfo((struct sockaddr *)&sin, sin.sin_len,
			      hbuf, sizeof(hbuf), NULL, 0, 0) != 0)
	    rerturn (-1);
	*hostPtr = strdup(hbuf);
    }
    if (portPtr != NULL)
	*portPtr = (int)ntohs(sin.sin_port);
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

