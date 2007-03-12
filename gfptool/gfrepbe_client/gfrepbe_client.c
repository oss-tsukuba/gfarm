#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "xxx_proto.h"
#include "io_fd.h"
#include "param.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "gfs_proto.h"
#include "gfs_client.h"

#define HACK_FOR_BWC

char *program_name = "gfrepbe_client";
char *my_name; /* == gfarm_host_get_self_name() */

char *gfrcmd_url = "gfarm:/bin/gfrcmd";
char *gfrep_backend_server_url = "gfarm:/libexec/gfrepbe_server";

long file_sync_rate;

struct gfs_client_rep_backend_state *backend = NULL;

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
	if (backend != NULL) {
		gfs_client_rep_backend_kill(backend);
		backend = NULL;
	}

	/* XXX FIXME: register transfered fragments to metadb */
	exit(EXIT_FAILURE);
}

enum gfs_proto_error
write_buffer(int fd, char *buffer, int len, int *disksync_cyclep)
{
	int i, rv;

	for (i = 0; i < len; i += rv) {
		rv = write(fd, buffer + i, len - i);
		if (rv <= 0) {
			/*
			 * write(2) never returns 0,
			 * so the following rv == 0 case is just warm fuzzy.
			 */
			return (rv == 0 ? ENOSPC :
			    gfs_errno_to_proto_error(errno));
		}
		if (file_sync_rate != 0) {
			*disksync_cyclep += rv;
			if (*disksync_cyclep >= file_sync_rate) {
				*disksync_cyclep -= file_sync_rate;
#ifdef HAVE_FDATASYNC
				fdatasync(fd);
#else
				fsync(fd);
#endif
			}
		}
	}
	return (GFS_ERROR_NOERROR);
}

#ifdef HAVE_PWRITE
enum gfs_proto_error
pwrite_buffer(int fd, char *buffer, int len, off_t offset,
	int *disksync_cyclep)
{
	int i, rv;

	for (i = 0; i < len; i += rv) {
		rv = pwrite(fd, buffer + i, len - i, offset + i);
		if (rv <= 0) {
			/*
			 * pwrite(2) never returns 0,
			 * so the following rv == 0 case is just warm fuzzy.
			 */
			return (rv == 0 ? ENOSPC :
			    gfs_errno_to_proto_error(errno));
		}
		if (file_sync_rate != 0) {
			*disksync_cyclep += rv;
			if (*disksync_cyclep >= file_sync_rate) {
				*disksync_cyclep -= file_sync_rate;
#ifdef HAVE_FDATASYNC
				fdatasync(fd);
#else
				fsync(fd);
#endif
			}
		}
	}
	return (GFS_ERROR_NOERROR);
}
#endif /* HAVE_PWRITE */

char *
sequential_transfer(int ofd, file_offset_t size, struct xxx_connection *conn,
	int *disksync_cyclep, gfarm_int32_t *write_resultp)
{
	char *e;
	int to_be_read, len, rv;
	gfarm_int32_t write_result = GFS_ERROR_NOERROR;
	file_offset_t transfered = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];

	while (transfered < size) {
		to_be_read = size - transfered < sizeof(buffer) ?
		    size - transfered : sizeof(buffer);
		e = xxx_read_direct(conn, buffer, to_be_read, &len);
		if (e != NULL)
			return (e);
		if (len == 0)
			return ("unexpected EOF");
		transfered += len;
		rv = write_buffer(ofd, buffer, len, disksync_cyclep);
		/*
		 * XXX - we cannot stop here, if write(2) failed, because
		 * currently there is no way to cancel on-going transfer.
		 */
		if (write_result == GFS_ERROR_NOERROR)
			write_result = rv;
	}
	*write_resultp = write_result;
	return (NULL);
}

