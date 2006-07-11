/*
 * $Id$
 */

#include <assert.h>
#include <sys/types.h> /* fd_set */
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <gfarm/gfarm.h>
#ifdef HAVE_POLL
#include <poll.h>
#endif
#include <unistd.h>
#include "gfs_client.h"

/*
 * concurrent processing of
 *	gfs_client_get_load_request()/gfs_client_get_load_result()
 */

#define MILLISEC_BY_MICROSEC	1000
#define SECOND_BY_MICROSEC	1000000

static int
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

static void
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

static void
timeval_sub(struct timeval *t1, struct timeval *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_usec -= t2->tv_usec;
	if (t1->tv_usec < 0) {
		--t1->tv_sec;
		t1->tv_usec += SECOND_BY_MICROSEC;
	}
}

struct gfs_client_udp_requests {
	char *requests_save_error;
	int nrequests, requests_free;

	struct {
		int sock;
		void *closure;
		void (*callback)(void *, struct sockaddr *,
		    struct gfs_client_load *, char *);
		struct sockaddr addr;
		int try;
		struct timeval timeout;
	} *requests ;

#ifdef HAVE_POLL
	struct pollfd *requests_poll_fds; /* only used on HAVE_POLL case */
#endif
};

char *
gfarm_client_init_load_requests(int max_requests,
	struct gfs_client_udp_requests **udp_requestsp)
{
	struct gfs_client_udp_requests *p;
	int i;

	GFARM_MALLOC(p);
	if (p == NULL)
		return (GFARM_ERR_NO_MEMORY);
	GFARM_MALLOC_ARRAY(p->requests, max_requests);
	if (p->requests == NULL) {
		free(p);
		return (GFARM_ERR_NO_MEMORY);
        }
#ifdef HAVE_POLL
	GFARM_MALLOC_ARRAY(p->requests_poll_fds, max_requests);
	if (p->requests_poll_fds == NULL) {
		free(p->requests);
		free(p);
		return (GFARM_ERR_NO_MEMORY);
	}
#endif
	for (i = 0; i < max_requests; i++)
		p->requests[i].sock = -1;
	p->nrequests = p->requests_free = max_requests;
	p->requests_save_error = NULL;
	*udp_requestsp = p;
	return (NULL);
}

static void
request_callback(int i, struct gfs_client_load *result, char *e,
	struct gfs_client_udp_requests *p)
{
	if (p->requests_save_error == NULL)
		p->requests_save_error = e;
	(*p->requests[i].callback)(p->requests[i].closure,
		 &p->requests[i].addr, result, e);
	close(p->requests[i].sock);
	p->requests[i].sock = -1;
	p->requests_free++;
}

static char *
request_time_tick(struct gfs_client_udp_requests *p)
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
	for (i = 0; i < p->nrequests; i++) {
		if (p->requests[i].sock == -1)
			continue;
		if (timeval_cmp(&timeout, &p->requests[i].timeout) > 0)
			timeout = p->requests[i].timeout;
#ifdef HAVE_POLL
		p->requests_poll_fds[nfds].fd = p->requests[i].sock;
		p->requests_poll_fds[nfds].events = POLLIN;
		nfds++;
#else
		if (max_fd < p->requests[i].sock) {
			max_fd = p->requests[i].sock;
			if (max_fd >= FD_SETSIZE)
				return ("gfs_client_udp.c:request_time_tick()"
				    ": FD_SETSIZE is too small");
		}
		FD_SET(p->requests[i].sock, &readable);
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
	rv = poll(p->requests_poll_fds, nfds, to);
#else
	rv = select(max_fd + 1, &readable, NULL, NULL, &timeout);
#endif
	if (rv == -1)
		return (gfarm_errno_to_error(errno));

	gettimeofday(&now, NULL);
#ifdef HAVE_POLL
	nfds = 0;
#endif
	for (i = 0; i < p->nrequests; i++) {
		if (p->requests[i].sock == -1)
			continue;
#ifdef HAVE_POLL
		/* revents shows not only POLLIN, but also POLLERR. */
		if (p->requests_poll_fds[nfds++].revents != 0)
#else
		if (FD_ISSET(p->requests[i].sock, &readable))
#endif
		{
			e = gfs_client_get_load_result(
			    p->requests[i].sock, NULL, NULL, &result);
			if (e != NULL) {
				request_callback(i, NULL, e, p);
				continue;
			}
			request_callback(i, &result, NULL, p);
			continue;
		}
		if (timeval_cmp(&p->requests[i].timeout, &now) > 0)
			continue;

		++p->requests[i].try;
		if (p->requests[i].try >= gfs_client_datagram_ntimeouts) {
			request_callback(i, NULL,
			    GFARM_ERR_CONNECTION_TIMED_OUT, p);
			continue;
		}
		timeval_add_microsec(&p->requests[i].timeout,
		    gfs_client_datagram_timeouts[p->requests[i].try] *
		    MILLISEC_BY_MICROSEC);
		e = gfs_client_get_load_request(p->requests[i].sock,
		    NULL, 0);
		if (e != NULL) {
			request_callback(i, NULL, e, p);
			continue;
		}
	}
	return NULL;
}

