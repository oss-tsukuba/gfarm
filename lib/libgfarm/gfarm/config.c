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

#include <sys/socket.h>
#include <netinet/in.h> /* ntohs */
#include <netdb.h>

#include <time.h>
#include <pwd.h>
#include <gfarm/gfarm.h>
#include "gfutil.h"
#include "liberror.h"
#include "hostspec.h"
#include "param.h"
#include "sockopt.h"
#if 0 /* XXX address_use is disabled for now */
#include "host.h"
#endif
#include "auth.h"
#include "config.h"
#include "gfm_proto.h" /* GFMD_DEFAULT_PORT */
#include "gfs_proto.h" /* GFSD_DEFAULT_PORT */

int gfarm_initialized = 0;

char *gfarm_config_file = GFARM_CONFIG;

void
gfarm_config_set_filename(char *filename)
{
	gfarm_config_file = filename;
}

int gfarm_authentication_verbose = 0;

/* XXX move actual function definition here */
static gfarm_error_t gfarm_strtoken(char **, char **);

/*
 * GFarm username handling
 */

static gfarm_stringlist local_user_map_file_list;

/* the return value of the following function should be free(3)ed */
static gfarm_error_t
map_user(char *from, char **to_p,
	char *(mapping)(char *, char *, char *), gfarm_error_t error_redefined)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	FILE *map = NULL;
	char *mapfile = NULL;
	int i, list_len, mapfile_mapped_index;
	char buffer[1024], *g_user, *l_user, *mapped, *tmp;
	int lineno = 0;

	*to_p = NULL;
	list_len = gfarm_stringlist_length(&local_user_map_file_list);
	mapfile_mapped_index = list_len;
	for (i = 0; i < list_len; i++) {
		mapfile = gfarm_stringlist_elem(&local_user_map_file_list, i);
		if ((map = fopen(mapfile, "r")) == NULL) {
			gflog_error(mapfile, strerror(errno));
			return (GFARM_ERR_CANT_OPEN);
		}
		lineno = 0;
		while (fgets(buffer, sizeof buffer, map) != NULL) {
			char *bp = buffer;

			lineno++;
			e = gfarm_strtoken(&bp, &g_user);
			if (e != GFARM_ERR_NO_ERROR)
				goto finish;
			if (g_user == NULL) /* blank or comment line */
				continue;
			e = gfarm_strtoken(&bp, &l_user);
			if (e != GFARM_ERR_NO_ERROR)
				goto finish;
			if (l_user == NULL) {
				e = GFARM_ERRMSG_MISSING_LOCAL_USER;
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
			e = gfarm_strtoken(&bp, &tmp);
			if (e != GFARM_ERR_NO_ERROR)
				goto finish;
			if (tmp != NULL) {
				e = GFARM_ERRMSG_TOO_MANY_ARGUMENTS;
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
	if (e != GFARM_ERR_NO_ERROR) {
		char *msgbuf = malloc(strlen(mapfile) + 6 /* " line " */
		    + GFARM_INT64STRLEN + 1);

		if (*to_p != NULL)	 
			free(*to_p);
		if (msgbuf == NULL) {
			gflog_error("gfarm: map_user", gfarm_error_string(e));
		} else {
			sprintf(msgbuf, "%s line %d", mapfile, lineno);
			gflog_error(msgbuf, gfarm_error_string(e));
			free(msgbuf);
		}
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
gfarm_error_t
gfarm_username_map_global_to_local(char *global_user, char **local_user_p)
{
	return (map_user(global_user, local_user_p,
	    map_global_to_local, GFARM_ERRMSG_GLOBAL_USER_REDEFIEND));
}

static char *
map_local_to_global(char *from, char *global_user, char *local_user)
{
	if (strcmp(from, local_user) == 0)
		return (global_user);
	return (NULL);
}

/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_username_map_local_to_global(char *local_user, char **global_user_p)
{
	return (map_user(local_user, global_user_p,
	    map_local_to_global, GFARM_ERRMSG_LOCAL_USER_REDEFIEND));
}

static gfarm_error_t
set_string(char **var, char *value)
{
	if (*var != NULL)
		free(*var);
	*var = strdup(value);
	if (*var == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * client side variables
 */
static char *gfarm_global_username = NULL;
static char *gfarm_local_username = NULL;
static char *gfarm_local_homedir = NULL;

gfarm_error_t
gfarm_set_global_username(char *global_username)
{
	return (set_string(&gfarm_global_username, global_username));
}

char *
gfarm_get_global_username(void)
{
	return (gfarm_global_username);
}

gfarm_error_t
gfarm_set_local_username(char *local_username)
{
	return (set_string(&gfarm_local_username, local_username));
}

char *
gfarm_get_local_username(void)
{
	return (gfarm_local_username);
}

gfarm_error_t
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
gfarm_error_t
gfarm_set_local_user_for_this_local_account(void)
{
	gfarm_error_t error;
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
	if (error != GFARM_ERR_NO_ERROR)
		return (error);
	error = gfarm_set_local_homedir(home);
	if (error != GFARM_ERR_NO_ERROR)
		return (error);
	return (error);
}

gfarm_error_t
gfarm_set_global_user_for_this_local_account(void)
{
	gfarm_error_t e;
	char *local_user, *global_user;

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
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
#ifdef HAVE_GSI
 set_global_username:
#endif
	e = gfarm_set_global_username(global_user);
	gfarm_stringlist_free_deeply(&local_user_map_file_list);
	return (e);
}

/*
 * GFarm Configurations.
 *
 * These initial values should be NULL, otherwise the value incorrectly
 * free(3)ed in the get_one_argument() function below.
 * If you would like to provide default value other than NULL, set the
 * value at the end of gfarm_config_read().
 */
static char gfarm_spool_root_default[] = GFARM_SPOOL_ROOT;
char *gfarm_spool_root = NULL;
static char *gfarm_spool_server_portname = NULL;

/* GFM dependent */
char *gfarm_metadb_server_name = NULL;
static char *gfarm_metadb_server_portname = NULL;

/* LDAP dependent */
char *gfarm_ldap_server_name = NULL;
char *gfarm_ldap_server_port = NULL;
char *gfarm_ldap_base_dn = NULL;

int gfarm_spool_server_port = GFSD_DEFAULT_PORT;
int gfarm_metadb_server_port = GFMD_DEFAULT_PORT;

void
gfarm_config_clear(void)
{
	if (gfarm_spool_root != NULL) {
		/*
		 * In case of the default spool root, do not free the
		 * memory space becase it is statically allocated.
		 */
		if (gfarm_spool_root != gfarm_spool_root_default)
			free(gfarm_spool_root);
		gfarm_spool_root = NULL;
	}
	if (gfarm_spool_server_portname != NULL) {
		free(gfarm_spool_server_portname);
		gfarm_spool_server_portname = NULL;
	}
	if (gfarm_metadb_server_name != NULL) {
		free(gfarm_metadb_server_name);
		gfarm_metadb_server_name = NULL;
	}
	if (gfarm_metadb_server_portname != NULL) {
		free(gfarm_metadb_server_portname);
		gfarm_metadb_server_portname = NULL;
	}
	if (gfarm_ldap_server_name != NULL) {
		free(gfarm_ldap_server_name);
		gfarm_ldap_server_name = NULL;
	}
	if (gfarm_ldap_server_port != NULL) {
		free(gfarm_ldap_server_port);
		gfarm_ldap_server_port = NULL;
	}
	if (gfarm_ldap_base_dn != NULL) {
		free(gfarm_ldap_base_dn);
		gfarm_ldap_base_dn = NULL;
	}
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

gfarm_error_t
gfarm_strtoken(char **cursorp, char **tokenp)
{
	unsigned char *top, *p, *s = *(unsigned char **)cursorp;

	while (*s != '\n' && isspace(*s))
		s++;
	if (*s == '\0' || *s == '\n' || *s == '#') {
		/* end of line */
		*cursorp = (char *)s;
		*tokenp = NULL;
		return (GFARM_ERR_NO_ERROR);
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
				if (*s == '\0' || *s == '\n')
					return (GFARM_ERRMSG_UNTERMINATED_SINGLE_QUOTE);
				*p++ = *s++;
			}
			s++;
			break;
		case '"':
			s++;
			for (;;) {
				if (*s == '"')
					break;
				if (*s == '\0' || *s == '\n')
					return (GFARM_ERRMSG_UNTERMINATED_DOUBLE_QUOTE);
				if (*s == '\\') {
					if (s[1] == '\0' || s[1] == '\n')
						return (GFARM_ERRMSG_UNTERMINATED_DOUBLE_QUOTE);
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
			if (*s == '\0' || *s == '\n')
				return (GFARM_ERRMSG_INCOMPLETE_ESCAPE);
			*p++ = *s++;
			break;
		case '\n':	
		case '#':
		case '\0':
			*p = '\0';
			*cursorp = (char *)s;
			*tokenp = (char *)top;
			return (GFARM_ERR_NO_ERROR);
		default:
			if (isspace(*s)) {
				*p = '\0';
				*cursorp = (char *)(s + 1);
				*tokenp = (char *)top;
				return (GFARM_ERR_NO_ERROR);
			}
			*p++ = *s++;
			break;
		}
	}
}

static gfarm_error_t
parse_auth_arguments(char *p, char **op)
{
	gfarm_error_t e;
	char *tmp, *command, *auth, *host;
	enum gfarm_auth_method auth_method;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "auth") == 0); */

	e = gfarm_strtoken(&p, &command);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (command == NULL)
		return (GFARM_ERRMSG_MISSING_AUTH_COMMAND_ARGUMENT);

	e = gfarm_strtoken(&p, &auth);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (auth == NULL)
		return (GFARM_ERRMSG_MISSING_AUTH_METHOD_ARGUMENT);
	if (strcmp(auth, "*") == 0 || strcmp(auth, "ALL") == 0) {
		auth_method = GFARM_AUTH_METHOD_ALL;
	} else {
		e = gfarm_auth_method_parse(auth, &auth_method);
		if (e != GFARM_ERR_NO_ERROR) {
			*op = "2nd(auth-method) argument";
			if (e == GFARM_ERR_NO_SUCH_OBJECT)
				e = GFARM_ERRMSG_UNKNOWN_AUTH_METHOD;
			return (e);
		}
	}

	e = gfarm_strtoken(&p, &host);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (host == NULL)
		return (GFARM_ERRMSG_MISSING_HOST_SPEC_ARGUMENT);
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (tmp != NULL)
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
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
		return (GFARM_ERRMSG_UNKNOWN_AUTH_SUBCOMMAND);
	}
	if (e != GFARM_ERR_NO_ERROR)
		gfarm_hostspec_free(hostspecp);
	return (e);
}

static gfarm_error_t
parse_netparam_arguments(char *p, char **op)
{
	gfarm_error_t e;
	char *tmp, *option, *host;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "netparam") == 0); */

	e = gfarm_strtoken(&p, &option);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (option == NULL)
		return (GFARM_ERRMSG_MISSING_NETPARAM_OPTION_ARGUMENT);

	e = gfarm_strtoken(&p, &host);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (host == NULL) {
		/* if 2nd argument is omitted, it is treated as "*". */
		host = "*";
	} else if ((e = gfarm_strtoken(&p, &tmp)) != GFARM_ERR_NO_ERROR) {
		return (e);
	} else if (tmp != NULL) {
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	}
	
	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "2nd(host-spec) argument";
		return (e);
	}

	e = gfarm_netparam_config_add_long(option, hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `option' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(sockopt-option) argument";
		gfarm_hostspec_free(hostspecp);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_sockopt_arguments(char *p, char **op)
{
	gfarm_error_t e;
	char *tmp, *option, *host;
	struct gfarm_hostspec *hostspecp;
	int is_listener;

	/* assert(strcmp(*op, "sockopt") == 0); */

	e = gfarm_strtoken(&p, &option);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (option == NULL)
		return (GFARM_ERRMSG_MISSING_SOCKOPT_OPTION_ARGUMENT);

	e = gfarm_strtoken(&p, &host);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (host == NULL) {
		/*
		 * if 2nd argument is omitted, it is treated as:
		 *	"LISTENER" + "*".
		 */
		is_listener = 1;
	} else {
		is_listener = strcmp(host, "LISTENER") == 0;
		if ((e = gfarm_strtoken(&p, &tmp)) != GFARM_ERR_NO_ERROR)
			return (e);
		if (tmp != NULL)
			return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	}
	
	if (is_listener) {
		e = gfarm_sockopt_listener_config_add(option);
		if (e != GFARM_ERR_NO_ERROR) {
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
		if (e != GFARM_ERR_NO_ERROR) {
			/*
			 * we don't return `host' to *op here,
			 * because it may be too long.
			 */
			*op = "2nd(host-spec) argument";
			return (e);
		}

		e = gfarm_sockopt_config_add(option, hostspecp);
		if (e != GFARM_ERR_NO_ERROR) {
			/*
			 * we don't return `option' to *op here,
			 * because it may be too long.
			 */
			*op = "1st(sockopt-option) argument";
			gfarm_hostspec_free(hostspecp);
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

#if 0 /* XXX address_use is disabled for now */
static gfarm_error_t
parse_address_use_arguments(char *p, char **op)
{
	gfarm_error_t e;
	char *tmp, *address;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "address_use") == 0); */

	e = gfarm_strtoken(&p, &address);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (address == NULL)
		return (GFARM_ERRMSG_MISSING_ADDRESS_ARGUMENT);
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (tmp != NULL)
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);

	e = gfarm_hostspec_parse(address, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(address) argument";
		return (e);
	}

	e = gfarm_host_address_use(hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `option' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(address) argument";
		gfarm_hostspec_free(hostspecp);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}
#endif

static gfarm_error_t
parse_local_user_map(char *p, char **op)
{
	gfarm_error_t e;
	char *tmp, *mapfile;

	e = gfarm_strtoken(&p, &mapfile);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (mapfile == NULL)
		return (GFARM_ERRMSG_MISSING_USER_MAP_FILE_ARGUMENT);
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (tmp != NULL)
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	mapfile = strdup(mapfile);
	if (mapfile == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = gfarm_stringlist_add(&local_user_map_file_list, mapfile);
	return (e);
}

static gfarm_error_t
get_one_argument(char *p, char **rv)
{
	gfarm_error_t e;
	char *tmp, *s;

	e = gfarm_strtoken(&p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (s == NULL)
		return (GFARM_ERRMSG_MISSING_ARGUMENT);
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (tmp != NULL)
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);

	if (*rv != NULL) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);

	s = strdup(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*rv = s;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_one_line(char *s, char *p, char **op)
{
	gfarm_error_t e;
	char *o;

	if (strcmp(s, o = "spool") == 0) {
		e = get_one_argument(p, &gfarm_spool_root);
	} else if (strcmp(s, o = "spool_serverport") == 0) {
		e = get_one_argument(p, &gfarm_spool_server_portname);
	} else if (strcmp(s, o = "metadb_serverhost") == 0) {
		e = get_one_argument(p, &gfarm_metadb_server_name);
	} else if (strcmp(s, o = "metadb_serverport") == 0) {
		e = get_one_argument(p, &gfarm_metadb_server_portname);
	} else if (strcmp(s, o = "ldap_serverhost") == 0) {
		e = get_one_argument(p, &gfarm_ldap_server_name);
	} else if (strcmp(s, o = "ldap_serverport") == 0) {
		e = get_one_argument(p, &gfarm_ldap_server_port);
	} else if (strcmp(s, o = "ldap_base_dn") == 0) {
		e = get_one_argument(p, &gfarm_ldap_base_dn);
	} else if (strcmp(s, o = "auth") == 0) {
		e = parse_auth_arguments(p, &o);
	} else if (strcmp(s, o = "netparam") == 0) {
		e = parse_netparam_arguments(p, &o);
	} else if (strcmp(s, o = "sockopt") == 0) {
		e = parse_sockopt_arguments(p, &o);
#if 0 /* XXX address_use is disabled for now */
	} else if (strcmp(s, o = "address_use") == 0) {
		e = parse_address_use_arguments(p, &o);
#endif
	} else if (strcmp(s, o = "local_user_map") == 0) {
		e = parse_local_user_map(p, &o);
	} else {
		o = s;
		e = GFARM_ERRMSG_UNKNOWN_KEYWORD;
	}
	*op = o;
	return (e);
}

gfarm_error_t
gfarm_init_user_map(void)
{
	gfarm_stringlist_init(&local_user_map_file_list);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_config_read_file(FILE *config, char *config_file, int *lineno_p)
{
	gfarm_error_t e;
	int lineno = 0;
	char *s, *p, *o, buffer[1024];

	while (fgets(buffer, sizeof buffer, config) != NULL) {
		lineno++;
		p = buffer;
		e = gfarm_strtoken(&p, &s);

		if (e == GFARM_ERR_NO_ERROR) {
			if (s == NULL) /* blank or comment line */
				continue;
			e = parse_one_line(s, p, &o);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			fclose(config);
			*lineno_p = lineno;
			return (e);
		}
	}
	fclose(config);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * set default value of configurations.
 */
void
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
