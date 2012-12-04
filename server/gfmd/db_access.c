/*
 * $Id$
 */

#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "internal_host_info.h"

#include "gfutil.h"
#include "thrsubr.h"

#include "config.h"

#include "subr.h"
#include "quota_info.h"
#include "metadb_server.h"
#include "quota.h"
#include "mdhost.h"
#include "journal_file.h"	/* for enum journal_operation */
#include "db_access.h"
#include "db_ops.h"
#include "db_journal.h"
#include "db_journal_apply.h"

#define ALIGNMENT 8
#define ALIGN(offset)	(((offset) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

typedef void (*dbq_entry_func_callback_t)(gfarm_error_t, void *);
typedef gfarm_error_t (*db_enter_func_t)(dbq_entry_func_t, void *, int);

struct dbq_callback_arg {
	dbq_entry_func_t func;
	void *data;
	dbq_entry_func_callback_t cbfunc;
	void *cbdata;
};

struct dbq_entry {
	dbq_entry_func_t func;
	void *data;
	gfarm_uint64_t seqnum;
};

struct dbq {
	pthread_mutex_t mutex;
	pthread_cond_t nonempty, nonfull, finished;

	int n, in, out, quitting, quited;
	struct dbq_entry *entries;
} dbq;

static pthread_mutex_t db_access_mutex = PTHREAD_MUTEX_INITIALIZER;
#define DB_ACCESS_MUTEX_DIAG "db_access_mutex"

static gfarm_error_t db_journal_enter(dbq_entry_func_t, void *, int);
static gfarm_error_t dbq_enter1(dbq_entry_func_t, void *, int);

static const struct db_ops *ops;
const struct db_ops *store_ops;
static db_enter_func_t db_enter_op = dbq_enter1;

static int transaction_nesting = 0;