static void
wait_request_reply(struct gfs_client_udp_requests *p)
{
	int initial = p->requests_free;

	if (p->requests_free >= p->nrequests)
		return;
	while (p->requests_free <= initial)
		request_time_tick(p);
}

char *
gfarm_client_wait_all_load_results(struct gfs_client_udp_requests *p)
{
	char *e;

	while (p->requests_free < p->nrequests)
		wait_request_reply(p);
#ifdef HAVE_POLL	
	free(p->requests_poll_fds);
#endif
	free(p->requests);
	e = p->requests_save_error;
	free(p);
	return (e);
}

static int
free_request(struct gfs_client_udp_requests *p)
{
	int i;

	if (p->requests_free <= 0)
		wait_request_reply(p);
	for (i = 0; i < p->nrequests; i++)
		if (p->requests[i].sock == -1)
			return (i);
	assert(0);
	/*NOTREACHED*/
	return (-1);
}

char *
gfarm_client_add_load_request(struct gfs_client_udp_requests *udp_requests,
	struct sockaddr *peer_addr, void *closure,
	void (*callback)(void *, struct sockaddr *, struct gfs_client_load *,
	    char *))
{
	int i = free_request(udp_requests);
	/* use different socket each time, to identify error code */
	int sock = socket(PF_INET, SOCK_DGRAM, 0);
	char *e;

	if (sock == -1) {
		e = gfarm_errno_to_error(errno);
		(*callback)(closure, peer_addr, NULL, e);
		if (udp_requests->requests_save_error == NULL)
			udp_requests->requests_save_error = e;
		return e;
	}
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */
	/* connect UDP socket, to get error code */
	if (connect(sock, peer_addr, sizeof(*peer_addr)) == -1) {
		e = gfarm_errno_to_error(errno);
		close(sock);
		(*callback)(closure, peer_addr, NULL, e);
		if (udp_requests->requests_save_error == NULL)
			udp_requests->requests_save_error = e;
		return e;
	}
	e = gfs_client_get_load_request(sock, NULL, 0);
	if (e != NULL) {
		close(sock);
		(*callback)(closure, peer_addr, NULL, e);
		if (udp_requests->requests_save_error == NULL)
			udp_requests->requests_save_error = e;
		return e;
	}
	udp_requests->requests[i].sock = sock;
	udp_requests->requests[i].closure = closure;
	udp_requests->requests[i].callback = callback;
	udp_requests->requests[i].addr = *peer_addr;
	udp_requests->requests[i].try = 0;
	gettimeofday(&udp_requests->requests[i].timeout, NULL);
	timeval_add_microsec(&udp_requests->requests[i].timeout,
	    gfs_client_datagram_timeouts[0] * MILLISEC_BY_MICROSEC);
	--udp_requests->requests_free;
	return NULL;
}
