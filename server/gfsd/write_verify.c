#include <assert.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <openssl/evp.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "timer.h"
#include "tree.h"
#include "msgdigest.h"

#include "crc32.h"
#include "context.h"
#include "config.h"
#include "gfp_xdr.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "gfs_profile.h"

#include "gfsd_subr.h"
#include "write_verify.h"

#define TIMEBUF_FMT	"%Y-%m-%d %H:%M:%S"
#define TIMEBUF_SIZE	32 /* "1999-12-31 23:59:59" + '\0' + 12 bytes spare */

/*
 * RPC stub functions
 */

static int
read_nbytes(int fd, void *buffer, size_t sz, const char *diag)
{
	char *bufp = buffer;
	ssize_t rv;
	ssize_t done;

	for (done = 0; done < sz; ) {
		rv = read(fd, &bufp[done], sz - done);
		if (rv == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			fatal(GFARM_MSG_1004394,
			    "%s: read: %s", diag, strerror(errno));
		}
		if (rv == 0) {
			if (done > 0)
				gflog_error(GFARM_MSG_1004395,
				    "%s: partial read %zd bytes", diag, done);
			return (0);
		}
		done += rv;
	}
	return (1);
}

static int
write_nbytes(int fd, void *buffer, size_t sz, const char *diag)
{
	char *bufp = buffer;
	ssize_t rv;
	ssize_t done;

	for (done = 0; done < sz; ) {
		rv = write(fd, &bufp[done], sz - done);
		if (rv == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return (errno);
		}
		if (rv == 0) {
			fatal(GFARM_MSG_1004396,
			    "%s: write(2) returns 0 (%zd bytes written)",
			    diag, done);
		}
		done += rv;
	}
	return (0); /* no error */
}

/*
 * NOTE:
 * We don't have to take care of the protocol compatiblity about
 * struct write_verify_job_req, write_verify_job_reply and write_verify_req,
 * because all of them are only used between forked processes of same program.
 */

/*
 * from type_client or type_replication
 * to type_write_verify_controller
 *
 * reply protocol does not exist.
 */

struct write_verify_req {
	gfarm_int64_t ino;
	gfarm_uint64_t gen;
	gfarm_uint64_t mtime;
};

/* used by type_client and type_replication */
static int write_verify_request_send_fd = -1;

/* used by type_write_verify_controller */
static int write_verify_request_recv_fd = -1;

/* called from type_client and type_replication */
void
write_verify_request(gfarm_ino_t ino, gfarm_uint64_t gen, time_t mtime,
	const char *diag)
{
	ssize_t rv;
	struct write_verify_req req;

	assert(gfarm_write_verify);

	req.ino = ino;
	req.gen = gen;
	req.mtime = mtime;
	/*
	 * partial write isn't allowed,
	 * because this is NOT O_NONBLOCK and must be atomic
	 */
	rv = write(write_verify_request_send_fd, &req, sizeof req);
	if (rv != sizeof req) {
		if (rv == -1) {
			gflog_error_errno(GFARM_MSG_1004397,
			    "please run gfspooldigest: %s: "
			    "write_verify request "
			    "inode %lld:%lld (mtime: %lld)",
			    diag,
			    (long long)ino, (long long)gen, (long long)mtime);
		} else {
			gflog_error_errno(GFARM_MSG_1004398, "%s: "
			    "write_verify request %lld:%lld (mtime: %lld):"
			    "partial write %zd bytes: write_verify disabled",
			    diag,
			    (long long)ino, (long long)gen, (long long)mtime,
			    rv);
			/* disable, although this is too late */
			gfarm_write_verify = 0;
			close(write_verify_request_send_fd);
			write_verify_request_send_fd = -1;
		}
	}
}

static void
write_verify_request_recv(struct write_verify_req *req, const char *diag)
{
	ssize_t rv;

	/*
	 * partial read isn't allowed,
	 * because this is NOT O_NONBLOCK and must be atomic
	 */
	rv = read(write_verify_request_recv_fd, req, sizeof(*req));
	if (rv == 0) {
		gflog_notice(GFARM_MSG_1004399,
		    "%s: all clients finished", diag);
		cleanup(0);
		exit(0);
	}
	if (rv == sizeof(*req))
		return;

	if (rv == -1) {
		fatal(GFARM_MSG_1004400,
		    "%s: receiving write_verify request: %s",
		    diag, strerror(errno));
	} else {
		fatal(GFARM_MSG_1004401,
		    "%s: receiving write_verify request: "
		    "partial read %zd bytes", diag, rv);
	}
}

/*
 * write_verify_job_req:
 *	from type_write_verify_controller to type_write_verify
 * write_verify_job_reply:
 *	from type_write_verify to type_write_verify_controller
 */

struct write_verify_job_req {
	gfarm_int64_t ino;
	gfarm_uint64_t gen;
};

enum write_verify_job_reply_status {
	WRITE_VERIFY_JOB_REPLY_STATUS_DONE,
	WRITE_VERIFY_JOB_REPLY_STATUS_POSTPONE
};
struct write_verify_job_reply {
	unsigned char status;
};

/* used by type_write_verify_controller */
static int write_verify_job_controller_fd = -1;

/* used by type_write_verify */
static int write_verify_job_verifier_fd = -1;

static void
write_verify_job_req_send(
	gfarm_int64_t ino, gfarm_uint64_t gen, const char *diag)
{
	struct write_verify_job_req jreq;
	int err;

	jreq.ino = ino;
	jreq.gen = gen;
	err = write_nbytes(
	    write_verify_job_controller_fd, &jreq, sizeof(jreq), diag);
	if (err != 0)
		fatal(GFARM_MSG_1004402, "%s: write: %s",
		    diag, strerror(err));
}

static int
write_verify_job_req_recv(struct write_verify_job_req *jreq, const char *diag)
{
	return (read_nbytes(
	    write_verify_job_verifier_fd, jreq, sizeof(*jreq), diag));
}

static void
write_verify_job_reply_send(unsigned char status, const char *diag)
{
	struct write_verify_job_reply jreply;
	int err;

	jreply.status = status;
	err = write_nbytes(
	    write_verify_job_verifier_fd, &jreply, sizeof(jreply), diag);
	if (err == EPIPE) {
		/* type_write_verify_controller exited before this process */
		gflog_info(GFARM_MSG_1004403, "%s: write: %s, end",
		    diag, strerror(err));
		exit(0);
	} else if (err != 0) {
		fatal(GFARM_MSG_1004404, "%s: write: %s",
		    diag, strerror(err));
	}
}

