/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <pthread.h>

#include "db_journal.c"

#include "thrbarrier.h"
/* #include "thrsubr.h" */ /* already included in db_journal.c */

#include "crc32.h"
#include "user.h"
#include "group.h"
#include "mdhost.h"
#include "inode.h"
#include "file_copy.h"
#include "dir.h"
#include "gfmd.h"
#include "db_journal_test.h"
#include "db_journal_apply.h"

/* XXX FIXME - dummy definitions to link successfully without gfmd.o */
struct thread_pool *sync_protocol_get_thrpool(void) { return NULL; }
gfarm_error_t
event_waiter_alloc(struct peer *peer,
	gfarm_error_t (*action)(struct peer *, void *, int *),
	void *arg, struct event_waiter **listp)
{
	return (GFARM_ERR_NO_MEMORY);
}
void event_waiters_signal(struct event_waiter *list) {}
void gfmd_terminate(const char *diag) {}
int gfmd_port;

static char *program_name = "db_journal_test";
static const char *filepath;

#define TEST_FILE_MAX_SIZE	(200 + 4096)
#define TEST_FILE_MAX_SIZE2	(250 + 4096)
#define TEST_FILE_MAX_SIZE3	(280 + 4096)
#define TEST_SEQNUM_OFFSET	(GFARM_JOURNAL_MAGIC_SIZE + 2)

struct t_username {
	const char *username, *realname;
};

#define GETOPT_ARG	"apow?"
#define HELPOPT		"apow?"

static void
usage(void)
{
	fprintf(stderr, "%s -[" HELPOPT "] filepath\n",
	    program_name);
	exit(EXIT_FAILURE);
}

static char *
assert_strdup(const char *s)
{
	char *ss = strdup(s);

	TEST_ASSERT_B("strdup", ss != NULL);
	return (ss);
}

static int
fwrite_uint32(gfarm_uint32_t n, FILE *fp)
{
	gfarm_uint32_t nn = htonl(n);

	return (fwrite(&nn, sizeof(n), 1, fp));
}

static int
fwrite_uint64(gfarm_uint64_t n, FILE *fp)
{
	size_t sz;
	gfarm_uint32_t n1, n2;

	n1 = n >> 32;
	n2 = n & 0xFFFFFFFF;
	sz = fwrite_uint32(n1, fp);
	if (sz == 0)
		return (0);
	return (fwrite_uint32(n2, fp));
}

static void
fwrite_file_header(FILE *fp)
{
	int i;

	TEST_ASSERT0(fputs(GFARM_JOURNAL_FILE_MAGIC, fp) != EOF);
	TEST_ASSERT0(fwrite_uint32(GFARM_JOURNAL_VERSION, fp) > 0);
	for (i = 0; i < 4088 / sizeof(gfarm_uint32_t); ++i)
		TEST_ASSERT0(fwrite_uint32(0, fp) > 0);
}

static void
fwrite_record(gfarm_uint64_t seqnum, const char *data, FILE *fp)
{
	size_t len = strlen(data);
	gfarm_uint64_t crc = gfarm_crc32(0, data, len);

	TEST_ASSERT0(fputs(GFARM_JOURNAL_RECORD_MAGIC, fp) != EOF);
	TEST_ASSERT0(fputs("  ", fp) != EOF);
	TEST_ASSERT0(fwrite_uint64(seqnum, fp) > 0);
	TEST_ASSERT0(fwrite_uint32(len, fp) > 0);
	TEST_ASSERT0(fputs(data, fp) != EOF);
	TEST_ASSERT0(fwrite_uint32(crc, fp) > 0);
}

static void
create_test_file1(const char *path)
{
	FILE *fp;

	fp = fopen(path, "w");
	TEST_ASSERT0(fp != NULL);
	fwrite_file_header(fp);
	fwrite_record(1, "abcd", fp);
	fwrite_record(2, "efghijk", fp);
	fclose(fp);
}

static void
unlink_test_file(const char *path)
{
	int r;

	errno = 0;
	r = unlink(filepath);
	TEST_ASSERT0(r == 0 || errno == ENOENT);
}

static void
setup_write()
{
	journal_seqnum_pre = 0;
}

/***********************************************/
/* t_open */

static void
t_open_new(void)
{
	struct journal_file *jf;

	unlink_test_file(filepath);
	TEST_ASSERT_NOERR("journal_file_open",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE, 0, &jf,
	    GFARM_JOURNAL_RDWR));
	TEST_ASSERT("jf", jf != NULL);
	journal_file_close(jf);
}

static void
t_open_old(void)
{
	struct journal_file *jf;

	create_test_file1(filepath);
	TEST_ASSERT_NOERR("journal_file_open",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE, 0, &jf,
	    GFARM_JOURNAL_RDWR));
	TEST_ASSERT("jf", jf != NULL);
	journal_file_close(jf);
}

void
t_open(void)
{
	t_open_new();
	t_open_old();
}

/***********************************************/
/* t_write */

static struct gfarm_user_info *
t_new_user_info(const char *username, const char *realname)
{
	struct gfarm_user_info *ui;

	ui = malloc(sizeof(*ui));
	memset(ui, 0, sizeof(*ui));
	ui->username = (char *)username;
	ui->realname = (char *)realname;
	ui->homedir = "/";
	ui->gsi_dn = "";

	return (ui);
}

static void
t_write_sequential(void)
{
	int i;
	struct gfarm_user_info *ui;
	static const struct t_username names[] = {
		{ "user1", "USER1" },
		{ "user2", "U-S-E-R2" },
		{ "user3", "USR3" },
	};

	unlink_test_file(filepath);
	TEST_ASSERT_NOERR("journal_file_open",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE, 0,
		&self_jf, GFARM_JOURNAL_RDWR));

	setup_write();
	for (i = 0; i < GFARM_ARRAY_LENGTH(names); ++i) {
		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		TEST_ASSERT_NOERR("user_add",
		    db_journal_ops.user_add(i + 1, ui));
	}
	journal_file_close(self_jf);
}

static struct gfarm_user_info *t_user_infos[32];

static int user_idx;

static gfarm_error_t
t_check_user_post_read(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *arg, void *closure, size_t length,
	int *needs_freep)
{
	struct gfarm_user_info *ui1 = arg;
	struct gfarm_user_info *ui2 =
	    t_user_infos[user_idx++];

	TEST_ASSERT_S("user_info.username",
	    ui2->username, ui1->username);
	TEST_ASSERT_S("user_info.realname",
	    ui2->realname, ui1->realname);
	TEST_ASSERT_S("user_info.homedir",
	    ui2->homedir, ui1->homedir);
	TEST_ASSERT_S("user_info.gsi_dn",
	    ui2->gsi_dn, ui1->gsi_dn);

	return (GFARM_ERR_NO_ERROR);
}

static void
t_write_cyclic(void)
{
	int i, k = 0, eof;
	char msg[BUFSIZ];
	struct journal_file_reader *reader;
	struct journal_file_writer *writer;
	struct gfarm_user_info *ui;
	static const struct t_username names[] = {
		{ "user1", "USER-1" },
		{ "user2", "USER--2" },
		{ "user3", "USER---3" },
		{ "user4", "USER4" },
	};

	user_idx = 0;

	memset(t_user_infos, 0, sizeof(t_user_infos));

	unlink_test_file(filepath);
	/* must be use global variable journal_file */
	TEST_ASSERT_NOERR("journal_file_open",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE, 0,
		&self_jf, GFARM_JOURNAL_RDWR));
	reader = journal_file_main_reader(self_jf);
	writer = journal_file_writer(self_jf);
	(void)writer; /* shutup gcc warning - set but not used */
	setup_write();
	for (i = 0; i < 3; ++i) {
		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		TEST_ASSERT_NOERR("user_add",
		    db_journal_ops.user_add(i + 1, ui));
		/* ui is freed */

		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		t_user_infos[k++] = ui;
	}

	/* |user1|user2|user3|  |
	 * r                  w
	 */
	TEST_ASSERT_NOERR("db_journal_read",
	    db_journal_read(reader, NULL, t_check_user_post_read,
		NULL, &eof));
	TEST_ASSERT0(eof == 0);
	journal_file_reader_commit_pos(reader);

	/* |user1|user2|user3|  |
	 *        r           w
	 */

	ui = t_new_user_info(names[3].username, names[3].realname);
	TEST_ASSERT_NOERR("user_add",
	    db_journal_ops.user_add(i + 1, ui));
	ui = t_new_user_info(names[3].username, names[3].realname);
	t_user_infos[k++] = ui;
	TEST_ASSERT0(eof == 0);

	/* |user4|user2|user3|  |
	 *       wr
	 */

	for (i = 0; i < 3; ++i) { /* user2, user3, user4 */
		sprintf(msg, "db_journal_read (i=%d)", i);
		TEST_ASSERT_NOERR(msg, db_journal_read(reader,
		    NULL, t_check_user_post_read, NULL, &eof));
	}
	TEST_ASSERT0(eof == 0);
	journal_file_reader_commit_pos(reader);

	/* |user4|user2|user3|  |
	 *       rw
	 */
	setup_write();
	for (i = 0; i < 2; ++i) {
		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		TEST_ASSERT_NOERR("user_add",
		    db_journal_ops.user_add(i + 1, ui));
		/* ui is freed */

		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		t_user_infos[k++] = ui;
	}

	/* |user4|user1|user2|  |
	 *       r            w
	 */

	for (i = 0; i < 2; ++i) { /* user1, user2 */
		sprintf(msg, "db_journal_read (i=%d)", i);
		TEST_ASSERT_NOERR(msg, db_journal_read(reader,
		    NULL, t_check_user_post_read, NULL, &eof));
	}
	TEST_ASSERT0(eof == 0);
	journal_file_reader_commit_pos(reader);

	/* |user4|user1|user2|  |
	 *                   rw
	 */

	journal_file_close(self_jf);

	for (i = 0; i < GFARM_ARRAY_LENGTH(t_user_infos); ++i)
		free(t_user_infos[i]);
}

static gfarm_error_t
t_no_check_post_read(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *arg, void *closure, size_t length,
	int *needs_freep)
{
	return (GFARM_ERR_NO_ERROR);
}

static void
t_write_transaction_around1(void)
{
	int i, k = 0, eof, n = 1;
	off_t rpos, rpos1, wpos, wpos0, wpos1;
	gfarm_uint64_t rlap;
	struct journal_file_reader *reader;
	struct journal_file_writer *writer;
	struct gfarm_user_info *ui;
	static const struct t_username names[] = {
		{ "user1", "USER-1" },
		{ "user2", "USER--2" },
		{ "user3", "USER---3" },
	};

	user_idx = 0;

	memset(t_user_infos, 0, sizeof(t_user_infos));

	unlink_test_file(filepath);
	/* must be use global variable journal_file */
	TEST_ASSERT_NOERR("journal_file_open#1",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE2, 0,
		&self_jf, GFARM_JOURNAL_RDWR));
	reader = journal_file_main_reader(self_jf);
	writer = journal_file_writer(self_jf);
	setup_write();
	for (i = 0; i < 2; ++i) {
		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		TEST_ASSERT_NOERR("begin",
		    db_journal_ops.begin(n++, NULL));
		TEST_ASSERT_NOERR("user_add",
		    db_journal_ops.user_add(n++, ui));
		/* ui is freed */
		TEST_ASSERT_NOERR("end",
		    db_journal_ops.end(n++, NULL));

		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		t_user_infos[k++] = ui;
	}

	/*
	 * now journal file's layout is ...
	 * |b[1]|user1[2]|e[3]|b[4]|user2[5]|e[6]|    |
	 * r                                     w
	 */
	TEST_ASSERT_NOERR("db_journal_read(begin)",
	    db_journal_read(reader, NULL,
	        t_no_check_post_read, NULL, &eof));
	TEST_ASSERT0(eof == 0);
	TEST_ASSERT_NOERR("db_journal_read(user_add)",
	    db_journal_read(reader, NULL,
	        t_check_user_post_read, NULL, &eof));
	TEST_ASSERT0(eof == 0);
	TEST_ASSERT_NOERR("db_journal_read(end)",
	    db_journal_read(reader, NULL,
	        t_no_check_post_read, NULL, &eof));
	TEST_ASSERT0(eof == 0);
	journal_file_reader_commit_pos(reader);

	/*
	 * now journal file's layout is ...
	 * |b[1]|user1[2]|e[3]|b[4]|user2[5]|e[6]|    |
	 *                    r                  w
	 */

	wpos0 = journal_file_writer_pos(writer);

	TEST_ASSERT_NOERR("begin",
	    db_journal_ops.begin(n++, NULL));
	ui = t_new_user_info(names[2].username, names[2].realname);
	TEST_ASSERT_NOERR("user_add",
	    db_journal_ops.user_add(n++, ui));
	ui = t_new_user_info(names[2].username, names[2].realname);
	t_user_infos[k++] = ui;
	TEST_ASSERT0(eof == 0);
	TEST_ASSERT_NOERR("end",
	    db_journal_ops.end(n++, NULL));

	journal_file_reader_committed_pos(reader, &rpos1, &rlap);
	wpos1 = journal_file_writer_pos(writer);

	/*
	 * now journal file's layout is ...
	 * |user3[8]|e[9]|xxxx|b[4]|user2[5]|e[6]|b[7]|
	 *               w    r
	 */

	journal_file_close(self_jf);

	/* cur = xxxx[3] */
	TEST_ASSERT_NOERR("journal_file_open#2",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE2, 3,
		&self_jf, GFARM_JOURNAL_RDWR));
	reader = journal_file_main_reader(self_jf);
	writer = journal_file_writer(self_jf);

	journal_file_reader_committed_pos(reader, &rpos, &rlap);
	wpos = journal_file_writer_pos(writer);

	TEST_ASSERT_L("rpos", rpos1, rpos);
	TEST_ASSERT_L("wpos", wpos1, wpos);

	/*
	 * now journal file's layout is ...
	 * |user3[8]|e[9]|xxxx|b[4]|user2[5]|e[6]|b[7]|
	 *               w                       r
	 */

	journal_file_close(self_jf);

	/* cur = end[6] */
	TEST_ASSERT_NOERR("journal_file_open#3",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE2, 6,
		&self_jf, GFARM_JOURNAL_RDWR));
	reader = journal_file_main_reader(self_jf);
	writer = journal_file_writer(self_jf);

	journal_file_reader_committed_pos(reader, &rpos, &rlap);
	wpos = journal_file_writer_pos(writer);

	TEST_ASSERT_L("rpos", wpos0, rpos);
	TEST_ASSERT_L("wpos", wpos1, wpos);

	/*
	 * now journal file's layout is ...
	 * |user3[8]|e[9]|xxxx|b[4]|user2[5]|e[6]|b[7]|
	 *               w                       r
	 */

	journal_file_close(self_jf);

	for (i = 0; i < GFARM_ARRAY_LENGTH(t_user_infos); ++i)
		free(t_user_infos[i]);
}

