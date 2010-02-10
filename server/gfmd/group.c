#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"

#include "config.h"	/* gfarm_metadb_admin_user */
#include "gfp_xdr.h"
#include "auth.h"

#include "subr.h"
#include "db_access.h"
#include "user.h"
#include "group.h"
#include "peer.h"
#include "quota.h"

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
	if (ga == NULL)
		return (GFARM_ERR_NO_MEMORY);

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

int
hash_group(const void *key, int keylen)
{
	const char *const *groupnamep = key;
	const char *k = *groupnamep;

	return (gfarm_hash_default(k, strlen(k)));
}

int
hash_key_equal_group(
	const void *key1, int key1len,
	const void *key2, int key2len)
{
	const char *const *u1 = key1, *const *u2 = key2;
	const char *k1 = *u1, *k2 = *u2;
	int l1, l2;

	/* short-cut on most case */
	if (*k1 != *k2)
		return (0);
	l1 = strlen(k1);
	l2 = strlen(k2);
	if (l1 != l2)
		return (0);

	return (gfarm_hash_key_equal_default(k1, l1, k2, l2));
}

static void
group_invalidate(struct group *g)
{
	g->invalid = 1;
}

static void
group_activate(struct group *g)
{
	g->invalid = 0;
}

int
group_is_invalidated(struct group *g)
{
	return (g->invalid == 1);
}

int
group_is_active(struct group *g)
{
	return (g != NULL && !group_is_invalidated(g));
}

struct group *
group_lookup(const char *groupname)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(group_hashtab,
	    &groupname, sizeof(groupname));
	if (entry == NULL)
		return (NULL);
	return (*(struct group **)gfarm_hash_entry_data(entry));
}

/* note that groupname may be free'ed */
static gfarm_error_t
group_enter(char *groupname, struct group **gpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct group *g;

	g = group_lookup(groupname);
	if (g != NULL) {
		if (group_is_invalidated(g)) {
			group_activate(g);
			if (gpp != NULL)
				*gpp = g;
			free(groupname);
			return (GFARM_ERR_NO_ERROR);
		}
		else
			return (GFARM_ERR_ALREADY_EXISTS);
	}

	GFARM_MALLOC(g);
	if (g == NULL)
		return (GFARM_ERR_NO_MEMORY);
	g->groupname = groupname;

	entry = gfarm_hash_enter(group_hashtab,
	    &g->groupname, sizeof(g->groupname), sizeof(struct group *),
	    &created);
	if (entry == NULL) {
		free(g);
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		free(g);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	quota_data_init(&g->q);
	g->users.user_prev = g->users.user_next = &g->users;
	*(struct group **)gfarm_hash_entry_data(entry) = g;
	group_activate(g);
	if (gpp != NULL)
		*gpp = g;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
group_remove(const char *groupname)
{
	struct gfarm_hash_entry *entry;
	struct group *g;
	struct group_assignment *ga;

	entry = gfarm_hash_lookup(group_hashtab,
	    &groupname, sizeof(groupname));
	if (entry == NULL)
		return (GFARM_ERR_NO_SUCH_GROUP);
	g = *(struct group **)gfarm_hash_entry_data(entry);
	if (group_is_invalidated(g))
		return (GFARM_ERR_NO_SUCH_GROUP);
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

char *
group_name(struct group *g)
{
	return (group_is_active(g) ? g->groupname : REMOVED_GROUP_NAME);
}

struct quota *
group_quota(struct group *g)
{
	return (&g->q);
}

void
group_all(void *closure, void (*callback)(void *, struct group *),
	  int active_only)
{
	struct gfarm_hash_iterator it;
	struct group **g;

	for (gfarm_hash_iterator_begin(group_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		g = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (!active_only || group_is_active(*g))
			callback(closure, *g);
	}
}

/* The memory owner of `*gi' is changed to group.c */
void
group_add_one(void *closure, struct gfarm_group_info *gi)
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
		return;
	}
	for (i = 0; i < gi->nusers; i++) {
		u = user_lookup(gi->usernames[i]);
		if (u == NULL || user_is_invalidated(u)) {
			gflog_warning(GFARM_MSG_1000242,
			    "group_add_one: unknown user %s",
			    gi->usernames[i]);
			(void)group_remove(g->groupname);
			/* do not free gi->groupname */
			gi->groupname = NULL;
			gfarm_group_info_free(gi);
			return;
		}
		e = grpassign_add(u, g);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000243,
			    "group_add_one: grpassign(%s, %s): %s",
			    gi->usernames[i], g->groupname,
			    gfarm_error_string(e));
			(void)group_remove(g->groupname);
			/* do not free gi->groupname */
			gi->groupname = NULL;
			gfarm_group_info_free(gi);
			return;
		}
	}
	for (i = 0; i < gi->nusers; i++)
		free(gi->usernames[i]);
	if (gi->usernames != NULL)
		free(gi->usernames);
	/* do not free gi->groupname */
}