static int
write_verify_job_reply_recv(const char *diag)
{
	struct write_verify_job_reply jreply;

	if (!read_nbytes(
	    write_verify_job_controller_fd, &jreply, sizeof(jreply), diag))
		fatal(GFARM_MSG_1004405,
		    "%s: write_verify process is dead", diag);
	return (jreply.status);
}

/*
 * ring buffer data structure
 */

struct write_verify_entry {
	struct write_verify_req req;
	gfarm_uint64_t schedule;
};

#define RINGBUF_INITIAL_SIZE	512

static struct write_verify_entry *ringbuf_ptr = NULL;
static size_t ringbuf_size = 0;
static size_t ringbuf_n_entries = 0, ringbuf_in = 0, ringbuf_out = 0;
static int ringbuf_overflow = 0;

/* statistics for logging */
static size_t ringbuf_max_entries = 0;

static int
ringbuf_is_empty(void)
{
	return (ringbuf_n_entries == 0);
}

static int
ringbuf_has_room(void)
{
	struct write_verify_entry *new_buffer;
	size_t new_size;
	int overflow;

	if (ringbuf_n_entries < ringbuf_size)
		return (1);

	if (ringbuf_overflow)
		return (0);

	if (ringbuf_size == 0) {
		new_size = RINGBUF_INITIAL_SIZE;
	} else {
		overflow = 0;
		new_size =
		    gfarm_size_add(&overflow, ringbuf_size, ringbuf_size);
		if (overflow) {
			ringbuf_overflow = 1;
			gflog_warning(GFARM_MSG_1004406,
			    "write_verify: integer overflow "
			    "to remember more than %zd entries",
			    ringbuf_size);
			return (0);
		}
	}
	GFARM_REALLOC_ARRAY(new_buffer, ringbuf_ptr, new_size);
	if (new_buffer == NULL) {
		ringbuf_overflow = 1;
		gflog_warning(GFARM_MSG_1004407, "write_verify: "
		    "no memory to remember more than %zd entries",
		    ringbuf_size);
		return (0);
	}
	if (ringbuf_size > 0 && ringbuf_in <= ringbuf_out) {
		int n_move = ringbuf_size - ringbuf_out;
		memmove(&new_buffer[new_size - n_move],
			&new_buffer[ringbuf_size - n_move],
			n_move * sizeof(*ringbuf_ptr));
		ringbuf_out += new_size - ringbuf_size;
	}
	ringbuf_size = new_size;
	ringbuf_ptr = new_buffer;
	return (1);
}

static void
ringbuf_enqueue(const struct write_verify_entry *entry)
{
	int has_room = ringbuf_has_room();

	/* before calling this function, caller side should check this */
	assert(has_room);

	ringbuf_ptr[ringbuf_in] = *entry;
	ringbuf_in = (ringbuf_in + 1) % ringbuf_size;
	++ringbuf_n_entries;
	if (ringbuf_max_entries < ringbuf_n_entries)
		ringbuf_max_entries = ringbuf_n_entries;
}

static void
ringbuf_peek_head(struct write_verify_entry *entry)
{
	/* before calling this function, caller side should check this */
	assert(!ringbuf_is_empty());

	*entry = ringbuf_ptr[ringbuf_out];
}

static void
ringbuf_dequeue(struct write_verify_entry *entry)
{
	/* before calling this function, caller side should check this */
	assert(!ringbuf_is_empty());

	*entry = ringbuf_ptr[ringbuf_out];
	ringbuf_out = (ringbuf_out + 1) % ringbuf_size;
	--ringbuf_n_entries;
}

static gfarm_uint32_t
ringbuf_crc32(void)
{
	gfarm_uint32_t crc32 = 0;
	char *head, *tail;

	if (ringbuf_in > ringbuf_out) {
		head = (char *)&ringbuf_ptr[ringbuf_out];
		tail = (char *)&ringbuf_ptr[ringbuf_in];
		crc32 = gfarm_crc32(crc32, head, tail - head);
	} else if (ringbuf_n_entries > 0) {
		head = (char *)&ringbuf_ptr[ringbuf_out];
		tail = (char *)&ringbuf_ptr[ringbuf_size];
		crc32 = gfarm_crc32(crc32, head, tail - head);
		head = (char *)&ringbuf_ptr[0];
		tail = (char *)&ringbuf_ptr[ringbuf_in];
		crc32 = gfarm_crc32(crc32, head, tail - head);
	}
	return (crc32);
}

static int
ringbuf_write(int fd, const char *diag)
{
	char *head, *tail;
	struct iovec iov[2];
	int n = 0;
	ssize_t rv, sz = 0;

	if (ringbuf_in > ringbuf_out) {
		head = (char *)&ringbuf_ptr[ringbuf_out];
		tail = (char *)&ringbuf_ptr[ringbuf_in];
		iov[n].iov_base = head;
		iov[n].iov_len = tail - head;
		if (iov[n].iov_len > 0) {
			sz += iov[n].iov_len;
			++n;
		}
	} else if (ringbuf_n_entries > 0) {
		head = (char *)&ringbuf_ptr[ringbuf_out];
		tail = (char *)&ringbuf_ptr[ringbuf_size];
		iov[n].iov_base = head;
		iov[n].iov_len = tail - head;
		if (iov[n].iov_len > 0) {
			sz += iov[n].iov_len;
			++n;
		}
		head = (char *)&ringbuf_ptr[0];
		tail = (char *)&ringbuf_ptr[ringbuf_in];
		iov[n].iov_base = head;
		iov[n].iov_len = tail - head;
		if (iov[n].iov_len > 0) {
			sz += iov[n].iov_len;
			++n;
		}
	}
	if (n == 0)
		return (1);
	rv = writev(fd, iov, n);
	if (rv == -1) {
		gflog_error_errno(GFARM_MSG_1004408,
		    "%s: cannot write", diag);
		return (0);
	} else if (rv != sz) {
		gflog_error(GFARM_MSG_1004409,
		    "%s: partial write %zd/%zd", diag, rv, sz);
		return (0);
	}
	return (1);
}