static void
t_write_transaction_around2(void)
{
	int i, k = 0, eof, n = 1;
	off_t rpos, rpos1, wpos, wpos0, wpos1;
	gfarm_uint64_t rlap;
	struct journal_file_reader *reader;
	struct journal_file_writer *writer;
	struct gfarm_user_info *ui;
	static const struct t_username names[] = {
		{ "user1", "USER-1" },
		{ "user2", "USER--2" },
		{ "user3", "USER---3" },
	};

	user_idx = 0;

	memset(t_user_infos, 0, sizeof(t_user_infos));

	unlink_test_file(filepath);
	/* must be use global variable journal_file */
	TEST_ASSERT_NOERR("journal_file_open#1",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE3, 0,
		&self_jf, GFARM_JOURNAL_RDWR));
	reader = journal_file_main_reader(self_jf);
	writer = journal_file_writer(self_jf);
	setup_write();
	for (i = 0; i < 2; ++i) {
		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		TEST_ASSERT_NOERR("begin",
		    db_journal_ops.begin(n++, NULL));
		TEST_ASSERT_NOERR("user_add",
		    db_journal_ops.user_add(n++, ui));
		/* ui is freed */
		TEST_ASSERT_NOERR("end",
		    db_journal_ops.end(n++, NULL));

		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		t_user_infos[k++] = ui;
	}

	/*
	 * now journal file's layout is ...
	 * |b[1]|user1[2]|e[3]|b[4]|user2[5]|e[6]|            |
	 * r                                     w
	 */
	TEST_ASSERT_NOERR("db_journal_read(begin)",
	    db_journal_read(reader, NULL,
	        t_no_check_post_read, NULL, &eof));
	TEST_ASSERT0(eof == 0);
	TEST_ASSERT_NOERR("db_journal_read(user_add)",
	    db_journal_read(reader, NULL,
	        t_check_user_post_read, NULL, &eof));
	TEST_ASSERT0(eof == 0);
	TEST_ASSERT_NOERR("db_journal_read(end)",
	    db_journal_read(reader, NULL,
	        t_no_check_post_read, NULL, &eof));
	TEST_ASSERT0(eof == 0);
	journal_file_reader_commit_pos(reader);

	/*
	 * now journal file's layout is ...
	 * |b[1]|user1[2]|e[3]|b[4]|user2[5]|e[6]|            |
	 *                    r                  w
	 */

	wpos0 = journal_file_writer_pos(writer);

	TEST_ASSERT_NOERR("begin",
	    db_journal_ops.begin(n++, NULL));
	ui = t_new_user_info(names[2].username, names[2].realname);
	TEST_ASSERT_NOERR("user_add",
	    db_journal_ops.user_add(n++, ui));
	ui = t_new_user_info(names[2].username, names[2].realname);
	t_user_infos[k++] = ui;
	TEST_ASSERT0(eof == 0);
	TEST_ASSERT_NOERR("end",
	    db_journal_ops.end(n++, NULL));

	journal_file_reader_committed_pos(reader, &rpos1, &rlap);
	wpos1 = journal_file_writer_pos(writer);

	/*
	 * now journal file's layout is ...
	 *
	 *       (invalid rec)
	 *       xxxxxxxxxxxxx
	 * |e[9]|user1[2]|e[3]|b[4]|user2[5]|e[6]|b[7]|user[8]|
	 *      w             r
	 */

	journal_file_close(self_jf);

	/* cur = xxxx[3] */
	TEST_ASSERT_NOERR("journal_file_open#2",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE3, 3,
		&self_jf, GFARM_JOURNAL_RDWR));
	reader = journal_file_main_reader(self_jf);
	writer = journal_file_writer(self_jf);

	journal_file_reader_committed_pos(reader, &rpos, &rlap);
	wpos = journal_file_writer_pos(writer);

	TEST_ASSERT_L("rpos", rpos, rpos1);
	TEST_ASSERT_L("wpos", wpos, wpos1);

	/*
	 * now journal file's layout is ...
	 *
	 *       (invalid rec)
	 *       xxxxxxxxxxxxx
	 * |e[9]|user1[2]|e[3]|b[4]|user2[5]|e[6]|b[7]|user[8]|
	 *      w             r
	 */

	journal_file_close(self_jf);

	/* cur = end[6] */
	TEST_ASSERT_NOERR("journal_file_open#3",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE3, 6,
		&self_jf, GFARM_JOURNAL_RDWR));
	reader = journal_file_main_reader(self_jf);
	writer = journal_file_writer(self_jf);

	journal_file_reader_committed_pos(reader, &rpos, &rlap);
	wpos = journal_file_writer_pos(writer);

	TEST_ASSERT_L("rpos", rpos, wpos0);
	TEST_ASSERT_L("wpos", wpos, wpos1);

	/*
	 * now journal file's layout is ...
	 *
	 *       (invalid rec)
	 *       xxxxxxxxxxxxx
	 * |e[9]|user1[2]|e[3]|b[4]|user2[5]|e[6]|b[7]|user[8]|
	 *      w                                r
	 */

	journal_file_close(self_jf);

	for (i = 0; i < GFARM_ARRAY_LENGTH(t_user_infos); ++i)
		free(t_user_infos[i]);
}

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_barrier_t barrier;
static int t_write_blocked_num_rec_write = 0;
static int t_write_blocked_num_rec_read = 0;

static void *
t_write_add_op(void *arg)
{
	int i;
	struct gfarm_user_info *ui;
	struct journal_file_writer *writer;
	gfarm_uint64_t seqnum = 1;
	static const struct t_username names[] = {
		{ "user1", "USER-1" },
		{ "user2", "USER--2" },
		{ "user3", "USER---3" },
		{ "user4", "USER4" },
		{ "user5", "USER-5" },
	};
	static const char *thr_where = "t_write_add_op";
	static const char *thr_what  = "db_journal_test";

	setup_write();
	writer = journal_file_writer(self_jf);
	(void)writer; /* shutup gcc warning - set but not used */
	for (i = 0; i < GFARM_ARRAY_LENGTH(names); ++i) {
		ui = t_new_user_info(
		    names[i].username, names[i].realname);
		TEST_ASSERT_NOERR("user_add",
		    db_journal_ops.user_add(seqnum++, ui));
		gfarm_mutex_lock(&mutex, thr_where, thr_what);
		++t_write_blocked_num_rec_write;
		gfarm_mutex_unlock(&mutex, thr_where, thr_what);

		/*
		 * After writing data of user3, wait until the reader reads
		 * the written data.  See comments in t_write_blocked().
		 */
		if (i == 2)
			gfarm_barrier_wait(&barrier, thr_where, thr_what);
	}

	pthread_exit(NULL);
}

static gfarm_error_t
t_write_blocked_post_read(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *arg, void *closure, size_t length,
	int *needs_freep)
{
	++t_write_blocked_num_rec_read;
	return (GFARM_ERR_NO_ERROR);
}

static void
t_write_blocked(void)
{
	int i, eof;
	pthread_t write_th;
	struct journal_file_reader *reader;
	static const char *thr_where = "t_write_blocked";
	static const char *thr_what  = "db_journal_test";

	unlink_test_file(filepath);
	TEST_ASSERT_NOERR("journal_file_open",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE, 0,
		&self_jf, GFARM_JOURNAL_RDWR));

	gfarm_barrier_init(&barrier, 2, thr_where, thr_what);
	TEST_ASSERT0(pthread_create(
	    &write_th, NULL, &t_write_add_op, NULL) == 0);
	gfarm_barrier_wait(&barrier, thr_where, thr_what);

	/*
	 * Check if the writer thread has written data of user1, user2
	 * and user3.
	 *
	 *     |user1|user2|user3|  |
	 *     r                 w
	 */
	gfarm_mutex_lock(&mutex, thr_where, thr_what);
	TEST_ASSERT_I("t_write_blocked_num_rec_write",
	    3, t_write_blocked_num_rec_write);
	gfarm_mutex_unlock(&mutex, thr_where, thr_what);

	/*
	 * Read data of user1 and user2.
	 */
	reader = journal_file_main_reader(self_jf);
	for (i = 0; i < 2; ++i) {
		TEST_ASSERT_NOERR("db_journal_read",
		    db_journal_read(reader, NULL, t_write_blocked_post_read,
		    NULL, &eof));
		journal_file_reader_commit_pos(reader);
	}

	/*
	 * Check if the position of the reader has been stepped forward.
	 * Note that the writer now starts writing more data.
	 *
	 *     |user1|user2|user3|  |
	 *                 r
	 */
	TEST_ASSERT_I("t_write_blocked_num_rec_read",
	    2, t_write_blocked_num_rec_read);
	pthread_join(write_th, NULL);

	/*
	 * Check if the writer has written data of user4 and user5.
	 *
	 *     |user4|user5|user3|  |
	 *                 wr
	 */
	gfarm_mutex_lock(&mutex, thr_where, thr_what);
	TEST_ASSERT_I("t_write_blocked_num_rec_write",
	    5, t_write_blocked_num_rec_write);
	gfarm_mutex_unlock(&mutex, thr_where, thr_what);

	gfarm_barrier_destroy(&barrier, thr_where, thr_what);
}

void
t_write(void)
{
	t_write_sequential();
	t_write_cyclic();
	t_write_blocked();
	t_write_transaction_around1();
	t_write_transaction_around2();
}

static char * *
t_make_strary1(const char *v1)
{
	char **a;

	GFARM_MALLOC_ARRAY(a, 2);
	a[0] = assert_strdup(v1);
	a[1] = NULL;
	return (a);
}

static char * *
t_make_strary2(const char *v1, const char *v2)
{
	char **a;

	GFARM_MALLOC_ARRAY(a, 3);
	a[0] = assert_strdup(v1);
	a[1] = assert_strdup(v2);
	a[2] = NULL;
	return (a);
}

/***********************************************/
/* t_ops */

static struct gfarm_host_info *
t_ops_host_new(void)
{
	struct gfarm_host_info *hi;

	GFARM_MALLOC(hi);
	hi->hostname = assert_strdup("hostname");
	hi->port = 99;
	hi->nhostaliases = 2;
	hi->hostaliases = t_make_strary2("a1", "a2");
	hi->architecture = assert_strdup("arch");
	hi->ncpu = 4;
	hi->flags = 3;
	return (hi);
}

static void
t_ops_host_add(gfarm_uint64_t seqnum)
{
	struct gfarm_host_info *hi, *dhi;

	hi = t_ops_host_new();
	dhi = db_host_dup(hi, sizeof(*hi));
	db_journal_host_info_destroy(hi);
	TEST_ASSERT_NOERR("db_journal_write_host_add",
	    db_journal_write_host_add(5, dhi));
}

static void
t_ops_host_check(struct gfarm_host_info *hi)
{
	TEST_ASSERT_I("port", 99, hi->port);
	TEST_ASSERT_I("ncpu", 4, hi->ncpu);
	TEST_ASSERT_I("flags", 3, hi->flags);
	/* FIXME modify hostaliases is not implemented yet in db_host_dup
	TEST_ASSERT_I("nhostaliases", 2, hi->nhostaliases);
	TEST_ASSERT_B("hostaliases", hi->hostaliases != NULL);
	TEST_ASSERT_S("hostaliases#0", "a1", hi->hostaliases[0]);
	TEST_ASSERT_S("hostaliases#1", "a2", hi->hostaliases[1]);
	*/
}

static gfarm_error_t
t_ops_host_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_HOST_ADD, ope);
	t_ops_host_check(obj);
	return (GFARM_ERR_NO_ERROR);
}


static void
t_ops_host_modify(gfarm_uint64_t sn)
{
	struct db_host_modify_arg *m;
	struct gfarm_host_info *hi;
	char **a, **d;

	hi = t_ops_host_new();
	m = db_host_modify_arg_alloc(hi,
	    DB_HOST_MOD_ARCHITECTURE, /* modflags */
	    1, /* add_count */
	    (const char **)(a = t_make_strary1("a1")), /* add_aliases */
	    1, /* del_count */
	    (const char **)(d = t_make_strary1("d1")) /* del_aliases */
	    );
	db_journal_host_info_destroy(hi);
	db_journal_string_array_free(1, a);
	db_journal_string_array_free(1, d);
	TEST_ASSERT_NOERR("db_journal_write_host_modify",
	    db_journal_write_host_modify(sn, m));
}

static gfarm_error_t
t_ops_host_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct db_host_modify_arg *m = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_HOST_MODIFY, ope);
	TEST_ASSERT_I("modflags", DB_HOST_MOD_ARCHITECTURE, m->modflags);
