/*
 * $Id$
 */

#define _POSIX_PII_SOCKET /* to use struct msghdr on Tru64 */
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

#if defined(SCM_RIGHTS) && \
		(!defined(sun) || (!defined(__svr4__) && !defined(__SVR4)))
#define HAVE_MSG_CONTROL 1
#endif

#if !defined(WCOREDUMP) && defined(_AIX)
#define WCOREDUMP(status)	((status) & 0x80)
#endif

#ifndef SHUT_WR		/* some really old OS doesn't define this symbol. */
#define SHUT_WR	1
#endif

#include <openssl/evp.h>

#include <gfarm/gfarm_config.h>

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>	/* getloadavg() on Solaris */
#endif

#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#define GFLOG_USE_STDARG
#include "gfutil.h"

#include "iobuffer.h"
#include "xxx_proto.h"
#include "io_fd.h"
#include "param.h"
#include "sockopt.h"
#include "host.h"
#include "auth.h"
#include "config.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfs_misc.h" /* gfarm_path_dir() */
#include "spool_root.h"

#include "gfsd_subr.h"

#define COMPAT_OLD_GFS_PROTOCOL

#ifndef DEFAULT_PATH
#define DEFAULT_PATH "PATH=/usr/local/bin:/usr/bin:/bin:/usr/ucb:/usr/X11R6/bin:/usr/openwin/bin:/usr/pkg/bin"
#endif

#define GFARM_DEFAULT_PATH	DEFAULT_PATH ":" GFARM_DEFAULT_BINDIR

#ifndef PATH_BSHELL
#define PATH_BSHELL "/bin/sh"
#endif

#ifdef SOMAXCONN
#define LISTEN_BACKLOG	SOMAXCONN
#else
#define LISTEN_BACKLOG	5
#endif

/* limit maximum open files per client, when system limit is very high */
#ifndef FILE_TABLE_LIMIT
#define FILE_TABLE_LIMIT	2048
#endif

static const char READONLY_CONFIG_FILE[] = ".readonly";
static const char IO_SANITY_CHECK_FILE[] = ".GFARMTEST";

char *program_name = "gfsd";

int debug_mode = 0;

mode_t command_umask;

int restrict_user = 0;
uid_t restricted_user = 0;

long file_read_size;
long rate_limit;

struct xxx_connection *credential_exported = NULL;

/* this routine should be called before calling exit(). */
void
cleanup_service(void)
{
	if (credential_exported != NULL)
		xxx_connection_delete_credential(credential_exported);
	credential_exported = NULL;

	/* disconnect, do logging */
	gflog_notice("disconnected");
}

#define fatal_proto(proto, status)	fatal(proto, status)

void
fatal(char *message, char *status)
{
	cleanup_service();
	if (status != NULL)
		gflog_fatal("%s: %s", message, status);
	else
		gflog_fatal("%s", message);
}

void
fatal_errno(char *message)
{
	int save_errno = errno;

	cleanup_service();
	errno = save_errno;
	gflog_fatal_errno(message);
}

static void
check_input_output_error(char *message, int eno)
{
	if (eno == EIO)
		fatal(message, GFARM_ERR_INPUT_OUTPUT);
}

int accepting_unix_socks_count;
char **unix_sock_names, **unix_sock_dirs;

/* this routine should be called before accepting server calls exit(). */
void
cleanup_accepting(void)
{
	int i;

	for (i = 0; i < accepting_unix_socks_count; i++) {
		unlink(unix_sock_names[i]);
		rmdir(unix_sock_dirs[i]);
	}
}

void
accepting_fatal(const char *format, ...)
{
	va_list ap;

	cleanup_accepting();
	va_start(ap, format);
	gflog_vmessage(LOG_ERR, format, ap);
	va_end(ap);
	exit(2);
}

void
accepting_fatal_errno(char *message)
{
	int save_errno = errno;

	cleanup_accepting();
	errno = save_errno;
	gflog_fatal_errno(message);
}

void
accepting_sigterm_handler(int sig)
{
	cleanup_accepting();
	_exit(1);
}

void
gfs_server_get_request(struct xxx_connection *client, char *diag,
	char *format, ...)
{
	va_list ap;
	char *e;
	int eof;

	gflog_debug("request: %s", diag);

	va_start(ap, format);
	e = xxx_proto_vrecv(client, 0, &eof, &format, &ap);
	va_end(ap);

	if (e != NULL)
		fatal_proto(diag, e);
	if (eof)
		fatal_proto(diag, "missing RPC argument");
	if (*format != '\0')
		fatal(diag, "invalid format character to get request");
}

void
gfs_server_put_reply_common(struct xxx_connection *client, char *diag,
	int ecode, char *format, va_list *app)
{
	char *e;

	gflog_debug("reply: %s (%d): %s", diag, ecode,
	     gfs_proto_error_string(ecode));

	e = xxx_proto_send(client, "i", (gfarm_int32_t)ecode);
	if (e != NULL)
		fatal_proto(diag, e);
	if (ecode == 0) {
		e = xxx_proto_vsend(client, &format, app);
		if (e != NULL)
			fatal_proto(diag, e);
	}

	if (ecode == 0 && *format != '\0')
		fatal(diag, "invalid format character to put reply");
}

void
gfs_server_put_reply(struct xxx_connection *client, char *diag,
	int ecode, char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfs_server_put_reply_common(client, diag, ecode, format, &ap);
	va_end(ap);
}

void
gfs_server_put_reply_with_errno(struct xxx_connection *client, char *diag,
	int eno, char *format, ...)
{
	va_list ap;
	int ecode = gfs_errno_to_proto_error(eno);

	if (ecode == GFS_ERROR_UNKNOWN)
		gflog_info("%s: %s", diag, strerror(eno));
	va_start(ap, format);
	gfs_server_put_reply_common(client, diag, ecode, format, &ap);
	va_end(ap);
}

int file_table_free = 0;
int file_table_size = 0;
int *file_table;

void
file_table_init(int table_size)
{
	int i;

	GFARM_MALLOC_ARRAY(file_table, table_size);
	if (file_table == NULL) {
		errno = ENOMEM; fatal_errno("file table");
	}
	for (i = 0; i < table_size; i++)
		file_table[i] = -1;
	file_table_size = table_size;
}

int
file_table_add(int fd)
{
	if (file_table_free >= file_table_size) {
		close(fd);
		errno = EMFILE;
		return (-1);
	}
	file_table[file_table_free] = fd;
	fd = file_table_free;
	for (++file_table_free;
	     file_table_free < file_table_size; ++file_table_free)
		if (file_table[file_table_free] == -1)
			break;
	return (fd);
}

int
file_table_close(int fd)
{
	int rv = 0;

	if (fd >= file_table_size || file_table[fd] == -1)
		return (EBADF);
	if (close(file_table[fd]) < 0)
		rv = errno;
	file_table[fd] = -1;
	if (fd < file_table_free)
		file_table_free = fd;
	return (rv);
}

int
file_table_get(int fd)
{
	if (0 <= fd && fd < file_table_size)
		return (file_table[fd]);
	else
		return (-1);
}

void
local_path(char *file, char **pathp, char *diag)
{
	char *e;

	e = gfarm_spool_path_localize(file, pathp);
	free(file);
	if (e != NULL)
		fatal(diag, e);
}

void
local_path_for_write(char *file, char **pathp, char *diag)
{
	char *e;

	e = gfarm_spool_path_localize_for_write(file, pathp);
	free(file);
	if (e != NULL)
		fatal(diag, e);
}

static int
gfarm_fd_send_message(int fd, void *buf, size_t size, int fdc, int *fdv)
{
	char *buffer = buf;
	int rv;
	struct iovec iov[1];
	struct msghdr msg;
#ifdef HAVE_MSG_CONTROL /* 4.3BSD Reno or later */
	int i;
	struct {
		struct cmsghdr hdr;
		char data[CMSG_SPACE(sizeof(*fdv) * GFSD_MAX_PASSING_FD)
			  - sizeof(struct cmsghdr)];
	} cmsg;

	if (fdc > GFSD_MAX_PASSING_FD) {
#if 0
		gflog_fatal("gfarm_fd_send_message(): fd count %d > %d",
		    fdc, GFSD_MAX_PASSING_FD);
#endif
		return (EINVAL);
	}
#endif

	while (size > 0) {
		iov[0].iov_base = buffer;
		iov[0].iov_len = size;
		msg.msg_iov = iov;
		msg.msg_iovlen = 1;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
#ifndef HAVE_MSG_CONTROL
		if (fdc > 0) {
			msg.msg_accrights = (caddr_t)fdv;
			msg.msg_accrightslen = sizeof(*fdv) * fdc;
		} else {
			msg.msg_accrights = NULL;
			msg.msg_accrightslen = 0;
		}
#else /* 4.3BSD Reno or later */
		if (fdc > 0) {
			msg.msg_control = (caddr_t)&cmsg.hdr;
			msg.msg_controllen = CMSG_SPACE(sizeof(*fdv) * fdc);
			cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(*fdv) * fdc);
			cmsg.hdr.cmsg_level = SOL_SOCKET;
			cmsg.hdr.cmsg_type = SCM_RIGHTS;
			for (i = 0; i < fdc; i++)
				((int *)CMSG_DATA(&cmsg.hdr))[i] = fdv[i];
		} else {
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
		}
#endif
		rv = sendmsg(fd, &msg, 0);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			return (errno); /* failure */
		}
		fdc = 0; fdv = NULL;
		buffer += rv;
		size -= rv;
	}
	return (0); /* success */
}

void
gfs_server_open(struct xxx_connection *client)
{
	char *file, *path;
	gfarm_int32_t netflag, mode;
	int hostflag, fd = -1, save_errno = 0;
	char *msg = "open";

	gfs_server_get_request(client, msg, "sii", &file, &netflag, &mode);

	hostflag = gfs_open_flags_localize(netflag);
	if (hostflag == -1) {
		gfs_server_put_reply(client, msg, GFS_ERROR_INVAL, "");
		free(file);
		return;
	}

	local_path(file, &path, msg);
	if ((fd = open(path, hostflag, mode)) < 0) {
		save_errno = errno;
	} else if ((fd = file_table_add(fd)) < 0) {
		save_errno = errno;
	}
	free(path);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "i", fd);
}

void
gfs_server_open_local(struct xxx_connection *client)
{
	char *file, *path, *e;
	gfarm_int32_t netflag, mode;
	int hostflag, fd = -1, save_errno = 0, buf[1], rv;
	char *msg = "open_local";

	gfs_server_get_request(client, msg, "sii",
	    &file, &netflag, &mode);

	hostflag = gfs_open_flags_localize(netflag);
	if (hostflag == -1) {
		gfs_server_put_reply(client, msg, GFS_ERROR_INVAL, "");
		free(file);
		return;
	}

	local_path(file, &path, msg);
	if ((fd = open(path, hostflag, mode)) < 0)
		save_errno = errno;
	free(path);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	if (save_errno != 0)
		return;

	/* need to flush iobuffer before sending data w/o iobuffer */
	e = xxx_proto_flush(client);
	if (e != NULL)
		gflog_warning("%s: flush: %s", msg, e);

	buf[0] = 0;
	rv = gfarm_fd_send_message(xxx_connection_fd(client),
		buf, sizeof(buf), 1, &fd);
	if (rv != 0)
		gflog_error("%s: %s: sendmsg", msg, strerror(rv));
	if (close(fd) == -1)
		gflog_error("%s: %s: close", msg, strerror(errno));
}

