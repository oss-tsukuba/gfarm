/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/param.h>
#ifdef DEBUG_JOURNAL
#include <sys/time.h>
#endif

#include <gfarm/gfarm.h>

#include "gfutil.h"
#ifdef DEBUG_JOURNAL
#include "timer.h"
#endif

#include "metadb_common.h"
#include "xattr_info.h"
#include "quota_info.h"
#include "quota.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#ifdef DEBUG_JOURNAL
#include "gfs_profile.h"
#endif
#include "config.h"

#include "subr.h"
#include "thrsubr.h"
#include "host.h"
#include "user.h"
#include "group.h"
#include "inode.h"
#include "journal_file.h"
#include "db_common.h"
#include "db_access.h"
#include "db_ops.h"
#include "db_journal.h"

#define JOURNAL_SEQNUM_NOT_SET		UINT64_MAX
#define JOURNAL_W_XDR	journal_file_writer_xdr( \
	journal_file_writer(self_jf))

#define NON_NULL_STR(s) ((s) ? (s) : "")

#ifdef ENABLE_JOURNAL

static struct journal_file	*self_jf;
static const struct db_ops	*journal_apply_ops;

static gfarm_uint64_t	journal_seqnum = JOURNAL_SEQNUM_NOT_SET;
static int		journal_transaction_nesting = 0;

static void
db_seqnum_load_callback(void *closure, struct db_seqnum_arg *a)
{
	if (a->name == NULL || strlen(a->name) == 0)
		journal_seqnum = a->value;
	free(a->name);
}

static gfarm_error_t
db_journal_initialize(void)
{
	gfarm_error_t e;
	char path[MAXPATHLEN + 1];
	const char *journal_dir = gfarm_journal_dir();
	const char *diag = "db_journal_initialize";
#ifdef DEBUG_JOURNAL
	gfarm_timerval_t t1, t2;
	double ts;
#endif

	if (journal_dir == NULL) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_error(GFARM_MSG_UNFIXED,
		    "gfarm_journal_file_dir is empty : %s",
		    gfarm_error_string(e));
		return (e);
	}
	if ((e = db_seqnum_load(NULL, db_seqnum_load_callback))
	    != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "db_seqnum_load : %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (journal_seqnum == JOURNAL_SEQNUM_NOT_SET) {
		do {
			if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR)
				break;
			if ((e = db_seqnum_add("", 1)) != GFARM_ERR_NO_ERROR)
				break;
			e = db_end(diag);
		} while (0);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "failed to add the update sequence number to db"
			    " : %s", gfarm_error_string(e));
			return (e);
		}
	}

	snprintf(path, MAXPATHLEN, "%s/%010d.gmj", journal_dir, 0);
#ifdef DEBUG_JOURNAL
	gfs_profile_set();
	gfarm_gettimerval(&t1);
#endif
	if ((e = journal_file_open(path, gfarm_journal_max_size(),
	    journal_seqnum, &self_jf, GFARM_JOURNAL_RDWR))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_server_journal_file_open : %s",
		    gfarm_error_string(e));
		return (e);
	}
#ifdef DEBUG_JOURNAL
	gfarm_gettimerval(&t2);
	ts = gfarm_timerval_sub(&t2, &t1);
	gflog_info(GFARM_MSG_UNFIXED,
	    "DEBUG_JOURNAL : journal_file_open : %10.5lf sec", ts);
#endif

	return (GFARM_ERR_NO_ERROR);
}

void
db_journal_set_apply_ops(const struct db_ops *apply_ops)
{
	journal_apply_ops = apply_ops;
}