gfarm_error_t
dbq_init(struct dbq *q)
{
	static const char diag[] = "dbq_init";

	GFARM_MALLOC_ARRAY(q->entries, gfarm_metadb_dbq_size);
	if (q->entries == NULL) {
		gflog_debug(GFARM_MSG_1001999,
			"allocation of 'q->entries' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	gfarm_mutex_init(&q->mutex, diag, "mutex");
	gfarm_cond_init(&q->nonempty, diag, "nonempty");
	gfarm_cond_init(&q->nonfull, diag, "nonfull");
	gfarm_cond_init(&q->finished, diag, "finished");
	q->n = q->in = q->out = q->quitting = q->quited = 0;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
dbq_wait_to_finish(struct dbq *q)
{
	static const char diag[] = "dbq_wait_to_finish";

	gfarm_mutex_lock(&q->mutex, diag, "mutex");
	q->quitting = 1;
	while (!q->quited) {
		gfarm_cond_signal(&q->nonempty, diag, "nonempty");
		gfarm_cond_wait(&q->finished, &q->mutex, diag, "finished");
	}
	gfarm_mutex_unlock(&q->mutex, diag, "mutex");
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_enter(dbq_entry_func_t func, void *data, int with_seqnum)
{
	gfarm_error_t e;
	int transaction = 0;

	if (with_seqnum && transaction_nesting == 0) {
		transaction = 1;
		if ((e = db_enter_op(ops->begin, NULL, with_seqnum))
		    != GFARM_ERR_NO_ERROR)
			return (e);
	}
	e = db_enter_op(func, data, with_seqnum);
	if (transaction) {
		if (e == GFARM_ERR_NO_ERROR)
			e = db_enter_op(ops->end, NULL, with_seqnum);
	}
	return (e);
}

static gfarm_error_t
db_enter_nosn(dbq_entry_func_t func, void *data)
{
	return (db_enter(func, data, 0));
}

static gfarm_error_t
db_enter_sn(dbq_entry_func_t func, void *data)
{
	return (db_enter(func, data, 1));
}

static gfarm_error_t
dbq_enter0(struct dbq *q, dbq_entry_func_t func, void *data, int with_seqnum)
{
	gfarm_error_t e;
	static const char diag[] = "dbq_enter";
	struct dbq_entry *ent;

	assert(!gfarm_get_metadb_replication_enabled() || with_seqnum == 0);

	gfarm_mutex_lock(&q->mutex, diag, "mutex");
	if (q->quitting) {
		/*
		 * Because dbq_wait_to_finish() is only called while
		 * giant_lock() is held, the dbq shouldn't be partial state.
		 * So, this doesn't cause metadata inconsistency .
		 */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gfarm_cond_signal(&q->nonempty, diag, "nonempty");
	} else {
		e = GFARM_ERR_NO_ERROR;
		while (q->n >= gfarm_metadb_dbq_size) {
			gfarm_cond_wait(&q->nonfull, &q->mutex,
			    diag, "nonfull");
		}
		ent = &q->entries[q->in];
		ent->func = func;
		ent->data = data;
		q->in++;
		if (q->in >= gfarm_metadb_dbq_size)
			q->in = 0;
		q->n++;
		gfarm_cond_signal(&q->nonempty, diag, "nonempty");
	}
	gfarm_mutex_unlock(&q->mutex, diag, "mutex");
	return (e);
}

static gfarm_error_t
dbq_enter1(dbq_entry_func_t func, void *data, int with_seqnum)
{
	return (dbq_enter0(&dbq, func, data, with_seqnum));
}

static gfarm_error_t
dbq_enter(dbq_entry_func_t func, void *data)
{
	return (dbq_enter1(func, data, 0));
}

/* DO NOT REMOVE: this interfaces is provided for a private extension */
/* The official gfmd source code shouldn't use these interface */
gfarm_error_t
gfarm_dbq_enter(dbq_entry_func_t func, void *data)
{
	return (dbq_enter(func, data));
}

static gfarm_error_t
dbq_call_callback(gfarm_uint64_t seqnum, void *a)
{
	gfarm_error_t e;
	struct dbq_callback_arg *arg = (struct dbq_callback_arg *)a;

	e = (*arg->func)(seqnum, arg->data);
	if (arg->cbfunc != NULL) {
		(*arg->cbfunc)(e, arg->cbdata);
	}
	free(arg);
	return (e);
}

static gfarm_error_t
dbq_enter_withcallback(dbq_entry_func_t func, void *data,
		dbq_entry_func_callback_t cbfunc, void *cbdata)
{
	gfarm_error_t e;
	struct dbq_callback_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002000,
			"allocation of 'dbq_callback_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->func = func;
	arg->data = data;
	arg->cbfunc = cbfunc;
	arg->cbdata = cbdata;
	e = dbq_enter(dbq_call_callback, arg);
	if (e != GFARM_ERR_NO_ERROR)
		free(arg);
	return (e);
}

#define UNINITIALIZED_GFARM_ERROR	(-1)

void
db_waitctx_init(struct db_waitctx *ctx)
{
	static const char diag[] = "db_waitctx_init";

	if (ctx != NULL) {
		gfarm_mutex_init(&ctx->lock, diag, "lock");
		gfarm_cond_init(&ctx->cond, diag, "cond");
		ctx->e = UNINITIALIZED_GFARM_ERROR;
	}
}

void
db_waitctx_fini(struct db_waitctx *ctx)
{
	static const char diag[] = "db_waitctx_fini";

	if (ctx != NULL) {
		gfarm_cond_destroy(&ctx->cond, diag, "cond");
		gfarm_mutex_destroy(&ctx->lock, diag, "lock");
	}
}

static void
dbq_done_callback(gfarm_error_t e, void *c)
{
	struct db_waitctx *ctx = (struct db_waitctx *)c;
	static const char diag[] = "dbq_done_callback";

	if (ctx != NULL) {
		gfarm_mutex_lock(&ctx->lock, diag, "lock");
		ctx->e = e;
		gfarm_cond_signal(&ctx->cond, diag, "cond");
		gfarm_mutex_unlock(&ctx->lock, diag, "lock");
	}
}

gfarm_error_t
dbq_waitret(struct db_waitctx *ctx)
{
	gfarm_error_t e;
	static const char diag[] = "dbq_waitret";

	if (ctx == NULL)
		return (GFARM_ERR_NO_ERROR);

	gfarm_mutex_lock(&ctx->lock, diag, "lock");
	while (ctx->e == UNINITIALIZED_GFARM_ERROR) {
		gfarm_cond_wait(&ctx->cond, &ctx->lock, diag, "cond");
	}
	e = ctx->e;
	gfarm_mutex_unlock(&ctx->lock, diag, "lock");
	return (e);
}

static gfarm_error_t
dbq_enter_for_waitret(
	dbq_entry_func_t func, void *data, struct db_waitctx *ctx)
{
	return (dbq_enter_withcallback(func, data,
			dbq_done_callback, ctx));
}

/* DO NOT REMOVE: this interfaces is provided for a private extension */
/* The official gfmd source code shouldn't use these interface */
gfarm_error_t
gfarm_dbq_enter_for_waitret(
	dbq_entry_func_t func, void *data, struct db_waitctx *ctx)
{
	return (dbq_enter_for_waitret(func, data, ctx));
}

gfarm_error_t
dbq_delete(struct dbq *q, struct dbq_entry *entp)
{
	gfarm_error_t e;
	static const char diag[] = "dbq_delete";

	gfarm_mutex_lock(&q->mutex, diag, "mutex");
	while (q->n <= 0 && !q->quitting) {
		gfarm_cond_wait(&q->nonempty, &q->mutex, diag, "nonempty");
	}
	if (q->n <= 0) {
		assert(q->quitting);
		q->quited = 1;
		gfarm_cond_signal(&q->finished, diag, "finished");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else { /* (q->n > 0) */
		e = GFARM_ERR_NO_ERROR;
		*entp = q->entries[q->out++];
		if (q->out >= gfarm_metadb_dbq_size)
			q->out = 0;
		if (q->n-- >= gfarm_metadb_dbq_size) {
			gfarm_cond_signal(&q->nonfull, diag, "nonfull");
		}
	}
	gfarm_mutex_unlock(&q->mutex, diag, "mutex");
	return (e);
}

int
db_getfreenum(void)
{
	struct dbq *q = &dbq;
	int freenum;
	static const char diag[] = "db_getfreenum";

	/*
	 * This function is made only for gfm_server_findxmlattr().
	 */
	gfarm_mutex_lock(&q->mutex, diag, "mutex");
	freenum = (gfarm_metadb_dbq_size - q->n);
	gfarm_mutex_unlock(&q->mutex, diag, "mutex");
	return (freenum);
}

/* PREREQUISITE: mdhost_global_mutex */
static gfarm_error_t
db_journal_enter(dbq_entry_func_t func, void *data, int with_seqnum)
{
	gfarm_uint64_t seqnum;

	seqnum = with_seqnum ? db_journal_next_seqnum() : 0;
	return (func(seqnum, data));
}

/* DO NOT REMOVE: this interfaces is provided for a private extension */
/* The official gfmd source code shouldn't use these interface */
const struct db_ops *
db_get_ops(void)
{
	return (gfarm_get_metadb_replication_enabled() ?
		store_ops : ops);
}

gfarm_error_t
db_use(const struct db_ops *o)
{
	if (gfarm_get_metadb_replication_enabled()) {
		store_ops = o;
		ops = &db_journal_ops;
		db_enter_op = &db_journal_enter;
	} else {
		ops = o;
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
db_initialize(void)
{
	dbq_init(&dbq);
	if (gfarm_get_metadb_replication_enabled())
		return ((*store_ops->initialize)());
	else
		return ((*ops->initialize)());
}

gfarm_error_t
db_terminate(void)
{
	gfarm_error_t e;
	static const char *diag = "db_terminate";

	gflog_info(GFARM_MSG_1000406, "try to stop database syncer");
	dbq_wait_to_finish(&dbq);
	gflog_info(GFARM_MSG_1000407, "terminating the database");
	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ops->terminate();
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}

pthread_mutex_t *
get_db_access_mutex(void)
{
	return (&db_access_mutex);
}

void *
db_thread(void *arg)
{
	gfarm_error_t e;
	struct dbq_entry ent;
	static const char *diag = "db_thread";

	for (;;) {
		e = dbq_delete(&dbq, &ent);
		if (e == GFARM_ERR_NO_ERROR) {
			/* lock to avoid race condition between
			 * db_journal_store_thread. */
			gfarm_mutex_lock(&db_access_mutex, diag,
			    DB_ACCESS_MUTEX_DIAG);

			/* Do not execute a function that writes to database
			 * when metadata-replication enabled.
			 * Because we pass seqnum as zero. */
			do {
				e = (*ent.func)(0, ent.data);
			} while (e == GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED);

			gfarm_mutex_unlock(&db_access_mutex, diag,
			    DB_ACCESS_MUTEX_DIAG);
		} else if (e == GFARM_ERR_NO_SUCH_OBJECT)
			break;
	}
	return (NULL);
}

gfarm_error_t
db_begin(const char *diag)
{
	gfarm_error_t e;

	assert(transaction_nesting == 0);

	if (transaction_nesting == 0) {
		++transaction_nesting;
		e = db_enter_sn((dbq_entry_func_t)ops->begin, NULL);
	} else
		e = db_enter_nosn((dbq_entry_func_t)ops->begin, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000408,
		    "%s: db_begin(): %s", diag, gfarm_error_string(e));
		--transaction_nesting;
	}
	return (e);
}

gfarm_error_t
db_end(const char *diag)
{
	gfarm_error_t e;

	assert(transaction_nesting == 1);

	if (transaction_nesting > 0) {
		e = db_enter_sn((dbq_entry_func_t)ops->end, NULL);
		--transaction_nesting;
	} else
		e = db_enter_nosn((dbq_entry_func_t)ops->end, NULL);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000409,
		    "%s: db_end(): %s", diag, gfarm_error_string(e));
	return (e);
}

void *
db_host_dup(const struct gfarm_host_info *hi, size_t size)
{
	struct gfarm_host_info *r;
	size_t hsize = strlen(hi->hostname) + 1;
	size_t asize = strlen(hi->architecture) + 1;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	r = NULL;
#endif
	/* XXX FIXME missing hostaliases */
	/* LDAP needs this extra NULL at the end of r->hostaliases[] */
	sz = gfarm_size_add(&overflow, size, hsize + asize);
	if (!overflow)
		r = malloc(sz);
	if (overflow || r == NULL) {
		gflog_debug(GFARM_MSG_1002001,
			"allocation of 'gfarm_host_info' failed or overflow");
		return (NULL);
	}
	r->hostname = (char *)r + size;
	r->architecture = r->hostname + hsize;

	strcpy(r->hostname, hi->hostname);
	r->port = hi->port;
	r->nhostaliases = 0;
	r->hostaliases = NULL;
	/* LDAP needs this extra NULL at the end of r->hostaliases[] */
	strcpy(r->architecture, hi->architecture);
	r->ncpu = hi->ncpu;
	r->flags = hi->flags;
	return (r);
}

gfarm_error_t
db_host_add(const struct gfarm_host_info *hi)
{
	struct gfarm_host_info *h = db_host_dup(hi, sizeof(*hi));

	if (h == NULL) {
		gflog_debug(GFARM_MSG_1002002, "db_host_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->host_add, h));
}

struct db_host_modify_arg *
db_host_modify_arg_alloc(const struct gfarm_host_info *hi,
	int modflags,
	int add_count, const char **add_aliases,
	int del_count, const char **del_aliases)
{
	struct db_host_modify_arg *arg = db_host_dup(hi, sizeof(*arg));

	if (arg == NULL)
		return (NULL);
	arg->modflags = modflags;
	/* XXX FIXME missing hostaliases */
	arg->add_count = 0; arg->add_aliases = NULL;
	arg->del_count = 0; arg->del_aliases = NULL;
	return (arg);
}

gfarm_error_t
db_host_modify(const struct gfarm_host_info *hi,
	int modflags,
	int add_count, const char **add_aliases,
	int del_count, const char **del_aliases)
{
	struct db_host_modify_arg *arg = db_host_modify_arg_alloc(hi, modflags,
	    add_count, add_aliases, del_count, del_aliases);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1003019,
		    "db_host_modify_arg_alloc failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->host_modify, arg));
}

gfarm_error_t
db_host_remove(const char *hostname)
{
	char *h = strdup_log(hostname, "db_host_remove");

	if (h == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (db_enter_sn((dbq_entry_func_t)ops->host_remove, h));
}

gfarm_error_t
db_host_load(void *closure,
	void (*callback)(void *, struct gfarm_internal_host_info *))
{
	gfarm_error_t e;
	const char *diag = "db_host_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = (*ops->host_load)(closure, callback);
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}


void *
db_user_dup(const struct gfarm_user_info *ui, size_t size)
{
	struct gfarm_user_info *r;
	size_t usize = strlen(ui->username) + 1;
	size_t rsize = strlen(ui->realname) + 1;
	size_t hsize = strlen(ui->homedir) + 1;
	size_t gsize = strlen(ui->gsi_dn) + 1;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	r = NULL;
#endif
	sz = gfarm_size_add(&overflow, size, usize + rsize + hsize + gsize);
	if (!overflow)
		r = malloc(sz);
	if (overflow || r == NULL) {
		gflog_debug(GFARM_MSG_1002005,
			"allocation of 'gfarm_user_info' failed or overflow");
		return (NULL);
	}
	r->username = (char *)r + size;
	r->realname = r->username + usize;
	r->homedir = r->realname + rsize;
	r->gsi_dn = r->homedir + hsize;

	strcpy(r->username, ui->username);
	strcpy(r->realname, ui->realname);
	strcpy(r->homedir, ui->homedir);
	strcpy(r->gsi_dn, ui->gsi_dn);
	return (r);
}

gfarm_error_t
db_user_add(const struct gfarm_user_info *ui)
{
	struct gfarm_user_info *u = db_user_dup(ui, sizeof(*u));

	if (u == NULL) {
		gflog_debug(GFARM_MSG_1002006, "db_user_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->user_add, u));
}

struct db_user_modify_arg *
db_user_modify_arg_alloc(const struct gfarm_user_info *ui, int modflags)
{
	struct db_user_modify_arg *arg = db_user_dup(ui, sizeof(*arg));

	if (arg == NULL)
		return (NULL);
	arg->modflags = modflags;
	return (arg);
}

gfarm_error_t
db_user_modify(const struct gfarm_user_info *ui, int modflags)
{
	struct db_user_modify_arg *arg = db_user_modify_arg_alloc(
	    ui, modflags);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1003020,
		    "db_user_modify_arg_alloc failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->user_modify, arg));
}

gfarm_error_t
db_user_remove(const char *username)
{
	char *u = strdup_log(username, "db_user_remove");

	if (u == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (db_enter_sn((dbq_entry_func_t)ops->user_remove, u));
}

gfarm_error_t
db_user_load(void *closure, void (*callback)(void *, struct gfarm_user_info *))
{
	gfarm_error_t e;
	const char *diag = "db_user_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->user_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}


void *
db_group_dup(const struct gfarm_group_info *gi, size_t size)
{
	struct gfarm_group_info *r;
	size_t gsize = strlen(gi->groupname) + 1;
	size_t users_size;
	size_t sz;
	int overflow = 0;
	int i;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	r = NULL;
#endif
	size = ALIGN(size);
	users_size = 0;
	for (i = 0; i < gi->nusers; i++)
		users_size = gfarm_size_add(&overflow, users_size,
		    strlen(gi->usernames[i]) + 1);
	/* LDAP needs this extra NULL at the end of r->usernames[] */
	sz = gfarm_size_add(&overflow, size,
	    gfarm_size_add(&overflow,
		gfarm_size_mul(&overflow, sizeof(*r->usernames),
		    gfarm_size_add(&overflow, gi->nusers, 1)),
		gfarm_size_add(&overflow, gsize, users_size)));
	if (!overflow)
		r = malloc(sz);
	if (overflow || r == NULL) {
		gflog_debug(GFARM_MSG_1002009,
			"allocation of 'gfarm_group_info' failed or overflow");
		return (NULL);
	}

	r->usernames = (char **)((char *)r + size);
	r->groupname = (char *)r->usernames +
	    sizeof(*r->usernames) * (gi->nusers + 1);
	users_size = 0;
	for (i = 0; i < gi->nusers; i++) {
		r->usernames[i] = r->groupname + gsize + users_size;
		users_size += strlen(gi->usernames[i]) + 1;
	}
	/* LDAP needs this extra NULL at the end of r->usernames[] */
	r->usernames[gi->nusers] = NULL;

	strcpy(r->groupname, gi->groupname);
	r->nusers = gi->nusers;
	for (i = 0; i < gi->nusers; i++)
		strcpy(r->usernames[i], gi->usernames[i]);
	return (r);
}

gfarm_error_t
db_group_add(const struct gfarm_group_info *gi)
{
	struct gfarm_group_info *g = db_group_dup(gi, sizeof(*g));

	if (g == NULL) {
		gflog_debug(GFARM_MSG_1002010, "db_group_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->group_add, g));
}

struct db_group_modify_arg *
db_group_modify_arg_alloc(const struct gfarm_group_info *gi, int modflags,
	int add_count, const char **add_users,
	int del_count, const char **del_users)
{
	int i;
	struct db_group_modify_arg *arg;
	size_t size, users_size;
	int overflow = 0;
	char *p;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	users_size = 0;
	for (i = 0; i < add_count; i++)
		users_size = gfarm_size_add(&overflow, users_size,
		    strlen(add_users[i]) + 1);
	for (i = 0; i < del_count; i++)
		users_size = gfarm_size_add(&overflow, users_size,
		    strlen(del_users[i]) + 1);
	size = gfarm_size_add(&overflow,
	    gfarm_size_add(&overflow,
		gfarm_size_mul(&overflow, sizeof(*arg->add_users), add_count),
		gfarm_size_mul(&overflow, sizeof(*arg->del_users), del_count)),
	    gfarm_size_add(&overflow, ALIGN(sizeof(*arg)), users_size));

	if (!overflow)
		arg = db_group_dup(gi, size);
	if (overflow || arg == NULL)
		return (NULL);
	arg->add_users = (char **)((char *)arg + ALIGN(sizeof(*arg)));
	arg->del_users = (char **)((char *)arg->add_users +
	    sizeof(*arg->add_users) * add_count);
	p = (char *)arg->del_users +
	    sizeof(*arg->del_users) * del_count;
	users_size = 0;
	for (i = 0; i < add_count; i++) {
		arg->add_users[i] = p + users_size;
		users_size += strlen(add_users[i]) + 1;
	}
	for (i = 0; i < del_count; i++) {
		arg->del_users[i] = p + users_size;
		users_size += strlen(del_users[i]) + 1;
	}

	arg->modflags = modflags;
	arg->add_count = add_count;
	arg->del_count = del_count;
	for (i = 0; i < add_count; i++)
		strcpy(arg->add_users[i], add_users[i]);
	for (i = 0; i < del_count; i++)
		strcpy(arg->del_users[i], del_users[i]);
	return (arg);
}

gfarm_error_t
db_group_modify(const struct gfarm_group_info *gi, int modflags,
	int add_count, const char **add_users,
	int del_count, const char **del_users)
{
	struct db_group_modify_arg *arg = db_group_modify_arg_alloc(
	    gi, modflags, add_count, add_users, del_count, del_users);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1003021,
		    "db_group_modify_arg_alloc failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->group_modify, arg));
}

gfarm_error_t
db_group_remove(const char *groupname)
{
	char *g = strdup_log(groupname, "db_group_remove");

	if (g == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (db_enter_sn((dbq_entry_func_t)ops->group_remove, g));
}

gfarm_error_t
db_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	gfarm_error_t e;
	const char *diag = "db_group_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->group_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}


struct gfs_stat *
db_inode_dup(const struct gfs_stat *st, size_t size)
{
	struct gfs_stat *r;
	size_t usize = strlen(st->st_user) + 1;
	size_t gsize = strlen(st->st_group) + 1;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	r = NULL;
#endif
	sz = gfarm_size_add(&overflow, size, usize + gsize);
	if (!overflow)
		r = malloc(sz);
	if (overflow || r == NULL) {
		gflog_debug(GFARM_MSG_1002013,
			"allocation of 'gfs_stat' failed or overflow");
		return (NULL);
	}
	r->st_user = (char *)r + size;
	r->st_group = r->st_user + usize;

	r->st_ino = st->st_ino;
	r->st_gen = st->st_gen;
	r->st_mode = st->st_mode;
	r->st_nlink = st->st_nlink;
	strcpy(r->st_user, st->st_user);
	strcpy(r->st_group, st->st_group);
	r->st_size = st->st_size;
	r->st_ncopy = st->st_ncopy;
	r->st_atimespec = st->st_atimespec;
	r->st_mtimespec = st->st_mtimespec;
	r->st_ctimespec = st->st_ctimespec;
	return (r);
}

gfarm_error_t
db_inode_add(const struct gfs_stat *st)
{
	struct gfs_stat *i = db_inode_dup(st, sizeof(*i));

	if (i == NULL) {
		gflog_debug(GFARM_MSG_1002014, "db_inode_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->inode_add, i));
}

gfarm_error_t
db_inode_modify(const struct gfs_stat *st)
{
	struct gfs_stat *i = db_inode_dup(st, sizeof(*i));

	if (i == NULL) {
		gflog_debug(GFARM_MSG_1002015, "db_inode_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->inode_modify, i));
}

gfarm_error_t
db_inode_gen_modify(gfarm_ino_t inum, gfarm_uint64_t gen)
{
	struct db_inode_uint64_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002290,
			"allocation of 'db_inode_uint64_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	arg->uint64 = gen;
	return (db_enter_sn((dbq_entry_func_t)ops->inode_gen_modify, arg));
}

gfarm_error_t
db_inode_nlink_modify(gfarm_ino_t inum, gfarm_uint64_t nlink)
{
	struct db_inode_uint64_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002016,
			"allocation of 'db_inode_uint64_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	arg->uint64 = nlink;
	return (db_enter_sn((dbq_entry_func_t)ops->inode_nlink_modify, arg));
}

gfarm_error_t
db_inode_size_modify(gfarm_ino_t inum, gfarm_off_t size)
{
	struct db_inode_uint64_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002017,
			"allocation of 'db_inode_uint64_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	arg->uint64 = size;
	return (db_enter_sn((dbq_entry_func_t)ops->inode_size_modify, arg));
}

gfarm_error_t
db_inode_mode_modify(gfarm_ino_t inum, gfarm_mode_t mode)
{
	struct db_inode_uint32_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002018,
			"allocation of 'db_inode_uint32_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	arg->uint32 = mode;
	return (db_enter_sn((dbq_entry_func_t)ops->inode_mode_modify, arg));
}

struct db_inode_string_modify_arg *
db_inode_string_modify_arg_alloc(gfarm_ino_t inum, const char *str)
{
	struct db_inode_string_modify_arg *arg;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	sz = gfarm_size_add(&overflow, sizeof(*arg),
		gfarm_size_add(&overflow, strlen(str), 1));
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL)
		return (NULL);
	arg->string = (char *)arg + sizeof(*arg);

	arg->inum = inum;
	strcpy(arg->string, str);
	return (arg);
}

gfarm_error_t
db_inode_user_modify(gfarm_ino_t inum, const char *user)
{
	struct db_inode_string_modify_arg *arg =
	    db_inode_string_modify_arg_alloc(inum, user);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1003022,
		    "db_inode_string_modify_arg_alloc failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->inode_user_modify, arg));
}

gfarm_error_t
db_inode_group_modify(gfarm_ino_t inum, const char *group)
{
	struct db_inode_string_modify_arg *arg =
	    db_inode_string_modify_arg_alloc(inum, group);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1003023,
		    "db_inode_string_modify_arg_alloc failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->inode_group_modify, arg));
}

static gfarm_error_t
db_inode_time_modify(gfarm_ino_t inum, struct gfarm_timespec *t,
	dbq_entry_func_t op)
{
	struct db_inode_timespec_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002021,
			"allocation of 'db_inode_timespec_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	arg->time = *t;
	return (db_enter_sn(op, arg));
}

gfarm_error_t
db_inode_atime_modify(gfarm_ino_t inum, struct gfarm_timespec *atime)
{
	return (db_inode_time_modify(inum, atime,
		(dbq_entry_func_t)ops->inode_atime_modify));
}

gfarm_error_t
db_inode_mtime_modify(gfarm_ino_t inum, struct gfarm_timespec *mtime)
{
	return (db_inode_time_modify(inum, mtime,
		(dbq_entry_func_t)ops->inode_mtime_modify));
}

gfarm_error_t
db_inode_ctime_modify(gfarm_ino_t inum, struct gfarm_timespec *ctime)
{
	return (db_inode_time_modify(inum, ctime,
		(dbq_entry_func_t)ops->inode_ctime_modify));
}

gfarm_error_t
db_inode_load(void *closure, void (*callback)(void *, struct gfs_stat *))
{
	gfarm_error_t e;
	const char *diag = "db_inode_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->inode_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}

struct db_inode_cksum_arg *
db_inode_cksum_arg_alloc(gfarm_ino_t inum,
	const char *type, size_t len, const char *sum)
{
	size_t tsize = strlen(type) + 1;
	size_t sz;
	int overflow = 0;
	struct db_inode_cksum_arg *arg;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	sz = gfarm_size_add(&overflow, sizeof(*arg) + tsize,
	    gfarm_size_add(&overflow, len, 1));
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002024,
			"allocation of 'db_inode_cksum_arg' failed or "
			"overflow");
		return (NULL);
	}
	arg->type = (char *)arg + sizeof(*arg);
	arg->sum = arg->type + tsize;

	arg->inum = inum;
	strcpy(arg->type, type);
	memcpy(arg->sum, sum, len + 1);
	return (arg);
}

gfarm_error_t
db_inode_cksum_add(gfarm_ino_t inum,
	const char *type, size_t len, const char *sum)
{
	struct db_inode_cksum_arg *arg =
	    db_inode_cksum_arg_alloc(inum, type, len, sum);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002025,
			"db_inode_cksum_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->inode_cksum_add, arg));
}

gfarm_error_t
db_inode_cksum_modify(gfarm_ino_t inum,
	const char *type, size_t len, const char *sum)
{
	struct db_inode_cksum_arg *arg =
	    db_inode_cksum_arg_alloc(inum, type, len, sum);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002026,
			"db_inode_cksum_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->inode_cksum_modify, arg));
}

gfarm_error_t
db_inode_cksum_remove(gfarm_ino_t inum)
{
	struct db_inode_inum_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002027,
			"allocation of 'db_inode_inum_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	return (db_enter_sn((dbq_entry_func_t)ops->inode_cksum_remove, arg));
}

gfarm_error_t
db_inode_cksum_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	gfarm_error_t e;
	const char *diag = "db_inode_cksum_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->inode_cksum_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}


struct db_filecopy_arg *
db_filecopy_arg_alloc(gfarm_ino_t inum, const char *hostname)
{
	size_t hsize = strlen(hostname) + 1;
	struct db_filecopy_arg *arg;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	sz = gfarm_size_add(&overflow, sizeof(*arg), hsize);
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002028,
			"allocation of 'db_filecopy_arg' failed or overflow");
		return (NULL);
	}
	arg->hostname = (char *)arg + sizeof(*arg);

	arg->inum = inum;
	strcpy(arg->hostname, hostname);
	return (arg);
}

gfarm_error_t
db_filecopy_add(gfarm_ino_t inum, const char *hostname)
{
	struct db_filecopy_arg *arg =
	    db_filecopy_arg_alloc(inum, hostname);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002029,
			"db_filecopy_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->filecopy_add, arg));
}

gfarm_error_t
db_filecopy_remove(gfarm_ino_t inum, const char *hostname)
{
	struct db_filecopy_arg *arg =
	    db_filecopy_arg_alloc(inum, hostname);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002030,
			"db_filecopy_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->filecopy_remove, arg));
}

gfarm_error_t
db_filecopy_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	gfarm_error_t e;
	const char *diag = "db_filecopy_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->filecopy_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}


struct db_deadfilecopy_arg *
db_deadfilecopy_arg_alloc(gfarm_ino_t inum, gfarm_uint64_t igen,
	const char *hostname)
{
	size_t hsize = strlen(hostname) + 1;
	struct db_deadfilecopy_arg *arg;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	sz = gfarm_size_add(&overflow, sizeof(*arg), hsize);
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002031,
			"allocation of 'db_deadfilecopy_arg' failed or "
			"overflow");
		return (NULL);
	}
	arg->hostname = (char *)arg + sizeof(*arg);

	arg->inum = inum;
	arg->igen = igen;
	strcpy(arg->hostname, hostname);
	return (arg);
}

gfarm_error_t
db_deadfilecopy_add(gfarm_ino_t inum, gfarm_uint64_t igen,
	const char *hostname)
{
	struct db_deadfilecopy_arg *arg =
	    db_deadfilecopy_arg_alloc(inum, igen, hostname);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002032,
			"db_deadfilecopy_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->deadfilecopy_add, arg));
}

gfarm_error_t
db_deadfilecopy_remove(gfarm_ino_t inum, gfarm_uint64_t igen,
	const char *hostname)
{
	struct db_deadfilecopy_arg *arg =
	    db_deadfilecopy_arg_alloc(inum, igen, hostname);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002033,
			"db_deadfilecopy_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->deadfilecopy_remove, arg));
}

gfarm_error_t
db_deadfilecopy_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	gfarm_error_t e;
	const char *diag = "db_deadfilecopy_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->deadfilecopy_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}


struct db_direntry_arg *
db_direntry_arg_alloc(
	gfarm_ino_t dir_inum, const char *entry_name, int entry_len,
	gfarm_ino_t entry_inum)
{
	struct db_direntry_arg *arg;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	sz = gfarm_size_add(&overflow, sizeof(*arg), entry_len + 1);
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002034,
			"allocation of 'db_direntry_arg' failed or overflow");
		return (NULL);
	}
	arg->entry_name = (char *)arg + sizeof(*arg);

	arg->dir_inum = dir_inum;
	memcpy(arg->entry_name, entry_name, entry_len);
	arg->entry_name[entry_len] = '\0';
	arg->entry_len = entry_len;
	arg->entry_inum = entry_inum;
	return (arg);
}

gfarm_error_t
db_direntry_add(gfarm_ino_t dir_inum, const char *entry_name, int entry_len,
	gfarm_ino_t entry_inum)
{
	struct db_direntry_arg *arg =
	    db_direntry_arg_alloc(dir_inum, entry_name, entry_len, entry_inum);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002035,
			"db_direntry_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->direntry_add, arg));
}

gfarm_error_t
db_direntry_remove(gfarm_ino_t dir_inum, const char *entry_name, int entry_len)
{
	struct db_direntry_arg *arg =
	    db_direntry_arg_alloc(dir_inum, entry_name, entry_len, 0);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002036,
			"db_direntry_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->direntry_remove, arg));
}

gfarm_error_t
db_direntry_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	gfarm_error_t e;
	const char *diag = "db_direntry_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->direntry_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}

struct db_symlink_arg *
db_symlink_arg_alloc(gfarm_ino_t inum, const char *source_path)
{
	struct db_symlink_arg *arg;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	sz = gfarm_size_add(&overflow, sizeof(*arg), strlen(source_path) + 1);
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002037,
			"allocation of 'db_symlink_arg' failed or overflow");
		return (NULL);
	}
	arg->source_path = (char *)arg + sizeof(*arg);

	arg->inum = inum;
	strcpy(arg->source_path, source_path);
	return (arg);
}

gfarm_error_t
db_symlink_add(gfarm_ino_t inum, const char *source_path)
{
	struct db_symlink_arg *arg =
	    db_symlink_arg_alloc(inum, source_path);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002038,
			"db_symlink_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->symlink_add, arg));
}

gfarm_error_t
db_symlink_remove(gfarm_ino_t inum)
{
	struct db_inode_inum_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002039,
			"allocation of 'db_inode_inum_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	return (db_enter_sn((dbq_entry_func_t)ops->symlink_remove, arg));
}

gfarm_error_t
db_symlink_load(void *closure, void (*callback)(void *, gfarm_ino_t, char *))
{
	gfarm_error_t e;
	const char *diag = "db_symlink_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->symlink_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}

struct db_xattr_arg *
db_xattr_arg_alloc(int xmlMode, gfarm_ino_t inum, const char *attrname,
	void *value, size_t valsize)
{
	struct db_xattr_arg *arg;
	size_t size;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	size = gfarm_size_add(&overflow, sizeof(*arg), valsize);
	if (attrname != NULL) {
		size = gfarm_size_add(&overflow, size, strlen(attrname) + 1);
	}
	if (!overflow)
		arg = malloc(size);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002040,
			"allocation of 'db_xattr_arg' failed or overflow");
		return (NULL);
	}

	memset(arg, 0, sizeof(*arg));
	/* NOTE: we allow valsize == 0 as a valid xattr_add/modify argment */
	arg->value = arg + 1;
	if (attrname != NULL) {
		arg->attrname = (char *)(arg + 1) + valsize;
		strcpy(arg->attrname, attrname);
	} else
		arg->attrname = NULL;
	arg->xmlMode = xmlMode;
	arg->inum = inum;
	if (value != NULL) {
		memcpy(arg->value, value, valsize);
		arg->size = valsize;
	} else {
		arg->value = NULL;
		arg->size = 0;
	}
	return (arg);
}

gfarm_error_t
db_xattr_add(int xmlMode, gfarm_ino_t inum, char *attrname,
	void *value, size_t size, struct db_waitctx *waitctx)
{
	gfarm_error_t e;
	struct db_xattr_arg *arg = db_xattr_arg_alloc(xmlMode, inum,
	    attrname, value, size);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002041,
			"db_xattr_arg_alloc() failed");
		return (GFARM_ERR_NO_ERROR);
	}
	if (waitctx != NULL) {
		if (gfarm_get_metadb_replication_enabled()) {
			/* XXX FIXME we cannot wait for db transaction to be
			 * committed when the journal function is enabled. */
			e = db_enter_sn((dbq_entry_func_t)ops->xattr_add, arg);
			waitctx->e = e;
		} else {
			/*
			 * NOTE: EINVAL returns from PostgreSQL if value is
			 * invalid XML data. We must wait to check it.
			 * Same as db_xattr_modify().
			 */
			e = dbq_enter_for_waitret(
				(dbq_entry_func_t)ops->xattr_add, arg, waitctx);
		}
	} else
		e = db_enter_sn((dbq_entry_func_t)ops->xattr_add, arg);
	return (e);
}

gfarm_error_t
db_xattr_modify(int xmlMode, gfarm_ino_t inum, char *attrname,
	void *value, size_t size, struct db_waitctx *waitctx)
{
	gfarm_error_t e;
	struct db_xattr_arg *arg = db_xattr_arg_alloc(xmlMode, inum,
	    attrname, value, size);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002042,
			"db_xattr_arg_alloc() failed");
		return (GFARM_ERR_NO_ERROR);
	}
	if (waitctx != NULL) {
		if (gfarm_get_metadb_replication_enabled()) {
			/* XXX FIXME we cannot wait for db transaction to be
			 * committed when the journal function is enabled. */
			e = db_enter_sn((dbq_entry_func_t)ops->xattr_modify,
				arg);
			waitctx->e = e;
		} else {
			e = dbq_enter_for_waitret(
				(dbq_entry_func_t)ops->xattr_modify, arg,
				waitctx);
		}
	} else
		e = db_enter_sn((dbq_entry_func_t)ops->xattr_modify, arg);
	return (e);
}

gfarm_error_t
db_xattr_remove(int xmlMode, gfarm_ino_t inum, char *attrname)
{
	struct db_xattr_arg *arg = db_xattr_arg_alloc(xmlMode, inum,
	    attrname, NULL, 0);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002043,
			"db_xattr_arg_alloc() failed");
		return (GFARM_ERR_NO_ERROR);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->xattr_remove, arg));
}

gfarm_error_t
db_xattr_removeall(int xmlMode, gfarm_ino_t inum)
{
	if (ops->xattr_removeall != NULL) {
		struct db_xattr_arg *arg = db_xattr_arg_alloc(xmlMode, inum,
		    NULL, NULL, 0);
		if (arg == NULL) {
			gflog_debug(GFARM_MSG_1002044,
				"db_xattr_arg_alloc() failed");
			return (GFARM_ERR_NO_ERROR);
		}
		return (db_enter_sn(
			(dbq_entry_func_t)ops->xattr_removeall, arg));
	} else
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

gfarm_error_t
db_xattr_get(int xmlMode, gfarm_ino_t inum, char *attrname,
	void **valuep, size_t *sizep, struct db_waitctx *waitctx)
{
	struct db_xattr_arg *arg = db_xattr_arg_alloc(xmlMode, inum,
	    attrname, NULL, 0);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002045,
			"db_xattr_arg_alloc() failed");
		return (GFARM_ERR_NO_ERROR);
	}
	arg->valuep = valuep;
	arg->sizep = sizep;
	return (dbq_enter_for_waitret(
		(dbq_entry_func_t)ops->xattr_get, arg, waitctx));
}

