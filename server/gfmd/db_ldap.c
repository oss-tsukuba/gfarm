/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

/*
 * $Id$
 */

#include <pthread.h>	/* db_access.h currently needs this */
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

/* old openldap does not have ldap_memfree. */
#ifndef HAVE_LDAP_MEMFREE
#define	ldap_memfree(a)
#endif /* HAVE_LDAP_MEMFREE */

#include <gfarm/gfarm.h>

#include "internal_host_info.h"

#include "gfutil.h"

#include "config.h"
#include "metadb_common.h"
#include "xattr_info.h"
#include "metadb_server.h"

#include "subr.h"
#include "db_access.h"
#include "quota.h"
#include "db_ops.h"
#include "db_common.h"

#define INT32STRLEN	GFARM_INT32STRLEN
#define INT64STRLEN	GFARM_INT64STRLEN
#define ARRAY_LENGTH(array)	GFARM_ARRAY_LENGTH(array)

/**********************************************************************/

LDAP *gfarm_ldap_server = NULL;

static gfarm_error_t gfarm_ldap_initialize(void);
static gfarm_error_t gfarm_ldap_terminate(void);

static int
just_a_minute()
{
	struct timespec t;
	/* wait 10 msec */
	t.tv_sec = 0;
	t.tv_nsec = 10000000;
	return (nanosleep(&t, NULL));
}

static int
reconnect_to_ldap_server()
{
	gfarm_error_t error;
	int i;

	gflog_warning(GFARM_MSG_1000726, "reconnect");
	error = gfarm_ldap_terminate();
	if (error != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002115,
			"gfarm_ldap_terminate() failed: %s",
			gfarm_error_string(error));
		return (error);
	}

	for (i = 1;; ++i) {
		/*
		 * When OpenLDAP server is heavily loaded,
		 * it returns LDAP_SERVER_DOWN.  The first
		 * attempt to reconnect tends to fail.
		 * As a workaround, wait a minute and try
		 * to reconnect several times.
		 */
		just_a_minute();
		error = gfarm_ldap_initialize();
		if (error == GFARM_ERR_NO_ERROR)
			return (error);
		gflog_warning(GFARM_MSG_1000727, "reconnect [%d] failed", i);
		/* try again and again */
	}
}

void
gfarm_ldap_sanity(void)
{
	int rv;
	LDAPMessage *res = NULL;

retry:
	rv = ldap_search_s(gfarm_ldap_server, gfarm_ldap_base_dn,
	    LDAP_SCOPE_BASE, "objectclass=top", NULL, 0, &res);
	if (rv != LDAP_SUCCESS) {
		switch (rv) {
		case LDAP_SERVER_DOWN:
			if (reconnect_to_ldap_server() != GFARM_ERR_NO_ERROR)
				gflog_fatal(GFARM_MSG_1000728,
				    "can't contact LDAP server for gfarm");
			goto retry;
		case LDAP_NO_SUCH_OBJECT:
			gflog_fatal(GFARM_MSG_1000729,
			    "gfarm LDAP base dn (%s) not found",
			    gfarm_ldap_base_dn);
			break;
		default:
			gflog_fatal(GFARM_MSG_1000730,
			    "gfarm LDAP base dn (%s) access failed",
			    gfarm_ldap_base_dn);
			break;
		}
	}
	if (res != NULL)
		ldap_msgfree(res);
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
static gfarm_error_t
gfarm_ldap_new_default_ctx(SSL_CTX **ctxp)
{
	gfarm_error_t e;
	SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());

	if (ctx == NULL) {
		gflog_warning(GFARM_MSG_1000731,
		    "LDAP: cannot allocate SSL/TLS context");
		return (GFARM_ERR_CANT_OPEN);
	}

	/*
	 * The following operation is nearly equivalent to:
	 * rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_CIPHER_SUITE,
	 *     gfarm_ldap_tls_cipher_suite);
	 */
	if (gfarm_ldap_tls_cipher_suite != NULL &&
	    !SSL_CTX_set_cipher_list(ctx, gfarm_ldap_tls_cipher_suite)) {
		gflog_warning(GFARM_MSG_1000732,
		    "cannot set ldap_tls_cipher_suite");
		e = GFARM_ERR_CANT_OPEN;
		goto error;
	}

	/*
	 * The following operation is nearly equivalent to:
	 * rv = ldap_set_option(NULL,LDAP_OPT_X_TLS_CACERTDIR,GRID_CACERT_DIR);
	 */
	if (!SSL_CTX_load_verify_locations(ctx, NULL, GRID_CACERT_DIR)) {
		gflog_warning(GFARM_MSG_1000733, "cannot use " GRID_CACERT_DIR
		    " for LDAP TLS certificates directory");
		e = GFARM_ERR_CANT_OPEN;
		goto error;
	} else if (!SSL_CTX_set_default_verify_paths(ctx)) {
		gflog_warning(GFARM_MSG_1000734,
		    "failed to verify " GRID_CACERT_DIR
		    " for LDAP TLS certificates directory");
		e = GFARM_ERR_CANT_OPEN;
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
		gflog_warning(GFARM_MSG_1000735,
		    "failed to use ldap_tls_certificate_key_file");
		e = GFARM_ERR_CANT_OPEN;
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
		gflog_warning(GFARM_MSG_1000736,
		    "failed to use ldap_tls_certificate_file");
		e = GFARM_ERR_CANT_OPEN;
		goto error;
	}

	if ((gfarm_ldap_tls_certificate_key_file != NULL ||
	     gfarm_ldap_tls_certificate_file != NULL) &&
	    !SSL_CTX_check_private_key(ctx)) {
		gflog_warning(GFARM_MSG_1000737,
		    "ldap_tls_certificate_file/key_file "
		    "check failure");
		e = GFARM_ERR_CANT_OPEN;
		goto error;
	}

	/*
	 * The following operation is nearly equivalent to:
	 * tls = LDAP_OPT_X_TLS_HARD;
	 * rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_REQUIRE_CERT, &tls);
	 */
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

	*ctxp = ctx;
	return (GFARM_ERR_NO_ERROR);

error:
	SSL_CTX_free(ctx);
	if (ERR_peek_error()) {
		gflog_error(GFARM_MSG_1000738, "because:", e);
		do {
			gflog_error(GFARM_MSG_1000739, "%s", ERR_error_string(
			    ERR_get_error(), NULL));
		} while (ERR_peek_error());
	}
	return (e);
}

static SSL_CTX *ldap_ssl_context = NULL;
static SSL_CTX *ldap_ssl_default_context = NULL;

