#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <gfarm/gfarm.h>
#include "gfutil.h"

#include "gfm_client.h"
#include "gfs_misc.h"
#include "gfs_dirplusxattr.h"
#include "gfs_attrplus.h"

#define STR_BUFSIZE	32
#define NUM_FILES	3
#define USER_XATTR	"user.test"
#define USER_XMLATTR	"user.testx"

#define TEXT1	"ABCDEFGHIJ"
#define TEXT2	"KLMNOPQRST"
#define XML1	"<a><b>x</b></a>"

#define GFMD_FILETAB_MAX	1024

static char *srcdir = NULL;
static int auto_failover;
static long time0;

static void
msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

static void
chkerr_n(gfarm_error_t e, const char *diag, int i)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;
	if (i >= 0)
		fprintf(stderr, "error at gf[%d]: %s: %s\n", i, diag,
		    gfarm_error_string(e));
	else
		fprintf(stderr, "error: %s: %s\n", diag,
		    gfarm_error_string(e));
	exit(1);
}

static void
chkerr(gfarm_error_t e, const char *diag)
{
	chkerr_n(e, diag, -1);
}

static void
chkerr_e(gfarm_error_t e_expected, gfarm_error_t e, const char *diag)
{
	if (e_expected == e)
		return;
	fprintf(stderr, "%s: expected '%s' but '%s'\n", diag,
	    gfarm_error_string(e_expected), gfarm_error_string(e));
	exit(1);
}

static void
chkerrno_n(int rv, const char *diag, int i)
{
	if (rv != -1)
		return;
	fprintf(stderr, "error at [%d]: %s: %s\n", i, diag, strerror(errno));
	exit(1);
}

static void
chkerrno(int rv, const char *diag)
{
	if (rv != -1)
		return;
	fprintf(stderr, "error: %s: %s\n", diag, strerror(errno));
	exit(1);
}

static void
connection_failover(GFS_File gf, int idx)
{
	struct gfs_stat gfst;

	chkerr_n(gfs_pio_stat(gf, &gfst), "stat", 0);
	msg("gf[%d]: stat: size=%ld user:group=%s:%s\n",
	    idx, (long)gfst.st_size, gfst.st_user, gfst.st_group);
	gfs_stat_free(&gfst);
}

static void
short_sleep(void)
{
	struct timespec t;

	t.tv_sec = 0;
	t.tv_nsec = 300L * 1000L * 1000L;
	nanosleep(&t, NULL);
}

static void
wait_for_failover_automatically(void)
{
	int r;
	char cmd[1024];

	msg("*** gfmd failover ***\n");
	snprintf(cmd, sizeof(cmd), "%s/gfmd-failover.sh", srcdir);
	r = system(cmd);
	if (r == -1 || WEXITSTATUS(r) != 0) {
		fprintf(stderr, "failed auto failover (%s)\n", cmd);
		exit(1);
	}
}

#ifdef USE_SIGNAL_RESUME
static void
dummy_handler(int sig)
{
}
#endif

static void
wait_for_failover_manually(void)
{
	/* can receive signal under valgrind ? */
#ifdef USE_SIGNAL_RESUME
	sigset_t sigs;
	int sig;

	if (signal(SIGUSR2, dummy_handler) == SIG_ERR) {
		perror("signal");
		exit(1);
	}

	if (sigemptyset(&sigs) == -1) {
		perror("sigemptyset");
		exit(1);
	}

	if (sigaddset(&sigs, SIGUSR2) == -1) {
		perror("sigaddset");
		exit(1);
	}

	msg("*** wait for SIGUSR2 to continue ***\n");

	for (;;) {
		if (sigwait(&sigs, &sig) != 0) {
			perror("sigwait");
			exit(1);
		}
		if (sig == SIGUSR2)
			break;
	}
#else
	msg("*** Push Enter Key ***\n");
	getchar();
#endif
}

static void
wait_for_failover(void)
{
	if (auto_failover)
		wait_for_failover_automatically();
	else
		wait_for_failover_manually();
}

static void
wait_for_gfsd(const char *hostname)
{
	int r;
	char cmd[1024];

	snprintf(cmd, sizeof(cmd), "%s/wait_for_gfsd.sh %s", srcdir, hostname);
	r = system(cmd);
	if (r == -1 || WEXITSTATUS(r) != 0) {
		fprintf(stderr,
		    "failed to wait for gfsd (%s) connection. (%s)\n",
		    hostname, cmd);
		exit(1);
	}
}

static void
match_memory(const char *expected, const char *result, int len,
    const char *diag)
{
	if (memcmp(expected, result, len) != 0) {
		char *b1, *b2;

		b1 = malloc(len + 1);
		b2 = malloc(len + 1);
		memcpy(b1, expected, len);
		memcpy(b2, result, len);
		b1[len - 1] = 0;
		b2[len - 1] = 0;
		msg("error: %s: string not matched: "
		    "expected=[%s] result=[%s]\n", diag, b1, b2);
		free(b1);
		free(b2);
		exit(1);
	}
}

static void
match_file(const char *expected, int fd, off_t off, int len, const char *diag)
{
	ssize_t rv;
	char buf[STR_BUFSIZE];

	assert(len <= STR_BUFSIZE);

	if (lseek(fd, off, SEEK_SET) == -1)
		chkerrno(-1, diag);
	rv = read(fd, buf, len);
	if (rv != len) {
		if (rv == -1) {
			chkerrno(-1, diag);
		} else {
			fprintf(stderr,
			    "error: %s: read %d bytes expected, but %d\n",
			    diag, len, (int)rv);
			exit(1);
		}
	}
	if (memcmp(expected, buf, len) != 0) {
		msg("error: %s: string not matched: "
		    "expected=[%.*s] result=[%.*s]\n",
		    diag, len, expected, len, buf);
	}
}

static void
create_dirty_file(GFS_File gf[], char path[][PATH_MAX],
	const char *path_base, int nfiles)
{
	size_t sz1, sz2;
	int i, len;
	gfarm_off_t ofs;

	sz1 = strlen(TEXT1);
	sz2 = 1;

	for (i = 0; i < nfiles; ++i) {
		snprintf(path[i], PATH_MAX, "%s.%d", path_base, i);
		msg("gf[%d]: create %s\n", i, path[i]);
		chkerr_n(gfs_pio_create(path[i], GFARM_FILE_RDWR, 0777, &gf[i]),
		    "create", i);

		if (i != nfiles - 1) {
			msg("gf[%d]: write %d bytes\n", i, sz1);
			chkerr_n(gfs_pio_write(gf[i], TEXT1, sz1, &len),
			    "write1", i);
			msg("gf[%d]: write %d bytes ok\n", i, len);
		}
		msg("gf[%d]: seek to 0\n", i);
		chkerr_n(gfs_pio_seek(gf[i], 0, GFARM_SEEK_SET, &ofs),
		    "seek", i);
		msg("gf[%d]: seek to %ld ok\n", i, (long)ofs);
		if (i != nfiles - 1) {
			msg("gf[%d]: write %d bytes\n", i, sz2);
			chkerr_n(gfs_pio_write(gf[i], TEXT2, sz2, &len),
			    "write1", i);
			msg("gf[%d]: write %d bytes ok\n", i, len);
		}
	}
}

static void
create_and_write_file(GFS_File gf[], char path[][PATH_MAX],
	const char *path_base, int nfiles)
{
	size_t sz;
	int i, len;

	sz = strlen(TEXT1);

	for (i = 0; i < nfiles; ++i) {
		snprintf(path[i], PATH_MAX, "%s.%d", path_base, i);
		msg("gf[%d]: create %s\n", i, path[i]);
		chkerr_n(gfs_pio_create(path[i], GFARM_FILE_WRONLY,
		    0777, &gf[i]), "create", i);

		if (i != nfiles - 1) {
			msg("gf[%d]: write %d bytes\n", i, sz);
			chkerr_n(gfs_pio_write(gf[i], TEXT1, sz, &len),
			    "write1", i);
			msg("gf[%d]: write %d bytes ok\n", i, len);
		}
	}
}

static struct gfm_connection*
cache_gfm_connection(GFS_File *gfp, const char *path)
{
	msg("cache_gfm_connection: open %s\n", path);
	chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, gfp), "open");
	msg("cache_gfm_connection: open ok\n");
	return (gfs_pio_metadb(*gfp));
}

/* gfs_realpath calls gfm_inode_op_readonly */
static void
test_realpath(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	char *resolved;

	msg("gf: open %s\n", path);
	chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf), "open");
	msg("gf: open ok\n");

	wait_for_failover();

	msg("gf: realpath\n");
	chkerr(gfs_realpath(path, &resolved), "realpath");
	msg("gf: realpath ok\n");
	free(resolved);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

/* test_rename calls gfm_name2_op_modifiable */
static void
test_rename(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf[1];
	char *path, paths[1][PATH_MAX];
#       define TO_STRING ".to"
	char to_path[PATH_MAX + sizeof(TO_STRING)];

	create_and_write_file(gf, paths, path_base, 1);
	path = paths[0];
	snprintf(to_path, sizeof to_path, "%s" TO_STRING, path);

	wait_for_failover();

	msg("gf[0]: rename\n");
	chkerr(gfs_rename(path, to_path), "rename");
	msg("gf[0]: rename ok\n");

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf[0]), "close");
}

