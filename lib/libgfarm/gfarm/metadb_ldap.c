/*
 * $Id$
 */

#include <sys/time.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define LDAP_DEPRECATED 1 /* export deprecated functions on OpenLDAP-2.3 */
#include <lber.h>
#include <ldap.h>

#include <gfarm/gfarm.h>

#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_OPT_X_TLS_CTX)
#define	OPENLDAP_TLS_USABLE
#define OPENSSL_NO_KRB5 /* XXX - disabled for now to avoid conflict with GSI */
#else
#undef	OPENLDAP_TLS_USABLE
#undef  OPENSSL_NO_KRB5
#endif

#ifdef OPENLDAP_TLS_USABLE
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifndef LDAP_PORT
#define LDAP_PORT	389
#endif
#ifndef LDAPS_PORT
#define LDAPS_PORT	636	/* ldap over SSL/TLS */
#endif

#include "gfutil.h"

#include "gfpath.h"
#include "config.h"
#include "metadb_access.h"
#include "metadb_sw.h"

/* old openldap does not have ldap_memfree. */
#ifndef HAVE_LDAP_MEMFREE
#define	ldap_memfree(a)
#endif /* HAVE_LDAP_MEMFREE */

#define INT32STRLEN	GFARM_INT32STRLEN
#define INT64STRLEN	GFARM_INT64STRLEN
#define ARRAY_LENGTH(array)	GFARM_ARRAY_LENGTH(array)

/**********************************************************************/

static LDAP *gfarm_ldap_server = NULL;

static char *
gfarm_ldap_sanity(void)
{
	int rv;
	LDAPMessage *res = NULL;
	char *e = NULL;

	if (gfarm_ldap_server == NULL)
		return ("metadb connection already disconnected");

	rv = ldap_search_s(gfarm_ldap_server, gfarm_ldap_base_dn,
	    LDAP_SCOPE_BASE, "objectclass=top", NULL, 0, &res);
	if (rv != LDAP_SUCCESS) {
		switch (rv) {
		case LDAP_SERVER_DOWN:
			e = "can't contact gfarm meta-db server";
			break;
		case LDAP_NO_SUCH_OBJECT:
			e = "gfarm meta-db ldap_base_dn not found";
			break;
		default:
			e = "gfarm meta-db ldap_base_dn access failed";
		}
	}
	if (res != NULL)
		ldap_msgfree(res);
	return (e);
}

#ifdef OPENLDAP_TLS_USABLE /* OpenLDAP dependent SSL/TLS handling */

/*
 * The reason we do this is because OpenLDAP's handling of the following
 * options are fragile:
 *	LDAP_OPT_X_TLS_CIPHER_SUITE
 *	LDAP_OPT_X_TLS_CACERTDIR
 *	LDAP_OPT_X_TLS_KEYFILE
 *	LDAP_OPT_X_TLS_CERTFILE
 *	LDAP_OPT_X_TLS_REQUIRE_CERT
 *
 * These options affect global state, and need to be set before ldap_init().
 * Furthermore, these options only affect at initialization phase of
 * OpenLDAP's default SSL context (libraries/libldap/tls.c:tls_def_ctx)
 * at least in openldap-2.2.27.
 * Thus, if there is any other user of these options in the same process,
 * these options don't work (for gfarm if other uses the options first,
 * or for other user if gfarm uses the options first).
 * To avoid such problem, we have to set the default SSL context by
 * using LDAP_OPT_X_TLS_CTX and OpenSSL functions, instead of the OpenLDAP's
 * default one.
 */

/*
 * Initialize SSL context, but only for things needed by clients.
 *
 * This function is nearly equivalent to ldap_pvt_tls_init_def_ctx()
 * in libraries/libldap/tls.c:tls_def_ctx of openldap-2.2.27,
 * but only does client side initialization.
 *
 * XXX make this possible to use Globus LDAP certificate ("CN=ldap/HOSTNAME")
 */
static char *
gfarm_ldap_new_default_ctx(SSL_CTX **ctxp)
{
	char *e;
	SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());

	if (ctx == NULL)
		return ("LDAP: cannot allocate SSL/TLS context");

	/*
	 * The following operation is nearly equivalent to:
	 * rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_CIPHER_SUITE,
	 *     gfarm_ldap_tls_cipher_suite);
	 */
	if (gfarm_ldap_tls_cipher_suite != NULL &&
	    !SSL_CTX_set_cipher_list(ctx, gfarm_ldap_tls_cipher_suite)) {
		e = "cannot set ldap_tls_cipher_suite";
		goto error;
	}

	/*
	 * The following operation is nearly equivalent to:
	 * rv = ldap_set_option(NULL,LDAP_OPT_X_TLS_CACERTDIR,GRID_CACERT_DIR);
	 */
	if (!SSL_CTX_load_verify_locations(ctx, NULL, GRID_CACERT_DIR)) {
		e = "cannot use " GRID_CACERT_DIR
		    " for LDAP TLS certificates directory";
		goto error;
	} else if (!SSL_CTX_set_default_verify_paths(ctx)) {
		e = "failed to verify " GRID_CACERT_DIR
		    " for LDAP TLS certificates directory";
		goto error;
	}

	/*
	 * The following operation is nearly equivalent to:
	 * rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_KEYFILE,
	 *     gfarm_ldap_tls_certificate_key_file);
	 */
	if (gfarm_ldap_tls_certificate_key_file != NULL &&
	    !SSL_CTX_use_PrivateKey_file(ctx,
	    gfarm_ldap_tls_certificate_key_file, SSL_FILETYPE_PEM)) {
		e = "failed to use ldap_tls_certificate_key_file";
		goto error;
	}

	/*
	 * The following operation is nearly equivalent to:
	 * rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_CERTFILE,
	 *     gfarm_ldap_tls_certificate_file);
	 */
	if (gfarm_ldap_tls_certificate_file != NULL &&
	    !SSL_CTX_use_certificate_file(ctx,
	    gfarm_ldap_tls_certificate_file, SSL_FILETYPE_PEM)) {
		e = "failed to use ldap_tls_certificate_file";
		goto error;
	}

	if ((gfarm_ldap_tls_certificate_key_file != NULL ||
	     gfarm_ldap_tls_certificate_file != NULL) &&
	    !SSL_CTX_check_private_key(ctx)) {
		e = "ldap_tls_certificate_file/key_file check failure";
		goto error;
	}

	/*
	 * The following operation is nearly equivalent to:
	 * tls = LDAP_OPT_X_TLS_HARD;
	 * rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_REQUIRE_CERT, &tls);
	 */
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

	*ctxp = ctx;
	return (NULL);

