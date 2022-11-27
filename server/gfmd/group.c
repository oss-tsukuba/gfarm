#include <pthread.h>	/* db_access.h currently needs this */
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"

#include "quota_info.h"
#include "context.h"
#include "gfp_xdr.h"
#include "auth.h"
#include "gfm_proto.h"

#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "tenant.h"
#include "user.h"
#include "group.h"
#include "peer.h"
#include "quota.h"
#include "process.h"

#define GROUP_HASHTAB_SIZE	3079	/* prime number */

struct group {
	char *groupname;
	struct group_assignment users;
	struct quota quota;
	struct gfarm_quota_subject_info usage_tmp;
	int invalid;	/* set when deleted */

	struct tenant *tenant;
	char *name_in_tenant;
};

char ADMIN_GROUP_NAME[] = "gfarmadm";
char ROOT_GROUP_NAME[] = "gfarmroot";
char REMOVED_GROUP_NAME[] = "gfarm-removed-group";
char UNKNOWN_GROUP_NAME[] = "gfarm-unknown-group";

static struct gfarm_hash_table *group_hashtab = NULL;

gfarm_error_t
grpassign_add(struct user *u, struct group *g)
{
	struct group_assignment *ga;

	GFARM_MALLOC(ga);
	if (ga == NULL) {
		gflog_error(GFARM_MSG_1001514,
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
group_tenant_lookup_including_invalid(const char *groupname)
{
	char *delim = strchr(groupname, GFARM_TENANT_DELIMITER);
	const char *gname;
	char *tmp = NULL;
	struct gfarm_hash_entry *entry;

	if (delim == NULL || delim[1] != '\0') {
		gname = groupname;
	} else { /* treat "group+" as "gruop" */
		tmp = alloc_name_without_tenant(groupname,
		    "group_tenant_lookup_invalid");
		if (tmp == NULL)
			return (NULL);
		gname = tmp;
	}

	entry = gfarm_hash_lookup(group_hashtab, &gname, sizeof(gname));
	if (tmp != NULL) /* this check is redundant, but for speed */
		free(tmp);
	if (entry == NULL)
		return (NULL);
	return (*(struct group **)gfarm_hash_entry_data(entry));
}

struct group *
group_tenant_lookup(const char *groupname)
{
	struct group *g = group_tenant_lookup_including_invalid(groupname);

	if (g != NULL && group_is_valid(g))
		return (g);
	return (NULL);
}

struct group *
group_lookup_in_tenant_including_invalid(
	const char *groupname, struct tenant *tenant)
{
	struct gfarm_hash_table **table_ref;
	struct gfarm_hash_entry *entry;

	table_ref = tenant_group_hashtab_ref(tenant);
	if (*table_ref == NULL)
		return (NULL);

	entry = gfarm_hash_lookup(*table_ref, &groupname, sizeof(groupname));
	if (entry == NULL)
		return (NULL);
	return (*(struct group **)gfarm_hash_entry_data(entry));
}

struct group *
group_lookup_in_tenant(const char *groupname, struct tenant *tenant)
{
	struct group *g =
	    group_lookup_in_tenant_including_invalid(groupname, tenant);

	if (g != NULL && group_is_valid(g))
		return (g);
	return (NULL);
}

/* note that groupname may be free'ed */
static gfarm_error_t
group_tenant_enter(char *groupname, struct group **gpp)
{
	struct gfarm_hash_entry *entry, *tenant_entry;
	int created;
	struct group *g;
	char *tenant_name, *name_in_tenant;
	struct tenant *tenant;
	struct gfarm_hash_table **group_hashtab_ref_in_tenant;
	gfarm_error_t e;

	g = group_tenant_lookup_including_invalid(groupname);
	if (g != NULL) {
		if (group_is_invalid(g)) {
			group_validate(g);
			if (gpp != NULL)
				*gpp = g;
			free(groupname);
			return (GFARM_ERR_NO_ERROR);
		} else {
			gflog_debug(GFARM_MSG_1001515,
			    "\"%s\" group already exists",
			    group_tenant_name(g));
			return (GFARM_ERR_ALREADY_EXISTS);
		}
	}

	tenant_name = strchr(groupname, GFARM_TENANT_DELIMITER);
	if (tenant_name == NULL) {
		name_in_tenant = strdup_log(groupname, "group_tenant_enter");
		if (name_in_tenant == NULL)
			return (GFARM_ERR_NO_MEMORY);
		tenant_name = groupname + strlen(groupname); /* == "" */
	} else {
		size_t len = tenant_name - groupname;

		name_in_tenant = malloc(len + 1);
		if (name_in_tenant == NULL) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "user_tenant_enter(%s): no memory", groupname);
			return (GFARM_ERR_NO_MEMORY);
		}
		memcpy(name_in_tenant, groupname, len);
		name_in_tenant[len] = '\0';
		++tenant_name;
	}

	e = tenant_lookup_or_enter(tenant_name, &tenant);
	if (e != GFARM_ERR_NO_ERROR) {
		free(name_in_tenant);
		return (e);
	}

	GFARM_MALLOC(g);
	if (g == NULL) {
		gflog_error(GFARM_MSG_1001516,
		    "memory allocation of group failed");
		free(name_in_tenant);
		return (GFARM_ERR_NO_MEMORY);
	}
	g->groupname = groupname;

	entry = gfarm_hash_enter(group_hashtab,
	    &g->groupname, sizeof(g->groupname), sizeof(struct group *),
	    &created);
	if (entry == NULL) {
		free(name_in_tenant);
		free(g);
		gflog_error(GFARM_MSG_1001517,
		    "gfarm_hash_enter() failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_debug(GFARM_MSG_1001518,
		    "\"%s\" group already exists", group_tenant_name(g));
		free(name_in_tenant);
		free(g);
		return (GFARM_ERR_ALREADY_EXISTS);
	}

	assert(e == GFARM_ERR_NO_ERROR);
	group_hashtab_ref_in_tenant = tenant_group_hashtab_ref(tenant);
	if (*group_hashtab_ref_in_tenant == NULL) {
		*group_hashtab_ref_in_tenant =
		    gfarm_hash_table_alloc(GROUP_HASHTAB_SIZE,
			gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	}
	if (*group_hashtab_ref_in_tenant != NULL) {
		tenant_entry = gfarm_hash_enter(*group_hashtab_ref_in_tenant,
		    &name_in_tenant, sizeof(name_in_tenant),
		    sizeof(struct group **), &created);
		if (tenant_entry == NULL) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "no memory for group %s in tenant %s",
			    name_in_tenant, tenant_name);
			e = GFARM_ERR_NO_MEMORY;
		} else if (!created) {
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "group %s already exists in tenant %s, "
			    "possibly missing giant_lock",
			    name_in_tenant, tenant_name);
			/* never reaches here, but for defensive programming */
			e = GFARM_ERR_ALREADY_EXISTS;
		}
	}
	if (*group_hashtab_ref_in_tenant == NULL ||
	    e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "no meory for group_hashtab of group %s tenant %s",
		    name_in_tenant, tenant_name);
		gfarm_hash_purge(group_hashtab, &groupname, sizeof(groupname));
		free(name_in_tenant);
		free(g);
		return (GFARM_ERR_NO_MEMORY);
	}

	quota_data_init(&g->quota);
	g->users.user_prev = g->users.user_next = &g->users;
	*(struct group **)gfarm_hash_entry_data(entry) = g;
	group_validate(g);

	g->tenant = tenant;
	g->name_in_tenant = name_in_tenant;
	*(struct group **)gfarm_hash_entry_data(tenant_entry) = g;

	if (gpp != NULL)
		*gpp = g;
	return (GFARM_ERR_NO_ERROR);
}