static void
test_statfs(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	gfarm_off_t used, avail, files;
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("statfs\n");
	chkerr(gfs_statfs(&used, &avail, &files), "statfs");
	msg("statfs ok\n");

	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_statfsnode(const char **argv)
{
	const char *domain = argv[0];
	const char *path = argv[1];
	int nhosts;
	GFS_File gf;
	struct gfm_connection *con;
	struct gfarm_host_sched_info *infos;
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;

	msg("schedule_hosts_domain_all\n");
	chkerr(gfarm_schedule_hosts_domain_all("/", domain,
	    &nhosts, &infos), "schedule_hosts_domain_all");
	msg("schedule_hosts_domain_all ok\n");
	assert(nhosts > 0);

	msg("gf: open %s\n", path);
	chkerr(gfs_pio_create(path, GFARM_FILE_RDWR, 0777, &gf), "create");
	msg("gf: open ok\n");
	con = gfs_pio_metadb(gf);

	wait_for_failover();

	msg("statfsnode\n");
	chkerr(gfs_statfsnode(infos[0].host, infos[0].port,
	    &bsize, &blocks, &bfree, &bavail, &files, &ffree, &favail),
	    "statfsnode");
	msg("statfsnode ok\n");

	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");

	gfarm_host_sched_info_free(nhosts, infos);
}

static void
test_chmod0(const char **argv, int follow)
{
	const char *path = argv[0];
	GFS_File gf;
	const char *diag = follow ? "chmod" : "lchmod";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr((follow ? gfs_chmod : gfs_lchmod)(path, 0777), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_chmod(const char **argv)
{
	test_chmod0(argv, 1);
}

static void
test_lchmod(const char **argv)
{
	test_chmod0(argv, 0);
}

static void
test_chown0(const char **argv, int follow)
{
	const char *path = argv[0];
	GFS_File gf;
	const char *diag = follow ? "chown" : "lchown";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr_e(GFARM_ERR_NO_SUCH_USER,
	    (follow ? gfs_chown : gfs_lchown)
	    (path, "NOUSER", "gfarmadm"), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_chown(const char **argv)
{
	test_chown0(argv, 1);
}

static void
test_lchown(const char **argv)
{
	test_chown0(argv, 0);
}

static void
test_readlink(const char **argv)
{
	const char *path = argv[0];
	char *src;
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("readlink\n");
	chkerr(gfs_readlink(path, &src), "readlink");
	msg("readlink ok\n");
	assert(con != gfs_pio_metadb(gf));
	free(src);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_stat0(const char **argv, int follow)
{
	const char *path = argv[0];
	struct gfs_stat gfst;
	GFS_File gf;
	const char *diag = follow ? "stat" : "lstat";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr((follow ? gfs_stat : gfs_lstat)(path, &gfst), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));
	gfs_stat_free(&gfst);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_stat(const char **argv)
{
	test_stat0(argv, 1);
}

static void
test_lstat(const char **argv)
{
	test_stat0(argv, 0);
}

static void
test_fstat(const char **argv)
{
	const char *path = argv[0];
	struct gfs_stat gfst;
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("fstat\n");
	chkerr(gfs_fstat(gf, &gfst), "fstat");
	msg("fstat ok\n");
	assert(con != gfs_pio_metadb(gf));
	gfs_stat_free(&gfst);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_stat_cksum(const char **argv)
{
	const char *path = argv[0];
	struct gfs_stat_cksum gfcksum;
	GFS_File gf;
	static const char diag[] = "gfs_stat_cksum";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr(gfs_stat_cksum(path, &gfcksum), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));
	gfs_stat_cksum_free(&gfcksum);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_fstat_cksum(const char **argv)
{
	const char *path = argv[0];
	struct gfs_stat_cksum gfcksum;
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("fstat_cksum\n");
	chkerr(gfs_fstat_cksum(gf, &gfcksum), "fstat_cksum");
	msg("fstat_cksum ok\n");
	assert(con != gfs_pio_metadb(gf));
	gfs_stat_cksum_free(&gfcksum);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_pio_cksum(const char **argv)
{
	const char *path = argv[0];
	struct gfs_stat_cksum gfcksum;
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("pio_cksum\n");
	chkerr(gfs_pio_cksum(gf, "md5", &gfcksum), "pio_cksum");
	msg("pio_cksum ok\n");
	assert(con != gfs_pio_metadb(gf));
	gfs_stat_cksum_free(&gfcksum);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_utimes0(const char **argv, int follow)
{
	const char *path = argv[0];
	struct gfarm_timespec gfts;
	GFS_File gf;
	const char *diag = follow ? "utimes" : "lutimes";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	gfts.tv_sec = 1000000;
	gfts.tv_nsec = 0;
	msg("%s\n", diag);
	chkerr((follow ? gfs_utimes : gfs_lutimes)(path, &gfts), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_utimes(const char **argv)
{
	test_utimes0(argv, 0);
}

static void
test_lutimes(const char **argv)
{
	test_utimes0(argv, 1);
}

static void
test_remove(const char **argv)
{
	const char *path_base = argv[0];
	char path[PATH_MAX];
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, path_base);

	snprintf(path, sizeof path, "%s.0", path_base);

	msg("link\n");
	chkerr(gfs_link(path_base, path), "link");
	msg("link ok\n");

	wait_for_failover();

	msg("remove\n", path);
	chkerr(gfs_remove(path), "remove");
	msg("remove ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_unlink(const char **argv)
{
	const char *path_base = argv[0];
	char path[PATH_MAX];
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, path_base);

	snprintf(path, sizeof path, "%s.0", path_base);

	msg("link\n");
	chkerr(gfs_link(path_base, path), "link");
	msg("link ok\n");

	wait_for_failover();

	msg("unlink\n");
	chkerr(gfs_unlink(path), "unlink");
	msg("unlink ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_link(const char **argv)
{
	const char *src = argv[0];
	char dst[PATH_MAX];
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, src);

	snprintf(dst, sizeof dst, "%s.0", src);

	wait_for_failover();

	msg("link\n");
	chkerr(gfs_link(src, dst), "link");
	msg("link ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_symlink(const char **argv)
{
	const char *src = argv[0];
	char dst[PATH_MAX];
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, src);

	snprintf(dst, sizeof dst, "%s.0", src);

	wait_for_failover();

	msg("symlink\n");
	chkerr(gfs_symlink(src, dst), "symlink");
	msg("symlink ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_mkdir(const char **argv)
{
	const char *fpath = argv[0], *path_base = argv[1];
	char path[PATH_MAX];
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	wait_for_failover();

	snprintf(path, sizeof path, "%s.0", path_base);
	msg("mkdir\n");
	chkerr(gfs_mkdir(path, 0777), "mkdir");
	msg("mkdir ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_rmdir(const char **argv)
{
	const char *fpath = argv[0], *path_base = argv[1];
	char path[PATH_MAX];
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	snprintf(path, sizeof path, "%s.0", path_base);

	msg("mkdir\n");
	chkerr(gfs_mkdir(path, 0777), "mkdir");
	msg("mkdir ok\n");

	wait_for_failover();

	msg("rmdir\n");
	chkerr(gfs_rmdir(path), "rmdir");
	msg("rmdir ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_opendir(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	GFS_File gf;
	GFS_Dir gd;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	wait_for_failover();

	msg("opendir\n");
	chkerr(gfs_opendir(path, &gd), "opendir");
	msg("opendir ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closedir\n");
	chkerr(gfs_closedir(gd), "closedir");
	msg("closedir ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_opendirplus(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	GFS_File gf;
	GFS_DirPlus gd;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	wait_for_failover();

	msg("opendirplus\n");
	chkerr(gfs_opendirplus(path, &gd), "opendirplus");
	msg("opendirplus ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closedirplus\n");
	chkerr(gfs_closedirplus(gd), "closedirplus");
	msg("closedirplus ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_opendirplusxattr(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	GFS_File gf;
	GFS_DirPlusXAttr gd;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	wait_for_failover();

	msg("opendirplusxattr\n");
	chkerr(gfs_opendirplusxattr(path, &gd), "opendirplusxattr");
	msg("opendirplusxattr ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closedirplusxattr\n");
	chkerr(gfs_closedirplusxattr(gd), "closedirplusxattr");
	msg("closedirplusxattr ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_closedir(const char **argv)
{
	const char *path = argv[0];
	GFS_Dir gd;

	msg("opendir\n");
	chkerr(gfs_opendir(path, &gd), "opendir");
	msg("opendir ok\n");

	wait_for_failover();

	msg("closedir\n");
	chkerr(gfs_closedir(gd), "closedir");
	msg("closedir ok\n");
}

static void
test_closedirplus(const char **argv)
{
	const char *path = argv[0];
	GFS_DirPlus gd;

	msg("opendirplus\n");
	chkerr(gfs_opendirplus(path, &gd), "opendirplus");
	msg("opendirplus ok\n");

	wait_for_failover();

	msg("closedirplus\n");
	chkerr(gfs_closedirplus(gd), "closedirplus");
	msg("closedirplus ok\n");
}

static void
test_closedirplusxattr(const char **argv)
{
	const char *path = argv[0];
	GFS_DirPlusXAttr gd;

	msg("opendirplusxattr\n");
	chkerr(gfs_opendirplusxattr(path, &gd), "opendirplusxattr");
	msg("opendirplusxattr ok\n");

	wait_for_failover();

	msg("closedirplusxattr\n");
	chkerr(gfs_closedirplusxattr(gd), "closedirplusxattr");
	msg("closedirplusxattr ok\n");
}

/* gfs_readdir calls gfm_client_compound_readonly_file_op */
static void
test_readdir(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	GFS_File gf;
	GFS_Dir gd;
	struct gfs_dirent *dent;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	msg("opendir %s\n", path);
	chkerr(gfs_opendir(path, &gd), "opendir");
	msg("opendir ok\n");

	wait_for_failover();

	msg("readdir\n");
	chkerr(gfs_readdir(gd, &dent), "readdir");
	msg("readdir ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closedir\n");
	chkerr(gfs_closedir(gd), "closedir");
	msg("closedir ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_readdir2(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	GFS_File gf;
	GFS_Dir gd;
	char buf[STR_BUFSIZE];
	size_t sz = 10;
	int len;
	struct gfs_dirent *dent;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	msg("opendir\n");
	chkerr(gfs_opendir(path, &gd), "opendir");
	msg("opendir ok\n");

	wait_for_failover();

	/* failover by scheduling gfsd */
	msg("gf: read %d bytes\n", sz);
	chkerr_n(gfs_pio_read(gf, buf, sz, &len), "read", 0);
	msg("gf: read %d bytes ok\n", len);
	assert(con != gfs_pio_metadb(gf));

	msg("readdir\n");
	/* acquire_valid_connection() will be called */
	chkerr(gfs_readdir(gd, &dent), "readdir");
	msg("readdir ok\n");

	msg("closedir\n");
	chkerr(gfs_closedir(gd), "closedir");
	msg("closedir ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_readdirplus(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	GFS_File gf;
	GFS_DirPlus gd;
	struct gfs_dirent *dent;
	struct gfs_stat *gfst;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	msg("opendirplus %s\n", path);
	chkerr(gfs_opendirplus(path, &gd), "opendirplus");
	msg("opendirplus ok\n");

	wait_for_failover();

	msg("readdirplus\n");
	chkerr(gfs_readdirplus(gd, &dent, &gfst), "readdirplus");
	msg("readdirplus ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closedirplus\n");
	chkerr(gfs_closedirplus(gd), "closedirplus");
	msg("closedirplus ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_readdirplusxattr(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	char **names;
	void **values;
	int nattrs;
	size_t *sizes;
	GFS_File gf;
	GFS_DirPlusXAttr gd;
	struct gfs_dirent *dent;
	struct gfs_stat *gfst;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	msg("opendirplusxattr %s\n", path);
	chkerr(gfs_opendirplusxattr(path, &gd), "opendirplusxattr");
	msg("opendirplusxattr ok\n");

	wait_for_failover();

	msg("readdirplusxattr\n");
	chkerr(gfs_readdirplusxattr(gd, &dent, &gfst, &nattrs,
	   &names, &values, &sizes), "readdirplusxattr");
	msg("readdirplusxattr ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closedirplusxattr\n");
	chkerr(gfs_closedirplusxattr(gd), "closedirplusxattr");
	msg("closedirplusxattr ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_seekdir(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	GFS_File gf;
	GFS_Dir gd;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	msg("opendir\n");
	chkerr(gfs_opendir(path, &gd), "opendir");
	msg("opendir ok\n");

	wait_for_failover();

	msg("seekdir\n");
	chkerr(gfs_seekdir(gd, 1), "seekdir");
	msg("seekdir ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closedir\n");
	chkerr(gfs_closedir(gd), "closedir");
	msg("closedir ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_seekdirplusxattr(const char **argv)
{
	const char *fpath = argv[0], *path = argv[1];
	GFS_File gf;
	GFS_DirPlusXAttr gd;
	struct gfm_connection *con = cache_gfm_connection(&gf, fpath);

	msg("opendirplusxattr\n");
	chkerr(gfs_opendirplusxattr(path, &gd), "opendirplusxattr");
	msg("opendirplusxattr ok\n");

	wait_for_failover();

	msg("seekdirplusxattr\n");
	chkerr(gfs_seekdirplusxattr(gd, 1), "seekdirplusxattr");
	msg("seekdirplusxattr ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closedirplusxattr\n");
	chkerr(gfs_closedirplusxattr(gd), "closedirplusxattr");
	msg("closedirplusxattr ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_read0(const char *path, int explicit_failover)
{
	GFS_File gf[NUM_FILES];
	char buf[STR_BUFSIZE];
	size_t sz = 10;
	int i, len;
	gfarm_off_t ofs;

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: open %s\n", i, path);
		chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf[i]),
		    "open", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: read %d bytes\n", i, sz);
			chkerr_n(gfs_pio_read(gf[i], buf, sz, &len),
			    "read1", i);
			msg("gf[%d]: read %d bytes ok\n", i, len);
			match_memory(TEXT1, buf, len,
			    "match_memory1");
		}
	}

	wait_for_failover();
	if (explicit_failover)
		connection_failover(gf[0], 0);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: read %d bytes\n", i, sz);
		chkerr_n(gfs_pio_read(gf[i], buf, sz, &len),
		    "read2", i);
		msg("gf[%d]: read %d bytes ok\n", i, len);
		if (i != NUM_FILES - 1) {
			match_memory(TEXT2, buf, len, "match_memory2");
		} else {
			match_memory(TEXT1, buf, len, "match_memory2");
		}
		msg("gf[%d]: seek to 0\n", i);
		chkerr_n(gfs_pio_seek(gf[i], 0, GFARM_SEEK_SET, &ofs),
		    "peek", i);
		msg("gf[%d]: read %d bytes\n", i, sz);
		chkerr_n(gfs_pio_read(gf[i], buf, sz, &len),
		    "read3", i);
		msg("gf[%d]: read %d bytes ok\n", i, len);
		if (i != NUM_FILES - 1) {
			match_memory(TEXT1, buf, len, "match_memory3");
		} else {
			match_memory(TEXT1, buf, len, "match_memory3");
		}

		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_read(const char **argv)
{
	test_read0(argv[0], 0);
}

static void
test_read_stat(const char **argv)
{
	test_read0(argv[0], 1);
}

static void
test_recvfile0(const char *path, const char *local_dir, int explicit_failover)
{
	GFS_File gf[NUM_FILES];
	int lfd[NUM_FILES];
	size_t sz = 10;
	int i;
	gfarm_off_t len;
	char localtmp[PATH_MAX];

	for (i = 0; i < NUM_FILES; ++i) {
		snprintf(localtmp, sizeof localtmp, "%s/%d", local_dir, i);
		lfd[i] = open(localtmp, O_RDWR|O_CREAT|O_TRUNC, 0777);
		chkerrno_n(lfd[i], "open", i);

		msg("gf[%d]: gfs_pio_open %s\n", i, path);
		chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf[i]),
		    "gfs_pio_open", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: gfs_pio_recvfile %d bytes\n", i, sz);
			chkerr_n(gfs_pio_recvfile(gf[i], 0, lfd[i], 0, sz,
			    &len), "gfs_pio_recvfile#1", i);
			msg("gf[%d]: gfs_pio_recvfile %d bytes ok\n", i, len);
			match_file(TEXT1, lfd[i], 0, sz, "match_file#1");
		}
	}

	wait_for_failover();
	if (explicit_failover)
		connection_failover(gf[0], 0);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: gfs_pio_recvfile %d bytes\n", i, sz);
		chkerr_n(gfs_pio_recvfile(gf[i],
		    i != NUM_FILES - 1 ? sz : 0, lfd[i],
		    i != NUM_FILES - 1 ? sz : 0, sz, &len),
		    "gfs_pio_recvfile#2", i);
		msg("gf[%d]: gfs_pio_recvfile %d bytes ok\n", i, len);
		if (i != NUM_FILES - 1) {
			match_file(TEXT2, lfd[i], sz, sz, "match_file#2");
		} else {
			match_file(TEXT1, lfd[i], 0, sz, "match_file#2:last");
		}

		msg("gf[%d]: gfs_pio_recvfile %d bytes\n", i, sz);
		chkerr_n(gfs_pio_recvfile(gf[i], 0, lfd[i], 0, sz, &len),
		    "gfs_pio_recvfile#2", i);
		msg("gf[%d]: gfs_pio_recvfile %d bytes ok\n", i, len);
		match_file(TEXT1, lfd[i], 0, sz, "match_file#3");

		msg("gf[%d]: gfs_pio_close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
		chkerrno_n(close(lfd[i]), "close", i);
	}
}

static void
test_recvfile(const char **argv)
{
	test_recvfile0(argv[0], argv[1], 0);
}

static void
test_recvfile_stat(const char **argv)
{
	test_recvfile0(argv[0], argv[1], 1);
}

static void
test_write0(const char *path_base, int explicit_failover)
{
	GFS_File gf[NUM_FILES];
	size_t sz;
	int i, len;
	char path[NUM_FILES][PATH_MAX];

	create_and_write_file(gf, path, path_base, NUM_FILES);

	wait_for_failover();
	if (explicit_failover)
		connection_failover(gf[0], 0);

	sz = strlen(TEXT2);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: write %d bytes\n", i, sz);
		chkerr_n(gfs_pio_write(gf[i], TEXT2, sz, &len),
		    "write2", i);
		msg("gf[%d]: write %d bytes ok\n", i, len);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_write(const char **argv)
{
	test_write0(argv[0], 0);
}

static void
test_write_stat(const char **argv)
{
	test_write0(argv[0], 1);
}

static void
test_sendfile0(const char *path_base, const char *local_path,
	int explicit_failover)
{
	GFS_File gf[NUM_FILES];
	size_t sz1 = strlen(TEXT1), sz2 = strlen(TEXT2);
	int i, lfd;
	gfarm_off_t len;
	char path[NUM_FILES][PATH_MAX];

	lfd = open(local_path, O_RDONLY);
	chkerrno(lfd, "open");

	for (i = 0; i < NUM_FILES; ++i) {
		snprintf(path[i], sizeof path[i], "%s.%d", path_base, i);
		msg("gf[%d]: create %s\n", i, path[i]);
		chkerr_n(gfs_pio_create(path[i], GFARM_FILE_WRONLY,
		    0777, &gf[i]), "create", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: gfs_pio_sendfile %d bytes\n", i, sz1);
			chkerr_n(gfs_pio_sendfile(gf[i], 0, lfd, 0, sz1, &len),
			    "sendfile1", i);
			msg("gf[%d]: gfs_pio_sendfile %d bytes ok\n", i, len);
		}
	}

	wait_for_failover();
	if (explicit_failover)
		connection_failover(gf[0], 0);

	sz2 = strlen(TEXT2);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: gfs_pio_sendfile %d bytes\n", i, sz2);
		chkerr_n(gfs_pio_sendfile(gf[i], sz1, lfd, sz1, sz2, &len),
		    "sendfile2", i);
		msg("gf[%d]: gfs_pio_sendfile %d bytes ok\n", i, len);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}

	chkerrno(close(lfd), "close");
}

static void
test_sendfile(const char **argv)
{
	test_sendfile0(argv[0], argv[1], 0);
}

static void
test_sendfile_stat(const char **argv)
{
	test_sendfile0(argv[0], argv[1], 1);
}

static void
test_sched_read(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	char buf[STR_BUFSIZE];
	size_t sz = 10;
	int len;

	msg("gf[%d]: open %s\n", 0, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf),
	    "open", 0);

	wait_for_failover();

	msg("gf[%d]: read %d bytes\n", 0, sz);
	chkerr_n(gfs_pio_read(gf, buf, sz, &len), "read1", 0);
	msg("gf[%d]: read %d bytes ok\n", 0, len);
	match_memory(TEXT1, buf, len, "match_memory1");
	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf), "close", 0);
}

static void
test_sched_recvfile(const char **argv)
{
	const char *path = argv[0];
	const char *local_path = argv[1];
	GFS_File gf = NULL;
	int lfd;
	size_t sz = 10;
	gfarm_off_t len;

	lfd = open(local_path, O_RDWR|O_CREAT|O_TRUNC, 0777);
	chkerrno(lfd, "open");

	msg("gfs_pio_open\n", path);
	chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf), "gfs_pio_open");

	wait_for_failover();

	msg("gfs_pio_recvfile %d bytes\n", sz);
	chkerr(gfs_pio_recvfile(gf, 0, lfd, 0, sz, &len), "gfs_pio_recvfile");
	msg("gfs_pio_recvfile %d bytes ok\n", 0, (int)len);
	match_file(TEXT1, lfd, 0, sz, "match_file");
	msg("gfs_pio_close\n");
	chkerr(gfs_pio_close(gf), "gfs_pio_close");

	chkerrno(close(lfd), "close");
}

static void
test_sched_open_write(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf;
	size_t sz;
	int len;
	char path[PATH_MAX];

	sz = strlen(TEXT1);

	snprintf(path, sizeof path, "%s.%d", path_base, 0);
	msg("gf[%d]: create %s\n", 0, path);
	chkerr_n(gfs_pio_create(path, GFARM_FILE_WRONLY, 0777, &gf),
	    "create", 0);
	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf), "close1", 0);

	msg("gf[%d]: open %s\n", 0, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_WRONLY, &gf), "open", 0);

	wait_for_failover();

	msg("gf[%d]: write %d bytes\n", 0, sz);
	chkerr_n(gfs_pio_write(gf, TEXT1, sz, &len),
	    "write1", 0);
	msg("gf[%d]: write %d bytes ok\n", 0, len);

	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf), "close2", 0);
}

static void
test_sched_create_write(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf;
	size_t sz;
	int len;
	char path[PATH_MAX];

	sz = strlen(TEXT1);

	snprintf(path, sizeof path, "%s.%d", path_base, 0);
	msg("gf[%d]: create %s\n", 0, path);
	chkerr_n(gfs_pio_create(path, GFARM_FILE_WRONLY, 0777, &gf),
	    "create", 0);

	wait_for_failover();

	msg("gf[%d]: write %d bytes\n", 0, sz);
	chkerr_n(gfs_pio_write(gf, TEXT1, sz, &len),
	    "write1", 0);
	msg("gf[%d]: write %d bytes ok\n", 0, len);

	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf), "close2", 0);
}

static void
test_sched_open_sendfile(const char **argv)
{
	const char *path = argv[0];
	const char *local_path = argv[1];
	GFS_File gf;
	int lfd;
	size_t sz = strlen(TEXT1);
	gfarm_off_t len;

	lfd = open(local_path, O_RDONLY);
	chkerrno(lfd, "local-file");

	msg("gfs_pio_create %s\n", path);
	chkerr(gfs_pio_create(path, GFARM_FILE_WRONLY, 0777, &gf),
	    "gfs_pio_create");
	msg("gfs_pio_close#0\n");
	chkerr(gfs_pio_close(gf), "gfs_pio_close#0");

	msg("gfs_pio_open %s\n", path);
	chkerr(gfs_pio_open(path, GFARM_FILE_WRONLY, &gf), "gfs_pio_open");

	wait_for_failover();

	msg("gfs_pio_sendfile %d bytes\n", sz);
	chkerr(gfs_pio_sendfile(gf, 0, lfd, 0, sz, &len), "gfs_pio_sendfile");
	msg("gfs_pio_sendfile %d bytes ok\n", len);

	msg("gfs_pio_close#1\n");
	chkerr(gfs_pio_close(gf), "gfs_pio_close#1");

	chkerrno(close(lfd), "close");
}

static void
test_sched_create_sendfile(const char **argv)
{
	const char *path = argv[0];
	const char *local_path = argv[1];
	GFS_File gf;
	int lfd;
	size_t sz = strlen(TEXT1);
	gfarm_off_t len;

	lfd = open(local_path, O_RDONLY);
	chkerrno(lfd, "local-file");

	msg("gfs_pio_create %s\n", path);
	chkerr(gfs_pio_create(path, GFARM_FILE_WRONLY, 0777, &gf),
	    "gfs_pio_create");

	wait_for_failover();

	msg("gfs_pio_sendfile %d bytes\n", sz);
	chkerr(gfs_pio_sendfile(gf, 0, lfd, 0, sz, &len), "gfs_pio_sendfile");
	msg("gfs_pio_sendfile %d bytes ok\n", len);

	msg("gfs_pio_close\n");
	chkerr_n(gfs_pio_close(gf), "gfs_pio_close", 0);

	chkerrno(close(lfd), "close");
}

static void
test_close(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf;
	size_t sz;
	int len;
	char path[PATH_MAX];

	sz = strlen(TEXT1);

	snprintf(path, sizeof path, "%s.%d", path_base, 0);
	msg("gf[%d]: create %s\n", 0, path);
	chkerr_n(gfs_pio_create(path, GFARM_FILE_WRONLY, 0777, &gf),
	    "create", 0);

	msg("gf[%d]: write %d bytes\n", 0, sz);
	chkerr_n(gfs_pio_write(gf, TEXT1, sz, &len),
	    "write1", 0);
	msg("gf[%d]: write %d bytes ok\n", 0, len);

	wait_for_failover();

	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf), "close2", 0);
}

static void
do_read(GFS_File gf, int i)
{
	msg("gf[%d]: getc\n", i);
	chkerr_n((gfs_pio_getc(gf), gfs_pio_error(gf)), "getc", i);
	msg("gf[%d]: getc ok\n", i);
}

static void
test_close_open(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf0, gf1, gf2, gf3;
	struct gfm_connection *con0, *con1, *con3;

	msg("gf[%d]: open %s\n", 0, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf0), "open", 0);
	msg("gf[%d]: open ok\n", 0);

	msg("gf[%d]: open %s\n", 1, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf1), "open", 1);
	msg("gf[%d]: open ok\n", 1);

	msg("gf[%d]: open %s\n", 2, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf2), "open", 2);
	msg("gf[%d]: open ok\n", 2);
	/* schedule gf1 */
	do_read(gf1, 2);

	wait_for_failover();

	con0 = gfs_pio_metadb(gf0);
	con1 = gfs_pio_metadb(gf1);
	assert(con0 == con1);

	/* gfm_connection in gf1 will be purged */
	msg("gf[%d]: close\n", 1);
	/* no faileover */
	chkerr_n(gfs_pio_close(gf1), "close", 1);
	msg("gf[%d]: close ok\n", 1);
	/* now gfs_connection is not related to GFS_File */

	/* open and failover */
	msg("gf[%d]: open %s\n", 3, path);
	/* gfm_client_connection_failover_pre_connect() will be called */
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf3), "open", 3);
	msg("gf[%d]: open ok\n", 3);

	/* gfm_connection in gf2 is new connection */
	con3 = gfs_pio_metadb(gf3);
	assert(con0 != con3);
	/* schedule gf3 */
	do_read(gf3, 3);
	/* gf0 is scheduled */
	do_read(gf0, 0);

	assert(con0 != gfs_pio_metadb(gf0));
	/* gfm_connection is not changed in gf2 after failover */
	assert(con3 == gfs_pio_metadb(gf3));
	con0 = gfs_pio_metadb(gf0);
	assert(con0 == con3);

	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf0), "close", 0);
	msg("gf[%d]: close ok\n", 0);

	msg("gf[%d]: close\n", 2);
	chkerr_n(gfs_pio_close(gf2), "close", 2);
	msg("gf[%d]: close ok\n", 2);

	msg("gf[%d]: close\n", 3);
	chkerr_n(gfs_pio_close(gf3), "close", 3);
	msg("gf[%d]: close ok\n", 3);
}

/*
 * no scheduling version of test_close_open.
 * This checks wether or not gfm_connections related to opened-file-list are
 * same instance. If not, assertion in check_connection_in_file_list()
 * will fail.
 */
static void
test_close_open2(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf0, gf1, gf2;
	struct gfm_connection *con0, *con1, *con2;

	msg("gf[%d]: open %s\n", 0, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf0), "open", 0);
	msg("gf[%d]: open ok\n", 0);

	msg("gf[%d]: open %s\n", 1, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf1), "open", 1);
	msg("gf[%d]: open ok\n", 1);

	wait_for_failover();

	con0 = gfs_pio_metadb(gf0);
	con1 = gfs_pio_metadb(gf1);
	assert(con0 == con1);

	/* gfm_connection in gf1 will be purged */
	msg("gf[%d]: close\n", 1);
	/* no faileover */
	chkerr_n(gfs_pio_close(gf1), "close", 1);
	msg("gf[%d]: close ok\n", 1);

	/* open and failover */
	msg("gf[%d]: open %s\n", 2, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf2), "open", 2);
	msg("gf[%d]: open ok\n", 2);

	/* gfm_connection in gf2 is new connection */
	con2 = gfs_pio_metadb(gf2);
	assert(con0 != con2);

	assert(con0 != gfs_pio_metadb(gf0));
	/* gfm_connection is not changed in gf2 after failover */
	assert(con2 == gfs_pio_metadb(gf2));
	con0 = gfs_pio_metadb(gf0);
	assert(con0 == con2);

	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf0), "close", 0);
	msg("gf[%d]: close ok\n", 0);

	msg("gf[%d]: close\n", 2);
	chkerr_n(gfs_pio_close(gf2), "close", 2);
	msg("gf[%d]: close ok\n", 2);
}

