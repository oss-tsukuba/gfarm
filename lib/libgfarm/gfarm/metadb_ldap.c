/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lber.h>
#include <ldap.h>
#include <gfarm/gfarm.h>

/* old openldap does not have ldap_memfree. */
#define	ldap_memfree(a)

#define INT32STRLEN	GFARM_INT32STRLEN
#define INT64STRLEN	GFARM_INT64STRLEN
#define ARRAY_LENGTH(array)	GFARM_ARRAY_LENGTH(array)

/**********************************************************************/

static LDAP *gfarm_ldap_server = NULL;

char *gfarm_metadb_initialize(void)
{
	int rv;
	int port;
	char *e;
	LDAPMessage *res;

	if (gfarm_ldap_server_name == NULL)
		return ("gfarm.conf: ldap_serverhost is missing");
	if (gfarm_ldap_server_port == NULL)
		return ("gfarm.conf: ldap_serverport is missing");
	port = strtol(gfarm_ldap_server_port, &e, 0);
	if (e == gfarm_ldap_server_port || port <= 0 || port >= 65536)
		return ("gfarm.conf: ldap_serverport: "
			"illegal value");
	if (gfarm_ldap_base_dn == NULL)
		return ("gfarm.conf: ldap_base_dn is missing");

	/*
	 * initialize LDAP
	 */

	/* open a connection */
	gfarm_ldap_server = ldap_init(gfarm_ldap_server_name, port);
	if (gfarm_ldap_server == NULL) {
		switch (errno) {
		case EHOSTUNREACH:
			return ("gfarm meta-db ldap_serverhost "
				"access failed");
		case ECONNREFUSED:
			return ("gfarm meta-db ldap_serverport "
				"connection refused");
		default:
			return ("gfarm meta-db ldap_server "
				"access failed");
			/*return (strerror(errno));*/
		}
	}

	/* authenticate as nobody */
	rv = ldap_simple_bind_s(gfarm_ldap_server, NULL, NULL); 
	if (rv == LDAP_PROTOCOL_ERROR) {
		/* Try the version 3 */
		int version = LDAP_VERSION3;
		ldap_set_option(gfarm_ldap_server, LDAP_OPT_PROTOCOL_VERSION,
				&version);
		rv = ldap_simple_bind_s(gfarm_ldap_server, NULL, NULL); 
	}
	if (rv != LDAP_SUCCESS)
		return (ldap_err2string(rv));

	/* sanity check. base_dn can be accessed? */
	rv = ldap_search_s(gfarm_ldap_server, gfarm_ldap_base_dn,
	    LDAP_SCOPE_BASE, "objectclass=top", NULL, 0, &res);
	if (rv != LDAP_SUCCESS) {
		if (rv == LDAP_NO_SUCH_OBJECT)
			return ("gfarm meta-db ldap_base_dn not found");
		return ("gfarm meta-db ldap_base_dn access failed");
	}
	ldap_msgfree(res);

	return (NULL);
}

char *gfarm_metadb_terminate(void)
{
	int rv;

	if (gfarm_ldap_server == NULL)
		return ("metadb connection alrady disconnected");

	/* close and free connection resources */
	rv = ldap_unbind(gfarm_ldap_server);
	gfarm_ldap_server = NULL;
	if (rv != LDAP_SUCCESS)
		return (ldap_err2string(rv));

	return (NULL);
}
/**********************************************************************/

struct ldap_string_modify {
	LDAPMod mod;
	char *str[2];
};

static void
set_string_mod(
	LDAPMod **modp,
	int op,
	char *type,
	char *value,
	struct ldap_string_modify *storage)
{
	storage->str[0] = value;
	storage->str[1] = NULL;
	storage->mod.mod_op = op;
	storage->mod.mod_type = type;
	storage->mod.mod_vals.modv_strvals = storage->str;
	*modp = &storage->mod;
}

#if 0
static void
set_delete_mod(
	LDAPMod **modp,
	char *type,
	LDAPMod *storage)
{
	storage->mod_op = LDAP_MOD_DELETE;
	storage->mod_type = type;
	storage->mod_vals.modv_strvals = NULL;
	*modp = storage;
}
#endif

struct gfarm_generic_info_ops {
	size_t info_size;
	char *query_type;
	char *dn_template;
	char *(*make_dn)(void *key);
	void (*clear)(void *info);
	void (*set_field)(void *info, char *attribute, char **vals);
	int (*validate)(void *info);
	void (*free)(void *info);
};