gfarm_error_t
db_xattr_load(void *closure,
		void (*callback)(void *, struct xattr_info *))
{
	gfarm_error_t e;
	const char *diag = "db_xattr_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->xattr_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}

gfarm_error_t
db_xmlattr_find(gfarm_ino_t inum, const char *expr,
	gfarm_error_t (*foundcallback)(void *, int, void *), void *foundcbdata,
	void (*callback)(gfarm_error_t, void *), void *cbdata)
{
	struct db_xmlattr_find_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002046,
			"allocation of 'db_xmlattr_find_arg' failed");
		return (GFARM_ERR_NO_ERROR);
	}
	arg->inum = inum;
	arg->expr = expr;
	arg->foundcallback = foundcallback;
	arg->foundcbdata = foundcbdata;
	return (dbq_enter_withcallback(
		(dbq_entry_func_t)ops->xmlattr_find, arg,
		(dbq_entry_func_callback_t)callback, cbdata));
}

/* quota */
struct db_quota_arg *
db_quota_arg_alloc(const struct quota *q, const char *name, int is_group)
{
	struct db_quota_arg *arg;
	size_t sz;
	int overflow = 0;
	int name_len = strlen(name);

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	sz = gfarm_size_add(&overflow, sizeof(*arg), name_len + 1);
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002047,
			"allocation of 'db_quota_arg' failed or overflow");
		return (NULL);
	}
	arg->name = (char *)arg + sizeof(*arg);

	arg->quota = *q;
	arg->is_group = is_group;
	strcpy(arg->name, name);

	return (arg);
}

