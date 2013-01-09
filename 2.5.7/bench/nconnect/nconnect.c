/*
 * Copyright (C) 1993-2000 by Software Research Associates, Inc.
 *	1-1-1 Hirakawa-cho, Chiyoda-ku, Tokyo 102-8605, Japan
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
 * Id: nconnect.c,v 1.21 2002/09/19 06:43:57 soda Exp 
 *
 */

#include <sys/types.h>
#include <sys/param.h>		/* ntohs(), ... for BSD direct descendant */
#include <sys/socket.h>
#include <sys/un.h>		/* PF_UNIX */
#include <netinet/in.h>		/* PF_INET */
#include <netinet/tcp.h>	/* TCP_NODELAY */
#include <arpa/inet.h>		/* inet_addr(), inet_ntoa() */
#include <netdb.h>		/* gethostbyname(), getservbyname(), ... */
#include <stdio.h>
#include <ctype.h>
#include <signal.h>		/* SIGPIPE */
#include <errno.h>
extern int errno;		/* some <errno.h> doesn't define `errno' */
#ifdef __STDC__
#include <string.h>		/* strlen(), ... & bzero()/bcopy()/bcmp() */
#include <stdlib.h>		/* atoi(), ... */
#endif
#ifndef NO_UNISTD_H
#include <unistd.h>
#endif
#include <sys/wait.h>		/* waitpid()/wait3() */

#ifdef _AIX
#include <sys/select.h>		/* fd_set, ... */
#endif

#include <gfarm/gfarm_config.h>

/* 2nd argument of shutdown(2) */
#define	SHUTDOWN_RECV	0	/* shutdown receive */
#define	SHUTDOWN_SEND	1	/* shutdown send */
#define	SHUTDOWN_SOCK	2	/* shutdown send & receive */

/* 2nd argument of listen(2) */
#define	LISTEN_BACKLOG	5

/*** portable ************************************************/

#ifdef POSIX
# define NO_BSTRING
# define NO_INDEX
#endif

#ifdef NO_BSTRING
#define	bcmp(a, b, length)	memcmp(a, b, length)
#define	bcopy(s, d, length)	memcpy(d, s, length)
#define	bzero(b, length)	memset(b, 0, length)
#endif

extern char *index(), *rindex();

#ifdef NO_INDEX
extern char *strchr(), *strrchr();

#define	index(string, c)	strchr(string, c)
#define	rindex(string, c)	strrchr(string, c)
#endif

#ifdef NO_SIZE_T
typedef int size_t;
#endif

#ifdef __STDC__
typedef void *Pointer;
#else
typedef char *Pointer;
#define	const
#define	volatile
#endif

extern Pointer malloc();

#ifndef POSIX
# if !defined(SIGTSTP) || defined(SV_BSDSIG) || defined(SA_OLDSTYLE) || (defined(SA_NOCLDWAIT) && defined(SIGCLD))
		/* SysV3 or earlier || HP-UX || AIX || SVR4 */
#  define CAN_IGNORE_ZOMBIE
#  define SIGNAL signal
# else /* guarantee reliable signal */
void (*reliable_signal(sig, handler))()
	int sig;
	void (*handler)();
{
	struct sigvec sv, osv;

	sv.sv_handler = handler;
	sv.sv_mask = 0;
	sv.sv_flags = 0; /* XXX - no singal stack */
	if (sigvec(sig, &sv, &osv) < 0)
		return (void (*)())-1;
	return osv.sv_handler;
}
#  define SIGNAL reliable_signal
# endif
#else /* POSIX - don't use undefined behavior */
/*
 * This function installs reliable signal handler, but not (always)
 * BSD signal compatible. Because interrupted system call is not
 * always restarted.
 * At least, BSD Net/2 or later and SVR4 and AIX (SA_RESTART is not
 * specified) and HP-UX (struct sigcontext::sc_syscall_action ==
 * SIG_RETURN) are not BSD compatible. On SunOS4, this function is BSD
 * signal compatible (because SA_INTERRUPT is not specifed).
 */
void (*reliable_signal(sig, handler))()
	int sig;
	void (*handler)();
{
	struct sigaction sa, osa;

	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(sig, &sa, &osa) < 0)
		return (void (*)())-1;
	return osa.sa_handler;
}
# define SIGNAL	reliable_signal
#endif /* POSIX */