static gfarm_error_t
gfarm_ldap_set_ssl_context(void)
{
	gfarm_error_t e;
	int rv;

	if (ldap_ssl_context == NULL) {
		e = gfarm_ldap_new_default_ctx(&ldap_ssl_context);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002116,
				"gfarm_ldap_new_default_ctx() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}

	rv = ldap_get_option(NULL, LDAP_OPT_X_TLS_CTX,
	    &ldap_ssl_default_context);
	if (rv != LDAP_SUCCESS) {
		gflog_error(GFARM_MSG_1000740,
		    "LDAP get default SSL/TLS context: %s",
		    ldap_err2string(rv));
		return (GFARM_ERR_CANT_OPEN);
	}

	rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_CTX, ldap_ssl_context);
	if (rv != LDAP_SUCCESS) {
		gflog_error(GFARM_MSG_1000741,
		    "LDAP set default SSL/TLS context: %s",
		    ldap_err2string(rv));
		return (GFARM_ERR_CANT_OPEN);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_ldap_restore_ssl_context(void)
{
	int rv = ldap_set_option(NULL, LDAP_OPT_X_TLS_CTX,
	    ldap_ssl_default_context);

	if (rv != LDAP_SUCCESS) {
		gflog_error(GFARM_MSG_1000742,
		    "LDAP restore default SSL/TLS context: %s",
		    ldap_err2string(rv));
		return (GFARM_ERR_CANT_OPEN);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_ldap_switch_ssl_context(LDAP *ld)
{
	int rv;

	gfarm_ldap_restore_ssl_context();

	rv = ldap_set_option(ld, LDAP_OPT_X_TLS_CTX, ldap_ssl_context);

	if (rv != LDAP_SUCCESS) {
		gflog_error(GFARM_MSG_1000743, "LDAP set SSL/TLS context: %s",
		    ldap_err2string(rv));
		return (GFARM_ERR_CANT_OPEN);
	}
	return (GFARM_ERR_NO_ERROR);
}

#else
#define gfarm_ldap_restore_ssl_context()
#define gfarm_ldap_switch_ssl_context(ld)
#endif /* OPENLDAP_TLS_USABLE */

static gfarm_error_t
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
	char *s;

	/* we need to check "ldap_tls" at first to see default port number */
	if (gfarm_ldap_tls != NULL) {
		if (strcasecmp(gfarm_ldap_tls, "false") == 0) {
			tls_mode = LDAP_WITHOUT_TLS;
		} else if (strcasecmp(gfarm_ldap_tls, "true") == 0) {
			tls_mode = LDAP_WITH_TLS;
		} else if (strcasecmp(gfarm_ldap_tls, "start_tls") == 0) {
			tls_mode = LDAP_WITH_START_TLS;
		} else {
			gflog_error(GFARM_MSG_1000744,
			    "ldap_tls: unknown keyword %s",
			    gfarm_ldap_tls);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}
	/* sanity check */
	if (gfarm_ldap_server_name == NULL) {
		gflog_error(GFARM_MSG_1000745,
		    "ldap_server_host is not specified");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (gfarm_ldap_server_port == NULL) {
		if (tls_mode == LDAP_WITH_TLS)
			port = LDAPS_PORT;
		else
			port = LDAP_PORT;
	} else {
		port = strtol(gfarm_ldap_server_port, &s, 0);
		if (s == gfarm_ldap_server_port || port <= 0 || port >= 65536) {
			gflog_error(GFARM_MSG_1000746,
			    "ldap_server_port: illegal value `%s'",
			    gfarm_ldap_server_port);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}
	if (gfarm_ldap_base_dn == NULL) {
		gflog_error(GFARM_MSG_1000747,
		    "ldap_base_dn is not specified");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	/*
	 * initialize LDAP
	 */

	/* open a connection */
	gfarm_ldap_server = ldap_init(gfarm_ldap_server_name, port);
	if (gfarm_ldap_server == NULL) {
		/* ldap_init() defers actual connect(2) operation later */
		gflog_error(GFARM_MSG_1000748,
		    "ldap_init: %s", strerror(errno));
		return (GFARM_ERR_CANT_OPEN);
	}

	if (tls_mode != LDAP_WITHOUT_TLS) {
#ifdef OPENLDAP_TLS_USABLE
		e = gfarm_ldap_set_ssl_context();
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002117,
				"gfarm_ldap_set_ssl_context() failed: %s",
				gfarm_error_string(e));
			gfarm_ldap_terminate();
			return (e);
		}
#else
		gfarm_ldap_terminate();
		gflog_error(GFARM_MSG_1000749,
		    "\"ldap_tls\" is specified, but "
		    "the LDAP library linked with gfarm doesn't support it");
		return (GFARM_ERR_CANT_OPEN);
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
			gflog_error(GFARM_MSG_1000750, "LDAP use SSL/TLS: %s",
			    ldap_err2string(rv));
#ifdef HAVE_LDAP_PERROR
			/* XXX this can only output to stderr */
			ldap_perror(gfarm_ldap_server, "ldap_start_tls");
#endif
			gfarm_ldap_restore_ssl_context();
			gfarm_ldap_terminate();
			return (GFARM_ERR_CANT_OPEN);
		}
#else
		gfarm_ldap_restore_ssl_context();
		gfarm_ldap_terminate();
		gflog_error(GFARM_MSG_1000751,
		    "gfarm.conf: \"ldap_tls true\" is specified, but "
		    "the LDAP library linked with gfarm doesn't support it");
		return (GFARM_ERR_CANT_OPEN);
#endif /* defined(LDAP_OPT_X_TLS) && defined(HAVE_LDAP_SET_OPTION) */
	}

	if (tls_mode == LDAP_WITH_START_TLS) {
#if defined(HAVE_LDAP_START_TLS_S) && defined(HAVE_LDAP_SET_OPTION)
		rv = ldap_start_tls_s(gfarm_ldap_server, NULL, NULL);
		if (rv != LDAP_SUCCESS) {
			gflog_error(GFARM_MSG_1000752, "LDAP start TLS: %s",
			    ldap_err2string(rv));
#ifdef HAVE_LDAP_PERROR
			/* XXX this cannot output to syslog */
			ldap_perror(gfarm_ldap_server, "ldap_start_tls");
#endif
			gfarm_ldap_restore_ssl_context();
			gfarm_ldap_terminate();
			return (GFARM_ERR_CANT_OPEN);
		}
#else
		gfarm_ldap_restore_ssl_context();
		gfarm_ldap_terminate();
		gflog_error(GFARM_MSG_1000753,
		    "gfarm.conf: \"ldap_tls start_tls\" is specified, "
		  "but the LDAP library linked with gfarm doesn't support it");
		return (GFARM_ERR_CANT_OPEN);
#endif /* defined(HAVE_LDAP_START_TLS_S) && defined(HAVE_LDAP_SET_OPTION) */
	}

	/* gfarm_ldap_bind_dn and gfarm_ldap_bind_password may be NULL */
	rv = ldap_simple_bind_s(gfarm_ldap_server,
	    gfarm_ldap_bind_dn, gfarm_ldap_bind_password);
	if (rv != LDAP_SUCCESS) {
		gflog_error(GFARM_MSG_1000754,
		    "LDAP simple bind: %s", ldap_err2string(rv));
		if (tls_mode != LDAP_WITHOUT_TLS)
			gfarm_ldap_restore_ssl_context();
		gfarm_ldap_terminate();
		return (GFARM_ERR_CANT_OPEN);
	}

	/* sanity check. base_dn can be accessed? */
	gfarm_ldap_sanity();

	if (tls_mode != LDAP_WITHOUT_TLS)
		gfarm_ldap_switch_ssl_context(gfarm_ldap_server);
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfarm_ldap_terminate(void)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int rv;

	if (gfarm_ldap_server == NULL) {
		gflog_debug(GFARM_MSG_1002118, "'gfarm_ldap_server' is NULL");
		return (GFARM_ERR_NO_ERROR);
	}
	rv = ldap_unbind(gfarm_ldap_server);
	if (rv != LDAP_SUCCESS) {
		gflog_error(GFARM_MSG_1000755,
		    "ldap_unbind: %s", ldap_err2string(rv));
		e = GFARM_ERR_UNKNOWN;
	}
	gfarm_ldap_server = NULL;

	return (e);
}

#if !defined(LDAP_OPT_RESULT_CODE) && defined(LDAP_OPT_ERROR_NUMBER)
#define LDAP_OPT_RESULT_CODE LDAP_OPT_ERROR_NUMBER
#endif

static const char *
gfarm_ldap_session_error(void)
{
	int e;
	int rv = ldap_get_option(gfarm_ldap_server, LDAP_OPT_RESULT_CODE, &e);

	if (rv != LDAP_OPT_SUCCESS)
		gflog_fatal(GFARM_MSG_1002362, "ldap_get_option: %d", rv);

	return (ldap_err2string(e));
}

static char**
gfarm_strarray_dup_log(char **src, const char *diag)
{
	char **dst = gfarm_strarray_dup(src);

	if (dst == NULL)
		gflog_error(GFARM_MSG_1002363,
		    "%s: gfarm_strarray_dup(): no memory", diag);
	return (dst);
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

struct ldap_binary_modify {
	LDAPMod mod;
	struct berval *val[2];
	struct berval v;
};

static void
set_binary_mod(
	LDAPMod **modp,
	int op,
	char *type,
	void *value,
	int size,
	struct ldap_binary_modify *storage)
{
	storage->v.bv_val = value;
	storage->v.bv_len = size;
	storage->val[0] = &storage->v;
	storage->val[1] = NULL;
	storage->mod.mod_op = op | LDAP_MOD_BVALUES;
	storage->mod.mod_type = type;
	storage->mod.mod_vals.modv_bvals = storage->val;
	*modp = &storage->mod;
}

static void
set_delete_mod(
	LDAPMod **modp,
	char *type,
	LDAPMod *storage)
{
	storage->mod_op = LDAP_MOD_DELETE;
	storage->mod_type = type;
	storage->mod_values = NULL;
	*modp = storage;
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
	gfarm_error_t (*set_field)(void *info, char *attribute, char **vals);
};

#if 0 /* not used */

static gfarm_error_t
gfarm_ldap_generic_info_get(
	void *key,
	void *info,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	gfarm_error_t error = GFARM_ERR_NO_ERROR, err;
	LDAPMessage *res, *e;
	int n, rv;
	char *a, *dn;
	BerElement *ber;
	char **vals;

retry:
	dn = ops->make_dn(key);
	if (dn == NULL)
		return (GFARM_ERR_NO_MEMORY);
	res = NULL;
	rv = ldap_search_s(gfarm_ldap_server, dn,
	    LDAP_SCOPE_BASE, ops->query_type, NULL, 0, &res);
	if (rv != LDAP_SUCCESS) {
		switch (rv) {
		case LDAP_SERVER_DOWN:
			error = gfarm_ldap_initialize();
			if (error == GFARM_ERR_NO_ERROR)
				goto retry;
			break;
		case LDAP_NO_SUCH_OBJECT:
			error = GFARM_ERR_NO_SUCH_OBJECT;
			break;
		default:
			gflog_error(GFARM_MSG_UNUSED, "ldap_search_s(%s): %s",
			    dn, ldap_err2string(rv));
			error = GFARM_ERR_UNKNOWN;
			break;
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
	if (e == NULL) {
		gflog_error(GFARM_MSG_1002364,
		    "ldap_first_entry: %s",
		    ldap_err2string(gfarm_ldap_server->ld_errno));
		error = GFARM_ERR_UNKNOWN;
		goto msgfree;
	}

	ber = NULL;
	for (a = ldap_first_attribute(gfarm_ldap_server, e, &ber); a != NULL;
	    a = ldap_next_attribute(gfarm_ldap_server, e, ber)) {
		vals = ldap_get_values(gfarm_ldap_server, e, a);
		if (vals == NULL) {
			gflog_error(GFARM_MSG_1002365,
			    "ldap_get_values: %s", gfarm_ldap_session_error());
			error = GFARM_ERR_UNKNOWN;
		} else {
			if (vals[0] != NULL) {
				err = ops->set_field(info, a, vals);
				if (err != GFARM_ERR_NO_ERROR)
					error = err;
			}
			ldap_value_free(vals);
		}
		ldap_memfree(a);
	}
	if (ber != NULL)
		ber_free(ber, 0);

	if (error != GFARM_ERR_NO_ERROR) {
		ops->gen_ops->free(info);
	/* should check all fields are filled */
	} else if (!ops->gen_ops->validate(info)) {
		gflog_error(GFARM_MSG_1002366,
		    "gfarm_ldap_generic_info_get: validation error");
		ops->gen_ops->free(info);
		/* XXX - different error code is better ? */
		error = GFARM_ERR_NO_SUCH_OBJECT;
	}
msgfree:
	free(dn);
	/* free the search results */
	if (res != NULL)
		ldap_msgfree(res);

	return (error);
}
#endif /* not used */

static gfarm_error_t
gfarm_ldap_generic_info_add(
	void *key,
	LDAPMod **modv,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	gfarm_error_t error;
	int rv;
	char *dn;

	dn = ops->make_dn(key);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002119, "make_dn() failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
retry:
	rv = ldap_add_s(gfarm_ldap_server, dn, modv);

	switch (rv) {
	case LDAP_SUCCESS:
		error = GFARM_ERR_NO_ERROR;
		break;
	case LDAP_SERVER_DOWN:
		error = reconnect_to_ldap_server();
		if (error == GFARM_ERR_NO_ERROR)
			goto retry;
		break;
	case LDAP_ALREADY_EXISTS:
		error = GFARM_ERR_ALREADY_EXISTS;
		gflog_debug(GFARM_MSG_1002120, "entry already exists");
		break;
	default:
		gflog_error(GFARM_MSG_1000756,
		    "ldap_add_s(%s): %s", dn, ldap_err2string(rv));
		error = GFARM_ERR_UNKNOWN;
	}
	free(dn);
	return (error);
}

static gfarm_error_t
gfarm_ldap_generic_info_modify(
	void *key,
	LDAPMod **modv,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	gfarm_error_t error;
	int rv;
	char *dn;

	dn = ops->make_dn(key);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002121, "make_dn() failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
retry:
	rv = ldap_modify_s(gfarm_ldap_server, dn, modv);

	switch (rv) {
	case LDAP_SUCCESS:
		error = GFARM_ERR_NO_ERROR;
		break;
	case LDAP_SERVER_DOWN:
		error = reconnect_to_ldap_server();
		if (error == GFARM_ERR_NO_ERROR)
			goto retry;
		break;
	case LDAP_NO_SUCH_OBJECT:
		gflog_debug(GFARM_MSG_1002122,
			"ldap_modify_s(%s) failed: %s", dn,
			gfarm_error_string(GFARM_ERR_NO_SUCH_OBJECT));
		error = GFARM_ERR_NO_SUCH_OBJECT;
		break;
	case LDAP_ALREADY_EXISTS:
		gflog_debug(GFARM_MSG_1002123,
			"ldap_modify_s(%s) failed: %s", dn,
			gfarm_error_string(GFARM_ERR_ALREADY_EXISTS));
		error = GFARM_ERR_ALREADY_EXISTS;
		break;
	default:
		gflog_error(GFARM_MSG_1000757,
		    "ldap_modify_s(%s): %s", dn, ldap_err2string(rv));
		error = GFARM_ERR_UNKNOWN;
	}
	free(dn);
	return (error);
}

static gfarm_error_t
gfarm_ldap_generic_info_remove(
	void *key,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	gfarm_error_t error;
	int rv;
	char *dn;

	dn = ops->make_dn(key);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002124, "make_dn() failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
retry:
	rv = ldap_delete_s(gfarm_ldap_server, dn);

	switch (rv) {
	case LDAP_SUCCESS:
		error = GFARM_ERR_NO_ERROR;
		break;
	case LDAP_SERVER_DOWN:
		error = reconnect_to_ldap_server();
		if (error == GFARM_ERR_NO_ERROR)
			goto retry;
		break;
	case LDAP_NO_SUCH_OBJECT:
		error = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_debug(GFARM_MSG_1002125,
			"ldap_delete_s(%s) failed: %s", dn,
			gfarm_error_string(GFARM_ERR_NO_SUCH_OBJECT));
		break;
	default:
		gflog_error(GFARM_MSG_1000758,
		    "ldap_delete_s(%s): %s", dn, ldap_err2string(rv));
		error = GFARM_ERR_UNKNOWN;
	}
	free(dn);
	return (error);
}

#if 0 /* not used */
static gfarm_error_t
gfarm_ldap_generic_info_get_all(
	char *dn,
	int scope, /* LDAP_SCOPE_ONELEVEL or LDAP_SCOPE_SUBTREE */
	char *query,
	int *np,
	void *infosp,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	gfarm_error_t error = GFARM_ERR_NO_ERROR, err;
	LDAPMessage *res, *e;
	int i, n, rv;
	char *a;
	BerElement *ber;
	char **vals;
	char *infos, *tmp_info;
	size_t size;
	int overflow = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	infos = NULL;
#endif
	/* search for entries, return all attrs  */
retry:
	res = NULL;
	rv = ldap_search_s(gfarm_ldap_server, dn, scope, query, NULL, 0, &res);
	if (rv != LDAP_SUCCESS) {
		switch (rv) {
		case LDAP_SERVER_DOWN:
			error = gfarm_ldap_initialize();
			if (error == GFARM_ERR_NO_ERROR)
				goto retry;
			break;
		case LDAP_NO_SUCH_OBJECT:
			error = GFARM_ERR_NO_SUCH_OBJECT;
			break;
		default:
			gflog_error(GFARM_MSG_UNUSED,
			    "ldap_search_s(%s) - get all; %s",
			    dn, ldap_err2string(rv));
			error = GFARM_ERR_UNKNOWN;
		}
		goto msgfree;
	}
	n = ldap_count_entries(gfarm_ldap_server, res);
	if (n == 0) {
		error = GFARM_ERR_NO_SUCH_OBJECT;
		goto msgfree;
	}
	size = gfarm_size_mul(&overflow, ops->gen_ops->info_size, n);
	if (!overflow)
		GFARM_MALLOC_ARRAY(infos, size);
	if (overflow || infos == NULL) {
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
			if (vals == NULL) {
				gflog_error(GFARM_MSG_1002367,
				    "ldap_get_values: %s",
				    gfarm_ldap_session_error());
				error = GFARM_ERR_UNKNOWN;
			} else {
				if (vals[0] != NULL) {
					err = ops->set_field(tmp_info, a, vals);
					if (err != GFARM_ERR_NO_ERROR)
						error = err;
				}
				ldap_value_free(vals);
			}
			ldap_memfree(a);
		}
		if (ber != NULL)
			ber_free(ber, 0);

		if (error != GFARM_ERR_NO_ERROR ||
		    !ops->gen_ops->validate(tmp_info)) {
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
	if (error != GFARM_ERR_NO_ERROR) {
		while (--i >= 0)
			ops->gen_ops->free(infos + i * ops->gen_ops->info_size);
		free(infos);
		goto msgfree;
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
#endif /* not used */

static gfarm_error_t
gfarm_ldap_generic_info_get_foreach_withattrs(
	char *dn,
	int scope, /* LDAP_SCOPE_ONELEVEL or LDAP_SCOPE_SUBTREE */
	char *query,
	void *tmp_info, /* just used as a work area */
	void (*callback)(void *, void *),
	void *closure,
	const struct gfarm_ldap_generic_info_ops *ops,
	char *attrs[])
{
	gfarm_error_t error = GFARM_ERR_NO_ERROR, err;
	LDAPMessage *res, *e;
	int i, msgid, rv;
	char *a;
	BerElement *ber;
	char **vals;
	struct berval **bervals;

	/* search for entries asynchronously */
	msgid = ldap_search(gfarm_ldap_server, dn, scope, query, attrs, 0);
	if (msgid == -1) {
		gflog_error(GFARM_MSG_1000759,
		    "ldap_search(%s) - for each", dn);
		return (GFARM_ERR_UNKNOWN);
	}

	/* step through each entry returned */
	i = 0;
	res = NULL;
	while ((rv = ldap_result(gfarm_ldap_server, msgid, LDAP_MSG_ONE, NULL,
	    &res)) > 0) {
		e = ldap_first_entry(gfarm_ldap_server, res);
		if (e == NULL)
			break;
		for (; e != NULL; e = ldap_next_entry(gfarm_ldap_server, e)) {

			ops->gen_ops->clear(tmp_info);

			ber = NULL;
			for (a = ldap_first_attribute(gfarm_ldap_server, e,
			    &ber);
			    a != NULL;
			    a = ldap_next_attribute(gfarm_ldap_server, e,
			    ber)) {

				/* XXX FIXME: a hack for "attrvalue" */
				if (strcasecmp(a, "attrvalue") == 0) {
					bervals = ldap_get_values_len(
					    gfarm_ldap_server, e, a);
					if (bervals == NULL) {
						gflog_error(GFARM_MSG_1002511,
						    "ldap_get_values_len: %s",
						    gfarm_ldap_session_error());
						error = GFARM_ERR_UNKNOWN;
					} else {
						if (bervals[0] != NULL) {
							err = ops->set_field(
							    tmp_info, a,
							    (char **)bervals);
							if (err !=
							    GFARM_ERR_NO_ERROR)
								error = err;
						}
						ldap_value_free_len(bervals);
					}
					ldap_memfree(a);
					continue;
				}
						
				vals = ldap_get_values(gfarm_ldap_server, e, a);
				if (vals == NULL) {
					gflog_error(GFARM_MSG_1002368,
					    "ldap_get_values: %s",
					    gfarm_ldap_session_error());
					error = GFARM_ERR_UNKNOWN;
				} else {
					if (vals[0] != NULL) {
						err = ops->set_field(tmp_info,
						    a, vals);
						if (err != GFARM_ERR_NO_ERROR)
							error = err;
					}
					ldap_value_free(vals);
				}
				ldap_memfree(a);
			}
			if (ber != NULL)
				ber_free(ber, 0);

			/* should check all fields are filled */
			if (error != GFARM_ERR_NO_ERROR ||
			    !ops->gen_ops->validate(tmp_info)) {
				/* invalid record */
				ops->gen_ops->free(tmp_info);
				continue;
			}
			if (callback != NULL)
				(*callback)(closure, tmp_info);
#if 0		/* the (*callback)() routine frees this memory */
			ops->gen_ops->free(tmp_info);
#endif
			i++;
		}
		/* free the search results */
		ldap_msgfree(res);
		res = NULL;
	}
	if (res != NULL)
		ldap_msgfree(res);
	if (error != GFARM_ERR_NO_ERROR)
		return (error);
	if (rv == -1) {
		gflog_error(GFARM_MSG_1000760,
		    "ldap_search(%s) - for each - failed", dn);
		return (GFARM_ERR_UNKNOWN);
	}

	if (i == 0) {
		gflog_debug(GFARM_MSG_1002126,
			"finding ldap entry failed");
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_ldap_generic_info_get_foreach(
	char *dn,
	int scope, /* LDAP_SCOPE_ONELEVEL or LDAP_SCOPE_SUBTREE */
	char *query,
	void *tmp_info, /* just used as a work area */
	void (*callback)(void *, void *),
	void *closure,
	const struct gfarm_ldap_generic_info_ops *ops)
{
	return (gfarm_ldap_generic_info_get_foreach_withattrs(
		dn, scope, query, tmp_info,
		callback, closure, ops, NULL));
}

/**********************************************************************/

static gfarm_error_t
gfarm_ldap_nop(gfarm_uint64_t seqnum, void *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static char *gfarm_ldap_host_info_make_dn(void *vkey);
static gfarm_error_t gfarm_ldap_host_info_set_field(void *info,
	char *attribute, char **vals);

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
	    strlen(key->hostname) + strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002127,
			"allocation of string 'dn' failed");
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_host_info_ops.dn_template,
	    key->hostname, gfarm_ldap_base_dn);
	return (dn);
}

static gfarm_error_t
gfarm_ldap_host_info_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	static const char diag[] = "gfarm_ldap_host_info_set_field";
	struct gfarm_internal_host_info *info = vinfo;

	if (strcasecmp(attribute, "hostname") == 0) {
		info->hi.hostname = strdup_log(vals[0], diag);
		if (info->hi.hostname == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "port") == 0) {
		info->hi.port = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "hostalias") == 0) {
		info->hi.hostaliases = gfarm_strarray_dup_log(vals, diag);
		if (info->hi.hostaliases == NULL) {
			info->hi.nhostaliases = 0;
			err = GFARM_ERR_NO_MEMORY;
		} else {
			info->hi.nhostaliases =
			    gfarm_strarray_length(info->hi.hostaliases);
		}
	} else if (strcasecmp(attribute, "architecture") == 0) {
		info->hi.architecture = strdup_log(vals[0], diag);
		if (info->hi.architecture == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "ncpu") == 0) {
		info->hi.ncpu = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "flags") == 0) {
		info->hi.flags = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "fsngroupname") == 0) {
		info->fsngroupname = strdup_log(vals[0], diag);
		if (info->fsngroupname == NULL)
			err = GFARM_ERR_NO_MEMORY;
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_host_info_update(
	struct gfarm_host_info *info,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[8];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];
	char port_string[INT32STRLEN + 1];
	char ncpu_string[INT32STRLEN + 1];
	char flags_string[INT32STRLEN + 1];

	LDAPMod hostaliases_mod;

	struct gfarm_ldap_host_info_key key;

	key.hostname = info->hostname;

	sprintf(port_string, "%d", info->port);
	sprintf(ncpu_string, "%d", info->ncpu);
	sprintf(flags_string, "%d", info->flags);

	i = 0;
	set_string_mod(&modv[i], mod_op,
	    "objectclass", "GFarmHost", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "hostname", info->hostname, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "port", port_string, &storage[i]);
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
	set_string_mod(&modv[i], mod_op,
	    "flags", flags_string, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv) || i == ARRAY_LENGTH(modv) - 1);

	return ((*update_op)(&key, modv, &gfarm_ldap_host_info_ops));
}

static gfarm_error_t
gfarm_ldap_host_add(gfarm_uint64_t seqnum, struct gfarm_host_info *info)
{
	gfarm_error_t e;
	e = gfarm_ldap_host_info_update(info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_add);
	free(info);
	return (e);
}

static gfarm_error_t
gfarm_ldap_host_modify(gfarm_uint64_t seqnum, struct db_host_modify_arg *arg)
{
	gfarm_error_t e;
	/* XXX FIXME: should use modflags, add_aliases and del_aliases */

	e = gfarm_ldap_host_info_update(&arg->hi,
	    LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_host_remove(gfarm_uint64_t seqnum, char *hostname)
{
	gfarm_error_t e;
	struct gfarm_ldap_host_info_key key;

	key.hostname = hostname;

	e = gfarm_ldap_generic_info_remove(&key, &gfarm_ldap_host_info_ops);
	free(hostname);
	return (e);
}

static gfarm_error_t
gfarm_ldap_host_load(void *closure,
	void (*callback)(void *, struct gfarm_internal_host_info *))
{
	struct gfarm_host_info tmp_info;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_ldap_host_info_ops.query_type,
	    &tmp_info, (void (*)(void *, void *))callback, closure,
	    &gfarm_ldap_host_info_ops));
}

/**********************************************************************/

static char *gfarm_ldap_user_info_make_dn(void *vkey);
static gfarm_error_t gfarm_ldap_user_info_set_field(void *info,
	char *attribute, char **vals);

struct gfarm_ldap_user_info_key {
	const char *username;
};

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_user_info_ops = {
	&gfarm_base_user_info_ops,
	"(objectclass=GFarmUser)",
	"username=%s, %s",
	gfarm_ldap_user_info_make_dn,
	gfarm_ldap_user_info_set_field,
};

static char *
gfarm_ldap_user_info_make_dn(void *vkey)
{
	struct gfarm_ldap_user_info_key *key = vkey;
	char *dn;

	GFARM_MALLOC_ARRAY(dn, strlen(gfarm_ldap_user_info_ops.dn_template) +
	    strlen(key->username) + strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002128,
			"allocation of string 'dn' failed");
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_user_info_ops.dn_template,
	    key->username, gfarm_ldap_base_dn);
	return (dn);
}

static gfarm_error_t
gfarm_ldap_user_info_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct gfarm_user_info *info = vinfo;
	static const char diag[] = "gfarm_ldap_user_info_set_field";

	if (strcasecmp(attribute, "username") == 0) {
		info->username = strdup_log(vals[0], diag);
		if (info->username == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "realname") == 0) {
		/* XXX FIXME - hack to allow null string */
		if (strcmp(vals[0], " ") == 0)
			info->realname = strdup_log("", diag);
		else
			info->realname = strdup_log(vals[0], diag);
		if (info->realname == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "homedir") == 0) {
		/* XXX FIXME - hack to allow null string */
		if (strcmp(vals[0], " ") == 0)
			info->homedir = strdup_log("", diag);
		else
			info->homedir = strdup_log(vals[0], diag);
		if (info->homedir == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "gsiDN") == 0) {
		/* XXX FIXME - hack to allow null string */
		if (strcmp(vals[0], " ") == 0)
			info->gsi_dn = strdup_log("", diag);
		else
			info->gsi_dn = strdup_log(vals[0], diag);
		if (info->gsi_dn == NULL)
			err = GFARM_ERR_NO_MEMORY;
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_user_info_update(
	struct gfarm_user_info *info,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[6];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	struct gfarm_ldap_user_info_key key;

	key.username = info->username;

	i = 0;
	set_string_mod(&modv[i], mod_op,
	    "objectclass", "GFarmUser", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "username", info->username, &storage[i]);
	i++;
	/* XXX FIXME - hack to allow null string */
	set_string_mod(&modv[i], mod_op,
	    "realname", *info->realname ? info->realname : " ", &storage[i]);
	i++;
	/* XXX FIXME - hack to allow null string */
	set_string_mod(&modv[i], mod_op,
	    "homedir", *info->homedir ? info->homedir : " ", &storage[i]);
	i++;
	/* XXX FIXME - hack to allow null string */
	set_string_mod(&modv[i], mod_op,
	    "gsiDN", *info->gsi_dn ? info->gsi_dn : " ", &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return ((*update_op)(&key, modv, &gfarm_ldap_user_info_ops));
}

static gfarm_error_t
gfarm_ldap_user_add(gfarm_uint64_t seqnum, struct gfarm_user_info *info)
{
	gfarm_error_t e;
	e = gfarm_ldap_user_info_update(info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_add);
	free(info);
	return (e);
}

static gfarm_error_t
gfarm_ldap_user_modify(gfarm_uint64_t seqnum, struct db_user_modify_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_user_info_update(&arg->ui,
	    LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_user_remove(gfarm_uint64_t seqnum, char *username)
{
	gfarm_error_t e;
	struct gfarm_ldap_user_info_key key;

	key.username = username;

	e = gfarm_ldap_generic_info_remove(&key, &gfarm_ldap_user_info_ops);
	free(username);
	return (e);
}

static gfarm_error_t
gfarm_ldap_user_load(void *closure,
	void (*callback)(void *, struct gfarm_user_info *))
{
	struct gfarm_user_info tmp_info;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_ldap_user_info_ops.query_type,
	    &tmp_info, (void (*)(void *, void *))callback, closure,
	    &gfarm_ldap_user_info_ops));
}

/**********************************************************************/

static char *gfarm_ldap_group_info_make_dn(void *vkey);
static gfarm_error_t gfarm_ldap_group_info_set_field(void *info,
	char *attribute, char **vals);

struct gfarm_ldap_group_info_key {
	const char *groupname;
};

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_group_info_ops = {
	&gfarm_base_group_info_ops,
	"(objectclass=GFarmGroup)",
	"groupname=%s, %s",
	gfarm_ldap_group_info_make_dn,
	gfarm_ldap_group_info_set_field,
};

static char *
gfarm_ldap_group_info_make_dn(void *vkey)
{
	struct gfarm_ldap_group_info_key *key = vkey;
	char *dn;

	GFARM_MALLOC_ARRAY(dn, strlen(gfarm_ldap_group_info_ops.dn_template) +
	    strlen(key->groupname) + strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002129,
			"allocation of string 'dn' failed");
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_group_info_ops.dn_template,
	    key->groupname, gfarm_ldap_base_dn);
	return (dn);
}

static gfarm_error_t
gfarm_ldap_group_info_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct gfarm_group_info *info = vinfo;
	static const char diag[] = "gfarm_ldap_group_info_set_field";

	if (strcasecmp(attribute, "groupname") == 0) {
		info->groupname = strdup_log(vals[0], diag);
		if (info->groupname == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "groupusers") == 0) {
		info->usernames = gfarm_strarray_dup_log(vals, diag);
		if (info->usernames == NULL) {
			info->nusers = 0;
			err = GFARM_ERR_NO_MEMORY;
		} else {
			info->nusers = gfarm_strarray_length(info->usernames);
		}
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_group_info_update(
	struct gfarm_group_info *info,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[4];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	LDAPMod groupusers_mod;

	struct gfarm_ldap_group_info_key key;

	key.groupname = info->groupname;

	i = 0;
	set_string_mod(&modv[i], mod_op,
	    "objectclass", "GFarmGroup", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "groupname", info->groupname, &storage[i]);
	i++;

	/* "groupusers" is optional */
	if (info->usernames != NULL && info->nusers > 0) {
		groupusers_mod.mod_type = "groupusers";
		groupusers_mod.mod_op = mod_op;
		groupusers_mod.mod_vals.modv_strvals = info->usernames;
		modv[i] = &groupusers_mod;
		i++;
	}

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv) || i == ARRAY_LENGTH(modv) - 1);

	return ((*update_op)(&key, modv, &gfarm_ldap_group_info_ops));
}

static gfarm_error_t
gfarm_ldap_group_add(gfarm_uint64_t seqnum, struct gfarm_group_info *info)
{
	gfarm_error_t e;
	e = gfarm_ldap_group_info_update(info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_add);
	free(info);
	return (e);
}

static gfarm_error_t
gfarm_ldap_group_modify(gfarm_uint64_t seqnum,
	struct db_group_modify_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_group_info_update(&arg->gi,
	    LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_group_remove(gfarm_uint64_t seqnum, char *groupname)
{
	gfarm_error_t e;
	struct gfarm_ldap_group_info_key key;

	key.groupname = groupname;

	e = gfarm_ldap_generic_info_remove(&key, &gfarm_ldap_group_info_ops);
	free(groupname);
	return (e);
}

static gfarm_error_t
gfarm_ldap_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	struct gfarm_group_info tmp_info;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_ldap_group_info_ops.query_type,
	    &tmp_info, (void (*)(void *, void *))callback, closure,
	    &gfarm_ldap_group_info_ops));
}

/**********************************************************************/

static char *gfarm_ldap_db_inode_inum_make_dn(void *vkey);
static gfarm_error_t gfarm_ldap_gfs_stat_set_field(void *info,
	char *attribute, char **vals);

static char INODE_QUERY_TYPE[] = "(objectclass=GFarmINode)";
static char INODE_DN_TEMPLATE[] = "inumber=%" GFARM_PRId64 ", %s";

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_gfs_stat_ops = {
	&gfarm_base_gfs_stat_ops,
	INODE_QUERY_TYPE,
	INODE_DN_TEMPLATE,
	gfarm_ldap_db_inode_inum_make_dn,
	gfarm_ldap_gfs_stat_set_field,
};

static char *
gfarm_ldap_db_inode_inum_make_dn(void *vkey)
{
	struct db_inode_inum_arg *key = vkey;
	char *dn;

	GFARM_MALLOC_ARRAY(dn, strlen(gfarm_ldap_gfs_stat_ops.dn_template) +
	    INT64STRLEN + strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002130,
			"allocation of string 'dn' failed");
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_gfs_stat_ops.dn_template,
	    key->inum, gfarm_ldap_base_dn);
	return (dn);
}

static gfarm_error_t
gfarm_ldap_gfs_stat_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct gfs_stat *info = vinfo;
	static const char diag[] = "gfarm_ldap_gfs_stat_set_field";

	if (strcasecmp(attribute, "inumber") == 0) {
		info->st_ino = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "igen") == 0) {
		info->st_gen = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "mode") == 0) {
		info->st_mode = strtol(vals[0], NULL, 8);
	} else if (strcasecmp(attribute, "nlink") == 0) {
		info->st_nlink = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "size") == 0) {
		info->st_size = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "username") == 0) {
		info->st_user = strdup_log(vals[0], diag);
		if (info->st_user == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "groupname") == 0) {
		info->st_group = strdup_log(vals[0], diag);
		if (info->st_group == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "atimesec") == 0) {
		info->st_atimespec.tv_sec = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "atimensec") == 0) {
		info->st_atimespec.tv_nsec = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "mtimesec") == 0) {
		info->st_mtimespec.tv_sec = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "mtimensec") == 0) {
		info->st_mtimespec.tv_nsec = strtol(vals[0], NULL, 0);
	} else if (strcasecmp(attribute, "ctimesec") == 0) {
		info->st_ctimespec.tv_sec = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "ctimensec") == 0) {
		info->st_ctimespec.tv_nsec = strtol(vals[0], NULL, 0);
	}
	info->st_ncopy = 0;
	return (err);
}

