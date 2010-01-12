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
#include "gfutil.h"

#include "liberror.h"
#include "hostspec.h"
#include "param.h"
#include "sockopt.h"
#include "host.h" /* XXX address_use is disabled for now */
#include "auth.h"
#include "gfpath.h"
#include "config.h"
#include "gfm_proto.h" /* GFMD_DEFAULT_PORT */
#include "gfs_proto.h" /* GFSD_DEFAULT_PORT */
#include "gfs_profile.h"

char *gfarm_config_file = GFARM_CONFIG;

void
gfarm_config_set_filename(char *filename)
{
	gfarm_config_file = filename;
}

/* XXX move actual function definition here */
static gfarm_error_t gfarm_strtoken(char **, char **);

/*
 * GFarm username handling
 */

static gfarm_stringlist local_user_map_file_list;

/* the return value of the following function should be free(3)ed */
static gfarm_error_t
map_user(char *from, char **to_p,
	char *(*mapping)(char *, char *, char *),
	gfarm_error_t error_redefined)
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
			gflog_error(GFARM_MSG_1000009,
			    mapfile, strerror(errno));
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
		if (*to_p != NULL)	 
			free(*to_p);
		gflog_error(GFARM_MSG_1000010,
		    "%s line %d: %s", mapfile, lineno,
		    gfarm_error_string(e));
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

/*
 * XXX FIXME This function should not be used by gfarm clients,
 * because this doesn't cope with multiple metadata server.
 * Thus, this function only should be called from gfmd and gfsd,
 * and should move to config_server.c.
 */
/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_global_to_local_username(char *global_user, char **local_user_p)
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

/*
 * XXX FIXME This function should not be used by gfarm clients,
 * because this doesn't cope with multiple metadata server.
 * Thus, this function only should be called from gfmd and gfsd,
 * and should move to config_server.c.
 */
/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_local_to_global_username(char *local_user, char **global_user_p)
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
	struct passwd pwbuf, *pwd;
	char *buf;
	static int bufsize = 0;
#	define BUFSIZE_MAX 2048

	if (bufsize == 0) {
		bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
		if (bufsize == -1)
			bufsize = BUFSIZE_MAX;
	}
	buf = malloc(bufsize);
	if (buf == NULL) {
		error = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1000011, "gfarm_set_local_user: %s",
			gfarm_error_string(error));
		return (error);
	}
	if (getpwuid_r(geteuid(), &pwbuf, buf, bufsize, &pwd) != 0) {
		gflog_error(GFARM_MSG_1000012, "local account doesn't exist");
		error = GFARM_ERR_NO_SUCH_OBJECT;
		goto error;
	}
	error = gfarm_set_local_username(pwd->pw_name);
	if (error == GFARM_ERR_NO_ERROR)
		error = gfarm_set_local_homedir(pwd->pw_dir);
 error:
	free(buf);
	return (error);
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
char *gfarm_spool_root = NULL;

/* GFM dependent */
char *gfarm_metadb_server_name = NULL;
static char *gfarm_metadb_server_portname = NULL;
int gfarm_metadb_server_port = GFMD_DEFAULT_PORT;
enum gfarm_backend_db_type gfarm_backend_db_type =
	GFARM_BACKEND_DB_TYPE_UNKNOWN;

char *gfarm_metadb_admin_user = NULL;
char *gfarm_metadb_admin_user_gsi_dn = NULL;

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

