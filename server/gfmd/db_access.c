/*
 * $Id$
 */

#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "config.h"

#include "quota_info.h"
#include "quota.h"
#include "db_access.h"
#include "db_ops.h"
#include "thrsubr.h"

#define ALIGNMENT 8
#define ALIGN(offset)	(((offset) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

typedef void (*dbq_entry_func_callback_t)(gfarm_error_t, void *);

struct dbq_callback_arg {
	dbq_entry_func_t func;
	void *data;
	dbq_entry_func_callback_t cbfunc;
	void *cbdata;
};

struct dbq_entry {
	dbq_entry_func_t func;
	void *data;
};

struct dbq {
	pthread_mutex_t mutex;
	pthread_cond_t nonempty, nonfull, finished;

	int n, in, out, quitting, quited;
	struct dbq_entry *entries;
} dbq;

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
	mutex_init(&q->mutex, diag, "mutex");
	cond_init(&q->nonempty, diag, "nonempty");
	cond_init(&q->nonfull, diag, "nonfull");
	cond_init(&q->finished, diag, "finished");
	q->n = q->in = q->out = q->quitting = q->quited = 0;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
dbq_wait_to_finish(struct dbq *q)
{
	static const char diag[] = "dbq_wait_to_finish";

	mutex_lock(&q->mutex, diag, "mutex");
	q->quitting = 1;
	while (!q->quited) {
		cond_signal(&q->nonempty, diag, "nonempty");
		cond_wait(&q->finished, &q->mutex, diag, "finished");
	}
	mutex_unlock(&q->mutex, diag, "mutex");
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
dbq_enter(struct dbq *q, dbq_entry_func_t func, void *data)
{
	gfarm_error_t e;
	static const char diag[] = "dbq_enter";

	mutex_lock(&q->mutex, diag, "mutex");
	if (q->quitting) {
		/*
		 * Because dbq_wait_to_finish() is only called while
		 * giant_lock() is held, the dbq shouldn't be partial state.
		 * So, we this shouldn't cause inconsistent metadata.
		 */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		cond_signal(&q->nonempty, diag, "nonempty");
	} else {
		e = GFARM_ERR_NO_ERROR;
		while (q->n >= gfarm_metadb_dbq_size) {
			cond_wait(&q->nonfull, &q->mutex, diag, "nonfull");
		}
		q->entries[q->in].func = func;
		q->entries[q->in].data = data;
		q->in++;
		if (q->in >= gfarm_metadb_dbq_size)
			q->in = 0;
		q->n++;
		cond_signal(&q->nonempty, diag, "nonempty");
	}
	mutex_unlock(&q->mutex, diag, "mutex");
	return (e);
}

/* DO NOT REMOVE: this interfaces is provided for a private extension */
/* The official gfmd source code shouldn't use these interface */
gfarm_error_t
gfarm_dbq_enter(dbq_entry_func_t func, void *data)
{
	return (dbq_enter(&dbq, func, data));
}

static gfarm_error_t
dbq_call_callback(void *a)
{
	gfarm_error_t e;
	struct dbq_callback_arg *arg = (struct dbq_callback_arg *)a;

	e = (*arg->func)(arg->data);
	if (arg->cbfunc != NULL) {
		(*arg->cbfunc)(e, arg->cbdata);
	}
	free(arg);
	return (e);
}

static gfarm_error_t
dbq_enter_withcallback(struct dbq *q,
		dbq_entry_func_t func, void *data,
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
	e = dbq_enter(q, dbq_call_callback, arg);
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
		mutex_init(&ctx->lock, diag, "lock");
		cond_init(&ctx->cond, diag, "cond");
		ctx->e = UNINITIALIZED_GFARM_ERROR;
	}
}

void
db_waitctx_fini(struct db_waitctx *ctx)
{
	if (ctx != NULL) {
		pthread_cond_destroy(&ctx->cond);
		pthread_mutex_destroy(&ctx->lock);
	}
}

static void
dbq_done_callback(gfarm_error_t e, void *c)
{
	struct db_waitctx *ctx = (struct db_waitctx *)c;

	if (ctx != NULL) {
		pthread_mutex_lock(&ctx->lock);
		ctx->e = e;
		pthread_cond_signal(&ctx->cond);
		pthread_mutex_unlock(&ctx->lock);
	}
}

