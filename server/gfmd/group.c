#include <pthread.h>	/* db_access.h currently needs this */
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"

#include "context.h"
#include "gfp_xdr.h"
#include "auth.h"
#include "gfm_proto.h"

#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "user.h"
#include "group.h"
#include "peer.h"
#include "quota.h"
#include "mdhost.h"
#include "relay.h"

#define GROUP_HASHTAB_SIZE	3079	/* prime number */

struct group {
	char *groupname;
	struct group_assignment users;
	struct quota q;
	int invalid;	/* set when deleted */
};

char ADMIN_GROUP_NAME[] = "gfarmadm";
char ROOT_GROUP_NAME[] = "gfarmroot";
char REMOVED_GROUP_NAME[] = "gfarm-removed-group";

static struct gfarm_hash_table *group_hashtab = NULL;

gfarm_error_t
grpassign_add(struct user *u, struct group *g)
{
	struct group_assignment *ga;

	GFARM_MALLOC(ga);
	if (ga == NULL) {
		gflog_debug(GFARM_MSG_1001514,
		    "memory allocation of group_assignment failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	ga->u = u;
	ga->g = g;

	ga->user_next = &g->users;
	ga->user_prev = g->users.user_prev;
	g->users.user_prev->user_next = ga;
	g->users.user_prev = ga;

	grpassign_add_group(ga);

	return (GFARM_ERR_NO_ERROR);
}

void
grpassign_remove(struct group_assignment *ga)
{
	ga->user_prev->user_next = ga->user_next;
	ga->user_next->user_prev = ga->user_prev;

	ga->group_prev->group_next = ga->group_next;
	ga->group_next->group_prev = ga->group_prev;

	free(ga);
}

static void
group_invalidate(struct group *g)
{
	g->invalid = 1;
}

static void
group_validate(struct group *g)
{
	g->invalid = 0;
}

int
group_is_invalid(struct group *g)
{
	return (g->invalid != 0);
}

int
group_is_valid(struct group *g)
{
	return (g->invalid == 0);
}

struct group *
group_lookup_including_invalid(const char *groupname)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(group_hashtab,
	    &groupname, sizeof(groupname));
	if (entry == NULL)
		return (NULL);
	return (*(struct group **)gfarm_hash_entry_data(entry));
}

struct group *
group_lookup(const char *groupname)
{
	struct group *g = group_lookup_including_invalid(groupname);

	if (g != NULL && group_is_valid(g))
		return (g);
	return (NULL);
}

/* note that groupname may be free'ed */
static gfarm_error_t
group_enter(char *groupname, struct group **gpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct group *g;

	g = group_lookup_including_invalid(groupname);
	if (g != NULL) {
		if (group_is_invalid(g)) {
			group_validate(g);
			if (gpp != NULL)
				*gpp = g;
			free(groupname);
			return (GFARM_ERR_NO_ERROR);
		} else {
			gflog_debug(GFARM_MSG_1001515,
			    "\"%s\" group already exists", group_name(g));
			return (GFARM_ERR_ALREADY_EXISTS);
		}
	}

	GFARM_MALLOC(g);
	if (g == NULL) {
		gflog_debug(GFARM_MSG_1001516,
		    "memory allocation of group failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	g->groupname = groupname;

	entry = gfarm_hash_enter(group_hashtab,
	    &g->groupname, sizeof(g->groupname), sizeof(struct group *),
	    &created);
	if (entry == NULL) {
		free(g);
		gflog_debug(GFARM_MSG_1001517,
		    "gfarm_hash_enter() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_debug(GFARM_MSG_1001518,
		    "\"%s\" group already exists", group_name(g));
		free(g);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	quota_data_init(&g->q);
	g->users.user_prev = g->users.user_next = &g->users;
	*(struct group **)gfarm_hash_entry_data(entry) = g;
	group_validate(g);
	if (gpp != NULL)
		*gpp = g;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
group_remove_internal(const char *groupname, int update_quota)
{
	struct gfarm_hash_entry *entry;
	struct group *g;
	struct group_assignment *ga;

	entry = gfarm_hash_lookup(group_hashtab,
	    &groupname, sizeof(groupname));
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001519,
		    "\"%s\" group does not exist", groupname);
		return (GFARM_ERR_NO_SUCH_GROUP);
	}
	g = *(struct group **)gfarm_hash_entry_data(entry);
	if (group_is_invalid(g)) {
		gflog_debug(GFARM_MSG_1001520,
		    "\"%s\" group is invalid", groupname);
		return (GFARM_ERR_NO_SUCH_GROUP);
	}
	if (update_quota)
		quota_group_remove(g);
	/*
	 * do not purge the hash entry.  Instead, invalidate it so
	 * that it can be activated later.
	 */
	group_invalidate(g);

	/* free group_assignment */
	while ((ga = g->users.user_next) != &g->users)
		grpassign_remove(ga);
	g->users.user_prev = g->users.user_next = &g->users;

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
group_remove(const char *groupname)
{
	return (group_remove_internal(groupname, 1));
}

gfarm_error_t
group_remove_in_cache(const char *groupname)
{
	return (group_remove_internal(groupname, 0));
}

struct group *
group_lookup_or_enter_invalid(const char *groupname)
{
	gfarm_error_t e;
	struct group *g = group_lookup_including_invalid(groupname);
	char *n;
	static const char diag[] = "group_lookup_or_enter_invalid";

	if (g != NULL)
		return (g);

	n = strdup_ck(groupname, diag);
	if (n == NULL) {
		gflog_error(GFARM_MSG_1002758,
		    "group_lookup_or_enter_invalid(%s): no memory", groupname);
		return (NULL);
	}
	e = group_enter(n, &g);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002759,
		    "group_lookup_or_enter_invalid(%s): group_enter: %s",
		    groupname, gfarm_error_string(e));
		free(n);
		return (NULL);
	}
	e = group_remove_in_cache(groupname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002760,
		    "group_lookup_or_enter_invalid(%s): group_remove: %s",
		    groupname, gfarm_error_string(e));
	}
	return (g);
}

char *
group_name(struct group *g)
{
	return (g != NULL && group_is_valid(g) ?
	    g->groupname : REMOVED_GROUP_NAME);
}

struct quota *
group_quota(struct group *g)
{
	return (&g->q);
}

void
group_all(void *closure, void (*callback)(void *, struct group *),
	  int valid_only)
{
	struct gfarm_hash_iterator it;
	struct group **g;

	for (gfarm_hash_iterator_begin(group_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		g = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (!valid_only || group_is_valid(*g))
			callback(closure, *g);
	}
}

/* The memory owner of `*gi' is changed to group.c */
gfarm_error_t
group_info_add(struct gfarm_group_info *gi)
{
	struct group *g;
	gfarm_error_t e = group_enter(gi->groupname, &g);
	int i;
	struct user *u;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000241,
		    "group_add_one: adding group %s: %s",
		    gi->groupname, gfarm_error_string(e));
		gfarm_group_info_free(gi);
		return (e);
	}
	for (i = 0; i < gi->nusers; i++) {
		u = user_lookup(gi->usernames[i]);
		if (u == NULL) {
			gflog_warning(GFARM_MSG_1000242,
			    "group_add_one: unknown user %s",
			    gi->usernames[i]);
			(void)group_remove(g->groupname);
			e = GFARM_ERR_NO_SUCH_USER;
			goto free_gi;
		}
		e = grpassign_add(u, g);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000243,
			    "group_add_one: grpassign(%s, %s): %s",
			    gi->usernames[i], g->groupname,
			    gfarm_error_string(e));
			(void)group_remove(g->groupname);
			goto free_gi;
		}
	}
