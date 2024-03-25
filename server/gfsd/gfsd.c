/*
 * $Id$
 */

#define _POSIX_PII_SOCKET /* to use struct msghdr on Tru64 */
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
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
#ifndef INFTIM
#define INFTIM -1
#endif
#endif

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>	/* getloadavg() on Solaris */
#endif

#include <openssl/evp.h>

#if defined(__hurd__) || defined(__gnu_hurd__) && !defined(PATH_MAX)
#define PATH_MAX	2048	/* XXX FIXME */
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
#define GFARM_USE_OPENSSL
#include "msgdigest.h"
#include "nanosec.h"
#include "proctitle.h"
#include "timer.h"
#include "timespec.h"

#include "context.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "sockopt.h"
#include "hostspec.h"
#include "host.h"
#include "conn_hash.h"
#include "auth.h"
#include "config.h"
#include "gfs_proto.h"
#define GFARM_USE_OPENSSL
#include "gfs_client.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "filesystem.h"
#include "gfs_profile.h"
#include "iostat.h"

#include "gfsd_subr.h"
#include "write_verify.h"

#include "gfs_rdma.h"

#define COMPAT_OLD_GFS_PROTOCOL

#define FAILOVER_SIGNAL	SIGUSR1

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

#ifdef HAVE_INFINIBAND
#define IF_INFINIBAND(statements)	statements
#else
#define IF_INFINIBAND(statements)
#endif

/*
 * set initial sleep_interval to 1 sec for quick recovery
 * at gfmd failover.
 */
#define GFMD_CONNECT_SLEEP_INTVL_MIN	1	/* 1 sec */
#define GFMD_CONNECT_SLEEP_INTVL_MAX	512	/* about 8.5 min */
#define GFMD_CONNECT_SLEEP_LOG_OMIT	11	/* log until 512 sec */
#define GFMD_CONNECT_SLEEP_LOG_INTERVAL	86400	/* 1 day */

#define fatal_errno(msg_no, ...) \
	fatal_errno_full(msg_no, LOG_ERR, __FILE__, __LINE__, __func__, \
	    __VA_ARGS__)
/* connection related fatal error.  zabbix alert is unnecessary. */
#define conn_fatal(msg_no, ...) \
	fatal_full(msg_no, LOG_NOTICE, __FILE__, __LINE__, __func__, \
	    __VA_ARGS__)
#define accepting_fatal(msg_no, ...) \
	accepting_fatal_full(msg_no, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define accepting_fatal_errno(msg_no, ...) \
	accepting_fatal_errno_full(msg_no, __FILE__, __LINE__, __func__,\
				   __VA_ARGS__)

const char *program_name = "gfsd";

int debug_mode = 0;

/* my_type is a variable of enum gfsd_type */
static volatile sig_atomic_t my_type = type_listener;

#define USING_PIPE_FOR_FAILOVER_SIGNAL(my_type) \
	((my_type) == type_client || \
	 (my_type) == type_back_channel || \
	 (my_type) == type_write_verify)

/* available only if USING_PIPE_FOR_FAILOVER_SIGNAL(my_type) */
static int failover_notify_recv_fd = -1;
static int failover_notify_send_fd = -1;

pid_t master_gfsd_pid;
pid_t back_channel_gfsd_pid = -1;
pid_t write_verify_controller_gfsd_pid = -1;
uid_t gfsd_uid = -1;

struct gfm_connection *gfm_server;
char *canonical_self_name;
char *username; /* gfarm global user name */

int gfarm_spool_root_len[GFARM_SPOOL_ROOT_NUM];
int gfarm_spool_root_num;
static int gfarm_spool_root_len_max;

#define IOSTAT_PATH_NAME_MAX 16
static struct gfarm_iostat_spec iostat_spec[] =  {
	{ "rcount", GFARM_IOSTAT_TYPE_TOTAL },
	{ "wcount", GFARM_IOSTAT_TYPE_TOTAL },
	{ "rbytes", GFARM_IOSTAT_TYPE_TOTAL },
	{ "wbytes", GFARM_IOSTAT_TYPE_TOTAL },
};
static char *iostat_dirbuf;
static int iostat_dirlen;

struct rdma_context *rdma_ctx = NULL;

static volatile sig_atomic_t write_open_count = 0;
static volatile sig_atomic_t terminate_flag = 0;

static char *listen_addrname = NULL;

static int fd_usable_to_gfmd = 1;
static int client_failover_count; /* may be use in the future implement */

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

static void
cleanup_iostat(int sighandler)
{
	if (iostat_dirbuf != NULL && iostat_dirlen > 0) {
		/*
		 * XXX strcpy() is not defined as async-signal-safe
		 * in IEEE Std 1003.1, 2013 (POSIX).
		 * thus, the following code is not portable, strictly speaking.
		 */
		strcpy(&iostat_dirbuf[iostat_dirlen], "gfsd");
		(void) unlink(iostat_dirbuf);
		strcpy(&iostat_dirbuf[iostat_dirlen], "bcs");
		(void) unlink(iostat_dirbuf);
		strcpy(&iostat_dirbuf[iostat_dirlen], "wv");
		(void) unlink(iostat_dirbuf);
		if (!sighandler)
			free(iostat_dirbuf);
		iostat_dirbuf = NULL;
	}
}
void
gfsd_setup_iostat(const char *name, unsigned int row)
{
	if (iostat_dirbuf) {
		gfarm_error_t e;
		strncpy(&iostat_dirbuf[iostat_dirlen], name,
				IOSTAT_PATH_NAME_MAX);
		e = gfarm_iostat_mmap(iostat_dirbuf, iostat_spec,
			GFARM_IOSTAT_IO_NITEM, row);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1004507,
				"gfarm_iostat_mmap('%s', %d): %s",
				iostat_dirbuf, row, gfarm_error_string(e));

	}
}
static void close_all_fd(struct gfp_xdr *);
static int close_all_fd_for_process_reset(struct gfp_xdr *);
static struct gfp_xdr *current_client = NULL;

/* this routine should be called before calling exit(). */
void
cleanup(int sighandler)
{
	static int cleanup_started = 0;
	pid_t pid = getpid();

	if (!cleanup_started && !sighandler) {
		cleanup_started = 1; /* prevent recursive close_all_fd() */

#ifdef HAVE_INFINIBAND
		gfs_rdma_finish(rdma_ctx);
#endif
		if (my_type == type_client) {
			/* may recursivelly call cleanup() */
			close_all_fd(current_client);
		} else if (my_type == type_write_verify_controller)
			write_verify_controller_cleanup();
	}

	if (pid == master_gfsd_pid) {
		cleanup_accepting(sighandler);
		/* send terminate signal to a back channel process */
		if (back_channel_gfsd_pid != -1 &&
		    kill(back_channel_gfsd_pid, SIGTERM) == -1 && !sighandler)
			gflog_warning_errno(GFARM_MSG_1002377,
			    "kill(back_channel:%ld)",
			    (long)back_channel_gfsd_pid);
		if (write_verify_controller_gfsd_pid != -1 &&
		    kill(write_verify_controller_gfsd_pid, SIGTERM) == -1 &&
		    !sighandler)
			gflog_warning_errno(GFARM_MSG_1004472,
			    "kill(write_verify_controller:%ld)",
			    (long)write_verify_controller_gfsd_pid);
		cleanup_iostat(sighandler);
	}

	if (!sighandler) {
		/* It's not safe to do the following operation in sighandler */
		gflog_notice(GFARM_MSG_1000451, "disconnected");
	}
}

static void
cleanup_handler(int signo)
{
	terminate_flag = 1;
	if (my_type == type_write_verify_controller) {
		write_verify_controller_cleanup_signal();
		return;
	}
	if (write_open_count == 0) {
		cleanup(1);
		_exit(0);
	}
}

/*
 * if the connection to the client is down,
 * the client may already issued GFM_PROTO_REVOKE_GFSD_ACCESS
 */
static int
connection_is_down(int socket)
{
	union {
		struct sockaddr generic;
		struct sockaddr_in in;
		struct sockaddr_un un;
	} addr;
	int err;
	socklen_t addr_size = sizeof(addr), err_size = sizeof(err);

	if (getpeername(socket, &addr.generic, &addr_size) == -1)
		return (1);
	if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &err, &err_size) == -1)
		return (1);
	if (err != 0)
		return (1);
	return (0);
}

static void
gflog_put_fd_problem_full(int, const char *, int, const char *,
	struct gfp_xdr *, gfarm_error_t, const char *, ...)
	GFLOG_PRINTF_ARG(7, 8);

static void
gflog_put_fd_problem_full(int msg_no,
	const char *file, int line_no, const char *func,
	struct gfp_xdr *client, gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	/*
	 * GFARM_ERR_OPERATION_NOT_PERMITTED may be caused by
	 * GFM_PROTO_REVOKE_GFSD_ACCESS.
	 * See subversion r9079 (git commit f00a866df)
	 * and subversion r9114 (git commit 5cd885303)
	 */

	va_start(ap, format);
	gflog_vmessage(msg_no,
	    (e == GFARM_ERR_BAD_FILE_DESCRIPTOR ||
	     e == GFARM_ERR_OPERATION_NOT_PERMITTED) &&
	    client != NULL && connection_is_down(gfp_xdr_fd(client)) ?
		LOG_INFO :
	    IS_CONNECTION_ERROR(e) ? LOG_NOTICE : LOG_ERR,
	    file, line_no, func, format, ap);
	va_end(ap);
}

#define gflog_put_fd_problem(msg_no, client, e, ...) \
	gflog_put_fd_problem_full(msg_no, __FILE__, __LINE__, __func__, \
	    client, e, __VA_ARGS__)


static int kill_master_gfsd;

void
fatal_full(int msg_no, int priority, const char *file,
	int line_no, const char *func, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(msg_no, priority, file, line_no, func, format, ap);
	va_end(ap);

	/*
	 * gflog_fatal() shows similar message, but it's not called here,
	 * thus we need this too.
	 */
	gflog_notice(GFARM_MSG_1005071,
	    "gfsd is now aborting due to the message [%06d]", msg_no);

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

static void fatal_errno_full(int, int, const char *, int, const char*,
		const char *, ...) GFLOG_PRINTF_ARG(6, 7);
static void
fatal_errno_full(int msg_no, int priority, const char *file,
	int line_no, const char *func, const char *format, ...)
{
	char buffer[2048];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buffer, sizeof buffer, format, ap);
	va_end(ap);
	fatal_full(msg_no, priority, file, line_no, func, "%s: %s",
			buffer, strerror(errno));
}

void
fatal_metadb_proto_full(int msg_no,
	const char *file, int line_no, const char *func,
	const char *diag, const char *proto, gfarm_error_t e)
{
	fatal_full(msg_no, LOG_ERR, file, line_no, func,
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

/* return 0, if one of the file descriptors is available, otherwise errno */
static int
#ifdef HAVE_POLL
sleep_or_wait_fds(int seconds, int nfds, struct pollfd *fds, const char *diag)
#else
sleep_or_wait_fds(int seconds, int max_fd, fd_set *fds, const char *diag)
#endif
{
	int nfound;
	struct timeval expiration_time, now, t;

	gettimeofday(&expiration_time, NULL);
	expiration_time.tv_sec += seconds;
#ifdef HAVE_POLL
	for (;;) {
		gettimeofday(&now, NULL);
		if (gfarm_timeval_cmp(&now, &expiration_time) >= 0)
			return (EAGAIN);
		t = expiration_time;
		gfarm_timeval_sub(&t, &now);

		nfound = poll(fds, nfds,
		    t.tv_sec * GFARM_SECOND_BY_MILLISEC +
		    t.tv_usec / GFARM_MILLISEC_BY_MICROSEC);
		if (nfound == 0)
			return (EAGAIN);
		if (nfound == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return (errno);
		}
		return (0);
	}
#else /* !HAVE_POLL */
	for (;;) {
		gettimeofday(&now, NULL);
		if (gfarm_timeval_cmp(&now, &expiration_time) >= 0)
			return (EAGAIN);
		t = expiration_time;
		gfarm_timeval_sub(&t, &now);

		/* using the returned `t` from select(2) is not portable */
		nfound = select(max_fd, fds, NULL, NULL, &t);
		if (nfound == 0)
			return (EAGAIN);
		if (nfound == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return (errno);
		}
		return (0);
	}
#endif /* !HAVE_POLL */
}

static int
sleep_or_wait_failover_packet(int seconds)
{
	int i, rv;
	static const char diag[] = "sleep_or_wait_failover_packet";

#ifdef HAVE_POLL
	static int fds_alloced = 0;
	static struct pollfd *fds = NULL;

	if (fds_alloced <= accepting.udp_socks_count) {
		free(fds);
		GFARM_MALLOC_ARRAY(fds, accepting.udp_socks_count);
		if (fds == NULL)
			gflog_fatal(GFARM_MSG_1004210,
			    "cannot allocate pollfds for UDP sockets (%d)",
			    accepting.udp_socks_count);
		fds_alloced = accepting.udp_socks_count;
	}
	for (i = 0; i < accepting.udp_socks_count; i++) {
		fds[i].fd = accepting.udp_socks[i];
		fds[i].events = POLLIN;
	}
	rv = sleep_or_wait_fds(seconds, accepting.udp_socks_count, fds, diag);
#else /* !HAVE_POLL */
	int max_fd = -1;
	fd_set fds;

	FD_ZERO(&fds);
	for (i = 0; i < accepting.udp_socks_count; i++) {
		if (accepting.udp_socks[i] >= FD_SETSIZE)
			fatal(GFARM_MSG_1004211,
			    "too big descriptor: udp_fd:%d exceeds %d",
			    accepting.udp_socks[i], FD_SETSIZE);
		if (max_fd < accepting.udp_socks[i])
			max_fd = accepting.udp_socks[i];
		FD_SET(accepting.udp_socks[i], &fds);
	}
	rv = sleep_or_wait_fds(seconds, max_fd + 1, &fds, diag);
#endif /* !HAVE_POLL */
	return (rv);
}


static void
failover_handler(int signo)
{
	if (!USING_PIPE_FOR_FAILOVER_SIGNAL(my_type))
		return; /* nothing to do */
	fd_event_notify(failover_notify_send_fd);
}

void
fd_event_notified(int event_fd,
	int do_logging, const char *event_name, const char *diag)
{
	ssize_t rv;
	char dummy[1];

	if (do_logging)
		gflog_info(GFARM_MSG_1004473,
		    "%s: %s notified", event_name, diag);
	rv = read(event_fd, dummy, sizeof dummy);
	if (rv == -1)
		gflog_error_errno(GFARM_MSG_1004474,
		    "%s: %s notified: read", event_name, diag);
	else if (rv != sizeof dummy)
		gflog_error(GFARM_MSG_1004475,
		    "%s: %s notified: size expected %zd but %zd",
		    diag, event_name, sizeof dummy, rv);
}

/* NOTE: this function is called from a signal handler */
void
fd_event_notify(int event_fd)
{
	char dummy[1];
	ssize_t rv;

	if (event_fd == -1)
		abort();
	dummy[0] = 0;
	rv = write(event_fd, dummy, sizeof dummy);
	if (rv != sizeof dummy)
		abort(); /* cannot call assert() from a signal handler */
}

static void
failover_notified(int do_logging, const char *diag)
{
	fd_event_notified(failover_notify_recv_fd,
	    do_logging, "failover", diag);
}

static int
sleep_or_wait_failover_recv_fd(int seconds)
{
	int rv;
	static const char diag[] = "sleep_or_wait_failover_signal";

#ifdef HAVE_POLL
	struct pollfd fds;

	fds.fd = failover_notify_recv_fd;
	fds.events = POLLIN;
	rv = sleep_or_wait_fds(seconds, 1, &fds, diag);
#else /* !HAVE_POLL */
	fd_set fds;

	if (failover_notify_recv_fd >= FD_SETSIZE)
		fatal(GFARM_MSG_1004212,
		    "too big descriptor: failover_fd:%d exceeds %d",
		    failover_notify_recv_fd, FD_SETSIZE);
	FD_ZERO(&fds);
	FD_SET(failover_notify_recv_fd, &fds);
	rv = sleep_or_wait_fds(seconds, failover_notify_recv_fd + 1, &fds,
	    diag);
#endif /* !HAVE_POLL */
	if (rv == 0)
		failover_notified(debug_mode, diag);
	return (rv);
}

static int
sleep_or_wait_failover_signal(int seconds, int signo)
{
#ifdef HAVE_SIGTIMEDWAIT
	sigset_t sigs;
	siginfo_t info;
	struct timespec interval, now;
	struct gfarm_timespec timeout, gn, gi;

	if (sigemptyset(&sigs) == -1)
		fatal(GFARM_MSG_1004098, "sigemptyset(): %s", strerror(errno));
	if (sigaddset(&sigs, signo) == -1)
		fatal(GFARM_MSG_1004099, "sigaddset(%d): %s",
		    signo, strerror(errno));

	gfarm_gettime(&now);
	timeout.tv_sec = now.tv_sec + seconds;
	timeout.tv_nsec = now.tv_nsec;
	interval.tv_sec = seconds;
	interval.tv_nsec = 0;
	for (;;) {
		if (sigtimedwait(&sigs, &info, &interval) == -1) {
			if (errno == EINTR) {
				gfarm_gettime(&now);
				gn.tv_sec = now.tv_sec;
				gn.tv_nsec = now.tv_nsec;
				if (gfarm_timespec_cmp(&gn, &timeout) < 0) {
					gi = timeout;
					gfarm_timespec_sub(&gi, &gn);
					interval.tv_sec = gi.tv_sec;
					interval.tv_nsec = gi.tv_nsec;
					continue;
				}
				errno = EAGAIN; /* signal wasn't received */
			}
			return (errno);
		}
		return (0);
	}
#else /* XXX in this case, the signal is just ignored */
	gfarm_sleep(seconds);
	return (EAGAIN); /* signal wasn't received */
#endif
}

static void
sleep_or_wait_failover(int seconds)
{
	if (my_type == type_listener)
		sleep_or_wait_failover_packet(seconds);
	else if (USING_PIPE_FOR_FAILOVER_SIGNAL(my_type))
		sleep_or_wait_failover_recv_fd(seconds);
	else
		sleep_or_wait_failover_signal(seconds, FAILOVER_SIGNAL);
}

/* return true, if the negotiation succeeds, or timed_out happens */
static int
negotiate_with_gfm_server(int n_config_vars, void **config_vars,
	const char *diag, gfarm_error_t *ep)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_begin_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1004374, "compound_begin request: %s",
		    gfarm_error_string(e));
	else if (canonical_self_name != NULL &&
	    (e = gfm_client_hostname_set_request(gfm_server,
	    canonical_self_name)) != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1004375,
		    "hostname_set(%s) request: %s", canonical_self_name,
		    gfarm_error_string(e));
	else if ((e = gfm_client_config_get_vars_request(
	    gfm_server, n_config_vars, config_vars)) != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1004376,
		    "config_get_vars() request: %s", gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1004377, "compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1004378, "compound_begin result: %s",
		    gfarm_error_string(e));
	else if (canonical_self_name != NULL &&
	    (e = gfm_client_hostname_set_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1004379,
		    "hostname_set(%s) result: %s", canonical_self_name,
		    gfarm_error_string(e));
	else if ((e = gfm_client_config_get_vars_result(
	    gfm_server, n_config_vars, config_vars)) != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1004380,
		    "config_get_vars() result: %s", gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1004381, "compound_end result: %s",
		    gfarm_error_string(e));

	*ep = e;
	if (e == GFARM_ERR_NO_ERROR) {
		int major_version = gfarm_version_major();
		int minor_version = gfarm_version_minor();
		int teeny_version = gfarm_version_teeny();

		if (gfarm_metadb_version_major < major_version ||
		    (gfarm_metadb_version_major == major_version &&
		     (gfarm_metadb_version_minor < minor_version ||
		      (gfarm_metadb_version_minor == minor_version &&
		       gfarm_metadb_version_teeny < teeny_version)))) {
			gflog_error(GFARM_MSG_1004382,
			    "gfmd version %d.%d.%d or later is expected, "
			    "but it's %d.%d.%d",
			    major_version, minor_version, teeny_version,
			    gfarm_metadb_version_major,
			    gfarm_metadb_version_minor,
			    gfarm_metadb_version_teeny);
			*ep = GFARM_ERR_PROTOCOL_NOT_SUPPORTED;
			return (1);
		}
		gflog_info(GFARM_MSG_1004103, "%s: connected to gfmd", diag);
		return (1);
	}
	if (!IS_CONNECTION_ERROR(e)) {
		gflog_error(GFARM_MSG_1004383,
		    "negotiation with gfmd failed (as node: %s): %s",
		    canonical_self_name != NULL ? canonical_self_name : "-",
		    gfarm_error_string(e));
		return (1);
	}
	/* caller of this function will report the error in *ep */
	return (0);
}

static gfarm_error_t
connect_gfm_server0(int n_config_vars, void **config_vars, const char *diag)
{
	gfarm_error_t e;
	int sleep_interval = GFMD_CONNECT_SLEEP_INTVL_MIN;
	struct gflog_reduced_state connlog = GFLOG_REDUCED_STATE_INITIALIZER(
		GFMD_CONNECT_SLEEP_LOG_OMIT, 1,
		GFMD_CONNECT_SLEEP_INTVL_MAX * 10,
		GFMD_CONNECT_SLEEP_LOG_INTERVAL);
	struct gflog_reduced_state negolog = GFLOG_REDUCED_STATE_INITIALIZER(
		GFMD_CONNECT_SLEEP_LOG_OMIT, 1,
		GFMD_CONNECT_SLEEP_INTVL_MAX * 10,
		GFMD_CONNECT_SLEEP_LOG_INTERVAL);

	for (;;) {
		e = gfm_client_connect(gfarm_ctxp->metadb_server_name,
		    gfarm_ctxp->metadb_server_port, GFSD_USERNAME,
		    &gfm_server, listen_addrname);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_reduced_notice(GFARM_MSG_1004101, &connlog,
			    "%s: connecting to gfmd at %s:%d failed, "
			    "sleep %d sec: %s", diag,
			    gfarm_ctxp->metadb_server_name,
			    gfarm_ctxp->metadb_server_port,
			    sleep_interval, gfarm_error_string(e));
		} else {
			if (negotiate_with_gfm_server(
			    n_config_vars, config_vars, diag, &e))
				return (e);
			gflog_reduced_notice(GFARM_MSG_1004384, &negolog,
			    "negotiation with gfmd failed (as node: %s), "
			    "sleep %d sec: %s",
			    canonical_self_name != NULL ?
			    canonical_self_name : "-",
			    sleep_interval, gfarm_error_string(e));
			/* retry if IS_CONNECTION_ERROR(e) */
		}
		sleep_or_wait_failover(sleep_interval);
		if (sleep_interval < GFMD_CONNECT_SLEEP_INTVL_MAX)
			sleep_interval *= 2;
	}

}

static void *config_vars[] = {
	&gfarm_metadb_version_major,
	&gfarm_metadb_version_minor,
	&gfarm_metadb_version_teeny,
};

static void *initial_config_vars[] = {
	&gfarm_metadb_version_major,
	&gfarm_metadb_version_minor,
	&gfarm_metadb_version_teeny,
	&gfarm_write_verify,
	&gfarm_write_verify_interval,
	&gfarm_write_verify_retry_interval,
	&gfarm_write_verify_log_interval,
};

gfarm_error_t
connect_gfm_server(const char *diag)
{
	return (connect_gfm_server0(
	    GFARM_ARRAY_LENGTH(config_vars), config_vars, diag));
}

gfarm_error_t
connect_gfm_server_at_first(const char *diag)
{
	return (connect_gfm_server0(
	    GFARM_ARRAY_LENGTH(initial_config_vars), initial_config_vars,
	    diag));
}

void
free_gfm_server(void)
{
	if (gfm_server == NULL)
		return;
	gfm_client_connection_free(gfm_server);
	gfm_server = NULL;
}