static int
ringbuf_read(int fd, gfarm_uint64_t nrecords, gfarm_uint32_t expected_crc32,
	const char *diag)
{
	ssize_t rv, sz;
	gfarm_uint32_t crc32;

	assert(ringbuf_ptr == NULL && ringbuf_size == 0);

	ringbuf_size = nrecords * 2;
	if (ringbuf_size < nrecords)
		fatal(GFARM_MSG_1004410,
		    "write_verify: state file too big: %lld records",
		    (long long)nrecords);
	if (ringbuf_size < RINGBUF_INITIAL_SIZE)
		ringbuf_size = RINGBUF_INITIAL_SIZE;
	GFARM_MALLOC_ARRAY(ringbuf_ptr, ringbuf_size);
	if (ringbuf_ptr == NULL)
		fatal(GFARM_MSG_1004411,
		    "write_verify: no memory for %zd records", ringbuf_size);

	sz = nrecords * sizeof(*ringbuf_ptr);
	rv = read(fd, ringbuf_ptr, sz);
	if (rv == -1) {
		gflog_error_errno(GFARM_MSG_1004412,
		    "%s: cannot read", diag);
		return (0);
	} else if (rv != sz) {
		/*
		 * In traditional UNIX varints including Linux, the read(2)
		 * syscall against a local regular file without O_NONBLOCK
		 * guarantees to read the number of bytes requested,
		 * unless it reaches EOF or an I/O error happens.
		 */
		gflog_error(GFARM_MSG_1004413,
		    "%s: partial read %zd/%zd", diag, rv, sz);
		return (0);
	}
	crc32 = gfarm_crc32(0, ringbuf_ptr, rv);
	if (crc32 != expected_crc32) {
		gflog_error(GFARM_MSG_1004414,
		    "%s: bad CRC 0x%08X, should be 0x%08X",
		    diag, crc32, expected_crc32);
		return (0);
	}
	ringbuf_n_entries = nrecords;
	if (ringbuf_max_entries < ringbuf_n_entries)
		ringbuf_max_entries = ringbuf_n_entries;
	ringbuf_in = nrecords;
	return (1);
}

static void
ringbuf_free(void)
{
	free(ringbuf_ptr);
	ringbuf_ptr = NULL;
	ringbuf_size = ringbuf_n_entries = ringbuf_in = ringbuf_out = 0;
	ringbuf_overflow = 0;
}

/*
 * write_verify_mtime_rec data structure
 */

struct write_verify_mtime_rec {
	RB_ENTRY(write_verify_mtime_rec) rb_node;

	gfarm_uint64_t mtime, count;
};

static RB_HEAD(write_verify_mtime_tree, write_verify_mtime_rec) mtime_tree =
	RB_INITIALIZER(mtime_tree);

static int
mtime_rec_compare(
	struct write_verify_mtime_rec *a,
	struct write_verify_mtime_rec *b)
{
	if (a->mtime < b->mtime)
		return (-1);
	else if (a->mtime > b->mtime)
		return (1);
	else
		return (0);
}

RB_PROTOTYPE(write_verify_mtime_tree, write_verify_mtime_rec, \
	rb_node, mtime_rec_compare)
RB_GENERATE(write_verify_mtime_tree, write_verify_mtime_rec, \
	rb_node, mtime_rec_compare)

static void
mtime_rec_add(gfarm_uint64_t mtime)
{
	struct write_verify_mtime_rec *mtime_rec, *found;
	static struct write_verify_mtime_rec *next = NULL;

	if (next == NULL) {
		GFARM_MALLOC(next);
		if (next == NULL)
			fatal(GFARM_MSG_1004415,
			    "write_verify: no memory for %zdth entry",
			    ringbuf_n_entries);
	}
	mtime_rec = next;
	mtime_rec->mtime = mtime;
	mtime_rec->count = 1;
	found = RB_INSERT(write_verify_mtime_tree, &mtime_tree, mtime_rec);
	if (found != NULL) {
		++found->count;
		return; /* `next' was not used, let it be as is */
	}

	/*
	 * to make ringbuf_has_room() work for struct write_verify_mtime_rec
	 */
	GFARM_MALLOC(next);
	if (next == NULL && !ringbuf_overflow) {
		ringbuf_overflow = 1;
		gflog_warning(GFARM_MSG_1004416, "write_verify: "
		    "no memory to reserve %zdth entry",
		    ringbuf_n_entries + 1);
	}
}

static struct write_verify_mtime_rec *
mtime_rec_find(time_t mtime)
{
	struct write_verify_mtime_rec target;

	target.mtime = mtime;
	return (RB_FIND(write_verify_mtime_tree, &mtime_tree, &target));
}

static void
mtime_rec_remove(struct write_verify_mtime_rec *mtime_rec)
{
	struct write_verify_mtime_rec *deleted;

	if (mtime_rec->count > 1) {
		--mtime_rec->count;
		return;
	}

	deleted = RB_REMOVE(write_verify_mtime_tree, &mtime_tree, mtime_rec);
	if (deleted == NULL)
		fatal(GFARM_MSG_1004417,
		    "write_verify, removing mtime:%llu failed",
		    (long long)mtime_rec->mtime);

	free(deleted);
}

static struct write_verify_mtime_rec *
mtime_rec_oldest(void)
{
	return (RB_MIN(write_verify_mtime_tree, &mtime_tree));
}

static void
mtime_tree_free(void)
{
	struct write_verify_mtime_rec *rec;

	while ((rec = RB_MIN(write_verify_mtime_tree, &mtime_tree)) != NULL) {
		RB_REMOVE(write_verify_mtime_tree, &mtime_tree, rec);
		free(rec);
	}
}

static int
write_verify_entry_load(
	int fd, gfarm_uint64_t nrecords, gfarm_uint32_t expected_crc32,
	const char *diag)
{
	size_t i;

	if (!ringbuf_read(fd, nrecords, expected_crc32, diag))
		return (0);

	assert(ringbuf_out == 0);

	for (i = 0; i < ringbuf_n_entries; i++)
		mtime_rec_add(ringbuf_ptr[i].req.mtime);
	return (1);
}

/*
 * functions which run on type_write_verify
 */

static void
write_verify_calc_report(gfarm_int64_t ino, gfarm_uint64_t gen,
	gfarm_off_t calc_len,
	const gfarm_timerval_t *t1, const gfarm_timerval_t *t2,
	const char *result, const char *aux)
{
	const char *a1, *a2, *a3;
	time_t current_time = time(NULL);
	struct tm *tp = localtime(&current_time);
	double t = gfarm_timerval_sub(t2, t1);
	char end_time[TIMEBUF_SIZE];

	strftime(end_time, sizeof end_time, TIMEBUF_FMT, tp);
	if (aux == NULL) {
		a1 = a2 = a3 = "";
	} else {
		a1 = " (";
		a2 = aux;
		a3 = ")";
	}
	gflog_info(GFARM_MSG_1004501, "write_verified at %s total_time %g "
	    "inum %lld gen %lld size %lld: %s%s%s%s",
	    end_time, t, (long long)ino, (long long)gen,
	    (long long)calc_len, result, a1, a2, a3);
}

