#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <gfarm/gfarm.h>

#include "hash.h"
#include "queue.h"

#include "quota_info.h"
#include "auth.h"
#include "config.h"

#include "peer.h"
#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "user.h"
#include "inode.h"
#include "quota.h"
#include "dirset.h"
#include "process.h"

#define MANAGEMENT_USERS 1000

/*
 * quota_dir
 */

struct quota_dir {
	/*
	 * quota_dir entry will NOT be removed from list_head at its removal,
	 * it will remain linked at that time,
	 * but will be removed at quota_dir_free(),
	 * to make quota_dir_foreach_in_dirset_interruptible() work.
	 */
	GFARM_HCIRCLEQ_ENTRY(quota_dir) dirset_node;

	struct dirset *ds;
	gfarm_ino_t inum;

	gfarm_uint64_t refcount;
	int valid;
};

static struct gfarm_hash_table *quota_dir_table;

gfarm_error_t
quota_dir_enter(gfarm_ino_t inum, struct dirset *ds, struct quota_dir **qdp)
{
	struct quota_dir *qd, *list_head;
	struct gfarm_hash_entry *entry;
	int created;

	entry = gfarm_hash_enter(quota_dir_table,
	    &inum, sizeof(inum), sizeof(qd), &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1004642,
		    "quota_dir_enter: gfarm_hash_enter() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_debug(GFARM_MSG_1004643,
		    "quota_dir_enter_enter: already exists");
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	GFARM_MALLOC(qd);
	if (qd == NULL) {
		gfarm_hash_purge(quota_dir_table, &inum, sizeof(inum));
		gflog_debug(GFARM_MSG_1004644,
		    "quota_dir_enter: no memory for quota_dir(%lld)",
		    (long long)inum);
		return (GFARM_ERR_NO_MEMORY);
	}
	*(struct quota_dir **)gfarm_hash_entry_data(entry) = qd;

	qd->ds = ds;
	qd->inum = inum;
	qd->refcount = 1;
	qd->valid = 1;

	dirset_add_dir(ds, &list_head);
	GFARM_HCIRCLEQ_INSERT_TAIL(*list_head, qd, dirset_node);
	dirset_add_ref(ds);

	if (qdp != NULL)
		*qdp = qd;
	return (GFARM_ERR_NO_ERROR);
}

static void
quota_dir_free(struct quota_dir *qd)
{
	GFARM_HCIRCLEQ_REMOVE(qd, dirset_node);
	dirset_del_ref(qd->ds);
	free(qd);
}

static void
quota_dir_add_ref(struct quota_dir *qd)
{
	++qd->refcount;
}

static int
quota_dir_del_ref(struct quota_dir *qd)
{
	--qd->refcount;
	if (qd->refcount > 0)
		return (1); /* still referenced */

	quota_dir_free(qd);
	return (0); /* freed */
}

static void
quota_dir_remove_internal(struct quota_dir *qd)
{
	gfarm_hash_purge(quota_dir_table, &qd->inum, sizeof(qd->inum));
	dirset_remove_dir(qd->ds);
	/* keep qd->ds as is, for future dirset_del_ref() */
	qd->valid = 0;

	/*
	 * do not remove from qd->ds->dir_list here,
	 * to make quota_dir_foreach_in_dirset_interruptible() work.
	 */

	quota_dir_del_ref(qd);
}

struct quota_dir *
quota_dir_list_new(void)
{
	struct quota_dir *list_head;

	GFARM_MALLOC(list_head);
	if (list_head == NULL)
		return (NULL);
	list_head->ds = NULL;
	list_head->inum = 0;

	GFARM_HCIRCLEQ_INIT(*list_head, dirset_node);
	return (list_head);
}

void quota_dir_list_free(struct quota_dir *list_head)
{
	assert(GFARM_HCIRCLEQ_EMPTY(*list_head, dirset_node));
	free(list_head);
}

void
quota_dir_foreach_in_dirset(struct quota_dir *list_head,
	void *closure, void (*callback)(void *, struct quota_dir *))
{
	struct quota_dir *i, *tmp;

	GFARM_HCIRCLEQ_FOREACH_SAFE(i, *list_head, dirset_node, tmp) {
		if (i->valid)
			callback(closure, i);
	}
}