gfarm_error_t
dbq_waitret(struct db_waitctx *ctx)
{
	gfarm_error_t e;
	static const char diag[] = "dbq_waitret";

	if (ctx == NULL)
		return (GFARM_ERR_NO_ERROR);

	mutex_lock(&ctx->lock, diag, "lock");
	while (ctx->e == UNINITIALIZED_GFARM_ERROR) {
		cond_wait(&ctx->cond, &ctx->lock, diag, "cond");
	}
	e = ctx->e;
	mutex_unlock(&ctx->lock, diag, "lock");
	return (e);
}

static gfarm_error_t
dbq_enter_for_waitret(struct dbq *q,
	dbq_entry_func_t func, void *data, struct db_waitctx *ctx)
{
	return (dbq_enter_withcallback(q, func, data,
			dbq_done_callback, ctx));
}

/* DO NOT REMOVE: this interfaces is provided for a private extension */
/* The official gfmd source code shouldn't use these interface */
gfarm_error_t
gfarm_dbq_enter_for_waitret(
	dbq_entry_func_t func, void *data, struct db_waitctx *ctx)
{
	return (dbq_enter_for_waitret(&dbq, func, data, ctx));
}

gfarm_error_t
dbq_delete(struct dbq *q, struct dbq_entry *entp)
{
	gfarm_error_t e;
	static const char diag[] = "dbq_delete";

	mutex_lock(&q->mutex, diag, "mutex");
	while (q->n <= 0 && !q->quitting) {
		cond_wait(&q->nonempty, &q->mutex, diag, "nonempty");
	}
	if (q->n <= 0) {
		assert(q->quitting);
		q->quited = 1;
		cond_signal(&q->finished, diag, "finished");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else { /* (q->n > 0) */
		e = GFARM_ERR_NO_ERROR;
		*entp = q->entries[q->out++];
		if (q->out >= gfarm_metadb_dbq_size)
			q->out = 0;
		if (q->n-- >= gfarm_metadb_dbq_size) {
			cond_signal(&q->nonfull, diag, "nonfull");
		}
	}
	mutex_unlock(&q->mutex, diag, "mutex");
	return (e);
}

int
db_getfreenum(void)
{
	struct dbq *q = &dbq;
	int freenum;

	/*
	 * This function is made only for gfm_server_findxmlattr().
	 */
	pthread_mutex_lock(&q->mutex);
	freenum = (gfarm_metadb_dbq_size - q->n);
	pthread_mutex_unlock(&q->mutex);
	return (freenum);
}

static const struct db_ops *ops = &db_none_ops;

/* DO NOT REMOVE: this interfaces is provided for a private extension */
/* The official gfmd source code shouldn't use these interface */
const struct db_ops *
db_get_ops(void)
{
	return (ops);
}

gfarm_error_t
db_use(const struct db_ops *o)
{
	ops = o;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
db_initialize(void)
{
	dbq_init(&dbq);
	return ((*ops->initialize)());
}

gfarm_error_t
db_terminate(void)
{
	gflog_info(GFARM_MSG_1000406, "try to stop database syncer");
	dbq_wait_to_finish(&dbq);

	gflog_info(GFARM_MSG_1000407, "terminating the database");
	return ((*ops->terminate)());
}

void *
db_thread(void *arg)
{
	gfarm_error_t e;
	struct dbq_entry ent;

	for (;;) {
		e = dbq_delete(&dbq, &ent);
		if (e == GFARM_ERR_NO_ERROR) {
			(*ent.func)(ent.data);
		} else if (e == GFARM_ERR_NO_SUCH_OBJECT)
			break;
	}

	return (NULL);
}

gfarm_error_t
db_begin(const char *diag)
{
	gfarm_error_t e = dbq_enter(&dbq, (dbq_entry_func_t)ops->begin, NULL);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000408,
		    "%s: db_begin(): %s", diag, gfarm_error_string(e));
	return (e);
}

gfarm_error_t
db_end(const char *diag)
{
	gfarm_error_t e = dbq_enter(&dbq, (dbq_entry_func_t)ops->end, NULL);

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
	struct gfarm_host_info *h = db_host_dup(hi, sizeof(*h));

	if (h == NULL) {
		gflog_debug(GFARM_MSG_1002002, "db_host_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->host_add, h));
}

gfarm_error_t
db_host_modify(const struct gfarm_host_info *hi,
	int modflags,
	int add_count, const char **add_aliases,
	int del_count, const char **del_aliases)
{
	struct db_host_modify_arg *arg = db_host_dup(hi, sizeof(*arg));

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002003, "db_host_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	arg->modflags = modflags;
	/* XXX FIXME missing hostaliases */
	arg->add_count = 0; arg->add_aliases = NULL;
	arg->del_count = 0; arg->del_aliases = NULL;

	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->host_modify, arg));
}

gfarm_error_t
db_host_remove(const char *hostname)
{
	char *h = strdup(hostname);

	if (h == NULL) {
		gflog_debug(GFARM_MSG_1002004,
			"allocation of string 'h' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->host_remove, h));
}

gfarm_error_t
db_host_load(void *closure, void (*callback)(void *, struct gfarm_host_info *))
{
	return ((*ops->host_load)(closure, callback));
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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->user_add, u));
}

gfarm_error_t
db_user_modify(const struct gfarm_user_info *ui, int modflags)
{
	struct db_user_modify_arg *arg = db_user_dup(ui, sizeof(*arg));

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002007, "db_user_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->modflags = modflags;
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->user_modify, arg));
}