static gfarm_error_t
gfarm_ldap_gfs_stat_update(
	struct gfs_stat *info,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[15];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	char ino_string[INT64STRLEN + 1];
	char igen_string[INT64STRLEN + 1];
	char mode_string[INT32STRLEN + 1];
	char nlink_string[INT64STRLEN + 1];
	char size_string[INT64STRLEN + 1];
	char atime_sec_string[INT64STRLEN + 1];
	char atime_nsec_string[INT32STRLEN + 1];
	char mtime_sec_string[INT64STRLEN + 1];
	char mtime_nsec_string[INT32STRLEN + 1];
	char ctime_sec_string[INT64STRLEN + 1];
	char ctime_nsec_string[INT32STRLEN + 1];

	struct db_inode_inum_arg key;

	key.inum = info->st_ino;

	sprintf(ino_string, "%" GFARM_PRId64, info->st_ino);
	sprintf(igen_string, "%" GFARM_PRId64, info->st_gen);
	sprintf(mode_string, "%07o", info->st_mode);
	sprintf(nlink_string, "%" GFARM_PRId64, info->st_nlink);
	sprintf(size_string, "%" GFARM_PRId64, info->st_size);
	sprintf(atime_sec_string, "%" GFARM_PRId64, info->st_atimespec.tv_sec);
	sprintf(atime_nsec_string, "%d", info->st_atimespec.tv_nsec);
	sprintf(mtime_sec_string, "%" GFARM_PRId64, info->st_mtimespec.tv_sec);
	sprintf(mtime_nsec_string, "%d", info->st_mtimespec.tv_nsec);
	sprintf(ctime_sec_string, "%" GFARM_PRId64, info->st_ctimespec.tv_sec);
	sprintf(ctime_nsec_string, "%d", info->st_ctimespec.tv_nsec);

	i = 0;
	set_string_mod(&modv[i], mod_op,
	    "objectclass", "GFarmINode", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "inumber", ino_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "igen", igen_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "mode", mode_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "nlink", nlink_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "size", size_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "username", info->st_user, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "groupname", info->st_group, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "atimesec", atime_sec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "atimensec", atime_nsec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "mtimesec", mtime_sec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "mtimensec", mtime_nsec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "ctimesec", ctime_sec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "ctimensec", ctime_nsec_string, &storage[i]);
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return ((*update_op)(&key, modv, &gfarm_ldap_gfs_stat_ops));
}

