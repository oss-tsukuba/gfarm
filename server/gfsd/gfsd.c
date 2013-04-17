/*
 * $Id$
 */

#define _POSIX_PII_SOCKET /* to use struct msghdr on Tru64 */
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/resource.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <syslog.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <time.h>
#include <pwd.h>
#include <libgen.h>

#if defined(SCM_RIGHTS) && \
		(!defined(sun) || (!defined(__svr4__) && !defined(__SVR4)))
#define HAVE_MSG_CONTROL 1
#endif

#include <gfarm/gfarm_config.h>

#ifdef HAVE_POLL
#include <poll.h>
#endif

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>	/* getloadavg() on Solaris */
#endif

#define GFLOG_USE_STDARG
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/host_info.h>
#include <gfarm/gfarm_iostat.h>

#include "gfutil.h"
#include "gflog_reduced.h"
#include "hash.h"
#include "nanosec.h"
#include "timer.h"

#include "gfp_xdr.h"
#include "io_fd.h"
#include "sockopt.h"
#include "hostspec.h"
#include "host.h"
#include "conn_hash.h"
#include "auth.h"
#include "config.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "gfs_profile.h"
#include "iostat.h"

#include "gfsd_subr.h"

#define COMPAT_OLD_GFS_PROTOCOL

#define LOCAL_SOCKDIR_MODE	0755
#define LOCAL_SOCKET_MODE	0777
#define PERMISSION_MASK		0777

/* need to be accessed as an executable (in future, e.g. after chmod) */
#define	DATA_FILE_MASK		0711
#define	DATA_DIR_MASK		0700

/* limit maximum open files per client, when system limit is very high */
#ifndef FILE_TABLE_LIMIT
#define FILE_TABLE_LIMIT	2048
#endif

#define HOST_HASHTAB_SIZE	3079	/* prime number */

/*
 * set initial sleep_interval to 1 sec for quick recovery
 * at gfmd failover.
 */
#define GFMD_CONNECT_SLEEP_INTVL_MIN	1	/* 1 sec */
#define GFMD_CONNECT_SLEEP_INTVL_MAX	512	/* about 8.5 min */
#define GFMD_CONNECT_SLEEP_TIMEOUT	60	/* 1 min for gfmd failover */
#define GFMD_CONNECT_SLEEP_LOG_OMIT	11	/* log until 512 sec */
#define GFMD_CONNECT_SLEEP_LOG_INTERVAL	86400	/* 1 day */

#define fatal_errno(msg_no, ...) \
	fatal_errno_full(msg_no, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define accepting_fatal(msg_no, ...) \
	accepting_fatal_full(msg_no, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define accepting_fatal_errno(msg_no, ...) \
	accepting_fatal_errno_full(msg_no, __FILE__, __LINE__, __func__,\
				   __VA_ARGS__)

const char READONLY_CONFIG_FILE[] = ".readonly";

const char *program_name = "gfsd";

int debug_mode = 0;
pid_t master_gfsd_pid;
pid_t back_channel_gfsd_pid;
uid_t gfsd_uid = -1;

struct gfm_connection *gfm_server;
char *canonical_self_name;
char *username; /* gfarm global user name */

int gfarm_spool_root_len;

struct gfp_xdr *credential_exported = NULL;

long file_read_size;
#if 0 /* not yet in gfarm v2 */
long rate_limit;
#endif

static struct gfarm_iostat_spec iostat_spec[] =  {
	{ "rcount", GFARM_IOSTAT_TYPE_TOTAL },
	{ "wcount", GFARM_IOSTAT_TYPE_TOTAL },
	{ "rbytes", GFARM_IOSTAT_TYPE_TOTAL },
	{ "wbytes", GFARM_IOSTAT_TYPE_TOTAL },
};
static char *iostat_dirbuf;
static int iostat_dirlen;

static volatile sig_atomic_t write_open_count = 0;
static volatile sig_atomic_t terminate_flag = 0;

static char *listen_addrname = NULL;

static int client_failover_count;

static int shutting_down; /* set 1 at shutting down */

struct local_socket {
	int sock;
	char *dir, *name;
};

struct accepting_sockets {
	int local_socks_count, udp_socks_count;
	int tcp_sock, *udp_socks;
	struct local_socket *local_socks;
} accepting;

/* this routine should be called before the accepting server calls exit(). */
void
cleanup_accepting(int sighandler)
{
	int i;

	for (i = 0; i < accepting.local_socks_count; i++) {
		if (unlink(accepting.local_socks[i].name) == -1 && !sighandler)
			gflog_warning(GFARM_MSG_1002378,
			    "unlink(%s)", accepting.local_socks[i].name);
		if (rmdir(accepting.local_socks[i].dir) == -1 && !sighandler)
			gflog_warning(GFARM_MSG_1002379,
			    "rmdir(%s)", accepting.local_socks[i].dir);
	}
}
void
cleanup_iostat(void)
{
	if (iostat_dirbuf) {
		strcpy(&iostat_dirbuf[iostat_dirlen], "gfsd");
		(void) unlink(iostat_dirbuf);
		strcpy(&iostat_dirbuf[iostat_dirlen], "bcs");
		(void) unlink(iostat_dirbuf);
		free(iostat_dirbuf);
		iostat_dirbuf = NULL;
	}
}

static void close_all_fd(void);

/* this routine should be called before calling exit(). */
static void
cleanup(int sighandler)
{
	static int cleanup_started = 0;
	pid_t pid = getpid();

	if (!cleanup_started) {
		cleanup_started = 1;

		if (pid != master_gfsd_pid && pid != back_channel_gfsd_pid &&
		    !sighandler)
			close_all_fd(); /* may recursivelly call cleanup() */
	}

	if (pid == master_gfsd_pid) {
		cleanup_accepting(sighandler);
		/* send terminate signal to a back channel process */
		if (kill(back_channel_gfsd_pid, SIGTERM) == -1 && !sighandler)
			gflog_warning_errno(GFARM_MSG_1002377,
			    "kill(%ld)", (long)back_channel_gfsd_pid);
		cleanup_iostat();
	}

	if (credential_exported != NULL)
		gfp_xdr_delete_credential(credential_exported, sighandler);
	credential_exported = NULL;

	if (!sighandler) {
		/* It's not safe to do the following operation */
		gflog_notice(GFARM_MSG_1000451, "disconnected");
	}
}

static void
cleanup_handler(int signo)
{
	terminate_flag = 1;
	if (write_open_count == 0) {
		cleanup(1);
		_exit(2);
	}
}

static int kill_master_gfsd;

void
fatal_full(int msg_no, const char *file, int line_no, const char *func,
		const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(msg_no, LOG_ERR, file, line_no, func, format, ap);
	va_end(ap);

	if (!shutting_down) {
		shutting_down = 1;
		cleanup(0);
	}

	if (getpid() == back_channel_gfsd_pid || kill_master_gfsd) {
		/*
		 * send terminate signal to the master process.
		 * this should be done at the end of fatal(),
		 * because both the master process and the back channel process
		 * try to kill each other.
		 */
		kill(master_gfsd_pid, SIGTERM);
	}
	exit(2);
}

static void fatal_errno_full(int, const char *, int, const char*,
		const char *, ...) GFLOG_PRINTF_ARG(5, 6);
static void
fatal_errno_full(int msg_no, const char *file, int line_no, const char *func,
		const char *format, ...)
{
	char buffer[2048];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buffer, sizeof buffer, format, ap);
	va_end(ap);
	fatal_full(msg_no, file, line_no, func, "%s: %s",
			buffer, strerror(errno));
}

void
fatal_metadb_proto_full(int msg_no,
	const char *file, int line_no, const char *func,
	const char *diag, const char *proto, gfarm_error_t e)
{
	fatal_full(msg_no, file, line_no, func,
	    "gfmd protocol: %s error on %s: %s", proto, diag,
	    gfarm_error_string(e));
}

static void accepting_fatal_full(int, const char *, int, const char *,
		const char *, ...) GFLOG_PRINTF_ARG(5, 6);
static void
accepting_fatal_full(int msg_no, const char *file, int line_no,
		const char *func, const char *format, ...)
{
	va_list ap;

	if (!shutting_down) {
		shutting_down = 1;
		cleanup_accepting(0);
	}
	va_start(ap, format);
	gflog_vmessage(msg_no, LOG_ERR, file, line_no, func, format, ap);
	va_end(ap);
	exit(2);
}

static void accepting_fatal_errno_full(int, const char *, int, const char *,
		const char *, ...) GFLOG_PRINTF_ARG(5, 6);
static void
accepting_fatal_errno_full(int msg_no, const char *file, int line_no,
		const char *func, const char *format, ...)
{
	int save_errno = errno;
	char buffer[2048];

	va_list ap;

	va_start(ap, format);
	vsnprintf(buffer, sizeof buffer, format, ap);
	va_end(ap);
	accepting_fatal_full(msg_no, file, line_no, func, "%s: %s", buffer,
			strerror(save_errno));
}

static gfarm_error_t
connect_gfm_server0(int use_timeout)
{
	gfarm_error_t e;
	int sleep_interval = GFMD_CONNECT_SLEEP_INTVL_MIN;
	struct timeval expiration_time;
	int timed_out;
	struct gflog_reduced_state connlog = GFLOG_REDUCED_STATE_INITIALIZER(
		GFMD_CONNECT_SLEEP_LOG_OMIT, 1,
		GFMD_CONNECT_SLEEP_INTVL_MAX * 10,
		GFMD_CONNECT_SLEEP_LOG_INTERVAL);
	struct gflog_reduced_state hnamelog = GFLOG_REDUCED_STATE_INITIALIZER(
		GFMD_CONNECT_SLEEP_LOG_OMIT, 1,
		GFMD_CONNECT_SLEEP_INTVL_MAX * 10,
		GFMD_CONNECT_SLEEP_LOG_INTERVAL);

	if (use_timeout) {
		gettimeofday(&expiration_time, NULL);
		expiration_time.tv_sec += GFMD_CONNECT_SLEEP_TIMEOUT;
	}

	for (;;) {
		e = gfm_client_connect(gfarm_metadb_server_name,
		    gfarm_metadb_server_port, GFSD_USERNAME,
		    &gfm_server, listen_addrname);

		timed_out = use_timeout &&
		    gfarm_timeval_is_expired(&expiration_time);
		
		if (e != GFARM_ERR_NO_ERROR) {
			if (timed_out) {
				gflog_error(GFARM_MSG_1003668,
				    "connecting to gfmd at %s:%d failed: %s",
				    gfarm_metadb_server_name,
				    gfarm_metadb_server_port,
				    gfarm_error_string(e));
				return (e);
			}
			gflog_reduced_warning(GFARM_MSG_1000550, &connlog,
			    "connecting to gfmd at %s:%d failed, "
			    "sleep %d sec: %s",
			    gfarm_metadb_server_name,
			    gfarm_metadb_server_port,
			    sleep_interval, gfarm_error_string(e));
		} else {
			/*
			 * If canonical_self_name is specified (by the
			 * command-line argument), send the hostname to
			 * identify myself.  If not sending the hostname,
			 * the canonical name will be decided by the gfmd using
			 * the reverse lookup of the connected IP address.
			 */
			if (canonical_self_name == NULL)
				return (GFARM_ERR_NO_ERROR);

			e = gfm_client_hostname_set(gfm_server,
			    canonical_self_name);
			if (e == GFARM_ERR_NO_ERROR)
				return (GFARM_ERR_NO_ERROR);
			if (timed_out || !IS_CONNECTION_ERROR(e)) {
				gflog_error(GFARM_MSG_1000551,
				    "cannot set canonical hostname of "
				    "this node (%s): %s", canonical_self_name,
				    gfarm_error_string(e));
				return (e);
			}
			gflog_reduced_error(GFARM_MSG_1003669, &hnamelog,
			    "cannot set canonical hostname of this node (%s), "
			    "sleep %d sec: %s", canonical_self_name,
			    sleep_interval, gfarm_error_string(e));
			/* retry if IS_CONNECTION_ERROR(e) */
		}
		gfarm_sleep(sleep_interval);
		if (sleep_interval < GFMD_CONNECT_SLEEP_INTVL_MAX)
			sleep_interval *= 2;
	}

}

static gfarm_error_t
connect_gfm_server_with_timeout(void)
{
	return (connect_gfm_server0(1));
}

static gfarm_error_t
connect_gfm_server(void)
{
	return (connect_gfm_server0(0));
}

static void
free_gfm_server(void)
{
	gfm_client_connection_free(gfm_server);
	gfm_server = NULL;
}

static int
fd_send_message(int fd, void *buf, size_t size, int fdc, int *fdv)
{
	char *buffer = buf;
	int rv;
	struct iovec iov[1];
	struct msghdr msg;
#ifdef HAVE_MSG_CONTROL /* 4.3BSD Reno or later */
	struct {
		struct cmsghdr hdr;
		char data[CMSG_SPACE(sizeof(*fdv) * GFSD_MAX_PASSING_FD)
			  - sizeof(struct cmsghdr)];
	} cmsg;

	if (fdc > GFSD_MAX_PASSING_FD) {
		fatal(GFARM_MSG_1000453,
		    "gfsd: fd_send_message(): fd count %d > %d",
		    fdc, GFSD_MAX_PASSING_FD);
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
		msg.msg_flags = 0;
		if (fdc > 0) {
			int i;

			msg.msg_control = (caddr_t)&cmsg.hdr;
			msg.msg_controllen = CMSG_SPACE(sizeof(*fdv) * fdc);
			cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(*fdv) * fdc);
			cmsg.hdr.cmsg_level = SOL_SOCKET;
			cmsg.hdr.cmsg_type = SCM_RIGHTS;
			for (i = 0; i < fdc; i++)
				((int *)CMSG_DATA(&cmsg.hdr))[i] = fdv[i];

			/* to shut up valgrind's "uninitialised byte(s)" */
			if (CMSG_SPACE(sizeof(*fdv) * fdc)
			    > CMSG_LEN(sizeof(*fdv) * fdc))
				memset(&((int *)CMSG_DATA(&cmsg.hdr))[fdc], 0,
				    CMSG_SPACE(sizeof(*fdv) * fdc)
				    - CMSG_LEN(sizeof(*fdv) * fdc));
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
gfs_server_get_request(struct gfp_xdr *client, const char *diag,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv_request_parameters(client, 0, NULL, format, &ap);
	va_end(ap);

	/* XXX FIXME: should handle GFARM_ERR_NO_MEMORY gracefully */
	if (e != GFARM_ERR_NO_ERROR)
		fatal(GFARM_MSG_1000455, "%s get request: %s",
		    diag, gfarm_error_string(e));
}

#define IS_IO_ERROR(e) \
	((e) == GFARM_ERR_INPUT_OUTPUT || (e) == GFARM_ERR_STALE_FILE_HANDLE)

void
gfs_server_put_reply_common(struct gfp_xdr *client, const char *diag,
	gfp_xdr_xid_t xid,
	gfarm_int32_t ecode, const char *format, va_list *app)
{
	gfarm_error_t e;

	if (debug_mode)
		gflog_debug(GFARM_MSG_1000458, "reply: %s: %d (%s)",
		    diag, (int)ecode, gfarm_error_string(ecode));

	e = gfp_xdr_vsend_result(client, ecode, format, app);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(client);
	if (e != GFARM_ERR_NO_ERROR)
		fatal(GFARM_MSG_1000459, "%s put reply: %s",
		    diag, gfarm_error_string(e));

	/* if input/output error occurs, die */
	if (IS_IO_ERROR(ecode)) {
		kill_master_gfsd = 1;
		fatal(GFARM_MSG_1002513, "%s: %s, die", diag,
		    gfarm_error_string(ecode));
	}
}

void
gfs_server_put_reply_with_errno_common(struct gfp_xdr *client, const char *diag,
	gfp_xdr_xid_t xid,
	int eno, const char *format, va_list *app)
{
	gfarm_int32_t ecode = gfarm_errno_to_error(eno);

	if (ecode == GFARM_ERR_UNKNOWN)
		gflog_warning(GFARM_MSG_1000461, "%s: %s", diag, strerror(eno));
	gfs_server_put_reply_common(client, diag, xid, ecode, format, app);
}

void
gfs_server_put_reply(struct gfp_xdr *client, const char *diag,
	int ecode, char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfs_server_put_reply_common(client, diag, -1, ecode, format, &ap);
	va_end(ap);
}

void
gfs_server_put_reply_with_errno(struct gfp_xdr *client, const char *diag,
	int eno, char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfs_server_put_reply_with_errno_common(client, diag, -1, eno,
	    format, &ap);
	va_end(ap);
}

gfarm_error_t
gfs_async_server_get_request(struct gfp_xdr *client, size_t size,
	const char *diag, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv_request_parameters(client, 0, &size, format, &ap);
	va_end(ap);

	/* XXX FIXME: should handle GFARM_ERR_NO_MEMORY gracefully */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002380, "%s get request: %s",
		    diag, gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_async_server_put_reply_common(struct gfp_xdr *client, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t ecode, char *format, va_list *app)
{
	gfarm_error_t e;

	if (debug_mode)
		gflog_debug(GFARM_MSG_1002381, "async_reply: %s: %d (%s)",
		    diag, (int)ecode, gfarm_error_string(ecode));

	e = gfp_xdr_vsend_async_result(client, xid, ecode, format, app);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(client);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002382, "%s put reply: %s",
		    diag, gfarm_error_string(e));

	/* if input/output error occurs, die */
	if (IS_IO_ERROR(ecode)) {
		kill_master_gfsd = 1;
		fatal(GFARM_MSG_1003683, "%s: %s, die", diag,
		    gfarm_error_string(ecode));
	}
	return (e);
}

gfarm_error_t
gfs_async_server_put_reply(struct gfp_xdr *client, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t ecode, char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfs_async_server_put_reply_common(client, xid, diag, ecode,
	    format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfs_async_server_put_reply_with_errno(struct gfp_xdr *client,
	gfp_xdr_xid_t xid, const char *diag, int eno, char *format, ...)
{
	va_list ap;
	gfarm_int32_t ecode = gfarm_errno_to_error(eno);
	gfarm_error_t e;

	if (ecode == GFARM_ERR_UNKNOWN)
		gflog_warning(GFARM_MSG_1002383, "%s: %s", diag, strerror(eno));

	va_start(ap, format);
	e = gfs_async_server_put_reply_common(client, xid, diag, ecode,
	    format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfm_async_client_send_request(struct gfp_xdr *bc_conn,
	gfp_xdr_async_peer_t async, const char *diag,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *),
	void *closure,
	gfarm_int32_t command, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfp_xdr_vsend_async_request(bc_conn, async,
	    result_callback, disconnect_callback, closure,
	    command, format, &ap);
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002164,
		    "gfm_async_client_send_request %s: %s",
		    diag, gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfm_async_client_recv_reply(struct gfp_xdr *bc_conn, const char *diag,
	size_t size, const char *format, ...)
{
	gfarm_error_t e;
	gfarm_int32_t errcode;
	va_list ap;

	va_start(ap, format);
	e = gfp_xdr_vrpc_result_sized(bc_conn, 0, &size,
	    &errcode, &format, &ap);
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002165,
		    "gfm_async_client_recv_reply %s: %s",
		    diag, gfarm_error_string(e));
	} else if (size != 0) {
		gflog_error(GFARM_MSG_1002166,
		    "gfm_async_client_recv_reply %s: protocol residual %d",
		    diag, (int)size);
		if ((e = gfp_xdr_purge(bc_conn, 0, size))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning(GFARM_MSG_1002167,
			    "gfm_async_client_recv_reply %s: skipping: %s",
			    diag, gfarm_error_string(e));
		e = GFARM_ERR_PROTOCOL;
	} else {
		e = errcode;
	}
	return (e);
}


void
gfs_server_process_set(struct gfp_xdr *client)
{
	gfarm_int32_t e;
	gfarm_pid_t pid;
	gfarm_int32_t keytype;
	size_t keylen;
	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	static const char *diag = "process_set";

	gfs_server_get_request(client, diag,
	    "ibl", &keytype, sizeof(sharedkey), &keylen, sharedkey, &pid);

	if (gfm_client_process_is_set(gfm_server)) {
		gflog_debug(GFARM_MSG_1003399,
		    "process is already set");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = gfm_client_process_set(gfm_server,
	    keytype, sharedkey, keylen, pid)) != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003400,
		    "gfm_client_process_set: %s", gfarm_error_string(e));

	gfs_server_put_reply(client, diag, e, "");
}

void
gfs_server_process_reset(struct gfp_xdr *client)
{
	gfarm_int32_t e;
	gfarm_pid_t pid;
	gfarm_int32_t keytype, failover_count;
	size_t keylen;
	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];
	static const char *diag = "process_reset";

	gfs_server_get_request(client, diag,
	    "ibli", &keytype, sizeof(sharedkey), &keylen, sharedkey, &pid,
	    &failover_count);

	if ((e = gfm_client_process_set(gfm_server,
	    keytype, sharedkey, keylen, pid)) != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003401,
		    "gfm_client_process_set: %s", gfarm_error_string(e));
	else
		client_failover_count = failover_count;

	gfs_server_put_reply(client, diag, e, "");
}

int file_table_size = 0;

struct file_entry {
	off_t size;
	time_t mtime, atime;
	unsigned long mtimensec, atimensec;
	gfarm_ino_t ino;
	int flags, local_fd;
#define FILE_FLAG_LOCAL		0x01
#define FILE_FLAG_CREATED	0x02
#define FILE_FLAG_WRITABLE	0x04
#define FILE_FLAG_WRITTEN	0x08
#define FILE_FLAG_READ		0x10
	gfarm_uint64_t gen, new_gen;
/*
 * performance data (only available in profile mode)
 */
	struct timeval start_time;
	unsigned nwrite, nread;
	double write_time, read_time;
	gfarm_off_t write_size, read_size;
} *file_table;

static void
file_entry_set_atime(struct file_entry *fe,
	gfarm_time_t sec, gfarm_int32_t nsec)
{
	fe->flags |= FILE_FLAG_READ;
	fe->atime = sec;
	fe->atimensec = nsec;
}

static void
file_entry_set_mtime(struct file_entry *fe,
	gfarm_time_t sec, gfarm_int32_t nsec)
{
	fe->flags |= FILE_FLAG_WRITTEN;
	fe->mtime = sec;
	fe->mtimensec = nsec;
}

static void
file_entry_set_size(struct file_entry *fe, gfarm_off_t size)
{
	fe->flags |= FILE_FLAG_WRITTEN;
	fe->size = size;
}

void
file_table_init(int table_size)
{
	int i;

	GFARM_MALLOC_ARRAY(file_table, table_size);
	if (file_table == NULL) {
		errno = ENOMEM; fatal_errno(GFARM_MSG_1000462, "file table");
	}
	for (i = 0; i < table_size; i++)
		file_table[i].local_fd = -1;
	file_table_size = table_size;
}

int
file_table_is_available(gfarm_int32_t net_fd)
{
	if (0 <= net_fd && net_fd < file_table_size)
		return (file_table[net_fd].local_fd == -1);
	else
		return (0);
}

void
file_table_add(gfarm_int32_t net_fd, int local_fd, int flags, gfarm_ino_t ino,
	gfarm_uint64_t gen, struct timeval *start)
{
	struct file_entry *fe;
	struct stat st;

	if (fstat(local_fd, &st) < 0)
		fatal_errno(GFARM_MSG_1000463, "file_table_add: fstat failed");
	fe = &file_table[net_fd];
	fe->local_fd = local_fd;
	fe->flags = 0;
	fe->ino = ino;
	if (flags & O_CREAT)
		fe->flags |= FILE_FLAG_CREATED;
	if (flags & O_TRUNC)
		fe->flags |= FILE_FLAG_WRITTEN;
	if ((flags & O_ACCMODE) != O_RDONLY) {
		fe->flags |= FILE_FLAG_WRITABLE;
		++write_open_count;
	}
	fe->atime = st.st_atime;
	fe->atimensec = gfarm_stat_atime_nsec(&st);
	fe->mtime = st.st_mtime;
	fe->mtimensec = gfarm_stat_mtime_nsec(&st);
	fe->size = st.st_size;
	fe->gen = fe->new_gen = gen;

	/* performance data (only available in profile mode) */
	fe->start_time = *start;
	fe->nwrite = fe-> nread = 0;
	fe->write_time = fe->read_time = 0;
	fe->write_size = fe->read_size = 0;
}

struct file_entry *
file_table_entry(gfarm_int32_t net_fd)
{
	struct file_entry *fe;

	if (0 <= net_fd && net_fd < file_table_size) {
		fe = &file_table[net_fd];
		if (fe->local_fd != -1)
			return (fe);
	}
	return (NULL);
}

#define timeval_sub(t1, t2) \
	(((double)(t1)->tv_sec - (double)(t2)->tv_sec)	\
	+ ((double)(t1)->tv_usec - (double)(t2)->tv_usec) * .000001)

gfarm_error_t
file_table_close(gfarm_int32_t net_fd)
{
	gfarm_error_t e;
	struct file_entry *fe;
	struct timeval end_time;
	char time_buf[26], *t, *te;
	double total_time;

	fe = file_table_entry(net_fd);
	if (fe == NULL) {
		gflog_debug(GFARM_MSG_1002168,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (close(fe->local_fd) < 0)
		e = gfarm_errno_to_error(errno);
	else
		e = GFARM_ERR_NO_ERROR;
	fe->local_fd = -1;

	gfs_profile(
		gettimeofday(&end_time, NULL);
		total_time = timeval_sub(&end_time, &fe->start_time);
		ctime_r(&fe->start_time.tv_sec, time_buf);
		t = time_buf;
		te = &time_buf[sizeof(time_buf)];
		while (t < te && *t != '\n')
			++t;
		if (t < te && *t == '\n')
			*t = '\0';
		else if (t == te)
			*--t = '\0';
		gflog_info(GFARM_MSG_1003249, "%s total_time %g "
		    "inum %lld gen %lld (%lld) "
		    "write %d size %lld time %g "
		    "read %d size %lld time %g",
		    time_buf, total_time, (unsigned long long)fe->ino,
		    (unsigned long long)fe->gen,
		    (unsigned long long)fe->new_gen,
		    fe->nwrite, (long long)fe->write_size, fe->write_time,
		    fe->nread, (long long)fe->read_size, fe->read_time));

	if ((fe->flags & FILE_FLAG_WRITABLE) != 0) {
		--write_open_count;
		if (terminate_flag && write_open_count == 0) {
			gflog_debug(GFARM_MSG_1003432, "bye");
			cleanup(0);
			exit(2);
		}
	}
	return (e);
}

int
file_table_get(gfarm_int32_t net_fd)
{
	if (0 <= net_fd && net_fd < file_table_size)
		return (file_table[net_fd].local_fd);
	else
		return (-1);
}

static void
file_table_set_flag(gfarm_int32_t net_fd, int flags)
{
	struct file_entry *fe = file_table_entry(net_fd);

	if (fe != NULL)
		fe->flags |= flags;
}

static void
file_table_set_read(gfarm_int32_t net_fd)
{
	struct file_entry *fe = file_table_entry(net_fd);
	struct timespec now;

	if (fe == NULL)
		return;

	gfarm_gettime(&now);
	file_entry_set_atime(fe, now.tv_sec, now.tv_nsec);
}

static void
file_table_set_written(gfarm_int32_t net_fd)
{
	struct file_entry *fe = file_table_entry(net_fd);
	struct timespec now;

	if (fe == NULL)
		return;

	gfarm_gettime(&now);
	file_entry_set_mtime(fe, now.tv_sec, now.tv_nsec);
}

static void
file_table_for_each(void (*callback)(void *, gfarm_int32_t), void *closure)
{
	gfarm_int32_t net_fd;

	if (file_table == NULL)
		return;

	for (net_fd = 0; net_fd < file_table_size; net_fd++) {
		if (file_table[net_fd].local_fd != -1)
			(*callback)(closure, net_fd);
	}
}

int
gfs_open_flags_localize(int open_flags)
{
	int local_flags;

	switch (open_flags & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:	local_flags = O_RDONLY; break;
	case GFARM_FILE_WRONLY:	local_flags = O_WRONLY; break;
	case GFARM_FILE_RDWR:	local_flags = O_RDWR; break;
	default: return (-1);
	}

#if 0
	if ((open_flags & GFARM_FILE_CREATE) != 0)
		local_flags |= O_CREAT;
#endif
	if ((open_flags & GFARM_FILE_TRUNC) != 0)
		local_flags |= O_TRUNC;
	if ((open_flags & GFARM_FILE_APPEND) != 0)
		local_flags |= O_APPEND;
#if 0 /* not yet in gfarm v2 */
	if ((open_flags & GFARM_FILE_EXCLUSIVE) != 0)
		local_flags |= O_EXCL;
#endif /* not yet in gfarm v2 */
	return (local_flags);
}

/*
 * if inum == 0x0011223344556677, and gen == 0X8899AABBCCDDEEFF, then
 * local_path = gfarm_spool_root + "data/00112233/44/55/66/778899AABBCCDDEEFF".
 *
 * If the metadata server uses inum > 0x700000000000,
 * We need a modern filesystem which satisfies follows:
 * - can create more than 32765 (= 32767 - 1 (for current) - 1 (for parent))
 *   subdirectories.
 *   32767 comes from platforms which st_nlink is 16bit signed integer.
 *   ext2/ext3fs can create only 32000 subdirectories at maximum.
 * - uses B-tree or similar mechanism to search directory entries
 *   to avoid overhead of linear search.
 */

void
gfsd_local_path(gfarm_ino_t inum, gfarm_uint64_t gen, const char *diag,
	char **pathp)
{
	char *p;
	static int length = 0;
	static char template[] = "/data/00112233/44/55/66/778899AABBCCDDEEFF";
#define DIRLEVEL 5 /* there are 5 levels of directories in template[] */

	if (length == 0)
		length = gfarm_spool_root_len + sizeof(template);

	GFARM_MALLOC_ARRAY(p, length);
	if (p == NULL) {
		fatal(GFARM_MSG_1000464, "%s: no memory for %d bytes",
			diag, length);
	}
	snprintf(p, length, "%s/data/%08X/%02X/%02X/%02X/%02X%08X%08X",
	    gfarm_spool_root,
	    (unsigned int)((inum >> 32) & 0xffffffff),
	    (unsigned int)((inum >> 24) & 0xff),
	    (unsigned int)((inum >> 16) & 0xff),
	    (unsigned int)((inum >>  8) & 0xff),
	    (unsigned int)( inum        & 0xff),
	    (unsigned int)((gen  >> 32) & 0xffffffff),
	    (unsigned int)( gen         & 0xffffffff));
	*pathp = p;
}

/* with errno */
int
gfsd_create_ancestor_dir(char *path)
{
	int i, j, tail, slashpos[DIRLEVEL];
	struct stat st;

	/* errno == ENOENT, so, maybe we don't have an ancestor directory */
	tail = strlen(path);
	for (i = 0; i < DIRLEVEL; i++) {
		for (--tail; tail > 0 && path[tail] != '/'; --tail)
			;
		if (tail <= 0) {
			gflog_warning(GFARM_MSG_1000465,
			    "something wrong in local_path(): %s\n", path);
			errno = ENOENT;
			return (-1);
		}
		assert(path[tail] == '/');
		slashpos[i] = tail;
		path[tail] = '\0';

		if (stat(path, &st) == 0) {
			/* maybe race? */
		} else if (errno != ENOENT) {
			gflog_warning(GFARM_MSG_1000466,
			    "stat(`%s') failed: %s", path, strerror(errno));
			errno = ENOENT;
			return (-1);
		} else if (mkdir(path, DATA_DIR_MASK) < 0) {
			if (errno == ENOENT)
				continue;
			if (errno == EEXIST) {
				/* maybe race */
			} else {
				gflog_error(GFARM_MSG_1000467,
				    "mkdir(`%s') failed: %s", path,
				    strerror(errno));
				errno = ENOENT;
				return (-1);
			}
		}
		/* Now, we have the ancestor directory */
		for (j = i;; --j) {
			path[slashpos[j]] = '/';
			if (j <= 0)
				break;
			if (mkdir(path, DATA_DIR_MASK) < 0) {
				if (errno == EEXIST) /* maybe race */
					continue;
				gflog_warning(GFARM_MSG_1000468,
				    "unexpected mkdir(`%s') failure: %s",
				    path, strerror(errno));
				errno = ENOENT;
				return (-1);
			}
		}
		return (0);
	}
	gflog_warning(GFARM_MSG_1000469,
	    "gfsd spool_root doesn't exist?: %s\n", path);
	errno = ENOENT;
	return (-1);
}

/* with errno */
int
open_data(char *path, int flags)
{
	int fd = open(path, flags, DATA_FILE_MASK);

	if (fd >= 0)
		return (fd);
	if ((flags & O_CREAT) == 0 || errno != ENOENT)
		return (-1);
	if (gfsd_create_ancestor_dir(path))
		return (-1);
	return (open(path, flags, DATA_FILE_MASK));
}

static gfarm_error_t
gfm_client_compound_put_fd_request(gfarm_int32_t net_fd, const char *diag)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_begin_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002291,
		    "gfmd protocol: compound_begin request error on %s: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_request(gfm_server, net_fd))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002292,
		    "gfmd protocol: put_fd request error on %s: %s",
		    diag, gfarm_error_string(e));

	return (e);
}

static gfarm_error_t
gfm_client_compound_put_fd_result(const char *diag)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_end_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002293,
		    "gfmd protocol: compound_end request error on %s: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_compound_begin_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002294,
		    "gfmd protocol: compound_begin result error on %s: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002295,
		    "gfmd protocol: put_fd result error on %s: %s",
		    diag, gfarm_error_string(e));

	return (e);
}

static gfarm_error_t
gfm_client_compound_end(const char *diag)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_end_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002296,
		    "gfmd protocol: compound_end result error on %s: %s",
		    diag, gfarm_error_string(e));

	return (e);
}