free_gi:
	/* do not free gi->groupname */
	gi->groupname = NULL;
	gfarm_group_info_free(gi);
	return (e);
}

/* The memory owner of `*gi' is changed to group.c */
void
group_add_one(void *closure, struct gfarm_group_info *gi)
{
	gfarm_error_t e = group_info_add(gi);

	if (e != GFARM_ERR_NO_ERROR) {
		/* cannot use gi.groupname, since it's freed here */
		gflog_warning(GFARM_MSG_1002314, "group_add_one(): %s",
		    gfarm_error_string(e));
	}
}

gfarm_error_t
group_add_user(struct group *g, const char *username)
{
	struct user *u = user_lookup(username);

	if (u == NULL) {
		gflog_debug(GFARM_MSG_1001521,
		    "\"%s\" does not exist", username);
		return (GFARM_ERR_NO_SUCH_USER);
	}
	if (g == NULL || group_is_invalid(g)) {
		gflog_debug(GFARM_MSG_1001522,
		    "group is invalid or does not exist");
		return (GFARM_ERR_NO_SUCH_GROUP);
	}
	if (user_in_group(u, g)) {
		gflog_debug(GFARM_MSG_1001523,
		    "\"%s\" is already a member in \"%s\"",
		    username, group_name(g));
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	return (grpassign_add(u, g));
}

void
group_add_user_and_record(struct group *g, const char *username)
{
	gfarm_error_t e = group_add_user(g, username);
	struct gfarm_group_info gi;
	int n;
	struct group_assignment *ga;

	if (e == GFARM_ERR_ALREADY_EXISTS)
		return;
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000244,
		    "failed to add user %s to group %s: %s",
		    username, group_name(g), gfarm_error_string(e));
		return;
	}

	gflog_info(GFARM_MSG_1000245,
	    "added user %s to group %s", username, group_name(g));
	gi.groupname = group_name(g);
	n = 0;
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next)
		if (user_is_valid(ga->u))
			n++;
	gi.nusers = n;
	GFARM_MALLOC_ARRAY(gi.usernames, n);
	if (gi.usernames == NULL)
		gflog_fatal(GFARM_MSG_1002315,
		    "group_add_user_and_record(%s): no memory", username);
	n = 0;
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next)
		if (user_is_valid(ga->u))
			gi.usernames[n++] = user_name(ga->u);

	e = db_group_modify(&gi, 0, 1, &username, 0, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000246,
		    "failed to record user '%s' as group '%s' to storage: %s",
		    username, gi.groupname, gfarm_error_string(e));
	}

	free(gi.usernames);
}