static gfarm_error_t
gfarm_ldap_inode_add(gfarm_uint64_t seqnum, struct gfs_stat *info)
{
	gfarm_error_t e;
	e = gfarm_ldap_gfs_stat_update(info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_add);
	free(info);
	return (e);
}

static gfarm_error_t
gfarm_ldap_inode_modify(gfarm_uint64_t seqnum, struct gfs_stat *info)
{
	gfarm_error_t e;
	e = gfarm_ldap_gfs_stat_update(info,
	    LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify);
	free(info);
	return (e);
}

static gfarm_error_t
ldap_inode_uint64_modify(struct db_inode_uint64_modify_arg *arg, char *type)
{
	gfarm_error_t e;
	int i;
	LDAPMod *modv[2];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	char ino_string[INT64STRLEN + 1];
	char uint64_string[INT64STRLEN + 1];

	struct db_inode_inum_arg key;

	key.inum = arg->inum;

	sprintf(ino_string, "%" GFARM_PRId64, arg->inum);
	sprintf(uint64_string, "%" GFARM_PRId64, arg->uint64);

	i = 0;
	set_string_mod(&modv[i], LDAP_MOD_REPLACE,
	    type, uint64_string, &storage[i]);
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	e = gfarm_ldap_generic_info_modify(&key, modv,
	    &gfarm_ldap_gfs_stat_ops);

	free(arg);
	return (e);
}