static void
replica_lost_move_to_lost_found_by_fd(gfarm_ino_t ino, gfarm_uint64_t gen,
	int local_fd, const char *diag)
{
	struct stat st;

	if (fstat(local_fd, &st) == -1) {
		gflog_error_errno(GFARM_MSG_1004418, "%s: fstat(%lld:%lld)",
		    diag, (long long)ino, (long long)gen);
		return;
	}
	replica_lost_move_to_lost_found(ino, gen, local_fd, st.st_size);
}

#ifdef O_DIRECT
#define WRITE_VERIFY_OPEN_MODE	(O_RDONLY|O_DIRECT)
/* larger buffer size is better, because direct I/O doesn't do read-ahead */
#define WRITE_VERIFY_BUFSIZE	(1024*1024)
#else
#define WRITE_VERIFY_OPEN_MODE	(O_RDONLY)
/* small size is better, because of read-ahead */
#define WRITE_VERIFY_BUFSIZE	65536
#endif

#define LINUX_RAWIO_ALIGNMENT	512	/* Linux O_DIRECT feature needs this */

static char write_verify_buf[WRITE_VERIFY_BUFSIZE + LINUX_RAWIO_ALIGNMENT - 1];

static void
write_verify_calc_cksum(gfarm_int64_t ino, gfarm_uint64_t gen)
{
	gfarm_error_t e;
	char *buffer, *path;
	int local_fd, save_errno;
	gfarm_uint64_t open_status;

	char *cksum_type = NULL;
	gfarm_int32_t got_cksum_flags;
	size_t got_cksum_len, cksum_len;
	char got_cksum[GFM_PROTO_CKSUM_MAXLEN];
	char cksum[GFARM_MSGDIGEST_STRSIZE];
	gfarm_off_t calc_len;
	gfarm_timerval_t t1, t2;
	static const char diag[] = "write_verify_calc_cksum";

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1););

	/*
	 * do this regardless of whether it's linux or not,
	 * for some OS (or device) which may have similar restriction
	 */
	buffer = (char *)(
	    ((uintptr_t)write_verify_buf + (LINUX_RAWIO_ALIGNMENT - 1))
	    & ~(uintptr_t)(LINUX_RAWIO_ALIGNMENT - 1));

	for (;;) {
		e = gfm_client_replica_get_cksum(gfm_server, ino, gen,
		    &cksum_type, sizeof(got_cksum), &got_cksum_len,
		    got_cksum, &got_cksum_flags);
		if (!IS_CONNECTION_ERROR(e))
			break;
		free_gfm_server();
		if ((e = connect_gfm_server(diag)) != GFARM_ERR_NO_ERROR)
			fatal(GFARM_MSG_1004419, "die");
	}
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_NO_SUCH_OBJECT) { /* already removed */
			gflog_debug(GFARM_MSG_1004420,
			    "%s: %lld:%lld: already removed",
			    diag, (long long)ino, (long long)gen);
			gfs_profile(
				gfarm_gettimerval(&t2);
				write_verify_calc_report(ino, gen, 0, &t1, &t2,
				    "skipped", "updated before calculation");
			);
		} else {
			gflog_warning(GFARM_MSG_1004421, "%s: %lld:%lld: %s",
			    diag, (long long)ino, (long long)gen,
			    gfarm_error_string(e));
			gfs_profile(
				gfarm_gettimerval(&t2);
				write_verify_calc_report(ino, gen, 0, &t1, &t2,
				    "skipped", gfarm_error_string(e));
			);
		}
		write_verify_job_reply_send(
		    WRITE_VERIFY_JOB_REPLY_STATUS_DONE, diag);
		free(cksum_type);
		return;
	}
	if ((got_cksum_flags & GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED) != 0) {
		gflog_info(GFARM_MSG_1004422,
		    "%s: %lld:%lld: opened for write. postponed",
		    diag, (long long)ino, (long long)gen);
		gfs_profile(
			gfarm_gettimerval(&t2);
			write_verify_calc_report(ino, gen, 0, &t1, &t2,
			    "postpone", "opened for write before calculation");
		);
		write_verify_job_reply_send(
		    WRITE_VERIFY_JOB_REPLY_STATUS_POSTPONE, diag);
		free(cksum_type);
		return;
	}
	/* NOTE: maybe got_cksum_len == 0 here, if checksum is not set */

	gfsd_local_path(ino, gen, diag, &path);
	local_fd = open_data(path, WRITE_VERIFY_OPEN_MODE);
	save_errno = errno;
	free(path);
	if (local_fd == -1) {
		gflog_debug(GFARM_MSG_1004423,
		    "%s: %lld:%lld: must be generation updated: %s",
		    diag, (long long)ino, (long long)gen,
		    strerror(save_errno));
		gfs_profile(
			gfarm_gettimerval(&t2);
			write_verify_calc_report(ino, gen, 0, &t1, &t2,
			    "open_fail", strerror(save_errno));
		);
		write_verify_job_reply_send(
		    WRITE_VERIFY_JOB_REPLY_STATUS_DONE, diag);
		free(cksum_type);
		return;
	}

	e = calc_digest(local_fd, cksum_type, cksum, &cksum_len, &calc_len,
	    buffer, WRITE_VERIFY_BUFSIZE, diag, ino, gen);

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t2);
	gfs_profile(gfarm_gettimerval(&t2););

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004424,
		    "%s: %lld:%lld: checksum calculation failed: %s",
		    diag, (long long)ino, (long long)gen,
		    gfarm_error_string(e));
		gfs_profile(write_verify_calc_report(ino, gen, 0, &t1, &t2,
		    "calculation_fail", gfarm_error_string(e)););
		write_verify_job_reply_send(
		    WRITE_VERIFY_JOB_REPLY_STATUS_DONE, diag);
		close(local_fd);
		free(cksum_type);
		return;
	}

	if (got_cksum_len == 0) { /* cksum was not set */
		for (;;) {
			e = gfm_client_fhset_cksum(gfm_server, ino, gen,
			    cksum_type, cksum_len, cksum, 0);
			if (!IS_CONNECTION_ERROR(e))
				break;
			free_gfm_server();
			if ((e = connect_gfm_server(diag))
			    != GFARM_ERR_NO_ERROR)
				fatal(GFARM_MSG_1004425, "die");
		}
		if (e == GFARM_ERR_FILE_BUSY) {
			gflog_info(GFARM_MSG_1004426,
			    "%s: %lld:%lld: opened for write. postponed",
			    diag, (long long)ino, (long long)gen);
			gfs_profile(write_verify_calc_report(ino, gen,
			    calc_len, &t1, &t2,
			    "postpone", "opened for write w/o cksum"););
			write_verify_job_reply_send(
			    WRITE_VERIFY_JOB_REPLY_STATUS_POSTPONE, diag);
			close(local_fd);
			free(cksum_type);
			return;
		}
		if (e == GFARM_ERR_NO_ERROR) {
			gflog_notice(GFARM_MSG_1004427, "%s: inode %lld:%lld: "
			    "checksum set to <%s>:<%.*s> by write_verify",
			    diag, (long long)ino, (long long)gen,
			    cksum_type, (int)cksum_len, cksum);
			gfs_profile(write_verify_calc_report(ino, gen,
			    calc_len, &t1, &t2,
			    "cksum_set", NULL););
		} else if (e == GFARM_ERR_NO_SUCH_OBJECT) { /* updated */
			gflog_debug(GFARM_MSG_1004428,
			    "%s: %lld:%lld: already updated",
			    diag, (long long)ino, (long long)gen);
			gfs_profile(write_verify_calc_report(ino, gen,
			    calc_len, &t1, &t2,
			    "skipped", "updated w/o cksum"););
		} else {
			gflog_warning(GFARM_MSG_1004429, "%s: %lld:%lld: %s",
			    diag, (long long)ino, (long long)gen,
			    gfarm_error_string(e));
			gfs_profile(write_verify_calc_report(ino, gen,
			    calc_len, &t1, &t2,
			    "skipped", gfarm_error_string(e)););
		}
		write_verify_job_reply_send(
		    WRITE_VERIFY_JOB_REPLY_STATUS_DONE, diag);
		close(local_fd);
		free(cksum_type);
		return;
	}
	free(cksum_type);
	if (cksum_len == got_cksum_len &&
	    memcmp(cksum, got_cksum, cksum_len) == 0) {
		assert(cksum_len > 0);
		gflog_debug(GFARM_MSG_1004430,
		    "%s: %lld:%lld: cksum <%.*s> ok",
		    diag, (long long)ino, (long long)gen,
		    (int)cksum_len, cksum);
		gfs_profile(write_verify_calc_report(ino, gen,
		    calc_len, &t1, &t2, "ok", NULL);)
		write_verify_job_reply_send(
		    WRITE_VERIFY_JOB_REPLY_STATUS_DONE, diag);
		close(local_fd);
		return;
	}
	for (;;) {
		e = gfm_client_replica_open_status(gfm_server, ino, gen,
		    &open_status);
		if (!IS_CONNECTION_ERROR(e))
			break;
		free_gfm_server();
		if ((e = connect_gfm_server(diag)) != GFARM_ERR_NO_ERROR)
			fatal(GFARM_MSG_1004431, "die");
	}
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_NO_SUCH_OBJECT) { /* generation updated */
			gflog_debug(GFARM_MSG_1004432,
			    "%s: %lld:%lld: generation updated",
			    diag, (long long)ino, (long long)gen);
			gfs_profile(write_verify_calc_report(ino, gen,
			    calc_len, &t1, &t2, "skipped", "updated"););
		} else {
			gflog_warning(GFARM_MSG_1004433, "%s: %lld:%lld: %s",
			    diag, (long long)ino, (long long)gen,
			    gfarm_error_string(e));
			gfs_profile(write_verify_calc_report(ino, gen,
			    calc_len, &t1, &t2,
			    "skipped", gfarm_error_string(e)););
		}
		write_verify_job_reply_send(
		    WRITE_VERIFY_JOB_REPLY_STATUS_DONE, diag);
		close(local_fd);
		return;
	}

	if ((open_status & GFM_PROTO_REPLICA_OPENED_WRITE) != 0) {
		gflog_debug(GFARM_MSG_1004434,
		    "%s: %lld:%lld: opened for write. postponed",
		    diag, (long long)ino, (long long)gen);
		gfs_profile(write_verify_calc_report(ino, gen,
		    calc_len, &t1, &t2,
		    "postpone", "opened for write"););
		write_verify_job_reply_send(
		    WRITE_VERIFY_JOB_REPLY_STATUS_POSTPONE, diag);
		close(local_fd);
		return;
	}
	gflog_error(GFARM_MSG_1004435,
	    "%s: %lld:%lld: checksum mismatch <%.*s> should be <%.*s>", diag,
	    (long long)ino, (long long)gen, (int)cksum_len, cksum,
	    (int)got_cksum_len, got_cksum);
	replica_lost_move_to_lost_found_by_fd(ino, gen, local_fd, diag);
	close(local_fd);
	gfs_profile(write_verify_calc_report(ino, gen, calc_len, &t1, &t2,
	    "mismatch", NULL););
	write_verify_job_reply_send(WRITE_VERIFY_JOB_REPLY_STATUS_DONE, diag);
}


