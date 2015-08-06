/*
 * $Id$
 */

/*
 * << Journal File Header Format >>
 *
 *           |MAGIC(4)|VERSION(4)|0000...(4088)|
 *   offset  0        4          8          4096
 *
 * << Journal Record Format >>
 *
 *           |MAGIC(4)|SEQNUM(8)|OPE_ID(4)|DATA_LENGTH(4)=n|
 *   offset  0        4        12        16               20
 *
 *           |OBJECT(n)|CRC32(4)|
 *   offset 20        20+n    24+n
 */

#include <gfarm/gfarm_config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include <assert.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gflog.h>

#include "gfutil.h"
#include "queue.h"
#include "gflog_reduced.h"

#include "crc32.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "auth.h"
#include "gfm_proto.h"

#include "subr.h"
#include "thrsubr.h"
#include "journal_file.h"


struct journal_file;
struct journal_file_writer;

struct journal_file_reader {
	GFARM_HCIRCLEQ_ENTRY(journal_file_reader) readers;
	struct journal_file *file;
	off_t committed_pos;
	gfarm_uint64_t committed_lap;
	off_t fd_pos;
	gfarm_uint64_t fd_lap;
	size_t uncommitted_len;
	struct gfp_xdr *xdr;
	int flags;
	gfarm_uint64_t last_seqnum;
	const char *label;
};

static const char main_reader_label[] = "main_reader";

#define JOURNAL_FILE_READER_F_BLOCK_WRITER	0x1
#define JOURNAL_FILE_READER_F_INVALID		0x2
#define JOURNAL_FILE_READER_F_DRAIN		0x4
#define JOURNAL_FILE_READER_F_EXPIRED		0x8
#define JOURNAL_FILE_READER_IS_BLOCK_WRITER(r) \
	(((r)->flags & JOURNAL_FILE_READER_F_BLOCK_WRITER) != 0)
#define JOURNAL_FILE_READER_IS_INVALID(r) \
	(((r)->flags & JOURNAL_FILE_READER_F_INVALID) != 0)
#define JOURNAL_FILE_READER_DRAINED(r) \
	(((r)->flags & JOURNAL_FILE_READER_F_DRAIN) != 0)
#define JOURNAL_FILE_READER_IS_EXPIRED(r) \
	(((r)->flags & JOURNAL_FILE_READER_F_EXPIRED) != 0)

#define JOURNAL_RECORD_SIZE_MAX			(1024 * 1024)
#define JOURNAL_RECORD_MAGIC_SIZE		GFARM_JOURNAL_MAGIC_SIZE
#define JOURNAL_RECORD_SEQNUM_SIZE		8
#define JOURNAL_RECORD_OPEID_SIZE		4
#define JOURNAL_RECORD_DATALEN_SIZE		4
#define JOURNAL_RECORD_CRC_SIZE			4

#define JOURNAL_RECORD_MAGIC_OFFSET		0
#define JOURNAL_RECORD_SEQNUM_OFFSET		4
#define JOURNAL_RECORD_OPEID_OFFSET		12
#define JOURNAL_RECORD_DATALEN_OFFSET		16

#define JOURNAL_RECORD_HEADER_SIZE		20
#define JOURNAL_RECORD_SIZE(datalen) \
	(JOURNAL_RECORD_HEADER_SIZE + 4 + (datalen))

/* JOURNAL_FILE_HEADER_SIZE + maximum transaction size */
/* XXX - this assumes 128, but it is too small */
#define JOURNAL_FILE_MIN_SIZE			(JOURNAL_FILE_HEADER_SIZE + 128)
#define GFARM_JOURNAL_RECORD_HEADER_XDR_FMT	"cccclii"
#define JOURNAL_BUFSIZE				8192
#define JOURNAL_READ_AHEAD_SIZE			8192

#define JOURNAL_INITIAL_WLAP			1

struct journal_file_writer {
	struct journal_file *file;
	off_t pos;
	gfarm_uint64_t lap;
	struct gfp_xdr *xdr;
};

struct journal_file {
	GFARM_HCIRCLEQ_HEAD(journal_file_reader) reader_list;
	struct journal_file_writer writer;
	char *path;
	size_t size, max_size;
	off_t tail;
	int wait_until_nonempty, wait_until_nonfull;
	pthread_cond_t nonfull_cond, nonempty_cond, drain_cond;
	gfarm_uint64_t initial_max_seqnum;
	pthread_mutex_t mutex;
};

static const char JOURNAL_FILE_STR[] = "journal_file";

static const char *journal_operation_names[] = {
	NULL,

	"BEGIN",
	"END",

	"HOST_ADD",
	"HOST_MODIFY",
	"HOST_REMOVE",

	"USER_ADD",
	"USER_MODIFY",
	"USER_REMOVE",

	"GROUP_ADD",
	"GROUP_MODIFY",
	"GROUP_REMOVE",

	"INODE_ADD",
	"INODE_MODIFY",
	"INODE_GEN_MODIFY",
	"INODE_NLINK_MODIFY",
	"INODE_SIZE_MODIFY",
	"INODE_MODE_MODIFY",
	"INODE_USER_MODIFY",
	"INODE_GROUP_MODIFY",
	"INODE_ATIME_MODIFY",
	"INODE_MTIME_MODIFY",
	"INODE_CTIME_MODIFY",

	"INODE_CKSUM_ADD",
	"INODE_CKSUM_MODIFY",
	"INODE_CKSUM_REMOVE",

	"FILECOPY_ADD",
	"FILECOPY_REMOVE",

	"DEADFILECOPY_ADD",
	"DEADFILECOPY_REMOVE",

	"DIRENTRY_ADD",
	"DIRENTRY_REMOVE",

	"SYMLINK_ADD",
	"SYMLINK_REMOVE",

	"XATTR_ADD",
	"XATTR_MODIFY",
	"XATTR_REMOVE",
	"XATTR_REMOVEALL",

	"QUOTA_ADD",
	"QUOTA_MODIFY",
	"QUOTA_REMOVE",

	"MDHOST_ADD",
	"MDHOST_MODIFY",
	"MDHOST_REMOVE",

	"FSNGROUP_MODIFY",
	"NOP",
};

struct journal_file_writer *
journal_file_writer(struct journal_file *jf)
{
	return (&jf->writer);
}

off_t
journal_file_tail(struct journal_file *jf)
{
	return (jf->tail);
}

/* Called by gfjournal only */
off_t
journal_file_size(struct journal_file *jf)
{
	return (jf->size);
}

void
journal_file_mutex_lock(struct journal_file *jf, const char *diag)
{
	gfarm_mutex_lock(&jf->mutex, diag, JOURNAL_FILE_STR);
}

void
journal_file_mutex_unlock(struct journal_file *jf, const char *diag)
{
	gfarm_mutex_unlock(&jf->mutex, diag, JOURNAL_FILE_STR);
}

int
journal_file_is_waiting_until_nonempty(struct journal_file *jf)
{
	int r;
	static const char diag[] = "journal_file_is_waiting_until_nonempty";

	journal_file_mutex_lock(jf, diag);
	r = jf->wait_until_nonempty;
	journal_file_mutex_unlock(jf, diag);
	return (r);
}

struct journal_file_reader *
journal_file_main_reader(struct journal_file *jf)
{
	return (GFARM_HCIRCLEQ_FIRST(jf->reader_list, readers));
}

/* Called by gfjournal only */
gfarm_uint64_t
journal_file_get_inital_max_seqnum(struct journal_file *jf)
{
	return (jf->initial_max_seqnum);
}

static int
journal_file_has_writer(struct journal_file *jf)
{
	return (jf->writer.xdr != NULL);
}

static void
journal_file_reader_set_flag(struct journal_file_reader *reader, int f,
	int val)
{
	if (val)
		reader->flags |= f;
	else
		reader->flags &= ~f;
}

void
journal_file_reader_disable_block_writer(struct journal_file_reader *reader)
{
	journal_file_reader_set_flag(reader,
	    JOURNAL_FILE_READER_F_BLOCK_WRITER, 0);
}

/* PREREQUISITE: journal_file_mutex. */
void
journal_file_reader_invalidate_unlocked(struct journal_file_reader *reader)
{
	journal_file_reader_set_flag(reader, JOURNAL_FILE_READER_F_DRAIN, 1);
	journal_file_reader_set_flag(reader, JOURNAL_FILE_READER_F_INVALID, 1);
	if (reader->xdr != NULL) {
		gfp_xdr_free(reader->xdr);
		reader->xdr = NULL;
	}
}

void
journal_file_reader_invalidate(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	static const char diag[] = "journal_file_reader_invalidate";

	journal_file_mutex_lock(jf, diag);
	journal_file_reader_invalidate_unlocked(reader);
	journal_file_mutex_unlock(jf, diag);
}