static gfarm_error_t
gfs_server_reopen(char *diag, gfarm_int32_t net_fd, char **pathp, int *flagsp,
	gfarm_ino_t *inop, gfarm_uint64_t *genp)
{
	gfarm_error_t e;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	gfarm_int32_t mode, net_flags, to_create;
	char *path;
	int local_flags;

	if ((e = gfm_client_compound_put_fd_request(net_fd, diag))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1003331,
		    "compound_put_fd_request", diag, e);
	else if ((e = gfm_client_reopen_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1000472,
		    "reopen request", diag, e);
	else if ((e = gfm_client_compound_put_fd_result(diag))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1003332,
		    "compound_put_fd_result", diag, e);
	else if ((e = gfm_client_reopen_result(gfm_server,
	    &ino, &gen, &mode, &net_flags, &to_create))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000476,
			    "reopen(%s) result: %s", diag,
			    gfarm_error_string(e));
	} else if ((e = gfm_client_compound_end(diag))
	    != GFARM_ERR_NO_ERROR) {
		fatal_metadb_proto(GFARM_MSG_1003333,
		    "compound_end", diag, e);
	} else if (!GFARM_S_ISREG(mode) ||
	    (local_flags = gfs_open_flags_localize(net_flags)) == -1) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		/* this shouldn't happen */
		gflog_error(GFARM_MSG_1003699, "ino=%lld gen=%lld: "
		    "mode:0%o, flags:0x%0x, to_create:%d: shouldn't happen",
		    (long long)ino, (long long)gen,
		    mode, net_flags, to_create);
	} else {
		gfsd_local_path(ino, gen, diag, &path);
		if (to_create)
			local_flags |= O_CREAT;
		*pathp = path;
		*flagsp = local_flags;
		*inop = ino;
		*genp = gen;
	}
	return (e);
}

