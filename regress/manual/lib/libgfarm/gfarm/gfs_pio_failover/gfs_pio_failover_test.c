#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <libgen.h>
#include <signal.h>
#include <error.h>

#include <gfarm/gfarm.h>

#define STR_BUFSIZE	16
#define NUM_FILES	3

static void msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

static void
chkerr(gfarm_error_t e, const char *diag, int i)
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
do_failover(GFS_File gf, int idx)
{
	struct gfs_stat gfst;
	chkerr(gfs_pio_stat(gf, &gfst), "stat", 0);
	msg("gf[%d]: stat: size=%ld user:group=%s:%s\n",
	    idx, (long)gfst.st_size, gfst.st_user, gfst.st_group);
}

static void
dummy_handler(int sig)
{
}

static void
wait_for_failover()
{
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

#define TEXT1 "ABCDEFGHIJ"
#define TEXT2 "KLMNOPQRST"

static void
create_dirty_file(GFS_File gf[], char path[][GFS_MAXNAMLEN],
	const char *path_base, int nfiles)
{
	size_t sz1, sz2;
	int i, len;
	gfarm_off_t ofs;

	sz1 = strlen(TEXT1);
	sz2 = 1;

	for (i = 0; i < nfiles; ++i) {
		sprintf(path[i], "%s.%d", path_base, i);
		msg("gf[%d]: create %s\n", i, path[i]);
		chkerr(gfs_pio_create(path[i], GFARM_FILE_RDWR, 0777, &gf[i]),
		    "create", i);

		if (i != nfiles - 1) {
			msg("gf[%d]: write %d bytes\n", i, sz1);
			chkerr(gfs_pio_write(gf[i], TEXT1, sz1, &len),
			    "write1", i);
			msg("gf[%d]: write %d bytes ok\n", i, len);
		}
		msg("gf[%d]: seek to 0\n", i);
		chkerr(gfs_pio_seek(gf[i], 0, GFARM_SEEK_SET, &ofs),
		    "seek", i);
		msg("gf[%d]: seek to %ld ok\n", i, (long)ofs);
		if (i != nfiles - 1) {
			msg("gf[%d]: write %d bytes\n", i, sz2);
			chkerr(gfs_pio_write(gf[i], TEXT2, sz2, &len),
			    "write1", i);
			msg("gf[%d]: write %d bytes ok\n", i, len);
		}
	}
}

static void
create_and_write_file(GFS_File gf[], char path[][GFS_MAXNAMLEN],
	const char *path_base, int nfiles)
{
	size_t sz;
	int i, len;

	sz = strlen(TEXT1);

	for (i = 0; i < nfiles; ++i) {
		sprintf(path[i], "%s.%d", path_base, i);
		msg("gf[%d]: create %s\n", i, path[i]);
		chkerr(gfs_pio_create(path[i], GFARM_FILE_WRONLY, 0777, &gf[i]),
		    "create", i);

		if (i != nfiles - 1) {
			msg("gf[%d]: write %d bytes\n", i, sz);
			chkerr(gfs_pio_write(gf[i], TEXT1, sz, &len),
			    "write1", i);
			msg("gf[%d]: write %d bytes ok\n", i, len);
		}
	}
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
		chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf[i]),
		    "open", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: read %d bytes\n", i, sz);
			chkerr(gfs_pio_read(gf[i], buf, sz, &len),
			    "read1", i);
			msg("gf[%d]: read %d bytes ok\n", i, len);
			match_memory(TEXT1, buf, len,
			    "match_memory1");
		}
	}

	wait_for_failover();
	if (explicit_failover)
		do_failover(gf[0], 0);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: read %d bytes\n", i, sz);
		chkerr(gfs_pio_read(gf[i], buf, sz, &len),
		    "read2", i);
		msg("gf[%d]: read %d bytes ok\n", i, len);
		if (i != NUM_FILES - 1) {
			match_memory(TEXT2, buf, len, "match_memory2");
		} else {
			match_memory(TEXT1, buf, len,
			    "match_memory2");
		}
		msg("gf[%d]: seek to 0\n", i);
		chkerr(gfs_pio_seek(gf[i], 0, GFARM_SEEK_SET, &ofs),
		    "peek", i);
		msg("gf[%d]: read %d bytes\n", i, sz);
		chkerr(gfs_pio_read(gf[i], buf, sz, &len),
		    "read3", i);
		msg("gf[%d]: read %d bytes ok\n", i, len);
		if (i != NUM_FILES - 1) {
			match_memory(TEXT1, buf, len, "match_memory3");
		} else {
			match_memory(TEXT1, buf, len,
			    "match_memory3");
		}

		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_read(const char *path)
{
	test_read0(path, 0);
}

static void
test_read_stat(const char *path)
{
	test_read0(path, 1);
}

