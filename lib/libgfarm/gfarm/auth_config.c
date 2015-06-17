#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <gfarm/gfarm_config.h>

#ifdef HAVE_GSI
#include <gssapi.h>
#endif

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#ifdef HAVE_GSI
#include "gfarm_gsi.h"
#endif

#include "context.h"
#include "liberror.h"
#include "hostspec.h"
#include "auth.h"

/*
 * gfarm_auth_method
 */

struct gfarm_auth_method_name_value {
	char mnemonic;
	char *name;
	enum gfarm_auth_method method;
} gfarm_auth_method_name_value_table[] = {
	{ 's', "sharedsecret",	GFARM_AUTH_METHOD_SHAREDSECRET },
	{ 'G', "gsi",		GFARM_AUTH_METHOD_GSI },
	{ 'g', "gsi_auth",	GFARM_AUTH_METHOD_GSI_AUTH },
};

enum gfarm_auth_config_command { GFARM_AUTH_ENABLE, GFARM_AUTH_DISABLE };

struct gfarm_auth_config {
	struct gfarm_auth_config *next;

	enum gfarm_auth_config_command command;
	enum gfarm_auth_method method;
	struct gfarm_hostspec *hostspec;
};

struct gfarm_auth_cred_config {
	struct gfarm_auth_cred_config *next;
	const char *service_tag;
	enum gfarm_auth_cred_type type;
	char *service;
	char *name;
};

#define staticp	(gfarm_ctxp->auth_config_static)

struct gfarm_auth_config_static {
	struct gfarm_auth_config *auth_config_list;
	struct gfarm_auth_config **auth_config_last;
	struct gfarm_auth_cred_config *auth_server_cred_config_list;

	/* authentication status */
	int gsi_auth_error;
};

gfarm_error_t
gfarm_auth_config_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_auth_config_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->auth_config_list = NULL;
	s->auth_config_last = &s->auth_config_list;
	s->auth_server_cred_config_list = NULL;
	s->gsi_auth_error = 0;

	ctxp->auth_config_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_auth_config_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_auth_config_static *s = ctxp->auth_config_static;
	struct gfarm_auth_config *c, *c_next;
	struct gfarm_auth_cred_config *cc, *cc_next;

	if (s == NULL)
		return;

	for (c = s->auth_config_list; c != NULL; c = c_next) {
		c_next = c->next;
		gfarm_hostspec_free(c->hostspec);
		free(c);
	}
	for (cc = s->auth_server_cred_config_list; cc != NULL; cc = cc_next) {
		cc_next = cc->next;
		free(cc->service);
		free(cc->name);
		free(cc);
	}
	free(s);
}

char
gfarm_auth_method_mnemonic(enum gfarm_auth_method method)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_auth_method_name_value_table);
	     i++) {
		struct gfarm_auth_method_name_value *entry =
		    &gfarm_auth_method_name_value_table[i];

		if (entry->method == method)
			return (entry->mnemonic);
	}
	return ('-');
}

char *
gfarm_auth_method_name(enum gfarm_auth_method method)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_auth_method_name_value_table);
	     i++) {
		struct gfarm_auth_method_name_value *entry =
		    &gfarm_auth_method_name_value_table[i];

		if (entry->method == method)
			return (entry->name);
	}
	return (NULL);
}

gfarm_error_t
gfarm_auth_method_parse(char *name, enum gfarm_auth_method *methodp)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_auth_method_name_value_table);
	     i++) {
		if (strcmp(name, gfarm_auth_method_name_value_table[i].name)
		    == 0) {
			*methodp =
			    gfarm_auth_method_name_value_table[i].method;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_NO_SUCH_OBJECT);
}

gfarm_error_t
gfarm_auth_config_add(
	enum gfarm_auth_config_command command,
	enum gfarm_auth_method method,
	struct gfarm_hostspec *hsp)
{
	struct gfarm_auth_config *acp;