static gfarm_error_t
db_quota_set_common(struct quota *q, const char *name, int is_group)
{
	struct db_quota_arg *arg = db_quota_arg_alloc(q, name, is_group);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002048,
			"db_quota_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	if (q->on_db)
		return (db_enter_sn((dbq_entry_func_t)ops->quota_modify,
			arg));
	else
		return (db_enter_sn((dbq_entry_func_t)ops->quota_add, arg));
}

gfarm_error_t
db_quota_user_set(struct quota *q, const char *username)
{
	return (db_quota_set_common(q, username, 0));
}

gfarm_error_t
db_quota_group_set(struct quota *q, const char *groupname)
{
	return (db_quota_set_common(q, groupname, 1));
}

struct db_quota_remove_arg *
db_quota_remove_arg_alloc(const char *name, int is_group)
{
	struct db_quota_remove_arg *arg;
	size_t sz;
	int overflow = 0;
	int name_len = strlen(name);

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	arg = NULL;
#endif
	sz = gfarm_size_add(&overflow, sizeof(*arg), name_len + 1);
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002049,
			"allocation of 'db_quota_remove_arg' failed or "
			"overflow");
		return (NULL);
	}
	arg->name = (char *)arg + sizeof(*arg);

	arg->is_group = is_group;
	strcpy(arg->name, name);

	return (arg);
}

