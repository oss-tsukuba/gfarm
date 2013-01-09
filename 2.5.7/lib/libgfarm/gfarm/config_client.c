#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"

#include "liberror.h"
#include "gfs_profile.h"
#include "host.h"
#include "auth.h"
#include "gfpath.h"
#define GFARM_USE_STDIO
#include "config.h"
#include "gfm_client.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "lookup.h"
#include "filesystem.h"
#include "metadb_server.h"

static gfarm_error_t
gfarm_config_set_default_metadb_server(void)
{
	gfarm_error_t e;
	struct gfarm_metadb_server *m;
	struct gfarm_metadb_server *ms[1];
	struct gfarm_filesystem *fs;
	char *host;

	if (gfarm_filesystem_get(
	    gfarm_metadb_server_name, gfarm_metadb_server_port) != NULL)
		return (GFARM_ERR_NO_ERROR);

	fs = gfarm_filesystem_get_default();
	if ((host = strdup(gfarm_metadb_server_name)) == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1003433,
		    "%s", gfarm_error_string(e));
		return (e);
	}

	if ((e = gfarm_metadb_server_new(&m, host, gfarm_metadb_server_port))
	    != GFARM_ERR_NO_ERROR) {
		free(host);
		gflog_debug(GFARM_MSG_1002556,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	gfarm_metadb_server_set_is_master(m, 1);
	ms[0] = m;
	if ((e = gfarm_filesystem_set_metadb_server_list(fs, ms, 1))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002557,
		    "%s", gfarm_error_string(e));
	return (e);
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
gfarm_error_t
gfarm_config_read(void)
{
	gfarm_error_t e;
	char *home;
	FILE *config;
	int lineno, user_config_errno, rc_need_free;
	static const char gfarm_client_rc[] = GFARM_CLIENT_RC;
	char *rc;

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
		if (rc == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1000980,
			    "gfarm_config_read: %s", gfarm_error_string(e));
			return (e);
		}
		sprintf(rc, "%s/%s", home, gfarm_client_rc);
		rc_need_free = 1;
	}
	gfarm_init_config();
	if ((config = fopen(rc, "r")) == NULL) {
		user_config_errno = errno;
	} else {
		user_config_errno = 0;
		e = gfarm_config_read_file(config, &lineno);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1000015, "%s: line %d: %s",
			    rc, lineno, gfarm_error_string(e));
			if (rc_need_free)
				free(rc);
			return (e);
		}
	}
	if (rc_need_free)
		free(rc);

	if ((config = fopen(gfarm_config_file, "r")) == NULL) {
		if (user_config_errno != 0) {
			e = GFARM_ERRMSG_CANNOT_OPEN_CONFIG;
			gflog_debug(GFARM_MSG_1000981,
			    "%s: %s", gfarm_config_file, gfarm_error_string(e));
			return (e);
		}
	} else {
		e = gfarm_config_read_file(config, &lineno);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1000016, "%s: line %d: %s",
			    gfarm_config_file, lineno, gfarm_error_string(e));
			return (e);
		}
	}

	gfarm_config_set_default_ports();
	gfarm_config_set_default_misc();
	gfarm_config_set_default_metadb_server();

	return (GFARM_ERR_NO_ERROR);
}

int gf_on_demand_replication;

static void
gfarm_parse_env_client(void)
{
	char *env;

	if ((env = getenv("GFARM_FLAGS")) != NULL) {
		for (; *env; env++) {
			switch (*env) {
			case 'r': gf_on_demand_replication = 1; break;
			}
		}
	}
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
gfarm_error_t
gfarm_initialize(int *argcp, char ***argvp)
{
	gfarm_error_t e;

	gflog_initialize();
	if (argvp)
		gfarm_config_set_argv0(**argvp);

	e = gfarm_set_local_user_for_this_local_account();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000982,
		    "gfarm_set_local_user_for_this_local_account() failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	e = gfarm_config_read();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000983,
		    "gfarm_config_read() failed: %s", gfarm_error_string(e));
		return (e);
	}

	gfarm_setup_debug_command();
	gfarm_parse_env_client();

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_client_process_set_or_reset(struct gfs_connection *gfs_server,
	struct gfm_connection *gfm_server,
	gfarm_error_t (*process_set_or_reset_op)(struct gfs_connection *,
	    gfarm_int32_t, const char *, size_t, gfarm_pid_t))
{
	gfarm_int32_t key_type;
	const char *key;
	size_t key_size;
	gfarm_pid_t pid;
	gfarm_error_t e = gfm_client_process_get(gfm_server,
	    &key_type, &key, &key_size, &pid);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000986,
			"gfm_client_process_get() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	return (process_set_or_reset_op(gfs_server, key_type, key,
	    key_size, pid));
}

gfarm_error_t
gfarm_client_process_set(struct gfs_connection *gfs_server,
	struct gfm_connection *gfm_server)
{
	return (gfarm_client_process_set_or_reset(gfs_server, gfm_server,
	    gfs_client_process_set));
}

gfarm_error_t
gfarm_client_process_reset(struct gfs_connection *gfs_server,
	struct gfm_connection *gfm_server)
{
	return (gfarm_client_process_set_or_reset(gfs_server, gfm_server,
	    gfs_client_process_reset));
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
gfarm_error_t
gfarm_terminate(void)
{
	gfs_profile(gfs_display_timers());
	gfarm_free_config();
	gfs_client_terminate();
	gfm_client_terminate();
	gflog_terminate();

	return (GFARM_ERR_NO_ERROR);
}

#ifdef CONFIG_TEST
main()
{
	gfarm_error_t e;

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