static void
test_fhopen_file(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf0, gf1, gf2;
	struct gfs_stat st;
	struct gfm_connection *con0, *con1, *con2;

	msg("gf[%d]: open %s\n", 0, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf0), "open", 0);
	msg("gf[%d]: open ok\n", 0);

	msg("gf[%d]: pio_stat %s\n", 0, path);
	chkerr_n(gfs_pio_stat(gf0, &st), "pio_stat", 0);
	msg("gf[%d]: pio_stat ok\n", 0);

	msg("gf[%d]: fhopen %s\n", 1, path);
	chkerr_n(gfs_pio_fhopen(st.st_ino, st.st_gen, GFARM_FILE_RDONLY,
	    &gf1), "fhopen", 1);
	msg("gf[%d]: fhopen ok\n", 1);

	/* schedule gf0 */
	do_read(gf0, 0);

	/* schedule gf1 */
	do_read(gf1, 1);

	wait_for_failover();

	con0 = gfs_pio_metadb(gf0);
	con1 = gfs_pio_metadb(gf1);
	assert(con0 == con1);

	/* open and failover */
	msg("gf[%d]: open %s\n", 2, path);
	/* gfm_client_connection_failover_pre_connect() will be called */
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf2), "open", 2);
	msg("gf[%d]: open ok\n", 2);

	/* gfm_connection in gf1 is new connection */
	con2 = gfs_pio_metadb(gf2);
	assert(con0 != con2);
	/* schedule gf2 */
	do_read(gf2, 2);

	assert(con0 != gfs_pio_metadb(gf0));
	/* gfm_connection is not changed in gf2 after failover */
	assert(con2 == gfs_pio_metadb(gf2));
	con0 = gfs_pio_metadb(gf0);
	assert(con0 == con2);

	/* schedule gf1 */
	do_read(gf1, 1);

	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf0), "close", 0);
	msg("gf[%d]: close ok\n", 0);

	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf1), "close", 1);
	msg("gf[%d]: close ok\n", 1);

	msg("gf[%d]: close\n", 2);
	chkerr_n(gfs_pio_close(gf2), "close", 2);
	msg("gf[%d]: close ok\n", 2);
}