#if 0
	/* FIXME hostaliases allocation is not implemented yet
	 * in db_host_modify_arg_alloc
	 */
	TEST_ASSERT_I("add_count", 1, m->add_count);
	TEST_ASSERT_S("add_aliases#0", "a1", m->add_aliases[0]);
	TEST_ASSERT_I("del_count", 1, m->del_count);
	TEST_ASSERT_S("del_aliases#0", "d1", m->del_aliases[0]);
#else
	TEST_ASSERT_I("add_count", 0, m->add_count);
	TEST_ASSERT_B("add_aliases#0", NULL == m->add_aliases[0]);
	TEST_ASSERT_I("del_count", 0, m->del_count);
	TEST_ASSERT_B("del_aliases#0", NULL == m->del_aliases[0]);
#endif
	t_ops_host_check(&m->hi);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_host_remove(gfarm_uint64_t sn)
{
	db_journal_write_host_remove(sn, assert_strdup("name"));
}

static gfarm_error_t
t_ops_host_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	const char *name = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_HOST_REMOVE, ope);
	TEST_ASSERT_S("name", "name", name);
	return (GFARM_ERR_NO_ERROR);
}

static struct gfarm_user_info *
t_ops_user_new(void)
{
	struct gfarm_user_info *ui;

	GFARM_MALLOC(ui);
	ui->username = assert_strdup("username");
	ui->realname = assert_strdup("realname");
	ui->homedir = assert_strdup("homedir");
	ui->gsi_dn = assert_strdup("gsi_dn");
	return (ui);
}

static void
t_ops_user_add(gfarm_uint64_t sn)
{
	struct gfarm_user_info *ui, *dui;

	ui = t_ops_user_new();
	dui = db_user_dup(ui, sizeof(*ui));
	db_journal_user_info_destroy(ui);
	db_journal_write_user_add(sn, dui);
}

static void
t_ops_user_check(struct gfarm_user_info *ui)
{
	TEST_ASSERT_S("username", "username", ui->username);
	TEST_ASSERT_S("realname", "realname", ui->realname);
	TEST_ASSERT_S("homedir", "homedir", ui->homedir);
	TEST_ASSERT_S("gsi_dn", "gsi_dn", ui->gsi_dn);
}

static gfarm_error_t
t_ops_user_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct gfarm_user_info *ui = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_USER_ADD, ope);
	t_ops_user_check(ui);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_user_modify(gfarm_uint64_t sn)
{
	struct db_user_modify_arg *m;
	struct gfarm_user_info *ui;

	ui = t_ops_user_new();
	m = db_user_modify_arg_alloc(ui, DB_USER_MOD_REALNAME);
	db_journal_user_info_destroy(ui);
	TEST_ASSERT_NOERR("db_journal_write_user_modify",
	    db_journal_write_user_modify(sn, m));
}

static gfarm_error_t
t_ops_user_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct db_user_modify_arg *m = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_USER_MODIFY, ope);
	TEST_ASSERT_I("modflags", DB_USER_MOD_REALNAME, m->modflags);
	t_ops_user_check(&m->ui);

	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_user_remove(gfarm_uint64_t sn)
{
	db_journal_write_user_remove(sn, assert_strdup("name"));
}

static gfarm_error_t
t_ops_user_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	const char *name = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_USER_REMOVE, ope);
	TEST_ASSERT_S("name", "name", name);

	return (GFARM_ERR_NO_ERROR);
}

static struct gfarm_group_info *
t_ops_group_new(void)
{
	struct gfarm_group_info *gi;

	GFARM_MALLOC(gi);
	gi->groupname = assert_strdup("groupname");
	gi->nusers = 2;
	gi->usernames = t_make_strary2("u1", "u2");
	return (gi);
}

static void
t_ops_group_add(gfarm_uint64_t sn)
{
	struct gfarm_group_info *gi, *dgi;

	gi = t_ops_group_new();
	dgi = db_group_dup(gi, sizeof(*gi));
	db_journal_group_info_destroy(gi);
	TEST_ASSERT_NOERR("db_journal_write_group_add",
	    db_journal_write_group_add(sn, dgi));
}

static void
t_ops_group_check(struct gfarm_group_info *gi)
{
	TEST_ASSERT_S("groupname", "groupname", gi->groupname);
	TEST_ASSERT_I("nusers", 2, gi->nusers);
	TEST_ASSERT_S("usernames#0", "u1", gi->usernames[0]);
	TEST_ASSERT_S("usernames#1", "u2", gi->usernames[1]);
}

static gfarm_error_t
t_ops_group_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_GROUP_ADD, ope);
	t_ops_group_check(obj);

	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_group_modify(gfarm_uint64_t sn)
{
	struct db_group_modify_arg *m;
	struct gfarm_group_info *gi;
	char **a, **d;

	gi = t_ops_group_new();
	m = db_group_modify_arg_alloc(gi,
	    1111, /* modflags */
	    1, /* add_count */
	    (const char **)(a = t_make_strary1("a1")), /* add_users */
	    1, /* del_count */
	    (const char **)(d = t_make_strary1("d1")) /* del_users */
	    );
	db_journal_group_info_destroy(gi);
	free(a);
	free(d);
	TEST_ASSERT_NOERR("db_journal_write_group_modify",
	    db_journal_write_group_modify(sn, m));
}

static gfarm_error_t
t_ops_group_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct db_group_modify_arg *m = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_GROUP_MODIFY, ope);
	TEST_ASSERT_I("modflags", 1111, m->modflags);
	TEST_ASSERT_I("add_count", 1, m->add_count);
	TEST_ASSERT_S("add_users#0", "a1", m->add_users[0]);
	TEST_ASSERT_I("del_count", 1, m->del_count);
	TEST_ASSERT_S("del_users#0", "d1", m->del_users[0]);
	t_ops_group_check(&m->gi);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_group_remove(gfarm_uint64_t sn)
{
	db_journal_write_group_remove(sn, assert_strdup("name"));
}

static gfarm_error_t
t_ops_group_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	const char *name = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_GROUP_REMOVE, ope);
	TEST_ASSERT_S("name", "name", name);
	return (GFARM_ERR_NO_ERROR);
}

static struct gfs_stat *
t_ops_inode_new(void)
{
	struct gfs_stat *st;

	GFARM_MALLOC(st);
	st->st_ino = 111;
	st->st_gen = 222;
	st->st_mode = 333;
	st->st_nlink = 444;
	st->st_user = assert_strdup("user");
	st->st_group = assert_strdup("group");
	st->st_size = 555;
	st->st_atimespec.tv_sec = 7771;
	st->st_atimespec.tv_nsec = 7772;
	st->st_mtimespec.tv_sec = 8881;
	st->st_mtimespec.tv_nsec = 8882;
	st->st_ctimespec.tv_sec = 9991;
	st->st_ctimespec.tv_nsec = 9992;
	return (st);
}

static void
t_ops_inode_add(gfarm_uint64_t sn)
{
	struct gfs_stat *st, *dst;

	st = t_ops_inode_new();
	dst = db_inode_dup(st, sizeof(*st));
	db_journal_stat_destroy(st);
	TEST_ASSERT_NOERR("db_journal_write_inode_add",
	    db_journal_write_inode_add(sn, dst));
}

static void
t_ops_inode_check(struct gfs_stat *st)
{
	TEST_ASSERT_L("st_ino", 111, st->st_ino);
	TEST_ASSERT_L("st_gen", 222, st->st_gen);
	TEST_ASSERT_I("st_mode", 333, st->st_mode);
	TEST_ASSERT_L("st_nlink", 444, st->st_nlink);
	TEST_ASSERT_S("st_user", "user", st->st_user);
	TEST_ASSERT_S("st_group", "group", st->st_group);
	TEST_ASSERT_L("st_size", 555, st->st_size);
	TEST_ASSERT_L("st_atimespec.tv_sec", 7771, st->st_atimespec.tv_sec);
	TEST_ASSERT_I("st_atimespec.tv_nsec", 7772, st->st_atimespec.tv_nsec);
	TEST_ASSERT_L("st_mtimespec.tv_sec", 8881, st->st_mtimespec.tv_sec);
	TEST_ASSERT_I("st_mtimespec.tv_nsec", 8882, st->st_mtimespec.tv_nsec);
	TEST_ASSERT_L("st_ctimespec.tv_sec", 9991, st->st_ctimespec.tv_sec);
	TEST_ASSERT_I("st_ctimespec.tv_nsec", 9992, st->st_ctimespec.tv_nsec);
}

static gfarm_error_t
t_ops_inode_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct gfs_stat *st = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_ADD, ope);
	t_ops_inode_check(st);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_modify(gfarm_uint64_t sn)
{
	struct gfs_stat *st, *dst;

	st = t_ops_inode_new();
	dst = db_inode_dup(st, sizeof(*st));
	db_journal_stat_destroy(st);
	TEST_ASSERT_NOERR("db_journal_write_inode_modify",
	    db_journal_write_inode_modify(sn, dst));
}

static gfarm_error_t
t_ops_inode_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct gfs_stat *st = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_MODIFY, ope);
	t_ops_inode_check(st);
	return (GFARM_ERR_NO_ERROR);
}

static struct db_inode_uint32_modify_arg *
t_ops_inode_uint32_modify_new(void)
{
	struct db_inode_uint32_modify_arg *m;

	GFARM_MALLOC(m);
	m->inum = 111;
	m->uint32 = 222;
	return (m);
}

static void
t_ops_inode_uint32_modify_check(struct db_inode_uint32_modify_arg *m)
{
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_I("uint32", 222, m->uint32);
}

static struct db_inode_uint64_modify_arg *
t_ops_inode_uint64_modify_new(void)
{
	struct db_inode_uint64_modify_arg *m;

	GFARM_MALLOC(m);
	m->inum = 111;
	m->uint64 = 222;
	return (m);
}

static void
t_ops_inode_uint64_modify_check(struct db_inode_uint64_modify_arg *m)
{
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_L("uint64", 222, m->uint64);
}

static struct db_inode_string_modify_arg *
t_ops_inode_string_modify_new(void)
{
	return (db_inode_string_modify_arg_alloc(
	    111, /* inum */
	    "string" /* string */
	    ));
}

static void
t_ops_inode_string_modify_check(struct db_inode_string_modify_arg *m)
{
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_S("string", "string", m->string);
}

static struct db_inode_timespec_modify_arg *
t_ops_inode_timespec_modify_new(void)
{
	struct db_inode_timespec_modify_arg *m;

	GFARM_MALLOC(m);
	m->inum = 111;
	m->time.tv_sec = 222;
	m->time.tv_nsec = 333;
	return (m);
}

static void
t_ops_inode_timespec_modify_check(struct db_inode_timespec_modify_arg *m)
{
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_L("time.tv_sec", 222, m->time.tv_sec);
	TEST_ASSERT_I("time.tv_nsec", 333, m->time.tv_nsec);
}

static void
t_ops_inode_gen_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_gen_modify",
	    db_journal_write_inode_gen_modify(sn,
	    t_ops_inode_uint64_modify_new()));
}

static gfarm_error_t
t_ops_inode_gen_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_GEN_MODIFY, ope);
	t_ops_inode_uint64_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_nlink_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_nlink_modify",
	    db_journal_write_inode_nlink_modify(sn,
	    t_ops_inode_uint64_modify_new()));
}

static gfarm_error_t
t_ops_inode_nlink_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_NLINK_MODIFY, ope);
	t_ops_inode_uint64_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_size_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_size_modify",
	    db_journal_write_inode_size_modify(sn,
	    t_ops_inode_uint64_modify_new()));
}

static gfarm_error_t
t_ops_inode_size_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_SIZE_MODIFY, ope);
	t_ops_inode_uint32_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_mode_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_mode_modify",
	    db_journal_write_inode_mode_modify(sn,
	    t_ops_inode_uint32_modify_new()));
}

static gfarm_error_t
t_ops_inode_mode_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_MODE_MODIFY, ope);
	t_ops_inode_uint32_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_user_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_user_modify",
	    db_journal_write_inode_user_modify(sn,
	    t_ops_inode_string_modify_new()));
}

static gfarm_error_t
t_ops_inode_user_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_USER_MODIFY, ope);
	t_ops_inode_string_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_group_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_group_modify",
	    db_journal_write_inode_group_modify(sn,
	    t_ops_inode_string_modify_new()));
}

static gfarm_error_t
t_ops_inode_group_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_GROUP_MODIFY, ope);
	t_ops_inode_string_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_atime_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_atime_modify",
	    db_journal_write_inode_atime_modify(sn,
	    t_ops_inode_timespec_modify_new()));
}

static gfarm_error_t
t_ops_inode_atime_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_ATIME_MODIFY, ope);
	t_ops_inode_timespec_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_mtime_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_mtime_modify",
	    db_journal_write_inode_mtime_modify(sn,
	    t_ops_inode_timespec_modify_new()));
}

static gfarm_error_t
t_ops_inode_mtime_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_MTIME_MODIFY, ope);
	t_ops_inode_timespec_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_ctime_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_ctime_modify",
	    db_journal_write_inode_ctime_modify(sn,
	    t_ops_inode_timespec_modify_new()));
}

static gfarm_error_t
t_ops_inode_ctime_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_CTIME_MODIFY, ope);
	t_ops_inode_timespec_modify_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static struct db_inode_cksum_arg *
t_ops_inode_cksum_new(void)
{
	return (db_inode_cksum_arg_alloc(
	    111, /* inum */
	    "type", /* type */
	    strlen("sum"), /* len */
	    "sum" /* sum */
	    ));
}

