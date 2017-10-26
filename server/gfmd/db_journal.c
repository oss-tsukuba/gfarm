/*
 * $Id$
 */

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "queue.h"
#include "gfutil.h"
#include "thrsubr.h"
#ifdef DEBUG_JOURNAL
#include "timer.h"
#endif

#include "xattr_info.h"
#include "quota_info.h"
#include "quota.h"
#include "metadb_server.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#ifdef DEBUG_JOURNAL
#include "gfs_profile.h"
#endif
#include "config.h"
#include "gfm_proto.h"

#include "subr.h"
#include "journal_file.h"
#include "db_access.h"
#include "db_ops.h"
#include "db_journal.h"
/* Do not depend other object files such as host.o, mdhost.o, ... */

#define JOURNAL_W_XDR	journal_file_writer_xdr( \
	journal_file_writer(self_jf))

#define NON_NULL_STR(s) ((s) ? (s) : "")


static struct journal_file	*self_jf;
static const struct db_ops	*journal_apply_ops;
static void			(*db_journal_fail_store_op)(void);
static gfarm_error_t		(*db_journal_sync_op)(gfarm_uint64_t);

struct db_journal_recv_info {
	gfarm_uint64_t from_sn, to_sn;
	unsigned char *recs;
	int recs_len;
	GFARM_STAILQ_ENTRY(db_journal_recv_info) next;
};

static GFARM_STAILQ_HEAD(journal_recvq, db_journal_recv_info) journal_recvq
	= GFARM_STAILQ_HEAD_INITIALIZER(journal_recvq);
static int journal_recvq_nelems = 0;
static int journal_recvq_cancel;
static pthread_mutex_t journal_recvq_mutex;
static pthread_mutex_t journal_seqnum_mutex;
static pthread_cond_t journal_recvq_nonempty_cond;
static pthread_cond_t journal_recvq_nonfull_cond;
static pthread_cond_t journal_recvq_cancel_cond;

static const char RECVQ_MUTEX_DIAG[]		= "journal_recvq_mutex";
static const char SEQNUM_MUTEX_DIAG[]		= "journal_seqnum_mutex";
static const char RECVQ_NONEMPTY_COND_DIAG[]	= "journal_recvq_nonempty_cond";
static const char RECVQ_NONFULL_COND_DIAG[]	= "journal_recvq_nonfull_cond";
static const char RECVQ_CANCEL_COND_DIAG[]	= "journal_recvq_cancel_cond";
static const char DB_ACCESS_MUTEX_DIAG[]	= "db_access_mutex";

static gfarm_uint64_t journal_seqnum = GFARM_METADB_SERVER_SEQNUM_INVALID;
static gfarm_uint64_t journal_seqnum_pre = GFARM_METADB_SERVER_SEQNUM_INVALID;
static int journal_transaction_nesting = 0;
static int journal_begin_called = 0;
static int journal_slave_transaction_nesting = 0;