static void
test_open_read_loop(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	char buf[STR_BUFSIZE];
	size_t sz = 1;
	int len;
	int i;

	msg("gf: open/read/close loop start\n");
	for (i = 0; i < GFMD_FILETAB_MAX + 1; ++i) {
		chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf), "open", i);
		chkerr_n((gfs_pio_read(gf, buf, sz, &len),
		    gfs_pio_error(gf)), "read", i);
		if (i == 0)
			wait_for_failover();
		chkerr_n(gfs_pio_close(gf), "close", i);
	}
	msg("gf: open/read/close loop end\n");
}

static void
test_open_recvfile_loop(const char **argv)
{
	const char *path = argv[0];
	const char *local_path = argv[1];
	GFS_File gf;
	int lfd;
	size_t sz = 1;
	gfarm_off_t len;
	int i;

	msg("gf: gfs_pio_open/gfs_pio_recvfile/gfs_pio_close loop start\n");

	lfd = open(local_path, O_RDWR|O_CREAT|O_TRUNC, 0777);
	chkerrno(lfd, "open");

	for (i = 0; i < GFMD_FILETAB_MAX + 1; ++i) {
		chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf),
		    "gfs_pio_open", i);
		chkerr_n((gfs_pio_recvfile(gf, 0, lfd, 0, sz, &len),
		    gfs_pio_error(gf)), "gfs_pio_recvfile", i);
		if (i == 0)
			wait_for_failover();
		chkerr_n(gfs_pio_close(gf), "gfs_pio_close", i);
	}

	chkerrno(close(lfd), "close");

	msg("gf: gfs_pio_open/gfs_pio_recvfile/gfs_pio_close loop end\n");
}