static void
t_ops_inode_cksum_check(struct db_inode_cksum_arg *m)
{
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_S("type", "type", m->type);
	TEST_ASSERT_L("len", strlen("sum"), m->len);
	TEST_ASSERT_B("sum", strncmp("sum", m->sum, 3) == 0);
}

static struct db_inode_inum_arg *
t_ops_inode_inum_new(void)
{
	struct db_inode_inum_arg *m;

	GFARM_MALLOC(m);
	m->inum = 111;
	return (m);
}

static void
t_ops_inode_inum_check(struct db_inode_inum_arg *m)
{
	TEST_ASSERT_L("inum", 111, m->inum);
}

static void
t_ops_inode_cksum_add(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_cksum_add",
	    db_journal_write_inode_cksum_add(5, t_ops_inode_cksum_new()));
}

static gfarm_error_t
t_ops_inode_cksum_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_CKSUM_ADD, ope);
	t_ops_inode_cksum_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_cksum_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_cksum_modify",
	    db_journal_write_inode_cksum_modify(sn, t_ops_inode_cksum_new()));
}

static gfarm_error_t
t_ops_inode_cksum_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_CKSUM_MODIFY, ope);
	t_ops_inode_cksum_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_inode_cksum_remove(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_inode_cksum_remove",
	    db_journal_write_inode_cksum_remove(sn, t_ops_inode_inum_new()));
}

static gfarm_error_t
t_ops_inode_cksum_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_INODE_CKSUM_REMOVE, ope);
	t_ops_inode_inum_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static struct db_filecopy_arg *
t_ops_filecopy_new(void)
{
	return (db_filecopy_arg_alloc(
	    111, /* inum */
	    "hostname" /* hostname */
	    ));
}

static void
t_ops_filecopy_check(struct db_filecopy_arg *m)
{
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_S("hostname", "hostname", m->hostname);
}

static void
t_ops_filecopy_add(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_filecopy_add",
	    db_journal_write_filecopy_add(sn, t_ops_filecopy_new()));
}

static gfarm_error_t
t_ops_filecopy_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_FILECOPY_ADD, ope);
	t_ops_filecopy_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_filecopy_remove(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_filecopy_remove",
	    db_journal_write_filecopy_remove(sn, t_ops_filecopy_new()));
}

static gfarm_error_t
t_ops_filecopy_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_FILECOPY_REMOVE, ope);
	t_ops_filecopy_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static struct db_deadfilecopy_arg *
t_ops_deadfilecopy_new(void)
{
	return (db_deadfilecopy_arg_alloc(
	    111, /* inum */
	    222, /* gen */
	    "hostname"
	    ));
}

static void
t_ops_deadfilecopy_check(struct db_deadfilecopy_arg *m)
{
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_L("gen", 222, m->igen);
	TEST_ASSERT_S("hostname", "hostname", m->hostname);
}

static void
t_ops_deadfilecopy_add(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_deadfilecopy_add",
	    db_journal_write_deadfilecopy_add(sn, t_ops_deadfilecopy_new()));
}

static gfarm_error_t
t_ops_deadfilecopy_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_DEADFILECOPY_ADD, ope);
	t_ops_deadfilecopy_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_deadfilecopy_remove(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_deadfilecopy_remove",
	    db_journal_write_deadfilecopy_remove(sn, t_ops_deadfilecopy_new()));
}

static gfarm_error_t
t_ops_deadfilecopy_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_DEADFILECOPY_REMOVE, ope);
	t_ops_deadfilecopy_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static struct db_direntry_arg *
t_ops_direntry_new(void)
{
	return (db_direntry_arg_alloc(
	    111, /* dir_inum */
	    "entry_name", /* entry_name */
	    strlen("entry_name"), /* entry_len */
	    222 /* entry_inum */
	    ));
}

static void
t_ops_direntry_check(struct db_direntry_arg *m)
{
	TEST_ASSERT_L("dir_inum", 111, m->dir_inum);
	TEST_ASSERT_L("entry_inum", 222, m->entry_inum);
	TEST_ASSERT_L("entry_len", strlen("entry_name"), m->entry_len);
	TEST_ASSERT_S("entry_name", "entry_name", m->entry_name);
}

static void
t_ops_direntry_add(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_direntry_add",
	    db_journal_write_direntry_add(sn, t_ops_direntry_new()));
}

static gfarm_error_t
t_ops_direntry_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_DIRENTRY_ADD, ope);
	t_ops_direntry_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_direntry_remove(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_direntry_remove",
	    db_journal_write_direntry_remove(sn, t_ops_direntry_new()));
}

static gfarm_error_t
t_ops_direntry_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_DIRENTRY_REMOVE, ope);
	t_ops_direntry_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_symlink_add(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_symlink_add",
	    db_journal_write_symlink_add(sn, db_symlink_arg_alloc(
	    111, /* inum */
	    "source_path")));
}

static gfarm_error_t
t_ops_symlink_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct db_symlink_arg *m = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_SYMLINK_ADD, ope);
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_S("source_path", "source_path", m->source_path);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_symlink_remove(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_symlink_remove",
	    db_journal_write_symlink_remove(sn, t_ops_inode_inum_new()));
}

static gfarm_error_t
t_ops_symlink_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_SYMLINK_REMOVE, ope);
	t_ops_inode_inum_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static struct db_xattr_arg *
t_ops_xattr_new(void)
{
	return (db_xattr_arg_alloc(
	    1, /* xmlMode */
	    111, /* inum */
	    "attrname", /* attrname */
	    "value", /* value */
	    strlen("value") /* size */
	    ));
}

static void
t_ops_xattr_check(struct db_xattr_arg *m)
{
	TEST_ASSERT_I("xmlMode", 1, m->xmlMode);
	TEST_ASSERT_L("inum", 111, m->inum);
	TEST_ASSERT_S("attrname", "attrname", m->attrname);
	TEST_ASSERT_B("value",
	    strncmp("value", m->value, strlen("value")) == 0);
	TEST_ASSERT_L("size", strlen("value"), m->size);
}

static void
t_ops_xattr_add(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_xattr_add",
	    db_journal_write_xattr_add(sn, t_ops_xattr_new()));
}

static gfarm_error_t
t_ops_xattr_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_XATTR_ADD, ope);
	t_ops_xattr_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_xattr_modify(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_xattr_modify",
	    db_journal_write_xattr_modify(sn, t_ops_xattr_new()));
}

static gfarm_error_t
t_ops_xattr_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_XATTR_MODIFY, ope);
	t_ops_xattr_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_xattr_remove(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_xattr_remove",
	    db_journal_write_xattr_remove(sn, t_ops_xattr_new()));
}

static gfarm_error_t
t_ops_xattr_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_XATTR_REMOVE, ope);
	t_ops_xattr_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_xattr_removeall(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_xattr_removeall",
	    db_journal_write_xattr_removeall(sn, t_ops_xattr_new()));
}

static gfarm_error_t
t_ops_xattr_removeall_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_XATTR_REMOVEALL, ope);
	t_ops_xattr_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static struct db_quota_arg *
t_ops_quota_new(void)
{
	struct db_quota_arg *m;
	struct quota *q;

	GFARM_MALLOC(q);
	q->on_db = 1;
	q->grace_period = 111;
	q->space = 222;
	q->space_exceed = 333;
	q->space_soft = 444;
	q->space_hard = 555;
	q->num = 666;
	q->num_exceed = 777;
	q->num_soft = 888;
	q->num_hard = 999;
	q->phy_space = 1110;
	q->phy_space_exceed = 2220;
	q->phy_space_soft = 3330;
	q->phy_space_hard = 4440;
	q->phy_num = 5550;
	q->phy_num_exceed = 6660;
	q->phy_num_soft = 7770;
	q->phy_num_hard = 8880;
	m = db_quota_arg_alloc(
	    q, /* quota */
	    "name", /* name */
	    1 /* is_group */
	    );
	free(q);
	return (m);
}

static void
t_ops_quota_check(struct db_quota_arg *m)
{
	struct quota *q = &m->quota;

	TEST_ASSERT_I("quota.on_db", 1, q->on_db);
	TEST_ASSERT_L("quota.grace_period", 111, q->grace_period);
	TEST_ASSERT_L("quota.space", 222, q->space);
	TEST_ASSERT_L("quota.space_exceed", 333, q->space_exceed);
	TEST_ASSERT_L("quota.space_soft", 444, q->space_soft);
	TEST_ASSERT_L("quota.space_hard", 555, q->space_hard);
	TEST_ASSERT_L("quota.num", 666, q->num);
	TEST_ASSERT_L("quota.num_exceed", 777, q->num_exceed);
	TEST_ASSERT_L("quota.num_soft", 888, q->num_soft);
	TEST_ASSERT_L("quota.num_hard", 999, q->num_hard);
	TEST_ASSERT_L("quota.phy_space", 1110, q->phy_space);
	TEST_ASSERT_L("quota.phy_space_exceed", 2220, q->phy_space_exceed);
	TEST_ASSERT_L("quota.phy_space_soft", 3330, q->phy_space_soft);
	TEST_ASSERT_L("quota.phy_space_hard", 4440, q->phy_space_hard);
	TEST_ASSERT_L("quota.phy_num", 5550, q->phy_num);
	TEST_ASSERT_L("quota.phy_num_exceed", 6660, q->phy_num_exceed);
	TEST_ASSERT_L("quota.phy_num_soft", 7770, q->phy_num_soft);
	TEST_ASSERT_L("quota.phy_num_hard", 8880, q->phy_num_hard);
	TEST_ASSERT_I("is_group", 1, m->is_group);
	TEST_ASSERT_S("name", "name", m->name);
}

static void
t_ops_quota_add(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_quota_add",
	    db_journal_write_quota_add(sn, t_ops_quota_new()));
}

static gfarm_error_t
t_ops_quota_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_QUOTA_ADD, ope);
	t_ops_quota_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_quota_modify(gfarm_uint64_t sn)
{
	db_journal_write_quota_modify(5, t_ops_quota_new());
}

static gfarm_error_t
t_ops_quota_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_QUOTA_MODIFY, ope);
	t_ops_quota_check(obj);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_quota_remove(gfarm_uint64_t sn)
{
	TEST_ASSERT_NOERR("db_journal_write_quota_remove",
	    db_journal_write_quota_remove(sn, db_quota_remove_arg_alloc(
	    "name", /* name */
	    1 /* is_group */
	    )));
}

static gfarm_error_t
t_ops_quota_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct db_quota_remove_arg *m = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_QUOTA_REMOVE, ope);
	TEST_ASSERT_I("is_group", 1, m->is_group);
	TEST_ASSERT_S("name", "name", m->name);
	return (GFARM_ERR_NO_ERROR);
}

static struct gfarm_metadb_server *
t_ops_mdhost_new(void)
{
	struct gfarm_metadb_server *ms;

	GFARM_MALLOC(ms);
	ms->name = assert_strdup("name");
	ms->clustername = assert_strdup("clustername");
	ms->port = 1234;
	ms->flags = 2;
	return (ms);
}

static void
t_ops_mdhost_add(gfarm_uint64_t seqnum)
{
	struct gfarm_metadb_server *ms, *dms;

	ms = t_ops_mdhost_new();
	dms = db_mdhost_dup(ms, sizeof(*ms));
	db_journal_metadb_server_destroy(ms);
	TEST_ASSERT_NOERR("db_journal_write_mdhost_add",
	    db_journal_write_mdhost_add(5, dms));
}

static void
t_ops_mdhost_check(struct gfarm_metadb_server *ms)
{
	TEST_ASSERT_S("name", "name", ms->name);
	TEST_ASSERT_S("clustername", "clustername", ms->clustername);
	TEST_ASSERT_I("port", 1234, ms->port);
	TEST_ASSERT_I("flags", 2, ms->flags);
}

static gfarm_error_t
t_ops_mdhost_add_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_MDHOST_ADD, ope);
	t_ops_mdhost_check(obj);
	return (GFARM_ERR_NO_ERROR);
}


static void
t_ops_mdhost_modify(gfarm_uint64_t sn)
{
	struct db_mdhost_modify_arg *dms;
	struct gfarm_metadb_server *ms;

	ms = t_ops_mdhost_new();
	dms = db_mdhost_modify_arg_alloc(ms,
	    0 /* modflags */);
	db_journal_metadb_server_destroy(ms);
	TEST_ASSERT_NOERR("db_journal_write_mdhost_modify",
	    db_journal_write_mdhost_modify(sn, dms));
}

static gfarm_error_t
t_ops_mdhost_modify_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct db_mdhost_modify_arg *m = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_MDHOST_MODIFY, ope);
	TEST_ASSERT_I("modflags", 0, m->modflags);
	t_ops_mdhost_check(&m->ms);
	return (GFARM_ERR_NO_ERROR);
}

static void
t_ops_mdhost_remove(gfarm_uint64_t sn)
{
	db_journal_write_mdhost_remove(sn, assert_strdup("name"));
}

static gfarm_error_t
t_ops_mdhost_remove_check(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	const char *name = obj;

	TEST_ASSERT_L("seqnum", 5, seqnum);
	TEST_ASSERT_I("ope", GFM_JOURNAL_MDHOST_REMOVE, ope);
	TEST_ASSERT_S("name", "name", name);
	return (GFARM_ERR_NO_ERROR);
}


struct t_ops_info {
	const char *name;
	void (*write)(gfarm_uint64_t);
	journal_post_read_op_t check;
};

