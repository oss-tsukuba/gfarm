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
#include <gfarm/gflog.h>
#include <gfarm/error.h>

#ifdef HAVE_POLL
#include <poll.h>
#ifndef INFTIM
#define INFTIM -1
#endif
#else
#include <sys/time.h>
#endif

#include "gfnetdb.h"
#include "gfutil.h"

#include "tcputil.h"
#include "gfsl_config.h"

#define MAX_BACKLOG	10

#define	GFARM_OCTETS_PER_32BIT	4	/* 32/8 */
#define	GFARM_OCTETS_PER_16BIT	2	/* 16/8 */

int
gfarmTCPConnectPortByHost(char *host, int port)
{
    struct addrinfo hints, *res, *res0;
    int sock, error, save_errno = 0, rv;
    char sbuf[NI_MAXSERV];

    snprintf(sbuf, sizeof(sbuf), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    error = gfarm_getaddrinfo(host, sbuf, &hints, &res0);
    if (error != 0) {
	gflog_warning(GFARM_MSG_1000629,
	    "getaddrinfo: %s: %s", host, gai_strerror(error));
	errno = EINVAL; /* errno doesn't have GFARM_ERR_UNKNOWN_HOST */
	return (-1);
    }
    sock = -1;
    for (res = res0; res != NULL; res = res->ai_next) {
	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0) {
	    save_errno = errno;
	    gflog_error(GFARM_MSG_1000630,
		"socket(%d, %d, %d): %s", (int)res->ai_family,
		(int)res->ai_socktype, (int)res->ai_protocol, strerror(errno));
	    continue;
	}

	while ((rv = connect(sock, res->ai_addr, res->ai_addrlen)) < 0 &&
	       errno == EINTR)
		;
	if (rv < 0) {
	    save_errno = errno;
	    gflog_error(GFARM_MSG_1000631, "connect: %s", strerror(errno));
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
    errno = save_errno;
    return (sock);
}

int
gfarmTCPBindPort(int port, int *numSocksPtr, int **socksPtr)
{
    struct addrinfo hints, *res0, *res;
    int sock, n, numSocks, *socks, e, save_errno = 0;
    int one = 1;
    char sbuf[NI_MAXSERV];

    snprintf(sbuf, sizeof(sbuf), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    e = gfarm_getaddrinfo(NULL, sbuf, &hints, &res0);
    if (e) {
	gflog_error(GFARM_MSG_1000632,
	    "getaddrinfo(port = %u): %s", port, gai_strerror(e));
	errno = EINVAL; /* errno doesn't have GFARM_ERR_UNKNOWN_HOST */
	return -1;
    }
    if (res0 == NULL) {
	gflog_debug(GFARM_MSG_1000803,
	    "gfarm_getaddinfo() failed for port (%d)", port);
	errno = EINVAL; /* errno doesn't have GFARM_ERR_UNKNOWN_HOST */
	return -1;
    }
    for (n = 0, res = res0; res != NULL; res = res->ai_next, n++)
	;
    GFARM_MALLOC_ARRAY(socks, n);
    if (socks == NULL) {
	gflog_error(GFARM_MSG_UNFIXED, "no memory for %d sockets\n", n);
	gfarm_freeaddrinfo(res0);
	return -1;
    }

    numSocks = 0;
    for (res = res0; res != NULL; res = res->ai_next) {
	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0) {
	    save_errno = errno;
	    gflog_error(GFARM_MSG_1000633, "socket: %s", strerror(errno));
	} else if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
			      &one, sizeof(one)) != 0) {
	    save_errno = errno;
	    gflog_notice(GFARM_MSG_1000634, "setsockopt: %s", strerror(errno));
	    close(sock);
	    sock = -1;
#ifdef IPV6_V6ONLY
	} else if (res->ai_family == AF_INET6 &&
		   setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
			      &one, sizeof(one)) == -1) {
	    save_errno = errno;
	    gflog_warning_errno(GFARM_MSG_UNFIXED,
				"setsockopt(IPPROTO_IPV6, IPV6_V6ONLY)");
	    close(sock);
	    sock = -1;
#endif
	} else if (bind(sock, res->ai_addr, res->ai_addrlen) < 0) {
	    save_errno = errno;
	    gflog_error(GFARM_MSG_1000635, "bind: %s", strerror(errno));
	    close(sock);
	    sock = -1;
	} else if (listen(sock, MAX_BACKLOG) != 0) {
	    save_errno = errno;
	    gflog_error(GFARM_MSG_1000636, "listen: %s", strerror(errno));
	    close(sock);
	    sock = -1;
	/*
	 * To deal with race condition which may be caused by RST,
	 * listening socket must be O_NONBLOCK, if the socket will be
	 * used as a file descriptor for select(2)/poll(2) .
	 * See section 16.6 of "UNIX NETWORK PROGRAMMING, Volume1,
	 * Third Edition" by W. Richard Stevens, for detail.
	 */
	} else if (fcntl(sock, F_SETFL,
			 fcntl(sock, F_GETFL, NULL) | O_NONBLOCK) == -1) {
	    save_errno = errno;
	    gflog_error_errno(GFARM_MSG_UNFIXED,
		"accepting TCP socket O_NONBLOCK");
	    close(sock);
	    sock = -1;
	}
	if (sock >= 0)
	    socks[numSocks++] = sock;
    }
    gfarm_freeaddrinfo(res0);
    errno = save_errno;
    if (numSocks <= 0) {
	free(socks);
	return (-1);
    }

    *numSocksPtr = numSocks;
    *socksPtr = socks;
    return 0;
}

