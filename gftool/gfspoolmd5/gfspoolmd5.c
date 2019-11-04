/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <float.h>

#include <openssl/evp.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#define GFARM_USE_OPENSSL
#include "msgdigest.h"

#include "config.h"
#include "lookup.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "gfs_pio.h"

const char *progname = "gfspoolmd5";

static struct gfm_connection *gfm_server;
static long long total_count, total_size;
static long long count, size, start_count;
static struct timeval itime, start_time;
const char PROGRESS_FILE[] = ".md5.count";
long long *progress_addr;
static int mtime_max_day = -1, mtime_min_day = -1;
int foreground, cksum_check = 1;
static char *op_host;

/*
 * File format should be consistent with gfsd_local_path() in gfsd.c
 */
static int
get_inum_gen(const char *path, gfarm_ino_t *inump, gfarm_uint64_t *genp)
{
	unsigned int inum32, inum24, inum16, inum8, inum0;
	unsigned int gen32, gen0;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;

	if (sscanf(path, "data/%08X/%02X/%02X/%02X/%02X%08X%08X",
		   &inum32, &inum24, &inum16, &inum8, &inum0, &gen32, &gen0)
	    != 7)
		return (-1);

	inum = ((gfarm_ino_t)inum32 << 32) + (inum24 << 24) +
		(inum16 << 16) + (inum8 << 8) + inum0;
	gen = ((gfarm_uint64_t)gen32 << 32) + gen0;
	*inump = inum;
	*genp = gen;

	return (0);
}

static int
mtime_filter(struct stat *stp)
{
	int d = itime.tv_sec - stp->st_mtime;

	return ((mtime_max_day < 0 || d < mtime_max_day) &&
		(mtime_min_day < 0 || d >= mtime_min_day));
}