/* miscellaneous */
#define GFARM_ATTR_CACHE_LIMIT_DEFAULT		40000 /* 40,000 entries */
#define GFARM_ATTR_CACHE_TIMEOUT_DEFAULT	1000 /* 1,000 milli second */
#define GFARM_SCHEDULE_CACHE_TIMEOUT_DEFAULT 600 /* 10 minutes */
#define GFARM_SCHEDULE_WRITE_LOCAL_PRIORITY_DEFAULT 1 /* enable */
#define GFARM_MINIMUM_FREE_DISK_SPACE_DEFAULT	(128 * 1024 * 1024) /* 128MB */
#define GFARM_GFSD_CONNECTION_CACHE_DEFAULT 16 /* 16 free connections */
#define GFARM_GFMD_CONNECTION_CACHE_DEFAULT  8 /*  8 free connections */
#define GFARM_RECORD_ATIME_DEFAULT 1 /* enable */
#define MISC_DEFAULT -1
int gfarm_log_level = MISC_DEFAULT;
int gfarm_attr_cache_limit = MISC_DEFAULT;
int gfarm_attr_cache_timeout = MISC_DEFAULT;
int gfarm_schedule_cache_timeout = MISC_DEFAULT;
static char *schedule_write_target_domain = NULL;
static int schedule_write_local_priority = MISC_DEFAULT;
gfarm_int64_t gfarm_minimum_free_disk_space = MISC_DEFAULT;
int gfarm_gfsd_connection_cache = MISC_DEFAULT;
int gfarm_gfmd_connection_cache = MISC_DEFAULT;
int gfarm_metadb_stack_size = MISC_DEFAULT;
int gfarm_metadb_thread_pool_size = MISC_DEFAULT;
int gfarm_metadb_job_queue_length = MISC_DEFAULT;
int gfarm_metadb_heartbeat_interval = MISC_DEFAULT;
int gfarm_metadb_dbq_size = MISC_DEFAULT;
int gfarm_record_atime = MISC_DEFAULT;

void
gfarm_config_clear(void)
{
	static char **vars[] = {
		&gfarm_spool_server_listen_address,
		&gfarm_spool_root,
		&gfarm_metadb_server_name,
		&gfarm_metadb_server_portname,
		&gfarm_metadb_admin_user,
		&gfarm_metadb_admin_user_gsi_dn,
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
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(vars); i++) {
		if (*vars[i] != NULL) {
			free(*vars[i]);
			*vars[i] = NULL;
		}
	}

#if 0 /* XXX */
	config_read = gfarm_config_not_read;
#endif
}

static gfarm_error_t
set_backend_db_type(enum gfarm_backend_db_type set)
{
	if (gfarm_backend_db_type == set)
		return (GFARM_ERR_NO_ERROR);
	switch (gfarm_backend_db_type) {
	case GFARM_BACKEND_DB_TYPE_UNKNOWN:
		gfarm_backend_db_type = set;
		return (GFARM_ERR_NO_ERROR);
	case GFARM_BACKEND_DB_TYPE_LDAP:
		return (GFARM_ERRMSG_BACKEND_ALREADY_LDAP);
	case GFARM_BACKEND_DB_TYPE_POSTGRESQL:
		return (GFARM_ERRMSG_BACKEND_ALREADY_POSTGRESQL);
	case GFARM_BACKEND_DB_TYPE_LOCALFS:
		return (GFARM_ERRMSG_BACKEND_ALREADY_LOCALFS);
	default:
		assert(0);
		return (GFARM_ERR_UNKNOWN); /* workaround compiler warning */
	}
}

static gfarm_error_t
set_backend_db_type_ldap(void)
{
	return (set_backend_db_type(GFARM_BACKEND_DB_TYPE_LDAP));
}

static gfarm_error_t
set_backend_db_type_postgresql(void)
{
	return (set_backend_db_type(GFARM_BACKEND_DB_TYPE_POSTGRESQL));
}