gfarm_error_t
db_user_remove(const char *username)
{
	char *u = strdup(username);

	if (u == NULL) {
		gflog_debug(GFARM_MSG_1002008,
			"allocation of string 'u' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->user_remove, u));
}

gfarm_error_t
db_user_load(void *closure, void (*callback)(void *, struct gfarm_user_info *))
{
	return ((*ops->user_load)(closure, callback));
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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->group_add, g));
}

gfarm_error_t
db_group_modify(const struct gfarm_group_info *gi, int modflags,
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
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002011,
			"overflow occurred or db_group_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->group_modify, arg));
}

gfarm_error_t
db_group_remove(const char *groupname)
{
	char *g = strdup(groupname);

	if (g == NULL) {
		gflog_debug(GFARM_MSG_1002012,
			"allocation of string 'g' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->group_remove, g));
}

gfarm_error_t
db_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	return ((*ops->group_load)(closure, callback));
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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->inode_add, i));
}

gfarm_error_t
db_inode_modify(const struct gfs_stat *st)
{
	struct gfs_stat *i = db_inode_dup(st, sizeof(*i));

	if (i == NULL) {
		gflog_debug(GFARM_MSG_1002015, "db_inode_dup() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->inode_modify, i));
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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_nlink_modify, arg));
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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_size_modify, arg));
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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_mode_modify, arg));
}

gfarm_error_t
db_inode_user_modify(gfarm_ino_t inum, const char *user)
{
	struct db_inode_string_modify_arg *arg;
	size_t sz;
	int overflow = 0;

	sz = gfarm_size_add(&overflow, sizeof(*arg),
		gfarm_size_add(&overflow, strlen(user), 1));
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002019,
			"allocation of 'db_inode_string_modify_arg' failed "
			"or overflow");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->string = (char *)arg + sizeof(*arg);

	arg->inum = inum;
	strcpy(arg->string, user);
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_user_modify, arg));
}

gfarm_error_t
db_inode_group_modify(gfarm_ino_t inum, const char *group)
{
	struct db_inode_string_modify_arg *arg;
	size_t sz;
	int overflow = 0;

	sz = gfarm_size_add(&overflow, sizeof(*arg),
		gfarm_size_add(&overflow, strlen(group), 1));
	if (!overflow)
		arg = malloc(sz);
	if (overflow || arg == NULL) {
		gflog_debug(GFARM_MSG_1002020,
			"allocation of 'db_inode_string_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->string = (char *)arg + sizeof(*arg);

	arg->inum = inum;
	strcpy(arg->string, group);
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_group_modify, arg));
}