static void
write_verify(void)
{
	gfarm_error_t e;
	struct write_verify_job_req jreq;
	static const char diag[] = "write_verify";

	if ((e = connect_gfm_server("write_verify")) != GFARM_ERR_NO_ERROR)
		fatal(GFARM_MSG_1004436, "die");

	for (;;) {
		wait_fd_with_failover_pipe(
		    write_verify_job_verifier_fd, diag);

		if (!write_verify_job_req_recv(&jreq, diag)) {
			gflog_info(GFARM_MSG_1004437, "%s: end", diag);
			exit(0);
		}
		write_verify_calc_cksum(jreq.ino, jreq.gen);
	}
}

/*
 * var/gfarm-spool/state/write_verify handling
 */

#define WRITE_VERIFY_STATE_FILE_NAME	"state/write_verify"
#define WRITE_VERIFY_STATE_FILE_BAK	"state/write_verify.bak"
#define WRITE_VERIFY_STATE_FILE_TMP	"state/write_verify.tmp"
#define WRITE_VERIFY_STATE_FILE_MAGIC	\
	(('G' << 24) | ('f' << 16) | ('S' << 8) | ('s'))

struct write_verify_state_header {
	gfarm_uint32_t magic;
	gfarm_uint32_t crc32;
	gfarm_uint64_t n_records;
};

static int write_verify_state_fd = -1;
static char *write_verify_state_file;
static char *write_verify_state_tmp;