char *
gfarm_generic_info_get(
	void *key,
	void *info,
	const struct gfarm_generic_info_ops *ops)
{
	LDAPMessage *res, *e;
	int n, rv;
	char *a;
	BerElement *ber;
	char **vals;
	char *dn = ops->make_dn(key);

	if (dn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	rv = ldap_search_s(gfarm_ldap_server, dn, 
	    LDAP_SCOPE_BASE, ops->query_type, NULL, 0, &res);
	free(dn);
	if (rv != LDAP_SUCCESS) {
		if (rv == LDAP_NO_SUCH_OBJECT)
			return (GFARM_ERR_NO_SUCH_OBJECT);
		return (ldap_err2string(rv));
	}
	n = ldap_count_entries(gfarm_ldap_server, res);
	if (n == 0) {
		/* free the search results */
		ldap_msgfree(res);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	ops->clear(info);
	e = ldap_first_entry(gfarm_ldap_server, res);

	ber = NULL;
	for (a = ldap_first_attribute(gfarm_ldap_server, e, &ber); a != NULL;
	    a = ldap_next_attribute(gfarm_ldap_server, e, ber)) {
		vals = ldap_get_values(gfarm_ldap_server, e, a);
		if (vals[0] != NULL)
			ops->set_field(info, a, vals);
		ldap_value_free(vals);
		ldap_memfree(a);
	}
	if (ber != NULL)
		ber_free(ber, 0);

	/* free the search results */
	ldap_msgfree(res);

	/* should check all fields are filled */
	if (!ops->validate(info)) {
		ops->free(info);
		/* XXX - different error code is better ? */
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}

	return (NULL); /* success */
}

char *
gfarm_generic_info_set(
	void *key,
	LDAPMod **modv,
	const struct gfarm_generic_info_ops *ops)
{
	int rv;
	char *dn = ops->make_dn(key);

	if (dn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	rv = ldap_add_s(gfarm_ldap_server, dn, modv);
	free(dn);
	if (rv != LDAP_SUCCESS) {
		if (rv == LDAP_ALREADY_EXISTS)
			return (GFARM_ERR_ALREADY_EXISTS);
		return (ldap_err2string(rv));
	}
	return (NULL);
}

char *
gfarm_generic_info_modify(
	void *key,
	LDAPMod **modv,
	const struct gfarm_generic_info_ops *ops)
{
	int rv;
	char *dn = ops->make_dn(key);

	if (dn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	rv = ldap_modify_s(gfarm_ldap_server, dn, modv);
	free(dn);
	switch (rv) {
	case LDAP_SUCCESS:
		return (NULL);
	case LDAP_NO_SUCH_OBJECT:
		return (GFARM_ERR_NO_SUCH_OBJECT);
	case LDAP_ALREADY_EXISTS:
		return (GFARM_ERR_ALREADY_EXISTS);
	default:
		return (ldap_err2string(rv));
	}
}

char *
gfarm_generic_info_remove(
	void *key,
	const struct gfarm_generic_info_ops *ops)
{
	int rv;
	char *dn = ops->make_dn(key);

	if (dn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	rv = ldap_delete_s(gfarm_ldap_server, dn);
	free(dn);
	if (rv != LDAP_SUCCESS) {
		if (rv == LDAP_NO_SUCH_OBJECT)
			return (GFARM_ERR_NO_SUCH_OBJECT);
		return (ldap_err2string(rv));
	}
	return (NULL);
}

void
gfarm_generic_info_free_all(
	int n,
	void *vinfos,
	const struct gfarm_generic_info_ops *ops)
{
	int i;
	char *infos = vinfos;

	for (i = 0; i < n; i++) {
		ops->free(infos);
		infos += ops->info_size;
	}
	free(vinfos);
}

char *
gfarm_generic_info_get_all(
	char *dn,
	int scope, /* LDAP_SCOPE_ONELEVEL or LDAP_SCOPE_SUBTREE */
	char *query,
	int *np,
	void *infosp,
	const struct gfarm_generic_info_ops *ops)
{
	LDAPMessage *res, *e;
	int i, n, rv;
	char *a;
	BerElement *ber;
	char **vals;
	char *infos, *tmp_info;

	/* search for entries, return all attrs  */
	rv = ldap_search_s(gfarm_ldap_server, dn, scope, query, NULL, 0, &res);
	if (rv != LDAP_SUCCESS) {
		if (rv == LDAP_NO_SUCH_OBJECT)
			return (GFARM_ERR_NO_SUCH_OBJECT);
		return (ldap_err2string(rv));
	}
	n = ldap_count_entries(gfarm_ldap_server, res);
	if (n == 0) {
		/* free the search results */
		ldap_msgfree(res);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	infos = malloc(ops->info_size * n);
	if (infos == NULL) {
		/* free the search results */
		ldap_msgfree(res);
		return (GFARM_ERR_NO_MEMORY);
	}

	/* use last element as temporary buffer */
	tmp_info = infos + ops->info_size * (n - 1);

	/* step through each entry returned */
	for (i = 0, e = ldap_first_entry(gfarm_ldap_server, res); e != NULL;
	    e = ldap_next_entry(gfarm_ldap_server, e)) {

		ops->clear(tmp_info);

		ber = NULL;
		for (a = ldap_first_attribute(gfarm_ldap_server, e, &ber);
		    a != NULL;
		    a = ldap_next_attribute(gfarm_ldap_server, e, ber)) {
			vals = ldap_get_values(gfarm_ldap_server, e, a);
			if (vals[0] != NULL)
				ops->set_field(tmp_info, a, vals);
			ldap_value_free(vals);
			ldap_memfree(a);
		}
		if (ber != NULL)
			ber_free(ber, 0);

		if (!ops->validate(tmp_info)) {
			/* invalid record */
			ops->free(tmp_info);
			continue;
		}
		if (i < n - 1) { /* if (i == n - 1), do not have to copy */
			memcpy(infos + ops->info_size * i, tmp_info,
			       ops->info_size);
		}
		i++;
	}

	/* free the search results */
	ldap_msgfree(res);

	if (i == 0) {
		free(infos);
		/* XXX - data were all invalid */
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}

	/* XXX - if (i < n), element from (i+1) to (n-1) may be wasted */
	*np = i;
	*(char **)infosp = infos;
	return (NULL);
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
char *
gfarm_generic_info_get_foreach(
	char *dn,
	int scope, /* LDAP_SCOPE_ONELEVEL or LDAP_SCOPE_SUBTREE */
	char *query,
	void *tmp_info, /* just used as a work area */
	void (*callback)(void *, void *),
	void *closure,
	const struct gfarm_generic_info_ops *ops)
{
	LDAPMessage *res, *e;
	int i, rv;
	char *a;
	BerElement *ber;
	char **vals;

	/* search for entries, return all attrs  */
	rv = ldap_search_s(gfarm_ldap_server, dn, scope, query, NULL, 0, &res);
	if (rv != LDAP_SUCCESS) {
		if (rv == LDAP_NO_SUCH_OBJECT)
			return (GFARM_ERR_NO_SUCH_OBJECT);
		return (ldap_err2string(rv));
	}

	/* step through each entry returned */
	for (i = 0, e = ldap_first_entry(gfarm_ldap_server, res); e != NULL;
	    e = ldap_next_entry(gfarm_ldap_server, e)) {

		ops->clear(tmp_info);

		ber = NULL;
		for (a = ldap_first_attribute(gfarm_ldap_server, e, &ber);
		    a != NULL;
		    a = ldap_next_attribute(gfarm_ldap_server, e, ber)) {
			vals = ldap_get_values(gfarm_ldap_server, e, a);
			if (vals[0] != NULL)
				ops->set_field(tmp_info, a, vals);
			ldap_value_free(vals);
			ldap_memfree(a);
		}
		if (ber != NULL)
			ber_free(ber, 0);

		if (!ops->validate(tmp_info)) {
			/* invalid record */
			ops->free(tmp_info);
			continue;
		}
		(*callback)(closure, tmp_info);
		ops->free(tmp_info);
		i++;
	}
	/* free the search results */
	ldap_msgfree(res);

	if (i == 0)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	return (NULL);
}

/**********************************************************************/

static char *gfarm_host_info_make_dn(void *vkey);
static void gfarm_host_info_clear(void *info);
static void gfarm_host_info_set_field(void *info, char *attribute, char **vals);
static int gfarm_host_info_validate(void *info);

struct gfarm_host_info_key {
	const char *hostname;
};

static const struct gfarm_generic_info_ops gfarm_host_info_ops = {
	sizeof(struct gfarm_host_info),
	"(objectclass=GFarmHost)",
	"hostname=%s, %s",
	gfarm_host_info_make_dn,
	gfarm_host_info_clear,
	gfarm_host_info_set_field,
	gfarm_host_info_validate,
	(void (*)(void *))gfarm_host_info_free,
};

static char *
gfarm_host_info_make_dn(void *vkey)
{
	struct gfarm_host_info_key *key = vkey;
	char *dn = malloc(strlen(gfarm_host_info_ops.dn_template) +
			  strlen(key->hostname) +
			  strlen(gfarm_ldap_base_dn) + 1);

	if (dn == NULL)
		return (NULL);
	sprintf(dn, gfarm_host_info_ops.dn_template,
		key->hostname, gfarm_ldap_base_dn);
	return (dn);
}

static void
gfarm_host_info_clear(void *vinfo)
{
	struct gfarm_host_info *info = vinfo;

	memset(info, 0, sizeof(*info));
#if 0
	info->ncpu = GFARM_HOST_INFO_NCPU_NOT_SET;
#else
	info->ncpu = 1; /* assume 1 CPU by default */
#endif
}

static void
gfarm_host_info_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	struct gfarm_host_info *info = vinfo;

	if (strcasecmp(attribute, "hostname") == 0) {
		info->hostname = strdup(vals[0]);
	} else if (strcasecmp(attribute, "hostalias") == 0) {
		info->hostaliases = gfarm_strarray_dup(vals);
		info->nhostaliases = info->hostaliases == NULL ? 0 :
		    gfarm_strarray_length(info->hostaliases);
	} else if (strcasecmp(attribute, "architecture") == 0) {
		info->architecture = strdup(vals[0]);
	} else if (strcasecmp(attribute, "ncpu") == 0) {
		info->ncpu = strtol(vals[0], NULL, 0);
	}
}

static int
gfarm_host_info_validate(void *vinfo)
{
	struct gfarm_host_info *info = vinfo;

	/* info->hostaliases may be NULL */
	return (
	    info->hostname != NULL &&
	    info->architecture != NULL &&
	    info->ncpu != GFARM_HOST_INFO_NCPU_NOT_SET
	);
}

void
gfarm_host_info_free(
	struct gfarm_host_info *info)
{
	if (info->hostname != NULL)
		free(info->hostname);
	if (info->hostaliases != NULL) {
		gfarm_strarray_free(info->hostaliases);
		info->nhostaliases = 0;
	}
	if (info->architecture != NULL)
		free(info->architecture);
	/*
	 * if implementation of this function is changed,
	 * implementation of gfarm_host_info_get_architecture_by_host()
	 * should be changed, too.
	 */
}

char *gfarm_host_info_get(
	const char *hostname,
	struct gfarm_host_info *info)
{
	struct gfarm_host_info_key key;

	key.hostname = hostname;

	return (gfarm_generic_info_get(&key, info,
	    &gfarm_host_info_ops));
}

static char *
gfarm_host_info_update(
	char *hostname,
	struct gfarm_host_info *info,
	int mod_op,
	char *(*update_op)(void *, LDAPMod **,
	    const struct gfarm_generic_info_ops *))
{
	int i;
	LDAPMod *modv[6];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];
	char ncpu_string[INT32STRLEN + 1];

	LDAPMod hostaliases_mod;

	struct gfarm_host_info_key key;

	key.hostname = hostname;

	/*
	 * `info->hostname' doesn't have to be set,
	 * because this function uses its argument instead.
	 */
	sprintf(ncpu_string, "%d", info->ncpu);
	i = 0;
	set_string_mod(&modv[i], mod_op,
		       "objectclass", "GFarmHost", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "hostname", hostname, &storage[i]);
	i++;

	/* "hostalias" is optional */
	if (info->hostaliases != NULL && info->nhostaliases > 0) {
		hostaliases_mod.mod_type = "hostalias";
		hostaliases_mod.mod_op = mod_op;
		hostaliases_mod.mod_vals.modv_strvals = info->hostaliases;
		modv[i] = &hostaliases_mod;
		i++;
	}

	set_string_mod(&modv[i], mod_op,
		       "architecture", info->architecture, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "ncpu", ncpu_string, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv) || i == ARRAY_LENGTH(modv) - 1);

	return ((*update_op)(&key, modv, &gfarm_host_info_ops));
}

char *
gfarm_host_info_remove_hostaliases(const char *hostname)
{
	int i;
	LDAPMod *modv[2];
	LDAPMod hostaliases_mod;

	struct gfarm_host_info_key key;

	key.hostname = hostname;

	i = 0;

	hostaliases_mod.mod_type = "hostalias";
	hostaliases_mod.mod_op = LDAP_MOD_DELETE;
	hostaliases_mod.mod_vals.modv_strvals = NULL;
	modv[i] = &hostaliases_mod;
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return (gfarm_generic_info_modify(&key, modv, &gfarm_host_info_ops));
}

char *
gfarm_host_info_set(
	char *hostname,
	struct gfarm_host_info *info)
{
	return (gfarm_host_info_update(hostname, info,
	    LDAP_MOD_ADD, gfarm_generic_info_set));
}

char *
gfarm_host_info_replace(
	char *hostname,
	struct gfarm_host_info *info)
{
	return (gfarm_host_info_update(hostname, info,
	    LDAP_MOD_REPLACE, gfarm_generic_info_modify));
}

char *
gfarm_host_info_remove(const char *hostname)
{
	struct gfarm_host_info_key key;

	key.hostname = hostname;

	return (gfarm_generic_info_remove(&key,
	    &gfarm_host_info_ops));
}

void
gfarm_host_info_free_all(
	int n,
	struct gfarm_host_info *infos)
{
	gfarm_generic_info_free_all(n, infos,
	    &gfarm_host_info_ops);
}

char *
gfarm_host_info_get_all(
	int *np,
	struct gfarm_host_info **infosp)
{
	char *error;
	int n;
	struct gfarm_host_info *infos;

	error = gfarm_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_host_info_ops.query_type,
	    &n, &infos,
	    &gfarm_host_info_ops);
	if (error != NULL)
		return (error);

	*np = n;
	*infosp = infos;
	return (NULL);
}

char *
gfarm_host_info_get_by_name_alias(
	const char *name_alias,
	struct gfarm_host_info *info)
{
	char *error;
	int n;
	struct gfarm_host_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmHost)(|(hostname=%s)(hostalias=%s)))";
	char *query = malloc(sizeof(query_template) + strlen(name_alias) * 2);

	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, name_alias, name_alias);
	error = gfarm_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, query,
	    &n, &infos,
	    &gfarm_host_info_ops);
	free(query);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (GFARM_ERR_UNKNOWN_HOST);
		return (error);
	}

	if (n != 1) {
		gfarm_host_info_free_all(n, infos);
		return (GFARM_ERR_AMBIGUOUS_RESULT);
	}
	*info = infos[0];
	return (NULL);
}

