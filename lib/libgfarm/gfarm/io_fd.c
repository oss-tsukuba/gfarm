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
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h" /* gfarm_send_no_sigpipe() */

#include "iobuffer.h"
#include "xxx_proto.h"
#include "io_fd.h"

/*
 * nonblocking i/o
 */
int
gfarm_iobuffer_nonblocking_read_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv = read(fd, data, length);

	if (rv == -1)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	return (rv);

}

static int
gfarm_iobuffer_nonblocking_write_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv = write(fd, data, length);

	if (rv == -1)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	return (rv);
}

/*
 * We have to distinguish the write operation for sockets from
 * the operation for file descriptors, because gfarm_send_no_sigpipe()
 * may only work with sockets, since it may use send(2) internally.
 */
int
gfarm_iobuffer_nonblocking_write_socket_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv = gfarm_send_no_sigpipe(fd, data, length);

	if (rv == -1)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	return (rv);
}

void
gfarm_iobuffer_set_nonblocking_read_fd(struct gfarm_iobuffer *b, int fd)
{
	gfarm_iobuffer_set_read(b, gfarm_iobuffer_nonblocking_read_fd_op,
	    NULL, fd);
}

void
gfarm_iobuffer_set_nonblocking_write_fd(struct gfarm_iobuffer *b, int fd)
{
	gfarm_iobuffer_set_write(b, gfarm_iobuffer_nonblocking_write_fd_op,
	    NULL, fd);
}

/*
 * blocking i/o
 */
int
gfarm_iobuffer_blocking_read_fd_op(struct gfarm_iobuffer *b,
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
gfarm_iobuffer_blocking_write_socket_op(struct gfarm_iobuffer *b,
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

/*
 * an option for gfarm_iobuffer_set_write_close()
 */
#if 0 /* currently not used */
void
gfarm_iobuffer_write_close_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd)
{
	int rv = close(fd);

	if (rv == -1 && gfarm_iobuffer_get_error(b) == NULL)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
}
#endif /* currently not used */

/*
 * xxx_connection operation
 */

char *
xxx_iobuffer_close_fd_op(void *cookie, int fd)
{
	return (close(fd) == -1 ? gfarm_errno_to_error(errno) : NULL);
}

char *
xxx_iobuffer_export_credential_fd_op(void *cookie)
{
	return (NULL); /* it's already exported, or no way to export it. */
}

char *
xxx_iobuffer_delete_credential_fd_op(void *cookie)
{
	return (NULL);
}

char *
xxx_iobuffer_env_for_credential_fd_op(void *cookie)
{
	return (NULL);
}

static struct xxx_iobuffer_ops xxx_socket_iobuffer_ops = {
	xxx_iobuffer_close_fd_op,
	xxx_iobuffer_export_credential_fd_op,
	xxx_iobuffer_delete_credential_fd_op,
	xxx_iobuffer_env_for_credential_fd_op,
	gfarm_iobuffer_nonblocking_read_fd_op,
	gfarm_iobuffer_nonblocking_write_socket_op,
	gfarm_iobuffer_blocking_read_fd_op,
	gfarm_iobuffer_blocking_write_socket_op
};

char *
xxx_socket_connection_new(int fd, struct xxx_connection **connp)
{
	return (xxx_connection_new(&xxx_socket_iobuffer_ops, NULL, fd, connp));
}

char *
xxx_connection_set_socket(struct xxx_connection *conn, int fd)
{
	xxx_connection_set(conn, &xxx_socket_iobuffer_ops, NULL, fd);
	return (NULL);
}