void
gfs_server_close(struct xxx_connection *client)
{
	int fd, save_errno;
	char *msg = "close";

	gfs_server_get_request(client, msg, "i", &fd);

	save_errno = file_table_close(fd);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

void
gfs_server_seek(struct xxx_connection *client)
{
	int fd, whence, save_errno = 0;
	file_offset_t offset;
	off_t rv;
	char *msg = "seek";

	gfs_server_get_request(client, msg, "ioi", &fd, &offset, &whence);

	rv = lseek(file_table_get(fd), (off_t)offset, whence);
	if (rv == -1)
		save_errno = errno;
	else
		offset = rv;

	gfs_server_put_reply_with_errno(client, msg, save_errno,
	    "o", offset);
}

void
gfs_server_ftruncate(struct xxx_connection *client)
{
	int fd;
	file_offset_t length;
	int save_errno = 0;
	char *msg = "ftruncate";

	gfs_server_get_request(client, msg, "io", &fd, &length);

	if (ftruncate(file_table_get(fd), (off_t)length) == -1)
		save_errno = errno;

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

void
gfs_server_read(struct xxx_connection *client)
{
	ssize_t rv;
	int fd, size, save_errno = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	char *msg = "read";

	gfs_server_get_request(client, msg, "ii", &fd, &size);

	/* We truncatef i/o size bigger than GFS_PROTO_MAX_IOSIZE. */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	rv = read(file_table_get(fd), buffer, size);
	if (rv == -1)
		save_errno = errno;

	gfs_server_put_reply_with_errno(client, msg, save_errno,
	    "b", rv, buffer);
	check_input_output_error(msg, save_errno);
}

void
gfs_server_write(struct xxx_connection *client)
{
	ssize_t rv;
	size_t size;
	gfarm_int32_t fd;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	int save_errno = 0;
	char *msg = "write";

	gfs_server_get_request(client, msg, "ib",
	    &fd, sizeof(buffer), &size, buffer);

	/*
	 * We truncate i/o size bigger than GFS_PROTO_MAX_IOSIZE.
	 * This is inefficient because passed extra data are just
	 * abandoned. So client should avoid such situation.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	rv = write(file_table_get(fd), buffer, size);
	if (rv == -1)
		save_errno = errno;

	gfs_server_put_reply_with_errno(client, msg, save_errno,
	    "i", (gfarm_int32_t)rv);
	check_input_output_error(msg, save_errno);
}

void
gfs_server_fsync(struct xxx_connection *client)
{
	int fd;
	int operation;
	int save_errno = 0;
	char *msg = "fsync";

	gfs_server_get_request(client, msg, "ii", &fd, &operation);

	switch (operation) {
	case GFS_PROTO_FSYNC_WITHOUT_METADATA:      
#ifdef HAVE_FDATASYNC
		if (fdatasync(file_table_get(fd)) == -1)
			save_errno = errno;
		break;
#else
		/*FALLTHROUGH*/
#endif
	case GFS_PROTO_FSYNC_WITH_METADATA:
		if (fsync(file_table_get(fd)) == -1)
			save_errno = errno;
		break;
	default:
		save_errno = EINVAL;
		break;
	}

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

void
gfs_server_link(struct xxx_connection *client)
{
	char *from, *to, *fpath, *tpath, *spool;
	int save_errno = 0;
	char *msg = "link";

	gfs_server_get_request(client, msg, "ss", &from, &to);

	/* need to use the same spool directory */
	spool = gfarm_spool_root_get_for_read(from);
	fpath = gfarm_spool_path(spool, from);
	tpath = gfarm_spool_path(spool, to);
	free(from);
	free(to);
	if (spool == NULL || fpath == NULL || tpath == NULL)
		fatal(msg, GFARM_ERR_NO_MEMORY);
	if (link(fpath, tpath) == -1)
		save_errno = errno;
	free(fpath);
	free(tpath);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

void
gfs_server_unlink(struct xxx_connection *client)
{
	char *file, *path;
	int save_errno = 0;
	char *msg = "unlink";

	gfs_server_get_request(client, msg, "s", &file);

	local_path(file, &path, msg);
	if (unlink(path) == -1)
		save_errno = errno;
	free(path);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

void
gfs_server_rename(struct xxx_connection *client)
{
	char *from, *to, *fpath, *tpath, *spool;
	int save_errno = 0;
	char *msg = "rename";

	gfs_server_get_request(client, msg, "ss", &from, &to);

	/* need to use the same spool directory */
	spool = gfarm_spool_root_get_for_read(from);
	fpath = gfarm_spool_path(spool, from);
	tpath = gfarm_spool_path(spool, to);
	free(from);
	free(to);
	if (spool == NULL || fpath == NULL || tpath == NULL)
		fatal(msg, GFARM_ERR_NO_MEMORY);
	if (rename(fpath, tpath) == -1)
		save_errno = errno;
	free(fpath);
	free(tpath);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
}

static int
gfs_mkdir_(char *f, void *a)
{
	mode_t mode = (mode_t)a;

	return (mkdir(f, mode));
}

void
gfs_server_mkdir(struct xxx_connection *client)
{
	char *gpath;
	gfarm_int32_t mode;
	int save_errno = 0;
	char *msg = "mkdir";

	gfs_server_get_request(client, msg, "si", &gpath, &mode);

	save_errno = gfarm_spool_root_foreach(gfs_mkdir_, gpath, (void *)mode);
	free(gpath);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

static int
gfs_rmdir_(char *f, void *a)
{
	return (rmdir(f));
}

void
gfs_server_rmdir(struct xxx_connection *client)
{
	char *gpath;
	int save_errno = 0;
	char *msg = "rmdir";

	gfs_server_get_request(client, msg, "s", &gpath);

	save_errno = gfarm_spool_root_foreach(gfs_rmdir_, gpath, NULL);
	free(gpath);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
}

static int
gfs_chmod_(char *f, void *a)
{
	mode_t mode = (mode_t)a;

	return (chmod(f, mode));
}

void
gfs_server_chmod(struct xxx_connection *client)
{
	char *file;
	gfarm_int32_t mode;
	int save_errno = 0;
	char *msg = "chmod";

	gfs_server_get_request(client, msg, "si", &file, &mode);

	save_errno = gfarm_spool_root_foreach(gfs_chmod_, file, (void *)mode);
	free(file);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

/* XXX -- it does not work correctly */
void
gfs_server_chgrp(struct xxx_connection *client)
{
	char *file, *path, *group;
	int save_errno = 0;
	struct group *grp;
	char *msg = "chgrp";

	gfs_server_get_request(client, msg, "ss", &file, &group);

	local_path(file, &path, msg);
	grp = getgrnam(group);
	if (grp == NULL) {
		save_errno = EPERM;
	} else if (chown(path, -1, grp->gr_gid) == -1) {
		save_errno = errno;
	}
	free(path);
	free(group);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

void
gfs_server_fstat(struct xxx_connection *client)
{

	struct stat st;
	file_offset_t size = 0;
	int fd, save_errno = 0;
	char *msg = "fstat";

	gfs_server_get_request(client, msg, "i", &fd);

	if (fstat(file_table_get(fd), &st) == -1)
		save_errno = errno;
	else
		size = st.st_size;

	gfs_server_put_reply_with_errno(client, msg, save_errno,
		"iioiii", st.st_mode, st.st_nlink, size,
		st.st_atime, st.st_mtime, st.st_ctime);
}

void
gfs_server_exist(struct xxx_connection *client)
{
	char *file, *path;
	int save_errno = 0;
	struct stat buf;
	char *msg = "exist";

	gfs_server_get_request(client, msg, "s", &file);

	local_path(file, &path, msg);
	if (stat(path, &buf) == -1) {
		save_errno = errno;
	}
	free(path);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
}

void
gfs_server_digest(struct xxx_connection *client)
{
	char *digest_type;
	int fd, save_errno = 0;
	file_offset_t filesize;
	EVP_MD_CTX md_ctx;
	const EVP_MD *md_type;
	size_t digest_length;
	unsigned char digest_value[EVP_MAX_MD_SIZE];
	char buffer[GFS_PROTO_MAX_IOSIZE];
	char *msg = "digest";

	gfs_server_get_request(client, msg, "is", &fd, &digest_type);
	
	if ((md_type = EVP_get_digestbyname(digest_type)) == NULL) {
		save_errno = EINVAL;
		/*
		 * to avoid loading malformed value to floating register,
		 * if file_offset_t is double
		 */
		filesize = 0;
	} else {
		save_errno = gfs_digest_calculate_local(
		    file_table_get(fd), buffer, sizeof(buffer),
		    md_type, &md_ctx, &digest_length, digest_value, &filesize);
	}
	free(digest_type);

	gfs_server_put_reply_with_errno(client, msg, save_errno,
	    "bo", digest_length, digest_value, filesize);
	check_input_output_error(msg, save_errno);
}

void
gfs_server_get_spool_root(struct xxx_connection *client)
{
	gfs_server_put_reply(client, "get_spool_root",
	    GFS_ERROR_NOERROR, "s", gfarm_spool_root_get_for_compatibility());
}

void
gfs_server_statfs(struct xxx_connection *client)
{
	char *dir, *path;
	int save_errno = 0;
	gfarm_int32_t bsize;
	file_offset_t blocks, bfree, bavail, files, ffree, favail;
	struct stat st;
	char *msg = "statfs";

	gfs_server_get_request(client, msg, "s", &dir);

	/*
	 * XXX - currently, return the file system information having
	 * the most available space.
	 */
	local_path_for_write(dir, &path, msg);
	save_errno = gfsd_statfs(path, &bsize,
	    &blocks, &bfree, &bavail,
	    &files, &ffree, &favail);
	free(path);

	if (save_errno == 0 && stat(READONLY_CONFIG_FILE, &st) == 0) {
		/* pretend to be disk full, to make this gfsd read-only */
		bavail -= bfree;
		bfree = 0;
	}

	gfs_server_put_reply_with_errno(client, msg, save_errno,
	    "ioooooo", bsize, blocks, bfree, bavail, files, ffree, favail);
	check_input_output_error(msg, save_errno);
}

void
gfs_server_bulkread(struct xxx_connection *client)
{
	char *e;
	gfarm_int32_t fd;
	ssize_t rv;
	enum gfs_proto_error error = GFS_ERROR_NOERROR;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct gfs_client_rep_rate_info *rinfo = NULL;

	gfs_server_get_request(client, "bulkread", "i", &fd);

	if (file_read_size >= sizeof(buffer))
		file_read_size = sizeof(buffer);
	if (rate_limit != 0) {
		rinfo = gfs_client_rep_rate_info_alloc(rate_limit);
		if (rinfo == NULL)
			gflog_fatal("bulkread:rate_info_alloc: %s",
			    GFARM_ERR_NO_MEMORY);
	}

	fd = file_table_get(fd);
	do {
		rv = read(fd, buffer, file_read_size);
		if (rv <= 0) {
			if (rv == -1)
				error = gfs_errno_to_proto_error(errno);
			break;
		}
		e = xxx_proto_send(client, "b", rv, buffer);
		if (e != NULL) {
			error = gfs_string_to_proto_error(e);
			break;
		}
		if (file_read_size < GFS_PROTO_MAX_IOSIZE) {
			e = xxx_proto_flush(client);
			if (e != NULL) {
				error = gfs_string_to_proto_error(e);
				break;
			}
		}
		if (rate_limit != 0)
			gfs_client_rep_rate_control(rinfo, rv);
	} while (rv > 0);

	if (rinfo != NULL)
		gfs_client_rep_rate_info_free(rinfo);
	/* send EOF mark */
	e = xxx_proto_send(client, "b", 0, buffer);
	if (e != NULL && error == GFS_ERROR_NOERROR)
		error = gfs_string_to_proto_error(e);

	gfs_server_put_reply(client, "bulkread", error, "");
}

void
gfs_server_striping_read(struct xxx_connection *client)
{
	char *e;
	gfarm_int32_t fd, interleave_factor;
	file_offset_t offset, size, full_stripe_size;
	file_offset_t chunk_size;
	ssize_t rv;
	enum gfs_proto_error error = GFS_ERROR_NOERROR;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct gfs_client_rep_rate_info *rinfo = NULL;

	gfs_server_get_request(client, "striping_read", "iooio", &fd,
	    &offset, &size, &interleave_factor, &full_stripe_size);

	if (file_read_size >= sizeof(buffer))
		file_read_size = sizeof(buffer);
	if (rate_limit != 0) {
		rinfo = gfs_client_rep_rate_info_alloc(rate_limit);
		if (rinfo == NULL)
			gflog_fatal("striping_read:rate_info_alloc: %s",
			    GFARM_ERR_NO_MEMORY);
	}

	fd = file_table_get(fd);
	if (lseek(fd, (off_t)offset, SEEK_SET) == -1) {
		error = gfs_errno_to_proto_error(errno);
		goto finish;
	}
	for (;;) {
		chunk_size = interleave_factor == 0 || size < interleave_factor
		    ? size : interleave_factor;
		for (; chunk_size > 0; chunk_size -= rv, size -= rv) {
			rv = read(fd, buffer, chunk_size < file_read_size ?
			    chunk_size : file_read_size);
			if (rv <= 0) {
				if (rv == -1)
					error =
					    gfs_errno_to_proto_error(errno);
				goto finish;
			}
			e = xxx_proto_send(client, "b", rv, buffer);
			if (e != NULL) {
				error = gfs_string_to_proto_error(e);
				goto finish;
			}
			if (file_read_size < GFS_PROTO_MAX_IOSIZE) {
				e = xxx_proto_flush(client);
				if (e != NULL) {
					error = gfs_string_to_proto_error(e);
					goto finish;
				}
			}
			if (rate_limit != 0)
				gfs_client_rep_rate_control(rinfo, rv);
		}
		if (size <= 0)
			break;
		offset += full_stripe_size;
		if (lseek(fd, (off_t)offset, SEEK_SET) == -1) {
			error = gfs_errno_to_proto_error(errno);
			break;
		}
	}
 finish:
	if (rinfo != NULL)
		gfs_client_rep_rate_info_free(rinfo);
	/* send EOF mark */
	e = xxx_proto_send(client, "b", 0, buffer);
	if (e != NULL && error == GFS_ERROR_NOERROR)
		error = gfs_string_to_proto_error(e);

	gfs_server_put_reply(client, "striping_read", error, "");
}

void
gfs_server_replicate_file_sequential_common(struct xxx_connection *client,
	char *file, gfarm_int32_t mode,
	char *src_canonical_hostname, char *src_if_hostname)
{
	char *e, *path;
	struct gfs_connection *src_conn;
	int fd, src_fd;
	long file_sync_rate;
	enum gfs_proto_error error = GFS_ERROR_NOERROR;
	struct hostent *hp;
	struct sockaddr_in peer_addr;

	hp = gethostbyname(src_if_hostname);
	free(src_if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET) {
		e = GFARM_ERR_UNKNOWN_HOST;
	} else {
		memset(&peer_addr, 0, sizeof(peer_addr));
		memcpy(&peer_addr.sin_addr, hp->h_addr,
		       sizeof(peer_addr.sin_addr));
		peer_addr.sin_family = hp->h_addrtype;
		peer_addr.sin_port = htons(gfarm_spool_server_port);

		e = gfarm_netparam_config_get_long(
		    &gfarm_netparam_file_sync_rate,
		    src_canonical_hostname, (struct sockaddr *)&peer_addr,
		    &file_sync_rate);
		if (e != NULL) /* shouldn't happen */
			gflog_warning("file_sync_rate: %s", e);

		/*
		 * the following gfs_client_connect() accesses user & home
		 * information which was set in gfarm_authorize()
		 * with switch_to==1.
		 */
		e = gfs_client_connect(
		    src_canonical_hostname, (struct sockaddr *)&peer_addr,
		    &src_conn);
	}
	free(src_canonical_hostname);
	if (e != NULL) {
		error = gfs_string_to_proto_error(e);
		gflog_warning("replicate_file_seq:remote_connect: %s", e);
	} else {
		e = gfs_client_open(src_conn, file, GFARM_FILE_RDONLY, 0,
				    &src_fd);
		if (e != NULL) {
			error = gfs_string_to_proto_error(e);
			gflog_warning("replicate_file_seq:remote_open: %s", e);
		} else {
			e = gfarm_spool_path_localize(file, &path);
			if (e != NULL)
				fatal("replicate_file_seq:path", e);
			fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
			if (fd < 0) {
				error = gfs_errno_to_proto_error(errno);
				gflog_warning_errno(
				    "replicate_file_seq:local_open");
			} else {
				e = gfs_client_copyin(src_conn, src_fd, fd,
				    file_sync_rate);
				if (e != NULL) {
					error = gfs_string_to_proto_error(e);
					gflog_warning(
					    "replicate_file_seq:copyin: %s",e);
				}
				close(fd);
			}
			e = gfs_client_close(src_conn, src_fd);
			free(path);
		}
		gfs_client_disconnect(src_conn);
	}
	free(file);

	gfs_server_put_reply(client, "replicate_file_seq", error, "");
}

/* obsolete interafce, keeped for backward compatibility */
void
gfs_server_replicate_file_sequential_old(struct xxx_connection *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;

	gfs_server_get_request(client, "replicate_file_seq_old",
	    "sis", &file, &mode, &src_if_hostname);

	src_canonical_hostname = strdup(src_if_hostname);
	if (src_canonical_hostname == NULL) {
		gfs_server_put_reply(client, "replicate_file_seq_old",
		    GFS_ERROR_NOMEM, "");
		return;
	}
	gfs_server_replicate_file_sequential_common(client, file, mode,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_replicate_file_sequential(struct xxx_connection *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;

	gfs_server_get_request(client, "replicate_file_seq",
	    "siss", &file, &mode, &src_canonical_hostname, &src_if_hostname);

	gfs_server_replicate_file_sequential_common(client, file, mode,
	    src_canonical_hostname, src_if_hostname);
}

int iosize_alignment = 4096;
int iosize_minimum_division = 65536;

struct parallel_stream {
	struct gfs_connection *src_conn;
	int src_fd;
	enum { GSRFP_COPYING, GSRFP_FINISH } state;
};

char *
simple_division(int ofd, struct parallel_stream *divisions,
	off_t file_size, int n)
{
	char *e, *e_save = NULL;
	file_offset_t offset = 0, residual = file_size;
	file_offset_t size_per_division = file_offset_floor(file_size / n);
	int i;

	if (file_offset_floor(size_per_division / iosize_alignment) *
	    iosize_alignment != size_per_division) {
		size_per_division = (file_offset_floor(
		    size_per_division / iosize_alignment) + 1) *
		    iosize_alignment;
	}

	for (i = 0; i < n; i++) {
		file_offset_t size;

		if (residual <= 0 || e_save != NULL) {
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		size = residual <= size_per_division ?
		    residual : size_per_division;
		e = gfs_client_striping_copyin_request(
		    divisions[i].src_conn, divisions[i].src_fd, ofd,
		    offset, size, 0, 0);
		offset += size_per_division;
		residual -= size;
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			gflog_warning("replicate_file_division:copyin: %s", e);
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		divisions[i].state = GSRFP_COPYING;
	}
	return (e_save);
}

char *
striping(int ofd, struct parallel_stream *divisions,
	off_t file_size, int n, int interleave_factor)
{
	char *e, *e_save = NULL;
	file_offset_t full_stripe_size = (file_offset_t)interleave_factor * n;
	file_offset_t stripe_number = file_offset_floor(file_size /
	    full_stripe_size);
	file_offset_t size_per_division = interleave_factor * stripe_number;
	file_offset_t residual = file_size - full_stripe_size * stripe_number;
	file_offset_t chunk_number_on_last_stripe;
	file_offset_t last_chunk_size;
	file_offset_t offset = 0;
	int i;

	if (residual == 0) {
		chunk_number_on_last_stripe = 0;
		last_chunk_size = 0;
	} else {
		chunk_number_on_last_stripe = file_offset_floor(
		    residual / interleave_factor);
		last_chunk_size = residual - 
		    interleave_factor * chunk_number_on_last_stripe;
	}

	for (i = 0; i < n; i++) {
		file_offset_t size = size_per_division;

		if (i < chunk_number_on_last_stripe)
			size += interleave_factor;
		else if (i == chunk_number_on_last_stripe)
			size += last_chunk_size;
		if (size <= 0 || e_save != NULL) {
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		e = gfs_client_striping_copyin_request(
		    divisions[i].src_conn, divisions[i].src_fd, ofd,
		    offset, size, interleave_factor, full_stripe_size);
		offset += interleave_factor;
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			gflog_warning("replicate_file_stripe:copyin: %s", e);
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		divisions[i].state = GSRFP_COPYING;
	}
	return (e_save);
}

void
limit_division(int *ndivisionsp, file_offset_t file_size)
{
	int ndivisions = *ndivisionsp;

	/* do not divide too much */
	if (ndivisions > file_size / iosize_minimum_division) {
		ndivisions = file_size / iosize_minimum_division;
		if (ndivisions == 0)
			ndivisions = 1;
	}
	*ndivisionsp = ndivisions;
}

void
gfs_server_replicate_file_parallel_common(struct xxx_connection *client,
	char *file, gfarm_int32_t mode, file_offset_t file_size,
	gfarm_int32_t ndivisions, gfarm_int32_t interleave_factor,
	char *src_canonical_hostname, char *src_if_hostname)
{
	struct parallel_stream *divisions;
	char *e_save = NULL, *e, *path;
	long file_sync_rate, written;
	int i, j, n, ofd;
	enum gfs_proto_error error = GFS_ERROR_NOERROR;
	struct hostent *hp;
	struct sockaddr_in peer_addr;

	e = gfarm_spool_path_localize(file, &path);
	if (e != NULL)
		fatal("replicate_file_par", e);
	ofd = open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
	free(path);
	if (ofd == -1) {
		error = gfs_errno_to_proto_error(errno);
		gflog_warning_errno("replicate_file_par:local_open");
		goto finish;
	}

	limit_division(&ndivisions, file_size);

	GFARM_MALLOC_ARRAY(divisions, ndivisions);
	if (divisions == NULL) {
		error = GFS_ERROR_NOMEM;
		goto finish_ofd;
	}

	hp = gethostbyname(src_if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET) {
		error = GFS_ERROR_CONNREFUSED;
		goto finish_free_divisions;
	}
	memset(&peer_addr, 0, sizeof(peer_addr));
	memcpy(&peer_addr.sin_addr, hp->h_addr, sizeof(peer_addr.sin_addr));
	peer_addr.sin_family = hp->h_addrtype;
	peer_addr.sin_port = htons(gfarm_spool_server_port);

	e = gfarm_netparam_config_get_long(&gfarm_netparam_file_sync_rate,
	    src_canonical_hostname, (struct sockaddr *)&peer_addr,
	    &file_sync_rate);
	if (e != NULL) /* shouldn't happen */
		gflog_warning("file_sync_rate: %s", e);

	/* XXX - this should be done in parallel rather than sequential */
	for (i = 0; i < ndivisions; i++) {

		e = gfs_client_connect(
		    src_canonical_hostname, (struct sockaddr *)&peer_addr,
		    &divisions[i].src_conn);
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			gflog_warning("replicate_file_par:remote_connect: %s",
			    e);
			break;
		}
	}
	n = i;
	if (n == 0) {
		error = gfs_string_to_proto_error(e_save);
		goto finish_free_divisions;
	}
	e_save = NULL; /* not fatal */

	/* XXX - this should be done in parallel rather than sequential */
	for (i = 0; i < n; i++) {
		e = gfs_client_open(divisions[i].src_conn, file,
		    GFARM_FILE_RDONLY, 0, &divisions[i].src_fd);
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			gflog_warning("replicate_file_par:remote_open: %s", e);

			/*
			 * XXX - this should be done in parallel
			 * rather than sequential
			 */
			for (j = i; j < n; j++)
				gfs_client_disconnect(divisions[j].src_conn);
			n = i;
			break;
		}
	}
	if (n == 0) {
		error = gfs_string_to_proto_error(e_save);
		goto finish_free_divisions;
	}
	e_save = NULL; /* not fatal */

	if (interleave_factor == 0) {
		e = simple_division(ofd, divisions, file_size, n);
	} else {
		e = striping(ofd, divisions, file_size, n, interleave_factor);
	}
	e_save = e;

	written = 0;
	/*
	 * XXX - we cannot stop here, even if e_save != NULL,
	 * because currently there is no way to cancel
	 * striping_copyin request.
	 */
	for (;;) {
		int max_fd, fd, nfound, rv;
		fd_set readable;

		FD_ZERO(&readable);
		max_fd = -1;
		for (i = 0; i < n; i++) {
			if (divisions[i].state != GSRFP_COPYING)
				continue;
			fd = gfs_client_connection_fd(divisions[i].src_conn);
			/* XXX - prevent this happens */
			if (fd >= FD_SETSIZE) {
				fatal("replicate_file_par",
				    "too big file descriptor");
			}
			FD_SET(fd, &readable);
			if (max_fd < fd)
				max_fd = fd;
		}
		if (max_fd == -1)
			break;
		nfound = select(max_fd + 1, &readable, NULL, NULL, NULL);
		if (nfound <= 0) {
			if (nfound == -1 && errno != EINTR && errno != EAGAIN)
				gflog_warning_errno(
				    "replicate_file_par:select");
			continue;
		}
		for (i = 0; i < n; i++) {
			if (divisions[i].state != GSRFP_COPYING)
				continue;
			fd = gfs_client_connection_fd(divisions[i].src_conn);
			if (!FD_ISSET(fd, &readable))
				continue;
			e = gfs_client_striping_copyin_partial(
			    divisions[i].src_conn, &rv);
			if (e != NULL) {
				if (e_save == NULL)
					e_save = e;
				divisions[i].state = GSRFP_FINISH; /* XXX */
			} else if (rv == 0) {
				divisions[i].state = GSRFP_FINISH;
				e = gfs_client_striping_copyin_result(
				    divisions[i].src_conn);
				if (e != NULL) {
					if (e_save == NULL)
						e_save = e;
				} else {
					e = gfs_client_close(
					    divisions[i].src_conn,
					    divisions[i].src_fd);
					if (e_save == NULL)
						e_save = e;
				}
			} else if (file_sync_rate != 0) {
				written += rv;
				if (written >= file_sync_rate) {
					written -= file_sync_rate;
#ifdef HAVE_FDATASYNC
					fdatasync(ofd);
#else
					fsync(ofd);
#endif
				}
			}
			if (--nfound <= 0)
				break;
		}
	}

	/* XXX - this should be done in parallel rather than sequential */
	for (i = 0; i < n; i++) {
		e = gfs_client_disconnect(divisions[i].src_conn);
		if (e_save == NULL)
			e_save = e;
	}
	if (e_save != NULL)
		error = gfs_string_to_proto_error(e_save);

finish_free_divisions:
	free(divisions);
finish_ofd:
	close(ofd);
finish:
	free(file);
	free(src_canonical_hostname);
	free(src_if_hostname);
	gfs_server_put_reply(client, "replicate_file_par", error, "");
}

/* obsolete interafce, keeped for backward compatibility */
void
gfs_server_replicate_file_parallel_old(struct xxx_connection *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;
	gfarm_int32_t ndivisions; /* parallel_streams */
	gfarm_int32_t interleave_factor; /* stripe_unit_size, chuck size */
	file_offset_t file_size;

	gfs_server_get_request(client, "replicate_file_par_old", "sioiis",
	    &file, &mode, &file_size, &ndivisions, &interleave_factor,
	    &src_if_hostname);

	src_canonical_hostname = strdup(src_if_hostname);
	if (src_canonical_hostname == NULL) {
		gfs_server_put_reply(client, "replicate_file_par_old",
		    GFS_ERROR_NOMEM, "");
		return;
	}
	gfs_server_replicate_file_parallel_common(client,
	    file, mode, file_size, ndivisions, interleave_factor,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_replicate_file_parallel(struct xxx_connection *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;
	gfarm_int32_t ndivisions; /* parallel_streams */
	gfarm_int32_t interleave_factor; /* stripe_unit_size, chuck size */
	file_offset_t file_size;

	gfs_server_get_request(client, "replicate_file_par", "sioiiss",
	    &file, &mode, &file_size, &ndivisions, &interleave_factor,
	    &src_canonical_hostname, &src_if_hostname);

	gfs_server_replicate_file_parallel_common(client,
	    file, mode, file_size, ndivisions, interleave_factor,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_chdir(struct xxx_connection *client)
{
	char *gpath, *path;
	int save_errno = 0;
	char *msg = "chdir";

	gfs_server_get_request(client, msg, "s", &gpath);

	local_path(gpath, &path, msg);
	if (chdir(path) == -1)
		save_errno = errno;
	free(path);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

struct gfs_server_command_context {
	struct gfarm_iobuffer *iobuffer[NFDESC];

	enum { GFS_COMMAND_SERVER_STATE_NEUTRAL,
		       GFS_COMMAND_SERVER_STATE_OUTPUT,
		       GFS_COMMAND_SERVER_STATE_EXITED }
		server_state;
	int server_output_fd;
	int server_output_residual;
	enum { GFS_COMMAND_CLIENT_STATE_NEUTRAL,
		       GFS_COMMAND_CLIENT_STATE_OUTPUT }
		client_state;
	int client_output_residual;

	int pid;
	int exited_pid;
	int status;
} server_command_context;

#define COMMAND_IS_RUNNING()	(server_command_context.exited_pid == 0)

volatile sig_atomic_t sigchld_jmp_needed = 0;
sigjmp_buf sigchld_jmp_buf;

void
sigchld_handler(int sig)
{
	int pid, status, save_errno = errno;

	for (;;) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid == -1 || pid == 0)
			break;
		server_command_context.exited_pid = pid;
		server_command_context.status = status;
	}
	errno = save_errno;
	if (sigchld_jmp_needed) {
		sigchld_jmp_needed = 0;
		siglongjmp(sigchld_jmp_buf, 1);
	}
}

void
fatal_command(char *message, char *status)
{
	struct gfs_server_command_context *cc = &server_command_context;

	/* "-" is to send it to the process group */
	kill(-cc->pid, SIGTERM);

	fatal_proto(message, status);
}

char *
gfs_server_command_fd_set(struct xxx_connection *client,
			  fd_set *readable, fd_set *writable, int *max_fdp)
{
	struct gfs_server_command_context *cc = &server_command_context;
	int conn_fd = xxx_connection_fd(client);
	int i, fd;

	/*
	 * The following test condition should just match with
	 * the i/o condition in gfs_server_command_io_fd_set(),
	 * otherwise unneeded busy wait happens.
	 */

	if (cc->client_state == GFS_COMMAND_CLIENT_STATE_NEUTRAL ||
	    (cc->client_state == GFS_COMMAND_CLIENT_STATE_OUTPUT &&
	     gfarm_iobuffer_is_readable(cc->iobuffer[FDESC_STDIN]))) {
		FD_SET(conn_fd, readable);
		if (*max_fdp < conn_fd)
			*max_fdp = conn_fd;
	}
	if ((cc->server_state == GFS_COMMAND_SERVER_STATE_NEUTRAL &&
	     (gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDERR]) ||
	      gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDOUT]) ||
	      !COMMAND_IS_RUNNING())) ||
	    cc->server_state == GFS_COMMAND_SERVER_STATE_OUTPUT) {
		FD_SET(conn_fd, writable);
		if (*max_fdp < conn_fd)
			*max_fdp = conn_fd;
	}

	if (COMMAND_IS_RUNNING() &&
	    gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDIN])) {
		fd = gfarm_iobuffer_get_write_fd(cc->iobuffer[FDESC_STDIN]);
		FD_SET(fd, writable);
		if (*max_fdp < fd)
			*max_fdp = fd;
	}

	for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
		if (gfarm_iobuffer_is_readable(cc->iobuffer[i])) {
			fd = gfarm_iobuffer_get_read_fd(cc->iobuffer[i]);
			FD_SET(fd, readable);
			if (*max_fdp < fd)
				*max_fdp = fd;
		}
	}
	return (NULL);
}

char *
gfs_server_command_io_fd_set(struct xxx_connection *client,
			     fd_set *readable, fd_set *writable)
{
	char *e;
	struct gfs_server_command_context *cc = &server_command_context;
	int i, fd, conn_fd = xxx_connection_fd(client);

	fd = gfarm_iobuffer_get_write_fd(cc->iobuffer[FDESC_STDIN]);
	if (FD_ISSET(fd, writable)) {
		assert(gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDIN]));
		gfarm_iobuffer_write(cc->iobuffer[FDESC_STDIN], NULL);
		e = gfarm_iobuffer_get_error(cc->iobuffer[FDESC_STDIN]);
		if (e != NULL) {
			/* just purge the content */
			gfarm_iobuffer_purge(cc->iobuffer[FDESC_STDIN], NULL);
			gflog_warning("command: abandon stdin: %s", e);
			gfarm_iobuffer_set_error(cc->iobuffer[FDESC_STDIN],
			    NULL);
		}
		if (gfarm_iobuffer_is_eof(cc->iobuffer[FDESC_STDIN])) {
			/*
			 * We need to use shutdown(2) instead of close(2) here,
			 * to make bash happy...
			 * At least on Solaris 9, getpeername(2) returns EINVAL
			 * if the opposite side of the socketpair is closed,
			 * and bash doesn't read ~/.bashrc in such case.
			 * Read the comment about socketpair(2) in
			 * gfs_server_command() too.
			 */
			shutdown(fd, SHUT_WR);
		}
	}

	for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
		fd = gfarm_iobuffer_get_read_fd(cc->iobuffer[i]);
		if (!FD_ISSET(fd, readable))
			continue;
		gfarm_iobuffer_read(cc->iobuffer[i], NULL);
		e = gfarm_iobuffer_get_error(cc->iobuffer[i]);
		if (e == NULL)
			continue;
		/* treat this as eof */
		gfarm_iobuffer_set_read_eof(cc->iobuffer[i]);
		gflog_warning("%s: %s", i == FDESC_STDOUT ?
		    "command: reading stdout" :
		    "command: reading stderr",
		     e);
		gfarm_iobuffer_set_error(cc->iobuffer[i], NULL);
	}

	if (FD_ISSET(conn_fd, readable) &&
	    cc->server_state != GFS_COMMAND_SERVER_STATE_EXITED) {
		if (cc->client_state == GFS_COMMAND_CLIENT_STATE_NEUTRAL) {
			gfarm_int32_t cmd, fd, len, sig;
			int eof;

			e = xxx_proto_recv(client, 1, &eof, "i", &cmd);
			if (e != NULL)
				fatal_command("command:client subcommand", e);
			if (eof)
				fatal_command("command:client subcommand",
				    "eof");
			switch (cmd) {
			case GFS_PROTO_COMMAND_EXIT_STATUS:
				fatal_command("command:client subcommand",
				    "unexpected exit_status");
				break;
			case GFS_PROTO_COMMAND_SEND_SIGNAL:
				e = xxx_proto_recv(client, 1, &eof, "i", &sig);
				if (e != NULL)
					fatal_command(
					    "command_send_signal", e);
				if (eof)
					fatal_command(
					    "command_send_signal", "eof");
				/* "-" is to send it to the process group */
				kill(-cc->pid, sig);
				break;
			case GFS_PROTO_COMMAND_FD_INPUT:
				e = xxx_proto_recv(client, 1, &eof,
						   "ii", &fd, &len);
				if (e != NULL)
					fatal_command("command_fd_input", e);
				if (eof)
					fatal_command("command_fd_input",
					    "eof");
				if (fd != FDESC_STDIN) {
					/* XXX - something wrong */
					fatal_command(
					    "command_fd_input", "fd");
				}
				if (len <= 0) {
					/* notify closed */
					gfarm_iobuffer_set_read_eof(
					    cc->iobuffer[FDESC_STDIN]);
				} else {
					cc->client_state =
					    GFS_COMMAND_CLIENT_STATE_OUTPUT;
					cc->client_output_residual = len;
				}
				break;
			default:
				/* XXX - something wrong */
				fatal_command("command_io",
					    "unknown subcommand");
				break;
			}
		} else if (cc->client_state==GFS_COMMAND_CLIENT_STATE_OUTPUT) {
			gfarm_iobuffer_read(cc->iobuffer[FDESC_STDIN],
				&cc->client_output_residual);
			if (cc->client_output_residual == 0)
				cc->client_state =
					GFS_COMMAND_CLIENT_STATE_NEUTRAL;
			e = gfarm_iobuffer_get_error(
			    cc->iobuffer[FDESC_STDIN]);
			if (e != NULL) {
				/* treat this as eof */
				gfarm_iobuffer_set_read_eof(
				    cc->iobuffer[FDESC_STDIN]);
				gflog_warning("command: receiving stdin: %s",
				    e);
				gfarm_iobuffer_set_error(
				    cc->iobuffer[FDESC_STDIN], NULL);
			}
			if (gfarm_iobuffer_is_read_eof(
					cc->iobuffer[FDESC_STDIN])) {
				fatal_command("command_fd_input_content",
				    "eof");
			}
		}
	}
	if (FD_ISSET(conn_fd, writable)) {
		if (cc->server_state == GFS_COMMAND_SERVER_STATE_NEUTRAL) {
			if (gfarm_iobuffer_is_writable(
				cc->iobuffer[FDESC_STDERR]) ||
			    gfarm_iobuffer_is_writable(
				cc->iobuffer[FDESC_STDOUT])) {
				if (gfarm_iobuffer_is_writable(
						cc->iobuffer[FDESC_STDERR]))
					cc->server_output_fd = FDESC_STDERR;
				else
					cc->server_output_fd = FDESC_STDOUT;
				/*
				 * cc->server_output_residual may be 0,
				 * if stdout or stderr is closed.
				 */
				cc->server_output_residual =
				    gfarm_iobuffer_avail_length(
					cc->iobuffer[cc->server_output_fd]);
				e = xxx_proto_send(client, "iii",
					GFS_PROTO_COMMAND_FD_OUTPUT,
					cc->server_output_fd,
					cc->server_output_residual);
				if (e != NULL ||
				    (e = xxx_proto_flush(client)) != NULL)
					fatal_command("command: fd_output", e);
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_OUTPUT;
			} else if (!COMMAND_IS_RUNNING()) {
				e = xxx_proto_send(client, "i",
					GFS_PROTO_COMMAND_EXITED);
				if (e != NULL ||
				    (e = xxx_proto_flush(client)) != NULL)
					fatal_proto("command: report exit", e);
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_EXITED;
			}
		} else if (cc->server_state==GFS_COMMAND_SERVER_STATE_OUTPUT) {
			gfarm_iobuffer_write(
				cc->iobuffer[cc->server_output_fd],
				&cc->server_output_residual);
			if (cc->server_output_residual == 0)
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_NEUTRAL;
			e = gfarm_iobuffer_get_error(
			    cc->iobuffer[cc->server_output_fd]);
			if (e != NULL) {
				fatal_command(
				    cc->server_output_fd == FDESC_STDOUT ?
				    "command: sending stdout" :
				    "command: sending stderr",
				    e);
			}
		}
	}
	return (NULL);
}

char *
gfs_server_command_io(struct xxx_connection *client, struct timeval *timeout)
{
	volatile int nfound;
	int max_fd, conn_fd = xxx_connection_fd(client);
	fd_set readable, writable;
	char *e;

	if (server_command_context.server_state ==
	    GFS_COMMAND_SERVER_STATE_EXITED)
		return (NULL);

	max_fd = -1;
	FD_ZERO(&readable);
	FD_ZERO(&writable);

	gfs_server_command_fd_set(client, &readable, &writable, &max_fd);

	/*
	 * We wait for either SIGCHLD or select(2) event here,
	 * and use siglongjmp(3) to avoid a race condition about the signal.
	 *
	 * The race condition happens, if the SIGCHLD signal is delivered
	 * after the if-statement which does FD_SET(conn_fd, writable) in
	 * gfs_server_command_fd_set(), and before the select(2) below.
	 * In that condition, the following select(2) may wait that the
	 * `conn_fd' becomes readable, and because it may never happan,
	 * it waits forever (i.e. both gfrcmd and gfsd wait each other
	 * forever), and hangs, unless there is the sigsetjmp()/siglongjmp()
	 * handling.
	 *
	 * If the SIGCHLD is delivered inside the select(2) system call,
	 * the problem doesn't happen, because select(2) will return
	 * with EINTR.
	 *
	 * Also, if the SIGCHLD is delivered before an EOF from either
	 * cc->iobuffer[FDESC_STDOUT] or cc->iobuffer[FDESC_STDERR],
	 * the problem doesn't happen, either. Because the select(2)
	 * will be woken up by the EOF. But actually the SIGCHLD is
	 * delivered after the EOF (of both FDESC_STDOUT and FDESC_STDERR,
	 * and even after the EOFs are reported to a client) at least
	 * on linux-2.4.21-pre4.
	 */
	nfound = -1; errno = EINTR;
	if (sigsetjmp(sigchld_jmp_buf, 1) == 0) {
		sigchld_jmp_needed = 1;
		/*
		 * Here, we have to wait until the `conn_fd' is writable,
		 * if this is !COMMAND_IS_RUNNING() case.
		 */
		if (COMMAND_IS_RUNNING() || FD_ISSET(conn_fd, &writable)) {
			nfound = select(max_fd + 1, &readable, &writable, NULL,
			    timeout);
		}
	}
	sigchld_jmp_needed = 0;

	if (nfound > 0)
		e = gfs_server_command_io_fd_set(client, &readable, &writable);
	else
		e = NULL;

	return (e);
}

char *
gfs_server_client_command_result(struct xxx_connection *client)
{
	struct gfs_server_command_context *cc = &server_command_context;
	gfarm_int32_t cmd, fd, len, sig;
	int finish, eof;
	char *e;

	while (cc->server_state != GFS_COMMAND_SERVER_STATE_EXITED)
		gfs_server_command_io(client, NULL);
	/*
	 * Because COMMAND_IS_RUNNING() must be false here,
	 * we don't have to call fatal_command() from now on.
	 */

	/*
	 * Now, we recover the connection file descriptor blocking mode.
	 */
	if (fcntl(xxx_connection_fd(client), F_SETFL, 0) == -1)
		gflog_warning("command-result:block: %s", strerror(errno));

	/* make cc->client_state neutral */
	if (cc->client_state == GFS_COMMAND_CLIENT_STATE_OUTPUT) {
		e = xxx_proto_purge(client, 0, cc->client_output_residual);
		if (e != NULL)
			fatal_proto("command_fd_input:purge", e);
	}

	for (finish = 0; !finish; ) {
		e = xxx_proto_recv(client, 0, &eof, "i", &cmd);
		if (e != NULL)
			fatal_proto("command:client subcommand", e);
		if (eof)
			fatal_proto("command:client subcommand", "eof");
		switch (cmd) {
		case GFS_PROTO_COMMAND_EXIT_STATUS:
			finish = 1;
			break;
		case GFS_PROTO_COMMAND_SEND_SIGNAL:
			e = xxx_proto_recv(client, 0, &eof, "i", &sig);
			if (e != NULL)
				fatal_proto("command_send_signal", e);
			if (eof)
				fatal_proto("command_send_signal","eof");
			break;
		case GFS_PROTO_COMMAND_FD_INPUT:
			e = xxx_proto_recv(client, 0, &eof, "ii", &fd, &len);
			if (e != NULL)
				fatal_proto("command_fd_input", e);
			if (eof)
				fatal_proto("command_fd_input", "eof");
			if (fd != FDESC_STDIN) {
				/* XXX - something wrong */
				fatal_proto("command_fd_input", "fd");
			}
			e = xxx_proto_purge(client, 0, len);
			if (e != NULL)
				fatal_proto("command_fd_input:purge", e);
			break;
		default:
			/* XXX - something wrong */
			fatal_proto("command_io", "unknown subcommand");
			break;
		}
	}
	gfs_server_put_reply(client, "command:exit_status",
	    GFS_ERROR_NOERROR, "iii",
	    WIFEXITED(cc->status) ? 0 : WTERMSIG(cc->status),
	    WIFEXITED(cc->status) ? WEXITSTATUS(cc->status) : 0,
	    WIFEXITED(cc->status) ? 0 : WCOREDUMP(cc->status));
	return (NULL);
}

void
gfs_server_command(struct xxx_connection *client, char *cred_env)
{
	struct gfs_server_command_context *cc = &server_command_context;
	gfarm_int32_t argc, argc_opt, nenv, flags, error;
	char *path, *command, **argv_storage = NULL, **argv = NULL;
	char **envp, *xauth;
	int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
	int conn_fd = xxx_connection_fd(client);
	int i, eof;
	socklen_t siz;
	char *e = NULL;

	char *user, *home, *shell;
	char *user_env, *home_env, *shell_env, *xauth_env; /* cred_end */
	static char user_format[] = "USER=%s";
	static char home_format[] = "HOME=%s";
	static char shell_format[] = "SHELL=%s";
	static char path_env[] = GFARM_DEFAULT_PATH;
#define N_EXTRA_ENV	4	/* user_env, home_env, shell_env, path_env */
	int use_cred_env = cred_env != NULL ? 1 : 0;

	static char xauth_format[] = "XAUTHORITY=%s";
	static char xauth_template[] = "/tmp/.xaXXXXXX";
	static char xauth_filename[sizeof(xauth_template)];
	int use_xauth_env = 0;
	size_t size;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	envp = NULL;
	user_env = home_env =shell_env = xauth_env = NULL;
#endif
	gfs_server_get_request(client, "command", "siii",
			       &path, &argc, &nenv, &flags);
	argc_opt = flags & GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND ? 2 : 0;
	/* 2 for "$SHELL" + "-c" */

	size = gfarm_size_add(&overflow, argc, argc_opt + 1);
	if (!overflow)
		GFARM_MALLOC_ARRAY(argv_storage, size);
	if (overflow || argv_storage == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto rpc_error;
	}
	argv = argv_storage + argc_opt;
	if ((flags & GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY) != 0)
		use_xauth_env = 1;
	size = gfarm_size_add(&overflow, nenv, 
			N_EXTRA_ENV + use_cred_env + use_xauth_env + 1);
	if (!overflow)
		GFARM_MALLOC_ARRAY(envp, size);
	if (overflow || envp == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_argv;
	}

	user = gfarm_get_local_username();
	home = gfarm_get_local_homedir();
	GFARM_MALLOC_ARRAY(user_env, sizeof(user_format) + strlen(user));
	if (user_env == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_envp;
	}
	sprintf(user_env, user_format, user);

	GFARM_MALLOC_ARRAY(home_env, sizeof(home_format) + strlen(home));
	if (home_env == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_user_env;
	}
	sprintf(home_env, home_format, home);

	shell = getpwnam(user)->pw_shell; /* XXX - this shouldn't fail */
	if (*shell == '\0')
		shell = PATH_BSHELL;

	if ((flags & GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND) == 0) {
		/*
		 * SECURITY.
		 * disallow anyone who doesn't have a standard shell.
		 */
		char *s;

		while ((s = getusershell()) != NULL)
			if (strcmp(s, shell) == 0)
				break;
		endusershell();
		if (s == NULL) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			goto free_home_env;
		}
	}

	GFARM_MALLOC_ARRAY(shell_env, sizeof(shell_format) + strlen(shell));
	if (shell_env == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_home_env;
	}
	sprintf(shell_env, shell_format, shell);

	argv[argc] = envp[nenv + N_EXTRA_ENV + use_cred_env + use_xauth_env] =
	    NULL;
	envp[nenv + 0] = user_env;
	envp[nenv + 1] = home_env;
	envp[nenv + 2] = shell_env;
	envp[nenv + 3] = path_env;

	for (i = 0; i < argc; i++) {
		e = xxx_proto_recv(client, 0, &eof, "s", &argv[i]);
		if (e != NULL || eof) {
			while (--i >= 0)
				free(argv[i]);
			goto free_shell_env;
		}
	}
	for (i = 0; i < nenv; i++) {
		e = xxx_proto_recv(client, 0, &eof, "s", &envp[i]);
		if (e != NULL || eof) {
			while (--i >= 0)
				free(envp[i]);
			goto free_argv_array;
		}
	}
	if ((flags & GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND) != 0) {
		argv_storage[0] = shell;
		argv_storage[1] = "-c";
		argv[1] = NULL; /* ignore argv[1 ... argc - 1] in this case. */
		command = shell;
	} else {
		command = path;
	}
	if (use_cred_env)
		envp[nenv + N_EXTRA_ENV] = cred_env;
	if (use_xauth_env) {
		static char xauth_command_format[] =
			"%s %s nmerge - 2>/dev/null";
		char *xauth_command;
		FILE *fp;
		int xauth_fd;

		e = xxx_proto_recv(client, 0, &eof, "s", &xauth);
		if (e != NULL || eof)
			goto free_envp_array;

		/*
		 * don't touch $HOME/.Xauthority to avoid lock contention
		 * on NFS home. (Is this really better? XXX)
		 */
		xauth_fd = mkstemp(strcpy(xauth_filename, xauth_template));
		if (xauth_fd == -1)
			goto free_xauth;
		close(xauth_fd);

		GFARM_MALLOC_ARRAY(xauth_env,
		    sizeof(xauth_format) + sizeof(xauth_filename));
		if (xauth_env == NULL)
			goto remove_xauth;
		sprintf(xauth_env, xauth_format, xauth_filename);
		envp[nenv + N_EXTRA_ENV + use_cred_env] = xauth_env;

		GFARM_MALLOC_ARRAY(xauth_command,
				   sizeof(xauth_command_format) +
				   strlen(xauth_env) +
				   strlen(XAUTH_COMMAND));
		if (xauth_command == NULL)
			goto free_xauth_env;
		sprintf(xauth_command, xauth_command_format,
			xauth_env, XAUTH_COMMAND);
		if ((fp = popen(xauth_command, "w")) == NULL)
			goto free_xauth_env;
		fputs(xauth, fp);
		pclose(fp);
		free(xauth_command);
	}
#if 1	/*
	 * The reason why we use socketpair(2) instead of pipe(2) is
	 * to make bash read ~/.bashrc. Because the condition that
	 * bash reads it is as follows:
	 *   1. $SSH_CLIENT/$SSH2_CLIENT is set, or stdin is a socket.
	 * and
	 *   2. $SHLVL < 2
	 * This condition that bash uses is broken, for example, this
	 * doesn't actually work with Sun's variant of OpenSSH on Solaris 9.
	 *
	 * Read the comment about shutdown(2) in gfs_server_command_io_fd_set()
	 * too.
	 * Honestly, people should use zsh instead of bash.
	 */
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, stdin_pipe) == -1)
#else
	if (pipe(stdin_pipe) == -1)