static void
test_write0(const char *path_base, int explicit_failover)
{
	GFS_File gf[NUM_FILES];
	size_t sz;
	int i, len;
	char path[NUM_FILES][GFS_MAXNAMLEN];

	create_and_write_file(gf, path, path_base, NUM_FILES);

	wait_for_failover(gf);
	if (explicit_failover)
		do_failover(gf[0], 0);

	sz = strlen(TEXT2);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: write %d bytes\n", i, sz);
		chkerr(gfs_pio_write(gf[i], TEXT2, sz, &len),
		    "write2", i);
		msg("gf[%d]: write %d bytes ok\n", i, len);
		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_write(const char *path)
{
	test_write0(path, 0);
}

static void
test_write_stat(const char *path)
{
	test_write0(path, 1);
}

static void
test_sched_read(const char *path)
{
	GFS_File gf;
	char buf[STR_BUFSIZE];
	size_t sz = 10;
	int len;

	msg("gf[%d]: open %s\n", 0, path);
	chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf),
	    "open", 0);

	wait_for_failover();

	msg("gf[%d]: read %d bytes\n", 0, sz);
	chkerr(gfs_pio_read(gf, buf, sz, &len), "read1", 0);
	msg("gf[%d]: read %d bytes ok\n", 0, len);
	match_memory(TEXT1, buf, len, "match_memory1");
	msg("gf[%d]: close\n", 0);
	chkerr(gfs_pio_close(gf), "close", 0);
}

static void
test_sched_open_write(const char *path_base)
{
	GFS_File gf;
	size_t sz;
	int len;
	char path[GFS_MAXNAMLEN];

	sz = strlen(TEXT1);

	sprintf(path, "%s.%d", path_base, 0);
	msg("gf[%d]: create %s\n", 0, path);
	chkerr(gfs_pio_create(path, GFARM_FILE_WRONLY, 0777, &gf),
	    "create", 0);
	msg("gf[%d]: close\n", 0);
	chkerr(gfs_pio_close(gf), "close1", 0);

	msg("gf[%d]: open %s\n", 0, path);
	chkerr(gfs_pio_open(path, GFARM_FILE_WRONLY, &gf), "open", 0);

	wait_for_failover(gf);

	msg("gf[%d]: write %d bytes\n", 0, sz);
	chkerr(gfs_pio_write(gf, TEXT1, sz, &len),
	    "write1", 0);
	msg("gf[%d]: write %d bytes ok\n", 0, len);

	msg("gf[%d]: close\n", 0);
	chkerr(gfs_pio_close(gf), "close2", 0);
}

static void
test_sched_create_write(const char *path_base)
{
	GFS_File gf;
	size_t sz;
	int len;
	char path[GFS_MAXNAMLEN];

	sz = strlen(TEXT1);

	sprintf(path, "%s.%d", path_base, 0);
	msg("gf[%d]: create %s\n", 0, path);
	chkerr(gfs_pio_create(path, GFARM_FILE_WRONLY, 0777, &gf),
	    "create", 0);

	wait_for_failover(gf);

	msg("gf[%d]: write %d bytes\n", 0, sz);
	chkerr(gfs_pio_write(gf, TEXT1, sz, &len),
	    "write1", 0);
	msg("gf[%d]: write %d bytes ok\n", 0, len);

	msg("gf[%d]: close\n", 0);
	chkerr(gfs_pio_close(gf), "close2", 0);
}

static void
test_getc(const char *path)
{
	GFS_File gf[NUM_FILES];
	int i, ch;
	char c;

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: open %s\n", i, path);
		chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf[i]),
		    "open", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: getc\n", i);
			ch = gfs_pio_getc(gf[i]);
			chkerr(gfs_pio_error(gf[i]), "getc1", i);
			msg("gf[%d]: getc ok\n", i);
			c = (unsigned char)ch;
			match_memory("A", &c, 1, "match_memory1");
		}
	}

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: getc\n", i);
		ch = gfs_pio_getc(gf[i]);
		chkerr(gfs_pio_error(gf[i]), "getc2", i);
		msg("gf[%d]: getc ok\n", i);
		c = (unsigned char)ch;

		if (i != NUM_FILES - 1)
			match_memory("B", &c, 1, "match_memory2");
		else
			match_memory("A", &c, 1, "match_memory2");

		msg("gf[%d]: ungetc\n", i);
		ch = gfs_pio_ungetc(gf[i], 'C');
		chkerr(gfs_pio_error(gf[i]), "ungetc1", i);
		msg("gf[%d]: ungetc ok\n", i);
		c = (unsigned char)ch;
		match_memory("C", &c, 1, "match_memory3");
		msg("gf[%d]: close\n", i);
		gfs_pio_close(gf[i]);
	}
}