gfarm_error_t
db_inode_atime_modify(gfarm_ino_t inum, struct gfarm_timespec *atime)
{
	struct db_inode_timespec_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002021,
			"allocation of 'db_inode_timespec_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	arg->time = *atime;
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_atime_modify, arg));
}

gfarm_error_t
db_inode_mtime_modify(gfarm_ino_t inum, struct gfarm_timespec *mtime)
{
	struct db_inode_timespec_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002022,
			"allocation of 'db_inode_timespec_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	arg->time = *mtime;
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_mtime_modify, arg));
}

gfarm_error_t
db_inode_ctime_modify(gfarm_ino_t inum, struct gfarm_timespec *ctime)
{
	struct db_inode_timespec_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002023,
			"allocation of 'db_inode_timespec_modify_arg' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->inum = inum;
	arg->time = *ctime;
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_ctime_modify, arg));
}

gfarm_error_t
db_inode_load(void *closure, void (*callback)(void *, struct gfs_stat *))
{
	return ((*ops->inode_load)(closure, callback));
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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_cksum_add, arg));
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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_cksum_modify, arg));
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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_cksum_remove, arg));
}

gfarm_error_t
db_inode_cksum_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	return ((*ops->inode_cksum_load)(closure, callback));
}


struct db_filecopy_arg *
db_filecopy_arg_alloc(gfarm_ino_t inum, const char *hostname)
{
	size_t hsize = strlen(hostname) + 1;
	struct db_filecopy_arg *arg;
	size_t sz;
	int overflow = 0;

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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->filecopy_add, arg));
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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->filecopy_remove, arg));
}

gfarm_error_t
db_filecopy_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return ((*ops->filecopy_load)(closure, callback));
}


struct db_deadfilecopy_arg *
db_deadfilecopy_arg_alloc(gfarm_ino_t inum, gfarm_uint64_t igen,
	const char *hostname)
{
	size_t hsize = strlen(hostname) + 1;
	struct db_deadfilecopy_arg *arg;
	size_t sz;
	int overflow = 0;

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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->deadfilecopy_add, arg));
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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->deadfilecopy_remove, arg));
}

gfarm_error_t
db_deadfilecopy_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	return ((*ops->deadfilecopy_load)(closure, callback));
}


struct db_direntry_arg *
db_direntry_arg_alloc(
	gfarm_ino_t dir_inum, const char *entry_name, int entry_len,
	gfarm_ino_t entry_inum)
{
	struct db_direntry_arg *arg;
	size_t sz;
	int overflow = 0;

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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->direntry_add, arg));
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
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->direntry_remove, arg));
}

gfarm_error_t
db_direntry_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	return ((*ops->direntry_load)(closure, callback));
}

struct db_symlink_arg *
db_symlink_arg_alloc(gfarm_ino_t inum, const char *source_path)
{
	struct db_symlink_arg *arg;
	size_t sz;
	int overflow = 0;

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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->symlink_add, arg));
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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->symlink_remove, arg));
}

gfarm_error_t
db_symlink_load(void *closure, void (*callback)(void *, gfarm_ino_t, char *))
{
	return ((*ops->symlink_load)(closure, callback));
}

static struct db_xattr_arg *
db_xattr_arg_alloc(char *attrname, size_t valsize)
{
	struct db_xattr_arg *arg;
	size_t size;
	int overflow = 0;

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
	}
	return (arg);
}

gfarm_error_t
db_xattr_add(int xmlMode, gfarm_ino_t inum, char *attrname,
	void *value, size_t size, struct db_waitctx *waitctx)
{
	gfarm_error_t e;
	struct db_xattr_arg *arg = db_xattr_arg_alloc(attrname, size);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002041,
			"db_xattr_arg_alloc() failed");
		return (GFARM_ERR_NO_ERROR);
	}
	arg->xmlMode = xmlMode;
	arg->inum = inum;
	memcpy(arg->value, value, size);
	arg->size = size;
	if (waitctx != NULL) {
		/*
		 * NOTE: EINVAL returns from PostgreSQL if value is
		 * invalid XML data. We must wait to check it.
		 * Same as db_xattr_modify().
		 */
		e = dbq_enter_for_waitret(&dbq,
			(dbq_entry_func_t)ops->xattr_add, arg, waitctx);
	} else
		e = dbq_enter(&dbq,
			(dbq_entry_func_t)ops->xattr_add, arg);
	return (e);
}