static gfarm_error_t
ldap_inode_string_modify(struct db_inode_string_modify_arg *arg, char *type)
{
	gfarm_error_t e;
	int i;
	LDAPMod *modv[2];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	char ino_string[INT64STRLEN + 1];

	struct db_inode_inum_arg key;

	key.inum = arg->inum;

	sprintf(ino_string, "%" GFARM_PRId64, arg->inum);

	i = 0;
	set_string_mod(&modv[i], LDAP_MOD_REPLACE,
	    type, arg->string, &storage[i]);
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	e = gfarm_ldap_generic_info_modify(&key, modv,
	    &gfarm_ldap_gfs_stat_ops);

	free(arg);
	return (e);
}

static gfarm_error_t
ldap_inode_timespec_modify(struct db_inode_timespec_modify_arg *arg,
	char *sec_type, char *nsec_type)
{
	gfarm_error_t e;
	int i;
	LDAPMod *modv[3];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	char ino_string[INT64STRLEN + 1];
	char sec_string[INT64STRLEN + 1];
	char nsec_string[INT32STRLEN + 1];

	struct db_inode_inum_arg key;

	key.inum = arg->inum;

	sprintf(ino_string, "%" GFARM_PRId64, arg->inum);
	sprintf(sec_string, "%" GFARM_PRId64, arg->time.tv_sec);
	sprintf(nsec_string, "%d", arg->time.tv_nsec);

	i = 0;
	set_string_mod(&modv[i], LDAP_MOD_REPLACE,
	    sec_type, sec_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], LDAP_MOD_REPLACE,
	    nsec_type, nsec_string, &storage[i]);
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	e = gfarm_ldap_generic_info_modify(&key, modv,
	    &gfarm_ldap_gfs_stat_ops);

	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_inode_gen_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (ldap_inode_uint64_modify(arg, "igen"));
}

static gfarm_error_t
gfarm_ldap_inode_nlink_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (ldap_inode_uint64_modify(arg, "nlink"));
}

static gfarm_error_t
gfarm_ldap_inode_size_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (ldap_inode_uint64_modify(arg, "size"));
}

static gfarm_error_t
gfarm_ldap_inode_mode_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint32_modify_arg *arg)
{
	gfarm_error_t e;
	int i;
	LDAPMod *modv[2];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	char ino_string[INT64STRLEN + 1];
	char mode_string[INT32STRLEN + 1];

	struct db_inode_inum_arg key;

	key.inum = arg->inum;

	sprintf(ino_string, "%" GFARM_PRId64, arg->inum);
	sprintf(mode_string, "%07o", arg->uint32);

	i = 0;
	set_string_mod(&modv[i], LDAP_MOD_REPLACE,
	    "mode", mode_string, &storage[i]);
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	e = gfarm_ldap_generic_info_modify(&key, modv,
	    &gfarm_ldap_gfs_stat_ops);

	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_inode_user_modify(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	return (ldap_inode_string_modify(arg, "username"));
}

static gfarm_error_t
gfarm_ldap_inode_group_modify(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	return (ldap_inode_string_modify(arg, "groupname"));
}

static gfarm_error_t
gfarm_ldap_inode_atime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (ldap_inode_timespec_modify(arg, "atimesec", "atimensec"));
}

static gfarm_error_t
gfarm_ldap_inode_mtime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (ldap_inode_timespec_modify(arg, "mtimesec", "mtimensec"));
}

static gfarm_error_t
gfarm_ldap_inode_ctime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (ldap_inode_timespec_modify(arg, "ctimesec", "ctimensec"));
}