error:
	SSL_CTX_free(ctx);
	if (gflog_auth_get_verbose()) {
		if (!ERR_peek_error()) {
			gflog_error("%s", e);
		} else {
			gflog_error("%s, because:", e);
			do {
				gflog_error("%s", ERR_error_string(
				    ERR_get_error(), NULL));
			} while (ERR_peek_error());
		}
	}
	return (e);

}

static SSL_CTX *ldap_ssl_context = NULL;
static SSL_CTX *ldap_ssl_default_context = NULL;

static char *
gfarm_ldap_set_ssl_context(void)
{
	int rv;
	
	if (ldap_ssl_context == NULL) {
		char *e = gfarm_ldap_new_default_ctx(&ldap_ssl_context);

		if (e != NULL)
			return (e);
	}

	rv = ldap_get_option(NULL, LDAP_OPT_X_TLS_CTX,
	    &ldap_ssl_default_context);
	if (rv != LDAP_SUCCESS) {
		gflog_auth_error("LDAP get default SSL/TLS context: %s",
		    ldap_err2string(rv));
		return (ldap_err2string(rv));
	}

	rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_CTX, ldap_ssl_context);
	if (rv != LDAP_SUCCESS) {
		gflog_auth_error("LDAP set default SSL/TLS context: %s",
		    ldap_err2string(rv));
		return (ldap_err2string(rv));
	}
	return (NULL);
}

static char *
gfarm_ldap_restore_ssl_context(void)
{
	int rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_CTX,
	    ldap_ssl_default_context);

	if (rv != LDAP_SUCCESS) {
		gflog_auth_error("LDAP restore default SSL/TLS context: %s",
		    ldap_err2string(rv));
		return (ldap_err2string(rv));
	}
	return (NULL);
}

static char *
gfarm_ldap_switch_ssl_context(LDAP *ld)
{
	int rv;

	gfarm_ldap_restore_ssl_context();

	rv = ldap_set_option(ld, LDAP_OPT_X_TLS_CTX, ldap_ssl_context);

	if (rv != LDAP_SUCCESS) {
		gflog_auth_error("LDAP set SSL/TLS context: %s",
		    ldap_err2string(rv));
		return (ldap_err2string(rv));
	}
	return (NULL);
}

#else
#define gfarm_ldap_restore_ssl_context()
#define gfarm_ldap_switch_ssl_context(ld)
#endif /* OPENLDAP_TLS_USABLE */