char *
gfarm_host_info_get_allhost_by_architecture(const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	char *error;
	int n;
	struct gfarm_host_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmHost)(architecture=%s))";
	char *query = malloc(sizeof(query_template) +
			     strlen(architecture));

	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, architecture);
	error = gfarm_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, query,
	    &n, &infos,
	    &gfarm_host_info_ops);
	free(query);
	if (error != NULL)
		return (error);

	*np = n;
	*infosp = infos;
	return (NULL);
}

char *
gfarm_host_info_get_architecture_by_host(const char *hostname)
{
	char *error;
	struct gfarm_host_info info;

	error = gfarm_host_info_get(hostname, &info);
	if (error != NULL)
		return (NULL);

	/* free info except info.architecture */
	free(info.hostname);
	if (info.hostaliases != NULL) {
		gfarm_strarray_free(info.hostaliases);
		info.nhostaliases = 0;
	}

	return (info.architecture);
}

/**********************************************************************/

static char *gfarm_path_info_make_dn(void *vkey);
static void gfarm_path_info_clear(void *info);
static void gfarm_path_info_set_field(void *info, char *attribute, char **vals);
static int gfarm_path_info_validate(void *info);
struct gfarm_path_info;

struct gfarm_path_info_key {
	const char *pathname;
};

static int
gfarm_metadb_ldap_need_escape(char c)
{
	switch (c) {
	case ',': case '+': case '"': case '\\':
	case '<': case '>': case ';':
		return (1);
	}
	return (0);
}

