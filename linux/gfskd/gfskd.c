#include <sys/param.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <gfarm/gfarm.h>
#include "context.h"
#include "gfm_client.h"
#include "gfs_client.h"
#include "metadb_server.h"
#include "config.h"
#include "gfpath.h"
#include "lookup.h"
#include "gfsk_if.h"
#include "gfskd.h"

int gfskd_term;
static char  mount_buf[GFSK_BUF_SIZE + 128];
static struct gfsk_mount_data *mdatap = (struct gfsk_mount_data *)mount_buf;

struct gfskd_param {
	unsigned long	mnt_flags;
	const char *mnt_dev;
	const char *mnt_point;
	const char *conf_path;
	const char *key_path;
	uid_t	local_uid;
	int	uid_set;
	const char *local_name;
	const char *dev_name;
	int foreground;
	int debug;
	int use_syslog;
	char *facility;
	char *loglevel;
	int auto_uid_min;
	int auto_uid_max;
	int auto_gid_min;
	int auto_gid_max;
	int genuine_nlink;
};

char *program_name = "gfskd";

static char GFARM2FS_SYSLOG_FACILITY_DEFAULT[] = "local0";
static char GFARM2FS_SYSLOG_PRIORITY_DEFAULT[] = "notice";
static char GFARM2FS_SYSLOG_PRIORITY_DEBUG[] = "debug";

static gfarm_error_t
gfskd_connection_acquire(const char *path,
		struct gfm_connection **gfm_serverp) {
	char *hostname;
	int port;
	char *user;
	gfarm_error_t error;

	if ((error = gfarm_get_hostname_by_url(path, &hostname, &port))
			!= GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004776,
		    "gfarm_get_hostname_by_url0 failed: %s",
		    gfarm_error_string(error));
		return (error);
	}
	if ((error = gfarm_get_global_username_by_host(
		hostname, port, &user)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004777,
		    "gfarm_get_global_username_by_host: %s",
		    gfarm_error_string(error));
		return (error);
	}
	error = gfm_client_connection_acquire(hostname, port, user,
		gfm_serverp);
	return (error);
}

static gfarm_error_t
gfmd_connect(struct gfsk_req_connect *req, struct gfsk_rpl_connect *rpl,
		struct gfm_connection **gfm_serverp)
{
	gfarm_error_t	error;
	struct gfm_connection *gfm_server;

	error = gfskd_connection_acquire(GFARM_PATH_ROOT, gfm_serverp);
	if (error != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004778,
		    "connecting to gfmd at %s:%d: %s\n",
		    gfarm_ctxp->metadb_server_name,
		    gfarm_ctxp->metadb_server_port,
		    gfarm_error_string(error));
		return (error);
	}
	gfm_server = *gfm_serverp;
	strcpy(rpl->r_hostname,
		gfm_client_connection_get_real_server(gfm_server)->name);
	rpl->r_fd = gfm_client_connection_fd(gfm_server);
	return (error);
}
gfarm_error_t
gfskd_req_connect_gfmd(struct gfskd_req_t *reqctx, void *arg)
{
	gfarm_error_t	error = GFARM_ERR_NO_ERROR;
	struct gfsk_req_connect *req = (struct gfsk_req_connect *)arg;
	struct gfsk_rpl_connect	rpl;
	struct gfm_connection *gfm_server;
	int	err;

	switch (fork()) {
	case 0:
		break;
	case -1:
		err = errno;
		error = gfarm_errno_to_error(err);
		gflog_debug(GFARM_MSG_1004779, "fork: %s", strerror(err));
		/* fall through */
	default:
		return (error);
	}
	uid_t orguid = getuid();
	setuid(req->r_uid);
	gfarm_set_local_user_for_this_uid(req->r_uid);
	err = gfmd_connect(req, &rpl, &gfm_server);
	if (err != GFARM_ERR_NO_ERROR) {
		setuid(orguid);
		gfarm_set_local_user_for_this_uid(orguid);
	}
	gflog_debug(GFARM_MSG_1004780, "uid=%d, err=%d", req->r_uid, err);

	if (err) {
		gfskd_send_reply(reqctx, err, NULL, 0);
	} else {
		gfskd_send_reply(reqctx, 0, &rpl, sizeof(rpl));
		gfm_client_purge_from_cache(gfm_server);
		gfm_client_connection_free(gfm_server);
	}
	exit(0);
}
static gfarm_error_t
gfsd_connect(struct gfsk_req_connect *req, struct gfsk_rpl_connect *rpl,
		struct gfs_connection **gfs_serverp)
{
	gfarm_error_t	error;
	struct gfs_connection *gfs_server;
	struct sockaddr_in in;