/* PREREQUISITE: journal_file_mutex. */
static void
journal_file_reader_close_unlocked(struct journal_file_reader *reader)
{
	assert(reader != NULL);
	journal_file_reader_invalidate_unlocked(reader);
	GFARM_HCIRCLEQ_REMOVE(reader, readers);
	free(reader);
}

void
journal_file_reader_close(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	static const char diag[] = "journal_file_reader_close";

	journal_file_mutex_lock(jf, diag);
	journal_file_reader_close_unlocked(reader);
	journal_file_mutex_unlock(jf, diag);
}

struct gfp_xdr *
journal_file_reader_xdr(struct journal_file_reader *reader)
{
	return (reader->xdr);
}

void
journal_file_reader_committed_pos(struct journal_file_reader *reader,
    off_t *rposp, gfarm_uint64_t *rlapp)
{
	struct journal_file *jf = reader->file;
	static const char diag[] = "journal_file_reader_committed_pos";

	journal_file_mutex_lock(jf, diag);
	journal_file_reader_committed_pos_unlocked(reader, rposp, rlapp);
	journal_file_mutex_unlock(jf, diag);
}

void
journal_file_reader_committed_pos_unlocked(struct journal_file_reader *reader,
    off_t *rposp, gfarm_uint64_t *rlapp)
{
	*rposp = reader->committed_pos;
	*rlapp = reader->committed_lap;
}

/* Called by gfjournal only */
off_t
journal_file_reader_fd_pos(struct journal_file_reader *reader)
{
	return (reader->fd_pos);
}

static int
journal_file_reader_is_empty(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	struct journal_file_writer *writer = &jf->writer;

	return (reader->committed_pos == writer->pos &&
	    reader->committed_lap == writer->lap);
}

void
journal_file_nonfull_cond_signal(struct journal_file_reader *reader,
	const char *diag)
{
	struct journal_file *jf = reader->file;

	gfarm_cond_signal(&jf->nonfull_cond, diag, JOURNAL_FILE_STR);
}

/* PREREQUISITE: journal_file_mutex. */
void
journal_file_reader_commit_pos(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	static const char diag[] = "journal_file_reader_commit_pos";
	off_t pos;

	pos = reader->committed_pos + reader->uncommitted_len;
	if (pos < jf->tail)
		reader->committed_pos = pos;
	else if (pos == jf->tail && reader->committed_lap == jf->writer.lap)
		reader->committed_pos = pos;
	else {
		reader->committed_pos = 
		    pos - jf->tail + JOURNAL_FILE_HEADER_SIZE;
		reader->committed_lap++;
	}
	reader->uncommitted_len = 0;
	journal_file_nonfull_cond_signal(reader, diag);
}

static int
journal_file_reader_is_invalid(struct journal_file_reader *reader)
{
	return (JOURNAL_FILE_READER_IS_INVALID(reader));
}

static int
journal_file_reader_is_expired_unlocked(struct journal_file_reader *reader)
{
	return (JOURNAL_FILE_READER_IS_EXPIRED(reader));
}

int
journal_file_reader_is_expired(struct journal_file_reader *reader)
{
	int r;
	struct journal_file *jf = reader->file;
	static const char diag[] = "journal_file_reader_is_expired";

	journal_file_mutex_lock(jf, diag);
	r = journal_file_reader_is_expired_unlocked(reader);
	journal_file_mutex_unlock(jf, diag);

	return (r);
}

static void
journal_file_reader_expire(struct journal_file_reader *reader)
{
	journal_file_reader_set_flag(reader,
	    JOURNAL_FILE_READER_F_EXPIRED, 1);
}

static void
journal_file_writer_set(struct journal_file *jf, struct gfp_xdr *xdr,
	off_t pos, gfarm_uint64_t lap, struct journal_file_writer *writer)
{
	writer->file = jf;
	writer->xdr = xdr;
	writer->pos = pos;
	writer->lap = lap;
}

struct gfp_xdr *
journal_file_writer_xdr(struct journal_file_writer *writer)
{
	return (writer->xdr);
}

off_t
journal_file_writer_pos(struct journal_file_writer *writer)
{
	return (writer->pos);
}

static int
jounal_read_fully(int fd, void *buf, size_t sz, off_t *posp, int *eofp)
{
	size_t rsz = sz;
	ssize_t ssz;

	*eofp = 0;
	while (rsz > 0) {
		if ((ssz = read(fd, buf, rsz)) < 0)
			return (ssz);
		if (ssz == 0) {
			*eofp = 1;
			sz = sz - rsz;
			break;
		}
		buf += ssz;
		rsz -= ssz;
		if (posp)
			*posp += ssz;
	}
	return (sz);
}

static int
jounal_fread_fully(FILE *file, void *buf, size_t sz, off_t *posp, int *eofp)
{
	size_t rsz = sz;
	size_t ssz;

	*eofp = 0;
	while (rsz > 0) {
		if ((ssz = fread(buf, 1, rsz, file)) == 0) {
			if (ferror(file))
				return (-1);
			*eofp = 1;
			sz = sz - rsz;
			break;
		}
		buf += ssz;
		rsz -= ssz;
		if (posp)
			*posp += ssz;
	}
	return (sz);
}

static int
journal_fread_uint32(FILE *file, gfarm_uint32_t *np, void *data, off_t *posp)
{
	ssize_t ssz;
	int eof;

	if ((ssz = jounal_fread_fully(file, np, sizeof(*np), posp,
	    &eof)) < 0)
		return (ssz);
	if (eof)
		return (0);
	if (data)
		memcpy(data, np, sizeof(*np));
	*np = ntohl(*np);
	return (sizeof(*np));
}

static gfarm_uint32_t
journal_deserialize_uint32(unsigned char *d)
{
	return (d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3]);
}

static gfarm_uint64_t
journal_deserialize_uint64(unsigned char *d)
{
	gfarm_uint32_t n1, n2;
	n1 = journal_deserialize_uint32(d);
	n2 = journal_deserialize_uint32(d + 4);
	return ((((gfarm_uint64_t)n1) << 32) | n2);
}

static gfarm_error_t
journal_write_zeros(int fd, size_t len)
{
	char buf[JOURNAL_BUFSIZE];
	size_t wlen;
	ssize_t ssz;

	memset(buf, 0, JOURNAL_BUFSIZE);
	errno = 0;
	while (len > 0) {
		wlen = len > sizeof(buf) ? sizeof(buf) : len;
		if ((ssz = write(fd, buf, wlen)) < 0)
			return (gfarm_errno_to_error(errno));
		len -= ssz;
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_file_writer_fill(struct journal_file_writer *writer)
{
	ssize_t len;
	struct journal_file *jf = writer->file;

	gfp_xdr_purge_all(writer->xdr);
	len = jf->max_size - writer->pos;
	assert(len >= 0);
	return (journal_write_zeros(gfp_xdr_fd(writer->xdr), len));
}

static int
journal_write_fully(int fd, void *data, size_t length, off_t *posp)
{
	size_t rlen = length;
	ssize_t ssz;

	errno = 0;
	while (rlen > 0) {
		if ((ssz = write(fd, data, rlen)) < 0)
			return (ssz);
		if (ssz == 0)
			return (length - rlen);
		rlen -= ssz;
		data += ssz;
		*posp += ssz;
	}
	return (length);
}

static gfarm_error_t
journal_fread_rec_outline(FILE *file, size_t file_size,
	gfarm_uint64_t *seqnump, enum journal_operation *opeidp, off_t *posp,
	off_t *next_posp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_uint32_t crc1, crc2;
	off_t pos, pos_saved;
	int eof;
	gfarm_uint64_t seqnum = 0;
	gfarm_uint32_t opeid;
	gfarm_uint32_t datalen;
	unsigned char *buf = NULL;
	size_t buflen;
	int backward = 0;

	*posp = *next_posp = -1;
	errno = 0;

	if ((pos = ftell(file)) < 0)
		return (gfarm_errno_to_error(errno));
	pos_saved = pos;

	buf = malloc(JOURNAL_BUFSIZE);
	if (buf == NULL)
		return (GFARM_ERR_NO_MEMORY);
	buflen = JOURNAL_BUFSIZE;

	for (;;) {
		if (backward) {
			pos = pos_saved + 1;
			if (fseek(file, pos, SEEK_SET) < 0) {
				e = gfarm_errno_to_error(errno);
				break;
			}
			backward = 0;
		}
		pos_saved = pos;

		/* MAGIC(4) */
		if (jounal_fread_fully(file, buf, JOURNAL_RECORD_HEADER_SIZE,
			&pos, &eof) < 0) {
			e = gfarm_errno_to_error(errno);
			break;
		}
		if (eof)
			break;
		if (memcmp(buf, GFARM_JOURNAL_RECORD_MAGIC,
			JOURNAL_RECORD_MAGIC_SIZE) != 0) {
			backward = 1;
			continue;
		}
		/* SEQNUM */
		seqnum = journal_deserialize_uint64(buf +
		    JOURNAL_RECORD_SEQNUM_OFFSET);
		/* OPE_ID */
		opeid = journal_deserialize_uint32(buf +
		    JOURNAL_RECORD_OPEID_OFFSET);
		/* DATA_LENGTH */
		datalen = journal_deserialize_uint32(buf +
		    JOURNAL_RECORD_DATALEN_OFFSET);
		/* DATA */
		if (pos + datalen + JOURNAL_RECORD_CRC_SIZE > file_size ||
		    datalen > JOURNAL_RECORD_SIZE_MAX) {
			backward = 1;
			continue;
		}
		crc1 = gfarm_crc32(0, buf, JOURNAL_RECORD_HEADER_SIZE);
		if (datalen + JOURNAL_RECORD_CRC_SIZE > buflen) {
			unsigned char *newbuf = realloc(buf, datalen + 4);
			if (newbuf == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				break;
			}
			buf = newbuf;
			buflen = datalen + 4;
		}
		if (jounal_fread_fully(file, buf,
			datalen + JOURNAL_RECORD_CRC_SIZE, &pos, &eof) < 0) {
			backward = 1;
			continue;
		}
		if (eof) {
			backward = 1;
			continue;
		}
		/* CRC */
		crc2 = journal_deserialize_uint32(buf + datalen);
		crc1 = gfarm_crc32(crc1, buf, datalen);
		if (crc1 != crc2) {
			backward = 1;
			continue;
		}
		*opeidp = opeid;
		*seqnump = seqnum;
		*posp = pos_saved;
		*next_posp = pos;
		break;
	}

	free(buf);
	return (e);
}

static gfarm_error_t
journal_file_reader_rewind(struct journal_file_reader *reader)
{
	gfarm_error_t e;

	errno = 0;
	reader->fd_pos = lseek(gfp_xdr_fd(reader->xdr),
		JOURNAL_FILE_HEADER_SIZE, SEEK_SET);
	if (reader->fd_pos == -1) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1002874,
		    "lseek : %s", gfarm_error_string(e));
		return (e);
	}
	reader->fd_lap++;
	gfp_xdr_recvbuffer_clear_read_eof(reader->xdr);
	return (GFARM_ERR_NO_ERROR);
}