char *
parallel_async_transfer(int ofd, file_offset_t size,
	int ndivisions, int interleave_factor, int file_sync_stripe,
	struct xxx_connection **conns, int *socks,
	struct gfs_client_rep_transfer_state **transfers,
	int *disksync_cyclep, gfarm_int32_t *write_resultp)
{
	char *e;
	int i, len, rv, max_fd, nfound;
	gfarm_int32_t write_result = GFS_ERROR_NOERROR;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	fd_set readable;

	/* file_sync_stripe is ignored with this function */

	for (;;) {
		/* prepare `readable' */
		FD_ZERO(&readable);
		max_fd = -1;
		for (i = 0; i < ndivisions; i++) {
			if (gfs_client_rep_transfer_finished(transfers[i]))
				continue;
			if (socks[i] >= FD_SETSIZE) { /* XXX - use poll? */
				fprintf(stderr, "%s: too big fd %d on %s\n",
				    program_name, socks[i], my_name);
				fatal();
			}
			FD_SET(socks[i], &readable);
			if (max_fd < socks[i])
				max_fd = socks[i];
		}

		if (max_fd == -1) /* completed */
			break;
		do {
			nfound = select(max_fd + 1, &readable, NULL,
			    NULL, NULL);
		} while (nfound == -1 && errno == EINTR);
		if (nfound <= 0) {
			fprintf(stderr,
			    "%s: parallel_transfer select: %s\n",
			    program_name,
			    nfound < 0 ? strerror(errno) : "assertion failed");
			fatal();
		}

		for (i = 0; i < ndivisions; i++) {
			if (!FD_ISSET(socks[i], &readable))
				continue;
			len = gfs_client_rep_transfer_length(transfers[i],
			    sizeof(buffer));
			e = xxx_read_direct(conns[i], buffer, len, &len);
			if (e != NULL)
				return (e);
			if (len == 0)
				return ("unexpected EOF");
#ifndef HAVE_PWRITE
			if (lseek(ofd,
			    (off_t)
			    gfs_client_rep_transfer_offset(transfers[i]),
			    SEEK_SET) == -1)
				return (gfarm_errno_to_error(errno));
			rv = write_buffer(ofd, buffer, len, disksync_cyclep);
#else
			rv = pwrite_buffer(ofd, buffer, len,
			    (off_t)
			    gfs_client_rep_transfer_offset(transfers[i]),
			    disksync_cyclep);
#endif
			/*
			 * XXX - we cannot stop here, if write(2)
			 * failed, because currently there is no way
			 * to cancel on-going transfer.
			 */
			if (write_result == GFS_ERROR_NOERROR)
				write_result = rv;
			gfs_client_rep_transfer_progress(transfers[i], len);
		}
	}
	*write_resultp = write_result;
	return (NULL);
}

char *
parallel_sync_transfer(int ofd, file_offset_t size,
	int ndivisions, int interleave_factor, int file_sync_stripe,
	struct xxx_connection **conns, int *socks,
	struct gfs_client_rep_transfer_state **transfers,
	int *disksync_cyclep, gfarm_int32_t *write_resultp)
{
	char *e;
	int full_stripe_size = ndivisions * interleave_factor;
	int i, k, len, rv, max_fd, nfound;
	gfarm_int32_t write_result = GFS_ERROR_NOERROR;
	file_offset_t transfered;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	fd_set readable;

	if (interleave_factor == 0)
		return ("interleave_factor == 0 doesn't make sense "
		    "for parallel_sync_transfer");

	k = 0;
	for (transfered = 0; transfered < size; ){
		/* prepare `readable' */
		FD_ZERO(&readable);
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
			FD_SET(socks[i], &readable);
			if (max_fd < socks[i])
				max_fd = socks[i];
		}

		if (max_fd == -1) { /* full_stripe_finished */
			transfered += full_stripe_size;
			for (i = 0; i < ndivisions; i++)
				gfs_client_rep_transfer_stripe_next(
				    transfers[i]);
			if (file_sync_stripe > 0) {
				if (++k >= file_sync_stripe) {
#ifdef HAVE_FDATASYNC
					fdatasync(ofd);
#else
					fsync(ofd);
#endif
					k = 0;
				}
			}
			continue;
		}
		do {
			nfound = select(max_fd + 1, &readable, NULL,
			    NULL, NULL);
		} while (nfound == -1 && errno == EINTR);
		if (nfound <= 0) {
			fprintf(stderr,
			    "%s: parallel_transfer select: %s\n",
			    program_name,
			    nfound < 0 ? strerror(errno) : "assertion failed");
			fatal();
		}

		for (i = 0; i < ndivisions; i++) {
			if (!FD_ISSET(socks[i], &readable))
				continue;
			len = gfs_client_rep_transfer_length(transfers[i],
			    sizeof(buffer));
			e = xxx_read_direct(conns[i], buffer, len, &len);
			if (e != NULL)
				return (e);
			if (len == 0)
				return ("unexpected EOF");
#ifndef HAVE_PWRITE
			if (lseek(ofd,
			    (off_t)
			    gfs_client_rep_transfer_offset(transfers[i]),
			    SEEK_SET) == -1)
				return (gfarm_errno_to_error(errno));
			rv = write_buffer(ofd, buffer, len, disksync_cyclep);
#else
			rv = pwrite_buffer(ofd, buffer, len,
			    (off_t)
			    gfs_client_rep_transfer_offset(transfers[i]),
			    disksync_cyclep);
#endif
			/*
			 * XXX - we cannot stop here, if write(2)
			 * failed, because currently there is no way
			 * to cancel on-going transfer.
			 */
			if (write_result == GFS_ERROR_NOERROR)
				write_result = rv;
			gfs_client_rep_transfer_stripe_progress(transfers[i],
			    len);
		}
	}
	*write_resultp = write_result;
	return (NULL);
}

