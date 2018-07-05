/*
 * Copyright (C) 1993-2018 by Software Research Associates, Inc.
 *	2-32-8, Minami-Ikebukuro, Toshima-ku, Tokyo 171-8513, Japan
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE SOFTWARE RESEARCH
 * ASSOCIATES BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the Software
 * Research Associates shall not be used in advertising or otherwise
 * to promote the sale, use or other dealings in this Software without
 * prior written authorization from the Software Research Associates.
 *
 * $Id$
 *
 */

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <poll.h>
#include <netdb.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>

#ifndef INFTIM
#define INFTIM	(-1)
#endif

#define MAX_SOCKETS 256

enum exit_code {
	EXIT_OK		= 0,
	EXIT_NETERR	= 1,
	EXIT_USAGE	= 2,
	EXIT_SYSERR	= 3,
	EXIT_TIMEOUT	= 4,
	EXIT_AI_ERROR	= 5,
};

static char *progname = "nconnect_simple";

static bool debug_flag = false;

#define	debug(statement) \
	{ \
		if (debug_flag) { \
			statement; \
		} \
	}

#ifdef __GNUC__
static void
fatal_errno(char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
#endif

static void
fatal_errno(char *fmt, ...)
{
	int save_errno = errno;
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", progname);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ": %s\n", strerror(save_errno));
	exit(EXIT_NETERR);
}

#ifdef __GNUC__
static void
numeric_printf(FILE *fp, const struct sockaddr *sa, socklen_t salen,
	char *fmt, ...)
	__attribute__((__format__(__printf__, 4, 5)));
#endif

static void
numeric_printf(FILE *fp, const struct sockaddr *sa, socklen_t salen,
	char *fmt, ...)
{
	va_list ap;
	int ai_error;
	char host[NI_MAXHOST], service[NI_MAXSERV];

	va_start(ap, fmt);

	ai_error = getnameinfo(sa, salen,
	    host, sizeof(host), service, sizeof(service),
	    NI_NUMERICHOST | NI_NUMERICSERV);
	if (ai_error != 0) {
		fprintf(stderr, "getnameinfo(): %s\n", gai_strerror(ai_error));
		vfprintf(fp, fmt, ap);
	}  else {
		fprintf(fp, "%s <%s>: ", host, service);
		vfprintf(fp, fmt, ap);
	}

	va_end(ap);
}

static void
numeric_report(const struct sockaddr *sa, socklen_t salen)
{
	numeric_printf(stderr, sa, salen, "\n");
}

static void
numeric_error(struct addrinfo *res, const char *cause, int saved_errno)
{
	numeric_printf(stderr, res->ai_addr, res->ai_addrlen, "%s: %s\n",
	    cause, strerror(saved_errno));
}

static int
make_family_type(const char *name)
{
	if (strcmp(name, "unspec") == 0)
		return AF_UNSPEC;
	else if (strcmp(name, "unix") == 0)
		return AF_UNIX;
#ifdef AF_LOCAL
	else if (strcmp(name, "local") == 0)
		return AF_LOCAL;
#endif
	else if (strcmp(name, "inet") == 0)
		return AF_INET;
#ifdef AF_INET6
	else if (strcmp(name, "inet6") == 0)
		return AF_INET6;
#endif
#ifdef AF_BLUETOOTH
	else if (strcmp(name, "bluetooth") == 0)
		return AF_BLUETOOTH;
#endif
	else {
		fprintf(stderr, "%s: unknown family name \"%s\"\n",
			progname, name);
		exit(EXIT_USAGE);
	}
}

static int
make_socket_type(const char *name)
{
	if (strcmp(name, "stream") == 0)
		return SOCK_STREAM;
	else if (strcmp(name, "dgram") == 0 || strcmp(name, "datagram") == 0)
		return SOCK_DGRAM;
#ifdef SOCK_RAW
	else if (strcmp(name, "raw") == 0)
		return SOCK_RAW;
#endif
#ifdef SOCK_RDM
	else if (strcmp(name, "rdm") == 0) /* reliably-delivered message */
		return SOCK_RDM;
#endif
#ifdef SOCK_SEQPACKET
	else if (strcmp(name, "seqpacket") == 0) /* sequenced packet stream */
		return SOCK_SEQPACKET;
#endif
#ifdef SOCK_DCCP
	else if (strcmp(name, "dccp") == 0)
		return SOCK_DCCP; /* Datagram Congestion Control Protocol */
#endif
	else {
		fprintf(stderr, "%s: unknown socket type \"%s\"\n",
			progname, name);
		exit(EXIT_USAGE);
	}
}

static bool
socket_type_needs_listen(int socket_type)
{
	switch (socket_type) {
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		return true;
	default:
		return false;
	}
}

static int
my_socket(int domain, int type, int protocol)
{
	int rv = socket(domain, type, protocol);

	if (rv < 0)
		return (rv);
#ifdef IPV6_V6ONLY
	if (domain == PF_INET6) {
		int save_errno = errno, opt = 1;

		if (setsockopt(rv, IPPROTO_IPV6, IPV6_V6ONLY,
		    &opt, sizeof(opt)) < 0) {
			fprintf(stderr, "%s: setsockopt(, "
			    "IPPROTO_IPV6, IPV6_V6ONLY): %s\n",
			    progname, strerror(errno));
		}
		errno = save_errno;
	}
#endif
	return (rv);
}

/*** Queue ***************************************************/

#define	QBUFSIZE 4096

typedef struct Queue {
	char buffer[QBUFSIZE];
	int length;
	int point;
	const char *tag;
}	Queue;

static char *session_log = NULL;	/* default - do not log session */
static FILE *session_log_fp = NULL;	/* default - log file is not opened */

static void
makeQueue(Queue *q, const char *tag)
{
	q->length = 0;
	q->point = 0;
	q->tag = tag;
}

static bool
queueIsEmpty(const Queue *q)
{
	return q->point >= q->length;
}

static bool
queueIsFull(const Queue *q)
{
    /* this queue is NOT true queue */
	return !queueIsEmpty(q);
}

static bool
enqueue(Queue *q, int fd)	/* if success (not end-of-file) then YES */
{
	int done;

	if (queueIsFull(q))
		return true;
	done = read(fd, q->buffer, QBUFSIZE);
	debug(fprintf(stderr, "\tread() - %d\n", done));
	if (done < 0) {
		if (errno == EINTR)
			return true;
		fatal_errno("enqueue");
	}
	q->point = 0;
	q->length = done;

	if (session_log != NULL) {
		switch (session_log_fp == NULL) {
		case 1:
			session_log_fp = fopen(session_log, "a");
			if (session_log_fp == NULL) {
				perror(session_log);
				session_log = NULL; /* give up logging */
				break;
			}
			/*FALLTHROUGH*/
		default:
			fprintf(session_log_fp, "%s:%d/<%.*s>\n",
				q->tag, done, done, q->buffer);
			fflush(session_log_fp);
			break;
		}
	}

	return done > 0;
}

static void
dequeue(Queue *q, int fd)
{
	int done;

	if (queueIsEmpty(q))
		return;
	done = write(fd, q->buffer + q->point, q->length - q->point);
	debug(fprintf(stderr, "\twrite() - %d\n", done));
	if (done < 0) {
		if (errno == EINTR)
			return;
		fatal_errno("dequeue");
	}
	q->point += done;
}

/*** main  ***************************************************/

static void
set_fd(fd_set *set, int fd, bool available)
{
	if (available)
		FD_SET(fd, set);
	else
		FD_CLR(fd, set);
}

enum SocketMode { SOCK_SENDRECV, SOCK_SEND, SOCK_RECV };

void
transfer(int fd, enum SocketMode socket_mode, bool one_eof_exit)
{
	fd_set	readfds, writefds, exceptfds;
	int	nfds = fd + 1,
		nfound;
	bool	eof_stdin  = false,
		eof_socket = false;
	Queue	sendq, recvq;

	switch (socket_mode) {
	case SOCK_RECV:
		eof_stdin = true;
		break;
	case SOCK_SEND:
		eof_socket = true;
		break;
	case SOCK_SENDRECV:
		/* nothing to do */
		break;
	}
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	makeQueue(&sendq, "SEND");
	makeQueue(&recvq, "RECV");
	while (!eof_stdin  || !queueIsEmpty(&sendq) ||
	       !eof_socket || !queueIsEmpty(&recvq)) {
		set_fd(&readfds,   0, !queueIsFull(&sendq) && !eof_stdin);
		set_fd(&readfds,  fd, !queueIsFull(&recvq) && !eof_socket);
		set_fd(&writefds, fd, !queueIsEmpty(&sendq));
		set_fd(&writefds,  1, !queueIsEmpty(&recvq));
		debug(fprintf(stderr, "\tselect() wait : %d%d%d%d\n",
			FD_ISSET(0, &readfds)!=0, FD_ISSET(1, &writefds)!=0,
			FD_ISSET(fd,&readfds)!=0, FD_ISSET(fd,&writefds)!=0));
		/* if socket is EOF, select() says "you can read socket." */
		nfound = select(nfds, &readfds, &writefds, &exceptfds, NULL);
		debug(fprintf(stderr, "\tselect() -- %d : %d%d%d%d\n",
			nfound,
			FD_ISSET(0, &readfds)!=0, FD_ISSET(1, &writefds)!=0,
			FD_ISSET(fd,&readfds)!=0, FD_ISSET(fd,&writefds)!=0));
		if (nfound < 0) {
			if (errno == EINTR)
				continue;
			fatal_errno("select");
		}
		if (FD_ISSET(0, &readfds)) {
			if (!enqueue(&sendq, 0)) {
				if (one_eof_exit)
					eof_socket = true;
				eof_stdin = true;
				if (queueIsEmpty(&sendq)) {
					if (shutdown(fd, SHUT_WR) < 0)
						fatal_errno("shutdown");
				}
			}
		}
		if (FD_ISSET(fd, &writefds) && !queueIsEmpty(&sendq)) {
			dequeue(&sendq, fd);
			if (eof_stdin && queueIsEmpty(&sendq)) {
				if (shutdown(fd, SHUT_WR) < 0)
					fatal_errno("shutdown");
			}
		}
		if (FD_ISSET(fd, &readfds)) {
			if (!enqueue(&recvq, fd)) {
				if (one_eof_exit)
					eof_stdin = true;
				eof_socket = true;
			}
		}
		if (FD_ISSET(1, &writefds)) {
			dequeue(&recvq, 1);
		}
	}

	if (session_log_fp != NULL) {
		fprintf(session_log_fp, "CLOSED\n");
		fclose(session_log_fp);
		session_log_fp = NULL;
	}
}

static void
doit(int fd, enum SocketMode socket_mode, bool one_eof_exit)
{
	switch (socket_mode) {
	case SOCK_RECV:
		if (shutdown(fd, SHUT_WR) < 0)
			fatal_errno("shutdown");
		break;
	case SOCK_SEND:
		/*
		 * This shutdown(2) has no effect, I THINK.
		 */
		if (shutdown(fd, SHUT_RD) < 0)
			fatal_errno("shutdown");
		break;
	case SOCK_SENDRECV:
		/* nothing to do */
		break;
	}
	transfer(fd, socket_mode, one_eof_exit);
	close(fd);
	exit(EXIT_OK);
}

static void
initiate(const char *host, const char *service,
	const char *bind_host, const char *bind_service,
	struct addrinfo *hints,
	enum SocketMode socket_mode, bool one_eof_exit, bool verbose)
{
	int ai_error;
	struct addrinfo *res, *res0;

	ai_error = getaddrinfo(host, service, hints, &res0);
	if (service == NULL)
		service = "(null)";
	if (ai_error != 0) {
		fprintf(stderr, "%s:%s: %s\n",
		    host, service, gai_strerror(ai_error));
		exit(EXIT_AI_ERROR);
	}
	for (res = res0; res != NULL; res = res->ai_next) {
		int sock = my_socket(res->ai_family,
		    res->ai_socktype, res->ai_protocol);

		if (sock < 0) {
			numeric_error(res, "socket", errno);
			continue;
		}
		if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
			numeric_error(res, "connect", errno);
			continue;
		}
		/* OK */
		if (verbose)
			numeric_report(res->ai_addr, res->ai_addrlen);
		doit(sock, socket_mode, one_eof_exit);
	}
	freeaddrinfo(res0);
	exit(EXIT_SYSERR);
}

