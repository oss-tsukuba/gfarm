/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <assert.h>

#include <sys/socket.h>
#include <netinet/in.h> /* ntohs */
#include <netdb.h>

#include <time.h>
#include <pwd.h>

#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"

#include "gfpath.h"
#include "hostspec.h"
#include "host.h"
#include "param.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "metadb_access.h" /* for gfarm_metadb_use_*() */
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfs_misc.h"	/* gfarm_redirect_file() */
#include "agent_wrap.h"

static int gfarm_initialize_done = 0;
int gfarm_is_active_file_system_node = 0;

char *gfarm_config_file = GFARM_CONFIG;

void
gfarm_config_set_filename(char *filename)
{
	gfarm_config_file = filename;
}

int
gfarm_initialized(void)
{
	return (gfarm_initialize_done);
}

/*
 * GFarm username handling
 */

static gfarm_stringlist local_user_map_file_list;

static char GFARM_ERR_TOO_MANY_ARGUMENTS[] = "too many arguments";
static char GFARM_ERR_LOCAL_USER_REDEFIEND[] = "local user name redifined";
static char GFARM_ERR_GLOBAL_USER_REDEFIEND[] = "global user name redifined";

/* the return value of the following function should be free(3)ed */
static char *
map_user(char *from, char **to_p,
	char *(mapping)(char *, char *, char *), char *error_redefined)
{
	FILE *map = NULL;
	char *mapfile = NULL;
	int i, list_len, mapfile_mapped_index;
	char buffer[1024], *g_user, *l_user, *mapped, *e;
	int lineno = 0;
	static char fmt_open_error[] = "%s: %s";
	static char fmt_config_error[] = "%s: line %d: %s";
	static char error[256];

	e = NULL;
	*to_p = NULL;
	list_len = gfarm_stringlist_length(&local_user_map_file_list);
	mapfile_mapped_index = list_len;
	for (i = 0; i < list_len; i++) {
		mapfile = gfarm_stringlist_elem(&local_user_map_file_list, i);
		if ((map = fopen(mapfile, "r")) == NULL) {
			e = "cannot read"; 
#ifdef HAVE_SNPRINTF
			snprintf(error, sizeof(error), fmt_open_error,
			    mapfile, e);
			e = error;
#else
			if (strlen(fmt_open_error) +
			    strlen(mapfile) + strlen(e) < sizeof(error)) {
				sprintf(error, "%s: %s", mapfile, e);
				e = error;
			} else {
				/* XXX: no file name */
				e = "cannot read local_user_map file";
			}
#endif
			return (e);
		}
		lineno = 0;
		while (fgets(buffer, sizeof buffer, map) != NULL) {
			char *bp = buffer;

			lineno++;
			g_user = gfarm_strtoken(&bp, &e);
			if (e != NULL)
				goto finish;
			if (g_user == NULL) /* blank or comment line */
				continue;
			l_user = gfarm_strtoken(&bp, &e);
			if (e != NULL)
				goto finish;
			if (l_user == NULL) {
				e = "missing second field (local user)";
				goto finish;
			}
			mapped = (*mapping)(from, g_user, l_user);
			if (mapped != NULL) {
				if (*to_p != NULL &&
				    strcmp(mapped, *to_p) != 0 &&
				    i == mapfile_mapped_index) {
					e = error_redefined;
					goto finish;
				}
				if (*to_p == NULL) {
					*to_p = strdup(mapped);
					if (*to_p == NULL) {
						e = GFARM_ERR_NO_MEMORY;
						goto finish;
					}
				}
				mapfile_mapped_index = i;
			}
			if (gfarm_strtoken(&bp, &e) != NULL) {
				e = GFARM_ERR_TOO_MANY_ARGUMENTS;
				goto finish;
			}
		}
		fclose(map);
		map = NULL;
	}
	if (*to_p == NULL) { /* not found */
	 	*to_p = strdup(from);
		if (*to_p == NULL)
			e = GFARM_ERR_NO_MEMORY;
	}	
finish:	
	if (map != NULL)
		fclose(map);
	if (e != NULL) {
		if (*to_p != NULL)	 
			free(*to_p);
#ifdef HAVE_SNPRINTF
		snprintf(error, sizeof(error), fmt_config_error,
		    mapfile, lineno, e);
		e = error;
#else
		if (strlen(fmt_config_error) + strlen(mapfile) +
		    GFARM_INT32STRLEN + strlen(e) <
		    sizeof(error)) {
			sprintf(error, fmt_config_error, mapfile, lineno, e);
			e = error;
		} else {
			/* XXX: no file name, no line number */
			/* leave `e' as is */
		}
#endif
	}
	return (e);
}

static char *
map_global_to_local(char *from, char *global_user, char *local_user)
{
	if (strcmp(from, global_user) == 0)
		return (local_user);
	return (NULL);
}

/* the return value of the following function should be free(3)ed */
char *
gfarm_global_to_local_username(char *global_user, char **local_user_p)
{
	return (map_user(global_user, local_user_p,
	    map_global_to_local, GFARM_ERR_GLOBAL_USER_REDEFIEND));
}

static char *
map_local_to_global(char *from, char *global_user, char *local_user)
{
	if (strcmp(from, local_user) == 0)
		return (global_user);
	return (NULL);
}

/* the return value of the following function should be free(3)ed */
char *
gfarm_local_to_global_username(char *local_user, char **global_user_p)
{
	return (map_user(local_user, global_user_p,
	    map_local_to_global, GFARM_ERR_LOCAL_USER_REDEFIEND));
}

