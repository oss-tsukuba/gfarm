#include <pthread.h>	/* db_access.h currently needs this */
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

#include "context.h"
#include "auth.h"
#include "gfp_xdr.h"
#include "gfm_proto.h"	/* GFARM_LOGIN_NAME_MAX, etc */

#include "inode.h"
#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "user.h"
#include "group.h"
#include "peer.h"
#include "quota.h"
#include "mdhost.h"
#include "relay.h"


#define USER_HASHTAB_SIZE	3079	/* prime number */
#define USER_DN_HASHTAB_SIZE	3079	/* prime number */

/* in-core gfarm_user_info */
struct user {
	struct gfarm_user_info ui;
	struct group_assignment groups;
	struct quota q;
	int invalid;	/* set when deleted */
};

char ADMIN_USER_NAME[] = "gfarmadm";
char REMOVED_USER_NAME[] = "gfarm-removed-user";

static struct gfarm_hash_table *user_hashtab = NULL;
static struct gfarm_hash_table *user_dn_hashtab = NULL;

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

static void
user_invalidate(struct user *u)
{
	u->invalid = 1;
}

static void
user_validate(struct user *u)
{
	u->invalid = 0;
}

int
user_is_invalid(struct user *u)
{
	return (u->invalid != 0);
}

int
user_is_valid(struct user *u)
{
	return (u->invalid == 0);
}

struct user *
user_lookup_including_invalid(const char *username)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(user_hashtab, &username, sizeof(username));
	if (entry == NULL)
		return (NULL);
	return (*(struct user **)gfarm_hash_entry_data(entry));
}

static int
user_is_null_str(const char *s)
{
	return (s == NULL || *s == '\0');
}

static struct user *
user_lookup_gsi_dn_including_invalid(const char *gsi_dn)
{
	struct gfarm_hash_entry *entry;

	if (user_is_null_str(gsi_dn))
		return (NULL);

	entry = gfarm_hash_lookup(user_dn_hashtab, &gsi_dn, sizeof(gsi_dn));
	if (entry == NULL)
		return (NULL);
	return (*(struct user **)gfarm_hash_entry_data(entry));
}

struct user *
user_lookup(const char *username)
{
	struct user *u = user_lookup_including_invalid(username);

	if (u != NULL && user_is_valid(u))
		return (u);
	return (NULL);
}

struct user *
user_lookup_gsi_dn(const char *gsi_dn)
{
	struct user *u = user_lookup_gsi_dn_including_invalid(gsi_dn);

	if (u != NULL && user_is_valid(u))
		return (u);
	return (NULL);
}