char *
transfer(int ofd, file_offset_t size,
	int ndivisions, int interleave_factor,
	int file_sync_stripe, int recv_stripe_sync,
	struct xxx_connection **conns, int *socks,
	int *disksync_cyclep, gfarm_int32_t *write_resultp)
{
	char *e;
	struct gfs_client_rep_transfer_state **transfers;

	ndivisions = gfs_client_rep_limit_division(
	    GFS_CLIENT_REP_ALGORITHM_LATEST, ndivisions, size);
	if (ndivisions == -1)
		return ("unknown algorithm version");
	if (ndivisions == 1)
		return (sequential_transfer(ofd, size, conns[0],
		    disksync_cyclep, write_resultp));

	e = gfs_client_rep_transfer_state_alloc(size,
	    GFS_CLIENT_REP_ALGORITHM_LATEST, ndivisions, interleave_factor,
	    &transfers);
	if (e != NULL)
		return (e);
	e = (*(recv_stripe_sync?parallel_sync_transfer:parallel_async_transfer)
	    )(ofd, size, ndivisions, interleave_factor, file_sync_stripe,
	    conns, socks, transfers, disksync_cyclep, write_resultp);
	gfs_client_rep_transfer_state_free(ndivisions, transfers);
	return (e);
}

int
try_to_make_parent_dir(char *pathname)
{
	char *e, *p, *parent, *dirbuf;
	int rv, dirlen;
	gfarm_mode_t mode;
	struct stat st;
	struct gfarm_path_info pi;

	parent = strdup(pathname);
	if (parent == NULL) /* give up */
		return (0);

	/* get parent directory */
	p = (char *)gfarm_path_dir_skip(parent);
	while (p > parent && p[-1] == '/')
		*--p = '\0';
	if (p == parent) { /* no directory part */
		free(parent);
		return (0);
	}

	if (stat(parent, &st) == 0) { /* parent directory already exists */
		free(parent);
		return (0);
	}
	
	dirbuf = strdup(parent);
	if (dirbuf == NULL) {
		free(parent);
		return (0);
	}

	for (dirlen = 0;;) {
		dirlen += strcspn(parent + dirlen, "/");
		memcpy(dirbuf, parent, dirlen);
		dirbuf[dirlen] = '\0';
		if (stat(dirbuf, &st) == 0) {
			if (!S_ISDIR(st.st_mode)) {
				free(dirbuf);
				free(parent);
				return (0);
			}
		} else {
			e = gfarm_path_info_get(dirbuf, &pi);
			mode = pi.status.st_mode;
			if (e != NULL || !GFARM_S_ISDIR(mode)) {
				if (e == NULL)
					gfarm_path_info_free(&pi);
				free(dirbuf);
				free(parent);
				return (0);
			}
			mode &= GFARM_S_ALLPERM;
			if (strcmp(pi.status.st_user,
			    gfarm_get_global_username()) != 0) {
				mode |= 0777;
			}
			gfarm_path_info_free(&pi);
			rv = mkdir(dirbuf, mode);
			if (rv == -1) {
				free(dirbuf);
				free(parent);
				return (0);
			}
		}
		dirlen += strspn(parent + dirlen, "/");
		if (parent[dirlen] == '\0') { /* OK, made */
			free(dirbuf);
			free(parent);
			return (1);
		}
	}
}