static gfarm_error_t
gfarm_ldap_inode_load(
	void *closure,
	void (*callback)(void *, struct gfs_stat *))
{
	struct gfs_stat tmp_info;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_ldap_gfs_stat_ops.query_type,
	    &tmp_info, (void (*)(void *, void *))callback, closure,
	    &gfarm_ldap_gfs_stat_ops));
}

/**********************************************************************/

static gfarm_error_t gfarm_ldap_inode_cksum_set_field(void *info,
	char *attribute, char **vals);

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_inode_cksum_ops = {
	&db_base_inode_cksum_arg_ops,
	INODE_QUERY_TYPE,
	INODE_DN_TEMPLATE,
	gfarm_ldap_db_inode_inum_make_dn,
	gfarm_ldap_inode_cksum_set_field,
};

static gfarm_error_t
gfarm_ldap_inode_cksum_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct db_inode_cksum_arg *info = vinfo;
	static const char diag[] = "gfarm_ldap_inode_cksum_set_field";

	if (strcasecmp(attribute, "inumber") == 0) {
		info->inum = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "checksumType") == 0) {
		info->type = strdup_log(vals[0], diag);
		if (info->type == NULL)
			err = GFARM_ERR_NO_MEMORY;
	} else if (strcasecmp(attribute, "checksum") == 0) {
		info->sum = strdup_log(vals[0], diag);
		if (info->sum == NULL) {
			info->sum = 0;
			err = GFARM_ERR_NO_MEMORY;
		} else {
			info->len = strlen(info->sum);
		}
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_inode_cksum_update(
	struct db_inode_cksum_arg *info,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[3];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	struct db_inode_inum_arg key;

	key.inum = info->inum;

	i = 0;
	set_string_mod(&modv[i], mod_op,
	    "checksumType", info->type, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "checksum", info->sum, &storage[i]);
	i++;

	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return ((*update_op)(&key, modv, &gfarm_ldap_inode_cksum_ops));
}

static gfarm_error_t
gfarm_ldap_inode_cksum_add(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_inode_cksum_update(arg,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_modify);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_inode_cksum_modify(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_inode_cksum_update(arg,
	    LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_inode_cksum_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	gfarm_error_t e;
	int i;
	LDAPMod *modv[3];
	LDAPMod storage[ARRAY_LENGTH(modv) - 1];

	i = 0;
	set_delete_mod(&modv[i], "checksumType", &storage[i]);
	i++;
	set_delete_mod(&modv[i], "checksum", &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	e = gfarm_ldap_generic_info_modify(arg, modv,
	    &gfarm_ldap_inode_cksum_ops);

	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_inode_cksum_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	struct db_inode_cksum_arg tmp_info;
	struct db_inode_cksum_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_ldap_inode_cksum_ops.query_type,
	    &tmp_info, db_inode_cksum_callback_trampoline, &c,
	    &gfarm_ldap_inode_cksum_ops));
}

/**********************************************************************/

static char *gfarm_ldap_db_filecopy_make_dn(void *vkey);
static gfarm_error_t gfarm_ldap_db_filecopy_set_field(void *info,
	char *attribute, char **vals);

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_db_filecopy_ops = {
	&db_base_filecopy_arg_ops,
	"(objectclass=GFarmFileCopy)",
	"hostname=%s, inumber=%" GFARM_PRId64 ", %s",
	gfarm_ldap_db_filecopy_make_dn,
	gfarm_ldap_db_filecopy_set_field,
};

static char *
gfarm_ldap_db_filecopy_make_dn(void *vkey)
{
	struct db_filecopy_arg *key = vkey;
	char *dn;

	GFARM_MALLOC_ARRAY(dn, strlen(gfarm_ldap_db_filecopy_ops.dn_template)
	    + strlen(key->hostname) + INT64STRLEN +
	    + strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002131,
			"allocation of string 'dn' failed");
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_db_filecopy_ops.dn_template,
	    key->hostname, key->inum, gfarm_ldap_base_dn);
	return (dn);
}

static gfarm_error_t
gfarm_ldap_db_filecopy_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct db_filecopy_arg *info = vinfo;
	static const char diag[] = "gfarm_ldap_db_filecopy_set_field";

	if (strcasecmp(attribute, "inumber") == 0) {
		info->inum = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "hostname") == 0) {
		info->hostname = strdup_log(vals[0], diag);
		if (info->hostname == NULL)
			err = GFARM_ERR_NO_MEMORY;
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_db_filecopy_update(
	struct db_filecopy_arg *info,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[4];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	char ino_string[INT64STRLEN + 1];

	sprintf(ino_string, "%" GFARM_PRId64, info->inum);

	i = 0;
	set_string_mod(&modv[i], mod_op,
	    "objectclass", "GFarmFileCopy", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "inumber", ino_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "hostname", info->hostname, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return ((*update_op)(info, modv, &gfarm_ldap_db_filecopy_ops));
}

static gfarm_error_t
gfarm_ldap_filecopy_add(gfarm_uint64_t seqnum, struct db_filecopy_arg *info)
{
	gfarm_error_t e;
	e = gfarm_ldap_db_filecopy_update(info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_add);
	free(info);
	return (e);
}

static gfarm_error_t
gfarm_ldap_filecopy_remove(gfarm_uint64_t seqnum, struct db_filecopy_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_generic_info_remove(arg, &gfarm_ldap_db_filecopy_ops);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_filecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	struct db_filecopy_arg tmp_info;
	struct db_filecopy_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_SUBTREE, gfarm_ldap_db_filecopy_ops.query_type,
	    &tmp_info, db_filecopy_callback_trampoline, &c,
	    &gfarm_ldap_db_filecopy_ops));
}

/**********************************************************************/

static char *gfarm_ldap_db_deadfilecopy_make_dn(void *vkey);
static gfarm_error_t gfarm_ldap_db_deadfilecopy_set_field(void *info,
	char *attribute, char **vals);

static const struct gfarm_ldap_generic_info_ops
    gfarm_ldap_db_deadfilecopy_ops = {
	&db_base_deadfilecopy_arg_ops,
	"(objectclass=GFarmDeadFileCopy)",
	/* XXX FIXME - the following assumption is wrong: delay of removal */
	/* There should not be two entries for the same inumber */
	"hostname=%s, inumber=%" GFARM_PRId64 ", %s",
	gfarm_ldap_db_deadfilecopy_make_dn,
	gfarm_ldap_db_deadfilecopy_set_field,
};

static char *
gfarm_ldap_db_deadfilecopy_make_dn(void *vkey)
{
	struct db_deadfilecopy_arg *key = vkey;
	char *dn;

	GFARM_MALLOC_ARRAY(dn,
	    strlen(gfarm_ldap_db_deadfilecopy_ops.dn_template) +
	    strlen(key->hostname) + INT64STRLEN +
	    strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002132,
			"allocation of string 'dn' failed");
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_db_deadfilecopy_ops.dn_template,
	    key->hostname, key->inum, gfarm_ldap_base_dn);
	return (dn);
}

static gfarm_error_t
gfarm_ldap_db_deadfilecopy_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct db_deadfilecopy_arg *info = vinfo;
	static const char diag[] = "gfarm_ldap_db_deadfilecopy_set_field";

	if (strcasecmp(attribute, "inumber") == 0) {
		info->inum = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "igen") == 0) {
		info->igen = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "hostname") == 0) {
		info->hostname = strdup_log(vals[0], diag);
		if (info->hostname == NULL)
			err = GFARM_ERR_NO_MEMORY;
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_db_deadfilecopy_update(
	struct db_deadfilecopy_arg *info,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[5];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	char ino_string[INT64STRLEN + 1];
	char igen_string[INT64STRLEN + 1];

	sprintf(ino_string, "%" GFARM_PRId64, info->inum);
	sprintf(igen_string, "%" GFARM_PRId64, info->igen);

	i = 0;
	set_string_mod(&modv[i], mod_op,
	    "objectclass", "GFarmDeadFileCopy", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "inumber", ino_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "igen", igen_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "hostname", info->hostname, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return ((*update_op)(info, modv, &gfarm_ldap_db_deadfilecopy_ops));
}

static gfarm_error_t
gfarm_ldap_deadfilecopy_add(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *info)
{
	gfarm_error_t e;
	e = gfarm_ldap_db_deadfilecopy_update(info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_add);
	free(info);
	return (e);
}

static gfarm_error_t
gfarm_ldap_deadfilecopy_remove(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_generic_info_remove(
	    arg, &gfarm_ldap_db_deadfilecopy_ops);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_deadfilecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	struct db_deadfilecopy_arg tmp_info;
	struct db_deadfilecopy_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_SUBTREE, gfarm_ldap_db_deadfilecopy_ops.query_type,
	    &tmp_info, db_deadfilecopy_callback_trampoline, &c,
	    &gfarm_ldap_db_deadfilecopy_ops));
}

/**********************************************************************/

static char *gfarm_ldap_db_direntry_make_dn(void *vkey);
static gfarm_error_t gfarm_ldap_db_direntry_set_field(void *info,
	char *attribute, char **vals);

static const struct gfarm_ldap_generic_info_ops
    gfarm_ldap_db_direntry_ops = {
	&db_base_direntry_arg_ops,
	"(objectclass=GFarmDirEntry)",
	"entryName=%s, inumber=%" GFARM_PRId64 ", %s",
	gfarm_ldap_db_direntry_make_dn,
	gfarm_ldap_db_direntry_set_field,
};

/* see RFC 2253 (Section 2.4 and 3) for detail */
static void
gfarm_ldap_quote_string(char *quoted, const char *string, int slen)
{
	int i;
	unsigned char c;

	*quoted++ = '"';
	for (i = 0; i < slen; i++) {
		c = string[i];
		if (c < ' ') {
			*quoted++ = '\\';
			*quoted++ = c / 0x10;
			*quoted++ = c % 0x10;
		} else {
			if (c == '"' || c == '\\')
				*quoted++ = '\\';
			*quoted++ = c;
		}
	}
	*quoted++ = '"';
	*quoted++ = '\0';
}