gfarm_uint64_t
db_journal_next_seqnum(void)
{
	gfarm_uint64_t n;
	static const char diag[] = "db_journal_next_seqnum";

	gfarm_mutex_lock(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
	n = ++journal_seqnum;
	gfarm_mutex_unlock(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
	return (n);
}

gfarm_uint64_t
db_journal_get_current_seqnum(void)
{
	gfarm_uint64_t n;
	static const char diag[] = "db_journal_get_current_seqnum";

	gfarm_mutex_lock(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
	n = journal_seqnum;
	gfarm_mutex_unlock(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
	return (n);
}

static void
db_journal_set_current_seqnum(gfarm_uint64_t sn)
{
	static const char diag[] = "db_journal_set_current_seqnum";

	gfarm_mutex_lock(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
	journal_seqnum = sn;
	gfarm_mutex_unlock(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
}

static gfarm_uint64_t
db_journal_subtract_current_seqnum(int n)
{
	gfarm_uint64_t sn;
	static const char diag[] = "db_journal_subtract_current_seqnum";

	gfarm_mutex_lock(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
	journal_seqnum -= n;
	sn = journal_seqnum;
	gfarm_mutex_unlock(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
	return (sn);
}

static void
db_seqnum_load_callback(void *closure, struct db_seqnum_arg *a)
{
	if (a->name == NULL || strcmp(a->name, DB_SEQNUM_MASTER_NAME) == 0)
		db_journal_set_current_seqnum(a->value);
	free(a->name);
}

static gfarm_error_t
db_journal_noaction(void)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
db_journal_init_status(void)
{
	static const char diag[] = "db_journal_init_status";

	gfarm_mutex_init(&journal_recvq_mutex, diag, RECVQ_MUTEX_DIAG);
	gfarm_mutex_init(&journal_seqnum_mutex, diag, SEQNUM_MUTEX_DIAG);
	gfarm_cond_init(&journal_recvq_nonempty_cond, diag,
	    RECVQ_NONEMPTY_COND_DIAG);
	gfarm_cond_init(&journal_recvq_nonfull_cond, diag,
	    RECVQ_NONEMPTY_COND_DIAG);
	gfarm_cond_init(&journal_recvq_cancel_cond, diag,
	    RECVQ_CANCEL_COND_DIAG);

	return (GFARM_ERR_NO_ERROR);
}

void
db_journal_init_seqnum(void)
{
	gfarm_error_t e;

	if ((e = db_seqnum_load(NULL, db_seqnum_load_callback))
	    != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT) {
		gflog_fatal(GFARM_MSG_1003040,
		    "db_seqnum_load : %s",
		    gfarm_error_string(e));
	}
}

/*
 * the reason why we use this hook instead of directly calling
 * mdhost_master_disconnect_request() is
 * because db_journal.c is used by programs other than gfmd,
 * and we want to keep such programs independent from mdhost.c
 */
static void (*master_disconnect_request)(struct peer *);

void
db_journal_init(void (*disconnect_request)(struct peer *))
{
	gfarm_error_t e;
	char path[MAXPATHLEN + 1];
	const char *journal_dir = gfarm_get_journal_dir();
#ifdef DEBUG_JOURNAL
	gfarm_timerval_t t1, t2;
	double ts;
#endif

	master_disconnect_request = disconnect_request;

	if (journal_dir == NULL) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_fatal(GFARM_MSG_1003039,
		    "gfarm_journal_file_dir is empty : %s",
		    gfarm_error_string(e));
	}

	db_journal_init_seqnum();
	gflog_info(GFARM_MSG_1003503,
	    "db_journal_init_seqnum : seqnum=%llu",
	    (unsigned long long)journal_seqnum);

	snprintf(path, MAXPATHLEN, "%s/%010d.gmj", journal_dir, 0);
#ifdef DEBUG_JOURNAL
	gfs_profile_set();
	gfarm_gettimerval(&t1);
#endif
	if ((e = journal_file_open(path, gfarm_get_journal_max_size(),
	    journal_seqnum, &self_jf, GFARM_JOURNAL_RDWR))
	    != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_1003041,
		    "gfm_server_journal_file_open : %s",
		    gfarm_error_string(e));
	}
#ifdef DEBUG_JOURNAL
	gfarm_gettimerval(&t2);
	ts = gfarm_timerval_sub(&t2, &t1);
	gflog_info(GFARM_MSG_1003042,
	    "journal_file_open : %10.5lf sec", ts);
#endif

	if ((e = db_journal_init_status()) 
	    != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_1003326,
		    "db_journal_init_status : %s",
		    gfarm_error_string(e));
	}
}

void
db_journal_set_apply_ops(const struct db_ops *apply_ops)
{
	journal_apply_ops = apply_ops;
}

void
db_journal_set_fail_store_op(void (*func)(void))
{
	db_journal_fail_store_op = func;
}

void
db_journal_set_sync_op(gfarm_error_t (*func)(gfarm_uint64_t))
{
	db_journal_sync_op = func;
}

gfarm_error_t
db_journal_terminate(void)
{
	store_ops->terminate();
	journal_file_close(self_jf);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_read_string(struct gfp_xdr *xdr, enum journal_operation ope,
	char **strp)
{
	gfarm_error_t e;
	int eof;

	if ((e = gfp_xdr_recv(xdr, 1, &eof, "s", strp))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003043,
		    "gfp_xdr_recv", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_xdr_send_string_array_size_add(size_t *sizep,
	enum journal_operation ope, int n, char **ary)
{
	gfarm_error_t e;
	int i;

	if ((e = gfp_xdr_send_size_add(sizep, "i", n))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003044,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	for (i = 0; i < n; ++i) {
		if ((e = gfp_xdr_send_size_add(sizep, "s",
		    ary[i])) != GFARM_ERR_NO_ERROR) {
			GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003045,
			    "gfp_xdr_send_size_add", e, ope);
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_xdr_send_string_array(
	enum journal_operation ope, int n, char **ary)
{
	gfarm_error_t e;
	int i;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "i", n))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003046,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	for (i = 0; i < n; ++i) {
		if ((e = gfp_xdr_send(JOURNAL_W_XDR, "s",
		    ary[i])) != GFARM_ERR_NO_ERROR) {
			GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003047,
			    "gfp_xdr_send", e, ope);
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_xdr_recv_string_array(struct gfp_xdr *xdr,
	enum journal_operation ope, int *np, char ***aryp)
{
	gfarm_error_t e;
	char **ary;
	int i, j, eof, n;

	*aryp = NULL;
	*np = 0;

	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i", &n))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003048,
		    "gfp_xdr_recv", e, ope);
		return (e);
	}
	GFARM_MALLOC_ARRAY(ary, n + 1);
	if (ary == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003049,
		    "GFARM_MALLOC_ARRAY", e, ope);
		return (e);
	}
	for (i = 0; i < n; ++i) {
		if ((e = gfp_xdr_recv(xdr, 1, &eof, "s",
		    &ary[i])) != GFARM_ERR_NO_ERROR) {
			GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003050,
			    "gfp_xdr_recv", e, ope);
			for (j = 0; j < i; ++j)
				free(ary[i]);
			free(ary);
			return (e);
		}
	}
	ary[n] = NULL;
	*aryp = ary;
	*np = n;
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************/
/* destructor */

static void
db_journal_string_array_free(int n, char **ary)
{
	int i;

	for (i = 0; i < n; ++i)
		free(ary[i]);
	if (ary)
		free(ary);
}

static void
db_journal_host_info_destroy(struct gfarm_host_info *hi)
{
	gfarm_host_info_free(hi);
	free(hi);
}

static void
db_journal_host_modify_arg_destroy(struct db_host_modify_arg *arg)
{
	gfarm_host_info_free(&arg->hi);
	db_journal_string_array_free(arg->add_count, arg->add_aliases);
	db_journal_string_array_free(arg->del_count, arg->del_aliases);
	free(arg);
}

static void
db_journal_fsngroup_modify_arg_destroy(struct db_fsngroup_modify_arg *arg)
{
	free(arg->hostname);
	free(arg->fsngroupname);
	free(arg);
}

static void
db_journal_user_modify_arg_destroy(struct db_user_modify_arg *arg)
{
	gfarm_user_info_free(&arg->ui);
	free(arg);
}

static void
db_journal_user_info_destroy(struct gfarm_user_info *ui)
{
	gfarm_user_info_free(ui);
	free(ui);
}

static void
db_journal_group_info_destroy(struct gfarm_group_info *gi)
{
	gfarm_group_info_free(gi);
	free(gi);
}

static void
db_journal_group_modify_arg_destroy(struct db_group_modify_arg *arg)
{
	gfarm_group_info_free(&arg->gi);
	db_journal_string_array_free(arg->add_count, arg->add_users);
	db_journal_string_array_free(arg->del_count, arg->del_users);
	free(arg);
}

static void
db_journal_inode_string_modify_arg_destroy(
	struct db_inode_string_modify_arg *arg)
{
	free(arg->string);
	free(arg);
}

static void
db_journal_inode_cksum_arg_destroy(struct db_inode_cksum_arg *arg)
{
	free(arg->type);
	free(arg->sum);
	free(arg);
}

static void
db_journal_filecopy_arg_destroy(struct db_filecopy_arg *arg)
{
	free(arg->hostname);
	free(arg);
}

static void
db_journal_deadfilecopy_arg_destroy(struct db_deadfilecopy_arg *arg)
{
	free(arg->hostname);
	free(arg);
}

static void
db_journal_direntry_arg_destroy(struct db_direntry_arg *arg)
{
	free(arg->entry_name);
	free(arg);
}

static void
db_journal_symlink_arg_destroy(struct db_symlink_arg *arg)
{
	free(arg);
}

static void
db_journal_xattr_arg_destroy(struct db_xattr_arg *arg)
{
	free(arg->attrname);
	free(arg->value);
	free(arg);
}

static void
db_journal_quota_arg_destroy(struct db_quota_arg *arg)
{
	free(arg->name);
	free(arg);
}

static void
db_journal_quota_remove_arg_destroy(struct db_quota_remove_arg *arg)
{
	free(arg->name);
	free(arg);
}

static void
db_journal_stat_destroy(struct gfs_stat *st)
{
	gfs_stat_free(st);
	free(st);
}

static void
db_journal_metadb_server_destroy(struct gfarm_metadb_server *ms)
{
	gfarm_metadb_server_free(ms);
	free(ms);
}

static void
db_journal_metadb_server_modify_arg_destroy(
	struct db_mdhost_modify_arg *arg)
{
	gfarm_metadb_server_free(&arg->ms);
	free(arg);
}

/**********************************************************/

static gfarm_error_t db_journal_write_begin0(gfarm_uint64_t);

/* PREREQUISITE: giant_lock */
static gfarm_error_t
db_journal_write(gfarm_uint64_t seqnum, enum journal_operation ope,
	void *arg, journal_size_add_op_t size_add_op,
	journal_send_op_t send_op)
{
	gfarm_error_t e;

	assert(journal_seqnum_pre == GFARM_METADB_SERVER_SEQNUM_INVALID ||
	    journal_seqnum_pre + 1 == seqnum);
	if (journal_begin_called) {
		/* Write 'BEGIN' which is suppressed in previous
		 * sequence number.
		 */
		journal_begin_called = 0;
		journal_seqnum_pre = seqnum - 2;
		if ((e = db_journal_write_begin0(seqnum - 1))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003051,
			    "%s", gfarm_error_string(e));
			return (e);
		}
	}
	journal_seqnum_pre = seqnum;
	if ((e = journal_file_write(self_jf, seqnum, ope, arg, size_add_op,
	    send_op)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003052,
		    "%s", gfarm_error_string(e));
		db_journal_fail_store_op();
		return (e);
	}
	if (ope == GFM_JOURNAL_END && db_journal_sync_op) {
		if ((e = db_journal_sync_op(seqnum)) != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1003053,
			    "%s", gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
db_journal_file_writer_sync(void)
{
	return (journal_file_writer_sync(journal_file_writer(self_jf)));
}

static gfarm_error_t
db_journal_write_string_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	char *str = arg;

	if ((e = gfp_xdr_send_size_add(sizep, "s", str))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003054,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_string_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	char *str = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "s", str))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003055,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_string(gfarm_uint64_t seqnum, enum journal_operation ope,
	char *str)
{
	return (db_journal_write(seqnum, ope, str,
		db_journal_write_string_size_add,
		db_journal_write_string_core));
}

void
db_journal_wait_until_readable(void)
{
	journal_file_wait_until_readable(self_jf);
}

/**********************************************************/
/* transaction */

static gfarm_error_t
db_journal_write_transaction_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_transaction(enum journal_operation ope, void *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_begin0(gfarm_uint64_t seqnum)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_BEGIN, NULL,
	    db_journal_write_transaction_size_add,
	    db_journal_write_transaction));
}

static gfarm_error_t
db_journal_write_begin(gfarm_uint64_t seqnum, void *arg)
{
	if (++journal_transaction_nesting > 1)
		return (GFARM_ERR_NO_ERROR);
	journal_begin_called = 1;
	journal_seqnum_pre = seqnum;
	/* We write 'BEGIN' if one or more opererations are called
	 * before writing 'END'. So, we do not write it here.
	 */
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_end(gfarm_uint64_t seqnum, void *arg)
{
	if (--journal_transaction_nesting > 0)
		return (GFARM_ERR_NO_ERROR);
	/* If no operations exist between 'BEGIN' and 'END',
	 * We suppress 'BEGIN' and 'END'.
	 */
	if (journal_begin_called) {
		journal_begin_called = 0;
		journal_seqnum_pre = db_journal_subtract_current_seqnum(2);
		return (GFARM_ERR_NO_ERROR);
	}
	return (db_journal_write(seqnum, GFM_JOURNAL_END, arg,
		db_journal_write_transaction_size_add,
		db_journal_write_transaction));
}

static gfarm_error_t
db_journal_read_begin(struct gfp_xdr *xdr, void **objp)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_read_end(struct gfp_xdr *xdr, void **objp)
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************/
/* host */

#define GFM_JOURNAL_HOST_CORE_XDR_FMT		"sisii"

static gfarm_error_t
db_journal_write_host_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct gfarm_host_info *hi = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_HOST_CORE_XDR_FMT,
	    NON_NULL_STR(hi->hostname),
	    hi->port,
	    NON_NULL_STR(hi->architecture),
	    hi->ncpu,
	    hi->flags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003056,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, hi->nhostaliases, hi->hostaliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003057,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_host_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct gfarm_host_info *hi = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_HOST_CORE_XDR_FMT,
	    NON_NULL_STR(hi->hostname),
	    hi->port,
	    NON_NULL_STR(hi->architecture),
	    hi->ncpu,
	    hi->flags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003058,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, hi->nhostaliases, hi->hostaliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003059,
		    "db_journal_xdr_send_string_array", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_read_host_core(struct gfp_xdr *xdr, enum journal_operation ope,
	struct gfarm_host_info *hi)
{
	gfarm_error_t e;
	int eof;

	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_HOST_CORE_XDR_FMT,
	    &hi->hostname,
	    &hi->port,
	    &hi->architecture,
	    &hi->ncpu,
	    &hi->flags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003060,
		    "gfp_xdr_recv", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &hi->nhostaliases, &hi->hostaliases)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003061,
		    "db_journal_xdr_recv_string_array", e, ope);
	}
	return (e);
}

static gfarm_error_t
db_journal_write_host_add(gfarm_uint64_t seqnum, struct gfarm_host_info *hi)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_HOST_ADD, hi,
		db_journal_write_host_size_add,
		db_journal_write_host_core));
}

static gfarm_error_t
db_journal_read_host_info(struct gfp_xdr *xdr, struct gfarm_host_info **hip)
{
	gfarm_error_t e;
	struct gfarm_host_info *hi;
	const enum journal_operation ope = GFM_JOURNAL_HOST_ADD;

	GFARM_MALLOC(hi);
	if (hi == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003062,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(hi, 0, sizeof(*hi));
	if ((e = db_journal_read_host_core(xdr, ope, hi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003063,
		    "db_journal_read_host_core", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*hip = hi;
	else {
		db_journal_host_info_destroy(hi);
		*hip = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_host_modify_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_host_modify_arg *m = arg;
	struct gfarm_host_info *hi = &m->hi;

	if ((e = db_journal_write_host_size_add(ope, sizep, hi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003064,
		    "db_journal_write_host_size_add", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send_size_add(sizep, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003065,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, m->add_count, m->add_aliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003066,
		    "db_journal_xdr_send_string_array_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, m->del_count, m->del_aliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003067,
		    "db_journal_xdr_send_string_array_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_host_modify_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_host_modify_arg *m = arg;
	struct gfarm_host_info *hi = &m->hi;

	if ((e = db_journal_write_host_core(ope, hi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003068,
		    "db_journal_write_host_core", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003069,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, m->add_count, m->add_aliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003070,
		    "db_journal_xdr_send_string_array", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, m->del_count, m->del_aliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003071,
		    "db_journal_xdr_send_string_array", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_host_modify(gfarm_uint64_t seqnum,
	struct db_host_modify_arg *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_HOST_MODIFY, arg,
		db_journal_write_host_modify_size_add,
		db_journal_write_host_modify_core));
}

static gfarm_error_t
db_journal_read_host_modify(struct gfp_xdr *xdr,
	struct db_host_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_host_modify_arg *arg;
	struct gfarm_host_info *hi;
	int eof;
	const enum journal_operation ope = GFM_JOURNAL_HOST_MODIFY;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003072,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	hi = &arg->hi;
	if ((e = db_journal_read_host_core(xdr, ope, hi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003073,
		    "db_journal_read_host_core", e, ope);
		goto end;
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i",
	    &arg->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003074,
		    "gfp_xdr_recv", e, ope);
		goto end;
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &arg->add_count, &arg->add_aliases)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003075,
		    "db_journal_xdr_recv_string_array", e, ope);
		goto end;
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &arg->del_count, &arg->del_aliases)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003076,
		    "db_journal_xdr_recv_string_array", e, ope);
		goto end;
	}
end:
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_host_modify_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_host_remove(gfarm_uint64_t seqnum, char *hostname)
{
	return (db_journal_write_string(
		seqnum, GFM_JOURNAL_HOST_REMOVE, hostname));
}

/**********************************************************/
/* fsngroup */

#define GFM_JOURNAL_FSNGROUP_CORE_XDR_FMT		"ss"

static gfarm_error_t
db_journal_write_fsngroup_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_fsngroup_modify_arg *fsnarg =
		(struct db_fsngroup_modify_arg *)arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_FSNGROUP_CORE_XDR_FMT,
	    NON_NULL_STR(fsnarg->hostname),
	    NON_NULL_STR(fsnarg->fsngroupname))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1004044,
			"gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_fsngroup_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_fsngroup_modify_arg *fsnarg =
		(struct db_fsngroup_modify_arg *)arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_FSNGROUP_CORE_XDR_FMT,
	    NON_NULL_STR(fsnarg->hostname),
	    NON_NULL_STR(fsnarg->fsngroupname))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1004045,
			"gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_fsngroup_modify(gfarm_uint64_t seqnum,
	struct db_fsngroup_modify_arg *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_FSNGROUP_MODIFY, arg,
		db_journal_write_fsngroup_size_add,
		db_journal_write_fsngroup_core));
}

static gfarm_error_t
db_journal_read_fsngroup_core(struct gfp_xdr *xdr, enum journal_operation ope,
	struct db_fsngroup_modify_arg *fsnarg)
{
	gfarm_error_t e;
	int eof;

	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_FSNGROUP_CORE_XDR_FMT,
	    &fsnarg->hostname,
	    &fsnarg->fsngroupname)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1004046,
			"gfp_xdr_recv", e, ope);
	}
	return (e);
}

static gfarm_error_t
db_journal_read_fsngroup_modify(struct gfp_xdr *xdr,
	struct db_fsngroup_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_fsngroup_modify_arg *arg;
	const enum journal_operation ope = GFM_JOURNAL_FSNGROUP_MODIFY;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1004047,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = db_journal_read_fsngroup_core(xdr, ope, arg))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1004048,
		    "db_journal_read_fsngroup_core", e, ope);
		goto end;
	}
end:
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_fsngroup_modify_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

/**********************************************************/
/* user */

#define GFM_JOURNAL_USER_CORE_XDR_FMT "ssss"

static gfarm_error_t
db_journal_write_user_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct gfarm_user_info *ui = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_USER_CORE_XDR_FMT,
	    NON_NULL_STR(ui->username),
	    NON_NULL_STR(ui->homedir),
	    NON_NULL_STR(ui->realname),
	    NON_NULL_STR(ui->gsi_dn))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003077,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_user_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct gfarm_user_info *ui = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_USER_CORE_XDR_FMT,
	    NON_NULL_STR(ui->username),
	    NON_NULL_STR(ui->homedir),
	    NON_NULL_STR(ui->realname),
	    NON_NULL_STR(ui->gsi_dn))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003078,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_read_user_core(struct gfp_xdr *xdr, enum journal_operation ope,
	struct gfarm_user_info *ui)
{
	gfarm_error_t e;
	int eof;

	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_USER_CORE_XDR_FMT,
	    &ui->username,
	    &ui->homedir,
	    &ui->realname,
	    &ui->gsi_dn)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003079,
		    "gfp_xdr_recv", e, ope);
	}
	return (e);
}

static gfarm_error_t
db_journal_write_user_add(gfarm_uint64_t seqnum, struct gfarm_user_info *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_USER_ADD, arg,
		db_journal_write_user_size_add,
		db_journal_write_user_core));
}