static void
reconnect_gfm_server_for_failover(const char *diag)
{
	gfarm_error_t e;

	gflog_notice(GFARM_MSG_1003348,
	    "%s: gfmd may be failed over, try to reconnecting", diag);
	free_gfm_server();
	if ((e = connect_gfm_server(diag))
	    != GFARM_ERR_NO_ERROR) {
		/* mark gfmd reconnection failed */
		free_gfm_server();
		fatal(GFARM_MSG_1004104,
		    "%s: cannot reconnect to gfm server: %s",
		    diag, gfarm_error_string(e));
	}
	fd_usable_to_gfmd = 0;
}


pid_t
do_fork(enum gfsd_type new_type)
{
	sigset_t old, new;
	pid_t rv;
	int save_errno;
	int i, pipefds[2];
	struct gfarm_iostat_items *statp;

	assert((my_type == type_listener &&
		(new_type == type_client ||
		 new_type == type_back_channel ||
		 new_type == type_write_verify_controller)) ||
	       (my_type == type_back_channel &&
		new_type == type_replication) ||
	       (my_type == type_write_verify_controller &&
		new_type == type_write_verify));

	/* block FAILOVER_SIGNAL to prevent race condition */
	if (sigemptyset(&new) == -1)
		fatal(GFARM_MSG_1004108, "sigemptyset(): %s", strerror(errno));
	if (sigaddset(&new, FAILOVER_SIGNAL) == -1)
		fatal(GFARM_MSG_1004109, "sigaddset(FAILOVER_SIGNAL): %s",
		    strerror(errno));
	if (sigprocmask(SIG_BLOCK, &new, &old) == -1)
		fatal(GFARM_MSG_1004110, "sigprocmask: block failover: %s",
		    strerror(errno));

	switch (new_type) {
	case type_client:
	case type_replication:
	case type_write_verify:
		statp = gfarm_iostat_find_space(0);
		break;
	default:
		statp = NULL;
		break;
	}

	rv = fork();
	save_errno = errno;
	if (rv == -1) {
		if (statp)
			gfarm_iostat_clear_ip(statp);
	} else if (rv != 0) { /* parent process */
		if (my_type == type_listener) {
			if (new_type == type_back_channel)
				back_channel_gfsd_pid = rv;
			else if (new_type == type_write_verify_controller)
				write_verify_controller_gfsd_pid = rv;
		}
		if (statp)
			gfarm_iostat_set_id(statp, (gfarm_uint64_t) rv);
	} else { /* child process */
		free_gfm_server(); /* to make sure to avoid race */
		if (statp) {
			gfarm_iostat_set_id(statp, (gfarm_uint64_t) getpid());
			gfarm_iostat_set_local_ip(statp);
		}
		if (my_type == type_listener) {
			for (i = 0; i < accepting.local_socks_count; i++) {
				close(accepting.local_socks[i].sock);
				accepting.local_socks[i].sock = -1;
			}
			close(accepting.tcp_sock);
			accepting.tcp_sock = -1;
			for (i = 0; i < accepting.udp_socks_count; i++) {
				close(accepting.udp_socks[i]);
				accepting.udp_socks[i] = -1;
			}
		}

		if (new_type == type_back_channel) {
			/* this should be set before fatal() */
			back_channel_gfsd_pid = getpid();
			gfsd_setup_iostat("bcs", gfarm_iostat_max_client);
		} else if (new_type == type_write_verify_controller)
			gfsd_setup_iostat("wv", 2);

		my_type = new_type; /* this should be set before fatal() */
		if (USING_PIPE_FOR_FAILOVER_SIGNAL(my_type)) {
			if (pipe(pipefds) == -1)
				fatal(GFARM_MSG_1004111, "pipe after fork: %s",
				    strerror(errno));
			failover_notify_recv_fd = pipefds[0];
			failover_notify_send_fd = pipefds[1];
		} else {
			if (failover_notify_recv_fd != -1)
				close(failover_notify_recv_fd);
			if (failover_notify_send_fd != -1)
				close(failover_notify_send_fd);
			failover_notify_recv_fd = failover_notify_send_fd = -1;
		}
	}
	if (sigprocmask(SIG_SETMASK, &old, NULL) == -1)
		fatal(GFARM_MSG_1004112, "sigprocmask: unblock failover: %s",
		    strerror(errno));
	errno = save_errno;
	return (rv);
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
	if (e != GFARM_ERR_NO_ERROR) {
		conn_fatal(GFARM_MSG_1000455, "%s get request: %s",
		    diag, gfarm_error_string(e));
	}
}

#define IS_IO_ERROR(e) \
	((e) == GFARM_ERR_INPUT_OUTPUT || (e) == GFARM_ERR_STALE_FILE_HANDLE)

static void
io_error_check(gfarm_error_t ecode, const char *diag)
{
	/* if input/output error occurs, die */
	if (IS_IO_ERROR(ecode)) {
		kill_master_gfsd = 1;
		fatal(GFARM_MSG_1002513, "%s: %s, die", diag,
		    gfarm_error_string(ecode));
	}
}

static void
io_error_check_errno(const char *diag)
{
	/* if input/output error occurs, die */
	if (errno == EIO || errno == ESTALE
#ifdef EUCLEAN
	    || errno == EUCLEAN
#endif
	    ) {
		kill_master_gfsd = 1;
		fatal(GFARM_MSG_1004213, "%s: %s, die", diag, strerror(errno));
	}
}

static void
sanity_check_ecode(const char *diag, gfarm_int32_t ecode)
{
	if (ecode < 0 || ecode >= GFARM_ERR_NUMBER) {
		gflog_notice(GFARM_MSG_1005230,
		    "%s: unexpected ecode: %d (%s)",
		    diag, (int)ecode, gfarm_error_string(ecode));
	}
}

void
gfs_server_put_reply_common(struct gfp_xdr *client, const char *diag,
	gfp_xdr_xid_t xid,
	gfarm_int32_t ecode, const char *format, va_list *app)
{
	gfarm_error_t e;

	if (debug_mode)
		gflog_debug(GFARM_MSG_1000458, "reply: %s: %d (%s)",
		    diag, (int)ecode, gfarm_error_string(ecode));

	/* sanity check, this shouldn't happen */
	sanity_check_ecode(diag, ecode);

	e = gfp_xdr_vsend_result(client, 1, ecode, format, app); /*do timeout*/
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(client);
	if (e != GFARM_ERR_NO_ERROR) {
		conn_fatal(GFARM_MSG_1000459, "%s put reply: %s",
		    diag, gfarm_error_string(e));
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

	/* sanity check, this shouldn't happen */
	sanity_check_ecode(diag, ecode);

	e = gfp_xdr_vsend_async_result_notimeout(
	    client, xid, ecode, format, app);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush_notimeout(client);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002382, "%s put reply: %s",
		    diag, gfarm_error_string(e));
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
	e = gfp_xdr_vsend_async_request_notimeout(bc_conn, async,
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
	static const char diag[] = "GFS_PROTO_PROCESS_SET";

	gfs_server_get_request(client, diag,
	    "ibl", &keytype, sizeof(sharedkey), &keylen, sharedkey, &pid);

	/*
	 * We don't have to check fd_usable_to_gfmd here.
	 * Let other protocol handlers notify GFARM_ERR_GFMD_FAILED_OVER to
	 * this client.  Thus, only gfs_server_process_reset() have to call
	 * close_all_fd_for_process_reset().
	 */

	if (gfm_client_process_is_set(gfm_server)) {
		gflog_debug(GFARM_MSG_1003399,
		    "process is already set");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = gfm_client_process_set(gfm_server,
	    keytype, sharedkey, keylen, pid)) != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003400,
		    "gfm_client_process_set: %s", gfarm_error_string(e));
	else
		(void)gfarm_proctitle_set("cilent/%lld %s",
		    (long long)pid, gflog_get_auxiliary_info());

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
	int i, failedover;
	static const char diag[] = "GFS_PROTO_PROCESS_RESET";

	gfs_server_get_request(client, diag,
	    "ibli", &keytype, sizeof(sharedkey), &keylen, sharedkey, &pid,
	    &failover_count);
	client_failover_count = failover_count;

	/*
	 * close all fd before client gets new fd from gfmd.
	 * if gfmd failed over, gfsd detects it in
	 * close_all_fd_for_process_reset().
	 */
	failedover = close_all_fd_for_process_reset(client);

	for (i = 0; i < 2; ++i) {
		e = gfm_client_process_set(gfm_server, keytype, sharedkey,
		    keylen, pid);
		if (e == GFARM_ERR_NO_ERROR) {
			fd_usable_to_gfmd = 1;
			(void)gfarm_proctitle_set("cilent/%lld %s",
			    (long long)pid, gflog_get_auxiliary_info());
			break;
		}
		if (e == GFARM_ERR_ALREADY_EXISTS) {
			if ((e = gfm_client_process_free(gfm_server))
			    != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1004113,
				    "gfm_client_process_free: %s",
				    gfarm_error_string(e));
			}
			continue;
		}
		gflog_notice(GFARM_MSG_1003401,
		    "gfm_client_process_set: %s", gfarm_error_string(e));
		if (!IS_CONNECTION_ERROR(e))
			break;
		/* gfmd failed over after close_all_fd() */
		if (i == 0) {
			reconnect_gfm_server_for_failover(diag);
			failedover = 1;
		}
	}
#if 0 /* currently, no need to tell the failedover flag to the client */
	if (failedover && e == GFARM_ERR_NO_ERROR)
		e = some other code, instead of GFARM_ERR_GFMD_FAILED_OVER;
#else
	(void)failedover;
#endif

	gfs_server_put_reply(client, diag, e, "");
}

static EVP_MD_CTX *
gfsd_msgdigest_alloc(const char *md_type_name,
	const char *diag, gfarm_ino_t diag_ino, gfarm_uint64_t diag_gen)
{
	EVP_MD_CTX *md_ctx;
	int cause;

	md_ctx = gfarm_msgdigest_alloc_by_name(md_type_name, &cause);
	if (md_ctx != NULL)
		return (md_ctx);

	if (cause)
		gflog_warning(GFARM_MSG_1004520,
		    "%s: inum %lld gen %lld: "
		    "digest type <%s> - %s",
		    diag,
		    (unsigned long long)diag_ino,
		    (unsigned long long)diag_gen, md_type_name,
		    strerror(cause));
	return (NULL);
}

/* with errno */
int
open_data(char *path, int flags)
{
	int fd = open(path, flags, DATA_FILE_MASK);
	static char diag[] = "open_data";

	io_error_check_errno(diag);
	if (fd >= 0)
		return (fd);
	if ((flags & O_CREAT) == 0 || errno != ENOENT)
		return (-1);
	if (gfsd_create_ancestor_dir(path))
		return (-1);
	fd = open(path, flags, DATA_FILE_MASK);
	io_error_check_errno(diag);
	return (fd);
}

int file_table_size = 0;

struct file_entry {
	off_t size;
	time_t mtime, atime;
	unsigned long mtimensec, atimensec;
	gfarm_ino_t ino;
	gfarm_uint64_t gen, new_gen;
	int local_fd;
	int local_fd_rdonly; /* only for register_to_lost_found() */
	int flags, local_flags;
#define FILE_FLAG_LOCAL		0x01
#define FILE_FLAG_CREATED	0x02
#define FILE_FLAG_WRITABLE	0x04
#define FILE_FLAG_WRITTEN	0x08
#define FILE_FLAG_READ		0x10
#define FILE_FLAG_DIGEST_CALC	0x20
#define FILE_FLAG_DIGEST_AVAIL	0x40
#define FILE_FLAG_DIGEST_FINISH	0x80
#define FILE_FLAG_DIGEST_ERROR	0x100
	/*
	 * if (md_type_name != NULL)
	 *	md_ctx was initialized, and EVP_DigestFinal() has to be called,
	 *	unless FILE_FLAG_DIGEST_FINISH bit is set.
	 *
	 * switch (flags & (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) {
	 * case  0:
	 *	do not calculate digest, or the digest was invalidated
	 * case  FILE_FLAG_DIGEST_CALC:
	 *	digest calculation is ongoing
	 * case (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH):
	 *	digest calculation is completed
	 * case  FILE_FLAG_DIGEST_FINISH:
	 *	digest calculation is completed, but the digest was invalidated
	 * }
	 */

/*
 * digest
 */
	off_t md_offset;

	char *md_type_name;
	EVP_MD_CTX *md_ctx;