gfarm_error_t
group_add_user(struct group *g, const char *username)
{
	struct user *u = user_lookup(username);

	if (u == NULL || user_is_invalidated(u))
		return (GFARM_ERR_NO_SUCH_USER);
	if (g == NULL || group_is_invalidated(g))
		return (GFARM_ERR_NO_SUCH_GROUP);
	if (user_in_group(u, g))
		return (GFARM_ERR_ALREADY_EXISTS);
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
		if (user_is_active(ga->u))
			n++;
	gi.nusers = n;
	GFARM_MALLOC_ARRAY(gi.usernames, n);
	n = 0;
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next)
		if (user_is_active(ga->u))
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

	group_hashtab =
	    gfarm_hash_table_alloc(GROUP_HASHTAB_SIZE,
		hash_group, hash_key_equal_group);
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

		gi.groupname = strdup(ADMIN_GROUP_NAME);
		gi.nusers = gfarm_metadb_admin_user == NULL ? 1 : 2;
		GFARM_MALLOC_ARRAY(gi.usernames, gi.nusers);
		gi.usernames[0] = strdup(ADMIN_USER_NAME);
		if (gfarm_metadb_admin_user != NULL)
			gi.usernames[1] = strdup(gfarm_metadb_admin_user);

		/*
		 * We have to call this before group_add_one(),
		 * because group_add_one() frees the memory of gi
		 */
		e = db_group_add(&gi);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000250,
			    "failed to store group '%s' to storage: %s",
			    gi.groupname, gfarm_error_string(e));

		group_add_one(NULL, &gi);
		
	} else {
		group_add_user_and_record(admin, ADMIN_USER_NAME);
		if (gfarm_metadb_admin_user != NULL)
			group_add_user_and_record(admin,
			    gfarm_metadb_admin_user);
	}

	if ((admin = group_lookup(ROOT_GROUP_NAME)) == NULL) {
		gflog_info(GFARM_MSG_1000251,
		    "group %s not found, creating it",
		    ROOT_GROUP_NAME);

		gi.groupname = strdup(ROOT_GROUP_NAME);
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

		group_add_one(NULL, &gi);
		
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
		if (user_is_active(ga->u))
			n++;
	e = gfp_xdr_send(client, "si", g->groupname, n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next) {
		if (user_is_active(ga->u))
			if ((e = gfp_xdr_send(client, "s", user_name(ga->u)))
			    != GFARM_ERR_NO_ERROR)
				return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_group_info_get_all(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	struct gfarm_hash_iterator it;
	gfarm_int32_t ngroups;
	struct group **gp;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	/* XXX FIXME too long giant lock */
	giant_lock();

	ngroups = 0;
	for (gfarm_hash_iterator_begin(group_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		gp = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (group_is_active(*gp))
			++ngroups;
	}
	e = gfm_server_put_reply(peer, "group_info_get_all",
	    GFARM_ERR_NO_ERROR, "i", ngroups);
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		return (e);
	}
	for (gfarm_hash_iterator_begin(group_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		gp = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (group_is_active(*gp)) {
			e = group_info_send(client, *gp);
			if (e != GFARM_ERR_NO_ERROR) {
				giant_unlock();
				return (e);
			}
		}
	}
	giant_unlock();
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_group_info_get_by_names(struct peer *peer,
	int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	gfarm_int32_t ngroups;
	char *groupname, **groups;
	int i, j, eof, no_memory = 0;
	struct group *g;
	const char msg[] = "protocol GROUP_INFO_GET_BY_NAMES";

	e = gfm_server_get_request(peer, msg,
	    "i", &ngroups);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(groups, ngroups);
	if (groups == NULL)
		no_memory = 1;
	for (i = 0; i < ngroups; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &groupname);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_PROTOCOL;
			if (groups != NULL) {
				for (j = 0; j < i; j++) {
					if (groups[j] != NULL)
						free(groups[j]);
				}
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
		e = GFARM_ERR_NO_ERROR;
		goto free_group;
	}

	e = gfm_server_put_reply(peer, msg,
		no_memory ? GFARM_ERR_NO_MEMORY : e, "");
	if (no_memory || e != GFARM_ERR_NO_ERROR)
		goto free_group;

	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < ngroups; i++) {
		g = group_lookup(groups[i]);
		if (g == NULL || group_is_invalidated(g)) {
			e = gfm_server_put_reply(peer, msg,
			    GFARM_ERR_NO_SUCH_GROUP, "");
		} else {
			e = gfm_server_put_reply(peer, msg,
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = group_info_send(client, g);
		}
		if (peer_had_protocol_error(peer))
			break;
	}
	giant_unlock();

free_group:
	if (groups != NULL) {
		for (i = 0; i < ngroups; i++) {
			if (groups[i] != NULL)
				free(groups[i]);
		}
		free(groups);
	}
	return (no_memory ? GFARM_ERR_NO_MEMORY : e);
}

static gfarm_error_t
get_group(struct peer *peer, const char *diag, struct gfarm_group_info *gp)
{
	gfarm_error_t e;
	int i, eof;

	e = gfm_server_get_request(peer, diag, "si",
		&gp->groupname, &gp->nusers);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (gp->nusers <= 0) {
		gp->usernames = NULL;
	} else {
		GFARM_MALLOC_ARRAY(gp->usernames, gp->nusers);
		if (gp->usernames == NULL) {
			free(gp->groupname);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	for (i = 0; i < gp->nusers; ++i) {
		e = gfp_xdr_recv(peer_get_conn(peer), 0, &eof, "s",
			&gp->usernames[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			for (--i; i >= 0; --i)
				free(&gp->usernames[i]);
			free(gp->usernames);
			free(gp->groupname);
			return (e);
		}
	}
	return (e);
}

static gfarm_error_t
group_user_check(struct gfarm_group_info *gi, const char *msg)
{
	int i;
	struct user *u;

	for (i = 0; i < gi->nusers; i++) {
		u = user_lookup(gi->usernames[i]);
		if (u == NULL || user_is_invalidated(u)) {
			gflog_warning(GFARM_MSG_1000253,
			    "%s: unknown user %s", msg,
				    gi->usernames[i]);
			return (GFARM_ERR_NO_SUCH_USER);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_group_info_set(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct gfarm_group_info gi;
	struct user *user = peer_get_user(peer);
	int need_free;
	char *saved_groupname;
	const char msg[] = "protocol GROUP_INFO_SET";

	e = get_group(peer, msg, &gi);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		gfarm_group_info_free(&gi);
		return (GFARM_ERR_NO_ERROR);
	}
	need_free = 1;
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (group_is_active(group_lookup(gi.groupname))) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if ((e = group_user_check(&gi, msg)) != GFARM_ERR_NO_ERROR)
		;
	else {
		/*
		 * We have to call this before group_add_one(),
		 * because group_add_one() frees the memory of gi
		 */
		e = db_group_add(&gi);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000254,
			    "failed to store group '%s' to storage: %s",
			    gi.groupname, gfarm_error_string(e));

		saved_groupname = strdup(gi.groupname);
		group_add_one(NULL, &gi);
		if (saved_groupname != NULL) {
			if (group_lookup(saved_groupname) == NULL)
				e = GFARM_ERR_INVALID_ARGUMENT;
			free(saved_groupname);
		}
		need_free = 0;
	}
	if (need_free)
		gfarm_group_info_free(&gi);
	giant_unlock();
	return (gfm_server_put_reply(peer, msg, e, ""));
}

gfarm_error_t
gfm_server_group_info_modify(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct gfarm_group_info gi;
	struct user *user = peer_get_user(peer);
	struct group *group;
	struct group_assignment *ga;
	int i;
	const char msg[] = "protocol GROUP_INFO_MODIFY";

	e = get_group(peer, msg, &gi);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		gfarm_group_info_free(&gi);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((group = group_lookup(gi.groupname)) == NULL ||
		   group_is_invalidated(group)) {
		e = GFARM_ERR_NO_SUCH_GROUP;
	} else if ((e = group_user_check(&gi, msg)) != GFARM_ERR_NO_ERROR)
		;
	else {
		/* free group_assignment */
		while ((ga = group->users.user_next) != &group->users)
			grpassign_remove(ga);

		for (i = 0; i < gi.nusers; i++) {
			struct user *u = user_lookup(gi.usernames[i]);

			if (u == NULL || user_is_invalidated(u)) {
				gflog_warning(GFARM_MSG_1000255,
				    "%s: unknown user %s", msg,
				    gi.usernames[i]);
				continue;
			}
			e = grpassign_add(u, group);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1000256,
				    "%s: grpassign(%s, %s): %s", msg,
				    gi.usernames[i], gi.groupname,
				    gfarm_error_string(e));
				break; /* XXX - no memory */
			}
		}

		/* change all entries */
		e = db_group_modify(&gi, 0, 0, NULL, 0, NULL);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000257,
			    "failed to modify group '%s' in db: %s",
			    gi.groupname, gfarm_error_string(e));
	}
	gfarm_group_info_free(&gi);
	giant_unlock();
	return (gfm_server_put_reply(peer, msg, e, ""));
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
gfm_server_group_info_remove(struct peer *peer, int from_client, int skip)
{
	char *groupname;
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	const char msg[] = "protocol GROUP_INFO_REMOVE";

	e = gfm_server_get_request(peer, msg, "s", &groupname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(groupname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else
		e = group_info_remove(groupname, msg);
	free(groupname);
	giant_unlock();
	return (gfm_server_put_reply(peer, msg, e, ""));
}

gfarm_error_t
gfm_server_group_info_add_users(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000259,
	    "group_info_add_users: not implemented");

	e = gfm_server_put_reply(peer, "group_info_add_users",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_info_remove_users(struct peer *peer,
	int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000260,
	    "group_info_remove_users: not implemented");

	e = gfm_server_put_reply(peer, "group_info_remove_users",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_names_get_by_users(struct peer *peer,
	int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000261,
	    "group_names_get_by_users: not implemented");

	e = gfm_server_put_reply(peer, "group_names_get_by_users",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#endif /* TEST */