static gfarm_error_t
db_journal_read_user_info(struct gfp_xdr *xdr, struct gfarm_user_info **uip)
{
	gfarm_error_t e;
	struct gfarm_user_info *ui;
	const enum journal_operation ope = GFM_JOURNAL_USER_ADD;

	GFARM_MALLOC(ui);
	if (ui == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003080,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(ui, 0, sizeof(*ui));
	if ((e = db_journal_read_user_core(xdr, ope, ui))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003081,
		    "db_journal_read_user_core", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*uip = ui;
	else {
		db_journal_user_info_destroy(ui);
		*uip = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_user_modify_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_user_modify_arg *m = arg;
	struct gfarm_user_info *ui = &m->ui;

	if ((e = db_journal_write_user_size_add(ope, sizep, ui))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003082,
		    "db_journal_write_user_size_add", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send_size_add(sizep, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003083,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_user_modify_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_user_modify_arg *m = arg;
	struct gfarm_user_info *ui = &m->ui;

	if ((e = db_journal_write_user_core(ope, ui))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003084,
		    "db_journal_write_user_core", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003085,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_user_modify(gfarm_uint64_t seqnum,
	struct db_user_modify_arg *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_USER_MODIFY, arg,
		db_journal_write_user_modify_size_add,
		db_journal_write_user_modify_core));
}

static gfarm_error_t
db_journal_read_user_modify(struct gfp_xdr *xdr,
	struct db_user_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_user_modify_arg *arg;
	struct gfarm_user_info *ui;
	int eof;
	const enum journal_operation ope = GFM_JOURNAL_USER_MODIFY;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003086,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	ui = &arg->ui;
	if ((e = db_journal_read_user_core(xdr, ope, ui))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003087,
		    "db_journal_read_user_core", e, ope);
		goto end;
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i",
	    &arg->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003088,
		    "gfp_xdr_send", e, ope);
		goto end;
	}
end:
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_user_modify_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_user_remove(gfarm_uint64_t seqnum, char *username)
{
	return (db_journal_write_string(
		seqnum, GFM_JOURNAL_USER_REMOVE, username));
}

/**********************************************************/
/* group */

#define GFM_JOURNAL_GROUP_CORE_XDR_FMT "s"

static gfarm_error_t
db_journal_write_group_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct gfarm_group_info *gi = arg;

	if ((e = gfp_xdr_send_size_add(sizep, GFM_JOURNAL_GROUP_CORE_XDR_FMT,
	    NON_NULL_STR(gi->groupname),
	    gi->nusers)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003089,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep, ope,
		gi->nusers, gi->usernames)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003090,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_group_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct gfarm_group_info *gi = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_GROUP_CORE_XDR_FMT,
	    NON_NULL_STR(gi->groupname),
	    gi->nusers)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003091,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(ope,
		gi->nusers, gi->usernames)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003092,
		    "db_journal_xdr_send_string_array", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_read_group_core(struct gfp_xdr *xdr, enum journal_operation ope,
	struct gfarm_group_info *gi)
{
	gfarm_error_t e;
	int eof;

	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_GROUP_CORE_XDR_FMT,
	    &gi->groupname)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003093,
		    "gfp_xdr_recv", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &gi->nusers, &gi->usernames)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003094,
		    "db_journal_xdr_recv_string_array", e, ope);
	}
	return (e);
}

static gfarm_error_t
db_journal_write_group_add(gfarm_uint64_t seqnum, struct gfarm_group_info *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_GROUP_ADD, arg,
		db_journal_write_group_size_add,
		db_journal_write_group_core));
}

static gfarm_error_t
db_journal_read_group_info(struct gfp_xdr *xdr, struct gfarm_group_info **gip)
{
	gfarm_error_t e;
	struct gfarm_group_info *gi;
	const enum journal_operation ope = GFM_JOURNAL_GROUP_ADD;

	GFARM_MALLOC(gi);
	if (gi == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003095,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(gi, 0, sizeof(*gi));
	if ((e = db_journal_read_group_core(xdr, ope, gi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003096,
		    "db_journal_read_group_core", e, ope);
		goto end;
	}
end:
	if (e == GFARM_ERR_NO_ERROR)
		*gip = gi;
	else {
		db_journal_group_info_destroy(gi);
		free(gi);
		*gip = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_group_modify_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_group_modify_arg *m = arg;

	if ((e = db_journal_write_group_size_add(ope, sizep, m))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003097,
		    "db_journal_write_group_size_add", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send_size_add(sizep, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003098,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, m->add_count, m->add_users))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003099,
		    "db_journal_xdr_send_string_array_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, m->del_count, m->del_users))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003100,
		    "db_journal_xdr_send_string_array_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_group_modify_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_group_modify_arg *m = arg;
	struct gfarm_group_info *gi = &m->gi;

	if ((e = db_journal_write_group_core(ope, gi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003101,
		    "db_journal_write_group_core", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003102,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, m->add_count, m->add_users))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003103,
		    "db_journal_xdr_send_string_array", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, m->del_count, m->del_users))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003104,
		    "db_journal_xdr_send_string_array", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_group_modify(gfarm_uint64_t seqnum,
	struct db_group_modify_arg *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_GROUP_MODIFY, arg,
		db_journal_write_group_modify_size_add,
		db_journal_write_group_modify_core));
}

static gfarm_error_t
db_journal_read_group_modify(struct gfp_xdr *xdr,
	struct db_group_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_group_modify_arg *arg;
	struct gfarm_group_info *gi;
	int eof;
	const enum journal_operation ope = GFM_JOURNAL_GROUP_MODIFY;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003105,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	gi = &arg->gi;
	if ((e = db_journal_read_group_core(xdr, ope, gi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003106,
		    "db_journal_read_group_core", e, ope);
		goto end;
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i",
	    &arg->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003107,
		    "gfp_xdr_send", e, ope);
		goto end;
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &arg->add_count, &arg->add_users)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003108,
		    "db_journal_xdr_recv_string_array", e, ope);
		goto end;
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &arg->del_count, &arg->del_users)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003109,
		    "db_journal_xdr_recv_string_array", e, ope);
		goto end;
	}
end:
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_group_modify_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_group_remove(gfarm_uint64_t seqnum, char *groupname)
{
	return (db_journal_write_string(
		seqnum, GFM_JOURNAL_GROUP_REMOVE, groupname));
}

/**********************************************************/
/* inode */

#define GFM_JOURNAL_INODE_XDR_FMT		"llllisslilili"
#define GFM_JOURNAL_INODE_UINT64_XDR_FMT	"ll"
#define GFM_JOURNAL_INODE_UINT32_XDR_FMT	"li"
#define GFM_JOURNAL_INODE_STR_XDR_FMT		"ls"
#define GFM_JOURNAL_INODE_TIMESPEC_XDR_FMT	"lli"

static gfarm_error_t
db_journal_write_stat_size_add(enum journal_operation ope, size_t *sizep,
	void *arg)
{
	gfarm_error_t e;
	struct gfs_stat *st = arg;

	if ((e = gfp_xdr_send_size_add(sizep, GFM_JOURNAL_INODE_XDR_FMT,
	    st->st_ino,
	    st->st_gen,
	    st->st_nlink,
	    st->st_size,
	    st->st_mode,
	    NON_NULL_STR(st->st_user),
	    NON_NULL_STR(st->st_group),
	    st->st_atimespec.tv_sec,
	    st->st_atimespec.tv_nsec,
	    st->st_mtimespec.tv_sec,
	    st->st_mtimespec.tv_nsec,
	    st->st_ctimespec.tv_sec,
	    st->st_ctimespec.tv_nsec)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003110,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_stat_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct gfs_stat *st = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR, GFM_JOURNAL_INODE_XDR_FMT,
	    st->st_ino,
	    st->st_gen,
	    st->st_nlink,
	    st->st_size,
	    st->st_mode,
	    NON_NULL_STR(st->st_user),
	    NON_NULL_STR(st->st_group),
	    st->st_atimespec.tv_sec,
	    st->st_atimespec.tv_nsec,
	    st->st_mtimespec.tv_sec,
	    st->st_mtimespec.tv_nsec,
	    st->st_ctimespec.tv_sec,
	    st->st_ctimespec.tv_nsec)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003111,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_stat(gfarm_uint64_t seqnum, enum journal_operation ope,
	struct gfs_stat *st)
{
	return (db_journal_write(seqnum, ope, st,
		db_journal_write_stat_size_add,
		db_journal_write_stat_core));
}

static gfarm_error_t
db_journal_read_stat(struct gfp_xdr *xdr, enum journal_operation ope,
	struct gfs_stat **stp)
{
	gfarm_error_t e;
	struct gfs_stat *st;
	int eof;

	GFARM_MALLOC(st);
	if (st == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003112,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(st, 0, sizeof(*st));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_XDR_FMT,
	    &st->st_ino,
	    &st->st_gen,
	    &st->st_nlink,
	    &st->st_size,
	    &st->st_mode,
	    &st->st_user,
	    &st->st_group,
	    &st->st_atimespec.tv_sec,
	    &st->st_atimespec.tv_nsec,
	    &st->st_mtimespec.tv_sec,
	    &st->st_mtimespec.tv_nsec,
	    &st->st_ctimespec.tv_sec,
	    &st->st_ctimespec.tv_nsec)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003113,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*stp = st;
	else {
		db_journal_stat_destroy(st);
		*stp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_inode_add(gfarm_uint64_t seqnum, struct gfs_stat *si)
{
	return (db_journal_write_stat(
		seqnum, GFM_JOURNAL_INODE_ADD, si));
}

static gfarm_error_t
db_journal_write_inode_modify(gfarm_uint64_t seqnum, struct gfs_stat *st)
{
	return (db_journal_write_stat(
		seqnum, GFM_JOURNAL_INODE_MODIFY, st));
}

static gfarm_error_t
db_journal_write_inode_uint64_modify_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_inode_uint64_modify_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_INODE_UINT64_XDR_FMT,
	    m->inum,
	    m->uint64)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003114,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_uint64_modify_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_inode_uint64_modify_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_INODE_UINT64_XDR_FMT,
	    m->inum,
	    m->uint64)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003115,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_uint64_modify(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_inode_uint64_modify_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_inode_uint64_modify_size_add,
		db_journal_write_inode_uint64_modify_core));
}

static gfarm_error_t
db_journal_read_inode_uint64_modify(struct gfp_xdr *xdr,
	enum journal_operation ope, struct db_inode_uint64_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_inode_uint64_modify_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003116,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_UINT64_XDR_FMT,
	    &arg->inum,
	    &arg->uint64)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003117,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		free(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_inode_uint32_modify_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_inode_uint32_modify_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_INODE_UINT32_XDR_FMT,
	    m->inum,
	    m->uint32)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003118,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_uint32_modify_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_inode_uint32_modify_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_INODE_UINT32_XDR_FMT,
	    m->inum,
	    m->uint32)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003119,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_uint32_modify(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_inode_uint32_modify_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_inode_uint32_modify_size_add,
		db_journal_write_inode_uint32_modify_core));
}

static gfarm_error_t
db_journal_read_inode_uint32_modify(struct gfp_xdr *xdr,
	enum journal_operation ope, struct db_inode_uint32_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_inode_uint32_modify_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003120,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_UINT32_XDR_FMT,
	    &arg->inum,
	    &arg->uint32)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003121,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		free(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_inode_string_modify_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_inode_string_modify_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_INODE_STR_XDR_FMT,
	    m->inum,
	    NON_NULL_STR(m->string))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003122,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_string_modify_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_inode_string_modify_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_INODE_STR_XDR_FMT,
	    m->inum,
	    NON_NULL_STR(m->string))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003123,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_string_modify(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_inode_string_modify_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_inode_string_modify_size_add,
		db_journal_write_inode_string_modify_core));
}

static gfarm_error_t
db_journal_read_inode_string_modify(struct gfp_xdr *xdr,
	enum journal_operation ope, struct db_inode_string_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_inode_string_modify_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003124,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_STR_XDR_FMT,
	    &arg->inum,
	    &arg->string)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003125,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_inode_string_modify_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_inode_timespec_modify_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_inode_timespec_modify_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_INODE_TIMESPEC_XDR_FMT,
	    m->inum,
	    m->time.tv_sec,
	    m->time.tv_nsec)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003126,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_timespec_modify_core(enum journal_operation ope,
	void *arg)
{
	gfarm_error_t e;
	struct db_inode_timespec_modify_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_INODE_TIMESPEC_XDR_FMT,
	    m->inum,
	    m->time.tv_sec,
	    m->time.tv_nsec)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003127,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_timespec_modify(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_inode_timespec_modify_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_inode_timespec_modify_size_add,
		db_journal_write_inode_timespec_modify_core));
}

