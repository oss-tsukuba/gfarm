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
#include "hostspec.h"
#include "host.h"
#include "param.h"
#include "sockopt.h"
#include "auth.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfs_pio.h"	/* GFS_FILE_MODE_CALC_DIGEST */

#ifndef GFARM_CONFIG
#define GFARM_CONFIG	"/etc/gfarm.conf"
#endif
#ifndef GFARM_CLIENT_RC
#define GFARM_CLIENT_RC		".gfarmrc"
#endif
#ifndef GFARM_SPOOL_ROOT
#define GFARM_SPOOL_ROOT	"/var/spool/gfarm"
#endif

int gfarm_initialized = 0;

char *gfarm_config_file = GFARM_CONFIG;

void
gfarm_config_set_filename(char *filename)
{
	gfarm_config_file = filename;
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
	int i, list_len;
	char buffer[1024], *g_user, *l_user, *mapped, *e;
	int lineno = 0;
	static char fmt_open_error[] = "%s: %s";
	static char fmt_config_error[] = "%s: line %d: %s";
	static char error[256];

	e = NULL;
	*to_p = NULL;
	list_len = gfarm_stringlist_length(&local_user_map_file_list);
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
				    strcmp(mapped, *to_p) != 0) {
					e = error_redefined;
					goto finish;
				}
				*to_p = strdup(mapped);
				if (*to_p == NULL) {
					e = GFARM_ERR_NO_MEMORY;
					goto finish;
				}
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
	char *error = NULL;
	char *user;
	char *home;
	struct passwd *pwd;

	user = getenv("USER");
	if (user == NULL)
		user = getenv("LOGIN");
	if (user == NULL)
		user = getlogin();

	home = getenv("HOME");

	if (user == NULL || home == NULL) {
		pwd = NULL;
		if (user != NULL)
			pwd = getpwnam(user);
		if (pwd == NULL) {
			/*user = NULL;*/ /* maybe getpwnam(user) failed */
			pwd = getpwuid(getuid());
		}
		if (pwd != NULL) {
			if (user == NULL)
				user = pwd->pw_name;
			if (home == NULL)
				home = pwd->pw_dir;
		} else {
			/* XXX */
			if (user == NULL)
				user = "nobody";
			if (home == NULL)
				home = "/";
		}
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

	local_user = gfarm_get_local_username();
	e = gfarm_local_to_global_username(local_user, &global_user);
	if (e != NULL)
		return (e);
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
		*cursorp = s;
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
			*cursorp = s;
			return (top);
		default:
			if (isspace(*s)) {
				*p = '\0';
				*cursorp = s + 1;
				return (top);
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
	if (strcmp(auth, "ALL") == 0) {
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
		/* if 2nd argument is omitted, it is treated as "ALL". */
		host = "ALL";
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
		 *	"LISTENER" + "ALL".
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
		e = gfarm_hostspec_parse(host != NULL ? host : "ALL",
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
get_one_argument(char *p, char **rv)
{
	char *s, *e;

	s = gfarm_strtoken(&p, &e);
	if (e != NULL)
		return (e);
	if (s == NULL)
		return ("missing argument");
	if (gfarm_strtoken(&p, &e) != NULL)
		return (GFARM_ERR_TOO_MANY_ARGUMENTS);

	if (*rv != NULL) /* first line has precedence */
		return (NULL);

	s = strdup(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*rv = s;
	return (NULL);
}

static char *
parse_one_line(char *s, char *p, char **op)
{
	char *e, *o;

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
	} else if (strcmp(s, o = "address_use") == 0) {
		e = parse_address_use_arguments(p, &o);
	} else if (strcmp(s, o = "local_user_map") == 0) {
		e = parse_local_user_map(p, &o);
	} else {
		o = s;
		e = "unknown keyword";
	}
	*op = o;
	return (e);
}

static char *
gfarm_config_read_file(FILE *config, char *config_file)
{
	int lineno = 0;
	char *s, *p, *e, *o, buffer[1024];
	static char format[] = "%s: line %d: %s: %s\n";
	static char error[256];

	while (fgets(buffer, sizeof buffer, config) != NULL) {
		lineno++;
		p = buffer;
		s = gfarm_strtoken(&p, &e);

		if (e == NULL) {
			if (s == NULL) /* blank or comment line */
				continue;
			e = parse_one_line(s, p, &o);
		}
		if (e != NULL) {
#ifdef HAVE_SNPRINTF
			snprintf(error, sizeof(error),
				 format, config_file, lineno, o, e);
			e = error;
#else
			if (strlen(format) + strlen(config_file) +
			    GFARM_INT32STRLEN + strlen(o) + strlen(e) <
			    sizeof(error)) {
				sprintf(error,
					format, config_file, lineno, o, e);
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
gfarm_config_set_default1(void)
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
gfarm_config_set_default2(void)
{
	if (gfarm_spool_root == NULL) {
		/*
		 * When this node is a filesystem node,
		 * gfarm_spool_root should be obtained by gfsd.
		 */
		char *host, *e;

		e = gfarm_host_get_canonical_self_name(&host);
		if (e == NULL && gfarm_host_is_local(host)) {
			struct sockaddr peer_addr;
			struct gfs_connection *gfs_server;

			e = gfarm_host_address_get(host,
				gfarm_spool_server_port, &peer_addr, NULL);
			if (e != NULL)
				goto ignore_error;

			e = gfs_client_connection(host, &peer_addr,
				&gfs_server);
			if (e != NULL)
				goto ignore_error;

			e = gfs_client_get_spool_root(gfs_server,
				&gfarm_spool_root);
		ignore_error:
			;
		}
		if (gfarm_spool_root == NULL)
			/* XXX - this case is not recommended. */
			gfarm_spool_root = GFARM_SPOOL_ROOT;
	}
}

static void
gfarm_config_set_default2_server(void)
{
	if (gfarm_spool_root == NULL) {
		/* XXX - this case is not recommended. */
		gfarm_spool_root = GFARM_SPOOL_ROOT;
	}
}

static enum {
	gfarm_config_not_read,
	gfarm_config_user_read,
	gfarm_config_server_read
} config_read = gfarm_config_not_read;

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
	FILE *config;
	int user_config_errno;
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

	/*
	 * result of gfarm_get_local_homedir() should not be trusted.
	 * (maybe forged)
	 */
	home = gfarm_get_local_homedir();

	rc = malloc(strlen(home) + 1 + sizeof(gfarm_client_rc));
	if (rc == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(rc, "%s/%s", home, gfarm_client_rc);
	gfarm_stringlist_init(&local_user_map_file_list);
	if ((config = fopen(rc, "r")) == NULL) {
		user_config_errno = errno;
	} else {
		user_config_errno = 0;
		/*
		 * The reason why we don't just pass `rc' as the
		 * second argument of gfarm_config_read_file() is
		 * because `rc' may be too long name to generate error
		 * message.
		 */
		e = gfarm_config_read_file(config, "~/" GFARM_CLIENT_RC);
		if (e != NULL) {
			free(rc);
			return (e);
		}
	}
	free(rc);

	if ((config = fopen(gfarm_config_file, "r")) == NULL) {
		if (user_config_errno != 0)
			return ("gfarm.conf: cannot read");
	} else {
		e = gfarm_config_read_file(config, gfarm_config_file);
		if (e != NULL)
			return (e);
	}

	gfarm_config_set_default1();

	return (NULL);
}

/* the following function is for server. */
char *
gfarm_server_config_read(void)
{
	char *e;
	FILE *config;

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

	gfarm_stringlist_init(&local_user_map_file_list);
	if ((config = fopen(gfarm_config_file, "r")) == NULL) {
		return ("gfarm.conf: cannot read");
	}
	e = gfarm_config_read_file(config, gfarm_config_file);
	if (e != NULL)
		return (e);

	gfarm_config_set_default1();

	return (NULL);
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

char *gfarm_debug_command;
char gfarm_debug_pid[GFARM_INT64STRLEN];

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
	static char message[] = "signal 00 caught\n";

	message[7] = sig / 10 + '0';
	message[8] = sig % 10 + '0';
	write(2, message, sizeof(message) - 1);

	pid = gfarm_call_debugger();
	if (pid == -1)
		perror("fork");
	else
		waitpid(pid, &status, 0);
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

static char *
gfarm_redirect_file(int fd, char *file, GFS_File *gf)
{
	int nfd;
	char *e;

	if (file == NULL)
		return (NULL);

	e = gfs_pio_create(file, GFARM_FILE_WRONLY, 0644, gf);
	if (e != NULL)
		return (e);

	e = gfs_pio_set_view_local(*gf, 0);
	if (e != NULL)
		return (e);

	nfd = gfs_pio_fileno(*gf);
	if (nfd == -1)
		return (gfarm_errno_to_error(errno));

	/*
	 * This assumes the file fragment is created in the local
	 * spool.
	 */
	if (dup2(nfd, fd) == -1)
		e = gfarm_errno_to_error(errno);

	/* XXX - apparently violate the layer */
	((struct gfs_file_section_context *)(*gf)->view_context)->fd = fd;
	(*gf)->mode &= ~GFS_FILE_MODE_CALC_DIGEST;

	close(nfd);

	return (e);
}

/*
 * eliminate arguments added by the gfrun command.
 */

static GFS_File gf_stdout, gf_stderr;

char *
gfarm_parse_argv(int *argcp, char ***argvp)
{
	int total_nodes = -1, node_index = -1;
	int argc = *argcp;
	char **argv = *argvp;
	char *argv0 = *argv;
	int call_set_local = 0;
	char *stdout_file = NULL, *stderr_file = NULL, *e;

	--argc;
	++argv;
	while (argc > 0 && argv[0][0] == '-' && argv[0][1] == '-') {
		if (strcmp(&argv[0][2], "gfarm_index") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				node_index = strtol(*argv, NULL, 0);
			call_set_local |= 1;
		}
		else if (strcmp(&argv[0][2], "gfarm_nfrags") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				total_nodes = strtol(*argv, NULL, 0);
			call_set_local |= 2;
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
		else
			break;
		--argc;
		++argv;
	}
	if (call_set_local == 3) {
		e = gfs_pio_set_local(node_index, total_nodes);
		if (e != NULL)
			return (e);

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
		++argc;
		--argv;

		*argcp = argc;
		*argv = argv0;
		*argvp = argv;
	}	

	return (NULL);
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
char *
gfarm_initialize(int *argcp, char ***argvp)
{
	char *e;

	gfarm_error_initialize();

	e = gfarm_set_local_user_for_this_local_account();
	if (e != NULL)
		return (e);
	e = gfarm_config_read();
	if (e != NULL)
		return (e);
	e = gfarm_set_global_user_for_this_local_account();
	if (e != NULL)
		return (e);
 	e = gfarm_metadb_initialize();
	if (e != NULL)
		return (e);

	gfarm_config_set_default2();

	if (argvp != NULL) {
		gfarm_debug_initialize((*argvp)[0]);
		e = gfarm_parse_argv(argcp, argvp);
		if (e != NULL)
			return (e);
	}
	gfarm_initialized = 1;

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

	gfarm_config_set_default2_server();

	return (NULL);
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
char *
gfarm_terminate(void)
{
	char *e;

	if (gf_stdout != NULL) {
		fflush(stdout);
		e = gfs_pio_close(gf_stdout);
		if (e != NULL)
			return (e);
	}
	if (gf_stderr != NULL) {
		fflush(stderr);
		e = gfs_pio_close(gf_stderr);
		if (e != NULL)
			return (e);
	}
	e = gfarm_metadb_terminate();
	if (e != NULL)
		return (e);
	return (NULL);
}

/* the following function is for server. */
char *
gfarm_server_terminate(void)
{
	/* nothing to do (and also may never be called) */
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