static struct t_ops_info ops_tests[] = {
	{ "t_ops_host_add",
	    t_ops_host_add, t_ops_host_add_check },
	{ "t_ops_host_modify",
	    t_ops_host_modify, t_ops_host_modify_check },
	{ "t_ops_host_remove",
	    t_ops_host_remove, t_ops_host_remove_check },
	{ "t_ops_user_add",
	    t_ops_user_add, t_ops_user_add_check },
	{ "t_ops_user_modify",
	    t_ops_user_modify, t_ops_user_modify_check },
	{ "t_ops_user_remove",
	    t_ops_user_remove, t_ops_user_remove_check },
	{ "t_ops_group_add",
	    t_ops_group_add, t_ops_group_add_check },
	{ "t_ops_group_modify",
	    t_ops_group_modify, t_ops_group_modify_check },
	{ "t_ops_group_remove",
	    t_ops_group_remove, t_ops_group_remove_check },
	{ "t_ops_inode_add",
	    t_ops_inode_add, t_ops_inode_add_check },
	{ "t_ops_inode_modify",
	    t_ops_inode_modify, t_ops_inode_modify_check },
	{ "t_ops_inode_gen_modify",
	    t_ops_inode_gen_modify, t_ops_inode_gen_modify_check },
	{ "t_ops_inode_nlink_modify",
	    t_ops_inode_nlink_modify, t_ops_inode_nlink_modify_check },
	{ "t_ops_inode_size_modify",
	    t_ops_inode_size_modify, t_ops_inode_size_modify_check },
	{ "t_ops_inode_mode_modify",
	    t_ops_inode_mode_modify, t_ops_inode_mode_modify_check },
	{ "t_ops_inode_user_modify",
	    t_ops_inode_user_modify, t_ops_inode_user_modify_check },
	{ "t_ops_inode_group_modify",
	    t_ops_inode_group_modify, t_ops_inode_group_modify_check },
	{ "t_ops_inode_atime_modify",
	    t_ops_inode_atime_modify, t_ops_inode_atime_modify_check },
	{ "t_ops_inode_mtime_modify",
	    t_ops_inode_mtime_modify, t_ops_inode_mtime_modify_check },
	{ "t_ops_inode_ctime_modify",
	    t_ops_inode_ctime_modify, t_ops_inode_ctime_modify_check },
	{ "t_ops_inode_cksum_add",
	    t_ops_inode_cksum_add, t_ops_inode_cksum_add_check },
	{ "t_ops_inode_cksum_modify",
	    t_ops_inode_cksum_modify, t_ops_inode_cksum_modify_check },
	{ "t_ops_inode_cksum_remove",
	    t_ops_inode_cksum_remove, t_ops_inode_cksum_remove_check },
	{ "t_ops_filecopy_add",
	    t_ops_filecopy_add, t_ops_filecopy_add_check },
	{ "t_ops_filecopy_remove",
	    t_ops_filecopy_remove, t_ops_filecopy_remove_check },
	{ "t_ops_deadfilecopy_add",
	    t_ops_deadfilecopy_add, t_ops_deadfilecopy_add_check },
	{ "t_ops_deadfilecopy_remove",
	    t_ops_deadfilecopy_remove, t_ops_deadfilecopy_remove_check },
	{ "t_ops_direntry_add",
	    t_ops_direntry_add, t_ops_direntry_add_check },
	{ "t_ops_direntry_remove",
	    t_ops_direntry_remove, t_ops_direntry_remove_check },
	{ "t_ops_symlink_add",
	    t_ops_symlink_add, t_ops_symlink_add_check },
	{ "t_ops_symlink_remove",
	    t_ops_symlink_remove, t_ops_symlink_remove_check },
	{ "t_ops_xattr_add",
	    t_ops_xattr_add, t_ops_xattr_add_check },
	{ "t_ops_xattr_modify",
	    t_ops_xattr_modify, t_ops_xattr_modify_check },
	{ "t_ops_xattr_remove",
	    t_ops_xattr_remove, t_ops_xattr_remove_check },
	{ "t_ops_xattr_removeall",
	    t_ops_xattr_removeall, t_ops_xattr_removeall_check },
	{ "t_ops_quota_add",
	    t_ops_quota_add, t_ops_quota_add_check },
	{ "t_ops_quota_modify",
	    t_ops_quota_modify, t_ops_quota_modify_check },
	{ "t_ops_quota_remove",
	    t_ops_quota_remove, t_ops_quota_remove_check },
	{ "t_ops_mdhost_add",
	    t_ops_mdhost_add, t_ops_mdhost_add_check },
	{ "t_ops_mdhost_modify",
	    t_ops_mdhost_modify, t_ops_mdhost_modify_check },
	{ "t_ops_mdhost_remove",
	    t_ops_mdhost_remove, t_ops_mdhost_remove_check },
};

void
t_ops(void)
{
	int i, eof;
	struct t_ops_info *ti;
	struct journal_file_reader *reader;

	unlink_test_file(filepath);
	TEST_ASSERT_NOERR("journal_file_open",
	    journal_file_open(filepath, 4096 + 300, 0,
	    &self_jf, GFARM_JOURNAL_RDWR));

	reader = journal_file_main_reader(self_jf);
	for (i = 0; i < GFARM_ARRAY_LENGTH(ops_tests); ++i) {
		ti = &ops_tests[i];
		journal_seqnum_pre = 4;
		ti->write(5);
		printf("executing %s_check ...", ti->name);
		fflush(stdout);
		TEST_ASSERT_NOERR("db_journal_read",
		    db_journal_read(reader, NULL, ti->check, NULL, &eof));
		printf("ok\n");
		journal_file_reader_commit_pos(reader);
	}
	journal_file_close(self_jf);
}

/***********************************************/
/* t_apply */

#define T_APPLY_HOST_NAME "host1"

static void
t_apply_host_add(void)
{
	const char *hostname = T_APPLY_HOST_NAME;
	struct gfarm_host_info hi;
	struct host *h;

	hi.hostname = assert_strdup(hostname);
	hi.port = 1234;
	hi.nhostaliases = 0; /* setting aliases is not implemented */
	hi.hostaliases = NULL;
	hi.architecture = assert_strdup("arch");
	hi.ncpu = 1;
	hi.flags = 2;

	TEST_ASSERT_B("host_lookup",
	    (h = host_lookup(hostname)) == NULL);
	TEST_ASSERT_NOERR("host_add",
	    db_journal_apply_ops.host_add(0, &hi));
	gfarm_host_info_free(&hi);
	TEST_ASSERT_B("host_lookup",
	    (h = host_lookup(hostname)) != NULL);
	TEST_ASSERT_S("hostname",
	    hostname, host_name(h));
	TEST_ASSERT_I("port",
	    1234, host_port(h));
	TEST_ASSERT_S("architecture",
	    "arch", host_architecture(h));
	TEST_ASSERT_I("ncpu",
	    1, host_ncpu(h));
	TEST_ASSERT_I("flags",
	    2, host_flags(h));
}

static void
t_apply_host_modify(void)
{
	const char *hostname = T_APPLY_HOST_NAME;
	struct db_host_modify_arg m;
	struct host *h;

	TEST_ASSERT_B("host_lookup",
	    (h = host_lookup(hostname)) != NULL);

	memset(&m.hi, 0, sizeof(m.hi));
	m.hi.hostname = assert_strdup(hostname);
	m.hi.port = 5678;
	m.hi.nhostaliases = 0; /* setting aliases is not implemented */
	m.hi.hostaliases = NULL;
	m.hi.architecture = assert_strdup("arch2");
	m.hi.ncpu = 4;
	m.hi.flags = 5;
	m.modflags = DB_HOST_MOD_FLAGS; /* modflags is not used yet */

	TEST_ASSERT_NOERR("host_modify",
	    db_journal_apply_ops.host_modify(0, &m));
	gfarm_host_info_free(&m.hi);
	TEST_ASSERT_I("port",
	    5678, host_port(h));
	TEST_ASSERT_S("architecture",
	    "arch2", host_architecture(h));
	TEST_ASSERT_I("ncpu",
	    4, host_ncpu(h));
	TEST_ASSERT_I("flags",
	    5, host_flags(h));
}

static void
t_apply_host_remove(void)
{
	const char *hostname = T_APPLY_HOST_NAME;
	struct host *h;

	TEST_ASSERT_B("host_lookup",
	    (h = host_lookup(hostname)) != NULL);
	TEST_ASSERT_NOERR("host_remove",
	    db_journal_apply_ops.host_remove(0, (char *)hostname));
	TEST_ASSERT_B("host_lookup",
	    (h = host_lookup(hostname)) == NULL);
}

#define T_APPLY_USER_NAME "user1"

static void
t_apply_user_add(void)
{
	const char *username = T_APPLY_USER_NAME;
	const char *realname = "USER1";
	struct gfarm_user_info ui;
	struct user *u;

	TEST_ASSERT_B("user_lookup",
	    (user_lookup(username) == NULL));
	memset(&ui, 0, sizeof(ui));
	ui.username = assert_strdup(username);
	ui.realname = assert_strdup(realname);
	TEST_ASSERT_NOERR("user_add",
	    db_journal_apply_ops.user_add(0, &ui));
	gfarm_user_info_free(&ui);
	TEST_ASSERT_B("user_lookup",
	    (u = user_lookup(username)) != NULL);
	TEST_ASSERT_S("user_name",
	    username, user_name(u));
	TEST_ASSERT_S("user_realname",
	    realname, user_realname(u));
}

static void
t_apply_user_modify(void)
{
	const char *username = T_APPLY_USER_NAME;
	const char *realname = "USER1-MOD";
	struct db_user_modify_arg m;
	struct user *u;

	/* user1 is created by t_apply_user_add() */

	memset(&m, 0, sizeof(m));
	m.ui.username = assert_strdup(username);
	m.ui.realname = assert_strdup(realname);
	m.modflags = 0xFFFF;  /* modflags is not used yet */

	TEST_ASSERT_NOERR("user_modify",
	    db_journal_apply_ops.user_modify(0, &m));
	gfarm_user_info_free(&m.ui);
	TEST_ASSERT_B("user_lookup",
	    (u = user_lookup(username)) != NULL);
	TEST_ASSERT_S("user_name",
	    username, user_name(u));
	TEST_ASSERT_S("user_realname",
	    realname, user_realname(u));
}

static void
t_apply_user_remove(void)
{
	const char *username = T_APPLY_USER_NAME;
	struct user *u;

	TEST_ASSERT_NOERR("user_remove",
	    db_journal_apply_ops.user_remove(0, (char *)username));
	/* invalid user cannot be acquired. */
	TEST_ASSERT_B("user_lookup",
	    (u = user_lookup(username)) == NULL);
}

#define T_APPLY_GROUP_NAME "group1"

static void
t_apply_group_add(void)
{
	const char *username = T_APPLY_USER_NAME;
	const char *groupname = T_APPLY_GROUP_NAME;
	struct gfarm_user_info ui;
	struct gfarm_group_info gi;
	struct group *g;
	struct user *u;

	/* add user1 */
	memset(&ui, 0, sizeof(ui));
	ui.username = assert_strdup(username);
	ui.realname = assert_strdup("USER1");
	TEST_ASSERT_NOERR("user_add",
	    db_journal_apply_ops.user_add(0, &ui));
	gfarm_user_info_free(&ui);
	TEST_ASSERT_B("user_lookup",
	    (u = user_lookup(username)) != NULL);

	/* add group1 */
	memset(&gi, 0, sizeof(gi));
	gi.groupname = assert_strdup(groupname);
	gi.nusers = 1;
	gi.usernames = t_make_strary1(T_APPLY_USER_NAME);

	TEST_ASSERT_B("group_lookup",
	    group_lookup(groupname) == NULL);
	TEST_ASSERT_NOERR("group_add",
	    db_journal_apply_ops.group_add(0, &gi));
	gfarm_group_info_free(&gi);
	TEST_ASSERT_B("group_lookup",
	    (g = group_lookup(groupname)) != NULL);
	TEST_ASSERT_S("group_name",
	    groupname, group_name(g));
	TEST_ASSERT_B("user_in_group",
	    user_in_group(u, g));
}

static void
t_apply_group_modify(void)
{
	const char *username = T_APPLY_USER_NAME;
	const char *groupname = T_APPLY_GROUP_NAME;
	struct db_group_modify_arg m;
	struct group *g;
	struct user *u;

	/* user1 and group1 is created in t_apply_group_add */
	TEST_ASSERT_B("user_lookup",
	    (u = user_lookup(username)) != NULL);
	TEST_ASSERT_B("group_lookup",
	    (g = group_lookup(groupname)) != NULL);

	memset(&m, 0, sizeof(m));
	m.gi.groupname = assert_strdup(groupname);
	m.gi.nusers = 0;
	m.gi.usernames = NULL;
	m.modflags = 0xFFFF;  /* modflags is not used yet */

	TEST_ASSERT_NOERR("group_modify",
	    db_journal_apply_ops.group_modify(0, &m));
	gfarm_group_info_free(&m.gi);
	TEST_ASSERT_B("user_in_group",
	    user_in_group(u, g) == 0);

	TEST_ASSERT_NOERR("user_remove",
	    db_journal_apply_ops.user_remove(0, (char *)username));
}

static void
t_apply_group_remove(void)
{
	const char *groupname = T_APPLY_GROUP_NAME;
	struct group *g;

	TEST_ASSERT_B("group_lookup",
	    (g = group_lookup(groupname)) != NULL);
	TEST_ASSERT_NOERR("group_remove",
	    db_journal_apply_ops.group_remove(0, (char *)groupname));
	TEST_ASSERT_B("group_is_invalidated",
	    group_is_invalid(g));
}

#define T_APPLY_INODE_FILE_INUM 10

static struct host *
t_add_test_host(const char *hostname)
{
	struct gfarm_host_info hi;
	struct host *h;

	memset(&hi, 0, sizeof(hi));
	hi.hostname = assert_strdup(hostname);
	hi.architecture = assert_strdup("arch");
	hi.ncpu = 1;
	TEST_ASSERT_NOERR("host_add",
	    db_journal_apply_ops.host_add(0, &hi));
	gfarm_host_info_free(&hi);
	TEST_ASSERT_B("host_lookup",
	    (h = host_lookup(hostname)) != NULL);
	return (h);
}