int
quota_dir_foreach_in_dirset_interruptible(struct quota_dir *list_head,
	void *closure, int (*callback)(void *, struct quota_dir *))
{
	struct quota_dir *qd, *next;
	int interrupted;

	for (qd = GFARM_HCIRCLEQ_FIRST(*list_head, dirset_node);
	    !GFARM_HCIRCLEQ_IS_END(*list_head, qd);) {
		if (!qd->valid) {
			qd = GFARM_HCIRCLEQ_NEXT(qd, dirset_node);
		} else {
			quota_dir_add_ref(qd);

			/*
			 * CAUTION: this callback function may
			 * release and re-acquire giant_lock internally
			 */
			interrupted = (*callback)(closure, qd);

			next = GFARM_HCIRCLEQ_NEXT(qd, dirset_node);
			quota_dir_del_ref(qd);
			qd = next;

			if (interrupted)
				return (1);
		}
	}
	return (0);
}

static struct quota_dir *
quota_dir_lookup(gfarm_ino_t inum)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(quota_dir_table, &inum, sizeof(inum));
	if (entry == NULL)
		return (NULL);
	return (*(struct quota_dir **)gfarm_hash_entry_data(entry));
}

gfarm_error_t
quota_dir_remove(gfarm_ino_t inum)
{
	gfarm_error_t e;
	struct quota_dir *qd = quota_dir_lookup(inum);

	if (qd == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	e = db_quota_dir_remove(qd->inum);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004645,
		    "failed to remove quota dir %lld from backend DB: %s",
		    (long long)qd->inum, gfarm_error_string(e));

	quota_dir_remove_internal(qd);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
quota_dir_remove_in_cache(gfarm_ino_t inum)
{
	struct quota_dir *qd = quota_dir_lookup(inum);

	if (qd == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	quota_dir_remove_internal(qd);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_ino_t
quota_dir_get_inum(struct quota_dir *qd)
{
	return (qd->inum);
}

struct dirset *
quota_dir_get_dirset_by_inum(gfarm_ino_t inum)
{
	struct quota_dir *qd = quota_dir_lookup(inum);

	if (qd == NULL)
		return (NULL);
	return (qd->ds);
}

static void
quota_dir_add_one(
	void *closure, gfarm_ino_t inum, struct gfarm_dirset_info *dirset)
{
	gfarm_error_t e;
	struct user *u;
	struct dirset *ds;
	static const char diag[] = "quota_dir_add_one";

	u = user_lookup(dirset->username);
	if (u == NULL) {
		gflog_error(GFARM_MSG_1004646,
		    "%s: unknown user %s", diag, dirset->username);
		return;
	}
	ds = user_lookup_dirset(u, dirset->dirsetname);
	if (ds == NULL) {
		gflog_error(GFARM_MSG_1004647,
		    "%s: unknown dirset %s:%s",
		    diag, dirset->username, dirset->dirsetname);
		return;
	}
	e = quota_dir_enter(inum, ds, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004648,
		    "%s: cannot enter inode %lld to dirset %s:%s", diag,
		    (long long)inum, dirset->username, dirset->dirsetname);
		return;
	}

	free(dirset->username);
	free(dirset->dirsetname);
}

void
quota_dir_init(void)
{
	gfarm_error_t e;

	/* XXX DQTODO: this estimate of number may be totally wrong */
	quota_dir_table = gfarm_hash_table_alloc(
	    gfarm_directory_quota_count_per_user_limit * MANAGEMENT_USERS,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	if (quota_dir_table == NULL)
		gflog_fatal(GFARM_MSG_1004649,
		    "quota_dir_init: cannot create hash table (size %d)",
		    gfarm_directory_quota_count_per_user_limit *
		    MANAGEMENT_USERS);

	e = db_quota_dir_load(NULL, quota_dir_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1004650,
		    "loading quota_dir: %s", gfarm_error_string(e));
}

/*
 * protocol handlers
 */

gfarm_error_t
gfm_server_quota_dir_get(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;
	struct user *user;
	gfarm_int32_t fd;
	struct inode *inode;
	struct dirset *tdirset;
	static const char diag[] = "GFM_PROTO_QUOTA_DIR_GET";

	e = gfm_server_get_request(peer, diag, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client) {
		gflog_debug(GFARM_MSG_1004651,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1004652, "%s: %s has no process",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_1004653, "%s: user %s inconsistent",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004654, "%s: %s has no descriptor",
		    diag, peer_get_username(peer));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		;
	} else if (user != inode_get_user(inode) &&
	    !user_is_root_for_inode(user, inode) &&
	    (e = inode_access(inode, user, GFS_X_OK|GFS_W_OK))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004655, "%s: %s has no privilege",
		    diag, peer_get_username(peer));
	} else if (!inode_is_dir(inode)) {
		gflog_debug(GFARM_MSG_1004656, "%s: %s specified non-dir",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_NOT_A_DIRECTORY;
	} else if ((tdirset = inode_search_tdirset(inode))
	    == TDIRSET_IS_UNKNOWN || tdirset == TDIRSET_IS_NOT_SET) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else {
		e = quota_dirset_put_reply(peer, tdirset, diag);
		giant_unlock();
		return (e);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

static gfarm_error_t
quota_dir_settable(struct inode *inode, struct dirset *ds,
	struct user *user, const char *diag)
{
	gfarm_error_t e;

	e = dirquota_limit_check(ds, 1, 0, 0);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (inode_dir_is_empty(inode))
		return (GFARM_ERR_NO_ERROR);

	/*
	 * use user_is_root() instead of user_is_root_for_inode() here,
	 * because inode_is_ok_to_set_dirset() may take giant_lock
	 * too long period,
	 * and only real gfarmroot is allowed to do such thing
	 * usually during maintenance.
	 */
	if (!user_is_root(user)) {
		gflog_debug(GFARM_MSG_1004657,
		    "%s: %s specified non-empty dir %lld:%lld",
		    diag, user_name(user),
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode));
		return (GFARM_ERR_DIRECTORY_NOT_EMPTY);
	} else if (!inode_is_ok_to_set_dirset(inode)) {
		/* nested quota_dir, or has a hardlink to outside of the dir */
		gflog_debug(GFARM_MSG_1004658,
		    "%s: %s specified prohibited dir %lld:%lld",
		    diag, user_name(user),
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode));
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_quota_dir_set(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *username, *dirsetname;
	struct process *process;
	struct user *user, *dirset_user;
	gfarm_int32_t fd;
	struct inode *inode;
	struct dirset *ds, *existing_tdirset;
	int transaction = 0;
	static const char diag[] = "GFM_PROTO_QUOTA_DIR_SET";

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
		gflog_debug(GFARM_MSG_1004659,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1004660, "%s: %s has no process",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_1004661, "%s: user %s inconsistent",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004662, "%s: %s has no descriptor",
		    diag, peer_get_username(peer));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		;
	} else if (user != inode_get_user(inode) &&
	    !user_is_root_for_inode(user, inode)) {
		gflog_debug(GFARM_MSG_1004663, "%s: %s has no privilege",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((dirset_user = user_lookup(username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (user != dirset_user && !user_is_root(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((ds = user_lookup_dirset(dirset_user, dirsetname))
	    == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if (!inode_is_dir(inode)) {
		gflog_debug(GFARM_MSG_1004664, "%s: %s specified non-dir",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_NOT_A_DIRECTORY;
	} else if ((existing_tdirset = inode_search_tdirset(inode))
	    == TDIRSET_IS_UNKNOWN) {
		e = GFARM_ERR_UNKNOWN;
	} else if (existing_tdirset != TDIRSET_IS_NOT_SET) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if ((e = quota_dir_settable(inode, ds, user, diag))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else if ((e = quota_dir_enter(inode_get_number(inode), ds, NULL))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;

		/* if this changes exceed, calls db_quota_dirset_modify() */
		if (inode_dir_is_empty(inode)) {
			dirquota_update_file_add(inode, ds);
		} else { /* user_is_root(user) case */
			inode_subtree_fixup_tdirset(inode, ds);
			dirquota_invalidate(ds);
		}
		e = db_quota_dir_add(inode_get_number(inode),
		    dirset_get_username(ds), dirset_get_dirsetname(ds));

		if (transaction)
			db_end(diag);

		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1004665,
			    "failed to store quota_dir %lld (dirset '%s:%s') "
			    "to backend DB: %s",
			    (long long)inode_get_number(inode),
			    dirset_get_username(ds), dirset_get_dirsetname(ds),
			    gfarm_error_string(e));
	}

	giant_unlock();
	free(username);
	free(dirsetname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}