/*** miscellaneous *******************************************/

#define	ARRAY_LENGTH(array)	(sizeof(array)/sizeof(array[0]))

typedef int Bool;
#define	YES	1
#define	NO	0

static char *progname = "nconnect";

static Bool debug_flag = NO;

#define	debug(statement) \
	{ \
		if (debug_flag) { \
			statement; \
		} \
	}

#define	EXIT_OK		0
#define	EXIT_USAGE	2
#define	EXIT_NETERR	1
#define	EXIT_SYSERR	3

void fatal(message)
	char *message;
{
	perror(message);
	exit(EXIT_NETERR);
}

Pointer emalloc(size)
	size_t size;
{
	Pointer p = malloc(size);

	if (p == NULL) {
		fprintf(stderr, "%s: no memory\n", progname);
		exit(EXIT_SYSERR);
	}
	return p;
}

/*** sockaddr ************************************************/

union generic_sockaddr {
	struct sockaddr address;
	struct sockaddr_un un;
	struct sockaddr_in in;
};

size_t make_unix_address(address, pathname)
	struct sockaddr_un *address;
	char *pathname;
{
	size_t len = strlen(pathname);

	if (len >= sizeof(address->sun_path)) {
		fputs(pathname, stderr);
		fputs(": socket name too long\n", stderr);
		exit(EXIT_USAGE);
	}
	bzero((char *)address, sizeof(*address)); /* needless ? */
	bcopy(pathname, address->sun_path, len + 1);
	address->sun_family = AF_UNIX;
#ifdef SUN_LEN /* derived from 4.4BSD */
	return SUN_LEN(address);
#else
	return sizeof(*address) - sizeof(address->sun_path) + len;
#endif
}

/*
 * make Internet address
 * argument `ip_address' is network byte order IP-address,
 *	or htonl(INADDR_ANY), or htonl(INADDR_BROADCAST).
 * argument `port' is network byte order port-number or 0.
 *	0 is for bind(2). bind(2) automagically allocates port number.
 */
size_t make_inet_address(address, ip_address, port)
	struct sockaddr_in *address;
	unsigned long ip_address;
	int port;
{
	bzero((char *)address, sizeof(*address));
	address->sin_addr.s_addr = ip_address;
	address->sin_family = AF_INET;
	address->sin_port = port;
	return sizeof(*address);
}

size_t make_inet_address_by_string(address, host, port)
	struct sockaddr_in *address;
	char *host;
	int port;
{
	struct hostent *hp;
	struct in_addr in;

	if ((in.s_addr = inet_addr(host)) != (unsigned long)-1)
		return make_inet_address(address, in.s_addr, port);
	if ((hp = gethostbyname(host)) != NULL) {
		bzero((char *)address, sizeof(*address));
		bcopy(hp->h_addr, (char *)&address->sin_addr,
		      sizeof(address->sin_addr));
		address->sin_family = hp->h_addrtype;
		address->sin_port = port;
		return sizeof(*address);
	}
	/*
	 * BUG:
	 *	if (hp == NULL && h_errno == TRY_AGAIN)
	 *		we must try again;
	 */
	fputs(host, stderr); fputs(": unknown host\n", stderr);
	exit(EXIT_USAGE);
}

int make_inet_port_by_string(portname, protocol)
	char *portname, *protocol;
{
	struct servent *sp;

	if (isdigit(portname[0])) {
		return htons(atoi(portname));
	} else {
		if (protocol == NULL) {
			fprintf(stderr, "%s: need port number\n", portname);
			exit(EXIT_SYSERR);
		}
		if ((sp = getservbyname(portname, protocol)) == NULL) {
			fprintf(stderr, "%s/%s: unknown service\n",
				portname, protocol);
			exit(EXIT_SYSERR);
		}
		return sp->s_port;
	}
}