static void
write_verify_state_snapshot(void)
{
	struct write_verify_mtime_rec *oldest_mtime = mtime_rec_oldest();
	struct timeval tv[2];

	if (oldest_mtime == NULL) {
		gettimeofday(&tv[0], NULL);
	} else {
		tv[0].tv_sec = oldest_mtime->mtime;
		tv[0].tv_usec = 0;
	}
	tv[1] = tv[0];
	if (utimes(write_verify_state_file, tv) == -1)
		gflog_info_errno(GFARM_MSG_1004438,
		    "recording mtime to %s", write_verify_state_file);
	if (fsync(write_verify_state_fd) == -1)
		gflog_error_errno(GFARM_MSG_1004439,
		    "fsync(%s)", write_verify_state_file);
}

static void
write_verify_state_save(void)
{
	struct write_verify_state_header header;
	ssize_t rv;
	int fd = open(write_verify_state_tmp, O_CREAT|O_WRONLY|O_TRUNC, 0600);

	if (fd == -1) {
		gflog_error_errno(GFARM_MSG_1004440,
		    "write_verify: open(%s)", write_verify_state_tmp);
		return;
	}
	header.magic = WRITE_VERIFY_STATE_FILE_MAGIC;
	header.crc32 = ringbuf_crc32();
	header.n_records = ringbuf_n_entries;
	rv = write(fd, &header, sizeof(header));
	if (rv != sizeof(header)) {
		if (rv == -1) {
			gflog_error_errno(GFARM_MSG_1004441,
			    "write_verify: writing %s",
			    write_verify_state_tmp);
		} else {
			/*
			 * In traditional UNIX varints including Linux,
			 * the write(2) syscall against a local regular
			 * file without O_NONBLOCK guarantees to write
			 * the number of bytes requested,
			 * unless disk full or an I/O error happens.
			 */
			gflog_error(GFARM_MSG_1004442,
			    "write_verify: writing %s: partial write %zd/%zd",
			    write_verify_state_tmp, rv, sizeof(header));
		}
		close(fd);
		unlink(write_verify_state_tmp);
		return;
	}
	if (!ringbuf_write(fd, write_verify_state_tmp)) {
		close(fd);
		unlink(write_verify_state_tmp);
		return;
	}
	if (rename(write_verify_state_tmp, write_verify_state_file)
	    == -1) {
		gflog_error_errno(GFARM_MSG_1004443,
		    "write_verify: renaming %s to %s",
		    write_verify_state_tmp, write_verify_state_file);
		close(fd);
		/* unlink(write_verify_state_tmp); ? */
		return;
	}
	if (fsync(fd) == -1)
		gflog_error_errno(GFARM_MSG_1004444,
		    "write_verify: fsync(%s)", write_verify_state_file);
	close(fd);
}

static int
write_verify_state_load(int fd)
{
	struct write_verify_state_header header;
	ssize_t rv;

	rv = read(fd, &header, sizeof(header));
	if (rv != sizeof(header)) {
		if (rv == -1)
			gflog_error_errno(GFARM_MSG_1004445,
			    "write_verify: reading %s",
			    write_verify_state_file);
		else
			gflog_error(GFARM_MSG_1004446,
			    "write_verify: reading %s: partial read %zd/%zd",
			    write_verify_state_file, rv, sizeof(header));
		return (0);
	}
	if (header.magic != WRITE_VERIFY_STATE_FILE_MAGIC) {
		gflog_error(GFARM_MSG_1004447,
		    "write_verify: %s: bad magic 0x%08X, should be 0x%08X",
		    write_verify_state_file,
		    (int)header.magic, WRITE_VERIFY_STATE_FILE_MAGIC);
		return (0);
	}
	if (!write_verify_entry_load(
	    fd, header.n_records, header.crc32, write_verify_state_file))
		return (0);

	return (1);
}

static void
write_verify_report_crash(time_t crash_time)
{
	time_t current_time = time(NULL);
	struct tm *tp;
	char crash[TIMEBUF_SIZE], current[TIMEBUF_SIZE];

	tp = localtime(&crash_time);
	strftime(crash, sizeof crash, TIMEBUF_FMT, tp);
	tp = localtime(&current_time);
	strftime(current, sizeof current, TIMEBUF_FMT, tp);
	gflog_error(GFARM_MSG_1004448,
	    "write_verify: gfsd crashed previously.  "
	    "please calculate checksum since %s until %s (%lld..%lld)",
	    crash, current, (long long)crash_time, (long long)current_time);
}

/* called from type_listener */
void
write_verify_state_init(void)
{
	int fd;
	char *bakfile, *tmpfile;
	struct stat st;
	static const char diag[] = "write_verify";

	write_verify_state_file =
	    gfsd_make_path(WRITE_VERIFY_STATE_FILE_NAME, diag);
	fd = open(write_verify_state_file, O_RDWR);

	if (!gfarm_write_verify) {
		if (fd == -1)
			return; /* just OK */
		close(fd);
		bakfile = gfsd_make_path(WRITE_VERIFY_STATE_FILE_BAK, diag);
		gflog_error(GFARM_MSG_1004449,
		    "write_verify is disabled, but old state file %s exists.  "
		    "renamed to %s", write_verify_state_file, bakfile);
		if (rename(write_verify_state_file, bakfile) == -1)
			gflog_error_errno(GFARM_MSG_1004450,
			    "renaming %s to %s",
			    write_verify_state_file, bakfile);
		free(bakfile);
		return;
	}

	/* allocate this memory before loading ringbuf */
	tmpfile = gfsd_make_path(WRITE_VERIFY_STATE_FILE_TMP, diag);

	if (fd == -1) {
		gflog_info(GFARM_MSG_1004451,
		    "%s: does not exist.  creating", write_verify_state_file);
	} else {
		if (fstat(fd, &st) == -1) {
			gflog_error(GFARM_MSG_1004452,
			    "fstat(%s): %s.  write_verify is disabled",
			    write_verify_state_file, strerror(errno));
			gfarm_write_verify = 0;
			free(tmpfile);
			close(fd);
			return;
		}
		if (st.st_size == 0) {
			write_verify_report_crash(st.st_mtime);
		} else if (!write_verify_state_load(fd)) {
			close(fd);
			fd = -1;
			bakfile =
			    gfsd_make_path(WRITE_VERIFY_STATE_FILE_BAK, diag);
			gflog_error(GFARM_MSG_1004453,
			    "incomplete state file %s is renamed to %s",
			    write_verify_state_file, bakfile);
			if (rename(write_verify_state_file, bakfile) == -1)
				gflog_error_errno(GFARM_MSG_1004454,
				    "renaming %s to %s",
				    write_verify_state_file, bakfile);
			free(bakfile);
		} else { /* old state is successfully loaded */
			close(fd);
			fd = -1;
		}
	}
	if (fd == -1) {
		if (gfsd_create_ancestor_dir(write_verify_state_file) == -1) {
			gflog_error(GFARM_MSG_1004455,
			    "write_verify is disabled");
			gfarm_write_verify = 0;
			free(tmpfile);
			return;
		}
		fd = open(write_verify_state_file,
		    O_CREAT|O_RDWR|O_TRUNC, 0600);
		if (fd == -1) {
			gflog_error(GFARM_MSG_1004456,
			    "failed to create %s: %s.  "
			    "write_verify is disabled",
			    write_verify_state_file, strerror(errno));
			gfarm_write_verify = 0;
			free(tmpfile);
			return;
		}
	}
	write_verify_state_fd = fd;
	write_verify_state_tmp = tmpfile;
	write_verify_state_snapshot();
}