	/* the followings are available if FILE_FLAG_DIGEST_FINISH is set */
	char md_string[GFARM_MSGDIGEST_STRSIZE];
	size_t md_strlen;

/*
 * performance data (only available in profile mode)
 */
	struct timeval start_time;
	unsigned nwrite, nread;
	double write_time, read_time;
	gfarm_off_t write_size, read_size;
#ifdef HAVE_INFINIBAND
	double rdma_write_time, rdma_read_time;
	gfarm_off_t rdma_write_size, rdma_read_size;
#endif
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
	for (i = 0; i < table_size; i++) {
		file_table[i].local_fd = -1;
		file_table[i].local_fd_rdonly = -1;
	}
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

/*
 * confirm_local_path() should never return 0,
 * so this function is purely for sanity check.
 *
 * the reason why this never returns 0 is:
 * - gfmd ensures that only one gfsd process can create a new replica
 *   by using the `to_create' result of GFM_PROTO_REOPEN RPC.
 *   (`to_create' result will be converted to O_CREAT flag for open(2))
 *   simultaneously running other gfsd processes fail to open the replica
 *   before the creation due to the lack of the O_CREAT flag, and
 *   the gfsd processes retry opening the replica in gfs_server_open_common().
 *   gflog_debug(GFARM_MSG_1002299, ...) will be called in the case of
 *   the retry.
 * - if a replication is ongoing, file creation won't be scheduled.
 *   gfmd ensures this by its FILE_COPY_IS_VALID() check.
 * - if a dead file copy of the replica remains, any replica creation or any
 *   replication won't be scheduled to the gfsd which has the dead file copy.
 *   gfmd ensures this by its FILE_COPY_IS_BEING_REMOVED() check.
 * - simultaneous replication, file creation, file deletion won't happen.
 *   gfmd ensures this by its FILE_COPY_IS_VALID() check and
 *   FILE_COPY_IS_BEING_REMOVED() check.
 */
static int
confirm_local_path(gfarm_ino_t inum, gfarm_uint64_t gen, const char *diag)
{
	char *p;
	static int length = 0;
	static char template[] = "/data/00112233/44/55/66/778899AABBCCDDEEFF";
	static char format[] = "/data/%08X/%02X/%02X/%02X/%02X%08X%08X";
	struct stat sb;
	int i, n = 0;

	if (gfarm_spool_root_num == 1)
		return (1);

	if (length == 0)
		length = gfarm_spool_root_len_max + sizeof(template);

	snprintf(template, sizeof(template), format,
	    (unsigned int)((inum >> 32) & 0xffffffff),
	    (unsigned int)((inum >> 24) & 0xff),
	    (unsigned int)((inum >> 16) & 0xff),
	    (unsigned int)((inum >>  8) & 0xff),
	    (unsigned int)(inum         & 0xff),
	    (unsigned int)((gen  >> 32) & 0xffffffff),
	    (unsigned int)(gen          & 0xffffffff));

	GFARM_MALLOC_ARRAY(p, length);
	if (p == NULL) {
		fatal(GFARM_MSG_1004492, "%s: no memory for %d bytes",
			diag, length);
	}
	for (i = 0; i < gfarm_spool_root_num; ++i) {
		char *r = gfarm_spool_root[i];

		if (r == NULL)
			break;
		snprintf(p, length, "%s%s", r, template);
		if (stat(p, &sb) == 0)
			++n;
	}
	free(p);
	return (n == 1);
}

static void
move_to_local_lost_found(char *path, const char *diag)
{
	char *p, *pp, *root = NULL;
	int i;

	for (i = 0; i < gfarm_spool_root_num; ++i) {
		root = gfarm_spool_root[i];
		if (root == NULL)
			break;
		if (strncmp(root, path, strlen(root)) == 0)
			break;
	}
	if (root == NULL || i == gfarm_spool_root_num) {
		gflog_error(GFARM_MSG_1004493, "%s: no spool root, "
		    "move inconsistent file manually: %s", diag, path);
		return;
	}
	p = strdup(path);
	if (p == NULL)
		fatal(GFARM_MSG_1004494, "%s: no memory for %d bytes",
		    diag, (int)strlen(path) + 1);
	for (pp = p + strlen(root) + 1; *pp; ++pp) {
		if (*pp == '/')
			*pp = '_';
	}
	if (rename(path, p) == -1) {
		gflog_error(GFARM_MSG_1004495,
		    "%s: rename(%s, %s) failed, move inconsistent file "
		    "manually: %s", diag, path, p, strerror(errno));
	} else
		gflog_warning(GFARM_MSG_1004496, "%s: race detected: "
		    "%s moved to %s", diag, path, p);
	free(p);
}

static gfarm_error_t
file_table_add(gfarm_int32_t net_fd,
	int flags, gfarm_ino_t ino, gfarm_uint64_t gen, char *cksum_type,
	size_t cksum_len, char *cksum, int cksum_flags, struct timeval *start,
	int *local_fdp, const char *diag)
{
	struct file_entry *fe;
	int local_fd, local_fd_rdonly, r, save_errno, is_new_file = 0;
	struct stat st;
	char *path;

	gfsd_local_path(ino, gen, diag, &path);
	r = lstat(path, &st);
	if (r == -1) {
		if (errno != ENOENT)
			fatal_errno(GFARM_MSG_1003769, "%s: %s", diag, path);
		is_new_file = 1;
	}
	local_fd = open_data(path, flags);
	if (local_fd == -1) {
		free(path);
		return (gfarm_errno_to_error(errno));
	}
	if (is_new_file && !confirm_local_path(ino, gen, diag)) {
		close(local_fd);
		move_to_local_lost_found(path, diag);
		free(path);
		return (GFARM_ERR_INTERNAL_ERROR);
	}
	if (r < 0 && fstat(local_fd, &st) < 0)
		fatal_errno(GFARM_MSG_1000463, "%s: %s", diag, path);
	if ((flags & O_ACCMODE) != O_WRONLY) {
		local_fd_rdonly = -1;
	} else if ((local_fd_rdonly = open_data(path,
	    (flags & ~O_ACCMODE) | O_RDONLY)) == -1) {
		save_errno = errno;
		close(local_fd);
		free(path);
		return (gfarm_errno_to_error(save_errno));
	}
	free(path);
	fe = &file_table[net_fd];
	fe->local_fd = *local_fdp = local_fd;
	fe->local_fd_rdonly = local_fd_rdonly;
	fe->local_flags = flags;
	fe->flags = 0;
	fe->ino = ino;
	if (flags & O_CREAT)
		fe->flags |= FILE_FLAG_CREATED;
	if ((flags & O_TRUNC) != 0)
		fe->flags |= FILE_FLAG_WRITTEN;
	/*
	 * if it's opened for O_RDONLY, do not set FILE_FLAG_WRITTEN,
	 * even if is_new_file is true, because the FILE_FLAG_WRITTEN flag
	 * makes gfsd issue GFM_PROTO_CLOSE_WRITE_V2_4 against the O_RDONLY
	 * descriptor, and causes GFARM_ERR_BAD_FILE_DESCRIPTOR (SF.net #957).
	 * that means gfs_stat() and gfs_pio_stat() shows inconsistent result
	 * in the O_RDONLY case, but currently no application is known to
	 * cause a problem due to the inconsistency.
	 */
	if ((flags & O_ACCMODE) != O_RDONLY) {
		if (is_new_file) {
			/*
			 * SF.net #942 - mtime inconsistency between
			 * gfs_pio_stat() and gfs_stat().
			 *
			 * FILE_FLAG_WRITTEN has to be set here to fix
			 * the inconsistency, for the case when GNU tar
			 * extracts a 0-byte file.
			 *
			 * XXX: if this file was created a long time ago,
			 * and won't be written this time as well,
			 * undesired st_mtime change will happen.
			 */
			fe->flags |= FILE_FLAG_WRITTEN;
		}
		fe->flags |= FILE_FLAG_WRITABLE;
		++write_open_count;
	}
	fe->atime = st.st_atime;
	fe->atimensec = gfarm_stat_atime_nsec(&st);
	fe->mtime = st.st_mtime;
	fe->mtimensec = gfarm_stat_mtime_nsec(&st);
	fe->size = st.st_size;
	fe->gen = fe->new_gen = gen;

	/* checksum */
	if (cksum_len > sizeof(fe->md_string)) {
		gflog_warning(GFARM_MSG_1004115,
		    "%s: inum %lld gen %lld: "
		    "digest type <%s> len:%zd is larger than %zd: unsupported",
		    diag,
		    (unsigned long long)ino,
		    (unsigned long long)gen, cksum_type, cksum_len,
		    sizeof(fe->md_string));
		free(cksum_type);
		fe->md_type_name = NULL;
	} else if ((fe->md_ctx = gfsd_msgdigest_alloc(
	    cksum_type, diag, ino, gen)) == NULL)  {
		free(cksum_type);
		fe->md_type_name = NULL;
	} else {
		/* memory owner of cksum_type is moved to `fe->md_type_name' */
		fe->md_type_name = cksum_type;
		if (cksum_len == 0) {
			fe->md_strlen = 0;
		} else {
			fe->flags |= FILE_FLAG_DIGEST_AVAIL;
			memcpy(fe->md_string, cksum, cksum_len);
			fe->md_strlen = cksum_len;
		}
		if ((cksum_flags & (GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED|
		    GFM_PROTO_CKSUM_GET_EXPIRED)) != 0)
			gflog_debug(GFARM_MSG_1003770,
			    "%lld:%lld cksum flag %d, may be expired",
			    (long long)fe->ino, (long long)fe->gen,
			    cksum_flags);
		else
			fe->flags |= FILE_FLAG_DIGEST_CALC;
		fe->md_offset = 0;
	}
	/* performance data (only available in profile mode) */
	fe->start_time = *start;
	fe->nwrite = fe->nread = 0;
	fe->write_time = fe->read_time = 0;
	fe->write_size = fe->read_size = 0;
#ifdef HAVE_INFINIBAND
	fe->rdma_write_time = fe->rdma_read_time = 0;
	fe->rdma_write_size = fe->rdma_read_size = 0;
#endif
	return (GFARM_ERR_NO_ERROR);
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

	if (close(fe->local_fd) == -1)
		e = gfarm_errno_to_error(errno);
	else
		e = GFARM_ERR_NO_ERROR;
	fe->local_fd = -1;

	if (fe->local_fd_rdonly != -1) {
		if (close(fe->local_fd_rdonly) == -1)
			gflog_warning_errno(GFARM_MSG_1004116,
			    "read-only fd close(%d) for inode %lld:%lld",
			    fe->local_fd_rdonly,
			    (long long)fe->ino, (long long)fe->gen);
		fe->local_fd_rdonly = -1;
	}

	if (fe->md_type_name != NULL &&
	    (fe->flags & FILE_FLAG_DIGEST_FINISH) == 0) {
		unsigned char md_value[EVP_MAX_MD_SIZE];

		/* We need to do this to avoid memory leak */
		gfarm_msgdigest_free(fe->md_ctx, md_value);
	}
	free(fe->md_type_name);
	fe->md_type_name = NULL;

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
		    fe->nread, (long long)fe->read_size, fe->read_time);
		IF_INFINIBAND(
			gflog_info(GFARM_MSG_1004716,
			    "rdma_write size %lld time %g Bps %g "
			    "rdma_read size %lld time %g Bps %g",
			    (long long)fe->rdma_write_size,
			    fe->rdma_write_time,
			    fe->rdma_write_time > 0
			    ? (fe->rdma_write_size / fe->rdma_write_time)
			    : fe->rdma_write_time,
			    (long long)fe->rdma_read_size,
			    fe->rdma_read_time,
			    fe->rdma_read_time > 0
			    ? (fe->rdma_read_size / fe->rdma_read_time)
			    : fe->rdma_read_time);
		)
	);

	if ((fe->flags & FILE_FLAG_WRITABLE) != 0) {
		--write_open_count;
		if (terminate_flag && write_open_count == 0) {
			gflog_debug(GFARM_MSG_1003432, "bye");
			cleanup(0);
			exit(0);
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
file_table_unset_flag(gfarm_int32_t net_fd, int flags)
{
	struct file_entry *fe = file_table_entry(net_fd);

	if (fe != NULL)
		fe->flags &= ~flags;
}

static void
file_table_set_read(gfarm_int32_t net_fd)
{
	struct file_entry *fe = file_table_entry(net_fd);

	if (fe == NULL)
		return;

	fe->flags |= FILE_FLAG_READ;
}

static void
file_table_set_written(gfarm_int32_t net_fd)
{
	struct file_entry *fe = file_table_entry(net_fd);

	if (fe == NULL)
		return;

	fe->flags |= FILE_FLAG_WRITTEN;
}

static void
file_table_for_each(void (*callback)(struct gfp_xdr *, void *, gfarm_int32_t),
	struct gfp_xdr *client, void *closure)
{
	gfarm_int32_t net_fd;

	if (file_table == NULL)
		return;

	for (net_fd = 0; net_fd < file_table_size; net_fd++) {
		if (file_table[net_fd].local_fd != -1)
			(*callback)(client, closure, net_fd);
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


char *
gfsd_make_path(const char *relpath, const char *diag)
{
	/* gfarm_spool_root + "/" + relpath + "\0" */
	size_t length = gfarm_spool_root_len[0] + 1 + strlen(relpath) + 1;
	char *p;

	GFARM_MALLOC_ARRAY(p, length);
	if (p == NULL) {
		fatal(GFARM_MSG_1004385, "%s: no memory for %s/%s (%zd bytes)",
		      diag, gfarm_spool_root[0], relpath, length);
	}
	snprintf(p, length, "%s/%s", gfarm_spool_root[0], relpath);
	return (p);
}

char *
gfsd_skip_spool_root(char *path)
{
	int i, len;

	for (i = 0; i < gfarm_spool_root_num; ++i) {
		len = strlen(gfarm_spool_root[i]);
		if (strncmp(gfarm_spool_root[i], path, len) == 0 &&
		    path[len] == '/')
			return (path + len + 1);
	}
	return (path);
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
gfsd_local_path2(gfarm_ino_t inum, gfarm_uint64_t gen, const char *diag,
	char **pathp, gfarm_ino_t inum2, gfarm_uint64_t gen2,
	const char *diag2, char **pathp2)
{
	char *p, *p2;
	static int length = 0;
	static char template[] = "/data/00112233/44/55/66/778899AABBCCDDEEFF";
	static char template2[] = "/data/00112233/44/55/66/778899AABBCCDDEEFF";
	static char format[] = "/data/%08X/%02X/%02X/%02X/%02X%08X%08X";
	int i, max_i = 0;
	struct statvfs fsb;
	struct stat sb;
	unsigned long long max_avail = 0, avail;
#define DIRLEVEL 5 /* there are 5 levels of directories in template[] */

	if (length == 0)
		length = gfarm_spool_root_len_max + sizeof(template);

	snprintf(template, sizeof(template), format,
	    (unsigned int)((inum >> 32) & 0xffffffff),
	    (unsigned int)((inum >> 24) & 0xff),
	    (unsigned int)((inum >> 16) & 0xff),
	    (unsigned int)((inum >>  8) & 0xff),
	    (unsigned int)(inum         & 0xff),
	    (unsigned int)((gen  >> 32) & 0xffffffff),
	    (unsigned int)(gen          & 0xffffffff));

	GFARM_MALLOC_ARRAY(p, length);
	if (p == NULL) {
		fatal(GFARM_MSG_1000464, "%s: no memory for %d bytes",
			diag, length);
	}
	for (i = 0; i < gfarm_spool_root_num; ++i) {
		char *r = gfarm_spool_root[i];

		if (r == NULL)
			break;
		snprintf(p, length, "%s%s", r, template);
		if (stat(p, &sb) == 0) {
			max_i = i;
			break;
		}
		if (gfarm_spool_root_num == 1)
			break;
		if (statvfs(r, &fsb))
			gflog_fatal_errno(GFARM_MSG_1004478, "%d %s", i, r);
		if (gfsd_is_readonly_mode(i)) {
			/* pretend to be disk full to make gfsd read-only */
			fsb.f_bavail = fsb.f_bfree = 0;
		}
		avail = fsb.f_bsize * fsb.f_bavail;
		if (max_avail < avail) {
			max_avail = avail;
			max_i = i;
		}
	}
	if (gfarm_spool_root_num > 1 && i == gfarm_spool_root_num &&
		max_i != i - 1)
		snprintf(p, length, "%s%s", gfarm_spool_root[max_i], template);
	*pathp = p;

	if (inum2 != 0) {
		snprintf(template2, sizeof(template2), format,
		    (unsigned int)((inum2 >> 32) & 0xffffffff),
		    (unsigned int)((inum2 >> 24) & 0xff),
		    (unsigned int)((inum2 >> 16) & 0xff),
		    (unsigned int)((inum2 >>  8) & 0xff),
		    (unsigned int)(inum2         & 0xff),
		    (unsigned int)((gen2  >> 32) & 0xffffffff),
		    (unsigned int)(gen2          & 0xffffffff));

		GFARM_MALLOC_ARRAY(p2, length);
		if (p2 == NULL) {
			fatal(GFARM_MSG_1004479, "%s: no memory for %d bytes",
				diag2, length);
		}
		snprintf(p2, length, "%s%s", gfarm_spool_root[max_i],
		    template2);
		*pathp2 = p2;
	}
}

void
gfsd_local_path(gfarm_ino_t inum, gfarm_uint64_t gen, const char *diag,
	char **pathp)
{
	gfsd_local_path2(inum, gen, diag, pathp, 0, 0, NULL, NULL);
}

/* with errno */
int
gfsd_create_ancestor_dir(char *path)
{
	int i, j, tail, slashpos[DIRLEVEL], save_errno;
	struct stat st;

	/* errno == ENOENT, so, maybe we don't have an ancestor directory */
	tail = strlen(path);
	for (i = 0; i < DIRLEVEL; i++) {
		for (--tail; tail > 0 && path[tail] != '/'; --tail)
			;
		if (tail <= 0) {
			gflog_warning(GFARM_MSG_1000465,
			    "something wrong in local_path(): %s", path);
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
			save_errno = errno;
			if (errno == ENOENT)
				continue;
			if (errno == EEXIST) {
				/* maybe race */
			} else {
				gflog_error(GFARM_MSG_1000467,
				    "mkdir(`%s') failed: %s", path,
				    strerror(errno));
				errno = save_errno;
				return (-1);
			}
		}
		/* Now, we have the ancestor directory */
		for (j = i;; --j) {
			path[slashpos[j]] = '/';
			if (j <= 0)
				break;
			if (mkdir(path, DATA_DIR_MASK) < 0) {
				save_errno = errno;
				if (errno == EEXIST) /* maybe race */
					continue;
				gflog_warning(GFARM_MSG_1000468,
				    "unexpected mkdir(`%s') failure: %s",
				    path, strerror(errno));
				errno = save_errno;
				return (-1);
			}
		}
		return (0);
	}
	gflog_warning(GFARM_MSG_1000469,
	    "gfsd spool_root doesn't exist?: %s", path);
	errno = ENOENT;
	return (-1);
}

gfarm_error_t
gfsd_copy_file(int fd, gfarm_ino_t inum, gfarm_uint64_t gen, const char *diag,
	char **pathp)
{
#define COPY_BLOCK_SIZE 65536
	char buf[COPY_BLOCK_SIZE], *path;
	ssize_t sz, rv;
	int dst, i, save_e;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (lseek(fd, 0, SEEK_SET) == -1)
		return (gfarm_errno_to_error(errno));
	gfsd_local_path(inum, gen, diag, &path);
	dst = open_data(path, O_WRONLY|O_CREAT|O_TRUNC);
	if (dst < 0) {
		save_e = errno;
		free(path);
		return (gfarm_errno_to_error(save_e));
	}
	if (!confirm_local_path(inum, gen, diag)) {
		close(dst);
		move_to_local_lost_found(path, diag);
		free(path);
		return (GFARM_ERR_INTERNAL_ERROR);
	}
	while ((sz = read(fd, buf, sizeof buf)) > 0
	       || (sz == -1 && errno == EINTR)) {
		for (i = 0; i < sz; i += rv) {
			rv = write(dst, buf + i, sz - i);
			if (rv > 0)
				continue;
			else if (rv == 0)
				e = GFARM_ERR_NO_SPACE;
			else if (errno == EINTR) {
				rv = 0;
				continue;
			} else
				e = gfarm_errno_to_error(errno);
			break;
		}
		if (i < sz)
			break;
	}
	if (sz == -1 && e == GFARM_ERR_NO_ERROR)
		e = gfarm_errno_to_error(errno);
	close(dst);
	if (*pathp != NULL)
		*pathp = path;
	else
		free(path);
	return (e);
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
gfm_client_compound_put_fd_result(struct gfp_xdr *client, const char *diag)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_end_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002293,
		    "gfmd protocol: compound_end request error on %s: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_compound_begin_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_notice(GFARM_MSG_1002294,
		    "gfmd protocol: compound_begin result error on %s: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_put_fd_problem(GFARM_MSG_1002295, client, e,
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
gfs_server_reopen(const char *diag, struct gfp_xdr *client,
	gfarm_int32_t net_fd, int *net_flagsp, int *to_createp,
	gfarm_ino_t *inop, gfarm_uint64_t *genp, char **cksum_typep,
	size_t *cksum_lenp, char cksum[], int *cksum_flagsp)
{
	gfarm_error_t e;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	gfarm_int32_t mode, net_flags, to_create;
	int cksum_flags;
	char *cksum_type, tmp_cksum[GFM_PROTO_CKSUM_MAXLEN];
	size_t cksum_len;

	if ((e = gfm_client_compound_put_fd_request(net_fd, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004117,
		    "%s: compound_put_fd_request fd=%d: %s",
		    diag, net_fd, gfarm_error_string(e));
	else if ((e = gfm_client_reopen_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004118,
		    "%s: reopen_request fd=%d: %s",
		    diag, net_fd, gfarm_error_string(e));
	else if ((e = gfm_client_cksum_get_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003772,
		    "%s cksum_get request: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_compound_put_fd_result(client, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_put_fd_problem(GFARM_MSG_1003332, client, e,
		    "%s: put_fd_result fd=%d: %s",
		    diag, net_fd, gfarm_error_string(e));
	else if ((e = gfm_client_reopen_result(gfm_server,
	    &ino, &gen, &mode, &net_flags, &to_create))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004119,
		    "%s: reopen_result fd=%d: %s",
		    diag, net_fd, gfarm_error_string(e));
	} else if ((e = gfm_client_cksum_get_result(gfm_server, &cksum_type,
	     sizeof tmp_cksum, &cksum_len, tmp_cksum, &cksum_flags))
	    != GFARM_ERR_NO_ERROR)
		gflog_info(GFARM_MSG_1003773,
		    "%s cksum_get result: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_compound_end(diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004120,
		    "%s: compound_end fd=%d: %s",
		    diag, net_fd, gfarm_error_string(e));
	} else if (!GFARM_S_ISREG(mode)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		/* this shouldn't happen */
		gflog_error(GFARM_MSG_1003699, "ino=%lld gen=%lld: "
		    "mode:0%o, flags:0x%0x, to_create:%d: bad mode",
		    (long long)ino, (long long)gen,
		    mode, net_flags, to_create);
	} else {
		*net_flagsp = net_flags;
		*to_createp = to_create;
		*inop = ino;
		*genp = gen;
		*cksum_typep = cksum_type;
		*cksum_lenp = cksum_len;
		memcpy(cksum, tmp_cksum, cksum_len);
		*cksum_flagsp = cksum_flags;
	}

	if (IS_CONNECTION_ERROR(e)) {
		reconnect_gfm_server_for_failover("gfs_server_reopen");
		e = GFARM_ERR_GFMD_FAILED_OVER;
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
		gflog_error(GFARM_MSG_1004121,
		    "%s: replica_lost_request ino=%lld gen=%lld: %s",
		    diag, (unsigned long long)ino, (unsigned long long)gen,
		    gfarm_error_string(e));
	else if ((e = gfm_client_replica_lost_result(gfm_server))
	     != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT)
		if (debug_mode)
			gflog_info(GFARM_MSG_1004122,
			    "%s: replica_lost_result ino=%lld gen=%lld: %s",
			    diag, (unsigned long long)ino,
			    (unsigned long long)gen, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
close_on_metadb_server(struct gfp_xdr *client, gfarm_int32_t fd,
	const char *diag)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_put_fd_request(fd, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004123,
		    "%s: compound_put_fd_request fd=%d: %s",
		    diag, fd, gfarm_error_string(e));
	else if ((e = gfm_client_close_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004124,
		    "%s: close_request fd=%d: %s",
		    diag, fd, gfarm_error_string(e));
	else if ((e = gfm_client_compound_put_fd_result(client, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_put_fd_problem(GFARM_MSG_1004125, client, e,
		    "%s: compound_put_fd_result fd=%d: %s",
		    diag, fd, gfarm_error_string(e));
	else if ((e = gfm_client_close_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004126,
		    "%s: close_result() fd=%d: %s",
		    diag, fd, gfarm_error_string(e));
	else if ((e = gfm_client_compound_end(diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004127,
		    "%s: compound_end fd=%d: %s",
		    diag, fd, gfarm_error_string(e));

	if (IS_CONNECTION_ERROR(e)) {
		reconnect_gfm_server_for_failover("close_on_metadb_server");
		e = GFARM_ERR_GFMD_FAILED_OVER;
	}

	return (e);
}

gfarm_error_t
gfs_server_open_common(struct gfp_xdr *client, const char *diag,
	gfarm_int32_t *net_fdp, int *local_fdp)
{
	gfarm_error_t e, e2;
	gfarm_ino_t ino = 0;
	gfarm_uint64_t gen = 0;
	gfarm_int32_t net_flags = 0;
	int to_create = 0;
	int net_fd, local_flags;
	char *cksum_type = NULL, cksum[GFM_PROTO_CKSUM_MAXLEN];
	size_t cksum_len = 0;
	int cksum_flags = 0;
	struct timeval start;

	gettimeofday(&start, NULL);

	gfs_server_get_request(client, diag, "i", &net_fd);

	if (!fd_usable_to_gfmd) {
		e = GFARM_ERR_GFMD_FAILED_OVER;
	} else if (!file_table_is_available(net_fd)) {
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
		gflog_debug(GFARM_MSG_1002171,
			"bad file descriptor");
	} else {
		for (;;) {
			if ((e = gfs_server_reopen(diag, client, net_fd,
			    &net_flags, &to_create, &ino, &gen,
			    &cksum_type, &cksum_len, cksum, &cksum_flags)) !=
			    GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002172,
					"gfs_server_reopen() failed: %s",
					gfarm_error_string(e));
				break;
			}

			if ((local_flags = gfs_open_flags_localize(net_flags))
			    == -1) {
				/* this shouldn't happen */
				gflog_error(GFARM_MSG_1004128,
				    "ino=%lld gen=%lld: "
				    "flags:0x%0x, to_create:%d: bad flags",
				    (long long)ino, (long long)gen,
				    net_flags, to_create);
				e = GFARM_ERR_OPERATION_NOT_PERMITTED;
				break;
			}
			if (to_create)
				local_flags |= O_CREAT;
			e2 = file_table_add(net_fd, local_flags,
			    ino, gen, cksum_type, cksum_len, cksum,
			    cksum_flags, &start, local_fdp, diag);
			if (e2 == GFARM_ERR_NO_ERROR) {
				/*
				 * the memory owner of cksum_type is moved
				 * to the file_table_entry
				 */
				*net_fdp = net_fd;
				break;
			}
			free(cksum_type);

			if ((e = close_on_metadb_server(client, net_fd, diag))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1004129,
				    "close_on_metadb_server: %s",
				    gfarm_error_string(e));
				break;
			}

			if (e2 == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY ||
			    e2 == GFARM_ERR_NO_SPACE) {
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
			} else
				gflog_error(GFARM_MSG_1004480, "%s: "
				    "%lld:%lld: %s", diag, (long long)ino,
				    (long long)gen, gfarm_error_string(e2));
			e = e2;
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
	file_table_unset_flag(net_fd, FILE_FLAG_DIGEST_CALC);
}

gfarm_error_t
close_request(struct file_entry *fe)
{
	if (fe->flags & FILE_FLAG_WRITTEN) {
		return (gfm_client_close_write_v2_8_request(gfm_server));
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
		return (gfm_client_fhclose_write_v2_8_request(gfm_server,
		    fe->ino, fe->gen));
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
    gfarm_int64_t new_gen, const char *conflict_message)
{
	gfarm_error_t e = GFARM_ERR_INTERNAL_ERROR;
	int save_errno;
	char *old, *new;
	struct stat old_st, new_st, *st;
	int old_avail = 0, new_avail = 0;

	gfsd_local_path2(fe->ino, old_gen, "close_write: old", &old,
	    fe->ino, new_gen, "close_write: new", &new);
	if (rename(old, new) == -1) {
		save_errno = errno;
		gflog_error(GFARM_MSG_1004130,
		    "inode %llu:%llu: new generation %llu -> %llu: %s",
		    (unsigned long long)fe->ino,
		    (unsigned long long)fe->gen,
		    (unsigned long long)old_gen,
		    (unsigned long long)new_gen,
		    strerror(save_errno));
		e = gfarm_errno_to_error(save_errno);
	} else {
		if (stat(new, &new_st) != -1) {
			new_avail = 1;
		} else {
			save_errno = errno;
			gflog_error(GFARM_MSG_1004131,
			    "inode %llu:%llu: new generation %llu -> %llu: "
			    "stat(\"%s\"): %s",
			    (unsigned long long)fe->ino,
			    (unsigned long long)fe->gen,
			    (unsigned long long)old_gen,
			    (unsigned long long)new_gen,
			    new, strerror(save_errno));
			/*
			 * gfmd treats ENOENT as a kind of fatal error,
			 * but ENOENT here is not fatal.
			 * So we assign a different error code
			 */
			if (save_errno == ENOENT)
				e = GFARM_ERR_NO_SUCH_OBJECT;
			else
				e = gfarm_errno_to_error(save_errno);
		}
		if (fstat(fe->local_fd, &old_st) != -1) {
			old_avail = 1;
		} else {
			save_errno = errno;
			gflog_error(GFARM_MSG_1004132,
			    "inode %llu:%llu: new generation %llu -> %llu: "
			    "fstat(\"%s\"?): %s",
			    (unsigned long long)fe->ino,
			    (unsigned long long)fe->gen,
			    (unsigned long long)old_gen,
			    (unsigned long long)new_gen,
			    new, strerror(save_errno));
			if (new_avail)
				e = gfarm_errno_to_error(save_errno);
		}
		if (!new_avail || !old_avail) {
			if (new_avail) {
				st = &new_st;
			} else if (old_avail) {
				st = &old_st;
			} else {
				st = NULL;
			}
			/* `e' must be already set */
		} else if (new_st.st_ino != old_st.st_ino) {
			gflog_error(GFARM_MSG_1004133,
			    "inode %llu:%llu: new generation %llu -> %llu: "
			    "st_ino old:%lld differs from new:%lld - %s",
			    (unsigned long long)fe->ino,
			    (unsigned long long)fe->gen,
			    (unsigned long long)old_gen,
			    (unsigned long long)new_gen,
			    (unsigned long long)old_st.st_ino,
			    (unsigned long long)new_st.st_ino,
			    conflict_message);
			st = &new_st;
			/* rename(2) and {f,}stat(2) never return this error */
			e = GFARM_ERR_CONFLICT_DETECTED;
		} else {
			st = &new_st;
			e = GFARM_ERR_NO_ERROR;
			if (gfarm_write_verify) {
				/*
				 * request even if cksum does not exist at this
				 * point,  because it may be added later.
				 */
				write_verify_request(
				    fe->ino, new_gen, new_st.st_mtime,
				    "generation update");
			}
		}
		fe->new_gen = new_gen; /* rename(2) succeeded, at least */

		if (st != NULL) {
			/*
			 * update file_table, because
			 * another process might modify these values
			 */
			file_entry_set_size(fe, st->st_size);
			file_entry_set_atime(fe,
			    st->st_atime, gfarm_stat_atime_nsec(st));
			file_entry_set_mtime(fe,
			    st->st_mtime, gfarm_stat_mtime_nsec(st));
		}
	}
	free(old);
	free(new);

	return (e);
}

gfarm_error_t
close_result(struct file_entry *fe, gfarm_int32_t *gen_update_result_p)
{
	gfarm_error_t e;
	gfarm_int32_t flags;
	gfarm_int64_t old_gen, new_gen;

	if (fe->flags & FILE_FLAG_WRITTEN) {
		e = gfm_client_close_write_v2_8_result(gfm_server,
		    &flags, &old_gen, &new_gen);
		if (e == GFARM_ERR_NO_ERROR &&
		    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED))
			*gen_update_result_p = update_local_file_generation(
			    fe, old_gen, new_gen, "unexpected inconsistency");
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
		e = gfm_client_fhclose_write_v2_8_result(gfm_server,
		    &flags, &old_gen, &new_gen, cookie_p);
		if (e == GFARM_ERR_NO_ERROR &&
		    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED))
			*gen_update_result_p = update_local_file_generation(
			    fe, old_gen, new_gen,
			    "an update conflict is caused by gfmd failover");
		return (e);
	} else if (fe->flags & FILE_FLAG_READ) {
		return (gfm_client_fhclose_read_result(gfm_server));
	} else {
		return (GFARM_ERR_NO_ERROR);
	}
}

gfarm_error_t
calc_digest(int fd,
	const char *md_type_name, char *md_string, size_t *md_strlenp,
	gfarm_off_t *calc_lenp,
	char *data_buf, size_t data_bufsize,
	const char *diag, gfarm_ino_t diag_ino, gfarm_uint64_t diag_gen)
{
	ssize_t sz;
	gfarm_off_t calc_len = 0;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	EVP_MD_CTX *md_ctx;
	unsigned int md_len;
	unsigned char md_value[EVP_MAX_MD_SIZE];

	/* do this before msgdigest_init() to prevent memory leak */
	if (lseek(fd, 0, SEEK_SET) == -1)
		return (gfarm_errno_to_error(errno));

	md_ctx = gfsd_msgdigest_alloc(md_type_name, diag, diag_ino, diag_gen);
	if (md_ctx == NULL)
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);

	while ((sz = read(fd, data_buf, data_bufsize)) > 0) {
		EVP_DigestUpdate(md_ctx, data_buf, sz);
		calc_len += sz;
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RBYTES, sz);
	}
	io_error_check_errno("calc_digest");
	if (sz == -1)
		e = gfarm_errno_to_error(errno);

	md_len = gfarm_msgdigest_free(md_ctx, md_value);
	if (e == GFARM_ERR_NO_ERROR) {
		*md_strlenp =
		    gfarm_msgdigest_to_string(md_string, md_value, md_len);
		if (calc_lenp != NULL)
			*calc_lenp = calc_len;
	}

	return (e);
}

static int
is_not_modified(struct gfp_xdr *client, gfarm_int32_t fd, const char *diag)
{
	struct stat st;
	struct file_entry *fe;
	int cksum_flags, ret = 1;
	char *cksum_type = NULL, tmp_cksum[GFM_PROTO_CKSUM_MAXLEN];
	size_t cksum_len;
	gfarm_error_t e;

	if ((fe = file_table_entry(fd)) == NULL)
		gflog_error(GFARM_MSG_1003774, "fd %d: %s", fd,
		    gfarm_error_string(GFARM_ERR_BAD_FILE_DESCRIPTOR));
	else if (fstat(fe->local_fd, &st) == -1)
		gflog_notice(GFARM_MSG_1003775, "is_not_modified: %s",
		    strerror(errno));
	else if (st.st_size != fe->size || st.st_mtime != fe->mtime ||
	    gfarm_stat_mtime_nsec(&st) != fe->mtimensec)
		return (0);
	else if ((e = gfm_client_compound_put_fd_request(fd, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003776,
		    "%s: compound_put_fd_request: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_cksum_get_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003777, "%s cksum_get request: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_compound_put_fd_result(client, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_put_fd_problem(GFARM_MSG_1003778, client, e,
		    "%s: compound_put_fd_result: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_cksum_get_result(gfm_server, &cksum_type,
	     sizeof tmp_cksum, &cksum_len, tmp_cksum, &cksum_flags))
	    != GFARM_ERR_NO_ERROR)
		gflog_info(GFARM_MSG_1003779, "%s cksum_get result: %s",
		    diag, gfarm_error_string(e));
	else if ((e = gfm_client_compound_end(diag)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003780, "%s: compound_end: %s",
		    diag, gfarm_error_string(e));
	else if ((cksum_flags & (GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED|
	    GFM_PROTO_CKSUM_GET_EXPIRED)) != 0 || cksum_len == 0)
		ret = 0;
	free(cksum_type);
	return (ret);
}

static gfarm_error_t
digest_finish(struct gfp_xdr *client, gfarm_int32_t fd, const char *diag)
{
	struct file_entry *fe;
	char md_string[GFARM_MSGDIGEST_STRSIZE];
	size_t md_strlen;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if ((fe = file_table_entry(fd)) == NULL)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	if ((fe->flags & FILE_FLAG_DIGEST_FINISH) != 0)
		return (e);

	md_strlen = gfarm_msgdigest_to_string_and_free(fe->md_ctx, md_string);
	fe->flags |= FILE_FLAG_DIGEST_FINISH;

	if ((fe->flags & FILE_FLAG_WRITTEN) != 0 ||
	    (fe->flags & FILE_FLAG_DIGEST_AVAIL) == 0) {
		memcpy(fe->md_string, md_string, md_strlen);
		fe->md_strlen = md_strlen;
	} else if (memcmp(md_string, fe->md_string, fe->md_strlen) != 0 &&
	    is_not_modified(client, fd, diag)) {
		e = GFARM_ERR_CHECKSUM_MISMATCH;
		gflog_error(GFARM_MSG_1003781, "%lld:%lld: %s",
		    (long long)fe->ino, (long long)fe->gen,
		    gfarm_error_string(e));
		fe->flags &= ~FILE_FLAG_DIGEST_CALC; /* invalidate */
		if (gfarm_spool_digest_error_check)
			fe->flags |= FILE_FLAG_DIGEST_ERROR;
	}
	return (e);
}

static gfarm_error_t
update_file_entry_for_close(struct gfp_xdr *client,
	gfarm_int32_t fd, gfarm_int32_t close_flags, const char *diag)
{
	struct stat st;
	unsigned long atimensec, mtimensec;
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e2;
	struct file_entry *fe;

	if ((fe = file_table_entry(fd)) == NULL)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);

	if ((close_flags & GFS_PROTO_CLOSE_FLAG_MODIFIED) != 0) {
		fe->flags |= FILE_FLAG_WRITTEN;
		fe->flags &= ~FILE_FLAG_DIGEST_CALC;
	}

	if (fstat(fe->local_fd, &st) == -1) {
		e = gfarm_errno_to_error(errno);
		gflog_warning(GFARM_MSG_1000484,
		    "fd %d: stat failed at close: %s",
		    fd, strerror(errno));
	} else {
		atimensec = gfarm_stat_atime_nsec(&st);
		if (st.st_atime != fe->atime || atimensec != fe->atimensec)
			file_entry_set_atime(fe, st.st_atime, atimensec);
		/* another process might write this file */
		if ((fe->flags & FILE_FLAG_WRITABLE) != 0 ||
		    (fe->flags & FILE_FLAG_WRITTEN) != 0) {
			mtimensec = gfarm_stat_mtime_nsec(&st);
			if (st.st_mtime != fe->mtime ||
			    mtimensec != fe->mtimensec)
				file_entry_set_mtime(fe,
				    st.st_mtime, mtimensec);
			if (st.st_size != fe->size)
				file_entry_set_size(fe, st.st_size);
			/* NOTE: this may be caused by others */
		}
	}
	if ((fe->flags & (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
	    FILE_FLAG_DIGEST_CALC && fe->md_offset == fe->size) {
		e2 = digest_finish(client, fd, diag);
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
	}
	return (e);
}

static void
copy_to_lost_found(struct file_entry *fe)
{
	if (register_to_lost_found(1,
	    fe->local_fd_rdonly != -1 ? fe->local_fd_rdonly : fe->local_fd,
	    fe->ino, fe->gen) == GFARM_ERR_NO_ERROR)
		gflog_notice(GFARM_MSG_1004193, "lost file due to write "
		    "conflict is moved to /lost+found/%016llX%016llX-%s",
		    (unsigned long long)fe->ino, (unsigned long long)fe->gen,
		    canonical_self_name);
}

void
replica_lost_move_to_lost_found(gfarm_ino_t ino, gfarm_uint64_t gen,
	int local_fd, off_t size)
{
	gfarm_error_t e;
	char *path;
	static const char diag[] = "replica_lost_move_to_lost_found";

	for (;;) {
		e = gfm_client_replica_lost(ino, gen);
		if (!IS_CONNECTION_ERROR(e))
			break;
		free_gfm_server();
		if ((e = connect_gfm_server(diag)) != GFARM_ERR_NO_ERROR)
			fatal(GFARM_MSG_1004386, "die");
	}
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		gflog_notice(GFARM_MSG_1004481,
		    "%lld:%lld: possible race to move lost+found",
		    (long long)ino, (long long)gen);
		return;
	} else if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1004217,
		    "%lld:%lld: corrupted replica remains: %s",
		    (long long)ino, (long long)gen,
		    gfarm_error_string(e));
		return;
	}

	if (size == 0) {
		gfsd_local_path(ino, gen, diag, &path);
		if (unlink(path) == -1)
			gflog_error_errno(GFARM_MSG_1004214,
			    "unlink(%s)", path);
		else
			gflog_notice(GFARM_MSG_1004215,
			    "%lld:%lld: corrupted file removed",
			    (long long)ino, (long long)gen);
		free(path);
		return;
	}
	if (register_to_lost_found(0, local_fd, ino, gen)
	    == GFARM_ERR_NO_ERROR)
		gflog_notice(GFARM_MSG_1004216, "%lld:%lld: corrupted file "
		    "moved to /lost+found/%016llX%016llX-%s",
		    (long long)ino, (long long)gen,
		    (unsigned long long)ino, (unsigned long long)gen,
		    canonical_self_name);
}

gfarm_error_t
close_fd(struct gfp_xdr *client, gfarm_int32_t fd, struct file_entry *fe,
	const char *diag)
{
	gfarm_error_t e, e2;
	gfarm_int32_t gen_update_result = -1;

	if ((e = gfm_client_compound_put_fd_request(fd, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003337,
		    "%s compound_put_fd_request: %s",
		    diag, gfarm_error_string(e));
	else if ((fe->flags & (FILE_FLAG_DIGEST_FINISH|FILE_FLAG_DIGEST_AVAIL|
	    FILE_FLAG_WRITTEN)) == FILE_FLAG_DIGEST_FINISH &&
	    (e = gfm_client_cksum_set_request(gfm_server,
	    fe->md_type_name, fe->md_strlen, fe->md_string, 0, 0, 0))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003782, "%s cksum_set request: %s",
		    diag, gfarm_error_string(e));
	else if ((e = close_request(fe)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000488,
		    "%s close request: %s", diag, gfarm_error_string(e));
	else if ((e = gfm_client_compound_put_fd_result(client, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_put_fd_problem(GFARM_MSG_1003338, client, e,
		    "%s compound_put_fd_result: %s",
		    diag, gfarm_error_string(e));
	else if ((fe->flags & (FILE_FLAG_DIGEST_FINISH|FILE_FLAG_DIGEST_AVAIL|
	    FILE_FLAG_WRITTEN)) == FILE_FLAG_DIGEST_FINISH &&
	    (e = gfm_client_cksum_set_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_info(GFARM_MSG_1003783, "%s cksum_set result: %s",
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
			if (e == GFARM_ERR_READ_ONLY_FILE_SYSTEM) {
				/* will retry */
			} else if (fe->new_gen != fe->gen)
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
		else if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH) &&
		    fe->new_gen == fe->gen + 1 &&
		    (e2 = gfm_client_cksum_set_request(gfm_server,
		    fe->md_type_name, fe->md_strlen, fe->md_string,
		    0, 0, 0)) != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003784,
			    "%s cksum_set request: %s",
			    diag, gfarm_error_string(e2));
		else if ((e2 = gfm_client_generation_updated_v2_8_request(
		    gfm_server, gen_update_result, fe->size,
		    fe->atime, fe->atimensec, fe->mtime, fe->mtimensec))
		    != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1002301,
			    "%s generation_updated request: %s",
			    diag, gfarm_error_string(e2));
		else if ((e2 = gfm_client_compound_put_fd_result(client, diag))
		    != GFARM_ERR_NO_ERROR)
			gflog_put_fd_problem(GFARM_MSG_1003341, client, e2,
			    "%s compound_put_fd_result: %s",
			    diag, gfarm_error_string(e2));
		else if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH) &&
		    fe->new_gen == fe->gen + 1 &&
		    (e2 = gfm_client_cksum_set_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003785,
			    "%s cksum_set result: %s",
			    diag, gfarm_error_string(e2));
		else if ((e2 = gfm_client_generation_updated_v2_8_result(
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
		if (gen_update_result == GFARM_ERR_CONFLICT_DETECTED)
			copy_to_lost_found(fe);
	} else if ((fe->flags & (FILE_FLAG_DIGEST_FINISH|FILE_FLAG_DIGEST_AVAIL
	    |FILE_FLAG_WRITTEN)) == FILE_FLAG_DIGEST_FINISH) {
		gflog_notice(GFARM_MSG_1004387,
		    "inode %lld:%lld: checksum set to <%s>:<%.*s> by read",
		    (long long)fe->ino, (long long)fe->gen,
		    fe->md_type_name, (int)fe->md_strlen, fe->md_string);
	}
	return (e);
}

gfarm_error_t
fhclose_fd(struct gfp_xdr *client, struct file_entry *fe, const char *diag)
{
	gfarm_error_t e, e2;
	gfarm_uint64_t cookie;
	gfarm_int32_t gen_update_result = -1;

	if ((e = fhclose_request(fe))!= GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003344,
		   "%s: fhclose request: %s", diag, gfarm_error_string(e));
	else if ((e = fhclose_result(fe, &cookie, &gen_update_result))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003345,
		    "%s: fhclose result: %s", diag, gfarm_error_string(e));

	if (e != GFARM_ERR_NO_ERROR) {
		if (fe->flags & FILE_FLAG_WRITTEN) {
			if (e == GFARM_ERR_READ_ONLY_FILE_SYSTEM) {
				/* will retry */
			} else if (fe->new_gen != fe->gen)
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
			if (!IS_CONNECTION_ERROR(e)) {
				/* e.g. GFARM_ERR_STALE_FILE_HANDLE */
				copy_to_lost_found(fe);
			}
		}
	} else if (gen_update_result != -1) {
		if ((e2 = gfm_client_generation_updated_by_cookie_v2_8_request(
		    gfm_server, cookie, gen_update_result, fe->size,
		    fe->atime, fe->atimensec, fe->mtime, fe->mtimensec))
		    != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003346,
			    "%s: generation_updated_by_cookie request: %s",
			    diag, gfarm_error_string(e2));
		else if (
		    (e2 = gfm_client_generation_updated_by_cookie_v2_8_result(
		    gfm_server)) != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003347,
			    "%s: generation_updated_by_cookie result: %s",
			    diag, gfarm_error_string(e2));
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
		if (gen_update_result == GFARM_ERR_CONFLICT_DETECTED)
			copy_to_lost_found(fe);
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
close_fd_somehow(struct gfp_xdr *client,
	gfarm_int32_t fd, gfarm_int32_t close_flags, const char *diag)
{
	int failedover = 0;
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e2, e3;
	struct file_entry *fe;

	if ((fe = file_table_entry(fd)) == NULL) {
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
		gflog_debug(GFARM_MSG_1002174,
		    "bad file descriptor");
		return (e);
	}

	e3 = update_file_entry_for_close(client, fd, close_flags, diag);

	if (gfm_server == NULL) {
		if (fe->flags & FILE_FLAG_WRITTEN) {
			gflog_error(GFARM_MSG_1004497,
			    "inode %lld generation %lld (%lld): "
			    "error occurred during close operation "
			    "for writing: gfmd is down",
			    (long long)fe->ino, (long long)fe->gen,
			    (long long)fe->new_gen);
		}
	} else {

		if (fd_usable_to_gfmd) {
			while ((e = close_fd(client, fd, fe, diag)) ==
			    GFARM_ERR_READ_ONLY_FILE_SYSTEM) {
				gflog_info(GFARM_MSG_1005225,
				    "close_write: inode %llu:%llu waiting "
				    "for %d seconds until read_only disabled",
				    (long long)fe->ino, (long long)fe->gen,
				    gfarm_spool_server_read_only_retry_interval
				    );
				gfarm_sleep(
				    gfarm_spool_server_read_only_retry_interval
				    );
			}
			if (IS_CONNECTION_ERROR(e)) {
				/* fd_usable_to_gfmd will be set to 0 */
				reconnect_gfm_server_for_failover(
				    "close_fd_somehow/close_fd");
				failedover = 1;
			} else if (e != GFARM_ERR_NO_ERROR) {
				gflog_put_fd_problem(GFARM_MSG_1004134,
				    client, e, "close_fd: %s",
				    gfarm_error_string(e));
			}
		}

		if (!fd_usable_to_gfmd) {
			while ((e = fhclose_fd(client, fe, diag)) ==
			    GFARM_ERR_READ_ONLY_FILE_SYSTEM) {
				gflog_info(GFARM_MSG_1005226,
				    "fhclose_write: inode %llu:%llu waiting "
				    "for %d seconds until read_only disabled",
				    (long long)fe->ino, (long long)fe->gen,
				    gfarm_spool_server_read_only_retry_interval
				    );
				gfarm_sleep(
				    gfarm_spool_server_read_only_retry_interval
				    );
			}
			if (IS_CONNECTION_ERROR(e)) {
				reconnect_gfm_server_for_failover(
				    "close_fd_somehow/fhclose_fd");
				if ((e = fhclose_fd(client, fe, diag))
				    != GFARM_ERR_NO_ERROR) {
					gflog_error(GFARM_MSG_1004135,
					    "fhclose_fd: %s",
					    gfarm_error_string(e));
				} else
					failedover = 1;
			} else if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1004136,
				    "fhclose_fd: %s", gfarm_error_string(e));
			}
		}
	}
	if (fe->flags & FILE_FLAG_DIGEST_ERROR) {
		replica_lost_move_to_lost_found(
		    fe->ino, fe->gen, fe->local_fd, fe->size);
	}
	e2 = file_table_close(fd);
	if (e2 == GFARM_ERR_NO_ERROR)
		e2 = e3;
	e = failedover ? GFARM_ERR_GFMD_FAILED_OVER :
	    (e == GFARM_ERR_NO_ERROR ? e2 : e);

	return (e);
}

static void
close_fd_adapter(struct gfp_xdr *client, void *closure, gfarm_int32_t fd)
{
	close_fd_somehow(client, fd, 0, closure);
}

static void
close_all_fd(struct gfp_xdr *client)
{
	file_table_for_each(close_fd_adapter,
	    client, "closing all descriptor");
}

static void
close_fd_adapter_for_process_reset(struct gfp_xdr *client,
	void *closure, gfarm_int32_t fd)
{
	int *failedoverp = closure;
	gfarm_error_t e = close_fd_somehow(client, fd, 0,
	    "close_all_fd_for_process_reset");

	if (e == GFARM_ERR_GFMD_FAILED_OVER)
		*failedoverp = 1;
	else if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004137,
		    "close_fd_somehow: fd=%d: %s", fd, gfarm_error_string(e));
}

static int
close_all_fd_for_process_reset(struct gfp_xdr *client)
{
	int failedover = 0;

	file_table_for_each(close_fd_adapter_for_process_reset,
	    client, &failedover);
	return (failedover);
}

void
gfs_server_close(struct gfp_xdr *client)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	static const char diag[] = "GFS_PROTO_CLOSE";

	gfs_server_get_request(client, diag, "i", &fd);
	e = close_fd_somehow(client, fd, 0, diag);
	gfs_server_put_reply(client, diag, e, "");
}

void
gfs_server_close_write(struct gfp_xdr *client)
{
	gfarm_error_t e;
	gfarm_int32_t fd, flags;
	static const char diag[] = "GFS_PROTO_CLOSE_WRITE";

	gfs_server_get_request(client, diag, "ii", &fd, &flags);
	e = close_fd_somehow(client, fd, flags, diag);
	gfs_server_put_reply(client, diag, e, "");
}

void
gfs_server_pread(struct gfp_xdr *client)
{
	gfarm_int32_t fd, size;
	gfarm_int64_t offset;
	ssize_t rv = 0;
	unsigned char buffer[GFS_PROTO_MAX_IOSIZE];
	struct file_entry *fe;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_timerval_t t1, t2;
	static const char diag[] = "GFS_PROTO_PREAD";

	gfs_server_get_request(client, diag, "iil", &fd, &size, &offset);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}

	if ((fe = file_table_entry(fd)) == NULL) {
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
		goto reply;
	}
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	/* We truncatef i/o size bigger than GFS_PROTO_MAX_IOSIZE. */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	if ((rv = pread(fe->local_fd, buffer, size, offset)) == -1) {
		io_error_check_errno(diag);
		e = gfarm_errno_to_error(errno);
	} else {
		file_table_set_read(fd);
		/* update checksum */
		if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    FILE_FLAG_DIGEST_CALC) {
			if (fe->md_offset == offset) {
				EVP_DigestUpdate(fe->md_ctx, buffer, rv);
				fe->md_offset += rv;
				if (fe->md_offset == fe->size &&
				    (fe->flags & FILE_FLAG_WRITTEN) == 0)
					e = digest_finish(client, fd, diag);
			} else
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		}
	}
	if (rv > 0) {
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RBYTES, rv);
	}
	gfs_profile(
		gfarm_gettimerval(&t2);
		fe->nread++;
		fe->read_size += rv;
		fe->read_time += gfarm_timerval_sub(&t2, &t1));
reply:
	gfs_server_put_reply(client, diag, e, "b", rv, buffer);
}

/*
 * FUSE client does not report close error.  log write error since it
 * is called during the close process.
 */
static void
log_write_error(struct file_entry *fe, int eno, const char *diag)
{
	gflog_warning(GFARM_MSG_1005072, "%s (%llu:%llu): %s", diag,
		(unsigned long long)fe->ino, (unsigned long long)fe->gen,
		strerror(eno));
}

void
gfs_server_pwrite(struct gfp_xdr *client)
{
	gfarm_int32_t fd, localfd;
	size_t size;
	gfarm_int64_t offset;
	ssize_t rv = 0;
	int save_errno = 0;
	unsigned char buffer[GFS_PROTO_MAX_IOSIZE];
	struct file_entry *fe;
	gfarm_timerval_t t1, t2;
	static const char diag[] = "GFS_PROTO_PWRITE";

	gfs_server_get_request(client, diag, "ibl",
	    &fd, sizeof(buffer), &size, buffer, &offset);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}

	if ((fe = file_table_entry(fd)) == NULL) {
		save_errno = EBADF;
		goto reply;
	}
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));
	/*
	 * We truncate i/o size bigger than GFS_PROTO_MAX_IOSIZE.
	 * This is inefficient because passed extra data are just
	 * abandoned. So client should avoid such situation.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	localfd = file_table_get(fd);
	if (fe->local_flags & O_APPEND) {
		if ((rv = write(localfd, buffer, size)) != -1)
			offset = lseek(localfd, 0, SEEK_CUR) - rv;
	} else
		rv = pwrite(localfd, buffer, size, offset);
	if (rv == -1) {
		io_error_check_errno(diag);
		save_errno = errno;
		log_write_error(fe, save_errno, diag);
	} else {
		file_table_set_written(fd);
		/* update checksum */
		if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    FILE_FLAG_DIGEST_CALC) {
			if (fe->md_offset == offset) {
				EVP_DigestUpdate(fe->md_ctx, buffer, rv);
				fe->md_offset += rv;
			} else
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		} else if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH))
			fe->flags &= ~FILE_FLAG_DIGEST_CALC;
	}
	if (rv > 0) {
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WBYTES, rv);
	}
	gfs_profile(
		gfarm_gettimerval(&t2);
		fe->nwrite++;
		fe->write_size += rv;
		fe->write_time += gfarm_timerval_sub(&t2, &t1));
reply:
	gfs_server_put_reply_with_errno(client, diag, save_errno,
	    "i", (gfarm_int32_t)rv);
}

void
gfs_server_write(struct gfp_xdr *client)
{
	gfarm_int32_t fd, localfd;
	size_t size;
	ssize_t rv;
	gfarm_int64_t written_offset, total_file_size;
	int save_errno = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct file_entry *fe;
	gfarm_timerval_t t1, t2;
	static const char diag[] = "GFS_PROTO_WRITE";

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	written_offset = total_file_size = 0;
#endif
	gfs_server_get_request(client, diag, "ib",
	    &fd, sizeof(buffer), &size, buffer);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}
	if ((fe = file_table_entry(fd)) == NULL) {
		gfs_server_put_reply(client, diag,
		    GFARM_ERR_BAD_FILE_DESCRIPTOR, "");
		return;
	}

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));
	/*
	 * We truncate i/o size bigger than GFS_PROTO_MAX_IOSIZE.
	 * This is inefficient because passed extra data are just
	 * abandoned. So client should avoid such situation.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	localfd = file_table_get(fd);
	(void) lseek(localfd, 0, SEEK_END);
	if ((rv = write(localfd, buffer, size)) == -1) {
		io_error_check_errno(diag);
		save_errno = errno;
		log_write_error(fe, save_errno, diag);
	} else {
		written_offset = lseek(localfd, 0, SEEK_CUR) - rv;
		total_file_size = lseek(localfd, 0, SEEK_END);
		file_table_set_written(fd);
		/* update checksum */
		if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    FILE_FLAG_DIGEST_CALC) {
			if (fe->md_offset == written_offset) {
				EVP_DigestUpdate(fe->md_ctx, buffer, rv);
				fe->md_offset += rv;
			} else
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		} else if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH))
			fe->flags &= ~FILE_FLAG_DIGEST_CALC;
	}
	if (rv > 0) {
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WBYTES, rv);
	}
	gfs_profile(
		gfarm_gettimerval(&t2);
		fe->nwrite++;
		fe->write_size += rv;
		fe->write_time += gfarm_timerval_sub(&t2, &t1));

	gfs_server_put_reply_with_errno(client, diag, save_errno,
	    "ill", (gfarm_int32_t)rv, written_offset, total_file_size);
}

void
gfs_server_bulkread(struct gfp_xdr *client)
{
	gfarm_error_t e, e2;
	gfarm_int32_t src_err = GFARM_ERR_NO_ERROR;
	gfarm_int32_t fd;
	gfarm_int64_t len, offset, sent;
	struct file_entry *fe;
	EVP_MD_CTX *md_ctx;
	gfarm_timerval_t t1, t2;
	static const char diag[] = "GFS_PROTO_BULKREAD";

	gfs_server_get_request(client, diag, "ill", &fd, &len, &offset);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}
	if ((fe = file_table_entry(fd)) == NULL)
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
	else if (len < -1 || offset < 0)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else
		e = GFARM_ERR_NO_ERROR;
	e2 = gfp_xdr_send(client, "i", (gfarm_int32_t)e);
	if (e2 != GFARM_ERR_NO_ERROR) {
		conn_fatal(GFARM_MSG_1004138, "%s: put reply: %s",
		    diag, gfarm_error_string(e2));
	}
	if (e == GFARM_ERR_NO_ERROR) {
		GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
		gfs_profile(gfarm_gettimerval(&t1));

		/* update checksum? */
		if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) !=
		    FILE_FLAG_DIGEST_CALC) {
			md_ctx = NULL;
		} else if (fe->md_offset != offset) {
			md_ctx = NULL;
			fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		} else {
			md_ctx = fe->md_ctx;
		}

		e = gfs_sendfile_common(client, &src_err,
		    fe->local_fd, offset, len, md_ctx, &sent);
		io_error_check(src_err, diag);
		if (IS_CONNECTION_ERROR(e))
			conn_fatal(GFARM_MSG_1004139, "%s sendfile: %s",
			    diag, gfarm_error_string(e));
		if (md_ctx != NULL) {
			/* `sent' is set even if an error happens */
			fe->md_offset += sent;
			if (e != GFARM_ERR_NO_ERROR) {
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
			} else {
				if (fe->md_offset == fe->size &&
				    (fe->flags & FILE_FLAG_WRITTEN) == 0)
					e = digest_finish(client, fd, diag);
			}
		}
		if (sent > 0)
			file_table_set_read(fe->local_fd);

		gfs_profile(
			gfarm_gettimerval(&t2);
			fe->nread++;
			fe->read_size += sent;
			fe->read_time += gfarm_timerval_sub(&t2, &t1);
		);

		gfs_server_put_reply(client, diag,
		    e != GFARM_ERR_NO_ERROR ? e : src_err, "");
	} else if (debug_mode) {
		gflog_debug(GFARM_MSG_1004140, "reply: %s: %d (%s)",
		    diag, (int)e, gfarm_error_string(e));
	}
}