static gfarm_error_t
user_enter_gsi_dn(const char *gsi_dn, struct user *u)
{
	struct gfarm_hash_entry *entry;
	int created;

	if (user_is_null_str(gsi_dn))
		return (GFARM_ERR_NO_ERROR);

	entry = gfarm_hash_enter(user_dn_hashtab,
	    &gsi_dn, sizeof(gsi_dn), sizeof(struct user *), &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (!created)
		return (GFARM_ERR_ALREADY_EXISTS);
	*(struct user **)gfarm_hash_entry_data(entry) = u;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
user_enter(struct gfarm_user_info *ui, struct user **upp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct user *u;
	gfarm_error_t e;

	u = user_lookup_including_invalid(ui->username);
	if (u != NULL) {
		if (user_is_invalid(u)) {
			e = user_enter_gsi_dn(ui->gsi_dn, u);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			user_validate(u);
			if (upp != NULL)
				*upp = u;
			/* copy user info but keeping address of username */
			free(ui->username);
			ui->username = u->ui.username;
			u->ui.username = NULL; /* prevent to free this area */
			gfarm_user_info_free(&u->ui);
			u->ui = *ui;
			return (GFARM_ERR_NO_ERROR);
		} else {
			gflog_debug(GFARM_MSG_1001492,
				"User already exists");
			return (GFARM_ERR_ALREADY_EXISTS);
		}
	}

	GFARM_MALLOC(u);
	if (u == NULL) {
		gflog_debug(GFARM_MSG_1001493,
			"allocation of 'user' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	u->ui = *ui;

	entry = gfarm_hash_enter(user_hashtab,
	    &u->ui.username, sizeof(u->ui.username), sizeof(struct user *),
	    &created);
	if (entry == NULL) {
		free(u);
		gflog_debug(GFARM_MSG_1001494,
			"gfarm_hash_enter() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		free(u);
		gflog_debug(GFARM_MSG_1001495,
			"Entry already exists");
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	e = user_enter_gsi_dn(u->ui.gsi_dn, u);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_hash_purge(user_hashtab,
		    &u->ui.username, sizeof(u->ui.username));
		free(u);
		return (e);
	}

	quota_data_init(&u->q);
	u->groups.group_prev = u->groups.group_next = &u->groups;
	*(struct user **)gfarm_hash_entry_data(entry) = u;
	user_validate(u);
	if (upp != NULL)
		*upp = u;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
user_remove_internal(const char *username, int update_quota)
{
	struct gfarm_hash_entry *entry;
	struct user *u;
	struct group_assignment *ga;

	entry = gfarm_hash_lookup(user_hashtab, &username, sizeof(username));
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001496,
			"gfarm_hash_lookup() failed: %s", username);
		return (GFARM_ERR_NO_SUCH_USER);
	}
	u = *(struct user **)gfarm_hash_entry_data(entry);
	if (user_is_invalid(u)) {
		gflog_debug(GFARM_MSG_1001497,
			"user is invalid");
		return (GFARM_ERR_NO_SUCH_USER);
	}

	if (!user_is_null_str(u->ui.gsi_dn))
		gfarm_hash_purge(user_dn_hashtab,
		    &u->ui.gsi_dn, sizeof(u->ui.gsi_dn));
	if (update_quota)
		quota_user_remove(u);

	/* free group assignment */
	while ((ga = u->groups.group_next) != &u->groups)
		grpassign_remove(ga);
	/*
	 * do not purge the hash entry.  Instead, invalidate it so
	 * that it can be activated later.
	 */
	user_invalidate(u);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
user_remove(const char *username)
{
	return (user_remove_internal(username, 1));
}

gfarm_error_t
user_remove_in_cache(const char *username)
{
	return (user_remove_internal(username, 0));
}

struct user *
user_lookup_or_enter_invalid(const char *username)
{
	gfarm_error_t e;
	struct user *u = user_lookup_including_invalid(username);
	struct gfarm_user_info ui;
	static const char diag[] = "user_lookup_or_enter_invalid";

	if (u != NULL)
		return (u);

	ui.username = strdup_ck(username, diag);
	ui.realname = strdup_ck("", diag);
	ui.homedir = strdup_ck("", diag);
	ui.gsi_dn = strdup_ck("", diag);
	if (ui.username == NULL || ui.realname == NULL ||
	    ui.homedir == NULL || ui.gsi_dn == NULL) {
		gflog_error(GFARM_MSG_1002751,
		    "user_lookup_or_enter_invalid(%s): no memory", username);
		return (NULL);
	}
	e = user_enter(&ui, &u);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002752,
		    "user_lookup_or_enter_invalid(%s): user_enter: %s",
		    username, gfarm_error_string(e));
		gfarm_user_info_free(&ui);
		return (NULL);
	}
	e = user_remove_in_cache(username);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002753,
		    "user_lookup_or_enter_invalid(%s): user_remove: %s",
		    username, gfarm_error_string(e));
	}
	return (u);
}

char *
user_name(struct user *u)
{
	return (u != NULL && user_is_valid(u) ?
	    u->ui.username : REMOVED_USER_NAME);
}

char *
user_realname(struct user *u)
{
	return (u != NULL && user_is_valid(u) ?
	    u->ui.realname : REMOVED_USER_NAME);
}

char *
user_gsi_dn(struct user *u)
{
	return (u != NULL && user_is_valid(u) ?
	    u->ui.gsi_dn : REMOVED_USER_NAME);
}

struct quota *
user_quota(struct user *u)
{
	return (&u->q);
}

void
user_all(void *closure, void (*callback)(void *, struct user *),
	 int valid_only)
{
	struct gfarm_hash_iterator it;
	struct user **u;

	for (gfarm_hash_iterator_begin(user_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		u = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (!valid_only || user_is_valid(*u))
			callback(closure, *u);
	}
}

int
user_in_group(struct user *user, struct group *group)
{
	struct group_assignment *ga;

	if (user == NULL || group == NULL) /* either is already removed */
		return (0);

	if (user_is_invalid(user))
		return (0);
	if (group_is_invalid(group))
		return (0);

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

#define is_nl_cr(c)  ((c == '\n' || c == '\r' || c == '\0') ? 1 : 0)

static gfarm_error_t
list_to_names(void **value_p, size_t size,
	      char ***names_p, size_t *names_num_p)
{
	char *value, *priv, *now, *end;
	char **names;
	size_t i = 0;

	value = *value_p;
	if (!is_nl_cr(value[size - 1])) { /* allocation for last '\0' */
		char *tmp;
		GFARM_MALLOC_ARRAY(tmp, size + 1);
		if (tmp == NULL) {
			gflog_warning(GFARM_MSG_1002754,
				      "allocation of tmp failed");
			return (GFARM_ERR_NO_MEMORY);
		}
		memcpy(tmp, value, size);
		tmp[size] = '\0';
		free(value);
		value = tmp;
		*value_p = tmp;
		size++;
	}
	end = &value[size];
	*names_num_p = 0;

	/* count */
	now = value;
	priv = NULL;
	while (now != end) {
		if (!is_nl_cr(*now) && (priv == NULL || is_nl_cr(*priv)))
			(*names_num_p)++;
		priv = now;
		now++;
	}
	GFARM_MALLOC_ARRAY(names, *names_num_p);
	if (names == NULL) {
		gflog_warning(GFARM_MSG_1002755,
			      "allocation of names failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	now = value;
	priv = NULL;
	while (now != end) {
		if (!is_nl_cr(*now)) {
			if ((priv == NULL || is_nl_cr(*priv)))
				names[i++] = now;
		} else
			*now = '\0';
		priv = now;
		now++;
	}
	*names_p = names;
	return (GFARM_ERR_NO_ERROR);
}

static int
user_in_user_list(struct inode *inode, struct user *user)
{
	gfarm_error_t e;
	void *value;
	size_t size, names_num, i;
	char **names;

	e = inode_xattr_get_cache(inode, 0, GFARM_ROOT_EA_USER, &value, &size);
	if (e == GFARM_ERR_NO_SUCH_OBJECT || value == NULL)
		return (0);
	else if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1002756,
			      "inode_xattr_get_cache(%s) failed: %s",
			      GFARM_ROOT_EA_USER, gfarm_error_string(e));
		return (0);
	}
	e = list_to_names(&value, size, &names, &names_num);
	if (e != GFARM_ERR_NO_ERROR) {
		free(value);
		return (0);
	}
	for (i = 0; i < names_num; i++) {
		if (user == user_lookup(names[i])) {
			free(names);
			free(value);
			return (1);
		}
	}
	free(names);
	free(value);
	return (0);
}

static int
user_in_group_list(struct inode *inode, struct user *user)
{
	gfarm_error_t e;
	void *value;
	size_t size, names_num, i;
	char **names;

	e = inode_xattr_get_cache(inode, 0, GFARM_ROOT_EA_GROUP,
				  &value, &size);
	if (e == GFARM_ERR_NO_SUCH_OBJECT || value == NULL)
		return (0);
	else if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1002757,
			      "inode_xattr_get_cache(%s) failed: %s",
			      GFARM_ROOT_EA_GROUP, gfarm_error_string(e));
		return (0);
	}
	e = list_to_names(&value, size, &names, &names_num);
	if (e != GFARM_ERR_NO_ERROR) {
		free(value);
		return (0);
	}
	for (i = 0; i < names_num; i++) {
		if (user_in_group(user, group_lookup(names[i]))) {
			free(names);
			free(value);
			return (1);
		}
	}
	free(names);
	free(value);
	return (0);
}

int
user_is_root(struct inode *inode, struct user *user)
{
	static struct group *root = NULL;

	if (root == NULL)
		root = group_lookup(ROOT_GROUP_NAME);
	if (user_in_group(user, root))
		return (1);
	else if (user_in_user_list(inode, user))
		return (1);
	return (user_in_group_list(inode, user));
}

/* The memory owner of `*ui' is changed to user.c */
void
user_add_one(void *closure, struct gfarm_user_info *ui)
{
	gfarm_error_t e = user_enter(ui, NULL);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000233,
		    "user_add_one: %s", gfarm_error_string(e));
}