/* this implementation should match with gfarm_ldap_quote_string() */
static int
gfarm_ldap_quote_string_length(const char *string, int slen)
{
	int i, qlen = 0;
	unsigned char c;

	++qlen;
	for (i = 0; i < slen; i++) {
		c = string[i];
		if (c < ' ') {
			qlen += 3;
		} else {
			if (c == '"' || c == '\\')
				++qlen;
			++qlen;
		}
	}
	qlen += 2;
	return (qlen);
}

static char *
gfarm_ldap_db_direntry_make_dn(void *vkey)
{
	struct db_direntry_arg *key = vkey;
	char *dn, *quoted;
	int quoted_len;

	quoted_len =
	    gfarm_ldap_quote_string_length(key->entry_name, key->entry_len);
	GFARM_MALLOC_ARRAY(quoted, quoted_len);
	if (quoted == NULL) {
		gflog_debug(GFARM_MSG_1002133,
			"allocation of string 'quoted' failed");
		return (NULL);
	}
	gfarm_ldap_quote_string(quoted, key->entry_name, key->entry_len);

	GFARM_MALLOC_ARRAY(dn, strlen(gfarm_ldap_db_direntry_ops.dn_template) +
	    quoted_len + INT64STRLEN + strlen(gfarm_ldap_base_dn) + 1);
	if (dn != NULL)
		sprintf(dn, gfarm_ldap_db_direntry_ops.dn_template,
		    quoted, key->dir_inum, gfarm_ldap_base_dn);
	else
		gflog_debug(GFARM_MSG_1002134,
			"allocation of string 'dn' failed");

	free(quoted);
	return (dn);
}

static gfarm_error_t
gfarm_ldap_db_direntry_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct db_direntry_arg *info = vinfo;
	static const char diag[] = "gfarm_ldap_db_direntry_set_field";

	if (strcasecmp(attribute, "inumber") == 0) {
		info->dir_inum = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "entryName") == 0) {
		info->entry_name = strdup_log(vals[0], diag);
		if (info->entry_name == NULL) {
			info->entry_len = 0;
			err = GFARM_ERR_NO_MEMORY;
		} else {
			info->entry_len = strlen(info->entry_name);
		}
	} else if (strcasecmp(attribute, "entryINumber") == 0) {
		info->entry_inum = gfarm_strtoi64(vals[0], NULL);
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_db_direntry_update(
	struct db_direntry_arg *info,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	int i;
	LDAPMod *modv[5];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];

	char ino_string[INT64STRLEN + 1];
	char entry_ino_string[INT64STRLEN + 1];

	struct db_direntry_arg key;

	key.dir_inum = info->dir_inum;
	key.entry_name = info->entry_name;
	key.entry_len = info->entry_len;

	sprintf(ino_string, "%" GFARM_PRId64, info->dir_inum);
	sprintf(entry_ino_string, "%" GFARM_PRId64, info->entry_inum);

	i = 0;
	set_string_mod(&modv[i], mod_op,
	    "objectclass", "GFarmDirEntry", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "inumber", ino_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "entryName", info->entry_name, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "entryINumber", entry_ino_string, &storage[i]);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	return ((*update_op)(&key, modv, &gfarm_ldap_db_direntry_ops));
}

static gfarm_error_t
gfarm_ldap_direntry_add(gfarm_uint64_t seqnum,
	struct db_direntry_arg *info)
{
	gfarm_error_t e;
	e = gfarm_ldap_db_direntry_update(info,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_add);
	free(info);
	return (e);
}

static gfarm_error_t
gfarm_ldap_direntry_remove(gfarm_uint64_t seqnum,
	struct db_direntry_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_generic_info_remove(arg, &gfarm_ldap_db_direntry_ops);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_direntry_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	struct db_direntry_arg tmp_info;
	struct db_direntry_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_SUBTREE, gfarm_ldap_db_direntry_ops.query_type,
	    &tmp_info, db_direntry_callback_trampoline, &c,
	    &gfarm_ldap_db_direntry_ops));
}

/**********************************************************************/

static gfarm_error_t gfarm_ldap_db_symlink_set_field(void *info,
	char *attribute, char **vals);

static const struct gfarm_ldap_generic_info_ops
    gfarm_ldap_db_symlink_ops = {
	&db_base_symlink_arg_ops,
	"(objectclass=GFarmSymlink)",
	"inumber=%" GFARM_PRId64 ", %s",
	gfarm_ldap_db_inode_inum_make_dn,
	gfarm_ldap_db_symlink_set_field,
};

static gfarm_error_t
gfarm_ldap_db_symlink_set_field(
	void *vinfo,
	char *attribute,
	char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct db_symlink_arg *info = vinfo;
	static const char diag[] = "gfarm_ldap_db_symlink_set_field";

	if (strcasecmp(attribute, "inumber") == 0) {
		info->inum = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "sourcePath") == 0) {
		info->source_path = strdup_log(vals[0], diag);
		if (info->source_path == NULL)
			err = GFARM_ERR_NO_MEMORY;
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_symlink_add(gfarm_uint64_t seqnum,
	struct db_symlink_arg *info)
{
	gfarm_error_t e;
	int i;
	LDAPMod *modv[3];
	LDAPMod objectclass_modify;
	static char *objectclass_value[] = {
		"GFarmINode", "GFarmSymlink", NULL
	};
	struct ldap_string_modify path_storage;

	struct db_inode_inum_arg key;

	key.inum = info->inum;

	i = 0;

	objectclass_modify.mod_op = LDAP_MOD_REPLACE;
	objectclass_modify.mod_type = "objectclass";
	objectclass_modify.mod_vals.modv_strvals = objectclass_value;
	modv[i] = &objectclass_modify;
	i++;

	set_string_mod(&modv[i], LDAP_MOD_REPLACE,
	    "sourcePath", info->source_path, &path_storage);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	/* use "modify" instead of "add", since this is an AUXILIARY object */
	e = gfarm_ldap_generic_info_modify(&key, modv,
	    &gfarm_ldap_db_symlink_ops);

	free(info);
	return (e);
}

static gfarm_error_t
gfarm_ldap_symlink_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	gfarm_error_t e;
	int i;
	LDAPMod *modv[3];
	struct ldap_string_modify objectclass_storage;
	LDAPMod source_path_storage;

	struct db_inode_inum_arg key;

	key.inum = arg->inum;

	i = 0;
	set_string_mod(&modv[i], LDAP_MOD_REPLACE,
	    "objectclass", "GFarmINode", &objectclass_storage);
	i++;
	set_delete_mod(&modv[i], "sourcePath", &source_path_storage);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	/*
	 * use "modify" instead of "remove", since this is an AUXILIARY object
	 */
	e = gfarm_ldap_generic_info_modify(&key, modv,
	    &gfarm_ldap_db_symlink_ops);

	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_symlink_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	struct db_symlink_arg tmp_info;
	struct db_symlink_trampoline_closure c;

	c.closure = closure;
	c.callback = callback;

	return (gfarm_ldap_generic_info_get_foreach(gfarm_ldap_base_dn,
	    LDAP_SCOPE_ONELEVEL, gfarm_ldap_db_symlink_ops.query_type,
	    &tmp_info, db_symlink_callback_trampoline, &c,
	    &gfarm_ldap_db_symlink_ops));
}

/**********************************************************************/

static char *gfarm_ldap_xattr_info_make_dn(void *vkey);
static gfarm_error_t gfarm_ldap_db_xattr_set_field(void *vinfo,
	char *attribute, char **vals);
static gfarm_error_t gfarm_ldap_db_xmlattr_set_field(void *vinfo,
	char *attribute, char **vals);

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_xattr_info_ops = {
	&gfarm_base_xattr_info_ops,
	"(objectclass=XAttr)",
	"attrname=%s, inumber=%" GFARM_PRId64 ", %s",
	gfarm_ldap_xattr_info_make_dn,
	gfarm_ldap_db_xattr_set_field,
};

static const struct gfarm_ldap_generic_info_ops gfarm_ldap_xmlattr_info_ops = {
	&gfarm_base_xattr_info_ops,
	"(objectclass=XAttr)",
	"attrname=%s, inumber=%" GFARM_PRId64 ", %s",
	gfarm_ldap_xattr_info_make_dn,
	gfarm_ldap_db_xmlattr_set_field,
};

struct gfarm_ldap_xattr_get_info_key {
	gfarm_ino_t inumber;
	const char *attrname;
};

static char *
gfarm_ldap_xattr_info_make_dn(void *vkey)
{
	struct gfarm_ldap_xattr_get_info_key *key = vkey;
	char *dn;

	GFARM_MALLOC_ARRAY(dn, strlen(gfarm_ldap_xattr_info_ops.dn_template) +
		strlen(key->attrname) + INT64STRLEN
		+ strlen(gfarm_ldap_base_dn) + 1);
	if (dn == NULL) {
		gflog_debug(GFARM_MSG_1002135,
			"allocation of string 'dn' failed");
		return (NULL);
	}
	sprintf(dn, gfarm_ldap_xattr_info_ops.dn_template,
	    key->attrname, key->inumber, gfarm_ldap_base_dn);
	return (dn);
}

/* NOTE: if xmlMode, attrvalue is unnecessary to load. */
static gfarm_error_t
gfarm_ldap_db_xmlattr_set_field(void *vinfo, char *attribute, char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct xattr_info *info = vinfo;
	static const char diag[] = "gfarm_ldap_db_x(ml)attr_set_field";

	if (strcasecmp(attribute, "inumber") == 0) {
		info->inum = gfarm_strtoi64(vals[0], NULL);
	} else if (strcasecmp(attribute, "attrname") == 0) {
		info->attrname = strdup_log(vals[0], diag);
		if (info->attrname == NULL) {
			info->namelen = 0;
			err = GFARM_ERR_NO_MEMORY;
		} else { /* include '\0' */
			info->namelen = strlen(info->attrname) + 1;
		}
	}
	return (err);
}

static gfarm_error_t
gfarm_ldap_db_xattr_set_field(void *vinfo, char *attribute, char **vals)
{
	gfarm_error_t err = GFARM_ERR_NO_ERROR;
	struct xattr_info *info = vinfo;
	struct berval **bervals;

	if (strcasecmp(attribute, "attrvalue") == 0) {
		bervals = (struct berval **)vals;
		info->attrsize = bervals[0]->bv_len;
		if (info->attrsize <= 0) {
			info->attrvalue = NULL;
		} else {
			info->attrvalue = malloc(info->attrsize);
			if (info->attrvalue == NULL) {
				gflog_debug(GFARM_MSG_1002512,
				    "failed to allocate %d bytes xattr value",
				    info->attrsize);
				info->attrsize = 0;
				err = GFARM_ERR_NO_MEMORY;
			} else {
				memcpy(info->attrvalue, bervals[0]->bv_val,
				    info->attrsize);
			}
		}
		return (err);
	} else
		return (gfarm_ldap_db_xmlattr_set_field(
		    vinfo, attribute, vals));
}

