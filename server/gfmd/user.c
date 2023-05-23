#include <pthread.h>	/* db_access.h currently needs this */
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h> /* fd_set for "filetab.h" */

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "bool.h"

#include "quota_info.h"
#include "context.h"
#include "auth.h"
#include "gfp_xdr.h"
#include "gfm_proto.h"	/* GFARM_LOGIN_NAME_MAX, etc */

#include "inode.h"
#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "tenant.h"
#include "user.h"
#include "group.h"
#include "dirset.h"
#include "peer.h"
#include "quota.h"
#include "process.h"
#include "metadb_server.h"
#include "db_ops.h"

#define USER_HASHTAB_SIZE	3079	/* prime number */
#define USER_DN_HASHTAB_SIZE	3079	/* prime number */
#define USER_ID_MAP_SIZE	9239

/* in-core gfarm_user_info */
struct user {
	struct gfarm_user_info ui;
	struct group_assignment groups;
	struct quota quota;
	struct gfarm_quota_subject_info usage_tmp;
	struct dirsets *dirsets;
	int invalid;	/* set when deleted */

	int needs_chroot;
	struct tenant *tenant;
	char *name_in_tenant;

	char *auth_user_id[AUTH_USER_ID_TYPE_MAX];
};

static const char *const auth_user_id_type_map[AUTH_USER_ID_TYPE_MAX] = {
	GFARM_AUTH_USER_ID_TYPE_X509,
	GFARM_AUTH_USER_ID_TYPE_KERBEROS,
	GFARM_AUTH_USER_ID_TYPE_SASL
};

const char *
gfarm_auth_user_id_type_name(enum auth_user_id_type auth_user_id_type)
{
	if (auth_user_id_type < 0 || AUTH_USER_ID_TYPE_MAX <= auth_user_id_type)
		return (NULL);
	return (auth_user_id_type_map[auth_user_id_type]);
}