static gfarm_error_t
set_backend_db_type_localfs(void)
{
	return (set_backend_db_type(GFARM_BACKEND_DB_TYPE_LOCALFS));
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

gfarm_off_t
gfarm_get_minimum_free_disk_space(void)
{
	return (gfarm_minimum_free_disk_space);
}

void
gfarm_set_record_atime(int boolean)
{
	gfarm_record_atime = boolean;
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
		return (GFARM_ERRMSG_MISSING_1ST_AUTH_COMMAND_ARGUMENT);

	e = gfarm_strtoken(&p, &auth);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (auth == NULL)
		return (GFARM_ERRMSG_MISSING_2ND_AUTH_METHOD_ARGUMENT);
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
		return (GFARM_ERRMSG_MISSING_3RD_HOST_SPEC_ARGUMENT);
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

#if 0 /* XXX NOTYET */
static gfarm_error_t
parse_client_architecture(char *p, char **op)
{
	gfarm_error_t e;
	char *architecture, *host, *junk;
	struct gfarm_hostspec *hostspecp;

	e = gfarm_strtoken(&p, &architecture);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (architecture == NULL)
		return (GFARM_ERRMSG_MISSING_1ST_ARCHITECTURE_ARGUMENT);
	e = gfarm_strtoken(&p, &host);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (host == NULL)
		return (GFARM_ERRMSG_MISSING_2ND_HOST_SPEC_ARGUMENT);
	e = gfarm_strtoken(&p, &junk);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (junk != NULL)
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "2nd(host-spec) argument";
		return (e);
	}
	e = gfarm_set_client_architecture(architecture, hostspecp);
	if (e != GFARM_ERR_NO_ERROR)
		gfarm_hostspec_free(hostspecp);
	return (e);
}
#endif /* XXX NOTYET */

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

	*rv = s;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_var(char *p, char **rv)
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (*rv != NULL) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	s = strdup(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*rv = s;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_misc_int(char *p, int *vp)
{
	gfarm_error_t e;
	char *ep, *s;
	int v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (*vp != MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	errno = 0;
	v = strtol(s, &ep, 10);
	if (errno != 0)
		return (gfarm_errno_to_error(errno));
	if (ep == s)
		return (GFARM_ERRMSG_INTEGER_EXPECTED);
	if (*ep != '\0')
		return (GFARM_ERRMSG_INVALID_CHARACTER);
	*vp = v;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_misc_offset(char *p, gfarm_off_t *vp)
{
	gfarm_error_t e;
	char *ep, *s;
	gfarm_off_t v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (*vp != MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	errno = 0;
	v = gfarm_strtoi64(s, &ep);
	if (errno != 0)
		return (gfarm_errno_to_error(errno));
	if (ep == s)
		return (GFARM_ERRMSG_INTEGER_EXPECTED);
	if (*ep != '\0') {
		switch (*ep) {
		case 'k': case 'K': ep++; v *= 1024; break;
		case 'm': case 'M': ep++; v *= 1024 * 1024; break;
		case 'g': case 'G': ep++; v *= 1024 * 1024 * 1024; break;
		case 't': case 'T': ep++; v *=1024*1024; v *=1024*1024; break;
		}
		if (*ep != '\0')
			return (GFARM_ERRMSG_INVALID_CHARACTER);
	}
	*vp = v;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_misc_enabled(char *p, int *vp)
{
	gfarm_error_t e;
	char *s;
	int v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (*vp != MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	if (strcmp(s, "enable") == 0)
		v = 1;
	else if (strcmp(s, "disable") == 0)
		v = 0;
	else
		return (GFARM_ERRMSG_ENABLED_OR_DISABLED_EXPECTED);
	*vp = v;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_cred_config(char *p, char *service,
	gfarm_error_t (*set)(char *, char *))
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	return ((*set)(service, s));
}

static gfarm_error_t
parse_log_level(char *p, int *vp)
{
	gfarm_error_t e;
	char *s;
	int v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (*vp != MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	v = gflog_syslog_name_to_priority(s);
	if (v == -1)
		return (GFARM_ERRMSG_INVALID_SYSLOG_PRIORITY_LEVEL);
	*vp = v;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_one_line(char *s, char *p, char **op)
{
	gfarm_error_t e;
	char *o;

	if (strcmp(s, o = "spool") == 0) {
		e = parse_set_var(p, &gfarm_spool_root);
	} else if (strcmp(s, o = "spool_server_listen_address") == 0) {
		e = parse_set_var(p, &gfarm_spool_server_listen_address);
	} else if (strcmp(s, o = "spool_server_cred_type") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_type_set_by_string);
	} else if (strcmp(s, o = "spool_server_cred_service") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_service_set);
	} else if (strcmp(s, o = "spool_server_cred_name") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_name_set);

	} else if (strcmp(s, o = "metadb_server_host") == 0) {
		e = parse_set_var(p, &gfarm_metadb_server_name);
	} else if (strcmp(s, o = "metadb_server_port") == 0) {
		e = parse_set_var(p, &gfarm_metadb_server_portname);
	} else if (strcmp(s, o = "admin_user") == 0) {
		e = parse_set_var(p, &gfarm_metadb_admin_user);
	} else if (strcmp(s, o = "admin_user_gsi_dn") == 0) {
		e = parse_set_var(p, &gfarm_metadb_admin_user_gsi_dn);
	} else if (strcmp(s, o = "metadb_server_cred_type") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_type_set_by_string);
	} else if (strcmp(s, o = "metadb_server_cred_service") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_service_set);
	} else if (strcmp(s, o = "metadb_server_cred_name") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_name_set);

	} else if (strcmp(s, o = "ldap_server_host") == 0) {
		e = parse_set_var(p, &gfarm_ldap_server_name);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_server_port") == 0) {
		e = parse_set_var(p, &gfarm_ldap_server_port);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_base_dn") == 0) {
		e = parse_set_var(p, &gfarm_ldap_base_dn);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_bind_dn") == 0) {
		e = parse_set_var(p, &gfarm_ldap_bind_dn);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_bind_password") == 0) {
		e = parse_set_var(p, &gfarm_ldap_bind_password);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_tls") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_tls_cipher_suite") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_cipher_suite);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_tls_certificate_key_file") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_certificate_key_file);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_tls_certificate_file") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_certificate_file);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();

	} else if (strcmp(s, o = "postgresql_server_host") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_server_name);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_server_port") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_server_port);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_dbname") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_dbname);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_user") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_user);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_password") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_password);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_conninfo") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_conninfo);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();

	} else if (strcmp(s, o = "localfs_datadir") == 0) {
		e = parse_set_var(p, &gfarm_localfs_datadir);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_localfs();

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
#if 0 /* XXX NOTYET */
	} else if (strcmp(s, o = "client_architecture") == 0) {
		e = parse_client_architecture(p, &o);
#endif

	} else if (strcmp(s, o = "log_level") == 0) {
		e = parse_log_level(p, &gfarm_log_level);
	} else if (strcmp(s, o = "attr_cache_limit") == 0) {
		e = parse_set_misc_int(p, &gfarm_attr_cache_limit);
	} else if (strcmp(s, o = "attr_cache_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_attr_cache_timeout);
	} else if (strcmp(s, o = "schedule_cache_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_schedule_cache_timeout);
	} else if (strcmp(s, o = "write_local_priority") == 0) {
		e = parse_set_misc_enabled(p, &schedule_write_local_priority);
	} else if (strcmp(s, o = "write_target_domain") == 0) {
		e = parse_set_var(p, &schedule_write_target_domain);
	} else if (strcmp(s, o = "minimum_free_disk_space") == 0) {
		e = parse_set_misc_offset(p, &gfarm_minimum_free_disk_space);
	} else if (strcmp(s, o = "gfsd_connection_cache") == 0) {
		e = parse_set_misc_int(p, &gfarm_gfsd_connection_cache);
	} else if (strcmp(s, o = "gfmd_connection_cache") == 0) {
		e = parse_set_misc_int(p, &gfarm_gfmd_connection_cache);
	} else if (strcmp(s, o = "metadb_server_stack_size") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_stack_size);
	} else if (strcmp(s, o = "metadb_server_thread_pool_size") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_thread_pool_size);
	} else if (strcmp(s, o = "metadb_server_job_queue_length") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_job_queue_length);
	} else if (strcmp(s, o = "metadb_server_heartbeat_interval") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_heartbeat_interval);
	} else if (strcmp(s, o = "metadb_server_dbq_size") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_dbq_size);
	} else if (strcmp(s, o = "record_atime") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_record_atime);

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
gfarm_free_user_map(void)
{
	gfarm_stringlist_free_deeply(&local_user_map_file_list);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_config_read_file(FILE *config, int *lineno_p)
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
	if (gfarm_metadb_server_name == NULL)
		gflog_fatal(GFARM_MSG_1000013,
		    "metadb_serverhost isn't specified in "
		    GFARM_CONFIG " file");

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

void
gfarm_config_set_default_misc(void)
{
	if (gfarm_log_level == MISC_DEFAULT)
		gfarm_log_level = GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG;
	gflog_set_priority_level(gfarm_log_level);

	if (gfarm_attr_cache_limit == MISC_DEFAULT)
		gfarm_attr_cache_limit = GFARM_ATTR_CACHE_LIMIT_DEFAULT;
	if (gfarm_attr_cache_timeout == MISC_DEFAULT)
		gfarm_attr_cache_timeout = GFARM_ATTR_CACHE_TIMEOUT_DEFAULT;
	if (gfarm_schedule_cache_timeout == MISC_DEFAULT)
		gfarm_schedule_cache_timeout =
		    GFARM_SCHEDULE_CACHE_TIMEOUT_DEFAULT;
	if (schedule_write_local_priority == MISC_DEFAULT)
		schedule_write_local_priority =
		    GFARM_SCHEDULE_WRITE_LOCAL_PRIORITY_DEFAULT;
	if (gfarm_minimum_free_disk_space == MISC_DEFAULT)
		gfarm_minimum_free_disk_space =
		    GFARM_MINIMUM_FREE_DISK_SPACE_DEFAULT;
	if (gfarm_gfsd_connection_cache == MISC_DEFAULT)
		gfarm_gfsd_connection_cache =
		    GFARM_GFSD_CONNECTION_CACHE_DEFAULT;
	if (gfarm_gfmd_connection_cache == MISC_DEFAULT)
		gfarm_gfmd_connection_cache =
		    GFARM_GFMD_CONNECTION_CACHE_DEFAULT;
	if (gfarm_metadb_stack_size == MISC_DEFAULT)
		gfarm_metadb_stack_size = GFARM_METADB_STACK_SIZE_DEFAULT;
	if (gfarm_metadb_thread_pool_size == MISC_DEFAULT)
		gfarm_metadb_thread_pool_size =
		    GFARM_METADB_THREAD_POOL_SIZE_DEFAULT;
	if (gfarm_metadb_job_queue_length == MISC_DEFAULT)
		gfarm_metadb_job_queue_length =
		    GFARM_METADB_JOB_QUEUE_LENGTH_DEFAULT;
	if (gfarm_metadb_heartbeat_interval == MISC_DEFAULT)
		gfarm_metadb_heartbeat_interval =
		    GFARM_METADB_HEARTBEAT_INTERVAL_DEFAULT;
	if (gfarm_metadb_dbq_size == MISC_DEFAULT)
		gfarm_metadb_dbq_size = GFARM_METADB_DBQ_SIZE_DEFAULT;
	if (gfarm_record_atime == MISC_DEFAULT)
		gfarm_record_atime = GFARM_RECORD_ATIME_DEFAULT;
}

void
gfs_display_timers(void)
{
	gfs_pio_display_timers();
#if 0
	gfs_pio_section_display_timers();
#endif
	gfs_stat_display_timers();
	gfs_unlink_display_timers();
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
