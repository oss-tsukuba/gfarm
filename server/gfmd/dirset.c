#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "queue.h"
#include "hash.h"

#include "quota_info.h"
#include "gfp_xdr.h"
#include "auth.h"
#include "gfm_proto.h"
#include "config.h"

#include "quota.h"
#include "user.h"
#include "inode.h"
#include "dirset.h"
#include "quota_dir.h"
#include "db_access.h"
#include "peer.h"
#include "subr.h"
#include "rpcsubr.h"

/*
 * dirset
 */

struct dirset {
	char *dirsetname;
	struct user *user;
	struct dirquota dq;
	gfarm_uint64_t dir_count;
	struct quota_dir *dir_list;

	GFARM_HCIRCLEQ_ENTRY(dirset) dirsets_node;

	gfarm_uint64_t refcount;
	int valid;

};

struct dirset dirset_is_not_set = {
	"DIRSET_IS_NOT_SET",
	NULL,
	{ { { { 0, {0}, {0} }, {0}, {0} }, 0}, {0}, 0, 0},
	0,
	NULL,
	{ &dirset_is_not_set, &dirset_is_not_set },
	1,
	0
};

/*
 * dirset entry will NOT be removed from all_dirset_list_head at its removal,
 * it will remain linked at that time,
 * but will be removed at dirset_del_ref(),
 * to make dirset_foreach_interruptible() work.
 */
static GFARM_HCIRCLEQ_HEAD(dirset) all_dirset_list_head;

static struct dirset *
dirset_new(const char *dirsetname, struct user *u)
{
	struct dirset *ds;
	struct quota_dir *list_head;

	list_head = quota_dir_list_new();
	if (list_head == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "dirset_new(%s): no memory for list_head", dirsetname);
		return (NULL);
	}

	GFARM_MALLOC(ds);
	if (ds == NULL) {
		quota_dir_list_free(list_head);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "dirset_new(%s): no memory", dirsetname);
		return (NULL);
	}
	GFARM_MALLOC_ARRAY(ds->dirsetname, strlen(dirsetname) + 1);
	if (ds->dirsetname == NULL) {
		quota_dir_list_free(list_head);
		free(ds);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "dirset_new(%s): no memory", dirsetname);
		return (NULL);
	}

	strcpy(ds->dirsetname, dirsetname);
	ds->user = u;
	dirquota_init(&ds->dq);
	ds->dir_count = 0;
	ds->dir_list = list_head;
	/* ds->dirsets_node is initialized in dirset_enter() */
	ds->refcount = 1;
	ds->valid = 1;
	return (ds);
}

static void
dirset_free(struct dirset *ds)
{
	quota_dir_list_free(ds->dir_list);
	free(ds->dirsetname);
	free(ds);
}

int
dirset_is_valid(struct dirset *ds)
{
	return (ds->valid);
}

const char *
dirset_get_username(struct dirset *ds)
{
	return (user_name(ds->user));
}

const char *
dirset_get_dirsetname(struct dirset *ds)
{
	return (ds->dirsetname);
}

struct dirquota *
dirset_get_dirquota(struct dirset *ds)
{
	return (&ds->dq);
}

void
dirset_set_quota_metadata_in_cache(struct dirset *ds,
	const struct quota_metadata *q)
{
	quota_metadata_memory_convert_from_db(&ds->dq.qmm, q);
}

void
dirset_add_ref(struct dirset *ds)
{
	++ds->refcount;
}

int
dirset_del_ref(struct dirset *ds)
{
	--ds->refcount;
	if (ds->refcount > 0)
		return (1); /* still referenced */

	GFARM_HCIRCLEQ_REMOVE(ds, dirsets_node);
	dirset_free(ds);
	return (0); /* freed */
}

void
dirset_add_dir(struct dirset *ds, struct quota_dir **list_headp)
{
	++ds->dir_count;
	*list_headp = ds->dir_list;
}

void
dirset_remove_dir(struct dirset *ds)
{
	--ds->dir_count;
}