#endif
	{
		e = gfarm_errno_to_error(errno);
		goto free_xauth_env;
	}
	if (pipe(stdout_pipe) == -1) {
		e = gfarm_errno_to_error(errno);
		goto close_stdin_pipe;
	}
	if (pipe(stderr_pipe) == -1) {
		e = gfarm_errno_to_error(errno);
		goto close_stdout_pipe;
	}
	cc->pid = cc->exited_pid = 0;
	if ((cc->pid = fork()) == 0) {
		struct rlimit rlim;

		/*
		 * XXX - Some Linux distributions set coredump size 0
		 *	 by default.
		 */
		if (getrlimit(RLIMIT_CORE, &rlim) != -1) {
			rlim.rlim_cur = rlim.rlim_max;
			setrlimit(RLIMIT_CORE, &rlim);
		}

		/* child */
		dup2(stdin_pipe[0], 0);
		dup2(stdout_pipe[1], 1);
		dup2(stderr_pipe[1], 2);
		close(stderr_pipe[1]);
		close(stderr_pipe[0]);
		close(stdout_pipe[1]);
		close(stdout_pipe[0]);
		close(stdin_pipe[1]);
		close(stdin_pipe[0]);
		/* close client connection, syslog and other sockets */
		for (i = 3; i < stderr_pipe[1]; i++)
			close(i);
		/* re-install default signal handler (see main) */
		signal(SIGPIPE, SIG_DFL);
		/*
		 * create a process group
		 * to make it possible to send a signal later
		 */
		setpgid(0, getpid());
		umask(command_umask);
		execve(command, argv_storage, envp);
		fprintf(stderr, "%s: ", gfarm_host_get_self_name());
		perror(path);
		_exit(1);
	} else if (cc->pid == -1) {
		e = gfarm_errno_to_error(errno);
		goto close_stderr_pipe;
	}
	close(stderr_pipe[1]);
	close(stdout_pipe[1]);
	close(stdin_pipe[0]);
	error = GFS_ERROR_NOERROR;
	goto rpc_reply;

