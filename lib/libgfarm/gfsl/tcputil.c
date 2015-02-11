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
    hints.ai_family = AF_INET;
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
gfarmTCPBindPort(int port)
{
    struct addrinfo hints, *res;
    int sock, e, save_errno = 0;
    int one = 1;
    char sbuf[NI_MAXSERV];

    snprintf(sbuf, sizeof(sbuf), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    e = gfarm_getaddrinfo(NULL, sbuf, &hints, &res);
    if (e) {
	gflog_error(GFARM_MSG_1000632,
	    "getaddrinfo(port = %u): %s", port, gai_strerror(e));
	errno = EINVAL; /* errno doesn't have GFARM_ERR_UNKNOWN_HOST */
	return -1;
    }
    if (res == NULL) {
	gflog_debug(GFARM_MSG_1000803,
	    "gfarm_getaddinfo() failed for port (%d)", port);
	errno = EINVAL; /* errno doesn't have GFARM_ERR_UNKNOWN_HOST */
	return -1;
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
	save_errno = errno;
	gflog_error(GFARM_MSG_1000633, "socket: %s", strerror(errno));
    } else if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
			  (void *)&one, sizeof(int)) != 0) {
	save_errno = errno;
	gflog_notice(GFARM_MSG_1000634, "setsockopt: %s", strerror(errno));
	close(sock);
	sock = -1;
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
    }
    gfarm_freeaddrinfo(res);
    errno = save_errno;

    return sock;
}


int
gfarmGetPeernameOfSocket(int sock, int *portPtr, char **hostPtr)
{
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    int save_errno;
    char hbuf[NI_MAXHOST];

    if (getpeername(sock, (struct sockaddr *)&sin, &slen) != 0) {
	save_errno = errno;
	gflog_notice(GFARM_MSG_1000637, "getpeername: %s", strerror(errno));
	errno = save_errno;
	return (-1);
    }
    if (hostPtr != NULL) {
	if (gfarm_getnameinfo((struct sockaddr *)&sin, slen,
			      hbuf, sizeof(hbuf), NULL, 0, 0) != 0) {
	    gflog_debug(GFARM_MSG_1000804, "gfarm_getnameinfo() failed");
	    /*
	     * errno doesn't have
	     * GFARM_ERR_CANNOT_RESOLVE_AN_IP_ADDRESS_INTO_A_HOSTNAME
	     */
	    errno = EINVAL; 
	    return (-1);
	}
	*hostPtr = strdup(hbuf);
	if (*hostPtr == NULL) {
	    errno = ENOMEM;
	    return (-1);
	}
    }
    if (portPtr != NULL)
	*portPtr = (int)ntohs(sin.sin_port);
    return (0);
}


int
gfarmGetNameOfSocket(int sock, int *portPtr)
{
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    int save_errno;
    
    if (getsockname(sock, (struct sockaddr *)&sin, &slen) != 0) {
	save_errno = errno;
	gflog_notice(GFARM_MSG_1000638, "getsockname: %s", strerror(errno));
	errno = save_errno;
	return (-1);
    }
    if (portPtr != NULL)
	*portPtr = (int)ntohs(sin.sin_port);
    return (0);
}


int
gfarmWaitReadable(int fd, int timeoutMsec)
{
    int sel, err, save_errno;
    char hostbuf[NI_MAXHOST], *hostaddr_prefix, *hostaddr;
    struct sockaddr_in sin;
    socklen_t slen = sizeof(sin);

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
	    if (getpeername(fd, (struct sockaddr *)&sin, &slen) == -1) {
		hostaddr = strerror(errno);
		hostaddr_prefix = "cannot get peer address: ";
	    } else if ((err = gfarm_getnameinfo((struct sockaddr *)&sin, slen,
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