gfarm_error_t
db_xattr_modify(int xmlMode, gfarm_ino_t inum, char *attrname,
	void *value, size_t size, struct db_waitctx *waitctx)
{
	gfarm_error_t e;
	struct db_xattr_arg *arg = db_xattr_arg_alloc(attrname, size);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002042,
			"db_xattr_arg_alloc() failed");
		return (GFARM_ERR_NO_ERROR);
	}
	arg->xmlMode = xmlMode;
	arg->inum = inum;
	memcpy(arg->value, value, size);
	arg->size = size;
	if (waitctx != NULL)
		e = dbq_enter_for_waitret(&dbq,
			(dbq_entry_func_t)ops->xattr_modify, arg, waitctx);
	else
		e = dbq_enter(&dbq,
			(dbq_entry_func_t)ops->xattr_modify, arg);
	return (e);
}

gfarm_error_t
db_xattr_remove(int xmlMode, gfarm_ino_t inum, char *attrname)
{
	struct db_xattr_arg *arg = db_xattr_arg_alloc(attrname, 0);

	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002043,
			"db_xattr_arg_alloc() failed");
		return (GFARM_ERR_NO_ERROR);
	}
	arg->xmlMode = xmlMode;
	arg->inum = inum;
	return (dbq_enter(&dbq,
		(dbq_entry_func_t)ops->xattr_remove, arg));
}

gfarm_error_t
db_xattr_removeall(int xmlMode, gfarm_ino_t inum)
{
	if (ops->xattr_removeall != NULL) {
		struct db_xattr_arg *arg = db_xattr_arg_alloc(NULL, 0);
		if (arg == NULL) {
			gflog_debug(GFARM_MSG_1002044,
				"db_xattr_arg_alloc() failed");
			return (GFARM_ERR_NO_ERROR);
		}
		arg->xmlMode = xmlMode;
		arg->inum = inum;
		return (dbq_enter(&dbq,
			(dbq_entry_func_t)ops->xattr_removeall, arg));
	} else
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

gfarm_error_t
db_xattr_get(int xmlMode, gfarm_ino_t inum, char *attrname,
	void **valuep, size_t *sizep, struct db_waitctx *waitctx)
{
	struct db_xattr_arg *arg = db_xattr_arg_alloc(attrname, 0);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002045,
			"db_xattr_arg_alloc() failed");
		return (GFARM_ERR_NO_ERROR);
	}
	arg->xmlMode = xmlMode;
	arg->inum = inum;
	arg->valuep = valuep;
	arg->sizep = sizep;
	return (dbq_enter_for_waitret(&dbq,
		(dbq_entry_func_t)ops->xattr_get, arg, waitctx));
}

gfarm_error_t
db_xattr_load(void *closure,
		void (*callback)(void *, struct xattr_info *))
{
	return ((*ops->xattr_load)(closure, callback));
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
	return (dbq_enter_withcallback(&dbq,
		(dbq_entry_func_t)ops->xmlattr_find, arg,
		(dbq_entry_func_callback_t)callback, cbdata));
}

/* quota */
static struct db_quota_arg *
db_quota_arg_alloc(const struct quota *q, const char *name, int is_group)
{
	struct db_quota_arg *arg;
	size_t sz;
	int overflow = 0;
	int name_len = strlen(name);

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
		return (dbq_enter(&dbq, (dbq_entry_func_t)ops->quota_modify,
				  arg));
	else
		return (dbq_enter(&dbq, (dbq_entry_func_t)ops->quota_add,
				  arg));
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

static struct db_quota_remove_arg *
db_quota_remove_arg_alloc(const char *name, int is_group)
{
	struct db_quota_remove_arg *arg;
	size_t sz;
	int overflow = 0;
	int name_len = strlen(name);

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
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->quota_remove, arg));
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
	return ((*ops->quota_load)(closure, 0, callback));
}

gfarm_error_t
db_quota_group_load(void *closure,
	      void (*callback)(void *, struct gfarm_quota_info *))
{
	return ((*ops->quota_load)(closure, 1, callback));
}