static void
respond(int listen_backlog,
	const char *bind_host, const char *bind_service,
	struct addrinfo *hints,
	enum SocketMode socket_mode, bool one_eof_exit, bool verbose)
{
	int ai_error;
	struct addrinfo *res, *res0, *ai[MAX_SOCKETS];
	int nsocks = 0, socks[MAX_SOCKETS];

	hints->ai_flags = AI_PASSIVE;
	ai_error = getaddrinfo(bind_host, bind_service, hints, &res0);
	if (bind_service == NULL)
		bind_service = "(null)";
	if (ai_error != 0) {
		fprintf(stderr, "<%s>: %s\n",
		    bind_service, gai_strerror(ai_error));
		exit(EXIT_AI_ERROR);
	}
	for (res = res0; res != NULL; res = res->ai_next) {
		if (nsocks >= MAX_SOCKETS) {
			fprintf(stderr, "number of addresses exceeds "
			    "%d, please increase MAX_SOCKETS\n",
			    nsocks);
			exit(EXIT_SYSERR);
		}
		socks[nsocks] = my_socket(res->ai_family,
		    res->ai_socktype, res->ai_protocol);
		if (socks[nsocks] < 0) {
			numeric_error(res, "socket", errno);
			continue;
		}
		if (bind(socks[nsocks], res->ai_addr, res->ai_addrlen) < 0) {
			numeric_error(res, "bind", errno);
			continue;
		}
		if (socket_type_needs_listen(hints->ai_socktype) &&
		    listen(socks[nsocks], listen_backlog) < 0) {
			numeric_error(res, "listen", errno);
			continue;
		}
		ai[nsocks] = res;
		nsocks++;
	}
	if (nsocks == 0)
		exit(EXIT_SYSERR);
	for (;;) {
		struct pollfd pollfds[MAX_SOCKETS];
		int i, rv;

		for (i = 0; i < nsocks; i++) {
			struct pollfd *pfd = &pollfds[i];

			pfd->fd = socks[i];
			pfd->events = POLLIN;
			pfd->revents = 0;
		}
		while ((rv = poll(pollfds, nsocks, INFTIM)) == -1 &&
		    errno == EINTR)
			continue;
		for (i = 0; i < nsocks; i++) {
			if (pollfds[i].revents == 0)
				continue;

			if (!socket_type_needs_listen(hints->ai_socktype)) {
				if (verbose)
					numeric_report(
					    ai[i]->ai_addr, ai[i]->ai_addrlen);
				doit(socks[i], socket_mode, one_eof_exit);
				exit(EXIT_OK);
			} else {
				struct sockaddr_storage ss;
				struct sockaddr *const sa =
				    (struct sockaddr *)&ss;
				socklen_t socklen = sizeof(ss);
				int sock = accept(socks[i], sa, &socklen);

				if (sock < 0) {
					numeric_error(ai[i], "accept", errno);
					continue;
				}
				if (verbose)
					numeric_report(sa, socklen);
				doit(sock, socket_mode, one_eof_exit);
				exit(EXIT_OK);
			}
		}
	}
	/*NOTREACHED*/
}