close_stderr_pipe:
	close(stderr_pipe[0]);
	close(stderr_pipe[1]);
close_stdout_pipe:
	close(stdout_pipe[0]);
	close(stdout_pipe[1]);
close_stdin_pipe:
	close(stdin_pipe[0]);
	close(stdin_pipe[1]);
free_xauth_env:
	if (use_xauth_env)
		free(xauth_env);
remove_xauth:
	if (use_xauth_env)
		unlink(xauth_filename);
free_xauth:
	if (use_xauth_env)
		free(xauth);
free_envp_array:
	for (i = 0; i < nenv; i++)
		free(envp[i]);
free_argv_array:
	for (i = 0; i < argc; i++)
		free(argv[i]);
free_shell_env:
	free(shell_env);
free_home_env:
	free(home_env);
free_user_env:
	free(user_env);
free_envp:
	free(envp);
free_argv:
	free(argv_storage);
rpc_error:
	free(path);
	error = gfs_string_to_proto_error(e);
rpc_reply:
	gfs_server_put_reply(client, "command-start", error, "i", cc->pid);
	xxx_proto_flush(client);
	if (error != GFS_ERROR_NOERROR)
		return;

	/*
	 * Now, we set the connection file descriptor non-blocking mode.
	 */
	if (fcntl(conn_fd, F_SETFL, O_NONBLOCK) == -1) /* shouldn't fail */
		gflog_warning("command-start:nonblock: %s", strerror(errno));

	siz = sizeof(i);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_RCVBUF, &i, &siz))
		i = GFARM_DEFAULT_COMMAND_IOBUF_SIZE;
	cc->iobuffer[FDESC_STDIN] = gfarm_iobuffer_alloc(i);

	siz = sizeof(i);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, &i, &siz))
		i = GFARM_DEFAULT_COMMAND_IOBUF_SIZE;
	cc->iobuffer[FDESC_STDOUT] = gfarm_iobuffer_alloc(i);
	cc->iobuffer[FDESC_STDERR] = gfarm_iobuffer_alloc(i);

	/*
	 * It's safe to use gfarm_iobuffer_set_nonblocking_write_fd()
	 * instead of gfarm_iobuffer_set_nonblocking_write_socket() here,
	 * because we always ignore SIGPIPE in gfsd.
	 * cf. gfarm_sigpipe_ignore() in main().
	 */
	gfarm_iobuffer_set_nonblocking_read_xxx(
		cc->iobuffer[FDESC_STDIN], client);
	gfarm_iobuffer_set_nonblocking_write_fd(
		cc->iobuffer[FDESC_STDIN], stdin_pipe[1]);

	gfarm_iobuffer_set_nonblocking_read_fd(
		cc->iobuffer[FDESC_STDOUT], stdout_pipe[0]);
	gfarm_iobuffer_set_nonblocking_write_xxx(
		cc->iobuffer[FDESC_STDOUT], client);

	gfarm_iobuffer_set_nonblocking_read_fd(
		cc->iobuffer[FDESC_STDERR], stderr_pipe[0]);
	gfarm_iobuffer_set_nonblocking_write_xxx(
		cc->iobuffer[FDESC_STDERR], client);

	while (cc->server_state != GFS_COMMAND_SERVER_STATE_EXITED)
		gfs_server_command_io(client, NULL);

	gfs_server_client_command_result(client);

	/*
	 * clean up
	 */

	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDIN]);
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDOUT]);
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDERR]);

	close(stderr_pipe[0]);
	close(stdout_pipe[0]);
	close(stdin_pipe[1]);
	if (use_xauth_env) {
		free(xauth_env);
		unlink(xauth_filename);
		free(xauth);
	}
	for (i = 0; i < nenv; i++)
		free(envp[i]);
	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(shell_env);
	free(home_env);
	free(user_env);
	free(envp);
	free(argv_storage);
	free(path);
}