#if 0
static gfarm_error_t
group_enter_in_tenant(char *groupname, struct tenant *tenant,
	struct group **gpp)
{
	gfarm_error_t e;
	const char *tname;
	char *delim = strchr(groupname, GFARM_TENANT_DELIMITER);
	char *group_tenant_name = NULL, *groupname_save = NULL;

	if (delim != NULL) /* do not allow '+' in username */
		return (GFARM_ERR_INVALID_ARGUMENT);

	tname = tenant_name(tenant);
	if (tname[0] != '\0') {
		group_tenant_name = alloc_name_with_tenant(groupname, tname,
		    "group_enter_in_tenant");
		if (group_tenant_name == NULL)
			return (GFARM_ERR_NO_MEMORY);
		groupname_save = groupname;
		groupname = group_tenant_name;
	}

	e = group_tenant_enter(groupname, gpp);
	if (e != GFARM_ERR_NO_ERROR) {
		free(group_tenant_name);
		return (e);
	}

	free(groupname_save);

	return (GFARM_ERR_NO_ERROR);
}
#endif

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

	/* NOTE: this group remains in struct inode */

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
group_tenant_remove(const char *groupname)
{
	return (group_remove_internal(groupname, 1));
}

gfarm_error_t
group_tenant_remove_in_cache(const char *groupname)
{
	return (group_remove_internal(groupname, 0));
}