void
group_init(void)
{
	gfarm_error_t e;
	struct group *admin;
	struct gfarm_group_info gi;
	static const char diag[] = "group_init";

	group_hashtab =
	    gfarm_hash_table_alloc(GROUP_HASHTAB_SIZE,
		gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (group_hashtab == NULL)
		gflog_fatal(GFARM_MSG_1000247, "no memory for group hashtab");

	e = db_group_load(NULL, group_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000248,
		    "loading groups: %s", gfarm_error_string(e));

	if ((admin = group_lookup(ADMIN_GROUP_NAME)) == NULL) {
		gflog_info(GFARM_MSG_1000249,
		    "group %s not found, creating it",
		    ADMIN_GROUP_NAME);

		gi.groupname = strdup_ck(ADMIN_GROUP_NAME, diag);
		gi.nusers = gfarm_ctxp->metadb_admin_user == NULL ? 1 : 2;
		GFARM_MALLOC_ARRAY(gi.usernames, gi.nusers);
		if (gi.usernames == NULL)
			gflog_fatal(GFARM_MSG_1002316,
			    "creating group %s: no memory", gi.groupname);
		gi.usernames[0] = strdup_ck(ADMIN_USER_NAME, diag);
		if (gfarm_ctxp->metadb_admin_user != NULL)
			gi.usernames[1] =
			    strdup_ck(gfarm_ctxp->metadb_admin_user, diag);

		/*
		 * We have to call this before group_add_one(),
		 * because group_add_one() frees the memory of gi
		 */
		e = db_group_add(&gi);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000250,
			    "failed to store group '%s' to storage: %s",
			    gi.groupname, gfarm_error_string(e));

		group_add_one(NULL, &gi); /* this should always success */
	} else {
		group_add_user_and_record(admin, ADMIN_USER_NAME);
		if (gfarm_ctxp->metadb_admin_user != NULL)
			group_add_user_and_record(admin,
			    gfarm_ctxp->metadb_admin_user);
	}

	if ((admin = group_lookup(ROOT_GROUP_NAME)) == NULL) {
		gflog_info(GFARM_MSG_1000251,
		    "group %s not found, creating it",
		    ROOT_GROUP_NAME);

		gi.groupname = strdup_ck(ROOT_GROUP_NAME, diag);
		gi.nusers = 0;
		gi.usernames = NULL;

		/*
		 * We have to call this before group_add_one(),
		 * because group_add_one() frees the memory of gi
		 */
		e = db_group_add(&gi);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000252,
			    "failed to store group '%s' to storage: %s",
			    gi.groupname, gfarm_error_string(e));

		group_add_one(NULL, &gi); /* this should always success */
	}
}