void
session(char *server_name, struct sockaddr *server_addr,
	struct xxx_connection *from_server, struct xxx_connection *to_server,
	char *my_canonical_name,
	int ndivisions, int interleave_factor,
	int file_sync_stripe, int recv_stripe_sync,
	int n, gfarm_stringlist *files, gfarm_stringlist *sections,
	struct gfarm_path_info *path_infos, gfarm_int32_t *results)
{
	char *e, *pathname;
	int i, eof, ofd, mode;
	gfarm_int32_t r, port;
	int *socks, disksync_cycle = 0;
	struct xxx_connection **conns;
	struct sockaddr_in *sin = (struct sockaddr_in *)server_addr;
	file_offset_t size;

#ifdef __GNUC__ /* workaround gcc warning:  might be used uninitialized */
	ofd = -1;
#endif
	GFARM_MALLOC_ARRAY(socks, ndivisions);
	GFARM_MALLOC_ARRAY(conns, ndivisions);
	if (socks == NULL || conns == NULL) {
		fprintf(stderr,
		    "%s: no memory for %d connections to %s on %s\n",
		     program_name, ndivisions, server_name, my_name);
		fatal();
	}

	/*
	 * XXX FIXME: this port may be blocked by firewall.
	 * This should be implemented via gfsd connection passing service.
	 */

	e = xxx_proto_recv(from_server, 0, &eof, "i", &port);
	if (e != NULL || eof) {
		fprintf(stderr,
		    "%s: while reading port number from %s on %s: %s\n",
		     program_name, server_name, my_name,
		    e != NULL ? e : "unexpected EOF");
		fatal();
	}
	sin->sin_port = htons(port);