static void
dirset_add_one(void *closure,
	struct gfarm_dirset_info *di, struct quota_metadata *q)
{
	gfarm_error_t e;
	struct user *u = user_lookup(di->username);
	struct dirset *ds;

	if (u == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "dirset_add_one(%s, %s): user doesn't exist",
		    di->username, di->dirsetname);
		return;
	}
	/* do not check gfarm_directory_quota_count_per_user_limit here */
	e = user_enter_dirset(u, di->dirsetname, 0, &ds);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "dirset_add_one(%s, %s): %s",
		    di->username, di->dirsetname, gfarm_error_string(e));
		return;
	}
	dirset_set_quota_metadata_in_cache(ds, q);

	gfarm_dirset_info_free(di);
}

void
dirset_init(void)
{
	gfarm_error_t e;

	GFARM_HCIRCLEQ_INIT(all_dirset_list_head, dirsets_node);

	e = db_quota_dirset_load(NULL, dirset_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "loading dirset: %s", gfarm_error_string(e));
}

int
dirset_foreach_interruptible(
	void *closure, int (*callback)(void *, struct dirset *))
{
	struct dirset *ds, *next;
	int interrupted;

	for (ds = GFARM_HCIRCLEQ_FIRST(all_dirset_list_head, dirsets_node);
	    !GFARM_HCIRCLEQ_IS_END(all_dirset_list_head, ds);) {
		if (!ds->valid) {
			ds = GFARM_HCIRCLEQ_NEXT(ds, dirsets_node);
		} else {
			dirset_add_ref(ds);

			/*
			 * CAUTION: this callback function may
			 * release and re-acquire giant_lock internally
			 */
			interrupted = (*callback)(closure, ds);

			next = GFARM_HCIRCLEQ_NEXT(ds, dirsets_node);
			dirset_del_ref(ds);
			ds = next;

			if (interrupted)
				return (1);
		}
	}
	return (0);
}

int
dirset_foreach_quota_dir_interruptible(struct dirset *ds,
	void *closure, int (*callback)(void *, struct quota_dir *))
{
	return (quota_dir_foreach_in_dirset_interruptible(ds->dir_list,
	    closure, callback));
}

gfarm_error_t
xattr_list_set_by_dirset(struct xattr_list *entry,
	const char *name, struct dirset *ds, const char *diag)
{
	const char *username = user_name(ds->user);
	size_t userlen = strlen(username);
	size_t dslen = strlen(ds->dirsetname);
	size_t size = userlen + 1 + dslen + 1; /* never overflow */
	char *value;

	GFARM_MALLOC_ARRAY(value, size);
	if (value == NULL)
		return (GFARM_ERR_NO_MEMORY);
	strcpy(value, username);
	strcpy(value + userlen + 1, ds->dirsetname);
	entry->name = strdup_log(name, diag);
	entry->value = value;
	entry->size = size;
	return (GFARM_ERR_NO_ERROR);
}


/*
 * dirsets
 */

struct dirsets {
	/* dirset entry will be removed from hash at its removal */
	struct gfarm_hash_table *table;

	int dirset_count;
};