static void
create_user(const char *username, const char *gsi_dn)
{
	gfarm_error_t e;
	struct gfarm_user_info ui;
	static const char diag[] = "create_user";

	gflog_info(GFARM_MSG_1000234,
	    "user '%s' not found, creating...", username);

	ui.username = strdup_ck(username, diag);
	ui.realname = strdup_ck("Gfarm administrator", diag);
	ui.homedir = strdup_ck("/", diag);
	ui.gsi_dn = strdup_ck(gsi_dn == NULL ? "" : gsi_dn, diag);
	user_add_one(NULL, &ui);
	e = db_user_add(&ui);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000235,
		    "failed to store user '%s' to storage: %s",
		    username, gfarm_error_string(e));
}

void
user_init(void)
{
	gfarm_error_t e;

	user_hashtab =
	    gfarm_hash_table_alloc(USER_HASHTAB_SIZE,
		gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	user_dn_hashtab =
	    gfarm_hash_table_alloc(USER_DN_HASHTAB_SIZE,
		gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (user_hashtab == NULL || user_dn_hashtab == NULL)
		gflog_fatal(GFARM_MSG_1000236, "no memory for user hashtab");

	e = db_user_load(NULL, user_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000237,
		    "loading users: %s", gfarm_error_string(e));

	/*
	 * there is no removed (invalid) user since the hash is
	 * just created.
	 */
	if (user_lookup(ADMIN_USER_NAME) == NULL)
		create_user(ADMIN_USER_NAME, NULL);
	if (gfarm_ctxp->metadb_admin_user != NULL &&
	    user_lookup(gfarm_ctxp->metadb_admin_user) == NULL)
		create_user(gfarm_ctxp->metadb_admin_user,
		    gfarm_ctxp->metadb_admin_user_gsi_dn);
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
gfm_server_user_info_get_all(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct peer *mhpeer;
	gfarm_error_t e;
	int size_pos;
	struct gfp_xdr *client = peer_get_conn(peer);
	struct gfarm_hash_iterator it;
	gfarm_int32_t nusers;
	struct user **u;
	static const char diag[] = "GFM_PROTO_USER_INFO_GET_ALL";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = wait_db_update_info(peer, DBUPDATE_USER, diag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e));
		return (e);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();

	nusers = 0;
	for (gfarm_hash_iterator_begin(user_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		u = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (user_is_valid(*u))
			++nusers;
	}

	e = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    GFARM_ERR_NO_ERROR, "i", nusers);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001498,
			"gfm_server_put_reply() failed: %s",
			gfarm_error_string(e));
		giant_unlock();
		return (e);
	}
	for (gfarm_hash_iterator_begin(user_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		u = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (user_is_valid(*u)) {
			/* XXXRELAY FIXME */
			e = user_info_send(client, &(*u)->ui);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001499,
					"user_info_send() failed: %s",
					gfarm_error_string(e));
				giant_unlock();
				gfm_server_put_reply_end(peer, mhpeer, diag,
				    size_pos);
				return (e);
			}
		}
	}

	giant_unlock();
	gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * We need to allow gfsd use this operation
 * to implement gfarm_metadb_verify_username()
 */