gfarm_error_t
gfm_client_replica_lost(gfarm_ino_t ino, gfarm_uint64_t gen)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_REPLICA_LOST";

	if ((e = gfm_client_replica_lost_request(gfm_server, ino, gen))
	     != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1000478,
		    "replica_lost request", diag, e);
	else if ((e = gfm_client_replica_lost_result(gfm_server))
	     != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT)
		if (debug_mode)
			gflog_info(GFARM_MSG_1000479,
			    "replica_lost(%s) result: %s", diag,
			    gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_server_open_common(struct gfp_xdr *client, char *diag,
	gfarm_int32_t *net_fdp, int *local_fdp)
{
	gfarm_error_t e;
	char *path = NULL;
	gfarm_ino_t ino = 0;
	gfarm_uint64_t gen = 0;
	int net_fd, local_fd, save_errno, local_flags = 0;
	struct timeval start;

	gettimeofday(&start, NULL);

	gfs_server_get_request(client, diag, "i", &net_fd);

	if (!file_table_is_available(net_fd)) {
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
		gflog_debug(GFARM_MSG_1002171,
			"bad file descriptor");
	} else {
		for (;;) {
			if ((e = gfs_server_reopen(diag, net_fd,
			    &path, &local_flags, &ino, &gen)) !=
			    GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002172,
					"gfs_server_reopen() failed: %s",
					gfarm_error_string(e));
				break;
			}
			local_fd = open_data(path, local_flags);
			save_errno = errno;
			free(path);
			if (local_fd >= 0) {
				file_table_add(net_fd, local_fd, local_flags,
				    ino, gen, &start);
				*net_fdp = net_fd;
				*local_fdp = local_fd;
				break;
			}

			if ((e = gfm_client_compound_put_fd_request(net_fd,
			   diag)) != GFARM_ERR_NO_ERROR)
				fatal_metadb_proto(GFARM_MSG_1003334,
				    "compound_put_fd_request", diag, e);
			if ((e = gfm_client_close_request(gfm_server)) !=
			    GFARM_ERR_NO_ERROR)
				fatal(GFARM_MSG_1002297,
				    "%s: close(%d) request: %s",
				    diag, net_fd, gfarm_error_string(e));
			if ((e = gfm_client_compound_put_fd_result(diag)) !=
			    GFARM_ERR_NO_ERROR)
				fatal_metadb_proto(GFARM_MSG_1003335,
				    "compound_put_fd_result", diag, e);
			if ((e = gfm_client_close_result(gfm_server)) !=
			    GFARM_ERR_NO_ERROR)
				gflog_info(GFARM_MSG_1002298,
				    "%s: close(%d): %s",
				    diag, net_fd, gfarm_error_string(e));
			else if ((e = gfm_client_compound_end(diag)) !=
			    GFARM_ERR_NO_ERROR)
				fatal_metadb_proto(GFARM_MSG_1003336,
				    "compound_end", diag, e);

			if (save_errno == ENOENT) {
				e = gfm_client_replica_lost(ino, gen);
				if (e == GFARM_ERR_NO_SUCH_OBJECT) {
					gflog_debug(GFARM_MSG_1002299,
					    "possible race between "
					    "rename & reopen: "
					    "ino %lld, gen %lld",
					    (long long)ino, (long long)gen);
					continue;
				}
				if (e == GFARM_ERR_NO_ERROR) {
					gflog_info(GFARM_MSG_1000480,
					    "invalid metadata deleted: "
					    "ino %lld, gen %lld",
					    (long long)ino, (long long)gen);
					/*
					 * the physical file is lost.
					 * return GFARM_ERR_FILE_MIGRATED to
					 * try another available file.
					 */
					e = GFARM_ERR_FILE_MIGRATED;
					break;
				} else if (e == GFARM_ERR_FILE_BUSY) {
					/* sourceforge.net #455, #666 */
					gflog_debug(GFARM_MSG_1003559,
					    "possible race against "
					    "reopen with O_CREAT "
					    "ino %lld, gen %lld",
					    (long long)ino, (long long)gen);
					e = GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE;
					break;
				} else
					gflog_warning(GFARM_MSG_1000481,
					    "fails to delete invalid metadata"
					    ": ino %lld, gen %lld: %s",
					    (long long)ino, (long long)gen,
					    gfarm_error_string(e));
			}
			e = gfarm_errno_to_error(save_errno);
			break;
		}
	}

	gfs_server_put_reply(client, diag, e, "");
	return (e);
}

void
gfs_server_open(struct gfp_xdr *client)
{
	gfarm_int32_t net_fd;
	int local_fd;

	gfs_server_open_common(client, "open", &net_fd, &local_fd);
}

void
gfs_server_open_local(struct gfp_xdr *client)
{
	gfarm_error_t e;
	gfarm_int32_t net_fd;
	int local_fd, rv;
	gfarm_int8_t dummy = 0; /* needs at least 1 byte */

	if (gfs_server_open_common(client, "open_local", &net_fd, &local_fd) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002173,
			"gfs_server_open_common() failed");
		return;
	}

	/* need to flush iobuffer before sending data w/o iobuffer */
	e = gfp_xdr_flush(client);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000482, "open_local: flush: %s",
		    gfarm_error_string(e));

	/* layering violation, but... */
	rv = fd_send_message(gfp_xdr_fd(client),
	    &dummy, sizeof(dummy), 1, &local_fd);
	if (rv != 0)
		gflog_warning(GFARM_MSG_1000483,
		    "open_local: send_message: %s", strerror(rv));

	file_table_set_flag(net_fd, FILE_FLAG_LOCAL);
}

gfarm_error_t
close_request(struct file_entry *fe)
{
	if (fe->flags & FILE_FLAG_WRITTEN) {
		return (gfm_client_close_write_v2_4_request(gfm_server,
		    fe->size,
		    (gfarm_int64_t)fe->atime, (gfarm_int32_t)fe->atimensec,
		    (gfarm_int64_t)fe->mtime, (gfarm_int32_t)fe->mtimensec));
	} else if (fe->flags & FILE_FLAG_READ) {
		return (gfm_client_close_read_request(gfm_server,
		    (gfarm_int64_t)fe->atime, (gfarm_int32_t)fe->atimensec));
	} else {
		return (gfm_client_close_request(gfm_server));
	}
}

gfarm_error_t
fhclose_request(struct file_entry *fe)
{
	if (fe->flags & FILE_FLAG_WRITTEN) {
		return (gfm_client_fhclose_write_request(gfm_server,
		    fe->ino, fe->gen, fe->size,
		    (gfarm_int64_t)fe->atime, (gfarm_int32_t)fe->atimensec,
		    (gfarm_int64_t)fe->mtime, (gfarm_int32_t)fe->mtimensec));
	} else if (fe->flags & FILE_FLAG_READ) {
		return (gfm_client_fhclose_read_request(gfm_server,
		    fe->ino, fe->gen, 
		    (gfarm_int64_t)fe->atime, (gfarm_int32_t)fe->atimensec));
	} else {
		return (GFARM_ERR_NO_ERROR);
	}
}

gfarm_error_t
update_local_file_generation(struct file_entry *fe, gfarm_int64_t old_gen,
    gfarm_int64_t new_gen)
{
	int save_errno;
	char *old, *new;

	gfsd_local_path(fe->ino, old_gen, "close_write: old", &old);
	gfsd_local_path(fe->ino, new_gen, "close_write: new", &new);
	if (rename(old, new) == -1) {
		save_errno = errno;
		gflog_error(GFARM_MSG_1002300,
		    "close_write: new generation: %llu -> %llu: %s",
		    (unsigned long long)old_gen,
		    (unsigned long long)new_gen,
		    strerror(save_errno));
	} else {
		save_errno = 0;
		fe->new_gen = new_gen;
	}
	free(old);
	free(new);

	return (gfarm_errno_to_error(save_errno));
}

gfarm_error_t
close_result(struct file_entry *fe, gfarm_int32_t *gen_update_result_p)
{
	gfarm_error_t e;
	gfarm_int32_t flags;
	gfarm_int64_t old_gen, new_gen;

	if (fe->flags & FILE_FLAG_WRITTEN) {
		e = gfm_client_close_write_v2_4_result(gfm_server,
		    &flags, &old_gen, &new_gen);
		if (e == GFARM_ERR_NO_ERROR &&
		    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED))
			*gen_update_result_p = update_local_file_generation(
			    fe, old_gen, new_gen);
		else
			*gen_update_result_p = -1;
		return (e);
	} else if (fe->flags & FILE_FLAG_READ) {
		*gen_update_result_p = -1;
		return (gfm_client_close_read_result(gfm_server));
	} else {
		*gen_update_result_p = -1;
		return (gfm_client_close_result(gfm_server));
	}
}

gfarm_error_t
fhclose_result(struct file_entry *fe, gfarm_uint64_t *cookie_p,
    gfarm_int32_t *gen_update_result_p)
{
	gfarm_error_t e;
	gfarm_int32_t flags;
	gfarm_int64_t old_gen, new_gen;

	*gen_update_result_p = -1;
	*cookie_p = 0;
	if (fe->flags & FILE_FLAG_WRITTEN) {
		e = gfm_client_fhclose_write_result(gfm_server,
		    &flags, &old_gen, &new_gen, cookie_p);
		if (e == GFARM_ERR_NO_ERROR &&
		    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED))
			*gen_update_result_p = update_local_file_generation(
			    fe, old_gen, new_gen);
		return (e);
	} else if (fe->flags & FILE_FLAG_READ) {
		return (gfm_client_fhclose_read_result(gfm_server));
	} else {
		return (GFARM_ERR_NO_ERROR);
	}
}

void
update_file_entry_for_close(gfarm_int32_t fd, struct file_entry *fe)
{
	struct stat st;
	int stat_is_done = 0;
	unsigned long atimensec, mtimensec;

	if ((fe->flags & FILE_FLAG_LOCAL) == 0) { /* remote? */
		;
	} else if (fstat(fe->local_fd, &st) == -1) {
		gflog_warning(GFARM_MSG_1000484,
		    "fd %d: stat failed at close: %s",
		    fd, strerror(errno));
	} else {
		stat_is_done = 1;
		atimensec = gfarm_stat_atime_nsec(&st);
		if (st.st_atime != fe->atime || atimensec != fe->atimensec)
			file_entry_set_atime(fe, st.st_atime, atimensec);
		/* another process might write this file */
		if ((fe->flags & FILE_FLAG_WRITABLE) != 0) {
			mtimensec = gfarm_stat_mtime_nsec(&st);
			if (st.st_mtime != fe->mtime ||
			    mtimensec != fe->mtimensec)
				file_entry_set_mtime(fe,
				    st.st_mtime, mtimensec);
			if (st.st_size != fe->size)
				file_entry_set_size(fe, st.st_size);
			/* XXX FIXME this may be caused by others */
		}
	}
	if ((fe->flags & FILE_FLAG_WRITTEN) != 0 && !stat_is_done) {
		if (fstat(fe->local_fd, &st) == -1)
			gflog_warning(GFARM_MSG_1000485,
			    "fd %d: stat failed at close: %s",
			    fd, strerror(errno));
		else
			fe->size = st.st_size;
	}
}

gfarm_error_t
close_fd(gfarm_int32_t fd, const char *diag)
{
	gfarm_error_t e, e2;
	struct file_entry *fe;
	gfarm_int32_t gen_update_result = -1;

	if ((fe = file_table_entry(fd)) == NULL) {
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
		gflog_debug(GFARM_MSG_1002174,
			"bad file descriptor");
		return (e);
	}
	update_file_entry_for_close(fd, fe);

	if ((e = gfm_client_compound_put_fd_request(fd, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003337,
		    "%s compound_put_fd_request: %s",
		    diag, gfarm_error_string(e));
	else if ((e = close_request(fe)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000488,
		    "%s close request: %s", diag, gfarm_error_string(e));
	else if ((e = gfm_client_compound_put_fd_result(diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003338,
		    "%s compound_put_fd_result: %s",
		    diag, gfarm_error_string(e));
	else if ((e = close_result(fe, &gen_update_result))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000492,
			    "close(%s) result: %s", diag,
			    gfarm_error_string(e));
	} else if ((e = gfm_client_compound_end(diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003339,
		    "%s compound_end: %s", diag, gfarm_error_string(e));

	if (e != GFARM_ERR_NO_ERROR) {
		if (fe->flags & FILE_FLAG_WRITTEN) {
			if (fe->new_gen != fe->gen)
				gflog_error(GFARM_MSG_1003507,
				    "inode %lld generation %lld -> %lld: "
				    "error occurred during close operation "
				    "for writing: %s",
				    (long long)fe->ino, (long long)fe->gen,
				    (long long)fe->new_gen,
				    gfarm_error_string(e));
			else
				gflog_error(GFARM_MSG_1003508,
				    "inode %lld generation %lld: "
				    "error occurred during close operation "
				    "for writing: %s",
				    (long long)fe->ino, (long long)fe->gen,
				    gfarm_error_string(e));
		}
	} else if (gen_update_result != -1) {
		if ((e2 = gfm_client_compound_put_fd_request(fd, diag))
		    != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003340,
			    "%s compound_put_fd_request: %s",
			    diag, gfarm_error_string(e2));
		else if ((e2 = gfm_client_generation_updated_request(
		    gfm_server, gen_update_result)) != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002301,
			    "%s generation_updated request: %s",
			    diag, gfarm_error_string(e2));
		else if ((e2 = gfm_client_compound_put_fd_result(diag))
		    != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003341,
			    "%s compound_put_fd_result: %s",
			    diag, gfarm_error_string(e2));
		else if ((e2 = gfm_client_generation_updated_result(
		    gfm_server)) != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002302,
			    "%s generation_updated result: %s", 
			    diag, gfarm_error_string(e2));
		else if ((e2 = gfm_client_compound_end(diag))
		    != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003342, "%s compound_end: %s",
			    diag, gfarm_error_string(e2));
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
	}

	return (e);
}

gfarm_error_t
fhclose_fd(gfarm_int32_t fd, const char *diag)
{
	gfarm_error_t e, e2;
	struct file_entry *fe;
	gfarm_uint64_t cookie;
	gfarm_int32_t gen_update_result = -1;

	if ((fe = file_table_entry(fd)) == NULL) {
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
		gflog_debug(GFARM_MSG_1003343,
			"bad file descriptor");
		return (e);
	}

#if 0
	/*
	 * For efficiency, we don't call update_file_entry_for_close() here.
	 * We expect a caller has called update_file_entry_for_close()
	 * in advance.
	 */
	update_file_entry_for_close(fd, fe);
#endif

	if ((e = fhclose_request(fe))!= GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003344,
		   "%s: fhclose request: %s", diag, gfarm_error_string(e));
	else if ((e = fhclose_result(fe, &cookie, &gen_update_result))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003345,
		    "%s: fhclose result: %s", diag, gfarm_error_string(e));

	if (e != GFARM_ERR_NO_ERROR) {
		if (fe->flags & FILE_FLAG_WRITTEN) {
			if (fe->new_gen != fe->gen)
				gflog_error(GFARM_MSG_1003509,
				    "inode %lld generation %lld -> %lld: "
				    "error occurred during close operation "
				    "for writing after gfmd failover: %s",
				    (long long)fe->ino, (long long)fe->gen,
				    (long long)fe->new_gen,
				    gfarm_error_string(e));
			else
				gflog_error(GFARM_MSG_1003510,
				    "inode %lld generation %lld: "
				    "error occurred during close operation "
				    "for writing after gfmd failover: %s",
				    (long long)fe->ino, (long long)fe->gen,
				    gfarm_error_string(e));
		}
	} else if (gen_update_result != -1) {
		if ((e2 = gfm_client_generation_updated_by_cookie_request(
		    gfm_server, cookie, gen_update_result))
		    != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003346,
			    "%s: generation_updated_by_cookie request: %s", 
			    diag, gfarm_error_string(e2));
		else if ((e2 = gfm_client_generation_updated_by_cookie_result(
		    gfm_server)) != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003347,
			    "%s: generation_updated_by_cookie result: %s", 
			    diag, gfarm_error_string(e2));
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
	}

	return (e);
}

/*
 * Take care that this function is called from running state and shutting
 * down state represeted by shutting_down.
 * If reconnect_gfm_server_for_failover() fails, gfm_server is set to NULL
 * and reenter this function from cleanup().
 */
gfarm_error_t
close_fd_somehow(gfarm_int32_t fd, const char *diag)
{
	gfarm_error_t e, e2;
	int fc = gfm_server != NULL ? gfm_client_connection_failover_count(
		gfm_server) : 0;

	if (gfm_server != NULL && client_failover_count == fc)
		e = close_fd(fd, diag);
	else
		e = GFARM_ERR_NO_ERROR;

	if (e == GFARM_ERR_NO_ERROR)
		; /*FALLTHROUGH*/
	else if (e == GFARM_ERR_BAD_FILE_DESCRIPTOR)
		return (e);
	else if (IS_CONNECTION_ERROR(e) && !shutting_down) {
		gflog_error(GFARM_MSG_1003348,
		    "%s: gfmd may be failed over, try to reconnect", diag);
		free_gfm_server();
		if ((e = connect_gfm_server_with_timeout()) !=
		    GFARM_ERR_NO_ERROR) {
			/* mark gfmd reconnection failed */
			if (gfm_server != NULL)
				free_gfm_server();
			fatal(GFARM_MSG_1003349, 
			    "%s: cannot reconnect to gfm server", diag);
		}
		if ((e = fhclose_fd(fd, diag)) == GFARM_ERR_NO_ERROR) {
			e = GFARM_ERR_GFMD_FAILED_OVER;
			gfm_client_connection_set_failover_count(
			    gfm_server, fc + 1);
		} else
			gflog_error(GFARM_MSG_1003402, "fhclose_fd : %s",
				gfarm_error_string(e));
	} else {
		fatal_metadb_proto(GFARM_MSG_1003351,
		    "close_fd_somehow", diag, e);
	}

	e2 = file_table_close(fd);
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;

	return (e);
}

static void
close_fd_adapter(void *closure, gfarm_int32_t fd)
{
	close_fd_somehow(fd, closure);
}

static void
close_all_fd(void)
{
	file_table_for_each(close_fd_adapter, "closing all descriptor");
}

void
gfs_server_close(struct gfp_xdr *client)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	static const char diag[] = "GFS_PROTO_CLOSE";

	gfs_server_get_request(client, diag, "i", &fd);
	e = close_fd_somehow(fd, diag);
	gfs_server_put_reply(client, diag, e, "");
}

void
gfs_server_pread(struct gfp_xdr *client)
{
	gfarm_int32_t fd, size;
	gfarm_int64_t offset;
	ssize_t rv;
	int save_errno = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct file_entry *fe;
	gfarm_timerval_t t1, t2;

	gfs_server_get_request(client, "pread", "iil", &fd, &size, &offset);

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	/* We truncatef i/o size bigger than GFS_PROTO_MAX_IOSIZE. */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
#if 0 /* XXX FIXME: pread(2) on NetBSD-3.0_BETA is broken */
	if ((rv = pread(file_table_get(fd), buffer, size, offset)) == -1)
#else
	rv = 0;
	if (lseek(file_table_get(fd), offset, SEEK_SET) == -1)
		save_errno = errno;
	else if ((rv = read(file_table_get(fd), buffer, size)) == -1)
#endif
		save_errno = errno;
	else
		file_table_set_read(fd);

	if (rv > 0) {
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RBYTES, rv);
	}

	gfs_profile(
		gfarm_gettimerval(&t2);
		fe = file_table_entry(fd);
		if (fe != NULL) {
			fe->nread++;
			fe->read_size += rv;
			fe->read_time += gfarm_timerval_sub(&t2, &t1);
		});

	gfs_server_put_reply_with_errno(client, "pread", save_errno,
	    "b", rv, buffer);
}

void
gfs_server_pwrite(struct gfp_xdr *client)
{
	gfarm_int32_t fd;
	size_t size;
	gfarm_int64_t offset;
	ssize_t rv;
	int save_errno = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct file_entry *fe;
	gfarm_timerval_t t1, t2;

	gfs_server_get_request(client, "pwrite", "ibl",
	    &fd, sizeof(buffer), &size, buffer, &offset);

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));
	/*
	 * We truncate i/o size bigger than GFS_PROTO_MAX_IOSIZE.
	 * This is inefficient because passed extra data are just
	 * abandoned. So client should avoid such situation.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
#if 0 /* XXX FIXME: pwrite(2) on NetBSD-3.0_BETA is broken */
	if ((rv = pwrite(file_table_get(fd), buffer, size, offset)) == -1)
#else
	rv = 0;
	if (lseek(file_table_get(fd), offset, SEEK_SET) == -1)
		save_errno = errno;
	else if ((rv = write(file_table_get(fd), buffer, size)) == -1)
#endif
		save_errno = errno;
	else
		file_table_set_written(fd);

	if (rv > 0) {
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WBYTES, rv);
	}
	gfs_profile(
		gfarm_gettimerval(&t2);
		fe = file_table_entry(fd);
		if (fe != NULL) {
			fe->nwrite++;
			fe->write_size += rv;
			fe->write_time += gfarm_timerval_sub(&t2, &t1);
		});

	gfs_server_put_reply_with_errno(client, "pwrite", save_errno,
	    "i", (gfarm_int32_t)rv);
}

void
gfs_server_write(struct gfp_xdr *client)
{
	gfarm_int32_t fd;
	size_t size;
	ssize_t rv;
	gfarm_int64_t written_offset, total_file_size;
	int save_errno = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct file_entry *fe;
	gfarm_timerval_t t1, t2;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	written_offset = total_file_size = 0;
#endif
	gfs_server_get_request(client, "write", "ib",
	    &fd, sizeof(buffer), &size, buffer);

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));
	/*
	 * We truncate i/o size bigger than GFS_PROTO_MAX_IOSIZE.
	 * This is inefficient because passed extra data are just
	 * abandoned. So client should avoid such situation.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	if ((rv = write(file_table_get(fd), buffer, size)) == -1)
		save_errno = errno;
	else {
		written_offset = lseek(fd, 0, SEEK_CUR) - rv;
		total_file_size = lseek(fd, 0, SEEK_END);
		file_table_set_written(fd);
	}
	if (rv > 0) {
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WBYTES, rv);
	}
	gfs_profile(
		gfarm_gettimerval(&t2);
		fe = file_table_entry(fd);
		if (fe != NULL) {
			fe->nwrite++;
			fe->write_size += rv;
			fe->write_time += gfarm_timerval_sub(&t2, &t1);
		});

	gfs_server_put_reply_with_errno(client, "write", save_errno,
	    "ill", (gfarm_int32_t)rv, written_offset, total_file_size);
}

void
gfs_server_ftruncate(struct gfp_xdr *client)
{
	int fd;
	gfarm_int64_t length;
	int save_errno = 0;

	gfs_server_get_request(client, "ftruncate", "il", &fd, &length);

	if (ftruncate(file_table_get(fd), (off_t)length) == -1)
		save_errno = errno;
	else
		file_table_set_written(fd);

	gfs_server_put_reply_with_errno(client, "ftruncate", save_errno, "");
}

void
gfs_server_fsync(struct gfp_xdr *client)
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

	gfs_server_put_reply_with_errno(client, "fsync", save_errno, "");
}

void
gfs_server_fstat(struct gfp_xdr *client)
{
	struct stat st;
	gfarm_int32_t fd;
	gfarm_off_t size = 0;
	gfarm_int64_t atime_sec = 0, mtime_sec = 0;
	gfarm_int32_t atime_nsec = 0, mtime_nsec = 0;
	int save_errno = 0;

	gfs_server_get_request(client, "fstat", "i", &fd);

	if (fstat(file_table_get(fd), &st) == -1)
		save_errno = errno;
	else {
		size = st.st_size;
		atime_sec = st.st_atime;
		atime_nsec = gfarm_stat_atime_nsec(&st);
		mtime_sec = st.st_mtime;
		mtime_nsec = gfarm_stat_mtime_nsec(&st);
	}

	gfs_server_put_reply_with_errno(client, "fstat", save_errno,
	    "llili", size, atime_sec, atime_nsec, mtime_sec, mtime_nsec);
}

void
gfs_server_cksum_set(struct gfp_xdr *client)
{
	gfarm_error_t e;
	int fd;
	gfarm_int32_t cksum_len;
	char *cksum_type;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	struct file_entry *fe;
	int was_written;
	time_t mtime;
	struct stat st;
	static const char diag[] = "GFS_PROTO_CKSUM_SET";

	gfs_server_get_request(client, diag, "isb", &fd,
	    &cksum_type, sizeof(cksum), &cksum_len, cksum);

	if ((fe = file_table_entry(fd)) == NULL) {
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
		gflog_debug(GFARM_MSG_1002175,
			"bad file descriptor");
	} else {
		/* NOTE: local client could use remote operation as well */
		was_written = (fe->flags & FILE_FLAG_WRITTEN) != 0;
		mtime = fe->mtime;
		if ((fe->flags & FILE_FLAG_LOCAL) == 0) { /* remote? */
			;
		} else if (fstat(fe->local_fd, &st) == -1) {
			gflog_warning(GFARM_MSG_1000494,
			    "fd %d: stat failed at cksum_set: %s",
			    fd, strerror(errno));
		} else {
			if (st.st_mtime != fe->mtime) {
				mtime = st.st_mtime;
				was_written = 1;
			}
			/* XXX FIXME st_mtimespec.tv_nsec */
		}

		if ((e = gfm_client_compound_put_fd_request(fd, diag)) !=
		    GFARM_ERR_NO_ERROR)
			fatal_metadb_proto(GFARM_MSG_1003352,
			    "compound_put_fd_request", diag, e);
		if ((e = gfm_client_cksum_set_request(gfm_server,
		    cksum_type, cksum_len, cksum,
		    was_written, (gfarm_int64_t)mtime, (gfarm_int32_t)0)) !=
		    GFARM_ERR_NO_ERROR)
			fatal_metadb_proto(GFARM_MSG_1000497,
			    "cksum_set request", diag, e);
		if ((e = gfm_client_compound_put_fd_result(diag)) !=
		    GFARM_ERR_NO_ERROR)
			fatal_metadb_proto(GFARM_MSG_1003353,
			    "compound_put_fd_result", diag, e);
		if ((e = gfm_client_cksum_set_result(gfm_server)) !=
		    GFARM_ERR_NO_ERROR) {
			if (debug_mode)
				gflog_info(GFARM_MSG_1000501,
				    "cksum_set(%s) result: %s", diag,
				    gfarm_error_string(e));
		} else if ((e = gfm_client_compound_end(diag)) != 
		    GFARM_ERR_NO_ERROR) {
			fatal_metadb_proto(GFARM_MSG_1003354,
			    "compound_end", diag, e);
		}
	}

	gfs_server_put_reply(client, diag, e, "");
}