static struct user *
t_add_test_user(const char *username)
{
	struct gfarm_user_info ui;
	struct user *u;

	memset(&ui, 0, sizeof(ui));
	ui.username = assert_strdup(username);
	ui.realname = assert_strdup("realname");
	TEST_ASSERT_NOERR("user_add",
	    db_journal_apply_ops.user_add(0, &ui));
	gfarm_user_info_free(&ui);
	TEST_ASSERT_B("user_lookup",
	    (u = user_lookup(username)) != NULL);
	return (u);
}

static struct inode *
t_add_test_inode(int inum, gfarm_mode_t mode)
{
	struct gfs_stat st;
	struct inode *i;
	struct gfarm_timespec atm, mtm, ctm;

	st.st_ino = inum;
	st.st_gen = 1;
	st.st_mode = mode|GFARM_S_ALLPERM;
	st.st_nlink = 0;
	st.st_user = assert_strdup("inode_user1");
	st.st_group = assert_strdup("inode_group1");
	st.st_size = 123;
	atm.tv_sec = 111;
	atm.tv_nsec = 222;
	st.st_atimespec = atm;
	mtm.tv_sec = 333;
	mtm.tv_nsec = 444;
	st.st_mtimespec = mtm;
	ctm.tv_sec = 555;
	ctm.tv_nsec = 666;
	st.st_ctimespec = ctm;

	TEST_ASSERT_NOERR("inode_add",
	    db_journal_apply_ops.inode_add(0, &st));
	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(inum)) != NULL);
	return (i);
}

static struct group *
t_add_test_group(const char *groupname)
{
	struct gfarm_group_info gi;
	struct group *g;

	memset(&gi, 0, sizeof(gi));
	gi.groupname = assert_strdup(groupname);
	gi.nusers = 0;
	gi.usernames = NULL;

	TEST_ASSERT_NOERR("group_add",
	    db_journal_apply_ops.group_add(0, &gi));
	gfarm_group_info_free(&gi);
	TEST_ASSERT_B("group_lookup",
	    (g = group_lookup(groupname)) != NULL);
	return (g);
}

static void
t_apply_inode_add(void)
{
	struct gfs_stat st;
	struct inode *i;
	struct user *u;
	struct group *g;
	struct gfarm_timespec atm, mtm, ctm;

	u = t_add_test_user("inode_user1");
	g = t_add_test_group("inode_group1");

	st.st_ino = T_APPLY_INODE_FILE_INUM;
	st.st_gen = 1;
	st.st_mode = GFARM_S_IFREG|GFARM_S_ALLPERM;
	st.st_nlink = 0;
	st.st_user = assert_strdup(user_name(u));
	st.st_group = assert_strdup(group_name(g));
	st.st_size = 123;
	atm.tv_sec = 111;
	atm.tv_nsec = 222;
	st.st_atimespec = atm;
	mtm.tv_sec = 333;
	mtm.tv_nsec = 444;
	st.st_mtimespec = mtm;
	ctm.tv_sec = 555;
	ctm.tv_nsec = 666;
	st.st_ctimespec = ctm;

	TEST_ASSERT_NOERR("inode_add",
	    db_journal_apply_ops.inode_add(0, &st));
	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	TEST_ASSERT_L("st_gen",
	    1, inode_get_gen(i));
	TEST_ASSERT_I("st_mode",
	    (GFARM_S_IFREG|GFARM_S_ALLPERM), inode_get_mode(i));
	TEST_ASSERT_L("st_nlink",
	    0, inode_get_nlink(i));
	TEST_ASSERT_B("st_user",
	    u == inode_get_user(i));
	TEST_ASSERT_B("st_group",
	    g == inode_get_group(i));
	TEST_ASSERT_L("st_size",
	    123, inode_get_size(i));
	TEST_ASSERT_T("st_atimespec",
	    atm, *inode_get_atime(i));
	TEST_ASSERT_T("st_mtimespec",
	    mtm, *inode_get_mtime(i));
	TEST_ASSERT_T("st_ctimespec",
	    ctm, *inode_get_ctime(i));
}

static void
t_apply_inode_modify(void)
{
	struct gfs_stat st;
	struct inode *i;
	struct user *u;
	struct group *g;
	struct gfarm_timespec atm, mtm, ctm;

	u = t_add_test_user("inode_user2");
	g = t_add_test_group("inode_group2");

	st.st_ino = T_APPLY_INODE_FILE_INUM;
	st.st_gen = 2;
	st.st_mode = GFARM_S_IFREG|0755;
	st.st_nlink = 0;
	st.st_user = assert_strdup(user_name(u));
	st.st_group = assert_strdup(group_name(g));
	st.st_size = 1123;
	atm.tv_sec = 1111;
	atm.tv_nsec = 1222;
	st.st_atimespec = atm;
	mtm.tv_sec = 1333;
	mtm.tv_nsec = 1444;
	st.st_mtimespec = mtm;
	ctm.tv_sec = 1555;
	ctm.tv_nsec = 1666;
	st.st_ctimespec = ctm;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	TEST_ASSERT_NOERR("inode_modify",
	    db_journal_apply_ops.inode_modify(0, &st));
	TEST_ASSERT_L("st_gen",
	    2, inode_get_gen(i));
	TEST_ASSERT_I("st_mode",
	    (GFARM_S_IFREG|0755), inode_get_mode(i));
	TEST_ASSERT_L("st_nlink",
	    0, inode_get_nlink(i));
	TEST_ASSERT_B("st_user",
	    u == inode_get_user(i));
	TEST_ASSERT_B("st_group",
	    g == inode_get_group(i));
	TEST_ASSERT_L("st_size",
	    1123, inode_get_size(i));
	TEST_ASSERT_T("st_atimespec",
	    atm, *inode_get_atime(i));
	TEST_ASSERT_T("st_mtimespec",
	    mtm, *inode_get_mtime(i));
	TEST_ASSERT_T("st_ctimespec",
	    ctm, *inode_get_ctime(i));
}

static void
t_apply_inode_gen_modify(void)
{
	struct inode *i;
	struct db_inode_uint64_modify_arg m;

	m.inum = T_APPLY_INODE_FILE_INUM;
	m.uint64 = 3;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_gen",
	    m.uint64 != inode_get_gen(i));
	TEST_ASSERT_NOERR("inode_gen_modify",
	    db_journal_apply_ops.inode_gen_modify(0, &m));
	TEST_ASSERT_L("st_gen",
	    3, inode_get_gen(i));
}

static void
t_apply_inode_nlink_modify(void)
{
	struct inode *i;
	struct db_inode_uint64_modify_arg m;

	m.inum = T_APPLY_INODE_FILE_INUM;
	m.uint64 = 1;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_nlink",
	    m.uint64 != inode_get_nlink(i));
	TEST_ASSERT_NOERR("inode_nlink_modify",
	    db_journal_apply_ops.inode_nlink_modify(0, &m));
	TEST_ASSERT_L("st_nlink",
	    1, inode_get_nlink(i));
}

static void
t_apply_inode_size_modify(void)
{
	struct inode *i;
	struct db_inode_uint64_modify_arg m;

	m.inum = T_APPLY_INODE_FILE_INUM;
	m.uint64 = 4321;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_size",
	    m.uint64 != inode_get_size(i));
	TEST_ASSERT_NOERR("inode_size_modify",
	    db_journal_apply_ops.inode_size_modify(0, &m));
	TEST_ASSERT_L("st_size",
	    4321, inode_get_size(i));
}

static void
t_apply_inode_mode_modify(void)
{
	struct inode *i;
	struct db_inode_uint32_modify_arg m;

	m.inum = T_APPLY_INODE_FILE_INUM;
	m.uint32 = GFARM_S_IFREG|0644;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_mode",
	    m.uint32 != inode_get_mode(i));
	TEST_ASSERT_NOERR("inode_mode_modify",
	    db_journal_apply_ops.inode_mode_modify(0, &m));
	TEST_ASSERT_L("st_mode",
	    (GFARM_S_IFREG|0644), inode_get_mode(i));
}

static void
t_apply_inode_user_modify(void)
{
	struct inode *i;
	struct user *u;
	struct db_inode_string_modify_arg m;

	u = t_add_test_user("inode_user3");
	m.inum = T_APPLY_INODE_FILE_INUM;
	m.string = assert_strdup(user_name(u));

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_user",
	    strcmp(m.string, user_name(inode_get_user(i))) != 0);
	TEST_ASSERT_NOERR("inode_user_modify",
	    db_journal_apply_ops.inode_user_modify(0, &m));
	TEST_ASSERT_B("st_user",
	    u == inode_get_user(i));
}

static void
t_apply_inode_group_modify(void)
{
	struct inode *i;
	struct group *u;
	struct db_inode_string_modify_arg m;

	u = t_add_test_group("inode_group3");
	m.inum = T_APPLY_INODE_FILE_INUM;
	m.string = assert_strdup(group_name(u));

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_group",
	    strcmp(m.string, group_name(inode_get_group(i))) != 0);
	TEST_ASSERT_NOERR("inode_group_modify",
	    db_journal_apply_ops.inode_group_modify(0, &m));
	TEST_ASSERT_B("st_group",
	    u == inode_get_group(i));
}

static void
t_apply_inode_atime_modify(void)
{
	struct inode *i;
	struct db_inode_timespec_modify_arg m;
	struct gfarm_timespec tm;

	m.inum = T_APPLY_INODE_FILE_INUM;
	tm.tv_sec = 2111;
	tm.tv_nsec = 2222;
	m.time = tm;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_atimespec",
	    memcmp(&tm, inode_get_atime(i), sizeof(tm)) != 0);
	TEST_ASSERT_NOERR("inode_atime_modify",
	    db_journal_apply_ops.inode_atime_modify(0, &m));
	TEST_ASSERT_T("st_atimespec",
	    tm, *inode_get_atime(i));
}

static void
t_apply_inode_mtime_modify(void)
{
	struct inode *i;
	struct db_inode_timespec_modify_arg m;
	struct gfarm_timespec tm;

	m.inum = T_APPLY_INODE_FILE_INUM;
	tm.tv_sec = 2333;
	tm.tv_nsec = 2444;
	m.time = tm;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_mtimespec",
	    memcmp(&tm, inode_get_mtime(i), sizeof(tm)) != 0);
	TEST_ASSERT_NOERR("inode_mtime_modify",
	    db_journal_apply_ops.inode_mtime_modify(0, &m));
	TEST_ASSERT_T("st_mtimespec",
	    tm, *inode_get_mtime(i));
}

static void
t_apply_inode_ctime_modify(void)
{
	struct inode *i;
	struct db_inode_timespec_modify_arg m;
	struct gfarm_timespec tm;

	m.inum = T_APPLY_INODE_FILE_INUM;
	tm.tv_sec = 2555;
	tm.tv_nsec = 2666;
	m.time = tm;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_B("current st_atimespec",
	    memcmp(&tm, inode_get_atime(i), sizeof(tm)) != 0);
	TEST_ASSERT_NOERR("inode_atime_modify",
	    db_journal_apply_ops.inode_atime_modify(0, &m));
	TEST_ASSERT_T("st_atimespec",
	    tm, *inode_get_atime(i));
}

static void
t_apply_filecopy_add(void)
{
	struct inode *i;
	struct host *h1, *h2;
	struct db_filecopy_arg m;
	int nhosts;
	char **hostnames;

	h1 = t_add_test_host("filecopy_host1");
	h2 = t_add_test_host("filecopy_host2");
	m.inum = T_APPLY_INODE_FILE_INUM;
	m.hostname = assert_strdup(host_name(h1));

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_NOERR("filecopy_add",
	    db_journal_apply_ops.filecopy_add(0, &m));

	m.hostname = assert_strdup(host_name(h2));

	TEST_ASSERT_NOERR("filecopy_add",
	    db_journal_apply_ops.filecopy_add(0, &m));

	TEST_ASSERT_NOERR("inode_replica_list_by_name_with_dead_host",
	    inode_replica_list_by_name_with_dead_host(
		i, &nhosts, &hostnames));
	TEST_ASSERT_I("nhosts",
	    2, nhosts);
	TEST_ASSERT_S("hostnames[0]",
	    host_name(h2), hostnames[0]);
	TEST_ASSERT_S("hostnames[1]",
	    host_name(h1), hostnames[1]);

	free(hostnames[0]);
	free(hostnames[1]);
	free(hostnames);
}

static void
t_apply_filecopy_remove(void)
{
	struct inode *i;
	struct db_filecopy_arg m;
	int nhosts;
	char **hostnames;

	m.inum = T_APPLY_INODE_FILE_INUM;
	m.hostname = assert_strdup("filecopy_host1");

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(m.inum)) != NULL);
	TEST_ASSERT_NOERR("filecopy_remove",
	    db_journal_apply_ops.filecopy_remove(0, &m));
	TEST_ASSERT_NOERR("inode_replica_list_by_name_with_dead_host",
	    inode_replica_list_by_name_with_dead_host(
		i, &nhosts, &hostnames));
	TEST_ASSERT_I("nhosts",
	    1, nhosts);
	TEST_ASSERT_S("hostnames[0]",
	    "filecopy_host2", hostnames[0]);

	free(hostnames[0]);
	free(hostnames);
}

static void
t_apply_deadfilecopy_add(void)
{
	struct inode *i;
	struct db_deadfilecopy_arg m;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = T_APPLY_INODE_FILE_INUM;
	m.igen = inode_get_gen(i);
	m.hostname = assert_strdup("filecopy_host1");

	/* no action */
	TEST_ASSERT_NOERR("deadfilecopy_add",
	    db_journal_apply_ops.deadfilecopy_add(0, &m));
}