void
server(int client_fd)
{
	char *e;
#if 0
	char *user, *host, *aux;
#endif
	struct xxx_connection *client;
	int eof;
	gfarm_int32_t request;

	e = xxx_socket_connection_new(client_fd, &client);
	if (e != NULL) {
		close(client_fd);
		gflog_fatal("xxx_connection_new: %s", e);
	}
#if 1
	/*
	 * The following function switches deamon's privilege
	 * to the authenticated user.
	 * This also enables gfarm_get_global_username() and
	 * gfarm_get_local_homedir() which are necessary for
	 * gfs_client_connect() called from gfs_server_replicate_file().
	 */
	e = gfarm_authorize(client, 1, GFS_SERVICE_TAG, NULL, NULL, NULL);
#else
	e = gfarm_authorize(client, 0, GFS_SERVICE_TAG, &user, &host, NULL);
	if (e != NULL)
		gflog_fatal("gfarm_authorize: %s", e);

	gfarm_set_global_username(user);
	size = gfarm_size_add(&overflow, strlen(user) + 1, strlen(host) + 1);
	if (!overflow)
		GFARM_MALLOC_ARRAY(aux, size);
	if (overflow || aux == NULL)
		gflog_fatal("set_auxiliary_info: %s", GFARM_ERR_NO_MEMORY);
	sprintf(aux, "%s@%s", user, host);
	gflog_set_auxiliary_info(aux);
#endif
	/* set file creation mask */
	command_umask = umask(0);

	for (;;) {
		e = xxx_proto_recv(client, 0, &eof, "i", &request);
		if (e != NULL)
			fatal_proto("request number", e);
		if (eof) {
			cleanup_service();
			/*
			 * NOTE: cleanup_service() acceses `client' via
			 * variable `credential_exported'.
			 * Thus, xxx_connection_free() must be done after that.
			 */
			e = xxx_connection_free(client);
			if (e != NULL)
				gflog_error("%s", e);
			exit(0);
		}
		switch (request) {
		case GFS_PROTO_OPEN:	gfs_server_open(client); break;
		case GFS_PROTO_OPEN_LOCAL: gfs_server_open_local(client); break;
		case GFS_PROTO_CLOSE:	gfs_server_close(client); break;
		case GFS_PROTO_SEEK:	gfs_server_seek(client); break;
		case GFS_PROTO_FTRUNCATE:
			gfs_server_ftruncate(client); break;
		case GFS_PROTO_READ:	gfs_server_read(client); break;
		case GFS_PROTO_WRITE:	gfs_server_write(client); break;
		case GFS_PROTO_UNLINK:	gfs_server_unlink(client); break;
		case GFS_PROTO_MKDIR:	gfs_server_mkdir(client); break;
		case GFS_PROTO_RMDIR:	gfs_server_rmdir(client); break;
		case GFS_PROTO_CHMOD:	gfs_server_chmod(client); break;
		case GFS_PROTO_CHGRP:	gfs_server_chgrp(client); break;
		case GFS_PROTO_FSTAT:	gfs_server_fstat(client); break;
		case GFS_PROTO_EXIST:	gfs_server_exist(client); break;
		case GFS_PROTO_DIGEST:	gfs_server_digest(client); break;
		case GFS_PROTO_GET_SPOOL_ROOT:
			gfs_server_get_spool_root(client); break;
		case GFS_PROTO_BULKREAD:
			gfs_server_bulkread(client); break;
		case GFS_PROTO_STRIPING_READ:
			gfs_server_striping_read(client); break;
		case GFS_PROTO_REPLICATE_FILE_SEQUENTIAL_OLD:
			gfs_server_replicate_file_sequential_old(client);break;
		case GFS_PROTO_REPLICATE_FILE_PARALLEL_OLD:
			gfs_server_replicate_file_parallel_old(client); break;
		case GFS_PROTO_REPLICATE_FILE_SEQUENTIAL:
			gfs_server_replicate_file_sequential(client); break;
		case GFS_PROTO_REPLICATE_FILE_PARALLEL:
			gfs_server_replicate_file_parallel(client); break;
		case GFS_PROTO_CHDIR:	gfs_server_chdir(client); break;
		case GFS_PROTO_COMMAND:
			if (credential_exported == NULL) {
				e = xxx_connection_export_credential(client);
				if (e == NULL)
					credential_exported = client;
				else
					gflog_warning(
					    "export delegated credential: %s",
					    e);
			}
			gfs_server_command(client,
			    credential_exported == NULL ? NULL :
			    xxx_connection_env_for_credential(client));
			break;
		case GFS_PROTO_RENAME:	gfs_server_rename(client); break;
		case GFS_PROTO_LINK:	gfs_server_link(client); break;
		case GFS_PROTO_STATFS:	gfs_server_statfs(client); break;
		case GFS_PROTO_FSYNC:	gfs_server_fsync(client); break;
		default:
			gflog_warning("unknown request %d", (int)request);
			cleanup_service();
			exit(1);
		}
	}
}