static gfarm_error_t
db_journal_read_inode_timespec_modify(struct gfp_xdr *xdr,
	enum journal_operation ope, struct db_inode_timespec_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_inode_timespec_modify_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003128,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_TIMESPEC_XDR_FMT,
	    &arg->inum,
	    &arg->time.tv_sec,
	    &arg->time.tv_nsec)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003129,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		free(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_inode_gen_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (db_journal_write_inode_uint64_modify(
		seqnum, GFM_JOURNAL_INODE_GEN_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_nlink_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (db_journal_write_inode_uint64_modify(
		seqnum, GFM_JOURNAL_INODE_NLINK_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_size_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (db_journal_write_inode_uint64_modify(
		seqnum, GFM_JOURNAL_INODE_SIZE_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_mode_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint32_modify_arg *arg)
{
	return (db_journal_write_inode_uint32_modify(
		seqnum, GFM_JOURNAL_INODE_MODE_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_user_modify(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	return (db_journal_write_inode_string_modify(
		seqnum, GFM_JOURNAL_INODE_USER_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_group_modify(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	return (db_journal_write_inode_string_modify(
		seqnum, GFM_JOURNAL_INODE_GROUP_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_atime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (db_journal_write_inode_timespec_modify(
		seqnum, GFM_JOURNAL_INODE_ATIME_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_mtime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (db_journal_write_inode_timespec_modify(
		seqnum, GFM_JOURNAL_INODE_MTIME_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_ctime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (db_journal_write_inode_timespec_modify(
		seqnum, GFM_JOURNAL_INODE_CTIME_MODIFY, arg));
}

/**********************************************************/
/* inode_inum */

#define GFM_JOURNAL_INODE_INUM_XDR_FMT		"l"

static gfarm_error_t
db_journal_write_inode_inum_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_inode_inum_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep, GFM_JOURNAL_INODE_INUM_XDR_FMT,
	    m->inum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003130,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_inum_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_inode_inum_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_INODE_INUM_XDR_FMT,
	    m->inum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003131,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_inum(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_inode_inum_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_inode_inum_size_add,
		db_journal_write_inode_inum_core));
}

static gfarm_error_t
db_journal_read_inode_inum(struct gfp_xdr *xdr, enum journal_operation ope,
	struct db_inode_inum_arg **argp)
{
	gfarm_error_t e;
	struct db_inode_inum_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003132,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_INUM_XDR_FMT,
	    &arg->inum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003133,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		free(arg);
		*argp = NULL;
	}
	return (e);
}

/**********************************************************/
/* inode_cksum */

#define GFM_JOURNAL_INODE_CKSUM_XDR_FMT		"lss"

static gfarm_error_t
db_journal_write_inode_cksum_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_inode_cksum_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_INODE_CKSUM_XDR_FMT,
	    m->inum,
	    NON_NULL_STR(m->type),
	    NON_NULL_STR(m->sum))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003134,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_cksum_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_inode_cksum_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_INODE_CKSUM_XDR_FMT,
	    m->inum,
	    NON_NULL_STR(m->type),
	    NON_NULL_STR(m->sum))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003135,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_cksum(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_inode_cksum_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_inode_cksum_size_add,
		db_journal_write_inode_cksum_core));
}

static gfarm_error_t
db_journal_read_inode_cksum(struct gfp_xdr *xdr,
	enum journal_operation ope, struct db_inode_cksum_arg **argp)
{
	gfarm_error_t e;
	struct db_inode_cksum_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003136,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_CKSUM_XDR_FMT,
	    &arg->inum,
	    &arg->type,
	    &arg->sum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003137,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR) {
		arg->len = strlen(arg->sum);
		*argp = arg;
	} else {
		db_journal_inode_cksum_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_inode_cksum_add(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	return (db_journal_write_inode_cksum(
		seqnum, GFM_JOURNAL_INODE_CKSUM_ADD, arg));
}

static gfarm_error_t
db_journal_write_inode_cksum_modify(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	return (db_journal_write_inode_cksum(
		seqnum, GFM_JOURNAL_INODE_CKSUM_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_inode_cksum_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	return (db_journal_write_inode_inum(
		seqnum, GFM_JOURNAL_INODE_CKSUM_REMOVE, arg));
}

/**********************************************************/
/* filecopy */

#define GFM_JOURNAL_FILECOPY_XDR_FMT "ls"

static gfarm_error_t
db_journal_write_filecopy_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_filecopy_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_FILECOPY_XDR_FMT,
	    m->inum,
	    NON_NULL_STR(m->hostname))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003138,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_filecopy_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_filecopy_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_FILECOPY_XDR_FMT,
	    m->inum,
	    NON_NULL_STR(m->hostname))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003139,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_filecopy_modify(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_filecopy_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_filecopy_size_add,
		db_journal_write_filecopy_core));
}

static gfarm_error_t
db_journal_read_filecopy(struct gfp_xdr *xdr, enum journal_operation ope,
	struct db_filecopy_arg **argp)
{
	gfarm_error_t e;
	struct db_filecopy_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003140,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_FILECOPY_XDR_FMT,
	    &arg->inum,
	    &arg->hostname)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003141,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_filecopy_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_filecopy_add(gfarm_uint64_t seqnum,
	struct db_filecopy_arg *arg)
{
	return (db_journal_write_filecopy_modify(
		seqnum, GFM_JOURNAL_FILECOPY_ADD, arg));
}

static gfarm_error_t
db_journal_write_filecopy_remove(gfarm_uint64_t seqnum,
	struct db_filecopy_arg *arg)
{
	return (db_journal_write_filecopy_modify(
		seqnum, GFM_JOURNAL_FILECOPY_REMOVE, arg));
}

/**********************************************************/
/* deadfilecopy */

#define GFM_JOURNAL_DEADFILECOPY_XDR_FMT "lls"

static gfarm_error_t
db_journal_write_deadfilecopy_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_deadfilecopy_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_DEADFILECOPY_XDR_FMT,
	    m->inum,
	    m->igen,
	    NON_NULL_STR(m->hostname))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003142,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_deadfilecopy_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_deadfilecopy_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_DEADFILECOPY_XDR_FMT,
	    m->inum,
	    m->igen,
	    NON_NULL_STR(m->hostname))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003143,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_deadfilecopy_modify(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_deadfilecopy_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_deadfilecopy_size_add,
		db_journal_write_deadfilecopy_core));
}

static gfarm_error_t
db_journal_read_deadfilecopy(struct gfp_xdr *xdr,
	enum journal_operation ope, struct db_deadfilecopy_arg **argp)
{
	gfarm_error_t e;
	struct db_deadfilecopy_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003144,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_DEADFILECOPY_XDR_FMT,
	    &arg->inum,
	    &arg->igen,
	    &arg->hostname)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003145,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_deadfilecopy_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_deadfilecopy_add(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *arg)
{
	return (db_journal_write_deadfilecopy_modify(
		seqnum, GFM_JOURNAL_DEADFILECOPY_ADD, arg));
}

static gfarm_error_t
db_journal_write_deadfilecopy_remove(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *arg)
{
	return (db_journal_write_deadfilecopy_modify(
		seqnum, GFM_JOURNAL_DEADFILECOPY_REMOVE, arg));
}

/**********************************************************/
/* direntry */

#define GFM_JOURNAL_DIRENTRY_XDR_FMT			"lsl"

static gfarm_error_t
db_journal_write_direntry_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_direntry_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_DIRENTRY_XDR_FMT,
	    m->dir_inum,
	    NON_NULL_STR(m->entry_name),
	    m->entry_inum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003146,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_direntry_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_direntry_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_DIRENTRY_XDR_FMT,
	    m->dir_inum,
	    NON_NULL_STR(m->entry_name),
	    m->entry_inum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003147,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_direntry_update(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_direntry_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_direntry_size_add,
		db_journal_write_direntry_core));
}

static gfarm_error_t
db_journal_read_direntry(struct gfp_xdr *xdr, enum journal_operation ope,
	struct db_direntry_arg **argp)
{
	gfarm_error_t e;
	struct db_direntry_arg *arg;
	int eof;
	;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003148,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_DIRENTRY_XDR_FMT,
	    &arg->dir_inum,
	    &arg->entry_name,
	    &arg->entry_inum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003149,
		    "gfp_xdr_recv", e, ope);
		goto end;
	}
	arg->entry_len = strlen(arg->entry_name);
end:
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_direntry_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_direntry_add(gfarm_uint64_t seqnum,
	struct db_direntry_arg *arg)
{
	return (db_journal_write_direntry_update(
		seqnum, GFM_JOURNAL_DIRENTRY_ADD, arg));
}

static gfarm_error_t
db_journal_write_direntry_remove(gfarm_uint64_t seqnum,
	struct db_direntry_arg *arg)
{
	return (db_journal_write_direntry_update(
		seqnum, GFM_JOURNAL_DIRENTRY_REMOVE, arg));
}

/**********************************************************/
/* symlink */

#define GFM_JOURNAL_SYMLINK_XDR_FMT		"ls"

static gfarm_error_t
db_journal_write_symlink_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_symlink_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_SYMLINK_XDR_FMT,
	    m->inum,
	    NON_NULL_STR(m->source_path))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003150,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_symlink_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_symlink_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_SYMLINK_XDR_FMT,
	    m->inum,
	    NON_NULL_STR(m->source_path))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003151,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_symlink_add(gfarm_uint64_t seqnum,
	struct db_symlink_arg *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_SYMLINK_ADD, arg,
		db_journal_write_symlink_size_add,
		db_journal_write_symlink_core));
}

static gfarm_error_t
db_journal_read_symlink_add(struct gfp_xdr *xdr, struct db_symlink_arg **argp)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	char *source_path;
	int eof;
	const enum journal_operation ope = GFM_JOURNAL_SYMLINK_ADD;
	struct db_symlink_arg *arg = NULL;

	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_SYMLINK_XDR_FMT,
	    &inum, &source_path)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003153,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR) {
		arg = db_symlink_arg_alloc(inum, source_path);
		if (arg == NULL) {
			GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003152,
			    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
			e = GFARM_ERR_NO_MEMORY;
		}
		free(source_path);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else
		*argp = NULL;
	return (e);
}

static gfarm_error_t
db_journal_write_symlink_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	return (db_journal_write_inode_inum(
		seqnum, GFM_JOURNAL_SYMLINK_REMOVE, arg));
}

/**********************************************************/
/* xattr */

#define GFM_JOURNAL_XATTR_XDR_SEND_FMT	"ilsb"
#define GFM_JOURNAL_XATTR_XDR_RECV_FMT	"ilsB"

static gfarm_error_t
db_journal_write_xattr_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_xattr_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_XATTR_XDR_SEND_FMT,
	    m->xmlMode,
	    m->inum,
	    NON_NULL_STR(m->attrname),
	    m->size,
	    m->value)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003154,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_xattr_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_xattr_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_XATTR_XDR_SEND_FMT,
	    m->xmlMode,
	    m->inum,
	    NON_NULL_STR(m->attrname),
	    m->size,
	    m->value)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003155,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_xattr_update(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_xattr_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_xattr_size_add,
		db_journal_write_xattr_core));
}

static gfarm_error_t
db_journal_read_xattr(struct gfp_xdr *xdr,
	enum journal_operation ope, struct db_xattr_arg **argp)
{
	gfarm_error_t e;
	struct db_xattr_arg *arg;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003156,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_XATTR_XDR_RECV_FMT,
	    &arg->xmlMode,
	    &arg->inum,
	    &arg->attrname,
	    &arg->size,
	    &arg->value)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003157,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_xattr_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_xattr_add(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	return (db_journal_write_xattr_update(
		seqnum, GFM_JOURNAL_XATTR_ADD, arg));
}