static gfarm_error_t
group_remove_in_tenant(const char *groupname, struct tenant *tenant)
{
	gfarm_error_t e;
	char *group_tenant_name = alloc_name_with_tenant(
	    groupname, tenant_name(tenant), "group_remove_in_tenant");

	if (group_tenant_name == NULL)
		return (GFARM_ERR_NO_ERROR);

	e = group_tenant_remove(group_tenant_name);
	free(group_tenant_name);
	return (e);
}

struct group *
group_tenant_lookup_or_enter_invalid(const char *groupname)
{
	gfarm_error_t e;
	struct group *g = group_tenant_lookup_including_invalid(groupname);
	char *n;

	if (g != NULL)
		return (g);

	n = strdup(groupname);
	if (n == NULL) {
		gflog_error(GFARM_MSG_1002758,
		    "group_tenant_lookup_or_enter_invalid(%s): no memory",
		    groupname);
		return (NULL);
	}
	e = group_tenant_enter(n, &g);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1002759,
		    "group_tenant_lookup_or_enter_invalid(%s): "
		    "group_enter: %s",
		    groupname, gfarm_error_string(e));
		free(n);
		return (NULL);
	}
	e = group_tenant_remove_in_cache(groupname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1002760,
		    "group_tenant_lookup_or_enter_invalid(%s): "
		    "group_remove: %s",
		    groupname, gfarm_error_string(e));
	}
	return (g);
}

char *
group_tenant_name_even_invalid(struct group *g)
{
	return (g != NULL ? g->groupname : REMOVED_GROUP_NAME);
}

char *
group_tenant_name(struct group *g)
{
	return (g != NULL && group_is_valid(g) ?
	    g->groupname : REMOVED_GROUP_NAME);
}

char *
group_name_in_tenant_even_invalid(struct group *g, struct process *p)
{
	if (g == NULL)
		return (REMOVED_GROUP_NAME);
	else if (g->tenant == process_get_tenant(p))
		return (g->name_in_tenant);
	else
		return (UNKNOWN_GROUP_NAME);
}

char *
group_name_in_tenant(struct group *g, struct process *p)
{
	if (g == NULL || !group_is_valid(g))
		return (REMOVED_GROUP_NAME);
	else
		return (group_name_in_tenant_even_invalid(g, p));
}

const char *
group_get_tenant_name(struct group *g)
{
	return (tenant_name(g->tenant));
}

struct quota *
group_quota(struct group *g)
{
	return (&g->quota);
}

struct gfarm_quota_subject_info *
group_usage_tmp(struct group *g)
{
	return (&g->usage_tmp);
}