	GFARM_MALLOC(acp);
	if (acp == NULL) {
		gflog_debug(GFARM_MSG_1000903,
			"allocation of 'gfarm_auth_config' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	acp->next = NULL;
	acp->command = command;
	acp->method = method;
	acp->hostspec = hsp;

	*staticp->auth_config_last = acp;
	staticp->auth_config_last = &acp->next;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_auth_enable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp)
{
	return (gfarm_auth_config_add(GFARM_AUTH_ENABLE,
	    method, hsp));
}

gfarm_error_t
gfarm_auth_disable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp)
{
	return (gfarm_auth_config_add(GFARM_AUTH_DISABLE,
	    method, hsp));
}

/* this i/f have to be changed, if we support more than 31 auth methods */
gfarm_int32_t
gfarm_auth_method_get_enabled_by_name_addr(
	const char *name, struct sockaddr *addr)
{
	struct gfarm_auth_config *acp = staticp->auth_config_list;
	gfarm_int32_t enabled = 0, disabled = 0, methods;

	assert(GFARM_AUTH_METHOD_NUMBER <= sizeof(gfarm_int32_t) * CHAR_BIT);

	for (; acp != NULL; acp = acp->next) {
		if (gfarm_hostspec_match(acp->hostspec, name, addr)) {
			if (acp->method == GFARM_AUTH_METHOD_ALL) {
				methods = ((1 << GFARM_AUTH_METHOD_NUMBER) - 1)
				    & ~(1 << GFARM_AUTH_METHOD_NONE)
				    & ~(enabled | disabled);
			} else {
				methods = 1 << acp->method;
				if (((enabled | disabled) & methods) != 0)
					continue; /* already determined */
			}
			switch (acp->command) {
			case GFARM_AUTH_ENABLE:
				enabled |= methods;
				break;
			case GFARM_AUTH_DISABLE:
				disabled |= methods;
				break;
			}
		}
	}
	return (enabled);
}

gfarm_error_t
gfarm_auth_method_gsi_available(void)
{
#ifdef HAVE_GSI
	if (gfarmGssAcquireCredential(NULL, GSS_C_NO_NAME, GSS_C_INITIATE,
		NULL, NULL, NULL) < 0) {
		gfarm_auth_set_gsi_auth_error(1);
		return (GFARM_ERR_INVALID_CREDENTIAL);
	}
	return (GFARM_ERR_NO_ERROR);
#else
	return (GFARM_ERRMSG_AUTH_METHOD_NOT_AVAILABLE_FOR_THE_HOST);
#endif
}

gfarm_int32_t
gfarm_auth_method_get_available(void)
{
	int i;
	gfarm_int32_t methods;

	assert(GFARM_AUTH_METHOD_NUMBER <= sizeof(gfarm_int32_t) * CHAR_BIT);

	methods = 0;
	for (i = GFARM_AUTH_METHOD_NONE + 1; i < GFARM_AUTH_METHOD_NUMBER;
	    i++) {
		switch (i) {
		case GFARM_AUTH_METHOD_GSI_OLD: /* obsolete */
			break;
#ifndef HAVE_GSI
		case GFARM_AUTH_METHOD_GSI:
			break;
		case GFARM_AUTH_METHOD_GSI_AUTH:
			break;
#endif
		default:
			methods |= 1 << i;
			break;
		}
	}
	return (methods);
}

void
gfarm_auth_set_gsi_auth_error(int s)
{
	staticp->gsi_auth_error = s;
}

gfarm_error_t
gfarm_auth_check_gsi_auth_error(void)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (staticp->gsi_auth_error) {
		e = gfarm_auth_method_gsi_available();
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	gfarm_auth_set_gsi_auth_error(0);
	return (e);
}

/*
 * gfarm_auth_server_cred
 */

struct gfarm_auth_cred_type_name_value {
	char *name;
	enum gfarm_auth_cred_type type;
} gfarm_auth_cred_type_name_value_table[] = {
	{ "",			GFARM_AUTH_CRED_TYPE_DEFAULT },
	{ "no-name",		GFARM_AUTH_CRED_TYPE_NO_NAME },
	{ "mechanism-specific",	GFARM_AUTH_CRED_TYPE_MECHANISM_SPECIFIC },
	{ "host",		GFARM_AUTH_CRED_TYPE_HOST },
	{ "user",		GFARM_AUTH_CRED_TYPE_USER },
	{ "self",		GFARM_AUTH_CRED_TYPE_SELF },
};

gfarm_error_t
gfarm_auth_cred_type_parse(char *type_name, enum gfarm_auth_cred_type *typep)
{
	int i;

	for (i = 0;
	     i < GFARM_ARRAY_LENGTH(gfarm_auth_cred_type_name_value_table);
	     i++) {
		struct gfarm_auth_cred_type_name_value *entry =
		    &gfarm_auth_cred_type_name_value_table[i];

		if (strcmp(type_name, entry->name) == 0) {
			*typep = entry->type;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	gflog_debug(GFARM_MSG_1000904,
		"Unknown credential type (%s)",
		type_name);
	return (GFARM_ERRMSG_UNKNOWN_CREDENTIAL_TYPE);
}

static struct gfarm_auth_cred_config**
gfarm_auth_server_cred_config_lookup(const char *service_tag)
{
	struct gfarm_auth_cred_config *conf, **p;

	for (p = &staticp->auth_server_cred_config_list;
	    (conf = *p) != NULL; p = &conf->next) {
		if (strcmp(service_tag, conf->service_tag) == 0)
			break;
	}
	return (p);
}

/* service_tag must be statically allocated */
static gfarm_error_t
gfarm_auth_server_cred_config_enter(char *service_tag,
	struct gfarm_auth_cred_config **confp)
{
	struct gfarm_auth_cred_config *conf,
	    **p = gfarm_auth_server_cred_config_lookup(service_tag);

	if (*p != NULL) {
		*confp = *p;
		return (GFARM_ERR_NO_ERROR);
	}
	GFARM_MALLOC(conf);
	if (conf == NULL) {
		gflog_debug(GFARM_MSG_1000905,
			"allocation of credential config failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	conf->next = NULL;
	conf->service_tag = service_tag;
	conf->type = GFARM_AUTH_CRED_TYPE_DEFAULT;
	conf->service = NULL;
	conf->name = NULL;
	*confp = *p = conf;
	return (GFARM_ERR_NO_ERROR);
}

enum gfarm_auth_cred_type
gfarm_auth_server_cred_type_get(const char *service_tag)
{
	struct gfarm_auth_cred_config *conf =
	    *gfarm_auth_server_cred_config_lookup(service_tag);

	if (conf == NULL)
		return (GFARM_AUTH_CRED_TYPE_DEFAULT);
	return (conf->type);
}

char *
gfarm_auth_server_cred_service_get(const char *service_tag)
{
	struct gfarm_auth_cred_config *conf =
	    *gfarm_auth_server_cred_config_lookup(service_tag);

	if (conf == NULL)
		return (NULL);
	return (conf->service);
}

char *
gfarm_auth_server_cred_name_get(const char *service_tag)
{
	struct gfarm_auth_cred_config *conf =
	    *gfarm_auth_server_cred_config_lookup(service_tag);

	if (conf == NULL)
		return (NULL);
	return (conf->name);
}

/* service_tag must be statically allocated */
gfarm_error_t
gfarm_auth_server_cred_type_set_by_string(char *service_tag, char *string)
{
	gfarm_error_t e;
	enum gfarm_auth_cred_type type;

	e = gfarm_auth_cred_type_parse(string, &type);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000906,
			"gfarm_auth_cred_type_parse(%s) failed: %s",
			string,
			gfarm_error_string(e));
		return (e);
	}
	return (gfarm_auth_server_cred_type_set(service_tag, type));
}

/* service_tag must be statically allocated */
gfarm_error_t
gfarm_auth_server_cred_type_set(char *service_tag,
	enum gfarm_auth_cred_type type)
{
	struct gfarm_auth_cred_config *conf;
	gfarm_error_t e =
	    gfarm_auth_server_cred_config_enter(service_tag, &conf);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000907,
			"gfarm_auth_server_cred_config_enter(%s) failed: %s",
			service_tag,
			gfarm_error_string(e));
		return (e);
	}
	/* first line has precedence */
	if (conf->type != GFARM_AUTH_CRED_TYPE_DEFAULT)
		return (GFARM_ERR_NO_ERROR);
	conf->type = type;
	return (GFARM_ERR_NO_ERROR);
}

/* service_tag must be statically allocated */
gfarm_error_t
gfarm_auth_server_cred_service_set(char *service_tag, char *service)
{
	struct gfarm_auth_cred_config *conf;
	gfarm_error_t e =
	    gfarm_auth_server_cred_config_enter(service_tag, &conf);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000908,
			"gfarm_auth_server_cred_config_enter(%s) failed: %s",
			service_tag,
			gfarm_error_string(e));
		return (e);
	}
	if (conf->service != NULL) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	if ((conf->service = strdup(service)) == NULL) {
		gflog_debug(GFARM_MSG_1000909,
			"allocation of string 'service' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (GFARM_ERR_NO_ERROR);
}

/* service_tag must be statically allocated */
gfarm_error_t
gfarm_auth_server_cred_name_set(char *service_tag, char *name)
{
	struct gfarm_auth_cred_config *conf;
	gfarm_error_t e =
	    gfarm_auth_server_cred_config_enter(service_tag, &conf);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000910,
			"gfarm_auth_server_cred_config_enter(%s): %s",
			service_tag,
			gfarm_error_string(e));
		return (e);
	}
	if (conf->name != NULL) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	if ((conf->name = strdup(name)) == NULL) {
		gflog_debug(GFARM_MSG_1000911,
			"allocation of string 'name' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	return (GFARM_ERR_NO_ERROR);
}
