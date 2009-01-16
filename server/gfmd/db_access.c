/*
 * $Id$
 */

#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "db_access.h"
#include "db_ops.h"

#define ALIGNMENT 8
#define ALIGN(offset)	(((offset) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

typedef void (*dbq_entry_func_t)(void *);

struct dbq_entry {
	dbq_entry_func_t func;
	void *data;
};

#define DBQ_SIZE	1000

struct dbq {
	pthread_mutex_t mutex;
	pthread_cond_t nonempty, nonfull, finished;

	int n, in, out, quitting, quited;
	struct dbq_entry entries[DBQ_SIZE];
} dbq;

gfarm_error_t
dbq_init(struct dbq *q)
{
	int err;
	const char msg[] = "dbq_init";

	err = pthread_mutex_init(&q->mutex, NULL);
	if (err != 0)
		gflog_fatal("%s: mutex: %s", msg, strerror(err));
	err = pthread_cond_init(&q->nonempty, NULL);
	if (err != 0)
		gflog_fatal("%s: condvar: %s", msg, strerror(err));
	err = pthread_cond_init(&q->nonfull, NULL);
	if (err != 0)
		gflog_fatal("%s: condvar: %s", msg, strerror(err));
	err = pthread_cond_init(&q->finished, NULL);
	if (err != 0)
		gflog_fatal("%s: condvar: %s", msg, strerror(err));
	q->n = q->in = q->out = q->quitting = q->quited = 0;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
dbq_wait_to_finish(struct dbq *q)
{
	int err;
	const char msg[] = "dbq_wait_to_finish";

	err = pthread_mutex_lock(&q->mutex);
	if (err != 0)
		gflog_fatal("%s: mutex lock: %s", msg, strerror(err));
	q->quitting = 1;
	while (!q->quited) {
		err = pthread_cond_signal(&q->nonempty);
		if (err != 0)
			gflog_fatal("%s: nonempty signal: %s",
			    msg, strerror(err));
		err = pthread_cond_wait(&q->finished, &q->mutex);
		if (err != 0)
			gflog_fatal("%s: condwait finished: %s",
			    msg, strerror(err));
	}
	err = pthread_mutex_unlock(&q->mutex);
	if (err != 0)
		gflog_fatal("%s: mutex unlock: %s", msg, strerror(err));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
dbq_enter(struct dbq *q, dbq_entry_func_t func, void *data)
{
	int err;
	gfarm_error_t e;
	const char msg[] = "dbq_enter";

	err = pthread_mutex_lock(&q->mutex);
	if (err != 0)
		gflog_fatal("%s: mutex lock: %s", msg, strerror(err));
	if (q->quitting) {
		/*
		 * Because dbq_wait_to_finish() is only called while
		 * giant_lock() is held, the dbq shouldn't be partial state.
		 * So, we this shouldn't cause inconsistent metadata.
		 */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		err = pthread_cond_signal(&q->nonempty);
		if (err != 0)
			gflog_fatal("%s: nonempty signal: %s",
			    msg, strerror(err));
	} else {
		e = GFARM_ERR_NO_ERROR;
		while (q->n >= DBQ_SIZE) {
			err = pthread_cond_wait(&q->nonfull, &q->mutex);
			if (err != 0)
				gflog_fatal("%s: condwait nonfull: %s",
				    msg, strerror(err));
		}
		q->entries[q->in].func = func;
		q->entries[q->in].data = data;
		q->in++;
		if (q->in >= DBQ_SIZE)
			q->in = 0;
		q->n++;
		err = pthread_cond_signal(&q->nonempty);
		if (err != 0)
			gflog_fatal("%s: nonempty signal: %s",
			    msg, strerror(err));
	}
	err = pthread_mutex_unlock(&q->mutex);
	if (err != 0)
		gflog_fatal("%s: mutex unlock: %s", msg, strerror(err));
	return (e);
}

gfarm_error_t
dbq_delete(struct dbq *q, struct dbq_entry *entp)
{
	int err;
	gfarm_error_t e;
	const char msg[] = "dbq_delete";

	err = pthread_mutex_lock(&q->mutex);
	if (err != 0)
		gflog_fatal("%s: mutex lock: %s", msg, strerror(err));
	while (q->n <= 0 && !q->quitting) {
		err = pthread_cond_wait(&q->nonempty, &q->mutex);
		if (err != 0)
			gflog_fatal("%s: condwait nonempty: %s",
			    msg, strerror(err));
	}
	if (q->n <= 0) {
		assert(q->quitting);
		q->quited = 1;
		err = pthread_cond_signal(&q->finished);
		if (err != 0)
			gflog_fatal("%s: finished signal: %s",
			    msg, strerror(err));
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else { /* (q->n > 0) */
		e = GFARM_ERR_NO_ERROR;
		*entp = q->entries[q->out++];
		if (q->out >= DBQ_SIZE)
			q->out = 0;
		if (q->n-- >= DBQ_SIZE) {
			err = pthread_cond_signal(&q->nonfull);
			if (err != 0)
				gflog_fatal("%s: nonfull signal: %s",
				    msg, strerror(err));
		}
	}
	err = pthread_mutex_unlock(&q->mutex);
	if (err != 0)
		gflog_fatal("%s: mutex unlock: %s", msg, strerror(err));
	return (e);
}

static const struct db_ops *ops = &db_none_ops;

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
	gflog_info("try to stop database syncer");
	dbq_wait_to_finish(&dbq);

	gflog_info("terminating the database");
	return ((*ops->terminate)());
}

void *
db_thread(void *arg)
{
	gfarm_error_t e;
	struct dbq_entry ent;

	for (;;) {
		e = dbq_delete(&dbq, &ent);
		if (e == GFARM_ERR_NO_ERROR)
			(*ent.func)(ent.data);
		else if (e == GFARM_ERR_NO_SUCH_OBJECT)
			break;
	}

	return (NULL);
}

#if 0 /* XXX for now */
gfarm_error_t
db_begin(void)
{
}

gfarm_error_t
db_end(void)
{
}
#endif /* XXX for now */

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
	if (overflow || r == NULL)
		return (NULL);
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

	if (h == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->host_add, h));
}