int
gfarmAcceptFds(int numSocks, int *socks,
	       struct sockaddr *sa, socklen_t *sa_lenp)
{
    struct pollfd *pfds, *pfd;
    int i, rv;

    GFARM_MALLOC_ARRAY(pfds, numSocks);
    if (pfds == NULL) {
	gflog_error(GFARM_MSG_UNFIXED, "no memory for %d sockets\n", numSocks);
	errno = ENOMEM;
	return -1;
    }
    for (;;) {
	for (i = 0; i < numSocks; i++) {
	    pfd = &pfds[i];
	    pfd->fd = socks[i];
	    pfd->events = POLLIN;
	    pfd->revents = 0;
	}
	rv = poll(pfds, numSocks, INFTIM);
	if (rv == -1) {
	    if (errno == EINTR)
		continue;
	    gflog_warning_errno(GFARM_MSG_UNFIXED, "poll accepting sockts");
	    continue;
	}
	for (i = 0; i < numSocks; i++) {
	    pfd = &pfds[i];
	    if (pfd->revents) {
		rv = accept(pfd->fd, sa, sa_lenp);
		if (rv != -1) {
		    free(pfds);
		    return rv;
		}
	    }
	}
    }
}

void
gfarmCloseFds(int numSocks, int *socks)
{
    int i;

    for (i = 0; i < numSocks; i++)
	close(socks[i]);
    free(socks);
}

int
gfarmGetPeernameOfSocket(int sock, int *portPtr, char **hostPtr)
{
    struct sockaddr_storage sa;
    socklen_t sa_len = sizeof(sa);
    int save_errno;

    if (getpeername(sock, (struct sockaddr *)&sa, &sa_len) != 0) {
	save_errno = errno;
	gflog_notice(GFARM_MSG_1000637, "getpeername: %s", strerror(errno));
	errno = save_errno;
	return (-1);
    }
    if (hostPtr != NULL || portPtr != NULL) {
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	if (gfarm_getnameinfo((struct sockaddr *)&sa, sa_len,
			      hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
			      NI_NUMERICSERV) != 0) {
	    gflog_debug(GFARM_MSG_1000804, "gfarm_getnameinfo() failed");
	    /*
	     * errno doesn't have
	     * GFARM_ERR_CANNOT_RESOLVE_AN_IP_ADDRESS_INTO_A_HOSTNAME
	     */
	    errno = EINVAL;
	    return (-1);
	}
	if (hostPtr != NULL) {
	    *hostPtr = strdup(hbuf);
	    if (*hostPtr == NULL) {
		errno = ENOMEM;
		return (-1);
	    }
	}
	if (portPtr != NULL)
	    *portPtr = atoi(sbuf);
    }
    return (0);
}

int
gfarmGetNameOfSocket(int sock, int *portPtr, char **hostPtr)
{
    struct sockaddr_storage sa;
    socklen_t sa_len = sizeof(sa);
    int save_errno;

    if (getsockname(sock, (struct sockaddr *)&sa, &sa_len) != 0) {
	save_errno = errno;
	gflog_notice(GFARM_MSG_1000638, "getsockname: %s", strerror(errno));
	errno = save_errno;
	return (-1);
    }
    if (hostPtr != NULL || portPtr != NULL) {
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	if (gfarm_getnameinfo((struct sockaddr *)&sa, sa_len,
			      hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
			      NI_NUMERICHOST|NI_NUMERICSERV) != 0) {
	    gflog_debug(GFARM_MSG_UNFIXED, "gfarm_getnameinfo() failed");
	    /*
	     * errno doesn't have
	     * GFARM_ERR_CANNOT_RESOLVE_AN_IP_ADDRESS_INTO_A_HOSTNAME
	     */
	    errno = EINVAL;
	    return (-1);
	}

	if (hostPtr != NULL) {
	    *hostPtr = strdup(hbuf);
	    if (*hostPtr == NULL) {
		errno = ENOMEM;
		return (-1);
	    }
	}
	if (portPtr != NULL)
	    *portPtr = atoi(sbuf);
    }
    return (0);
}