	in.sin_family = AF_INET;
	in.sin_port = htons(req->r_port);
	in.sin_addr.s_addr = htonl(req->r_v4addr);
	error = gfs_client_connection_acquire(req->r_hostname, req->r_global,
		(struct sockaddr *)&in, gfs_serverp);

	if (error != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004781,
		    "connecting to gfsd at %s:%d: %s\n",
		    req->r_hostname, req->r_port,
		    gfarm_error_string(error));
		return (error);
	}
	gfs_server = *gfs_serverp;
	rpl->r_fd = gfs_client_connection_fd(gfs_server);
	return (error);
}
gfarm_error_t
gfskd_req_connect_gfsd(struct gfskd_req_t *reqctx, void *arg)
{
	gfarm_error_t	error = GFARM_ERR_NO_ERROR;
	struct gfsk_req_connect *req = (struct gfsk_req_connect *)arg;
	struct gfsk_rpl_connect	rpl;
	struct gfs_connection *gfs_server;
	int	err;

	switch (fork()) {
	case 0:
		break;
	case -1:
		err = errno;
		error = gfarm_errno_to_error(err);
		gflog_debug(GFARM_MSG_1004782, "fork: %s", strerror(err));
		/* fall through */
	default:
		return (error);
	}
	uid_t orguid = getuid();
	setuid(req->r_uid);
	gfarm_set_local_user_for_this_uid(req->r_uid);
	err = gfsd_connect(req, &rpl, &gfs_server);
	if (err != GFARM_ERR_NO_ERROR) {
		setuid(orguid);
		gfarm_set_local_user_for_this_uid(orguid);
	}
	gflog_debug(GFARM_MSG_1004783, "uid=%d, err=%d", req->r_uid, err);

	if (err) {
		gfskd_send_reply(reqctx, err, NULL, 0);
	} else {
		gfskd_send_reply(reqctx, 0, &rpl, sizeof(rpl));
		gfs_client_purge_from_cache(gfs_server);
		gfs_client_connection_free(gfs_server);
	}
	exit(0);
}
void
gfskd_set_term(gfarm_error_t error)
{
	gfskd_term = 1;
}
gfarm_error_t
gfskd_req_term(struct gfskd_req_t *req, void *arg)
{
	gfskd_set_term(0);
	return (GFARM_ERR_NO_ERROR);
}
static gfarm_error_t
set_signal_handler(int sig, void (*handler)(int))
{
	gfarm_error_t error;
	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = handler;
	sigemptyset(&(sa.sa_mask));
	sa.sa_flags = 0;

	if (sigaction(sig, &sa, NULL) == -1) {
		error = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1004784,
			"sigaction, %s", gfarm_error_string(error));
		return (error);
	}
	return (0);
}

gfarm_error_t
gfskd_main(struct gfsk_mount_data *mdatap, struct gfskd_param *paramsp)
{
	gfarm_error_t error;
	if (!paramsp->foreground) {
		daemon(0, 0);
	}
	if ((error = set_signal_handler(SIGPIPE, SIG_IGN))
			|| (error = set_signal_handler(SIGCHLD, SIG_IGN))) {
		return (error);
	}
	error = gfskd_loop(mdatap->m_dfd, GFSK_BUF_SIZE);
	return (error);
}

static void
usage(const char *progname, struct gfskd_param *paramsp)
{
	fprintf(stderr,
"usage: %s special mountpoint [options]\n"
"\n"
"general options:\n"
"    -o opt,[opt...]         mount options\n"
"    -h   --help             print help\n"
"    -V   --version          print version\n"
"\n"
"GFARM2FS options:\n"
"    -o conf_path=path       gfarm config file path (default: %s)\n"
"    -o key_path=path        shared key file path (default: %s)\n"
"    -o uid=uid              local user uid (default: from luser)\n"
"    -o luser=name           local user name (default: from current user)\n"
"    -o device=name          device name (default: %s)\n"
"    -o syslog=facility      syslog facility (default: %s)\n"
"    -o loglevel=priority    syslog priority level (default: %s)\n"
"    -o auto_uid_min=N       minimum number of auto uid (default: %d)\n"
"    -o auto_uid_max=N       maximum number of auto uid (default: %d)\n"
"    -o auto_gid_min=N       minimum number of auto gid (default: %d)\n"
"    -o auto_gid_max=N       maximum number of auto gid (default: %d)\n"
"    -o on_demand_replication        set on-demand replication (default: no)\n"
"    -o call_ftruncate       call ftruncate instead rpc (default: rpc)\n"
		"\n", progname,
		paramsp->conf_path,
		paramsp->key_path,
		paramsp->dev_name,
		GFARM2FS_SYSLOG_FACILITY_DEFAULT,
		GFARM2FS_SYSLOG_PRIORITY_DEFAULT,
		paramsp->auto_uid_min,
		paramsp->auto_uid_max,
		paramsp->auto_gid_min,
		paramsp->auto_gid_max);
}

