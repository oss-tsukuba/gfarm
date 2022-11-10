#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#ifdef HAVE_GSS
#include <gssapi.h>
#endif

#include "gfutil.h"

#ifdef HAVE_GSS
#include "gfsl_gss.h"
#include "gfsl_secure_session.h"
#include "gfarm_gss.h"
#include "gss.h"
#endif

#include "context.h"
#include "liberror.h"
#include "hostspec.h"
#include "auth.h"

const char *
gfarm_auth_id_type_name(enum gfarm_auth_id_type type)
{
	switch (type) {
	case GFARM_AUTH_ID_TYPE_UNKNOWN: return ("unknown-auth-ID-type");
	case GFARM_AUTH_ID_TYPE_USER: return ("user");
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST: return ("gfsd");
	case GFARM_AUTH_ID_TYPE_METADATA_HOST: return ("gfmd");
	default: return ("invalid-auth-ID-type");
	}
};

/*
 * gfarm_auth_method
 */

struct gfarm_auth_method_name_value {
	char mnemonic;
	char *name;
	enum gfarm_auth_method method;
} gfarm_auth_method_name_value_table[] = {
	{ 'S', "tls_sharedsecret",
	  GFARM_AUTH_METHOD_TLS_SHAREDSECRET },
	{ 's', "sharedsecret",
	  GFARM_AUTH_METHOD_SHAREDSECRET },
	{ 'T', "tls_client_certificate",
	  GFARM_AUTH_METHOD_TLS_CLIENT_CERTIFICATE },
	{ 'G', "gsi",
	  GFARM_AUTH_METHOD_GSI },
	{ 'g', "gsi_auth",
	  GFARM_AUTH_METHOD_GSI_AUTH },
	{ 'K', "kerberos",
	  GFARM_AUTH_METHOD_KERBEROS },
	{ 'k', "kerberos_auth",
	  GFARM_AUTH_METHOD_KERBEROS_AUTH },
	{ 'A', "sasl",
	  GFARM_AUTH_METHOD_SASL },
	{ 'a', "sasl_auth",
	  GFARM_AUTH_METHOD_SASL_AUTH },
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
	struct gfarm_auth_config **auth_config_mark;
	struct gfarm_auth_cred_config *auth_server_cred_config_list;

	/* authentication credential expired?, and which was the protocol? */
	struct gfarm_gss *gss_cred_failed;
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
	s->auth_config_mark = &s->auth_config_list;
	s->auth_server_cred_config_list = NULL;
	s->gss_cred_failed = NULL;

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

static char *
gfarm_auth_config_command_name(enum gfarm_auth_config_command command)
{
	return (
	    command == GFARM_AUTH_ENABLE ? "enable" :
	    command == GFARM_AUTH_DISABLE ? "disable" :
	    "internal-error");
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

static gfarm_error_t
gfarm_auth_config_add(
	enum gfarm_auth_config_command command,
	enum gfarm_auth_method method,
	struct gfarm_hostspec *hsp,
	enum gfarm_auth_config_position position)
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

	switch (position) {
	case GFARM_AUTH_CONFIG_AT_HEAD:
		acp->next = staticp->auth_config_list;
		staticp->auth_config_list = acp;
		break;
	case GFARM_AUTH_CONFIG_AT_TAIL:
		if (staticp->auth_config_mark == staticp->auth_config_last)
			staticp->auth_config_mark = &acp->next;
		*staticp->auth_config_last = acp;
		staticp->auth_config_last = &acp->next;
		break;
	case GFARM_AUTH_CONFIG_AT_MARK:
		if (staticp->auth_config_last == staticp->auth_config_mark)
			staticp->auth_config_last = &acp->next;
		acp->next = *staticp->auth_config_mark;
		*staticp->auth_config_mark = acp;
		staticp->auth_config_mark = &acp->next;
		break;
	}
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_auth_config_set_mark(void)
{
	staticp->auth_config_mark = &staticp->auth_config_list;
}

gfarm_error_t
gfarm_auth_enable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp,
	enum gfarm_auth_config_position position)
{
	return (gfarm_auth_config_add(GFARM_AUTH_ENABLE,
	    method, hsp, position));
}

gfarm_error_t
gfarm_auth_disable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp,
	enum gfarm_auth_config_position position)
{
	return (gfarm_auth_config_add(GFARM_AUTH_DISABLE,
	    method, hsp, position));
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

static int
gfarm_auth_gss_credential_available(struct gfarm_gss *gss)
{
#ifdef HAVE_GSS
	if (gss->gfarmGssAcquireCredential(
	    NULL, GSS_C_NO_NAME, GSS_C_INITIATE, NULL, NULL, NULL) < 0) {
		return (0);
	}
	return (1);
#else
	return (0);
#endif
}

#ifdef HAVE_GSS

/* NOTE: it's OK to pass NULL as gss */
static gfarm_error_t
gfarm_auth_method_protocol_available(struct gfarm_gss *gss)
{
	if (gss == NULL)
		return (GFARM_ERR_PROTOCOL_NOT_AVAILABLE);
	if (gfarm_auth_gss_credential_available(gss))
		return (GFARM_ERR_NO_ERROR);

	/*
	 * do not overwrite staticp->gss_cred_failed,
	 * because maybe staticp->gss_cred_failed != gss
	 */
	if (staticp->gss_cred_failed == NULL)
		gfarm_auth_set_gss_cred_failed(gss);

	return (GFARM_ERR_INVALID_CREDENTIAL);
}

#endif

gfarm_error_t
gfarm_auth_method_gsi_available(void)
{
#ifdef HAVE_GSI
	return (gfarm_auth_method_protocol_available(gfarm_gss_gsi()));
#else
	return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
#endif
}

gfarm_error_t
gfarm_auth_method_kerberos_available(void)
{
#ifdef HAVE_KERBEROS
	return (gfarm_auth_method_protocol_available(gfarm_gss_kerberos()));
#else
	return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
#endif
}

gfarm_error_t
gfarm_auth_method_sasl_available(void)
{
#if defined(HAVE_CYRUS_SASL) && defined(HAVE_TLS_1_3)
	/* XXX TODO(?) check whether JWT is available or not? */
	return (GFARM_ERR_NO_ERROR);
#else
	return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
#endif
}

static gfarm_int32_t
gfarm_auth_method_get_available(int is_server,
	enum gfarm_auth_id_type self_type)
{
	int i;
	gfarm_int32_t methods;