static gfarm_error_t
db_journal_write_xattr_modify(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	return (db_journal_write_xattr_update(
		seqnum, GFM_JOURNAL_XATTR_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_xattr_remove(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	return (db_journal_write_xattr_update(
		seqnum, GFM_JOURNAL_XATTR_REMOVE, arg));
}

static gfarm_error_t
db_journal_write_xattr_removeall(gfarm_uint64_t seqnum,
	struct db_xattr_arg *arg)
{
	return (db_journal_write_xattr_update(
		seqnum, GFM_JOURNAL_XATTR_REMOVEALL, arg));
}

/**********************************************************/
/* quota */

#define GFM_JOURNAL_QUOTA_UPDATE_XDR_FMT	"isilllllllllllllllll"
#define GFM_JOURNAL_QUOTA_REMOVE_XDR_FMT	"is"

static gfarm_error_t
db_journal_write_quota_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_quota_arg *m = arg;
	struct quota *q = &m->quota;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_QUOTA_UPDATE_XDR_FMT,
	    m->is_group,
	    NON_NULL_STR(m->name),
	    q->on_db,
	    q->grace_period,
	    q->space,
	    q->space_exceed,
	    q->space_soft,
	    q->space_hard,
	    q->num,
	    q->num_exceed,
	    q->num_soft,
	    q->num_hard,
	    q->phy_space,
	    q->phy_space_exceed,
	    q->phy_space_soft,
	    q->phy_space_hard,
	    q->phy_num,
	    q->phy_num_exceed,
	    q->phy_num_soft,
	    q->phy_num_hard)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003158,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_quota_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_quota_arg *m = arg;
	struct quota *q = &m->quota;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_QUOTA_UPDATE_XDR_FMT,
	    m->is_group,
	    NON_NULL_STR(m->name),
	    q->on_db,
	    q->grace_period,
	    q->space,
	    q->space_exceed,
	    q->space_soft,
	    q->space_hard,
	    q->num,
	    q->num_exceed,
	    q->num_soft,
	    q->num_hard,
	    q->phy_space,
	    q->phy_space_exceed,
	    q->phy_space_soft,
	    q->phy_space_hard,
	    q->phy_num,
	    q->phy_num_exceed,
	    q->phy_num_soft,
	    q->phy_num_hard)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003159,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_quota_update(gfarm_uint64_t seqnum,
	enum journal_operation ope, struct db_quota_arg *arg)
{
	return (db_journal_write(seqnum, ope, arg,
		db_journal_write_quota_size_add,
		db_journal_write_quota_core));
}

static gfarm_error_t
db_journal_read_quota(struct gfp_xdr *xdr, enum journal_operation ope,
	struct db_quota_arg **argp)
{
	gfarm_error_t e;
	struct db_quota_arg *arg;
	struct quota *q;
	int eof;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003160,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	q = &arg->quota;
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_QUOTA_UPDATE_XDR_FMT,
	    &arg->is_group,
	    &arg->name,
	    &q->on_db,
	    &q->grace_period,
	    &q->space,
	    &q->space_exceed,
	    &q->space_soft,
	    &q->space_hard,
	    &q->num,
	    &q->num_exceed,
	    &q->num_soft,
	    &q->num_hard,
	    &q->phy_space,
	    &q->phy_space_exceed,
	    &q->phy_space_soft,
	    &q->phy_space_hard,
	    &q->phy_num,
	    &q->phy_num_exceed,
	    &q->phy_num_soft,
	    &q->phy_num_hard)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003161,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_quota_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_quota_add(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	return (db_journal_write_quota_update(
		seqnum, GFM_JOURNAL_QUOTA_ADD, arg));
}

static gfarm_error_t
db_journal_write_quota_modify(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	return (db_journal_write_quota_update(
		seqnum, GFM_JOURNAL_QUOTA_MODIFY, arg));
}

static gfarm_error_t
db_journal_write_quota_remove_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_quota_remove_arg *m = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_QUOTA_REMOVE_XDR_FMT,
	    m->is_group,
	    NON_NULL_STR(m->name))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003162,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_quota_remove_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_quota_remove_arg *m = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_QUOTA_REMOVE_XDR_FMT,
	    m->is_group,
	    NON_NULL_STR(m->name))) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003163,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_quota_remove(gfarm_uint64_t seqnum,
	struct db_quota_remove_arg *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_QUOTA_REMOVE, arg,
		db_journal_write_quota_remove_size_add,
		db_journal_write_quota_remove_core));
}

static gfarm_error_t
db_journal_read_quota_remove(struct gfp_xdr *xdr,
	struct db_quota_remove_arg **argp)
{
	gfarm_error_t e;
	struct db_quota_remove_arg *arg;
	int eof;
	enum journal_operation ope = GFM_JOURNAL_QUOTA_REMOVE;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003164,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_QUOTA_REMOVE_XDR_FMT,
	    &arg->is_group,
	    &arg->name)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003165,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_quota_remove_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

/**********************************************************/
/* mdhost */

#define GFM_JOURNAL_MDHOST_CORE_XDR_FMT "sisi"