static char *
gfarm_ldap_initialize(void)
{
	enum { LDAP_WITHOUT_TLS, LDAP_WITH_TLS, LDAP_WITH_START_TLS }
		tls_mode = LDAP_WITHOUT_TLS;
	int rv, port;
#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_VERSION3)
	int version;
#endif
#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_OPT_NETWORK_TIMEOUT)
	struct timeval timeout = { 5, 0 };
#endif
#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_OPT_X_TLS)
	int tls;
#endif
	char *e;

	/* we need to check "ldap_tls" at first to see default port number */
	if (gfarm_ldap_tls != NULL) {
		if (strcasecmp(gfarm_ldap_tls, "false") == 0) {
			tls_mode = LDAP_WITHOUT_TLS;
		} else if (strcasecmp(gfarm_ldap_tls, "true") == 0) {
			tls_mode = LDAP_WITH_TLS;
		} else if (strcasecmp(gfarm_ldap_tls, "start_tls") == 0) {
			tls_mode = LDAP_WITH_START_TLS;
		} else {
			return ("gfarm.conf: ldap_tls: unknown keyword");
		}
	}
	/* sanity check */
	if (gfarm_ldap_server_name == NULL)
		return ("gfarm.conf: ldap_serverhost is missing");
	if (gfarm_ldap_server_port == NULL) {
		if (tls_mode == LDAP_WITH_TLS)
			port = LDAPS_PORT;
		else
			port = LDAP_PORT;
	} else {
		port = strtol(gfarm_ldap_server_port, &e, 0);
		if (e == gfarm_ldap_server_port || port <= 0 || port >= 65536)
			return ("gfarm.conf: ldap_serverport: "
				"illegal value");
	}
	if (gfarm_ldap_base_dn == NULL)
		return ("gfarm.conf: ldap_base_dn is missing");

	/*
	 * initialize LDAP
	 */

	/* open a connection */
	gfarm_ldap_server = ldap_init(gfarm_ldap_server_name, port);
	if (gfarm_ldap_server == NULL) {
		/* ldap_init() defers actual connect(2) operation later */
		gflog_auth_error("ldap_init: %s", strerror(errno));
		return ("gfarm meta-db ldap_server access failed");
	}

	if (tls_mode != LDAP_WITHOUT_TLS) {
#ifdef OPENLDAP_TLS_USABLE
		e = gfarm_ldap_set_ssl_context();
		if (e != NULL) {
			(void)gfarm_metadb_terminate();
			return (e);
		}
#else
		(void)gfarm_metadb_terminate();
		return ("gfarm.conf: \"ldap_tls\" is specified, but "
		    "the LDAP library linked with gfarm doesn't support it");
#endif
	}


#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_VERSION3)
	version = LDAP_VERSION3;
	ldap_set_option(gfarm_ldap_server, LDAP_OPT_PROTOCOL_VERSION, &version);
#endif

	ldap_set_option(gfarm_ldap_server, LDAP_OPT_REFERRALS, LDAP_OPT_ON);

#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_OPT_NETWORK_TIMEOUT)
	ldap_set_option(gfarm_ldap_server, LDAP_OPT_NETWORK_TIMEOUT,
	    (void *)&timeout);
#endif

	if (tls_mode == LDAP_WITH_TLS) {
#if defined(HAVE_LDAP_SET_OPTION) && defined(LDAP_OPT_X_TLS)
		tls = LDAP_OPT_X_TLS_HARD;
		rv = ldap_set_option(gfarm_ldap_server, LDAP_OPT_X_TLS, &tls);
		if (rv != LDAP_SUCCESS) {
			gflog_auth_error("LDAP use SSL/TLS: %s",
			    ldap_err2string(rv));
#ifdef HAVE_LDAP_PERROR
			if (gflog_auth_get_verbose()) {
				/* XXX this can only output to stderr */
				ldap_perror(gfarm_ldap_server,
				    "ldap_start_tls");
			}
#endif
			gfarm_ldap_restore_ssl_context();
			(void)gfarm_metadb_terminate();
			return (ldap_err2string(rv));
		}
#else
		gfarm_ldap_restore_ssl_context();
		(void)gfarm_metadb_terminate();
		return ("gfarm.conf: \"ldap_tls true\" is specified, but "
		    "the LDAP library linked with gfarm doesn't support it");
#endif /* defined(LDAP_OPT_X_TLS) && defined(HAVE_LDAP_SET_OPTION) */
	}

	if (tls_mode == LDAP_WITH_START_TLS) {
#if defined(HAVE_LDAP_START_TLS_S) && defined(HAVE_LDAP_SET_OPTION)
		rv = ldap_start_tls_s(gfarm_ldap_server, NULL, NULL);
		if (rv != LDAP_SUCCESS) {
			gflog_auth_error("LDAP start TLS: %s",
			    ldap_err2string(rv));
#ifdef HAVE_LDAP_PERROR
			if (gflog_auth_get_verbose()) {
				/* XXX this cannot output to syslog */
				ldap_perror(gfarm_ldap_server,
				    "ldap_start_tls");
			}
#endif
			gfarm_ldap_restore_ssl_context();
			(void)gfarm_metadb_terminate();
			return ("gfarm meta-db ldap_server TLS access failed");
		}
#else
		gfarm_ldap_restore_ssl_context();
		(void)gfarm_metadb_terminate();
		return ("gfarm.conf: \"ldap_tls start_tls\" is specified, but "
		    "the LDAP library linked with gfarm doesn't support it");
#endif /* defined(HAVE_LDAP_START_TLS_S) && defined(HAVE_LDAP_SET_OPTION) */
	}

	/* gfarm_ldap_bind_dn and gfarm_ldap_bind_password may be NULL */
	rv = ldap_simple_bind_s(gfarm_ldap_server,
	    gfarm_ldap_bind_dn, gfarm_ldap_bind_password);
	if (rv != LDAP_SUCCESS) {
		gflog_auth_error("LDAP simple bind: %s", ldap_err2string(rv));
		if (tls_mode != LDAP_WITHOUT_TLS)
			gfarm_ldap_restore_ssl_context();
		(void)gfarm_metadb_terminate();
		return (ldap_err2string(rv));
	}

	/* sanity check. base_dn can be accessed? */
	e = gfarm_ldap_sanity();
	if (e != NULL) {
		if (tls_mode != LDAP_WITHOUT_TLS)
			gfarm_ldap_restore_ssl_context();
		(void)gfarm_metadb_terminate();
		return (e);
	}

	if (tls_mode != LDAP_WITHOUT_TLS)
		gfarm_ldap_switch_ssl_context(gfarm_ldap_server);
	return (NULL);
}

static char *
gfarm_ldap_terminate(void)
{
	int rv;
	char *e = NULL;

	if (gfarm_ldap_server == NULL)
		return ("metadb connection already disconnected");

	/* close and free connection resources */
	if (gfarm_does_own_metadb_connection()) {
		rv = ldap_unbind(gfarm_ldap_server);
		if (rv != LDAP_SUCCESS)
			e = ldap_err2string(rv);
	}
	gfarm_ldap_server = NULL;

	return (e);
}

/*
 * LDAP connection cannot be used from forked children unless
 * the connection is ensured to be used exclusively.
 * This routine guarantees that never happens.
 * NOTE:
 * This is where gfarm_metadb_initialize() is called from.
 * Nearly every interface functions must call this function.
 */