void
gfs_server_bulkwrite(struct gfp_xdr *client)
{
	gfarm_error_t e, e2;
	gfarm_int32_t dst_err = GFARM_ERR_NO_ERROR;
	gfarm_int32_t fd;
	gfarm_int64_t offset;
	gfarm_off_t written = 0;
	struct file_entry *fe;
	EVP_MD_CTX *md_ctx;
	int md_aborted;
	gfarm_timerval_t t1, t2;
	static const char diag[] = "GFS_PROTO_BULKWRITE";

	gfs_server_get_request(client, diag, "il", &fd, &offset);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}

	if ((fe = file_table_entry(fd)) == NULL)
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
	else if (offset < 0)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else
		e = GFARM_ERR_NO_ERROR;
	e2 = gfp_xdr_send(client, "i", (gfarm_int32_t)e);
	if (e2 == GFARM_ERR_NO_ERROR)
		e2 = gfp_xdr_flush(client);
	if (e2 != GFARM_ERR_NO_ERROR) {
		conn_fatal(GFARM_MSG_1004141, "%s: put reply: %s",
		    diag, gfarm_error_string(e2));
	}
	if (e == GFARM_ERR_NO_ERROR) {
		GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
		gfs_profile(gfarm_gettimerval(&t1));

		/* update checksum? */
		if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) !=
		    FILE_FLAG_DIGEST_CALC) {
			if ((fe->flags &
			    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
			    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH))
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
			md_ctx = NULL;
		} else if (fe->md_offset != offset) {
			md_ctx = NULL;
			fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		} else {
			md_ctx = fe->md_ctx;
		}

		e = gfs_recvfile_common(client, &dst_err, fe->local_fd, offset,
		    (fe->local_flags & O_APPEND) != 0, md_ctx, &md_aborted,
		    &written);
		io_error_check(dst_err, diag);
		if (IS_CONNECTION_ERROR(e))
			conn_fatal(GFARM_MSG_1004142, "%s recvfile: %s",
			    diag, gfarm_error_string(e));
		if (written > 0)
			file_table_set_written(fd);
		if (md_ctx != NULL) {
			/* `written' is set even if an error happens */
			fe->md_offset += written;
			if (e != GFARM_ERR_NO_ERROR || md_aborted)
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		}

		gfs_profile(
			gfarm_gettimerval(&t2);
			fe->nwrite++;
			fe->write_size += written;
			fe->write_time += gfarm_timerval_sub(&t2, &t1);
		);

		gfs_server_put_reply(client, diag,
		    e != GFARM_ERR_NO_ERROR ? e : dst_err, "l",
		    (gfarm_int64_t)written);
	} else if (debug_mode) {
		gflog_debug(GFARM_MSG_1004143, "reply: %s: %d (%s)",
		    diag, (int)e, gfarm_error_string(e));
	}
}

