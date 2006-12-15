#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "xxx_proto.h"
#include "io_fd.h"
#include "param.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "gfs_proto.h"
#include "gfs_client.h"

#ifdef SOMAXCONN
#define LISTEN_BACKLOG	SOMAXCONN
#else
#define LISTEN_BACKLOG	5
#endif

#define PORT_MINIMUM	11000
#define PORT_MAXIMUM	12000

char *program_name = "gfrepbe_server";
char *my_name; /* == gfarm_host_get_self_name() */

long rate_limit;

/*
 * XXX
 * The definition of GFREPBE_SERVICE_TAG and gfrepbe_auth_initialize() in
 * gfrepbe_server.c and gfrepbe_client.c must be factored out.
 */

#define GFREPBE_SERVICE_TAG "gfarm-replication-backend"

char *
gfrepbe_auth_initialize(void)
{
	return (gfarm_auth_server_cred_type_set(GFREPBE_SERVICE_TAG,
	    GFARM_AUTH_CRED_TYPE_SELF));
}

void fatal(void)
{
	exit(EXIT_FAILURE);
}

char *
sequential_transfer(int ifd, file_offset_t size, int file_read_size,
	struct xxx_connection *conn,
	struct gfs_client_rep_rate_info *rinfo, gfarm_int32_t *read_resultp)
{
	char *e;
	int i, len, rv = 0;
	gfarm_int32_t read_result = GFS_ERROR_NOERROR;
	file_offset_t transfered = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];

	if (file_read_size >= sizeof(buffer))
		file_read_size = sizeof(buffer);

	while (transfered < size) {
		if (read_result == GFS_ERROR_NOERROR) {
			rv = read(ifd, buffer, file_read_size);
			if (rv <= 0) {
				read_result = rv == -1 ?
				    gfs_errno_to_proto_error(errno) :
				    GFS_ERROR_EXPIRED; /* file is truncated */
				/*
				 * XXX - we cannot stop here, because
				 * currently there is no way to cancel
				 * on-going transfer.
				 */
				memset(buffer, 0, sizeof(buffer));
				rv = size - transfered < sizeof(buffer) ?
				    size - transfered : sizeof(buffer);
			} else if (rv > size - transfered) { /* file changed */
				read_result = GFS_ERROR_EXPIRED;
				rv = size - transfered;
			}
		} else {
			/* buffer is already zero filled above */
			rv = size - transfered < sizeof(buffer) ?
			    size - transfered : sizeof(buffer);
		}
		for (i = 0; i < rv; i += len) {
			e = xxx_write_direct(conn, buffer + i, rv - i, &len);
			if (e != NULL)
				return (e);
			if (rinfo != NULL)
				gfs_client_rep_rate_control(rinfo, len);
		}
		transfered += rv;
	}
	*read_resultp = read_result;
	return (NULL);
}