static char *
gfarm_metadb_ldap_escape_pathname(const char *pathname)
{
	const char *c = pathname;
	char *escaped_pathname, *d;

	/* if pathname is a null string, return immediately */
	if (*c == '\0')
		return (NULL);

	escaped_pathname = malloc(strlen(pathname) * 3);
	if (escaped_pathname == NULL)
		return (escaped_pathname);

	d = escaped_pathname;
	/* Escape the first character; ' ', '#', and need_escape(). */
	if (*c == ' ' || *c == '#' || gfarm_metadb_ldap_need_escape(*c))
		*d++ = '\\';
	*d++ = *c++;
	while (*c) {
		if (gfarm_metadb_ldap_need_escape(*c))
			*d++ = '\\';
		*d++ = *c++;
	}
	/*
	 * Escape the last 'space' character.  pathname should have a
	 * length of more than 1.  If d[-1] == ' ', it should have a
	 * length more than 2.
	 */
	if (d[-1] == ' ' && d[-2] != '\\') {
		d[-1] = '\\';
		*d++ = ' ';
	}
	*d = '\0';
	return (escaped_pathname);
}

static const struct gfarm_generic_info_ops gfarm_path_info_ops = {
	sizeof(struct gfarm_path_info),
	"(objectclass=GFarmPath)",
	"pathname=%s, %s",
	gfarm_path_info_make_dn,
	gfarm_path_info_clear,
	gfarm_path_info_set_field,
	gfarm_path_info_validate,
	(void (*)(void *))gfarm_path_info_free,
};

static char *
gfarm_path_info_make_dn(void *vkey)
{
	struct gfarm_path_info_key *key = vkey;
	char *escaped_pathname, *dn;

	escaped_pathname = gfarm_metadb_ldap_escape_pathname(key->pathname);
	if (escaped_pathname == NULL)
		return (NULL);

	dn = malloc(strlen(gfarm_path_info_ops.dn_template) +
		    strlen(escaped_pathname) + strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		free(escaped_pathname);
		return (NULL);
	}
	sprintf(dn, gfarm_path_info_ops.dn_template,
		escaped_pathname, gfarm_ldap_base_dn);
	free(escaped_pathname);
	return (dn);
}

static void
gfarm_path_info_clear(void *vinfo)
{
	struct gfarm_path_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static void
gfarm_path_info_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	struct gfarm_path_info *info = vinfo;

	if (strcasecmp(attribute, "pathname") == 0) {
		info->pathname = strdup(vals[0]);
	} else if (strcasecmp(attribute, "mode") == 0) {
		info->status.st_mode = strtol(vals[0], NULL, 8);
	} else if (strcasecmp(attribute, "user") == 0) {
		info->status.st_user = strdup(vals[0]);
	} else if (strcasecmp(attribute, "group") == 0) {
		info->status.st_group = strdup(vals[0]);
	} else if (strcasecmp(attribute, "atimesec") == 0) {
		info->status.st_atimespec.tv_sec = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "atimensec") == 0) {
		info->status.st_atimespec.tv_nsec = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "mtimesec") == 0) {
		info->status.st_mtimespec.tv_sec = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "mtimensec") == 0) {
		info->status.st_mtimespec.tv_nsec = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "ctimesec") == 0) {
		info->status.st_ctimespec.tv_sec = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "ctimensec") == 0) {
		info->status.st_ctimespec.tv_nsec = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "nsections") == 0) {
		info->status.st_nsections = strtol(vals[0], NULL, 0);
	}
}

static int
gfarm_path_info_validate(void *vinfo)
{
	struct gfarm_path_info *info = vinfo;

	/* XXX - should check all fields are filled */
	return (
	    info->pathname != NULL &&
	    info->status.st_user != NULL &&
	    info->status.st_group != NULL
	);
}

