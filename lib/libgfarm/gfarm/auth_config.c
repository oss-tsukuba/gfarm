#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "hostspec.h"
#include "auth.h"

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

struct gfarm_auth_config *gfarm_auth_config_list = NULL;
struct gfarm_auth_config **gfarm_auth_config_last = &gfarm_auth_config_list;

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
	return ("unknown auth method");
}

char *
gfarm_auth_method_parse(char *name, enum gfarm_auth_method *methodp)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_auth_method_name_value_table);
	     i++) {
		if (strcmp(name, gfarm_auth_method_name_value_table[i].name)
		    == 0) {
			*methodp =
			    gfarm_auth_method_name_value_table[i].method;
			return (NULL);
		}
	}
	return (GFARM_ERR_NO_SUCH_OBJECT);
}

char *
gfarm_auth_config_add(
	enum gfarm_auth_config_command command,
	enum gfarm_auth_method method,
	struct gfarm_hostspec *hsp)
{
	struct gfarm_auth_config *acp =
	    malloc(sizeof(struct gfarm_auth_config));

	if (acp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	acp->next = NULL;
	acp->command = command;
	acp->method = method;
	acp->hostspec = hsp;

	*gfarm_auth_config_last = acp;
	gfarm_auth_config_last = &acp->next;
	return (NULL);
}

char *
gfarm_auth_enable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp)
{
	return (gfarm_auth_config_add(GFARM_AUTH_ENABLE,
	    method, hsp));
}

char *
gfarm_auth_disable(enum gfarm_auth_method method, struct gfarm_hostspec *hsp)
{
	return (gfarm_auth_config_add(GFARM_AUTH_DISABLE,
	    method, hsp));
}

/* this i/f have to be changed, if we support more than 31 auth methods */
gfarm_int32_t
gfarm_auth_method_get_enabled_by_name_addr(char *name, struct sockaddr *addr)
{
	struct gfarm_auth_config *acp = gfarm_auth_config_list;
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