static int
is_readonly_mode(void)
{
	struct stat st;
	int length;
	static char *p = NULL;
	static const char diag[] = "is_readonly_mode";

	if (p == NULL) {
		length = gfarm_spool_root_len + 1 +
			sizeof(READONLY_CONFIG_FILE);
		GFARM_MALLOC_ARRAY(p, length);
		if (p == NULL)
			fatal(GFARM_MSG_1000503, "%s: no memory for %d bytes",
			    diag, length);
		snprintf(p, length, "%s/%s", gfarm_spool_root,
			 READONLY_CONFIG_FILE);
	}		
	return (stat(p, &st) == 0);
}

void
gfs_server_statfs(struct gfp_xdr *client)
{
	char *dir;
	int save_errno = 0;
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;

	/*
	 * do not use dir since there is no way to know gfarm_spool_root.
	 * this code is kept for backward compatibility reason.
	 */
	gfs_server_get_request(client, "statfs", "s", &dir);

	save_errno = gfsd_statfs(gfarm_spool_root, &bsize,
	    &blocks, &bfree, &bavail,
	    &files, &ffree, &favail);
	free(dir);

	if (save_errno == 0 && is_readonly_mode()) {
		/* pretend to be disk full, to make this gfsd read-only */
		bavail -= bfree;
		bfree = 0;
	}

	gfs_server_put_reply_with_errno(client, "statfs", save_errno,
	    "illllll", bsize, blocks, bfree, bavail, files, ffree, favail);
}

static gfarm_error_t
replica_adding(gfarm_int32_t net_fd, char *src_host,
	gfarm_ino_t *inop, gfarm_uint64_t *genp,
	gfarm_int64_t *mtime_secp, gfarm_int32_t *mtime_nsecp,
	const char *request)
{
	gfarm_error_t e;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	gfarm_int64_t mtime_sec;
	gfarm_int32_t mtime_nsec;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDING";

	if ((e = gfm_client_compound_put_fd_request(net_fd, diag))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1003355,
		    "compound_put_fd_request", diag, e);
	if ((e = gfm_client_replica_adding_request(gfm_server, src_host))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1000506,
		    request, diag, e);
	if ((e = gfm_client_compound_put_fd_result(diag))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1003356,
		    "compound_put_fd_result", diag, e);
	if ((e = gfm_client_replica_adding_result(gfm_server,
	    &ino, &gen, &mtime_sec, &mtime_nsec))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000510,
			    "%s result error on %s: %s", diag, request,
			    gfarm_error_string(e));
	} else if ((e = gfm_client_compound_end(diag))
	    != GFARM_ERR_NO_ERROR) {
		fatal_metadb_proto(GFARM_MSG_1003357,
		    "compound_end", diag, e);
	} else {
		*inop = ino;
		*genp = gen;
		*mtime_secp = mtime_sec;
		*mtime_nsecp = mtime_nsec;
	}
	return (e);
}

static gfarm_error_t
replica_added(gfarm_int32_t net_fd,
    gfarm_int32_t flags, gfarm_int64_t mtime_sec, gfarm_int32_t mtime_nsec,
    gfarm_off_t size, const char *request)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDED2";

	if ((e = gfm_client_compound_put_fd_request(net_fd, diag))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1003358,
		    "compound_put_fd_request", diag, e);
	if ((e = gfm_client_replica_added2_request(gfm_server,
	    flags, mtime_sec, mtime_nsec, size))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1000514, request, diag, e);
	if ((e = gfm_client_compound_put_fd_result(diag))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto(GFARM_MSG_1003359,
		    "compound_put_fd_result", diag, e);
	if ((e = gfm_client_replica_added_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000518,
			    "%s result on %s: %s", diag, request,
			    gfarm_error_string(e));
	} else if ((e = gfm_client_compound_end(diag))
	    != GFARM_ERR_NO_ERROR) {
		fatal_metadb_proto(GFARM_MSG_1003360,
		    "compound_end", diag, e);
	}
	return (e);
}

void
gfs_server_replica_add_from(struct gfp_xdr *client)
{
	gfarm_int32_t net_fd, local_fd, port, mtime_nsec = 0;
	gfarm_int64_t mtime_sec = 0;
	gfarm_ino_t ino = 0;
	gfarm_uint64_t gen = 0;
	gfarm_error_t e, e2;
	char *host, *path;
	struct gfs_connection *server;
	int flags = 0; /* XXX - for now */
	struct stat sb;
	static const char diag[] = "GFS_PROTO_REPLICA_ADD_FROM";

	sb.st_size = -1;
	gfs_server_get_request(client, diag, "sii", &host, &port, &net_fd);

	e = replica_adding(net_fd, host, &ino, &gen, &mtime_sec, &mtime_nsec,
	    diag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002176,
			"replica_adding() failed: %s",
			gfarm_error_string(e));
		goto free_host;
	}

	gfsd_local_path(ino, gen, diag, &path);
	local_fd = open_data(path, O_WRONLY|O_CREAT|O_TRUNC);
	free(path);
	if (local_fd < 0) {
		e = gfarm_errno_to_error(errno);
		/* invalidate the creating file replica */
		mtime_sec = mtime_nsec = 0;
		goto adding_cancel;
	}

	e = gfs_client_connection_acquire_by_host(gfm_server, host, port,
	    &server, listen_addrname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002177,
			"gfs_client_connection_acquire_by_host() failed: %s",
			gfarm_error_string(e));
		mtime_sec = mtime_nsec = 0; /* invalidate */
		goto close;
	}
	e = gfs_client_replica_recv(server, ino, gen, local_fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002178,
			"gfs_client_replica_recv() failed: %s",
			gfarm_error_string(e));
		mtime_sec = mtime_nsec = 0; /* invalidate */
		goto free_server;
	}
	if (fstat(local_fd, &sb) == -1) {
		e = gfarm_errno_to_error(errno);
		mtime_sec = mtime_nsec = 0; /* invalidate */
	}
 free_server:
	gfs_client_connection_free(server);
 close:
	close(local_fd);
 adding_cancel:
	e2 = replica_added(net_fd, flags, mtime_sec, mtime_nsec, sb.st_size,
	    diag);
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;
 free_host:
	free(host);
	gfs_server_put_reply(client, diag, e, "");
	return;
}

void
gfs_server_replica_recv(struct gfp_xdr *client,
	enum gfarm_auth_id_type peer_type)
{
	gfarm_error_t e, error = GFARM_ERR_NO_ERROR;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	ssize_t rv;
	char buffer[GFS_PROTO_MAX_IOSIZE];
#if 0 /* not yet in gfarm v2 */
	struct gfs_client_rep_rate_info *rinfo = NULL;
#endif
	char *path;
	int local_fd;
	unsigned long long msl = 1, total_msl = 0; /* sleep millisec. */
	static const char diag[] = "GFS_PROTO_REPLICA_RECV";

	gfs_server_get_request(client, diag, "ll", &ino, &gen);
	/* from gfsd only */
	if (peer_type != GFARM_AUTH_ID_TYPE_SPOOL_HOST) {
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002179,
			"operation is not permitted(peer_type)");
		goto send_eof;
	}

	gfsd_local_path(ino, gen, diag, &path);
	for (;;) {
		local_fd = open_data(path, O_RDONLY);
		if (local_fd >= 0)
			break; /* success */
		if (errno != ENOENT || total_msl >= 3000) { /* 3 sec. */
			error = gfarm_errno_to_error(errno);
			gflog_error(GFARM_MSG_1003511,
			    "open_data(%lld:%lld): %s",
			    (long long) ino, (long long) gen,
			    gfarm_error_string(error));
			free(path);
			goto send_eof;
		}
		/* ENOENT: wait generation-update, retry open_data() */
		gfarm_nanosleep(
		    (unsigned long long)msl * GFARM_MILLISEC_BY_NANOSEC);
		total_msl += msl;
		msl *= 2;
		gflog_info(GFARM_MSG_1003512,
		    "retry open_data(%lld:%lld): sleep %lld msec.",
		    (long long) ino, (long long) gen, (long long) total_msl);
	}
	free(path);

	/* data transfer */
	if (file_read_size >= sizeof(buffer))
		file_read_size = sizeof(buffer);
#if 0 /* not yet in gfarm v2 */
	if (rate_limit != 0) {
		rinfo = gfs_client_rep_rate_info_alloc(rate_limit);
		if (rinfo == NULL)
			fatal("%s:rate_info_alloc: %s", diag,
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
	}
#endif
	do {
		rv = read(local_fd, buffer, file_read_size);
		if (rv <= 0) {
			if (rv == -1)
				error = gfarm_errno_to_error(errno);
			break;
		}
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RBYTES, rv);
		e = gfp_xdr_send(client, "b", rv, buffer);
		if (e != GFARM_ERR_NO_ERROR) {
			error = e;
			gflog_debug(GFARM_MSG_1002180,
				"gfp_xdr_send() failed: %s",
				gfarm_error_string(e));
			break;
		}
		if (file_read_size < GFS_PROTO_MAX_IOSIZE) {
			e = gfp_xdr_flush(client);
			if (e != GFARM_ERR_NO_ERROR) {
				error = e;
				gflog_debug(GFARM_MSG_1002181,
					"gfp_xdr_send() failed: %s",
					gfarm_error_string(e));
				break;
			}
		}
#if 0 /* not yet in gfarm v2 */
		if (rate_limit != 0)
			gfs_client_rep_rate_control(rinfo, rv);
#endif
	} while (rv > 0);

#if 0 /* not yet in gfarm v2 */
	if (rinfo != NULL)
		gfs_client_rep_rate_info_free(rinfo);
#endif
	e = close(local_fd);
	if (error == GFARM_ERR_NO_ERROR)
		error = e;
 send_eof:
	/* send EOF mark */
	e = gfp_xdr_send(client, "b", 0, buffer);
	if (error == GFARM_ERR_NO_ERROR)
		error = e;

	gfs_server_put_reply(client, diag, error, "");
}

/* from gfmd */