static gfarm_error_t
db_journal_write_mdhost_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct gfarm_metadb_server *ms = arg;

	if ((e = gfp_xdr_send_size_add(sizep,
	    GFM_JOURNAL_MDHOST_CORE_XDR_FMT,
	    NON_NULL_STR(ms->name),
	    ms->port,
	    NON_NULL_STR(ms->clustername),
	    ms->flags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003166,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_mdhost_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct gfarm_metadb_server *ms = arg;

	if ((e = gfp_xdr_send(JOURNAL_W_XDR,
	    GFM_JOURNAL_MDHOST_CORE_XDR_FMT,
	    NON_NULL_STR(ms->name),
	    ms->port,
	    NON_NULL_STR(ms->clustername),
	    ms->flags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003167,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_read_mdhost_core(struct gfp_xdr *xdr, enum journal_operation ope,
	struct gfarm_metadb_server *ms)
{
	gfarm_error_t e;
	int eof;

	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_MDHOST_CORE_XDR_FMT,
	    &ms->name,
	    &ms->port,
	    &ms->clustername,
	    &ms->flags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003168,
		    "gfp_xdr_recv", e, ope);
	}
	return (e);
}

static gfarm_error_t
db_journal_write_mdhost_add(gfarm_uint64_t seqnum,
	struct gfarm_metadb_server *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_MDHOST_ADD, arg,
		db_journal_write_mdhost_size_add,
		db_journal_write_mdhost_core));
}

static gfarm_error_t
db_journal_read_metadb_server(struct gfp_xdr *xdr,
	struct gfarm_metadb_server **msp)
{
	gfarm_error_t e;
	struct gfarm_metadb_server *ms;
	const enum journal_operation ope = GFM_JOURNAL_MDHOST_ADD;

	GFARM_MALLOC(ms);
	if (ms == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003169,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(ms, 0, sizeof(*ms));
	if ((e = db_journal_read_mdhost_core(xdr, ope, ms))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003170,
		    "db_journal_read_mdhost_core", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*msp = ms;
	else {
		db_journal_metadb_server_destroy(ms);
		*msp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_mdhost_modify_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	struct db_mdhost_modify_arg *m = arg;
	struct gfarm_metadb_server *ms = &m->ms;

	if ((e = db_journal_write_mdhost_size_add(ope, sizep, ms))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003171,
		    "db_journal_write_mdhost_size_add", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send_size_add(sizep, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003172,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_mdhost_modify_core(enum journal_operation ope, void *arg)
{
	gfarm_error_t e;
	struct db_mdhost_modify_arg *m = arg;
	struct gfarm_metadb_server *ms = &m->ms;

	if ((e = db_journal_write_mdhost_core(ope, ms))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003173,
		    "db_journal_write_mdhost_core", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003174,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_mdhost_modify(gfarm_uint64_t seqnum,
	struct db_mdhost_modify_arg *arg)
{
	return (db_journal_write(seqnum, GFM_JOURNAL_MDHOST_MODIFY, arg,
		db_journal_write_mdhost_modify_size_add,
		db_journal_write_mdhost_modify_core));
}

static gfarm_error_t
db_journal_read_mdhost_modify(struct gfp_xdr *xdr,
	struct db_mdhost_modify_arg **argp)
{
	gfarm_error_t e;
	struct db_mdhost_modify_arg *arg;
	struct gfarm_metadb_server *ms;
	int eof;
	const enum journal_operation ope = GFM_JOURNAL_MDHOST_MODIFY;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003175,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	ms = &arg->ms;
	if ((e = db_journal_read_mdhost_core(xdr, ope, ms))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003176,
		    "db_journal_read_mdhost_core", e, ope);
		goto end;
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i",
	    &arg->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003177,
		    "gfp_xdr_send", e, ope);
		goto end;
	}
end:
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_metadb_server_modify_arg_destroy(arg);
		*argp = NULL;
	}
	return (e);
}

static gfarm_error_t
db_journal_write_mdhost_remove(gfarm_uint64_t seqnum, char *name)
{
	return (db_journal_write_string(
		seqnum, GFM_JOURNAL_MDHOST_REMOVE, name));
}

 /**********************************************************/
/* nop */
 
static gfarm_error_t
db_journal_read_nop(struct gfp_xdr *xdr,
	struct db_mdhost_modify_arg **argp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int eof;
	int len;
	int c;
	int i;

	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i", &len))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003504,
		    "gfp_xdr_recv", e, GFM_JOURNAL_NOP);
	}
	for (i = 0; i < len; i++) {
		if ((e = gfp_xdr_recv(xdr, 1, &eof, "c", &c))
		    != GFARM_ERR_NO_ERROR) {
			GFLOG_DEBUG_WITH_OPE(GFARM_MSG_1003505,
			    "gfp_xdr_recv", e, GFM_JOURNAL_NOP);
		}
	}

	return (e);
}

/**********************************************************/

static void
db_journal_ops_free(void *op_arg, enum journal_operation ope, void *obj)
{
	switch (ope) {
	case GFM_JOURNAL_HOST_ADD:
		db_journal_host_info_destroy(obj);
		break;
	case GFM_JOURNAL_HOST_MODIFY:
		db_journal_host_modify_arg_destroy(obj);
		break;
	case GFM_JOURNAL_USER_ADD:
		db_journal_user_info_destroy(obj);
		break;
	case GFM_JOURNAL_USER_MODIFY:
		db_journal_user_modify_arg_destroy(obj);
		break;
	case GFM_JOURNAL_GROUP_ADD:
		db_journal_group_info_destroy(obj);
		break;
	case GFM_JOURNAL_GROUP_MODIFY:
		db_journal_group_modify_arg_destroy(obj);
		break;
	case GFM_JOURNAL_INODE_ADD:
	case GFM_JOURNAL_INODE_MODIFY:
		db_journal_stat_destroy(obj);
		break;
	case GFM_JOURNAL_INODE_USER_MODIFY:
	case GFM_JOURNAL_INODE_GROUP_MODIFY:
		db_journal_inode_string_modify_arg_destroy(obj);
		break;
	case GFM_JOURNAL_INODE_CKSUM_ADD:
	case GFM_JOURNAL_INODE_CKSUM_MODIFY:
		db_journal_inode_cksum_arg_destroy(obj);
		break;
	case GFM_JOURNAL_FILECOPY_ADD:
	case GFM_JOURNAL_FILECOPY_REMOVE:
		db_journal_filecopy_arg_destroy(obj);
		break;
	case GFM_JOURNAL_DEADFILECOPY_ADD:
	case GFM_JOURNAL_DEADFILECOPY_REMOVE:
		db_journal_deadfilecopy_arg_destroy(obj);
		break;
	case GFM_JOURNAL_DIRENTRY_ADD:
	case GFM_JOURNAL_DIRENTRY_REMOVE:
		db_journal_direntry_arg_destroy(obj);
		break;
	case GFM_JOURNAL_SYMLINK_ADD:
		db_journal_symlink_arg_destroy(obj);
		break;
	case GFM_JOURNAL_XATTR_ADD:
	case GFM_JOURNAL_XATTR_MODIFY:
	case GFM_JOURNAL_XATTR_REMOVE:
	case GFM_JOURNAL_XATTR_REMOVEALL:
		db_journal_xattr_arg_destroy(obj);
		break;
	case GFM_JOURNAL_QUOTA_ADD:
	case GFM_JOURNAL_QUOTA_MODIFY:
		db_journal_quota_arg_destroy(obj);
		break;
	case GFM_JOURNAL_QUOTA_REMOVE:
		db_journal_quota_remove_arg_destroy(obj);
		break;
	case GFM_JOURNAL_MDHOST_ADD:
		db_journal_metadb_server_destroy(obj);
		break;
	case GFM_JOURNAL_MDHOST_MODIFY:
		db_journal_metadb_server_modify_arg_destroy(obj);
		break;
	case GFM_JOURNAL_HOST_REMOVE: /* char[] */
	case GFM_JOURNAL_USER_REMOVE: /* char[] */
	case GFM_JOURNAL_GROUP_REMOVE: /* char[] */
	case GFM_JOURNAL_INODE_GEN_MODIFY: /* db_inode_uint64_modify_arg */
	case GFM_JOURNAL_INODE_NLINK_MODIFY: /* db_inode_uint64_modify_arg */
	case GFM_JOURNAL_INODE_SIZE_MODIFY: /* db_inode_uint64_modify_arg */
	case GFM_JOURNAL_INODE_MODE_MODIFY: /* db_inode_uint32_modify_arg */
	case GFM_JOURNAL_INODE_ATIME_MODIFY: /* db_inode_timespec_modify_arg */
	case GFM_JOURNAL_INODE_MTIME_MODIFY: /* db_inode_timespec_modify_arg */
	case GFM_JOURNAL_INODE_CTIME_MODIFY: /* db_inode_timespec_modify_arg */
	case GFM_JOURNAL_INODE_CKSUM_REMOVE: /* db_inode_inum_arg */
	case GFM_JOURNAL_SYMLINK_REMOVE: /* db_inode_inum_arg */
	case GFM_JOURNAL_MDHOST_REMOVE: /* char[] */
		free(obj);
		break;
	case GFM_JOURNAL_FSNGROUP_MODIFY:
		db_journal_fsngroup_modify_arg_destroy(
			(struct db_fsngroup_modify_arg *)obj);
		break;
	default:
		break;
	}
}

static gfarm_error_t
db_journal_read_ops(void *op_arg, struct gfp_xdr *xdr,
	enum journal_operation ope, void **objp)
{
	gfarm_error_t e;
	*objp = NULL;

	switch (ope) {
	case GFM_JOURNAL_BEGIN:
		e = db_journal_read_begin(xdr, objp);
		break;
	case GFM_JOURNAL_END:
		e = db_journal_read_end(xdr, objp);
		break;
	case GFM_JOURNAL_HOST_ADD:
		e = db_journal_read_host_info(xdr,
			(struct gfarm_host_info **)objp);
		break;
	case GFM_JOURNAL_HOST_MODIFY:
		e = db_journal_read_host_modify(xdr,
			(struct db_host_modify_arg **)objp);
		break;
	case GFM_JOURNAL_USER_ADD:
		e = db_journal_read_user_info(xdr,
			(struct gfarm_user_info **)objp);
		break;
	case GFM_JOURNAL_USER_MODIFY:
		e = db_journal_read_user_modify(xdr,
			(struct db_user_modify_arg **)objp);
		break;
	case GFM_JOURNAL_GROUP_ADD:
		e = db_journal_read_group_info(xdr,
			(struct gfarm_group_info **)objp);
		break;
	case GFM_JOURNAL_GROUP_MODIFY:
		e = db_journal_read_group_modify(xdr,
			(struct db_group_modify_arg **)objp);
		break;
	case GFM_JOURNAL_HOST_REMOVE:
	case GFM_JOURNAL_USER_REMOVE:
	case GFM_JOURNAL_GROUP_REMOVE:
	case GFM_JOURNAL_MDHOST_REMOVE:
		e = db_journal_read_string(xdr, ope, (char **)objp);
		break;
	case GFM_JOURNAL_INODE_ADD:
	case GFM_JOURNAL_INODE_MODIFY:
		e = db_journal_read_stat(xdr, ope, (struct gfs_stat **)objp);
		break;
	case GFM_JOURNAL_INODE_GEN_MODIFY:
	case GFM_JOURNAL_INODE_NLINK_MODIFY:
	case GFM_JOURNAL_INODE_SIZE_MODIFY:
		e = db_journal_read_inode_uint64_modify(xdr, ope,
			(struct db_inode_uint64_modify_arg **)objp);
		break;
	case GFM_JOURNAL_INODE_MODE_MODIFY:
		e = db_journal_read_inode_uint32_modify(xdr, ope,
			(struct db_inode_uint32_modify_arg **)objp);
		break;
	case GFM_JOURNAL_INODE_USER_MODIFY:
	case GFM_JOURNAL_INODE_GROUP_MODIFY:
		e = db_journal_read_inode_string_modify(xdr, ope,
			(struct db_inode_string_modify_arg **)objp);
		break;
	case GFM_JOURNAL_INODE_ATIME_MODIFY:
	case GFM_JOURNAL_INODE_MTIME_MODIFY:
	case GFM_JOURNAL_INODE_CTIME_MODIFY:
		e = db_journal_read_inode_timespec_modify(xdr, ope,
			(struct db_inode_timespec_modify_arg **)objp);
		break;
	case GFM_JOURNAL_INODE_CKSUM_ADD:
	case GFM_JOURNAL_INODE_CKSUM_MODIFY:
		e = db_journal_read_inode_cksum(xdr, ope,
			(struct db_inode_cksum_arg **)objp);
		break;
	case GFM_JOURNAL_INODE_CKSUM_REMOVE:
	case GFM_JOURNAL_SYMLINK_REMOVE:
		e = db_journal_read_inode_inum(xdr, ope,
			(struct db_inode_inum_arg **)objp);
		break;
	case GFM_JOURNAL_FILECOPY_ADD:
	case GFM_JOURNAL_FILECOPY_REMOVE:
		e = db_journal_read_filecopy(xdr, ope,
			(struct db_filecopy_arg **)objp);
		break;
	case GFM_JOURNAL_DEADFILECOPY_ADD:
	case GFM_JOURNAL_DEADFILECOPY_REMOVE:
		e = db_journal_read_deadfilecopy(xdr, ope,
			(struct db_deadfilecopy_arg **)objp);
		break;
	case GFM_JOURNAL_DIRENTRY_ADD:
	case GFM_JOURNAL_DIRENTRY_REMOVE:
		e = db_journal_read_direntry(xdr, ope,
			(struct db_direntry_arg **)objp);
		break;
	case GFM_JOURNAL_SYMLINK_ADD:
		e = db_journal_read_symlink_add(xdr,
			(struct db_symlink_arg **)objp);
		break;
	case GFM_JOURNAL_XATTR_ADD:
	case GFM_JOURNAL_XATTR_MODIFY:
	case GFM_JOURNAL_XATTR_REMOVE:
	case GFM_JOURNAL_XATTR_REMOVEALL:
		e = db_journal_read_xattr(xdr, ope,
			(struct db_xattr_arg **)objp);
		break;
	case GFM_JOURNAL_QUOTA_ADD:
	case GFM_JOURNAL_QUOTA_MODIFY:
		e = db_journal_read_quota(xdr, ope,
			(struct db_quota_arg **)objp);
		break;
	case GFM_JOURNAL_QUOTA_REMOVE:
		e = db_journal_read_quota_remove(xdr,
			(struct db_quota_remove_arg **)objp);
		break;
	case GFM_JOURNAL_MDHOST_ADD:
		e = db_journal_read_metadb_server(xdr,
			(struct gfarm_metadb_server **)objp);
		break;
	case GFM_JOURNAL_MDHOST_MODIFY:
		e = db_journal_read_mdhost_modify(xdr,
			(struct db_mdhost_modify_arg **)objp);
		break;
	case GFM_JOURNAL_FSNGROUP_MODIFY:
		e = db_journal_read_fsngroup_modify(xdr,
			(struct db_fsngroup_modify_arg **)objp);
		break;
	case GFM_JOURNAL_NOP:
		e = db_journal_read_nop(xdr,
			(struct db_mdhost_modify_arg **)objp);
		break;
	default:
		e = GFARM_ERR_INVALID_ARGUMENT;
		break;
	}

	if (e != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_OPE(GFARM_MSG_1003178,
		    "read record", e, ope);
	}
	return (e);
}

static gfarm_error_t
db_journal_ops_call(const struct db_ops *ops, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, const char *diag)
{
	gfarm_error_t e;

	switch (ope) {
	case GFM_JOURNAL_BEGIN:
		e = ops->begin(seqnum, obj); break;
	case GFM_JOURNAL_END:
		e = ops->end(seqnum, obj); break;
	case GFM_JOURNAL_HOST_ADD:
		e = ops->host_add(seqnum, obj); break;
	case GFM_JOURNAL_HOST_MODIFY:
		e = ops->host_modify(seqnum, obj); break;
	case GFM_JOURNAL_HOST_REMOVE:
		e = ops->host_remove(seqnum, obj); break;
	case GFM_JOURNAL_USER_ADD:
		e = ops->user_add(seqnum, obj); break;
	case GFM_JOURNAL_USER_MODIFY:
		e = ops->user_modify(seqnum, obj); break;
	case GFM_JOURNAL_USER_REMOVE:
		e = ops->user_remove(seqnum, obj); break;
	case GFM_JOURNAL_GROUP_ADD:
		e = ops->group_add(seqnum, obj); break;
	case GFM_JOURNAL_GROUP_MODIFY:
		e = ops->group_modify(seqnum, obj); break;
	case GFM_JOURNAL_GROUP_REMOVE:
		e = ops->group_remove(seqnum, obj); break;
	case GFM_JOURNAL_INODE_ADD:
		e = ops->inode_add(seqnum, obj); break;
	case GFM_JOURNAL_INODE_MODIFY:
		e = ops->inode_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_GEN_MODIFY:
		e = ops->inode_gen_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_NLINK_MODIFY:
		e = ops->inode_nlink_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_SIZE_MODIFY:
		e = ops->inode_size_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_MODE_MODIFY:
		e = ops->inode_mode_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_USER_MODIFY:
		e = ops->inode_user_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_GROUP_MODIFY:
		e = ops->inode_group_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_ATIME_MODIFY:
		e = ops->inode_atime_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_MTIME_MODIFY:
		e = ops->inode_mtime_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_CTIME_MODIFY:
		e = ops->inode_ctime_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_CKSUM_ADD:
		e = ops->inode_cksum_add(seqnum, obj); break;
	case GFM_JOURNAL_INODE_CKSUM_MODIFY:
		e = ops->inode_cksum_modify(seqnum, obj); break;
	case GFM_JOURNAL_INODE_CKSUM_REMOVE:
		e = ops->inode_cksum_remove(seqnum, obj); break;
	case GFM_JOURNAL_FILECOPY_ADD:
		e = ops->filecopy_add(seqnum, obj); break;
	case GFM_JOURNAL_FILECOPY_REMOVE:
		e = ops->filecopy_remove(seqnum, obj); break;
	case GFM_JOURNAL_DEADFILECOPY_ADD:
		e = ops->deadfilecopy_add(seqnum, obj); break;
	case GFM_JOURNAL_DEADFILECOPY_REMOVE:
		e = ops->deadfilecopy_remove(seqnum, obj);
		/* ignore 'no such object' in gfarm_pgsql_deadfilecopy_remove
		 * which occurs due to the race condition at boot time.
		 */
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			e = GFARM_ERR_NO_ERROR;
		break;
	case GFM_JOURNAL_DIRENTRY_ADD:
		e = ops->direntry_add(seqnum, obj); break;
	case GFM_JOURNAL_DIRENTRY_REMOVE:
		e = ops->direntry_remove(seqnum, obj); break;
	case GFM_JOURNAL_SYMLINK_ADD:
		e = ops->symlink_add(seqnum, obj); break;
	case GFM_JOURNAL_SYMLINK_REMOVE:
		e = ops->symlink_remove(seqnum, obj); break;
	case GFM_JOURNAL_XATTR_ADD:
		e = ops->xattr_add(seqnum, obj); break;
	case GFM_JOURNAL_XATTR_MODIFY:
		e = ops->xattr_modify(seqnum, obj); break;
	case GFM_JOURNAL_XATTR_REMOVE:
		e = ops->xattr_remove(seqnum, obj); break;
	case GFM_JOURNAL_XATTR_REMOVEALL:
		e = ops->xattr_removeall(seqnum, obj); break;
	case GFM_JOURNAL_QUOTA_ADD:
		e = ops->quota_add(seqnum, obj); break;
	case GFM_JOURNAL_QUOTA_MODIFY:
		e = ops->quota_modify(seqnum, obj); break;
	case GFM_JOURNAL_QUOTA_REMOVE:
		e = ops->quota_remove(seqnum, obj); break;
	case GFM_JOURNAL_MDHOST_ADD:
		e = ops->mdhost_add(seqnum, obj); break;
	case GFM_JOURNAL_MDHOST_MODIFY:
		e = ops->mdhost_modify(seqnum, obj); break;
	case GFM_JOURNAL_MDHOST_REMOVE:
		e = ops->mdhost_remove(seqnum, obj); break;
	case GFM_JOURNAL_FSNGROUP_MODIFY:
		e = ops->fsngroup_modify(seqnum, obj); break;
	case GFM_JOURNAL_NOP:
		e = GFARM_ERR_NO_ERROR; break;
	default:
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_fatal(GFARM_MSG_1003179,
		    "%s : seqnum=%llu ope=%d",
		    diag, (unsigned long long)seqnum, ope); /* exit */
		return (e);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003180,
		    "%s : seqnum=%llu ope=%s : %s",
		    diag, (unsigned long long)seqnum,
		    journal_operation_name(ope), gfarm_error_string(e));
	}
	return (e);
}

struct db_journal_rec {
	gfarm_uint64_t seqnum;
	enum journal_operation ope;
	void *obj;
	GFARM_STAILQ_ENTRY(db_journal_rec) next;
};

static int
db_journal_is_rec_stored(struct db_journal_rec *rec)
{
	gfarm_uint64_t seqnum;
	gfarm_error_t e;

	e = store_ops->seqnum_get(DB_SEQNUM_MASTER_NAME, &seqnum);
	return (e == GFARM_ERR_NO_ERROR && seqnum >= rec->seqnum);
}

GFARM_STAILQ_HEAD(db_journal_rec_list, db_journal_rec);

/* PREREQUISITE: journal_file_mutex */
static gfarm_error_t
db_journal_apply_op(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct db_journal_rec *ai, *tai;
	struct db_journal_rec_list *c = closure;
	static const char diag[] = "db_journal_apply_op";

	*needs_freep = 0;
	GFARM_MALLOC(ai);
	if (ai == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1003181,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	ai->seqnum = seqnum;
	ai->ope = ope;
	ai->obj = obj;
	GFARM_STAILQ_INSERT_TAIL(c, ai, next);
	if (ope != GFM_JOURNAL_END)
		return (GFARM_ERR_NO_ERROR);

	/*
	 * Since the giant lock must be taken preceeded by the journal
	 * file mutex in order to avoid deadlock, we unlock the journal 
	 * file mutex once, take the giant lock, and then lock the journal
	 * file mutex again.
	 */
	journal_file_mutex_unlock(self_jf, diag);
	giant_lock();
	journal_file_mutex_lock(self_jf, diag);

	ai = GFARM_STAILQ_FIRST(c);
	if (ai->ope != GFM_JOURNAL_BEGIN) {
		e = GFARM_ERR_INTERNAL_ERROR;
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1003182,
		    "first record must be BEGIN", e,
		    (unsigned long long)ai->seqnum, ai->ope);
		goto end;
	}
retry:
	GFARM_STAILQ_FOREACH(ai, c, next) {
#ifdef DEBUG_JOURNAL
		gflog_info(GFARM_MSG_1003183,
		    "apply seqnum=%llu ope=%s",
		    (unsigned long long)ai->seqnum,
		    journal_operation_name(ai->ope));
#endif
		if ((e = db_journal_ops_call(store_ops, ai->seqnum, ai->ope,
		    ai->obj, "db_journal_apply_op[store]"))
		    == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED) {
			if (!db_journal_is_rec_stored(ai))
				goto retry;
			gflog_info(GFARM_MSG_1003398,
			    "db seems to have been committed the "
			    "last operation, no retry is needed");
			e = GFARM_ERR_NO_ERROR;
		} else if (e != GFARM_ERR_NO_ERROR)
			goto end;
	}

	journal_file_reader_commit_pos(
		journal_file_main_reader(self_jf));

	GFARM_STAILQ_FOREACH(ai, c, next) {
		if ((e = db_journal_ops_call(journal_apply_ops, ai->seqnum,
		    ai->ope, ai->obj, "db_journal_apply_op[apply]"))
		    != GFARM_ERR_NO_ERROR)
			goto end;
	}
end:
	giant_unlock();
	GFARM_STAILQ_FOREACH_SAFE(ai, c, next, tai) {
		db_journal_ops_free(NULL, ai->ope, ai->obj);
		free(ai);
	}
	GFARM_STAILQ_INIT(c);
	return (e);
}

/* PREREQUISITE: giant_lock */
gfarm_error_t
db_journal_read(struct journal_file_reader *reader, void *op_arg,
	journal_post_read_op_t post_read_op, void *closure, int *eofp)
{
	return (journal_file_read(reader, op_arg,
	    db_journal_read_ops, post_read_op,
	    db_journal_ops_free, closure, eofp));
}

void
db_journal_wait_for_apply_thread(void)
{
	journal_file_wait_for_read_completion(
		journal_file_main_reader(self_jf));
}

static gfarm_error_t
db_journal_add_rec(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, void *closure, size_t length,
	int *needs_freep)
{
	struct db_journal_rec *rec;
	struct db_journal_rec_list *recs = closure;

	*needs_freep = 0;
	GFARM_MALLOC(rec);
	if (rec == NULL) {
		gflog_error(GFARM_MSG_1003184,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	rec->seqnum = seqnum;
	rec->ope = ope;
	rec->obj = obj;
	GFARM_STAILQ_INSERT_TAIL(recs, rec, next);
	return (GFARM_ERR_NO_ERROR);
}

static void
db_journal_free_rec_list(struct db_journal_rec_list *recs)
{
	struct db_journal_rec *rec, *rec2;

	GFARM_STAILQ_FOREACH_SAFE(rec, recs, next, rec2) {
		db_journal_ops_free(NULL, rec->ope, rec->obj);
		free(rec);
	}
}

void *
db_journal_store_thread(void *arg)
{
	int boot_apply = (arg != NULL) ? *(int *)arg : 0;
	int first;
	gfarm_error_t e;
	struct journal_file_reader *reader =
		journal_file_main_reader(self_jf);
	struct db_journal_rec *rec;
	struct db_journal_rec_list recs;
	static const char diag[] = "db_journal_store_thread";

	for (;;) {
		GFARM_STAILQ_INIT(&recs);
		first = 1;
		do {
			if ((e = journal_file_read(reader, NULL,
			    db_journal_read_ops, db_journal_add_rec, NULL,
			    &recs, NULL)) != GFARM_ERR_NO_ERROR) {
				if (boot_apply && e == GFARM_ERR_CANT_OPEN)
					goto end;
				gflog_error(GFARM_MSG_1003185,
				    "failed to read journal record : %s",
				    gfarm_error_string(e));
				goto error;
			}
			if (journal_file_is_closed(self_jf))
				goto end;
			rec = GFARM_STAILQ_LAST(&recs, db_journal_rec, next);
			if (first) {
				if (rec->ope != GFM_JOURNAL_BEGIN)
					gflog_fatal(GFARM_MSG_1003186,
					    "invalid journal record");
					    /* exit */
				else
					first = 0;
			}
		} while (rec->ope != GFM_JOURNAL_END);

		/* lock to avoid race condition between db_thread. */
		gfarm_mutex_lock(get_db_access_mutex(), diag,
		    DB_ACCESS_MUTEX_DIAG);
retry:
		GFARM_STAILQ_FOREACH(rec, &recs, next) {
#ifdef DEBUG_JOURNAL
			gflog_info(GFARM_MSG_1003187,
			    "store seqnum=%" GFARM_PRId64 " ope=%s",
			    rec->seqnum, journal_operation_name(rec->ope));
#endif
			if ((e = db_journal_ops_call(store_ops, rec->seqnum,
			    rec->ope, rec->obj, diag))
			    == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED) {
				if (!db_journal_is_rec_stored(rec))
					goto retry;
				gflog_info(GFARM_MSG_1003327,
				    "db seems to have been committed the "
				    "last operation, no retry is needed");
				e = GFARM_ERR_NO_ERROR;
			} else if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1003188,
				    "failed to store to db : %s",
				    gfarm_error_string(e));
				gfarm_mutex_unlock(get_db_access_mutex(), diag,
				    DB_ACCESS_MUTEX_DIAG);
				goto error;
			}
		}
		gfarm_mutex_unlock(get_db_access_mutex(), diag,
		    DB_ACCESS_MUTEX_DIAG);
		if (journal_file_is_closed(self_jf))
			goto end;

		journal_file_mutex_lock(self_jf, diag);
		journal_file_reader_commit_pos(reader);
		journal_file_mutex_unlock(self_jf, diag);

		db_journal_free_rec_list(&recs);
	}
error:
	db_journal_fail_store_op();
end:
	db_journal_free_rec_list(&recs);
	return (NULL);
}

void *
db_journal_apply_thread(void *arg)
{
	int eof;
	gfarm_error_t e;
	struct journal_file_reader *reader = journal_file_main_reader(self_jf);
	struct db_journal_rec_list closure =
		GFARM_STAILQ_HEAD_INITIALIZER(closure);

	for (;;) {
		if ((e = db_journal_read(reader,
		    (void *)store_ops /* UNCONST */, db_journal_apply_op,
		    &closure, &eof)) != GFARM_ERR_NO_ERROR) {
			if (e == GFARM_ERR_CANT_OPEN)
				break; /* transforming to master */
			gflog_fatal(GFARM_MSG_1003189,
			    "failed to read journal or apply to memory/db : %s",
			    gfarm_error_string(e)); /* exit */
		}
		if (journal_file_is_closed(self_jf))
			break;
	}
	return (NULL);
}

void
db_journal_reset_slave_transaction_nesting(void)
{
	journal_file_wait_until_empty(self_jf);
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1003190,
	    "journal_seqnum=%llu", (unsigned long long)journal_seqnum);
#endif
	if (journal_slave_transaction_nesting > 0) {
		gflog_error(GFARM_MSG_1003191,
		    "transaction end point is not found in journal file.");
		giant_lock();
		while (journal_slave_transaction_nesting-- > 0)
			db_end("db_journal_reset_slave_transaction_nesting");
		giant_unlock();
	}
}

gfarm_error_t
db_journal_reader_reopen_if_needed(const char *label,
	struct journal_file_reader **readerp,
	gfarm_uint64_t last_fetch_seqnum, int *initedp)
{
	return (journal_file_reader_reopen_if_needed(self_jf, label, readerp,
		last_fetch_seqnum, initedp));
}

struct db_journal_fetch_info {
	struct db_journal_fetch_info *next;
	char *rec;
	gfarm_uint32_t rec_len;
};

static void
db_journal_fetch_info_list_free(struct db_journal_fetch_info *fi_list)
{
	struct db_journal_fetch_info *fi, *fin;

	if (fi_list == NULL)
		return;

	for (fi = fi_list, fin = fi ? fi->next : NULL; fi != NULL; fi = fin,
	    fin = fi ? fi->next : NULL) {
		free(fi->rec);
		free(fi);
	}
}

gfarm_error_t
db_journal_fetch(struct journal_file_reader *reader,
	gfarm_uint64_t min_seqnum, char **datap, int *lenp,
	gfarm_uint64_t *from_seqnump, gfarm_uint64_t *to_seqnump,
	int *no_recp, const char *diag)
{
#define FETCH_SIZE_THRESHOLD 8000
	gfarm_error_t e;
	gfarm_uint64_t cur_seqnum, seqnum;
	char *rec, *recs, *p;
	int eof, num_fi = 0;
	gfarm_uint32_t rec_len, all_len = 0;
	struct db_journal_fetch_info *fi = NULL, *fi0 = NULL, *fih = NULL;
	gfarm_uint64_t from_sn = 0, to_sn;

	cur_seqnum = db_journal_get_current_seqnum();

	if (cur_seqnum + 1 == min_seqnum) {
		*no_recp = 1;
		return (GFARM_ERR_NO_ERROR);
	}
	if (journal_file_reader_is_expired(reader)) {
		gflog_debug(GFARM_MSG_1003431,
		    "%s : already expired (cur:%llu target:%llu)",
		    diag, (unsigned long long)cur_seqnum,
		    (unsigned long long)min_seqnum);
		return (GFARM_ERR_EXPIRED);
	}
	if (cur_seqnum < min_seqnum) {
		gflog_error(GFARM_MSG_1003192,
		    "%s : invalid seqnum (cur:%llu < target:%llu)",
		    diag, (unsigned long long)cur_seqnum,
		    (unsigned long long)min_seqnum);
		return (GFARM_ERR_EXPIRED);
	}

	for (;;) {
		e = journal_file_read_serialized(reader, &rec, &rec_len,
		    &seqnum, &eof);

		journal_file_mutex_lock(self_jf, diag);
		journal_file_reader_commit_pos(reader);
		journal_file_mutex_unlock(self_jf, diag);

		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003194,
			    "%s : %s", diag, gfarm_error_string(e));
			goto error;
		}
		if (eof)
			break;
		if (seqnum < min_seqnum)
			free(rec);
		else {
			if (fih == NULL && seqnum > 0 && seqnum != min_seqnum) {
				free(rec);
				journal_file_reader_invalidate(reader);
				gflog_debug(GFARM_MSG_1003195,
				    "%s : target journal records are expired "
				    "(cur:%llu, target:%llu)", diag,
				    (unsigned long long)seqnum,
				    (unsigned long long)min_seqnum);
				return (GFARM_ERR_EXPIRED);
			}
			GFARM_MALLOC(fi);
			if (fi == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				gflog_error(GFARM_MSG_1003196,
				    "%s: %s", diag, gfarm_error_string(e));
				goto error;
			}
			fi->next = NULL; /* do this here for "goto error" */
			fi->rec = rec;
			fi->rec_len = rec_len;
			if (fih == NULL) {
				fih = fi;
				from_sn = seqnum;
			}
			if (fi0 != NULL)
				fi0->next = fi;
			fi0 = fi;
			all_len += rec_len;
			++num_fi;
			if (all_len >= FETCH_SIZE_THRESHOLD)
				break;
		}
	}
	if (fih == NULL) {
		*no_recp = 1;
		return (GFARM_ERR_NO_ERROR);
	}
	*no_recp = 0;
	to_sn = seqnum;
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1003197,
	    "%s : fetch %llu to %llu (n=%d)", diag,
	    (unsigned long long)from_sn, (unsigned long long)to_sn, num_fi);
#endif
	GFARM_MALLOC_ARRAY(recs, all_len);
	if (recs == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1003198,
		    "%s : %s", diag, gfarm_error_string(e));
		goto error;
	}
	p = recs;
	for (fi = fih; fi != NULL; fi = fi->next) {
		memcpy(p, fi->rec, fi->rec_len);
		p += fi->rec_len;
	}
	db_journal_fetch_info_list_free(fih);
	*datap = recs;
	*lenp = all_len;
	*from_seqnump = from_sn;
	*to_seqnump = to_sn;
	return (GFARM_ERR_NO_ERROR);