static void
t_apply_deadfilecopy_remove(void)
{
	struct inode *i;
	struct db_deadfilecopy_arg m;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = T_APPLY_INODE_FILE_INUM;
	m.igen = inode_get_gen(i);
	m.hostname = assert_strdup("filecopy_host1");

	/* no action */
	TEST_ASSERT_NOERR("deadfilecopy_remove",
	    db_journal_apply_ops.deadfilecopy_remove(0, &m));
}

#define T_APPLY_INODE_DIR_INUM 20

static void
t_apply_direntry_add(void)
{
	struct inode *id, *ie, *ie1;
	struct db_direntry_arg m;
	const char *entry_name = "entry";
	char *entry_name1;
	Dir dir;
	DirCursor curs;

	id = t_add_test_inode(T_APPLY_INODE_DIR_INUM, GFARM_S_IFDIR);
	TEST_ASSERT_B("inode_lookup",
	    (ie = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);

	m.dir_inum = inode_get_number(id);
	m.entry_inum = inode_get_number(ie);
	m.entry_name = assert_strdup(entry_name);
	m.entry_len = strlen(entry_name);

	TEST_ASSERT_NOERR("direntry_add",
	    db_journal_apply_ops.direntry_add(0, &m));
	TEST_ASSERT_B("inode_get_dir",
	    (dir = inode_get_dir(id)) != NULL);
	TEST_ASSERT_B("dir_cursor_set_pos",
	    dir_cursor_set_pos(dir, 0, &curs));
	TEST_ASSERT_NOERR("dir_cursor_get_name_and_inode",
	    dir_cursor_get_name_and_inode(dir, &curs, &entry_name1, &ie1));
	TEST_ASSERT_B("dir_cursor_get_entry",
	    entry_name1 != NULL);
	TEST_ASSERT_S("entry_name",
	    entry_name, entry_name1);
	TEST_ASSERT_B("dir_entry_get_inode",
	    ie1 != NULL);
	TEST_ASSERT_B("entry",
	    ie == ie1);
	free(entry_name1);
}

static void
t_apply_direntry_remove(void)
{
	struct inode *id, *ie;
	struct db_direntry_arg m;
	const char *entry_name = "entry";
	Dir dir;
	DirCursor curs;

	TEST_ASSERT_B("inode_lookup",
	    (id = inode_lookup(T_APPLY_INODE_DIR_INUM)) != NULL);
	TEST_ASSERT_B("inode_lookup",
	    (ie = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);

	m.dir_inum = inode_get_number(id);
	m.entry_name = assert_strdup(entry_name);
	m.entry_len = strlen(entry_name);

	TEST_ASSERT_NOERR("direntry_remove",
	    db_journal_apply_ops.direntry_remove(0, &m));
	TEST_ASSERT_B("inode_get_dir",
	    (dir = inode_get_dir(id)) != NULL);
	TEST_ASSERT_B("dir_cursor_set_pos",
	    dir_cursor_set_pos(dir, 0, &curs) == 0);
}

#define T_APPLY_INODE_SYMLINK_INUM 30

static void
t_apply_symlink_add(void)
{
	struct inode *i;
	struct db_symlink_arg m;

	i = t_add_test_inode(T_APPLY_INODE_SYMLINK_INUM, GFARM_S_IFLNK);
	m.inum = inode_get_number(i);
	m.source_path = assert_strdup("notexist");

	TEST_ASSERT_NOERR("symlink_add",
	    db_journal_apply_ops.symlink_add(0, &m));
	TEST_ASSERT_S("inode_get_symlink",
	    "notexist", inode_get_symlink(i));
}

static void
t_apply_symlink_remove(void)
{
	struct inode *i;
	struct db_inode_inum_arg m;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_SYMLINK_INUM)) != NULL);
	m.inum = inode_get_number(i);

	TEST_ASSERT_NOERR("symlink_remove",
	    db_journal_apply_ops.symlink_remove(0, &m));
	TEST_ASSERT_B("inode_get_symlink",
	    inode_get_symlink(i) == NULL);
}

static void
t_apply_xattr_add(void)
{
	struct inode *i;
	struct db_xattr_arg m;
	const char *attrname = "attrname";
	const char *value = "value";

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = inode_get_number(i);
	m.xmlMode = 0;
	m.attrname = assert_strdup(attrname);
	m.value = assert_strdup(value);
	m.size = strlen(value);

	TEST_ASSERT_NOERR("xattr_add",
	    db_journal_apply_ops.xattr_add(0, &m));

	TEST_ASSERT_B("inode_xattr_has_attr",
	    inode_xattr_has_attr(i, 0 /*xmlMode*/, attrname));
}

static void
t_apply_xattr_add_cached(void)
{
	struct inode *i;
	struct db_xattr_arg m;
	const char *attrname = GFARM_EA_NCOPY;
	const char *value = "2";
	void *value1;
	size_t value_len;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = inode_get_number(i);
	m.xmlMode = 0;
	m.attrname = assert_strdup(attrname);
	m.value = assert_strdup(value);
	m.size = strlen(value);

	TEST_ASSERT_NOERR("xattr_add",
	    db_journal_apply_ops.xattr_add(0, &m));

	TEST_ASSERT_NOERR("inode_xattr_get_cache",
	    inode_xattr_get_cache(i, 0 /*xmlMode*/,
		attrname, (void **)&value1, &value_len));

	TEST_ASSERT_L("value_len",
	    strlen(value), value_len);
	TEST_ASSERT_B("value",
	    strncmp(value, value1, value_len) == 0);
}

static void
t_apply_xattr_modify(void)
{
	struct inode *i;
	struct db_xattr_arg m;
	const char *attrname = "attrname";
	const char *value = "value2";

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = inode_get_number(i);
	m.xmlMode = 0;
	m.attrname = assert_strdup(attrname);
	m.value = assert_strdup(value);
	m.size = strlen(value);

	TEST_ASSERT_NOERR("xattr_modify",
	    db_journal_apply_ops.xattr_modify(0, &m));

	TEST_ASSERT_B("inode_xattr_has_attr",
	    inode_xattr_has_attr(i, 0 /*xmlMode*/, attrname));
}

static void
t_apply_xattr_modify_cached(void)
{
	struct inode *i;
	struct db_xattr_arg m;
	const char *attrname = GFARM_EA_NCOPY;
	const char *value = "3";
	void *value1;
	size_t value_len;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = inode_get_number(i);
	m.xmlMode = 0;
	m.attrname = assert_strdup(attrname);
	m.value = assert_strdup(value);
	m.size = strlen(value);

	TEST_ASSERT_NOERR("xattr_modify",
	    db_journal_apply_ops.xattr_modify(0, &m));

	TEST_ASSERT_NOERR("inode_xattr_get_cache",
	    inode_xattr_get_cache(i, 0 /*xmlMode*/,
		attrname, (void **)&value1, &value_len));

	TEST_ASSERT_L("value_len",
	    strlen(value), value_len);
	TEST_ASSERT_B("value",
	    strncmp(value, value1, value_len) == 0);
}

static void
t_apply_xattr_remove(void)
{
	struct inode *i;
	struct db_xattr_arg m;
	const char *attrname = "attrname";

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = inode_get_number(i);
	m.xmlMode = 0;
	m.attrname = assert_strdup(attrname);

	TEST_ASSERT_NOERR("xattr_remove",
	    db_journal_apply_ops.xattr_remove(0, &m));
	TEST_ASSERT_B("inode_xattr_has_attr",
	    inode_xattr_has_attr(i, 0 /*xmlMode*/, attrname) == 0);
}

static void
t_apply_xattr_remove_cached(void)
{
	struct inode *i;
	struct db_xattr_arg m;
	const char *attrname = GFARM_EA_NCOPY;
	void *value;
	size_t value_len;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = inode_get_number(i);
	m.xmlMode = 0;
	m.attrname = assert_strdup(attrname);

	TEST_ASSERT_NOERR("xattr_remove",
	    db_journal_apply_ops.xattr_remove(0, &m));
	TEST_ASSERT_E("inode_xattr_get_cache",
	    GFARM_ERR_NO_SUCH_OBJECT,
	    inode_xattr_get_cache(i, 0 /*xmlMode*/,
		attrname, (void **)&value, &value_len));
	TEST_ASSERT_B("inode_xattr_has_attr",
	    inode_xattr_has_attr(i, 0 /*xmlMode*/, attrname) == 0);
}

static void
t_apply_xattr_removeall(void)
{
	struct inode *i;
	struct db_xattr_arg m;
	const char *attrname1 = "attrname";
	const char *attrname2 = GFARM_EA_NCOPY;
	const char *value1 = "value";
	const char *value2 = "2";
	void *value;
	size_t value_len;

	TEST_ASSERT_B("inode_lookup",
	    (i = inode_lookup(T_APPLY_INODE_FILE_INUM)) != NULL);
	m.inum = inode_get_number(i);
	m.xmlMode = 0;
	m.attrname = assert_strdup(attrname1);
	m.value = assert_strdup(value1);
	m.size = strlen(value1);
	TEST_ASSERT_NOERR("xattr_add",
	    db_journal_apply_ops.xattr_add(0, &m));
	m.inum = inode_get_number(i);
	m.xmlMode = 0;
	m.attrname = assert_strdup(attrname2);
	m.value = assert_strdup(value2);
	m.size = strlen(value2);
	TEST_ASSERT_NOERR("xattr_removeall",
	    db_journal_apply_ops.xattr_removeall(0, &m));

	TEST_ASSERT_B("inode_xattr_has_attr attrname1",
	    inode_xattr_has_attr(i, 0 /*xmlMode*/, attrname1) == 0);
	TEST_ASSERT_B("inode_xattr_has_attr attrname2",
	    inode_xattr_has_attr(i, 0 /*xmlMode*/, attrname2) == 0);
	TEST_ASSERT_E("inode_xattr_get_cache",
	    GFARM_ERR_NO_SUCH_OBJECT,
	    inode_xattr_get_cache(i, 0 /*xmlMode*/,
		attrname2, (void **)&value, &value_len));
}

#define T_APPLY_QUOTA_USER_NAME "quota_user"
#define T_APPLY_QUOTA_GROUP_NAME "quota_group"

static void
t_apply_quota_add(void)
{
	struct db_quota_arg m;
	struct user *u;
	struct group *g;
	struct quota *q, *qu, *qg;

	u = t_add_test_user(T_APPLY_QUOTA_USER_NAME);
	g = t_add_test_group(T_APPLY_QUOTA_GROUP_NAME);
	memset(&m, 0, sizeof(m));
	m.name = assert_strdup(user_name(u));
	m.is_group = 0;
	q = &m.quota;
	q->on_db = 1;
	q->grace_period = 111;
	q->space = 100;
	q->space_exceed = 222;
	q->space_soft = 101;
	q->space_hard = 102;
	q->num = 103;
	q->num_exceed = 333;
	q->num_soft = 104;
	q->num_hard = 105;
	q->phy_space = 106;
	q->phy_space_exceed = 444;
	q->phy_space_soft = 107;
	q->phy_space_hard = 108;
	q->phy_num = 109;
	q->phy_num_exceed = 555;
	q->phy_num_soft = 110;
	q->phy_num_hard = 111;

	TEST_ASSERT_NOERR("quota_add",
	    db_journal_apply_ops.quota_add(0, &m));
	TEST_ASSERT_B("user_quota",
	    (qu = user_quota(u)) != NULL);
	TEST_ASSERT_I("on_db",
	    1, qu->on_db);
	TEST_ASSERT_L("grace_period",
	    111, qu->grace_period);
	TEST_ASSERT_L("space",
	    100, qu->space);
	TEST_ASSERT_L("space_exceed",
	    222, qu->space_exceed);
	TEST_ASSERT_L("space_soft",
	    101, qu->space_soft);
	TEST_ASSERT_L("space_hard",
	    102, qu->space_hard);
	TEST_ASSERT_L("num",
	    103, qu->num);
	TEST_ASSERT_L("num_exceed",
	    333, qu->num_exceed);
	TEST_ASSERT_L("num_soft",
	    104, qu->num_soft);
	TEST_ASSERT_L("num_hard",
	    105, qu->num_hard);
	TEST_ASSERT_L("phy_space",
	    106, qu->phy_space);
	TEST_ASSERT_L("phy_space_exceed",
	    444, qu->phy_space_exceed);
	TEST_ASSERT_L("phy_space_soft",
	    107, qu->phy_space_soft);
	TEST_ASSERT_L("phy_space_hard",
	    108, qu->phy_space_hard);
	TEST_ASSERT_L("phy_num",
	    109, qu->phy_num);
	TEST_ASSERT_L("phy_num_exceed",
	    555, qu->phy_num_exceed);
	TEST_ASSERT_L("phy_num_soft",
	    110, qu->phy_num_soft);
	TEST_ASSERT_L("phy_num_hard",
	    111, qu->phy_num_hard);

	m.name = assert_strdup(group_name(g));
	m.is_group = 1;

	TEST_ASSERT_NOERR("quota_add",
	    db_journal_apply_ops.quota_add(0, &m));
	TEST_ASSERT_B("group_quota",
	    (qg = group_quota(g)) != NULL);
	TEST_ASSERT_I("on_db",
	    1, qg->on_db);
	TEST_ASSERT_L("grace_period",
	    111, qg->grace_period);
}