static char *
set_string(char **var, char *value)
{
	if (*var != NULL)
		free(*var);
	*var = strdup(value);
	if (*var == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (NULL);
}

/*
 * client side variables
 */
static char *gfarm_global_username = NULL;
static char *gfarm_local_username = NULL;
static char *gfarm_local_homedir = NULL;

char *
gfarm_set_global_username(char *global_username)
{
	return (set_string(&gfarm_global_username, global_username));
}

char *
gfarm_get_global_username(void)
{
	return (gfarm_global_username);
}

char *
gfarm_set_local_username(char *local_username)
{
	return (set_string(&gfarm_local_username, local_username));
}

char *
gfarm_get_local_username(void)
{
	return (gfarm_local_username);
}

char *
gfarm_set_local_homedir(char *local_homedir)
{
	return (set_string(&gfarm_local_homedir, local_homedir));
}

char *
gfarm_get_local_homedir(void)
{
	return (gfarm_local_homedir);
}

/*
 * We should not trust gfarm_get_*() values as a result of this function
 * (because it may be forged).
 */
char *
gfarm_set_local_user_for_this_local_account(void)
{
	char *error;
	char *user;
	char *home;
	struct passwd *pwd;

	pwd = getpwuid(geteuid());
	if (pwd != NULL) {
		user = pwd->pw_name;
		home = pwd->pw_dir;
	} else { /* XXX */
		user = "nobody";
		home = "/";
	}
	error = gfarm_set_local_username(user);
	if (error != NULL)
		return (error);
	error = gfarm_set_local_homedir(home);
	if (error != NULL)
		return (error);
	return (error);
}

char *
gfarm_set_global_user_for_this_local_account(void)
{
	char *e, *local_user, *global_user;

#ifdef HAVE_GSI
	/*
	 * Global user name determined by the distinguished name.
	 *
	 * XXX - Currently, a local user map is used.
	 */
	local_user = gfarm_gsi_client_cred_name();
	if (local_user != NULL) {
		e = gfarm_local_to_global_username(local_user, &global_user);
		if (e == NULL)
			if (strcmp(local_user, global_user) == 0)
				free(global_user);
				/* continue to the next method */
			else
				goto set_global_username;
		else
			return (e);
	}
#endif
	/* Global user name determined by the local user account. */
	local_user = gfarm_get_local_username();
	e = gfarm_local_to_global_username(local_user, &global_user);
	if (e != NULL)
		return (e);
#ifdef HAVE_GSI
 set_global_username:
#endif
	e = gfarm_set_global_username(global_user);
	free(global_user);
	gfarm_stringlist_free_deeply(&local_user_map_file_list);
	return (e);
}

/*
 * GFarm Configurations.
 *
 * Initial string values should be NULL, otherwise the value incorrectly
 * free(3)ed in the gfarm_config_clear() function below.
 * If you would like to provide default value other than NULL, set the
 * value at gfarm_config_set_default*().
 */
/* GFS dependent */
char *gfarm_spool_server_listen_address = NULL;
static char gfarm_spool_root_default[] = GFARM_SPOOL_ROOT;
char *gfarm_spool_root = NULL;
static char *gfarm_spool_server_portname = NULL;
int gfarm_spool_server_port = GFSD_DEFAULT_PORT;

/* GFM dependent */
char *gfarm_metadb_server_name = NULL;
static char *gfarm_metadb_server_portname = NULL;
int gfarm_metadb_server_port = GFMD_DEFAULT_PORT;

/* LDAP dependent */
char *gfarm_ldap_server_name = NULL;
char *gfarm_ldap_server_port = NULL;
char *gfarm_ldap_base_dn = NULL;
char *gfarm_ldap_bind_dn = NULL;
char *gfarm_ldap_bind_password = NULL;
char *gfarm_ldap_tls = NULL;
char *gfarm_ldap_tls_cipher_suite = NULL;
char *gfarm_ldap_tls_certificate_key_file = NULL;
char *gfarm_ldap_tls_certificate_file = NULL;

/* PostgreSQL dependent */
char *gfarm_postgresql_server_name = NULL;
char *gfarm_postgresql_server_port = NULL;
char *gfarm_postgresql_dbname = NULL;
char *gfarm_postgresql_user = NULL;
char *gfarm_postgresql_password = NULL;
char *gfarm_postgresql_conninfo = NULL;

/* LocalFS dependent */
char *gfarm_localfs_datadir = NULL;

enum gfarm_metadb_backend_type {
	GFARM_METADB_TYPE_UNKNOWN,
	GFARM_METADB_TYPE_LDAP,
	GFARM_METADB_TYPE_POSTGRESQL,
	GFARM_METADB_TYPE_LOCALFS
};

/* miscellaneous */
#define GFARM_HOST_CACHE_TIMEOUT_DEFAULT 600 /* 10 minutes */
#define GFARM_SCHEDULE_CACHE_TIMEOUT_DEFAULT 600 /* 10 minutes */
#define GFARM_SCHEDULE_WRITE_LOCAL_PRIORITY_DEFAULT 1 /* enable */
#define GFARM_MINIMUM_FREE_DISK_SPACE_DEFAULT	(128 * 1024 * 1024) /* 128MB */
#define GFARM_GFSD_CONNECTION_CACHE_DEFAULT 16 /* 16 free connections */
#define MISC_DEFAULT -1
int gfarm_dir_cache_timeout = MISC_DEFAULT;
int gfarm_host_cache_timeout = MISC_DEFAULT;
int gfarm_schedule_cache_timeout = MISC_DEFAULT;
static int schedule_write_local_priority = MISC_DEFAULT;
static char *schedule_write_target_domain = NULL;
static file_offset_t minimum_free_disk_space = MISC_DEFAULT;
int gfarm_gfsd_connection_cache = MISC_DEFAULT;

/* static variables */
static enum {
	gfarm_config_not_read,
	gfarm_config_user_read,
	gfarm_config_server_read
} config_read = gfarm_config_not_read;

static void
gfarm_config_clear(void)
{
	static char **vars[] = {
		&gfarm_spool_server_listen_address,
		&gfarm_spool_server_portname,
		&gfarm_metadb_server_name,
		&gfarm_metadb_server_portname,
		&gfarm_ldap_server_name,
		&gfarm_ldap_server_port,
		&gfarm_ldap_base_dn,
		&gfarm_ldap_bind_dn,
		&gfarm_ldap_bind_password,
		&gfarm_ldap_tls,
		&gfarm_ldap_tls_cipher_suite,
		&gfarm_ldap_tls_certificate_key_file,
		&gfarm_ldap_tls_certificate_file,
		&gfarm_postgresql_server_name,
		&gfarm_postgresql_server_port,
		&gfarm_postgresql_dbname,
		&gfarm_postgresql_user,
		&gfarm_postgresql_password,
		&gfarm_postgresql_conninfo,
		&gfarm_localfs_datadir,
		&schedule_write_target_domain,
	};
	static void (*funcs[])(void) = {
		gfarm_agent_name_clear,
		gfarm_agent_port_clear,
		gfarm_agent_sock_path_clear
	};
	int i;

	if (gfarm_spool_root != NULL) {
		/*
		 * In case of the default spool root, do not free the
		 * memory space becase it is statically allocated.
		 */
		if (gfarm_spool_root != gfarm_spool_root_default)
			free(gfarm_spool_root);
		gfarm_spool_root = NULL;
	}
	for (i = 0; i < GFARM_ARRAY_LENGTH(vars); i++) {
		if (*vars[i] != NULL) {
			free(*vars[i]);
			*vars[i] = NULL;
		}
	}
	for (i = 0; i < GFARM_ARRAY_LENGTH(funcs); i++) {
		(*funcs[i])();
	}

	config_read = gfarm_config_not_read;
}

static char *
config_metadb_type(enum gfarm_metadb_backend_type metadb_type)
{
	switch (metadb_type) {
	case GFARM_METADB_TYPE_UNKNOWN:
		return (gfarm_metadb_use_none());
	case GFARM_METADB_TYPE_LDAP:
		return (gfarm_metadb_use_ldap());
	case GFARM_METADB_TYPE_POSTGRESQL:
		return (gfarm_metadb_use_postgresql());
	case GFARM_METADB_TYPE_LOCALFS:
		return (gfarm_metadb_use_localfs());
	default:
		assert(0);
		return (GFARM_ERR_UNKNOWN); /* workaround compiler warning */
	}
}

static char *
set_metadb_type(enum gfarm_metadb_backend_type *metadb_typep,
	enum gfarm_metadb_backend_type set)
{
	if (*metadb_typep == set)
		return (NULL);
	switch (*metadb_typep) {
	case GFARM_METADB_TYPE_UNKNOWN:
		*metadb_typep = set;
		return (NULL);
	case GFARM_METADB_TYPE_LDAP:
		return ("inconsistent configuration, "
		    "LDAP is specified as metadata backend before");
	case GFARM_METADB_TYPE_POSTGRESQL:
		return ("inconsistent configuration, "
		    "PostgreSQL is specified as metadata backend before");
	case GFARM_METADB_TYPE_LOCALFS:
		return ("inconsistent configuration, "
		    "LocalFS is specified as metadata backend before");
	default:
		assert(0);
		return (GFARM_ERR_UNKNOWN); /* workaround compiler warning */
	}
}

static char *
set_metadb_type_ldap(enum gfarm_metadb_backend_type *metadb_typep)
{
	return (set_metadb_type(metadb_typep, GFARM_METADB_TYPE_LDAP));
}

static char *
set_metadb_type_postgresql(enum gfarm_metadb_backend_type *metadb_typep)
{
	return (set_metadb_type(metadb_typep, GFARM_METADB_TYPE_POSTGRESQL));
}

static char *
set_metadb_type_localfs(enum gfarm_metadb_backend_type *metadb_typep)
{
	return (set_metadb_type(metadb_typep, GFARM_METADB_TYPE_LOCALFS));
}

int
gfarm_schedule_write_local_priority(void)
{
	return (schedule_write_local_priority);
}

char *
gfarm_schedule_write_target_domain(void)
{
	return (schedule_write_target_domain);
}

char *
gfarm_set_minimum_free_disk_space(file_offset_t size)
{
	minimum_free_disk_space = size;
	return (NULL);
}

file_offset_t
gfarm_get_minimum_free_disk_space(void)
{
	return (minimum_free_disk_space);
}

/*
 * get (almost) shell style token.
 * e.g.
 *	string...
 *	'string...' (do not interpret escape character `\')
 *	"string..." (interpret escape character `\')
 *	# comment
 * difference from shell token:
 *	don't allow newline in "..." and '...".
 *
 * return value:
 *	string
 *   OR
 *	NULL	- if error or end-of-line.
 * output parameter:
 *	*cursorp:
 *		next character to read
 *	*errorp:
 *		NULL (if success or end-of-line)
 *	    OR
 *		error message
 */

char *
gfarm_strtoken(char **cursorp, char **errorp)
{
	unsigned char *top, *p, *s = *(unsigned char **)cursorp;

	*errorp = NULL;
	while (*s != '\n' && isspace(*s))
		s++;
	if (*s == '\0' || *s == '\n' || *s == '#') {
		/* end of line */
		*cursorp = (char *)s;
		return (NULL);
	}
	top = s;
	p = s;
	for (;;) {
		switch (*s) {
		case '\'':
			s++;
			for (;;) {
				if (*s == '\'')
					break;
				if (*s == '\0' || *s == '\n') {
					*errorp = "unterminated single quote";
					return (NULL);
				}
				*p++ = *s++;
			}
			s++;
			break;
		case '"':
			s++;
			for (;;) {
				if (*s == '"')
					break;
				if (*s == '\0' || *s == '\n') {
					*errorp = "unterminated double quote";
					return (NULL);
				}
				if (*s == '\\') {
					if (s[1] == '\0' || s[1] == '\n') {
						*errorp =
						   "unterminated double quote";
						return (NULL);
					}
					/*
					 * only interpret `\"' and `\\'
					 * in double quotation.
					 */
					if (s[1] == '"' || s[1] == '\\')
						s++;
				}
				*p++ = *s++;
			}
			s++;
			break;
		case '\\':
			s++;
			if (*s == '\0' || *s == '\n') {
				*errorp = "incomplete escape: \\";
				return (NULL);
			}
			*p++ = *s++;
			break;
		case '\n':	
		case '#':
		case '\0':
			*p = '\0';
			*cursorp = (char *)s;
			return ((char *)top);
		default:
			if (isspace(*s)) {
				*p = '\0';
				*cursorp = (char *)(s + 1);
				return ((char *)top);
			}
			*p++ = *s++;
			break;
		}
	}
}

static char *
parse_auth_arguments(char *p, char **op)
{
	char *e, *command, *auth, *host;
	enum gfarm_auth_method auth_method;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "auth") == 0); */

	command = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (command == NULL)
		return ("missing 1st(auth-command) argument");

	auth = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (auth == NULL)
		return ("missing 2nd(auth-method) argument");
	if (strcmp(auth, "*") == 0 || strcmp(auth, "ALL") == 0) {
		auth_method = GFARM_AUTH_METHOD_ALL;
	} else {
		e = gfarm_auth_method_parse(auth, &auth_method);
		if (e != NULL) {
			*op = "2nd(auth-method) argument";
			if (e == GFARM_ERR_NO_SUCH_OBJECT)
				e = "unknown authentication method";
			return (e);
		}
	}

	host = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (host == NULL)
		return ("missing 3rd(host-spec) argument");
	if (gfarm_strtoken(&p, &e) != NULL)
		return (GFARM_ERR_TOO_MANY_ARGUMENTS);
	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != NULL) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "3rd(host-spec) argument";
		return (e);
	}

	if (strcmp(command, "enable") == 0) {
		e = gfarm_auth_enable(auth_method, hostspecp);
	} else if (strcmp(command, "disable") == 0) {
		e = gfarm_auth_disable(auth_method, hostspecp);
	} else {
		/*
		 * we don't return `command' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(auth-command) argument";
		gfarm_hostspec_free(hostspecp);
		return ("unknown auth subcommand");
	}
	if (e != NULL)
		gfarm_hostspec_free(hostspecp);
	return (e);
}

static char *
parse_netparam_arguments(char *p, char **op)
{
	char *e, *option, *host;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "netparam") == 0); */

	option = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (option == NULL)
		return ("missing 1st(netparam-option) argument");

	host = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (host == NULL) {
		/* if 2nd argument is omitted, it is treated as "*". */
		host = "*";
	} else if (gfarm_strtoken(&p, &e) != NULL) {
		return (GFARM_ERR_TOO_MANY_ARGUMENTS);
	}
	
	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != NULL) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "2nd(host-spec) argument";
		return (e);
	}

	e = gfarm_netparam_config_add_long(option, hostspecp);
	if (e != NULL) {
		/*
		 * we don't return `option' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(sockopt-option) argument";
		gfarm_hostspec_free(hostspecp);
		return (e);
	}
	return (NULL);
}

static char *
parse_sockopt_arguments(char *p, char **op)
{
	char *e, *option, *host;
	struct gfarm_hostspec *hostspecp;
	int is_listener;

	/* assert(strcmp(*op, "sockopt") == 0); */

	option = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (option == NULL)
		return ("missing 1st(sockopt-option) argument");

	host = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (host == NULL) {
		/*
		 * if 2nd argument is omitted, it is treated as:
		 *	"LISTENER" + "*".
		 */
		is_listener = 1;
	} else {
		is_listener = strcmp(host, "LISTENER") == 0;
		if (gfarm_strtoken(&p, &e) != NULL)
			return (GFARM_ERR_TOO_MANY_ARGUMENTS);
	}
	
	if (is_listener) {
		e = gfarm_sockopt_listener_config_add(option);
		if (e != NULL) {
			/*
			 * we don't return `option' to *op here,
			 * because it may be too long.
			 */
			*op = "1st(sockopt-option) argument";
			return (e);
		}
	}
	if (host == NULL || !is_listener) {
		e = gfarm_hostspec_parse(host != NULL ? host : "*",
		    &hostspecp);
		if (e != NULL) {
			/*
			 * we don't return `host' to *op here,
			 * because it may be too long.
			 */
			*op = "2nd(host-spec) argument";
			return (e);
		}

		e = gfarm_sockopt_config_add(option, hostspecp);
		if (e != NULL) {
			/*
			 * we don't return `option' to *op here,
			 * because it may be too long.
			 */
			*op = "1st(sockopt-option) argument";
			gfarm_hostspec_free(hostspecp);
			return (e);
		}
	}
	return (NULL);
}