static char *
gfarm_path_info_update(
	char *pathname,
	struct gfarm_path_info *info,
	int mod_op,
	char *(*update_op)(void *, LDAPMod **,
	    const struct gfarm_generic_info_ops *))
{
	int i;
	LDAPMod *modv[13];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];
	char mode_string[INT32STRLEN + 1];
	char atimespec_sec_string[INT32STRLEN + 1];
	char atimespec_nsec_string[INT32STRLEN + 1];
	char mtimespec_sec_string[INT32STRLEN + 1];
	char mtimespec_nsec_string[INT32STRLEN + 1];
	char ctimespec_sec_string[INT32STRLEN + 1];
	char ctimespec_nsec_string[INT32STRLEN + 1];
	char nsections_string[INT32STRLEN + 1];

	struct gfarm_path_info_key key;

	key.pathname = pathname;

	/*
	 * `info->pathname' doesn't have to be set,
	 * because this function uses its argument instead.
	 */
	sprintf(mode_string, "%07o", info->status.st_mode);
	sprintf(atimespec_sec_string, "%d", info->status.st_atimespec.tv_sec);
	sprintf(atimespec_nsec_string, "%d", info->status.st_atimespec.tv_nsec);
	sprintf(mtimespec_sec_string, "%d", info->status.st_mtimespec.tv_sec);
	sprintf(mtimespec_nsec_string, "%d", info->status.st_mtimespec.tv_nsec);
	sprintf(ctimespec_sec_string, "%d", info->status.st_ctimespec.tv_sec);
	sprintf(ctimespec_nsec_string, "%d", info->status.st_ctimespec.tv_nsec);
	sprintf(nsections_string, "%d", info->status.st_nsections);
	i = 0;
	set_string_mod(&modv[i], mod_op,
		       "objectclass", "GFarmPath", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "pathname", pathname, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "mode", mode_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "user", info->status.st_user, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "group", info->status.st_group, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "atimesec", atimespec_sec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "atimensec", atimespec_nsec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "mtimesec", mtimespec_sec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "mtimensec", mtimespec_nsec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "ctimesec", ctimespec_sec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "ctimensec", ctimespec_nsec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "nsections", nsections_string, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return ((*update_op)(&key, modv, &gfarm_path_info_ops));
}

void
gfarm_path_info_free(
	struct gfarm_path_info *info)
{
	if (info->pathname != NULL)
		free(info->pathname);
	if (info->status.st_user != NULL)
		free(info->status.st_user);
	if (info->status.st_group != NULL)
		free(info->status.st_group);
}

char *gfarm_path_info_get(
	const char *pathname,
	struct gfarm_path_info *info)
{
	struct gfarm_path_info_key key;

	/*
	 * This case intends to investigate the root directory.  Because
	 * Gfarm-1.0.x does not have an entry for the root directory, and
	 * moreover, because OpenLDAP-2.1.X does not accept a dn such as
	 * 'pathname=, dc=xxx', return immediately with an error.
	 */
	if (pathname[0] == '\0')
		return (GFARM_ERR_NO_SUCH_OBJECT);
	else
		key.pathname = pathname;

	return (gfarm_generic_info_get(&key, info,
	    &gfarm_path_info_ops));
}


char *
gfarm_path_info_set(
	char *pathname,
	struct gfarm_path_info *info)
{
	return (gfarm_path_info_update(pathname, info,
	    LDAP_MOD_ADD, gfarm_generic_info_set));
}

char *
gfarm_path_info_replace(
	char *pathname,
	struct gfarm_path_info *info)
{
	return (gfarm_path_info_update(pathname, info,
	    LDAP_MOD_REPLACE, gfarm_generic_info_modify));
}

char *
gfarm_path_info_remove(const char *pathname)
{
	struct gfarm_path_info_key key;

	key.pathname = pathname;

	return (gfarm_generic_info_remove(&key,
	    &gfarm_path_info_ops));
}

void
gfarm_path_info_free_all(
	int n,
	struct gfarm_path_info *infos)
{
	gfarm_generic_info_free_all(n, infos,
	    &gfarm_path_info_ops);
}

char *
gfarm_path_info_get_all(
	int *np,
	struct gfarm_path_info **infosp)
{
	char *error;
	int n;
	struct gfarm_path_info *infos;

	error = gfarm_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_path_info_ops.query_type,
	    &n, &infos,
	    &gfarm_path_info_ops);
	if (error != NULL)
		return (error);

	*np = n;
	*infosp = infos;
	return (NULL);
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
char *
gfarm_path_info_get_all_foreach(
	void (*callback)(void *, struct gfarm_path_info *),
	void *closure)
{
	struct gfarm_path_info tmp_info;

	return (gfarm_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_path_info_ops.query_type,
	    &tmp_info, /*XXX*/(void (*)(void *, void *))callback, closure,
	    &gfarm_path_info_ops));
}

void
gfarm_file_history_free_allfile(int n, char **v)
{
	gfarm_path_info_free_all(n, (struct gfarm_path_info *)v);
}

/* get GFarmFiles which were created by the program */
char *
gfarm_file_history_get_allfile_by_program(
	char *program,
	int *np,
	char ***gfarm_files_p)
{
	char *error;
	int n;
	struct gfarm_path_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmFile)(generatorProgram=%s))";
	char *query = malloc(sizeof(query_template) + strlen(program));

	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, program);
	error = gfarm_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, query,
	    &n, &infos,
	    &gfarm_path_info_ops);
	free(query);
	if (error != NULL)
		return (error);

	*np = n;
	*gfarm_files_p = (char **)infos;
	return (NULL);
}

/* get GFarmFiles which were created from the file as a input */
char *
gfarm_file_history_get_allfile_by_file(
	char *input_gfarm_file,
	int *np,
	char ***gfarm_files_p)
{
	char *error;
	int n;
	struct gfarm_path_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmFile)(generatorInputGFarmFiles=%s))";
	char *query = malloc(sizeof(query_template) +
			     strlen(input_gfarm_file));

	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, input_gfarm_file);
	error = gfarm_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, query,
	    &n, &infos,
	    &gfarm_path_info_ops);
	free(query);
	if (error != NULL)
		return (error);

	*np = n;
	*gfarm_files_p = (char **)infos;
	return (NULL);
}

/**********************************************************************/

static char *gfarm_file_section_info_make_dn(void *vkey);
static void gfarm_file_section_info_clear(void *info);
static void gfarm_file_section_info_set_field(void *info, char *attribute, char **vals);
static int gfarm_file_section_info_validate(void *info);