/* called from type_listener */
void
write_verify_state_free(void)
{

	mtime_tree_free();
	ringbuf_free();
}

/*
 * functions which run on type_write_verify_controller
 *
 * NOTE: type_write_verify_controller won't access gfmd
 */

static int cleanup_notify_recv_fd = -1;
static int cleanup_notify_send_fd = -1;

static pid_t write_verify_gfsd_pid = -1;

/* statistics for logging */
static gfarm_int64_t total_requests = 0;
static gfarm_int64_t total_processed = 0;
static gfarm_int64_t total_retries = 0;
static time_t last_report;

static void
write_verify_requests_report(int force)
{
	struct write_verify_mtime_rec *oldest_mtime;
	time_t oldest_time, current_time = time(NULL);
	static gfarm_int64_t last_requests = 0;
	static gfarm_int64_t last_processed = 0;
	static gfarm_int64_t last_retries = 0;
	struct tm *tp;
	char oldest[TIMEBUF_SIZE], current[TIMEBUF_SIZE];

	if (!force) {
		if (gfarm_write_verify_log_interval != 0 &&
		    current_time <
		    last_report + gfarm_write_verify_log_interval)
			return;
	}

	oldest_mtime = mtime_rec_oldest();
	if (oldest_mtime == NULL) {
		oldest_time = 0;
		strcpy(oldest, "none");
	} else {
		oldest_time = oldest_mtime->mtime;
		tp = localtime(&oldest_time);
		strftime(oldest, sizeof oldest, TIMEBUF_FMT, tp);
	}

	tp = localtime(&current_time);
	strftime(current, sizeof current, TIMEBUF_FMT, tp);

	gflog_info(GFARM_MSG_1004502, "write_verify_requests at %s "
	    "oldest %s (%lld) pending %zd max_pending %zd | "
	    "total_requests %lld total_processed %lld total_retries %lld | "
	    "requests %lld processed %lld retries %lld within %lld seconds",
	    current, oldest, (long long)oldest_time,
	    ringbuf_n_entries, ringbuf_max_entries,
	    (long long)total_requests, (long long)total_processed,
	    (long long)total_retries,
	    (long long)(total_requests - last_requests),
	    (long long)(total_processed - last_processed),
	    (long long)(total_retries - last_retries),
	    (long long)(current_time - last_report));

	last_requests = total_requests;
	last_processed = total_processed;
	last_processed = total_retries;
	last_report = current_time;
}

static void
write_verify_start_report(struct write_verify_entry *todo_entry)
{
	struct write_verify_mtime_rec *oldest_mtime;
	time_t oldest_time, todo_time, current_time = time(NULL);
	struct tm *tp;
	char oldest[TIMEBUF_SIZE], todo[TIMEBUF_SIZE], current[TIMEBUF_SIZE];

	oldest_mtime = mtime_rec_oldest();
	if (oldest_mtime == NULL) { /* shouldn't happen, but for safety */
		oldest_time = 0;
		strcpy(oldest, "none");
	} else {
		oldest_time = oldest_mtime->mtime;
		tp = localtime(&oldest_time);
		strftime(oldest, sizeof oldest, TIMEBUF_FMT, tp);
	}

	todo_time = todo_entry->req.mtime;
	tp = localtime(&todo_time);
	strftime(todo, sizeof todo, TIMEBUF_FMT, tp);

	tp = localtime(&current_time);
	strftime(current, sizeof current, TIMEBUF_FMT, tp);

	gflog_info(GFARM_MSG_1004503, "write_verify_starting at %s "
	    "inum %lld gen %lld mtime %s (%lld) "
	    "oldest %s (%lld) pending %zd max_pending %zd",
	    current,
	    (long long)todo_entry->req.ino, (long long)todo_entry->req.gen,
	    todo, (long long)todo_time, oldest, (long long)oldest_time,
	    ringbuf_n_entries, ringbuf_max_entries);
}

static void
write_verify_request_get(const char *diag)
{
	struct write_verify_entry entry;

	assert(ringbuf_has_room());

	write_verify_request_recv(&entry.req, diag);

	entry.schedule = entry.req.mtime + gfarm_write_verify_interval;
	ringbuf_enqueue(&entry);
	mtime_rec_add(entry.req.mtime);
	++total_requests;

	write_verify_state_snapshot();

	write_verify_requests_report(0);
}

static void
write_verify_request_abandon(const char *diag)
{
	struct write_verify_entry entry;

	write_verify_request_recv(&entry.req, diag);
	gflog_error(GFARM_MSG_1004485,
	    "please run gfspooldigest: inode %lld:%lld is written at %lld, "
	    "but not write_verified due to memory shortage",
	    (long long)entry.req.ino, (long long)entry.req.gen,
	    (long long)entry.req.mtime);
}

void
write_verify_controller_cleanup(void)
{
	const char diag[] = "write_verify_controller_cleanup";

	while (fd_is_ready(write_verify_request_recv_fd, diag)) {
		if (ringbuf_has_room())
			write_verify_request_get(diag);
		else
			write_verify_request_abandon(diag);
	}
	/*
	 * NOTE: there is race condition here that the requests
	 * in the write_verify_request pipe may not be logged
	 */
	write_verify_state_save();

	write_verify_requests_report(1);
}

static void
cleanup_notified(const char *diag)
{
	fd_event_notified(cleanup_notify_recv_fd, 1, "cleanup", diag);
}

void
write_verify_controller_cleanup_signal(void)
{
	fd_event_notify(cleanup_notify_send_fd);
}