void
gfs_server_ftruncate(struct gfp_xdr *client)
{
	int fd;
	gfarm_int64_t length;
	struct file_entry *fe;
	int save_errno = 0;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	static const char diag[] = "GFS_PROTO_FTRUNCATE";

	gfs_server_get_request(client, diag, "il", &fd, &length);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}

	if ((fe = file_table_entry(fd)) == NULL)
		save_errno = EBADF;
	else if (ftruncate(file_table_get(fd), (off_t)length) == -1)
		save_errno = errno;
	else {
		file_table_set_written(fd);

		/* update checksum */
		if ((fe->flags & FILE_FLAG_DIGEST_CALC) != 0) {
			if (length == 0) {
				if ((fe->flags & FILE_FLAG_DIGEST_FINISH)
				    == 0) {
					/* to avoid memory leak*/
					gfarm_msgdigest_free(
					    fe->md_ctx, md_value);
					fe->flags |= FILE_FLAG_DIGEST_FINISH;
				}
				fe->md_ctx = gfsd_msgdigest_alloc(
				    fe->md_type_name, diag, fe->ino, fe->gen);
				if (fe->md_ctx == NULL) {
					free(fe->md_type_name);
					fe->md_type_name = NULL;
					fe->flags &= ~FILE_FLAG_DIGEST_CALC;
				} else {
					fe->flags &= ~FILE_FLAG_DIGEST_FINISH;
					fe->md_offset = 0;
				}
			} else if (length < fe->md_offset)
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		}
	}

	gfs_server_put_reply_with_errno(client, diag, save_errno, "");
}

void
gfs_server_fsync(struct gfp_xdr *client)
{
	int fd;
	int operation;
	int save_errno = 0;
	static const char diag[] = "GFS_PROTO_FSYNC";

	gfs_server_get_request(client, diag, "ii", &fd, &operation);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}

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

	gfs_server_put_reply_with_errno(client, diag, save_errno, "");
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
	static const char diag[] = "GFS_PROTO_FSTAT";

	gfs_server_get_request(client, diag, "i", &fd);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}

	if (fstat(file_table_get(fd), &st) == -1)
		save_errno = errno;
	else {
		size = st.st_size;
		atime_sec = st.st_atime;
		atime_nsec = gfarm_stat_atime_nsec(&st);
		mtime_sec = st.st_mtime;
		mtime_nsec = gfarm_stat_mtime_nsec(&st);
	}

	gfs_server_put_reply_with_errno(client, diag, save_errno,
	    "llili", size, atime_sec, atime_nsec, mtime_sec, mtime_nsec);
}

void
gfs_server_cksum(struct gfp_xdr *client)
{
	gfarm_int32_t fd;
	struct file_entry *fe;
	char *type = NULL, cksum[GFARM_MSGDIGEST_STRSIZE];
	size_t len = 0;
	gfarm_error_t e;
#define DATA_BUFSIZE	65536 /* small size is better, because of read-ahead */
	char data_buf[DATA_BUFSIZE];
	static const char diag[] = "GFS_PROTO_CKSUM";

	gfs_server_get_request(client, "cksum", "is", &fd, &type);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}

	if ((fe = file_table_entry(fd)) == NULL)
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
	else {
		e = calc_digest(file_table_get(fd), type, cksum, &len, NULL,
		    data_buf, sizeof(data_buf), diag, fe->ino, fe->gen);
	}
	free(type);
	gfs_server_put_reply(client, "cksum", e, "b", len, cksum);
}

void
gfsd_statfs_all(int ronly_behaves_disk_full, gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp,
	int *readonlyp)
{
	int i, err;
	gfarm_int32_t bsize, bsize_t = 0;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;
	gfarm_off_t blocks_t, bfree_t, bavail_t, files_t, ffree_t, favail_t;
	int readonly = 1, ronly;
	float brel;

	blocks_t = bfree_t = bavail_t = files_t = ffree_t = favail_t = 0;

	for (i = 0; i < gfarm_spool_root_num; ++i) {
		if (gfarm_spool_root[i] == NULL)
			break;
		err = gfsd_statfs_readonly(i, &bsize,
		    &blocks, &bfree, &bavail, &files, &ffree, &favail, &ronly);
		if (err)
			gflog_fatal_errno(GFARM_MSG_1004482, "statfs");
		if (ronly && ronly_behaves_disk_full) {
			/* pretend to be disk full to make gfsd read-only */
			bavail = bfree = 0;
		}
		if (i == 0)
			bsize_t = bsize;
		if (bsize_t == bsize) {
			blocks_t += blocks;
			bfree_t += bfree;
			bavail_t += bavail;
		} else {
			brel = (float)bsize_t / bsize;
			blocks_t += brel * blocks;
			bfree_t += brel * bfree;
			bavail_t += brel * bavail;
		}
		files_t += files;
		ffree_t += ffree;
		favail_t += favail;
		if (ronly == 0)
			readonly = 0;
	}
	*bsizep = bsize_t;
	*blocksp = blocks_t;
	*bfreep = bfree_t;
	*bavailp = bavail_t;
	*filesp = files_t;
	*ffreep = ffree_t;
	*favailp = favail_t;
	*readonlyp = readonly;
}

/* for client */
void
gfs_server_statfs(struct gfp_xdr *client)
{
	char *dir;
	int save_errno = 0;
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;
	int readonly;

	/*
	 * do not use dir since there is no way to know gfarm_spool_root.
	 * this code is kept for backward compatibility reason.
	 */
	gfs_server_get_request(client, "statfs", "s", &dir);
	free(dir);

	gfsd_statfs_all(1 /* readonly mode behaves disk-full */,
	    &bsize, &blocks, &bfree, &bavail,
	    &files, &ffree, &favail, &readonly);

	gfs_server_put_reply_with_errno(client, "statfs", save_errno,
	    "illllll", bsize, blocks, bfree, bavail, files, ffree, favail);
}