static struct gflog_reduced_state wait_log_state =
	GFLOG_REDUCED_STATE_INITIALIZER(3, 60, 600, 60);

static int
journal_file_reader_writer_wait(struct journal_file_reader *reader,
	struct journal_file *jf, size_t rec_len)
{
	struct journal_file_writer *writer = &jf->writer;
	static const char diag[] = "journal_file_reader_writer_wait";
	off_t wpos, rpos;
	gfarm_uint64_t wlap, rlap;
	int needed, waited = 0;

	if (journal_file_reader_is_invalid(reader) ||
	    !JOURNAL_FILE_READER_IS_BLOCK_WRITER(reader) ||
	    rec_len == 0)
		return (0);

	for (;;) {
		wpos = writer->pos;
		wlap = writer->lap;
		rpos = reader->committed_pos;
		rlap = reader->committed_lap;

		/*
		 * Check if the writer must wait the reader.
		 */
		if (wpos == rpos) {
			/*
			 * Case 1: wpos == rpos
			 *   The writer must wait for the reader if the 
			 *   reader is a lap behind.
			 */
			needed = (wlap > rlap);

		} else if (wpos < rpos) {
			/*
			 * Case 2: wpos < rpos
			 *   The writer must wait for the reader if it will
			 *    pass the reader ahead.
			 *
			 *           w    r          max_size
			 *     +--+--+----+----------+
			 *     |FH|  |rec_len|
			 */
			needed = (wpos + rec_len > rpos);
		} else {
			/*
			 * Case 3: wpos > rpos
			 *   The writer must wait for the reader if it will
			 *   pass the reader ahead.
			 *
			 *             r          w  max_size
			 *     +--+----+----------+--+
			 *     |FH|rec_len|       |rec_len|
			 */
			needed = (rpos < rec_len + JOURNAL_FILE_HEADER_SIZE &&
			    wpos + rec_len > jf->max_size);
		}

		/*
		 * The writer waits the reader, if needed.
		 */
		if (!needed)
			break;

		gflog_reduced_notice(GFARM_MSG_1004257, &wait_log_state,
		    "journal write: wait until %s reads the journal file",
		    reader->label);
		if (reader->label == main_reader_label)
			jf->wait_until_nonfull = 1;
		gfarm_cond_wait(&jf->nonfull_cond, &jf->mutex,
		    diag, JOURNAL_FILE_STR);
		if (reader->label == main_reader_label)
			jf->wait_until_nonfull = 0;
		waited = 1;
	}
	return (waited);
}

static gfarm_error_t
journal_file_reader_check_pos(struct journal_file_reader *reader,
	struct journal_file *jf, size_t rec_len)
{
	struct journal_file_writer *writer = &jf->writer;
	off_t wpos, rpos;
	gfarm_uint64_t wlap, rlap;
	int valid = 1;

	wpos = writer->pos;
	wlap = writer->lap;
	rpos = reader->committed_pos;
	rlap = reader->committed_lap;

	if (wlap == rlap) {
		/*
		 * The reader and the writer are in the same lap.
		 */
		if (rpos > wpos) {
			/*
			 * 0       w      r         max_size
			 * +--+----+------+---------+
			 * |FH|
			 */
			valid = 0;
		} else if (wpos + rec_len > jf->max_size &&
		    rec_len + JOURNAL_FILE_HEADER_SIZE > rpos) {
			/*
			 * 0      r              w  max_size
			 * +--+---+--------------+--+
			 * |FH|rec_len|
			 */
			valid = 0;
		}
	} else if (wlap == rlap + 1) {
		/*
		 * The reader is a lap behind the writer.
		 *
		 * 0       w    r           max_size
		 * +--+----+----+-----------+
		 * |FH|    |rec_len|
		 */
		if (rpos < wpos + rec_len)
			valid = 0;
	} else
		valid = 0;