static void
check_spool_directory()
{
	int fd;
	ssize_t rv;

	fd = open(IO_SANITY_CHECK_FILE, O_CREAT|O_WRONLY, 0666);
	if (fd == -1)
		accepting_fatal_errno("creat(2) test");
	rv = write(fd, "X", 1);
	if (rv == -1) {
		/* accept "no space left on device" */
		if (errno == ENOSPC)
			gflog_warning_errno("write(2) test");
		else
			accepting_fatal_errno("write(2) test");
	}
	if (rv == 0)
		accepting_fatal("write(2) returned 0");
	if (fsync(fd) == -1)
		accepting_fatal_errno("fsync(2) test");
	if (close(fd) == -1)
		accepting_fatal_errno("close(2) test");
	if (unlink(IO_SANITY_CHECK_FILE) == -1)
		accepting_fatal_errno("unlink(2) test");
}

static int accepting_inet_sock, *accepting_unix_socks, *datagram_socks;
static int datagram_socks_count;

void
start_server(int sock)
{
	struct sockaddr_un client_addr;
	socklen_t client_addr_size;
	int client, i;
	char *e;

	client_addr_size = sizeof(client_addr);
	client = accept(sock,
		(struct sockaddr *)&client_addr, &client_addr_size);
	if (client < 0) {
		if (errno == EINTR || errno == ECONNABORTED ||
#ifdef EPROTO
		    errno == EPROTO ||
#endif
		    errno == EAGAIN)
			return;
		accepting_fatal_errno("accept");
	}
	/* sanity check for io error in a spool directory */
	/* XXX - need to check all spool directories */
	check_spool_directory();

#ifndef GFSD_DEBUG
	switch (fork()) {
	case 0:
		/*
		 * NOTE: The following signals should match with signals that
		 * main() routine makes them call accepting_sigterm_handler().
		 * The reason why we won't make them call cleanup_service() is
		 * because xxx_connection_delete_credential() is not
		 * async signal safe.
		 */
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
#endif
		close(accepting_inet_sock);
		for (i = 0; i < accepting_unix_socks_count; i++)
			close(accepting_unix_socks[i]);
		for (i = 0; i < datagram_socks_count; i++)
			close(datagram_socks[i]);

		e = gfarm_netparam_config_get_long(
			&gfarm_netparam_file_read_size,
			NULL, (struct sockaddr *)&client_addr,
			&file_read_size);
		if (e != NULL) /* shouldn't happen */
			accepting_fatal("file_read_size: %s", e);

		e = gfarm_netparam_config_get_long(
			&gfarm_netparam_rate_limit,
			NULL, (struct sockaddr *)&client_addr,
			&rate_limit);
		if (e != NULL) /* shouldn't happen */
			accepting_fatal("rate_limit: %s", e);

		server(client);
		/*NOTREACHED*/
#ifndef GFSD_DEBUG
	case -1:
		gflog_warning_errno("fork");
		/*FALLTHROUGH*/
	default:
		close(client);
		break;
	}
#endif
}