gfarm_error_t
db_host_modify(const struct gfarm_host_info *hi,
	int modflags,
	int add_count, const char **add_aliases,
	int del_count, const char **del_aliases)
{
	struct db_host_modify_arg *arg = db_host_dup(hi, sizeof(*arg));

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);

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

	if (h == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (overflow || r == NULL)
		return (NULL);
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

	if (u == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->user_add, u));
}

gfarm_error_t
db_user_modify(const struct gfarm_user_info *ui, int modflags)
{
	struct db_user_modify_arg *arg = db_user_dup(ui, sizeof(*arg));

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	arg->modflags = modflags;
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->user_modify, arg));
}

gfarm_error_t
db_user_remove(const char *username)
{
	char *u = strdup(username);

	if (u == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (overflow || r == NULL)
		return (NULL);

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

	if (g == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (overflow || arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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

	if (g == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (overflow || r == NULL)
		return (NULL);
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

	if (i == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->inode_add, i));
}

gfarm_error_t
db_inode_modify(const struct gfs_stat *st)
{
	struct gfs_stat *i = db_inode_dup(st, sizeof(*i));

	if (i == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->inode_modify, i));
}

gfarm_error_t
db_inode_nlink_modify(gfarm_ino_t inum, gfarm_uint64_t nlink)
{
	struct db_inode_uint64_modify_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	arg->inum = inum;
	arg->uint32 = mode;
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_mode_modify, arg));
}

gfarm_error_t
db_inode_user_modify(gfarm_ino_t inum, const char *user)
{
	struct db_inode_string_modify_arg *arg =
	    malloc(sizeof(*arg) + strlen(user) + 1);

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	arg->string = (char *)arg + sizeof(*arg);

	arg->inum = inum;
	strcpy(arg->string, user);
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_user_modify, arg));
}

gfarm_error_t
db_inode_group_modify(gfarm_ino_t inum, const char *group)
{
	struct db_inode_string_modify_arg *arg =
	    malloc(sizeof(*arg) + strlen(group) + 1);

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	if (overflow || arg == NULL)
		return (NULL);
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

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_cksum_add, arg));
}

gfarm_error_t
db_inode_cksum_modify(gfarm_ino_t inum,
	const char *type, size_t len, const char *sum)
{
	struct db_inode_cksum_arg *arg =
	    db_inode_cksum_arg_alloc(inum, type, len, sum);

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->inode_cksum_modify, arg));
}

gfarm_error_t
db_inode_cksum_remove(gfarm_ino_t inum)
{
	struct db_inode_inum_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	struct db_filecopy_arg *arg = malloc(sizeof(*arg) + hsize);

	if (arg == NULL)
		return (NULL);
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

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq, (dbq_entry_func_t)ops->filecopy_add, arg));
}

gfarm_error_t
db_filecopy_remove(gfarm_ino_t inum, const char *hostname)
{
	struct db_filecopy_arg *arg =
	    db_filecopy_arg_alloc(inum, hostname);

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	struct db_deadfilecopy_arg *arg = malloc(sizeof(*arg) + hsize);

	if (arg == NULL)
		return (NULL);
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

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->deadfilecopy_add, arg));
}

gfarm_error_t
db_deadfilecopy_remove(gfarm_ino_t inum, gfarm_uint64_t igen,
	const char *hostname)
{
	struct db_deadfilecopy_arg *arg =
	    db_deadfilecopy_arg_alloc(inum, igen, hostname);

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
	struct db_direntry_arg *arg = malloc(sizeof(*arg) + entry_len + 1);

	if (arg == NULL)
		return (NULL);
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

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->direntry_add, arg));
}

gfarm_error_t
db_direntry_remove(gfarm_ino_t dir_inum, const char *entry_name, int entry_len)
{
	struct db_direntry_arg *arg =
	    db_direntry_arg_alloc(dir_inum, entry_name, entry_len, 0);

	if (arg == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (dbq_enter(&dbq,
	    (dbq_entry_func_t)ops->direntry_remove, arg));
}

gfarm_error_t
db_direntry_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	return ((*ops->direntry_load)(closure, callback));
}