static gfarm_error_t
dir_foreach(
	gfarm_error_t (*op_file)(char *, struct stat *, void *),
	gfarm_error_t (*op_dir1)(char *, struct stat *, void *),
	gfarm_error_t (*op_dir2)(char *, struct stat *, void *),
	char *dir, void *arg)
{
	DIR *dirp;
	struct dirent *dp;
	struct stat st;
	gfarm_error_t e;
	char *dir1;

	if (lstat(dir, &st))
		return (gfarm_errno_to_error(errno));

	if (S_ISREG(st.st_mode)) {
		if (op_file != NULL)
			return (op_file(dir, &st, arg));
		else
			return (GFARM_ERR_NO_ERROR);
	} else if (!S_ISDIR(st.st_mode))
		return (GFARM_ERR_INVALID_ARGUMENT); /* XXX */

	if (op_dir1 != NULL) {
		e = op_dir1(dir, &st, arg);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if ((dirp = opendir(dir)) == NULL)
		return (gfarm_errno_to_error(errno));

	/* if dir is '.', remove it */
	if (dir[0] == '.' && dir[1] == '\0')
		dir = "";
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
		    (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
			continue;

		GFARM_MALLOC_ARRAY(dir1, strlen(dir) + strlen(dp->d_name) + 2);
		if (dir1 == NULL) {
			closedir(dirp);
			return (GFARM_ERR_NO_MEMORY);
		}
		strcpy(dir1, dir);
		if (strcmp(dir, "") != 0 && dir[strlen(dir) - 1] != '/')
			strcat(dir1, "/");
		strcat(dir1, dp->d_name);
		e = dir_foreach(op_file, op_dir1, op_dir2, dir1, arg);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003787, "%s: %s", dir1,
			    gfarm_error_string(e));
		free(dir1);
	}
	if (closedir(dirp))
		return (gfarm_errno_to_error(errno));
	if (op_dir2 != NULL)
		return (op_dir2(dir, &st, arg));
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
calc_digest(const char *file,
	const char *md_type_name, char *md_string, size_t *md_strlenp)
{
#define BUFSIZE	65536
	char buf[BUFSIZE];
	ssize_t sz;
	int fd;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	EVP_MD_CTX *md_ctx;
	size_t md_len;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	int cause;

	md_ctx = gfarm_msgdigest_alloc_by_name(md_type_name, &cause);
	if (md_ctx == NULL)
		gflog_fatal(GFARM_MSG_1004521, "%s: fatal error. "
		    "digest type <%s> - %s",
		    file, md_type_name, cause != 0 ? strerror(cause) :
		    "digest calculation disabled");

	if ((fd = open(file, O_RDONLY)) == -1)
		return (gfarm_errno_to_error(errno));

	while ((sz = read(fd, buf, sizeof buf)) > 0)
		EVP_DigestUpdate(md_ctx, buf, sz);
	if (sz == -1)
		e = gfarm_errno_to_error(errno);

	md_len = gfarm_msgdigest_free(md_ctx, md_value);
	if (e == GFARM_ERR_NO_ERROR)
		*md_strlenp = gfarm_msgdigest_to_string(
		    md_string, md_value, md_len);
	close(fd);
	return (e);
}

static void
show_progress(void)
{
	struct timeval t;
	int min, hour;
	double time, bw, sec;

	gettimeofday(&t, NULL);
	time = t.tv_sec - start_time.tv_sec +
	    .000001 * (t.tv_usec - start_time.tv_usec);
	bw = time == 0 ? DBL_MAX : size / 1000.0 / time;
	sec = bw == 0.0 ? INT_MAX : (total_size - size) / 1000.0 / bw;
	hour = sec / 3600;
	min = (sec - hour * 3600) / 60;
	sec = sec - hour * 3600 - min * 60;

	printf("file: %lld/%lld size: %lld/%lld (%2lld%%) "
	       "ave: %.2f KB/s Est: %d:%02d:%02d \r",
	       count, total_count, size, total_size,
	       total_size == 0 ? 100 : size * 100 / total_size,
	       bw, hour, min, (int)sec);
	fflush(stdout);
}

static void
show_statistics(void)
{
	struct timeval t;
	int min, hour;
	double time, bw, sec;

	gettimeofday(&t, NULL);
	time = t.tv_sec - start_time.tv_sec +
	    .000001 * (t.tv_usec - start_time.tv_usec);
	bw = time == 0 ? DBL_MAX : size / 1000.0 / time;
	hour = time / 3600;
	min = (time - hour * 3600) / 60;
	sec = time - hour * 3600 - min * 60;

	gflog_info(GFARM_MSG_1004282,
	    "file: %lld size: %lld time: %d:%02d:%02d bandwidth: %.2f KB/s",
	    total_count, total_size, hour, min, (int)sec, bw);
}

static gfarm_error_t
check_file_size(GFS_File gf, char *file)
{
	struct gfs_stat st;
	struct stat sb;
	gfarm_error_t e;
	int r;

	if (stat(file, &sb) == -1)
		return (gfarm_errno_to_error(errno));
	else if ((e = gfs_fstat(gf, &st)) != GFARM_ERR_NO_ERROR)
		return (e);
	r = sb.st_size == st.st_size;
	gfs_stat_free(&st);
	return (r ? GFARM_ERR_NO_ERROR : GFARM_ERR_INVALID_FILE_REPLICA);
}

static gfarm_error_t
read_file(char *file, struct stat *stp, void *arg)
{
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	GFS_File gf;
	int fd;
	gfarm_error_t e, e2;

	if (!mtime_filter(stp))
		return (GFARM_ERR_NO_ERROR);
	if (get_inum_gen(file, &inum, &gen))
		return (GFARM_ERR_NO_ERROR);

	if (count < start_count) {
		count += 1;
		size += stp->st_size;
		return (GFARM_ERR_NO_ERROR);
	}
	e = gfs_pio_fhopen(inum, gen, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		goto progress;
	if ((fd = open("/dev/null", O_WRONLY)) == -1) {
		e = gfarm_errno_to_error(errno);
		goto close_progress;
	}
	/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
	e = gfs_pio_internal_set_view_section(gf, op_host);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfs_pio_recvfile(gf, 0, fd, 0, -1, NULL);
	close(fd);
close_progress:
	e2 = gfs_pio_close(gf);
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;
progress:
	count += 1;
	size += stp->st_size;
	if (foreground)
		show_progress();
	*progress_addr = count;
	return (e);
}

static gfarm_error_t
check_file(char *file, struct stat *stp, void *arg)
{
	struct gfs_stat_cksum c;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	GFS_File gf;
	gfarm_error_t e;
	size_t md_strlen = 0;
	char md_string[GFARM_MSGDIGEST_STRSIZE];

	if (!mtime_filter(stp))
		return (GFARM_ERR_NO_ERROR);
	if (get_inum_gen(file, &inum, &gen))
		return (GFARM_ERR_NO_ERROR);

	if (count < start_count) {
		count += 1;
		size += stp->st_size;
		return (GFARM_ERR_NO_ERROR);
	}
	e = gfs_pio_fhopen(inum, gen, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		goto progress;
	if ((e = gfs_fstat_cksum(gf, &c)) != GFARM_ERR_NO_ERROR)
		goto close_progress;
	if (c.type[0] == '\0') {
		/* do not calculate, if "digest" is not configured in gfmd */
		gflog_debug(GFARM_MSG_1004195,
		    "%s: cksum type is not specified", file);
		e = GFARM_ERR_NO_ERROR;
	} else if ((c.flags & (GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED|
	    GFM_PROTO_CKSUM_GET_EXPIRED)) != 0) {
		gflog_info(GFARM_MSG_1003788, "%s: cksum flag %d, skipped",
		    file, c.flags);
		e = GFARM_ERR_NO_ERROR;
	} else if ((e = check_file_size(gf, file)) != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1003803, "%s: size mismatch, skipped: %s",
		   file, gfarm_error_string(e));
		e = GFARM_ERR_NO_ERROR;
	} else if (c.len > 0 && !cksum_check) {
		e = GFARM_ERR_NO_ERROR;
	} else if ((e = calc_digest(file, c.type, md_string, &md_strlen))
	    != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_OPERATION_NOT_SUPPORTED) {
			gflog_warning(GFARM_MSG_1004196,
			    "%s: cksum type <%s> isn't supported on this host",
			    file, c.type);
		} else if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
			/*
			 * GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY means
			 * this file is updated simultaneously
			 */
		} else {
			gflog_warning(GFARM_MSG_1004197, "%s: %s",
			    file, gfarm_error_string(e));
		}
	} else if (c.len > 0 &&
	    (md_strlen != c.len || memcmp(md_string, c.cksum, c.len) != 0)) {
		gfs_stat_cksum_free(&c);
		if ((e = gfs_fstat_cksum(gf, &c)) != GFARM_ERR_NO_ERROR)
			goto close_progress;
		if (c.len > 0 && (c.flags & (GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED|
		    GFM_PROTO_CKSUM_GET_EXPIRED)) == 0) {
			e = GFARM_ERR_CHECKSUM_MISMATCH;
			gflog_error(GFARM_MSG_1003789, "%s: %s: file %.*s "
			    "mds %.*s", file, gfarm_error_string(e),
			    (int)md_strlen, md_string, (int)c.len, c.cksum);
		}
	} else {
		struct gfs_stat_cksum ck;

		ck.type = c.type;
		ck.len = md_strlen;
		ck.cksum = md_string;
		e = gfs_fstat_cksum_set(gf, &ck);
	}
	gfs_stat_cksum_free(&c);
close_progress:
	gfs_pio_close(gf);
progress:
	count += 1;
	size += stp->st_size;
	if (foreground)
		show_progress();
	*progress_addr = count;
	return (e);
}