#ifndef TEST
/*
 * protocol handler
 */

gfarm_error_t
group_info_send(struct gfp_xdr *client, struct group *g)
{
	gfarm_error_t e;
	int n;
	struct group_assignment *ga;

	n = 0;
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next)
		if (user_is_valid(ga->u))
			n++;
	e = gfp_xdr_send(client, "si", g->groupname, n);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001524,
			"gfp_xdr_send(groupname) failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next) {
		if (user_is_valid(ga->u))
			if ((e = gfp_xdr_send(client, "s", user_name(ga->u)))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001525,
					"gfp_xdr_send(user_name) failed: %s",
					gfarm_error_string(e));
				return (e);
			}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_group_info_get_all(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct peer *mhpeer;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
	int size_pos;
	struct gfarm_hash_iterator it;
	gfarm_int32_t ngroups;
	struct group **gp;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_GET_ALL";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e_rpc = wait_db_update_info(peer, DBUPDATE_GROUP, diag);
	if (e_rpc != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e_rpc));
	}

	/* XXX FIXME too long giant lock */
	giant_lock();

	ngroups = 0;
	for (gfarm_hash_iterator_begin(group_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		gp = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (group_is_valid(*gp))
			++ngroups;
	}

	e_ret = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    e_rpc, "i", ngroups);
	if (e_ret != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		gflog_debug(GFARM_MSG_1001526,
		    "gfm_server_put_reply(%s): %s",
		    diag, gfarm_error_string(e_ret));
		return (e_ret);
	}
	for (gfarm_hash_iterator_begin(group_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		gp = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (group_is_valid(*gp)) {
			/* XXXRELAY FIXME */
			e_ret = group_info_send(client, *gp);
			if (e_ret != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001527,
					"group_info_send() failed: %s",
					gfarm_error_string(e_ret));
				giant_unlock();
				gfm_server_put_reply_end(peer, mhpeer, diag,
				    size_pos);
				return (e_ret);
			}
		}
	}
	giant_unlock();
	gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);
	return (e_ret);
}

gfarm_error_t
gfm_server_group_info_get_by_names(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct peer *mhpeer;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	int size_pos;
	gfarm_int32_t ngroups;
	char *groupname, **groups;
	int i, j, eof, no_memory = 0;
	struct group *g;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_GET_BY_NAMES";

	e = gfm_server_get_request(peer, sizep, diag, "i", &ngroups);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC_ARRAY(groups, ngroups);
	if (groups == NULL) {
		no_memory = 1;
		/* Continue processing. */
	}

	for (i = 0; i < ngroups; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &groupname);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			gflog_debug(GFARM_MSG_1003465,
			    "%s: gfp_xdr_recv(): %s",
			    diag, gfarm_error_string(e));
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_PROTOCOL;
			if (groups != NULL) {
				for (j = 0; j < i; j++)
					free(groups[j]);
				free(groups);
			}
			return (e);
		}
		if (groups == NULL) {
			free(groupname);
		} else {
			if (groupname == NULL)
				no_memory = 1;
			groups[i] = groupname;
		}
	}
	if (skip) {
		e = GFARM_ERR_NO_ERROR; /* ignore GFARM_ERR_NO_MEMORY */
		goto free_groups;
	}

	if (no_memory) {
		e = GFARM_ERR_NO_MEMORY;
	} else if ((e = wait_db_update_info(peer, DBUPDATE_GROUP, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e));
	}

	e = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    e, "");
	/* if network error doesn't happen, `e' holds RPC result here */
	if (e != GFARM_ERR_NO_ERROR)
		goto free_groups;

	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < ngroups; i++) {
		g = group_lookup(groups[i]);
		if (g == NULL) {
			gflog_debug(GFARM_MSG_1003466,
			    "%s: group lookup <%s>: failed", diag, groups[i]);
			e = gfp_xdr_send(client, "i", GFARM_ERR_NO_SUCH_GROUP);
		} else {
			gflog_debug(GFARM_MSG_1003467,
			    "%s: group lookup <%s>: ok", diag, groups[i]);
			e = gfp_xdr_send(client, "i", GFARM_ERR_NO_ERROR);
			if (e == GFARM_ERR_NO_ERROR)
				e = group_info_send(client, g);
		}
		if (peer_had_protocol_error(peer))
			break;
	}
	/*
	 * if (!peer_had_protocol_error(peer))
	 *	the variable `e' holds last group's reply code
	 */
	giant_unlock();

	gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);

