#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h> /* fd_set for "filetab.h" */

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"

#include "config.h"	/* gfarm_metadb_admin_user */
#include "auth.h"
#include "gfp_xdr.h"

#include "subr.h"
#include "db_access.h"
#include "user.h"
#include "group.h"
#include "peer.h"

#define USER_HASHTAB_SIZE	3079	/* prime number */

/* in-core gfarm_user_info */
struct user {
	struct gfarm_user_info ui;
	struct group_assignment groups;
};

char ADMIN_USER_NAME[] = "gfarmadm";
char REMOVED_USER_NAME[] = "gfarm-removed-user";

static struct gfarm_hash_table *user_hashtab = NULL;

/* subroutine of grpassign_add(), shouldn't be called from elsewhere */
void
grpassign_add_group(struct group_assignment *ga)
{
	struct user *u = ga->u;

	ga->group_next = &u->groups;
	ga->group_prev = u->groups.group_prev;
	u->groups.group_prev->group_next = ga;
	u->groups.group_prev = ga;
}

int
hash_user(const void *key, int keylen)
{
	const char *const *usernamep = key;
	const char *k = *usernamep;

	return (gfarm_hash_default(k, strlen(k)));
}

int
hash_key_equal_user(
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

struct user *
user_lookup(const char *username)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(user_hashtab, &username, sizeof(username));
	if (entry == NULL)
		return (NULL);
	return (*(struct user **)gfarm_hash_entry_data(entry));
}