static char *
gfarm_ldap_check(void)
{
	/*
	 * if there is a valid LDAP connection, return.  If not,
	 * create a new connection.
	 */
	if (gfarm_ldap_server != NULL && gfarm_does_own_metadb_connection())
		return (NULL);
	/* XXX close the file descriptor for gfarm_ldap_server, but how? */
	gfarm_ldap_server = NULL;
	return (gfarm_metadb_initialize());
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

struct gfarm_ldap_generic_info_ops {
	const struct gfarm_base_generic_info_ops *gen_ops;
	char *query_type;
	char *dn_template;
	char *(*make_dn)(void *key);
	void (*set_field)(void *info, char *attribute, char **vals);
};

static char *
gfarm_ldap_generic_info_get(
	void *key,
	void *info,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	LDAPMessage *res, *e;
	int n, rv;
	char *a, *dn, *error;
	BerElement *ber;
	char **vals;

	if ((error = gfarm_ldap_check()) != NULL)
		return (error);
retry:
	dn = ops->make_dn(key);
	if (dn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	res = NULL;
	rv = ldap_search_s(gfarm_ldap_server, dn,
	    LDAP_SCOPE_BASE, ops->query_type, NULL, 0, &res);
	free(dn);
	if (rv != LDAP_SUCCESS) {
		switch (rv) {
		case LDAP_SERVER_DOWN:
			error = gfarm_metadb_initialize();
			if (error == NULL)
				goto retry;
			break;
		case LDAP_NO_SUCH_OBJECT:
			error = GFARM_ERR_NO_SUCH_OBJECT;
			break;
		default:
			error = ldap_err2string(rv);
		}
		goto msgfree;
	}
	n = ldap_count_entries(gfarm_ldap_server, res);
	if (n == 0) {
		error = GFARM_ERR_NO_SUCH_OBJECT;
		goto msgfree;
	}
	ops->gen_ops->clear(info);
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

	/* should check all fields are filled */
	if (!ops->gen_ops->validate(info)) {
		ops->gen_ops->free(info);
		/* XXX - different error code is better ? */
		error = GFARM_ERR_NO_SUCH_OBJECT;
	}
msgfree:
	/* free the search results */
	if (res != NULL)
		ldap_msgfree(res);

	return (error); /* success */
}

static char *
gfarm_ldap_generic_info_set(
	void *key,
	LDAPMod **modv,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	int rv;
	char *dn, *error;

	if ((error = gfarm_ldap_check()) != NULL)
		return (error);

	dn = ops->make_dn(key);
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

static char *
gfarm_ldap_generic_info_modify(
	void *key,
	LDAPMod **modv,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	int rv;
	char *dn, *error;

	if ((error = gfarm_ldap_check()) != NULL)
		return (error);

	dn = ops->make_dn(key);
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

static char *
gfarm_ldap_generic_info_remove(
	void *key,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	int rv;
	char *dn, *error;

	if ((error = gfarm_ldap_check()) != NULL)
		return (error);

	dn = ops->make_dn(key);
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

static char *
gfarm_ldap_generic_info_get_all(
	char *dn,
	int scope, /* LDAP_SCOPE_ONELEVEL or LDAP_SCOPE_SUBTREE */
	char *query,
	int *np,
	void *infosp,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	LDAPMessage *res, *e;
	int i, n, rv;
	char *a;
	BerElement *ber;
	char **vals;
	char *infos = NULL, *tmp_info;
	char *error;
	size_t size;
	int overflow = 0;

	if ((error = gfarm_ldap_check()) != NULL)
		return (error);
	/* search for entries, return all attrs  */
retry:
	res = NULL;
	rv = ldap_search_s(gfarm_ldap_server, dn, scope, query, NULL, 0, &res);
	if (rv != LDAP_SUCCESS) {
		switch (rv) {
		case LDAP_SERVER_DOWN:
			error = gfarm_metadb_initialize();
			if (error == NULL)
				goto retry;
			break;
		case LDAP_NO_SUCH_OBJECT:
			error = GFARM_ERR_NO_SUCH_OBJECT;
			break;
		default:
			error = ldap_err2string(rv);
		}
		goto msgfree;
	}
	n = ldap_count_entries(gfarm_ldap_server, res);
	if (n == 0) {
		error = GFARM_ERR_NO_SUCH_OBJECT;
		goto msgfree;
	}
	size = gfarm_size_mul(&overflow, ops->gen_ops->info_size, n);
	if (overflow)
		errno = ENOMEM;
	else
		GFARM_MALLOC_ARRAY(infos, size);
	if (infos == NULL) {
		error = GFARM_ERR_NO_MEMORY;
		goto msgfree;
	}

	/* use last element as temporary buffer */
	tmp_info = infos + ops->gen_ops->info_size * (n - 1);

	/* step through each entry returned */
	for (i = 0, e = ldap_first_entry(gfarm_ldap_server, res); e != NULL;
	    e = ldap_next_entry(gfarm_ldap_server, e)) {

		ops->gen_ops->clear(tmp_info);

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

		if (!ops->gen_ops->validate(tmp_info)) {
			/* invalid record */
			ops->gen_ops->free(tmp_info);
			continue;
		}
		if (i < n - 1) { /* if (i == n - 1), do not have to copy */
			memcpy(infos + ops->gen_ops->info_size * i, tmp_info,
			       ops->gen_ops->info_size);
		}
		i++;
	}
	if (i == 0) {
		free(infos);
		/* XXX - data were all invalid */
		error = GFARM_ERR_NO_SUCH_OBJECT;
		goto msgfree;
	}
	/* XXX - if (i < n), element from (i+1) to (n-1) may be wasted */
	*np = i;
	*(char **)infosp = infos;
msgfree:
	/* free the search results */
	if (res != NULL)
		ldap_msgfree(res);

	return (error);
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
static char *
gfarm_ldap_generic_info_get_foreach(
	char *dn,
	int scope, /* LDAP_SCOPE_ONELEVEL or LDAP_SCOPE_SUBTREE */
	char *query,
	void *tmp_info, /* just used as a work area */
	void (*callback)(void *, void *),
	void *closure,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	LDAPMessage *res, *e;
	int i, msgid, rv;
	char *a;
	BerElement *ber;
	char **vals;
	char *error;

	if ((error = gfarm_ldap_check()) != NULL)
		return (error);
	/* search for entries asynchronously */
	msgid = ldap_search(gfarm_ldap_server, dn, scope, query, NULL, 0);
	if (msgid == -1)
		return ("ldap_search: error");

	/* step through each entry returned */
	i = 0;
	res = NULL;
	while ((rv = ldap_result(gfarm_ldap_server,
			msgid, LDAP_MSG_ONE, NULL, &res)) > 0) {
		e = ldap_first_entry(gfarm_ldap_server, res);
		if (e == NULL)
			break;
		for (; e != NULL; e = ldap_next_entry(gfarm_ldap_server, e)) {

			ops->gen_ops->clear(tmp_info);

			ber = NULL;
			for (a = ldap_first_attribute(
				     gfarm_ldap_server, e, &ber);
			     a != NULL;
			     a = ldap_next_attribute(
				     gfarm_ldap_server, e, ber)) {
				vals = ldap_get_values(gfarm_ldap_server, e, a);
				if (vals[0] != NULL)
					ops->set_field(tmp_info, a, vals);
				ldap_value_free(vals);
				ldap_memfree(a);
			}
			if (ber != NULL)
				ber_free(ber, 0);

			if (!ops->gen_ops->validate(tmp_info)) {
				/* invalid record */
				ops->gen_ops->free(tmp_info);
				continue;
			}
			(*callback)(closure, tmp_info);
			ops->gen_ops->free(tmp_info);
			i++;
		}
		/* free the search results */
		ldap_msgfree(res);
		res = NULL;
	}
	if (res != NULL)
		ldap_msgfree(res);
	if (rv == -1)
		return ("ldap_result: error");

	if (i == 0)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	return (NULL);
}

/**********************************************************************/

static char *gfarm_ldap_host_info_make_dn(void *vkey);
static void gfarm_ldap_host_info_set_field(void *info, char *attribute,
	char **vals);

struct gfarm_ldap_host_info_key {
	const char *hostname;
};

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_host_info_ops = {
	&gfarm_base_host_info_ops,
	"(objectclass=GFarmHost)",
	"hostname=%s, %s",
	gfarm_ldap_host_info_make_dn,
	gfarm_ldap_host_info_set_field,
};

static char *
gfarm_ldap_host_info_make_dn(void *vkey)
{
	struct gfarm_ldap_host_info_key *key = vkey;
	char *dn;

	GFARM_MALLOC_ARRAY(dn, strlen(gfarm_ldap_host_info_ops.dn_template) +
			  strlen(key->hostname) +
			  strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL)
		return (NULL);
	sprintf(dn, gfarm_ldap_host_info_ops.dn_template,
		key->hostname, gfarm_ldap_base_dn);
	return (dn);
}

static void
gfarm_ldap_host_info_set_field(
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

static char *
gfarm_ldap_host_info_get(
	const char *hostname,
	struct gfarm_host_info *info)
{
	struct gfarm_ldap_host_info_key key;

	key.hostname = hostname;

	return (gfarm_ldap_generic_info_get(&key, info,
	    &gfarm_ldap_host_info_ops));
}

static char *
gfarm_ldap_host_info_update(
	char *hostname,
	struct gfarm_host_info *info,
	int mod_op,
	char *(*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[6];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];
	char ncpu_string[INT32STRLEN + 1];

	LDAPMod hostaliases_mod;

	struct gfarm_ldap_host_info_key key;

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

	return ((*update_op)(&key, modv, &gfarm_ldap_host_info_ops));
}

static char *
gfarm_ldap_host_info_remove_hostaliases(const char *hostname)
{
	int i;
	LDAPMod *modv[2];
	LDAPMod hostaliases_mod;

	struct gfarm_ldap_host_info_key key;

	key.hostname = hostname;

	i = 0;

	hostaliases_mod.mod_type = "hostalias";
	hostaliases_mod.mod_op = LDAP_MOD_DELETE;
	hostaliases_mod.mod_vals.modv_strvals = NULL;
	modv[i] = &hostaliases_mod;
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return (gfarm_ldap_generic_info_modify(&key, modv,
	    &gfarm_ldap_host_info_ops));
}

static char *
gfarm_ldap_host_info_set(
	char *hostname,
	struct gfarm_host_info *info)
{
	return (gfarm_ldap_host_info_update(hostname, info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_set));
}

static char *
gfarm_ldap_host_info_replace(
	char *hostname,
	struct gfarm_host_info *info)
{
	return (gfarm_ldap_host_info_update(hostname, info,
	    LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify));
}

static char *
gfarm_ldap_host_info_remove(const char *hostname)
{
	struct gfarm_ldap_host_info_key key;

	key.hostname = hostname;

	return (gfarm_ldap_generic_info_remove(&key,
	    &gfarm_ldap_host_info_ops));
}

static char *
gfarm_ldap_host_info_get_all(
	int *np,
	struct gfarm_host_info **infosp)
{
	char *error;
	int n;
	struct gfarm_host_info *infos;

	error = gfarm_ldap_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_ldap_host_info_ops.query_type,
	    &n, &infos,
	    &gfarm_ldap_host_info_ops);
	if (error != NULL)
		return (error);

	*np = n;
	*infosp = infos;
	return (NULL);
}

static char *
gfarm_ldap_host_info_get_by_name_alias(
	const char *name_alias,
	struct gfarm_host_info *info)
{
	char *error;
	int n;
	struct gfarm_host_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmHost)(|(hostname=%s)(hostalias=%s)))";
	char *query = NULL;
	size_t size;
	int overflow = 0;

	size = gfarm_size_add(&overflow, sizeof(query_template),
			gfarm_size_mul(&overflow, strlen(name_alias), 2));
	if (overflow)
		errno = ENOMEM;
	else
		GFARM_MALLOC_ARRAY(query, size);
	if (overflow || query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, name_alias, name_alias);
	error = gfarm_ldap_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, query,
	    &n, &infos,
	    &gfarm_ldap_host_info_ops);
	free(query);
	if (error != NULL) {
		if (error == GFARM_ERR_NO_SUCH_OBJECT)
			return (GFARM_ERR_UNKNOWN_HOST);
		return (error);
	}

	if (n != 1) {
		gfarm_metadb_host_info_free_all(n, infos);
		return (GFARM_ERR_AMBIGUOUS_RESULT);
	}
	*info = infos[0];
	free(infos);
	return (NULL);
}

static char *
gfarm_ldap_host_info_get_allhost_by_architecture(const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	char *error;
	int n;
	struct gfarm_host_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmHost)(architecture=%s))";
	char *query;

	GFARM_MALLOC_ARRAY(query,
		sizeof(query_template) + strlen(architecture));
	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, architecture);
	error = gfarm_ldap_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, query,
	    &n, &infos,
	    &gfarm_ldap_host_info_ops);
	free(query);
	if (error != NULL)
		return (error);

	*np = n;
	*infosp = infos;
	return (NULL);
}