void
datagram_server(int sock)
{
	int rv;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size = sizeof(client_addr);
	double loadavg[3];
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nloadavg[3];
#else
#	define nloadavg loadavg
#endif
	char buffer[1024];

	rv = recvfrom(sock, buffer, sizeof(buffer), 0,
	    (struct sockaddr *)&client_addr, &client_addr_size);
	if (rv == -1) {
		gflog_warning_errno("datagram_server: recvfrom");
		return;
	}
	rv = getloadavg(loadavg, GFARM_ARRAY_LENGTH(loadavg));
	if (rv == -1) {
		gflog_warning("datagram_server: cannot get load average");
		return;
	}
#ifndef WORDS_BIGENDIAN /* prototype of swab() uses (char *) on Solaris */
	swab((void *)&loadavg[0], (void *)&nloadavg[0], sizeof(nloadavg[0]));
	swab((void *)&loadavg[1], (void *)&nloadavg[1], sizeof(nloadavg[1]));
	swab((void *)&loadavg[2], (void *)&nloadavg[2], sizeof(nloadavg[2]));
#endif
	rv = sendto(sock, nloadavg, sizeof(nloadavg), 0,
	    (struct sockaddr *)&client_addr, sizeof(client_addr));
	if (rv == -1)
		gflog_warning_errno("datagram_server: %s %f",
		    inet_ntoa(client_addr.sin_addr), loadavg[0]);
	else
		gflog_debug("datagram_server: %s %f",
		    inet_ntoa(client_addr.sin_addr), loadavg[0]);
}

void
open_accepting_unix_domain(struct in_addr address, int port,
	int *sockp, char **sock_namep, char **sock_dirp)
{
	struct sockaddr_un self_addr;
	socklen_t self_addr_size;
	int sock;
	char *msg, *sock_name, *sock_dir;
	int save_errno;

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sun_family = AF_UNIX;
	snprintf(self_addr.sun_path, sizeof(self_addr.sun_path),
	    GFSD_LOCAL_SOCKET_NAME, inet_ntoa(address), port);
	self_addr_size = sizeof(self_addr);

	sock_name = strdup(self_addr.sun_path);
	sock_dir = gfarm_path_dir(sock_name);
	if (sock_name == NULL || sock_dir == NULL)
		accepting_fatal("not enough memory");
	/* to make sure */
	unlink(sock_name);
	rmdir(sock_dir);
	mkdir(sock_dir, 0755);

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		save_errno = errno;
		(void)rmdir(sock_dir);
		accepting_fatal("accepting unix socket: %s",
		    strerror(save_errno));
	}
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0) {
		msg = "bind accepting socket";
		goto error;
	}
	/* ensure access from every user */
	if (chmod(self_addr.sun_path, 0777) < 0) {
		msg = "chmod";
		goto error;
	}
	if (listen(sock, LISTEN_BACKLOG) < 0) {
		msg = "listen";
		goto error;
	}
	*sockp = sock;
	*sock_namep = sock_name;
	*sock_dirp = sock_dir;
	return;
error:
	save_errno = errno;
	(void)unlink(self_addr.sun_path);
	(void)rmdir(sock_dir);
	accepting_fatal("%s: %s", msg, strerror(save_errno));
}