char *
parallel_transfer_with_parallel_diskio(char *file, int ifd,
	file_offset_t size,
	int ndivisions, int interleave_factor, int file_read_size,
	struct xxx_connection **conns, int *socks,
	struct gfs_client_rep_transfer_state **transfers,
	struct gfs_client_rep_rate_info **rinfos, 
	gfarm_int32_t *read_resultp)
{
	char *e;
	int i, to_be_read, len, rv, st, *pids;
	gfarm_int32_t read_result = GFS_ERROR_NOERROR;
	char buffer[GFS_PROTO_MAX_IOSIZE];

	if (file_read_size >= sizeof(buffer))
		file_read_size = sizeof(buffer);

	GFARM_MALLOC_ARRAY(pids, ndivisions);
	if (pids == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < ndivisions; i++) {
		switch (pids[i] = fork()) {
		case -1:
			e = gfarm_errno_to_error(errno);
			fprintf(stderr, "%s: replicating %s fork(%d): %s\n",
			    program_name, file, i, e);
			return (e);
		case 0:
			if (i > 0) /* to unshare file offset */
				ifd = open(file, O_RDONLY);
			if (ifd == -1) {
				read_result = gfs_errno_to_proto_error(errno);
				memset(buffer, 0, sizeof(buffer));
			}
			while (!gfs_client_rep_transfer_finished(
			    transfers[i])){
				to_be_read = gfs_client_rep_transfer_length(
				    transfers[i], file_read_size);
				if (read_result != GFS_ERROR_NOERROR) {
					memset(buffer, 0, to_be_read);
				} else {
#ifndef HAVE_PREAD
					rv = lseek(ifd,
					    (off_t)
					    gfs_client_rep_transfer_offset(
					    transfers[i]),
					    SEEK_SET);
					if (rv != -1)
						rv = read(ifd, buffer,
						    to_be_read);
#else
					rv = pread(ifd, buffer, to_be_read, 
					    (off_t)
					    gfs_client_rep_transfer_offset(
					    transfers[i]));
#endif
					/*
					 * XXX - we cannot stop here,
					 * because currently there is
					 * no way to cancel on-going
					 * transfer.
					 */
					if (rv == -1) {
						read_result =
						    gfs_errno_to_proto_error(
						    errno);
						memset(buffer, 0, to_be_read);
					} else if (rv < to_be_read) {
						read_result =
						    GFS_ERROR_EXPIRED;
						memset(buffer + rv, 0,
						    to_be_read - rv);
					}
				}
				for (i = 0; i < to_be_read; i += len) {
					e = xxx_write_direct(conns[i],
					    buffer + i, to_be_read - i, &len);
					if (e != NULL) {
						_exit(GFS_ERROR_PIPE);
					}
					/*
					 * XXX FIXME
					 * Since this process is forked,
					 * rate_control_info isn't maintained
					 * correctly.
					 */
					if (rinfos != NULL)
						gfs_client_rep_rate_control(
						    rinfos[i], len);
				}
				gfs_client_rep_transfer_progress(transfers[i],
				    to_be_read);
			}
			if (ifd != -1)
				close(ifd);
			_exit(read_result);
		default:
			break;
		}
	}
	for (i = 0; i < ndivisions; i++) {
		rv = waitpid(pids[i], &st, 0);
		if (rv == -1)
			return (gfarm_errno_to_error(errno));
		if (WIFSIGNALED(st))
			return ("parallel transfer process dead");
		if (WIFEXITED(st)) {
			st = WEXITSTATUS(st);
			if (st == GFS_ERROR_PIPE)
				return ("parallel transfer connection lost");
			if (read_result == GFS_ERROR_NOERROR)
				read_result = st;
		}
	}
	free(pids);
	*read_resultp = read_result;
	return (NULL);
}

