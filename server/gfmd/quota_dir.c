#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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
#include "dirset.h"
#include "process.h"

#define MANAGEMENT_USERS 1000

/*
 * quota_dir
 */

struct quota_dir {
	GFARM_HCIRCLEQ_ENTRY(quota_dir) dirset_node;
	struct dirset *ds;
	gfarm_ino_t inum;
};

GFARM_STAILQ_HEAD(quota_dir_stailq, quota_dir);

static struct gfarm_hash_table *quota_dir_table;

gfarm_error_t
quota_dir_enter(gfarm_ino_t inum, struct dirset *ds, struct quota_dir **qdp)
{
	struct quota_dir *qd, *list_head;
	struct gfarm_hash_entry *entry;
	int created;

	entry = gfarm_hash_enter(quota_dir_table,
	    &inum, sizeof(inum), sizeof(*qd), &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "quota_dir_enter: gfarm_hash_enter() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "quota_dir_enter_enter: already exists");
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	qd = gfarm_hash_entry_data(entry);
	qd->ds = ds;
	qd->inum = inum;

	dirset_add_dir(ds, &list_head);
	GFARM_HCIRCLEQ_INSERT_TAIL(*list_head, qd, dirset_node);

	if (qdp != NULL)
		*qdp = qd;
	return (GFARM_ERR_NO_ERROR);
}

static void
quota_dir_free(struct quota_dir *qd)
{
	struct dirset *ds = qd->ds;

	gfarm_hash_purge(quota_dir_table, &qd->inum, sizeof(qd->inum));
	dirset_remove_dir(ds);
	GFARM_HCIRCLEQ_REMOVE(qd, dirset_node);
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
		callback(closure, i);
	}
}

static struct quota_dir *
quota_dir_lookup(gfarm_ino_t inum)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(quota_dir_table, &inum, sizeof(inum));
	if (entry == NULL)
		return (NULL);
	return (gfarm_hash_entry_data(entry));
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
		gflog_error(GFARM_MSG_UNFIXED,
		    "failed to remove quota dir %lld from backend DB: %s",
		    (long long)qd->inum, gfarm_error_string(e));

	quota_dir_free(qd);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
quota_dir_remove_in_cache(gfarm_ino_t inum)
{
	struct quota_dir *qd = quota_dir_lookup(inum);

	if (qd == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	quota_dir_free(qd);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
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
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: unknown user %s", diag, dirset->username);
		return;
	}
	ds = user_lookup_dirset(u, dirset->dirsetname);
	if (ds == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: unknown dirset %s:%s",
		    diag, dirset->username, dirset->dirsetname);
		return;
	}
	e = quota_dir_enter(inum, ds, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: cannot enter inode %lld to dirset %s:%s", diag,
		    (long long)inum, dirset->username, dirset->dirsetname);
		return;
	}
}

void
quota_dir_init(void)
{
	gfarm_error_t e;

	quota_dir_table = gfarm_hash_table_alloc(
	    gfarm_directory_quota_count_per_user_limit * MANAGEMENT_USERS,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	if (quota_dir_table == NULL)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "quota_dir_init: cannot create hash table (size %d)",
		    gfarm_directory_quota_count_per_user_limit *
		    MANAGEMENT_USERS);

	e = db_quota_dir_load(NULL, quota_dir_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
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
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s has no process",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: user %s inconsistent",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s has no descriptor",
		    diag, peer_get_username(peer));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		;
	} else if (user != inode_get_user(inode) &&
	    !user_is_root_for_inode(user, inode)) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s has no privilege",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (!inode_is_dir(inode)) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s specified non-dir",
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
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: from gfsd %s", diag, peer_get_hostname(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s has no process",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: user %s inconsistent",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s has no descriptor",
		    diag, peer_get_username(peer));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		;
	} else if (user != inode_get_user(inode) &&
	    !user_is_root_for_inode(user, inode)) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s has no privilege",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_PERMISSION_DENIED;
	} else if ((dirset_user = user_lookup(username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (user != dirset_user && !user_is_root(user)) {
		e = GFARM_ERR_PERMISSION_DENIED;
	} else if ((ds = user_lookup_dirset(dirset_user, dirsetname))
	    == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if (!inode_is_dir(inode)) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s specified non-dir",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_NOT_A_DIRECTORY;
	} else if (!inode_dir_is_empty(inode)) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s specified non-empty dir",
		    diag, peer_get_username(peer));
		e = GFARM_ERR_DIRECTORY_NOT_EMPTY;
	} else if ((existing_tdirset = inode_search_tdirset(inode))
	    == TDIRSET_IS_UNKNOWN) {
		e = GFARM_ERR_UNKNOWN;
	} else if (existing_tdirset != TDIRSET_IS_NOT_SET) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if ((e = quota_dir_enter(inode_get_number(inode), ds, NULL))
	    != GFARM_ERR_NO_ERROR) {
		;
	} else {
		e = db_quota_dir_add(inode_get_number(inode),
		    dirset_get_username(ds), dirset_get_dirsetname(ds));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_UNFIXED,
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