static gfarm_error_t
db_quota_remove_common(const char *name, int is_group)
{
	struct db_quota_remove_arg *arg
		= db_quota_remove_arg_alloc(name, is_group);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002050,
			"db_quota_remove_arg_alloc() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->quota_remove, arg));
}

gfarm_error_t
db_quota_user_remove(const char *username)
{
	return (db_quota_remove_common(username, 0));
}

gfarm_error_t
db_quota_group_remove(const char *groupname)
{
	return (db_quota_remove_common(groupname, 1));
}

gfarm_error_t
db_quota_user_load(void *closure,
	      void (*callback)(void *, struct gfarm_quota_info *))
{
	gfarm_error_t e;
	const char *diag = "db_quota_user_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->quota_load)(closure, 0, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}

gfarm_error_t
db_quota_group_load(void *closure,
	      void (*callback)(void *, struct gfarm_quota_info *))
{
	gfarm_error_t e;
	const char *diag = "db_quota_group_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->quota_load)(closure, 1, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}

gfarm_error_t
db_seqnum_add(char *name, gfarm_uint64_t value)
{
	struct db_seqnum_arg a;

	a.name = name;
	a.value = value;
	/* a is not freed */
	return (ops->seqnum_add(&a));
}

gfarm_error_t
db_seqnum_modify(char *name, gfarm_uint64_t value)
{
	struct db_seqnum_arg a;

	a.name = name;
	a.value = value;
	/* a is not freed */
	return (ops->seqnum_modify(&a));
}

gfarm_error_t
db_seqnum_remove(char *name)
{
	return (ops->seqnum_remove(name));
}

gfarm_error_t
db_seqnum_load(void *closure,
	      void (*callback)(void *, struct db_seqnum_arg *))
{
	gfarm_error_t e;
	const char *diag = "db_seqnum_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->seqnum_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}

void *
db_mdhost_dup(const struct gfarm_metadb_server *ms, size_t size)
{
	struct gfarm_metadb_server *r;
	size_t nsize = strlen(ms->name) + 1;
	size_t csize = ms->clustername ? strlen(ms->clustername) + 1 : 0;
	size_t sz;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	r = NULL;
#endif
	sz = gfarm_size_add(&overflow, size, nsize + csize);
	if (!overflow)
		r = malloc(sz);
	if (overflow || r == NULL) {
		gflog_debug(GFARM_MSG_1003024,
		    "allocation of 'gfarm_metadb_server' failed or overflow");
		return (NULL);
	}
	r->name = (char *)r + size;
	r->clustername = ms->clustername ? r->name + nsize : NULL;

	strcpy(r->name, ms->name);
	if (ms->clustername)
		strcpy(r->clustername, ms->clustername);
	r->port = ms->port;
	r->flags = ms->flags;
	r->tflags = 0;
	return (r);
}

gfarm_error_t
db_mdhost_add(const struct gfarm_metadb_server *ms)
{
	struct gfarm_metadb_server *m = db_mdhost_dup(ms, sizeof(*ms));

	if (m == NULL) {
		gflog_debug(GFARM_MSG_1003025, "db_mdhost_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->mdhost_add, m));
}

struct db_mdhost_modify_arg *
db_mdhost_modify_arg_alloc(const struct gfarm_metadb_server *ms, int modflags)
{
	struct db_mdhost_modify_arg *arg = db_mdhost_dup(ms, sizeof(*arg));

	if (arg == NULL)
		return (NULL);
	arg->modflags = modflags;
	return (arg);
}

gfarm_error_t
db_mdhost_modify(const struct gfarm_metadb_server *ms, int modflags)
{
	struct db_mdhost_modify_arg *arg = db_mdhost_modify_arg_alloc(
	    ms, modflags);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1003026,
		    "db_mdhost_modify_arg_alloc failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (db_enter_sn((dbq_entry_func_t)ops->mdhost_modify, arg));
}

gfarm_error_t
db_mdhost_remove(const char *name)
{
	char *u = strdup_log(name, "db_mdhost_remove");

	if (u == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (db_enter_sn((dbq_entry_func_t)ops->mdhost_remove, u));
}

gfarm_error_t
db_mdhost_load(void *closure,
	void (*callback)(void *, struct gfarm_metadb_server *))
{
	gfarm_error_t e;
	const char *diag = "db_mdhost_load";

	gfarm_mutex_lock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	e = ((*ops->mdhost_load)(closure, callback));
	gfarm_mutex_unlock(&db_access_mutex, diag, DB_ACCESS_MUTEX_DIAG);
	return (e);
}