static gfarm_error_t
replica_adding(struct gfp_xdr *client, gfarm_int32_t net_fd, char *src_host,
	gfarm_ino_t *inop, gfarm_uint64_t *genp, gfarm_off_t *filesizep,
	char **cksum_typep, size_t cksum_size, size_t *cksum_lenp, char *cksum,
	gfarm_int32_t *cksum_request_flagsp,
	const char *request)
{
	gfarm_error_t e;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	gfarm_off_t filesize;
	char *cksum_type;
	size_t cksum_len;
	gfarm_int32_t cksum_request_flags;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDING_CKSUM";

	if ((e = gfm_client_compound_put_fd_request(net_fd, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004144,
		    "%s: compound_put_fd_request request=%s: %s",
		    diag, request, gfarm_error_string(e));
	else if ((e = gfm_client_replica_adding_cksum_request(gfm_server,
	    src_host)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004145,
		    "%s: gfm_client_replica_adding_request request=%s: %s",
		    diag, request, gfarm_error_string(e));
	else if ((e = gfm_client_compound_put_fd_result(client, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_put_fd_problem(GFARM_MSG_1003356, client, e,
		    "%s: compound_put_fd_result reqeust=%s: %s",
		    diag, request, gfarm_error_string(e));
	else if ((e = gfm_client_replica_adding_cksum_result(gfm_server,
	    &ino, &gen, &filesize,
	    &cksum_type, cksum_size, &cksum_len, cksum, &cksum_request_flags))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000510,
			    "%s result error on %s: %s", diag, request,
			    gfarm_error_string(e));
	} else if ((e = gfm_client_compound_end(diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004146,
		    "%s: compound_end request=%s: %s",
		    diag, request, gfarm_error_string(e));
		free(cksum_type);
	} else {
		*inop = ino;
		*genp = gen;
		*filesizep = filesize;
		*cksum_typep = cksum_type;
		*cksum_lenp = cksum_len;
		*cksum_request_flagsp = cksum_request_flags;
	}

	if (IS_CONNECTION_ERROR(e)) {
		reconnect_gfm_server_for_failover("replica_adding");
		e = GFARM_ERR_GFMD_FAILED_OVER;
	}
	return (e);
}

static gfarm_error_t
replica_added(struct gfp_xdr *client, gfarm_int32_t net_fd,
	gfarm_int32_t src_err, gfarm_int32_t dst_err, gfarm_int32_t flags,
	gfarm_int64_t filesize,
	char *cksum_type, size_t cksum_len, char *cksum,
	gfarm_int32_t cksum_result_flags, const char *request)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDED_CKSUM";

	if ((e = gfm_client_compound_put_fd_request(net_fd, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004147,
		    "%s: compound_put_fd_request request=%s: %s",
		    diag, request, gfarm_error_string(e));
	else if ((e = gfm_client_replica_added_cksum_request(gfm_server,
	    src_err, dst_err, flags, filesize, cksum_type, cksum_len, cksum,
	    cksum_result_flags)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004148,
		    "%s: gfm_client_replica_added2_request request=%s: %s",
		    diag, request, gfarm_error_string(e));
	else if ((e = gfm_client_compound_put_fd_result(client, diag))
	    != GFARM_ERR_NO_ERROR)
		gflog_put_fd_problem(GFARM_MSG_1003359, client, e,
		    "%s: compound_put_fd_result request=%s: %s",
		    diag, request, gfarm_error_string(e));
	else if ((e = gfm_client_replica_added_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000518,
			    "%s result on %s: %s", diag, request,
			    gfarm_error_string(e));
	} else if ((e = gfm_client_compound_end(diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004149,
		    "%s: compound_end request=%s: %s",
		    diag, request, gfarm_error_string(e));
	}

	if (IS_CONNECTION_ERROR(e)) {
		reconnect_gfm_server_for_failover("replica_added");
		e = GFARM_ERR_GFMD_FAILED_OVER;
	}
	return (e);
}

gfarm_error_t
replication_dst_cksum_verify(int issue_cksum_protocol,
	size_t req_cksum_len, const char *req_cksum,
	size_t src_cksum_len, const char *src_cksum,
	size_t dst_cksum_len, const char *dst_cksum,
	const char *diag, const char *issue_diag,
	gfarm_ino_t diag_ino, gfarm_uint64_t diag_gen,
	const char *src_hostname, int src_port)
{
	gfarm_error_t dst_err = GFARM_ERR_NO_ERROR;

	if (issue_cksum_protocol &&
	    src_cksum_len > 0 /* 0, if cksum_type is unsupported */ &&
	    (dst_cksum_len != src_cksum_len ||
	    memcmp(dst_cksum, src_cksum, src_cksum_len) != 0)) {
		/* network malfunction */
		gflog_error(GFARM_MSG_1004150,
		    "%s: %s %lld:%lld from %s:%d: "
		    "checksum mismatch during network transfer. "
		    "<%.*s> expected, but <%.*s>", diag, issue_diag,
		    (long long)diag_ino, (long long)diag_gen,
		   src_hostname, src_port,
		    (int)src_cksum_len, src_cksum,
		    (int)dst_cksum_len, dst_cksum);
		dst_err = GFARM_ERR_CHECKSUM_MISMATCH;
	}
	if (req_cksum_len > 0 &&
	    (dst_cksum_len != req_cksum_len ||
	    memcmp(dst_cksum, req_cksum, req_cksum_len) != 0)) {
		/* may not be critical.  modified after cksum set */
		gflog_info(GFARM_MSG_1004151,
		    "%s: %s %lld:%lld from %s:%d: checksum mismatch. "
		    "<%.*s> expected, but <%.*s>", diag, issue_diag,
		    (long long)diag_ino, (long long)diag_gen,
		    src_hostname, src_port,
		    (int)req_cksum_len, req_cksum,
		    (int)dst_cksum_len, dst_cksum);
		dst_err = GFARM_ERR_CHECKSUM_MISMATCH;
	}
	return (dst_err);
}

void
gfs_server_replica_add_from(struct gfp_xdr *client)
{
	gfarm_error_t e, e2;
	int save_errno;
	char *host, *path;
	struct gfs_connection *server;
	gfarm_int32_t net_fd, local_fd, port;

	gfarm_ino_t ino = 0;
	gfarm_uint64_t gen = 0;
	gfarm_off_t filesize = -1;
	char *cksum_type = NULL;
	size_t req_cksum_len = 0;
	char req_cksum[GFM_PROTO_CKSUM_MAXLEN];
	gfarm_int32_t cksum_request_flags = 0;
	int issue_cksum_protocol = 0;

	EVP_MD_CTX *md_ctx = NULL; /* non-NULL == calculate message-digest */
	size_t md_strlen = 0;
	char md_string[GFARM_MSGDIGEST_STRSIZE];

	gfarm_int32_t src_err = GFARM_ERR_NO_ERROR;
	gfarm_int32_t dst_err = GFARM_ERR_NO_ERROR;
	size_t src_cksum_len = 0;
	char src_cksum[GFM_PROTO_CKSUM_MAXLEN];
	gfarm_int32_t cksum_result_flags = 0;

	int flags = 0; /* XXX - for now */
	struct stat sb;
	static const char diag[] = "GFS_PROTO_REPLICA_ADD_FROM";
	const char *issue_diag;

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
		    "");
		return;
	}

	gfs_server_get_request(client, diag, "sii", &host, &port, &net_fd);

	e = replica_adding(client, net_fd, host, &ino, &gen, &filesize,
	    &cksum_type, sizeof req_cksum, &req_cksum_len, req_cksum,
	    &cksum_request_flags, diag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002176,
			"replica_adding() failed: %s",
			gfarm_error_string(e));
		goto free_host;
	}

	gfsd_local_path(ino, gen, diag, &path);
	local_fd = open_data(path, O_WRONLY|O_CREAT|O_TRUNC);
	save_errno = errno;
	free(path);
	if (local_fd == -1) {
		/* dst_err: invalidate */
		e = dst_err = gfarm_errno_to_error(save_errno);
		goto adding_cancel;
	}
	if (!confirm_local_path(ino, gen, diag)) {
		gflog_error(GFARM_MSG_1004498, "%s: %lld:%lld: race detected",
		    diag, (long long)ino, (long long)gen);
		/* dst_err: invalidate */
		e = dst_err = GFARM_ERR_INTERNAL_ERROR;
		goto close;
	}
	e = gfs_client_connection_acquire_by_host(gfm_server, host, port,
	    &server, listen_addrname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002177,
			"gfs_client_connection_acquire_by_host() failed: %s",
			gfarm_error_string(e));
		src_err = e; /* invalidate */
		goto close;
	}
	md_ctx = gfsd_msgdigest_alloc(cksum_type, diag, ino, gen);
	if ((cksum_request_flags &
	    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_SRC_SUPPORTS) != 0) {
		issue_cksum_protocol = 1;
		issue_diag = "GFS_PROTO_REPLICA_RECV_CKSUM";
		e = gfs_client_replica_recv_cksum_md(server,
		    &src_err, &dst_err, ino, gen, filesize,
		    cksum_type, req_cksum_len, req_cksum, cksum_request_flags,
		    sizeof(src_cksum), &src_cksum_len, src_cksum,
		    &cksum_result_flags,
		    local_fd, md_ctx);
	} else {
		issue_cksum_protocol = 0;
		issue_diag = "GFS_PROTO_REPLICA_RECV";
		e = gfs_client_replica_recv_md(server,
		    &src_err, &dst_err, ino, gen, local_fd, md_ctx);
	}

	if (md_ctx != NULL) {
		/*
		 * call EVP_DigestFinal() even if an error happens,
		 * otherwise memory leaks
		 */
		md_strlen = gfarm_msgdigest_to_string_and_free(
		    md_ctx, md_string);
	}

	if (e == GFARM_ERR_NO_ERROR)
		e = src_err != GFARM_ERR_NO_ERROR ? src_err : dst_err;
	else /* gfs_client_replica_recv*() may not change src_err/dst_err */
		dst_err = e; /* invalidate */
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002178,
			"gfs_client_replica_recv() failed: %s",
			gfarm_error_string(e));
		goto free_server;
	}
	if (md_ctx != NULL) {
		/* dst_err: invalidate */
		e = dst_err = replication_dst_cksum_verify(
		    issue_cksum_protocol,
		    req_cksum_len, req_cksum,
		    src_cksum_len, src_cksum,
		    md_strlen, md_string,
		    diag, issue_diag, ino, gen, host, port);
	}

	if (e == GFARM_ERR_NO_ERROR) {
		if (fsync(local_fd) == -1) {
			save_errno = errno;
			e = gfarm_errno_to_error(save_errno);
			if (dst_err == GFARM_ERR_NO_ERROR)
				dst_err = e; /* invalidate */
			gflog_error(GFARM_MSG_1005236,
			    "%s: %lld:%lld fsync(): %s", diag,
			    (unsigned long long)ino,
			    (unsigned long long)gen, strerror(save_errno));
		} else if (fstat(local_fd, &sb) == -1) {
			save_errno = errno;
			e = gfarm_errno_to_error(save_errno);
			if (dst_err == GFARM_ERR_NO_ERROR)
				dst_err = e; /* invalidate */
			gflog_error(GFARM_MSG_1005237,
			    "%s: %lld:%lld fstat(): %s", diag,
			    (unsigned long long)ino,
			    (unsigned long long)gen, strerror(save_errno));
		} else {
			filesize = sb.st_size;
			if (gfarm_write_verify)
				write_verify_request(
				    ino, gen, sb.st_mtime, diag);
		}
	}
 free_server:
	gfs_client_connection_free(server);
 close:
	close(local_fd);
 adding_cancel:
	e2 = replica_added(client, net_fd, src_err, dst_err, flags, filesize,
	    cksum_type != NULL ? cksum_type : "",
	    issue_cksum_protocol ? src_cksum_len : md_strlen,
	    issue_cksum_protocol ? src_cksum     : md_string,
	    cksum_result_flags, diag);
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;
 free_host:
	free(host);
	free(cksum_type);
	gfs_server_put_reply(client, diag, e, "");
	return;
}

int
replication_src_cksum_error(gfarm_ino_t ino, gfarm_uint64_t gen,
	int local_fd, off_t size)
{
	gfarm_error_t e;
	gfarm_uint64_t open_status;

	e = gfm_client_replica_open_status(gfm_server, ino, gen, &open_status);
	if (e != GFARM_ERR_NO_ERROR)
		return (0); /* generation is updated. i.e. file modified */

	if ((open_status & GFM_PROTO_REPLICA_OPENED_WRITE) != 0)
		return (0); /* file replica is being opened for write */

	replica_lost_move_to_lost_found(ino, gen, local_fd, size);
	return (1);
}

void
gfs_server_replica_recv(struct gfp_xdr *client,
	enum gfarm_auth_id_role peer_role, int cksum_protocol)
{
	gfarm_error_t e, error;
	gfarm_int32_t src_err;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	char *path;
	int local_fd = -1, rv;

	gfarm_int64_t filesize = 0;
	char *cksum_type = NULL;
	size_t cksum_len = 0;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	gfarm_int32_t cksum_request_flags;
	EVP_MD_CTX *md_ctx = NULL; /* non-NULL == calculate message-digest */
	size_t md_strlen = 0;
	char md_string[GFARM_MSGDIGEST_STRSIZE];
	gfarm_int32_t cksum_result_flags = 0;
	gfarm_off_t sent = 0;

	unsigned long long msl = 1, total_msl = 0; /* sleep millisec. */
	const char *diag = cksum_protocol ?
	    "GFS_PROTO_REPLICA_RECV_CKSUM" :
	    "GFS_PROTO_REPLICA_RECV";

	if (cksum_protocol)
		gfs_server_get_request(client, diag, "lllsbi", &ino, &gen,
		    &filesize,
		    &cksum_type, sizeof(cksum), &cksum_len, cksum,
		    &cksum_request_flags);
	else
		gfs_server_get_request(client, diag, "ll", &ino, &gen);
	/* from gfsd only */
	if (peer_role != GFARM_AUTH_ID_ROLE_SPOOL_HOST) {
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002179,
			"operation is not permitted(peer_role)");
		 /* send EOF */
		e = gfs_sendfile_common(client, &src_err, -1, 0, 0,
		    NULL, NULL);
		goto finish;
	}
	/*
	 * We don't have to check fd_usable_to_gfmd here,
	 * because this doesn't use file_table[] at all.
	 */

	gfsd_local_path(ino, gen, diag, &path);
	for (;;) {
		local_fd = open_data(path, O_RDONLY);
		if (local_fd >= 0)
			break; /* success */
		if (errno != ENOENT || total_msl >= 3000) { /* 3 sec. */
			error = gfarm_errno_to_error(errno);
			gflog_notice(GFARM_MSG_1003511,
			    "open_data(%lld:%lld): %s",
			    (long long) ino, (long long) gen,
			    gfarm_error_string(error));
			free(path);
			/* send EOF */
			e = gfs_sendfile_common(client, &src_err, -1, 0, 0,
			    NULL, NULL);
			goto finish;
		}
		/* ENOENT: wait generation-update, retry open_data() */
		gfarm_nanosleep(
		    (unsigned long long)msl * GFARM_MILLISEC_BY_NANOSEC);
		total_msl += msl;
		msl *= 2;
#if 0	/* too many logs */
		gflog_info(GFARM_MSG_1003512,
		    "retry open_data(%lld:%lld): sleep %lld msec.",
		    (long long) ino, (long long) gen, (long long) total_msl);
#endif
	}
	free(path);

	if (cksum_protocol) {
		struct stat st;

		errno = 0;
		if (fstat(local_fd, &st) == -1 || st.st_size != filesize) {
			int save_errno = errno;
			if (save_errno != 0) {
				error = gfarm_errno_to_error(save_errno);
			} else { /* st.st_size != filesize */
				error = GFARM_ERR_INVALID_FILE_REPLICA;
				(void)replication_src_cksum_error(
				    ino, gen, local_fd, st.st_size);
			}
			 /* send EOF */
			e = gfs_sendfile_common(client, &src_err, -1, 0, 0,
			    NULL, NULL);
			goto finish;
		}

		md_ctx = gfsd_msgdigest_alloc(cksum_type, diag, ino, gen);
		if (md_ctx == NULL) {
			/*
			 * do NOT return an error to caller,
			 * just make cksum_len == 0
			 */
		}
	}

	error = GFARM_ERR_NO_ERROR;
	/* data transfer */
	e = gfs_sendfile_common(client, &src_err, local_fd, 0, -1,
	    md_ctx, &sent);
	io_error_check(src_err, diag);

	/*
	 * call EVP_DigestFinal() even if an error happens,
	 * otherwise memory leaks
	 */
	if (md_ctx != NULL)
		md_strlen = gfarm_msgdigest_to_string_and_free(
		    md_ctx, md_string);

finish:
	if (IS_CONNECTION_ERROR(e))
		conn_fatal(GFARM_MSG_1005102, "%s sendfile: %s",
		    diag, gfarm_error_string(e));
	if (error == GFARM_ERR_NO_ERROR)
		error = src_err;
	if (cksum_protocol) {
		if (error == GFARM_ERR_NO_ERROR &&
		    md_ctx != NULL && cksum_len > 0 &&
		    (cksum_len != md_strlen ||
		     memcmp(cksum, md_string, md_strlen) != 0)) {
			(void)replication_src_cksum_error(
			    ino, gen, local_fd, sent);
			/* maybe no problem, if modified after cksum set */
			gflog_info(GFARM_MSG_1004152, "%s: %lld:%lld: "
			    "checksum mismatch. <%.*s> expected, "
			    "but <%.*s>",
			    diag, (long long)ino, (long long)gen,
			    (int)cksum_len, cksum,
			    (int)md_strlen, md_string);
#if 1 /* change this value to 0 at a test of cksum validation on receiver */
			error = GFARM_ERR_CHECKSUM_MISMATCH;
#endif
		}
		if (local_fd >= 0) {
			rv = close(local_fd);
			if (rv == -1 && error == GFARM_ERR_NO_ERROR)
				error = gfarm_errno_to_error(errno);
		}
		gfs_server_put_reply(client, diag, error, "bi",
		    md_strlen, md_string, cksum_result_flags);
	} else {
		if (local_fd >= 0) {
			rv = close(local_fd);
			if (rv == -1 && error == GFARM_ERR_NO_ERROR)
				error = gfarm_errno_to_error(errno);
		}
		gfs_server_put_reply(client, diag, error, "");
	}
	free(cksum_type);
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

/* a * b / 1024 */
static gfarm_off_t
multiply_and_divide_by_1024(gfarm_off_t a, gfarm_off_t b)
{
	/* assume b is a power of 2 */
	if (b >= 1024)
		return (a * (b / 1024));
	return (a / (1024 / b));
}

static gfarm_error_t
gfs_async_server_status(struct gfp_xdr *conn, gfp_xdr_xid_t xid, size_t size,
	int use_host_info_flags)
{
	gfarm_error_t e;
	int save_errno = 0, readonly, host_info_flags;
	double loadavg[3];
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;
	gfarm_off_t used = 0, avail = 0;
	static const char diag[] = "gfs_server_status";

	if (use_host_info_flags) {
		e = gfs_async_server_get_request(conn, size, "status", "i",
		    &host_info_flags);
		gfsd_readonly_config_update(host_info_flags);
	} else {
		/* just check that size == 0 */
		e = gfs_async_server_get_request(conn, size, "status", "");
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (getloadavg(loadavg, GFARM_ARRAY_LENGTH(loadavg)) == -1) {
		save_errno = EPERM; /* XXX */
		gflog_warning(GFARM_MSG_1000520,
		    "%s: cannot get load average", diag);
	} else {
		/* use real disk space even if readonly mode is enabled */
		gfsd_statfs_all(0, &bsize, &blocks, &bfree, &bavail,
		    &files, &ffree, &favail, &readonly);
		used = multiply_and_divide_by_1024(blocks - bfree, bsize);
		avail = multiply_and_divide_by_1024(bavail, bsize);
	}
	/* add base load */
	loadavg[0] += gfarm_spool_base_load;
	loadavg[1] += gfarm_spool_base_load;
	loadavg[2] += gfarm_spool_base_load;

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

	/* this gfsd got GFS_PROTO_REPLICATION_CKSUM_REQUEST from gfmd */
	int handling_cksum_protocol;

	/* issue GFS_PROTO_REPLICA_RECV_CKSUM to the src gfsd */
	int issue_cksum_protocol;

	/*
	 * parent gfsd receives cksum from child gfsd,
	 * and replies it to gfmd, and gfmd sets the cksum to this inode.
	 */
	int reply_cksum; /* currently this flag is not actually used */

	gfp_xdr_xid_t xid;
	gfarm_ino_t ino;
	gfarm_int64_t gen;

	/* only used in case of GFS_PROTO_REPLICATION_CKSUM_REQUEST */
	gfarm_uint64_t filesize;
	char *cksum_type;
	size_t cksum_len;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	gfarm_uint32_t cksum_request_flags;

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

union replication_results {
	struct replica_recv_results {
		struct replication_errcodes e;
	} recv;
	struct replica_recv_cksum_results {
		struct replication_errcodes e;
		size_t cksum_len;
		char cksum[GFM_PROTO_CKSUM_MAXLEN];
		gfarm_int32_t cksum_result_flags;
	} recv_cksum;
};

/* error codes are returned by *res */
static void
replica_receive(struct gfarm_hash_entry *q, struct replication_request *rep,
	struct gfs_connection *src_gfsd, int local_fd,
	union replication_results *res,	const char *diag)
{
	gfarm_int32_t conn_err;
	gfarm_int32_t src_err = GFARM_ERR_NO_ERROR;
	gfarm_int32_t dst_err = GFARM_ERR_NO_ERROR;
	int rv, save_errno;

	EVP_MD_CTX *md_ctx = NULL; /* non-NULL == calculate message-digest */
	size_t md_strlen = 0;
	char md_string[GFARM_MSGDIGEST_STRSIZE];
	size_t src_cksum_len = 0;
	char src_cksum[GFM_PROTO_CKSUM_MAXLEN];
	gfarm_int32_t cksum_result_flags = 0;

	const char *issue_diag = rep->issue_cksum_protocol ?
	    "GFS_PROTO_REPLICA_RECV_CKSUM" :
	    "GFS_PROTO_REPLICA_RECV";

	if (rep->handling_cksum_protocol) {
		md_ctx = gfsd_msgdigest_alloc(
		    rep->cksum_type, diag, rep->ino, rep->gen);
		if (md_ctx == NULL) {
			/* do NOT return an error to caller here */
		}
	}
	if (rep->issue_cksum_protocol) {
		conn_err = gfs_client_replica_recv_cksum_md(src_gfsd,
		    &src_err, &dst_err,
		    rep->ino, rep->gen, rep->filesize,
		    rep->cksum_type, rep->cksum_len, rep->cksum,
		    rep->cksum_request_flags,
		    sizeof(src_cksum), &src_cksum_len, src_cksum,
		    &cksum_result_flags,
		    local_fd, md_ctx);
	} else {
		conn_err = gfs_client_replica_recv_md(src_gfsd,
		    &src_err, &dst_err,
		    rep->ino, rep->gen, local_fd, md_ctx);
	}

	if (md_ctx != NULL) {
		/*
		 * call EVP_DigestFinal() even if an error happens,
		 * otherwise memory leaks
		 */
		md_strlen = gfarm_msgdigest_to_string_and_free(
		    md_ctx, md_string);
	}

	if (conn_err != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1004234,
		    "%s: %s %lld:%lld from %s:%d: %s", diag, issue_diag,
		    (long long)rep->ino, (long long)rep->gen,
		    gfp_conn_hash_hostname(q), gfp_conn_hash_port(q),
		    gfarm_error_string(conn_err));
	} else if (src_err != GFARM_ERR_NO_ERROR ||
	    dst_err != GFARM_ERR_NO_ERROR) {
		/* this should be reported by inode_replicated() of gfmd */
		gflog_notice(GFARM_MSG_1004235,
		    "%s: %s %lld:%lld from %s:%d: %s/%s", diag, issue_diag,
		    (long long)rep->ino, (long long)rep->gen,
		    gfp_conn_hash_hostname(q), gfp_conn_hash_port(q),
		    gfarm_error_string(src_err),
		    gfarm_error_string(dst_err));
	} else if (md_ctx != NULL) { /* no error case */
		dst_err = replication_dst_cksum_verify(
		    rep->issue_cksum_protocol,
		    rep->cksum_len, rep->cksum,
		    src_cksum_len, src_cksum,
		    md_strlen, md_string,
		    diag, issue_diag, rep->ino, rep->gen,
		    gfp_conn_hash_hostname(q), gfp_conn_hash_port(q));
	}

	if (conn_err == GFARM_ERR_NO_ERROR &&
	    src_err == GFARM_ERR_NO_ERROR && dst_err == GFARM_ERR_NO_ERROR) {
		int save_errno;

		if (fsync(local_fd) == -1) {
			save_errno = errno;
			dst_err = gfarm_errno_to_error(save_errno);
			gflog_error(GFARM_MSG_1005238,
			    "%s: %s %lld:%lld fsync(): %s",
			    diag, issue_diag,
			    (long long)rep->ino, (long long)rep->gen,
			    strerror(save_errno));
		} else if (gfarm_write_verify) {
			struct stat st;

			if (fstat(local_fd, &st) == -1) {
				save_errno = errno;
				dst_err = gfarm_errno_to_error(save_errno);
				gflog_error_errno(GFARM_MSG_1004388,
				    "%s: %s %lld:%lld fstat(): %s",
				    diag, issue_diag,
				    (long long)rep->ino, (long long)rep->gen,
				    strerror(save_errno));
			} else {
				write_verify_request(rep->ino, rep->gen,
				    st.st_mtime, diag);
			}
		}
	}

	rv = close(local_fd);
	if (rv == -1) {
		save_errno = errno;
		gflog_error(GFARM_MSG_1003514,
		    "%s: %s %lld:%lld from %s:%d: close: %s", diag, issue_diag,
		    (long long)rep->ino, (long long)rep->gen,
		    gfp_conn_hash_hostname(q), gfp_conn_hash_port(q),
		    strerror(save_errno));
		if (dst_err == GFARM_ERR_NO_ERROR)
			dst_err = gfarm_errno_to_error(save_errno);
	}

	res->recv.e.src_errcode = conn_err != GFARM_ERR_NO_ERROR ?
	    conn_err : src_err;
	res->recv.e.dst_errcode = dst_err;
	if (rep->handling_cksum_protocol) {
		memset(res->recv_cksum.cksum, 0,
		    sizeof(res->recv_cksum.cksum));
		if (rep->issue_cksum_protocol) {
			res->recv_cksum.cksum_len = src_cksum_len;
			if (src_cksum_len > 0)
				memcpy(res->recv_cksum.cksum, src_cksum,
				    src_cksum_len);
		} else {
			res->recv_cksum.cksum_len = md_strlen;
			if (md_strlen > 0)
				memcpy(res->recv_cksum.cksum, md_string,
				    md_strlen);
		}
		res->recv_cksum.cksum_result_flags = cksum_result_flags;
	}
}

/* returns gfmd_err */
gfarm_error_t
try_replication(struct gfp_xdr *conn, struct gfarm_hash_entry *q,
	gfarm_error_t *conn_errp, gfarm_error_t *dst_errp)
{
	gfarm_int32_t conn_err = GFARM_ERR_NO_ERROR;
	gfarm_int32_t dst_err = GFARM_ERR_NO_ERROR;
	struct replication_queue_data *qd = gfarm_hash_entry_data(q);
	struct replication_request *rep = qd->head;
	char *path;
	struct gfs_connection *src_gfsd;
	int fds[2];
	pid_t pid = -1; /* == GFS_PROTO_REPLICATION_HANDLE_INVALID */
	int local_fd, save_errno;
	size_t sz;
	ssize_t rv;
	union replication_results res;
	const char *diag = rep->handling_cksum_protocol ?
	    "GFS_PROTO_REPLICATION_CKSUM_REQUEST" :
	    "GFS_PROTO_REPLICATION_REQUEST";

	/*
	 * XXX FIXME:
	 * gfs_client_connection_acquire_by_host() needs timeout, otherwise
	 * the remote gfsd (or its kernel) can block this backchannel gfsd.
	 * See http://sourceforge.net/apps/trac/gfarm/ticket/130
	 */
	gfsd_local_path(rep->ino, rep->gen, diag, &path);
	local_fd = open_data(path, O_WRONLY|O_CREAT|O_TRUNC);
	save_errno = errno;
	free(path);
	if (local_fd == -1) {
		dst_err = gfarm_errno_to_error(save_errno);
		gflog_error(GFARM_MSG_1002182,
		    "%s: cannot open local file for %lld:%lld: %s", diag,
		    (long long)rep->ino, (long long)rep->gen,
		    strerror(save_errno));
	} else if (!confirm_local_path(rep->ino, rep->gen, diag)) {
		dst_err = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1004499, "%s: %lld:%lld: race detected",
		    diag, (long long)rep->ino, (long long)rep->gen);
		close(local_fd);
	} else if ((conn_err = gfs_client_connection_acquire_by_host(
	    gfm_server, gfp_conn_hash_hostname(q), gfp_conn_hash_port(q),
	    &src_gfsd, listen_addrname)) != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1002184, "%s: connecting to %s:%d: %s",
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
	} else if (fds[0] >= FD_SETSIZE) { /* for select(2) */
		dst_err = GFARM_ERR_TOO_MANY_OPEN_FILES;
		gflog_error(GFARM_MSG_1002186, "%s: cannot select %d: %s",
		    diag, fds[0], gfarm_error_string(dst_err));
		close(fds[0]);
		close(fds[1]);
		gfs_client_connection_free(src_gfsd);
		close(local_fd);
#endif
	} else if ((pid = do_fork(type_replication)) == 0) { /* child */
		close(fds[0]);

		(void)gfarm_proctitle_set(
		    "replication %s", gfp_conn_hash_hostname(q));

		memset(&res, 0, sizeof(res)); /* to shut up valgrind */
		replica_receive(q, rep, src_gfsd, local_fd, &res, diag);

		sz = rep->handling_cksum_protocol ?
		    sizeof(res.recv_cksum) : sizeof(res.recv);
		if ((rv = write(fds[1], &res, sz)) == -1)
			gflog_notice(GFARM_MSG_1002188, "%s: write pipe: %s",
			    diag, strerror(errno));
		else if (rv != sz) /* XXX "%zd" but not worth changing msgid */
			gflog_error(GFARM_MSG_1002189, "%s: partial write: "
			    "%d < %d", diag, (int)rv, (int)sz);
		close(fds[1]);
		exit(rv == sz &&
		    res.recv.e.src_errcode == GFARM_ERR_NO_ERROR &&
		    res.recv.e.dst_errcode == GFARM_ERR_NO_ERROR ? 0 : 1);
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

	*conn_errp = conn_err;
	*dst_errp = dst_err;

	if (rep->handling_cksum_protocol) {
		return (gfs_async_server_put_reply(conn, rep->xid, diag,
		    dst_err, "li", (gfarm_int64_t)pid, conn_err));
	} else {
		/*
		 * XXX FIXME,
		 * src_err and dst_err should be passed separately
		 */
		return (gfs_async_server_put_reply(conn, rep->xid, diag,
		    conn_err != GFARM_ERR_NO_ERROR ? conn_err : dst_err,
		    "l", (gfarm_int64_t)pid));
	}
}

gfarm_error_t
start_replication(struct gfp_xdr *conn, struct gfarm_hash_entry *q)
{
	gfarm_error_t gfmd_err, dst_err, conn_err;
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

			if (rep->handling_cksum_protocol) {
				gfmd_err = gfs_async_server_put_reply(conn,
				    rep->xid, diag, GFARM_ERR_NO_ERROR, "li",
				    GFS_PROTO_REPLICATION_HANDLE_INVALID,
				    src_net_err);
			} else {
				/*
				 * XXX FIXME
				 * src_err and dst_err should be passed
				 * separately
				 */
				gfmd_err = gfs_async_server_put_reply(conn,
				    rep->xid, diag, src_net_err, "");

			}
			if (gfmd_err != GFARM_ERR_NO_ERROR) {
				/* kill_pending_replications() frees rep */
				return (gfmd_err);
			}
		} else {
			gfmd_err = try_replication(conn, q,
			    &conn_err, &dst_err);
			if (gfmd_err != GFARM_ERR_NO_ERROR) {
				/* kill_pending_replications() frees rep */
				return (gfmd_err);
			}

			if (conn_err == GFARM_ERR_NO_ERROR &&
			    dst_err == GFARM_ERR_NO_ERROR)
				return (GFARM_ERR_NO_ERROR);
			if (IS_CONNECTION_ERROR(conn_err)) {
				src_net_err = conn_err;
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
		free(qd->head->cksum_type);
		free(qd->head);

		qd->head = rep;
	} while (rep != NULL);

	qd->tail = &qd->head;
	return (GFARM_ERR_NO_ERROR); /* no gfmd_err */
}

gfarm_error_t
gfs_async_server_replication_request(struct gfp_xdr *conn,
	const char *user, gfp_xdr_xid_t xid, size_t size,
	int handling_cksum_protocol)
{
	gfarm_error_t e;
	char *host;
	gfarm_int32_t port;
	gfarm_ino_t ino;
	gfarm_uint64_t gen, filesize = -1;
	char *cksum_type = NULL;
	size_t cksum_len = 0;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	gfarm_int32_t cksum_request_flags = 0;

	struct gfarm_hash_entry *q;
	struct replication_queue_data *qd;
	struct replication_request *rep;
	const char *const diag = handling_cksum_protocol ?
	    "GFS_PROTO_REPLICATION_CKSUM_REQUEST" :
	    "GFS_PROTO_REPLICATION_REQUEST";

	if (handling_cksum_protocol) {
		e = gfs_async_server_get_request(conn, size, diag, "silllsbi",
		    &host, &port, &ino, &gen,
		    &filesize, &cksum_type, sizeof(cksum), &cksum_len, cksum,
		    &cksum_request_flags);
	} else {
		e = gfs_async_server_get_request(conn, size, diag, "sill",
		    &host, &port, &ino, &gen);
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (cksum_type != NULL &&
	    strlen(cksum_type) > GFM_PROTO_CKSUM_TYPE_MAXLEN) {
		gflog_warning(GFARM_MSG_1004154,
		    "too long cksum type: \"%s\"", cksum_type);
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if (cksum_len > GFM_PROTO_CKSUM_MAXLEN) {
		gflog_warning(GFARM_MSG_1004155,
		    "too long cksum (type: \"%s\"): %d bytes",
		    cksum_type, (int)cksum_len);
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = replication_queue_lookup(host, port, user, &q)) !=
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

			rep->handling_cksum_protocol = handling_cksum_protocol;
			if (!handling_cksum_protocol) {
				rep->issue_cksum_protocol = 0;
				rep->reply_cksum = 0;
			} else if ((cksum_request_flags &
			    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_SRC_SUPPORTS)
			    == 0) { /* src gfsd doesn't support cksum */
				assert(cksum_len > 0);
				rep->issue_cksum_protocol = 0;
				rep->reply_cksum = 0;
			} else if (cksum_len > 0) {
				rep->issue_cksum_protocol = 1;
				rep->reply_cksum = 0;
			} else {
				rep->issue_cksum_protocol = 1;
				rep->reply_cksum = 1;
			}

			rep->xid = xid;
			rep->ino = ino;
			rep->gen = gen;

			rep->filesize = filesize;
			rep->cksum_type = cksum_type;
			rep->cksum_len = cksum_len;
			if (cksum_len > 0)
				memcpy(rep->cksum, cksum, cksum_len);
			rep->cksum_request_flags = cksum_request_flags;

			/* not set yet, will be set in try_replication() */
			rep->src_gfsd = NULL;
			rep->file_fd = -1;
			rep->pipe_fd = -1;
			rep->pid = GFS_PROTO_REPLICATION_HANDLE_INVALID;
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
	free(cksum_type);

	/* only used in an error case */
	return (gfs_async_server_put_reply(conn, xid, diag, e, ""));
}

static void
gfs_server_rdma_exch_info(struct gfp_xdr *client)
{
	int success = 0;
	size_t size = 0;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_uint32_t client_lid, lid = 0;
	gfarm_uint32_t client_qpn, qpn = 0;
	gfarm_uint32_t client_psn, psn = 0;
	unsigned char buffer[IBV_GID_SIZE], *gid = buffer;

	gfs_server_get_request(client, "rdma_exch_info", "iiib",
		&client_lid, &client_qpn, &client_psn,
		sizeof(buffer), &size, gid);
	if (!gfs_rdma_check(rdma_ctx)) {
		gflog_debug(GFARM_MSG_1004717, "gfs_rdma_check(): failed");
		size = 0;
		goto reply;
	}
	gfs_rdma_set_remote_lid(rdma_ctx, client_lid);
	gfs_rdma_set_remote_qpn(rdma_ctx, client_qpn);
	gfs_rdma_set_remote_psn(rdma_ctx, client_psn);
	gfs_rdma_set_remote_gid(rdma_ctx, gid);

	if ((e = gfs_rdma_connect(rdma_ctx)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004718, "rdma_connect(): failed");
		gfs_rdma_disable(rdma_ctx);
		size = 0;
	} else {
		lid = gfs_rdma_get_local_lid(rdma_ctx);
		qpn = gfs_rdma_get_local_qpn(rdma_ctx);
		psn = gfs_rdma_get_local_psn(rdma_ctx);
		gid = gfs_rdma_get_local_gid_iff_global(rdma_ctx);

		gflog_debug(GFARM_MSG_1004719, "rdma_connect(): success");
		gfs_rdma_enable(rdma_ctx);
		size = gfs_rdma_get_gid_size();
	}

	success = gfs_rdma_check(rdma_ctx);
reply:
	gfs_server_put_reply(client, "rdma_exch_info", e, "iiiib", success,
		lid, qpn, psn, size, gid);
}

void
gfs_server_rdma_hello(struct gfp_xdr *client)
{
	gfarm_error_t e = GFARM_ERR_DEVICE_NOT_CONFIGURED;
	int size;
	gfarm_uint32_t rkey;
	gfarm_uint64_t addr;

	gfs_server_get_request(client, "rdma_hello", "iil",
		&rkey, &size, &addr);
	if (!gfs_rdma_check(rdma_ctx)) {
		gflog_debug(GFARM_MSG_1004720, "gfs_rdma_check(): failed");
		goto reply;
	}
	if ((e = gfs_rdma_remote_read(rdma_ctx, rkey, addr, size))
	   != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004721, "rdma_remote_read() failed,"
			"rkey=%u size=0x%x addr=0x%lx", rkey, size,
			(unsigned long)addr);
		goto reply;
	}
	if (memcmp(gfs_rdma_get_buffer(rdma_ctx),
		gfs_rdma_get_remote_gid(rdma_ctx), gfs_rdma_get_gid_size())) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1004722, "data maching failed");
	}
reply:
	if (e != GFARM_ERR_NO_ERROR)
		gfs_rdma_disable(rdma_ctx);

	gfs_server_put_reply(client, "rdma_hello", e, "");
}

void
gfs_server_rdma_pread(struct gfp_xdr *client)
{
	gfarm_int32_t fd, size, bsize;
	gfarm_int64_t offset;
	gfarm_uint32_t rkey;
	gfarm_uint64_t addr;
	ssize_t rv = 0;
	struct file_entry *fe;
	gfarm_error_t e = GFARM_ERR_DEVICE_NOT_CONFIGURED;
	gfarm_timerval_t t1, t2, t3;
	static const char diag[] = "gfs_server_rdma_pread";

	gfs_server_get_request(client, diag, "iilil",
		&fd, &size, &offset, &rkey, &addr);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
			"");
		return;
	}
	if (!gfs_rdma_check(rdma_ctx)) {
		gflog_debug(GFARM_MSG_1004723, "gfs_rdma_check(): failed");
		goto reply;
	}
	if ((fe = file_table_entry(fd)) == NULL) {
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
		goto reply;
	}
	/* We truncatef i/o size bigger than GFS_PROTO_MAX_IOSIZE. */
	bsize = gfs_rdma_resize_buffer(rdma_ctx, size);
	if (size > bsize)
		size = bsize;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if ((rv = pread(fe->local_fd, gfs_rdma_get_buffer(rdma_ctx),
		size, offset)) == -1) {
		io_error_check_errno(diag);
		e = gfarm_errno_to_error(errno);
	} else {
		file_table_set_read(fd);
		/* update checksum */
		if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    FILE_FLAG_DIGEST_CALC) {
			if (fe->md_offset == offset) {
				EVP_DigestUpdate(fe->md_ctx,
				    gfs_rdma_get_buffer(rdma_ctx), rv);
				fe->md_offset += rv;
				if (fe->md_offset == fe->size &&
				    (fe->flags & FILE_FLAG_WRITTEN) == 0)
					e = digest_finish(client, fd, diag);
			} else
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		}
		e = GFARM_ERR_NO_ERROR;
	}

	if (rv > 0) {
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_RBYTES, rv);
		gfs_profile(
			gfarm_gettimerval(&t2);
			fe->nread++;
			fe->read_size += rv;
			fe->read_time += gfarm_timerval_sub(&t2, &t1));

		if ((e = gfs_rdma_remote_write(rdma_ctx, rkey, addr, rv))
		   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1004724,
						"rdma_remote_write() failed");
			goto reply;
		}
		gfs_profile(
			gfarm_gettimerval(&t3);
			IF_INFINIBAND(
				fe->rdma_read_size += rv;
				fe->rdma_read_time +=
				    gfarm_timerval_sub(&t3, &t2);
			)
		);
	}