static void
parse_host_service(char *s, char **hostp, char **servicep)
{
	char *p;

	if (*s == '\0') {
		/* "" -> NULL:NULL */
		*hostp = NULL;
		*servicep = NULL;
		return;
	} else if (*s == '[') {
		p = strchr(s + 1, ']');
		if (p == NULL) {
			fprintf(stderr, "%s: \"%s\": closing-] not found\n",
			    progname, s);
			exit(EXIT_USAGE);
		}
		*p = '\0';
		*hostp = s + 1;
		if (p[1] == '\0') {
			/* "[host]" -> host:NULL */
			*servicep = NULL;
			return;
		}
		if (p[1] != ':') {
			fprintf(stderr,
			    "%s: \"%s\": unexpected char at \"%s\"\n",
			    progname, s, p);
			exit(EXIT_USAGE);
		}
		/* "[host]:*" -> host:* */
		s = p + 2;
	} else if (*s == ':') {
		/* ":*" -> NULL:* */
		*hostp = NULL;
		s++;
	} else {
		*hostp = s;
		p = strchr(s + 1, ':');
		if (p == NULL) {
			/* "host" -> host:NULL */
			*servicep = NULL;
			return;
		}
		/* "host:*" -> host:* */
		*p = '\0';
		s = p + 1;
	}
	if (*s == '\0')
		*servicep = NULL;
	else
		*servicep = s;
}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-srk] [-b <address>] <address>\n",
		progname);
	fprintf(stderr, "       %s [-srk] -b <address>\n", progname);
	fprintf(stderr,	"\t\t<address> format is :\n");
	fprintf(stderr,	"\t\t\tunix/pathname\t\t- unix domain socket\n");
	fprintf(stderr, "\t\t\t[host][:port[:port]]\t- inet domain socket\n");
	exit(EXIT_USAGE);
}