static int
gfskd_map_file(struct gfsk_fbuf *fbufp, const char *map_file,
					const char *name)
{
	gfarm_error_t e;
	int	fd = -1;

	if (fbufp->f_name.d_buf) {
		fprintf(stderr, "%s:%s map already registered", __func__
			, name);
		e = EEXIST;
	} else if ((fd = open(map_file, O_RDONLY)) < 0) {
		e = errno;
		fprintf(stderr, "%s:open %s: %s\n", __func__, map_file,
			strerror(errno));
	} else if ((fbufp->f_buf.d_len = lseek(fd, 0, SEEK_END)) < 0) {
		e = errno;
		fprintf(stderr, "%s:seek %s: %s\n", __func__, map_file,
			strerror(errno));
	} else if ((fbufp->f_buf.d_buf = mmap(NULL, fbufp->f_buf.d_len,
				PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		e = errno;
		fprintf(stderr, "%s:map %s: %s\n", __func__, map_file,
			strerror(errno));
	} else {
		fbufp->f_name.d_buf = (char *)map_file;
		fbufp->f_name.d_len = strlen(map_file) + 1;
		e = 0;
	}
	if (fd >= 0) {
		close(fd);
	}
	if (e) {
		gfskd_set_term(e);
	}
	return (e);
}

static void
gfskd_ug_maps_enter(const char *hostname, int port, int is_user,
	const char *map_file)
{
	gfskd_map_file(&mdatap->m_fbuf[is_user ? GFSK_FBUF_USER_MAP
			: GFSK_FBUF_GROUP_MAP], map_file,
			  is_user ? "user" : "group");
}
static gfarm_error_t
gfskd_mount(struct gfsk_mount_data *mdatap, struct gfskd_param *paramsp)
{
	gfarm_error_t error;
	struct gfm_connection *gfm_server;
	struct stat stbuf;

	if (stat(paramsp->mnt_point, &stbuf) < 0) {
		error = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1004785,
		    "stat %s, %s", paramsp->mnt_point,
		    gfarm_error_string(error));
		return (error);
	}
	if (paramsp->key_path) {
		if (setenv("GFARM_SHARED_KEY", paramsp->key_path, 1)) {
			gflog_error(GFARM_MSG_1004786,
			    "setenv fail for '%s'", paramsp->key_path);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	error = gfskd_connection_acquire(GFARM_PATH_ROOT, &gfm_server);
	if (error != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004787,
		    "connecting to gfmd at %s:%d: %s\n",
		    gfarm_ctxp->metadb_server_name,
		    gfarm_ctxp->metadb_server_port,
		    gfarm_error_string(error));
		return (error);
	}
	unsetenv("GFARM_SHARED_KEY");

	mdatap->m_version = GFSK_VER;
	mdatap->m_uid = paramsp->local_uid;
	mdatap->m_mfd = gfm_client_connection_fd(gfm_server);
	mdatap->m_dfd = open(paramsp->dev_name, O_RDWR);
	if (mdatap->m_dfd < 0) {
		error = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1004788,
		    "open %s, %s", paramsp->dev_name,
		    gfarm_error_string(error));
		goto out_conn;
	}
	strcpy(mdatap->m_host,
		gfm_client_connection_get_real_server(gfm_server)->name);
	if ((error = gfskd_map_file(&mdatap->m_fbuf[GFSK_FBUF_CONFIG],
		paramsp->conf_path, "config"))) {
		goto out_conn;
	}
	mdatap->m_fbuf[GFSK_FBUF_CONFIG].f_name.d_buf = (char *)GFARM_CONFIG;
	mdatap->m_fbuf[GFSK_FBUF_CONFIG].f_name.d_len =
		strlen(mdatap->m_fbuf[GFSK_FBUF_CONFIG].f_name.d_buf) + 1;

	if (mount(paramsp->mnt_dev, paramsp->mnt_point, GFARM_FSNAME,
		paramsp->mnt_flags|MS_NODEV, mdatap) < 0) {
		error = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1004789,
		    "mount %s, %s", paramsp->mnt_point,
		    gfarm_error_string(error));
		goto out_dev;
	}
	set_signal_handler(SIGCHLD, SIG_IGN);
	switch (fork()) {
	case 0:
		execl("/bin/mount", "/bin/mount", "--no-canonicalize", "-i",
			"-f", "-t", GFARM_FSNAME, "-o", mdatap->m_opt,
			paramsp->mnt_dev, paramsp->mnt_point, NULL);
		error = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1004790,
		    "/bin/mount fail,%s", gfarm_error_string(error));
		exit(1);
	case -1:
		break;
	default:
		break;
	}