static void
test_getc(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf[NUM_FILES];
	int i, ch;
	char c;

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: open %s\n", i, path);
		chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf[i]),
		    "open", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: getc\n", i);
			ch = gfs_pio_getc(gf[i]);
			chkerr_n(gfs_pio_error(gf[i]), "getc1", i);
			msg("gf[%d]: getc ok\n", i);
			c = (unsigned char)ch;
			match_memory("A", &c, 1, "match_memory1");
		}
	}

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: getc\n", i);
		ch = gfs_pio_getc(gf[i]);
		chkerr_n(gfs_pio_error(gf[i]), "getc2", i);
		msg("gf[%d]: getc ok\n", i);
		c = (unsigned char)ch;

		if (i != NUM_FILES - 1)
			match_memory("B", &c, 1, "match_memory2");
		else
			match_memory("A", &c, 1, "match_memory2");

		msg("gf[%d]: ungetc\n", i);
		ch = gfs_pio_ungetc(gf[i], 'C');
		chkerr_n(gfs_pio_error(gf[i]), "ungetc1", i);
		msg("gf[%d]: ungetc ok\n", i);
		c = (unsigned char)ch;
		match_memory("C", &c, 1, "match_memory3");
		msg("gf[%d]: close\n", i);
		gfs_pio_close(gf[i]);
	}
}

static void
test_seek(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf[NUM_FILES];
	int i;
	gfarm_off_t ofs;

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: open %s\n", i, path);
		chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf[i]),
		    "open", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: seek to 1\n", i);
			chkerr_n(gfs_pio_seek(gf[i], 1, GFARM_SEEK_SET, &ofs),
			    "seek1", i);
			msg("gf[%d]: seek to %ld ok\n", i, (long)ofs);
		}
	}

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: seek to 8\n", i);
		chkerr_n(gfs_pio_seek(gf[i], 8, GFARM_SEEK_SET, &ofs),
		    "seek2", i);
		msg("gf[%d]: seek to %ld ok\n", i, (long)ofs);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_seek_dirty(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf[NUM_FILES];
	int i;
	gfarm_off_t ofs;
	char path[NUM_FILES][PATH_MAX];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: seek to 8\n", i);
		chkerr_n(gfs_pio_seek(gf[i], 8, GFARM_SEEK_SET, &ofs),
		    "seek1", i);
		msg("gf[%d]: seek to %ld ok\n", i, (long)ofs);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_putc(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][PATH_MAX];

	for (i = 0; i < NUM_FILES; ++i) {
		snprintf(path[i], sizeof path[i], "%s.%d", path_base, i);
		msg("gf[%d]: create %s\n", i, path[i]);
		chkerr_n(gfs_pio_create(path[i], GFARM_FILE_WRONLY,
		    0777, &gf[i]), "create", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: putc\n", i);
			chkerr_n(gfs_pio_putc(gf[i], 'A'), "putc1", i);
			msg("gf[%d]: putc ok\n", i);
		}
	}

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: putc \n", i);
		chkerr_n(gfs_pio_putc(gf[i], 'B'), "putc2", i);
		msg("gf[%d]: putc ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_truncate(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][PATH_MAX];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: truncate\n", i);
		chkerr_n(gfs_pio_truncate(gf[i], 50), "truncate", i);
		msg("gf[%d]: truncate ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_flush(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][PATH_MAX];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: flush\n", i);
		chkerr_n(gfs_pio_flush(gf[i]), "flush", i);
		msg("gf[%d]: flush ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_sync(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][PATH_MAX];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: sync\n", i);
		chkerr_n(gfs_pio_sync(gf[i]), "sync", i);
		msg("gf[%d]: sync ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_datasync(const char **argv)
{
	const char *path_base = argv[0];
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][PATH_MAX];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: datasync\n", i);
		chkerr_n(gfs_pio_datasync(gf[i]), "datasync", i);
		msg("gf[%d]: datasync ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr_n(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_read_close_read(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf0, gf1;
	struct gfm_connection *con0, *con1;
	char buf[STR_BUFSIZE];
	size_t sz = 10;
	int len;

	msg("gf[%d]: open %s\n", 0, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf0), "open0", 0);
	msg("gf[%d]: open ok\n", 0);

	msg("gf[%d]: open %s\n", 1, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf1), "open1", 1);
	msg("gf[%d]: open ok\n", 1);

	msg("gf[%d]: read %d bytes\n", 1, sz);
	chkerr_n(gfs_pio_read(gf1, buf, sz, &len), "read1", 1);
	msg("gf[%d]: read %d bytes ok\n", 1, len);
	match_memory(TEXT1, buf, len, "match_memory1");

	wait_for_failover();

	con0 = gfs_pio_metadb(gf0);
	con1 = gfs_pio_metadb(gf1);
	assert(con0 == con1);

	/* gfm_connection in gf0 will be purged */
	msg("gf[%d]: close\n", 1);
	/* no faileover */
	chkerr_n(gfs_pio_close(gf1), "close", 1);
	msg("gf[%d]: close ok\n", 1);

	msg("gf[%d]: read %d bytes\n", 0, sz);
	chkerr_n(gfs_pio_read(gf0, buf, sz, &len), "read0", 1);
	msg("gf[%d]: read %d bytes ok\n", 0, len);
	match_memory(TEXT1, buf, len, "match_memory0");

	msg("gf[%d]: close\n", 0);
	chkerr_n(gfs_pio_close(gf0), "close", 0);
	msg("gf[%d]: close ok\n", 0);
}

static void
test_recvfile_close_recvfile(const char **argv)
{
	const char *path = argv[0];
	const char *local_dir = argv[1];
	char localtmp0[PATH_MAX], localtmp1[PATH_MAX];
	GFS_File gf0, gf1;
	int lfd0, lfd1;
	struct gfm_connection *con0, *con1;
	size_t sz = 10;
	gfarm_off_t len;

	snprintf(localtmp0, sizeof localtmp0, "%s/%d", local_dir, 0);
	lfd0 = open(localtmp0, O_RDWR|O_CREAT|O_TRUNC, 0777);
	chkerrno_n(lfd0, "open#0", 0);

	snprintf(localtmp1, sizeof localtmp1, "%s/%d", local_dir, 1);
	lfd1 = open(localtmp1, O_RDWR|O_CREAT|O_TRUNC, 0777);
	chkerrno_n(lfd1, "open#1", 1);

	msg("gf[%d]: gfs_pio_open %s\n", 0, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf0),
	    "gfs_pio_open#0", 0);
	msg("gf[%d]: gfs_pio_open ok\n", 0);

	msg("gf[%d]: gfs_pio_open %s\n", 1, path);
	chkerr_n(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf1),
	    "gfs_pio_open#1", 1);
	msg("gf[%d]: gfs_pio_open ok\n", 1);

	msg("gf[%d]: gfs_pio_recvfile %d bytes\n", 1, sz);
	chkerr_n(gfs_pio_recvfile(gf1, 0, lfd1, 0, sz, &len),
	    "gfs_pio_recvfile#0", 1);
	msg("gf[%d]: gfs_pio_recvfile %d bytes ok\n", 1, len);
	match_file(TEXT1, lfd1, 0, sz, "match_file#1");

	wait_for_failover();

	con0 = gfs_pio_metadb(gf0);
	con1 = gfs_pio_metadb(gf1);
	assert(con0 == con1);

	/* gfm_connection in gf0 will be purged */
	msg("gf[%d]: gfs_pio_close\n", 1);
	/* no faileover */
	chkerr_n(gfs_pio_close(gf1), "gfs_pio_close", 1);
	msg("gf[%d]: gfs_pio_close ok\n", 1);

	msg("gf[%d]: gfs_pio_recvfile %d bytes\n", 0, sz);
	chkerr_n(gfs_pio_recvfile(gf0, 0, lfd0, 0, sz, &len),
	    "gfs_pio_recvfile#1", 1);
	msg("gf[%d]: gfs_pio_recvfile %d bytes ok\n", 0, len);
	match_file(TEXT1, lfd0, 0, sz, "match_file#0");

	msg("gf[%d]: gfs_pio_close\n", 0);
	chkerr_n(gfs_pio_close(gf0), "close", 0);
	msg("gf[%d]: gfs_pio_close ok\n", 0);

	chkerrno(close(lfd1), "close#1");
	chkerrno(close(lfd0), "close#0");
}