struct dirsets *
dirsets_new(void)
{
	struct dirsets *sets;

	GFARM_MALLOC(sets);
	if (sets == NULL)
		return (NULL);

	sets->table = gfarm_hash_table_alloc(
	    gfarm_directory_quota_count_per_user_limit,
	    gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (sets->table == NULL) {
		free(sets);
		return (NULL);
	}

	sets->dirset_count = 0;
	return (sets);
}

gfarm_error_t
dirset_enter(struct dirsets *sets, const char *dirsetname, struct user *u,
	int limit_check,
	struct dirset **dirsetp)
{
	struct dirset *ds = dirset_new(dirsetname, u);
	struct gfarm_hash_entry *entry;
	int created;

	if (ds == NULL)
		return (GFARM_ERR_NO_MEMORY);

	entry = gfarm_hash_enter(sets->table,
	    &ds->dirsetname, sizeof(ds->dirsetname), sizeof(ds),
	    &created);
	if (entry == NULL) {
		dirset_free(ds);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "dirset_enter: gfarm_hash_enter() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		dirset_free(ds);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "dirset_enter: already exists");
		return (GFARM_ERR_ALREADY_EXISTS);
	}

	if (limit_check &&
	    sets->dirset_count >= gfarm_directory_quota_count_per_user_limit) {
		(void)gfarm_hash_purge(sets->table,
		    &ds->dirsetname, sizeof(ds->dirsetname));
		dirset_free(ds);
		return (GFARM_ERR_NO_SPACE);
	}

	GFARM_HCIRCLEQ_INSERT_TAIL(all_dirset_list_head, ds, dirsets_node);

	*(struct dirset **)gfarm_hash_entry_data(entry) = ds;
	++sets->dirset_count;
	if (dirsetp != NULL)
		*dirsetp = ds;
	return (GFARM_ERR_NO_ERROR);
}

struct dirset *
dirset_lookup(struct dirsets *sets, const char *dirsetname)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(sets->table,
	    &dirsetname, sizeof(dirsetname));
	if (entry == NULL)
		return (NULL);
	return (*(struct dirset **)gfarm_hash_entry_data(entry));
}

