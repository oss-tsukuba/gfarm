#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/group_info.h>

#include "gfutil.h"
#include "hash.h"
#include "gfp_xdr.h"
#include "auth.h"

#include "subr.h"
#include "user.h"
#include "group.h"
#include "peer.h"

#define GROUP_HASHTAB_SIZE	3079	/* prime number */

struct group {
	char *groupname;
	struct group_assignment users;
};

char REMOVED_GROUP_NAME[] = "gfarm-removed-group";

static struct gfarm_hash_table *group_hashtab = NULL;

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
	const unsigned char *const *groupnamep = key;
	const unsigned char *k = *groupnamep;

	return (gfarm_hash_default(k, strlen(k)));
}

int
hash_key_equal_group(
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
grpassign_init(void)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
group_init(void)
{
	group_hashtab =
	    gfarm_hash_table_alloc(GROUP_HASHTAB_SIZE,
		hash_group, hash_key_equal_group);
	if (group_hashtab == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (GFARM_ERR_NO_ERROR);
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

gfarm_error_t
group_enter(char *groupname, struct group **gpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct group *g = malloc(sizeof(*g));

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
	g->users.user_prev = g->users.user_next = &g->users;
	*(struct group **)gfarm_hash_entry_data(entry) = g;
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
		return (GFARM_ERR_NO_SUCH_OBJECT);
	g = *(struct group **)gfarm_hash_entry_data(entry);

	free(g->groupname);

	/* free group_assignment */
	while ((ga = g->users.user_next) != &g->users)
		grpassign_remove(ga);

	/* mark this as removed */
	g->groupname = REMOVED_GROUP_NAME;
	/* XXX We should have a list which points all removed groups */
	return (GFARM_ERR_NO_ERROR);
}

char *
group_name(struct group *g)
{
	return (g->groupname);
}

/*
 * I/O
 */

static FILE *group_fp;

gfarm_error_t
group_info_open_for_seq_read(void)
{
	group_fp = fopen("group", "r");
	if (group_fp == NULL)
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
group_info_open_for_seq_write(void)
{
	group_fp = fopen("group", "w");
	if (group_fp == NULL)
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

/* This function needs to allocate strings */
gfarm_error_t
group_info_read_next(struct gfarm_group_info *ui)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
group_info_write_next(struct gfarm_group_info *ui)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
group_info_close_for_seq_read(void)
{
	fclose(group_fp);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
group_info_close_for_seq_write(void)
{
	fclose(group_fp);
	return (GFARM_ERR_NO_ERROR);
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
		n++;
	e = gfp_xdr_send(client, "si", g->groupname, n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (ga = g->users.user_next; ga != &g->users; ga = ga->user_next) {
		if ((e = gfp_xdr_send(client, "s", user_name(ga->u))) !=
		    GFARM_ERR_NO_ERROR)
			return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_group_info_get_all(struct peer *peer, int from_client)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	struct gfarm_hash_iterator it;
	gfarm_int32_t ngroups;

	/* XXX FIXME too long giant lock */
	giant_lock();

	ngroups = 0;
	for (gfarm_hash_iterator_begin(group_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
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
		e = group_info_send(client,
		    gfarm_hash_entry_data(gfarm_hash_iterator_access(&it)));
		if (e != GFARM_ERR_NO_ERROR) {
			giant_unlock();
			return (e);
		}
	}
	giant_unlock();
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_group_info_get_by_names(struct peer *peer, int from_client)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	gfarm_int32_t ngroups;
	char **groups;
	int i, j, eof, no_memory = 0;
	struct group *g;
	char *groupname;

	e = gfm_server_get_request(peer, "group_info_get_by_names",
	    "i", &ngroups);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	groups = malloc(sizeof(*groups) * ngroups);
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
	if (no_memory) {
		e = gfm_server_put_reply(peer, "group_info_get_by_names",
		    GFARM_ERR_NO_MEMORY, "");
	} else {
		e = gfm_server_put_reply(peer, "group_info_get_by_names",
		    GFARM_ERR_NO_ERROR, "");
	}
	if (no_memory || e != GFARM_ERR_NO_ERROR) {
		if (groups != NULL) {
			for (i = 0; i < ngroups; i++) {
				if (groups[i] != NULL)
					free(groups[i]);
			}
			free(groups);
		}
		return (e);
	}
	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < ngroups; i++) {
		g = group_lookup(groups[i]);
		if (g == NULL) {
			e = gfm_server_put_reply(peer,
			    "group_info_get_by_name",
			    GFARM_ERR_NO_SUCH_OBJECT, "");
		} else {
			e = gfm_server_put_reply(peer,
			    "group_info_get_by_name",
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = group_info_send(client, g);
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	for (i = 0; i < ngroups; i++)
		free(groups[i]);
	free(groups);
	giant_unlock();
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_group_info_set(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("group_info_set", "not implemented");

	e = gfm_server_put_reply(peer, "group_info_set",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_info_modify(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("group_info_modify", "not implemented");

	e = gfm_server_put_reply(peer, "group_info_modify",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_info_remove(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("group_info_remove", "not implemented");

	e = gfm_server_put_reply(peer, "group_info_remove",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_info_add_users(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("group_info_add_users", "not implemented");

	e = gfm_server_put_reply(peer, "group_info_add_users",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_info_remove_users(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("group_info_remove_users", "not implemented");

	e = gfm_server_put_reply(peer, "group_info_remove_users",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_group_names_get_by_users(struct peer *peer, int from_client)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("group_names_get_by_users", "not implemented");

	e = gfm_server_put_reply(peer, "group_names_get_by_users",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#endif /* TEST */