/**********************************************************************/

static char *gfarm_ldap_path_info_make_dn(void *vkey);
static void gfarm_ldap_path_info_set_field(void *info, char *attribute,
	char **vals);

struct gfarm_ldap_path_info_key {
	const char *pathname;
};

static int
gfarm_ldap_need_escape(char c)
{
	/* According to RFC 2253 (Section 2.4 and 3), following characters 
	 * must be escaped.
	 * Note: '#' should also be escaped. But it seems to be unnecessary
	 *       when using OpenLDAP 2.2.x.
	 */
	switch (c) {
	case ',': case '+': case '"': case '\\':
	case '<': case '>': case ';': case '=':
		return (1);
	}
	return (0);
}

static char *
gfarm_ldap_escape_pathname(const char *pathname)
{
	const char *s = pathname;
	char *escaped_pathname = NULL, *d;
	size_t size;
	int overflow = 0;

	/* if pathname is a null string, return immediately */
	if (*s == '\0')
		return (NULL);
	
	size = gfarm_size_mul(&overflow, strlen(pathname), 3);
	if (overflow)
		errno = ENOMEM;
	else
		GFARM_MALLOC_ARRAY(escaped_pathname, size);
	if (escaped_pathname == NULL)
		return (escaped_pathname);

	d = escaped_pathname;
	/* Escape the first character; ' ', '#', and need_escape(). */
	if (*s == ' ' || *s == '#' || gfarm_ldap_need_escape(*s))
		*d++ = '\\';
	*d++ = *s++;
	while (*s) {
		if (gfarm_ldap_need_escape(*s) ||
		    (*s == ' ' && d[-1] == ' '))
			*d++ = '\\';
		*d++ = *s++;
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

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_path_info_ops = {
	&gfarm_base_path_info_ops,
	"(objectclass=GFarmPath)",
	"pathname=%s, %s",
	gfarm_ldap_path_info_make_dn,
	gfarm_ldap_path_info_set_field,
};

static char *
gfarm_ldap_path_info_make_dn(void *vkey)
{
	struct gfarm_ldap_path_info_key *key = vkey;
	char *escaped_pathname, *dn;

	escaped_pathname = gfarm_ldap_escape_pathname(key->pathname);
	if (escaped_pathname == NULL)
		return (NULL);

	GFARM_MALLOC_ARRAY(dn, strlen(gfarm_ldap_path_info_ops.dn_template) +
		    strlen(escaped_pathname) + strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		free(escaped_pathname);
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_path_info_ops.dn_template,
		escaped_pathname, gfarm_ldap_base_dn);
	free(escaped_pathname);
	return (dn);
}

static void
gfarm_ldap_path_info_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	struct gfarm_path_info *info = vinfo;

	/* XXX - info->status.st_ino is set not here but at upper level */

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

static char *
gfarm_ldap_path_info_update(
	char *pathname,
	struct gfarm_path_info *info,
	int mod_op,
	char *(*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
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

	struct gfarm_ldap_path_info_key key;

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

	return ((*update_op)(&key, modv, &gfarm_ldap_path_info_ops));
}

static char *
gfarm_ldap_path_info_get(
	const char *pathname,
	struct gfarm_path_info *info)
{
	struct gfarm_ldap_path_info_key key;

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

	return (gfarm_ldap_generic_info_get(&key, info,
	    &gfarm_ldap_path_info_ops));
}

static char *
gfarm_ldap_path_info_set(
	char *pathname,
	struct gfarm_path_info *info)
{
	return (gfarm_ldap_path_info_update(pathname, info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_set));
}

static char *
gfarm_ldap_path_info_replace(
	char *pathname,
	struct gfarm_path_info *info)
{
	return (gfarm_ldap_path_info_update(pathname, info,
	    LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify));
}

static char *
gfarm_ldap_path_info_remove(const char *pathname)
{
	struct gfarm_ldap_path_info_key key;

	key.pathname = pathname;

	return (gfarm_ldap_generic_info_remove(&key,
	    &gfarm_ldap_path_info_ops));
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
static char *
gfarm_ldap_path_info_get_all_foreach(
	void (*callback)(void *, struct gfarm_path_info *),
	void *closure)
{
	struct gfarm_path_info tmp_info;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_ldap_path_info_ops.query_type,
	    &tmp_info, /*XXX*/(void (*)(void *, void *))callback, closure,
	    &gfarm_ldap_path_info_ops));
}

#if 0 /* GFarmFile history isn't actually used yet */

/* get GFarmFiles which were created by the program */
static char *
gfarm_ldap_file_history_get_allfile_by_program(
	char *program,
	int *np,
	char ***gfarm_files_p)
{
	char *error;
	int n;
	struct gfarm_path_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmFile)(generatorProgram=%s))";
	char *query;

	GFARM_MALLOC_ARRAY(query, sizeof(query_template) + strlen(program));
	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, program);
	error = gfarm_ldap_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, query,
	    &n, &infos,
	    &gfarm_ldap_path_info_ops);
	free(query);
	if (error != NULL)
		return (error);

	*np = n;
	*gfarm_files_p = (char **)infos;
	return (NULL);
}