gfarm_error_t
gfs_async_server_fhstat(struct gfp_xdr *conn, gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	struct stat st;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	gfarm_off_t filesize = 0;
	gfarm_int64_t atime_sec = 0, mtime_sec = 0;
	gfarm_int32_t atime_nsec = 0, mtime_nsec = 0;
	int save_errno = 0;
	char *path;

	e = gfs_async_server_get_request(conn, size, "fhstat",
	    "ll", &ino, &gen);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	gfsd_local_path(ino, gen, "fhstat", &path);
	if (stat(path, &st) == -1)
		save_errno = errno;
	else {
		filesize = st.st_size;
		atime_sec = st.st_atime;
		atime_nsec = gfarm_stat_atime_nsec(&st);
		mtime_sec = st.st_mtime;
		mtime_nsec = gfarm_stat_mtime_nsec(&st);
	}
	free(path);

	return (gfs_async_server_put_reply_with_errno(conn, xid,
	    "fhstat", save_errno,
	    "llili", filesize, atime_sec, atime_nsec, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfs_async_server_fhremove(struct gfp_xdr *conn, gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	int save_errno = 0;
	char *path;

	e = gfs_async_server_get_request(conn, size, "fhremove",
	    "ll", &ino, &gen);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	gfsd_local_path(ino, gen, "fhremove", &path);
	if (unlink(path) == -1)
		save_errno = errno;
	free(path);

	return (gfs_async_server_put_reply_with_errno(conn, xid,
	    "fhremove", save_errno, ""));
}

gfarm_error_t
gfs_async_server_status(struct gfp_xdr *conn, gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	int save_errno = 0;
	double loadavg[3];
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;
	gfarm_off_t used = 0, avail = 0;
	static const char diag[] = "gfs_server_status";

	/* just check that size == 0 */
	e = gfs_async_server_get_request(conn, size, "status", "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (getloadavg(loadavg, GFARM_ARRAY_LENGTH(loadavg)) == -1) {
		save_errno = EPERM; /* XXX */
		gflog_warning(GFARM_MSG_1000520,
		    "%s: cannot get load average", diag);
	} else {
		save_errno = gfsd_statfs(gfarm_spool_root, &bsize,
			&blocks, &bfree, &bavail, &files, &ffree, &favail);

		/* pretend to be disk full, to make this gfsd read-only */
		if (save_errno == 0 && is_readonly_mode()) {
			bavail -= bfree;
			bfree = 0;
		}
		if (save_errno == 0) {
			used = (blocks - bfree) * bsize / 1024;
			avail = bavail * bsize / 1024;
		}
	}
	e = gfs_async_server_put_reply_with_errno(conn, xid,
	    "status", save_errno,
	    "fffll", loadavg[0], loadavg[1], loadavg[2], used, avail);
	/* die if save_errno != 0 since the gfmd disconnects the connection */
	if (save_errno != 0) {
		kill_master_gfsd = 1;
		fatal(GFARM_MSG_1003684, "%s: %s, die", diag,
		    strerror(save_errno));
	}
	return (e);
}

static struct gfarm_hash_table *replication_queue_set = NULL;

/* per source-host queue */
struct replication_queue_data {
	struct replication_request *head;
	struct replication_request **tail;
};

gfarm_error_t
replication_queue_lookup(const char *hostname, int port,
	const char *user, struct gfarm_hash_entry **qp)
{
	gfarm_error_t e;
	int created;
	struct gfarm_hash_entry *q;
	struct replication_queue_data *qd;

	e = gfp_conn_hash_enter(&replication_queue_set, HOST_HASHTAB_SIZE,
	    sizeof(*qd), hostname, port, user,
	    &q, &created);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002514,
		    "creating replication queue for %s:%d: %s",
		    hostname, port, gfarm_error_string(e));
		return (e);
	}
	qd = gfarm_hash_entry_data(q);
	if (created) {
		qd->head = NULL;
		qd->tail = &qd->head;
	}
	*qp = q;
	return (GFARM_ERR_NO_ERROR);
}

struct replication_request {
	/* only used when actual replication is ongoing */
	struct replication_request *ongoing_next, *ongoing_prev;

	struct replication_request *q_next;
	struct gfarm_hash_entry *q;

	gfp_xdr_xid_t xid;
	gfarm_ino_t ino;
	gfarm_int64_t gen;

	/* the followings are only used when actual replication is ongoing */
	struct gfs_connection *src_gfsd;
	int file_fd, pipe_fd;
	pid_t pid;

};

/* dummy header of doubly linked circular list */
struct replication_request ongoing_replications = 
	{ &ongoing_replications, &ongoing_replications };

struct replication_errcodes {
	gfarm_int32_t src_errcode;
	gfarm_int32_t dst_errcode;
};

gfarm_error_t
try_replication(struct gfp_xdr *conn, struct gfarm_hash_entry *q,
	gfarm_error_t *src_errp, gfarm_error_t *dst_errp)
{
	gfarm_error_t e, dst_err = GFARM_ERR_NO_ERROR;
	gfarm_error_t conn_err = GFARM_ERR_NO_ERROR;
	struct replication_queue_data *qd = gfarm_hash_entry_data(q);
	struct replication_request *rep = qd->head;
	char *path;
	struct gfs_connection *src_gfsd;
	int save_errno, fds[2];
	pid_t pid = -1;
	struct replication_errcodes errcodes;
	int local_fd, rv;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST";
	struct gfarm_iostat_items *statp;

	statp = gfarm_iostat_find_space(0);

	/*
	 * XXX FIXME:
	 * gfs_client_connection_acquire_by_host() needs timeout, otherwise
	 * the remote gfsd (or its kernel) can block this backchannel gfsd.
	 * See http://sourceforge.net/apps/trac/gfarm/ticket/130
	 */
	gfsd_local_path(rep->ino, rep->gen, diag, &path);
	local_fd = open_data(path, O_WRONLY|O_CREAT|O_TRUNC);
	free(path);
	if (local_fd < 0) {
		dst_err = gfarm_errno_to_error(errno);
		gflog_notice(GFARM_MSG_1002182,
		    "%s: cannot open local file for %lld:%lld: %s", diag,
		    (long long)rep->ino, (long long)rep->gen, strerror(errno));
	} else if ((conn_err = gfs_client_connection_acquire_by_host(gfm_server,
	    gfp_conn_hash_hostname(q), gfp_conn_hash_port(q),
	    &src_gfsd, listen_addrname)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002184, "%s: connecting to %s:%d: %s",
		    diag,
		    gfp_conn_hash_hostname(q), gfp_conn_hash_port(q),
		    gfarm_error_string(conn_err));
		close(local_fd);
	} else if (pipe(fds) == -1) {
		dst_err = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1002185, "%s: cannot create pipe: %s",
		    diag, strerror(errno));
		gfs_client_connection_free(src_gfsd);
		close(local_fd);
#ifndef HAVE_POLL /* i.e. use select(2) */
	} else if (fds[0] > FD_SETSIZE) { /* for select(2) */
		dst_err = GFARM_ERR_TOO_MANY_OPEN_FILES;
		gflog_error(GFARM_MSG_1002186, "%s: cannot select %d: %s",
		    diag, fds[0], gfarm_error_string(dst_err));
		close(fds[0]);
		close(fds[1]);
		gfs_client_connection_free(src_gfsd);
		close(local_fd);
#endif
	} else if ((pid = fork()) == 0) { /* child */
		if (statp) {
			gfarm_iostat_set_id(statp, (gfarm_uint64_t) getpid());
			gfarm_iostat_set_local_ip(statp);
		}
		close(fds[0]);
		e = gfs_client_replica_recv(src_gfsd, rep->ino, rep->gen,
		    local_fd);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1003513,
			    "%s: replica_recv %lld:%lld: %s",
			    diag, (long long)rep->ino, (long long)rep->gen,
			    gfarm_error_string(e));
		}
		rv = close(local_fd);
		if (rv == 0) {
			save_errno = 0;
		} else {
			save_errno = errno;
			gflog_error(GFARM_MSG_1003514,
			    "%s: replica_recv %lld:%lld: close: %s",
			    diag, (long long)rep->ino, (long long)rep->gen,
			    strerror(save_errno));
		}
		/*
		 * XXX FIXME
		 * modify gfs_client_replica_recv() interface to return
		 * the error codes for both source and destination side.
		 */
		if (IS_CONNECTION_ERROR(e) ||
		    e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY ||
		    e == GFARM_ERR_PERMISSION_DENIED) {
			errcodes.src_errcode = e;
			errcodes.dst_errcode = gfarm_errno_to_error(save_errno);
		} else {
			errcodes.src_errcode = GFARM_ERR_NO_ERROR;
			errcodes.dst_errcode = e != GFARM_ERR_NO_ERROR ? e :
			    gfarm_errno_to_error(save_errno);
		}
		if ((rv = write(fds[1], &errcodes, sizeof(errcodes))) == -1)
			gflog_error(GFARM_MSG_1002188, "%s: write pipe: %s",
			    diag, strerror(errno));
		else if (rv != sizeof(errcodes))
			gflog_error(GFARM_MSG_1002189, "%s: partial write: "
			    "%d < %d", diag, rv, (int)sizeof(e));
		close(fds[1]);
		exit(e == GFARM_ERR_NO_ERROR ? 0 : 1);
	} else { /* parent */
		if (pid == -1) {
			dst_err = gfarm_errno_to_error(errno);
			gflog_error(GFARM_MSG_1002190,
			    "%s: cannot create child process: %s",
			    diag, strerror(errno));
			close(fds[0]);
			gfs_client_connection_free(src_gfsd);
			close(local_fd);
		} else {
			if (statp) {
				gfarm_iostat_set_id(statp, (gfarm_uint64_t)pid);
				statp = NULL;
			}
			rep->src_gfsd = src_gfsd;
			rep->file_fd = local_fd;
			rep->pipe_fd = fds[0];
			rep->pid = pid;
			rep->ongoing_next = &ongoing_replications;
			rep->ongoing_prev = ongoing_replications.ongoing_prev;
			ongoing_replications.ongoing_prev->ongoing_next = rep;
			ongoing_replications.ongoing_prev = rep;
		}
		close(fds[1]);
	}
	if (statp)
		gfarm_iostat_clear_ip(statp);

	*src_errp = conn_err;
	*dst_errp = dst_err;

	/* XXX FIXME, src_err and dst_err should be passed separately */
	return (gfs_async_server_put_reply(conn, rep->xid, diag,
	    conn_err != GFARM_ERR_NO_ERROR ? conn_err : dst_err,
	    "l", (gfarm_int64_t)pid));
}

gfarm_error_t
start_replication(struct gfp_xdr *conn, struct gfarm_hash_entry *q)
{
	gfarm_error_t gfmd_err, dst_err, src_err;
	gfarm_error_t src_net_err = GFARM_ERR_NO_ERROR;
	int src_net_err_count = 0;
	struct replication_queue_data *qd = gfarm_hash_entry_data(q);
	struct replication_request *rep;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST";

	do {
		if (src_net_err_count > 1) {
			rep = qd->head;
			/*
			 * avoid retries, because this may take long time,
			 * if the host is down or network is unreachable.
			 */
			gflog_warning(GFARM_MSG_1002515,
			    "skipping replication for %lld:%lld, "
			    "because %s:%d is down: %s",
			    (long long)rep->ino, (long long)rep->gen,
			    gfp_conn_hash_hostname(q), gfp_conn_hash_port(q),
			    gfarm_error_string(src_net_err));

			/*
			 * XXX FIXME
			 * src_err and dst_err should be passed separately
			 */
			gfmd_err = gfs_async_server_put_reply(conn, rep->xid,
			    diag, src_net_err, "");
			if (gfmd_err != GFARM_ERR_NO_ERROR)
				return (gfmd_err);
		} else {
			gfmd_err = try_replication(conn, q,
			    &src_err, &dst_err);
			if (gfmd_err != GFARM_ERR_NO_ERROR)
				return (gfmd_err);
			if (src_err == GFARM_ERR_NO_ERROR &&
			    dst_err == GFARM_ERR_NO_ERROR)
				return (GFARM_ERR_NO_ERROR);
			if (IS_CONNECTION_ERROR(src_err)) {
				src_net_err = src_err;
				++src_net_err_count;
			}
		}

		/*
		 * failed to start a replication, try next entry.
		 *
		 * we don't have to touch rep->ongoing_{next,prev} here,
		 * since they are updated only after a replication actually
		 * started or finished.
		 */
		rep = qd->head->q_next;
		free(qd->head);

		qd->head = rep;
	} while (rep != NULL);

	qd->tail = &qd->head;
	return (GFARM_ERR_NO_ERROR); /* no gfmd_err */
}

gfarm_error_t
gfs_async_server_replication_request(struct gfp_xdr *conn,
	const char *user, gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	char *host;
	gfarm_int32_t port;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	struct gfarm_hash_entry *q;
	struct replication_queue_data *qd;
	struct replication_request *rep;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST";

	e = gfs_async_server_get_request(conn, size, diag,
	    "sill", &host, &port, &ino, &gen);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if ((e = replication_queue_lookup(host, port, user, &q)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002516,
		    "cannot allocate replication queue for %s:%d: %s",
		    host, port, gfarm_error_string(e));
	} else {
		GFARM_MALLOC(rep);
		if (rep == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_error(GFARM_MSG_1002517,
			    "cannot allocate replication record for "
			    "%s:%d %lld:%lld: no memory",
			    host, port, (long long)ino, (long long)gen);
		} else {
			free(host);

			rep->xid = xid;
			rep->ino = ino;
			rep->gen = gen;

			/* not set yet, will be set in try_replication() */
			rep->src_gfsd = NULL;
			rep->file_fd = -1;
			rep->pipe_fd = -1;
			rep->pid = 0;
			rep->ongoing_next = rep->ongoing_prev = rep;

			rep->q = q;
			rep->q_next = NULL;

			qd = gfarm_hash_entry_data(q);
			*qd->tail = rep;
			qd->tail = &rep->q_next;
			if (qd->head == rep) { /* this host is idle */
				return (start_replication(conn, q));
			} else { /* the replication is postponed */
				return (GFARM_ERR_NO_ERROR);
			}
		}
	}
	free(host);

	/* only used in an error case */
	return (gfs_async_server_put_reply(conn, xid, diag, e, ""));
}

#if 0 /* not yet in gfarm v2 */