free_groups:
	if (groups != NULL) {
		for (i = 0; i < ngroups; i++)
			free(groups[i]);
		free(groups);
	}
	return (e);
}

gfarm_error_t
group_user_check(struct gfarm_group_info *gi, const char *diag)
{
	int i, j;
	struct user *u, **ulist;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	GFARM_MALLOC_ARRAY(ulist, gi->nusers);
	if (ulist == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < gi->nusers && e == GFARM_ERR_NO_ERROR; i++) {
		u = user_lookup(gi->usernames[i]);
		if (u == NULL) {
			gflog_warning(GFARM_MSG_1000253,
			    "%s: unknown user %s", diag,
				    gi->usernames[i]);
			e = GFARM_ERR_NO_SUCH_USER;
			break;
		}
		for (j = 0; j < i; j++) {
			if (ulist[j] == u) {
				gflog_warning(GFARM_MSG_1003468,
				    "%s: %s: specified multiple times",
				    diag, gi->usernames[i]);
				e = GFARM_ERR_ALREADY_EXISTS;
				break;
			}
		}
		ulist[i] = u;
	}
	free(ulist);
	return (e);
}

/*
 * Closure for
 *     gfm_server_group_info_set_request()
 *     gfm_server_group_info_set_reply()
 *     gfm_server_group_info_set()
 *     gfm_server_group_info_modify()
 *
 * They are protocol handlers for
 *     GFM_PROTO_GROUP_INFO_SET
 *     GFM_PROTO_GROUP_INFO_MODIFY
 */
struct group_info_closure {
	struct gfarm_group_info group_info;
	gfarm_error_t error;
};

static void
group_info_closure_init(struct group_info_closure *gic)
{
	gic->group_info.usernames = NULL;
	gic->group_info.groupname = NULL;
	gic->group_info.nusers = 0;
	gic->error = GFARM_ERR_NO_ERROR;
}

static gfarm_error_t
group_info_closure_alloc_nusers(struct group_info_closure *gic, int nusers)
{
	char **usernames;

	GFARM_REALLOC_ARRAY(usernames, gic->group_info.usernames, nusers);
	if (usernames == NULL) {
		gflog_debug(GFARM_MSG_1001532,
		    "allocation of usernames failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(usernames, 0, sizeof(*usernames) * nusers);
	gic->group_info.usernames = usernames;
	gic->group_info.nusers = nusers;
	return (GFARM_ERR_NO_ERROR);
}

static struct gfarm_group_info *
group_info_closure_get_group_info(struct group_info_closure *gic)
{
	return (&gic->group_info);
}

static gfarm_error_t
group_info_closure_get_error(struct group_info_closure *gic)
{
	return (gic->error);
}

static void
group_info_closure_set_error(struct group_info_closure *gic, gfarm_error_t e)
{
	gic->error = e;
}

static void
group_info_closure_term(struct group_info_closure *gic)
{
	if (gic == NULL)
		return;
	gfarm_group_info_free(&gic->group_info);
}

/*
 * Both GFM_PROTO_GROUP_INFO_SET and GFM_PROTO_GROUP_INFO_MODIFY use
 * this function to receive a request.
 */
static gfarm_error_t
gfm_server_group_info_set_request(enum request_reply_mode mode,
	struct peer *peer, size_t *sizep, int skip, struct relayed_request *r,
	void *closure, const char *diag)
{
	gfarm_error_t e_ret, e_rpc = GFARM_ERR_NO_ERROR;
	struct gfarm_group_info *gi;
	int nusers;
	char *username;
	char *groupname;
	int i;

	gi = group_info_closure_get_group_info(closure);
	if (mode == RELAY_TRANSFER) {
		groupname = gi->groupname;
		nusers = gi->nusers;
	}
	e_ret = gfm_server_relay_get_request_dynarg(peer, sizep, skip, r, diag,
	    "si", &groupname, &nusers);
	if (e_ret != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001531, "%s request failure: %s",
		    diag, gfarm_error_string(e_ret));
		return (e_ret);
	}
	gi->groupname = groupname;
	if (mode != RELAY_TRANSFER && nusers > 0)
		e_rpc = group_info_closure_alloc_nusers(closure, nusers);

	for (i = 0; i < nusers; ++i) {
		if (gi->usernames == NULL)
			username = NULL;
		else
			username = gi->usernames[i];

		e_ret = gfm_server_relay_get_request_dynarg(peer, sizep, skip,
		    r, diag, "s", &username);
		if (e_ret != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001533,
				"gfp_xdr_recv(usernames) failed: %s",
				gfarm_error_string(e_ret));
			return (e_ret);
		}

		if (gi->usernames == NULL)
			free(username);
		else
			gi->usernames[i] = username;
	}

	group_info_closure_set_error(closure, e_rpc);
	return (e_ret);
}