gfarm_error_t
gfm_server_user_info_get_by_names(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct peer *mhpeer;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	int size_pos;
	gfarm_int32_t nusers;
	char *user, **users;
	int i, j, eof, no_memory = 0;
	struct user *u;
	static const char diag[] = "GFM_PROTO_USER_INFO_GET_BY_NAMES";

	e = gfm_server_get_request(peer, sizep, diag, "i", &nusers);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC_ARRAY(users, nusers);
	if (users == NULL) {
		no_memory = 1;
		/* Continue processing. */
	}

	for (i = 0; i < nusers; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &user);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			gflog_debug(GFARM_MSG_1003456,
			    "%s: gfp_xdr_recv(): %s",
			    diag, gfarm_error_string(e));
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_PROTOCOL;
			if (users != NULL) {
				for (j = 0; j < i; j++)
					free(users[j]);
				free(users);
			}
			return (e);
		}
		if (users == NULL) {
			free(user);
		} else {
			if (user == NULL)
				no_memory = 1;
			users[i] = user;
		}
	}
	if (skip) {
		e = GFARM_ERR_NO_ERROR; /* ignore GFARM_ERR_NO_MEMORY */
		goto free_users;
	}

	if (no_memory) {
		e = GFARM_ERR_NO_MEMORY;
	} else if ((e = wait_db_update_info(peer, DBUPDATE_USER, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e));
	}

	e = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    e, "");
	/* if network error doesn't happen, `e' holds RPC result here */
	if (e != GFARM_ERR_NO_ERROR)
		goto free_users;

	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < nusers; i++) {
		u = user_lookup(users[i]);
		if (u == NULL) {
			gflog_debug(GFARM_MSG_1003457,
			    "%s: user lookup <%s>: failed", diag, users[i]);
			e = gfp_xdr_send(client, "i", GFARM_ERR_NO_SUCH_USER);
		} else {
			gflog_debug(GFARM_MSG_1003458,
			    "%s: user lookup <%s>: ok", diag, users[i]);
			e = gfp_xdr_send(client, "i", GFARM_ERR_NO_ERROR);
			if (e == GFARM_ERR_NO_ERROR)
				e = user_info_send(client, &u->ui);
		}
		if (peer_had_protocol_error(peer))
			break;
	}
	/*
	 * if (!peer_had_protocol_error(peer))
	 *	the variable `e' holds last user's reply code
	 */
	giant_unlock();
	gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);