static gfarm_error_t
gfarm_ldap_xattr_update(
	struct db_xattr_arg *arg,
	int mod_op,
	gfarm_error_t (*update_op)(void *, LDAPMod **,
	    const struct gfarm_ldap_generic_info_ops *))
{
	gfarm_error_t e;
	int i;
	LDAPMod *modv[5];
	struct ldap_string_modify storage[ARRAY_LENGTH(modv) - 1];
	struct ldap_binary_modify binstorage;
	char ino_string[INT64STRLEN + 1];
	struct gfarm_ldap_xattr_get_info_key key;

	if (arg->xmlMode)
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);

	key.inumber = arg->inum;
	key.attrname = arg->attrname;

	sprintf(ino_string, "%" GFARM_PRId64, arg->inum);

	i = 0;
	set_string_mod(&modv[i], mod_op,
		"objectclass", "XAttr", &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "inumber", ino_string, &storage[i]);
	i++;
	set_string_mod(&modv[i], mod_op,
	    "attrname", (char *)arg->attrname, &storage[i]);
	i++;
	set_binary_mod(&modv[i], mod_op,
		"attrvalue", (void *)arg->value, arg->size, &binstorage);
	i++;
	modv[i++] = NULL;
	assert(i == ARRAY_LENGTH(modv));

	e = ((*update_op)(&key, modv, &gfarm_ldap_xattr_info_ops));
	return (e);
}

static gfarm_error_t
gfarm_ldap_xattr_add(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_xattr_update(arg,
	    LDAP_MOD_ADD, gfarm_ldap_generic_info_add);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_xattr_modify(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	e = gfarm_ldap_xattr_update(arg,
		LDAP_MOD_REPLACE, gfarm_ldap_generic_info_modify);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_xattr_remove(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	struct gfarm_ldap_xattr_get_info_key key;

	if (arg->xmlMode)
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);

	key.inumber = arg->inum;
	key.attrname = arg->attrname;

	e = gfarm_ldap_generic_info_remove(&key, &gfarm_ldap_xattr_info_ops);
	free(arg);
	return (e);
}

static gfarm_error_t
gfarm_ldap_xattr_get(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	char *query_type;
	char *attrs[2] = { "attrvalue", NULL };
	struct timeval tout = { 10, 0 };
	int cnt, rv;
	LDAPMessage  *res = NULL, *m;
	struct berval **vals;
	void *p;

	if (arg->xmlMode) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		gflog_debug(GFARM_MSG_1002136,
			"operation not supported(xmlMode is true)");
		goto quit;
	}

	GFARM_MALLOC_ARRAY(query_type, 64+INT64STRLEN+strlen(arg->attrname));
	if (query_type == NULL) {
		gflog_debug(GFARM_MSG_1002137,
			"allocation of string 'query_type' failed");
		e = GFARM_ERR_NO_MEMORY;
		goto quit;
	}

	sprintf(query_type,
		"(&(objectclass=XAttr)(inumber=%"GFARM_PRId64")(attrname=%s))",
		arg->inum, arg->attrname);
	rv = ldap_search_st(gfarm_ldap_server, gfarm_ldap_base_dn,
		LDAP_SCOPE_SUBTREE, query_type, attrs, 0, &tout, &res);
	free(query_type);
	if (rv == LDAP_NO_SUCH_OBJECT) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_debug(GFARM_MSG_1002138,
			"ldap_search_st() failed: %s",
			gfarm_error_string(e));
		goto quit;
	} else if ((rv != LDAP_SUCCESS) || (res == NULL)) {
		e = GFARM_ERR_UNKNOWN;
		gflog_debug(GFARM_MSG_1002139,
			"unknown error occurred in ldap_search_st()");
		goto quit;
	}

	cnt = ldap_count_messages(gfarm_ldap_server, res);
	if (cnt <= 1) {
		/* NOTE: cnt==1 means res have a NULL message only */
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_debug(GFARM_MSG_1002140,
			"ldap count messages() <= 1");
	} else if (cnt != 2) {
		/* NOTE: res must have a valid message and a NULL message */
		e =  GFARM_ERR_UNKNOWN;
		gflog_debug(GFARM_MSG_1002141,
			"ldap_count_messages() != 2");
	} else if ((m = ldap_first_message(gfarm_ldap_server, res)) == NULL) {
		e = GFARM_ERR_UNKNOWN;
		gflog_error(GFARM_MSG_1002369,
		    "ldap_first_message: %s", gfarm_ldap_session_error());
	} else if ((vals = ldap_get_values_len(gfarm_ldap_server, m, attrs[0]))
	    == NULL) {
		e = GFARM_ERR_UNKNOWN;
		gflog_error(GFARM_MSG_1002370,
		    "ldap_get_values_len: %s", gfarm_ldap_session_error());
	} else {
		if ((vals[0] != NULL) && (vals[1] == NULL)) {
			e = GFARM_ERR_NO_ERROR;
			*arg->sizep = vals[0]->bv_len;
			if (vals[0]->bv_len > 0) {
				p = malloc(vals[0]->bv_len);
				if (p != NULL) {
					memcpy(p, vals[0]->bv_val,
						vals[0]->bv_len);
					*arg->valuep = p;
				} else {
					e = GFARM_ERR_NO_MEMORY;
					gflog_debug(GFARM_MSG_1002142,
						"allocation of 'vals' failed");
				}
			} else
				*arg->valuep = NULL;
		} else {
			e = GFARM_ERR_UNKNOWN;
			gflog_debug(GFARM_MSG_1002143,
				"unknown error occurred when "
				"getting first ldap message");
		}
		ldap_value_free_len(vals);
	}
	ldap_msgfree(res);
quit:
	free(arg);
	return (e);
}

gfarm_error_t
gfarm_ldap_xattr_load(void *closure,
		void (*callback)(void *, struct xattr_info *))
{
	int xmlMode = (closure != NULL) ? (*(int *)closure) : 0;
	struct xattr_info tmp_info;
	char *attrs[] = { "inumber", "attrname", NULL };

	if (xmlMode)
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);

	return (gfarm_ldap_generic_info_get_foreach_withattrs(
		gfarm_ldap_base_dn,
		LDAP_SCOPE_SUBTREE, gfarm_ldap_xattr_info_ops.query_type,
		&tmp_info, (void (*)(void *, void *))callback, &xmlMode,
		/* NOTE: if xmlMode, attrvalue is unnecessary to load. */
		xmlMode ?
		&gfarm_ldap_xmlattr_info_ops : &gfarm_ldap_xattr_info_ops,
		attrs));
}

static gfarm_error_t
gfarm_ldap_xmlattr_find(gfarm_uint64_t seqnum,
	struct db_xmlattr_find_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_ldap_quota_add(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	/* XXX not implemented yet */
	free(arg);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static gfarm_error_t
gfarm_ldap_quota_modify(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	/* XXX not implemented yet */
	free(arg);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static gfarm_error_t
gfarm_ldap_quota_remove(gfarm_uint64_t seqnum,
	struct db_quota_remove_arg *arg)
{
	/* XXX not implemented yet */
	free(arg);
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static gfarm_error_t
gfarm_ldap_quota_load(void *closure, int is_group,
		void (*callback)(void *, struct gfarm_quota_info *))
{
	/* XXX not implemented yet */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/**********************************************************************/
const struct db_ops db_ldap_ops = {
	gfarm_ldap_initialize,
	gfarm_ldap_terminate,

	gfarm_ldap_nop,
	gfarm_ldap_nop,

	gfarm_ldap_host_add,
	gfarm_ldap_host_modify,
	gfarm_ldap_host_remove,
	gfarm_ldap_host_load,

	gfarm_ldap_user_add,
	gfarm_ldap_user_modify,
	gfarm_ldap_user_remove,
	gfarm_ldap_user_load,

	gfarm_ldap_group_add,
	gfarm_ldap_group_modify,
	gfarm_ldap_group_remove,
	gfarm_ldap_group_load,

	gfarm_ldap_inode_add,
	gfarm_ldap_inode_modify,
	gfarm_ldap_inode_gen_modify,
	gfarm_ldap_inode_nlink_modify,
	gfarm_ldap_inode_size_modify,
	gfarm_ldap_inode_mode_modify,
	gfarm_ldap_inode_user_modify,
	gfarm_ldap_inode_group_modify,
	gfarm_ldap_inode_atime_modify,
	gfarm_ldap_inode_mtime_modify,
	gfarm_ldap_inode_ctime_modify,
	/* inode_remove: never remove any inode to keep inode->i_gen */
	gfarm_ldap_inode_load,

	/* cksum */
	gfarm_ldap_inode_cksum_add,
	gfarm_ldap_inode_cksum_modify,
	gfarm_ldap_inode_cksum_remove,
	gfarm_ldap_inode_cksum_load,

	gfarm_ldap_filecopy_add,
	gfarm_ldap_filecopy_remove,
	gfarm_ldap_filecopy_load,

	gfarm_ldap_deadfilecopy_add,
	gfarm_ldap_deadfilecopy_remove,
	gfarm_ldap_deadfilecopy_load,

	gfarm_ldap_direntry_add,
	gfarm_ldap_direntry_remove,
	gfarm_ldap_direntry_load,

	gfarm_ldap_symlink_add,
	gfarm_ldap_symlink_remove,
	gfarm_ldap_symlink_load,

	gfarm_ldap_xattr_add,
	gfarm_ldap_xattr_modify,
	gfarm_ldap_xattr_remove,
	NULL, /* gfarm_ldap_xattr_removeall not supported */
	gfarm_ldap_xattr_get,
	gfarm_ldap_xattr_load,
	gfarm_ldap_xmlattr_find,

	gfarm_ldap_quota_add,
	gfarm_ldap_quota_modify,
	gfarm_ldap_quota_remove,
	gfarm_ldap_quota_load,

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
};
