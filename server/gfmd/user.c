#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h> /* fd_set for "filetab.h" */

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/user_info.h>

#include "hash.h"
#include "auth.h"
#include "gfp_xdr.h"

#include "subr.h"
#include "user.h"
#include "group.h"
#include "peer.h"

#define USER_HASHTAB_SIZE	3079	/* prime number */

/* in-core gfarm_user_info */
struct user {
	struct gfarm_user_info ui;
	struct group_assignment groups;
};

char ADMIN_USER_NAME[] = "gfadmin";
char REMOVED_USER_NAME[] = "gfarm-removed-user";

static struct gfarm_hash_table *user_hashtab = NULL;

int
hash_user(const void *key, int keylen)
{
	const unsigned char *const *usernamep = key;
	const unsigned char *k = *usernamep;

	return (gfarm_hash_default(k, strlen(k)));
}

int
hash_key_equal_user(
	const void *key1, int key1len,
	const void *key2, int key2len)
{
	const unsigned char *const *u1 = key1, *const *u2 = key2;
	const unsigned char *k1 = *u1, *k2 = *u2;
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

gfarm_error_t
user_init(void)
{
	user_hashtab =
	    gfarm_hash_table_alloc(USER_HASHTAB_SIZE,
		hash_user, hash_key_equal_user);
	if (user_hashtab == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (GFARM_ERR_NO_ERROR);
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
	struct user *u = malloc(sizeof(*u));

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

	/* mark this as removed */
	u->ui.username = REMOVED_USER_NAME;
	u->ui.realname = NULL;
	u->ui.homedir = NULL;
	u->ui.gsi_dn = NULL;
	/* XXX We should have a list which points all removed users */
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
user_is_admin(struct user *user)
{
	static struct user *admin = NULL;

	if (admin == NULL)
		admin = user_lookup(ADMIN_USER_NAME);
	return (user == admin);
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

/*
 * I/O
 */

static FILE *user_fp;

gfarm_error_t
user_info_open_for_seq_read(void)
{
	user_fp = fopen("user", "r");
	if (user_fp == NULL)
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
user_info_open_for_seq_write(void)
{
	user_fp = fopen("user", "w");
	if (user_fp == NULL)
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

/* This function needs to allocate strings */
gfarm_error_t
user_info_read_next(struct gfarm_user_info *ui)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
user_info_write_next(struct gfarm_user_info *ui)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
user_info_close_for_seq_read(void)
{
	fclose(user_fp);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
user_info_close_for_seq_write(void)
{
	fclose(user_fp);
	return (GFARM_ERR_NO_ERROR);
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
gfm_server_user_info_get_all(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	struct gfarm_hash_iterator it;
	gfarm_int32_t nusers;
	struct user *u;

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
		e = user_info_send(client, &u->ui);
		if (e != GFARM_ERR_NO_ERROR) {
			giant_unlock();
			return (e);
		}
	}

	giant_unlock();
	return (NULL);
}

gfarm_error_t
gfm_server_user_info_get_by_names(struct peer *peer, int from_client)
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
	if (!from_client) {
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
		users = NULL;
	} else {
		users = malloc(sizeof(*users) * nusers);
		if (users == NULL)
			error = GFARM_ERR_NO_MEMORY;
	}
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
	e = gfm_server_put_reply(peer, "user_info_get_by_names", error, "");
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
			e = gfm_server_put_reply(peer,
			    "USER_INFO_GET_BY_NAMES/no-user",
			    GFARM_ERR_NO_SUCH_OBJECT, "");
		} else {
			e = gfm_server_put_reply(peer,
			    "USER_INFO_GET_BY_NAMES/send-reply",
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = user_info_send(client, &u->ui);
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	for (i = 0; i < nusers; i++)
		free(users[i]);
	free(users);
	giant_unlock();
	return (e);
}

gfarm_error_t
gfm_server_user_info_set(struct peer *peer, int from_client)
{
	struct gfarm_user_info ui;
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);

	e = gfm_server_get_request(peer, "USER_INFO_SET",
	    "ssss", &ui.username, &ui.realname, &ui.homedir, &ui.gsi_dn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (user_lookup(ui.username) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else {
		e = user_enter(&ui, NULL);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		free(ui.username);
		free(ui.realname);
		free(ui.homedir);
		free(ui.gsi_dn);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, "USER_INFO_SET", e, ""));
}

gfarm_error_t
gfm_server_user_info_modify(struct peer *peer, int from_client)
{
	struct gfarm_user_info ui;
	struct user *u, *user = peer_get_user(peer);
	gfarm_error_t e;

	e = gfm_server_get_request(peer, "USER_INFO_MODIFY",
	    "ssss", &ui.username, &ui.realname, &ui.homedir, &ui.gsi_dn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		free(ui.username);
		free(ui.realname);
		free(ui.homedir);
		free(ui.gsi_dn);
	} else if ((u = user_lookup(ui.username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		free(ui.username);
		free(ui.realname);
		free(ui.homedir);
		free(ui.gsi_dn);
	} else {
		free(u->ui.realname);
		free(u->ui.homedir);
		free(u->ui.gsi_dn);
		u->ui.realname = ui.realname;
		u->ui.homedir = ui.homedir;
		u->ui.gsi_dn = ui.gsi_dn;
		free(ui.username);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, "USER_INFO_MODIFY", e, ""));
}

gfarm_error_t
gfm_server_user_info_remove(struct peer *peer, int from_client)
{
	char *username;
	gfarm_int32_t e;
	struct user *user = peer_get_user(peer);

	e = gfm_server_get_request(peer, "USER_INFO_REMOVE",
	    "s", &username);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		e = user_remove(username);
	}
	free(username);
	giant_unlock();
	return (gfm_server_put_reply(peer, "USER_INFO_REMOVE", e, ""));
}
#endif /* TEST */