struct gfarm_file_section_info_key {
	const char *pathname;
	const char *section;
};

static const struct gfarm_generic_info_ops gfarm_file_section_info_ops = {
	sizeof(struct gfarm_file_section_info),
	"(objectclass=GFarmFileSection)",
	"section=%s, pathname=%s, %s",
	gfarm_file_section_info_make_dn,
	gfarm_file_section_info_clear,
	gfarm_file_section_info_set_field,
	gfarm_file_section_info_validate,
	(void (*)(void *))gfarm_file_section_info_free,
};

static char *
gfarm_file_section_info_make_dn(void *vkey)
{
	struct gfarm_file_section_info_key *key = vkey;
	char *escaped_pathname, *dn;

	escaped_pathname = gfarm_metadb_ldap_escape_pathname(key->pathname);
	if (escaped_pathname == NULL)
		return (NULL);

	dn = malloc(strlen(gfarm_file_section_info_ops.dn_template) +
		    strlen(key->section) + strlen(escaped_pathname) +
		    strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		free(escaped_pathname);
		return (NULL);
	}
	sprintf(dn, gfarm_file_section_info_ops.dn_template,
		key->section, escaped_pathname, gfarm_ldap_base_dn);
	free(escaped_pathname);
	return (dn);
}

static void
gfarm_file_section_info_clear(void *vinfo)
{
	struct gfarm_file_section_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static void
gfarm_file_section_info_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	struct gfarm_file_section_info *info = vinfo;

	if (strcasecmp(attribute, "pathname") == 0) {
		info->pathname = strdup(vals[0]);
	} else if (strcasecmp(attribute, "section") == 0) {
		info->section = strdup(vals[0]);
	} else if (strcasecmp(attribute, "filesize") == 0) {
		info->filesize = string_to_file_offset(vals[0], NULL);
	} else if (strcasecmp(attribute, "checksumType") == 0) {
		info->checksum_type = strdup(vals[0]);
	} else if (strcasecmp(attribute, "checksum") == 0) {
		info->checksum = strdup(vals[0]);
	}
}

static int
gfarm_file_section_info_validate(void *vinfo)
{
	struct gfarm_file_section_info *info = vinfo;

	/* XXX - should check all fields are filled */
	return (
	    info->section != NULL
	);
}

void
gfarm_file_section_info_free(struct gfarm_file_section_info *info)
{
	if (info->pathname != NULL)
		free(info->pathname);
	if (info->section != NULL)
		free(info->section);
	if (info->checksum_type != NULL)
		free(info->checksum_type);
	if (info->checksum != NULL)
		free(info->checksum);
}

char *gfarm_file_section_info_get(
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	struct gfarm_file_section_info_key key;

	key.pathname = pathname;
	key.section = section;

	return (gfarm_generic_info_get(&key, info,
	    &gfarm_file_section_info_ops));
}

char *
gfarm_file_section_info_set(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	int i;
	LDAPMod *modv[7];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];
	char filesize_string[INT64STRLEN + 1];

	struct gfarm_file_section_info_key key;

	key.pathname = pathname;
	key.section = section;

	/*
	 * `info->section' doesn't have to be set,
	 * because this function uses its argument instead.
	 */
	sprintf(filesize_string, "%" PR_FILE_OFFSET,
		CAST_PR_FILE_OFFSET info->filesize);
	i = 0;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "objectclass", "GFarmFileSection", &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "pathname", pathname, &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "section", section, &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "filesize", filesize_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "checksumType", info->checksum_type, &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "checksum", info->checksum, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return (gfarm_generic_info_set(&key, modv,
	    &gfarm_file_section_info_ops));
}

char *
gfarm_file_section_info_remove(
	const char *pathname,
	const char *section)
{
	struct gfarm_file_section_info_key key;

	key.pathname = pathname;
	key.section = section;

	return (gfarm_generic_info_remove(&key,
	    &gfarm_file_section_info_ops));
}

void
gfarm_file_section_info_free_all(
	int n,
	struct gfarm_file_section_info *infos)
{
	gfarm_generic_info_free_all(n, infos,
	    &gfarm_file_section_info_ops);
}

char *
gfarm_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	char *error;
	int n;
	struct gfarm_file_section_info *infos;
	static char dn_template[] = "pathname=%s, %s";
	char *escaped_pathname, *dn;

	escaped_pathname = gfarm_metadb_ldap_escape_pathname(pathname);
	if (escaped_pathname == NULL)
		return (NULL);

	dn = malloc(sizeof(dn_template) + strlen(escaped_pathname) +
		    strlen(gfarm_ldap_base_dn));
	if (dn == NULL) {
		free(escaped_pathname);
		return (NULL);
	}
	sprintf(dn, dn_template, escaped_pathname, gfarm_ldap_base_dn);
	free(escaped_pathname);
	error = gfarm_generic_info_get_all(dn, LDAP_SCOPE_ONELEVEL,
	    gfarm_file_section_info_ops.query_type,
	    &n, &infos,
	    &gfarm_file_section_info_ops);
	free(dn);
	if (error != NULL)
		return (error);
	*np = n;
	*infosp = infos;
	return (NULL);
}

static int
gfarm_file_section_info_compare_serial(const void *d, const void *s)
{
	const struct gfarm_file_section_info *df = d, *sf = s;

	return (atoi(df->section) - atoi(sf->section));
}

char *
gfarm_file_section_info_get_sorted_all_serial_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	int n;
	struct gfarm_file_section_info *infos;
	char *error = gfarm_file_section_info_get_all_by_file(
		pathname, &n, &infos);

	if (error != NULL)
		return (error);

	qsort(infos, n, sizeof(infos[0]),
	      gfarm_file_section_info_compare_serial);
	*np = n;
	*infosp = infos;
	return (NULL);
}

char *
gfarm_file_section_info_remove_all_by_file(const char *pathname)
{
	char *error, *error_save;
	int i, n;
	struct gfarm_file_section_info *infos;

	error = gfarm_file_section_info_get_all_by_file(pathname,
	    &n, &infos);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (NULL);
		return (error);
	}

	/*
	 * remove GfarmFileSection's
	 */
	error_save = NULL;
	for (i = 0; i < n; i++) {
		error = gfarm_file_section_info_remove(pathname,
		    infos[i].section);
		if (error != NULL && error != GFARM_ERR_NO_SUCH_OBJECT)
			error_save = error;
	}
	gfarm_file_section_info_free_all(n, infos);

	/* XXX - do not remove parent GFarmPath here */

	return (error_save);
}