size_t make_socket_address(address, name, socket_type)
	union generic_sockaddr *address;
	char *name;
	int socket_type;
{
	int rv;

	if (bcmp(name, "unix/", (size_t)5) == 0) {		/* AF_UNIX */
		return make_unix_address(&address->un, name + 5);
	} else {						/* AF_INET */
		char *colon = index(name, ':'), *protocol;
		int port;

		if (colon == NULL) {
			port = 0;
		} else {
			*colon = '\0';	/* XXX - breaks *name */
			switch (socket_type) {
			case SOCK_STREAM:
				protocol = "tcp";
				break;
			case SOCK_DGRAM:
				protocol = "udp";
				break;
			default:
				protocol = NULL;
				break;
			}
			port = make_inet_port_by_string(colon + 1, protocol);
		}
		rv = *name == '\0' ?
			make_inet_address(&address->in,
				htonl(INADDR_ANY), port) :
			make_inet_address_by_string(&address->in,
				name, port);
		if (colon != NULL)
			*colon = ':';	/* XXX - restore *name */
		return rv;
	}
}

int make_socket_type(name)
	char *name;
{
	if (strcmp(name, "stream") == 0)
		return SOCK_STREAM;
	else if (strcmp(name, "dgram") == 0 || strcmp(name, "datagram") == 0)
		return SOCK_DGRAM;
#ifdef USE_RAW_SOCKET
	else if (strcmp(name, "raw") == 0)
		return SOCK_RAW;
#endif
	else if (strcmp(name, "rdm") == 0) /* reliably-delivered message */
		return SOCK_RDM;
	else if (strcmp(name, "seqpacket") == 0) /* sequenced packet stream */
		return SOCK_SEQPACKET;
	else {
		fprintf(stderr, "%s: unknown socket type \"%s\"\n",
			progname, name);
		exit(EXIT_NETERR);
	}
}

Bool socket_type_is_stream(socket_type)
	int socket_type;
{
	switch (socket_type) {
	case SOCK_STREAM:
		return YES;
	default:
		return NO;
	}
}

void print_sockaddr(fp, address, addr_size)
	FILE *fp;
	struct sockaddr *address;
	size_t addr_size;
{
	union generic_sockaddr *p = (union generic_sockaddr *)address;

	switch (p->address.sa_family) {
	case AF_UNIX:
		fputs("AF_UNIX: pathname = \"", fp);
		fwrite(p->un.sun_path, sizeof(char),
		       addr_size - (sizeof(p->un) - sizeof(p->un.sun_path)),
		       fp);
		fputs("\"\n", fp);
		break;
	case AF_INET:
		fprintf(fp, "AF_INET: address = %s, port = %d\n",
			inet_ntoa(p->in.sin_addr), ntohs(p->in.sin_port));
		break;
	default:
		fprintf(fp,"%s: unknown address family %d\n",
			progname, p->address.sa_family);
		break;
	}
}

void print_sockname(fd)
	int fd;
{
	union generic_sockaddr generic;
	socklen_t addr_size = sizeof(generic);

	if (getsockname(fd, &generic.address, &addr_size) < 0) {
		perror("getsockname");
		/* exit(EXIT_NETERR); */
	} else {
		print_sockaddr(stderr, &generic.address, (size_t)addr_size);
	}
}

void print_peername(fd)
	int fd;
{
	union generic_sockaddr generic;
	socklen_t addr_size = sizeof(generic);

	if (getpeername(fd, &generic.address, &addr_size) < 0) {
		perror("getpeername");
		/* exit(EXIT_NETERR); */
	} else {
		print_sockaddr(stderr, &generic.address, (size_t)addr_size);
	}
}

void print_sockport(fp, fd)
	FILE *fp;
	int fd;
{
	union generic_sockaddr generic;
	socklen_t addr_size = sizeof(generic);

	if (getsockname(fd, &generic.address, &addr_size) < 0) {
		fatal("getsockname");
	}
	switch (generic.address.sa_family) {
	case AF_INET:
		fprintf(fp, "%d\n", ntohs(generic.in.sin_port));
		break;
	default:
		fprintf(fp,"%s: cannot print port for address family %d\n",
			progname, generic.address.sa_family);
		exit(EXIT_SYSERR);
	}
}

/*** Queue ***************************************************/

#define	QBUFSIZE 4096

typedef struct Queue {
	char buffer[QBUFSIZE];
	int length;
	int point;
	char *tag;
}	Queue;

char *session_log = NULL;	/* default - do not log session */
FILE *session_log_fp = NULL;	/* default - log file is not opened */