char *
parallel_transfer_with_sequential_diskio(char *file, int ifd,
	file_offset_t size,
	int ndivisions, int interleave_factor, int file_read_size,
	struct xxx_connection **conns, int *socks,
	struct gfs_client_rep_transfer_state **transfers,
	struct gfs_client_rep_rate_info **rinfos,
	gfarm_int32_t *read_resultp)
{
	char *e = NULL;
	int full_stripe_size = ndivisions * interleave_factor;
	int buffer_to_be_read, buffer_read, to_be_read, len, offset;
	int buffer_cycle, max_fd, nfound, i, rv;
	gfarm_int32_t read_result = GFS_ERROR_NOERROR;
	file_offset_t transfered = 0;
	char *buffers[2];
	fd_set writable;

	if (interleave_factor == 0)
		return ("interleave_factor == 0 doesn't make sense "
		    "for parallel_transfer_with_sequential_diskio");

	if (size == 0) /* nothing to send */
		return (NULL);

	GFARM_MALLOC_ARRAY(buffers[0], full_stripe_size);
	GFARM_MALLOC_ARRAY(buffers[1], full_stripe_size);
	if (buffers[0] == NULL || buffers[1] == NULL) {
		if (buffers[0] != NULL)
			free(buffers[0]);
		if (buffers[1] != NULL)
			free(buffers[1]);
		return (GFARM_ERR_NO_MEMORY);
	}

	to_be_read = size < full_stripe_size ? size : full_stripe_size;

	rv = read(ifd, buffers[0], to_be_read);
	/*
	 * XXX - we cannot stop here, because currently there is no
	 * way to cancel on-going transfer.
	 */
	if (rv == -1) {
		read_result = gfs_errno_to_proto_error(errno);
		memset(buffers[0], 0, to_be_read);
	} else if (rv < to_be_read) {
		read_result = GFS_ERROR_EXPIRED;
		memset(buffers[0] + rv, 0, to_be_read - rv);
	}
	buffer_to_be_read =
	    size - to_be_read < full_stripe_size ?
	    size - to_be_read : full_stripe_size;
	buffer_read = 0;
	buffer_cycle = 0; /* current active buffer of double buffering */

	for (transfered = 0; transfered < size; ) {
		FD_ZERO(&writable);
		max_fd = -1;
		for (i = 0; i < ndivisions; i++) {
			if (gfs_client_rep_transfer_stripe_finished(
			    transfers[i]))
				continue;
			if (socks[i] >= FD_SETSIZE) { /* XXX - use poll? */
				fprintf(stderr, "%s: too big fd %d on %s\n",
				    program_name, socks[i], my_name);
				fatal();
			}
			FD_SET(socks[i], &writable);
			if (max_fd < socks[i])
				max_fd = socks[i];
		}

		if (max_fd == -1) { /* full_stripe finished */
			transfered += full_stripe_size;
			for (i = 0; i < ndivisions; i++)
				gfs_client_rep_transfer_stripe_next(
				    transfers[i]);
			if (buffer_read < buffer_to_be_read) {
				to_be_read = buffer_to_be_read - buffer_read;
				if (read_result != GFS_ERROR_NOERROR) {
					memset(buffers[1 - buffer_cycle] +
					    buffer_read, 0, to_be_read);
				} else {
					rv = read(ifd,
					    buffers[1 - buffer_cycle] +
					    buffer_read,
					    to_be_read);
					/*
					 * XXX - we cannot stop here, because
					 * currently there is no way to cancel
					 * on-going transfer.
					 */
					if (rv == -1) {
						read_result =
						    gfs_errno_to_proto_error(
						    errno);
						memset(buffers[1-buffer_cycle]+
						    buffer_read, 0,to_be_read);
					} else if (rv < to_be_read) {
						read_result =
						    GFS_ERROR_EXPIRED;
						memset(buffers[1-buffer_cycle]+
						    buffer_read + rv, 0,
						    to_be_read - rv);
					}
				}
			}
			buffer_to_be_read =
			    size - transfered - buffer_to_be_read <
				full_stripe_size ?
			    size - transfered - buffer_to_be_read :
				full_stripe_size;
			buffer_read = 0;
			buffer_cycle = 1 - buffer_cycle;
			continue;
		}
		do {
			nfound = select(max_fd + 1, NULL, &writable,
			    NULL, NULL);
		} while (nfound == -1 && errno == EINTR);
		if (nfound <= 0) {
			fprintf(stderr,
			    "%s: parallel_transfer select: %s\n",
			    program_name,
			    nfound < 0 ? strerror(errno) : "assertion failed");
			fatal();
		}

		for (i = 0, offset = 0; i < ndivisions;
		    i++, offset += interleave_factor) {
			if (!FD_ISSET(socks[i], &writable))
				continue;
			len = gfs_client_rep_transfer_length(transfers[i],
			    interleave_factor);
			e = xxx_write_direct(conns[i],
			   buffers[buffer_cycle] + offset +
			   gfs_client_rep_transfer_stripe_offset(transfers[i]),
			   len, &len);
			if (e != NULL)
				goto finish;
			gfs_client_rep_transfer_stripe_progress(transfers[i],
			    len);
			if (rinfos != NULL)
				gfs_client_rep_rate_control(rinfos[i], len);
		}
		if (buffer_read < buffer_to_be_read) {
			to_be_read =
			    file_read_size < buffer_to_be_read - buffer_read ?
			    file_read_size : buffer_to_be_read - buffer_read;

			if (read_result != GFS_ERROR_NOERROR) {
				memset(buffers[1 - buffer_cycle] + buffer_read,
				    0, to_be_read);
			} else {
				rv = read(ifd,
				    buffers[1 - buffer_cycle] + buffer_read,
				    to_be_read);
				/*
				 * XXX - we cannot stop here, because
				 * currently there is no way to cancel
				 * on-going transfer.
				 */
				if (rv == -1) {
					read_result =
					    gfs_errno_to_proto_error(errno);
					memset(buffers[1 - buffer_cycle] +
					    buffer_read, 0, to_be_read);
				} else {
					read_result = GFS_ERROR_EXPIRED;
					memset(buffers[1 - buffer_cycle] +
					    buffer_read + rv, 0,
					    to_be_read - rv);
				}
			}
			buffer_read += to_be_read;
		}
	}
	*read_resultp = read_result;
 finish:
	if (ifd != -1)
		close(ifd);
	free(buffers[1]);
	free(buffers[0]);
	return (e);
}

