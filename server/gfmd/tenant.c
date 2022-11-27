#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"

#include "subr.h"

#include "tenant.h"

#define TENANT_HASHTAB_SIZE	3079	/* prime number */

static struct gfarm_hash_table *tenant_hashtab = NULL;

struct tenant {
	char *tenant_name;
	struct gfarm_hash_table *user_hashtab;
	struct gfarm_hash_table *group_hashtab;
};

struct tenant *
tenant_lookup(const char *tenant_name)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(tenant_hashtab,
	    &tenant_name, sizeof(tenant_name));
	if (entry == NULL)
		return (NULL);
	return (*(struct tenant **)gfarm_hash_entry_data(entry));
}

gfarm_error_t
tenant_lookup_or_enter(const char *tenant_name, struct tenant **tp)
{
	struct tenant *t;
	char *name;
	int created;
	struct gfarm_hash_entry *entry;

	t = tenant_lookup(tenant_name);
	if (t != NULL) {
		*tp = t;
		return (GFARM_ERR_NO_ERROR);
	}

	name = strdup_log(tenant_name, "tenant_lookup_or_enter");
	if (name == NULL)
		return (GFARM_ERR_NO_MEMORY);

	GFARM_MALLOC(t);
	if (t == NULL) {
		free(name);
		gflog_debug(GFARM_MSG_UNFIXED, "no memory for new tenant");
		return (GFARM_ERR_NO_MEMORY);
	}

	entry = gfarm_hash_enter(tenant_hashtab,
	    &name, sizeof(name), sizeof(struct tenant *),
	    &created);
	if (entry == NULL) {
		free(name);
		free(t);
		gflog_debug(GFARM_MSG_UNFIXED,
			"no memory for tenant hashtab entry");
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "tenant '%s' already exists, possibly missing giant_lock",
		    tenant_name);
	}

	t->tenant_name = name;
	t->user_hashtab = NULL;
	t->group_hashtab = NULL;
	*(struct tenant **)gfarm_hash_entry_data(entry) = t;
	*tp = t;
	return (GFARM_ERR_NO_ERROR);
}

const char *
tenant_name(struct tenant *t)
{
	return (t->tenant_name);
}

int
tenant_needs_chroot(struct tenant *t)
{
	return (t->tenant_name[0] != '\0');
}


struct gfarm_hash_table **
tenant_user_hashtab_ref(struct tenant *t)
{
	return (&t->user_hashtab);
}

struct gfarm_hash_table **
tenant_group_hashtab_ref(struct tenant *t)
{
	return (&t->group_hashtab);
}

struct tenant *
tenant_default(void)
{
	struct tenant *def = tenant_lookup("");

	if (def == NULL)
		gflog_fatal(GFARM_MSG_UNFIXED, "default tenant not found");
	return (def);
}

void
tenant_init(void)
{
	tenant_hashtab =
	    gfarm_hash_table_alloc(TENANT_HASHTAB_SIZE,
		gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (tenant_hashtab == NULL)
		gflog_fatal(GFARM_MSG_UNFIXED, "no memory for tenant hashtab");
}
