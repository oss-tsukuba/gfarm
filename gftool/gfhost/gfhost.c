/*
 * $Id$
 */

#include <assert.h>
#include <sys/types.h> /* fd_set */
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <errno.h>
#include <gfarm/gfarm.h>
#ifdef HAVE_POLL
#include <poll.h>
#endif
#include "gfs_client.h"

char *program_name = "gfhost";

char *
update_host(char *hostname, int nhostaliases, char **hostaliases,
	char *architecture, int ncpu,
	char *(*update_op)(char *, struct gfarm_host_info *))
{
	struct gfarm_host_info hi;

	hi.hostname = hostname;
	hi.nhostaliases = nhostaliases;
	hi.hostaliases = hostaliases;
	hi.architecture = architecture;
	hi.ncpu = ncpu;
	return ((*update_op)(hostname, &hi));
}

char *
check_hostname(char *hostname)
{
	char *e, *n;

	e = gfarm_host_get_canonical_name(hostname, &n);
	if (e == NULL || e == GFARM_ERR_AMBIGUOUS_RESULT) {
		if (e == NULL)
			free(n);
		return ("the hostname is already registered");
	}
	return (NULL);
}

char *
check_hostaliases(int nhostaliases, char **hostaliases)
{
	int i;

	for (i = 0; i < nhostaliases; i++) {
		if (check_hostname(hostaliases[i]) != NULL)
			return ("the hostalias is already registered");
	}
	return (NULL);
}

char *
add_host(char *hostname, char **hostaliases, char *architecture, int ncpu)
{
	int nhostaliases = gfarm_strarray_length(hostaliases);
	char *e;

	e = check_hostname(hostname);
	if (e != NULL)
		return (e);
	e = check_hostaliases(nhostaliases, hostaliases);
	if (e != NULL)
		return (e);

	return (update_host(hostname, nhostaliases, hostaliases,
	    architecture, ncpu, gfarm_host_info_set));
}

char *
modify_host(char *hostname, char **hostaliases, char *architecture, int ncpu,
	int add_aliases)
{
	char *e;
	struct gfarm_host_info hi;
	int host_info_needs_free = 0;
	gfarm_stringlist aliases;

	if (*hostaliases == NULL || architecture == NULL || ncpu < 1 ||
	    add_aliases) {
		e = gfarm_host_info_get(hostname, &hi);
		if (e != NULL)
			return (e);
		host_info_needs_free = 1;
		if (!add_aliases) {
			/* XXX - do check_hostaliases() here, too. */
			hostaliases = hostaliases;
		} else {
			e = check_hostaliases(
			    gfarm_strarray_length(hostaliases), hostaliases);
			if (e != NULL)
				goto free_host_info;

			e = gfarm_stringlist_init(&aliases);
			if (e != NULL)
				goto free_host_info;
			if (hi.hostaliases != NULL) {
				e = gfarm_stringlist_cat(&aliases,
				    hi.hostaliases);
				if (e != NULL)
					goto free_aliases;
			}
			if (hostaliases != NULL) {
				e = gfarm_stringlist_cat(&aliases,
				    hostaliases);
				if (e != NULL)
					goto free_aliases;
			}
			e = gfarm_stringlist_add(&aliases, NULL);
			if (e != NULL)
				goto free_aliases;
			hostaliases = GFARM_STRINGLIST_STRARRAY(aliases);
		}
		if (architecture == NULL)
			architecture = hi.architecture;
		if (ncpu < 1)
			ncpu = hi.ncpu;
	}
	e = update_host(hostname,
	    gfarm_strarray_length(hostaliases), hostaliases,
	    architecture, ncpu,
	    gfarm_host_info_replace);
	if (e == NULL && !add_aliases && *hostaliases == NULL)
		e = gfarm_host_info_remove_hostaliases(hostname);
 free_aliases:
	if (add_aliases)
		gfarm_stringlist_free(&aliases);
 free_host_info:
	if (host_info_needs_free)
		gfarm_host_info_free(&hi);
	return (e);
}