	/* sanity */
	for (i = 0; i < ndivisions; i++)
		socks[i] = -1;
	/* XXX FIXME: make connections in parallel */
	for (i = 0; i < ndivisions; i++) {
		struct xxx_connection *tmp_conn;
		int tmp_sock;
		gfarm_int32_t index;

		tmp_sock = socket(PF_INET, SOCK_STREAM, 0);
		if (tmp_sock == -1) {
			fprintf(stderr,
			    "%s: while opening sockets on %s: %s\n",
			     program_name, my_name, strerror(errno));
			fatal();
		}
		/* XXX FIXME canonical_name? */
		e = gfarm_sockopt_apply_by_name_addr(tmp_sock,
		    server_name, server_addr);
		if (e != NULL) {
			fprintf(stderr, "%s: setsockopt to %s on %s: %s\n",
			    program_name, server_name, my_name, e);
		}
		if (connect(tmp_sock, server_addr, sizeof(*server_addr))== -1){
			fprintf(stderr,
			    "%s: while connecting to %s on %s: %s\n",
			    program_name, server_name, my_name, e);
			fatal();
		}
		/* XXX read-only connection */
		e = xxx_socket_connection_new(tmp_sock, &tmp_conn);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: while allocating memory to %s on %s: %s\n",
			    program_name, server_name, my_name, e);
			fatal();
		}
		e = gfarm_auth_request(tmp_conn, GFREPBE_SERVICE_TAG,
		    server_name, server_addr, NULL);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: authorization request to %s on %s: %s\n",
			    program_name, server_name, my_name, e);
			fatal();
		}
		/* XXX call shutdown(2) here? */
		e = xxx_proto_recv(tmp_conn, 1, &eof, "i", &index);
		if (e != NULL || eof) {
			fprintf(stderr,
			    "%s: while reading port index from %s on %s: %s\n",
			    program_name, server_name, my_name,
			    e != NULL ? e : "unexpected EOF");
			fatal();
		}
		if (index < 0 || index >= n) {
			fprintf(stderr,
			    "%s: invalid port index %d from %s on %s\n",
			    program_name, index, server_name, my_name);
			fatal();
		}
		/* sanity */
		if (socks[index] != -1) {
			fprintf(stderr,
			    "%s: duplicate port index %d from %s on %s\n",
			    program_name, index, server_name, my_name);
			fatal();
		}
		socks[index] = tmp_sock;
		conns[index] = tmp_conn;
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
				    " to replicate it from %s to %s\n",
				     program_name, file, section, server_name,
				     my_name);
				fatal();
			}
			sprintf(pathname, "%s:%s", file, section);
		}
		/* NOTE: just==TRUE here */
		e = xxx_proto_recv(conns[0], 1, &eof, "i", &r);
		if (e != NULL || eof) {
			fprintf(stderr,
			    "%s: while reading status "
			    "to replicate %s from %s to %s: %s\n",
			     program_name, pathname,
			    server_name, my_name,
			    e != NULL ? e : "unexpected EOF");
			fatal();
		}
		if (r != GFS_ERROR_NOERROR) {
			if (results[i] == GFS_ERROR_NOERROR)
				results[i] = r;
			fprintf(stderr, "%s: replicate %s from %s to %s: %s\n",
			    program_name, pathname, server_name, my_name,
			    gfs_proto_error_string(r));
			if (*section != '\0')
				free(pathname);
			continue;
		}
		/* NOTE: just==TRUE here */
		e = xxx_proto_recv(conns[0], 1, &eof, "o", &size);
		if (e != NULL || eof) {
			fprintf(stderr,
			    "%s: while reading file size "
			    "to replicate %s from %s to %s: %s\n",
			    program_name, pathname, server_name, my_name,
			    e != NULL ? e : "unexpected EOF");
			fatal();
		}
		/* XXX FIXME - FT! */
		/* XXX FIXME - owner! */
		/* XXX FIXME - mode! */
		if (results[i] == GFS_ERROR_NOERROR) {
			mode =
			    (path_infos[i].status.st_mode & GFARM_S_ALLPERM);

			/*
			 * XXX FIXME
			 * if the owner of a file is not the same,
			 * permit a group/other write access - This
			 * should be fixed in the next major release.
			 */
			if (strcmp(path_infos[i].status.st_user,
			    gfarm_get_global_username()) != 0) {
				/* don't allow setuid/setgid */
				mode = (mode | 022) & 0777;
			}

			ofd = open(pathname, O_CREAT|O_WRONLY, mode);
			/* FT */
			if (ofd == -1 && try_to_make_parent_dir(pathname))
				ofd = open(pathname, O_CREAT|O_WRONLY, mode);
			if (ofd == -1) {
				results[i] = gfs_errno_to_proto_error(errno);
				fprintf(stderr,
				    "%s: cannot create file"
				    "to replicate %s from %s to %s: %s\n",
				    program_name, pathname, server_name,
				    my_name, strerror(errno));
			}
		}
		if (results[i] != GFS_ERROR_NOERROR) {
			/* XXX FIXME: just waste. detect this error before */
			ofd = open("/dev/null", O_WRONLY);
			if (ofd == -1) {
				fprintf(stderr,
				    "%s: cannot open /dev/null"
				    "to replicate %s from %s to %s: %s\n",
				    program_name, pathname,
				    server_name, my_name, strerror(errno));
				fatal();
			}
		}

		e = transfer(ofd, size,
		    ndivisions, interleave_factor,
		    file_sync_stripe, recv_stripe_sync,
		    conns, socks, &disksync_cycle, &r);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: while replicating %s from %s to %s: %s\n",
			    program_name, pathname, server_name, my_name, e);
			fatal();
		}
		if (r != GFS_ERROR_NOERROR) {
			if (results[i] == GFS_ERROR_NOERROR)
				results[i] = r;
			fprintf(stderr,
			    "%s: while writing contents "
			    "to replicate %s from %s to %s: %s\n",
			    program_name, pathname, server_name, my_name,
			    gfs_proto_error_string(r));
		}

		/* NOTE: just==TRUE here */
		e = xxx_proto_recv(conns[0], 1, &eof, "i", &r);
		if (e != NULL || eof) {
			fprintf(stderr,
			    "%s: while reading status "
			    "to replicate %s from %s to %s: %s\n",
			     program_name, pathname, server_name, my_name,
			    e != NULL ? e : "unexpected EOF");
			fatal();
		}
		if (r != GFS_ERROR_NOERROR) {
			if (results[i] == GFS_ERROR_NOERROR)
				results[i] = r;
			fprintf(stderr,
			    "%s: replicated %s from %s to %s: %s\n",
			    program_name, pathname, server_name, my_name,
			    gfs_proto_error_string(r));
		}

