#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <libgen.h>

#include <gfarm/gfarm.h>


static void
chkerr(gfarm_error_t e, const char *diag, int i)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;
	if (i >= 0)
		printf("error at gf[%d]: %s: %s\n", i, diag,
		    gfarm_error_string(e));
	else
		printf("error: %s: %s\n", diag, gfarm_error_string(e));
	exit(1);
}

static void
wait_for_failover(GFS_File gf)
{
	printf("*** Please failover gfmd manually and push Enter Key ***\n");
	getchar();

	struct gfs_stat gfst;
	chkerr(gfs_pio_stat(gf, &gfst), "stat", 0);
	printf("gf[0]: stat: size=%ld user:group=%s:%s\n",
	    (long)gfst.st_size, gfst.st_user, gfst.st_group);
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
		printf("error: %s: string not matched: "
		    "expected=[%s] result=[%s]\n", diag, b1, b2);
		free(b1);
		free(b2);
		exit(1);
	}
}

#define TEXT1 "ABCDEFGHIJ"
#define TEXT2 "KLMNOPQRST"

static void
test_read(const char *path)
{
	GFS_File gf[3];
	char buf[16];
	size_t sz = 10;
	int i, len;
	gfarm_off_t ofs;

	for (i = 0; i < 3; ++i) {
		printf("gf[%d]: open %s\n", i, path);
		chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf[i]),
		    "open", i);

		if (i <= 1) {
			chkerr(gfs_pio_read(gf[i], buf, sz, &len),
			    "read1", i);
			printf("gf[%d]: read %d bytes\n", i, len);
			match_memory(TEXT1, buf, len,
			    "match_memory1");
			printf("gf[%d]: read %d bytes\n", i, len);
		}
	}

	wait_for_failover(gf[0]);

	for (i = 0; i < 3; ++i) {
		chkerr(gfs_pio_read(gf[i], buf, sz, &len),
		    "read2", i);
		printf("gf[%d]: read %d bytes\n", i, len);
		if (i <= 1) {
			match_memory(TEXT2, buf, len, "match_memory2");
		} else {
			match_memory(TEXT1, buf, len,
			    "match_memory2");
		}
		printf("gf[%d]: seek to 0\n", i);
		chkerr(gfs_pio_seek(gf[i], 0, GFARM_SEEK_SET, &ofs),
		    "peek", i);
		chkerr(gfs_pio_read(gf[i], buf, sz, &len),
		    "read3", i);
		printf("gf[%d]: read %d bytes\n", i, len);
		if (i <= 1) {
			match_memory(TEXT1, buf, len, "match_memory3");
		} else {
			match_memory(TEXT1, buf, len,
			    "match_memory3");
		}

		printf("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_write(const char *path_base)
{
	GFS_File gf[3];
	size_t sz;
	int i, len;
	char path[3][256], buf[16];

	strcpy(buf, TEXT1);
	sz = strlen(buf);

	for (i = 0; i < 3; ++i) {
		sprintf(path[i], "%s.%d", path_base, i);
		printf("gf[%d]: create %s\n", i, path[i]);
		chkerr(gfs_pio_create(path[i], GFARM_FILE_WRONLY, 0777, &gf[i]),
		    "create", i);

		if (i <= 1) {
			chkerr(gfs_pio_write(gf[i], buf, sz, &len),
			    "write1", i);
			printf("gf[%d]: write %d bytes\n", i, len);
		}
	}

	wait_for_failover(gf[0]);

	strcpy(buf, TEXT2);
	sz = strlen(buf);

	for (i = 0; i < 3; ++i) {
		chkerr(gfs_pio_write(gf[i], buf, sz, &len),
		    "write2", i);
		printf("gf[%d]: write %d bytes\n", i, len);
		printf("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
		printf("%s: unlink\n", path[i], i);
		gfs_unlink(path[i]);
	}
}

static void
usage()
{
	fprintf(stderr, "usage: gfs_pio_stat_failover [r|w] path\n");
	fprintf(stderr,
	    "  r : read, seek\n"
	    "  w : write\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	const char *path;
	char mode;

	chkerr(gfarm_initialize(&argc, &argv), "initialize", -1);
	gflog_set_priority_level(LOG_DEBUG);

	if (argc != 3)
		usage();

	mode = argv[1][0];
	path = argv[2];

	switch (mode) {
	case 'r':
		test_read(path);
		break;
	case 'w':
		test_write(path);
		break;
	default:
		usage();
	}

	/* if error occurred, this program will exit immediately. */
	printf("OK\n");

	gfarm_terminate();

	return (0);
}