char *
transfer(char *file, int ifd, file_offset_t size,
	int algorithm_version, int ndivisions, int interleave_factor,
	int file_read_size, int send_stripe_sync,
	struct xxx_connection **conns, int *socks,
	struct gfs_client_rep_rate_info **rinfos,
	gfarm_int32_t *read_resultp)
{
	char *e;
	struct gfs_client_rep_transfer_state **transfers;

	ndivisions = gfs_client_rep_limit_division(
	    algorithm_version, ndivisions, size);
	if (ndivisions == -1)
		return ("unknown algorithm version");
	if (ndivisions == 1)
		return (sequential_transfer(ifd, size, file_read_size,
		    conns[0], rinfos != NULL ? rinfos[0] : NULL,
		    read_resultp));

	e = gfs_client_rep_transfer_state_alloc(size,
	    algorithm_version, ndivisions, interleave_factor,
	    &transfers);
	if (e != NULL)
		return (e);
	e = (*(send_stripe_sync ?
       	    parallel_transfer_with_parallel_diskio :
	    parallel_transfer_with_sequential_diskio))(file, ifd,
	    size, ndivisions, interleave_factor, file_read_size, conns, socks,
	    transfers, rinfos, read_resultp);
	gfs_client_rep_transfer_state_free(ndivisions, transfers);
	return (e);
}

int
open_accepting_socket()
{
	char *e;
	struct sockaddr_in self_addr;
	socklen_t self_addr_size;
	int sock, port;
#if 0
	int sockopt;
#endif

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		fprintf(stderr, "%s: accepting socket: %s\n",
		    program_name, strerror(errno));
		fatal();
	}
#if 0
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1) {
		fprintf(stderr, "%s: SO_REUSEADDR: %s\n",
		    program_name, strerror(errno));
	}
#endif
	for (port = PORT_MINIMUM; port <= PORT_MAXIMUM; port++) {
		memset(&self_addr, 0, sizeof(self_addr));
		self_addr.sin_family = AF_INET;
		self_addr.sin_addr.s_addr = INADDR_ANY;
		self_addr.sin_port = htons(port);
		self_addr_size = sizeof(self_addr);
		if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size)
		    == -1) {
			if (errno == EADDRINUSE)
				continue;
			fprintf(stderr, "%s: bind accepting socket: %s\n",
			    program_name, strerror(errno));
			fatal();
		}
		break;
	}
	if (port > PORT_MAXIMUM) {
		fprintf(stderr,
		    "%s: bind: port not available between %d and %d\n",
		    program_name, PORT_MINIMUM, PORT_MAXIMUM);
		fatal();
	}
	e = gfarm_sockopt_apply_listener(sock);
	if (e != NULL) {
		fprintf(stderr, "%s: setsockopt: %s\n", program_name, e);
	}
	if (listen(sock, LISTEN_BACKLOG) < 0) {
		fprintf(stderr, "%s: listen: %s\n",
		    program_name, strerror(errno));
		fatal();
	}
	return (sock);
}