#ifndef HACK_FOR_BWC
		/* register metadata */
		if (results[i] == GFS_ERROR_NOERROR) {
			struct gfarm_file_section_copy_info fci;

			e = gfarm_file_section_copy_info_set(file, section,
			    my_canonical_name, &fci);
			if (e != NULL)
				results[i] = gfs_string_to_proto_error(e);
		}
#endif

		if (results[i] != GFS_ERROR_NOERROR)
			unlink(pathname);
		close(ofd);
		if (*section != '\0')
			free(pathname);
	}

	for (i = 0; i < ndivisions; i++)
		xxx_connection_free(conns[i]);
	free(conns);
	free(socks);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-p] "
	    "[-i interleave_factor] [-n <ndivisions>] <server_name>\n",
	    program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	gfarm_int32_t interleave_factor = 0;
	gfarm_int32_t ndivisions = 1;
	int recv_stripe_sync = 0;
	int send_stripe_sync = 0;
	int file_sync_stripe = 0;
	struct xxx_connection *from_frontend, *to_frontend;
	struct xxx_connection *from_server, *to_server;
	char *e, *src_host, *my_canonical_name;
	char **gfrcmd_paths, **gfrep_backend_server_paths;
	int c, i, n, replication_method_save;
	gfarm_stringlist files, sections;
	struct gfarm_path_info *path_infos;
	gfarm_int32_t *results;
	struct sockaddr server_addr;
	char *spool_root;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: gfarm_initialize on %s: %s\n",
		    program_name, my_name, e);
		fatal();
	}
	e = gfrepbe_auth_initialize();
	if (e != NULL) {
		fprintf(stderr, "gfarm_auth_initialize: %s\n", e);
		exit(1);
	}
	while ((c = getopt(argc, argv, "i:n:rsS:")) != -1) {
		switch (c) {
		case 'i':
			interleave_factor = atoi(optarg);
			break;
		case 'n':
			ndivisions = atoi(optarg);
			break;
		case 'r':
			recv_stripe_sync = 1;
			break;
		case 's':
			send_stripe_sync = 1;
			break;
		case 'S':
			file_sync_stripe = atoi(optarg);
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
	my_name = gfarm_host_get_self_name();
	if (argc != 1) {
		fprintf(stderr, "%s: missing server name argument on %s\n",
		    program_name, my_name);
		usage();
		/*NOTREACHED*/
	}
	src_host = argv[0];

	e = gfarm_host_get_canonical_self_name(&my_canonical_name);
	if (e != NULL) {
		fprintf(stderr, "%s: host %s isn't a filesystem node: %s\n",
			program_name, my_name, e);
		fatal();
	}

	replication_method_save = gfarm_replication_get_method();
	gfarm_replication_set_method(GFARM_REPLICATION_BOOTSTRAP_METHOD);

	e = gfarm_url_program_deliver(gfrcmd_url,
	    1, &my_canonical_name, &gfrcmd_paths);
	if (e != NULL) {
		fprintf(stderr, "%s: cannot deliver %s to host %s on %s: %s\n",
		    program_name, gfrcmd_url, my_canonical_name, my_name, e);
		fatal();
	}

	e = gfarm_url_program_deliver(gfrep_backend_server_url,
	    1, &src_host, &gfrep_backend_server_paths);
	if (e != NULL) {
		fprintf(stderr, "%s: cannot deliver %s to host %s on %s: %s\n",
		    program_name, gfrep_backend_server_url, src_host, my_name,
		    e);
		fatal();
	}

	gfarm_replication_set_method(replication_method_save);

	e = gfs_client_rep_backend_invoke(
	    src_host, gfrcmd_paths[0], gfrep_backend_server_paths[0], NULL,
	    GFS_CLIENT_REP_ALGORITHM_LATEST,
	    ndivisions, interleave_factor, 0, send_stripe_sync, 0,
	    program_name,
	    &from_server, &to_server, &backend);
	if (e != NULL)
		fatal();

	e = gfarm_host_address_get(src_host, 0, &server_addr, NULL);
	if (e != NULL) {
		fprintf(stderr, "%s: %s: %s\n", program_name, src_host, e);
		fatal();
	}

	e = gfarm_netparam_config_get_long(&gfarm_netparam_file_sync_rate,
	    src_host, (struct sockaddr *)&server_addr, &file_sync_rate);
	if (e != NULL) {/* shouldn't happen */
		fprintf(stderr,
		    "%s: get netparam file_sync_rate on %s (%s): %s\n",
		    program_name, my_name, src_host, e);
		fatal();
	}

	/* XXX read-only connection */
	e = xxx_socket_connection_new(STDIN_FILENO, &from_frontend);
	if (e != NULL) {
		fprintf(stderr, "%s: %s for stdin\n", program_name, e);
		fatal();
	}
	/* XXX write-only connection */
	e = xxx_socket_connection_new(STDOUT_FILENO, &to_frontend);
	if (e != NULL) {
		fprintf(stderr, "%s: %s for stdout\n", program_name, e);
		fatal();
	}

	e = gfs_client_rep_filelist_receive(from_frontend,
	    &n, &files, &sections, program_name);
	if (e != NULL)
		fatal();

	e = gfs_client_rep_filelist_send(src_host, to_server, program_name,
	    n, &files, &sections);
	if (e != NULL)
		fatal();

	GFARM_MALLOC_ARRAY(results, n);
	if (results == NULL) {
		fprintf(stderr, "%s: no memory for %d ints on %s\n",
		    program_name, n, my_name);
		fatal();
	}

	/* make current directory == spool_root */
	spool_root = gfarm_spool_root_for_compatibility;
	if (chdir(spool_root) == -1) {
		fprintf(stderr, "%s: chdir(%s) on %s: %s\n",
		    program_name, spool_root, my_name, e);
		fatal();
	}

	GFARM_MALLOC_ARRAY(path_infos, n);
	if (results == NULL) {
		fprintf(stderr, "%s: no memory for %d path_info on %s\n",
		    program_name, n, my_name);
		fatal();
	}
	for (i = 0; i < n; i++) {
		e = gfarm_path_info_get(gfarm_stringlist_elem(&files, i),
		    &path_infos[i]);
		results[i] = e == NULL ? GFS_ERROR_NOERROR :
			gfs_string_to_proto_error(e);
	}

	umask(0); /* don't mask, just use the 3rd parameter of open(2) */

	session(src_host, &server_addr, from_server, to_server,
	    my_canonical_name,
	    ndivisions, interleave_factor, file_sync_stripe, recv_stripe_sync,
	    n, &files, &sections, path_infos, results);

#ifdef HACK_FOR_BWC
	/*
	 * If this program fails with fatal(), or is killed by a signal,
	 * this metadata update isn't executed. (So, #undef HACK_FOR_BWC)
	 */
	for (i = 0; i < n; i++) {
		char *file = gfarm_stringlist_elem(&files, i);
		char *section = gfarm_stringlist_elem(&sections, i);
		struct gfarm_file_section_copy_info fci;

		if (results[i] == GFS_ERROR_NOERROR) {
			e = gfarm_file_section_copy_info_set(file, section,
			    my_canonical_name, &fci);
			if (e != NULL)
				results[i] = gfs_string_to_proto_error(e);
		}
	}
#endif

	for (i = 0; i < n; i++)
		xxx_proto_send(to_frontend, "i", results[i]);
	xxx_proto_flush(to_frontend);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: gfarm_terminate on %s: %s\n",
		    program_name, my_name, e);
		fatal();
	}
	return (0);
}