out_dev:
	if (error)
		close(mdatap->m_mfd);
out_conn:
	gfm_client_purge_from_cache(gfm_server);
	gfm_client_connection_free(gfm_server);
	return (error);
}

static gfarm_error_t
gfskd_initialize(struct gfskd_param *paramsp)
{
	gfarm_error_t e;
#ifdef HAVE_GSI
	enum gfarm_auth_method auth_method;
	int saved_auth_verb;
#endif

	e = gfarm_context_init();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004791,
			"gfarm_context_init failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	gflog_initialize();

	e = gfarm_set_local_user_for_this_uid(paramsp->local_uid);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004792,
		    "gfskd_set_local_user_for_this_local_account() failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (setenv("GFARM_CONFIG_FILE", "/dev/null", 1)) { /* empty file */
		gflog_error(GFARM_MSG_1004793,
		    "setenv fail for '/dev/null'");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (paramsp->conf_path) {
		gfarm_config_set_filename((char *)paramsp->conf_path);
	}

	gfarm_ug_maps_notify = gfskd_ug_maps_enter;

	e = gfarm_config_read();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004794,
		    "gfarm_config_read() failed: %s", gfarm_error_string(e));
		return (e);
	}
	gfarm_ctxp->gfmd_connection_cache = 0; /* num_cachep is pointer */

	gfarm_ug_maps_notify = NULL;

	gfarm_setup_debug_command();

#ifdef HAVE_GSI
	/* Force to display verbose error messages. */
	saved_auth_verb = gflog_auth_set_verbose(1);
	(void)gfarm_gsi_client_initialize();
#endif
	return (GFARM_ERR_NO_ERROR);
}
static void
gfskd_have_val(char *name, char *val)
{
	if (!val) {
		fprintf(stderr, "option '%s' need value\n", name);
		exit(1);
	}
}
static long
gfskd_have_int(char *name, char *val)
{
	long	n;
	char	*ep;
	gfskd_have_val(name, val);

	n = strtol((const char *)val, &ep, 0);
	if (*ep) {
		fprintf(stderr, "option '%s=%s' is not number\n", name, val);
		exit(1);
	}
	return (n);
}