	assert(GFARM_AUTH_METHOD_NUMBER <= sizeof(gfarm_int32_t) * CHAR_BIT);

	methods = 0;
	for (i = GFARM_AUTH_METHOD_NONE + 1; i < GFARM_AUTH_METHOD_NUMBER;
	    i++) {
		switch (i) {
		case GFARM_AUTH_METHOD_GSI_OLD: /* obsolete */
			continue; /* not available */
		case GFARM_AUTH_METHOD_GSI:
		case GFARM_AUTH_METHOD_GSI_AUTH:
#ifdef HAVE_GSI
			if (gfarm_gss_gsi() != NULL)
				break; /* available */
#endif
			continue; /* not available */
		case GFARM_AUTH_METHOD_KERBEROS:
		case GFARM_AUTH_METHOD_KERBEROS_AUTH:
#ifdef HAVE_KERBEROS
			if (gfarm_gss_kerberos() != NULL)
				break; /* available */
#endif
			continue; /* not available */
		case GFARM_AUTH_METHOD_SASL:
		case GFARM_AUTH_METHOD_SASL_AUTH:
#if defined(HAVE_CYRUS_SASL) && defined(HAVE_TLS_1_3)
			if (is_server) {
				if (gfarm_auth_server_method_sasl_available())
					break; /* available */
			} else if (self_type == GFARM_AUTH_ID_TYPE_USER) {
				if (gfarm_auth_client_method_sasl_available())
					break; /* available */
			}
#endif
			continue; /* not available */
		default:
			break; /* available */
		}
		methods |= 1 << i;
	}
	return (methods);
}

gfarm_int32_t
gfarm_auth_server_method_get_available(void)
{
	return (
	    gfarm_auth_method_get_available(1, GFARM_AUTH_ID_TYPE_UNKNOWN));
}

gfarm_int32_t
gfarm_auth_client_method_get_available(enum gfarm_auth_id_type self_type)
{
	return (gfarm_auth_method_get_available(0, self_type));
}

void
gfarm_auth_set_gss_cred_failed(struct gfarm_gss *gss)
{
	staticp->gss_cred_failed = gss;
}

/* to prevent to connect servers with expired client credential */
gfarm_error_t
gfarm_auth_check_gss_cred_failed(void)
{
	if (staticp->gss_cred_failed != NULL) {
		if (!gfarm_auth_gss_credential_available(
		    staticp->gss_cred_failed)) {
			return (GFARM_ERR_INVALID_CREDENTIAL);
		}
	}
	gfarm_auth_set_gss_cred_failed(NULL);
	return (GFARM_ERR_NO_ERROR);
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

#define ADVANCE(string, size, len) \
	do { \
		if (string == NULL) { \
			/* to shut up gcc -Wformat-truncation warning */ \
			size = 0; \
		} else if (size > len) { \
			size -= len; \
			string += len; \
		} else { \
			size = 0; \
			string = NULL; \
		} \
	} while (0)

/* string can be NULL */
static int
gfarm_auth_config_elem_string(
	struct gfarm_auth_config *acp, char *string, size_t size)
{
	int len, len2, save_errno;

	len = snprintf(string, size, "%s %s ",
	    gfarm_auth_config_command_name(acp->command),
	    gfarm_auth_method_name(acp->method));
	if (len < 0) {
		save_errno = errno;
		gflog_debug_errno(GFARM_MSG_1005114, "snprintf");
		errno = save_errno;
		return (-1);
	}
	ADVANCE(string, size, (size_t)len);

	len2 = gfarm_hostspec_to_string(acp->hostspec, string, size);
	if (len2 < 0) {
		save_errno = errno;
		gflog_debug_errno(GFARM_MSG_1005115, "snprintf");
		errno = save_errno;
		return (-1);
	}
	
	/* the following shouldn't overflow. i.e. must be less than INT_MAX */
	return (len + len2);
}

/* string can be NULL */
static int
gfarm_auth_config_string(char *string, size_t size)
{
	struct gfarm_auth_config *acp;
	int rv, len, save_errno;
	int overflow = 0;

	rv = 0;
	for (acp = staticp->auth_config_list; acp != NULL; acp = acp->next) {
		len = gfarm_auth_config_elem_string(acp, string, size);
		if (len < 0) {
			save_errno = errno;
			gflog_debug_errno(GFARM_MSG_1005116, "snprintf");
			errno = save_errno;
			return (-1);
		}
		rv = gfarm_size_add(&overflow, rv, len);
		ADVANCE(string, size, (size_t)len);

		len = snprintf(string, size, "%c", '\n');
		if (len < 0) {
			save_errno = errno;
			gflog_debug_errno(GFARM_MSG_1005117, "snprintf");
			errno = save_errno;
			return (-1);
		}
		rv = gfarm_size_add(&overflow, rv, len);
		ADVANCE(string, size, (size_t)len);
	}
	if (overflow) {
		rv = -1;
		errno = EOVERFLOW;
	}
	return (rv);
}

char *
gfarm_auth_config_string_dup(void)
{
	char *string;
	int size;
	int overflow = 0;

	size = gfarm_auth_config_string(NULL, 0);
	if (size < 0)
		return (NULL);

	size = gfarm_size_add(&overflow, size, 1); /* for '\0' */
	if (overflow)
		return (NULL);

	GFARM_MALLOC_ARRAY(string, size);
	if (string == NULL)
		return (NULL);

	gfarm_auth_config_string(string, size);
	return (string);
}