/* get GFarmFiles which were created from the file as a input */
static char *
gfarm_ldap_file_history_get_allfile_by_file(
	char *input_gfarm_file,
	int *np,
	char ***gfarm_files_p)
{
	char *error;
	int n;
	struct gfarm_path_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmFile)(generatorInputGFarmFiles=%s))";
	char *query;

	GFARM_MALLOC_ARRAY(query, sizeof(query_template) +
			     strlen(input_gfarm_file));
	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, input_gfarm_file);
	error = gfarm_ldap_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, query,
	    &n, &infos,
	    &gfarm_ldap_path_info_ops);
	free(query);
	if (error != NULL)
		return (error);

	*np = n;
	*gfarm_files_p = (char **)infos;
	return (NULL);
}

#endif /* GFarmFile history isn't actually used yet */

/**********************************************************************/

static char *gfarm_ldap_file_section_info_make_dn(void *vkey);
static void gfarm_ldap_file_section_info_set_field(void *info, char *attribute,
	char **vals);

struct gfarm_ldap_file_section_info_key {
	const char *pathname;
	const char *section;
};

static const struct gfarm_ldap_generic_info_ops
	gfarm_ldap_file_section_info_ops =
{
	&gfarm_base_file_section_info_ops,
	"(objectclass=GFarmFileSection)",
	"section=%s, pathname=%s, %s",
	gfarm_ldap_file_section_info_make_dn,
	gfarm_ldap_file_section_info_set_field,
};