	if (!valid) {
		journal_file_reader_expire(reader);
		journal_file_reader_invalidate_unlocked(reader);
		gflog_debug(GFARM_MSG_1002876,
		    "invalidated journal_file_reader : rec_len=%lu, "
		    "rpos=%lu, wpos=%lu",
		    (unsigned long)rec_len, (unsigned long)rpos,
		    (unsigned long)wpos);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_file_writer_rewind(struct journal_file *jf)
{
	struct journal_file_writer *writer = &jf->writer;
	gfarm_error_t e;
	off_t pos;

	jf->tail = writer->pos;
	if ((e = journal_file_writer_fill(writer))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002878,
		    "journal_file_writer_fill : %s",
		    gfarm_error_string(e));
		return (e);
	}
	jf->size = jf->max_size;
	pos = lseek(gfp_xdr_fd(writer->xdr), JOURNAL_FILE_HEADER_SIZE,
	    SEEK_SET);
	if (pos < 0) {
		e = gfarm_errno_to_error(e);
		gflog_error(GFARM_MSG_1002879,
		    "lseek : %s",
		    gfarm_error_string(e));
		return (e);
	}
	writer->pos = pos;
	writer->lap++;

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_file_check_rec_len(struct journal_file *jf, size_t rec_len)
{
	gfarm_error_t e;

	if (rec_len + JOURNAL_FILE_HEADER_SIZE > jf->max_size) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_error(GFARM_MSG_1002877,
		    "rec_len(%lu) + fh(%lu) > max_size(%lu) : %s",
		    (unsigned long)rec_len,
		    (unsigned long)JOURNAL_FILE_HEADER_SIZE,
		    (unsigned long)jf->max_size,
		    gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_fread_file_header(FILE *file)
{
	gfarm_error_t e;
	int no;
	off_t pos = 0;
	int eof;
	char magic[GFARM_JOURNAL_MAGIC_SIZE];
	gfarm_uint32_t version;

	errno = 0;
	if (jounal_fread_fully(file, magic, GFARM_JOURNAL_MAGIC_SIZE,
	    &pos, &eof) < 0) {
		no = GFARM_MSG_1002880;
		e = gfarm_errno_to_error(errno);
	} else if (strncmp(magic, GFARM_JOURNAL_FILE_MAGIC,
	    GFARM_JOURNAL_MAGIC_SIZE) != 0) {
		no = GFARM_MSG_1002881;
		e = GFARM_ERR_INTERNAL_ERROR;
	} else if (journal_fread_uint32(file, &version, NULL, &pos) <= 0) {
		no = GFARM_MSG_1002882;
		e = GFARM_ERR_INTERNAL_ERROR;
	} else if (version != GFARM_JOURNAL_VERSION) {
		no = GFARM_MSG_1002883;
		e = GFARM_ERR_INTERNAL_ERROR;
	} else if (fseek(file, JOURNAL_FILE_HEADER_SIZE, SEEK_SET) < 0) {
		no = GFARM_MSG_1002884;
		e = gfarm_errno_to_error(errno);
	} else
		return (GFARM_ERR_NO_ERROR);

	gflog_error(no, "invalid journal file format");
	return (e);
}

static gfarm_error_t
journal_find_rw_pos0(FILE *file, int has_writer, size_t file_size,
	gfarm_uint64_t db_seqnum, off_t *rposp, gfarm_uint64_t *rlapp, 
	off_t *wposp, gfarm_uint64_t wlap, off_t *tailp,
	gfarm_uint64_t *max_seqnump)
{
	gfarm_error_t e;
	off_t pos = 0;
	off_t begin_pos = 0;
	off_t first_begin_pos = 0, first_end_pos = 0;
	off_t last_begin_pos = 0, last_end_pos = 0;
	off_t next_pos = JOURNAL_FILE_HEADER_SIZE;
	off_t min_seqnum_pos = JOURNAL_FILE_HEADER_SIZE;
	off_t max_seqnum_next_pos = JOURNAL_FILE_HEADER_SIZE;
	enum journal_operation ope = 0;
	gfarm_uint64_t min_seqnum = GFARM_UINT64_MAX;
	gfarm_uint64_t max_seqnum = GFARM_METADB_SERVER_SEQNUM_INVALID;
	gfarm_uint64_t seqnum = 0;
	gfarm_uint64_t begin_seqnum = 0;
	gfarm_uint64_t first_seqnum = 0, last_seqnum = 0;
	gfarm_uint64_t first_end_seqnum = 0;
	gfarm_uint64_t last_begin_seqnum = 0;
	int incomplete_transaction = 0;

	/*
	 * Read all records in the journal file successively and collect
	 * status information to determine read/write positions.
	 */
	if ((e = journal_fread_file_header(file)) != GFARM_ERR_NO_ERROR)
		return (e);

	errno = 0;
	for (;;) {
		if ((e = journal_fread_rec_outline(file, file_size, &seqnum,
		    &ope, &pos, &next_pos)) != GFARM_ERR_NO_ERROR)
			return (e);
		if (pos == -1)
			break;

		/*
		 * 'seqnum' decreases when the function reads a record of
		 * a lap behind.  In case it meets a record of a lap behind,
		 * it must ignore records until it finds the next
		 * 'GFM_JOURNAL_BEGIN' record.
		 */
		if (seqnum <= last_seqnum)
			incomplete_transaction = 1;
		if (first_seqnum == 0)
			first_seqnum = seqnum;
		last_seqnum = seqnum;
		if (ope == GFM_JOURNAL_BEGIN) {
			if (first_begin_pos == 0)
				first_begin_pos = pos;
			begin_pos = pos;
			begin_seqnum = seqnum;
			last_begin_seqnum = seqnum;
			last_begin_pos = pos;
			incomplete_transaction = 0;
		} else if (ope == GFM_JOURNAL_END && !incomplete_transaction) {
			if (first_end_seqnum == 0) {
				first_end_seqnum = seqnum;
				first_end_pos = pos;
			}
			last_end_pos = pos;
			if (max_seqnum < seqnum &&
			    db_seqnum <= seqnum) {
				max_seqnum = seqnum;
				max_seqnum_next_pos = next_pos;
			} 
			if (db_seqnum < begin_seqnum &&
			    min_seqnum > begin_seqnum) {
				min_seqnum = begin_seqnum;
				min_seqnum_pos = begin_pos;
			}
		}
		*tailp = next_pos;
	}

	/*
	 * The last transaction may be written separately in the journal
	 * file; GFM_JOURNAL_BEGIN and its corresponding GFM_JOURNAL_END
	 * records are located at the tail and beginning of the file
	 * respectively.
	 */
	if (first_seqnum == last_seqnum + 1 && /* circulated */
	    db_seqnum < last_begin_seqnum && /* newer than current seqnum */
	    last_begin_seqnum < first_end_seqnum && /* valid BEGIN - END */
	    last_begin_seqnum < min_seqnum && /* is min BEGIN seqnum */
	    first_end_pos < first_begin_pos && /* END in head */
	    last_end_pos < last_begin_pos && /* BEGIN in tail */
	    !incomplete_transaction /* the transaction is valid */) {
		min_seqnum = last_begin_seqnum;
		min_seqnum_pos = last_begin_pos;
	}

	/*
	 * Check 'max_seqnum' and 'min_seqnum'.
	 *
	 * - '!has_writer' means that this function is called for creating
	 *   a reader which is used for sending records to a slave gfmd.
	 *
	 * - 'db_seqnum != GFARM_METADB_SERVER_SEQNUM_INVALID' means that
	 *   gfmd is about to start.  In case of gfjournal command,
	 *   'db_seqnum' is always 0.
	 *
	 * - 'max_seqnum != GFARM_METADB_SERVER_SEQNUM_INVALID' means that
	 *   at least one transaction is recorded in the journal file.
	 *
	 * - 'min_seqnum == GFARM_UINT64_MAX' means that there is no record
	 *   with the seqnum greater than 'db_seqnum'.
	 *   If 'db_seqnum == max_seqnum', it means that all records in
	 *   the journal file has been written in database.  Otherwise,
	 *   database, the journal file or both are corrupted.
	 *
	 * - 'db_seqnum + 1 < min_seqnum' means that sequece numbers
	 *   around 'db_seqnum' are not succesive.  One or more records
	 *   may have been lost.
	 *
	 * - 'max_seqnum < db_seqnum' means that database, the journal
	 *   file or both are corrupted.
	 */
	if (!has_writer && db_seqnum != max_seqnum &&
	    db_seqnum != GFARM_METADB_SERVER_SEQNUM_INVALID &&
	    max_seqnum != GFARM_METADB_SERVER_SEQNUM_INVALID &&
	    (min_seqnum == GFARM_UINT64_MAX || db_seqnum + 1 < min_seqnum ||
	    max_seqnum < db_seqnum)) {
		e = GFARM_ERR_EXPIRED;
		gflog_info(GFARM_MSG_1003421,
		    "%s: seqnum=%llu min_seqnum=%llu max_seqnum=%llu",
		    gfarm_error_string(e),
		    (unsigned long long)db_seqnum,
		    (unsigned long long)min_seqnum,
		    (unsigned long long)max_seqnum);
		return (e);
	}

	/*
	 * Determine the read/write positions.
	 */
	if (min_seqnum == GFARM_UINT64_MAX) {
		/*
		 * In the journal file, there is no seqnum greater than
		 * 'db_seqnum'.  It means that all data in the file have
		 * been stored into database.
		 */
		*rposp = max_seqnum_next_pos;
		*rlapp = wlap;
		*wposp = max_seqnum_next_pos;
	} else if (min_seqnum_pos < max_seqnum_next_pos) {
		/*
		 * The read-position is less than the writer-position.
		 */
		*rposp = min_seqnum_pos;
		*rlapp = wlap;
		*wposp = max_seqnum_next_pos;
	} else {
		/*
		 * The read-position is greater than the write-position.
		 * It means that the writer rewinded one more time than
		 * the reader.
		 */
		*rposp = min_seqnum_pos;
		*rlapp = wlap - 1;
		*wposp = max_seqnum_next_pos;
	}
	if (max_seqnump != NULL)
		*max_seqnump = max_seqnum;

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_find_rw_pos(int rfd, int wfd, size_t file_size,
	gfarm_uint64_t db_seqnum, off_t *rposp, gfarm_uint64_t *rlapp,
	off_t *wposp, gfarm_uint64_t wlap, off_t *tailp,
	gfarm_uint64_t *max_seqnump)
{
	gfarm_error_t e;
	int duped_rfd;
	FILE *rf;

	duped_rfd = dup(rfd);
	if (duped_rfd < 0)
		return (gfarm_errno_to_error(errno));
	rf = fdopen(duped_rfd, "r");
	if (rf == NULL) {
		e = gfarm_errno_to_error(errno);
		close(duped_rfd);
		return (e);
	}

	e = journal_find_rw_pos0(rf, wfd >= 0, file_size, db_seqnum, rposp,
	    rlapp,  wposp, wlap, tailp, max_seqnump);
	fclose(rf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (lseek(rfd, *rposp, SEEK_SET) < 0)
		return (gfarm_errno_to_error(errno));
	if (wfd >= 0 && lseek(wfd, *wposp, SEEK_SET) < 0)
		return (gfarm_errno_to_error(errno));

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_close_fd_op(void *cookie, int fd)
{
	return (close(fd) == -1 ? gfarm_errno_to_error(errno) :
	    GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_export_credential_fd_op(void *cookie)
{
	/* it's already exported, or no way to export it. */
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_delete_credential_fd_op(void *cookie, int sighandler)
{
	return (GFARM_ERR_NO_ERROR);
}

static char *
journal_env_for_credential_fd_op(void *cookie)
{
	return (NULL);
}

static int
journal_blocking_read_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	gfarm_error_t e;
	int eof;
	size_t rlen;
	ssize_t ssz;
	struct journal_file_reader *reader = cookie;
	struct journal_file *jf = reader->file;
	struct journal_file_writer *writer = &jf->writer;

	assert(reader->fd_pos >= 0 && writer->pos >= 0);

	if (reader->fd_pos == writer->pos && reader->fd_lap == writer->lap)
		return (0);
	if (reader->fd_lap == writer->lap) {
		assert(reader->fd_pos <= writer->pos);
		if (reader->fd_pos + length <= writer->pos)
			rlen = length;
		else
			rlen = writer->pos - reader->fd_pos;
	} else if (reader->fd_pos < jf->tail) {
		assert(reader->fd_lap + 1 == writer->lap);
		if (reader->fd_pos + length <= jf->tail)
			rlen = length;
		else
			rlen = jf->tail - reader->fd_pos;
	} else {
		assert(reader->fd_lap + 1 == writer->lap);
		assert(reader->fd_pos == jf->tail);
		if ((e = journal_file_reader_rewind(reader))
		    != GFARM_ERR_NO_ERROR) {
			gfarm_iobuffer_set_error(b, e);
			return (-1);
		}
		if (length <= writer->pos - JOURNAL_FILE_HEADER_SIZE)
			rlen = length;
		else
			rlen = writer->pos - JOURNAL_FILE_HEADER_SIZE;
	}

	errno = 0;
	ssz = jounal_read_fully(fd, data, rlen, &reader->fd_pos, &eof);
	if (ssz < 0) {
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
		return (ssz);
	}
	if (eof)
		gfarm_iobuffer_set_read_eof(b);
	return (ssz);
}

static int
journal_blocking_write_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t ssz;
	struct journal_file_writer *writer = cookie;
	struct journal_file *jf = writer->file;

	errno = 0;
	if ((ssz = journal_write_fully(fd, data, length, &writer->pos)) < 0)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	if (jf->size < writer->pos)
		jf->size = writer->pos;
	if (jf->tail < writer->pos)
		jf->tail = writer->pos;
	return (ssz);
}

static gfarm_error_t
journal_write_file_header(int fd)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	char *header = NULL;
	gfarm_uint32_t version = htonl(GFARM_JOURNAL_VERSION);
	int sync_result;
 	off_t pos = 0;
 
	GFARM_MALLOC_ARRAY(header, JOURNAL_FILE_HEADER_SIZE);
	if (header == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1003321,
			"allocation of memory failed: %s",
			gfarm_error_string(e));
		goto error;
	}
 
	memset(header, 0, JOURNAL_FILE_HEADER_SIZE);
	memcpy(header, GFARM_JOURNAL_FILE_MAGIC, GFARM_JOURNAL_MAGIC_SIZE);
	memcpy(header + GFARM_JOURNAL_MAGIC_SIZE, (void *) &version,
	    sizeof(version));

	if (journal_write_fully(fd, header, JOURNAL_FILE_HEADER_SIZE, &pos)
	    < 0) {
		gflog_error(GFARM_MSG_1002885,
		    "failed to write journal file header : %s",
		    gfarm_error_string(e));
		goto error;
	}

	errno = 0;
#ifdef HAVE_FDATASYNC
	sync_result = fdatasync(fd);
#else
	sync_result = fsync(fd);
#endif
	if (sync_result == -1) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1003322,
			"failed to sync journal file : %s",
			gfarm_error_string(e));
		goto error;
 	}

error:
	free(header);
 	return (e);
}

static struct gfp_iobuffer_ops journal_iobuffer_ops = {
	journal_close_fd_op,
	journal_export_credential_fd_op,
	journal_delete_credential_fd_op,
	journal_env_for_credential_fd_op,
	journal_blocking_read_op,
	journal_blocking_read_op,
	journal_blocking_write_op
};

static gfarm_error_t
journal_file_reader_init(struct journal_file *jf, int fd,
	off_t pos, gfarm_uint64_t lap, int block_writer, 
	struct journal_file_reader *reader)
{
	gfarm_error_t e;
	struct gfp_xdr *xdr;

	if (lseek(fd, pos, SEEK_SET) == -1) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1002888,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = gfp_xdr_new(&journal_iobuffer_ops, reader, fd,
	    GFP_XDR_NEW_RECV|GFP_XDR_NEW_AUTO_RECV_EXPANSION, &xdr))) {
		free(reader);
		gflog_error(GFARM_MSG_1002889,
		    "%s", gfarm_error_string(e));
		return (e);
	}

	reader->file = jf;
	reader->fd_pos = pos;
	reader->fd_lap = lap;
	reader->committed_pos = pos;
	reader->committed_lap = lap;
	reader->uncommitted_len = 0;
	reader->xdr = xdr;
	reader->flags = 0;
	/* do not init last_seqnum here */

	journal_file_reader_set_flag(reader,
	    JOURNAL_FILE_READER_F_BLOCK_WRITER, block_writer);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_file_reader_new(struct journal_file *jf, int fd,
	off_t pos, gfarm_uint64_t lap, int block_writer, const char *label,
	struct journal_file_reader **readerp)
{
	gfarm_error_t e;
	struct journal_file_reader *reader;

	GFARM_MALLOC(reader);
	if (reader == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1002890,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = journal_file_reader_init(jf, fd, pos, lap, block_writer,
	    reader)) != GFARM_ERR_NO_ERROR) {
		free(reader);
		return (e);
	}
	reader->last_seqnum = 0;
	reader->label = label;
	GFARM_HCIRCLEQ_INSERT_TAIL(jf->reader_list, reader, readers);
	*readerp = reader;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
journal_file_open(const char *path, size_t max_size,
	gfarm_uint64_t cur_seqnum, struct journal_file **jfp, int flags)
{
	gfarm_error_t e;
	struct stat st;
	int rfd = -1, wfd = -1, rv, save_errno;
	size_t cur_size = 0;
	off_t rpos = 0, wpos = 0;
	gfarm_uint64_t rlap = 0;
	off_t tail = 0;
	struct journal_file *jf;
	struct journal_file_reader *reader = NULL;
	struct gfp_xdr *writer_xdr = NULL;
	static const char diag[] = "journal_file_open";

	GFARM_MALLOC(jf);
	if (jf == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1002891,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	memset(jf, 0, sizeof(*jf));
	gfarm_privilege_lock(diag);
	rv = stat(path, &st);
	save_errno = errno;
	gfarm_privilege_unlock(diag);
	if (rv == -1) {
		if (save_errno != ENOENT) {
			e = gfarm_errno_to_error(save_errno);
			gflog_error(GFARM_MSG_1002892,
			    "stat : %s",
			    gfarm_error_string(e));
			goto error;
		}
	} else {
		cur_size = st.st_size;
		if (cur_size < JOURNAL_FILE_HEADER_SIZE) {
			e = GFARM_ERR_INTERNAL_ERROR;
			gflog_warning(GFARM_MSG_1002893,
			    "invalid journal file size : %lu",
			    (unsigned long)cur_size);
			gfarm_privilege_lock(diag);
			rv = unlink(path);
			save_errno = errno;
			gfarm_privilege_unlock(diag);
			if (rv == -1) {
				e = gfarm_errno_to_error(save_errno);
				gflog_warning(GFARM_MSG_1002894,
				    "failed to unlink %s: %s", path,
				    gfarm_error_string(e));
				goto error;
			}
		}
	}

	if (cur_size > 0 && max_size > 0 && cur_size > max_size) {
		e = GFARM_ERR_FILE_TOO_LARGE;
		gflog_error(GFARM_MSG_1002895,
		    "journal file is larger than journal_max_size."
		    " path : %s, size : %lu > %lu", path,
		    (unsigned long)cur_size, (unsigned long)max_size);
		return (e);
	}
	if ((flags & GFARM_JOURNAL_RDWR) != 0 &&
	    max_size < JOURNAL_FILE_MIN_SIZE) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_error(GFARM_MSG_1002896,
		    "journal_max_size must be larger than %d. current=%lu",
		    JOURNAL_FILE_MIN_SIZE, (unsigned long)max_size);
		return (e);
	}

	jf->size = cur_size;
	jf->max_size = (flags & GFARM_JOURNAL_RDONLY) != 0 ?
		cur_size : max_size;

	if ((flags & GFARM_JOURNAL_RDWR) != 0) {
		gfarm_privilege_lock(diag);
		wfd = open(path, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);
		save_errno = errno;
		gfarm_privilege_unlock(diag);
		if (wfd < 0) {
			e = gfarm_errno_to_error(save_errno);
			gflog_error(GFARM_MSG_1002897,
			    "open for write: %s",
			    gfarm_error_string(e));
			goto error;
		}
		fsync(wfd);
	}
	gfarm_privilege_lock(diag);
	rfd = open(path, O_RDONLY);
	save_errno = errno;
	gfarm_privilege_unlock(diag);
	if (rfd < 0) {
		e = gfarm_errno_to_error(save_errno);
		gflog_error(GFARM_MSG_1002898,
		    "open for read: %s",
		    gfarm_error_string(e));
		goto error;
	}
	if (cur_size > 0) {
		if ((e = journal_find_rw_pos(rfd, wfd, cur_size,
		    cur_seqnum, &rpos, &rlap, &wpos, JOURNAL_INITIAL_WLAP,
		    &tail, &jf->initial_max_seqnum)) != GFARM_ERR_NO_ERROR)
			goto error;
	} else {
		if (wfd >= 0) {
			if ((e = journal_write_file_header(wfd))
			    != GFARM_ERR_NO_ERROR)
				goto error;
		}
		wpos = JOURNAL_FILE_HEADER_SIZE;
		if (lseek(rfd, wpos, SEEK_SET) < 0) {
			e = gfarm_errno_to_error(errno);
			goto error;
		}
		tail = JOURNAL_FILE_HEADER_SIZE;
		rpos = JOURNAL_FILE_HEADER_SIZE;
		rlap = JOURNAL_INITIAL_WLAP;
	}
	jf->path = strdup_log(path, "journal_file_open");
	if (jf->path == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto error;
	}
	jf->tail = tail;
	if ((flags & GFARM_JOURNAL_RDWR) != 0) {
		if ((e = gfp_xdr_new(&journal_iobuffer_ops,
		    &jf->writer, wfd, GFP_XDR_NEW_SEND, &writer_xdr)))
			goto error;
		journal_file_writer_set(jf, writer_xdr, wpos,
		    JOURNAL_INITIAL_WLAP, &jf->writer);
	} else {
		journal_file_writer_set(jf, NULL, wpos, JOURNAL_INITIAL_WLAP,
		    &jf->writer);
	}

	GFARM_HCIRCLEQ_INIT(jf->reader_list, readers);

	/* create journal_file_main_reader */
	if ((e = journal_file_reader_new(jf, rfd, rpos, rlap, 1,
	    main_reader_label, &reader)) != GFARM_ERR_NO_ERROR)
		goto error;

	gfarm_cond_init(&jf->nonfull_cond, diag, JOURNAL_FILE_STR);
	gfarm_cond_init(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);
	gfarm_cond_init(&jf->drain_cond, diag, JOURNAL_FILE_STR);
	gfarm_mutex_init(&jf->mutex, diag, JOURNAL_FILE_STR);
	jf->wait_until_nonempty =
	jf->wait_until_nonfull = 0;
	*jfp = jf;

	return (GFARM_ERR_NO_ERROR);
error:
	if (rfd >= 0)
		close(rfd);
	if (writer_xdr)
		gfp_xdr_free(writer_xdr);
	else if (wfd >= 0)
		close(wfd);
	free(jf);
	if (reader != NULL)
		journal_file_reader_close_unlocked(reader);
	*jfp = NULL;
	return (e);
}

gfarm_error_t
journal_file_reader_reopen_if_needed(struct journal_file *jf,
	const char *label, struct journal_file_reader **readerp,
	gfarm_uint64_t seqnum, int *initedp)
{
	gfarm_error_t e, e2;
	int fd = -1;
	off_t rpos, wpos;
	gfarm_uint64_t rlap;
	off_t tail;
	struct journal_file_writer *writer = &jf->writer;
	static const char diag[] = "journal_file_reader_reopen";

	journal_file_mutex_lock(jf, diag);

	if (*readerp) {
		if ((*readerp)->xdr) {
			/* opened reader has xdr */
			e = GFARM_ERR_NO_ERROR;
			goto unlock;
		}
		if (journal_file_reader_is_expired_unlocked(*readerp) &&
		    (*readerp)->last_seqnum == seqnum) {
			/* avoid unnecessary calling journal_find_rw_pos() */
			e = GFARM_ERR_EXPIRED;
			goto unlock;
		}
	}

	/* *readerp is invalidated or non-initialized */

	gfarm_privilege_lock(diag);
	fd = open(jf->path, O_RDONLY);
	gfarm_privilege_unlock(diag);
	if (fd == -1) {
		e = gfarm_errno_to_error(errno);
		gflog_debug(GFARM_MSG_1003422,
		    "open: %s", gfarm_error_string(e));
		goto unlock;
	}

	assert(*readerp == NULL || (*readerp)->xdr == NULL);
	if ((e2 = journal_find_rw_pos(fd, -1, jf->tail, seqnum, &rpos,
	    &rlap, &wpos, writer->lap, &tail, &jf->initial_max_seqnum))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003423,
		    "journal_find_rw_pos: %s", gfarm_error_string(e2));
		if (e2 != GFARM_ERR_EXPIRED) {
			e = e2;
			goto unlock;
		}
		rpos = 0;
		rlap = 1;
	}

	if (*readerp != NULL)
		e = journal_file_reader_init(jf, fd, rpos, rlap, 0, *readerp);
	else
		e = journal_file_reader_new(
			jf, fd, rpos, rlap, 0, label, readerp);
	gfarm_cond_broadcast(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);
	if (e == GFARM_ERR_NO_ERROR) {
		if (e2 != GFARM_ERR_NO_ERROR) {
			journal_file_reader_expire(*readerp);
			journal_file_reader_invalidate_unlocked(*readerp);
			(*readerp)->last_seqnum = seqnum;
			e = e2;
		}
		*initedp = 1;
	}
unlock:
	journal_file_mutex_unlock(jf, diag);
	if (e != GFARM_ERR_NO_ERROR && fd != -1)
		(void)close(fd);
	return (e);
}

void
journal_file_wait_until_empty(struct journal_file *jf)
{
	struct journal_file_writer *writer = &jf->writer;
	struct journal_file_reader *reader = journal_file_main_reader(jf);
	static const char diag[] = "journal_file_wait_until_empty";
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1002899,
	    "wait until applying all rest journal records");
#endif
	journal_file_mutex_lock(jf, diag);
	while (writer->pos != reader->fd_pos || writer->lap != reader->fd_lap)
		gfarm_cond_wait(&jf->nonfull_cond, &jf->mutex, diag,
		    JOURNAL_FILE_STR);
	journal_file_mutex_unlock(jf, diag);
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1002900, "end applying journal records");
#endif
}