static void
cleanup_notify_setup(void)
{
	int pipefds[2];

	if (pipe(pipefds) == -1)
		fatal(GFARM_MSG_1004486, "pipe after fork: %s",
		    strerror(errno));
	cleanup_notify_recv_fd = pipefds[0];
	cleanup_notify_send_fd = pipefds[1];
}

static void
write_verify_job_done(struct write_verify_entry *todo,
	struct write_verify_mtime_rec *mtime_rec)
{
	int status;
	struct write_verify_entry tmp;
	static const char diag[] = "write_verify_controller";

	status = write_verify_job_reply_recv(diag);
	ringbuf_dequeue(&tmp);
	assert(
	    tmp.req.ino == todo->req.ino &&
	    tmp.req.gen == todo->req.gen &&
	    tmp.req.mtime == todo->req.mtime &&
	    tmp.schedule == todo->schedule);
	switch (status) {
	case WRITE_VERIFY_JOB_REPLY_STATUS_DONE:
		mtime_rec_remove(mtime_rec);
		++total_processed;
		write_verify_state_snapshot();
		break;
	case WRITE_VERIFY_JOB_REPLY_STATUS_POSTPONE:
		tmp.schedule = time(NULL) + gfarm_write_verify_retry_interval;
		ringbuf_enqueue(&tmp);
		++total_retries;
		break;
	default:
		assert(0);
	}
}

static void
write_verify_controller(void)
{
	enum { WRITE_VERIFY_IDLE, WRITE_VERIFY_RESERVED, WRITE_VERIFY_RUNNING }
	    state = WRITE_VERIFY_IDLE;

	/* the followings are only available if state != WRITE_VERIFY_IDLE */
	struct write_verify_entry todo;
	struct write_verify_mtime_rec *mtime_rec = NULL;
	static const char diag[] = "write_verify_controller";

	last_report = time(NULL);
	write_verify_requests_report(1); /* report records in the state file */

	cleanup_notify_setup();

	for (;;) {
		time_t now = time(NULL);
		int rv, has_room;

		if (state == WRITE_VERIFY_IDLE && !ringbuf_is_empty()) {
			ringbuf_peek_head(&todo);
			mtime_rec = mtime_rec_find(todo.req.mtime);
			assert(mtime_rec != NULL);
			state = WRITE_VERIFY_RESERVED;
		}
		if (state == WRITE_VERIFY_RESERVED && todo.schedule <= now) {
			write_verify_job_req_send(
			    todo.req.ino, todo.req.gen, diag);
			state = WRITE_VERIFY_RUNNING;
			gfs_profile(write_verify_start_report(&todo););
		}
		has_room = ringbuf_has_room();
		if (has_room && state == WRITE_VERIFY_RUNNING) {
			rv = wait_3fds(write_verify_request_recv_fd,
			    write_verify_job_controller_fd,
			    cleanup_notify_recv_fd, diag);
			if ((rv & 1) != 0)
				write_verify_request_get(diag);
			if ((rv & 2) != 0) {
				write_verify_job_done(&todo, mtime_rec);
				state = WRITE_VERIFY_IDLE;
				mtime_rec = NULL; /* unnecessary, to be sure */
			}
			if ((rv & 4) != 0)
				break;
		} else if (has_room) {
			rv = timedwait_2fds(write_verify_request_recv_fd,
			    cleanup_notify_recv_fd,
			    state == WRITE_VERIFY_IDLE ? TIMEDWAIT_INFINITE :
			    todo.schedule - now, diag);
			if ((rv & 1) != 0)
				write_verify_request_get(diag);
			if ((rv & 2) != 0)
				break;
		} else if (state == WRITE_VERIFY_RUNNING) {
			/* only check job_reply, due to !ringbuf_has_room() */
			rv = wait_2fds(write_verify_job_controller_fd,
			    cleanup_notify_recv_fd, diag);
			if ((rv & 1) != 0)
				write_verify_job_done(&todo, mtime_rec);
			if ((rv & 2) != 0)
				break;
			state = WRITE_VERIFY_IDLE;
			mtime_rec = NULL; /* unnecessary, to be sure */
		} else {
			/*
			 * if state == IDLE, has_room must be true,
			 * because ringbuf_is_empty().
			 */
			assert(state == WRITE_VERIFY_RESERVED);
			assert(todo.schedule > now);
			if (timedwait_fd(cleanup_notify_recv_fd,
			    todo.schedule - now, diag))
				break;
		}
	}
	cleanup_notified(diag);
	if (kill(write_verify_gfsd_pid, SIGTERM) == -1)
		gflog_warning_errno(GFARM_MSG_1004487,
		    "kill(write_verify:%ld)",
		    (long)write_verify_gfsd_pid);
	cleanup(0);
	exit(0);
}

void
start_write_verify_controller(void)
{
	pid_t pid;
	int pipefds[2], sockfds[2];

	/* assert(my_type == type_listener); */
	assert(sizeof(struct write_verify_req) <= PIPE_BUF);
	assert(gfarm_write_verify);

	/* use pipe, because we need the guarantee of PIPE_BUF for this */
	if (pipe(pipefds) == -1)
		fatal(GFARM_MSG_1004457, "pipe for write_verify: %s",
		    strerror(errno));

	pid = do_fork(type_write_verify_controller);
	switch (pid) {
	case -1:
		gflog_error_errno(GFARM_MSG_1004458, "fork");
		break;
	case 0: /* child: type_write_verify_controller */

		gflog_set_auxiliary_info(canonical_self_name);

		if (socketpair(PF_UNIX, SOCK_STREAM, 0, sockfds) == -1)
			fatal(GFARM_MSG_1004459,
			    "socketpair for write_verify: %s",
			    strerror(errno));

		pid = do_fork(type_write_verify);
		switch (pid) {
		case -1:
			fatal(GFARM_MSG_1004460, "fork: %s", strerror(errno));
			/*NOTREACHED*/
			break;
		case 0: /* child: type_write_verify */
			close(pipefds[0]);
			close(pipefds[1]);
			write_verify_job_verifier_fd = sockfds[0];
			close(sockfds[1]);
			write_verify();
			/*NOTREACHED*/
			break;
		default: /* parent: type_write_verify_controller */
			write_verify_request_recv_fd = pipefds[0];
			close(pipefds[1]);
			close(sockfds[0]);
			write_verify_job_controller_fd = sockfds[1];
			write_verify_gfsd_pid = pid;
			write_verify_controller();
			/*NOTREACHED*/
			break;
		}
		break;
	default: /* parent: type_listener */
		close(pipefds[0]);
		write_verify_request_send_fd = pipefds[1];
		break;
	}
}