free_users:
	if (users != NULL) {
		for (i = 0; i < nusers; i++)
			free(users[i]);
		free(users);
	}
	return (e);
}

gfarm_error_t
gfm_server_user_info_get_by_gsi_dn(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e, e2;
	char *gsi_dn;
	struct user *u;
	struct gfarm_user_info *ui;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_USER_INFO_GET_BY_GSI_DN";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_USER_INFO_GET_BY_GSI_DN, "s", &gsi_dn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(gsi_dn);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay == NULL) {
		/* XXX FIXME too long giant lock */
		giant_lock();
		u = user_lookup_gsi_dn(gsi_dn);
		if (u == NULL) {
			e2 = GFARM_ERR_NO_SUCH_USER;
			e = gfm_server_relay_put_reply(peer, xid, sizep,
			    relay, diag, &e2, "");
		} else {
			ui = &u->ui;
			e2 = e;
			e = gfm_server_relay_put_reply(peer, xid, sizep,
			    relay, diag, &e2, "ssss", &ui->username,
			    &ui->realname, &ui->homedir, &ui->gsi_dn);
		}
		giant_unlock();
	}
	free(gsi_dn);
	return (e);
}

gfarm_error_t
user_info_verify(struct gfarm_user_info *ui, const char *diag)
{
	if (strlen(ui->username) > GFARM_LOGIN_NAME_MAX ||
	    strlen(ui->realname) > GFARM_USER_REALNAME_MAX ||
	    strlen(ui->homedir) > GFARM_PATH_MAX ||
	    strlen(ui->gsi_dn) > GFARM_USER_GSI_DN_MAX) {
		gflog_debug(GFARM_MSG_1002418,
		    "%s: invalid user info(%s, %s, %s, %s): argument too long",
		    diag, ui->username, ui->realname, ui->homedir, ui->gsi_dn);
		return (GFARM_ERR_INVALID_ARGUMENT);
	} else {
		return (GFARM_ERR_NO_ERROR);
	}
}