void
gfs_server_striping_read(struct gfp_xdr *client)
{
	gfarm_error_t e;
	gfarm_int32_t fd, interleave_factor;
	gfarm_off_t offset, size, full_stripe_size;
	gfarm_off_t chunk_size;
	ssize_t rv;
	gfarm_error_t error = GFARM_ERR_NO_ERROR;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct gfs_client_rep_rate_info *rinfo = NULL;

	gfs_server_get_request(client, "striping_read", "iooio", &fd,
	    &offset, &size, &interleave_factor, &full_stripe_size);

	if (file_read_size >= sizeof(buffer))
		file_read_size = sizeof(buffer);
	if (rate_limit != 0) {
		rinfo = gfs_client_rep_rate_info_alloc(rate_limit);
		if (rinfo == NULL)
			fatal("striping_read:rate_info_alloc: %s",
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
	}

	fd = file_table_get(fd);
	if (lseek(fd, (off_t)offset, SEEK_SET) == -1) {
		error = gfarm_errno_to_error(errno);
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
					    gfarm_errno_to_error(errno);
				goto finish;
			}
			gfarm_iostat_local_add(GFARM_IOSTAT_IO_RCOUNT, 1);
			gfarm_iostat_local_add(GFARM_IOSTAT_IO_RBYTES, rv);
			e = gfp_xdr_send(client, "b", rv, buffer);
			if (e != GFARM_ERR_NO_ERROR) {
				error = e;
				goto finish;
			}
			if (file_read_size < GFS_PROTO_MAX_IOSIZE) {
				e = gfp_xdr_flush(client);
				if (e != GFARM_ERR_NO_ERROR) {
					error = e;
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
			error = gfarm_errno_to_error(errno);
			break;
		}
	}
 finish:
	if (rinfo != NULL)
		gfs_client_rep_rate_info_free(rinfo);
	/* send EOF mark */
	e = gfp_xdr_send(client, "b", 0, buffer);
	if (e != GFARM_ERR_NO_ERROR && error == GFARM_ERR_NO_ERROR)
		error = e;

	gfs_server_put_reply(client, "striping_read", error, "");
}

void
gfs_server_replicate_file_sequential_common(struct gfp_xdr *client,
	char *file, gfarm_int32_t mode,
	char *src_canonical_hostname, char *src_if_hostname)
{
	gfarm_error_t e;
	char *path;
	struct gfs_connection *src_conn;
	int fd, src_fd;
	long file_sync_rate;
	gfarm_error_t error = GFARM_ERR_NO_ERROR;
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
		if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
			gflog_warning(GFARM_MSG_1000521, "file_sync_rate: %s",
			    gfarm_error_string(e));

		/*
		 * the following gfs_client_connect() accesses user & home
		 * information which was set in gfarm_authorize()
		 * with switch_to==1.
		 */
		e = gfs_client_connect(src_canonical_hostname,
		    gfarm_spool_server_port, (struct sockaddr *)&peer_addr,
		    &src_conn);
	}
	free(src_canonical_hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		error = e;
		gflog_warning(GFARM_MSG_1000522,
		    "replicate_file_seq:remote_connect: %s",
		    gfarm_error_string(e));
	} else {
		e = gfs_client_open(src_conn, file, GFARM_FILE_RDONLY, 0,
				    &src_fd);
		if (e != GFARM_ERR_NO_ERROR) {
			error = e;
			gflog_warning(GFARM_MSG_1000523,
			    "replicate_file_seq:remote_open: %s",
			    gfarm_error_string(e));
		} else {
			e = gfarm_path_localize(file, &path);
			if (e != GFARM_ERR_NO_ERROR)
				fatal(GFARM_MSG_1000524,
				    "replicate_file_seq:path: %s",
				    gfarm_error_string(e));
			fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
			if (fd < 0) {
				error = gfarm_errno_to_error(errno);
				gflog_warning_errno(GFARM_MSG_1000525,
				    "replicate_file_seq:local_open");
			} else {
				e = gfs_client_copyin(src_conn, src_fd, fd,
				    file_sync_rate);
				if (e != GFARM_ERR_NO_ERROR) {
					error = e;
					gflog_warning(GFARM_MSG_1000526,
					    "replicate_file_seq:copyin: %s",
					    gfarm_error_string(e));
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
gfs_server_replicate_file_sequential_old(struct gfp_xdr *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;

	gfs_server_get_request(client, "replicate_file_seq_old",
	    "sis", &file, &mode, &src_if_hostname);

	src_canonical_hostname = strdup(src_if_hostname);
	if (src_canonical_hostname == NULL) {
		gfs_server_put_reply(client, "replicate_file_seq_old",
		    GFARM_ERR_NO_MEMORY, "");
		return;
	}
	gfs_server_replicate_file_sequential_common(client, file, mode,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_replicate_file_sequential(struct gfp_xdr *client)
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

gfarm_error_t
simple_division(int ofd, struct parallel_stream *divisions,
	off_t file_size, int n)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	gfarm_off_t offset = 0, residual = file_size;
	gfarm_off_t size_per_division = file_size / n;
	int i;

	if ((size_per_division / iosize_alignment) *
	    iosize_alignment != size_per_division) {
		size_per_division =
		    ((size_per_division / iosize_alignment) + 1) *
		    iosize_alignment;
	}

	for (i = 0; i < n; i++) {
		gfarm_off_t size;

		if (residual <= 0 || e_save != GFARM_ERR_NO_ERROR) {
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
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			gflog_warning(GFARM_MSG_1000527,
			    "replicate_file_division:copyin: %s",
			    gfarm_error_string(e));
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		divisions[i].state = GSRFP_COPYING;
	}
	return (e_save);
}

gfarm_error_t
striping(int ofd, struct parallel_stream *divisions,
	off_t file_size, int n, int interleave_factor)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	gfarm_off_t full_stripe_size = (gfarm_off_t)interleave_factor * n;
	gfarm_off_t stripe_number = file_size / full_stripe_size;
	gfarm_off_t size_per_division = interleave_factor * stripe_number;
	gfarm_off_t residual = file_size - full_stripe_size * stripe_number;
	gfarm_off_t chunk_number_on_last_stripe;
	gfarm_off_t last_chunk_size;
	gfarm_off_t offset = 0;
	int i;

	if (residual == 0) {
		chunk_number_on_last_stripe = 0;
		last_chunk_size = 0;
	} else {
		chunk_number_on_last_stripe = residual / interleave_factor;
		last_chunk_size = residual - 
		    interleave_factor * chunk_number_on_last_stripe;
	}

	for (i = 0; i < n; i++) {
		gfarm_off_t size = size_per_division;

		if (i < chunk_number_on_last_stripe)
			size += interleave_factor;
		else if (i == chunk_number_on_last_stripe)
			size += last_chunk_size;
		if (size <= 0 || e_save != GFARM_ERR_NO_ERROR) {
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		e = gfs_client_striping_copyin_request(
		    divisions[i].src_conn, divisions[i].src_fd, ofd,
		    offset, size, interleave_factor, full_stripe_size);
		offset += interleave_factor;
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			gflog_warning(GFARM_MSG_1000528,
			    "replicate_file_stripe:copyin: %s",
			    gfarm_error_string(e));
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		divisions[i].state = GSRFP_COPYING;
	}
	return (e_save);
}

void
limit_division(int *ndivisionsp, gfarm_off_t file_size)
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
gfs_server_replicate_file_parallel_common(struct gfp_xdr *client,
	char *file, gfarm_int32_t mode, gfarm_off_t file_size,
	gfarm_int32_t ndivisions, gfarm_int32_t interleave_factor,
	char *src_canonical_hostname, char *src_if_hostname)
{
	struct parallel_stream *divisions;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	char *path;
	long file_sync_rate, written;
	int i, j, n, ofd;
	gfarm_error_t error = GFARM_ERR_NO_ERROR;
	struct hostent *hp;
	struct sockaddr_in peer_addr;

	e = gfarm_path_localize(file, &path);
	if (e != GFARM_ERR_NO_ERROR)
		fatal(GFARM_MSG_1000529, "replicate_file_par: %s",
		    gfarm_error_string(e));
	ofd = open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
	free(path);
	if (ofd == -1) {
		error = gfarm_errno_to_error(errno);
		gflog_warning_errno(GFARM_MSG_1000530,
		    "replicate_file_par:local_open");
		goto finish;
	}

	limit_division(&ndivisions, file_size);

	GFARM_MALLOC_ARRAY(divisions, ndivisions);
	if (divisions == NULL) {
		error = GFARM_ERR_NO_MEMORY;
		goto finish_ofd;
	}

	hp = gethostbyname(src_if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET) {
		error = GFARM_ERR_CONNECTION_REFUSED;
		goto finish_free_divisions;
	}
	memset(&peer_addr, 0, sizeof(peer_addr));
	memcpy(&peer_addr.sin_addr, hp->h_addr, sizeof(peer_addr.sin_addr));
	peer_addr.sin_family = hp->h_addrtype;
	peer_addr.sin_port = htons(gfarm_spool_server_port);

	e = gfarm_netparam_config_get_long(&gfarm_netparam_file_sync_rate,
	    src_canonical_hostname, (struct sockaddr *)&peer_addr,
	    &file_sync_rate);
	if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
		gflog_warning(GFARM_MSG_1000531, "file_sync_rate: %s",
		    gfarm_error_string(e));

	/* XXX - this should be done in parallel rather than sequential */
	for (i = 0; i < ndivisions; i++) {

		e = gfs_client_connect(src_canonical_hostname,
		    gfarm_spool_server_port, (struct sockaddr *)&peer_addr,
		    &divisions[i].src_conn);
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			gflog_warning(GFARM_MSG_1000532,
			    "replicate_file_par:remote_connect: %s",
			    gfarm_error_string(e));
			break;
		}
	}
	n = i;
	if (n == 0) {
		error = e;
		goto finish_free_divisions;
	}
	e_save = GFARM_ERR_NO_ERROR; /* not fatal */

	/* XXX - this should be done in parallel rather than sequential */
	for (i = 0; i < n; i++) {
		e = gfs_client_open(divisions[i].src_conn, file,
		    GFARM_FILE_RDONLY, 0, &divisions[i].src_fd);
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			gflog_warning(GFARM_MSG_1000533,
			    "replicate_file_par:remote_open: %s",
			    gfarm_error_string(e));

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
		error = e_save;
		goto finish_free_divisions;
	}
	e_save = GFARM_ERR_NO_ERROR; /* not fatal */

	if (interleave_factor == 0) {
		e = simple_division(ofd, divisions, file_size, n);
	} else {
		e = striping(ofd, divisions, file_size, n, interleave_factor);
	}
	e_save = e;

	written = 0;
	/*
	 * XXX - we cannot stop here, even if e_save != GFARM_ERR_NO_ERROR,
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
				fatal(GFARM_MSG_1000534, "replicate_file_par: "
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
				gflog_warning_errno(GFARM_MSG_1000535,
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
			if (e != GFARM_ERR_NO_ERROR) {
				if (e_save == GFARM_ERR_NO_ERROR)
					e_save = e;
				divisions[i].state = GSRFP_FINISH; /* XXX */
			} else if (rv == 0) {
				divisions[i].state = GSRFP_FINISH;
				e = gfs_client_striping_copyin_result(
				    divisions[i].src_conn);
				if (e != GFARM_ERR_NO_ERROR) {
					if (e_save == GFARM_ERR_NO_ERROR)
						e_save = e;
				} else {
					e = gfs_client_close(
					    divisions[i].src_conn,
					    divisions[i].src_fd);
					if (e_save == GFARM_ERR_NO_ERROR)
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
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	if (e_save != GFARM_ERR_NO_ERROR)
		error = e_save;

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
gfs_server_replicate_file_parallel_old(struct gfp_xdr *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;
	gfarm_int32_t ndivisions; /* parallel_streams */
	gfarm_int32_t interleave_factor; /* stripe_unit_size, chuck size */
	gfarm_off_t file_size;

	gfs_server_get_request(client, "replicate_file_par_old", "sioiis",
	    &file, &mode, &file_size, &ndivisions, &interleave_factor,
	    &src_if_hostname);

	src_canonical_hostname = strdup(src_if_hostname);
	if (src_canonical_hostname == NULL) {
		gfs_server_put_reply(client, "replicate_file_par_old",
		    GFARM_ERR_NO_MEMORY, "");
		return;
	}
	gfs_server_replicate_file_parallel_common(client,
	    file, mode, file_size, ndivisions, interleave_factor,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_replicate_file_parallel(struct gfp_xdr *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;
	gfarm_int32_t ndivisions; /* parallel_streams */
	gfarm_int32_t interleave_factor; /* stripe_unit_size, chuck size */
	gfarm_off_t file_size;

	gfs_server_get_request(client, "replicate_file_par", "sioiiss",
	    &file, &mode, &file_size, &ndivisions, &interleave_factor,
	    &src_canonical_hostname, &src_if_hostname);

	gfs_server_replicate_file_parallel_common(client,
	    file, mode, file_size, ndivisions, interleave_factor,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_chdir(struct gfp_xdr *client)
{
	char *gpath, *path;
	int save_errno = 0;
	char *msg = "chdir";

	gfs_server_get_request(client, msg, "s", &gpath);

	gfsd_local_path(gpath, &path, msg);
	if (chdir(path) == -1)
		save_errno = errno;
	free(path);

	gfs_server_put_reply_with_errno(client, msg, save_errno, "");
	check_input_output_error(msg, save_errno);
}

#endif /* not yet in gfarm v2 */

static int got_sigchld;
void
sigchld_handler(int sig)
{
	got_sigchld = 1;
}
void
clear_child(void)
{
	pid_t pid;
	int  status;

	got_sigchld = 0;
	for (;;) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid == -1 || pid == 0)
			break;
		gfarm_iostat_clear_id(pid, 0);
	}
}

void
server(int client_fd, char *client_name, struct sockaddr *client_addr)
{
	gfarm_error_t e;
	struct gfp_xdr *client;
	int eof;
	gfarm_int32_t request;
	char *aux, addr_string[GFARM_SOCKADDR_STRLEN];
	enum gfarm_auth_id_type peer_type;
	enum gfarm_auth_method auth_method;

	if ((e = connect_gfm_server()) != GFARM_ERR_NO_ERROR)
		fatal(GFARM_MSG_1003361, "die");

	if (client_name == NULL) { /* i.e. not UNIX domain socket case */
		char *s;
		int port;

		e = gfarm_sockaddr_to_name(client_addr, &client_name);
		if (e != GFARM_ERR_NO_ERROR) {
			gfarm_sockaddr_to_string(client_addr,
			    addr_string, GFARM_SOCKADDR_STRLEN);
			gflog_notice(GFARM_MSG_1000552, "%s: %s", addr_string,
			    gfarm_error_string(e));
			client_name = strdup(addr_string);
			if (client_name == NULL)
				fatal(GFARM_MSG_1000553, "%s: no memory",
				    addr_string);
		}
		e = gfm_host_get_canonical_name(gfm_server, client_name,
		    &s, &port);
		if (e == GFARM_ERR_NO_ERROR) {
			free(client_name);
			client_name = s;
		}
	}

#if 0 /* not yet in gfarm v2 */
	e = gfarm_netparam_config_get_long(&gfarm_netparam_file_read_size,
	    client_name, client_addr, &file_read_size);
	if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
		fatal("file_read_size: %s", gfarm_error_string(e));

	e = gfarm_netparam_config_get_long(&gfarm_netparam_rate_limit,
	    client_name, client_addr, &rate_limit);
	if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
		fatal("rate_limit: %s", gfarm_error_string(e));
#else
	file_read_size = GFS_PROTO_MAX_IOSIZE;
#endif /* not yet in gfarm v2 */

	e = gfp_xdr_new_socket(client_fd, &client);
	if (e != GFARM_ERR_NO_ERROR) {
		close(client_fd);
		fatal(GFARM_MSG_1000554, "%s: gfp_xdr_new: %s",
		    client_name, gfarm_error_string(e));
	}

	e = gfarm_authorize(client, 0, GFS_SERVICE_TAG,
	    client_name, client_addr,
	    gfarm_auth_uid_to_global_username, gfm_server,
	    &peer_type, &username, &auth_method);
	if (e != GFARM_ERR_NO_ERROR)
		fatal(GFARM_MSG_1000555, "%s: gfarm_authorize: %s",
		    client_name, gfarm_error_string(e));
	GFARM_MALLOC_ARRAY(aux, strlen(username)+1 + strlen(client_name)+1);
	if (aux == NULL)
		fatal(GFARM_MSG_1000556, "%s: no memory\n", client_name);
	sprintf(aux, "%s@%s", username, client_name);
	gflog_set_auxiliary_info(aux);

	/*
	 * In GSI authentication, small packets are sent frequently,
	 * which requires TCP_NODELAY for reasonable performance.
	 */
	if (auth_method == GFARM_AUTH_METHOD_GSI) {
		e = gfarm_sockopt_set_option(client_fd, "tcp_nodelay");
		if (e == GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1003403, "tcp_nodelay option is "
			    "specified for performance in GSI");
		else
			gflog_debug(GFARM_MSG_1003404, "tcp_nodelay option is "
			    "specified, but fails: %s", gfarm_error_string(e));
	}

	for (;;) {
		e = gfp_xdr_recv_notimeout(client, 0, &eof, "i", &request);
		if (e != GFARM_ERR_NO_ERROR)
			fatal(GFARM_MSG_1000557, "request number: %s",
			    gfarm_error_string(e));
		if (eof) {
			/*
			 * XXX FIXME update metadata of all opened
			 * file descriptor before exit.
			 */
			cleanup(0);
			exit(0);
		}
		switch (request) {
		case GFS_PROTO_PROCESS_SET:
			gfs_server_process_set(client); break;
		case GFS_PROTO_PROCESS_RESET:
			gfs_server_process_reset(client); break;
		case GFS_PROTO_OPEN_LOCAL:
			gfs_server_open_local(client); break;
		case GFS_PROTO_OPEN:	gfs_server_open(client); break;
		case GFS_PROTO_CLOSE:	gfs_server_close(client); break;
		case GFS_PROTO_PREAD:	gfs_server_pread(client); break;
		case GFS_PROTO_PWRITE:	gfs_server_pwrite(client); break;
		case GFS_PROTO_WRITE:	gfs_server_write(client); break;
		case GFS_PROTO_FTRUNCATE: gfs_server_ftruncate(client); break;
		case GFS_PROTO_FSYNC:	gfs_server_fsync(client); break;
		case GFS_PROTO_FSTAT:	gfs_server_fstat(client); break;
		case GFS_PROTO_CKSUM_SET: gfs_server_cksum_set(client); break;
		case GFS_PROTO_STATFS:	gfs_server_statfs(client); break;
#if 0 /* not yet in gfarm v2 */
		case GFS_PROTO_COMMAND:
			if (credential_exported == NULL) {
				e = gfp_xdr_export_credential(client);
				if (e == GFARM_ERR_NO_ERROR)
					credential_exported = client;
				else
					gflog_warning(GFARM_MSG_UNUSED,
					    "export delegated credential: %s",
					    gfarm_error_string(e));
			}
			gfs_server_command(client,
			    credential_exported == NULL ? NULL :
			    gfp_xdr_env_for_credential(client));
			break;
#endif /* not yet in gfarm v2 */
		case GFS_PROTO_REPLICA_ADD_FROM:
			gfs_server_replica_add_from(client); break;
		case GFS_PROTO_REPLICA_RECV:
			gfs_server_replica_recv(client, peer_type); break;
		default:
			gflog_warning(GFARM_MSG_1000558, "unknown request %d",
			    (int)request);
			cleanup(0);
			exit(1);
		}
		if (gfm_client_is_connection_error(
		    gfp_xdr_flush(gfm_client_connection_conn(gfm_server)))) {
			free_gfm_server();
			if ((e = connect_gfm_server()) != GFARM_ERR_NO_ERROR)
				fatal(GFARM_MSG_1003362, "die");
		}
	}
}

void
start_server(int accepting_sock,
	struct sockaddr *client_addr_storage, socklen_t client_addr_size,
	struct sockaddr *client_addr, char *client_name,
	struct accepting_sockets *accepting)
{
#ifndef GFSD_DEBUG
	pid_t pid = 0;
#endif
	int i, client = accept(accepting_sock,
	   client_addr_storage, &client_addr_size);
	struct gfarm_iostat_items *statp;

	if (client < 0) {
		if (errno == EINTR || errno == ECONNABORTED ||
#ifdef EPROTO
		    errno == EPROTO ||
#endif
		    errno == EAGAIN)
			return;
		fatal_errno(GFARM_MSG_1000559, "accept");
	}
	statp = gfarm_iostat_find_space(0);
#ifndef GFSD_DEBUG
	switch ((pid = fork())) {
	case 0:
#endif
		if (statp) {
			gfarm_iostat_set_id(statp, (gfarm_uint64_t) getpid());
			gfarm_iostat_set_local_ip(statp);
		}
		for (i = 0; i < accepting->local_socks_count; i++)
			close(accepting->local_socks[i].sock);
		close(accepting->tcp_sock);
		for (i = 0; i < accepting->udp_socks_count; i++)
			close(accepting->udp_socks[i]);

		server(client, client_name, client_addr);
		/*NOTREACHED*/
#ifndef GFSD_DEBUG
	case -1:
		gflog_warning_errno(GFARM_MSG_1000560, "fork");
		/*FALLTHROUGH*/
	default:
		close(client);
		break;
	}
	if (pid != -1 && statp)
		gfarm_iostat_set_id(statp, (gfarm_uint64_t) pid);
	else
		gfarm_iostat_clear_ip(statp);
#endif
}

/* XXX FIXME: add protocol magic number and transaction ID */
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
	if (rv == -1)
		return;
	rv = getloadavg(loadavg, GFARM_ARRAY_LENGTH(loadavg));
	if (rv == -1) {
		gflog_warning(GFARM_MSG_1000561,
		    "datagram_server: cannot get load average");
		return;
	}
#ifndef WORDS_BIGENDIAN
	swab(&loadavg[0], &nloadavg[0], sizeof(nloadavg[0]));
	swab(&loadavg[1], &nloadavg[1], sizeof(nloadavg[1]));
	swab(&loadavg[2], &nloadavg[2], sizeof(nloadavg[2]));
#endif
	rv = sendto(sock, nloadavg, sizeof(nloadavg), 0,
	    (struct sockaddr *)&client_addr, sizeof(client_addr));
}

gfarm_int32_t
gfm_async_client_replication_result(void *peer, void *arg, size_t size)
{
	struct gfp_xdr *bc_conn = peer;

	return (gfm_async_client_recv_reply(bc_conn,
	    "gfm_async_client_replication_result", size, ""));
}

/*
 * called from gfp_xdr_async_peer_free() via gfp_xdr_async_xid_free(),
 * when disconnected.
 */
void
gfm_async_client_replication_free(void *peer, void *arg)
{
#if 0
	struct gfp_xdr *bc_conn = peer;
#endif
}

gfarm_error_t
replication_result_notify(struct gfp_xdr *bc_conn,
	gfp_xdr_async_peer_t async, struct gfarm_hash_entry *q)
{
	gfarm_error_t e, e2 = GFARM_ERR_NO_ERROR;
	struct replication_queue_data *qd = gfarm_hash_entry_data(q);
	struct replication_request *rep = qd->head;
	struct replication_errcodes errcodes;
	int rv = read(rep->pipe_fd, &errcodes, sizeof(errcodes)), status;
	struct stat st;
	static const char diag[] = "GFM_PROTO_REPLICATION_RESULT";

	if (rv != sizeof(errcodes)) {
		if (rv == -1) {
			gflog_error(GFARM_MSG_1002191,
			    "%s: cannot read child result: %s",
			    diag, strerror(errno));
		} else {
			gflog_error(GFARM_MSG_1002192,
			    "%s: too short child result: %d bytes", diag, rv);
		}
		errcodes.src_errcode = 0;
		errcodes.dst_errcode = GFARM_ERR_UNKNOWN;
	} else if (fstat(rep->file_fd, &st) == -1) {
		gflog_error(GFARM_MSG_1002193,
		    "%s: cannot stat local fd: %s", diag, strerror(errno));
		if (errcodes.dst_errcode == GFARM_ERR_NO_ERROR)
			errcodes.dst_errcode = GFARM_ERR_UNKNOWN;
	}
	e = gfm_async_client_send_request(bc_conn, async, diag,
	    gfm_async_client_replication_result,
	    gfm_async_client_replication_free,
	    /* rep */ NULL,
	    GFM_PROTO_REPLICATION_RESULT, "llliil",
	    rep->ino, rep->gen, (gfarm_int64_t)rep->pid,
	    errcodes.src_errcode, errcodes.dst_errcode,
	    (gfarm_int64_t)st.st_size);
	close(rep->pipe_fd);
	close(rep->file_fd);
	if ((rv = waitpid(rep->pid, &status, 0)) == -1)
		gflog_warning(GFARM_MSG_1002303,
		    "replication(%lld, %lld): child %d: %s",
		    (long long)rep->ino, (long long)rep->gen, (int)rep->pid,
		    strerror(errno));
	else
		gfarm_iostat_clear_id(rep->pid, 0);

	if (gfs_client_is_connection_error(errcodes.src_errcode))
		gfs_client_purge_from_cache(rep->src_gfsd);
	gfs_client_connection_free(rep->src_gfsd);

	rep->ongoing_prev->ongoing_next = rep->ongoing_next;
	rep->ongoing_next->ongoing_prev = rep->ongoing_prev;

	rep = rep->q_next;
	free(qd->head);

	qd->head = rep;
	if (rep == NULL) {
		qd->tail = &qd->head;
	} else {
		e2 = start_replication(bc_conn, q);
	}

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

static int
watch_fds(struct gfp_xdr *conn, gfp_xdr_async_peer_t async)
{
	gfarm_error_t e;
	int nfound;
	struct replication_request *rep;

#ifdef HAVE_POLL
#define MIN_NFDS 32
	int gfmd_fd, i, n, n_alloc;

	static int nfds = 0;
	static struct pollfd *fds = NULL;
	static struct replication_request **fd_rep_map = NULL;

	for (;;) {
		gfmd_fd = gfp_xdr_fd(conn);
		n = 1; /* fds[0] is for gfmd_fd */
		for (rep = ongoing_replications.ongoing_next;
		    rep != &ongoing_replications; rep = rep->ongoing_next)
			++n;
		if (nfds < n) {
			if (nfds < MIN_NFDS)
				n_alloc = MIN_NFDS;
			else
				n_alloc = nfds * 2;
			while (n_alloc < n)
				n_alloc *= 2;
			GFARM_REALLOC_ARRAY(fds, fds, n_alloc);
			GFARM_REALLOC_ARRAY(fd_rep_map, fd_rep_map, n_alloc);
			if (fds == NULL || fd_rep_map == NULL) {
				gflog_fatal(GFARM_MSG_1003670,
				    "no memory for %d descriptors, "
				    "current = %d, alloc = %d",
				    n, nfds, n_alloc);
			}
			nfds = n_alloc;
		}
		fds[0].fd = gfmd_fd;
		fds[0].events = POLLIN;
		fd_rep_map[0] = NULL;
		n = 1;
		for (rep = ongoing_replications.ongoing_next;
		    rep != &ongoing_replications; rep = rep->ongoing_next) {
			fds[n].fd = rep->pipe_fd;
			fds[n].events = POLLIN;
			fd_rep_map[n] = rep;
			++n;
		}

		nfound =
		    poll(fds, n, gfarm_metadb_heartbeat_interval * 2 * 1000);
		if (nfound == 0) {
			gflog_error(GFARM_MSG_1003671,
			    "back channel: gfmd is down");
			return (0);
		}
		if (nfound < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			fatal_errno(GFARM_MSG_1003672, "back channel poll");
		}
		for (i = 1; i < n; i++) {
			if (fds[i].revents == 0)
				continue;
			e = replication_result_notify(conn, async,
			    fd_rep_map[i]->q);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1003673,
				    "back channel: "
				    "communication error: %s",
				    gfarm_error_string(e));
				return (0);
			}
			
		}
		if (fds[0].revents != 0) /* check gfmd_fd */
			return (1);
	}
#else /* !HAVE_POLL */
	fd_set fds;
	int max_fd;
	struct timeval timeout;
	struct replication_request *next;

	for (;;) {
		FD_ZERO(&fds);
		max_fd = gfp_xdr_fd(conn);
		FD_SET(max_fd, &fds);
		for (rep = ongoing_replications.ongoing_next;
		    rep != &ongoing_replications; rep = rep->ongoing_next) {
			FD_SET(rep->pipe_fd, &fds);
			if (max_fd < rep->pipe_fd)
				max_fd = rep->pipe_fd;
		}

		timeout.tv_sec = gfarm_metadb_heartbeat_interval * 2;
		timeout.tv_usec = 0;

		nfound = select(max_fd + 1, &fds, NULL, NULL, &timeout);
		if (nfound == 0) {
			gflog_error(GFARM_MSG_1002304,
			    "back channel: gfmd is down");
			return (0);
		}
		if (nfound < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			fatal_errno(GFARM_MSG_1002194, "back channel select");
		}

		for (rep = ongoing_replications.ongoing_next;
		    rep != &ongoing_replications; rep = next) {
			if (FD_ISSET(rep->pipe_fd, &fds)) {
				/*
				 * replication_result_notify() may add an entry
				 * at the tail of the ongoing_replications.
				 * accessing and ignoring the new entry at
				 * further iteration of this loop are both ok.
				 */
				next = rep->ongoing_next;

				/*
				 * the following is necessary to make it
				 * possible to access a new entry in this loop.
				 * note that the new entry may use same pipe_fd
				 * with this rep->pipe_fd.
				 */
				FD_CLR(rep->pipe_fd, &fds);

				e = replication_result_notify(conn, async,
				    rep->q);
				if (e != GFARM_ERR_NO_ERROR) {
					gflog_error(GFARM_MSG_1002385,
					    "back channel: "
					    "communication error: %s",
					    gfarm_error_string(e));
					return (0);
				}
			} else {
				next = rep->ongoing_next;
			}
		}
		if (FD_ISSET(gfp_xdr_fd(conn), &fds))
			return (1);
	}
#endif /* !HAVE_POLL */
}

static void
kill_pending_replications(void)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *q;
	struct replication_queue_data *qd;
	struct replication_request *rep, *next;

	if (replication_queue_set == NULL)
		return;
	for (gfarm_hash_iterator_begin(replication_queue_set, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		q = gfarm_hash_iterator_access(&it);
		qd = gfarm_hash_entry_data(q);
		if (qd->head == NULL)
			continue;
		/* do not free active replication (i.e. qd->head) */
		for (rep = qd->head->q_next; rep != NULL; rep = next) {
			next = rep->q_next;
			gflog_debug(GFARM_MSG_1002518,
			    "forget pending replication request "
			    "%s:%d %lld:%lld",
			    gfp_conn_hash_hostname(q), gfp_conn_hash_port(q),
			    (long long)rep->ino, (long long)rep->gen);
			free(rep);
		}
		qd->head->q_next = NULL;
		qd->tail = &qd->head->q_next;		
	}
}

static void
back_channel_server(void)
{
	gfarm_error_t e;
	struct gfm_connection *back_channel;
	struct gfp_xdr *bc_conn;
	gfp_xdr_async_peer_t async;
	enum gfp_xdr_msg_type type;
	gfp_xdr_xid_t xid;
	size_t size;
	gfarm_int32_t gfmd_knows_me, rv, request;

	static int hack_to_make_cookie_not_work = 0; /* XXX FIXME */

	if (iostat_dirbuf) {
		strcpy(&iostat_dirbuf[iostat_dirlen], "bcs");
		e = gfarm_iostat_mmap(iostat_dirbuf, iostat_spec,
			GFARM_IOSTAT_IO_NITEM, gfarm_iostat_max_client);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003674,
				"gfarm_iostat_mmap(%s): %s",
				iostat_dirbuf, gfarm_error_string(e));

	}
	for (;;) {
		back_channel = gfm_server;
		bc_conn = gfm_client_connection_conn(gfm_server);

		e = gfm_client_switch_async_back_channel(back_channel,
		    GFS_PROTOCOL_VERSION,
		    (gfarm_int64_t)(getpid() + hack_to_make_cookie_not_work++),
		    &gfmd_knows_me);
		if (IS_CONNECTION_ERROR(e)) {
			gflog_error(GFARM_MSG_1003685,
			    "back channel disconnected, try to reconnect: %s",
			    gfarm_error_string(e));
			goto reconnect_backchannel;
		} else if (e != GFARM_ERR_NO_ERROR) {
			/*
			 * gfmd has to be newer than gfsd.
			 * so we won't try GFM_PROTO_SWITCH_BACK_CHANNEL,
			 * if GFM_PROTO_SWITCH_TO_ASYNC_BACK_CHANNEL is
			 * not supported.
			 */
			fatal(GFARM_MSG_1000562,
			    "cannot switch to async back channel: %s",
			    gfarm_error_string(e));
		}
		e = gfp_xdr_async_peer_new(&async);
		if (e != GFARM_ERR_NO_ERROR) {
			fatal(GFARM_MSG_1002195,
			    "cannot allocate resource for async protocol: %s",
			    gfarm_error_string(e));
		}

		/* create another gfmd connection for a foreground channel */
		gfm_server = NULL;
		if ((e = connect_gfm_server()) != GFARM_ERR_NO_ERROR)
			fatal(GFARM_MSG_1003363, "die");

		gflog_debug(GFARM_MSG_1000563, "back channel mode");
		for (;;) {
			if (!gfp_xdr_recv_is_ready(bc_conn)) {
				if (!watch_fds(bc_conn, async))
					break;
			}

			e = gfp_xdr_recv_async_header(bc_conn, 0,
			    &type, &xid, &size);
			if (e != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gflog_error(GFARM_MSG_1002386,
					    "back channel disconnected");
				} else {
					gflog_error(GFARM_MSG_1002387,
					    "back channel RPC protocol error, "
					    "reset: %s", gfarm_error_string(e));
				}
				break;
			}
			if (type == GFP_XDR_TYPE_RESULT) {
				e = gfp_xdr_callback_async_result(async,
				    bc_conn, xid, size, &rv);
				if (e != GFARM_ERR_NO_ERROR) {
					gflog_warning(GFARM_MSG_1002196,
					    "(back channel) unknown reply "
					    "xid:%d size:%d",
					    (int)xid, (int)size);
					e = gfp_xdr_purge(bc_conn, 0, size);
					if (e != GFARM_ERR_NO_ERROR) {
						gflog_error(GFARM_MSG_1002197,
						    "skipping %d bytes: %s",
						    (int)size,
						    gfarm_error_string(e));
						break;
					}
				} else if (IS_CONNECTION_ERROR(rv)) {
					gflog_error(GFARM_MSG_1002198,
					    "back channel result: %s",
					    gfarm_error_string(e));
					break;
				}
				continue;
			} else if (type != GFP_XDR_TYPE_REQUEST) {
				fatal(GFARM_MSG_1002199,
				    "async_back_channel_service: type %d",
				    type);
			}
			e = gfp_xdr_recv_request_command(bc_conn, 0, &size,
			    &request);
			if (e != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_UNEXPECTED_EOF) {
					gflog_error(GFARM_MSG_1000564,
					    "back channel disconnected");
				} else {
					gflog_error(GFARM_MSG_1000565,
					    "(back channel) request error, "
					    "reset: %s", gfarm_error_string(e));
				}
				break;
			}
			switch (request) {
			case GFS_PROTO_FHSTAT:
				e = gfs_async_server_fhstat(
				    bc_conn, xid, size);
				break;
			case GFS_PROTO_FHREMOVE:
				e = gfs_async_server_fhremove(
				    bc_conn, xid, size);
				break;
			case GFS_PROTO_STATUS:
				e = gfs_async_server_status(
				    bc_conn, xid, size);
				break;
			case GFS_PROTO_REPLICATION_REQUEST:
				e = gfs_async_server_replication_request(
				    bc_conn, gfm_client_username(back_channel),
				    xid, size);
				break;
			default:
				gflog_error(GFARM_MSG_1000566,
				    "(back channel) unknown request %d "
				    "(xid:%d size:%d), skip",
				    (int)request, (int)xid, (int)size);
				e = gfp_xdr_purge(bc_conn, 0, size);
				if (e != GFARM_ERR_NO_ERROR) {
					gflog_error(GFARM_MSG_1002200,
					    "skipping %d bytes: %s",
					    (int)size, gfarm_error_string(e));
				}
				break;
			}
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1003675,
				    "back channel disconnected "
				    "during a reply: %s",
				    gfarm_error_string(e));
				break;
			}
		}

		kill_pending_replications();

		/* free the foreground channel */
		gfm_client_connection_free(gfm_server);

		gfp_xdr_async_peer_free(async, bc_conn);

reconnect_backchannel:
		gfm_server = back_channel;
		free_gfm_server();
		if ((e = connect_gfm_server()) != GFARM_ERR_NO_ERROR)
			fatal(GFARM_MSG_1003364, "die");
	}
}

static void
start_back_channel_server(void)
{
	pid_t pid;

	pid = fork();
	switch (pid) {
	case 0:
		back_channel_gfsd_pid = getpid();
		back_channel_server();
		/*NOTREACHED*/
	case -1:
		gflog_warning_errno(GFARM_MSG_1000567, "fork");
		/*FALLTHROUGH*/
	default:
		back_channel_gfsd_pid = pid;
		gfm_client_connection_free(gfm_server);
		gfm_server = NULL;
		break;
	}
}

int
open_accepting_tcp_socket(struct in_addr address, int port)
{
	gfarm_error_t e;
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
		accepting_fatal_errno(GFARM_MSG_1000568, "accepting socket");
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1)
		gflog_warning_errno(GFARM_MSG_1000569, "SO_REUSEADDR");
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0)
		accepting_fatal_errno(GFARM_MSG_1000570,
		    "bind accepting socket");
	e = gfarm_sockopt_apply_listener(sock);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000571, "setsockopt: %s",
		    gfarm_error_string(e));
	if (listen(sock, gfarm_spool_server_listen_backlog) < 0)
		accepting_fatal_errno(GFARM_MSG_1000572, "listen");
	return (sock);
}