void makeQueue(q, tag)
	Queue *q;
	char *tag;
{
	q->length = 0;
	q->point = 0;
	q->tag = tag;
}

Bool queueIsEmpty(q)
	Queue *q;
{
	return q->point >= q->length;
}

Bool queueIsFull(q)
	Queue *q;
{
    /* this queue is NOT true queue */
	return !queueIsEmpty(q);
}

Bool enqueue(q, fd)		/* if success (not end-of-file) then YES */
	Queue *q;
	int fd;
{
	int done;

	if (queueIsFull(q))
		return YES;
	done = read(fd, q->buffer, QBUFSIZE);
	debug(fprintf(stderr, "\tread() - %d\n", done));
	if (done < 0) {
		if (errno == EINTR)
			return YES;
		fatal("enqueue");
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

int sync_rate = 0;

void dequeue(q, fd)
	Queue *q;
	int fd;
{
	int done;

	if (queueIsEmpty(q))
		return;
	done = write(fd, q->buffer + q->point, q->length - q->point);
	debug(fprintf(stderr, "\twrite() - %d\n", done));
	if (done < 0) {
		if (errno == EINTR)
			return;
		fatal("dequeue");
	}
	q->point += done;
	if (sync_rate != 0 && fd == 1) { /* only for stdout */
		static int written = 0;

		written += done;
		if (written >= sync_rate) {
			written -= sync_rate;
			fdatasync(fd);
		}
	}
}

/*** sockopts ************************************************/

static struct sockopt_table {
	char *name, *proto;
	int level, option, default_value;
} sockopt_tab[] = {
	{ "debug",	NULL, SOL_SOCKET,	SO_DEBUG,	1, },
	{ "reuseaddr",	NULL, SOL_SOCKET,	SO_REUSEADDR,	1, },
	{ "keepalive",	NULL, SOL_SOCKET,	SO_KEEPALIVE,	1, },
	{ "dontroute",	NULL, SOL_SOCKET,	SO_DONTROUTE,	1, },
	{ "broadcast",	NULL, SOL_SOCKET,	SO_BROADCAST,	1, },
#if defined(SO_USELOOPBACK)
	{ "useloopback",NULL, SOL_SOCKET,	SO_USELOOPBACK,	1, },
#endif
	{ "linger",	NULL, SOL_SOCKET,	SO_LINGER,	1, },
	{ "oobinline",	NULL, SOL_SOCKET,	SO_OOBINLINE,	1, },
#if defined(SO_REUSEPORT)
	{ "reuseport",	NULL, SOL_SOCKET,	SO_REUSEPORT,	1, },
#endif
	{ "sndbuf",	NULL, SOL_SOCKET,	SO_SNDBUF,	16384, },
	{ "rcvbuf",	NULL, SOL_SOCKET,	SO_RCVBUF,	16384, },
	{ "sndlowat",	NULL, SOL_SOCKET,	SO_SNDLOWAT,	2048, },
	{ "rcvlowat",	NULL, SOL_SOCKET,	SO_RCVLOWAT,	1, },
#if 0 /* typeof(option) == struct timeval */
	{ "sndtimeo",	NULL, SOL_SOCKET,	SO_SNDTIMEO,	, },
	{ "rcvtimeo",	NULL, SOL_SOCKET,	SO_RCVTIMEO,	, },
#endif
	{ "tcp_nodelay","tcp", 0,		TCP_NODELAY,	1, },
};

#define	MAX_SOCKOPTS	ARRAY_LENGTH(sockopt_tab)

int nsockopts = 0;

struct socket_option {
	char *string;
	int level, option, value;
} sockopts[MAX_SOCKOPTS];

void record_sockopt(option)
	char *option;
{
	/*
	 * SO_TYPE and SO_ERROR are used for
	 * getsockopt() only.
	 * SO_ACCEPTCONN, ...
	 */
	struct sockopt_table *tab;
	char *equal;
	struct protoent *proto;

	equal = strchr(option, '=');
	if (equal != NULL)
		*equal = '\0';
	for (tab = sockopt_tab; tab < &sockopt_tab[MAX_SOCKOPTS]; tab++) {
		if (strcmp(option, tab->name) == 0) {
			if (nsockopts >= MAX_SOCKOPTS) {
				fprintf(stderr, "%s: too many socket options",
					progname);
				exit(EXIT_USAGE);
			}
			sockopts[nsockopts].string = option;
			if (tab->proto == NULL) {
				sockopts[nsockopts].level = tab->level;
			} else {
				proto = getprotobyname(tab->proto);
				if (proto == NULL) {
					fprintf(stderr,
					 "%s: getprotobyname(\"%s\") failed\n",
						progname, tab->proto);
					exit(EXIT_SYSERR);
				}
				sockopts[nsockopts].level = proto->p_proto;
			}
			sockopts[nsockopts].option = tab->option;
			if (equal == NULL)
				sockopts[nsockopts].value = tab->default_value;
			else
				sockopts[nsockopts].value = atol(equal + 1);
			nsockopts++;
			if (equal != NULL)
				*equal = '=';
			return;
		}
	}
	fprintf(stderr, "%s: unknown socket option \"%s\"\n",
		progname, option);
	exit(EXIT_USAGE);
}

void apply_sockopts(fd)
	int fd;
{
	int i;

	for (i = 0; i < nsockopts; i++) {
		if (setsockopt(fd, sockopts[i].level, sockopts[i].option,
				&sockopts[i].value, sizeof(sockopts[i].value))
		    == -1) {
			fatal(sockopts[i].string);
		}
	}
}

/*** main  ***************************************************/

void broken_pipe_handler(sig)
	int sig;
{
	fprintf(stderr, "%s: Broken pipe or socket\n", progname);
	exit(EXIT_NETERR);
}

void set_fd(set, fd, available)
	fd_set *set;
	int fd;
	Bool available;
{
	if (available)
		FD_SET(fd, set);
	else
		FD_CLR(fd, set);
}

enum SocketMode { sock_sendrecv, sock_send, sock_recv };

void transfer(fd, socket_mode, one_eof_exit)
	int fd;
	enum SocketMode socket_mode;
	Bool one_eof_exit;
{
	fd_set	readfds, writefds, exceptfds;
	int	nfds = fd + 1,
		nfound;
	Bool	eof_stdin  = NO,
		eof_socket = NO;
	Queue	sendq, recvq;

	debug(SIGNAL(SIGPIPE, broken_pipe_handler));

	switch (socket_mode) {
	case sock_recv:
		eof_stdin = YES;
		break;
	case sock_send:
		eof_socket = YES;
		break;
#ifdef __GNUC__ /* workaround gcc warning: enumeration value not handled */
	case sock_sendrecv:
		/* nothing to do */
		break;
#endif
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
			fatal("select");
		}
		if (FD_ISSET(0, &readfds)) {
			if (!enqueue(&sendq, 0)) {
				if (one_eof_exit)
					eof_socket = YES;
				eof_stdin = YES;
				if (queueIsEmpty(&sendq)) {
					if (shutdown(fd, SHUTDOWN_SEND) < 0)
						fatal("shutdown");
				}
			}
		}
		if (FD_ISSET(fd, &writefds) && !queueIsEmpty(&sendq)) {
			dequeue(&sendq, fd);
			if (eof_stdin && queueIsEmpty(&sendq)) {
				if (shutdown(fd, SHUTDOWN_SEND) < 0)
					fatal("shutdown");
			}
		}
		if (FD_ISSET(fd, &readfds)) {
			if (!enqueue(&recvq, fd)) {
				if (one_eof_exit)
					eof_stdin = YES;
				eof_socket = YES;
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

void putenv_address(generic, addr_size, prefix)
	union generic_sockaddr *generic;
	int addr_size;
	char *prefix;
{
	struct hostent *hp;
	char buffer[BUFSIZ];

	switch (generic->address.sa_family) {
	case AF_INET:
		sprintf(buffer, "%s_ADDR=%s", prefix,
			inet_ntoa(generic->in.sin_addr));
		putenv(strdup(buffer));
		sprintf(buffer, "%s_PORT=%d", prefix,
			ntohs(generic->in.sin_port));
		putenv(strdup(buffer));
		hp = gethostbyaddr((char *) &generic->in.sin_addr,
			(int)sizeof(generic->in.sin_addr), AF_INET);
		if (hp != NULL) {
			sprintf(buffer, "%s_NAME=%s", prefix, hp->h_name);
			putenv(strdup(buffer));
		}
		break;
	}
}

void env_setup(fd)
	int fd;
{
	union generic_sockaddr generic;
	socklen_t addr_size = sizeof(generic);

	if (getsockname(fd, &generic.address, &addr_size) == 0)
		putenv_address(&generic, addr_size, "SOCK");
	if (getpeername(fd, &generic.address, &addr_size) == 0)
		putenv_address(&generic, addr_size, "PEER");
}

void doit(argv, fd, socket_mode, one_eof_exit)
	int fd;
	enum SocketMode socket_mode;
	char **argv;
	Bool one_eof_exit;
{
	switch (socket_mode) {
	case sock_recv:
		if (shutdown(fd, SHUTDOWN_SEND) < 0)
			fatal("shutdown");
		break;
	case sock_send:
		/*
		 * This shutdown(2) has no effect, I THINK.
		 */
		if (shutdown(fd, SHUTDOWN_RECV) < 0)
			fatal("shutdown");
		break;
#ifdef __GNUC__ /* workaround gcc warning: enumeration value not handled */
	case sock_sendrecv:
		/* nothing to do */
		break;
#endif
	}
	if (argv == NULL) {
		transfer(fd, socket_mode, one_eof_exit);
		close(fd);
		exit(EXIT_OK);
	} else {
		env_setup(fd);
		if (socket_mode != sock_send) {
			close(0); dup2(fd, 0);
		}
		if (socket_mode != sock_recv) {
			close(1); dup2(fd, 1);
		}
		close(fd);
		execvp(argv[0], argv);
		fatal(argv[0]);
	}
}

#ifndef CAN_IGNORE_ZOMBIE
void child_exit_handler(sig)
	int sig;
{
#ifdef POSIX
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
#else
	while (wait3(NULL, WNOHANG, NULL) > 0)
		;
#endif
}
#endif /* CAN_IGNORE_ZOMBIE */

void usage()
{
	fprintf(stderr, "Usage: %s [-srk] [-b <address>] <address>\n",
		progname);
	fprintf(stderr, "       %s [-srk] -b <address>\n", progname);
	fprintf(stderr,	      "\t\t<address> format is :\n");
	fprintf(stderr,	      "\t\t\tunix/pathname\t- unix domain socket\n");
	fprintf(stderr,	      "\t\t\t[host]:port\t- inet domain socket\n");
	exit(EXIT_USAGE);
}

int main(argc, argv)
	int argc;
	char **argv;
{
	union generic_sockaddr peer_addr, self_addr;
	size_t peer_addr_size, self_addr_size;
	int fd;
	int socket_type = SOCK_STREAM;
	enum SocketMode socket_mode = sock_sendrecv;
	char *bind_to = NULL;
	char *connect_to = NULL;
	char **shell_command = NULL; /* if NULL: only data transfer */
	Bool one_eof_exit = YES;
	Bool accept_one_client = YES;
	FILE *port_output = stderr;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	peer_addr_size = 0;
#endif

	if (argc >= 1)
		progname = argv[0];
	while (--argc > 0) {
		char *s;

		if ((s = *++argv)[0] != '-')
			break;
		while (*++s) {
			static char end_loop[] = ".";

			switch(*s) {
			case 'S':
				if (s[1] != '\0') {
					sync_rate = strtol(&s[1], NULL, 0);
				} else {
					if (--argc <= 0)
						usage();
					sync_rate = strtol(*++argv, NULL, 0);
				}
				s = end_loop;
				break;
			case 'd':
				debug_flag = YES;
				break;
			case 'b': /* bind sockname */
				if (s[1] != '\0') {
					bind_to = &s[1];
				} else {
					if (--argc <= 0)
						usage();
					bind_to = *++argv;
				}
				s = end_loop;
				break;
			case 't':
				if (s[1] != '\0') {
					socket_type = make_socket_type(&s[1]);
				} else {
					if (--argc <= 0)
						usage();
					socket_type = make_socket_type(*++argv);
				}
				s = end_loop;
				break;
			case 's':
				socket_mode = sock_send;
				break;
			case 'r':
				socket_mode = sock_recv;
				break;
			case 'k': /* keep */
				one_eof_exit = NO;
				break;
			case 'i': /* interactive (like `telnet') */
				one_eof_exit = YES;
				break;
			case 'l': /* log session - not work if shell_command */
				if (s[1] != '\0') {
					session_log = &s[1];
				} else {
					if (--argc <= 0)
						usage();
					session_log = *++argv;
				}
				s = end_loop;
				break;
			case 'n': /* nowait && accept number of clients */
				accept_one_client = NO;
				break;
			case 'o':
				if (s[1] != '\0') {
					record_sockopt(&s[1]);
				} else {
					if (--argc <= 0)
						usage();
					record_sockopt(*++argv);
				}
				s = end_loop;
				break;
			case 'p':
				if (s[1] != '\0') {
					s = &s[1];
				} else {
					if (--argc <= 0)
						usage();
					s = *++argv;
				}
				if ((port_output = fopen(s, "w")) == NULL)
					fatal(s);
				s = end_loop;
				break;
			case 'c':
				if (s[1] != '\0') {
					connect_to = &s[1];
				} else {
					if (--argc <= 0)
						usage();
					connect_to = *++argv;
				}
				s = end_loop;
				break;
			case 'e':
			case 'f':
				shell_command = argv - 1;
				shell_command[0] = "/bin/sh";
				shell_command[1] = *s == 'f' ? "-f" : "-c";
				argv += argc - 1;
				argc = 1; /* i.e. argc -= argc - 1; */
				break;
			default:
				usage();
			}
		}
	}
	switch (argc) {
	case 0:
		if (bind_to == NULL && connect_to == NULL)
			usage();
		break;
	case 1:
		connect_to = argv[0];
		break;
	default:
		usage();
	}
	if (connect_to != NULL) {
		peer_addr_size =
			make_socket_address(&peer_addr,connect_to,socket_type);
	}
	if (bind_to == NULL) {
		fd = socket(peer_addr.address.sa_family, socket_type, 0);
		if (fd < 0)
			fatal("socket");
		apply_sockopts(fd);
	} else {
		Bool print_port;

		self_addr_size =
			make_socket_address(&self_addr, bind_to, socket_type);
		print_port =
			self_addr.in.sin_family == AF_INET &&
			self_addr.in.sin_port == 0;
		fd = socket(self_addr.address.sa_family, socket_type, 0);
		if (fd < 0)
			fatal("socket");
		switch (self_addr.address.sa_family) {
		case AF_UNIX:
			unlink(self_addr.un.sun_path);
			break;
		case AF_INET:
			break;
		}
		apply_sockopts(fd);
		if (bind(fd, &self_addr.address, (int)self_addr_size) < 0)
			fatal("bind");
		debug(print_sockname(fd));
		if (print_port)
			print_sockport(port_output, fd);
		if (port_output != stderr)
			fclose(port_output);
	}
	if (connect_to == NULL && socket_type_is_stream(socket_type)) {
		int client;

#ifdef CAN_IGNORE_ZOMBIE
		SIGNAL(SIGCLD, SIG_IGN);
#else
		reliable_signal(SIGCHLD, child_exit_handler);
#endif
		if (listen(fd, accept_one_client ? 1 : LISTEN_BACKLOG) < 0)
			fatal("listen");
		for (;;) {
			union generic_sockaddr client_addr;
			socklen_t client_addr_size = sizeof(client_addr);

			client = accept(fd, &client_addr.address,
					&client_addr_size);
			if (client < 0) {
				if (errno == EINTR)
					continue;
				fatal("accept");
			}
			if (accept_one_client)
				break;
			switch (fork()) {
			case -1:
				fatal("fork");
			case 0:
				close(fd);
				debug(print_peername(client));
				doit(shell_command, client, socket_mode,
				     one_eof_exit);
			default:
				close(client);
				break;
			}
		}
		close(fd);
		debug(print_peername(client));
		doit(shell_command, client, socket_mode, one_eof_exit);
	} else if (connect_to == NULL) {
		doit(shell_command, fd, socket_mode, one_eof_exit);
	} else {
		if (connect(fd, &peer_addr.address, (int)peer_addr_size) < 0)
			fatal("connect");
		debug(print_peername(fd));
		doit(shell_command, fd, socket_mode, one_eof_exit);
	}
	/*NOTREACHED*/
	return EXIT_SYSERR;
}