/**********************************************************************/

static char *gfarm_file_section_copy_info_make_dn(void *vkey);
static void gfarm_file_section_copy_info_clear(void *info);
static void gfarm_file_section_copy_info_set_field(void *info, char *attribute, char **vals);
static int gfarm_file_section_copy_info_validate(void *info);

struct gfarm_file_section_copy_info_key {
	const char *pathname;
	const char *section;
	const char *hostname;
};

static const struct gfarm_generic_info_ops gfarm_file_section_copy_info_ops = {
	sizeof(struct gfarm_file_section_copy_info),
	"(objectclass=GFarmFileSectionCopy)",
	"hostname=%s, section=%s, pathname=%s, %s",
	gfarm_file_section_copy_info_make_dn,
	gfarm_file_section_copy_info_clear,
	gfarm_file_section_copy_info_set_field,
	gfarm_file_section_copy_info_validate,
	(void (*)(void *))gfarm_file_section_copy_info_free,
};

static char *
gfarm_file_section_copy_info_make_dn(void *vkey)
{
	struct gfarm_file_section_copy_info_key *key = vkey;
	char *escaped_pathname, *dn;

	escaped_pathname = gfarm_metadb_ldap_escape_pathname(key->pathname);
	if (escaped_pathname == NULL)
		return (NULL);

	dn = malloc(strlen(gfarm_file_section_copy_info_ops.dn_template) +
		    strlen(key->hostname) +
		    strlen(key->section) + strlen(escaped_pathname) +
		    strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		free(escaped_pathname);
		return (NULL);
	}
	sprintf(dn, gfarm_file_section_copy_info_ops.dn_template,
		key->hostname, key->section, escaped_pathname,
		gfarm_ldap_base_dn);
	free(escaped_pathname);
	return (dn);
}

static void
gfarm_file_section_copy_info_clear(void *vinfo)
{
	struct gfarm_file_section_copy_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static void
gfarm_file_section_copy_info_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	struct gfarm_file_section_copy_info *info = vinfo;

	if (strcasecmp(attribute, "pathname") == 0) {
		info->pathname = strdup(vals[0]);
	} else if (strcasecmp(attribute, "section") == 0) {
		info->section = strdup(vals[0]);
	} else if (strcasecmp(attribute, "hostname") == 0) {
		info->hostname = strdup(vals[0]);
	}
}

static int
gfarm_file_section_copy_info_validate(void *vinfo)
{
	struct gfarm_file_section_copy_info *info = vinfo;

	return (
	    info->pathname != NULL &&
	    info->section != NULL &&
	    info->hostname != NULL
	);
}

void
gfarm_file_section_copy_info_free(struct gfarm_file_section_copy_info *info)
{
	if (info->pathname != NULL)
		free(info->pathname);
	if (info->section != NULL)
		free(info->section);
	if (info->hostname != NULL)
		free(info->hostname);
}

char *
gfarm_file_section_copy_info_get(
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	struct gfarm_file_section_copy_info_key key;

	key.pathname = pathname;
	key.section = section;
	key.hostname = hostname;

	return (gfarm_generic_info_get(&key, info,
	    &gfarm_file_section_copy_info_ops));
}

char *
gfarm_file_section_copy_info_set(
	char *pathname,
	char *section,
	char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	int i;
	LDAPMod *modv[5];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	struct gfarm_file_section_copy_info_key key;

	key.pathname = pathname;
	key.section = section;
	key.hostname = hostname;

	/*
	 * `info->pathname', `info->section' and `info->hostname'
	 * don't have to be set,
	 * because this function uses its argument instead.
	 */
	i = 0;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "objectclass", "GFarmFileSectionCopy", &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "pathname", pathname, &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "section", section, &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "hostname", hostname, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return (gfarm_generic_info_set(&key, modv,
	    &gfarm_file_section_copy_info_ops));
}

char *
gfarm_file_section_copy_info_remove(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	struct gfarm_file_section_copy_info_key key;

	key.pathname = pathname;
	key.section = section;
	key.hostname = hostname;

	return (gfarm_generic_info_remove(&key,
	    &gfarm_file_section_copy_info_ops));
}

void
gfarm_file_section_copy_info_free_all(
	int n,
	struct gfarm_file_section_copy_info *infos)
{
	gfarm_generic_info_free_all(n, infos,
	    &gfarm_file_section_copy_info_ops);
}

char *
gfarm_file_section_copy_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *error;
	int n;
	struct gfarm_file_section_copy_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmFileSectionCopy)(pathname=%s))";
	char *query = malloc(sizeof(query_template) + strlen(pathname));

	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, pathname);
	error = gfarm_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_SUBTREE, query, &n, &infos,
	    &gfarm_file_section_copy_info_ops);
	free(query);
	if (error != NULL)
		return (error);
	*np = n;
	*infosp = infos;
	return (NULL);
}

char *
gfarm_file_section_copy_info_remove_all_by_file(
	const char *pathname)
{
	char *error, *error_save;
	int i, n;
	struct gfarm_file_section_copy_info *infos;

	error = gfarm_file_section_copy_info_get_all_by_file(pathname,
	    &n, &infos);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (NULL);
		return (error);
	}

	/*
	 * remove GFarmFileSectionCopies
	 */
	error_save = NULL;
	for (i = 0; i < n; i++) {
		error = gfarm_file_section_copy_info_remove(pathname,
		    infos[i].section, infos[i].hostname);
		if (error != NULL && error != GFARM_ERR_NO_SUCH_OBJECT)
			error_save = error;
	}
	gfarm_file_section_copy_info_free_all(n, infos);

	return (error_save);
}