void
journal_file_close(struct journal_file *jf)
{
#if 0
	struct journal_file_reader *reader, *reader2;
#endif
	static const char diag[] = "journal_file_close";

	if (jf == NULL)
		return;
	journal_file_mutex_lock(jf, diag);
#if 0	/*
	 * We don't have to do this, because storage for jf is never freed.
	 * We shouldn't do this, because doing so makes
	 * journal_file_mutex_lock(reader->file, ) dump core,
	 * or db_journal_store_thread() dump core (see SF.net #737).
	 */
	journal_file_reader_close_unlocked(journal_file_main_reader(jf));
	GFARM_HCIRCLEQ_FOREACH_SAFE(reader, jf->reader_list, readers, reader2)
		journal_file_reader_detach(reader);
#endif
	if (jf->writer.xdr != NULL)
		gfp_xdr_free(jf->writer.xdr);
	jf->writer.xdr = NULL;
	free(jf->path);
	jf->path = NULL;
	journal_file_mutex_unlock(jf, diag);
}

int
journal_file_is_closed(struct journal_file *jf)
{
	return (jf->path == NULL);
}

gfarm_error_t
journal_file_writer_sync(struct journal_file_writer *writer)
{
#ifdef HAVE_FDATASYNC
	if (fdatasync(gfp_xdr_fd(writer->xdr)) == -1)
#else
	if (fsync(gfp_xdr_fd(writer->xdr)) == -1)
#endif
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
journal_file_writer_flush(struct journal_file_writer *writer)
{
	gfarm_error_t e = gfp_xdr_flush(writer->xdr);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002901,
		    "gfp_xdr_flush : %s", gfarm_error_string(e));
	}
	return (e);
}

