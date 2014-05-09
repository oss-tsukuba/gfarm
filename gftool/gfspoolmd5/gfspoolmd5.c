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

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "md5.h"

#include "lookup.h"
#include "gfm_proto.h"
#include "gfm_client.h"

char *progname = "gfspoolmd5";

static struct gfm_connection *gfm_server;
static long long total_count, total_size;
static long long count, size, start_count;
static struct timeval itime, start_time;
const char PROGRESS_FILE[] = ".md5.count";
long long *progress_addr;
int mtime_day = -1;
int foreground, cksum_check = 1;

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
	return (mtime_day < 0 || itime.tv_sec - stp->st_mtime < mtime_day);
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

#define MD5_SIZE	16

static gfarm_error_t
calc_md5(char *file, char md5[MD5_SIZE * 2 + 1])
{
#define bufsize	65536
	md5_state_t state;
	md5_byte_t digest[16], buf[bufsize];
	int di, fd, sz;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if ((fd = open(file, O_RDONLY)) == -1)
		return (gfarm_errno_to_error(errno));
	md5_init(&state);
	while ((sz = read(fd, buf, sizeof buf)) > 0)
		md5_append(&state, buf, sz);
	if (sz == -1)
		e = gfarm_errno_to_error(errno);
	md5_finish(&state, digest);
	if (e == GFARM_ERR_NO_ERROR)
		for (di = 0; di < 16; ++di)
			sprintf(&md5[di * 2], "%02x", digest[di]);
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
check_file(char *file, struct stat *stp, void *arg)
{
	struct gfs_stat_cksum c;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	GFS_File gf;
	gfarm_error_t e;
	size_t md5_size = MD5_SIZE * 2;
	char md5[MD5_SIZE * 2 + 1];

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
	if (c.len > 0 && strcmp(c.type, "md5") != 0) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
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
	} else if ((e = calc_md5(file, md5)) != GFARM_ERR_NO_ERROR) {
		;
	} else if (c.len > 0 && memcmp(md5, c.cksum, md5_size) != 0) {
		gfs_stat_cksum_free(&c);
		if ((e = gfs_fstat_cksum(gf, &c)) != GFARM_ERR_NO_ERROR)
			goto close_progress;
		if (c.len > 0 && (c.flags & (GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED|
		    GFM_PROTO_CKSUM_GET_EXPIRED)) == 0) {
			e = GFARM_ERR_CHECKSUM_MISMATCH;
			gflog_error(GFARM_MSG_1003789, "%s: file %.*s mds %.*s",
			    file, (int)md5_size, md5, (int)c.len, c.cksum);
		}
	} else {
		struct gfs_stat_cksum ck;

		ck.type = "md5";
		ck.len = md5_size;
		ck.cksum = md5;
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
check_spool(char *dir)
{
	gfarm_error_t e;

	e = dir_foreach(check_file, NULL, NULL, dir, NULL);
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
error_check(char *msg, gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;
	gflog_fatal(GFARM_MSG_1003794, "%s: %s", msg, gfarm_error_string(e));
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [ -n ] [ -m mtime_day ] ", progname);
	fprintf(stderr, "-r spool_root [ dir ... ]\n");
	fprintf(stderr, "       %s -f [ -n ] [ -m mtime_day ] ", progname);
	fprintf(stderr, "-r spool_root [ dir ... ] 2> log\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	char *spool_root = NULL;
	int c, i;
	int syslog_facility = GFARM_DEFAULT_FACILITY;

	if (argc > 0)
		progname = basename(argv[0]);

	while ((c = getopt(argc, argv, "fhm:nr:?")) != -1) {
		switch (c) {
		case 'f':
			foreground = 1;
			break;
		case 'm':
			mtime_day = atoi(optarg) * 3600 * 24;
			break;
		case 'n':
			cksum_check = 0;
			break;
		case 'r':
			spool_root = optarg;
			break;
		case 'h':
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
	if (!foreground) {
		gflog_syslog_open(LOG_PID, syslog_facility);
		if (gfarm_daemon(1, 0) == -1)
			gflog_warning_errno(GFARM_MSG_1003795, "daemon");
		gflog_info(GFARM_MSG_1003796, "%s: start", progname);
	}
	progress_addr = mmap_progress_file(PROGRESS_FILE);
	start_count = *progress_addr;
	gettimeofday(&itime, NULL);
	for (i = 0; i < argc; ++i)
		count_size(argv[i]);
	gettimeofday(&start_time, NULL);
	for (i = 0; i < argc; ++i)
		check_spool(argv[i]);
	if (i == 0) {
		count_size(".");
		gettimeofday(&start_time, NULL);
		check_spool(".");
	}
	if (foreground)
		puts("");
	else
		gflog_info(GFARM_MSG_1003797, "%s: finish", progname);
	munmap_progress_file(progress_addr);
	if (unlink(PROGRESS_FILE) == -1)
		gflog_error_errno(GFARM_MSG_1003798, PROGRESS_FILE);

	gfm_client_connection_free(gfm_server);
	e = gfarm_terminate();
	error_check(progname, e);
	exit(0);
}