void
open_accepting_local_socket(struct in_addr address, int port,
	struct local_socket *result)
{
	struct sockaddr_un self_addr;
	socklen_t self_addr_size;
	int sock, save_errno;
	char *sock_name, *sock_dir, dir_buf[PATH_MAX];
	struct stat st;

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sun_family = AF_UNIX;
	snprintf(self_addr.sun_path, sizeof self_addr.sun_path,
	    GFSD_LOCAL_SOCKET_NAME, inet_ntoa(address), port);
	self_addr_size = sizeof(self_addr);

	snprintf(dir_buf, sizeof dir_buf,
	    GFSD_LOCAL_SOCKET_DIR, inet_ntoa(address), port);

	sock_name = strdup(self_addr.sun_path);
	sock_dir = strdup(dir_buf);
	if (sock_name == NULL || sock_dir == NULL)
		accepting_fatal(GFARM_MSG_1000573, "not enough memory");

	/* to make sure */
	if (unlink(sock_name) == 0)
		gflog_info(GFARM_MSG_1002441,
		    "%s: remaining socket found and removed", sock_name);
	else if (errno != ENOENT)
		accepting_fatal_errno(GFARM_MSG_1002442,
		    "%s: failed to remove remaining socket", sock_name);
	if (rmdir(sock_dir) == 0)
		gflog_info(GFARM_MSG_1002443,
		    "%s: remaining socket directory found and removed",
		    sock_dir);
	else if (errno != ENOENT) /* something wrong, but tries to continue */
		gflog_error_errno(GFARM_MSG_1002444,
		    "%s: failed to remove remaining socket directory",
		    sock_dir);

	if (mkdir(sock_dir, LOCAL_SOCKDIR_MODE) == -1) {
		if (errno != EEXIST) {
			accepting_fatal_errno(GFARM_MSG_1000574,
			    "%s: cannot mkdir", sock_dir);
		} else if (stat(sock_dir, &st) != 0) {
			accepting_fatal_errno(GFARM_MSG_1000575, "stat(%s)",
			    sock_dir);
		} else if (st.st_uid != gfsd_uid) {
			accepting_fatal(GFARM_MSG_1000576,
			    "%s: not owned by uid %ld",
			    sock_dir, (long)gfsd_uid);
		} else if ((st.st_mode & PERMISSION_MASK) != LOCAL_SOCKDIR_MODE
		    && chmod(sock_dir, LOCAL_SOCKDIR_MODE) != 0) {
			accepting_fatal_errno(GFARM_MSG_1000577,
			    "%s: cannot chmod to 0%o",
			    sock_dir, LOCAL_SOCKDIR_MODE);
		}
	}
	if (chown(sock_dir, gfsd_uid, -1) == -1)
		gflog_warning_errno(GFARM_MSG_1002201, "chown(%s, %d)",
		    sock_dir, (int)gfsd_uid);

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		save_errno = errno;
		if (rmdir(sock_dir) == -1)
			gflog_error_errno(GFARM_MSG_1002388,
			    "rmdir(%s)", sock_dir);
		accepting_fatal(GFARM_MSG_1000578,
		    "creating UNIX domain socket: %s",
		    strerror(save_errno));
	}
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) == -1) {
		save_errno = errno;
		if (rmdir(sock_dir) == -1)
			gflog_error_errno(GFARM_MSG_1002389,
			    "rmdir(%s)", sock_dir);
		accepting_fatal(GFARM_MSG_1000579,
		    "%s: cannot bind UNIX domain socket: %s",
		    sock_name, strerror(save_errno));
	}
	if (chown(sock_name, gfsd_uid, -1) == -1)
		gflog_warning_errno(GFARM_MSG_1002202, "chown(%s, %ld)",
		    sock_name, (long)gfsd_uid);
	/* ensure access from all user, Linux at least since 2.4 needs this. */
	if (chmod(sock_name, LOCAL_SOCKET_MODE) == -1)
		gflog_debug_errno(GFARM_MSG_1002390, "chmod(%s, 0%o)",
		    sock_name, (int)LOCAL_SOCKET_MODE);

	if (listen(sock, gfarm_spool_server_listen_backlog) == -1) {
		save_errno = errno;
		if (unlink(sock_name) == -1)
			gflog_error_errno(GFARM_MSG_1002391,
			    "unlink(%s)", sock_name);
		if (rmdir(sock_dir) == -1)
			gflog_error_errno(GFARM_MSG_1002392,
			    "rmdir(%s)", sock_dir);
		accepting_fatal(GFARM_MSG_1000580,
		    "listen UNIX domain socket: %s", strerror(save_errno));
	}

	result->sock = sock;
	result->name = sock_name;
	result->dir = sock_dir;
}