const char *
journal_operation_name(enum journal_operation ope)
{
	assert(sizeof(journal_operation_names) / sizeof(char *) ==
		GFM_JOURNAL_OPERATION_MAX);

	if (ope <= 0 || ope >= GFM_JOURNAL_OPERATION_MAX)
		return (NULL);
	return (journal_operation_names[ope]);
}

static gfarm_error_t
journal_write_rec_header(struct gfp_xdr *xdr, gfarm_uint64_t seqnum,
	enum journal_operation ope, size_t data_len, gfarm_uint32_t *crcp)
{
	gfarm_error_t e;
	size_t header_size = JOURNAL_RECORD_HEADER_SIZE;

	if ((e = gfp_xdr_send(xdr, GFARM_JOURNAL_RECORD_HEADER_XDR_FMT,
		GFARM_JOURNAL_RECORD_MAGIC[0],
		GFARM_JOURNAL_RECORD_MAGIC[1],
		GFARM_JOURNAL_RECORD_MAGIC[2],
		GFARM_JOURNAL_RECORD_MAGIC[3],
		seqnum,
		(gfarm_int32_t)ope,
		(gfarm_uint32_t)data_len))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002902,
		    "gfp_xdr_send : %s",
		    gfarm_error_string(e));
		return (e);
	}
	*crcp = gfp_xdr_send_calc_crc32(xdr, 0, -header_size, header_size);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_write_footer(struct gfp_xdr *xdr, size_t data_len,
	gfarm_uint32_t header_crc)
{
	gfarm_error_t e;
	gfarm_uint32_t crc;

	crc = gfp_xdr_send_calc_crc32(xdr, header_crc, -data_len, data_len);
	if ((e = gfp_xdr_send(xdr, "i", crc))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002903,
		    "gfp_xdr_send : %s",
		    gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
journal_file_write(struct journal_file *jf, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *arg,
	journal_size_add_op_t size_add_op, journal_send_op_t send_op)
{
	gfarm_error_t e;
	size_t data_len = 0;
	size_t rec_len = 0;
	struct journal_file_writer *writer = &jf->writer;
	struct gfp_xdr *xdr = writer->xdr;
	gfarm_uint32_t crc;
	static const char diag[] = "journal_file_write";
	struct journal_file_reader *reader;
	int waited;

	assert(jf);
	assert(size_add_op);
	assert(send_op);
	assert(seqnum > 0);
	if ((e = size_add_op(ope, &data_len, arg)) != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1002904,
		    "size_add_op", e, seqnum, ope);
		return (e);
	}

	journal_file_mutex_lock(jf, diag);
	if (xdr == NULL) {
		e = GFARM_ERR_INPUT_OUTPUT; /* shutting down */
		goto unlock;
	}
	rec_len = JOURNAL_RECORD_SIZE(data_len);
	if ((e = journal_file_check_rec_len(jf, rec_len))
	    != GFARM_ERR_NO_ERROR)
		goto unlock;
	do {
		waited = 0;
		GFARM_HCIRCLEQ_FOREACH(reader, jf->reader_list, readers) {
			if (journal_file_reader_writer_wait(reader, jf,
			    rec_len)) {
				waited = 1; /* rescan from the first */
				break;
			}
			if (!journal_file_reader_is_invalid(reader) &&
			    (e = journal_file_reader_check_pos(reader, jf,
			    rec_len)) != GFARM_ERR_NO_ERROR)
				goto unlock;
			if (reader->xdr != NULL)
				gfp_xdr_recvbuffer_clear_read_eof(reader->xdr);
		}
	} while (waited);
	if (writer->pos + rec_len > jf->max_size) {
		if ((e = journal_file_writer_rewind(jf)) != GFARM_ERR_NO_ERROR)
			goto unlock;
	}
	gfp_xdr_begin_sendbuffer_pindown(xdr);
	if ((e = journal_write_rec_header(xdr, seqnum, ope, data_len, &crc))
	    != GFARM_ERR_NO_ERROR)
		goto unlock;
	if ((e = send_op(ope, arg)) != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1002905,
		    "send_op", e, seqnum, ope);
		goto unlock;
	}
	if ((e = journal_write_footer(xdr, data_len, crc))
	    != GFARM_ERR_NO_ERROR)
		goto unlock;
	if ((e = journal_file_writer_flush(writer)) != GFARM_ERR_NO_ERROR)
		goto unlock;
	gfarm_cond_broadcast(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);
#ifdef DEBUG_JOURNAL
	gflog_info(GFARM_MSG_1002906,
	    "write seqnum=%llu ope=%s wpos=%llu wlap=%llu",
	    (unsigned long long)seqnum, journal_operation_name(ope),
	    (unsigned long long)writer->pos, (unsigned long long)writer->lap);
#endif
unlock:
	gfp_xdr_end_sendbuffer_pindown(xdr);
	journal_file_mutex_unlock(jf, diag);
	return (e);
}