#define GFSKD_OPT_ADD 1
#define GFSKD_OPT_THROUGH 0
static int
gfskd_opt_one(char *name, char *val, struct gfskd_param *paramsp)
{
	int ret = GFSKD_OPT_ADD;

	if (!strcmp(name, "help")) {
		usage(program_name, paramsp);
		exit(0);
	} else if (!strcmp(name, "version")) {
		fprintf(stderr, "GFSKD version #%x\n", GFSK_VER);
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "debug")) {
		paramsp->debug = 1;
	} else if (!strcmp(name, "foreground")) {
		paramsp->foreground = 1;
	} else if (!strcmp(name, "key_path")) {
		gfskd_have_val(name, val);
		paramsp->key_path = val;
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "conf_path")) {
		gfskd_have_val(name, val);
		paramsp->conf_path = val;
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "uid")) {
		paramsp->local_uid = (uid_t)gfskd_have_int(name, val);
		paramsp->uid_set = 1;
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "device")) {
		gfskd_have_val(name, val);
		paramsp->dev_name = val;
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "luser")) {
		gfskd_have_val(name, val);
		paramsp->local_name = val;
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "facility")) {
		gfskd_have_val(name, val);
		paramsp->facility = val;
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "loglevel")) {
		gfskd_have_val(name, val);
		paramsp->loglevel = val;
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "use_syslog")) {
		paramsp->use_syslog = 1;
		ret = GFSKD_OPT_THROUGH;
	} else if (!strcmp(name, "auto_uid_min")) {
		paramsp->auto_uid_min = gfskd_have_int(name, val);
	} else if (!strcmp(name, "auto_uid_max")) {
		paramsp->auto_uid_max = gfskd_have_int(name, val);
	} else if (!strcmp(name, "auto_gid_min")) {
		paramsp->auto_gid_min = gfskd_have_int(name, val);
	} else if (!strcmp(name, "auto_gid_max")) {
		paramsp->auto_gid_max = gfskd_have_int(name, val);
	} else if (!strcmp(name, "on_demand_replication")) {
		;
	} else if (!strcmp(name, "call_ftruncate")) {
		;
	} else if (!strcmp(name, "rw"))
		paramsp->mnt_flags &= ~MS_RDONLY;
	else if (!strcmp(name, "ro"))
		paramsp->mnt_flags |= MS_RDONLY;
#ifdef NOT_SUPPORTED_YET
	else if (!strcmp(name, "suid"))
		paramsp->mnt_flags |= MS_NOSUID;
	else if (!strcmp(name, "nosuid"))
		paramsp->mnt_flags &= ~MS_NOSUID;
	else if (!strcmp(name, "dev"))
		paramsp->mnt_flags |= MS_NODEV;
	else if (!strcmp(name, "nodev"))
		paramsp->mnt_flags &= ~MS_NODEV;
	else if (!strcmp(name, "exec"))
		paramsp->mnt_flags |= MS_NOEXEC;
	else if (!strcmp(name, "noexec"))
		paramsp->mnt_flags &= ~MS_NOEXEC;
	else if (!strcmp(name, "async"))
		paramsp->mnt_flags &= ~MS_SYNCHRONOUS;
	else if (!strcmp(name, "sync"))
		paramsp->mnt_flags |= MS_SYNCHRONOUS;
	else if (!strcmp(name, "atime"))
		paramsp->mnt_flags &= ~MS_NOATIME;
	else if (!strcmp(name, "noatime"))
		paramsp->mnt_flags |= MS_NOATIME;
#ifdef MS_DIRSYNC
	else if (!strcmp(name, "dirsync"))
		paramsp->mnt_flags |= MS_DIRSYNC;
#endif
#endif /* NOT_SUPPORTED_YET */
	else {
		fprintf(stderr, "unknown param %s %s\n", name, val ? val : "");
	}

	return (ret);
}
static int
gfskd_opt_parse(char *opts, struct gfsk_mount_data *mdatap,
	struct gfskd_param *paramsp)
{
	char *name, *val, *next, *eq, *mend;
	char *cp = 0;

	if (mdatap->m_optlen)
		cp = &mdatap->m_opt[mdatap->m_optlen];
	mend = mdatap->m_opt + GFSK_OPTLEN_MAX - 1;

	for (name = opts; name && *name; name = next) {
		next = strchr(name, ',');
		if (next) {
			*next = 0;
			next++;
		}
		eq = strchr(name, '=');
		if (eq) {
			*eq = 0;
			val = eq + 1;
		} else
			val = NULL;
		if (gfskd_opt_one(name, val, paramsp) == GFSKD_OPT_ADD) {
			if (eq)
				*eq = '=';
			if (cp) {
				*cp++ = ',';
			} else {
				cp = mdatap->m_opt;
			}
			strcat(cp, name);
			cp += strlen(name);
			if (cp >= mend) {
				fprintf(stderr, "too many option %s\n", name);
				exit(1);
			}
		}
	}
	mdatap->m_optlen = cp - mdatap->m_opt;
	return (0);
}

static int
gfskd_arg_parse(int argc, char *argv[],
	struct gfsk_mount_data *mdatap, struct gfskd_param *paramsp)
{
	struct passwd *pwd;
	int	i;