char *
gfarm_file_section_copy_info_get_all_by_section(
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *error, *dn;
	int n;
	struct gfarm_file_section_info_key frag_info_key;
	struct gfarm_file_section_copy_info *infos;

	frag_info_key.pathname = pathname;
	frag_info_key.section = section;
	dn = (*gfarm_file_section_info_ops.make_dn)(&frag_info_key);
	if (dn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	error = gfarm_generic_info_get_all(dn, LDAP_SCOPE_ONELEVEL,
	    gfarm_file_section_copy_info_ops.query_type, &n, &infos,
	    &gfarm_file_section_copy_info_ops);
	free(dn);
	if (error != NULL)
		return (error);
	*np = n;
	*infosp = infos;
	return (NULL);
}

char *
gfarm_file_section_copy_info_remove_all_by_section(
	const char *pathname,
	const char *section)
{
	char *error, *error_save;
	int i, n;
	struct gfarm_file_section_copy_info *infos;

	error = gfarm_file_section_copy_info_get_all_by_section(
	    pathname, section,
	    &n, &infos);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (NULL);
		return (error);
	}

	/*
	 * remove GfarmFileSectionCopies
	 */
	error_save = NULL;
	for (i = 0; i < n; i++) {
		error = gfarm_file_section_copy_info_remove(pathname,
		    section, infos[i].hostname);
		if (error != NULL && error != GFARM_ERR_NO_SUCH_OBJECT)
			error_save = error;
	}
	gfarm_file_section_copy_info_free_all(n, infos);

	return (error_save);
}

char *
gfarm_file_section_copy_info_get_all_by_host(
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *error;
	int n;
	struct gfarm_file_section_copy_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmFileSectionCopy)(hostname=%s))";
	char *query = malloc(sizeof(query_template) + strlen(hostname));

	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, hostname);
	error = gfarm_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_SUBTREE, query, &n, &infos,
	    &gfarm_file_section_copy_info_ops);
	free(query);
	if (error != NULL)
		return (error);
	*np = n;
	*infosp = infos;
	return (NULL);
}

char *
gfarm_file_section_copy_info_remove_all_by_host(
	const char *hostname)
{
	char *error, *error_save;
	int i, n;
	struct gfarm_file_section_copy_info *infos;

	error = gfarm_file_section_copy_info_get_all_by_host(hostname,
	    &n, &infos);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (NULL);
		return (error);
	}

	/*
	 * remove GfarmFileSectionCopy's
	 */
	error_save = NULL;
	for (i = 0; i < n; i++) {
		error = gfarm_file_section_copy_info_remove(
		    infos[i].pathname, infos[i].section,
		    hostname);
		if (error != NULL && error != GFARM_ERR_NO_SUCH_OBJECT)
			error_save = error;
	}
	gfarm_file_section_copy_info_free_all(n, infos);

	return (error_save);
}

int
gfarm_file_section_copy_info_does_exist(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	struct gfarm_file_section_copy_info info;

	if (gfarm_file_section_copy_info_get(pathname, section,
	    hostname, &info) != NULL)
		return (0);
	gfarm_file_section_copy_info_free(&info);
	return (1);
}

/**********************************************************************/

static char *gfarm_file_history_make_dn(void *vkey);
static void gfarm_file_history_clear(void *info);
static void gfarm_file_history_set_field(void *info, char *attribute, char **vals);
static int gfarm_file_history_validate(void *info);

struct gfarm_file_history_key {
	char *gfarm_file;
};

static const struct gfarm_generic_info_ops gfarm_file_history_ops = {
	sizeof(struct gfarm_file_history),
	"(objectclass=GFarmFile)",
	"gfarmFile=%s, %s",
	gfarm_file_history_make_dn,
	gfarm_file_history_clear,
	gfarm_file_history_set_field,
	gfarm_file_history_validate,
	(void (*)(void *))gfarm_file_history_free,
};

static char *
gfarm_file_history_make_dn(void *vkey)
{
	struct gfarm_file_history_key *key = vkey;
	char *dn = malloc(strlen(gfarm_file_history_ops.dn_template) +
			  strlen(key->gfarm_file) +
			  strlen(gfarm_ldap_base_dn) + 1);

	if (dn == NULL)
		return (NULL);
	sprintf(dn, gfarm_file_history_ops.dn_template,
		key->gfarm_file, gfarm_ldap_base_dn);
	return (dn);
}

static void
gfarm_file_history_clear(void *vinfo)
{
	struct gfarm_file_history *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static void
gfarm_file_history_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	struct gfarm_file_history *info = vinfo;

	if (strcasecmp(attribute, "generatorProgram") == 0) {
		info->program = strdup(vals[0]);
	} else if (strcasecmp(attribute, "generatorInputGFarmFiles") == 0) {
		info->input_files = gfarm_strarray_dup(vals);
	} else if (strcasecmp(attribute, "generatorParameter") == 0) {
		info->parameter = strdup(vals[0]);
	}
}

static int
gfarm_file_history_validate(void *vinfo)
{
	struct gfarm_file_history *info = vinfo;

	return (
	    info->program != NULL &&
	    info->input_files != NULL &&
	    info->parameter != NULL
	);
}

void
gfarm_file_history_free(
	struct gfarm_file_history *info)
{
	if (info->program != NULL)
		free(info->program);
	if (info->input_files != NULL)
		gfarm_strarray_free(info->input_files);
	if (info->parameter != NULL)
		free(info->parameter);
}

char *gfarm_file_history_get(
	char *gfarm_file,
	struct gfarm_file_history *info)
{
	struct gfarm_file_history_key key;

	key.gfarm_file = gfarm_file;

	return (gfarm_generic_info_get(&key, info,
	    &gfarm_file_history_ops));
}

char *
gfarm_file_history_set(
	char *gfarm_file,
	struct gfarm_file_history *info)
{
	int i;
	LDAPMod *modv[4];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	LDAPMod input_files_mod;

	struct gfarm_file_history_key key;

	key.gfarm_file = gfarm_file;

	i = 0;
#if 0 /* objectclass should be already set */
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "objectclass", "GFarmFile", &storage[i]);
	i++;
#endif
	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "generatorProgram", info->program, &storage[i]);
	i++;

	input_files_mod.mod_op = LDAP_MOD_ADD;
	input_files_mod.mod_type = "generatorInputGFarmFiles";
	input_files_mod.mod_vals.modv_strvals = info->input_files;
	modv[i] = &input_files_mod;
	i++;

	set_string_mod(&modv[i], LDAP_MOD_ADD,
		       "generatorParameter", info->parameter, &storage[i]);
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return (gfarm_generic_info_set(&key, modv,
	    &gfarm_file_history_ops));
}

char *
gfarm_file_history_remove(char *gfarm_file)
{
	struct gfarm_file_history_key key;

	key.gfarm_file = gfarm_file;

	return (gfarm_generic_info_remove(&key,
	    &gfarm_file_history_ops));
}