static char *
gfarm_ldap_file_section_info_make_dn(void *vkey)
{
	struct gfarm_ldap_file_section_info_key *key = vkey;
	char *escaped_pathname, *dn;

	escaped_pathname = gfarm_ldap_escape_pathname(key->pathname);
	if (escaped_pathname == NULL)
		return (NULL);

	GFARM_MALLOC_ARRAY(dn, 
		    strlen(gfarm_ldap_file_section_info_ops.dn_template) +
		    strlen(key->section) + strlen(escaped_pathname) +
		    strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		free(escaped_pathname);
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_file_section_info_ops.dn_template,
		key->section, escaped_pathname, gfarm_ldap_base_dn);
	free(escaped_pathname);
	return (dn);
}

static void
gfarm_ldap_file_section_info_set_field(
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

static char *
gfarm_ldap_file_section_info_get(
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	struct gfarm_ldap_file_section_info_key key;

	key.pathname = pathname;
	key.section = section;

	return (gfarm_ldap_generic_info_get(&key, info,
	    &gfarm_ldap_file_section_info_ops));
}

static char *
gfarm_ldap_file_section_info_update(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info,
	int mod_op,
	char *(*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[7];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];
	char filesize_string[INT64STRLEN + 1];

	struct gfarm_ldap_file_section_info_key key;

	key.pathname = pathname;
	key.section = section;

	/*
	 * `info->section' doesn't have to be set,
	 * because this function uses its argument instead.
	 */
	sprintf(filesize_string, "%" PR_FILE_OFFSET,
		CAST_PR_FILE_OFFSET info->filesize);
	i = 0;
	set_string_mod(&modv[i], mod_op,
		       "objectclass", "GFarmFileSection", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "pathname", pathname, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "section", section, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "filesize", filesize_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "checksumType", info->checksum_type, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
		       "checksum", info->checksum, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return ((*update_op)(&key, modv, &gfarm_ldap_file_section_info_ops));
}

static char *
gfarm_ldap_file_section_info_set(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	return (gfarm_ldap_file_section_info_update(pathname, section, info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_set));
}

static char *
gfarm_ldap_file_section_info_replace(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	return (gfarm_ldap_file_section_info_update(pathname, section, info,
	    LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify));
}

static char *
gfarm_ldap_file_section_info_remove(
	const char *pathname,
	const char *section)
{
	struct gfarm_ldap_file_section_info_key key;

	key.pathname = pathname;
	key.section = section;

	return (gfarm_ldap_generic_info_remove(&key,
	    &gfarm_ldap_file_section_info_ops));
}

static char *
gfarm_ldap_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	char *error;
	int n;
	struct gfarm_file_section_info *infos;
	static char dn_template[] = "pathname=%s, %s";
	char *escaped_pathname, *dn;

	escaped_pathname = gfarm_ldap_escape_pathname(pathname);
	if (escaped_pathname == NULL)
		return (NULL);

	GFARM_MALLOC_ARRAY(dn, sizeof(dn_template) + strlen(escaped_pathname) +
		    strlen(gfarm_ldap_base_dn));
	if (dn == NULL) {
		free(escaped_pathname);
		return (NULL);
	}
	sprintf(dn, dn_template, escaped_pathname, gfarm_ldap_base_dn);
	free(escaped_pathname);
	error = gfarm_ldap_generic_info_get_all(dn, LDAP_SCOPE_ONELEVEL,
	    gfarm_ldap_file_section_info_ops.query_type,
	    &n, &infos,
	    &gfarm_ldap_file_section_info_ops);
	free(dn);
	if (error != NULL)
		return (error);
	*np = n;
	*infosp = infos;
	return (NULL);
}

/**********************************************************************/

static char *gfarm_ldap_file_section_copy_info_make_dn(void *vkey);
static void gfarm_ldap_file_section_copy_info_set_field(
	void *info, char *attribute, char **vals);

struct gfarm_ldap_file_section_copy_info_key {
	const char *pathname;
	const char *section;
	const char *hostname;
};

static const struct gfarm_ldap_generic_info_ops
	gfarm_ldap_file_section_copy_info_ops =
{
	&gfarm_base_file_section_copy_info_ops,
	"(objectclass=GFarmFileSectionCopy)",
	"hostname=%s, section=%s, pathname=%s, %s",
	gfarm_ldap_file_section_copy_info_make_dn,
	gfarm_ldap_file_section_copy_info_set_field,
};

static char *
gfarm_ldap_file_section_copy_info_make_dn(void *vkey)
{
	struct gfarm_ldap_file_section_copy_info_key *key = vkey;
	char *escaped_pathname, *dn;

	escaped_pathname = gfarm_ldap_escape_pathname(key->pathname);
	if (escaped_pathname == NULL)
		return (NULL);

	GFARM_MALLOC_ARRAY(dn,
		    strlen(gfarm_ldap_file_section_copy_info_ops.dn_template) +
		    strlen(key->hostname) +
		    strlen(key->section) + strlen(escaped_pathname) +
		    strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		free(escaped_pathname);
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_file_section_copy_info_ops.dn_template,
		key->hostname, key->section, escaped_pathname,
		gfarm_ldap_base_dn);
	free(escaped_pathname);
	return (dn);
}

static void
gfarm_ldap_file_section_copy_info_set_field(
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

static char *
gfarm_ldap_file_section_copy_info_get(
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	struct gfarm_ldap_file_section_copy_info_key key;

	key.pathname = pathname;
	key.section = section;
	key.hostname = hostname;

	return (gfarm_ldap_generic_info_get(&key, info,
	    &gfarm_ldap_file_section_copy_info_ops));
}

static char *
gfarm_ldap_file_section_copy_info_set(
	char *pathname,
	char *section,
	char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	int i;
	LDAPMod *modv[5];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	struct gfarm_ldap_file_section_copy_info_key key;

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

	return (gfarm_ldap_generic_info_set(&key, modv,
	    &gfarm_ldap_file_section_copy_info_ops));
}

static char *
gfarm_ldap_file_section_copy_info_remove(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	struct gfarm_ldap_file_section_copy_info_key key;

	key.pathname = pathname;
	key.section = section;
	key.hostname = hostname;

	return (gfarm_ldap_generic_info_remove(&key,
	    &gfarm_ldap_file_section_copy_info_ops));
}

static char *
gfarm_ldap_file_section_copy_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *error;
	int n;
	struct gfarm_file_section_copy_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmFileSectionCopy)(pathname=%s))";
	char *query;

	GFARM_MALLOC_ARRAY(query, sizeof(query_template) + strlen(pathname));
	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, pathname);
	error = gfarm_ldap_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_SUBTREE, query, &n, &infos,
	    &gfarm_ldap_file_section_copy_info_ops);
	free(query);
	if (error != NULL)
		return (error);
	*np = n;
	*infosp = infos;
	return (NULL);
}