gfarm_error_t
gfarm_auth_user_id_type_from_name(char *name, enum auth_user_id_type *p)
{
	int i;

	for (i = 0; i < AUTH_USER_ID_TYPE_MAX; i++) {
		if (strcmp(name, auth_user_id_type_map[i]) == 0) {
			*p = (enum auth_user_id_type) i;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	/* used as a RPC return value */
	return (GFARM_ERR_INVALID_ARGUMENT);
}

/* used to access "/tenantes/${TENANT}", not registered in hashtabs */
struct user filesystem_superuser = {
	{ "<filesystem>", "<filesystem>", "/", "" },
	{ NULL,
	  NULL,
	  &filesystem_superuser.groups,
	  &filesystem_superuser.groups,
	  NULL,
	  NULL,
	},
	{ 0 },
	{ 0 },
	NULL,
	1
};

char ADMIN_USER_NAME[] = "gfarmadm";
char REMOVED_USER_NAME[] = "gfarm-removed-user";
char UNKNOWN_USER_NAME[] = "gfarm-unknown-user";

static struct gfarm_hash_table *user_hashtab = NULL;
static struct gfarm_hash_table *user_dn_hashtab = NULL;
static struct gfarm_hash_table *user_id_map = NULL;

struct user_auth_key {
	enum auth_user_id_type auth_id_type;
	const char *auth_user_id;
};

int
gfarm_hash_user_auth(const void *key, int keylen)
{
	const struct user_auth_key *ptr = key;
	const char *str = ptr->auth_user_id;
	int hash;

	hash = gfarm_hash_default(str, strlen(str));
	hash = gfarm_hash_add(hash,
		&ptr->auth_id_type, sizeof(ptr->auth_id_type));

	return (hash);
}

int
gfarm_hash_key_equal_user_auth(
	const void *key1, int key1len,
	const void *key2, int key2len)
{
	const struct user_auth_key *ptr1 = key1, *ptr2 = key2;
	const char *str1 = ptr1->auth_user_id;
	const char *str2 = ptr2->auth_user_id;

	return (ptr1->auth_id_type == ptr2->auth_id_type &&
		strcmp(str1, str2) == 0);
}

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
user_tenant_lookup_including_invalid(const char *username)
{
	char *delim = strchr(username, GFARM_TENANT_DELIMITER);
	const char *uname;
	char *tmp = NULL;
	struct gfarm_hash_entry *entry;

	if (delim == NULL || delim[1] != '\0') {
		uname = username;
	} else { /* treat "user+" as "user" */
		tmp = alloc_name_without_tenant(username,
		    "user_tenant_lookup_invalid");
		if (tmp == NULL)
			return (NULL);
		uname = tmp;
	}

	entry = gfarm_hash_lookup(user_hashtab, &uname, sizeof(uname));
	if (tmp != NULL) /* this check is redundant, but for speed */
		free(tmp);
	if (entry == NULL)
		return (NULL);
	return (*(struct user **)gfarm_hash_entry_data(entry));
}

static int
user_is_null_str(const char *s)
{
	return (s == NULL || *s == '\0');
}

struct user *
user_tenant_lookup(const char *username)
{
	struct user *u = user_tenant_lookup_including_invalid(username);

	if (u != NULL && user_is_valid(u))
		return (u);
	return (NULL);
}

struct user *
user_lookup_in_tenant_including_invalid(
	const char *username, struct tenant *tenant)
{
	struct gfarm_hash_table **table_ref;
	struct gfarm_hash_entry *entry;

	table_ref = tenant_user_hashtab_ref(tenant);
	if (*table_ref == NULL)
		return (NULL);

	entry = gfarm_hash_lookup(*table_ref, &username, sizeof(username));
	if (entry == NULL)
		return (NULL);
	return (*(struct user **)gfarm_hash_entry_data(entry));
}

struct user *
user_lookup_in_tenant(const char *username, struct tenant *tenant)
{
	struct user *u =
	    user_lookup_in_tenant_including_invalid(username, tenant);

	if (u != NULL && user_is_valid(u))
		return (u);
	return (NULL);
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
user_lookup_gsi_dn(const char *gsi_dn)
{
	struct user *u = user_lookup_gsi_dn_including_invalid(gsi_dn);

	if (u != NULL && user_is_valid(u))
		return (u);
	return (NULL);
}

struct user *
user_lookup_by_kerberos_principal(const char *auth_user_id)
{
	return (user_lookup_auth_id(AUTH_USER_ID_TYPE_KERBEROS,
				    auth_user_id));
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

static struct user *
user_lookup_auth_id_including_invalid(
	enum auth_user_id_type auth_user_id_type,
	const char *auth_user_id)
{
	struct gfarm_hash_entry *entry;

	if (user_is_null_str(auth_user_id))
		return (NULL);

	struct user_auth_key key = {
	  auth_user_id_type,
	  auth_user_id
	};

	entry = gfarm_hash_lookup(user_id_map, &key, sizeof(key));
	if (entry == NULL)
		return (NULL);
	return (*(struct user **)gfarm_hash_entry_data(entry));
}

struct user *
user_lookup_auth_id(
	enum auth_user_id_type auth_user_id_type,
	const char *auth_user_id)
{
	struct user *u = user_lookup_auth_id_including_invalid(
			auth_user_id_type, auth_user_id);

	if (u != NULL && user_is_valid(u))
		return (u);
	return (NULL);
}

static gfarm_error_t
user_enter_auth_id(
	enum auth_user_id_type auth_user_id_type,
	char *auth_user_id, struct user *u)
{
	struct gfarm_hash_entry *entry;
	int created;

	if (user_is_null_str(auth_user_id))
		return (GFARM_ERR_NO_ERROR);

	struct user_auth_key key = {
	  auth_user_id_type,
	  auth_user_id
	};

	entry = gfarm_hash_enter(user_id_map,
	    &key, sizeof(key), sizeof(struct user *), &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (!created)
		return (GFARM_ERR_ALREADY_EXISTS);
	*(struct user **)gfarm_hash_entry_data(entry) = u;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
user_auth_id_modify_internal(struct user *u,
		char *auth_id_type, char *auth_user_id,
		bool *need_to_update_dbp, bool *need_to_addp)
{
	enum auth_user_id_type auth_user_id_type = 0;
	char *new_auth_user_id = NULL;
	gfarm_error_t e;


	new_auth_user_id = strdup_log(auth_user_id,
				"user_auth_id_modify_internal");

	if (new_auth_user_id == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if (strlen(auth_user_id) > GFARM_AUTH_USER_ID_MAX) {
		gflog_debug(GFARM_MSG_UNFIXED,
		  "user_auth_id_modify, auth_user_id too long, %s",
		  auth_user_id);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if ((e = gfarm_auth_user_id_type_from_name(
		auth_id_type,
		&auth_user_id_type)) != GFARM_ERR_NO_ERROR) {
		free(new_auth_user_id);
		return (e);
	}

	if (u->auth_user_id[auth_user_id_type] != NULL &&
		strcmp(u->auth_user_id[auth_user_id_type], auth_user_id) == 0) {
		*need_to_update_dbp = false;
		free(new_auth_user_id);
		return (GFARM_ERR_NO_ERROR);
	}

	e = user_enter_auth_id(auth_user_id_type,
		       new_auth_user_id, u);

	if (e != GFARM_ERR_NO_ERROR) {
		free(new_auth_user_id);
		return (e);
	}

	if (u->auth_user_id[auth_user_id_type] != NULL) {
		struct user_auth_key key = {
			auth_user_id_type,
			u->auth_user_id[auth_user_id_type]
		};

		if (gfarm_hash_purge(user_id_map, &key, sizeof(key)) == 0) {
			gflog_fatal(GFARM_MSG_UNFIXED,
				"user %s: cannot purge auth_id_type %s",
				u->ui.username,
				auth_id_type);
		}
		*need_to_addp = false;
	} else {
		*need_to_addp = true;
	}

	free(u->auth_user_id[auth_user_id_type]);
	u->auth_user_id[auth_user_id_type] = new_auth_user_id;
	*need_to_update_dbp = true;

	return (e);
}

gfarm_error_t
user_auth_id_modify(struct user *user, char *auth_id_type,
	char *auth_user_id)
{
	gfarm_error_t e;
	bool need_to_update_db;
	bool need_to_add;

	e = user_auth_id_modify_internal(user,
		auth_id_type, auth_user_id,
		&need_to_update_db,
		&need_to_add);

	if (e == GFARM_ERR_INVALID_ARGUMENT) {
		gflog_fatal(GFARM_MSG_UNFIXED,
			"user %s: unknown auth_id_type %s, \
			slave gfmd is older than gfmd",
			user->ui.username,
			auth_id_type);
	} else if (e == GFARM_ERR_ALREADY_EXISTS) {
		gflog_fatal(GFARM_MSG_UNFIXED,
			"user %s: unexpected inconsistency, \
			adding duplicate auth_user_id_type %s, \
			auth_user_id %s",
			user->ui.username,
			auth_id_type,
			auth_user_id);
	} else if (e == GFARM_ERR_NO_MEMORY) {
		gflog_fatal(GFARM_MSG_UNFIXED,
			"user %s: no memory error, \
			modify auth_user_id_type %s, \
			auth_user_id %s",
			user->ui.username,
			auth_id_type,
			auth_user_id);
	} else if (e != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_UNFIXED,
			"user %s: unknown error , \
			modify auth_user_id_type %s, \
			auth_user_id %s",
			user->ui.username,
			auth_id_type,
			auth_user_id);
	}

	return (e);

}

static gfarm_error_t
user_auth_id_remove_internal(struct user *user,
	char *auth_id_type, bool *need_to_update_dbp)
{
	enum auth_user_id_type auth_user_id_type = 0;
	gfarm_error_t e;

	if ((e = gfarm_auth_user_id_type_from_name(
		auth_id_type,
		&auth_user_id_type)) != GFARM_ERR_NO_ERROR) {
		return (e);
	}

	if (user->auth_user_id[auth_user_id_type] == NULL) {
		*need_to_update_dbp = false;
		return (GFARM_ERR_NO_ERROR);
	} else {
		struct user_auth_key key = {
			auth_user_id_type,
			user->auth_user_id[auth_user_id_type]
		};

		if (gfarm_hash_purge(user_id_map, &key, sizeof(key)) == 0) {
			gflog_fatal(GFARM_MSG_UNFIXED,
				"user %s: cannot purge auth_id_type %s",
				user->ui.username,
				auth_id_type);
		}
		free(user->auth_user_id[auth_user_id_type]);
		user->auth_user_id[auth_user_id_type] = NULL;
	}

	*need_to_update_dbp = true;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
user_auth_id_remove(struct user *user, char *auth_id_type)
{
	gfarm_error_t e;
	bool need_to_update_db;

	e = user_auth_id_remove_internal(user,
		auth_id_type, &need_to_update_db);

	if (e == GFARM_ERR_INVALID_ARGUMENT) {
		gflog_fatal(GFARM_MSG_UNFIXED,
			"user %s: unknown auth_id_type %s,\
			slave gfmd is older than gfmd",
			user->ui.username,
			auth_id_type);
	} else if (e != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_UNFIXED,
			"user %s: unknown error,\
			modify auth_user_id_type %s",
			user->ui.username,
			auth_id_type);
	}

	return (e);
}

struct db_user_auth_arg;
void
user_auth_add_one(void *closure, struct db_user_auth_arg *p)
{
	gfarm_error_t e;
	struct user *u = user_tenant_lookup(p->username);

	if (u == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else {
		e = user_auth_id_modify(u,
			p->auth_id_type, p->auth_user_id);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_warning(GFARM_MSG_UNFIXED,
			"user_auth_add_one: %s", gfarm_error_string(e));
	}
}


/* memory owner of *ui will be moved, when this function succeeds */
gfarm_error_t
user_tenant_enter(struct gfarm_user_info *ui, struct user **upp)
{
	struct gfarm_hash_entry *entry, *tenant_entry;
	int created;
	struct user *u;
	char *tenant_name, *name_in_tenant;
	struct tenant *tenant;
	struct gfarm_hash_table **user_hashtab_ref_in_tenant;
	gfarm_error_t e;

	u = user_tenant_lookup_including_invalid(ui->username);
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

	tenant_name = strchr(ui->username, GFARM_TENANT_DELIMITER);
	if (tenant_name == NULL) {
		name_in_tenant = strdup_log(ui->username, "user_tenant_enter");
		if (name_in_tenant == NULL)
			return (GFARM_ERR_NO_MEMORY);
		tenant_name = ui->username + strlen(ui->username); /* == "" */
	} else {
		size_t len = tenant_name - ui->username;

		name_in_tenant = malloc(len + 1);
		if (name_in_tenant == NULL) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "user_tenant_enter(%s): no memory", ui->username);
			return (GFARM_ERR_NO_MEMORY);
		}
		memcpy(name_in_tenant, ui->username, len);
		name_in_tenant[len] = '\0';
		++tenant_name;
	}

	e = tenant_lookup_or_enter(tenant_name, &tenant);
	if (e != GFARM_ERR_NO_ERROR) {
		free(name_in_tenant);
		return (e);
	}

	GFARM_MALLOC(u);
	if (u == NULL) {
		gflog_debug(GFARM_MSG_1001493,
			"allocation of 'user' failed");
		free(name_in_tenant);
		return (GFARM_ERR_NO_MEMORY);
	}
	u->ui = *ui;

	entry = gfarm_hash_enter(user_hashtab,
	    &u->ui.username, sizeof(u->ui.username), sizeof(struct user *),
	    &created);
	if (entry == NULL) {
		free(name_in_tenant);
		free(u);
		gflog_debug(GFARM_MSG_1001494,
			"gfarm_hash_enter() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		free(name_in_tenant);
		free(u);
		gflog_debug(GFARM_MSG_1001495,
			"Entry already exists");
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	e = user_enter_gsi_dn(u->ui.gsi_dn, u);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_hash_purge(user_hashtab,
		    &u->ui.username, sizeof(u->ui.username));
		free(name_in_tenant);
		free(u);
		return (e);
	}

	assert(e == GFARM_ERR_NO_ERROR);
	user_hashtab_ref_in_tenant = tenant_user_hashtab_ref(tenant);
	if (*user_hashtab_ref_in_tenant == NULL) {
		*user_hashtab_ref_in_tenant =
		    gfarm_hash_table_alloc(USER_HASHTAB_SIZE,
			gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	}
	if (*user_hashtab_ref_in_tenant != NULL) {
		tenant_entry = gfarm_hash_enter(*user_hashtab_ref_in_tenant,
		    &name_in_tenant, sizeof(name_in_tenant),
		    sizeof(struct user **), &created);
		if (tenant_entry == NULL) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "no memory for user %s in tenant %s",
			    name_in_tenant, tenant_name);
			e = GFARM_ERR_NO_MEMORY;
		} else if (!created) {
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "user %s already exists in tenant %s, "
			    "possibly missing giant_lock",
			    name_in_tenant, tenant_name);
			/* never reaches here, but for defensive programming */
			e = GFARM_ERR_ALREADY_EXISTS;
		}
	}
	if (*user_hashtab_ref_in_tenant == NULL ||
	    e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "no meory for user_hashtab of user %s tenant %s",
		    name_in_tenant, tenant_name);
		if (!user_is_null_str(u->ui.gsi_dn)) {
			gfarm_hash_purge(user_dn_hashtab,
			    &u->ui.gsi_dn, sizeof(u->ui.gsi_dn));
		}
		gfarm_hash_purge(user_hashtab,
		    &u->ui.username, sizeof(u->ui.username));
		free(name_in_tenant);
		free(u);
		return (GFARM_ERR_NO_MEMORY);
	}

	quota_data_init(&u->quota);
	u->dirsets = NULL; /* delayed allocation.  see user_enter_dirset() */
	u->groups.group_prev = u->groups.group_next = &u->groups;
	*(struct user **)gfarm_hash_entry_data(entry) = u;
	user_validate(u);

	u->needs_chroot = tenant_name[0] != '\0';
	u->tenant = tenant;
	u->name_in_tenant = name_in_tenant;
	*(struct user **)gfarm_hash_entry_data(tenant_entry) = u;

	memset(u->auth_user_id, 0, sizeof u->auth_user_id);

	if (upp != NULL)
		*upp = u;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
user_enter_in_tenant(struct gfarm_user_info *ui, struct tenant *tenant,
	struct user **upp)
{
	gfarm_error_t e;
	struct gfarm_user_info ui_tmp;
	const char *tname;
	char *delim = strchr(ui->username, GFARM_TENANT_DELIMITER);
	char *user_tenant_name = NULL, *username_save = NULL;

	if (delim != NULL) /* do not allow '+' in username */
		return (GFARM_ERR_INVALID_ARGUMENT);

	ui_tmp = *ui;

	tname = tenant_name(tenant);
	if (tname[0] != '\0') {
		user_tenant_name = alloc_name_with_tenant(ui->username, tname,
		    "user_enter_in_tenant");
		if (user_tenant_name == NULL)
			return (GFARM_ERR_NO_MEMORY);
		username_save = ui->username;
		ui_tmp.username = user_tenant_name;
	}

	e = user_tenant_enter(&ui_tmp, upp);
	if (e != GFARM_ERR_NO_ERROR) {
		free(user_tenant_name);
		return (e);
	}

	free(username_save);
	ui->username = user_tenant_name;

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
user_remove_internal(const char *username, int update_quota)
{
	struct gfarm_hash_entry *entry;
	struct user *u;
	struct group_assignment *ga;
	enum auth_user_id_type auth_user_id_type;

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

	for (auth_user_id_type = 0;
		auth_user_id_type < AUTH_USER_ID_TYPE_MAX;
		auth_user_id_type++) {

		if (u->auth_user_id[auth_user_id_type] != NULL) {
			struct user_auth_key key = {
				auth_user_id_type,
				u->auth_user_id[auth_user_id_type]
			};

			gfarm_hash_purge(user_id_map, &key, sizeof(key));
			free(u->auth_user_id[auth_user_id_type]);
		}
	}

	if (update_quota)
		quota_user_remove(u);

	/* free group assignment */
	while ((ga = u->groups.group_next) != &u->groups)
		grpassign_remove(ga);

	/* NOTE: this user remains in struct inode, dirset and quota_dir */

	/*
	 * do not purge the hash entry.  Instead, invalidate it so
	 * that it can be activated later.
	 */
	user_invalidate(u);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
user_tenant_remove(const char *username)
{
	return (user_remove_internal(username, 1));
}

gfarm_error_t
user_tenant_remove_in_cache(const char *username)
{
	return (user_remove_internal(username, 0));
}

static gfarm_error_t
user_remove_in_tenant(const char *username, struct tenant *tenant)
{
	gfarm_error_t e;
	char *user_tenant_name = alloc_name_with_tenant(
	    username, tenant_name(tenant), "user_remove_in_tenant");

	if (user_tenant_name == NULL)
		return (GFARM_ERR_NO_ERROR);

	e = user_tenant_remove(user_tenant_name);
	free(user_tenant_name);
	return (e);
}

struct user *
user_tenant_lookup_or_enter_invalid(const char *username)
{
	gfarm_error_t e;
	struct user *u = user_tenant_lookup_including_invalid(username);
	struct gfarm_user_info ui;

	if (u != NULL)
		return (u);

	ui.username = strdup(username);
	ui.realname = strdup("");
	ui.homedir = strdup("");
	ui.gsi_dn = strdup("");
	if (ui.username == NULL || ui.realname == NULL ||
	    ui.homedir == NULL || ui.gsi_dn == NULL) {
		gflog_error(GFARM_MSG_1002751,
		    "user_tenant_lookup_or_enter_invalid(%s): no memory",
		    username);
		free(ui.username);
		free(ui.realname);
		free(ui.homedir);
		free(ui.gsi_dn);
		return (NULL);
	}
	e = user_tenant_enter(&ui, &u);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002752,
		    "user_tenant_lookup_or_enter_invalid(%s): user_enter: %s",
		    username, gfarm_error_string(e));
		gfarm_user_info_free(&ui);
		return (NULL);
	}
	e = user_tenant_remove_in_cache(username);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002753,
		    "user_tenant_lookup_or_enter_invalid(%s): user_remove: %s",
		    username, gfarm_error_string(e));
	}
	return (u);
}

char *
user_tenant_name_even_invalid(struct user *u)
{
	return (u != NULL ? u->ui.username : REMOVED_USER_NAME);
}

char *
user_tenant_name(struct user *u)
{
	return (u != NULL && user_is_valid(u) ?
	    u->ui.username : REMOVED_USER_NAME);
}

char *
user_name_in_tenant_even_invalid(struct user *u, struct process *p)
{
	if (u == NULL)
		return (REMOVED_USER_NAME);
	else if (u->tenant == process_get_tenant(p))
		return (u->name_in_tenant);
	else
		return (UNKNOWN_USER_NAME);
}

char *
user_name_in_tenant(struct user *u, struct process *p)
{
	if (u == NULL || !user_is_valid(u))
		return (REMOVED_USER_NAME);
	else
		return (user_name_in_tenant_even_invalid(u, p));
}

struct tenant *
user_get_tenant(struct user *u)
{
	return (u->tenant);
}

const char *
user_get_tenant_name(struct user *u)
{
	return (tenant_name(u->tenant));
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
	return (&u->quota);
}

struct gfarm_quota_subject_info *
user_usage_tmp(struct user *u)
{
	return (&u->usage_tmp);
}

gfarm_error_t
user_enter_dirset(struct user *u, const char *dirsetname, int limit_check,
	struct dirset **dirsetp)
{
	struct dirsets *sets = u->dirsets;

	/* most users don't define dirset, so we usually don't allocate it */
	if (sets == NULL) {
		sets = dirsets_new();

		if (sets == NULL) {
			gflog_debug(GFARM_MSG_1004633,
			    "allocation of 'dirsets' for user %s failed",
			    u->ui.username);
			return (GFARM_ERR_NO_MEMORY);
		}
		u->dirsets = sets;
	}
	return (dirset_enter(sets, dirsetname, u, limit_check, dirsetp));
}

struct dirset *
user_lookup_dirset(struct user *u, const char *dirsetname)
{
	/* most users don't define dirset, so we usually don't allocate it */
	if (u->dirsets == NULL)
		return (NULL);

	return (dirset_lookup(u->dirsets, dirsetname));
}

gfarm_error_t
user_remove_dirset(struct user *u, const char *dirsetname)
{
	struct dirsets *sets = u->dirsets;

	if (sets == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	return (dirset_remove(sets, dirsetname));
}

struct dirsets *
user_get_dirsets(struct user *u)
{
	return (u->dirsets);
}

static void
user_foreach_internal(struct gfarm_hash_table *hashtab,
	void *closure, void (*callback)(void *, struct user *), int flags)
{
	struct gfarm_hash_iterator it;
	struct user **u;
	int valid_only = (flags & USER_FOREARCH_FLAG_VALID_ONLY) != 0;

	for (gfarm_hash_iterator_begin(hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		u = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (!valid_only || user_is_valid(*u))
			callback(closure, *u);
	}
}

void
user_foreach_in_all_tenants(
	void *closure, void (*callback)(void *, struct user *),
	int flags)
{
	user_foreach_internal(user_hashtab, closure, callback, flags);
}

void
user_foreach_in_tenant(
	void *closure, void (*callback)(void *, struct user *),
	struct tenant *tenant, int flags)
{
	struct gfarm_hash_table **hashtab_ref =
		tenant_user_hashtab_ref(tenant);

	if (*hashtab_ref == NULL)
		return;
	user_foreach_internal(*hashtab_ref, closure, callback, flags);
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
user_is_super_admin(struct user *user)
{
	/* protected by giant lock */
	static struct group *admin = NULL;

	if (admin == NULL)
		admin = group_tenant_lookup(ADMIN_GROUP_NAME);
	return (user_in_group(user, admin));
}

int
user_is_tenant_admin(struct user *user, struct tenant *tenant)
{
	struct group *tenant_admin;

	if (user_is_super_admin(user))
		return (1);

	tenant_admin = group_lookup_in_tenant(ADMIN_GROUP_NAME, tenant);
	if (tenant_admin == NULL)
		return (0);
	return (user_in_group(user, tenant_admin));
}

int
user_is_super_root(struct user *user)
{
	/* protected by giant lock */
	static struct group *root = NULL;

	/* currently this condition never comes true, but for safety */
	if (user == &filesystem_superuser)
		return (1);

	if (root == NULL)
		root = group_tenant_lookup(ROOT_GROUP_NAME);
	return (user_in_group(user, root));
}

int
user_is_tenant_root(struct user *user, struct tenant *tenant)
{
	struct group *tenant_root;

	if (user_is_super_root(user))
		return (1);

	tenant_root = group_lookup_in_tenant(ROOT_GROUP_NAME, tenant);
	if (tenant_root == NULL)
		return (0);
	return (user_in_group(user, tenant_root));
}

int
user_needs_chroot(struct user *user)
{
	return (user->needs_chroot);
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
user_in_root_user_list(struct inode *inode, struct user *user)
{
	gfarm_error_t e;
	void *value;
	size_t size, names_num, i;
	char **names;
	struct tenant *tenant = user_get_tenant(user);

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
		if (user == user_lookup_in_tenant(names[i], tenant)) {
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
user_in_root_group_list(struct inode *inode, struct user *user)
{
	gfarm_error_t e;
	void *value;
	size_t size, names_num, i;
	char **names;
	struct tenant *tenant = user_get_tenant(user);

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
		if (user_in_group(user,
		    group_lookup_in_tenant(names[i], tenant))) {
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
user_is_root_for_inode(struct user *user, struct inode *inode)
{
	if (user_is_tenant_root(user, user_get_tenant(user)))
		return (1);
	else if (user_in_root_user_list(inode, user))
		return (1);
	return (user_in_root_group_list(inode, user));
}

/* The memory owner of `*ui' is changed to user.c */
void
user_add_one(void *closure, struct gfarm_user_info *ui)
{
	gfarm_error_t e = user_tenant_enter(ui, NULL);

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
	user_id_map =
	    gfarm_hash_table_alloc(USER_ID_MAP_SIZE,
		gfarm_hash_user_auth, gfarm_hash_key_equal_user_auth);
	if (user_hashtab == NULL || user_dn_hashtab == NULL ||
	    user_id_map == NULL)
		gflog_fatal(GFARM_MSG_1000236, "no memory for user hashtab");

	e = db_user_load(NULL, user_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000237,
		    "loading users: %s", gfarm_error_string(e));
	e = db_user_auth_load(NULL, user_auth_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "loading user_auth: %s", gfarm_error_string(e));
}

void
user_initial_entry(void)
{
	/*
	 * there is no removed (invalid) user since the hash is
	 * just created.
	 */
	if (user_tenant_lookup(ADMIN_USER_NAME) == NULL)
		create_user(ADMIN_USER_NAME, NULL);
	if (gfarm_ctxp->metadb_admin_user != NULL &&
	    user_tenant_lookup(gfarm_ctxp->metadb_admin_user) == NULL)
		create_user(gfarm_ctxp->metadb_admin_user,
		    gfarm_ctxp->metadb_admin_user_gsi_dn);
}

#ifndef TEST
/*
 * protocol handler
 */

gfarm_error_t
user_info_send(struct gfp_xdr *client, struct user *u, struct process *p,
	int name_with_tenant)
{
	struct gfarm_user_info *ui = &u->ui;

	return (gfp_xdr_send(client, "ssss",
	    name_with_tenant ? ui->username : user_name_in_tenant(u, p),
	    ui->realname, ui->homedir, ui->gsi_dn));
}

gfarm_error_t
gfm_server_user_info_get_all(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	struct gfarm_hash_table *hashtab;
	struct gfarm_hash_iterator it;
	gfarm_int32_t nusers;
	struct process *process;
	struct user **u;
	int name_with_tenant;
	static const char diag[] = "GFM_PROTO_USER_INFO_GET_ALL";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	/* XXX FIXME too long giant lock */
	giant_lock();

	e = rpc_name_with_tenant(peer, from_client,
	    &name_with_tenant, &process, diag);
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		e = gfm_server_put_reply(peer, diag, e, "");
		return (e);
	}

	if (name_with_tenant) {
		hashtab = user_hashtab;
	} else if ((hashtab = *tenant_user_hashtab_ref(
	    process_get_tenant(process))) == NULL) {
		e = gfm_server_put_reply(peer, diag,
		    GFARM_ERR_NO_ERROR, "i", 0);
		giant_unlock();
		return (e);
	}

	nusers = 0;
	for (gfarm_hash_iterator_begin(hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		u = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (user_is_valid(*u))
			++nusers;
	}
	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_NO_ERROR, "i", nusers);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001498,
			"gfm_server_put_reply() failed: %s",
			gfarm_error_string(e));
		giant_unlock();
		return (e);
	}
	for (gfarm_hash_iterator_begin(hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		u = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (user_is_valid(*u)) {
			e = user_info_send(client, *u, process,
			    name_with_tenant);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001499,
					"user_info_send() failed: %s",
					gfarm_error_string(e));
				giant_unlock();
				return (e);
			}
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
	gfarm_int32_t nusers;
	char *user, **users;
	int i, j, eof, name_with_tenant, no_memory = 0;
	struct user *u;
	struct process *process = NULL;
	struct tenant *tenant;
	static const char diag[] = "GFM_PROTO_USER_INFO_GET_BY_NAMES";

	e = gfm_server_get_request(peer, diag, "i", &nusers);
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

	/* XXX FIXME too long giant lock */
	giant_lock();

	if (no_memory) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else if ((e = rpc_name_with_tenant(peer, from_client,
	    &name_with_tenant, &process, diag)) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else
		e = GFARM_ERR_NO_ERROR;

	e = gfm_server_put_reply(peer, diag, e, "");
	/* if network error doesn't happen, `e' holds RPC result here */
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		goto free_users;
	}

	tenant = name_with_tenant ? NULL : process_get_tenant(process);

	for (i = 0; i < nusers; i++) {
		u = name_with_tenant ?
		    user_tenant_lookup(users[i]) :
		    user_lookup_in_tenant(users[i], tenant);
		if (u == NULL) {
			gflog_debug(GFARM_MSG_1003457,
			    "%s: user lookup <%s>: failed", diag, users[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_NO_SUCH_USER, "");
		} else {
			gflog_debug(GFARM_MSG_1003458,
			    "%s: user lookup <%s>: ok", diag, users[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = user_info_send(client, u, process,
				    name_with_tenant);
		}
		if (peer_had_protocol_error(peer))
			break;
	}
	/*
	 * if (!peer_had_protocol_error(peer))
	 *	the variable `e' holds last user's reply code
	 */
	giant_unlock();

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
	struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *gsi_dn;
	struct user *u;
	struct process *process;
	struct gfarm_user_info *ui;
	int name_with_tenant;
	static const char diag[] = "GFM_PROTO_USER_INFO_GET_BY_GSI_DN";

	e = gfm_server_get_request(peer, diag, "s", &gsi_dn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001503,
		    "%s request: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(gsi_dn);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();

	if ((e = rpc_name_with_tenant(peer, from_client,
	    &name_with_tenant, &process, diag)) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((u = user_lookup_gsi_dn(gsi_dn)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else
		e = GFARM_ERR_NO_ERROR;

	if (e != GFARM_ERR_NO_ERROR) {
		e = gfm_server_put_reply(peer, diag, e, "");
	} else {
		ui = &u->ui;
		/* peer_get_user(peer) is not NULL if from_client */
		e = gfm_server_put_reply(peer, diag, e, "ssss",
		    name_with_tenant ?
		    ui->username : user_name_in_tenant(u, process),
		    ui->realname, ui->homedir, ui->gsi_dn);
	}
	giant_unlock();
	free(gsi_dn);
	return (e);
}

gfarm_error_t
gfm_server_user_info_get_my_own(
	struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	struct gfarm_user_info *ui;
	static const char diag[] = "GFM_PROTO_USER_INFO_GET_MY_OWN";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	/* XXX FIXME too long giant lock */
	giant_lock();
	if (!from_client) {
		gflog_info(GFARM_MSG_UNFIXED, "%s: not from client (%s)",
		    diag, user == NULL ? "(null)" : user_tenant_name(user));
		e = gfm_server_put_reply(peer, diag,
		    GFARM_ERR_OPERATION_NOT_PERMITTED, "");
	} else if (user == NULL) {
		/* shouldn't happen */
		gflog_error(GFARM_MSG_UNFIXED, "%s: user is NULL", diag);
		e = gfm_server_put_reply(peer, diag,
			GFARM_ERR_INTERNAL_ERROR, "");
	} else {
		ui = &user->ui;
		e = gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR,
			"ssss", ui->username, ui->realname, ui->homedir,
			ui->gsi_dn);
	}
	giant_unlock();
	return (e);
}

gfarm_error_t
user_info_verify(struct gfarm_user_info *ui, const char *diag)
{
	if (strlen(ui->username) > GFARM_LOGIN_NAME_MAX ||
	    strlen(ui->realname) > GFARM_USER_REALNAME_MAX ||
	    strlen(ui->homedir) > GFARM_PATH_MAX ||
	    strlen(ui->gsi_dn) > GFARM_AUTH_USER_ID_MAX) {
		gflog_debug(GFARM_MSG_1002418,
		    "%s: invalid user info(%s, %s, %s, %s): argument too long",
		    diag, ui->username, ui->realname, ui->homedir, ui->gsi_dn);
		return (GFARM_ERR_INVALID_ARGUMENT);
	} else {
		return (GFARM_ERR_NO_ERROR);
	}
}

gfarm_error_t
gfm_server_user_info_set(struct peer *peer, int from_client, int skip)
{
	struct gfarm_user_info ui;
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	struct process *process;
	struct tenant *tenant;
	int is_super_admin, do_not_free = 0;

	static const char diag[] = "GFM_PROTO_USER_INFO_SET";

	e = gfm_server_get_request(peer, diag,
	    "ssss", &ui.username, &ui.realname, &ui.homedir, &ui.gsi_dn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001504,
			"USER_INFO_SET request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		gfarm_user_info_free(&ui);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client || user == NULL) {
		gflog_debug(GFARM_MSG_1001505,
			"Operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): no process",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
	} else if ((tenant = process_get_tenant(process)) == NULL) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): no tenant: %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else if (!user_is_tenant_admin(user, tenant)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else if (!(is_super_admin = user_is_super_admin(user)) &&
	    strchr(ui.username, GFARM_TENANT_DELIMITER) != NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s (%s@%s) '%s': '+' is not allowed as user name",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    ui.username);
	} else if ((is_super_admin ?
	    user_tenant_lookup(ui.username) :
	    user_lookup_in_tenant(ui.username, tenant))
	    != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
		gflog_debug(GFARM_MSG_1001506,
			"User already exists");
	} else if ((e = user_info_verify(&ui, diag)) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if (gfarm_read_only_mode()) {
		gflog_debug(GFARM_MSG_1005142, "%s (%s@%s) during read_only",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
	} else {
		struct user *new_user;

		if (is_super_admin)
			e = user_tenant_enter(&ui, &new_user);
		else
			e = user_enter_in_tenant(&ui, tenant, &new_user);
		if (e == GFARM_ERR_NO_ERROR) {
			e = db_user_add(&new_user->ui);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001507,
					"db_user_add(): %s",
					gfarm_error_string(e));
				user_tenant_remove(new_user->ui.username);
				/* do not free since ui still used in hash */
				do_not_free = 1;
			}
		}
	}
	if (e != GFARM_ERR_NO_ERROR && !do_not_free)
		gfarm_user_info_free(&ui);
	giant_unlock();

	return (gfm_server_put_reply(peer, diag, e, ""));
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
gfm_server_user_info_modify(struct peer *peer, int from_client, int skip)
{
	struct gfarm_user_info ui;
	struct user *u, *user = peer_get_user(peer);
	struct process *process;
	struct tenant *tenant;
	gfarm_error_t e;
	int already_free = 0;
	static const char diag[] = "GFM_PROTO_USER_INFO_MODIFY";

	e = gfm_server_get_request(peer, diag,
	    "ssss", &ui.username, &ui.realname, &ui.homedir, &ui.gsi_dn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001508,
			"USER_INFO_MODIFY request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		gfarm_user_info_free(&ui);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client || user == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1003460, "%s: %s", diag,
		    gfarm_error_string(e));
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): no process",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
	} else if ((tenant = process_get_tenant(process)) == NULL) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): no tenant: %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else if (!user_is_tenant_admin(user, tenant)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else if ((u = user_is_super_admin(user) ?
	    user_tenant_lookup(ui.username) :
	    user_lookup_in_tenant(ui.username, tenant))
	    == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s) %s: %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    ui.username, gfarm_error_string(e));
	} else if ((e = user_info_verify(&ui, diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003462,
		    "%s: user_info_verify: %s", diag, gfarm_error_string(e));
	} else if (gfarm_read_only_mode()) {
		gflog_debug(GFARM_MSG_1005143, "%s (%s@%s) during read_only",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
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
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_user_info_remove(struct peer *peer, int from_client, int skip)
{
	char *username;
	gfarm_int32_t e, e2;
	struct user *user = peer_get_user(peer);
	struct process *process;
	struct tenant *tenant;
	int is_super_admin;
	static const char diag[] = "GFM_PROTO_USER_INFO_REMOVE";

	e = gfm_server_get_request(peer, diag,
	    "s", &username);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001512,
			"USER_INFO_REMOVE request failed:%s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(username);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client || user == NULL) {
		gflog_debug(GFARM_MSG_1001513,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): no process",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
	} else if ((tenant = process_get_tenant(process)) == NULL) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): no tenant: %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else if (!(is_super_admin = user_is_super_admin(user)) &&
	    strchr(username, GFARM_TENANT_DELIMITER) != NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s (%s@%s) '%s': '+' is not allowed as user name",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    username);
	} else if (gfarm_read_only_mode()) {
		gflog_debug(GFARM_MSG_1005144, "%s (%s@%s) during read_only",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
	} else {
		if (is_super_admin)
			e = user_tenant_remove(username);
		else
			e = user_remove_in_tenant(username, tenant);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s (%s@%s) '%s': %s", diag,
			    peer_get_username(peer), peer_get_hostname(peer),
			    username, gfarm_error_string(e));
		} else {
			char *name;

			if (is_super_admin || !tenant_needs_chroot(tenant)) {
				e2 = db_user_remove(username);
			} else if ((name = alloc_name_with_tenant(username,
			    tenant_name(tenant), diag)) == NULL) {
				e2 = GFARM_ERR_NO_MEMORY;
			} else {
				e2 = db_user_remove(name);
				free(name);
			}
			if (e2 != GFARM_ERR_NO_ERROR)
				gflog_error(GFARM_MSG_1000240,
				    "%s: db_user_remove: %s",
				    diag, gfarm_error_string(e2));
		}
	}
	free(username);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_user_info_get_by_auth_id(
	struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *auth_user_id_type_str;
	char *auth_user_id;
	enum auth_user_id_type auth_user_id_type;
	struct user *u;
	struct process *process;
	struct gfarm_user_info *ui;
	int name_with_tenant;
	static const char diag[] = "GFM_PROTO_USER_INFO_GET_BY_AUTH_ID";

	e = gfm_server_get_request(peer, diag, "ss",
				   &auth_user_id_type_str, &auth_user_id);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s request: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(auth_user_id_type_str);
		free(auth_user_id);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();

	e = gfarm_auth_user_id_type_from_name(auth_user_id_type_str,
					  &auth_user_id_type);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "invalid auth_user_id_type");
	} else if ((e = rpc_name_with_tenant(peer, from_client,
	    &name_with_tenant, &process, diag)) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((u = user_lookup_auth_id(auth_user_id_type,
					    auth_user_id)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
	} else
		e = GFARM_ERR_NO_ERROR;

	if (e != GFARM_ERR_NO_ERROR) {
		e = gfm_server_put_reply(peer, diag, e, "");
	} else {
		ui = &u->ui;
		/* peer_get_user(peer) is not NULL if from_client */
		e = gfm_server_put_reply(peer, diag, e, "ssss",
		    name_with_tenant ?
		    ui->username : user_name_in_tenant(u, process),
		    ui->realname, ui->homedir, ui->gsi_dn);
	}
	giant_unlock();
	free(auth_user_id_type_str);
	free(auth_user_id);
	return (e);
}

gfarm_error_t
gfm_server_user_auth_get(
	struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *username;
	char *auth_user_id_type_str;
	enum auth_user_id_type auth_user_id_type;
	struct user *u, *user = peer_get_user(peer);
	struct process *process;
	struct tenant *tenant = NULL;
	static const char diag[] = "GFM_PROTO_USER_AUTH_GET";

	e = gfm_server_get_request(peer, diag, "ss",
				   &username, &auth_user_id_type_str);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s request: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(username);
		free(auth_user_id_type_str);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();

	e = gfarm_auth_user_id_type_from_name(auth_user_id_type_str,
					  &auth_user_id_type);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "invalid auth_user_id_type");
	} else if (!from_client || user == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s", diag,
			gfarm_error_string(e));
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): no process",
			diag, peer_get_username(peer), peer_get_hostname(peer));
	} else if ((tenant = process_get_tenant(process)) == NULL) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): no tenant: %s",
			diag, peer_get_username(peer), peer_get_hostname(peer),
			gfarm_error_string(e));
	} else if (!user_is_tenant_admin(user, tenant)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): %s",
			diag, peer_get_username(peer), peer_get_hostname(peer),
			gfarm_error_string(e));
	} else if ((u = user_is_super_admin(user) ?
			user_tenant_lookup(username) :
			user_lookup_in_tenant(username, tenant))
			== NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s) %s: %s",
			diag, peer_get_username(peer), peer_get_hostname(peer),
			username, gfarm_error_string(e));
	} else if (gfarm_read_only_mode()) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s) during read_only",
			diag, peer_get_username(peer), peer_get_hostname(peer));
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
	} else
		e = GFARM_ERR_NO_ERROR;

	if (e != GFARM_ERR_NO_ERROR) {
		e = gfm_server_put_reply(peer, diag, e, "");
	} else {
		/* peer_get_user(peer) is not NULL if from_client */
		e = gfm_server_put_reply(peer, diag, e, "s",
			u->auth_user_id[auth_user_id_type] != NULL ?
			u->auth_user_id[auth_user_id_type] : "");
	}
	giant_unlock();
	free(username);
	free(auth_user_id_type_str);
	return (e);
}