gfarm_error_t
dirset_remove(struct dirsets *sets, const char *dirsetname)
{
	struct dirset *ds;
	int removed;

	ds = dirset_lookup(sets, dirsetname);
	if (ds == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if (ds->dir_count > 0)
		return (GFARM_ERR_DIRECTORY_QUOTA_EXISTS);

	removed = gfarm_hash_purge(sets->table,
	    &dirsetname, sizeof(dirsetname));
	if (!removed)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	/*
	 * do not remove from sets->list_head here,
	 * to make dirset_foreach_interruptible() work.
	 */

	--sets->dirset_count;

	ds->valid = 0;
	dirset_del_ref(ds);

	return (GFARM_ERR_NO_ERROR);
}

static void
dirset_foreach_in_dirsets(struct dirsets *sets,
	void *closure, void (*callback)(void *, struct dirset *))
{
	struct gfarm_hash_iterator it;
	struct dirset *ds;

	/* only valid entries are in sets->table */
	for (gfarm_hash_iterator_begin(sets->table, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		ds = *(struct dirset **)gfarm_hash_entry_data(
		    gfarm_hash_iterator_access(&it));
		callback(closure, ds);
	}
}

/*
 * protocol handlers
 */

gfarm_error_t
gfm_server_dirset_info_set(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *username, *dirsetname;
	struct user *u;
	struct dirset *ds;
	struct quota_metadata q;
	static const char diag[] = "GFM_PROTO_DIRSET_INFO_SET";

	e = gfm_server_get_request(peer, diag, "ss", &username, &dirsetname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		free(dirsetname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((u = user_lookup(username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (u != peer_get_user(peer) &&
	    !user_is_root(peer_get_user(peer))) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (strlen(dirsetname) > GFARM_DIRSET_NAME_MAX) {
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if (*dirsetname == '\0') {
		/*
		 * prohibit null string as a dirset, because that means
		 * all diresets in the GFM_PROTO_DIRSET_DIR_LIST protocol
		 */
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = user_enter_dirset(u, dirsetname, 1, &ds))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else {
		quota_metadata_memory_convert_to_db(&ds->dq.qmm, &q);
		e = db_quota_dirset_add(username, dirsetname, &q);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_UNFIXED,
			    "failed to store dirset '%s:%s' to backend DB: %s",
			    username, dirsetname, gfarm_error_string(e));
	}

	giant_unlock();
	free(username);
	free(dirsetname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_dirset_info_remove(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *username, *dirsetname;
	struct user *u;
	static const char diag[] = "GFM_PROTO_DIRSET_INFO_REMOVE";

	e = gfm_server_get_request(peer, diag, "ss", &username, &dirsetname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		free(dirsetname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((u = user_lookup(username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (u != peer_get_user(peer) &&
	    !user_is_root(peer_get_user(peer))) {
		e = GFARM_ERR_PERMISSION_DENIED;
	} else if ((e = user_remove_dirset(u, dirsetname))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else {
		e = db_quota_dirset_remove(username, dirsetname);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_UNFIXED,
			    "failed to remove dirset '%s:%s' from backend DB: "
			    "%s", username, dirsetname, gfarm_error_string(e));
	}

	giant_unlock();
	free(username);
	free(dirsetname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

static void
dirset_count_add(void *closure, struct user *u)
{
	gfarm_uint64_t *countp = closure;
	struct dirsets *sets;

	sets = user_get_dirsets(u);
	if (sets != NULL)
		*countp += sets->dirset_count;
}

void
dirset_reply(void *closure, struct dirset *ds)
{
	struct gfp_xdr *client = closure;

	gfp_xdr_send(client, "ss", user_name(ds->user), ds->dirsetname);
}

static void
dirset_reply_per_user(void *closure, struct user *u)
{
	struct gfp_xdr *client = closure;
	struct dirsets *sets;

	sets = user_get_dirsets(u);
	if (sets != NULL)
		dirset_foreach_in_dirsets(sets, client, dirset_reply);
}

gfarm_error_t
gfm_server_dirset_info_list(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *username;
	struct user *u = NULL;
	int all_users;
	gfarm_uint64_t count;
	static const char diag[] = "GFM_PROTO_DIRSET_INFO_LIST";

	e = gfm_server_get_request(peer, diag, "s", &username);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		return (GFARM_ERR_NO_ERROR);
	}
	all_users = *username == '\0';
	giant_lock();

	if (!from_client) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (!all_users && (u = user_lookup(username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (all_users ? !user_is_root(peer_get_user(peer)) :
	    (u != peer_get_user(peer) && !user_is_root(peer_get_user(peer)))) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		/* XXX FIXME too long giant lock */
		count = 0;
		if (all_users) {
			user_foreach(&count, dirset_count_add, 1);
		} else {
			dirset_count_add(&count, u);
		}
		if ((gfarm_uint32_t)count != count) {
			e = GFARM_ERR_MESSAGE_TOO_LONG;
		} else if ((e = gfm_server_put_reply(peer, diag, e, "i",
		    (gfarm_uint32_t)count)) != GFARM_ERR_NO_ERROR) {
			;
		} else {
			if (all_users) {
				user_foreach(peer_get_conn(peer),
				    dirset_reply_per_user, 1);
			} else {
				dirset_reply_per_user(peer_get_conn(peer), u);
			}
			giant_unlock();
			free(username);
			return (GFARM_ERR_NO_ERROR);
		}
	}

	giant_unlock();
	free(username);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_quota_dirset_get(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *username, *dirsetname;
	struct user *u;
	struct dirset *ds;
	struct quota_metadata q;
	struct gfarm_quota_subject_time grace;
	gfarm_uint64_t flags = 0;
	static const char diag[] = "GFM_PROTO_QUOTA_DIRSET_GET";

	e = gfm_server_get_request(peer, diag, "ss", &username, &dirsetname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		free(dirsetname);
		return (GFARM_ERR_NO_ERROR);
	}
	quota_metadata_init(&q);
	quota_subject_time_init(&grace);
	giant_lock();

	if (!from_client) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((u = user_lookup(username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (u != peer_get_user(peer) &&
	    !user_is_root(peer_get_user(peer))) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((ds = user_lookup_dirset(u, dirsetname)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else {
		e = GFARM_ERR_NO_ERROR;
		if (!dirquota_is_checked(&ds->dq))
			flags |= GFM_PROTO_QUOTA_INACCURATE;
		q = ds->dq.qmm.q;
		quota_exceed_to_grace(q.limit.grace_period, &q.exceed, &grace);
	}

	giant_unlock();
	free(username);
	free(dirsetname);
	return (gfm_server_put_reply(peer, diag, e, "llllllllllllllllll",
	    flags,
	    q.limit.grace_period,
	    q.usage.space,
	    grace.space_time,
	    q.limit.soft.space,
	    q.limit.hard.space,
	    q.usage.num,
	    grace.num_time,
	    q.limit.soft.num,
	    q.limit.hard.num,
	    q.usage.phy_space,
	    grace.phy_space_time,
	    q.limit.soft.phy_space,
	    q.limit.hard.phy_space,
	    q.usage.phy_num,
	    grace.phy_num_time,
	    q.limit.soft.phy_num,
	    q.limit.hard.phy_num));
}

gfarm_error_t
quota_dirset_put_reply(struct peer *peer, struct dirset *ds, const char *diag)
{
	struct gfarm_quota_subject_time grace;
	gfarm_uint64_t flags = 0;

	if (!dirquota_is_checked(&ds->dq))
		flags |= GFM_PROTO_QUOTA_INACCURATE;
	quota_exceed_to_grace(ds->dq.qmm.q.limit.grace_period,
	    &ds->dq.qmm.q.exceed, &grace);

	return (gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR,
	    "ssllllllllllllllllll",
	    user_name(ds->user),
	    ds->dirsetname,
	    flags,
	    ds->dq.qmm.q.limit.grace_period,
	    ds->dq.qmm.q.usage.space,
	    grace.space_time,
	    ds->dq.qmm.q.limit.soft.space,
	    ds->dq.qmm.q.limit.hard.space,
	    ds->dq.qmm.q.usage.num,
	    grace.num_time,
	    ds->dq.qmm.q.limit.soft.num,
	    ds->dq.qmm.q.limit.hard.num,
	    ds->dq.qmm.q.usage.phy_space,
	    grace.phy_space_time,
	    ds->dq.qmm.q.limit.soft.phy_space,
	    ds->dq.qmm.q.limit.hard.phy_space,
	    ds->dq.qmm.q.usage.phy_num,
	    grace.phy_num_time,
	    ds->dq.qmm.q.limit.soft.phy_num,
	    ds->dq.qmm.q.limit.hard.phy_num));
}

gfarm_error_t
gfm_server_quota_dirset_set(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *username, *dirsetname;
	struct gfarm_quota_limit_info limit;
	struct quota_metadata q;
	struct user *u;
	struct dirset *ds;
	static const char diag[] = "GFM_PROTO_QUOTA_DIRSET_SET";

	e = gfm_server_get_request(peer, diag, "sslllllllll",
	    &username, &dirsetname,
	    &limit.grace_period,
	    &limit.soft.space,
	    &limit.hard.space,
	    &limit.soft.num,
	    &limit.hard.num,
	    &limit.soft.phy_space,
	    &limit.hard.phy_space,
	    &limit.soft.phy_num,
	    &limit.hard.phy_num);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		free(dirsetname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((u = user_lookup(username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (u != peer_get_user(peer) &&
	    !user_is_root(peer_get_user(peer))) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((ds = user_lookup_dirset(u, dirsetname)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else {
		ds->dq.qmm.q.limit = limit;
		dirquota_softlimit_exceed(&ds->dq.qmm.q, ds);
		quota_metadata_memory_convert_to_db(&ds->dq.qmm, &q);
		e = db_quota_dirset_modify(username, dirsetname, &q);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_UNFIXED,
			    "failed to store dirset '%s:%s' to backend DB: %s",
			    username, dirsetname, gfarm_error_string(e));
	}

	giant_unlock();
	free(username);
	free(dirsetname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

struct dirset_dir_list_closure {
	struct gfp_xdr *client;
	struct process *process;
	char *dirsetname;
	gfarm_uint64_t count;

	/* temporary working space */
	struct dirset *ds;
};

static void
quota_dir_reply(void *closure, struct quota_dir *qd)
{
	gfarm_error_t e;
	struct dirset_dir_list_closure *c = closure;
	gfarm_ino_t inum = quota_dir_get_inum(qd);
	struct inode *inode = inode_lookup(inum);
	char *pathname;

	if (inode == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "quota_dir %lld in dirset %s:%s does not exist",
		    (long long)inum,
		    user_name(c->ds->user), c->ds->dirsetname);
		e = GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY;
	} else if ((e = inode_getdirpath(inode, c->process, &pathname))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else {
		gfp_xdr_send(c->client, "is",
		    (gfarm_int32_t)GFARM_ERR_NO_ERROR, pathname);
		free(pathname);
		return;
	}
	gfp_xdr_send(c->client, "i", (gfarm_int32_t)e);
}

static void
dirset_reply_dirs(void *closure, struct dirset *ds)
{
	struct dirset_dir_list_closure *c = closure;
	gfarm_uint32_t ndirs;

	ndirs = ds->dir_count;
	if (ndirs != ds->dir_count) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "GFM_PROTO_DIRSET_DIR_LIST: %s:%s %llu dirs - too many",
		    user_name(ds->user), ds->dirsetname,
		    (unsigned long long)ds->dir_count);
		ndirs = 0;
	}
	gfp_xdr_send(c->client, "ssi",
		     user_name(ds->user), ds->dirsetname, ndirs);

	if (ndirs > 0) {
		c->ds = ds;
		quota_dir_foreach_in_dirset(ds->dir_list, c, quota_dir_reply);
	}
}

static void
named_dirset_reply_per_user(void *closure, struct user *u)
{
	struct dirset_dir_list_closure *c = closure;
	int all_dirsets = *c->dirsetname == '\0';
	struct dirsets *sets;
	struct dirset *ds;

	if (all_dirsets) {
		sets = user_get_dirsets(u);
		if (sets != NULL)
			dirset_foreach_in_dirsets(sets, c, dirset_reply_dirs);
	} else {
		ds = user_lookup_dirset(u, c->dirsetname);
		if (ds != NULL)
			dirset_reply_dirs(c, ds);
	}
}

static void
named_dirset_count_per_user(void *closure, struct user *u)
{
	struct dirset_dir_list_closure *c = closure;
	int all_dirsets = *c->dirsetname == '\0';
	struct dirsets *sets;
	struct dirset *ds;

	if (all_dirsets) {
		sets = user_get_dirsets(u);
		if (sets != NULL)
			c->count += sets->dirset_count;
	} else {
		ds = user_lookup_dirset(u, c->dirsetname);
		if (ds != NULL)
			c->count++;
	}
}

gfarm_error_t
gfm_server_dirset_dir_list(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;
	char *username, *dirsetname;
	struct user *u = NULL;
	int all_users;
	struct dirset_dir_list_closure closure;
	static const char diag[] = "GFM_PROTO_DIRSET_DIR_LIST";

	e = gfm_server_get_request(peer, diag, "ss", &username, &dirsetname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		free(dirsetname);
		return (GFARM_ERR_NO_ERROR);
	}
	all_users = *username == '\0';
	giant_lock();

	if (!from_client) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s has no process",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (!all_users && (u = user_lookup(username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (all_users ? !user_is_root(peer_get_user(peer)) :
	    (u != peer_get_user(peer) && !user_is_root(peer_get_user(peer)))) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		/* XXX FIXME too long giant lock */
		closure.client = peer_get_conn(peer);
		closure.process = process;
		closure.dirsetname = dirsetname;
		closure.count = 0;
		if (all_users) {
			user_foreach(&closure, named_dirset_count_per_user, 1);
		} else {
			named_dirset_count_per_user(&closure, u);
		}
		if ((gfarm_uint32_t)closure.count != closure.count) {
			e = GFARM_ERR_MESSAGE_TOO_LONG;
		} else if ((e = gfm_server_put_reply(peer, diag, e, "i",
		    (gfarm_uint32_t)closure.count)) != GFARM_ERR_NO_ERROR) {
			;
		} else {
			if (all_users) {
				user_foreach(&closure,
				    named_dirset_reply_per_user, 1);
			} else {
				named_dirset_reply_per_user(&closure, u);
			}
			giant_unlock();
			free(username);
			free(dirsetname);
			return (GFARM_ERR_NO_ERROR);
		}
	}

	giant_unlock();
	free(username);
	free(dirsetname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}