static char *
gfarm_ldap_file_section_copy_info_get_all_by_section(
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *error, *dn;
	int n;
	struct gfarm_ldap_file_section_info_key frag_info_key;
	struct gfarm_file_section_copy_info *infos;

	frag_info_key.pathname = pathname;
	frag_info_key.section = section;
	dn = (*gfarm_ldap_file_section_info_ops.make_dn)(&frag_info_key);
	if (dn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	error = gfarm_ldap_generic_info_get_all(dn, LDAP_SCOPE_ONELEVEL,
	    gfarm_ldap_file_section_copy_info_ops.query_type, &n, &infos,
	    &gfarm_ldap_file_section_copy_info_ops);
	free(dn);
	if (error != NULL)
		return (error);
	*np = n;
	*infosp = infos;
	return (NULL);
}

static char *
gfarm_ldap_file_section_copy_info_get_all_by_host(
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	char *error;
	int n;
	struct gfarm_file_section_copy_info *infos;
	static char query_template[] =
		"(&(objectclass=GFarmFileSectionCopy)(hostname=%s))";
	char *query;

	GFARM_MALLOC_ARRAY(query, sizeof(query_template) + strlen(hostname));
	if (query == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(query, query_template, hostname);
	error = gfarm_ldap_generic_info_get_all(gfarm_ldap_base_dn,
	    LDAP_SCOPE_SUBTREE, query, &n, &infos,
	    &gfarm_ldap_file_section_copy_info_ops);
	free(query);
	if (error != NULL)
		return (error);
	*np = n;
	*infosp = infos;
	return (NULL);
}

/**********************************************************************/

#if 0 /* GFarmFile history isn't actually used yet */

static char *gfarm_ldap_file_history_make_dn(void *vkey);
static void gfarm_ldap_file_history_set_field(void *info, char *attribute,
	char **vals);

struct gfarm_ldap_file_history_key {
	char *gfarm_file;
};

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_file_history_ops = {
	&gfarm_base_file_history_ops,
	"(objectclass=GFarmFile)",
	"gfarmFile=%s, %s",
	gfarm_ldap_file_history_make_dn,
	gfarm_ldap_file_history_set_field,
};

static char *
gfarm_ldap_file_history_make_dn(void *vkey)
{
	struct gfarm_ldap_file_history_key *key = vkey;
	char *dn;
	
	GFARM_MALLOC_ARRAY(dn, 
			  strlen(gfarm_ldap_file_history_ops.dn_template) +
			  strlen(key->gfarm_file) +
			  strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL)
		return (NULL);
	sprintf(dn, gfarm_ldap_file_history_ops.dn_template,
		key->gfarm_file, gfarm_ldap_base_dn);
	return (dn);
}

static void
gfarm_ldap_file_history_set_field(
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

static char *
gfarm_ldap_file_history_get(
	char *gfarm_file,
	struct gfarm_file_history *info)
{
	struct gfarm_ldap_file_history_key key;

	key.gfarm_file = gfarm_file;

	return (gfarm_ldap_generic_info_get(&key, info,
	    &gfarm_ldap_file_history_ops));
}

static char *
gfarm_ldap_file_history_set(
	char *gfarm_file,
	struct gfarm_file_history *info)
{
	int i;
	LDAPMod *modv[4];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	LDAPMod input_files_mod;

	struct gfarm_ldap_file_history_key key;

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

	return (gfarm_ldap_generic_info_set(&key, modv,
	    &gfarm_ldap_file_history_ops));
}

static char *
gfarm_ldap_file_history_remove(char *gfarm_file)
{
	struct gfarm_ldap_file_history_key key;

	key.gfarm_file = gfarm_file;

	return (gfarm_ldap_generic_info_remove(&key,
	    &gfarm_ldap_file_history_ops));
}

#endif /* GFarmFile history isn't actually used yet */

/**********************************************************************/

const struct gfarm_metadb_internal_ops gfarm_ldap_metadb_ops = {
	gfarm_ldap_initialize,
	gfarm_ldap_terminate,

	gfarm_ldap_host_info_get,
	gfarm_ldap_host_info_remove_hostaliases,
	gfarm_ldap_host_info_set,
	gfarm_ldap_host_info_replace,
	gfarm_ldap_host_info_remove,
	gfarm_ldap_host_info_get_all,
	gfarm_ldap_host_info_get_by_name_alias,
	gfarm_ldap_host_info_get_allhost_by_architecture,

	gfarm_ldap_path_info_get,
	gfarm_ldap_path_info_set,
	gfarm_ldap_path_info_replace,
	gfarm_ldap_path_info_remove,
	gfarm_ldap_path_info_get_all_foreach,

	gfarm_ldap_file_section_info_get,
	gfarm_ldap_file_section_info_set,
	gfarm_ldap_file_section_info_replace,
	gfarm_ldap_file_section_info_remove,
	gfarm_ldap_file_section_info_get_all_by_file,

	gfarm_ldap_file_section_copy_info_get,
	gfarm_ldap_file_section_copy_info_set,
	gfarm_ldap_file_section_copy_info_remove,
	gfarm_ldap_file_section_copy_info_get_all_by_file,
	gfarm_ldap_file_section_copy_info_get_all_by_section,
	gfarm_ldap_file_section_copy_info_get_all_by_host,
};