int
main(int argc, char **argv)
{
	int c;
	struct addrinfo hints;
	enum SocketMode socket_mode = SOCK_SENDRECV;
	char *bind_host = NULL, *bind_service = NULL;
	bool one_eof_exit = true;
	bool verbose = false;
	int listen_backlog = SOMAXCONN;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	while ((c = getopt(argc, argv, "b:f:ikLRst:v")) != -1) {
		switch (c) {
		case 'b':
			parse_host_service(optarg, &bind_host, &bind_service);
			break;
		case 'f':
			hints.ai_family = make_family_type(optarg);
			break;
		case 'i': /* interactive (like `telnet') */
			one_eof_exit = true;
			break;
		case 'k': /* keep */
			one_eof_exit = false;
			break;
		case 'L':
			listen_backlog = atoi(optarg);
			break;
		case 'r':
			socket_mode = SOCK_RECV;
			break;
		case 's':
			socket_mode = SOCK_SEND;
			break;
		case 't':
			hints.ai_socktype = make_socket_type(optarg);
			break;
		case 'v':
			verbose = true;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc >= 2)
		usage();

	signal(SIGPIPE, SIG_IGN);

	if (argc == 1) {
		char *host, *service;

		if (bind_host != NULL || bind_service != NULL) {
			fprintf(stderr, "%s: -b option for initiator mode "
			    "isn't supported yet\n", progname);
			exit(EXIT_USAGE);
		}
		parse_host_service(argv[0], &host, &service);
		initiate(host, service, bind_host, bind_service, &hints,
		    socket_mode, one_eof_exit, verbose);
	} else {
		respond(listen_backlog, bind_host, bind_service, &hints,
		    socket_mode, one_eof_exit, verbose);
	}
	/*NOTREACHED*/
	return EXIT_SYSERR;
}