reply:
	gfs_server_put_reply(client, diag, e, "i", (int)rv);
}

void
gfs_server_rdma_pwrite(struct gfp_xdr *client)
{
	gfarm_int32_t fd, localfd, size;
	gfarm_int64_t offset;
	ssize_t rv = 0;
	int bsize, save_errno = 0;
	struct file_entry *fe;
	gfarm_timerval_t t1, t2, t0;
	gfarm_error_t e = GFARM_ERR_DEVICE_NOT_CONFIGURED;
	gfarm_uint32_t rkey;
	gfarm_uint64_t addr;
	static const char diag[] = "gfs_server_rdma_pwrite";

	gfs_server_get_request(client, diag, "iilil",
		&fd, &size, &offset, &rkey, &addr);

	if (!fd_usable_to_gfmd) {
		gfs_server_put_reply(client, diag, GFARM_ERR_GFMD_FAILED_OVER,
			"");
		return;
	}
	if (!gfs_rdma_check(rdma_ctx)) {
		gflog_debug(GFARM_MSG_1004725, "gfs_rdma_check(): failed");
		gfs_server_put_reply(client, diag,
			GFARM_ERR_DEVICE_NOT_CONFIGURED, "");
		return;
	}
	if ((fe = file_table_entry(fd)) == NULL) {
		save_errno = EBADF;
		goto reply;
	}

	/*
	 * We truncate i/o size bigger than GFS_PROTO_MAX_IOSIZE.
	 * This is inefficient because passed extra data are just
	 * abandoned. So client should avoid such situation.
	 */
	bsize = gfs_rdma_resize_buffer(rdma_ctx, size);
	if (size > bsize)
		size = bsize;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t0);
	gfs_profile(gfarm_gettimerval(&t0));
	if ((e = gfs_rdma_remote_read(rdma_ctx, rkey, addr, size))
	   != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004726,
					"rdma_remote_read() failed");
		gfs_server_put_reply(client, diag, e, "");
		return;
	}

	gfs_profile(
		gfarm_gettimerval(&t1);
		IF_INFINIBAND(
			fe->rdma_write_size += size;
			fe->rdma_write_time += gfarm_timerval_sub(&t1, &t0);
		)
	);

	localfd = file_table_get(fd);
	if (fe->local_flags & O_APPEND) {
		if ((rv = write(localfd, gfs_rdma_get_buffer(rdma_ctx),
			size)) != -1)
			offset = lseek(localfd, 0, SEEK_CUR) - rv;
	} else
		rv = pwrite(localfd, gfs_rdma_get_buffer(rdma_ctx),
			size, offset);
	if (rv == -1) {
		io_error_check_errno(diag);
		save_errno = errno;
	} else {
		file_table_set_written(fd);
		/* update checksum */
		if ((fe->flags &
		    (FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
		    FILE_FLAG_DIGEST_CALC) {
			if (fe->md_offset == offset) {
				EVP_DigestUpdate(fe->md_ctx,
				    gfs_rdma_get_buffer(rdma_ctx), rv);
				fe->md_offset += rv;
			} else
				fe->flags &= ~FILE_FLAG_DIGEST_CALC;
		} else if ((fe->flags &
			(FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH)) ==
			(FILE_FLAG_DIGEST_CALC|FILE_FLAG_DIGEST_FINISH))
			fe->flags &= ~FILE_FLAG_DIGEST_CALC;
	}
	if (rv > 0) {
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WCOUNT, 1);
		gfarm_iostat_local_add(GFARM_IOSTAT_IO_WBYTES, rv);
		gfs_profile(
			gfarm_gettimerval(&t2);
			fe->nwrite++;
			fe->write_size += rv;
			fe->write_time += gfarm_timerval_sub(&t2, &t1));
	}

reply:
	gfs_server_put_reply_with_errno(client, diag, save_errno,
					"i", (gfarm_int32_t)rv);
}

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
		if (pid == back_channel_gfsd_pid
		 || pid == write_verify_controller_gfsd_pid)
			continue;
		gfarm_iostat_clear_id(pid, 0);
	}
}

/*
 * input value:
 *  fd1 may be -1, in that case, it's ignored;
 * return value:
 *  0: timed out
 *  bit0 is set: fd0 is ready
 *  bit1 is set: fd1 is ready
 */
int
timedwait_2fds(int fd0, int fd1, time_t seconds, const char *diag)
{
	int nfound, rv = 0;
#ifdef HAVE_POLL
	time_t now, expire_time = 0;
	struct pollfd fds[2];
	int nfds;

	if (seconds != TIMEDWAIT_INFINITE)
		expire_time = time(NULL) + seconds;

	for (;;) {
		now = time(NULL);
		if (seconds != TIMEDWAIT_INFINITE && expire_time < now)
			expire_time = now;

		fds[0].fd = fd0;
		fds[0].events = POLLIN;
		if (fd1 != -1) {
			fds[1].fd = fd1;
			fds[1].events = POLLIN;
			nfds = GFARM_ARRAY_LENGTH(fds);
		} else {
			nfds = GFARM_ARRAY_LENGTH(fds) - 1;
		}
		nfound = poll(fds, nfds, seconds == TIMEDWAIT_INFINITE ?
		    INFTIM : (expire_time - now) * GFARM_SECOND_BY_MILLISEC);
		if (nfound == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			fatal_errno(GFARM_MSG_1004389,
			    "poll in timedwait_2fds()");
		}
		if (fds[0].revents != 0)
			rv |= 1;
		if (fd1 != -1 && fds[1].revents != 0)
			rv |= 2;
		return (rv);
	}
#else /* !HAVE_POLL */
	struct timeval expire_time, now, timeout;
	fd_set fds;
	int max_fd;

	if (seconds != TIMEDWAIT_INFINITE) {
		gettimeofday(&expire_time, NULL);
		expire_time.tv_sec += seconds;
	}

	for (;;) {
		gettimeofday(&now, NULL);
		if (seconds != TIMEDWAIT_INFINITE) {
			if (gfarm_timeval_cmp(&expire_time, &now) < 0)
				expire_time = now;
			timeout = expire_time;
			gfarm_timeval_sub(&timeout, &now);
		}

		max_fd = fd0;
		if (fd1 != -1 && fd1 > max_fd)
			max_fd = fd1;
		if (max_fd >= FD_SETSIZE)
			fatal(GFARM_MSG_1004390,
			    "too big descriptor: fd:%d", max_fd);
		FD_ZERO(&fds);
		FD_SET(fd0, &fds);
		if (fd1 != -1)
			FD_SET(fd1, &fds);
		nfound = select(max_fd + 1, &fds, NULL, NULL,
		    seconds == TIMEDWAIT_INFINITE ? NULL : &timeout);
		if (nfound == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			fatal_errno(GFARM_MSG_1004391,
			    "select in timedwait_2fds()");
		}
		if (FD_ISSET(fd0, &fds))
			rv |= 1;
		if (fd1 != -1 && FD_ISSET(fd1, &fds))
			rv |= 2;
		return (rv);
	}
#endif /* !HAVE_POLL */
}

int
timedwait_fd(int fd, time_t seconds, const char *diag)
{
	return (timedwait_2fds(fd, -1, seconds, diag));
}

int
fd_is_ready(int fd, const char *diag)
{
	return (timedwait_fd(fd, 0, diag));
}

/*
 * input value:
 *  fd2 may be -1, in that case, it's ignored;
 * return value:
 *  0: EINTR or EAGAIN
 *  bit0 is set: fd0 is ready
 *  bit1 is set: fd1 is ready
 *  bit2 is set: fd2 is ready
 */
int
wait_3fds(int fd0, int fd1, int fd2, const char *diag)
{
	int nfound, rv = 0;
#ifdef HAVE_POLL
	struct pollfd fds[3];
	int nfds;

	fds[0].fd = fd0;
	fds[0].events = POLLIN;
	fds[1].fd = fd1;
	fds[1].events = POLLIN;
	if (fd2 != -1) {
		fds[2].fd = fd2;
		fds[2].events = POLLIN;
		nfds = GFARM_ARRAY_LENGTH(fds);
	} else {
		nfds = GFARM_ARRAY_LENGTH(fds) - 1;
	}
	nfound = poll(fds, nfds, INFTIM);
	if (nfound == 0)
		fatal(GFARM_MSG_1004156,
		    "unexpected poll in wait_3fds()");
	if (nfound == -1) {
		if (errno == EINTR || errno == EAGAIN)
			return (0);
		fatal_errno(GFARM_MSG_1004157, "poll in wait_3fds()");
	}
	assert(nfound > 0);
	if (fds[0].revents != 0)
		rv |= 1;
	if (fds[1].revents != 0)
		rv |= 2;
	if (fd2 != -1 && fds[2].revents != 0)
		rv |= 4;
#else /* !HAVE_POLL */
	fd_set fds;
	int max_fd;

	max_fd = fd0;
	if (fd1 > max_fd)
		max_fd = fd1;
	if (fd2 != -1 && fd2 > max_fd)
		max_fd = fd2;
	if (max_fd >= FD_SETSIZE)
		fatal(GFARM_MSG_1004158,
		    "too big descriptor: fd0:%d fd1:%d fd2:%d", fd0, fd1, fd2);
	FD_ZERO(&fds);
	FD_SET(fd0, &fds);
	FD_SET(fd1, &fds);
	if (fd2 != -1)
		FD_SET(fd2, &fds);
	nfound = select(max_fd + 1, &fds, NULL, NULL, NULL);
	if (nfound == 0)
		fatal(GFARM_MSG_1004159, "unexpected select in wait_3fds()");
	if (nfound == -1) {
		if (errno == EINTR || errno == EAGAIN)
			return (0);
		fatal_errno(GFARM_MSG_1004160, "select in wait_3fds()");
	}
	assert(nfound > 0);
	if (FD_ISSET(fd0, &fds))
		rv |= 1;
	if (FD_ISSET(fd1, &fds))
		rv |= 2;
	if (fd2 != -1 && FD_ISSET(fd2, &fds))
		rv |= 4;
#endif /* !HAVE_POLL */
	return (rv);
}

/*
 * return value:
 *  0: EINTR or EAGAIN
 *  bit0 is set: fd0 is ready
 *  bit1 is set: fd1 is ready
 */
int
wait_2fds(int fd0, int fd1, const char *diag)
{
	return (wait_3fds(fd0, fd1, -1, diag));
}

void
wait_fd_with_failover_pipe(int waiting_fd, const char *diag)
{
	int rv;

	for (;;) {
		rv = wait_2fds(waiting_fd, failover_notify_recv_fd, diag);
		if (rv == 0)
			continue;
		if ((rv & 2) != 0) {
			failover_notified(debug_mode, diag);
			reconnect_gfm_server_for_failover("failover signal");
		}
		if ((rv & 1) != 0)
			break;
	}
}

void
server(int client_fd, char *client_name, struct sockaddr *client_addr)
{
	gfarm_error_t e;
	struct gfp_xdr *client;
	int eof;
	gfarm_int32_t request, last_request = -1;
	char *aux, addr_string[GFARM_SOCKADDR_STRLEN];
	enum gfarm_auth_id_role peer_role;
	enum gfarm_auth_method auth_method;

	(void)gfarm_proctitle_set("client");

	if ((e = connect_gfm_server("gfsd-for-client")) != GFARM_ERR_NO_ERROR)
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
	(void)gfarm_proctitle_set("client @%s", client_name);

	e = gfp_xdr_new_socket(client_fd, &client);
	if (e != GFARM_ERR_NO_ERROR) {
		close(client_fd);
		fatal(GFARM_MSG_1000554, "%s: gfp_xdr_new: %s",
		    client_name, gfarm_error_string(e));
	}

	e = gfarm_authorize_wo_setuid(client, GFS_SERVICE_TAG,
	    client_name, client_addr,
	    gfarm_auth_uid_to_global_username, gfm_server,
	    &peer_role, &username, &auth_method);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1000555, "%s: gfarm_authorize: %s",
		    client_name, gfarm_error_string(e));
		cleanup(0);
		exit(1);
	}
	GFARM_MALLOC_ARRAY(aux, strlen(username)+1 + strlen(client_name)+1);
	if (aux == NULL)
		fatal(GFARM_MSG_1000556, "%s: no memory", client_name);
	sprintf(aux, "%s@%s", username, client_name);
	gflog_set_auxiliary_info(aux);
	(void)gfarm_proctitle_set("client %s", aux);
	current_client = client; /* for cleanup() */

#ifdef HAVE_INFINIBAND
	/* should be after gfarm_authorize()::setuid() */
	if ((e = gfs_rdma_init(1, &rdma_ctx)) != GFARM_ERR_NO_ERROR) {
		/* don't care even if rdma is unabailable */
		gflog_info(GFARM_MSG_1004727, "rdma_init() failed");
	} else {
		gflog_debug(GFARM_MSG_1004728, "rdma_init(): success");
	}
#endif

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
		wait_fd_with_failover_pipe(
		    gfp_xdr_fd(client), "gfsd-for-client");
		e = gfp_xdr_recv_notimeout(client, 0, &eof, "i", &request);
		if (e != GFARM_ERR_NO_ERROR) {
			conn_fatal(GFARM_MSG_1000557, "request number: %s",
			    gfarm_error_string(e));
		}
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
		case GFS_PROTO_CLOSE_WRITE:
			gfs_server_close_write(client); break;
		case GFS_PROTO_PREAD:	gfs_server_pread(client); break;
		case GFS_PROTO_PWRITE:	gfs_server_pwrite(client); break;
		case GFS_PROTO_WRITE:	gfs_server_write(client); break;
		case GFS_PROTO_BULKREAD: gfs_server_bulkread(client); break;
		case GFS_PROTO_BULKWRITE: gfs_server_bulkwrite(client); break;
		case GFS_PROTO_FTRUNCATE: gfs_server_ftruncate(client); break;
		case GFS_PROTO_FSYNC:	gfs_server_fsync(client); break;
		case GFS_PROTO_FSTAT:	gfs_server_fstat(client); break;
		case GFS_PROTO_CKSUM:
			gfs_server_cksum(client); break;
		case GFS_PROTO_STATFS:	gfs_server_statfs(client); break;
		case GFS_PROTO_REPLICA_ADD_FROM:
			gfs_server_replica_add_from(client); break;
		case GFS_PROTO_REPLICA_RECV:
			gfs_server_replica_recv(client, peer_role, 0); break;
		case GFS_PROTO_REPLICA_RECV_CKSUM:
			gfs_server_replica_recv(client, peer_role, 1); break;
		case GFS_PROTO_RDMA_EXCH_INFO:
			gfs_server_rdma_exch_info(client); break;
		case GFS_PROTO_RDMA_HELLO:
			gfs_server_rdma_hello(client); break;
		case GFS_PROTO_RDMA_PREAD:
			gfs_server_rdma_pread(client); break;
		case GFS_PROTO_RDMA_PWRITE:
			gfs_server_rdma_pwrite(client); break;
		default:
			gflog_warning(GFARM_MSG_1000558, "unknown request %d",
			    (int)request);
			gflog_info(GFARM_MSG_1005227, "last request: %d",
			    (int)last_request);
			cleanup(0);
			exit(1);
		}
		if (!gfm_client_connection_empty(gfm_server)) {
			gflog_notice(GFARM_MSG_1003786, "protocol mismatch, "
			    "iobuffer not empty: request = %d", request);
			cleanup(0);
			exit(1);
		}
		if (gfm_client_is_connection_error(
		    gfp_xdr_flush(gfm_client_connection_conn(gfm_server)))) {
			free_gfm_server();
			if ((e = connect_gfm_server(client_name))
			    != GFARM_ERR_NO_ERROR)
				fatal(GFARM_MSG_1003362, "die");
		}
		last_request = request;
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
	int client = accept(accepting_sock,
	   client_addr_storage, &client_addr_size);

	if (client < 0) {
		if (errno == EINTR || errno == ECONNABORTED ||
#ifdef EPROTO
		    errno == EPROTO ||
#endif
		    errno == EAGAIN)
			return;
		fatal_errno(GFARM_MSG_1000559, "accept");
	}
#ifndef GFSD_DEBUG
	switch ((pid = do_fork(type_client))) {
	case 0:
#endif
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
#endif
}

static void
failover_event(const char *new_master_name, int new_master_port)
{
	gfarm_error_t e, e2;
	/* avoid gcc warning "passing arg 3 from incompatible pointer type" */
	const char *n = canonical_self_name;
	struct gfarm_host_info self_info;

	e = gfm_client_host_info_get_by_names(gfm_server,
	    1, &n, &e2, &self_info);
	if (e == GFARM_ERR_NO_ERROR) {
		if (e2 == GFARM_ERR_NO_ERROR)
			gfarm_host_info_free(&self_info);
		else
			gflog_info(GFARM_MSG_1004161, "%s: host_info_get: %s",
			    canonical_self_name, gfarm_error_string(e2));
		gflog_notice(GFARM_MSG_1004162,
		    "failover notification is received, "
		    "but current master gfmd is alive");
		return;
	}
	if (!IS_CONNECTION_ERROR(e)) {
		gflog_notice(GFARM_MSG_1004163,
		    "GFM_PROTO_HOST_INFO_GET_BY_NAMES(%s): %s",
		    canonical_self_name, gfarm_error_string(e));
		/* return; */ /* or, ignore this event? */
	}
	gflog_info(GFARM_MSG_1004164,
	    "failover notify(%s:%d): trying new master",
	    new_master_name, new_master_port);

	free_gfm_server();
	e = connect_gfm_server("failover notify");
	if (e != GFARM_ERR_NO_ERROR)
		fatal(GFARM_MSG_1004165,
		    "connect_gfm_server: %s", gfarm_error_string(e));

	if (kill(-getpid(), FAILOVER_SIGNAL) == -1)
		gflog_warning_errno(GFARM_MSG_1004166,
		    "kill(FAILOVER_SIGNAL)");
}

static void
gfs_udp_server_reply(int sock,
	struct sockaddr *client_addr, socklen_t client_addr_size,
	gfarm_uint32_t retry_count, const unsigned char *xid,
	gfarm_uint32_t request_type, gfarm_int32_t error_code,
	const char *diag)
{
	gfarm_uint32_t u32;
	int rv;
	unsigned char buffer[GFS_UDP_RPC_SIZE_MAX], *p = buffer;

	u32 = htonl(GFS_UDP_RPC_MAGIC);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(GFS_UDP_RPC_TYPE_REPLY);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(retry_count);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	memcpy(p, xid, GFS_UDP_RPC_XID_SIZE);
	p += GFS_UDP_RPC_XID_SIZE;

	u32 = htonl(request_type);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	u32 = htonl(error_code);
	memcpy(p, &u32, sizeof(u32)); p += sizeof(u32);

	rv = sendto(sock, buffer, p - buffer, 0,
	    client_addr, client_addr_size);
	if (rv == -1) {
		gflog_warning_errno(GFARM_MSG_1004167, "%s: sendto", diag);
	} else if (rv != p - buffer) {
		gflog_error(GFARM_MSG_1004168,
		    "%s: sentdo: short write: %d != %zd",
		    diag, rv, p - buffer);
	}
}

static int
failover_sanity_check(const char *new_master_name, int new_master_port)
{
	struct gfarm_filesystem *fs =
	    gfarm_filesystem_get(new_master_name, new_master_port);

	return (fs != NULL && fs == gfarm_filesystem_get_default());
}