static void
t_apply_quota_modify(void)
{
	struct db_quota_arg m;
	struct user *u;
	struct group *g;
	struct quota *q, *qu, *qg;

	TEST_ASSERT_B("user_lookup",
	    (u = user_lookup(T_APPLY_QUOTA_USER_NAME)) != NULL);
	TEST_ASSERT_B("user_quota",
	    (qu = user_quota(u)) != NULL);
	TEST_ASSERT_I("on_db",
	    1, qu->on_db);

	TEST_ASSERT_B("group_lookup",
	    (g = group_lookup(T_APPLY_QUOTA_GROUP_NAME)) != NULL);
	TEST_ASSERT_B("group_quota",
	    (qg = group_quota(g)) != NULL);
	TEST_ASSERT_I("on_db",
	    1, qg->on_db);

	memset(&m, 0, sizeof(m));
	m.name = assert_strdup(user_name(u));
	m.is_group = 0;
	q = &m.quota;
	q->on_db = 1;
	q->grace_period = 1111;
	q->space = 1100;
	q->space_exceed = 1222;
	q->space_soft = 1101;
	q->space_hard = 1102;
	q->num = 1103;
	q->num_exceed = 1333;
	q->num_soft = 1104;
	q->num_hard = 1105;
	q->phy_space = 1106;
	q->phy_space_exceed = 1444;
	q->phy_space_soft = 1107;
	q->phy_space_hard = 1108;
	q->phy_num = 1109;
	q->phy_num_exceed = 1555;
	q->phy_num_soft = 1110;
	q->phy_num_hard = 1111;

	TEST_ASSERT_NOERR("quota_modify",
	    db_journal_apply_ops.quota_modify(0, &m));
	TEST_ASSERT_I("on_db",
	    1, qu->on_db);
	TEST_ASSERT_L("grace_period",
	    1111, qu->grace_period);
	TEST_ASSERT_L("space",
	    1100, qu->space);
	TEST_ASSERT_L("space_exceed",
	    1222, qu->space_exceed);
	TEST_ASSERT_L("space_soft",
	    1101, qu->space_soft);
	TEST_ASSERT_L("space_hard",
	    1102, qu->space_hard);
	TEST_ASSERT_L("num",
	    1103, qu->num);
	TEST_ASSERT_L("num_exceed",
	    1333, qu->num_exceed);
	TEST_ASSERT_L("num_soft",
	    1104, qu->num_soft);
	TEST_ASSERT_L("num_hard",
	    1105, qu->num_hard);
	TEST_ASSERT_L("phy_space",
	    1106, qu->phy_space);
	TEST_ASSERT_L("phy_space_exceed",
	    1444, qu->phy_space_exceed);
	TEST_ASSERT_L("phy_space_soft",
	    1107, qu->phy_space_soft);
	TEST_ASSERT_L("phy_space_hard",
	    1108, qu->phy_space_hard);
	TEST_ASSERT_L("phy_num",
	    1109, qu->phy_num);
	TEST_ASSERT_L("phy_num_exceed",
	    1555, qu->phy_num_exceed);
	TEST_ASSERT_L("phy_num_soft",
	    1110, qu->phy_num_soft);
	TEST_ASSERT_L("phy_num_hard",
	    1111, qu->phy_num_hard);

	m.name = assert_strdup(group_name(g));
	m.is_group = 1;

	TEST_ASSERT_NOERR("quota_modify",
	    db_journal_apply_ops.quota_modify(0, &m));
	TEST_ASSERT_B("group_quota",
	    (qg = group_quota(g)) != NULL);
	TEST_ASSERT_I("on_db",
	    1, qg->on_db);
	TEST_ASSERT_L("grace_period",
	    1111, qg->grace_period);
}

static void
t_apply_quota_remove(void)
{
	struct db_quota_remove_arg m;
	struct user *u;
	struct group *g;
	struct quota *qu, *qg;

	TEST_ASSERT_B("user_lookup",
	    (u = user_lookup(T_APPLY_QUOTA_USER_NAME)) != NULL);
	TEST_ASSERT_B("user_quota",
	    (qu = user_quota(u)) != NULL);
	TEST_ASSERT_I("on_db",
	    1, qu->on_db);

	TEST_ASSERT_B("group_lookup",
	    (g = group_lookup(T_APPLY_QUOTA_GROUP_NAME)) != NULL);
	TEST_ASSERT_B("group_quota",
	    (qg = group_quota(g)) != NULL);
	TEST_ASSERT_I("on_db",
	    1, qg->on_db);

	memset(&m, 0, sizeof(m));
	m.name = assert_strdup(user_name(u));
	m.is_group = 0;

	TEST_ASSERT_NOERR("quota_remove",
	    db_journal_apply_ops.quota_remove(0, &m));
	TEST_ASSERT_I("on_db",
	    0, qu->on_db);

	m.name = assert_strdup(group_name(g));
	m.is_group = 1;

	TEST_ASSERT_NOERR("quota_remove",
	    db_journal_apply_ops.quota_remove(0, &m));
	TEST_ASSERT_I("on_db",
	    0, qg->on_db);
}

#define T_APPLY_MDHOST_NAME "mdhost1"

static void
t_apply_mdhost_add(void)
{
	const char *name = T_APPLY_MDHOST_NAME;
	struct gfarm_metadb_server ms;
	struct mdhost *h;

	ms.name = assert_strdup(name);
	ms.clustername = assert_strdup("clustername");
	ms.port = 1234;
	ms.flags = 2;

	TEST_ASSERT_B("mdhost_lookup",
	    (h = mdhost_lookup(name)) == NULL);
	TEST_ASSERT_NOERR("mdhost_add",
	    db_journal_apply_ops.mdhost_add(0, &ms));
	gfarm_metadb_server_free(&ms);
	TEST_ASSERT_B("mdhost_lookup",
	    (h = mdhost_lookup(name)) != NULL);
	TEST_ASSERT_S("name",
	    name, mdhost_get_name(h));
	TEST_ASSERT_I("port",
	    1234, mdhost_get_port(h));
	TEST_ASSERT_S("clustername",
	    "clustername", mdhost_get_cluster_name(h));
	TEST_ASSERT_I("flags",
	    2, mdhost_get_flags(h));
}

static void
t_apply_mdhost_modify(void)
{
	const char *name = T_APPLY_MDHOST_NAME;
	struct db_mdhost_modify_arg m;
	struct mdhost *mh;

	TEST_ASSERT_B("mdhost_lookup",
	    (mh = mdhost_lookup(name)) != NULL);

	memset(&m.ms, 0, sizeof(m.ms));
	m.ms.name = assert_strdup(name);
	m.ms.clustername = assert_strdup("clustername");
	m.ms.port = 5678;
	m.ms.flags = 5;
	m.modflags = 0; /* modflags is not used yet */

	TEST_ASSERT_NOERR("mdhost_modify",
	    db_journal_apply_ops.mdhost_modify(0, &m));
	gfarm_metadb_server_free(&m.ms);
	TEST_ASSERT_I("port",
	    5678, mdhost_get_port(mh));
	TEST_ASSERT_S("clustername",
	    "clustername", mdhost_get_cluster_name(mh));
	TEST_ASSERT_I("flags",
	    5, mdhost_get_flags(mh));
}

static void
t_apply_mdhost_remove(void)
{
	const char *name = T_APPLY_MDHOST_NAME;
	struct mdhost *h;

	TEST_ASSERT_B("mdhost_lookup",
	    (h = mdhost_lookup(name)) != NULL);
	TEST_ASSERT_NOERR("mdhost_remove",
	    db_journal_apply_ops.mdhost_remove(0, (char *)name));
	TEST_ASSERT_B("mdhost_lookup",
	    (h = mdhost_lookup(name)) == NULL);
}



struct t_apply_info {
	const char *name;
	void (*test)(void);
};

struct t_apply_info apply_tests[] = {
	{ "t_apply_host_add", t_apply_host_add },
	{ "t_apply_host_modify", t_apply_host_modify },
	{ "t_apply_host_remove", t_apply_host_remove },
	{ "t_apply_user_add", t_apply_user_add },
	{ "t_apply_user_modify", t_apply_user_modify },
	{ "t_apply_user_remove", t_apply_user_remove },
	{ "t_apply_group_add", t_apply_group_add },
	{ "t_apply_group_modify", t_apply_group_modify },
	{ "t_apply_group_remove", t_apply_group_remove },
	{ "t_apply_inode_add", t_apply_inode_add },
	{ "t_apply_inode_modify", t_apply_inode_modify },
	{ "t_apply_inode_gen_modify", t_apply_inode_gen_modify },
	{ "t_apply_inode_nlink_modify", t_apply_inode_nlink_modify },
	{ "t_apply_inode_size_modify", t_apply_inode_size_modify },
	{ "t_apply_inode_mode_modify", t_apply_inode_mode_modify },
	{ "t_apply_inode_user_modify", t_apply_inode_user_modify },
	{ "t_apply_inode_group_modify", t_apply_inode_group_modify },
	{ "t_apply_inode_atime_modify", t_apply_inode_atime_modify },
	{ "t_apply_inode_mtime_modify", t_apply_inode_mtime_modify },
	{ "t_apply_inode_ctime_modify", t_apply_inode_ctime_modify },
	/* XXX - NOT IMPLEMENTED
	{ "t_apply_inode_cksum_add", t_apply_inode_cksum_add },
	{ "t_apply_inode_cksum_modify", t_apply_inode_cksum_modify },
	{ "t_apply_inode_cksum_remove", t_apply_inode_cksum_remove },
	*/
	{ "t_apply_filecopy_add", t_apply_filecopy_add },
	{ "t_apply_filecopy_remove", t_apply_filecopy_remove },
	{ "t_apply_deadfilecopy_add", t_apply_deadfilecopy_add },
	{ "t_apply_deadfilecopy_remove", t_apply_deadfilecopy_remove },
	{ "t_apply_direntry_add", t_apply_direntry_add },
	{ "t_apply_direntry_remove", t_apply_direntry_remove },
	{ "t_apply_symlink_add", t_apply_symlink_add },
	{ "t_apply_symlink_remove", t_apply_symlink_remove },
	{ "t_apply_xattr_add", t_apply_xattr_add },
	{ "t_apply_xattr_add_cached", t_apply_xattr_add_cached },
	{ "t_apply_xattr_modify", t_apply_xattr_modify },
	{ "t_apply_xattr_modify_cached", t_apply_xattr_modify_cached },
	{ "t_apply_xattr_remove", t_apply_xattr_remove },
	{ "t_apply_xattr_remove_cached", t_apply_xattr_remove_cached },
	{ "t_apply_xattr_removeall", t_apply_xattr_removeall },
	{ "t_apply_quota_add", t_apply_quota_add },
	{ "t_apply_quota_modify", t_apply_quota_modify },
	{ "t_apply_quota_remove", t_apply_quota_remove },
	{ "t_apply_mdhost_add", t_apply_mdhost_add },
	{ "t_apply_mdhost_modify", t_apply_mdhost_modify },
	{ "t_apply_mdhost_remove", t_apply_mdhost_remove },
};

static gfarm_error_t
t_no_sync(gfarm_uint64_t seqnum)
{
	return (GFARM_ERR_NO_ERROR);
}

void
t_apply(void)
{
	struct journal_file_reader *reader;
	struct journal_file_writer *writer;
	off_t wpos1, wpos2;
	char msg[BUFSIZ];
	int i;

	giant_init();
	config_var_init();

	unlink_test_file(filepath);
	TEST_ASSERT_NOERR("journal_file_open",
	    journal_file_open(filepath, TEST_FILE_MAX_SIZE, 0,
		&self_jf, GFARM_JOURNAL_RDWR));
	reader = journal_file_main_reader(self_jf);
	journal_file_reader_disable_block_writer(reader);
	writer = journal_file_writer(self_jf);

	/* the database will not be changed and
	   only journal will be written to the temporary file. */

	journal_seqnum = 1;
	db_journal_apply_init();
	db_journal_init_status();
	db_journal_set_sync_op(t_no_sync);
	gfarm_set_metadb_replication_enabled(0);
	db_use(&empty_ops);
	gfarm_set_metadb_replication_enabled(1);

	db_use(&empty_ops);
	mdhost_init();
	host_init();
	user_init();
	group_init();
	inode_init();
	dir_entry_init();
	file_copy_init();
	symlink_init();
	xattr_init();
	quota_init();

	/* to call dead_file_copy_init() is unnecessary */

	/* after intiated, the journal must not be changed. */

	for (i = 0; i < GFARM_ARRAY_LENGTH(apply_tests); ++i) {
		struct t_apply_info *ai = &apply_tests[i];

		wpos1 = journal_file_writer_pos(writer);
		ai->test();
		wpos2 = journal_file_writer_pos(writer);
		sprintf(msg, "%s:wpos", ai->name);
		TEST_ASSERT_L(msg, wpos1, wpos2);
	}
	journal_file_close(self_jf);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, op = 0;

	/* XXX: settings in gfmd.conf doesn't work in this case */
	char *config  = getenv("GFARM_CONFIG_FILE");

	debug_mode = 1;
	e = gfarm_server_initialize(config, &argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_server_initialize: %s\n",
		    argv[0], gfarm_error_string(e));
		fprintf(stderr, "%s: aborting\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	gflog_set_priority_level(LOG_DEBUG);
	gflog_set_message_verbose(99);

	while ((c = getopt(argc, argv, GETOPT_ARG)) != -1) {
		switch (c) {
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
			    program_name, c);
			/* no break */
		case '?':
			usage();
			break;
		case 'a':
		case 'p':
		case 'o':
		case 'w':
			op = c;
			break;
		}
	}
	if (optind == 1)
		usage();
	argc -= optind;
	argv += optind;
	if (argc == 0)
		usage();
	filepath = argv[0];

	switch (op) {
	case 'a':
		t_apply();
		break;
	case 'o':
		t_open();
		break;
	case 'w':
		t_write();
		break;
	case 'p':
		t_ops();
		break;
	default:
		break;
	}

	printf("ok\n");
	return (EXIT_SUCCESS);
}