void
open_accepting_local_sockets(
	int self_addresses_count, struct in_addr *self_addresses, int port,
	struct accepting_sockets *accepting)
{
	int i;

	GFARM_MALLOC_ARRAY(accepting->local_socks, self_addresses_count);
	if (accepting->local_socks == NULL)
		accepting_fatal(GFARM_MSG_1000581,
		    "not enough memory for UNIX sockets");

	for (i = 0; i < self_addresses_count; i++) {
		open_accepting_local_socket(self_addresses[i], port,
		    &accepting->local_socks[i]);

		/* for cleanup_accepting() */
		accepting->local_socks_count = i + 1;
	}
}

int
open_udp_socket(struct in_addr address, int port)
{
	struct sockaddr_in bind_addr;
	socklen_t bind_addr_size;
	int s;

	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr = address;
	bind_addr.sin_port = ntohs(port);
	bind_addr_size = sizeof(bind_addr);
	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		accepting_fatal_errno(GFARM_MSG_1000582, "UDP socket");
	if (bind(s, (struct sockaddr *)&bind_addr, bind_addr_size) < 0)
		accepting_fatal_errno(GFARM_MSG_1000583,
		    "UDP socket bind(%s, %d)",
		    inet_ntoa(address), port);
	return (s);
}

int *
open_datagram_service_sockets(
	int self_addresses_count, struct in_addr *self_addresses, int port)
{
	int i, *sockets;

	GFARM_MALLOC_ARRAY(sockets, self_addresses_count);
	if (sockets == NULL)
		accepting_fatal(GFARM_MSG_1000584,
		    "no memory for %d datagram sockets",
		    self_addresses_count);
	for (i = 0; i < self_addresses_count; i++)
		sockets[i] = open_udp_socket(self_addresses[i], port);
	return (sockets);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-L <syslog-priority-level>\n");
	fprintf(stderr, "\t-P <pid-file>\n");
	fprintf(stderr, "\t-c\t\t\t\t... check and display invalid files\n");
	fprintf(stderr, "\t-cc\t\t\t\t... check and delete invalid files\n");
	fprintf(stderr,
	    "\t-ccc (default)\t\t\t... check and move invalid files to\n");
	fprintf(stderr, "\t\t\t\t\tgfarm:///lost+found, and delete invalid\n");
	fprintf(stderr, "\t\t\t\t\treplica-references from metadata\n");
	fprintf(stderr, "\t-d\t\t\t\t... debug mode\n");
	fprintf(stderr, "\t-f <gfarm-configuration-file>\n");
	fprintf(stderr, "\t-h <hostname>\n");
	fprintf(stderr, "\t-l <listen_address>\n");
	fprintf(stderr, "\t-r <spool_root>\n");
	fprintf(stderr, "\t-s <syslog-facility>\n");
	fprintf(stderr, "\t-v\t\t\t\t... make authentication log verbose\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	struct sockaddr_in client_addr, *self_sockaddr_array;
	struct sockaddr_un client_local_addr;
	gfarm_error_t e, e2;
	char *config_file = NULL, *pid_file = NULL;
	char *local_gfsd_user;
	struct gfarm_host_info self_info;
	struct passwd *gfsd_pw;
	FILE *pid_fp = NULL;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	int syslog_level = -1;
	struct in_addr *self_addresses, listen_address;
	int table_size, self_addresses_count, ch, i, nfound, max_fd, p;
	struct sigaction sa;
	fd_set requests;
	struct stat sb;
	int spool_check_level = 0;
	int is_root = geteuid() == 0;

	if (argc >= 1)
		program_name = basename(argv[0]);
	gflog_set_identifier(program_name);

	while ((ch = getopt(argc, argv, "L:P:cdf:h:l:r:s:v")) != -1) {
		switch (ch) {
		case 'L':
			syslog_level = gflog_syslog_name_to_priority(optarg);
			if (syslog_level == -1)
				gflog_fatal(GFARM_MSG_1000585,
				    "-L %s: invalid syslog priority", optarg);
			break;
		case 'P':
			pid_file = optarg;
			break;
		case 'c':
			++spool_check_level;
			break;
		case 'd':
			debug_mode = 1;
			if (syslog_level == -1)
				syslog_level = LOG_DEBUG;
			break;
		case 'f':
			config_file = optarg;
			break;
		case 'h':
			canonical_self_name = optarg;
			break;
		case 'l':
			listen_addrname = optarg;
			break;
		case 'r':
			gfarm_spool_root = strdup(optarg);
			if (gfarm_spool_root == NULL)
				gflog_fatal(GFARM_MSG_1000586, "%s",
				    gfarm_error_string(GFARM_ERR_NO_MEMORY));
			break;
		case 's':
			syslog_facility =
			    gflog_syslog_name_to_facility(optarg);
			if (syslog_facility == -1)
				gflog_fatal(GFARM_MSG_1000587,
				    "%s: unknown syslog facility", optarg);
			break;
		case 'v':
			gflog_auth_set_verbose(1);
			break;
		case '?':
		default:
			usage();
		}
	}

	switch (spool_check_level) {
	case 0:
		e = GFARM_ERR_NO_ERROR;
		break;
	case 1:
		e = gfarm_spool_check_level_set(
		    GFARM_SPOOL_CHECK_LEVEL_DISPLAY);
		break;
	case 2:
		e = gfarm_spool_check_level_set(
		    GFARM_SPOOL_CHECK_LEVEL_DELETE);
		break;
	default:
		e = gfarm_spool_check_level_set(
		    GFARM_SPOOL_CHECK_LEVEL_LOST_FOUND);
		break;
	}
	assert(e == GFARM_ERR_NO_ERROR);

	if (config_file != NULL)
		gfarm_config_set_filename(config_file);
	e = gfarm_server_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_server_initialize: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}

	argc -= optind;
	argv += optind;

	gfarm_spool_root_len = strlen(gfarm_spool_root);

	if (syslog_level != -1)
		gflog_set_priority_level(syslog_level);

	e = gfarm_global_to_local_username_by_url(GFARM_PATH_ROOT,
	    GFSD_USERNAME, &local_gfsd_user);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "no local user for the global `%s' user.\n",
		    GFSD_USERNAME);
		exit(1);
	}
	gfsd_pw = getpwnam(local_gfsd_user);
	if (gfsd_pw == NULL) {
		fprintf(stderr, "user `%s' is necessary, but doesn't exist.\n",
		    local_gfsd_user);
		exit(1);
	}
	gfsd_uid = gfsd_pw->pw_uid;

	if (seteuid(gfsd_uid) == -1 && is_root)
		gflog_error_errno(GFARM_MSG_1002393,
		    "seteuid(%d)", (int)gfsd_uid);

	e = gfarm_set_local_user_for_this_local_account();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "acquiring information about user `%s': %s\n",
		    local_gfsd_user, gfarm_error_string(e));
		exit(1);
	}
	free(local_gfsd_user);

	/* sanity check on a spool directory */
	if (stat(gfarm_spool_root, &sb) == -1)
		gflog_fatal_errno(GFARM_MSG_1000588, "%s", gfarm_spool_root);
	else if (!S_ISDIR(sb.st_mode))
		gflog_fatal(GFARM_MSG_1000589, "%s: %s", gfarm_spool_root,
		    gfarm_error_string(GFARM_ERR_NOT_A_DIRECTORY));

	if (pid_file != NULL) {
		/*
		 * We do this before calling gfarm_daemon()
		 * to print the error message to stderr.
		 */
		if (seteuid(0) == -1 && is_root)
			gflog_error_errno(GFARM_MSG_1002394, "seteuid(0)");
		pid_fp = fopen(pid_file, "w");
		if (seteuid(gfsd_uid) == -1 && is_root)
			gflog_error_errno(GFARM_MSG_1002395,
			    "seteuid(%d)", (int)gfsd_uid);
		if (pid_fp == NULL)
			accepting_fatal_errno(GFARM_MSG_1000590,
				"failed to open file: %s", pid_file);
	}

	if (!debug_mode) {
		gflog_syslog_open(LOG_PID, syslog_facility);
		if (gfarm_daemon(0, 0) == -1)
			gflog_warning_errno(GFARM_MSG_1002203, "daemon");
	}

	/* We do this after calling gfarm_daemon(), because it changes pid. */
	master_gfsd_pid = getpid();
	sa.sa_handler = cleanup_handler;
	if (sigemptyset(&sa.sa_mask) == -1)
		gflog_fatal_errno(GFARM_MSG_1002396, "sigemptyset()");
	sa.sa_flags = 0;
	if (sigaction(SIGHUP, &sa, NULL) == -1) /* XXX - need to restart gfsd */
		gflog_fatal_errno(GFARM_MSG_1002397, "sigaction(SIGHUP)");
	if (sigaction(SIGINT, &sa, NULL) == -1)
		gflog_fatal_errno(GFARM_MSG_1002398, "sigaction(SIGINT)");
	if (sigaction(SIGTERM, &sa, NULL) == -1)
		gflog_fatal_errno(GFARM_MSG_1002399, "sigaction(SIGTERM)");

	if (pid_file != NULL) {
		if (fprintf(pid_fp, "%ld\n", (long)master_gfsd_pid) == -1)
			gflog_error_errno(GFARM_MSG_1002400,
			    "writing PID to %s", pid_file);
		if (fclose(pid_fp) != 0)
			gflog_error_errno(GFARM_MSG_1002401,
			    "fclose(%s)", pid_file);
	}

	gfarm_set_auth_id_type(GFARM_AUTH_ID_TYPE_SPOOL_HOST);
	if ((e = connect_gfm_server()) != GFARM_ERR_NO_ERROR)
		fatal(GFARM_MSG_1003365, "die");
	/*
	 * in case of canonical_self_name != NULL, get_canonical_self_name()
	 * cannot be used because host_get_self_name() may not be registered.
	 */
	if (canonical_self_name == NULL &&
	    (e = gfm_host_get_canonical_self_name(gfm_server,
	    &canonical_self_name, &p)) != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_1000591,
		    "cannot get canonical hostname of %s, ask admin to "
		    "register this node in Gfarm metadata server, died: %s\n",
		    gfarm_host_get_self_name(), gfarm_error_string(e));
	}
	/* avoid gcc warning "passing arg 3 from incompatible pointer type" */
	{	
		const char *n = canonical_self_name;

		e = gfm_client_host_info_get_by_names(gfm_server,
		    1, &n, &e2, &self_info);
	}
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_1000592,
		    "cannot get canonical hostname of %s, ask admin to "
		    "register this node in Gfarm metadata server, died: %s\n",
		    canonical_self_name, gfarm_error_string(e));
	}

	if (seteuid(0) == -1 && is_root)
		gflog_error_errno(GFARM_MSG_1002402, "seteuid(0)");

	if (listen_addrname == NULL)
		listen_addrname = gfarm_spool_server_listen_address;
	if (listen_addrname == NULL) {
		e = gfarm_get_ip_addresses(
		    &self_addresses_count, &self_addresses);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1000593, "get_ip_addresses: %s",
			    gfarm_error_string(e));
		listen_address.s_addr = INADDR_ANY;
	} else {
		struct hostent *hp = gethostbyname(listen_addrname);

		if (hp == NULL || hp->h_addrtype != AF_INET)
			gflog_fatal(GFARM_MSG_1000594,
			    "listen address can't be resolved: %s",
			    listen_addrname);
		self_addresses_count = 1;
		GFARM_MALLOC(self_addresses);
		if (self_addresses == NULL)
			gflog_fatal(GFARM_MSG_1000595, "%s",
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
		memcpy(self_addresses, hp->h_addr, sizeof(*self_addresses));
		listen_address = *self_addresses;
	}
	if (gfarm_iostat_gfsd_path) {
		int len;

		len = strlen(gfarm_iostat_gfsd_path) + 6; /* for port */
		if (listen_addrname)
			len += strlen(listen_addrname) + 1;
		len += 1 + 16 + 1;	/* "-NAME\0" */
		GFARM_MALLOC_ARRAY(iostat_dirbuf, len);
		if (iostat_dirbuf == NULL)
			gflog_fatal(GFARM_MSG_1003676, "iostat_dirbuf:%s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));

		iostat_dirlen = snprintf(iostat_dirbuf, len, "%s%s%s-%d/",
			gfarm_iostat_gfsd_path, listen_addrname ? "-" : "",
			listen_addrname ? listen_addrname : "",
			(unsigned int) self_info.port);
		if (mkdir(iostat_dirbuf, 0755)) {
			if (errno != EEXIST)
				gflog_fatal_errno(GFARM_MSG_1003677,
					"mkdir:%s", iostat_dirbuf);
		} else if (chown(iostat_dirbuf, gfsd_uid, -1)) {
			if (errno != EEXIST)
				gflog_fatal_errno(GFARM_MSG_1003678,
					"chown:%s", iostat_dirbuf);
		}
	}
	GFARM_MALLOC_ARRAY(self_sockaddr_array, self_addresses_count);
	if (self_sockaddr_array == NULL)
		gflog_fatal(GFARM_MSG_1000596, "%s",
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
	for (i = 0; i < self_addresses_count; i++) {
		memset(&self_sockaddr_array[i], 0,
		    sizeof(self_sockaddr_array[i]));
		self_sockaddr_array[i].sin_family = AF_INET;
		self_sockaddr_array[i].sin_addr = self_addresses[i];
		self_sockaddr_array[i].sin_port = htons(self_info.port);
	}

	accepting.tcp_sock = open_accepting_tcp_socket(
	    listen_address, self_info.port);
	/* sets accepting.local_socks_count and accepting.local_socks */
	open_accepting_local_sockets(
	    self_addresses_count, self_addresses, self_info.port,
	    &accepting);
	accepting.udp_socks = open_datagram_service_sockets(
	    self_addresses_count, self_addresses, self_info.port);
	accepting.udp_socks_count = self_addresses_count;

	max_fd = accepting.tcp_sock;
	for (i = 0; i < accepting.local_socks_count; i++) {
		if (max_fd < accepting.local_socks[i].sock)
			max_fd = accepting.local_socks[i].sock;
	}
	for (i = 0; i < accepting.udp_socks_count; i++) {
		if (max_fd < accepting.udp_socks[i])
			max_fd = accepting.udp_socks[i];
	}
	if (max_fd > FD_SETSIZE)
		accepting_fatal(GFARM_MSG_1000597,
		    "too big socket file descriptor: %d", max_fd);

	if (seteuid(gfsd_uid) == -1) {
		int save_errno = errno;

		if (geteuid() == 0)
			gflog_error(GFARM_MSG_1002403,
			    "seteuid(%ld): %s",
			    (long)gfsd_uid, strerror(save_errno));
	}

	/* XXX - kluge for gfrcmd (to mkdir HOME....) for now */
	/* XXX - kluge for GFS_PROTO_STATFS for now */
	if (chdir(gfarm_spool_root) == -1)
		gflog_fatal_errno(GFARM_MSG_1000598, "chdir(%s)",
		    gfarm_spool_root);

	/* spool check */
	gfsd_spool_check();

	/*
	 * We don't want SIGPIPE, but want EPIPE on write(2)/close(2).
	 */
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		gflog_fatal_errno(GFARM_MSG_1002404,
		    "signal(SIGPIPE, SIG_IGN)");

	/* start back channel server */
	start_back_channel_server();

	table_size = FILE_TABLE_LIMIT;
	if (gfarm_limit_nofiles(&table_size) == 0)
		gflog_info(GFARM_MSG_1003515, "max descriptors = %d",
		    table_size);
	file_table_init(table_size);

	if (iostat_dirbuf) {
		strcpy(&iostat_dirbuf[iostat_dirlen], "gfsd");
		e = gfarm_iostat_mmap(iostat_dirbuf, iostat_spec,
			GFARM_IOSTAT_IO_NITEM, gfarm_iostat_max_client);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003679,
				"gfarm_iostat_mmap(%s): %s",
				iostat_dirbuf, gfarm_error_string(e));
	}
	/*
	 * Because SA_NOCLDWAIT is not implemented on some OS,
	 * we do not rely on the feature.
	 */
	sa.sa_handler = sigchld_handler;
	if (sigemptyset(&sa.sa_mask) == -1)
		gflog_fatal_errno(GFARM_MSG_1002405, "sigemptyset");
	sa.sa_flags = SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		gflog_fatal_errno(GFARM_MSG_1002406, "sigaction(SIGCHLD)");

	/*
	 * To deal with race condition which may be caused by RST,
	 * listening socket must be O_NONBLOCK, if the socket will be
	 * used as a file descriptor for select(2) .
	 * See section 16.6 of "UNIX NETWORK PROGRAMMING, Volume1,
	 * Third Edition" by W. Richard Stevens, for detail.
	 */
	if (fcntl(accepting.tcp_sock, F_SETFL,
	    fcntl(accepting.tcp_sock, F_GETFL, NULL) | O_NONBLOCK) == -1)
		gflog_warning_errno(GFARM_MSG_1000599,
		    "accepting TCP socket O_NONBLOCK");

	for (;;) {
		FD_ZERO(&requests);
		FD_SET(accepting.tcp_sock, &requests);
		for (i = 0; i < accepting.local_socks_count; i++)
			FD_SET(accepting.local_socks[i].sock, &requests);
		for (i = 0; i < accepting.udp_socks_count; i++)
			FD_SET(accepting.udp_socks[i], &requests);
		nfound = select(max_fd + 1, &requests, NULL, NULL, NULL);
		if (nfound <= 0) {
			if (got_sigchld)
				clear_child();
			if (nfound == 0 || errno == EINTR || errno == EAGAIN)
				continue;
			fatal_errno(GFARM_MSG_1000600, "select");
		}

		if (FD_ISSET(accepting.tcp_sock, &requests)) {
			start_server(accepting.tcp_sock,
			    (struct sockaddr*)&client_addr,sizeof(client_addr),
			    (struct sockaddr*)&client_addr, NULL, &accepting);
		}
		for (i = 0; i < accepting.local_socks_count; i++) {
			if (FD_ISSET(accepting.local_socks[i].sock,&requests)){
				start_server(accepting.local_socks[i].sock,
				    (struct sockaddr *)&client_local_addr,
				    sizeof(client_local_addr),
				    (struct sockaddr*)&self_sockaddr_array[i],
				    canonical_self_name,
				    &accepting);
			}
		}
		for (i = 0; i < accepting.udp_socks_count; i++) {
			if (FD_ISSET(accepting.udp_socks[i], &requests))
				datagram_server(accepting.udp_socks[i]);
		}
	}
	/*NOTREACHED*/
#ifdef __GNUC__ /* to shut up warning */
	return (0);
#endif
}
