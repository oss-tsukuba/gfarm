/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

#include "lookup.h"
#include "gfm_client.h"

#include "md5.h"

char *progname = "gfspoolmd5";

static struct gfm_connection *gfm_server;
static long long total_count, total_size;
static long long count, size;
static struct timeval stime;

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
	}
	if (!S_ISDIR(st.st_mode))
		return (GFARM_ERR_INVALID_ARGUMENT); /* XXX */

	if (op_dir1 != NULL) {
		e = op_dir1(dir, &st, arg);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	dirp = opendir(dir);
	if (dirp == NULL)
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
			fprintf(stderr, "%s: %s\n", dir1,
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

	fd = open(file, O_RDONLY);
	if (fd == -1)
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
	int sec, min, hour;
	double time, bw;

	gettimeofday(&t, NULL);
	time = t.tv_sec - stime.tv_sec + .000001 * (t.tv_usec - stime.tv_usec);
	bw = size / 1000 / time;
	sec = (int)((total_size - size) / 1000 / bw);
	hour = sec / 3600;
	min = (sec - hour * 3600) / 60;
	sec = sec - hour * 3600 - min * 60;

	printf("file: %lld/%lld size: %lld/%lld (%2lld%%) "
	       "ave: %.2f KB/s Est: %d:%02d:%02d \r",
	       count, total_count, size, total_size, size * 100 / total_size,
	       bw, hour, min, sec);
	fflush(stdout);
}

static const char xattr_md5[] = "gfarm.md5";

static gfarm_error_t
check_file(char *file, struct stat *stp, void *arg)
{
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	GFS_File gf;
	gfarm_error_t e;
	size_t md5_size = MD5_SIZE * 2;
	char md5[MD5_SIZE * 2 + 1], md5_mds[MD5_SIZE * 2 + 1];

	if (get_inum_gen(file, &inum, &gen))
		return (GFARM_ERR_INVALID_FILE_REPLICA);

	e = calc_md5(file, md5);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfs_pio_fhopen(inum, gen, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfs_fsetxattr(gf, xattr_md5, md5, md5_size, GFS_XATTR_CREATE);
	if (e == GFARM_ERR_NO_ERROR || e != GFARM_ERR_ALREADY_EXISTS)
		goto progress;
	e = gfs_fgetxattr(gf, xattr_md5, md5_mds, &md5_size);
	if (e != GFARM_ERR_NO_ERROR)
		goto progress;
	if (memcmp(md5, md5_mds, md5_size) != 0) {
		e = GFARM_ERR_INVALID_FILE_REPLICA;
		fprintf(stderr, "%s: md5 digest differs: "
		    "file %.32s mds %.*s\n", file, md5, (int)md5_size, md5_mds);
	}
progress:
	gfs_pio_close(gf);
	count += 1;
	size += stp->st_size;
	show_progress();
	return (e);
}

static void
check_spool(char *dir)
{
	gfarm_error_t e;

	e = dir_foreach(check_file, NULL, NULL, dir, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", dir, gfarm_error_string(e));
}

static gfarm_error_t
file_size(char *file, struct stat *stp, void *arg)
{
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

static void
error_check(char *msg, gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;
	fprintf(stderr, "%s: %s\n", msg, gfarm_error_string(e));
	exit(1);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s -r spool_root [ dir ... ] 2> log\n",
	    progname);
	exit(2);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	char *spool_root = NULL;
	int c, i;

	if (argc > 0)
		progname = basename(argv[0]);

	while ((c = getopt(argc, argv, "hr:?")) != -1) {
		switch (c) {
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
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
		exit(1);
	}
	e = gfarm_initialize(&argc, &argv);
	error_check(progname, e);

	e = gfm_client_connection_and_process_acquire_by_path(
		GFARM_PATH_ROOT, &gfm_server);
	error_check(progname, e);

	if (chdir(spool_root) == -1) {
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
		exit(1);
	}
	if (!is_gfarmroot()) {
		fprintf(stderr, "You are not gfarmroot\n");
		exit(1);
	}
	for (i = 0; i < argc; ++i)
		count_size(argv[i]);
	gettimeofday(&stime, NULL);
	for (i = 0; i < argc; ++i)
		check_spool(argv[i]);
	if (i == 0) {
		count_size(".");
		gettimeofday(&stime, NULL);
		check_spool(".");
	}
	puts("");

	gfm_client_connection_free(gfm_server);
	e = gfarm_terminate();
	error_check(progname, e);
	exit(0);
}