static gfarm_error_t
journal_read_rec_header(struct gfp_xdr *xdr, enum journal_operation *opep,
	gfarm_uint64_t *seqnump, size_t *rlenp)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	enum journal_operation ope;
	gfarm_uint32_t len, crc1, crc2;
	char magic[GFARM_JOURNAL_MAGIC_SIZE];
	int eof;
	size_t rlen, avail, header_size = JOURNAL_RECORD_HEADER_SIZE;

	memset(magic, 0, sizeof(magic));

	if ((e = gfp_xdr_recv(xdr, 0, &eof,
	    GFARM_JOURNAL_RECORD_HEADER_XDR_FMT,
	    &magic[0], &magic[1], &magic[2], &magic[3],
	    &seqnum, &ope, &len)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002908,
		    "gfp_xdr_recv : %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (memcmp(magic, GFARM_JOURNAL_RECORD_MAGIC,
	    GFARM_JOURNAL_MAGIC_SIZE) != 0) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1003323,
		    "invalid journal record magic: seqnum=%lld: %s",
		    (unsigned long long)seqnum,
		    gfarm_error_string(e));
		return (e);
	}
	/*
	 * cast "(unsigned)ope" is to shut up the following warning from clang
	 * against comparison "ope < 0":
	 *	comparison of unsigned enum expression < 0 is always false
	 *	[-Wtautological-compare]
	 */
	if ((unsigned)ope >= GFM_JOURNAL_OPERATION_MAX ||
	    len >= JOURNAL_RECORD_SIZE_MAX - header_size) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002909,
		    "invalid operation id (%d): seqnum=%lld: %s",
		    ope, (unsigned long long)seqnum,
		    gfarm_error_string(e));
		return (e);
	}
	crc1 = gfp_xdr_recv_calc_crc32(xdr, 0, -header_size, header_size);
	rlen = len + sizeof(gfarm_uint32_t);
	errno = 0;
	if ((e = gfp_xdr_recv_ahead(xdr, rlen, &avail))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002910,
		    "gfp_xdr_recv_ahead : %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (avail < rlen) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002911,
		    "record is too short (%lu < %lu): seqnum=%lld: %s",
		    (unsigned long)avail, (unsigned long)rlen,
		    (unsigned long long)seqnum,
		    gfarm_error_string(e));
		return (e);
	}
	crc1 = gfp_xdr_recv_calc_crc32(xdr, crc1, 0, len);
	crc2 = gfp_xdr_recv_get_crc32_ahead(xdr, len);
	if (crc1 != crc2) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002912,
		    "crc check failed (%u <-> %u): seqnum=%lld: %s",
		    crc1, crc2, (unsigned long long)seqnum,
		    gfarm_error_string(e));
		return (e);
	}
	*opep = ope;
	*seqnump = seqnum;
	*rlenp = rlen + header_size;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_read_purge(struct gfp_xdr *xdr, int len)
{
	gfarm_error_t e;

	if ((e = gfp_xdr_purge(xdr, 1, len))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002913,
		    "gfp_xdr_purge : %s",
		    gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
journal_file_read(struct journal_file_reader *reader, void *op_arg,
	journal_read_op_t read_op,
	journal_post_read_op_t post_read_op,
	journal_free_op_t free_op, void *closure, int *eofp)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	enum journal_operation ope = 0;
	int needs_free = 1, drained = 0;
	size_t len;
	void *obj = NULL;
	struct journal_file *jf = reader->file;
	struct gfp_xdr *xdr = reader->xdr;
	static const char diag[] = "journal_file_read";
	size_t avail;
	size_t min_rec_size = JOURNAL_RECORD_HEADER_SIZE
		+ sizeof(gfarm_uint32_t);

	if (eofp)
		*eofp = 0;
	journal_file_mutex_lock(jf, diag);
	if (journal_file_is_closed(jf)) {
		e = GFARM_ERR_NO_ERROR;
		goto unlock;
	}
	if (xdr == NULL) {
		e = GFARM_ERR_INPUT_OUTPUT; /* shutting down */
		goto unlock;
	}

	if ((e = gfp_xdr_recv_ahead(xdr, JOURNAL_READ_AHEAD_SIZE,
	    &avail)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002914,
		    "gfp_xdr_recv_ahead: %s", gfarm_error_string(e));
		goto unlock;
	}
	while (avail < min_rec_size) { /* no more record */
		if (journal_file_has_writer(jf) == 0) {
			*eofp = 1;
			goto unlock;
		}
		if (JOURNAL_FILE_READER_DRAINED(reader)) {
			journal_file_reader_set_flag(reader,
			    JOURNAL_FILE_READER_F_DRAIN, 0);
			drained = 1;
			e = GFARM_ERR_CANT_OPEN;
			goto unlock;
		}
		if (jf->wait_until_nonfull == 1 &&
		    reader->label == main_reader_label)
			gflog_fatal(GFARM_MSG_1004258, "deadlock detected: "
			    "increase \"metadb_journal_max_size\" "
			    "(currently %ld)", (unsigned long)jf->max_size);
		jf->wait_until_nonempty = 1;
		gfarm_cond_wait(&jf->nonempty_cond, &jf->mutex, diag,
		    JOURNAL_FILE_STR);
		jf->wait_until_nonempty = 0;
		if ((e = gfp_xdr_recv_ahead(xdr, JOURNAL_READ_AHEAD_SIZE,
		    &avail)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1002915,
			    "gfp_xdr_recv_ahead: %s", gfarm_error_string(e));
			goto unlock;
		}
	}
	if ((e = journal_read_rec_header(xdr, &ope, &seqnum, &len))
	    != GFARM_ERR_NO_ERROR)
		goto unlock;
	if (eofp && *eofp)
		goto unlock;
	if ((e = read_op(op_arg, xdr, ope, &obj))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1002916,
		    "read_arg_op", e, seqnum, ope);
		goto unlock;
	}
	if ((e = journal_read_purge(xdr, sizeof(gfarm_uint32_t)))
	    != GFARM_ERR_NO_ERROR) /* skip crc */
		goto unlock;
	if ((e = post_read_op(op_arg, seqnum, ope, obj, closure, len,
	    &needs_free)) != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1002917,
		    "post_read_op", e, seqnum, ope);
		goto unlock;
	}
	reader->uncommitted_len += len;