static char *
parse_address_use_arguments(char *p, char **op)
{
	char *e, *address;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "address_use") == 0); */

	address = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (address == NULL)
		return ("missing <address> argument");
	if (gfarm_strtoken(&p, &e) != NULL)
		return (GFARM_ERR_TOO_MANY_ARGUMENTS);

	e = gfarm_hostspec_parse(address, &hostspecp);
	if (e != NULL) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(address) argument";
		return (e);
	}

	e = gfarm_host_address_use(hostspecp);
	if (e != NULL) {
		/*
		 * we don't return `option' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(address) argument";
		gfarm_hostspec_free(hostspecp);
		return (e);
	}
	return (NULL);
}

static char *
parse_local_user_map(char *p, char **op)
{
	char *e, *mapfile;

	mapfile = gfarm_strtoken(&p, &e);
	if (mapfile == NULL)
		return ("missing <user map file> argument");
	if (gfarm_strtoken(&p, &e) != NULL)
		return (GFARM_ERR_TOO_MANY_ARGUMENTS);
	mapfile = strdup(mapfile);
	if (mapfile == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = gfarm_stringlist_add(&local_user_map_file_list, mapfile);
	return(e);
}

static char *
parse_client_architecture(char *p, char **op)
{
	char *e, *architecture, *host;
	struct gfarm_hostspec *hostspecp;

	architecture = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (architecture == NULL)
		return ("missing 1st(architecture) argument");
	host = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (host == NULL)
		return ("missing 2nd(host-spec) argument");
	if (gfarm_strtoken(&p, &e) != NULL)
		return (GFARM_ERR_TOO_MANY_ARGUMENTS);
	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != NULL) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "2nd(host-spec) argument";
		return (e);
	}
	e = gfarm_set_client_architecture(architecture, hostspecp);
	if (e != NULL)
		gfarm_hostspec_free(hostspecp);
	return (e);
}

static char *
get_one_argument(char *p, char **sp)
{
	char *e, *s;

	s = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (s == NULL)
		return ("missing argument");
	if (gfarm_strtoken(&p, &e) != NULL)
		return (GFARM_ERR_TOO_MANY_ARGUMENTS);
	if (e != NULL)
		return (e);
	*sp = s;
	return (NULL);
}

static char *
parse_set_var(char *p, char **rv)
{
	char *e, *s;

	e = get_one_argument(p, &s);
	if (e != NULL)
		return (e);

	if (*rv != NULL) /* first line has precedence */
		return (NULL);
	s = strdup(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*rv = s;
	return (NULL);
}

static char *
parse_set_misc_int(char *p, int *vp)
{
	char *e, *ep, *s;
	int v;

	e = get_one_argument(p, &s);
	if (e != NULL)
		return (e);

	if (*vp != MISC_DEFAULT) /* first line has precedence */
		return (NULL);
	errno = 0;
	v = strtol(s, &ep, 10);
	if (errno != 0)
		return (gfarm_errno_to_error(errno));
	if (ep == s)
		return ("integer expected");
	if (*ep != '\0')
		return ("invalid character");
	*vp = v;
	return (NULL);
}

static char *
parse_set_misc_offset(char *p, file_offset_t *vp)
{
	char *e, *ep, *s;
	file_offset_t v;

	e = get_one_argument(p, &s);
	if (e != NULL)
		return (e);

	if (*vp != MISC_DEFAULT) /* first line has precedence */
		return (NULL);
	errno = 0;
	v = string_to_file_offset(s, &ep);
	if (errno != 0)
		return (gfarm_errno_to_error(errno));
	if (ep == s)
		return ("integer expected");
	if (*ep != '\0') {
		switch (*ep) {
		case 'k': case 'K': ep++; v *= 1024; break;
		case 'm': case 'M': ep++; v *= 1024 * 1024; break;
		case 'g': case 'G': ep++; v *= 1024 * 1024 * 1024; break;
		case 't': case 'T': ep++; v *=1024*1024; v *=1024*1024; break;
		}
		if (*ep != '\0')
			return ("invalid character");
	}
	*vp = v;
	return (NULL);
}

static char *
parse_set_misc_enabled(char *p, int *vp)
{
	char *e, *s;
	int v;

	e = get_one_argument(p, &s);
	if (e != NULL)
		return (e);

	if (*vp != MISC_DEFAULT) /* first line has precedence */
		return (NULL);
	if (strcmp(s, "enable") == 0)
		v = 1;
	else if (strcmp(s, "disable") == 0)
		v = 0;
	else
		return ("\"enable\" or \"disable\" is expected");
	*vp = v;
	return (NULL);
}

static char *
parse_cred_config(char *p, char *service,
	char *(*set)(char *, char *))
{
	char *e, *s;

	e = get_one_argument(p, &s);
	if (e != NULL)
		return (e);

	return ((*set)(service, s));
}

static char *
parse_set_func(char *p, char *(*set)(char *))
{
	char *e, *s;

	e = get_one_argument(p, &s);
	if (e != NULL)
		return (e);

	return ((*set)(s));
}

static char *
parse_one_line(char *s, char *p, char **op,
	enum gfarm_metadb_backend_type *metadb_typep)
{
	char *e, *o;

	if (strcmp(s, o = "spool") == 0) {
		e = parse_set_var(p, &gfarm_spool_root);
	} else if (strcmp(s, o = "spool_server_listen_address") == 0) {
		e = parse_set_var(p, &gfarm_spool_server_listen_address);
	} else if (strcmp(s, o = "spool_serverport") == 0) {
		e = parse_set_var(p, &gfarm_spool_server_portname);
	} else if (strcmp(s, o = "spool_server_cred_type") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_type_set_by_string);
	} else if (strcmp(s, o = "spool_server_cred_service") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_service_set);
	} else if (strcmp(s, o = "spool_server_cred_name") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_name_set);

	} else if (strcmp(s, o = "metadb_serverhost") == 0) {
		e = parse_set_var(p, &gfarm_metadb_server_name);
	} else if (strcmp(s, o = "metadb_serverport") == 0) {
		e = parse_set_var(p, &gfarm_metadb_server_portname);
	} else if (strcmp(s, o = "metadb_server_cred_type") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_type_set_by_string);
	} else if (strcmp(s, o = "metadb_server_cred_service") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_service_set);
	} else if (strcmp(s, o = "metadb_server_cred_name") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_name_set);

	} else if (strcmp(s, o = "ldap_serverhost") == 0) {
		e = parse_set_var(p, &gfarm_ldap_server_name);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);
	} else if (strcmp(s, o = "ldap_serverport") == 0) {
		e = parse_set_var(p, &gfarm_ldap_server_port);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);
	} else if (strcmp(s, o = "ldap_base_dn") == 0) {
		e = parse_set_var(p, &gfarm_ldap_base_dn);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);
	} else if (strcmp(s, o = "ldap_bind_dn") == 0) {
		e = parse_set_var(p, &gfarm_ldap_bind_dn);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);
	} else if (strcmp(s, o = "ldap_bind_password") == 0) {
		e = parse_set_var(p, &gfarm_ldap_bind_password);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);
	} else if (strcmp(s, o = "ldap_tls") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);
	} else if (strcmp(s, o = "ldap_tls_cipher_suite") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_cipher_suite);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);
	} else if (strcmp(s, o = "ldap_tls_certificate_key_file") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_certificate_key_file);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);
	} else if (strcmp(s, o = "ldap_tls_certificate_file") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_certificate_file);
		if (e == NULL)
			e = set_metadb_type_ldap(metadb_typep);

	} else if (strcmp(s, o = "postgresql_serverhost") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_server_name);
		if (e == NULL)
			e = set_metadb_type_postgresql(metadb_typep);
	} else if (strcmp(s, o = "postgresql_serverport") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_server_port);
		if (e == NULL)
			e = set_metadb_type_postgresql(metadb_typep);
	} else if (strcmp(s, o = "postgresql_dbname") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_dbname);
		if (e == NULL)
			e = set_metadb_type_postgresql(metadb_typep);
	} else if (strcmp(s, o = "postgresql_user") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_user);
		if (e == NULL)
			e = set_metadb_type_postgresql(metadb_typep);
	} else if (strcmp(s, o = "postgresql_password") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_password);
		if (e == NULL)
			e = set_metadb_type_postgresql(metadb_typep);
	} else if (strcmp(s, o = "postgresql_conninfo") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_conninfo);
		if (e == NULL)
			e = set_metadb_type_postgresql(metadb_typep);

	} else if (strcmp(s, o = "localfs_datadir") == 0) {
		e = parse_set_var(p, &gfarm_localfs_datadir);
		if (e == NULL)
			e = set_metadb_type_localfs(metadb_typep);

	} else if (strcmp(s, o = "agent_serverhost") == 0) {
		e = parse_set_func(p, gfarm_agent_name_set);
	} else if (strcmp(s, o = "agent_serverport") == 0) {
		e = parse_set_func(p, gfarm_agent_port_set);
	} else if (strcmp(s, o = "agent_socket_path") == 0) {
		e = parse_set_func(p, gfarm_agent_sock_path_set);

	} else if (strcmp(s, o = "auth") == 0) {
		e = parse_auth_arguments(p, &o);
	} else if (strcmp(s, o = "netparam") == 0) {
		e = parse_netparam_arguments(p, &o);
	} else if (strcmp(s, o = "sockopt") == 0) {
		e = parse_sockopt_arguments(p, &o);
	} else if (strcmp(s, o = "address_use") == 0) {
		e = parse_address_use_arguments(p, &o);
	} else if (strcmp(s, o = "local_user_map") == 0) {
		e = parse_local_user_map(p, &o);
	} else if (strcmp(s, o = "client_architecture") == 0) {
		e = parse_client_architecture(p, &o);

	} else if (strcmp(s, o = "dir_cache_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_dir_cache_timeout);
	} else if (strcmp(s, o = "host_cache_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_host_cache_timeout);
	} else if (strcmp(s, o = "schedule_cache_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_schedule_cache_timeout);
	} else if (strcmp(s, o = "write_local_priority") == 0) {
		e = parse_set_misc_enabled(p, &schedule_write_local_priority);
	} else if (strcmp(s, o = "write_target_domain") == 0) {
		e = parse_set_var(p, &schedule_write_target_domain);
	} else if (strcmp(s, o = "minimum_free_disk_space") == 0) {
		e = parse_set_misc_offset(p, &minimum_free_disk_space);
	} else if (strcmp(s, o = "gfsd_connection_cache") == 0) {
		e = parse_set_misc_int(p, &gfarm_gfsd_connection_cache);

	} else {
		o = s;
		e = "unknown keyword";
	}
	*op = o;
	return (e);
}

static int
parse_write_local_priority(char *s)
{
	if (isdigit(*(unsigned char *)s))
		return (atoi(s) != 0);
	if (strcasecmp(s, "disable") == 0)
		return (0);
	return (1);
}

static char *
gfarm_config_env(void)
{
	char *s, *e = NULL;

	s = getenv("GFARM_WRITE_LOCAL_PRIORITY");
	if (s != NULL)
		schedule_write_local_priority = parse_write_local_priority(s);

	s = getenv("GFARM_WRITE_TARGET_DOMAIN");
	if (s != NULL)
		schedule_write_target_domain = strdup(s);

	s = getenv("GFARM_MINIMUM_FREE_DISK_SPACE");
	if (s != NULL)
		e = parse_set_misc_offset(s, &minimum_free_disk_space);

	return (e);
}

static char *
gfarm_config_read_file(char *config_file, int *open_failedp,
	enum gfarm_metadb_backend_type *metadb_typep)
{
	int lineno = 0;
	char *s, *p, *e, *o, buffer[1024];
	static const char open_error_fmt[] = "%s: %s";
	static const char token_error_fmt[] = "%s: line %d: %s";
	static const char syntax_error_fmt[] = "%s: line %d: %s: %s";
	static char error[256];
	FILE *config = fopen(config_file, "r");

	if (config == NULL) {
		if (open_failedp != NULL)
			*open_failedp = 1;
#ifdef HAVE_SNPRINTF
		snprintf(error, sizeof(error), open_error_fmt,
		    config_file, strerror(errno));
		return (error);
#else
		if (strlen(open_error_fmt) + strlen(config_file) +
		    strlen(strerror(errno)) < sizeof(error)) {
			sprintf(error, open_error_fmt,
			    config_file, strerror(errno));
			return (error);
		} else {
			return (gfarm_errno_to_error(errno));
		}
#endif
	}
	if (open_failedp != NULL)
		*open_failedp = 0;
	 
	while (fgets(buffer, sizeof buffer, config) != NULL) {
		lineno++;
		p = buffer;
		s = gfarm_strtoken(&p, &e);
		if (e != NULL) {
#ifdef HAVE_SNPRINTF
			snprintf(error, sizeof(error), token_error_fmt,
			    config_file, lineno, e);
			e = error;
#else
			if (strlen(token_error_fmt) + strlen(config_file) +
			    GFARM_INT32STRLEN + strlen(e) < sizeof(error)) {
				sprintf(error, token_error_fmt,
				    config_file, lineno, e);
				e = error;
			} else {
				/* XXX: no file name, no line number */
			}
#endif
			fclose(config);
			return (e);
		}

		if (s == NULL) /* blank or comment line */
			continue;
		e = parse_one_line(s, p, &o, metadb_typep);
		if (e != NULL) {
#ifdef HAVE_SNPRINTF
			snprintf(error, sizeof(error), syntax_error_fmt,
			    config_file, lineno, o, e);
			e = error;
#else
			if (strlen(syntax_error_fmt) + strlen(config_file) +
			    GFARM_INT32STRLEN + strlen(o) + strlen(e) <
			    sizeof(error)) {
				sprintf(error, syntax_error_fmt,
				    config_file, lineno, o, e);
				e = error;
			} else {
				/* XXX: no file name, no line number */
			}
#endif
			fclose(config);
			return (e);
		}
	}
	fclose(config);
	return (NULL);
}

/*
 * set default value of configurations.
 */
static void
gfarm_config_set_default_ports(void)
{
	if (gfarm_spool_server_portname != NULL) {
		int p = strtol(gfarm_spool_server_portname, NULL, 0);
		struct servent *sp =
			getservbyname(gfarm_spool_server_portname, "tcp");

		if (sp != NULL)
			gfarm_spool_server_port = ntohs(sp->s_port);
		else if (p != 0)
			gfarm_spool_server_port = p;
	}
	if (gfarm_metadb_server_portname != NULL) {
		int p = strtol(gfarm_metadb_server_portname, NULL, 0);
		struct servent *sp =
			getservbyname(gfarm_metadb_server_portname, "tcp");

		if (sp != NULL)
			gfarm_metadb_server_port = ntohs(sp->s_port);
		else if (p != 0)
			gfarm_metadb_server_port = p;
	}
}

static void
gfarm_config_set_default_spool_on_client(void)
{
	char *host, *e, *e2;
	/*
	 * When this node is a filesystem node,
	 * gfarm_spool_root should be obtained by gfsd
	 * not by the config file.
	 */
	e = gfarm_host_get_canonical_self_name(&host);
	if (e == NULL && gfarm_host_is_local(host)) {
		char *old_ptr = gfarm_spool_root;

		e = gfs_client_get_spool_root_with_reconnection(host,
		    NULL, &e2, &gfarm_spool_root);
		if (e == NULL && e2 == NULL) {
			gfarm_is_active_file_system_node = 1;
			if (old_ptr != NULL &&
			    old_ptr != gfarm_spool_root_default)
				free(old_ptr);
		}
	}
	if (gfarm_spool_root == NULL)
		/* XXX - this case is not recommended. */
		gfarm_spool_root = gfarm_spool_root_default;
}

static void
gfarm_config_set_default_spool_on_server(void)
{
	if (gfarm_spool_root == NULL) {
		/* XXX - this case is not recommended. */
		gfarm_spool_root = gfarm_spool_root_default;
	}
}

static void
gfarm_config_set_default_misc(void)
{
	if (gfarm_dir_cache_timeout == MISC_DEFAULT)
		gfarm_dir_cache_timeout = GFARM_DIR_CACHE_TIMEOUT_DEFAULT;
	if (gfarm_host_cache_timeout == MISC_DEFAULT)
		gfarm_host_cache_timeout = GFARM_HOST_CACHE_TIMEOUT_DEFAULT;
	if (gfarm_schedule_cache_timeout == MISC_DEFAULT)
		gfarm_schedule_cache_timeout =
		    GFARM_SCHEDULE_CACHE_TIMEOUT_DEFAULT;
	if (schedule_write_local_priority == MISC_DEFAULT)
		schedule_write_local_priority =
		    GFARM_SCHEDULE_WRITE_LOCAL_PRIORITY_DEFAULT;
	if (minimum_free_disk_space == MISC_DEFAULT)
		minimum_free_disk_space =
		    GFARM_MINIMUM_FREE_DISK_SPACE_DEFAULT;
	if (gfarm_gfsd_connection_cache == MISC_DEFAULT)
		gfarm_gfsd_connection_cache =
		    GFARM_GFSD_CONNECTION_CACHE_DEFAULT;
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
char *
gfarm_config_read(void)
{
	char *e, *home;
	int rc_open_failed, etc_open_failed, rc_need_free;
	enum gfarm_metadb_backend_type
		etc_config = GFARM_METADB_TYPE_UNKNOWN,
		rc_config = GFARM_METADB_TYPE_UNKNOWN;

	static char gfarm_client_rc[] = GFARM_CLIENT_RC;
	char *rc;

	switch (config_read) {
	case gfarm_config_not_read:
		config_read = gfarm_config_user_read;
		break;
	case gfarm_config_user_read:
		return (NULL);
	case gfarm_config_server_read:
		return ("gfarm_config_read() is called "
			"after gfarm_server_config_read() is called. "
			"something wrong");
	}

	/* obtain parameters from environment variables */
	gfarm_config_env();

	rc_need_free = 0;
	rc = getenv("GFARM_CONFIG_FILE");
	if (rc == NULL) {
		/*
		 * result of gfarm_get_local_homedir() should not be trusted.
		 * (maybe forged)
		 */
		home = gfarm_get_local_homedir();

		GFARM_MALLOC_ARRAY(rc,
			strlen(home) + 1 + sizeof(gfarm_client_rc));
		if (rc == NULL)
			return (GFARM_ERR_NO_MEMORY);
		sprintf(rc, "%s/%s", home, gfarm_client_rc);
		rc_need_free = 1;
	}
	gfarm_stringlist_init(&local_user_map_file_list);
	e = gfarm_config_read_file(rc, &rc_open_failed, &rc_config);
	if (rc_need_free)
		free(rc);
	if (e != NULL && !rc_open_failed)
		return (e);
	
	e = gfarm_config_read_file(gfarm_config_file,
	    &etc_open_failed, &etc_config);
	/* if ~/.gfarmrc was successfully read, an error at open here is ok */
	if (e != NULL && (!etc_open_failed || rc_open_failed))
		return (e);

	gfarm_config_set_default_ports();
	gfarm_config_set_default_misc();

	return (config_metadb_type(rc_config != GFARM_METADB_TYPE_UNKNOWN ?
	    rc_config : etc_config));
}

/* the following function is for server. */
char *
gfarm_server_config_read(void)
{
	char *e;
	enum gfarm_metadb_backend_type etc_config = GFARM_METADB_TYPE_UNKNOWN;

	switch (config_read) {
	case gfarm_config_not_read:
		config_read = gfarm_config_server_read;
		break;
	case gfarm_config_user_read:
		return ("gfarm_server_config_read() is called "
			"after gfarm_config_read() is called. "
			"something wrong");
	case gfarm_config_server_read:
		return (NULL);
	}

	/* obtain parameters from environment variables */
	gfarm_config_env();

	gfarm_stringlist_init(&local_user_map_file_list);
	e = gfarm_config_read_file(gfarm_config_file, NULL, &etc_config);
	if (e != NULL)
		return (e);

	gfarm_config_set_default_ports();
	gfarm_config_set_default_misc();

	/*
	 * the following config_metadb_type() may be unnecessary,
	 * because both gfmd and gfsd don't need metadb for now.
	 */
	return (config_metadb_type(etc_config));
}

#ifdef STRTOKEN_TEST
main()
{
	char buffer[1024];
	char *cursor, *token, *error;

	while (fgets(buffer, sizeof buffer, stdin) != NULL) {
		cursor = buffer;
		while ((token = strtoken(&cursor, &error)) != NULL)
			printf("token: <%s>\n", token);
		if (error == NULL)
			printf("newline\n");
		else
			printf("error: %s\n", error);
	}
}
#endif

void
gfs_display_timers(void)
{
	gfs_pio_display_timers();
	gfs_pio_section_display_timers();
	gfs_stat_display_timers();
	gfs_unlink_display_timers();
}

char *gfarm_debug_command;
char gfarm_debug_pid[GFARM_INT64STRLEN + 1];

static int
gfarm_call_debugger(void)
{
	int pid;

	if ((pid = fork()) == 0) {
		execlp("xterm", "xterm", "-e", "gdb",
		       gfarm_debug_command, gfarm_debug_pid, NULL);
		perror("xterm");
		_exit(1);
	}
	return (pid);
}

int
gfarm_attach_debugger(void)
{
	int pid = gfarm_call_debugger();

	/* not really correct way to wait until attached, but... */
	sleep(5);
	return (pid);
}

void
gfarm_sig_debug(int sig)
{
	int pid, status;
	static int already_called = 0;
	static char message[] = "signal 00 caught\n";

	message[7] = sig / 10 + '0';
	message[8] = sig % 10 + '0';
	write(2, message, sizeof(message) - 1);

	if (already_called)
		abort();
	already_called = 1;

	pid = gfarm_call_debugger();
	if (pid == -1) {
		perror("fork"); /* XXX dangerous to call from signal handler */
		abort();
	} else {
		waitpid(pid, &status, 0);
		_exit(1);
	}
}

void
gfarm_debug_initialize(char *command)
{
	gfarm_debug_command = command;
	sprintf(gfarm_debug_pid, "%ld", (long)getpid());

	signal(SIGQUIT, gfarm_sig_debug);
	signal(SIGILL,  gfarm_sig_debug);
	signal(SIGTRAP, gfarm_sig_debug);
	signal(SIGABRT, gfarm_sig_debug);
	signal(SIGFPE,  gfarm_sig_debug);
	signal(SIGBUS,  gfarm_sig_debug);
	signal(SIGSEGV, gfarm_sig_debug);
}

/*
 * redirect stdout
 */

GFS_File gf_stdout, gf_stderr;
int gf_profile;
int gf_on_demand_replication;
int gf_hook_default_global;

static int total_nodes = -1, node_index = -1;
static char *stdout_file = NULL, *stderr_file = NULL;

static char *
gfarm_parse_env_client(void)
{
	char *env;

	if ((env = getenv("GFARM_NODE_RANK")) != NULL)
		node_index = strtol(env, NULL, 0);

	if ((env = getenv("GFARM_NODE_SIZE")) != NULL)
		total_nodes = strtol(env, NULL, 0);

	if ((env = getenv("GFARM_FLAGS")) != NULL) {
		for (; *env; env++) {
			switch (*env) {
			case 'p': gf_profile = 1; break;
			case 'r': gf_on_demand_replication = 1; break;
			case 'g': gf_hook_default_global = 1; break;
			}
		}
	}

	return (NULL);
}

/*
 * eliminate arguments added by the gfrun command.
 * this way is only used when the gfarm program is invoked directly,
 * or invoked via "gfrun -S".
 * if the gfarm program is invoked via gfexec, gfarm_parse_env_client() is
 * used instead.
 */

static char *
gfarm_parse_argv(int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	char *argv0 = *argv;
	char *e;

	--argc;
	++argv;
	while (argc > 0 && argv[0][0] == '-' && argv[0][1] == '-') {
		if (strcmp(&argv[0][2], "gfarm_index") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				node_index = strtol(*argv, NULL, 0);
		}
		else if (strcmp(&argv[0][2], "gfarm_nfrags") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				total_nodes = strtol(*argv, NULL, 0);
		}
		else if (strcmp(&argv[0][2], "gfarm_stdout") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				stdout_file = *argv;
		}
		else if (strcmp(&argv[0][2], "gfarm_stderr") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				stderr_file = *argv;
		}
		else if (strcmp(&argv[0][2], "gfarm_profile") == 0)
			gf_profile = 1;
		else if (strcmp(&argv[0][2], "gfarm_replicate") == 0)
			gf_on_demand_replication = 1;
		else if (strcmp(&argv[0][2], "gfarm_hook_global") == 0)
			gf_hook_default_global = 1;
		else if (strcmp(&argv[0][2], "gfarm_cwd") == 0) {
			--argc;
			++argv;
			if (argc > 0) {
				e = gfs_chdir(*argv);
				if (e != NULL)
					return (e);
			}
		}
		else
			break;
		--argc;
		++argv;
	}

	++argc;
	--argv;

	*argcp = argc;
	*argv = argv0;
	*argvp = argv;

	return (NULL);
}

static char *
gfarm_eval_env_arg(void)
{
	char *e;

	if (node_index != -1 && total_nodes != -1) {
		e = gfs_pio_set_local(node_index, total_nodes);
		if (e != NULL)
			return (e);
	}

	/* redirect stdout and stderr */
	if (stdout_file != NULL) {
		e = gfarm_redirect_file(1, stdout_file, &gf_stdout);
		if (e != NULL)
			return (e);
	}
	if (stderr_file != NULL) {
		e = gfarm_redirect_file(2, stderr_file, &gf_stderr);
		if (e != NULL)
			return (e);
	}

	gfs_profile(gfarm_timerval_calibrate());

	return (NULL);
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
char *
gfarm_client_initialize(int *argcp, char ***argvp)
{
	char *e;
	int syslog_facility;
#ifdef HAVE_GSI
	int saved_auth_verb;
#endif

	if (gfarm_initialize_done)
		return (NULL);

	gfarm_error_initialize();

	if ((e = getenv("GFARM_SYSLOG_OUTPUT")) != NULL) {
		syslog_facility = gflog_syslog_name_to_facility(e);
		if (syslog_facility == -1) {
			gflog_warning(
			    "GFARM_SYSLOG_OUTPUT=%s: unknown facility", e);
			syslog_facility = LOG_USER;
		}
		gflog_syslog_open(LOG_PID, syslog_facility);
	}

	e = gfarm_set_local_user_for_this_local_account();
	if (e != NULL)
		return (e);
	e = gfarm_config_read();
	if (e != NULL)
		return (e);
#ifdef HAVE_GSI
	/*
	 * Suppress verbose error messages.  The message will be
	 * displayed later in gfarm_auth_request_gsi().
	 */
	saved_auth_verb = gflog_auth_set_verbose(0);
	(void)gfarm_gsi_client_initialize();
	gflog_auth_set_verbose(saved_auth_verb);
#endif
	e = gfarm_set_global_user_for_this_local_account();
	if (e != NULL)
		return (e);
	/* gfarm_metadb_initialize() is called on demand in gfarm_ldap_check */

	gfarm_config_set_default_spool_on_client();

	if (getenv("DISPLAY") != NULL && argvp != NULL)
		gfarm_debug_initialize((*argvp)[0]);

	e = gfarm_parse_env_client();
	if (e != NULL)
		return (e);
	if (argvp != NULL) { /* not called from gfs_hook */
		/* command line arguments take precedence over environments */
		e = gfarm_parse_argv(argcp, argvp);
		if (e != NULL)
			return (e);
	}
	e = gfarm_eval_env_arg();
	if (e != NULL)
		return (e);

	gfarm_initialize_done = 1;

	return (NULL);
}

/* the following function is for server. */
char *
gfarm_server_initialize(void)
{
	char *e;

	gfarm_error_initialize();

	e = gfarm_server_config_read();
	if (e != NULL)
		return (e);

	gfarm_config_set_default_spool_on_server();

	return (NULL);
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
char *
gfarm_client_terminate(void)
{
	char *e, *e_save = NULL;

	if (!gfarm_initialize_done)
		return (NULL);

	gfs_profile(gfs_display_timers());

	if (gf_stdout != NULL) {
		fflush(stdout);
		e = gfs_pio_close(gf_stdout);
		gf_stdout = NULL;
		if (e_save == NULL)
			e_save = e;
	}
	if (gf_stderr != NULL) {
		fflush(stderr);
		e = gfs_pio_close(gf_stderr);
		gf_stderr = NULL;
		if (e_save == NULL)
			e_save = e;
	}
	gfs_client_terminate();
	/* gfarm_metadb_terminate() will be called in gfarm_terminate() */
	gfarm_config_clear();

	gfarm_initialize_done = 0;

	return (e_save);
}

/* the following function is for server. */
char *
gfarm_server_terminate(void)
{
	gfs_client_terminate();
	gfarm_config_clear();
	return (NULL);
}

#ifdef CONFIG_TEST
main()
{
	char *e;

	gfarm_error_initialize();
	e = gfarm_set_local_user_for_this_local_account();
	if (e) {
		fprintf(stderr,
			"gfarm_set_local_user_for_this_local_account(): %s\n",
			e);
		exit(1);
	}
	e = gfarm_config_read();
	if (e) {
		fprintf(stderr, "gfarm_config_read(): %s\n", e);
		exit(1);
	}
	printf("gfarm_spool_root = <%s>\n", gfarm_spool_root);
	printf("gfarm_spool_server_port = <%d>\n", gfarm_spool_server_port);
	printf("gfarm_metadb_server_name = <%s>\n", gfarm_metadb_server_name);
	printf("gfarm_metadb_server_port = <%d>\n", gfarm_metadb_server_name);

	printf("gfarm_ldap_server_name = <%s>\n", gfarm_ldap_server_name);
	printf("gfarm_ldap_server_port = <%s>\n", gfarm_ldap_server_port);
	printf("gfarm_ldap_base_dn = <%s>\n", gfarm_ldap_base_dn);
	return (0);
}
#endif