static void
gfs_udp_server_failover_notify(int sock,
	struct sockaddr *client_addr, socklen_t client_addr_size,
	gfarm_uint32_t retry_count, const unsigned char *xid,
	unsigned char *request, int reqlen)
{
	unsigned char *p = request;
	char got_master_name[GFARM_MAXHOSTNAMELEN + 1];
	gfarm_uint32_t got_master_name_len, u32;
	gfarm_int32_t got_master_port, e;
	static const char diag[] = "GFS_UDP_PROTO_FAILOVER_NOTIFY";

	if (reqlen < GFS_UDP_PROTO_FAILOVER_NOTIFY_REQUEST_MIN_SIZE
	    - GFS_UDP_RPC_HEADER_SIZE) {
		/* XXX gfarm_sockaddr_to_name */
		gflog_warning(GFARM_MSG_1004169,
		    "%s: too short: %d bytes", diag,
		    GFS_UDP_RPC_HEADER_SIZE + reqlen);
		e = GFARM_ERR_PROTOCOL_NOT_SUPPORTED;
	} else {
		memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
		got_master_name_len = ntohl(u32);

		if (got_master_name_len > GFARM_MAXHOSTNAMELEN) {
			gflog_warning(GFARM_MSG_1004170,
			    "UDP request: too long hostname: %d bytes",
			    got_master_name_len);
			e = GFARM_ERR_MESSAGE_TOO_LONG;
		} else if (reqlen - (int)(p - request)
		    != got_master_name_len + sizeof(got_master_port)) {
			gflog_warning(GFARM_MSG_1004171,
			    "UDP request: invalid packet size: "
			    "hostname: %d bytes vs packet %d bytes",
			    got_master_name_len,
			    GFS_UDP_RPC_HEADER_SIZE + reqlen);
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else {
			memcpy(got_master_name, p, got_master_name_len);
			got_master_name[got_master_name_len] = '\0';
			p += got_master_name_len;

			memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
			got_master_port = ntohl(u32);

			if (!failover_sanity_check(
			    got_master_name, got_master_port)) {
				gflog_warning(GFARM_MSG_1004172,
				    "%s: host %s:%d doesn't match "
				    "with metadb_server_list",
				    diag, got_master_name, got_master_port);
				e = GFARM_ERR_UNKNOWN_HOST;
			} else {
				/*
				 * reply before calling failover_event(),
				 * because failover_event() may take a longer
				 * time the UDP timeout.
				 */
				gfs_udp_server_reply(sock,
				    client_addr, client_addr_size,
				    retry_count, xid,
				    GFS_UDP_PROTO_FAILOVER_NOTIFY,
				    GFARM_ERR_NO_ERROR, diag);

				gflog_info(GFARM_MSG_1004173,
				    "failover notified: %s:%d",
				    got_master_name, got_master_port);
				failover_event(
				    got_master_name, got_master_port);
				return;
			}
		}
	}

	/* report the error */
	gfs_udp_server_reply(sock, client_addr, client_addr_size,
	    retry_count, xid, GFS_UDP_PROTO_FAILOVER_NOTIFY, e, diag);
}

static void
gfs_udp_server(int sock,
	struct sockaddr *client_addr, socklen_t client_addr_size,
	unsigned char *request, int reqlen)
{
	gfarm_error_t e;
	unsigned char *got_xid, *p = request;
	gfarm_uint32_t u32, got_rpc_magic;
	gfarm_uint32_t got_rpc_type, got_retry_count, got_request_type;
	char addr_str[GFARM_SOCKADDR_STRLEN];
	int addr_strlen = GFARM_SOCKADDR_STRLEN;

	if (reqlen < GFS_UDP_RPC_HEADER_SIZE) {
		gfarm_sockaddr_to_string(client_addr, addr_str, addr_strlen);
		gflog_warning(GFARM_MSG_1004323, "UDP request: too short: "
		    "%d bytes from %s", reqlen, addr_str);
		return;
	}

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_rpc_magic = ntohl(u32);

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_rpc_type = ntohl(u32);

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_retry_count = ntohl(u32);

	got_xid = p; p += GFS_UDP_RPC_XID_SIZE;

	memcpy(&u32, p, sizeof(u32)); p += sizeof(u32);
	got_request_type = ntohl(u32);

	if (debug_mode) {
		/* XXX gfarm_sockaddr_to_name */
		gflog_info(GFARM_MSG_1004175,
		    "gfs_udp_server(): got udp request: "
		    "type=0x%08x, retry_count=%d, "
		    "xid=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, "
		    "request=0x%08x", got_rpc_type, got_retry_count,
		    got_xid[0], got_xid[1], got_xid[2], got_xid[3], got_xid[4],
		    got_xid[5], got_xid[6], got_xid[7], got_request_type);
	}

	if (got_rpc_magic != GFS_UDP_RPC_MAGIC ||
	    got_rpc_type != GFS_UDP_RPC_TYPE_REQUEST ||
	    got_retry_count > GFS_UDP_RPC_RETRY_COUNT_SANITY) {
		/* XXX gfarm_sockaddr_to_name */
		gflog_notice(GFARM_MSG_1004176,
		    "gfs_udp_server(): invalid packet: "
		    "type=0x%08x, retry_count=0x%08x (should be <= 0x%08x), "
		    "xid=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, "
		    "request=0x%08x", got_rpc_type, got_retry_count,
		    GFS_UDP_RPC_RETRY_COUNT_SANITY,
		    got_xid[0], got_xid[1], got_xid[2], got_xid[3], got_xid[4],
		    got_xid[5], got_xid[6], got_xid[7], got_request_type);
		/* do not reply, to prevent reflection attack */
		return;
	}

	if (got_request_type == GFS_UDP_PROTO_FAILOVER_NOTIFY) {
		static int last_failover_xid_available = 0;
		static struct sockaddr last_failover_xid_addr; /* XXX IPv6 */
		static socklen_t last_failover_xid_addr_size;
		static unsigned char last_failover_xid[GFS_UDP_RPC_XID_SIZE];
		static gfarm_error_t last_failover_xid_result;

		if (last_failover_xid_available &&
		    client_addr_size == last_failover_xid_addr_size &&
		    memcmp(client_addr, &last_failover_xid_addr,
			last_failover_xid_addr_size) == 0 &&
		    memcmp(got_xid, last_failover_xid, GFS_UDP_RPC_XID_SIZE)
			== 0) {
			/* XXX gfarm_sockaddr_to_name */
			gflog_notice(GFARM_MSG_1004177,
			    "gfs_udp_server(): duplicate xid: "
			    "type=0x%08x, retry_count=%d, "
			    "xid=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, "
			    "request=0x%08x", got_rpc_type, got_retry_count,
			    got_xid[0], got_xid[1], got_xid[2], got_xid[3],
			    got_xid[4], got_xid[5], got_xid[6], got_xid[7],
			    got_request_type);
			e = last_failover_xid_result;
		} else {
			gfs_udp_server_failover_notify(
			    sock, client_addr, client_addr_size,
			    got_retry_count, got_xid,
			    p, reqlen - (p - request));
			e = GFARM_ERR_NO_ERROR;
			if (client_addr_size > sizeof(last_failover_xid_addr)) {
				gflog_warning(GFARM_MSG_1004178,
				    "UDP request: too big client addr: %d",
				    (int)client_addr_size);
				/* accept duplicate xid in this case */
			} else {
				last_failover_xid_available = 1;
				memcpy(&last_failover_xid_addr, client_addr,
				    client_addr_size);
				last_failover_xid_addr_size = client_addr_size;
				memcpy(last_failover_xid, got_xid,
				    GFS_UDP_RPC_XID_SIZE);
				last_failover_xid_result = e;
			}
		}
	} else {
		/* XXX gfarm_sockaddr_to_name */
		gflog_notice(GFARM_MSG_1004179,
		    "gfs_udp_server(): unknown request: "
		    "type=0x%08x, retry_count=0x%08x, "
		    "xid=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, "
		    "request=0x%08x", got_rpc_type, got_retry_count,
		    got_xid[0], got_xid[1], got_xid[2], got_xid[3],
		    got_xid[4], got_xid[5], got_xid[6], got_xid[7],
		    got_request_type);
		gflog_warning(GFARM_MSG_1004180,
		    "UDP request: unknown request: %d",
		    got_request_type);
		e = GFARM_ERR_PROTOCOL_NOT_SUPPORTED;
	}

	if (e != GFARM_ERR_NO_ERROR) {
		gfs_udp_server_reply(sock, client_addr, client_addr_size,
		    got_retry_count, got_xid, got_request_type,
		    e, "gfp_udp_server");
	}
}

static void
gfs_udp_server_old_loadav(int sock,
	struct sockaddr *client_addr, socklen_t client_addr_size,
	unsigned char *request, int reqlen)
{
	double loadavg[3];
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nloadavg[3];
#else
#	define nloadavg loadavg
#endif
	int rv;

	rv = getloadavg(loadavg, GFARM_ARRAY_LENGTH(loadavg));
	if (rv == -1) {
		gflog_warning(GFARM_MSG_1000561,
		    "datagram_server: cannot get load average");
		return;
	}
	/* add base load */
	loadavg[0] += gfarm_spool_base_load;
	loadavg[1] += gfarm_spool_base_load;
	loadavg[2] += gfarm_spool_base_load;
#ifndef WORDS_BIGENDIAN
	swab(&loadavg[0], &nloadavg[0], sizeof(nloadavg[0]));
	swab(&loadavg[1], &nloadavg[1], sizeof(nloadavg[1]));
	swab(&loadavg[2], &nloadavg[2], sizeof(nloadavg[2]));
#endif
	rv = sendto(sock, nloadavg, sizeof(nloadavg), 0,
	    client_addr, client_addr_size);
	if (rv == -1) {
		/* XXX gfarm_sockaddr_to_name */
		gflog_warning_errno(GFARM_MSG_1004181,
		    "UDP loadav: sendto");
	}
}

/* XXX FIXME: add protocol magic number and transaction ID */
void
datagram_server(int sock)
{
	int rv;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size = sizeof(client_addr);
	unsigned char buffer[GFS_UDP_RPC_SIZE_MAX];

	rv = recvfrom(sock, buffer, sizeof(buffer), 0,
	    (struct sockaddr *)&client_addr, &client_addr_size);
	if (rv == -1)
		return;
	if (rv == GFS_UDP_PROTO_OLD_LOADAV_REQUEST_SIZE) {
		gfs_udp_server_old_loadav(sock,
		    (struct sockaddr *)&client_addr, client_addr_size,
		    buffer, rv);
	} else {
		gfs_udp_server(sock,
		    (struct sockaddr *)&client_addr, client_addr_size,
		    buffer, rv);
	}

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
	union replication_results res;
	size_t sz = rep->handling_cksum_protocol ?
	    sizeof(res.recv_cksum) : sizeof(res.recv);
	ssize_t rv = read(rep->pipe_fd, &res, sz);
	int status;
	struct stat st;
	static const char diag[] = "GFM_PROTO_REPLICATION_RESULT";

	if (rv != sz) {
		if (rv == -1) {
			gflog_error(GFARM_MSG_1002191,
			    "%s: cannot read child result: %s",
			    diag, strerror(errno));
		} else { /* XXX "%zd", but not worth changing msgid */
			gflog_error(GFARM_MSG_1002192,
			    "%s: too short child result: %d bytes", diag,
				    (int)rv);
		}
		res.recv.e.src_errcode = 0;
		res.recv.e.dst_errcode = GFARM_ERR_UNKNOWN;
		res.recv_cksum.cksum_len = 0;
		res.recv_cksum.cksum_result_flags = 0;
	} else if (fstat(rep->file_fd, &st) == -1) {
		gflog_error(GFARM_MSG_1002193,
		    "%s: cannot stat local fd: %s", diag, strerror(errno));
		if (res.recv.e.dst_errcode == GFARM_ERR_NO_ERROR)
			res.recv.e.dst_errcode = GFARM_ERR_UNKNOWN;
	}
	if (rep->handling_cksum_protocol) {
		e = gfm_async_client_send_request(bc_conn, async, diag,
		    gfm_async_client_replication_result,
		    gfm_async_client_replication_free,
		    /* rep */ NULL,
		    GFM_PROTO_REPLICATION_CKSUM_RESULT, "llliilsbi",
		    rep->ino, rep->gen, (gfarm_int64_t)rep->pid,
		    res.recv_cksum.e.src_errcode, res.recv_cksum.e.dst_errcode,
		    (gfarm_int64_t)st.st_size, rep->cksum_type,
		    res.recv_cksum.cksum_len, res.recv_cksum.cksum,
		    res.recv_cksum.cksum_result_flags);
	} else {
		e = gfm_async_client_send_request(bc_conn, async, diag,
		    gfm_async_client_replication_result,
		    gfm_async_client_replication_free,
		    /* rep */ NULL,
		    GFM_PROTO_REPLICATION_RESULT, "llliil",
		    rep->ino, rep->gen, (gfarm_int64_t)rep->pid,
		    res.recv.e.src_errcode, res.recv.e.dst_errcode,
		    (gfarm_int64_t)st.st_size);
	}
	close(rep->pipe_fd);
	close(rep->file_fd);
	if ((rv = waitpid(rep->pid, &status, 0)) == -1)
		gflog_warning(GFARM_MSG_1002303,
		    "replication(%lld, %lld): child %d: %s",
		    (long long)rep->ino, (long long)rep->gen, (int)rep->pid,
		    strerror(errno));
	else
		gfarm_iostat_clear_id(rep->pid, 0);

	if (gfs_client_is_connection_error(res.recv.e.src_errcode))
		gfs_client_purge_from_cache(rep->src_gfsd);
	gfs_client_connection_free(rep->src_gfsd);

	rep->ongoing_prev->ongoing_next = rep->ongoing_next;
	rep->ongoing_next->ongoing_prev = rep->ongoing_prev;

	rep = rep->q_next;
	free(qd->head->cksum_type);
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
#define	REP_FD_START 2 /* fds[0]: gfmd_fd, fds[1]: failover_notify_recv_fd */
	int gfmd_fd, i, n, n_alloc;

	static int nfds = 0;
	static struct pollfd *fds = NULL;
	static struct replication_request **fd_rep_map = NULL;

	for (;;) {
		gfmd_fd = gfp_xdr_fd(conn);
		n = REP_FD_START;
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
		fds[1].fd = failover_notify_recv_fd;
		fds[1].events = POLLIN;
		fd_rep_map[1] = NULL;
		n = REP_FD_START;
		for (rep = ongoing_replications.ongoing_next;
		    rep != &ongoing_replications; rep = rep->ongoing_next) {
			fds[n].fd = rep->pipe_fd;
			fds[n].events = POLLIN;
			fd_rep_map[n] = rep;
			++n;
		}

		nfound =
		    poll(fds, n, gfarm_metadb_heartbeat_interval * 2 *
		    GFARM_SECOND_BY_MILLISEC);
		if (nfound == 0) {
			gflog_error(GFARM_MSG_1003671,
			    "back channel: gfmd is down");
			return (0); /* reconnect gfmd */
		}
		if (nfound < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			fatal_errno(GFARM_MSG_1003672, "back channel poll");
		}
		for (i = REP_FD_START; i < n; i++) {
			if (fds[i].revents == 0)
				continue;
			e = replication_result_notify(conn, async,
			    fd_rep_map[i]->q);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1003673,
				    "back channel: "
				    "communication error: %s",
				    gfarm_error_string(e));
				return (0); /* reconnect gfmd */
			}

		}
		if (fds[1].revents != 0) { /* check failover_notify_recv_fd */
			gflog_info(GFARM_MSG_1004182,
			    "back channel: failover notified");
			failover_notified(1, "back channel gfsd");
			return (0); /* reconnect gfmd */
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
		FD_SET(failover_notify_recv_fd, &fds);
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
			return (0); /* reconnect gfmd */
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
					return (0); /* reconnect gfmd */
				}
			} else {
				next = rep->ongoing_next;
			}
		}
		if (FD_ISSET(failover_notify_recv_fd, &fds)) {
			gflog_info(GFARM_MSG_1004183,
			    "back channel: failover notified");
			failover_notified(1, "back channel gfsd");
			return (0); /* reconnect gfmd */
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
			free(rep->cksum_type);
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
	gfarm_int32_t gfmd_knows_me, rv, request, last_request = -1;

	static int hack_to_make_cookie_not_work = 0; /* XXX FIXME */

	for (;;) {
		if ((e = connect_gfm_server("back channel"))
		    != GFARM_ERR_NO_ERROR)
			fatal(GFARM_MSG_1003364, "die");
		back_channel = gfm_server;
		bc_conn = gfm_client_connection_conn(gfm_server);
		(void)gfarm_proctitle_set("back_channel %s:%d",
		    gfm_client_hostname(back_channel),
		    gfm_client_port(back_channel));

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
		(void)gfarm_sockbuf_apply_limit(
		    gfm_client_connection_fd(back_channel),
		    SO_RCVBUF, gfarm_spool_server_back_channel_rcvbuf_limit,
		    "spool_server_back_channel_rcvbuf_limit");

		/* create another gfmd connection for a foreground channel */
		gfm_server = NULL;
		if ((e = connect_gfm_server("back channel supplement"))
		    != GFARM_ERR_NO_ERROR)
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
				    bc_conn, xid, size, 0);
				break;
			case GFS_PROTO_STATUS2:
				e = gfs_async_server_status(
				    bc_conn, xid, size, 1 /* use flags */);
				break;
			case GFS_PROTO_REPLICATION_REQUEST:
				e = gfs_async_server_replication_request(
				    bc_conn, gfm_client_username(back_channel),
				    xid, size, 0);
				break;
			case GFS_PROTO_REPLICATION_CKSUM_REQUEST:
				e = gfs_async_server_replication_request(
				    bc_conn, gfm_client_username(back_channel),
				    xid, size, 1);
				break;
			default:
				gflog_error(GFARM_MSG_1000566,
				    "(back channel) unknown request %d "
				    "(xid:%d size:%d), skip",
				    (int)request, (int)xid, (int)size);
				gflog_info(GFARM_MSG_1005228,
				    "last request: %d", (int)last_request);
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
			last_request = request;
		}

		kill_pending_replications();

		/* free the foreground channel */
		gfm_client_connection_free(gfm_server);

		gfp_xdr_async_peer_free(async, bc_conn);

reconnect_backchannel:
		gfm_server = back_channel;
		free_gfm_server();
	}
}

static void
start_back_channel_server(void)
{
	pid_t pid;

	pid = do_fork(type_back_channel);
	switch (pid) {
	case 0:
		back_channel_server();
		/*NOTREACHED*/
	case -1:
		gflog_error_errno(GFARM_MSG_1000567, "fork");
		/*FALLTHROUGH*/
	default:
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
	fprintf(stderr, "\t-F <log-file>\n");
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
	/* specify static, to shut up valgrind */
	static struct sockaddr_in *self_sockaddr_array;

	struct sockaddr_in client_addr;
	struct sockaddr_un client_local_addr;
	gfarm_error_t e, e2;
	char *config_file = NULL, *pid_file = NULL;
	char *local_gfsd_user;
	struct gfarm_host_info self_info;
	struct passwd *gfsd_pw;
	FILE *pid_fp = NULL;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	int syslog_level = -1;
	char *syslog_file = NULL;
	struct in_addr *self_addresses, listen_address;
	int table_size, self_addresses_count, ch, i, nfound, max_fd, p;
	int save_errno;
	struct sigaction sa;
	fd_set requests;
	struct stat sb;
	int spool_check_level = 0;
	int is_root = geteuid() == 0;

	/*
	 * gfarm_proctitle_set() breaks argv[] contents on platforms which use
	 * the PROCTITLE_USE_ARGV_ENVIRON_SPACE implemenation.
	 * thus, gfarm_proctitle_init() has to called before saving
	 * a pointer to argv[] (including optarg).
	 */
	save_errno = gfarm_proctitle_init(program_name, argc, &argv);
	if (save_errno != 0)
		fprintf(stderr, "%s: setproctitle: %s", program_name,
		    strerror(save_errno));

	if (argc >= 1)
		program_name = basename(argv[0]);
	gflog_set_identifier(program_name);

	while ((ch = getopt(argc, argv, "F:L:P:cdf:h:l:r:s:v")) != -1) {
		switch (ch) {
		case 'F':
			syslog_file = optarg;
			break;
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
			e = gfarm_parse_set_spool_root(optarg);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_fatal(GFARM_MSG_1000586, "%s",
				    gfarm_error_string(e));
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

	e = gfarm_server_initialize_for_gfsd(config_file, &argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_server_initialize: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}
#ifdef HAVE_INFINIBAND
	gfs_ib_rdma_initialize(0);
#endif

	argc -= optind;
	argv += optind;

	for (i = 0; i < GFARM_SPOOL_ROOT_NUM; ++i) {
		int s;

		if (gfarm_spool_root[i] == NULL) {
			gfarm_spool_root_num = i;
			break;
		}
		s = strlen(gfarm_spool_root[i]);
		gfarm_spool_root_len[i] = s;
		if (gfarm_spool_root_len_max < s)
			gfarm_spool_root_len_max = s;
	}
	if (gfarm_spool_root_num == 0)
		gflog_fatal(GFARM_MSG_1004483, "no spool directory");
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
	for (i = 0; i < gfarm_spool_root_num; ++i) {
		if (gfarm_spool_root[i] == NULL)
			break;
		if (stat(gfarm_spool_root[i], &sb) == -1)
			gflog_fatal_errno(GFARM_MSG_1000588, "%s",
			    gfarm_spool_root[i]);
		else if (!S_ISDIR(sb.st_mode))
			gflog_fatal(GFARM_MSG_1000589, "%s: %s",
			    gfarm_spool_root[i],
			    gfarm_error_string(GFARM_ERR_NOT_A_DIRECTORY));
	}
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
		if (syslog_file != NULL) {
			if (gflog_file_open(syslog_file) == NULL)
				gflog_fatal_errno(GFARM_MSG_1005103,
				    "%s", syslog_file);
		} else
			gflog_syslog_open(LOG_PID, syslog_facility);
		if (gfarm_daemon(0, 0) == -1)
			gflog_warning_errno(GFARM_MSG_1002203, "daemon");
	}

	/* We do this after calling gfarm_daemon(), because it changes pid. */
	master_gfsd_pid = getpid();
	if (debug_mode) { /* for FAILOVER_SIGNAL */
		/*
		 * if it's not debug_mode, setsid(2) in gfarm_daemon() has
		 * already created a new process group, and setpgid(2)
		 * returns EPERM in that case on Linux and FreeBSD,
		 * but not on NetBSD.
		 */
		if (setpgid(0, 0) == -1)
			gflog_fatal_errno(GFARM_MSG_1004184, "setpgid()");
	}

	sa.sa_handler = failover_handler;
	if (sigemptyset(&sa.sa_mask) == -1)
		gflog_fatal_errno(GFARM_MSG_1004185, "sigemptyset()");
	sa.sa_flags = SA_RESTART;
	if (sigaction(FAILOVER_SIGNAL, &sa, NULL) == -1)
		gflog_fatal_errno(GFARM_MSG_1004186, "sigaction(FAILOVER)");

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

	gfarm_set_auth_id_role(GFARM_AUTH_ID_ROLE_SPOOL_HOST);
	e = connect_gfm_server_at_first("listener");
	if (e != GFARM_ERR_NO_ERROR)
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
		    "register this node in Gfarm metadata server, died: %s",
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
		    "register this node in Gfarm metadata server, died: %s",
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
		len += 1 + IOSTAT_PATH_NAME_MAX + 1;	/* "-NAME\0" */
		GFARM_MALLOC_ARRAY(iostat_dirbuf, len);
		if (iostat_dirbuf == NULL)
			gflog_fatal(GFARM_MSG_1003676, "iostat_dirbuf:%s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));

		iostat_dirbuf[len - 1] = 0;
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

	gfsd_readonly_config_init(self_info.port);

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
	if (max_fd >= FD_SETSIZE)
		accepting_fatal(GFARM_MSG_1000597,
		    "too big socket file descriptor: %d", max_fd);

	if (seteuid(gfsd_uid) == -1) {
		save_errno = errno;
		if (geteuid() == 0)
			gflog_error(GFARM_MSG_1002403,
			    "seteuid(%ld): %s",
			    (long)gfsd_uid, strerror(save_errno));
	}

	/* call before spool check to get ringbuf from spool_check (not-yet) */
	write_verify_state_init();

	/* spool check */
	gfsd_spool_check(); /* should be after write_verify_state_init() */

	/* XXX - kluge for gfrcmd (to mkdir HOME....) for now */
	/* XXX - kluge for GFS_PROTO_STATFS for now */
	if (chdir(gfarm_spool_root[0]) == -1)
		gflog_fatal_errno(GFARM_MSG_1000598, "chdir(%s)",
		    gfarm_spool_root[0]);

	/*
	 * We don't want SIGPIPE, but want EPIPE on write(2)/close(2).
	 */
	gfarm_sigpipe_ignore();

	/* call before start_back_channel_server() */
	if (gfarm_write_verify)
		start_write_verify_controller();
	write_verify_state_free(); /* type_listener doesn't need this */

	start_back_channel_server();

	table_size = FILE_TABLE_LIMIT;
	if (gfarm_limit_nofiles(&table_size) == 0)
		gflog_info(GFARM_MSG_1003515, "max descriptors = %d",
		    table_size);
	file_table_init(table_size);

	gfsd_setup_iostat("gfsd", gfarm_iostat_max_client);

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
			save_errno = errno;
			if (got_sigchld)
				clear_child();
			if (nfound == 0 || save_errno == EINTR ||
			    save_errno == EAGAIN)
				continue;
			errno = save_errno;
			fatal_errno(GFARM_MSG_1000600, "select");
		}

		if (FD_ISSET(accepting.tcp_sock, &requests)) {
			start_server(accepting.tcp_sock,
			    (struct sockaddr*)&client_addr,sizeof(client_addr),
			    (struct sockaddr*)&client_addr, NULL, &accepting);
		}
		for (i = 0; i < accepting.local_socks_count; i++) {
			if (FD_ISSET(accepting.local_socks[i].sock, &requests))
				start_server(accepting.local_socks[i].sock,
				    (struct sockaddr *)&client_local_addr,
				    sizeof(client_local_addr),
				    (struct sockaddr*)&self_sockaddr_array[i],
				    canonical_self_name,
				    &accepting);
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