error:
	db_journal_fetch_info_list_free(fih);
	journal_file_reader_invalidate(reader);
	return (e);
}

gfarm_error_t
db_journal_recvq_enter(gfarm_uint64_t from_sn, gfarm_uint64_t to_sn,
	int recs_len, unsigned char *recs)
{
	gfarm_error_t e;
	struct db_journal_recv_info *ri;
	static const char diag[] = "db_journal_recvq_enter";

	GFARM_MALLOC(ri);
	if (ri == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1003199,
		    "%s : %s", diag, gfarm_error_string(e));
		return (e);
	}
	ri->from_sn = from_sn;
	ri->to_sn = to_sn;
	ri->recs_len = recs_len;
	ri->recs = recs;
	gfarm_mutex_lock(&journal_recvq_mutex, diag, RECVQ_MUTEX_DIAG);
	while (journal_recvq_nelems >= gfarm_get_journal_recvq_size())
		gfarm_cond_wait(&journal_recvq_nonfull_cond,
		    &journal_recvq_mutex, diag, RECVQ_NONFULL_COND_DIAG);
	GFARM_STAILQ_INSERT_TAIL(&journal_recvq, ri, next);
	gfarm_cond_signal(&journal_recvq_nonempty_cond, diag,
	    RECVQ_NONEMPTY_COND_DIAG);
	++journal_recvq_nelems;
	gfarm_mutex_unlock(&journal_recvq_mutex, diag, RECVQ_MUTEX_DIAG);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_recvq_delete(struct db_journal_recv_info **rip)
{
	struct db_journal_recv_info *ri;
	gfarm_uint64_t next_sn;
	static const char diag[] = "db_journal_recvq_delete";

	next_sn = db_journal_get_current_seqnum() + 1;

retry_from_locking:
	gfarm_mutex_lock(&journal_recvq_mutex, diag, RECVQ_MUTEX_DIAG);
	while (journal_recvq_nelems <= 0 && journal_recvq_cancel == 0)
		gfarm_cond_wait(&journal_recvq_nonempty_cond,
		    &journal_recvq_mutex, diag, RECVQ_NONEMPTY_COND_DIAG);
	if (journal_recvq_cancel)
		ri = NULL;
	else {
		ri = GFARM_STAILQ_FIRST(&journal_recvq);
		GFARM_STAILQ_REMOVE_HEAD(&journal_recvq, next);
		--journal_recvq_nelems;
		gfarm_cond_signal(&journal_recvq_nonfull_cond, diag,
		    RECVQ_NONFULL_COND_DIAG);
		if (ri->from_sn != next_sn) {
			/* something is going wrong, disconnect & restart */

			gflog_error(GFARM_MSG_1004280,
			    "abandon invalid journal: seqnum %llu "
			    "should be %llu, disconnect/reconnecting master",
			    (unsigned long long)ri->from_sn,
			    (unsigned long long)next_sn);
			free(ri->recs);
			free(ri);

			gfarm_mutex_unlock(
			    &journal_recvq_mutex, diag, RECVQ_MUTEX_DIAG);

			(*master_disconnect_request)(NULL);
			goto retry_from_locking;
		}
	}
	gfarm_mutex_unlock(&journal_recvq_mutex, diag, RECVQ_MUTEX_DIAG);
	*rip = ri;
	return (GFARM_ERR_NO_ERROR);
}