char *
validate_architecture(char *architecture)
{
	unsigned char c, *s = (unsigned char *)architecture;

	while ((c = *s++) != '\0') {
		if (!isalnum(c) && c != '-' && c != '_' && c != '.')
			return (s - 1);
	}
	return (NULL);
}

char *
validate_hostname(char *hostname)
{
	unsigned char c, *s = (unsigned char *)hostname;

	while ((c = *s++) != '\0') {
		if (!isalnum(c) && c != '-' && c != '.')
			return (s - 1);
	}
	return (NULL);
}

char *
invalid_input(int lineno)
{
	fprintf(stderr, "line %d: invalid input format\n", lineno);
	fprintf(stderr, "%s: input must be "
	    "\"<architecture> <ncpu> <hostname> <hostalias>...\" format\n",
	    program_name);
	return (GFARM_ERR_INVALID_ARGUMENT);
}

#define LINE_BUFFER_SIZE 16384
#define MAX_HOSTALIASES 256

char *
add_line(char *line, int lineno)
{
	int len, ncpu, nhostaliases;
	char *e, *hostname, *architecture;
	char *hostaliases[MAX_HOSTALIASES + 1];
	static char space[] = " \t";

	/* parse architecture */
	line += strspn(line, space); /* skip space */
	len = strcspn(line, space);
	if (len == 0 || line[len] == '\0')
		return (invalid_input(lineno));
	line[len] = '\0';
	architecture = line;
	line += len + 1;
	e = validate_architecture(architecture);
	if (e != NULL) {
		fprintf(stderr,
		    "line %d: invalid character '%c' in architecture \"%s\"\n",
		    lineno, *e, architecture);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	/* parse ncpu */
	line += strspn(line, space); /* skip space */
	len = strcspn(line, space);
	if (len == 0 || line[len] == '\0')
		return (invalid_input(lineno));
	line[len] = '\0';
	errno = 0;
	ncpu = strtol(line, &e, 0);
	if (e == line) {
		return (invalid_input(lineno));
	} else if (*e != '\0') {
		fprintf(stderr, "line %d: garbage \"%s\" in ncpu \"%s\"\n",
		    lineno, e, line);
		return (GFARM_ERR_INVALID_ARGUMENT);
	} else if (errno != 0 && (ncpu == LONG_MIN || ncpu == LONG_MAX)) {
		fprintf(stderr, "line %d: %s on \"%s\"\n",
		    lineno, strerror(errno), line);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	line += len + 1;

	/* parse hostname */
	line += strspn(line, space); /* skip space */
	len = strcspn(line, space);
	if (len == 0)
		return (invalid_input(lineno));
	hostname = line;
	if (line[len] == '\0') {
		line += len;
	} else {
		line[len] = '\0';
		line += len + 1;
	}
	e = validate_hostname(hostname);
	if (e != NULL) {
		fprintf(stderr,
		    "line %d: invalid character '%c' in hostname \"%s\"\n",
		    lineno, *e, hostname);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	/* parse hostaliases */
	for (nhostaliases = 0;; nhostaliases++) {
		line += strspn(line, space); /* skip space */
		if (*line == '\0')
			break;
		len = strcspn(line, space);
		/* assert(len > 0); */
		if (nhostaliases >= MAX_HOSTALIASES) {
			fprintf(stderr, "line %d: "
			    "number of hostaliases exceeds %d\n",
			    lineno, nhostaliases);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		hostaliases[nhostaliases] = line;
		if (line[len] == '\0') {
			line += len;
		} else {
			line[len] = '\0';
			line += len + 1;
		}
		e = validate_hostname(hostaliases[nhostaliases]);
		if (e != NULL) {
			fprintf(stderr, "line %d: "
			    "invalid character '%c' in hostalias \"%s\"\n",
			    lineno, *e, hostaliases[nhostaliases]);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}
	hostaliases[nhostaliases] = NULL;

	e = add_host(hostname, hostaliases, architecture, ncpu);
	if (e != NULL)
		fprintf(stderr, "line %d: %s\n", lineno, e);
	return (e);
}

char *
register_db(void)
{
	char *e, *e_save = NULL;
	int len, lineno;
	char line[LINE_BUFFER_SIZE];

	if (fgets(line, sizeof line, stdin) == NULL)
		return (NULL);
	len = strlen(line);
	for (lineno = 1;; lineno++) {
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
		} else {
			fprintf(stderr, "line %d: too long line\n", lineno);
			if (e_save == NULL)
				e_save = GFARM_ERR_INVALID_ARGUMENT;
			do {
				if (fgets(line, sizeof line, stdin) == NULL)
					return (e_save);
				len = strlen(line);
			} while (len == 0 || line[len - 1] != '\n');
			continue;
		}
		e = add_line(line, lineno);
		if (e_save == NULL)
			e_save = e;
		if (fgets(line, sizeof line, stdin) == NULL)
			break;
		len = strlen(line);
	}
	return (e_save);
}

/*
 * concurrent processing of
 *	gfs_client_get_load_request()/gfs_client_get_load_result()
 */

#define MILLISEC_BY_MICROSEC	1000
#define SECOND_BY_MICROSEC	1000000

int
timeval_cmp(struct timeval *t1, struct timeval *t2)
{
	if (t1->tv_sec > t2->tv_sec)
		return (1);
	if (t1->tv_sec < t2->tv_sec)
		return (-1);
	if (t1->tv_usec > t2->tv_usec)
		return (1);
	if (t1->tv_usec < t2->tv_usec)
		return (-1);
	return (0);
}

void
timeval_add_microsec(struct timeval *t, long microsec)
{
	int n;

	t->tv_usec += microsec;
	if (t->tv_usec >= SECOND_BY_MICROSEC) {
		n = t->tv_usec / SECOND_BY_MICROSEC;
		t->tv_usec -= n * SECOND_BY_MICROSEC;
		t->tv_sec += n;
	}
}

void
timeval_sub(struct timeval *t1, struct timeval *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_usec -= t2->tv_usec;
	if (t1->tv_usec < 0) {
		--t1->tv_sec;
		t1->tv_usec += SECOND_BY_MICROSEC;
	}
}

char *requests_save_error;
int nrequests, requests_free;

struct gfs_datagram_request {
	int sock;
	void *closure;
	void (*callback)(void *, struct sockaddr *,
	    struct gfs_client_load *, char *);
	struct sockaddr addr;
	int try;
	struct timeval timeout;
} *requests;

#ifdef HAVE_POLL
struct pollfd *requests_poll_fds;
#endif

char *
init_requests(int max_requests)
{
	int i;

	requests = malloc(sizeof(*requests) * max_requests);
	if (requests == NULL)
		return (GFARM_ERR_NO_MEMORY);
	requests_poll_fds = malloc(sizeof(*requests_poll_fds) * max_requests);
	if (requests_poll_fds == NULL) {
		free(requests);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; i < max_requests; i++)
		requests[i].sock = -1;
	nrequests = requests_free = max_requests;
	requests_save_error = NULL;
	return (NULL);
}

void
request_callback(int i, struct gfs_client_load *result, char *e)
{
	if (requests_save_error == NULL)
		requests_save_error = e;
	(*requests[i].callback)(requests[i].closure, &requests[i].addr,
	    result, e);
	close(requests[i].sock);
	requests[i].sock = -1;
	requests_free++;
}

void
request_time_tick()
{
	char *e;
	int i, rv;
	struct timeval now, timeout;
	struct gfs_client_load result;
#ifdef HAVE_POLL
	int to, nfds = 0;
#else
	int max_fd = -1;
	fd_set readable;

	FD_ZERO(&readable);
#endif

	timeout.tv_sec = LONG_MAX;
	timeout.tv_usec = SECOND_BY_MICROSEC - 1;
	for (i = 0; i < nrequests; i++) {
		if (requests[i].sock == -1)
			continue;
		if (timeval_cmp(&timeout, &requests[i].timeout) > 0)
			timeout = requests[i].timeout;
#ifdef HAVE_POLL
		requests_poll_fds[nfds].fd = requests[i].sock;
		requests_poll_fds[nfds].events = POLLIN;
		nfds++;
#else
		if (max_fd < requests[i].sock) {
			max_fd = requests[i].sock;
			if (max_fd >= FD_SETSIZE) {
				fprintf(stderr, "%s: "
				    "FD_SETSIZE (%d) is too small, "
				    "increase it to more than %d\n",
				    program_name, FD_SETSIZE, max_fd);
				exit(1);
			}
		}
		FD_SET(requests[i].sock, &readable);
#endif
	}

	gettimeofday(&now, NULL);
	if (timeval_cmp(&timeout, &now) <= 0) {
#ifdef HAVE_POLL
		to = 0;
#else
		timeout.tv_sec = timeout.tv_usec = 0;
#endif
	} else {
		timeval_sub(&timeout, &now);
#ifdef HAVE_POLL
		to = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;
#endif
	}
#ifdef HAVE_POLL
	rv = poll(requests_poll_fds, nfds, to);
#else
	rv = select(max_fd + 1, &readable, NULL, NULL, &timeout);
#endif
	if (rv == -1) {
		if (errno != EINTR && errno != EAGAIN) {
#ifdef HAVE_POLL
			perror("poll");
#else
			perror("select");
#endif
		}
		return;
	}

	gettimeofday(&now, NULL);
#ifdef HAVE_POLL
	nfds = 0;
#endif
	for (i = 0; i < nrequests; i++) {
		if (requests[i].sock == -1)
			continue;
#ifdef HAVE_POLL
		/* revents shows not only POLLIN, but also POLLERR. */
		if (requests_poll_fds[nfds++].revents != 0)
#else
		if (FD_ISSET(requests[i].sock, &readable))
#endif
		{
			e = gfs_client_get_load_result(
			    requests[i].sock, NULL, NULL, &result);
			if (e != NULL) {
				request_callback(i, NULL, e);
				continue;
			}
			request_callback(i, &result, NULL);
			continue;
		}
		if (timeval_cmp(&requests[i].timeout, &now) > 0)
			continue;

		++requests[i].try;
		if (requests[i].try >= gfs_client_datagram_ntimeouts) {
			request_callback(i, NULL,
			    GFARM_ERR_CONNECTION_TIMED_OUT);
			continue;
		}
		timeval_add_microsec(&requests[i].timeout,
		    gfs_client_datagram_timeouts[requests[i].try] *
		    MILLISEC_BY_MICROSEC);
		e = gfs_client_get_load_request(requests[i].sock,
		    NULL, 0);
		if (e != NULL) {
			request_callback(i, NULL, e);
			continue;
		}
	}
}

void
wait_request_reply(void)
{
	int initial = requests_free;

	if (requests_free >= nrequests)
		return;
	while (requests_free <= initial)
		request_time_tick();
}

char *
wait_all_request_reply(void)
{
	while (requests_free < nrequests)
		wait_request_reply();
	return (requests_save_error);
}

int
free_request(void)
{
	int i;

	if (requests_free <= 0)
		wait_request_reply();
	for (i = 0; i < nrequests; i++)
		if (requests[i].sock == -1)
			return (i);
	assert(0);
	/*NOTREACHED*/
	return (-1);
}

void
add_request(struct sockaddr *peer_addr, void *closure,
	void (*callback)(void *, struct sockaddr *, struct gfs_client_load *,
	    char *))
{
	int i = free_request();
	/* use different socket each time, to identify error code */
	int sock = socket(PF_INET, SOCK_DGRAM, 0);
	char *e;

	if (sock == -1) {
		e = gfarm_errno_to_error(errno);
		(*callback)(closure, peer_addr, NULL, e);
		if (requests_save_error == NULL)
			requests_save_error = e;
		return;
	}
	/* connect UDP socket, to get error code */
	if (connect(sock, peer_addr, sizeof(*peer_addr)) == -1) {
		e = gfarm_errno_to_error(errno);
		close(sock);
		(*callback)(closure, peer_addr, NULL, e);
		if (requests_save_error == NULL)
			requests_save_error = e;
		return;
	}
	e = gfs_client_get_load_request(sock, NULL, 0);
	if (e != NULL) {
		close(sock);
		(*callback)(closure, peer_addr, NULL, e);
		if (requests_save_error == NULL)
			requests_save_error = e;
		return;
	}
	requests[i].sock = sock;
	requests[i].closure = closure;
	requests[i].callback = callback;
	requests[i].addr = *peer_addr;
	requests[i].try = 0;
	gettimeofday(&requests[i].timeout, NULL);
	timeval_add_microsec(&requests[i].timeout,
	    gfs_client_datagram_timeouts[0] * MILLISEC_BY_MICROSEC);
	--requests_free;
}

/*
 * handle option "-i" (ignore "address_use" directive in gfarm.conf(5))
 */

char *
resolv_addr_with_address_use(char *hostname,
	struct sockaddr *addr, char **if_hostnamep)
{
	return (gfarm_host_address_get(hostname, gfarm_spool_server_port,
	    addr, if_hostnamep));
}

char *
resolv_addr_without_address_use(char *hostname,
	struct sockaddr *addr, char **if_hostnamep)
{
	/* sizeof(struct sockaddr_in) == sizeof(struct sockaddr) */
	struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
	struct hostent *hp;

	hp = gethostbyname(hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (GFARM_ERR_UNKNOWN_HOST);
	memset(addr_in, 0, sizeof(*addr_in));
	memcpy(&addr_in->sin_addr, hp->h_addr,
	    sizeof(addr_in->sin_addr));
	addr_in->sin_family = hp->h_addrtype;
	addr_in->sin_port = htons(gfarm_spool_server_port);
	*if_hostnamep = strdup(hostname);
	if (*if_hostnamep == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (NULL);
}

char *(*opt_resolv_addr)(char *, struct sockaddr *, char **) =
	resolv_addr_with_address_use;

/*
 * listing options.
 */

int opt_verbose = 0;

void
callback_loadavg(void *closure, struct sockaddr *addr,
	struct gfs_client_load *result, char *error)
{
	char *if_hostname = closure;
	struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;

	if (error != NULL) {
		fprintf(stderr, "%s: %s\n", if_hostname, error);
	} else {
		printf("%4.2f/%4.2f/%4.2f %s(%s)\n",
		    result->loadavg_1min,
		    result->loadavg_5min,
		    result->loadavg_15min,
		    if_hostname, inet_ntoa(addr_in->sin_addr));
	}
	free(if_hostname);
}

char *
print_loadavg(struct gfarm_host_info *info)
{
	char *e;
	struct sockaddr addr;
	char *hostname = info->hostname;
	char *if_hostname;

	e = (*opt_resolv_addr)(hostname, &addr, &if_hostname);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", hostname, e);
		return (e);
	}
	add_request(&addr, if_hostname, callback_loadavg);
	return (NULL);
}

char *
list_loadavg(int nhosts, char **hosts)
{
	char *e, *e_save = NULL;
	int i;
	struct gfarm_host_info host;

	for (i = 0; i < nhosts; i++) {
		host.hostname = hosts[i];
		e = print_loadavg(&host);
		if (e_save == NULL)
			e_save = e;
	}
	return (e_save);
}

struct closure_for_host_info_and_loadavg {
	struct gfarm_host_info host_info;
	char *if_hostname;
};

void
callback_host_info_and_loadavg(void *closure, struct sockaddr *addr,
	struct gfs_client_load *result, char *error)
{
	struct closure_for_host_info_and_loadavg *c = closure;
	/* sizeof(struct sockaddr_in) == sizeof(struct sockaddr) */
	struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
	struct gfarm_host_info *info = &c->host_info;
	int i, print_ifaddr = c->if_hostname != NULL;

	if (error != NULL) {
		if (opt_verbose)
			fprintf(stderr, "%s: %s\n", info->hostname, error);
		if (error == GFARM_ERR_CONNECTION_REFUSED) /* machine is up */
			printf("-.--/-.--/-.-- ");
		else
			printf("x.xx/x.xx/x.xx ");
	} else {
		printf("%4.2f/%4.2f/%4.2f ",
		    result->loadavg_1min,
		    result->loadavg_5min,
		    result->loadavg_15min);
	}
	printf("%s %d %s", info->architecture, info->ncpu, info->hostname);
	if (print_ifaddr && strcasecmp(info->hostname, c->if_hostname) == 0) {
		print_ifaddr = 0;
		printf("(%s)", inet_ntoa(addr_in->sin_addr));
	}
	for (i = 0; i < info->nhostaliases; i++) {
		printf(" %s", info->hostaliases[i]);
		if (print_ifaddr &&
		    strcasecmp(info->hostaliases[i], c->if_hostname) == 0) {
			print_ifaddr = 0;
			printf("(%s)", inet_ntoa(addr_in->sin_addr));
		}
	}
	if (print_ifaddr) {
		printf(" [%s(%s)]", c->if_hostname,
		    inet_ntoa(addr_in->sin_addr));
	}
	putchar('\n');
	gfarm_host_info_free(&c->host_info);
	if (c->if_hostname != NULL)
		free(c->if_hostname);
	free(c);
}

char *
print_host_info_and_loadavg(struct gfarm_host_info *host_info)
{
	char *e;
	struct sockaddr addr;
	struct closure_for_host_info_and_loadavg *closure;

	closure = malloc(sizeof(*closure));
	if (closure == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (e);
	}
	/* dup `*host_info' -> `closure->host_info' */
	closure->host_info.hostname = strdup(host_info->hostname);
	closure->host_info.nhostaliases = host_info->nhostaliases;
	if (host_info->nhostaliases == 0) {
		closure->host_info.hostaliases = NULL;
	} else {
		closure->host_info.hostaliases =
		    gfarm_strarray_dup(host_info->hostaliases);
		if (closure->host_info.hostaliases == NULL)
			closure->host_info.nhostaliases = 0;
	}
	closure->host_info.architecture = strdup(host_info->architecture);
	closure->host_info.ncpu = host_info->ncpu;
	if (closure->host_info.hostname == NULL ||
	    closure->host_info.architecture == NULL) {
		gfarm_host_info_free(&closure->host_info);
		e = GFARM_ERR_NO_MEMORY;
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (e);
	}

	e = (*opt_resolv_addr)(host_info->hostname,
	    &addr, &closure->if_hostname);
	if (e != NULL) {
		closure->if_hostname = NULL;
		callback_host_info_and_loadavg(closure, NULL, NULL, e);
		return (e);
	}

	add_request(&addr, closure, callback_host_info_and_loadavg);
	return (NULL);
}

char *
print_host_info(struct gfarm_host_info *info)
{
	int i;

	printf("%s %d %s", info->architecture, info->ncpu, info->hostname);
	for (i = 0; i < info->nhostaliases; i++)
		printf(" %s", info->hostaliases[i]);
	putchar('\n');
	return (NULL);
}

char *
list_all(char *(*print_op)(struct gfarm_host_info *))
{
	char *e, *e_save = NULL;
	int i, nhosts;
	struct gfarm_host_info *hosts;

	e = gfarm_host_info_get_all(&nhosts, &hosts);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		return (e);
	}
	for (i = 0; i < nhosts; i++) {
		e = (*print_op)(&hosts[i]);
		if (e_save == NULL)
			e_save = e;
	}
	gfarm_host_info_free_all(nhosts, hosts);
	return (e_save);
}

char *
list(int nhosts, char **hosts, char *(*print_op)(struct gfarm_host_info *))
{
	char *e, *e_save = NULL;
	int i;
	struct gfarm_host_info hi;

	for (i = 0; i < nhosts; i++) {
		e = gfarm_host_info_get_by_if_hostname(hosts[i], &hi);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", hosts[i], e);
			if (e_save == NULL)
				e_save = e;
		} else {
			e = (*print_op)(&hi);
			if (e_save == NULL)
				e_save = e;
			gfarm_host_info_free(&hi);
		}
	}
	return (e_save);
}

/* Well, this is really ad hoc manner to sort output... */
int
setup_sort(char *sort_arg)
{
	int pid, fds[2];

	if (pipe(fds) == -1)
		return (-1);
	pid = fork();
	switch (pid) {
	case -1: /* error */
		close(fds[0]);
		close(fds[1]);
		return (-1);
	case 0: /* child */
		close(fds[1]);
		dup2(fds[0], 0);
		close(fds[0]);
		execlp("sort", "sort", sort_arg, NULL);
		exit(1);
	default: /* parent */
		close(fds[0]);
		dup2(fds[1], 1);
		close(fds[1]);
		return (pid);
	}
}

char *
wait_sort(int pid)
{
	char *e;
	int status;

	fclose(stdout);
	if (waitpid(pid, &status, 0) == -1)
		e = gfarm_errno_to_error(errno);
	else if (WIFSIGNALED(status))
		e = "killed by signal";
	else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
		e = "failed to invoke";
	else
		e = NULL;
	if (e != NULL)
		fprintf(stderr, "%s: sort command: %s\n", program_name, e);
	return (e);
}

void
usage(void)
{
	fprintf(stderr, "Usage:" 
	    "\t%s %s\n" "\t%s %s\n" "\t%s %s\n" "\t%s %s\n" "\t%s %s\n",
	    program_name, "[-lL] [-j <concurrency>] [-ipv]",
	    program_name,
	    "-c  -a <architecture>  [-n <ncpu>] <hostname> [<hostalias>...]",
	    program_name,
	    "-m [-a <architecture>] [-n <ncpu>] [-A] <hostname> [<hostalias>...]",
	    program_name, "-d <hostname>...",
	    program_name, "-r");
	exit(EXIT_FAILURE);
}

#define OP_DEFAULT	'\0'
#define OP_LIST_LOADAVG	'L'
#define OP_LIST_DB	'l'
#define OP_REGISTER_DB	'r'
#define OP_CREATE_ENTRY	'c'
#define OP_DELETE_ENTRY	'd'
#define OP_MODIFY_ENTRY	'm'

void
inconsistent_option(int c1, int c2)
{
	fprintf(stderr, "%s: inconsistent option -%c and -%c\n",
	    program_name, c1, c2);
	usage();
}

void
invalid_option(int c)
{
	fprintf(stderr, "%s: option -%c is only available with -%c or -%c\n",
	    program_name, c, OP_CREATE_ENTRY, OP_MODIFY_ENTRY);
	usage();
}

long
parse_opt_long(char *option, int option_char, char *argument_name)
{
	long value;
	char *s;

	errno = 0;
	value = strtol(option, &s, 0);
	if (s == option) {
		fprintf(stderr, "%s: missing %s after -%c\n",
		    program_name, argument_name, option_char);
		usage();
	} else if (*s != '\0') {
		fprintf(stderr, "%s: garbage in -%c %s\n",
		    program_name, option_char, option);
		usage();
	} else if (errno != 0 && (value == LONG_MIN || value == LONG_MAX)) {
		fprintf(stderr, "%s: %s with -%c %s\n",
		    program_name, strerror(errno), option_char, option);
		usage();
	}
	return (value);
}


#define DEFAULT_CONCURRENCY 10

int
main(int argc, char **argv)
{
	char *e, *e_save = NULL;
	char opt_operation = OP_DEFAULT;
	int opt_concurrency = DEFAULT_CONCURRENCY;
	int opt_alter_aliases = 0;
	char *opt_architecture = NULL;
	long opt_ncpu = 0;
	int opt_plain_order = 0; /* i.e. do not sort */
	int i, c, sort_pid;

#ifdef __GNUC__ /* shut up "warning: `...' might be used uninitialized" */
	sort_pid = 0;
#endif
	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	while ((c = getopt(argc, argv, "ALa:cdij:lmn:prv")) != -1) {
		switch (c) {
		case 'A':
			opt_alter_aliases = 1;
			break;
		case 'L':
		case 'c':
		case 'd':
		case 'l':
		case 'm':
		case 'r':
			if (opt_operation != OP_DEFAULT && opt_operation != c)
				inconsistent_option(opt_operation, c);
			opt_operation = c;
			break;
		case 'a':
			opt_architecture = optarg;
			e = validate_architecture(opt_architecture);
			if (e != NULL) {
				fprintf(stderr, "%s: "
				    "invalid character '%c' in \"-a %s\"\n",
				    program_name, *e, opt_architecture);
				exit(1);
			}
			break;
		case 'i':
			opt_resolv_addr = resolv_addr_without_address_use;
			break;
		case 'j':
			opt_concurrency = parse_opt_long(optarg,
			    c, "<concurrency>");
			if (opt_concurrency <= 0) {
				fprintf(stderr, "%s: invalid value: -%c %d\n",
				    program_name, c, opt_concurrency);
				usage();
			}
			break;
		case 'n':
			opt_ncpu = parse_opt_long(optarg, c, "<ncpu>");
			break;
		case 'p':
			opt_plain_order = 1;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case '?':
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (opt_operation == OP_CREATE_ENTRY) {
		if (opt_architecture == NULL) {
			fprintf(stderr, "%s: missing -a <architecture>\n",
			    program_name);
			usage();
		}
		if (opt_ncpu == 0) {
			opt_ncpu = 1;
		}
		/* opt_alter_aliases is meaningless, but allowed */
	} else if (opt_operation != OP_MODIFY_ENTRY) {
		if (opt_architecture != NULL)
			invalid_option('a');
		if (opt_ncpu != 0)
			invalid_option('n');
		if (opt_alter_aliases)
			invalid_option('A');
	}

	for (i = 0; i < argc; i++) {
		e = validate_hostname(argv[i]);
		if (e != NULL) {
			fprintf(stderr, "%s: "
			    "invalid character '%c' in hostname \"%s\"\n",
			    program_name, *e, argv[i]);
			exit(1);
		}
	}

	switch (opt_operation) {
	case OP_CREATE_ENTRY:
		if (argc > 0) {
			e_save = add_host(argv[0], &argv[1],
			    opt_architecture, opt_ncpu);
			if (e_save != NULL)
				fprintf(stderr, "%s: %s\n", argv[0], e_save);
		}
		break;
	case OP_MODIFY_ENTRY:
		if (argc > 0) {
			e_save = modify_host(argv[0], &argv[1],
			    opt_architecture, opt_ncpu, !opt_alter_aliases);
			if (e_save != NULL)
				fprintf(stderr, "%s: %s\n", argv[0], e_save);
		}
		break;
	case OP_DELETE_ENTRY:
		for (i = 0; i < argc; i++) {
			e = gfarm_host_info_remove(argv[i]);
			if (e != NULL) {
				fprintf(stderr, "%s: %s\n", argv[i], e);
				if (e_save == NULL)
					e_save = e;
			}
		}
		break;
	case OP_REGISTER_DB:
		if (argc > 0) {
			fprintf(stderr, "%s: too many argument: %s\n",
			    program_name, argv[0]);
			exit(1);
		}
		e_save = register_db();
		break;
	case OP_LIST_LOADAVG:
		if (!opt_plain_order)
			sort_pid = setup_sort("+1");
		e = init_requests(opt_concurrency);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
		if (argc == 0) {
			e_save = list_all(print_loadavg);
		} else {
			e_save = list_loadavg(argc, argv);
		}
		e = wait_all_request_reply();
		if (e_save == NULL)
			e_save = e;
		if (!opt_plain_order && sort_pid != -1) {
			e = wait_sort(sort_pid);
			if (e_save == NULL)
				e_save = e;
		}
		break;
	case OP_DEFAULT:
		if (!opt_plain_order)
			sort_pid = setup_sort("+3");
		e = init_requests(opt_concurrency);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
		if (argc == 0) {
			e_save = list_all(print_host_info_and_loadavg);
		} else {
			e_save = list(argc, argv, print_host_info_and_loadavg);
		}
		e = wait_all_request_reply();
		if (e_save == NULL)
			e_save = e;
		if (!opt_plain_order && sort_pid != -1) {
			e = wait_sort(sort_pid);
			if (e_save == NULL)
				e_save = e;
		}
		break;
	case OP_LIST_DB:
		if (argc == 0) {
			e_save = list_all(print_host_info);
		} else {
			e_save = list(argc, argv, print_host_info);
		}
		break;
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	exit(e_save == NULL ? 0 : 1);
}