int
gfarmWaitReadable(int fd, int timeoutMsec)
{
    int sel, err, save_errno;
    char hostbuf[NI_MAXHOST], *hostaddr_prefix, *hostaddr;
    struct sockaddr_storage sa;
    socklen_t sa_len = sizeof(sa);

    for (;;) {
#ifdef HAVE_POLL
	struct pollfd fds[1];

	fds[0].fd = fd;
	fds[0].events = POLLIN;
	errno = 0;
	sel = poll(fds, 1, timeoutMsec);
#else /* ! HAVE_POLL */
	fd_set rFd;
	struct timeval tv, *tvPtr;

	FD_ZERO(&rFd);
	FD_SET(fd, &rFd);
	if (timeoutMsec == GFARM_GSS_TIMEOUT_INFINITE)
	    tvPtr = NULL;
	else {
	    tv.tv_sec = timeoutMsec / 1000;
	    tv.tv_usec = timeoutMsec % 1000 * 1000;
	    tvPtr = &tv;
	}

	errno = 0;
	sel = select(fd + 1, &rFd, NULL, NULL, tvPtr);
#endif /* ! HAVE_POLL */
	if (sel == 0) {
	    if (getpeername(fd, (struct sockaddr *)&sa, &sa_len) == -1) {
		hostaddr = strerror(errno);
		hostaddr_prefix = "cannot get peer address: ";
	    } else if ((err = gfarm_getnameinfo((struct sockaddr *)&sa, sa_len,
						hostbuf, sizeof(hostbuf),
						NULL, 0,
						NI_NUMERICHOST|NI_NUMERICSERV)
			!= 0)) {
		hostaddr = strerror(err);
		hostaddr_prefix = "cannot convert peer address to string: ";
	    } else {
		hostaddr = hostbuf;
		hostaddr_prefix= "";
	    }
	    gflog_error(GFARM_MSG_1003439,
			"closing network connection due to "
			"no response within %d milliseconds from %s%s",
			timeoutMsec, hostaddr_prefix, hostaddr);
	} else if (sel < 0) {
	    if (errno == EINTR || errno == EAGAIN)
		continue;
	    save_errno = errno;
	    gflog_error(GFARM_MSG_1000639, "select: %s", strerror(errno));
	    errno = save_errno;
	}
	return sel;
    }
}


int
gfarmReadInt8(int fd, gfarm_int8_t *buf, int len, int timeoutMsec)
{
    int sum = 0;
    int cur = 0;
    int sel, save_errno;

    do {
	sel = gfarmWaitReadable(fd, timeoutMsec);
	if (sel <= 0) {
	    if (sel == 0)
		errno = ETIMEDOUT;
	    return sum;
	}
	cur = read(fd, buf + sum, len - sum);
	if (cur < 0) {
	    save_errno = errno;
	    gflog_info(GFARM_MSG_1000640, "read: %s", strerror(errno));
	    errno = save_errno;
	    return sum;
	} else if (cur == 0) {
	    /* errno doesn't have GFARM_ERR_UNEXPECTED_EOF */
	    errno = ECONNABORTED;
	    break;
	}
	sum += cur;
    } while (sum < len);
    return sum;
}


int
gfarmReadInt16(int fd, gfarm_int16_t *buf, int len, int timeoutMsec)
{
    int i;
    int n;
    gfarm_int16_t s;

    for (i = 0; i < len; i++) {
	n = gfarmReadInt8(fd, (gfarm_int8_t *)&s, GFARM_OCTETS_PER_16BIT,
			  timeoutMsec);
	if (n != GFARM_OCTETS_PER_16BIT) {
	    return i;
	}
	buf[i] = ntohs(s);
    }

    return i;
}


int
gfarmReadInt32(int fd, gfarm_int32_t *buf, int len, int timeoutMsec)
{
    int i;
    int n;
    gfarm_int32_t l;

    for (i = 0; i < len; i++) {
	n = gfarmReadInt8(fd, (gfarm_int8_t *)&l, GFARM_OCTETS_PER_32BIT,
			  timeoutMsec);
	if (n != GFARM_OCTETS_PER_32BIT) {
	    return i;
	}
	buf[i] = ntohl(l);
    }

    return i;
}


int
gfarmWriteInt8(int fd, gfarm_int8_t *buf, int len)
{
    int sum = 0;
    int cur = 0;
    int save_errno;

    do {
	cur = gfarm_send_no_sigpipe(fd, buf + sum, len - sum);
	if (cur < 0) {
	    save_errno = errno;
	    gflog_info(GFARM_MSG_1000641, "write: %s", strerror(errno));
	    errno = save_errno;
	    return sum;
	}
	sum += cur;
    } while (sum < len);
    return sum;
}


int
gfarmWriteInt16(int fd, gfarm_int16_t *buf, int len)
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
gfarmWriteInt32(int fd, gfarm_int32_t *buf, int len)
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