void
db_journal_cancel_recvq()
{
	static const char diag[] = "db_journal_recvq_cancel";

	gfarm_mutex_lock(&journal_recvq_mutex, diag, RECVQ_MUTEX_DIAG);
	journal_recvq_cancel = 1;
	gfarm_cond_signal(&journal_recvq_nonempty_cond, diag,
	    RECVQ_NONEMPTY_COND_DIAG);
	while (journal_recvq_cancel)
		gfarm_cond_wait(&journal_recvq_cancel_cond,
		    &journal_recvq_mutex, diag, RECVQ_CANCEL_COND_DIAG);
	gfarm_mutex_unlock(&journal_recvq_mutex, diag, RECVQ_MUTEX_DIAG);
}

static gfarm_error_t
db_journal_recvq_proc(int *canceledp)
{
	gfarm_error_t e, e2;
	gfarm_uint64_t last_seqnum;
	struct db_journal_recv_info *ri;
	static const char diag[] = "db_journal_recvq_proc";

	if ((e = db_journal_recvq_delete(&ri)) != GFARM_ERR_NO_ERROR)
		return (e);
	if (ri == NULL) {
		/* canceled */
		gfarm_mutex_lock(&journal_recvq_mutex, diag,
		    RECVQ_MUTEX_DIAG);
		journal_recvq_cancel = 0;
		gfarm_mutex_unlock(&journal_recvq_mutex, diag,
		    RECVQ_MUTEX_DIAG);
		gfarm_cond_signal(&journal_recvq_cancel_cond, diag,
		    RECVQ_NONEMPTY_COND_DIAG);
		*canceledp = 1;
		return (GFARM_ERR_NO_ERROR);
	}
	if ((e = journal_file_write_raw(self_jf, ri->recs_len, ri->recs,
	    &last_seqnum, &journal_slave_transaction_nesting))
	    == GFARM_ERR_NO_ERROR)
		db_journal_set_current_seqnum(last_seqnum);
	free(ri->recs);
	free(ri);
	if (gfarm_get_journal_sync_file()) {
		if ((e2 = db_journal_file_writer_sync()) != GFARM_ERR_NO_ERROR)
			if (e == GFARM_ERR_NO_ERROR)
				e = e2;
	}
	return (e);
}

void *
db_journal_recvq_thread(void *arg)
{
	gfarm_error_t e;
	int canceled = 0;

	for (;;) {
		if ((e = db_journal_recvq_proc(&canceled))
		    != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1003201,
			    "%s", gfarm_error_string(e)); /* exit */
		if (canceled)
			break;
	}
	return (NULL);
}

/**********************************************************/
/* delegated functions */

static gfarm_error_t
db_journal_host_load(void *closure,
	void (*callback)(void *, struct gfarm_internal_host_info *))
{
	return (store_ops->host_load(closure, callback));
}

static gfarm_error_t
db_journal_user_load(void *closure,
	void (*callback)(void *, struct gfarm_user_info *))
{
	return (store_ops->user_load(closure, callback));
}

static gfarm_error_t
db_journal_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	return (store_ops->group_load(closure, callback));
}

static gfarm_error_t
db_journal_inode_load(
	void *closure,
	void (*callback)(void *, struct gfs_stat *))
{
	return (store_ops->inode_load(closure, callback));
}

static gfarm_error_t
db_journal_inode_cksum_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	return (store_ops->inode_cksum_load(closure, callback));
}

static gfarm_error_t
db_journal_filecopy_remove_by_host(gfarm_uint64_t seqnum, char *hostname)
{
	return (store_ops->filecopy_remove_by_host(seqnum, hostname));
}

static gfarm_error_t
db_journal_filecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (store_ops->filecopy_load(closure, callback));
}

static gfarm_error_t
db_journal_deadfilecopy_remove_by_host(gfarm_uint64_t seqnum, char *hostname)
{
	return (store_ops->deadfilecopy_remove_by_host(seqnum, hostname));
}

static gfarm_error_t
db_journal_deadfilecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	return (store_ops->deadfilecopy_load(closure, callback));
}

static gfarm_error_t
db_journal_direntry_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	return (store_ops->direntry_load(closure, callback));
}

static gfarm_error_t
db_journal_symlink_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (store_ops->symlink_load(closure, callback));
}

static gfarm_error_t
db_journal_xattr_get(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	return (store_ops->xattr_get(seqnum, arg));
}

static gfarm_error_t
db_journal_xattr_load(void *closure,
		void (*callback)(void *, struct xattr_info *))
{
	return (store_ops->xattr_load(closure, callback));
}

static gfarm_error_t
db_journal_xmlattr_find(gfarm_uint64_t seqnum,
	struct db_xmlattr_find_arg *arg)
{
	return (store_ops->xmlattr_find(seqnum, arg));
}

static gfarm_error_t
db_journal_quota_load(void *closure, int is_group,
	void (*callback)(void *, struct gfarm_quota_info *))
{
	return (store_ops->quota_load(closure, is_group, callback));
}

static gfarm_error_t
db_journal_seqnum_get(const char *name, gfarm_uint64_t *seqnump)
{
	return (store_ops->seqnum_get(name, seqnump));
}

static gfarm_error_t
db_journal_seqnum_add(struct db_seqnum_arg *arg)
{
	return (store_ops->seqnum_add(arg));
}

static gfarm_error_t
db_journal_seqnum_modify(struct db_seqnum_arg *arg)
{
	return (store_ops->seqnum_modify(arg));
}

static gfarm_error_t
db_journal_seqnum_remove(char *name)
{
	return (store_ops->seqnum_remove(name));
}

static gfarm_error_t
db_journal_seqnum_load(void *closure,
	void (*callback)(void *, struct db_seqnum_arg *))
{
	return (store_ops->seqnum_load(closure, callback));
}

static gfarm_error_t
db_journal_mdhost_load(void *closure,
	void (*callback)(void *, struct gfarm_metadb_server *))
{
	return (store_ops->mdhost_load(closure, callback));
}

/**********************************************************/

struct db_ops db_journal_ops = {
	db_journal_noaction,
	db_journal_terminate,

	db_journal_write_begin,
	db_journal_write_end,

	db_journal_write_host_add,
	db_journal_write_host_modify,
	db_journal_write_host_remove,
	db_journal_host_load,

	db_journal_write_user_add,
	db_journal_write_user_modify,
	db_journal_write_user_remove,
	db_journal_user_load,

	db_journal_write_group_add,
	db_journal_write_group_modify,
	db_journal_write_group_remove,
	db_journal_group_load,

	db_journal_write_inode_add,
	db_journal_write_inode_modify,
	db_journal_write_inode_gen_modify,
	db_journal_write_inode_nlink_modify,
	db_journal_write_inode_size_modify,
	db_journal_write_inode_mode_modify,
	db_journal_write_inode_user_modify,
	db_journal_write_inode_group_modify,
	db_journal_write_inode_atime_modify,
	db_journal_write_inode_mtime_modify,
	db_journal_write_inode_ctime_modify,
	db_journal_inode_load,

	db_journal_write_inode_cksum_add,
	db_journal_write_inode_cksum_modify,
	db_journal_write_inode_cksum_remove,
	db_journal_inode_cksum_load,

	db_journal_write_filecopy_add,
	db_journal_write_filecopy_remove,
	db_journal_filecopy_remove_by_host,
			/* only called at initialization, bypass journal */
	db_journal_filecopy_load,

	db_journal_write_deadfilecopy_add,
	db_journal_write_deadfilecopy_remove,
	db_journal_deadfilecopy_remove_by_host,
			/* only called at initialization, bypass journal */
	db_journal_deadfilecopy_load,

	db_journal_write_direntry_add,
	db_journal_write_direntry_remove,
	db_journal_direntry_load,

	db_journal_write_symlink_add,
	db_journal_write_symlink_remove,
	db_journal_symlink_load,

	db_journal_write_xattr_add,
	db_journal_write_xattr_modify,
	db_journal_write_xattr_remove,
	db_journal_write_xattr_removeall,
	db_journal_xattr_get,
	db_journal_xattr_load,
	db_journal_xmlattr_find,

	db_journal_write_quota_add,
	db_journal_write_quota_modify,
	db_journal_write_quota_remove,
	db_journal_quota_load,

	db_journal_seqnum_get,
	db_journal_seqnum_add,
	db_journal_seqnum_modify,
	db_journal_seqnum_remove,
	db_journal_seqnum_load,

	db_journal_write_mdhost_add,
	db_journal_write_mdhost_modify,
	db_journal_write_mdhost_remove,
	db_journal_mdhost_load,

	db_journal_write_fsngroup_modify,
};