gfarm_error_t
gfm_server_group_info_set_reply(enum request_reply_mode mode,
	struct peer *peer, size_t *sizep, int skip, void *closure,
	const char *diag)
{
	gfarm_error_t e_ret;
	gfarm_error_t e_rpc = group_info_closure_get_error(closure);
	struct gfarm_group_info *gi =
	    group_info_closure_get_group_info(closure);
	struct user *user = peer_get_user(peer);
	int from_client = 
	    (peer_get_auth_id_type(peer) == GFARM_AUTH_ID_TYPE_USER);

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (mode != RELAY_TRANSFER) {
		giant_lock();
		if (e_rpc != GFARM_ERR_NO_ERROR) {
			;
		} else if (!from_client || user == NULL ||
		    !user_is_admin(user)) {
			gflog_debug(GFARM_MSG_1001535,
			    "operation is not permitted for user");
			e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (group_lookup(gi->groupname) != NULL) {
			gflog_debug(GFARM_MSG_1001536,
			    "group already exists");
			e_rpc = GFARM_ERR_ALREADY_EXISTS;
		} else if (strlen(gi->groupname) > GFARM_GROUP_NAME_MAX) {
			gflog_debug(GFARM_MSG_1003469,
			    "%s: too long group name: \"%s\"",
			    diag, gi->groupname);
			e_rpc = GFARM_ERR_INVALID_ARGUMENT;
		} else if ((e_rpc = group_user_check(gi, diag))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001537,
			    "group_user_check() failed: %s",
			    gfarm_error_string(e_rpc));
			/*
			 * We have to call db_group_add() before
			 * group_info_add(), because group_info_add()
			 * frees the memory of 'gi'.
			 */
		} else if ((e_rpc = db_group_add(gi)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1000254,
			    "failed to store group '%s' to storage: %s",
			    gi->groupname, gfarm_error_string(e_rpc));
		} else {
			e_rpc = group_info_add(gi);
			if (e_rpc != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002317,
				    "%s: group_info_add(): %s",
				    diag, gfarm_error_string(e_rpc));
			}
		}
		giant_unlock();
		group_info_closure_set_error(closure, e_rpc);
	}

	e_ret = gfm_server_relay_put_reply_dynarg(peer, sizep, diag, e_rpc,
	    "");
	return (e_ret);
}

gfarm_error_t
gfm_server_group_info_set(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, int from_client, int skip)
{
	gfarm_error_t e;
	struct group_info_closure closure;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_SET";

	group_info_closure_init(&closure);
	if ((e = gfm_server_relay_request_reply(peer, xid, skip,
	    gfm_server_group_info_set_request, gfm_server_group_info_set_reply,
	    GFM_PROTO_GROUP_INFO_SET, &closure, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s",
		    diag, gfarm_error_string(e));
	} else
		e = group_info_closure_get_error(&closure);

	group_info_closure_term(&closure);
	return (e);
}

void
group_modify(struct group *group, struct gfarm_group_info *gi,
	const char *diag)
{
	gfarm_error_t e;
	struct group_assignment *ga;
	int i;

	/* free group_assignment */
	while ((ga = group->users.user_next) != &group->users)
		grpassign_remove(ga);

	for (i = 0; i < gi->nusers; i++) {
		const char *username = gi->usernames[i];
		struct user *u = user_lookup(username);

		if (u == NULL) {
			gflog_warning(GFARM_MSG_1000255,
			    "%s: unknown user %s", diag,
			    username);
		} else if ((e = grpassign_add(u, group))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000256,
			    "%s: grpassign(%s, %s): %s", diag,
			    username, gi->groupname,
			    gfarm_error_string(e));
			break; /* no memory */
		}
	}
}