void
group_foreach_internal(struct gfarm_hash_table *hashtab,
	void *closure, void (*callback)(void *, struct group *), int flags)
{
	struct gfarm_hash_iterator it;
	struct group **g;
	int valid_only = (flags & GROUP_FOREARCH_FLAG_VALID_ONLY) != 0;

	for (gfarm_hash_iterator_begin(hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		g = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (!valid_only || group_is_valid(*g))
			callback(closure, *g);
	}
}

void
group_foreach_in_all_tenants(
	void *closure, void (*callback)(void *, struct group *),
	int flags)
{
	group_foreach_internal(group_hashtab, closure, callback, flags);
}

void
group_foreach_in_tenant(
	void *closure, void (*callback)(void *, struct group *),
	struct tenant *tenant, int flags)
{
	struct gfarm_hash_table **hashtab_ref =
		tenant_group_hashtab_ref(tenant);

	if (*hashtab_ref == NULL)
		return;
	group_foreach_internal(*hashtab_ref, closure, callback, flags);
}

/* The memory owner of `*gi' is changed to group.c */
gfarm_error_t
group_info_add(struct gfarm_group_info *gi)
{
	struct group *g;
	gfarm_error_t e = group_tenant_enter(gi->groupname, &g);
	int i;
	struct user *u;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000241,
		    "group_add_one: adding group %s: %s",
		    gi->groupname, gfarm_error_string(e));
		gfarm_group_info_free(gi);
		return (e);
	}
	for (i = 0; i < gi->nusers; i++) {
		u = user_tenant_lookup(gi->usernames[i]);
		if (u == NULL) {
			gflog_debug(GFARM_MSG_1000242,
			    "group_add_one: unknown user %s",
			    gi->usernames[i]);
			(void)group_tenant_remove(g->groupname);
			e = GFARM_ERR_NO_SUCH_USER;
			goto free_gi;
		}
		e = grpassign_add(u, g);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1000243,
			    "group_add_one: grpassign(%s, %s): %s",
			    gi->usernames[i], g->groupname,
			    gfarm_error_string(e));
			(void)group_tenant_remove(g->groupname);
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
		gflog_notice(GFARM_MSG_1002314, "group_add_one(): %s",
		    gfarm_error_string(e));
	}
}

static gfarm_error_t
group_add_user_tenant(struct group *g, const char *username)
{
	struct user *u = user_tenant_lookup(username);

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
		    username, group_tenant_name(g));
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	return (grpassign_add(u, g));
}

static void
group_add_user_and_record(struct group *g, const char *username)
{
	gfarm_error_t e = group_add_user_tenant(g, username);
	struct gfarm_group_info gi;
	int n;
	struct group_assignment *ga;

	if (e == GFARM_ERR_ALREADY_EXISTS)
		return;
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1000244,
		    "failed to add user %s to group %s: %s",
		    username, group_tenant_name(g), gfarm_error_string(e));
		return;
	}

	gflog_info(GFARM_MSG_1000245,
	    "added user %s to group %s", username, group_tenant_name(g));
	gi.groupname = group_tenant_name(g);
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
			gi.usernames[n++] = user_tenant_name(ga->u);

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

	group_hashtab =
	    gfarm_hash_table_alloc(GROUP_HASHTAB_SIZE,
		gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (group_hashtab == NULL)
		gflog_fatal(GFARM_MSG_1000247, "no memory for group hashtab");

	e = db_group_load(NULL, group_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000248,
		    "loading groups: %s", gfarm_error_string(e));
}