static void
check_spool(char *dir, gfarm_error_t (*op)(char *, struct stat *, void *))
{
	gfarm_error_t e;

	e = dir_foreach(op, NULL, NULL, dir, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003790, "%s: %s", dir,
		    gfarm_error_string(e));
}

static gfarm_error_t
file_size(char *file, struct stat *stp, void *arg)
{
	gfarm_ino_t inum;
	gfarm_uint64_t gen;

	if (!mtime_filter(stp))
		return (GFARM_ERR_NO_ERROR);
	if (get_inum_gen(file, &inum, &gen))
		return (GFARM_ERR_NO_ERROR);

	total_count += 1;
	total_size += stp->st_size;
	return (GFARM_ERR_NO_ERROR);
}

static void
count_size(char *dir)
{
	(void)dir_foreach(file_size, NULL, NULL, dir, NULL);
}

static int
is_gfarmroot(void)
{
	const int root_inum = 2, root_gen = 0;
	GFS_Dir dir;
	gfarm_error_t e;

	e = gfs_fhopendir(root_inum, root_gen, &dir);
	if (e == GFARM_ERR_NO_ERROR) {
		gfs_closedir(dir);
		return (1);
	}
	return (0);
}

static long long *
mmap_progress_file(const char *file)
{
	int fd;
	int size = sizeof(long long);
	void *addr;

	if ((fd = open(file, O_RDWR)) == -1) {
		if ((fd = open(file, O_RDWR|O_CREAT|O_TRUNC, 0644)) == -1)
			gflog_fatal_errno(GFARM_MSG_1003791, "%s", file);
		if (ftruncate(fd, size) == -1)
			gflog_fatal_errno(GFARM_MSG_1003792, "ftruncate");
	}
	if ((addr = mmap(NULL, 8, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0))
	    == MAP_FAILED)
		gflog_fatal_errno(GFARM_MSG_1003793, "mmap");
	close(fd);
	return (addr);
}