	if (argc < 3) {
		fprintf(stderr, "Too few options %s\n", argv[0]);
		usage(argv[0], paramsp);
		exit(1);
	}
	if (argv[1][0] != '-' && argv[2][0] != '-') {
		paramsp->mnt_dev = argv[1];
		paramsp->mnt_point = argv[2];
		i = 3;
	} else
		i = 1;
	for (; i < argc; i++) {
		if (argv[i][0] != '-') {
			if (i + 2 == argc && argv[i+1][0] != '-') {
				paramsp->mnt_dev = argv[i];
				paramsp->mnt_point = argv[i+1];
				break;
			} else {
				fprintf(stderr, "unknown mount point %s\n",
						argv[i]);
				usage(argv[0], paramsp);
				exit(1);
			}
		}
		switch (argv[i][1]) {
		case 'h':
			usage(argv[0], paramsp);
			exit(0);
		case 'V':
			fprintf(stderr, "GFSKD version #%x\n", GFSK_VER);
			break;
		case 'o':
			if (i+1 == argc) {
				fprintf(stderr, "option string is needed\n");
				usage(argv[0], paramsp);
				exit(1);
			}
			gfskd_opt_parse(argv[i+1], mdatap, paramsp);
			i++;
			break;
		default:
			fprintf(stderr, "unknown option %s, ignore\n", argv[i]);
			break;
		}
	}
	if (!paramsp->mnt_point) {
		fprintf(stderr, "mount point not specified\n");
		usage(argv[0], paramsp);
		exit(1);
	}
	if (!paramsp->local_name && !paramsp->uid_set) {
		fprintf(stderr, "no local user is specified\n");
		usage(argv[0], paramsp);
		exit(1);
	}
	if (!paramsp->uid_set) {
		if (!(pwd = getpwnam((const char *) paramsp->local_name))) {
			fprintf(stderr, "invalid local user '%s'\n",
				paramsp->local_name);
			exit(1);
		}
		paramsp->local_uid = pwd->pw_uid;
	} else {
		if (!(pwd = getpwuid(paramsp->local_uid))) {
			fprintf(stderr, "invalid local user '%d'\n",
				paramsp->local_uid);
			exit(1);
		}
	}
	return (0);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	int syslog_priority;
	int syslog_facility = -1;

	struct gfskd_param params = {
		.mnt_flags = 0,
		.mnt_point = NULL,
		.conf_path = GFARM_CONFIG,
		.key_path = NULL,
		.local_uid = 0,
		.uid_set = 0,
		.local_name = NULL,
		.dev_name = "/dev/gfarm",
		.foreground = 0,
		.debug = 0,
		.use_syslog = 1,
		.facility = NULL,
		.loglevel = NULL,
		.auto_uid_min = 70000,
		.auto_uid_max = 79999,
		.auto_gid_min = 70000,
		.auto_gid_max = 79999,
	};

	mdatap = (struct gfsk_mount_data *)mount_buf;

	gfskd_arg_parse(argc, argv, mdatap, &params);

	umask(0);
	e = gfskd_initialize(&params);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", *argv, gfarm_error_string(e));
		exit(1);
	}

	if (params.foreground || params.debug) {
		params.use_syslog = 0; /* use stderr */
		if (params.loglevel == NULL)
			params.loglevel = GFARM2FS_SYSLOG_PRIORITY_DEBUG;
	}

	if (params.loglevel == NULL)
		params.loglevel = GFARM2FS_SYSLOG_PRIORITY_DEFAULT;
	syslog_priority = gflog_syslog_name_to_priority(params.loglevel);
	if (syslog_priority == -1) {
		fprintf(stderr, "invalid loglevel: `%s'\n", params.loglevel);
		fprintf(stderr, "see `%s -h' for usage\n", program_name);
		exit(1);
	}
	gflog_set_priority_level(syslog_priority);

	gflog_set_identifier(program_name);
	gflog_auth_set_verbose(1);

	if (params.use_syslog) {
		if (params.facility == NULL)
			params.facility = GFARM2FS_SYSLOG_FACILITY_DEFAULT;
		syslog_facility = gflog_syslog_name_to_facility(
			params.facility);
		if (syslog_facility == -1) {
			fprintf(stderr, "invalid facility: `%s'\n",
				params.facility);
			fprintf(stderr, "see `%s -h' for usage\n",
				program_name);
			exit(1);
		}
	}

	if (params.use_syslog)
		gflog_syslog_open(LOG_PID, syslog_facility);

	e = gfskd_mount(mdatap, &params);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfskd_mount: %s\n", gfarm_error_string(e));
		exit(1);
	}
	e = gfskd_main(mdatap, &params);

	return (0);
}