gfarm_error_t
gfm_server_user_auth_modify(struct peer *peer,
			    int from_client, int skip)
{
	gfarm_error_t e;
	char *username;
	char *auth_user_id_type_str;
	char *auth_user_id;
	enum auth_user_id_type auth_user_id_type;
	struct user *u, *user = peer_get_user(peer);
	struct process *process;
	struct tenant *tenant;
	static const char diag[] = "GFM_PROTO_USER_AUTH_MODIFY";

	e = gfm_server_get_request(peer, diag, "sss",
		   &username, &auth_user_id_type_str, &auth_user_id);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s request: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(username);
		free(auth_user_id_type_str);
		free(auth_user_id);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();

	e = gfarm_auth_user_id_type_from_name(auth_user_id_type_str,
					  &auth_user_id_type);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "invalid auth_user_id_type");
	} else if (!from_client || user == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s", diag,
			gfarm_error_string(e));
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): no process",
			diag, peer_get_username(peer), peer_get_hostname(peer));
	} else if ((tenant = process_get_tenant(process)) == NULL) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): no tenant: %s",
			diag, peer_get_username(peer), peer_get_hostname(peer),
			gfarm_error_string(e));
	} else if (!user_is_tenant_admin(user, tenant)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s): %s",
			diag, peer_get_username(peer), peer_get_hostname(peer),
			gfarm_error_string(e));
	} else if ((u = user_is_super_admin(user) ?
			user_tenant_lookup(username) :
			user_lookup_in_tenant(username, tenant))
			== NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s) %s: %s",
			diag, peer_get_username(peer), peer_get_hostname(peer),
			username, gfarm_error_string(e));
	} else if (gfarm_read_only_mode()) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s (%s@%s) during read_only",
			diag, peer_get_username(peer), peer_get_hostname(peer));
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
	} else
		e = GFARM_ERR_NO_ERROR;

	if (e == GFARM_ERR_NO_ERROR) {

		if (auth_user_id == NULL || strcmp(auth_user_id, "") == 0) {
			bool need_to_update_db = false;
			e = user_auth_id_remove_internal(u,
				auth_user_id_type_str, &need_to_update_db);

			if (e == GFARM_ERR_NO_ERROR &&
				need_to_update_db) {

				struct db_user_auth_remove_arg arg = {
					username,
					auth_user_id_type_str
				};
				e = db_user_auth_remove(&arg);

				if (e != GFARM_ERR_NO_ERROR) {
					gflog_error(GFARM_MSG_UNFIXED,
					 "user %s:\
					 remove auth_user_id db failed,\
					 auth_user_id_type %s",
					 user->ui.username,
					 auth_user_id_type_str);
				}
			}
		} else {
			bool need_to_update_db = false;
			bool need_to_add = false;
			e = user_auth_id_modify_internal(u,
				auth_user_id_type_str, auth_user_id,
				&need_to_update_db, &need_to_add);

			if (e == GFARM_ERR_NO_ERROR &&
				need_to_update_db) {

				struct db_user_auth_arg arg = {
					username,
					auth_user_id_type_str,
					auth_user_id
				};

				if (need_to_add) {
					e = db_user_auth_add(&arg);

					if (e != GFARM_ERR_NO_ERROR) {
						gflog_error(GFARM_MSG_UNFIXED,
						 "user %s:\
						 add auth_user_id db failed,\
						 auth_user_id_type %s\
						 auth_user_id %s",
						 user->ui.username,
						 auth_user_id_type_str,
						 auth_user_id);
					}
				} else {
					e = db_user_auth_modify(&arg);
					if (e != GFARM_ERR_NO_ERROR) {
						gflog_error(GFARM_MSG_UNFIXED,
						 "user %s:\
						 modify auth_user_id db failed,\
						 auth_user_id_type %s\
						 auth_user_id %s",
						 user->ui.username,
						 auth_user_id_type_str,
						 auth_user_id);
					}
				}
			}
		}
	}

	giant_unlock();

	free(username);
	free(auth_user_id_type_str);
	free(auth_user_id);

	return (gfm_server_put_reply(peer, diag, e, ""));
}

#endif /* TEST */