gfarm_error_t
user_enter(struct gfarm_user_info *ui, struct user **upp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct user *u;

	GFARM_MALLOC(u);
	if (u == NULL)
		return (GFARM_ERR_NO_MEMORY);
	u->ui = *ui;

	entry = gfarm_hash_enter(user_hashtab,
	    &u->ui.username, sizeof(u->ui.username), sizeof(struct user *),
	    &created);
	if (entry == NULL) {
		free(u);
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		free(u);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	u->groups.group_prev = u->groups.group_next = &u->groups;
	*(struct user **)gfarm_hash_entry_data(entry) = u;
	if (upp != NULL)
		*upp = u;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
user_remove(const char *username)
{
	struct gfarm_hash_entry *entry;
	struct user *u;
	struct group_assignment *ga;

	entry = gfarm_hash_lookup(user_hashtab, &username, sizeof(username));
	if (entry == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	u = *(struct user **)gfarm_hash_entry_data(entry);
	gfarm_hash_purge(user_hashtab, &username, sizeof(username));

	/* free gfarm_user_info */
	free(u->ui.username);
	free(u->ui.realname);
	free(u->ui.homedir);
	free(u->ui.gsi_dn);

	/* free group_assignment */
	while ((ga = u->groups.group_next) != &u->groups)
		grpassign_remove(ga);
#if 0
	/*
	 * As long as the primary key is a username, REMOVED_USER_NAME
	 * cannot be used.  This entry is not referred to any more.
	 */
	/* mark this as removed */
	u->ui.username = REMOVED_USER_NAME;
	u->ui.realname = NULL;
	u->ui.homedir = NULL;
	u->ui.gsi_dn = NULL;
	/* XXX We should have a list which points all removed users */
#else
	free(u);
#endif
	return (GFARM_ERR_NO_ERROR);
}

char *
user_name(struct user *u)
{
	return (u->ui.username);
}

int
user_is_removed(struct user *u)
{
	return (u->ui.username == REMOVED_USER_NAME);
}

int
user_in_group(struct user *user, struct group *group)
{
	struct group_assignment *ga;

	for (ga = user->groups.group_next; ga != &user->groups;
	    ga = ga->group_next) {
		if (ga->g == group)
			return (1);
	}
	return (0);
}

int
user_is_admin(struct user *user)
{
	static struct group *admin = NULL;

	if (admin == NULL)
		admin = group_lookup(ADMIN_GROUP_NAME);
	return (user_in_group(user, admin));
}

/* The memory owner of `*ui' is changed to user.c */
void
user_add_one(void *closure, struct gfarm_user_info *ui)
{
	gfarm_error_t e = user_enter(ui, NULL);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning("user_add_one: %s", gfarm_error_string(e));
}

void
create_user(const char *username)
{
	gfarm_error_t e;
	struct gfarm_user_info ui;

	gflog_info("user '%s' not found, creating...", username);

	ui.username = strdup(username);
	ui.realname = strdup("Gfarm administrator");
	ui.homedir = strdup("/");
	ui.gsi_dn = strdup("");
	user_add_one(NULL, &ui);
	e = db_user_add(&ui);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("failed to store user '%s' to storage: %s",
		    username, gfarm_error_string(e));
}

void
user_init(void)
{
	gfarm_error_t e;

	user_hashtab =
	    gfarm_hash_table_alloc(USER_HASHTAB_SIZE,
		hash_user, hash_key_equal_user);
	if (user_hashtab == NULL)
		gflog_fatal("no memory for user hashtab");

	e = db_user_load(NULL, user_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error("loading users: %s", gfarm_error_string(e));

	if (user_lookup(ADMIN_USER_NAME) == NULL)
		create_user(ADMIN_USER_NAME);
	if (gfarm_metadb_admin_user != NULL &&
	    user_lookup(gfarm_metadb_admin_user) == NULL)
		create_user(gfarm_metadb_admin_user);
}

#ifndef TEST
/*
 * protocol handler
 */

gfarm_error_t
user_info_send(struct gfp_xdr *client, struct gfarm_user_info *ui)
{
	return (gfp_xdr_send(client, "ssss",
	    ui->username, ui->realname, ui->homedir, ui->gsi_dn));
}

gfarm_error_t
gfm_server_user_info_get_all(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	struct gfarm_hash_iterator it;
	gfarm_int32_t nusers;
	struct user **u;

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	/* XXX FIXME too long giant lock */
	giant_lock();

	nusers = 0;
	for (gfarm_hash_iterator_begin(user_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		++nusers;
	}
	e = gfm_server_put_reply(peer, "user_info_get_all",
	    GFARM_ERR_NO_ERROR, "i", nusers);
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		return (e);
	}
	for (gfarm_hash_iterator_begin(user_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		u = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		e = user_info_send(client, &(*u)->ui);
		if (e != GFARM_ERR_NO_ERROR) {
			giant_unlock();
			return (e);
		}
	}

	giant_unlock();
	return (GFARM_ERR_NO_ERROR);
}

/*
 * We need to allow gfsd use this operation
 * to implement gfarm_metadb_verify_username()
 */
gfarm_error_t
gfm_server_user_info_get_by_names(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	gfarm_int32_t error = GFARM_ERR_NO_ERROR;
	gfarm_int32_t nusers;
	char *user, **users;
	int i, j, eof;
	struct user *u;

	e = gfm_server_get_request(peer, "USER_INFO_GET_BY_NAMES",
	    "i", &nusers);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(users, nusers);
	if (users == NULL)
		error = GFARM_ERR_NO_MEMORY;
	for (i = 0; i < nusers; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &user);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_PROTOCOL;
			if (users != NULL) {
				for (j = 0; j < i; j++) {
					if (users[j] != NULL)
					    free(users[j]);
				}
				free(users);
			}
			return (e);
		}
		if (users == NULL) {
			free(user);
		} else {
			if (user == NULL)
				error = GFARM_ERR_NO_MEMORY;
			users[i] = user;
		}
	}
	if (!skip) {
		e = gfm_server_put_reply(peer, "user_info_get_by_names", error,
		    "");
		if (error != GFARM_ERR_NO_ERROR || e != GFARM_ERR_NO_ERROR) {
			if (users != NULL) {
				for (i = 0; i < nusers; i++) {
					if (users[i] != NULL)
						free(users[i]);
				}
				free(users);
			}
			return (e);
		}
		giant_lock();
		/* XXX FIXME too long giant lock */
		for (i = 0; i < nusers; i++) {
			u = user_lookup(users[i]);
			if (u == NULL) {
				if (debug_mode)
					gflog_info("user lookup <%s>: failed",
					    users[i]);
				e = gfm_server_put_reply(peer,
				    "USER_INFO_GET_BY_NAMES/no-user",
				    GFARM_ERR_NO_SUCH_OBJECT, "");
			} else {
				if (debug_mode)
					gflog_info("user lookup <%s>: ok",
					    users[i]);
				e = gfm_server_put_reply(peer,
				    "USER_INFO_GET_BY_NAMES/send-reply",
				    GFARM_ERR_NO_ERROR, "");
				if (e == GFARM_ERR_NO_ERROR)
					e = user_info_send(client, &u->ui);
			}
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
		giant_unlock();
	}
	for (i = 0; i < nusers; i++)
		free(users[i]);
	free(users);
	return (e);
}

gfarm_error_t
gfm_server_user_info_set(struct peer *peer, int from_client, int skip)
{
	struct gfarm_user_info ui;
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);

	e = gfm_server_get_request(peer, "USER_INFO_SET",
	    "ssss", &ui.username, &ui.realname, &ui.homedir, &ui.gsi_dn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(ui.username);
		free(ui.realname);
		free(ui.homedir);
		free(ui.gsi_dn);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (user_lookup(ui.username) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else {
		e = user_enter(&ui, NULL);
		if (e == GFARM_ERR_NO_ERROR) {
			e = db_user_add(&ui);
			if (e != GFARM_ERR_NO_ERROR) {
				user_remove(ui.username);
				ui.username = ui.realname = ui.homedir =
				    ui.gsi_dn = NULL;
			}
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		if (ui.username != NULL)
			free(ui.username);
		if (ui.realname != NULL)
			free(ui.realname);
		if (ui.homedir != NULL)
			free(ui.homedir);
		if (ui.gsi_dn != NULL)
			free(ui.gsi_dn);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, "USER_INFO_SET", e, ""));
}

gfarm_error_t
gfm_server_user_info_modify(struct peer *peer, int from_client, int skip)
{
	struct gfarm_user_info ui;
	struct user *u, *user = peer_get_user(peer);
	gfarm_error_t e;
	int needs_free = 0;

	e = gfm_server_get_request(peer, "USER_INFO_MODIFY",
	    "ssss", &ui.username, &ui.realname, &ui.homedir, &ui.gsi_dn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(ui.username);
		free(ui.realname);
		free(ui.homedir);
		free(ui.gsi_dn);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		needs_free = 1;
	} else if ((u = user_lookup(ui.username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		needs_free = 1;
	} else if ((e = db_user_modify(&ui,
	    DB_USER_MOD_REALNAME|DB_USER_MOD_HOMEDIR|DB_USER_MOD_GSI_DN)) !=
	    GFARM_ERR_NO_ERROR) {
		needs_free = 1;
	} else {
		free(u->ui.realname);
		free(u->ui.homedir);
		free(u->ui.gsi_dn);
		u->ui.realname = ui.realname;
		u->ui.homedir = ui.homedir;
		u->ui.gsi_dn = ui.gsi_dn;
		free(ui.username);
	}
	if (needs_free) {
		free(ui.username);
		free(ui.realname);
		free(ui.homedir);
		free(ui.gsi_dn);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, "USER_INFO_MODIFY", e, ""));
}

gfarm_error_t
gfm_server_user_info_remove(struct peer *peer, int from_client, int skip)
{
	char *username;
	gfarm_int32_t e, e2;
	struct user *user = peer_get_user(peer);

	e = gfm_server_get_request(peer, "USER_INFO_REMOVE",
	    "s", &username);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = user_remove(username)) == GFARM_ERR_NO_ERROR) {
		e2 = db_user_remove(username);
		if (e2 != GFARM_ERR_NO_ERROR)
			gflog_error("protocol USER_INFO_REMOVE db: %s",
			    gfarm_error_string(e2));
	}
	free(username);
	giant_unlock();
	return (gfm_server_put_reply(peer, "USER_INFO_REMOVE", e, ""));
}
#endif /* TEST */