static gfarm_error_t
db_journal_terminate(void)
{
	journal_file_close(self_jf);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_begin(gfarm_uint64_t seqnum, void *arg)
{
	++journal_transaction_nesting;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_end(gfarm_uint64_t seqnum, void *arg)
{
	if (journal_transaction_nesting > 0)
		--journal_transaction_nesting;
	return (GFARM_ERR_NO_ERROR);
}

/* PREREQUISITE: giant_lock */
gfarm_uint64_t
db_journal_next_seqnum(void)
{
	return (++journal_seqnum);
}

static gfarm_error_t
db_journal_read_string(struct gfp_xdr *xdr, enum journal_operation ope,
	char **strp)
{
	gfarm_error_t e;
	int eof;

	if ((e = gfp_xdr_recv(xdr, 1, &eof, "s", strp))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	for (i = 0; i < n; ++i) {
		if ((e = gfp_xdr_send_size_add(sizep, "s",
		    ary[i])) != GFARM_ERR_NO_ERROR) {
			GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	for (i = 0; i < n; ++i) {
		if ((e = gfp_xdr_send(JOURNAL_W_XDR, "s",
		    ary[i])) != GFARM_ERR_NO_ERROR) {
			GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_recv", e, ope);
		return (e);
	}
	GFARM_MALLOC_ARRAY(ary, n + 1);
	if (ary == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC_ARRAY", e, ope);
		return (e);
	}
	for (i = 0; i < n; ++i) {
		if ((e = gfp_xdr_recv(xdr, 1, &eof, "s",
		    &ary[i])) != GFARM_ERR_NO_ERROR) {
			GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
	free(arg->source_path);
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

/**********************************************************/

/* PREREQUISITE: giant_lock */
static gfarm_error_t
db_journal_write(gfarm_uint64_t seqnum, enum journal_operation ope,
	void *arg, journal_size_add_op_t size_add_op,
	journal_send_add_op_t send_op)
{
	gfarm_error_t e = journal_file_write(self_jf, seqnum, ope, arg,
		size_add_op, send_op);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "db_journal_file_write : %s",
		    gfarm_error_string(e)); /* exit */
		/* FIXME change to read-only mode */
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_string_size_add(enum journal_operation ope,
	size_t *sizep, void *arg)
{
	gfarm_error_t e;
	char *str = arg;

	if ((e = gfp_xdr_send_size_add(sizep, "s", str))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, hi->nhostaliases, hi->hostaliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, hi->nhostaliases, hi->hostaliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_recv", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &hi->nhostaliases, &hi->hostaliases)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(hi, 0, sizeof(*hi));
	if ((e = db_journal_read_host_core(xdr, ope, hi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_write_host_size_add", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send_size_add(sizep, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, m->add_count, m->add_aliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_xdr_send_string_array_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, m->del_count, m->del_aliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_write_host_core", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, m->add_count, m->add_aliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_xdr_send_string_array", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, m->del_count, m->del_aliases))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	hi = &arg->hi;
	if ((e = db_journal_read_host_core(xdr, ope, hi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_read_host_core", e, ope);
		goto end;
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i",
	    &arg->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_recv", e, ope);
		goto end;
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &arg->add_count, &arg->add_aliases)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_xdr_recv_string_array", e, ope);
		goto end;
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &arg->del_count, &arg->del_aliases)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(ui, 0, sizeof(*ui));
	if ((e = db_journal_read_user_core(xdr, ope, ui))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_write_user_size_add", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send_size_add(sizep, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_write_user_core", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	ui = &arg->ui;
	if ((e = db_journal_read_user_core(xdr, ope, ui))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_read_user_core", e, ope);
		goto end;
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i",
	    &arg->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep, ope,
		gi->nusers, gi->usernames)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(ope,
		gi->nusers, gi->usernames)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_recv", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &gi->nusers, &gi->usernames)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(gi, 0, sizeof(*gi));
	if ((e = db_journal_read_group_core(xdr, ope, gi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_write_group_size_add", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send_size_add(sizep, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, m->add_count, m->add_users))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_xdr_send_string_array_size_add", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array_size_add(sizep,
	    ope, m->del_count, m->del_users))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_write_group_core", e, ope);
		return (e);
	}
	if ((e = gfp_xdr_send(JOURNAL_W_XDR, "i",
	    m->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, m->add_count, m->add_users))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_xdr_send_string_array", e, ope);
		return (e);
	}
	if ((e = db_journal_xdr_send_string_array(
	    ope, m->del_count, m->del_users))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	gi = &arg->gi;
	if ((e = db_journal_read_group_core(xdr, ope, gi))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_read_group_core", e, ope);
		goto end;
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof, "i",
	    &arg->modflags)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send", e, ope);
		goto end;
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &arg->add_count, &arg->add_users)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "db_journal_xdr_recv_string_array", e, ope);
		goto end;
	}
	if ((e = db_journal_xdr_recv_string_array(xdr, ope,
	    &arg->del_count, &arg->del_users)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_UINT64_XDR_FMT,
	    &arg->inum,
	    &arg->uint64)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_send", e, ope);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_write_inode_uint32_modify(gfarm_uint32_t seqnum,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_UINT32_XDR_FMT,
	    &arg->inum,
	    &arg->uint32)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_STR_XDR_FMT,
	    &arg->inum,
	    &arg->string)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_TIMESPEC_XDR_FMT,
	    &arg->inum,
	    &arg->time.tv_sec,
	    &arg->time.tv_nsec)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_INUM_XDR_FMT,
	    &arg->inum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_INODE_CKSUM_XDR_FMT,
	    &arg->inum,
	    &arg->type,
	    &arg->sum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_FILECOPY_XDR_FMT,
	    &arg->inum,
	    &arg->hostname)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_DEADFILECOPY_XDR_FMT,
	    &arg->inum,
	    &arg->igen,
	    &arg->hostname)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(arg, 0, sizeof(*arg));
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_DIRENTRY_XDR_FMT,
	    &arg->dir_inum,
	    &arg->entry_name,
	    &arg->entry_inum)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
	struct db_symlink_arg *arg;
	int eof;
	const enum journal_operation ope = GFM_JOURNAL_SYMLINK_ADD;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_SYMLINK_XDR_FMT,
	    &arg->inum,
	    &arg->source_path)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "gfp_xdr_recv", e, ope);
	}
	if (e == GFARM_ERR_NO_ERROR)
		*argp = arg;
	else {
		db_journal_symlink_arg_destroy(arg);
		*argp = NULL;
	}
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "GFARM_MALLOC", GFARM_ERR_NO_MEMORY, ope);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfp_xdr_recv(xdr, 1, &eof,
	    GFM_JOURNAL_QUOTA_REMOVE_XDR_FMT,
	    &arg->is_group,
	    &arg->name)) != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
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
		free(obj);
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
	default:
		e = GFARM_ERR_INVALID_ARGUMENT;
		break;
	}

	if (e != GFARM_ERR_NO_ERROR) {
		GFLOG_DEBUG_WITH_OPE(GFARM_MSG_UNFIXED,
		    "read record", e, ope);
	}
	return (e);
}

static gfarm_error_t
db_journal_ops_call(const struct db_ops *ops, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, size_t length, const char *diag)
{
	gfarm_error_t e;

	switch (ope) {
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
		e = ops->deadfilecopy_remove(seqnum, obj); break;
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
	default:
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s : seqnum=%lld ope=%d",
		    diag, (unsigned long long)seqnum, ope); /* exit */
		return (e);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : seqnum=%lld ope=%s : %s",
		    diag, (unsigned long long)seqnum,
		    journal_operation_name(ope), gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
db_journal_store_op(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, size_t length)
{
	const struct db_ops *ops = op_arg;

#ifdef DEBUG_JOURNAL
	gflog_info(GFARM_MSG_UNFIXED,
	    "DEBUG_JOURNAL: store seqnum=%" GFARM_PRId64 " ope=%s", seqnum,
	    journal_operation_name(ope));
#endif
	return (db_journal_ops_call(ops, seqnum, ope, obj, length,
		"db_journal_store_op"));
}

static gfarm_error_t
db_journal_apply_op(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *obj, size_t length)
{
	gfarm_error_t e;

#ifdef DEBUG_JOURNAL
	gflog_info(GFARM_MSG_UNFIXED,
	    "DEBUG_JOURNAL: apply seqnum=%lld ope=%s",
	    (unsigned long long)seqnum, journal_operation_name(ope));
#endif
	giant_lock();
	if ((e = db_journal_ops_call(store_ops, seqnum, ope,
	    obj, length, "db_journal_apply_op[store]"))
	    == GFARM_ERR_NO_ERROR) {
		e = db_journal_ops_call(journal_apply_ops, seqnum, ope,
		    obj, length, "db_journal_apply_op[apply]");
	}
	giant_unlock();
	return (e);
}

/* PREREQUISITE: giant_lock */
gfarm_error_t
db_journal_read(struct journal_file_reader *reader, void *op_arg,
	journal_post_read_op_t post_read_op, int *eofp)
{
	return (journal_file_read(reader, op_arg,
	    db_journal_read_ops, post_read_op,
	    db_journal_ops_free, eofp));
}

void *
db_journal_store_thread(void *arg)
{
	int eof;
	gfarm_error_t e;
	struct journal_file_reader *reader =
		journal_file_main_reader(self_jf);

	for (;;) {
		if ((e = db_journal_read(reader,
		    (void *)store_ops, /* UNCONST */
		    db_journal_store_op, &eof)) != GFARM_ERR_NO_ERROR) {
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "failed to read journal or store to db : %s",
			    gfarm_error_string(e)); /* exit */
			/* FIXME change to read-only mode */
		}
	}
	return (NULL);
}

void *
db_journal_slave_thread(void *arg)
{
	int eof;
	gfarm_error_t e;
	struct journal_file_reader *reader = journal_file_main_reader(self_jf);

	for (;;) {
		if ((e = db_journal_read(reader,
		    (void *)store_ops, /* UNCONST */
		    db_journal_apply_op, &eof)) != GFARM_ERR_NO_ERROR) {
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "failed to read journal or apply to memory/db : %s",
			    gfarm_error_string(e)); /* exit */
		}
	}
	return (NULL);
}

gfarm_error_t
db_journal_add_reader(struct journal_file_reader **readerp)
{
	return (journal_file_reader_dup(journal_file_main_reader(self_jf),
	    readerp));
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

	for (fi = fi_list, fin = fi ? fi->next : NULL; fi != NULL; fi = fin,
	    fin = fi ? fi->next : NULL) {
		free(fi->rec);
		free(fi);
	}
}

/* PREREQUISITE: giant_lock */
gfarm_error_t
db_journal_fetch(struct journal_file_reader *reader,
	gfarm_uint64_t min_seqnum, char **datap, int *lenp,
	gfarm_uint64_t *from_seqnump, gfarm_uint64_t *to_seqnump,
	const char *diag)
{
#define FETCH_SIZE_THRESHOLD 8000
	gfarm_error_t e;
	gfarm_uint64_t cur_seqnum, seqnum;
	char *rec, *recs, *p;
	int eof, num_fi = 0;
	gfarm_uint32_t rec_len, all_len = 0;
	struct db_journal_fetch_info *fi, *fi0 = NULL, *fih = NULL;
	gfarm_uint64_t from_sn = 0, to_sn;

	cur_seqnum = journal_seqnum;
	if (cur_seqnum < min_seqnum) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : invalid seqnum (cur:%llu < target:%llu)",
		    diag, (unsigned long long)cur_seqnum,
		    (unsigned long long)min_seqnum);
		return (GFARM_ERR_EXPIRED);
	}
	if (journal_file_reader_is_invalid(reader)) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s : target journal records are expired", diag);
		return (GFARM_ERR_EXPIRED);
	}
	for (;;) {
		if ((e = journal_file_read_serialized(reader, &rec, &rec_len,
		    &seqnum, &eof)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s : %s", diag, gfarm_error_string(e));
			return (e);
		}
		if (eof)
			break;
		if (seqnum < min_seqnum)
			free(rec);
		else {
			GFARM_MALLOC(fi);
			if (fi == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				gflog_error(GFARM_MSG_UNFIXED,
				    "%s : %s", diag, gfarm_error_string(e));
				free(fi);
				goto error;
			}
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
		*datap = NULL;
		*lenp = 0;
		*from_seqnump = 0;
		*to_seqnump = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	fi->next = NULL;
	to_sn = seqnum;
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_UNFIXED,
	    "%s : fetch %llu to %llu (n=%d)", diag,
	    (unsigned long long)from_sn, (unsigned long long)to_sn, num_fi);
#endif
	if (fih == NULL) {
		*datap = NULL;
		*lenp = 0;
		*from_seqnump = 0;
		*to_seqnump = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	GFARM_MALLOC_ARRAY(recs, all_len);
	if (recs == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED,
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

/**********************************************************/
/* delegated functions */

static gfarm_error_t
db_journal_host_load(void *closure,
	void (*callback)(void *, struct gfarm_host_info *))
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
db_journal_filecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (store_ops->filecopy_load(closure, callback));
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

/**********************************************************/

struct db_ops db_journal_ops = {
	db_journal_initialize,
	db_journal_terminate,

	db_journal_begin,
	db_journal_end,

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
	db_journal_filecopy_load,

	db_journal_write_deadfilecopy_add,
	db_journal_write_deadfilecopy_remove,
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

	db_journal_seqnum_add,
	db_journal_seqnum_modify,
	db_journal_seqnum_remove,
	db_journal_seqnum_load,
};

#endif /* ENABLE_JOURNAL */