static int
munmap_progress_file(void *addr)
{
	int size = sizeof(long long);

	return (munmap(addr, size));
}

static void
error_check(const char *msg, gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;
	gflog_fatal(GFARM_MSG_1003794, "%s: %s", msg, gfarm_error_string(e));
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [options] -r spool_root [dir ...]\n",
	    progname);
	fprintf(stderr, "       %s [options] -f -r spool_root [dir ...] ",
	    progname);
	fprintf(stderr, "2> log\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-n\tdo not calculate checksum\n");
	fprintf(stderr, "\t-m day\tselect files modified within day days\n");
	fprintf(stderr, "\t-M day\tselect files modified older than day ");
	fprintf(stderr, "days ago\n");
	fprintf(stderr, "\t-G\tread files in Gfarm API to move corrupted ");
	fprintf(stderr, "files to lost+found\n");
	fprintf(stderr, "\t-h host\tspecify the file system node to read.  ");
	fprintf(stderr, "Effective with -G option\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	char *spool_root = NULL;
	int c, i;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	gfarm_error_t (*op)(char *, struct stat *, void *) = check_file;

	if (argc > 0)
		progname = basename(argv[0]);

	while ((c = getopt(argc, argv, "fh:m:nr:GM:?")) != -1) {
		switch (c) {
		case 'f':
			foreground = 1;
			break;
		case 'h':
			op_host = optarg;
			break;
		case 'm':
			mtime_max_day = atoi(optarg) * 3600 * 24;
			break;
		case 'M':
			mtime_min_day = atoi(optarg) * 3600 * 24;
			break;
		case 'n':
			cksum_check = 0;
			break;
		case 'r':
			spool_root = optarg;
			break;
		case 'G':
			op = read_file;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (spool_root == NULL)
		usage();

	if (chdir(spool_root) == -1) {
		perror(spool_root);
		exit(1);
	}
	e = gfarm_initialize(&argc, &argv);
	error_check(progname, e);

	e = gfm_client_connection_and_process_acquire_by_path(
		GFARM_PATH_ROOT, &gfm_server);
	error_check(progname, e);

	if (!is_gfarmroot()) {
		fprintf(stderr, "You are not gfarmroot\n");
		exit(1);
	}
	gflog_set_identifier(progname);
	if (!foreground) {
		gflog_syslog_open(LOG_PID, syslog_facility);
		if (gfarm_daemon(1, 0) == -1)
			gflog_warning_errno(GFARM_MSG_1003795, "daemon");
		gflog_info(GFARM_MSG_1003796, "start");
	}
	progress_addr = mmap_progress_file(PROGRESS_FILE);
	start_count = *progress_addr;
	gettimeofday(&itime, NULL);
	for (i = 0; i < argc; ++i)
		count_size(argv[i]);
	gettimeofday(&start_time, NULL);
	for (i = 0; i < argc; ++i)
		check_spool(argv[i], op);
	if (i == 0) {
		count_size(".");
		gettimeofday(&start_time, NULL);
		check_spool(".", op);
	}
	if (foreground)
		puts("");
	else
		gflog_info(GFARM_MSG_1003797, "finish");
	show_statistics();
	munmap_progress_file(progress_addr);
	if (unlink(PROGRESS_FILE) == -1)
		gflog_error_errno(GFARM_MSG_1003798, PROGRESS_FILE);

	gfm_client_connection_free(gfm_server);
	e = gfarm_terminate();
	error_check(progname, e);
	exit(0);
}