gfarm_error_t
gfm_server_user_info_set(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct gfarm_user_info ui;
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	int do_not_free = 0;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_USER_INFO_SET";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_USER_INFO_SET,
	    "ssss", &ui.username, &ui.realname, &ui.homedir, &ui.gsi_dn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		gfarm_user_info_free(&ui);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		gfarm_user_info_free(&ui);
	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (!from_client || user == NULL || !user_is_admin(user)) {
			gflog_debug(GFARM_MSG_1001505,
			    "Operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (user_lookup(ui.username) != NULL) {
			e = GFARM_ERR_ALREADY_EXISTS;
			gflog_debug(GFARM_MSG_1001506,
			    "User already exists");
		} else if ((e = user_info_verify(&ui, diag)) !=
		    GFARM_ERR_NO_ERROR) {
			/* nothing to do */
		} else {
			e = user_enter(&ui, NULL);
			if (e == GFARM_ERR_NO_ERROR) {
				e = db_user_add(&ui);
				if (e != GFARM_ERR_NO_ERROR) {
					gflog_debug(GFARM_MSG_1001507,
					    "db_user_add(): %s",
					    gfarm_error_string(e));
					user_remove(ui.username);
				/* do not free since ui still used in hash */
					do_not_free = 1;
				}
			}
		}
		if (e != GFARM_ERR_NO_ERROR && !do_not_free)
			gfarm_user_info_free(&ui);
		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

static int
user_strcmp(const char *s1, const char *s2)
{
	if (s1 == NULL && s2 == NULL)
		return (0);
	if (s1 == NULL)
		return (-1);
	if (s2 == NULL)
		return (1);
	return (strcmp(s1, s2));
}

gfarm_error_t
user_modify(struct user *u, struct gfarm_user_info *ui)
{
	gfarm_error_t e;

	if (user_strcmp(u->ui.gsi_dn, ui->gsi_dn) == 0) {
		/*
		 * u->ui.gsi_dn shouldn't be touched in this case,
		 * because it's pointed by user_dn_hashtab.
		 */
		free(ui->gsi_dn);
	} else { /* update the GSI DN hash table */
		if (!user_is_null_str(ui->gsi_dn)) {
			e = user_enter_gsi_dn(ui->gsi_dn, u);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1003459,
				    "update gsi_dn hash table: %s",
				    gfarm_error_string(e));
				return (e);
			}
		}
		if (!user_is_null_str(u->ui.gsi_dn))
			gfarm_hash_purge(user_dn_hashtab,
			    &u->ui.gsi_dn, sizeof(u->ui.gsi_dn));

		free(u->ui.gsi_dn);
		u->ui.gsi_dn = ui->gsi_dn;
	}
	ui->gsi_dn = NULL;

	free(u->ui.realname);
	u->ui.realname = ui->realname;
	ui->realname = NULL;

	free(u->ui.homedir);
	u->ui.homedir = ui->homedir;
	ui->homedir = NULL;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_user_info_modify(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct gfarm_user_info ui;
	struct user *u, *user = peer_get_user(peer);
	gfarm_error_t e;
	int already_free = 0;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_USER_INFO_MODIFY";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_USER_INFO_MODIFY,
	    "ssss", &ui.username, &ui.realname, &ui.homedir, &ui.gsi_dn);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		gfarm_user_info_free(&ui);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		gfarm_user_info_free(&ui);
	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (!from_client || user == NULL || !user_is_admin(user)) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1003460, "%s: %s", diag,
			    gfarm_error_string(e));
		} else if ((u = user_lookup(ui.username)) == NULL) {
			e = GFARM_ERR_NO_SUCH_USER;
			gflog_debug(GFARM_MSG_1003461,
			    "%s: user_lookup: %s", diag, gfarm_error_string(e));
		} else if ((e = user_info_verify(&ui, diag)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003462,
			    "%s: user_info_verify: %s", diag,
			    gfarm_error_string(e));
		} else if ((e = user_modify(u, &ui)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003463,
			    "%s: user_modify: %s", diag, gfarm_error_string(e));
		} else {
			free(ui.username);
			e = db_user_modify(&u->ui,
			    DB_USER_MOD_REALNAME|DB_USER_MOD_HOMEDIR|
			    DB_USER_MOD_GSI_DN);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1003464,
				    "%s: db_user_modify: %s", diag,
				    gfarm_error_string(e));
				/* XXX - need to revert the change in memory? */
			}
			already_free = 1;
		}
		giant_unlock();
		if (!already_free)
			gfarm_user_info_free(&ui);
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
user_info_remove_default(const char *username, const char *diag)
{
	gfarm_int32_t e, e2;

	if ((e = user_remove(username)) == GFARM_ERR_NO_ERROR) {
		e2 = db_user_remove(username);
		if (e2 != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000240,
			    "%s: db_user_remove: %s",
			    diag, gfarm_error_string(e2));
	}
	return (e);
}

gfarm_error_t (*user_info_remove)(const char *, const char *) =
	user_info_remove_default;

gfarm_error_t
gfm_server_user_info_remove(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	char *username;
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_USER_INFO_REMOVE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_USER_INFO_REMOVE, "s", &username);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		free(username);
	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (!from_client || user == NULL || !user_is_admin(user)) {
			gflog_debug(GFARM_MSG_1001513,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else
			e = user_info_remove(username, diag);
		free(username);
		giant_unlock();
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));

}
#endif /* TEST */