void
open_accepting_unix_sockets(
	int self_addresses_count, struct in_addr *self_addresses, int port)
{
	int i;

	GFARM_MALLOC_ARRAY(accepting_unix_socks, self_addresses_count);
	GFARM_MALLOC_ARRAY(unix_sock_names, self_addresses_count);
	GFARM_MALLOC_ARRAY(unix_sock_dirs, self_addresses_count);
	if (accepting_unix_socks == NULL ||
	    unix_sock_names == NULL || unix_sock_dirs == NULL)
		accepting_fatal("not enough memory for unix sockets");

	for (i = 0; i < self_addresses_count; i++) {
		open_accepting_unix_domain(self_addresses[i], port,
		    &accepting_unix_socks[i],
		    &unix_sock_names[i], &unix_sock_dirs[i]);

		/* for cleanup_accepting() */
		accepting_unix_socks_count = i + 1;
	}
}

int
open_accepting_inet_socket(struct in_addr address, int port)
{
	char *e;
	struct sockaddr_in self_addr;
	socklen_t self_addr_size;
	int sock, sockopt;

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sin_family = AF_INET;
	self_addr.sin_addr = address;
	self_addr.sin_port = htons(port);
	self_addr_size = sizeof(self_addr);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		accepting_fatal_errno("accepting socket");
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1)
		gflog_warning_errno("SO_REUSEADDR");
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0)
		accepting_fatal_errno("bind accepting socket");
	e = gfarm_sockopt_apply_listener(sock);
	if (e != NULL)
		gflog_warning("setsockopt: %s", e);
	if (listen(sock, LISTEN_BACKLOG) < 0)
		accepting_fatal_errno("listen");
	return (sock);
}

int *
open_datagram_service_sockets(
	int self_addresses_count, struct in_addr *self_addresses, int port)
{
	int i, *sockets, s;
	struct sockaddr_in bind_addr;
	socklen_t bind_addr_size;

	GFARM_MALLOC_ARRAY(sockets, self_addresses_count);
	if (sockets == NULL)
		accepting_fatal_errno("malloc datagram sockets");
	for (i = 0; i < self_addresses_count; i++) {
		memset(&bind_addr, 0, sizeof(bind_addr));
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_addr = self_addresses[i];
		bind_addr.sin_port = ntohs(port);
		bind_addr_size = sizeof(bind_addr);
		s = socket(PF_INET, SOCK_DGRAM, 0);
		if (s < 0)
			accepting_fatal_errno("datagram socket");
		if (bind(s, (struct sockaddr *)&bind_addr, bind_addr_size) < 0)
			accepting_fatal_errno("datagram bind");
		sockets[i] = s;
	}
	return (sockets);
}

void
usage(void)
{
	fprintf(stderr, "gfsd %s (%s)\n", GFARM_VERSION, GFARM_REVISION);
	fprintf(stderr, "Usage: %s [option]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-L <syslog-priority-level>\n");
	fprintf(stderr, "\t-P <pid-file>\n");
	fprintf(stderr, "\t-U\t\t\t\t... don't bind UNIX domain socket\n");
	fprintf(stderr, "\t-d\t\t\t\t... debug mode\n");
	fprintf(stderr, "\t-f <gfarm-configuration-file>\n");
	fprintf(stderr, "\t-l <listen_address>\n");
	fprintf(stderr, "\t-p <port>\n");
	fprintf(stderr, "\t-r <spool_root>\n");
	fprintf(stderr, "\t-s <syslog-facility>\n");
	fprintf(stderr, "\t-v\t\t\t\t... make authentication log verbose\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	char *e, *config_file = NULL;
	char *listen_addrname = NULL, *port_number = NULL, *pid_file = NULL;
	FILE *pid_fp = NULL;
	int syslog_level = -1;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	struct in_addr *self_addresses, listen_address;
	int table_size, self_addresses_count, bind_unix_domain = 1;
	int ch, i, nfound, max_fd;
	struct sigaction sa;
	fd_set requests;

	if (argc >= 1)
		program_name = basename(argv[0]);
	gflog_set_identifier(program_name);

	while ((ch = getopt(argc, argv, "L:P:Udf:l:p:r:s:uv")) != -1) {
		switch (ch) {
		case 'L':
			syslog_level = gflog_syslog_name_to_priority(optarg);
			if (syslog_level == -1)
				gflog_fatal("-L %s: invalid syslog priority", 
				    optarg);
			break;
		case 'P':
			pid_file = optarg;
			break;
		case 'U':
			bind_unix_domain = 0;
			break;
		case 'd':
			debug_mode = 1;
			if (syslog_level == -1)
				syslog_level = LOG_DEBUG;
			break;
		case 'f':
			config_file = optarg;
			break;
		case 'l':
			listen_addrname = optarg;
			break;
		case 'p':
			port_number = optarg;
			break;
		case 'r':
			e = gfarm_spool_root_set(optarg);
			if (e != NULL)
				gflog_fatal("%s", e);
			break;
		case 's':
			syslog_facility =
			    gflog_syslog_name_to_facility(optarg);
			if (syslog_facility == -1)
				gflog_fatal("%s: unknown syslog facility",
				    optarg);
			break;
		case 'u':
			restrict_user = 1;
			restricted_user = getuid();
			break;
		case 'v':
			gflog_auth_set_verbose(1);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (config_file != NULL)
		gfarm_config_set_filename(config_file);
	e = gfarm_server_initialize();
	if (e != NULL) {
		fprintf(stderr, "gfarm_server_initialize: %s\n", e);
		exit(1);
	}
	if (syslog_level != -1)
		gflog_set_priority_level(syslog_level);
	/* sanity check on a spool directory */
	gfarm_spool_root_check();

	if (port_number != NULL)
		gfarm_spool_server_port = strtol(port_number, NULL, 0);
	if (listen_addrname == NULL)
		listen_addrname = gfarm_spool_server_listen_address;
	if (listen_addrname == NULL) {
		e = gfarm_get_ip_addresses(
		    &self_addresses_count, &self_addresses);
		if (e != NULL)
			gflog_fatal("get_ip_addresses: %s", e);
		listen_address.s_addr = INADDR_ANY;
	} else {
		struct hostent *hp = gethostbyname(listen_addrname);

		if (hp == NULL || hp->h_addrtype != AF_INET)
			gflog_fatal("listen address can't be resolved: %s",
			    listen_addrname);
		self_addresses_count = 1;
		GFARM_MALLOC(self_addresses);
		if (self_addresses == NULL)
			gflog_fatal(GFARM_ERR_NO_MEMORY);
		memcpy(self_addresses, hp->h_addr, sizeof(*self_addresses));
		listen_address = *self_addresses;
	}

	accepting_inet_sock = open_accepting_inet_socket(
	    listen_address, gfarm_spool_server_port);
	if (bind_unix_domain) {
		/* sets accepting_unix_socks and accepting_unix_socks_count */
		open_accepting_unix_sockets(
		    self_addresses_count, self_addresses,
		    gfarm_spool_server_port);
	} else {
		accepting_unix_socks = NULL;
		accepting_unix_socks_count = 0;
	}
	datagram_socks = open_datagram_service_sockets(
	    self_addresses_count, self_addresses, gfarm_spool_server_port);
	datagram_socks_count = self_addresses_count;

	max_fd = accepting_inet_sock;
	for (i = 0; i < accepting_unix_socks_count; i++) {
		if (max_fd < accepting_unix_socks[i])
			max_fd = accepting_unix_socks[i];
	}
	for (i = 0; i < datagram_socks_count; i++) {
		if (max_fd < datagram_socks[i])
			max_fd = datagram_socks[i];
	}
	if (max_fd > FD_SETSIZE)
		accepting_fatal("datagram_service: too big file descriptor");

	if (pid_file != NULL) {
		/*
		 * We do this before calling gfarm_daemon()
		 * to print the error message to stderr.
		 */
		pid_fp = fopen(pid_file, "w");
		if (pid_fp == NULL)
			accepting_fatal_errno(pid_file);
	}
	if (!debug_mode) {
		gflog_syslog_open(LOG_PID, syslog_facility);
		gfarm_daemon(0, 0);
	}
	if (pid_file != NULL) {
		/*
		 * We do this after calling gfarm_daemon(),
		 * because it changes pid.
		 */
		fprintf(pid_fp, "%ld\n", (long)getpid());
		fclose(pid_fp);
	}

	/* XXX - kluge for gfrcmd (to mkdir HOME....) for now */
	if (chdir(gfarm_spool_root_get_for_compatibility()) == -1)
		gflog_fatal_errno(gfarm_spool_root_get_for_compatibility());

	table_size = FILE_TABLE_LIMIT;
	gfarm_unlimit_nofiles(&table_size);
	if (table_size > FILE_TABLE_LIMIT)
		table_size = FILE_TABLE_LIMIT;
	file_table_init(table_size);
	OpenSSL_add_all_digests(); /* for EVP_get_digestbyname() */

	/*
	 * Because SA_NOCLDWAIT is not implemented on some OS,
	 * we do not rely on the feature.
	 */
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);
	/*
	 * We don't want SIGPIPE, but want EPIPE on write(2)/close(2).
	 */
	gfarm_sigpipe_ignore();
	/*
	 * NOTE: The following signals should match with signals that
	 * start_server() routine makes them SIG_DFL.
	 */
	signal(SIGTERM, accepting_sigterm_handler);
	signal(SIGINT, accepting_sigterm_handler);

	/*
	 * To deal with race condition which may be caused by RST,
	 * listening socket must be O_NONBLOCK, if the socket will be
	 * used as a file descriptor for select(2) .
	 * See section 16.6 of "UNIX NETWORK PROGRAMMING, Volume1,
	 * Third Edition" by W. Richard Stevens, for detail.
	 */
	if (fcntl(accepting_inet_sock, F_SETFL,
	    fcntl(accepting_inet_sock, F_GETFL, NULL) | O_NONBLOCK) == -1)
		gflog_warning_errno("accepting inet socket O_NONBLOCK");
	for (i = 0; i < accepting_unix_socks_count; i++) {
		if (fcntl(accepting_unix_socks[i],F_SETFL,
		    fcntl(accepting_unix_socks[i],F_GETFL,NULL) | O_NONBLOCK)
		    == -1)
			gflog_warning_errno(
			    "accepting unix socket O_NONBLOCK");
	}

	for (;;) {
		FD_ZERO(&requests);
		FD_SET(accepting_inet_sock, &requests);
		for (i = 0; i < accepting_unix_socks_count; i++)
			FD_SET(accepting_unix_socks[i], &requests);
		for (i = 0; i < datagram_socks_count; i++)
			FD_SET(datagram_socks[i], &requests);
		nfound = select(max_fd + 1, &requests, NULL, NULL, NULL);
		if (nfound <= 0) {
			if (nfound == 0 || errno == EINTR || errno == EAGAIN)
				continue;
			accepting_fatal_errno("select");
		}

		if (FD_ISSET(accepting_inet_sock, &requests))
			start_server(accepting_inet_sock);
		for (i = 0; i < accepting_unix_socks_count; i++) {
			if (FD_ISSET(accepting_unix_socks[i], &requests))
				start_server(accepting_unix_socks[i]);
		}
		for (i = 0; i < datagram_socks_count; i++) {
			if (FD_ISSET(datagram_socks[i], &requests))
				datagram_server(datagram_socks[i]);
		}
	}
	/*NOTREACHED*/
#ifdef __GNUC__ /* to shut up warning */
	return (0);
#endif
}