static void
test_write_long_loop(const char **argv)
{
#define WRITE_LOOP_BUFSZ 512
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	const char *path_base = argv[0];
	int tmlimit = atoi(argv[1]);
	int files = atoi(argv[2]);
	int blocks = atoi(argv[3]);
	int filesz = blocks * WRITE_LOOP_BUFSZ;
	char cmd[1024];
	GFS_File *gf;
	size_t *fsz;
	int loop = 0, wstatus, r, i, len, rest_files;
	char **path;
	char buf[WRITE_LOOP_BUFSZ];
	struct gfs_stat st;
	struct timeval tm;

	gf = (GFS_File *)malloc(files * sizeof(GFS_File));
	fsz = (size_t *)malloc(files * sizeof(size_t));
	path = (char **)malloc(files * sizeof(char *));
	for (i = 0; i < files; ++i)
		path[i] = malloc(PATH_MAX);

	snprintf(cmd, sizeof cmd, "%s/failover-loop-start.sh %d &",
	    srcdir, tmlimit);
	wstatus = system(cmd);
	r = WEXITSTATUS(wstatus);
	if (r != 0) {
		msg("failed to exec \"%s\"\n", cmd);
		exit(1);
	}

	gettimeofday(&tm, NULL);
	tm.tv_sec += tmlimit;
	memset(buf, 'a', WRITE_LOOP_BUFSZ);

	for (i = 0; i < files; ++i) {
		snprintf(path[i], PATH_MAX, "%s.%d", path_base, i);
		(void)gfs_remove(path[i]);
	}

	for (;; ++loop) {
		msg("loop: %d\n", loop);
		for (i = 0; i < files; ++i) {
			msg("create: %s\n", path[i]);
			e = gfs_pio_create(path[i], GFARM_FILE_WRONLY, 0777,
			    &gf[i]);
			if (e != GFARM_ERR_NO_ERROR) {
				msg("gf[%d]: create: %s\n", i,
				    gfarm_error_string(e));
				goto end;
			}
			msg("create: %s ok\n", path[i]);
		}
		rest_files = files;
		memset(fsz, 0, files * sizeof(size_t));
		while (rest_files > 0) {
			for (i = 0; i < files; ++i) {
				e = gfs_pio_write(gf[i], buf,
				    WRITE_LOOP_BUFSZ, &len);
				if (e != GFARM_ERR_NO_ERROR) {
					msg("gf[%d]: write: %s\n", i,
					    gfarm_error_string(e));
					goto end;
				}
				fsz[i] += len;
				if (fsz[i] == filesz)
					--rest_files;
			}
		}
		for (i = 0; i < files; ++i) {
			msg("close: %s\n", path[i]);
			e = gfs_pio_close(gf[i]);
			if (e != GFARM_ERR_NO_ERROR) {
				msg("gf[%d]: close: %s\n", i,
				    gfarm_error_string(e));
				goto end;
			}
			msg("close: %s ok\n", path[i]);
			short_sleep();
		}
		for (i = 0; i < files; ++i) {
			msg("stat: %s\n", path[i]);
			e = gfs_stat(path[i], &st);
			if (e != GFARM_ERR_NO_ERROR) {
				msg("gf[%d]: stat: %s\n", i,
				    gfarm_error_string(e));
				goto end;
			}
			if (st.st_size != fsz[i]) {
				msg("gf[%d]: size: expected %ld but %ld \n", i,
				    (long)fsz[i], (long)st.st_size);
				e = GFARM_ERR_UNKNOWN;
				goto end;
			}
			msg("stat: %s ok\n", path[i]);
		}
		for (i = 0; i < files; ++i) {
			msg("remove: %s\n", path[i]);
			e = gfs_remove(path[i]);
			/*
			 * if failover occurs during removing file,
			 * a retry may fail by the error
			 * GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY.
			 */
			if (e != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
					msg("gf[%d]: remove(%s): "
					    "ignored error: %s\n", i,
					    path[i], gfarm_error_string(e));
				} else {
					msg("gf[%d]: remove(%s): %s\n", i,
					    path[i], gfarm_error_string(e));
					goto end;
				}
			}
			msg("remove ok: %s\n", path[i]);
		}

		if (gfarm_timeval_is_expired(&tm))
			break;
	}
end:
	snprintf(cmd, sizeof(cmd), "%s/failover-loop-end.sh", srcdir);
	wstatus = system(cmd);
	r = WEXITSTATUS(wstatus);
	if (r != 0) {
		msg("failed to exec \"%s\"\n", cmd);
		exit(1);
	}
	if (e != GFARM_ERR_NO_ERROR)
		exit(1);
	free(gf);
	free(fsz);
	for (i = 0; i < files; ++i)
		free(path[i]);
	free(path);
}

static void
test_sendfile_long_loop(const char **argv)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	const char *path_base = argv[0];
	const char *local_path = argv[1];
	int tmlimit = atoi(argv[2]);
	int files = atoi(argv[3]);
	int blocks = atoi(argv[4]);
	int chunksz = strlen(TEXT1);
	int filesz = blocks * chunksz;
	char cmd[1024];
	GFS_File *gf;
	int lfd;
	off_t *fsz;
	int loop = 0, wstatus, r, i, rest_files;
	gfarm_off_t len;
	char **path;
	struct gfs_stat st;
	struct timeval tm;

	lfd = open(local_path, O_RDONLY);
	chkerrno(lfd, "open");

	gf = (GFS_File *)malloc(files * sizeof(gf[0]));
	fsz = (off_t *)malloc(files * sizeof(fsz[0]));
	path = (char **)malloc(files * sizeof(path[0]));
	for (i = 0; i < files; ++i)
		path[i] = malloc(PATH_MAX);

	snprintf(cmd, sizeof cmd, "%s/failover-loop-start.sh %d &",
	    srcdir, tmlimit);
	wstatus = system(cmd);
	r = WEXITSTATUS(wstatus);
	if (r != 0) {
		msg("failed to exec \"%s\"\n", cmd);
		exit(1);
	}

	gettimeofday(&tm, NULL);
	tm.tv_sec += tmlimit;

	for (i = 0; i < files; ++i) {
		snprintf(path[i], PATH_MAX, "%s.%d", path_base, i);
		(void)gfs_remove(path[i]);
	}

	for (;; ++loop) {
		msg("loop: %d\n", loop);
		for (i = 0; i < files; ++i) {
			msg("gfs_pio_create: %s\n", path[i]);
			e = gfs_pio_create(path[i], GFARM_FILE_WRONLY, 0777,
			    &gf[i]);
			if (e != GFARM_ERR_NO_ERROR) {
				msg("gf[%d]: gfs_pio_create: %s\n", i,
				    gfarm_error_string(e));
				goto end;
			}
			msg("gfs_pio_create: %s ok\n", path[i]);
		}
		rest_files = files;
		for (i = 0; i < files; ++i)
			fsz[i] = 0;
		while (rest_files > 0) {
			for (i = 0; i < files; ++i) {
				e = gfs_pio_sendfile(gf[i], fsz[i], lfd, 0,
				    chunksz, &len);
				if (e != GFARM_ERR_NO_ERROR) {
					msg("gf[%d]: gfs_pio_sendfile: %s\n",
					    i, gfarm_error_string(e));
					goto end;
				}
				fsz[i] += len;
				if (fsz[i] == filesz)
					--rest_files;
			}
		}
		for (i = 0; i < files; ++i) {
			msg("gfs_pio_close: %s\n", path[i]);
			e = gfs_pio_close(gf[i]);
			if (e != GFARM_ERR_NO_ERROR) {
				msg("gf[%d]: gfs_pio_close: %s\n", i,
				    gfarm_error_string(e));
				goto end;
			}
			msg("close: %s ok\n", path[i]);
			short_sleep();
		}
		for (i = 0; i < files; ++i) {
			msg("gfs_stat: %s\n", path[i]);
			e = gfs_stat(path[i], &st);
			if (e != GFARM_ERR_NO_ERROR) {
				msg("gf[%d]: gfs_stat: %s\n", i,
				    gfarm_error_string(e));
				goto end;
			}
			if (st.st_size != fsz[i]) {
				msg("gf[%d]: size: expected %ld but %ld \n", i,
				    (long)fsz[i], (long)st.st_size);
				e = GFARM_ERR_UNKNOWN;
				goto end;
			}
			msg("gfs_stat: %s ok\n", path[i]);
		}
		for (i = 0; i < files; ++i) {
			msg("gfs_remove: %s\n", path[i]);
			e = gfs_remove(path[i]);
			/*
			 * if failover occurs during removing file,
			 * a retry may fail by the error
			 * GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY.
			 */
			if (e != GFARM_ERR_NO_ERROR) {
				if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
					msg("gf[%d]: gfs_remove(%s): "
					    "ignored error: %s\n", i,
					    path[i], gfarm_error_string(e));
				} else {
					msg("gf[%d]: gfs_remove(%s): %s\n", i,
					    path[i], gfarm_error_string(e));
					goto end;
				}
			}
			msg("gfs_remove ok: %s\n", path[i]);
		}

		if (gfarm_timeval_is_expired(&tm))
			break;
	}
end:
	snprintf(cmd, sizeof(cmd), "%s/failover-loop-end.sh", srcdir);
	wstatus = system(cmd);
	r = WEXITSTATUS(wstatus);
	if (r != 0) {
		msg("failed to exec \"%s\"\n", cmd);
		exit(1);
	}
	if (e != GFARM_ERR_NO_ERROR)
		exit(1);
	free(gf);
	free(fsz);
	for (i = 0; i < files; ++i)
		free(path[i]);
	free(path);

	chkerrno(close(lfd), "close");
}