void
session(struct xxx_connection *from_client, struct xxx_connection *to_client,
	int algorithm_version, int ndivisions, int interleave_factor,
	int send_stripe_sync,
	int n, gfarm_stringlist *files, gfarm_stringlist *sections)
{
	char *e, *pathname;
	gfarm_int32_t r;
	int i, listening_sock, *socks, ifd;
	long file_read_size;
	struct xxx_connection **conns;
	struct gfs_client_rep_rate_info **rinfos = NULL;
#ifdef __GNUC__ /* shut up stupid warning by gcc */
	file_offset_t file_size = 0;
#else
	file_offset_t file_size;
#endif
	struct stat st;

	struct sockaddr_in listen_addr, client_addr;
	socklen_t listen_addr_len, client_addr_len;

	if (rate_limit != 0) {
		GFARM_MALLOC_ARRAY(rinfos, ndivisions);
		if (rinfos == NULL) {
			fprintf(stderr,
			    "%s: no memory for %d rate limits "
			    "on gfrebe_server %s\n",
			    program_name, ndivisions, my_name);
			fatal();
		}
		for (i = 0; i < ndivisions; i++) {
			rinfos[i] =
			    gfs_client_rep_rate_info_alloc(rate_limit);
			if (rinfos[i] == NULL) {
				fprintf(stderr,
				    "%s: no memory for %d/%d rate limit "
				    "on gfrebe_server %s\n",
				    program_name, i, ndivisions, my_name);
				fatal();
			}
		}
	}

	GFARM_MALLOC_ARRAY(socks, ndivisions);
	GFARM_MALLOC_ARRAY(conns, ndivisions);
	if (socks == NULL || conns == NULL) {
		if (socks != NULL)
			free(socks);
		if (conns != NULL)
			free(conns);
		fprintf(stderr,
		    "%s: no memory for %d connections on gfrebe_server %s\n",
		     program_name, ndivisions, my_name);
		fatal();
	}

	/*
	 * XXX FIXME: this port may be blocked by firewall.
	 * This should be implemented via gfsd connection passing service.
	 */

	listening_sock = open_accepting_socket();
	listen_addr_len = sizeof(listen_addr);
	if (getsockname(listening_sock,
	    (struct sockaddr *)&listen_addr, &listen_addr_len) == -1) {
		fprintf(stderr, "%s: getsockname: %s\n",
		    program_name, strerror(errno));
		fatal();
	}
	e = xxx_proto_send(to_client, "i", htons(listen_addr.sin_port));
	if (e == NULL)
		e = xxx_proto_flush(to_client);
	if (e != NULL) {
		fprintf(stderr,
		    "%s: while sending port number on %s: %s\n",
		     program_name, my_name, e);
		fatal();
	}

	/* XXX FIXME: make connections in parallel */
	for (i = 0; i < ndivisions; i++) {
		client_addr_len = sizeof(client_addr);
		socks[i] = accept(listening_sock,
		   (struct sockaddr *)&client_addr, &client_addr_len);
		if (socks[i] == -1) {
			if (errno == EINTR) {
				--i;
				continue;
			}
			fprintf(stderr,
			    "%s: while accepting sockets on %s: %s\n",
			     program_name, my_name, strerror(errno));
			fatal();
		}
		e = xxx_socket_connection_new(socks[i], &conns[i]);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: while allocating connection %d on %s: %s\n",
			     program_name, i, my_name, e);
			fatal();
		}
		e = gfarm_authorize(conns[i], 1, GFREPBE_SERVICE_TAG,
		    NULL, NULL, NULL);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: authorization on %s: %s\n",
			     program_name, my_name, e);
			fatal();
		}
		e = xxx_proto_send(conns[i], "i", i);
		if (e == NULL)
			e = xxx_proto_flush(conns[i]);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: sending connection index on %s: %s\n",
			     program_name, my_name, e);
			fatal();
		}
	}
	close(listening_sock);

	e = gfarm_netparam_config_get_long(&gfarm_netparam_file_read_size,
	    NULL, (struct sockaddr *)&client_addr, &file_read_size);
	if (e != NULL) { /* shouldn't happen */
		fprintf(stderr, "%s: get netparam file_read_size on %s: %s\n",
		    program_name, my_name, e);
		fatal();
	}

	e = gfarm_netparam_config_get_long(&gfarm_netparam_rate_limit,
	    NULL, (struct sockaddr *)&client_addr, &rate_limit);
	if (e != NULL) { /* shouldn't happen */
		fprintf(stderr, "%s: get netparam rate_limit on %s: %s\n",
		    program_name, my_name, e);
		fatal();
	}

	for (i = 0; i < n; i++) {
		char *file = gfarm_stringlist_elem(files, i);
		char *section = gfarm_stringlist_elem(sections, i);

		/* NOTE: assumes current directory == spool_root */
		if (*section == '\0') {
			pathname = file;
		} else {
			GFARM_MALLOC_ARRAY(pathname,
				strlen(file) + strlen(section) + 2);
			if (pathname == NULL) {
				fprintf(stderr,
				    "%s: no memory for pathname %s:%s"
				    " to replicate on %s\n",
				     program_name, file, section, my_name);
				fatal();
			}
			sprintf(pathname, "%s:%s", file, section);
		}
		ifd = open(pathname, O_RDONLY);
		if (ifd == -1) {
			r = gfs_errno_to_proto_error(errno);
		} else if (fstat(ifd, &st) == -1) {
			r = gfs_errno_to_proto_error(errno);
		} else {
			r = GFS_ERROR_NOERROR;
			file_size = st.st_size;
		}
		e = xxx_proto_send(conns[0], "i", r);
		if (e != NULL) {
			fprintf(stderr, "%s: send reply on %s\n",
			    program_name, my_name);
			fatal();
		}
		if (r != GFS_ERROR_NOERROR)
			continue;
		e = xxx_proto_send(conns[0], "o", file_size);
		if (e == NULL)
			e = xxx_proto_flush(conns[0]);
		if (e != NULL) {
			fprintf(stderr, "%s: send reply on %s\n",
			    program_name, my_name);
			fatal();
		}
		e = transfer(pathname, ifd, file_size, algorithm_version,
		    ndivisions, interleave_factor, file_read_size,
		    send_stripe_sync, conns, socks, rinfos,
		    &r);
		close(ifd);
		e = xxx_proto_send(conns[0], "i", r);
		if (e != NULL) {
			fprintf(stderr, "%s: send reply on %s\n",
			    program_name, my_name);
			fatal();
		}
		if (*section != '\0')
			free(pathname);
	}
	e = xxx_proto_flush(conns[0]);
	if (e != NULL) {
		fprintf(stderr, "%s: send reply on %s\n",
		    program_name, my_name);
		fatal();
	}

	for (i = 0; i < ndivisions; i++)
		xxx_connection_free(conns[i]);
	free(conns);
	free(socks);

	if (rinfos != NULL) {
		for (i = 0; i < ndivisions; i++)
			gfs_client_rep_rate_info_free(rinfos[i]);
		free(rinfos);
	}
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-p] "
	    "[-i interleave_factor] [-n <ndivisions>]\n",
	    program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	int algorithm_version = -1;
	gfarm_int32_t interleave_factor = 0;
	gfarm_int32_t ndivisions = 1;
	int send_stripe_sync = 0;
	struct xxx_connection *from_client, *to_client;
	char *e, *spool_root;
	int c, n;
	gfarm_stringlist files, sections;

	/* should be read from /etc/gfarm.conf */
	int syslog_facility = GFARM_DEFAULT_FACILITY;

	while ((c = getopt(argc, argv, "a:i:n:sS:")) != -1) {
		switch (c) {
		case 'a':
			algorithm_version = atoi(optarg);
			break;
		case 'i':
			interleave_factor = atoi(optarg);
			break;
		case 'n':
			ndivisions = atoi(optarg);
			break;
		case 's':
			send_stripe_sync = 1;
			break;
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
			    program_name, c);
			usage();
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;
	my_name = my_name;
	if (argc != 0) {
		fprintf(stderr, "%s: too many argument on %s: %s\n",
		    program_name, my_name, argv[0]);
		usage();
		/*NOTREACHED*/
	}
	if (algorithm_version == -1) {
		fprintf(stderr, "%s: missing mandatory option: -a <version>\n",
		    program_name);
		usage();
		/*NOTREACHED*/
	}

	e = gfarm_server_initialize();
	if (e != NULL) {
		fprintf(stderr, "gfarm_server_initialize: %s\n", e);
		exit(1);
	}
	e = gfrepbe_auth_initialize();
	if (e != NULL) {
		fprintf(stderr, "gfarm_auth_initialize: %s\n", e);
		exit(1);
	}

	/* makes current directory == spool_root */
	spool_root = gfarm_spool_root_for_compatibility;
	if (chdir(spool_root) == -1) {
		fprintf(stderr, "%s: chdir(%s) on %s: %s\n",
		    program_name, spool_root, my_name, e);
		fatal();
	}

	/* XXX read-only connection */
	e = xxx_socket_connection_new(STDIN_FILENO, &from_client);
	if (e != NULL) {
		fprintf(stderr, "%s: %s for stdin\n", program_name, e);
		fatal();
	}
	/* XXX write-only connection */
	e = xxx_socket_connection_new(STDOUT_FILENO, &to_client);
	if (e != NULL) {
		fprintf(stderr, "%s: %s for stdout\n", program_name, e);
		fatal();
	}

	e = gfs_client_rep_filelist_receive(from_client,
	    &n, &files, &sections, program_name);
	if (e != NULL)
		fatal();

	gflog_set_identifier(program_name);
	gflog_syslog_open(LOG_PID, syslog_facility);

	session(from_client, to_client,
	    algorithm_version, ndivisions, interleave_factor, send_stripe_sync,
	    n, &files, &sections);
	return (0);
}