static void
test_seek(const char *path)
{
	GFS_File gf[NUM_FILES];
	int i;
	gfarm_off_t ofs;

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: open %s\n", i, path);
		chkerr(gfs_pio_open(path, GFARM_FILE_RDONLY, &gf[i]),
		    "open", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: seek to 1\n", i);
			chkerr(gfs_pio_seek(gf[i], 1, GFARM_SEEK_SET, &ofs),
			    "seek1", i);
			msg("gf[%d]: seek to %ld ok\n", i, (long)ofs);
		}
	}

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: seek to 8\n", i);
		chkerr(gfs_pio_seek(gf[i], 8, GFARM_SEEK_SET, &ofs),
		    "seek2", i);
		msg("gf[%d]: seek to %ld ok\n", i, (long)ofs);
		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_seek_dirty(const char *path_base)
{
	GFS_File gf[NUM_FILES];
	int i;
	gfarm_off_t ofs;
	char path[NUM_FILES][GFS_MAXNAMLEN];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover();

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: seek to 8\n", i);
		chkerr(gfs_pio_seek(gf[i], 8, GFARM_SEEK_SET, &ofs),
		    "seek1", i);
		msg("gf[%d]: seek to %ld ok\n", i, (long)ofs);
		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_putc(const char *path_base)
{
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][GFS_MAXNAMLEN];

	for (i = 0; i < NUM_FILES; ++i) {
		sprintf(path[i], "%s.%d", path_base, i);
		msg("gf[%d]: create %s\n", i, path[i]);
		chkerr(gfs_pio_create(path[i], GFARM_FILE_WRONLY, 0777, &gf[i]),
		    "create", i);

		if (i != NUM_FILES - 1) {
			msg("gf[%d]: putc\n", i);
			chkerr(gfs_pio_putc(gf[i], 'A'), "putc1", i);
			msg("gf[%d]: putc ok\n", i);
		}
	}

	wait_for_failover(gf);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: putc \n", i);
		chkerr(gfs_pio_putc(gf[i], 'B'), "putc2", i);
		msg("gf[%d]: putc ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_truncate(const char *path_base)
{
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][GFS_MAXNAMLEN];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover(gf);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: truncate\n", i);
		chkerr(gfs_pio_truncate(gf[i], 50), "truncate", i);
		msg("gf[%d]: truncate ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_flush(const char *path_base)
{
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][GFS_MAXNAMLEN];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover(gf);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: flush\n", i);
		chkerr(gfs_pio_flush(gf[i]), "flush", i);
		msg("gf[%d]: flush ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_sync(const char *path_base)
{
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][GFS_MAXNAMLEN];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover(gf);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: sync\n", i);
		chkerr(gfs_pio_sync(gf[i]), "sync", i);
		msg("gf[%d]: sync ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
test_datasync(const char *path_base)
{
	GFS_File gf[NUM_FILES];
	int i;
	char path[NUM_FILES][GFS_MAXNAMLEN];

	create_dirty_file(gf, path, path_base, NUM_FILES);

	wait_for_failover(gf);

	for (i = 0; i < NUM_FILES; ++i) {
		msg("gf[%d]: datasync\n", i);
		chkerr(gfs_pio_datasync(gf[i]), "datasync", i);
		msg("gf[%d]: datasync ok\n", i);
		msg("gf[%d]: close\n", i);
		chkerr(gfs_pio_close(gf[i]), "close", i);
	}
}

static void
usage()
{
	fprintf(stderr, "usage: gfs_pio_failover <MODE> <GFARM_FILE_PATH>\n");
	exit(1);
}

struct type_info {
	const char *opt;
	void (*test)(const char *);
} type_infos[] = {
	{ "sched-read",		test_sched_read },
	{ "sched-open-write",	test_sched_open_write },
	{ "sched-create-write",	test_sched_create_write },
	{ "read",		test_read },
	{ "read-stat",		test_read_stat },
	{ "getc",		test_getc },
	{ "seek",		test_seek },
	{ "seek-dirty",		test_seek_dirty },
	{ "write",		test_write },
	{ "write-stat",		test_write_stat },
	{ "putc",		test_putc },
	{ "truncate",		test_truncate },
	{ "flush",		test_flush },
	{ "sync",		test_sync },
	{ "datasync",		test_datasync },
};

int
main(int argc, char **argv)
{
	const char *path, *type_opt;
	int i, validtype = 0;

	chkerr(gfarm_initialize(&argc, &argv), "initialize", -1);
	gflog_set_priority_level(LOG_DEBUG);

	if (argc != 3)
		usage();

	type_opt = argv[1];
	path = argv[2];

	msg("<<test: %s>>\n", type_opt);

	for (i = 0; i < sizeof(type_infos) / sizeof(struct type_info); ++i) {
		struct type_info *m = &type_infos[i];
		if (strcmp(m->opt, type_opt) == 0) {
			validtype = 1;
			m->test(path);
			break;
		}
	}

	if (!validtype) {
		fprintf(stderr, "Invalid type: %s\n", type_opt);
		exit(1);
	}

	/* if error occurred, this program will exit immediately. */
	msg("OK\n");

	gfarm_terminate();

	return (0);
}
