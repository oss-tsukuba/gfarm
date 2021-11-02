/*
 * iobuffer operation: file descriptor read/write
 */

#include <gfarm/gfarm_config.h>

#include <sys/types.h>
#ifdef HAVE_POLL
#include <poll.h>
#else
#include <sys/time.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h> /* for NI_MAXHOST, NI_NUMERICHOST, etc */

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gflog.h>
#include <gfarm/gfs.h> /* for definition of gfarm_off_t */

#include "gfutil.h" /* gfarm_send_no_sigpipe() */
#include "gfnetdb.h"

#include "context.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "config.h"

/*
 * blocking i/o
 */
int
gfarm_iobuffer_blocking_read_timeout_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv;
	int avail, timeout = gfarm_ctxp->network_receive_timeout;
	const char *hostaddr_prefix, *hostaddr;
	char hostbuf[NI_MAXHOST];

	for (;;) {
#ifdef HAVE_POLL
		struct pollfd fds[1];

		fds[0].fd = fd;
		fds[0].events = POLLIN;
		avail = poll(fds, 1, timeout * 1000);
#else
		fd_set readable;
		struct timeval tv;

		FD_ZERO(&readable);
		FD_SET(fd, &readable);
		tv.tv_sec = timeout;
		tv.tv_usec = 0;
		avail = select(fd + 1, &readable, NULL, NULL, &tv);
#endif
		if (avail == 0) {
			gfarm_iobuffer_set_error(b,
			    GFARM_ERR_OPERATION_TIMED_OUT);
			gfarm_peer_name_string(fd, hostbuf, sizeof(hostbuf),
			    NI_NUMERICHOST | NI_NUMERICSERV,
			    &hostaddr_prefix, &hostaddr);
			gflog_error(GFARM_MSG_1003449,
			    "closing network connection due to "
			    "no response within %d seconds "
			    "(network_receive_timeout) from %s%s",
			    timeout, hostaddr_prefix, hostaddr);
			return (-1);
		} else if (avail == -1) {
			if (errno == EINTR)
				continue;
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
			return (-1);
		}

		/*
		 * On Linux, read() below sometimes fails and 'errno'
		 * is set to EAGAIN though we have confirmed the file
		 * descriptor is ready to read by calling poll() or
		 * select().  We should retry poll() or select() in
		 * that case.
		 */
		rv = read(fd, data, length);
		if (rv == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
			return (-1);
		}
		return (rv);
	}
}

int
gfarm_iobuffer_blocking_read_notimeout_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv;

	for (;;) {
		rv = read(fd, data, length);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
#ifdef HAVE_POLL
				struct pollfd fds[1];

				fds[0].fd = fd;
				fds[0].events = POLLIN;
				poll(fds, 1, -1);
#else
				fd_set readable;

				FD_ZERO(&readable);
				FD_SET(fd, &readable);
				select(fd + 1, &readable, NULL, NULL, NULL);
#endif
				continue;
			}
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
		}
		return (rv);
	}
}

int
gfarm_iobuffer_blocking_write_notimeout_socket_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv;

	for (;;) {
		rv = gfarm_send_no_sigpipe(fd, data, length);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
#ifdef HAVE_POLL
				struct pollfd fds[1];

				fds[0].fd = fd;
				fds[0].events = POLLOUT;
				fds[0].revents = 0;
				poll(fds, 1, -1);
#else
				fd_set writable;

				FD_ZERO(&writable);
				FD_SET(fd, &writable);
				select(fd + 1, NULL, &writable, NULL, NULL);
#endif
				continue;
			}
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
		}
		return (rv);
	}
}

int
gfarm_iobuffer_blocking_write_timeout_socket_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv;
	int avail, timeout = gfarm_ctxp->network_send_timeout;
	const char *hostaddr_prefix, *hostaddr;
	char hostbuf[NI_MAXHOST];

	if (timeout == 0) {
		return (gfarm_iobuffer_blocking_write_notimeout_socket_op(
		    b, cookie, fd, data, length));
	}

	for (;;) {
#ifdef HAVE_POLL
		struct pollfd fds[1];

		fds[0].fd = fd;
		fds[0].events = POLLOUT;
		avail = poll(fds, 1, timeout * 1000);
#else
		fd_set writable;
		struct timeval tv;

		FD_ZERO(&writable);
		FD_SET(fd, &writable);
		tv.tv_sec = timeout;
		tv.tv_usec = 0;
		avail = select(fd + 1, NULL, &writable, NULL, &tv);
#endif
		if (avail == 0) {
			gfarm_iobuffer_set_error(b,
			    GFARM_ERR_OPERATION_TIMED_OUT);
			gfarm_peer_name_string(fd, hostbuf, sizeof(hostbuf),
			    NI_NUMERICHOST | NI_NUMERICSERV,
			    &hostaddr_prefix, &hostaddr);
			gflog_error(GFARM_MSG_1005140,
			    "closing network connection due to "
			    "send blocking more than %d seconds "
			    "(network_send_timeout) to %s%s",
			    timeout, hostaddr_prefix, hostaddr);
			return (-1);
		} else if (avail == -1) {
			if (errno == EINTR)
				continue;
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
			return (-1);
		}

		rv = gfarm_send_no_sigpipe(fd, data, length);
		if (rv == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
		}
		return (rv);
	}
}

/*
 * an option for gfarm_iobuffer_set_write_close()
 */
#if 0 /* currently not used */
void
gfarm_iobuffer_write_close_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd)
{
	int rv = close(fd);

	if (rv == -1 && gfarm_iobuffer_get_error(b) == GFARM_ERR_NO_ERROR)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
}
#endif /* currently not used */

/*
 * gfp_xdr operation
 */

gfarm_error_t
gfp_iobuffer_close_fd_op(void *cookie, int fd)
{
	return (close(fd) == -1 ? gfarm_errno_to_error(errno) :
	    GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_iobuffer_shutdown_fd_op(void *cookie, int fd)
{
	return (shutdown(fd, SHUT_RDWR) == -1 ? gfarm_errno_to_error(errno) :
	    GFARM_ERR_NO_ERROR);
}

int
gfp_iobuffer_recv_is_ready_fd_op(void *cookie)
{
	/*
	 * pending read in socket can be detected by epoll(2)/poll(2)/select(2)
	 * thus, this has to do nothing.
	 */
	return (0);
}

struct gfp_iobuffer_ops gfp_xdr_socket_iobuffer_ops = {
	gfp_iobuffer_close_fd_op,
	gfp_iobuffer_shutdown_fd_op,
	gfp_iobuffer_recv_is_ready_fd_op,
	gfarm_iobuffer_blocking_read_timeout_fd_op,
	gfarm_iobuffer_blocking_read_notimeout_fd_op,
	gfarm_iobuffer_blocking_write_timeout_socket_op,
	gfarm_iobuffer_blocking_write_notimeout_socket_op
};

gfarm_error_t
gfp_xdr_new_socket(int fd, struct gfp_xdr **connp)
{
	return (gfp_xdr_new(&gfp_xdr_socket_iobuffer_ops, NULL, fd,
	    GFP_XDR_NEW_RECV|GFP_XDR_NEW_SEND, connp));
}

gfarm_error_t
gfp_xdr_set_socket(struct gfp_xdr *conn, int fd)
{
	gfp_xdr_set(conn, &gfp_xdr_socket_iobuffer_ops, NULL, fd);
	return (GFARM_ERR_NO_ERROR);
}