unlock:
	journal_file_mutex_unlock(jf, diag);
	if (needs_free && obj)
		free_op(op_arg, ope, obj);
	if (drained)
		gfarm_cond_signal(&jf->drain_cond, diag, JOURNAL_FILE_STR);
	return (e);
}

/* recp must be freed by caller */
gfarm_error_t
journal_file_read_serialized(struct journal_file_reader *reader,
	char **recp, gfarm_uint32_t *sizep, gfarm_uint64_t *seqnump, int *eofp)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	gfarm_uint32_t data_len, data_len2, rec_len;
	/* sizeof header must be larger than header_size */
	unsigned char header[64], *rec = NULL;
	int rlen;
	struct journal_file *jf = reader->file;
	struct gfp_xdr *xdr = reader->xdr;
	size_t avail, header_size = JOURNAL_RECORD_HEADER_SIZE;
	static const char diag[] = "journal_file_read_serialized";

	*eofp = 0;
	errno = 0;
	journal_file_mutex_lock(jf, diag);

	if (journal_file_reader_is_invalid(reader)) {
		/* invalidated reader while reading is always expired */
		e = GFARM_ERR_EXPIRED;
		gflog_error(GFARM_MSG_1003424,
		    "journal file is expired while reading records");
		goto error;
	}

	if ((e = gfp_xdr_recv_ahead(xdr, header_size, &avail)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002918,
		    "gfp_xdr_recv_ahead : %s", gfarm_error_string(e));
		goto error;
	}
	if ((e = gfp_xdr_recv_partial(xdr, 0, header, header_size, &rlen)) !=
	    GFARM_ERR_NO_ERROR) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002919,
		    "gfp_xdr_recv : %s",
		    gfarm_error_string(gfarm_errno_to_error(errno)));
		goto error;
	}
	if (rlen == 0) {
		*eofp = 1;
		e = GFARM_ERR_NO_ERROR;
		goto end;
	}
	seqnum = journal_deserialize_uint64(header + GFARM_JOURNAL_MAGIC_SIZE);
	data_len = journal_deserialize_uint32(header + header_size
		- sizeof(data_len));
	data_len2 = data_len + sizeof(gfarm_uint32_t);
	if ((e = gfp_xdr_recv_ahead(xdr, data_len2, &avail)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002920,
		    "gfp_xdr_recv_ahead : %s", gfarm_error_string(e));
		goto error;
	}
	rec_len = header_size + data_len2;
	GFARM_MALLOC_ARRAY(rec, rec_len);
	if (rec == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1002921,
		    "%s", gfarm_error_string(e));
		goto error;
	}
	e = gfp_xdr_recv_partial(xdr, 0, rec + header_size, data_len2, &rlen);
	if (e != GFARM_ERR_NO_ERROR || rlen == 0) {
		gflog_error(GFARM_MSG_1002922,
		    "gfp_xdr_recv : %s", e != GFARM_ERR_NO_ERROR ?
		    gfarm_error_string(e) : "eof");
		goto error;
	}
	if (rlen != data_len2) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002923,
		    "record is too short : %s",
		    gfarm_error_string(e));
		goto error;
	}
	memcpy(rec, header, header_size);
	*recp = (char *)rec;
	*seqnump = seqnum;
	*sizep = rec_len;
	reader->uncommitted_len += rec_len;
	reader->last_seqnum = seqnum;
	e = GFARM_ERR_NO_ERROR;
	goto end;
error:
	free(rec);
end:
	journal_file_mutex_unlock(jf, diag);
	return (e);
}

void
journal_file_wait_for_read_completion(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	static const char diag[] = "journal_file_wait_for_read_completion";

	journal_file_mutex_lock(jf, diag);
	journal_file_reader_set_flag(reader, JOURNAL_FILE_READER_F_DRAIN, 1);
	journal_file_mutex_unlock(jf, diag);
	gfarm_cond_signal(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);

	journal_file_mutex_lock(jf, diag);
	while (JOURNAL_FILE_READER_DRAINED(reader))
		gfarm_cond_wait(&jf->drain_cond, &jf->mutex, diag,
		    JOURNAL_FILE_STR);
	journal_file_mutex_unlock(jf, diag);
}

void
journal_file_wait_until_readable(struct journal_file *jf)
{
	struct journal_file_reader *reader;
	int isempty;
	static const char diag[] = "journal_file_wait_until_readable";

	journal_file_mutex_lock(jf, diag);

	for (;;) {
		isempty = 1;
		GFARM_HCIRCLEQ_FOREACH(reader, jf->reader_list, readers) {
			if (journal_file_reader_is_invalid(reader) ||
			    journal_file_reader_is_expired_unlocked(reader))
				continue;
			if (!journal_file_reader_is_empty(reader)) {
				isempty = 0;
				break;
			}
		}
		if (!isempty)
			break;
		gfarm_cond_wait(&jf->nonempty_cond, &jf->mutex, diag,
		    JOURNAL_FILE_STR);
	}

	journal_file_mutex_unlock(jf, diag);
}

gfarm_error_t
journal_file_write_raw(struct journal_file *jf, int recs_len,
	unsigned char *recs, gfarm_uint64_t *last_seqnump, int *trans_nestp)
{
	gfarm_error_t e;
	unsigned char *rec = recs;
	gfarm_uint32_t data_len, rec_len;
	gfarm_uint64_t seqnum;
	enum journal_operation ope;
	struct journal_file_writer *writer = &jf->writer;
	size_t header_size = JOURNAL_RECORD_HEADER_SIZE;
	static const char diag[] = "journal_file_write_raw";
	struct journal_file_reader *reader;
	int waited;

	journal_file_mutex_lock(jf, diag);

	for (;;) {
		seqnum = journal_deserialize_uint64(rec +
		    GFARM_JOURNAL_MAGIC_SIZE);
		ope = journal_deserialize_uint32(rec +
		    GFARM_JOURNAL_MAGIC_SIZE + sizeof(gfarm_uint64_t));
		data_len = journal_deserialize_uint32(rec + header_size -
			sizeof(data_len));
		rec_len = JOURNAL_RECORD_SIZE(data_len);
		if ((e = journal_file_check_rec_len(jf, rec_len))
		    != GFARM_ERR_NO_ERROR)
			goto unlock;
		do {
			waited = 0;
			GFARM_HCIRCLEQ_FOREACH(reader, jf->reader_list, readers) {
				if (journal_file_reader_writer_wait(reader, jf,
				    rec_len)) {
					waited = 1; /* rescan from the first */
					break;
				}
				if ((e = journal_file_reader_check_pos(reader, jf,
				    rec_len)) != GFARM_ERR_NO_ERROR)
					goto unlock;
				if (reader->xdr != NULL)
					gfp_xdr_recvbuffer_clear_read_eof(
					    reader->xdr);
			}
		} while (waited);
		if (writer->pos + rec_len > jf->max_size) {
			if ((e = journal_file_writer_rewind(jf))
			    != GFARM_ERR_NO_ERROR)
				goto unlock;
		}
		if ((e = gfp_xdr_send(writer->xdr, "r", rec_len, rec))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1002924,
			    "gfp_xdr_send : %s", gfarm_error_string(e));
			goto unlock;
		}
		if ((e = journal_file_writer_flush(writer))
		    != GFARM_ERR_NO_ERROR)
			goto unlock;
		rec += rec_len;
		switch (ope) {
		case GFM_JOURNAL_BEGIN:
			++(*trans_nestp);
			break;
		case GFM_JOURNAL_END:
			--(*trans_nestp);
			break;
		default:
			break;
		}
		if (rec - recs >= recs_len)
			break;
	}
	gfarm_cond_signal(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);
unlock:
	*last_seqnump = seqnum;
	journal_file_mutex_unlock(jf, diag);
	return (e);
}