static void
test_getxattr0(const char **argv, int follow, int xml)
{
	const char *path = argv[0];
	GFS_File gf;
	char val[STR_BUFSIZE];
	size_t sz;
	const char *diag = xml ? (follow ? "getxmlattr" : "lgetxmlattr") :
				follow ? "getxattr" : "lgetxattr";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr((xml ? (follow ? gfs_getxmlattr : gfs_lgetxmlattr) :
		    (follow ? gfs_getxattr : gfs_lgetxattr))
	    (path, xml ? USER_XMLATTR : USER_XATTR, val, &sz), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_getxattr(const char **argv)
{
	test_getxattr0(argv, 1, 0);
}

static void
test_lgetxattr(const char **argv)
{
	test_getxattr0(argv, 0, 0);
}

static void
test_getattrplus0(const char **argv, int follow)
{
	const char *path = argv[0];
	GFS_File gf;
	char *patterns[] = { "*" };
	char **names, **vals;
	int nattrs;
	size_t *sizes;
	struct gfs_stat gfst;
	const char *diag = follow ? "getattrplus" : "lgetattrplus";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr((follow ? gfs_getattrplus : gfs_lgetattrplus)
	    (path, patterns, 1, 0, &gfst, &nattrs, &names,
	    (void ***)(char *)&vals, &sizes), diag);
	msg("%s ok\n", diag);
	free(names);
	free(vals);
	free(sizes);
	gfs_stat_free(&gfst);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_getattrplus(const char **argv)
{
	test_getattrplus0(argv, 1);
}

static void
test_lgetattrplus(const char **argv)
{
	test_getattrplus0(argv, 0);
}

static void
test_setxattr0(const char **argv, int follow, int xml)
{
	const char *path = argv[0];
	GFS_File gf;
	const char *val0 = xml ? XML1 : TEXT1;
	char name[STR_BUFSIZE], val[STR_BUFSIZE];
	size_t sz = strlen(val0);
	const char *diag = xml ? (follow ? "setxmlattr" : "lsetxmlattr") :
				(follow ? "setxattr" : "lsetxattr");
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	memcpy(val, val0, sz);
	if (xml)
		val[sz++] = 0; /* xml must be null-termination string */

	wait_for_failover();

	snprintf(name, sizeof name, "user.%s.%ld", diag, time0);

	msg("%s\n", diag);
	chkerr((xml ? (follow ? gfs_setxmlattr : gfs_lsetxmlattr) :
			(follow ? gfs_setxattr : gfs_lsetxattr))
	    (path, name, val, sz, GFS_XATTR_CREATE), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_setxattr(const char **argv)
{
	test_setxattr0(argv, 1, 0);
}

static void
test_lsetxattr(const char **argv)
{
	test_setxattr0(argv, 0, 0);
}

static void
test_removexattr0(const char **argv, int follow, int xml)
{
	const char *path = argv[0];
	GFS_File gf;
	char name[STR_BUFSIZE], val[STR_BUFSIZE];
	const char *val0 = xml ? XML1 : TEXT1;
	size_t sz = strlen(val0);
	const char *diag1 = xml ? (follow ? "setxmlattr" : "lsetxmlattr") :
				(follow ? "setxattr" : "lsetxattr");
	const char *diag2 = xml ?
				(follow ? "removexmlattr" : "lremovexmlattr") :
				(follow ? "removexattr" : "lremovexattr");
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	memcpy(val, val0, sz);
	if (xml)
		val[sz++] = 0; /* xml must be null-termination string */

	snprintf(name, sizeof name, "user.removexattr.%d.%ld", follow, time0);
	msg("%s\n", diag1);
	chkerr((xml ? (follow ? gfs_setxmlattr : gfs_lsetxmlattr) :
		    (follow ? gfs_setxattr : gfs_lsetxattr))
	    (path, name, val, sz, GFS_XATTR_CREATE), diag1);
	msg("%s ok\n", diag1);

	wait_for_failover();

	msg("%s\n", diag2);
	chkerr((xml ? (follow ? gfs_removexmlattr : gfs_lremovexmlattr) :
		(follow ? gfs_removexattr : gfs_lremovexattr))
	    (path, name), diag2);
	msg("%s ok\n", diag2);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_removexattr(const char **argv)
{
	test_removexattr0(argv, 1, 0);
}

static void
test_lremovexattr(const char **argv)
{
	test_removexattr0(argv, 0, 0);
}

static void
test_fgetxattr(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	char val[STR_BUFSIZE];
	size_t sz = sizeof(val);
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("gf: fgetxattr\n");
	chkerr(gfs_fgetxattr(gf, USER_XATTR, val, &sz), "fgetxattr");
	msg("gf: fgetxattr ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

/* gfs_fsetxattr calls gfm_client_compound_modify_file_op */
static void
test_fsetxattr(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	const char *val = TEXT1;
	size_t sz = strlen(val);

	msg("gf: open %s\n", path);
	chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf), "open");
	msg("gf: open ok\n");

	wait_for_failover();

	msg("gf: fsetxattr\n");
	chkerr(gfs_fsetxattr(gf, USER_XATTR, val, sz, GFS_XATTR_REPLACE),
	    "fsetxattr");
	msg("gf: fsetxattr ok\n");

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_fremovexattr(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	char name[STR_BUFSIZE], val[STR_BUFSIZE] = TEXT1;
	size_t sz = strlen(val);
	const char *diag1 = "setxattr";
	const char *diag2 = "fremovexattr";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	snprintf(name, sizeof name, "user.fremovexattr.%ld", time0);
	msg("%s\n", diag1);
	chkerr(gfs_setxattr(path, name, val, sz, GFS_XATTR_CREATE), diag1);
	msg("%s ok\n", diag1);

	wait_for_failover();

	msg("%s\n", diag2);
	chkerr(gfs_fremovexattr(gf, name), diag2);
	msg("%s ok\n", diag2);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_listxattr0(const char **argv, int follow, int xml)
{
	const char *path = argv[0];
	GFS_File gf;
	char names[STR_BUFSIZE * 10];
	size_t sz;
	const char *diag = xml ? (follow ? "listxmlattr" : "llistxmlattr") :
				(follow ? "listxattr" : "llistxattr");
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr((xml ? (follow ? gfs_listxmlattr : gfs_llistxmlattr) :
		    (follow ? gfs_listxattr : gfs_llistxattr))
	    (path, names, &sz), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_listxattr(const char **argv)
{
	test_listxattr0(argv, 1, 0);
}

static void
test_llistxattr(const char **argv)
{
	test_listxattr0(argv, 0, 0);
}

static void
test_getxmlattr(const char **argv)
{
	test_getxattr0(argv, 1, 1);
}

static void
test_lgetxmlattr(const char **argv)
{
	test_getxattr0(argv, 0, 1);
}

static void
test_setxmlattr(const char **argv)
{
	test_setxattr0(argv, 1, 1);
}

static void
test_lsetxmlattr(const char **argv)
{
	test_setxattr0(argv, 0, 1);
}

static void
test_listxmlattr(const char **argv)
{
	test_listxattr0(argv, 1, 1);
}

static void
test_llistxmlattr(const char **argv)
{
	test_listxattr0(argv, 0, 1);
}

static void
test_removexmlattr(const char **argv)
{
	test_removexattr0(argv, 1, 1);
}

static void
test_lremovexmlattr(const char **argv)
{
	test_removexattr0(argv, 0, 1);
}

static void
test_findxmlattr(const char **argv)
{
	const char *path = argv[0];
	struct gfs_xmlattr_ctx *ctxp;
	GFS_File gf;
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("findxmlattr\n");
	chkerr(gfs_findxmlattr(path, "/", 0, &ctxp), "findxmlattr");
	msg("findxmlattr ok\n");
	assert(con != gfs_pio_metadb(gf));

	msg("closexmlattr\n");
	(void)gfs_closexmlattr(ctxp);
	msg("closexmlattr ok\n");
	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_getxmlent(const char **argv)
{
	const char *path = argv[0];
	struct gfs_xmlattr_ctx *ctxp;
	char *fpath, *attrname;

	msg("findxmlattr\n");
	chkerr(gfs_findxmlattr(path, "/", 0, &ctxp), "findxmlattr");
	msg("findxmlattr ok\n");

	wait_for_failover();

	for (;;) {
		msg("getxmlent\n");
		chkerr(gfs_getxmlent(ctxp, &fpath, &attrname), "getxmlent");
		msg("getxmlent ok\n");
		if (fpath == NULL)
			break;
	}

	msg("closexmlattr\n");
	(void)gfs_closexmlattr(ctxp);
	msg("closexmlattr ok\n");
}

static void
test_closexmlattr(const char **argv)
{
	const char *path = argv[0];
	struct gfs_xmlattr_ctx *ctxp;

	msg("findxmlattr\n");
	chkerr(gfs_findxmlattr(path, "/", 0, &ctxp), "findxmlattr");
	msg("findxmlattr ok\n");

	wait_for_failover();

	msg("closexmlattr\n");
	(void)gfs_closexmlattr(ctxp);
	msg("closexmlattr ok\n");
}

static void
test_schedule_hosts(const char **argv)
{
	const char *domain = argv[0];
	int nhosts, ports[1];
	char *hosts[1];
	struct gfarm_host_sched_info *infos;
	const char *diag1 = "schedule_hosts_domain_all";
	const char *diag2 = "schedule_hosts";

	msg("%s\n", diag1);
	chkerr(gfarm_schedule_hosts_domain_all("/", domain, &nhosts, &infos),
	    diag1);
	msg("%s ok\n", diag1);
	assert(nhosts > 0);

	wait_for_failover();

	msg("%s\n", diag2);
	chkerr(gfarm_schedule_hosts("/", 1, infos, 1, hosts, ports), diag2);
	msg("%s ok\n", diag2);

	gfarm_host_sched_info_free(nhosts, infos);
}

static void
test_schedule_hosts_domain_all(const char **argv)
{
	const char *domain = argv[0];
	int i, nhosts;
	struct gfarm_host_sched_info *infos;
	const char *diag = "schedule_hosts_domain_all";

	for (i = 0; i < 2; ++i) {
		msg("%s\n", diag);
		/*
		 * gfarm_schedule_hosts_domain_all does not access to gfmd
		 * in a typical pass ?
		 */
		chkerr(gfarm_schedule_hosts_domain_all("/", domain,
		    &nhosts, &infos), diag);
		msg("%s ok: nhosts=%d\n", diag, nhosts);
		gfarm_host_sched_info_free(nhosts, infos);

		if (i == 0)
			wait_for_failover();
	}
}

static void
test_schedule_hosts_domain_by_file(const char **argv)
{
	const char *path = argv[0];
	int i, nhosts;
	struct gfarm_host_sched_info *infos;
	const char *diag = "schedule_hosts_domain_by_file";

	for (i = 0; i < 2; ++i) {
		msg("%s\n", diag);
		chkerr(gfarm_schedule_hosts_domain_by_file(path, 0, "",
		    &nhosts, &infos), diag);
		msg("%s ok\n", diag);
		gfarm_host_sched_info_free(nhosts, infos);

		if (i == 0)
			wait_for_failover();
	}
}

static void
test_replica_info_by_name(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	struct gfs_replica_info *infos;
	const char *diag = "replica_info_by_name";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr(gfs_replica_info_by_name(path, 0, &infos), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));
	gfs_replica_info_free(infos);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_replica_list_by_name(const char **argv)
{
	const char *path = argv[0];
	GFS_File gf;
	int nhosts;
	char **hosts;
	const char *diag = "replica_list_by_name";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();

	msg("%s\n", diag);
	chkerr(gfs_replica_list_by_name(path, &nhosts, &hosts), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));
	gfarm_strings_free_deeply(nhosts, hosts);

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_replicate_to(const char **argv)
{
	char *path = (char *)argv[0], *host = (char *)argv[1];
	int port = atoi(argv[2]);
	GFS_File gf;
	const char *diag = "replicate_to";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();
	wait_for_gfsd(host);

	msg("%s\n", diag);
	chkerr(gfs_replicate_to(path, host, port), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_replicate_from_to(const char **argv)
{
	char *path = (char *)argv[0];
	char *shost = (char *)argv[1], *dhost = (char *)argv[3];
	int sport = atoi(argv[2]), dport = atoi(argv[4]);
	GFS_File gf;
	const char *diag = "replicate_from_to";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();
	wait_for_gfsd(shost);
	wait_for_gfsd(dhost);

	msg("%s\n", diag);
	chkerr(gfs_replicate_from_to(path, shost, sport, dhost, dport), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_replicate_file_to_request(const char **argv)
{
	char *path = (char *)argv[0], *host = (char *)argv[1];
	GFS_File gf;
	const char *diag = "replicate_file_to_request";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();
	wait_for_gfsd(host);

	msg("%s\n", diag);
	chkerr(gfs_replicate_file_to_request(path, host, 0), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_replicate_file_from_to_request(const char **argv)
{
	char *path = (char *)argv[0];
	char *shost = (char *)argv[1], *dhost = (char *)argv[2];
	GFS_File gf;
	const char *diag = "replicate_file_to_request";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();
	wait_for_gfsd(shost);
	wait_for_gfsd(dhost);

	msg("%s\n", diag);
	chkerr(gfs_replicate_file_from_to_request(path, shost, dhost, 0),
	    diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_replica_remove_by_file(const char **argv)
{
	char *path = (char *)argv[0], *host = (char *)argv[1];
	GFS_File gf;
	const char *diag = "replica_remove_by_file";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();
	wait_for_gfsd(host);

	msg("%s\n", diag);
	chkerr(gfs_replica_remove_by_file(path, host), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_migrate_to(const char **argv)
{
	char *path = (char *)argv[0], *host = (char *)argv[1];
	int port = atoi(argv[2]);
	GFS_File gf;
	const char *diag = "migrate_to";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();
	wait_for_gfsd(host);

	msg("%s\n", diag);
	chkerr(gfs_migrate_to(path, host, port), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

static void
test_migrate_from_to(const char **argv)
{
	char *path = (char *)argv[0];
	char *shost = (char *)argv[1], *dhost = (char *)argv[3];
	int sport = atoi(argv[2]), dport = atoi(argv[4]);
	GFS_File gf;
	const char *diag = "migrate_to";
	struct gfm_connection *con = cache_gfm_connection(&gf, path);

	wait_for_failover();
	wait_for_gfsd(shost);
	wait_for_gfsd(dhost);

	msg("%s\n", diag);
	chkerr(gfs_migrate_from_to(path, shost, sport, dhost, dport), diag);
	msg("%s ok\n", diag);
	assert(con != gfs_pio_metadb(gf));

	msg("gf: close\n");
	chkerr(gfs_pio_close(gf), "close");
}

struct type_info {
	const char *opt;
	int argc;
	void (*run)(const char **);
} type_infos[] = {
	/* basic */
	{ "realpath",		1, test_realpath },
	{ "rename",		1, test_rename },
	{ "statfs",		1, test_statfs },
	{ "statfsnode",		2, test_statfsnode },
	{ "chmod",		1, test_chmod },
	{ "lchmod",		1, test_lchmod },
	{ "chown",		1, test_chown },
	{ "lchown",		1, test_lchown },
	{ "readlink",		1, test_readlink },
	{ "stat",		1, test_stat },
	{ "lstat",		1, test_lstat },
	{ "fstat",		1, test_fstat },
	{ "stat_cksum",		1, test_stat_cksum },
	{ "fstat_cksum",	1, test_fstat_cksum },
	{ "pio_cksum",		1, test_pio_cksum },
	{ "utimes",		1, test_utimes },
	{ "lutimes",		1, test_lutimes },
	{ "remove",		1, test_remove },
	{ "unlink",		1, test_unlink },
	{ "link",		1, test_link },
	{ "symlink",		1, test_symlink },
	{ "mkdir",		2, test_mkdir },
	{ "rmdir",		2, test_rmdir },
	{ "opendir",		2, test_opendir },
	{ "opendirplus",	2, test_opendirplus },
	{ "opendirplusxattr",	2, test_opendirplusxattr },
	{ "closedir",		1, test_closedir },
	{ "closedirplus",	1, test_closedirplus },
	{ "closedirplusxattr",	1, test_closedirplusxattr },
	{ "readdir",		2, test_readdir },
	{ "readdir2",		2, test_readdir2 },
	{ "readdirplus",	2, test_readdirplus },
	{ "readdirplusxattr",	2, test_readdirplusxattr },
	{ "seekdir",		2, test_seekdir },
	{ "seekdirplusxattr",	2, test_seekdirplusxattr },

	/* gfs_pio */
	{ "sched-read",		1, test_sched_read },
	{ "sched-recvfile",	2, test_sched_recvfile },
	{ "sched-open-write",	1, test_sched_open_write },
	{ "sched-create-write",	1, test_sched_create_write },
	{ "sched-open-sendfile", 2, test_sched_open_sendfile },
	{ "sched-create-sendfile", 2, test_sched_create_sendfile },
	{ "fhopen-file",	1, test_fhopen_file },
	{ "close",		1, test_close },
	{ "close-open",		1, test_close_open },
	{ "close-open2",	1, test_close_open2 },
	{ "read",		1, test_read },
	{ "read-stat",		1, test_read_stat },
	{ "open-read-loop",	1, test_open_read_loop },
	{ "recvfile",		2, test_recvfile },
	{ "recvfile-stat",	2, test_recvfile_stat },
	{ "open-recvfile-loop",	2, test_open_recvfile_loop },
	{ "getc",		1, test_getc },
	{ "seek",		1, test_seek },
	{ "seek-dirty",		1, test_seek_dirty },
	{ "write",		1, test_write },
	{ "write-stat",		1, test_write_stat },
	{ "sendfile",		2, test_sendfile },
	{ "sendfile-stat",	2, test_sendfile_stat },
	{ "putc",		1, test_putc },
	{ "truncate",		1, test_truncate },
	{ "flush",		1, test_flush },
	{ "sync",		1, test_sync },
	{ "datasync",		1, test_datasync },
	{ "read-close-read",	1, test_read_close_read },
	{ "recvfile-close-recvfile", 2, test_recvfile_close_recvfile },
	{ "write-long-loop",	4, test_write_long_loop },
	{ "sendfile-long-loop",	5, test_sendfile_long_loop },

	/* xattr/xmlattr */
	{ "getxattr",		1, test_getxattr },
	{ "lgetxattr",		1, test_lgetxattr },
	{ "getattrplus",	1, test_getattrplus },
	{ "lgetattrplus",	1, test_lgetattrplus },
	{ "setxattr",		1, test_setxattr },
	{ "lsetxattr",		1, test_lsetxattr },
	{ "removexattr",	1, test_removexattr },
	{ "lremovexattr",	1, test_lremovexattr },
	{ "fgetxattr",		1, test_fgetxattr },
	{ "fsetxattr",		1, test_fsetxattr },
	{ "fremovexattr",	1, test_fremovexattr },
	{ "listxattr",		1, test_listxattr },
	{ "llistxattr",		1, test_llistxattr },
	{ "getxmlattr",		1, test_getxmlattr },
	{ "lgetxmlattr",	1, test_lgetxmlattr },
	{ "setxmlattr",		1, test_setxmlattr },
	{ "lsetxmlattr",	1, test_lsetxmlattr },
	{ "listxmlattr",	1, test_listxmlattr },
	{ "llistxmlattr",	1, test_llistxmlattr },
	{ "removexmlattr",	1, test_removexmlattr },
	{ "lremovexmlattr",	1, test_lremovexmlattr },
	{ "findxmlattr",	1, test_findxmlattr },
	{ "getxmlent",		1, test_getxmlent },
	{ "closexmlattr",	1, test_closexmlattr },

	/* scheduling */
	{ "shhosts",		1, test_schedule_hosts },
	{ "shhosts-domainall",	1, test_schedule_hosts_domain_all },
	{ "shhosts-domainfile",	1, test_schedule_hosts_domain_by_file },

	/* replication */
	{ "rep-info",		1, test_replica_info_by_name },
	{ "rep-list",		1, test_replica_list_by_name },
	{ "rep-to",		3, test_replicate_to },
	{ "rep-fromto",		5, test_replicate_from_to },
	{ "rep-toreq",		2, test_replicate_file_to_request },
	{ "rep-fromtoreq",	3, test_replicate_file_from_to_request },
	{ "rep-remove",		2, test_replica_remove_by_file },
	{ "migrate-to",		3, test_migrate_to },
	{ "migrate-fromto",	5, test_migrate_from_to },

	/*
	 * following function are not tested in this program
	 * but guaranteed to support gfmd connection failover
	 * feature because they access gfmd by tested functions above.
	 *
	 * basic:
	 *
	 *   gfs_telldir()
	 *
	 * gfs_pio:
	 *
	 *   gfs_pio_readline()
	 *   gfs_pio_readdelim()
	 *   gfs_pio_gets()
	 *   gfs_pio_getline()
	 *
	 * scheduling:
	 *
	 *   gfarm_schedule_hosts_to_write()
	 *   gfarm_schedule_hosts_acyclic()
	 *   gfarm_schedule_hosts_acyclic_to_write()
	 *
	 * replication:
	 *
	 *   gfs_replicate_to_local()
	 *
	 * cache:
	 *
	 *   gfs_stat_caching()
	 *   gfs_lstat_caching()
	 *   gfs_stat_cached()
	 *   gfs_lstat_cached()
	 *   gfs_getxattr_caching()
	 *   gfs_lgetxattr_caching()
	 *   gfs_getxattr_cached()
	 *   gfs_lgetxattr_cached()
	 *   gfs_opendir_caching()
	 *
	 * acl:
	 *
	 *   gfs_acl_get_file()
	 *   gfs_acl_get_file_cached()
	 *   gfs_acl_set_file()
	 *   gfs_acl_delete_def_file()
	 */
};

static void
usage()
{
	int i;

	fprintf(stderr,
	    "\n"
	    "usage: gfs_pio_failover [-auto] <TYPE> <TEST PARAMETER>\n"
	    "\n"
	    "  [TYPE]\n"
	    "\n");

	for (i = 0; i < sizeof(type_infos) / sizeof(struct type_info); ++i)
		fprintf(stderr, "    %s\n", type_infos[i].opt);
	fprintf(stderr, "\n");

	exit(1);
}

int
main(int argc, char **argv)
{
	const char *type_opt;
	int i, validtype = 0;
	struct type_info *ti;

	argv++;
	argc--;
	if (argc == 0)
		usage();

	if (strcmp(*argv, "-auto") == 0) {
		auto_failover = 1;
		argv++;
		argc--;
	}

	type_opt = *argv++;
	argc--;

	for (i = 0; i < sizeof(type_infos) / sizeof(struct type_info); ++i) {
		ti = &type_infos[i];
		if (strcmp(ti->opt, type_opt) == 0) {
			validtype = 1;
			break;
		}
	}

	if (!validtype) {
		fprintf(stderr, "Invalid type: %s\n", type_opt);
		exit(1);
	}

	srcdir = getenv("srcdir");
	if (srcdir == NULL)
		srcdir = ".";

	msg("<<test: %s>>\n", type_opt);

	if (ti->argc != argc) {
		fprintf(stderr, "Invalid number of arguments\n");
		exit(1);
	}

	chkerr(gfarm_initialize(&argc, &argv), "initialize");
	gflog_set_priority_level(LOG_DEBUG);
	gflog_set_message_verbose(2);
	time0 = (long)time(NULL);

	ti->run((const char **)argv);
	chkerr(gfarm_terminate(), "terminate");

	/* if error occurred, this program will exit immediately. */
	msg("OK\n");

	return (0);
}