void
group_initial_entry(void)
{
	gfarm_error_t e;
	struct group *admin;
	struct gfarm_group_info gi;
	static const char diag[] = "group_init";

	if ((admin = group_tenant_lookup(ADMIN_GROUP_NAME)) == NULL) {
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

	if ((admin = group_tenant_lookup(ROOT_GROUP_NAME)) == NULL) {
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
group_info_send(struct gfp_xdr *client, struct group *g,
	struct process *process, int name_with_tenant)
{
	gfarm_error_t e;
	int n;
	struct group_assignment *ga;

	n = 0;
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next)
		if (user_is_valid(ga->u))
			n++;
	e = gfp_xdr_send(client, "si",
	    name_with_tenant ? g->groupname : group_name_in_tenant(g, process),
	    n);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001524,
			"gfp_xdr_send(groupname) failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next) {
		if (user_is_valid(ga->u))
			if ((e = gfp_xdr_send(client, "s",
			    name_with_tenant
			    ? user_tenant_name(ga->u)
			    : user_name_in_tenant(ga->u, process)))
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
gfm_server_group_info_get_all(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	struct gfarm_hash_table *hashtab;
	struct gfarm_hash_iterator it;
	gfarm_int32_t ngroups;
	struct group **gp;
	struct process *process;
	int name_with_tenant;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_GET_ALL";

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
		hashtab = group_hashtab;
	} else if ((hashtab = *tenant_group_hashtab_ref(
	    process_get_tenant(process))) == NULL) {
		e = gfm_server_put_reply(peer, diag,
		    GFARM_ERR_NO_ERROR, "i", 0);
		giant_unlock();
		return (e);
	}

	ngroups = 0;
	for (gfarm_hash_iterator_begin(hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		gp = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (group_is_valid(*gp))
			++ngroups;
	}
	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_NO_ERROR, "i", ngroups);
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		gflog_debug(GFARM_MSG_1001526,
		    "gfm_server_put_reply(%s): %s",
		    diag, gfarm_error_string(e));
		return (e);
	}
	for (gfarm_hash_iterator_begin(hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		gp = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (group_is_valid(*gp)) {
			e = group_info_send(client, *gp, process,
			    name_with_tenant);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001527,
					"group_info_send() failed: %s",
					gfarm_error_string(e));
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
	int i, j, eof, name_with_tenant, no_memory = 0;
	struct group *g;
	struct process *process = NULL;
	struct tenant *tenant;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_GET_BY_NAMES";

	e = gfm_server_get_request(peer, diag, "i", &ngroups);
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

	/* XXX FIXME too long giant lock */
	giant_lock();

	if (no_memory) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED, "%s (%s@%s): %s",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gfarm_error_string(e));
	} else {
		e = rpc_name_with_tenant(peer, from_client,
		    &name_with_tenant, &process, diag);
	}

	e = gfm_server_put_reply(peer, diag, e, "");
	/* if network error doesn't happen, `e' holds RPC result here */
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		goto free_groups;
	}

	tenant = name_with_tenant ? NULL : process_get_tenant(process);

	for (i = 0; i < ngroups; i++) {
		g = name_with_tenant ?
		    group_tenant_lookup(groups[i]) :
		    group_lookup_in_tenant(groups[i], tenant);
		if (g == NULL) {
			gflog_debug(GFARM_MSG_1003466,
			    "%s: group lookup <%s>: failed", diag, groups[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_NO_SUCH_GROUP, "");
		} else {
			gflog_debug(GFARM_MSG_1003467,
			    "%s: group lookup <%s>: ok", diag, groups[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = group_info_send(client, g, process,
				    name_with_tenant);
		}
		if (peer_had_protocol_error(peer))
			break;
	}
	/*
	 * if (!peer_had_protocol_error(peer))
	 *	the variable `e' holds last group's reply code
	 */
	giant_unlock();

free_groups:
	if (groups != NULL) {
		for (i = 0; i < ngroups; i++)
			free(groups[i]);
		free(groups);
	}
	return (e);
}

static gfarm_error_t
get_group(struct peer *peer, const char *diag, struct gfarm_group_info *gp)
{
	gfarm_error_t e;
	int i, eof;

	e = gfm_server_get_request(peer, diag, "si",
		&gp->groupname, &gp->nusers);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001531,
			"%s request failure: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (gp->nusers <= 0) {
		gp->usernames = NULL;
	} else {
		GFARM_MALLOC_ARRAY(gp->usernames, gp->nusers);
		if (gp->usernames == NULL) {
			gflog_debug(GFARM_MSG_1001532,
				"allocation of usernames failed");
			free(gp->groupname);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	for (i = 0; i < gp->nusers; ++i) {
		e = gfp_xdr_recv(peer_get_conn(peer), 0, &eof, "s",
			&gp->usernames[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001533,
				"gfp_xdr_recv(usernames) failed: %s",
				gfarm_error_string(e));
			for (--i; i >= 0; --i)
				free(&gp->usernames[i]);
			free(gp->usernames);
			free(gp->groupname);
			return (e);
		}
	}
	return (e);
}

/* add tenant part to gi->usernames[] */
static gfarm_error_t
group_info_add_tenant(struct gfarm_group_info *gi, struct tenant *tenant,
	struct peer *diag_peer, const char *diag)
{
	int i;
	const char *tname = tenant_name(tenant);
	char *s;

	if (tname[0] == '\0') {
		/*
		 * this clause never will be executed.
		 * because if tname[0] == '\0', is_super_admin should be true,
		 * and this function won't be called.
		 * thus, this is just for safety.
		 */
		return (GFARM_ERR_NO_ERROR);
	}

	s = alloc_name_with_tenant(gi->groupname, tname, diag);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	free(gi->groupname);
	gi->groupname = s;

	for (i = 0; i < gi->nusers; i++) {
		if (strchr(gi->usernames[i], GFARM_TENANT_DELIMITER) != NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s (%s@%s) group '%s', user '%s': "
			    "'+' is not allowed as user name",
			    diag,
			    peer_get_username(diag_peer),
			    peer_get_hostname(diag_peer),
			    gi->groupname,
			    gi->usernames[i]);
			return (GFARM_ERR_OPERATION_NOT_PERMITTED);
		}
		s = alloc_name_with_tenant(gi->usernames[i], tname, diag);
		if (s == NULL)
			return (GFARM_ERR_NO_MEMORY);
		free(gi->usernames[i]);
		gi->usernames[i] = s;
	}
	return (GFARM_ERR_NO_ERROR);
}

/* do the user+tenant exist? and isn't there any duplication in user+tenant? */
gfarm_error_t
group_user_tenant_check(struct gfarm_group_info *gi, const char *diag)
{
	int i, j;
	struct user *u, **ulist;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	GFARM_MALLOC_ARRAY(ulist, gi->nusers);
	if (ulist == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < gi->nusers && e == GFARM_ERR_NO_ERROR; i++) {
		u = user_tenant_lookup(gi->usernames[i]);
		if (u == NULL) {
			gflog_debug(GFARM_MSG_1000253,
			    "%s: unknown user %s", diag,
				    gi->usernames[i]);
			e = GFARM_ERR_NO_SUCH_USER;
			break;
		}
		for (j = 0; j < i; j++) {
			if (ulist[j] == u) {
				gflog_debug(GFARM_MSG_1003468,
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

gfarm_error_t
gfm_server_group_info_set(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct gfarm_group_info gi;
	struct user *user = peer_get_user(peer);
	struct process *process;
	struct tenant *tenant;
	int is_super_admin, need_free;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_SET";

	e = get_group(peer, diag, &gi);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001534,
			"get_group() failed: %s", gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		gfarm_group_info_free(&gi);
		return (GFARM_ERR_NO_ERROR);
	}
	need_free = 1;
	giant_lock();

	if (!from_client || user == NULL) {
		gflog_debug(GFARM_MSG_1001535,
			"operation is not permitted for user");
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
	    strchr(gi.groupname, GFARM_TENANT_DELIMITER) != NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s (%s@%s) '%s': '+' is not allowed as group name",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    gi.groupname);
	} else if ((is_super_admin ?
	    group_tenant_lookup(gi.groupname) :
	    group_lookup_in_tenant(gi.groupname, tenant)) != NULL) {
		gflog_debug(GFARM_MSG_1001536,
			"group already exists");
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if (strlen(gi.groupname) > GFARM_GROUP_NAME_MAX) {
		gflog_debug(GFARM_MSG_1003469,
		    "%s: too long group name: \"%s\"",
		    diag, gi.groupname);
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if (!is_super_admin && (e = group_info_add_tenant(&gi, tenant,
	    peer, diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "group_info_add_tenant() failed: %s",
		    gfarm_error_string(e));
	} else if ((e = group_user_tenant_check(&gi, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001537,
			"group_user_tenant_check() failed: %s",
			gfarm_error_string(e));
	} else if (gfarm_read_only_mode()) {
		gflog_debug(GFARM_MSG_1005145, "%s (%s@%s) during read_only",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
	/*
	 * We have to call this before group_info_add(),
	 * because group_info_add() frees the memory of gi
	 */
	} else if ((e = db_group_add(&gi)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000254,
		    "failed to store group '%s' to storage: %s",
		    gi.groupname, gfarm_error_string(e));
	} else {
		e = group_info_add(&gi);
		if (e != GFARM_ERR_NO_ERROR) {
			/* cannot use gi.groupname, since it's freed here */
			gflog_debug(GFARM_MSG_1002317,
			    "%s: group_info_add(): %s",
			    diag, gfarm_error_string(e));
		}

		need_free = 0;
	}
	if (need_free)
		gfarm_group_info_free(&gi);
	giant_unlock();

	return (gfm_server_put_reply(peer, diag, e, ""));
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
		struct user *u = user_tenant_lookup(username);

		if (u == NULL) {
			/*
			 * shouldn't happen,
			 * because group_user_check() already checked this.
			 */
			gflog_error(GFARM_MSG_1000255,
			    "%s: unknown user %s", diag,
			    username);
		} else if ((e = grpassign_add(u, group))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1000256,
			    "%s: grpassign(%s, %s): %s", diag,
			    username, gi->groupname,
			    gfarm_error_string(e));
			break; /* must be no memory */
		}
	}
}

gfarm_error_t
gfm_server_group_info_modify(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct gfarm_group_info gi;
	struct user *user = peer_get_user(peer);
	struct group *group;
	struct process *process;
	struct tenant *tenant;
	int is_super_admin;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_MODIFY";

	e = get_group(peer, diag, &gi);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001539,
			"get_group() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		gfarm_group_info_free(&gi);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client || user == NULL) {
		gflog_debug(GFARM_MSG_1001540,
			"operation is not permitted for user");
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
	} else if ((group = (is_super_admin = user_is_super_admin(user)) ?
	    group_tenant_lookup(gi.groupname) :
	    group_lookup_in_tenant(gi.groupname, tenant)) == NULL) {
		gflog_debug(GFARM_MSG_1001541,
			"group_lookup() failed");
		e = GFARM_ERR_NO_SUCH_GROUP;
	} else if (!is_super_admin && (e = group_info_add_tenant(&gi, tenant,
	    peer, diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "group_info_add_tenant() failed: %s",
		    gfarm_error_string(e));
	} else if ((e = group_user_tenant_check(&gi, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001542, "group_user_check() failed: %s",
		    gfarm_error_string(e));
	} else if (gfarm_read_only_mode()) {
		gflog_debug(GFARM_MSG_1005146, "%s (%s@%s) during read_only",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
	} else {
		group_modify(group, &gi, diag);
		/* change all entries */
		e = db_group_modify(&gi, 0, 0, NULL, 0, NULL);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000257,
			    "failed to modify group '%s' in db: %s",
			    gi.groupname, gfarm_error_string(e));
	}
	gfarm_group_info_free(&gi);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_group_info_remove(struct peer *peer, int from_client, int skip)
{
	char *groupname;
	gfarm_error_t e, e2;
	struct user *user = peer_get_user(peer);
	struct process *process;
	struct tenant *tenant;
	int is_super_admin;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_REMOVE";

	e = gfm_server_get_request(peer, diag, "s", &groupname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001543,
			"group_info_remove request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(groupname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client || user == NULL) {
		gflog_debug(GFARM_MSG_1001544,
			"operation is not permitted for user");
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
	} else if (!tenant_needs_chroot(tenant) && (
	    strcmp(groupname, ADMIN_GROUP_NAME) == 0 ||
	    strcmp(groupname, ROOT_GROUP_NAME) == 0)) {
		gflog_debug(GFARM_MSG_1002211,
		    "%s: administrator group \"%s\" in default tenant "
		    "should not be deleted",
		    diag, groupname);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (!(is_super_admin = user_is_super_admin(user)) &&
	    strchr(groupname, GFARM_TENANT_DELIMITER) != NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s (%s@%s) '%s': '+' is not allowed as group name",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    groupname);
	} else if (gfarm_read_only_mode()) {
		gflog_debug(GFARM_MSG_1005147, "%s (%s@%s) during read_only",
		    diag, peer_get_username(peer), peer_get_hostname(peer));
		e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
	} else {
		if (is_super_admin)
			e = group_tenant_remove(groupname);
		else
			e = group_remove_in_tenant(groupname, tenant);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s (%s@%s) '%s': %s", diag,
			    peer_get_username(peer), peer_get_hostname(peer),
			    groupname, gfarm_error_string(e));
		} else {
			char *name;

			if (is_super_admin || !tenant_needs_chroot(tenant)) {
				e2 = db_group_remove(groupname);
			} else if ((name = alloc_name_with_tenant(groupname,
			    tenant_name(tenant), diag)) == NULL) {
				e2 = GFARM_ERR_NO_MEMORY;
			} else  {
				e2 = db_group_remove(name);
				free(name);
			}
			if (e2 != GFARM_ERR_NO_ERROR)
				gflog_error(GFARM_MSG_1000258,
				    "%s: db_group_remove: %s",
				    diag, gfarm_error_string(e2));
		}
	}
	free(groupname);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_group_info_add_users(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_ADD_USERS";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000259, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_info_remove_users(struct peer *peer,
	int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_GROUP_INFO_REMOVE_USERS";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000260, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_names_get_by_users(struct peer *peer,
	int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_GROUP_NAMES_GET_BY_USERS";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000261, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}
#endif /* TEST */