gfarm_error_t
gfm_server_group_info_modify_reply(enum request_reply_mode mode,
	struct peer *peer, size_t *sizep, int skip, void *closure,
	const char *diag)
{
	gfarm_error_t e_ret;
	gfarm_error_t e_rpc = group_info_closure_get_error(closure);
	struct gfarm_group_info *gi =
	    group_info_closure_get_group_info(closure);
	struct user *user = peer_get_user(peer);
	int from_client = 
	    (peer_get_auth_id_type(peer) == GFARM_AUTH_ID_TYPE_USER);
	struct group *group;

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (mode != RELAY_TRANSFER) {
		giant_lock();
		if (e_rpc != GFARM_ERR_NO_ERROR) {
			;
		} else if (!from_client || user == NULL ||
		    !user_is_admin(user)) {
			gflog_debug(GFARM_MSG_1001540,
			    "operation is not permitted for user");
			e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((group = group_lookup(gi->groupname)) == NULL) {
			gflog_debug(GFARM_MSG_1001541,
			    "group_lookup() failed");
			e_rpc = GFARM_ERR_NO_SUCH_GROUP;
		} else if ((e_rpc = group_user_check(gi, diag))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001542,
			    "group_user_check() failed: %s",
			    gfarm_error_string(e_rpc));
		} else {
			group_modify(group, gi, diag);
			/* change all entries */
			e_rpc = db_group_modify(gi, 0, 0, NULL, 0, NULL);
			if (e_rpc != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1000257,
				    "failed to modify group '%s' in db: %s",
				    gi->groupname, gfarm_error_string(e_rpc));
			}
		}
		giant_unlock();
		group_info_closure_set_error(closure, e_rpc);
	}

	e_ret = gfm_server_relay_put_reply_dynarg(peer, sizep, diag, e_rpc,
	    "");
	return (e_ret);
}

gfarm_error_t
gfm_server_group_info_modify(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, int from_client, int skip)
{
	gfarm_error_t e;
	struct group_info_closure closure;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_MODIFY";

	group_info_closure_init(&closure);
	if ((e = gfm_server_relay_request_reply(peer, xid, skip,
	    gfm_server_group_info_set_request,
	    gfm_server_group_info_modify_reply,
	    GFM_PROTO_GROUP_INFO_MODIFY, &closure, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s",
		    diag, gfarm_error_string(e));
	} else
		e = group_info_closure_get_error(&closure);

	group_info_closure_term(&closure);
	return (e);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
group_info_remove_default(const char *groupname, const char *diag)
{
	gfarm_error_t e, e2;

	if ((e = group_remove(groupname)) == GFARM_ERR_NO_ERROR) {
		e2 = db_group_remove(groupname);
		if (e2 != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000258,
			    "%s: db_group_remove: %s",
			    diag, gfarm_error_string(e2));
	}
	return (e);
}

/* this interface is made as a hook for a private extension */
gfarm_error_t (*group_info_remove)(const char *, const char *) =
	group_info_remove_default;

gfarm_error_t
gfm_server_group_info_remove(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	char *groupname;
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_REMOVE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_GROUP_INFO_REMOVE, "s", &groupname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(groupname);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		free(groupname);
	} else {
		giant_lock();
		if (!from_client || user == NULL || !user_is_admin(user)) {
			gflog_debug(GFARM_MSG_1001544,
			    "operation is not permitted for user");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (strcmp(groupname, ADMIN_GROUP_NAME) == 0 ||
			   strcmp(groupname, ROOT_GROUP_NAME) == 0) {
			gflog_debug(GFARM_MSG_1002211,
			    "%s: administrator group \"%s\" "
			    "should not be deleted", diag, groupname);
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else
			e = group_info_remove(groupname, diag);
		free(groupname);
		giant_unlock();
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_group_info_add_users(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_ADD_USERS";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000259, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, xid, sizep, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_info_remove_users(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_REMOVE_USERS";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000260, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, xid, sizep, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_names_get_by_users(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_GROUP_NAMES_GET_BY_USERS";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000261, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, xid, sizep, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#endif /* TEST */
