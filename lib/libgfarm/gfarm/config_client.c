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
#include "metadb_server.h" /* XXX FIXME this shouldn't be needed here */
#include "gfs_proto.h"
#include "gfs_client.h"

/*
 * XXX FIXME this shouldn't be necessary here
 * to support multiple metadata server.
 */
struct gfm_connection *gfarm_metadb_server;

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
	free(global_user);
#if 0 /* Unlink Gfarm Version 1, we won't free this */
	gfarm_stringlist_free_deeply(&local_user_map_file_list);
#endif
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
	int lineno, user_config_errno, rc_need_free;;
	static char gfarm_client_rc[] = GFARM_CLIENT_RC;
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
		if (rc == NULL)
			return (GFARM_ERR_NO_MEMORY);
		sprintf(rc, "%s/%s", home, gfarm_client_rc);
		rc_need_free = 1;
	}
	gfarm_init_user_map();
	if ((config = fopen(rc, "r")) == NULL) {
		user_config_errno = errno;
	} else {
		user_config_errno = 0;
		e = gfarm_config_read_file(config, &lineno);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error("%s: %d: %s",
			    rc, lineno, gfarm_error_string(e));
			if (rc_need_free)
				free(rc);
			return (e);
		}
	}
	if (rc_need_free)
		free(rc);

	if ((config = fopen(gfarm_config_file, "r")) == NULL) {
		if (user_config_errno != 0)
			return (GFARM_ERRMSG_CANNOT_OPEN_CONFIG);
	} else {
		e = gfarm_config_read_file(config, &lineno);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error("%s: %d: %s",
			    gfarm_config_file, lineno, gfarm_error_string(e));
			return (e);
		}
	}

	gfarm_config_set_default_ports();
	gfarm_config_set_default_misc();

	return (GFARM_ERR_NO_ERROR);
}

#if 0 /* not yet in gfarm v2 */

/*
 * redirect stdout
 */

static gfarm_error_t
gfarm_redirect_file(int fd, char *file, GFS_File *gf)
{
	gfarm_error_t e;
	int nfd;

	if (file == NULL)
		return (GFARM_ERR_NO_ERROR);

	e = gfs_pio_create(file, GFARM_FILE_WRONLY, 0644, gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfs_pio_set_view_local(*gf, 0);
	if (e != GFARM_ERR_NO_ERROR)
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
int gf_on_demand_replication;

gfarm_error_t
gfarm_parse_argv(int *argcp, char ***argvp)
{
	gfarm_error_t e;
	int total_nodes = -1, node_index = -1;
	int argc = *argcp;
	char **argv = *argvp;
	char *argv0 = *argv;
	int call_set_local = 0;
	char *stdout_file = NULL, *stderr_file = NULL;

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
		else if (strcmp(&argv[0][2], "gfarm_profile") == 0)
			gfs_profile_set();
		else if (strcmp(&argv[0][2], "gfarm_replicate") == 0)
			gf_on_demand_replication = 1;
		else if (strcmp(&argv[0][2], "gfarm_cwd") == 0) {
			--argc;
			++argv;
			if (argc > 0) {
				e = gfs_chdir(*argv);
				if (e != GFARM_ERR_NO_ERROR)
					return (e);
			}
		}
		else
			break;
		--argc;
		++argv;
	}
	if (call_set_local == 3) {
		e = gfs_pio_set_local(node_index, total_nodes);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);

		/* redirect stdout and stderr */
		if (stdout_file != GFARM_ERR_NO_ERROR) {
			e = gfarm_redirect_file(1, stdout_file, &gf_stdout);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		}
		if (stderr_file != GFARM_ERR_NO_ERROR) {
			e = gfarm_redirect_file(2, stderr_file, &gf_stderr);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		}

		++argc;
		--argv;

		*argcp = argc;
		*argv = argv0;
		*argvp = argv;
	}	

	return (GFARM_ERR_NO_ERROR);
}

#endif /* not yet in gfarm v2 */

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


gfarm_pid_t gfarm_client_pid;
gfarm_int32_t gfarm_client_pid_key_type = 1; /* XXX FIXME */
char gfarm_client_pid_key[32];
size_t gfarm_client_pid_key_len = sizeof(gfarm_client_pid_key);

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
#ifdef HAVE_GSI
	int saved_auth_verb;
#endif

	e = gfarm_set_local_user_for_this_local_account();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfarm_config_read();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
#ifdef HAVE_GSI
	/*
	 * Suppress verbose error messages.  The message will be
	 * displayed later in gfarm_auth_request_gsi().
	 */
	saved_auth_verb = gfarm_authentication_verbose;
	gfarm_authentication_verbose = 0;
	(void*)gfarm_gsi_client_initialize();
	gfarm_authentication_verbose = saved_auth_verb;
#endif
	e = gfarm_set_global_user_for_this_local_account();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/*
	 * XXX FIXME this shouldn't be necessary here
	 * to support multiple metadata server
	 */
	e = gfm_client_connection_acquire(gfarm_metadb_server_name,
	    gfarm_metadb_server_port, &gfarm_metadb_server);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "connecting gfmd: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}
	gfarm_metadb_set_server(gfarm_metadb_server);

	if (argvp != NULL) {
#if 0 /* not yet in gfarm v2 */
		if (getenv("DISPLAY") != NULL)
			gfarm_debug_initialize((*argvp)[0]);
		e = gfarm_parse_argv(argcp, argvp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
#endif /* not yet in gfarm v2 */
	}

	gfarm_auth_random(gfarm_client_pid_key, gfarm_client_pid_key_len);
	e = gfm_client_process_alloc(gfarm_metadb_server,
	    gfarm_client_pid_key_type,
	    gfarm_client_pid_key, gfarm_client_pid_key_len, &gfarm_client_pid);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal("failed to allocate gfarm PID: %s",
		    gfarm_error_string(e));
			
#if 0 /* not yet in gfarm v2 */
	gfarm_initialized = 1;
#endif /* not yet in gfarm v2 */

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_client_process_set(struct gfs_connection *gfs_server)
{
	return (gfs_client_process_set(gfs_server,
	    gfarm_client_pid_key_type,
	    gfarm_client_pid_key_len, gfarm_client_pid_key,
	    gfarm_client_pid));
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
#if 0 /* not yet in gfarm v2 */
	gfarm_error_t e;

	gfs_profile(gfs_display_timers());

	if (gf_stdout != NULL) {
		fflush(stdout);
		e = gfs_pio_close(gf_stdout);
		gf_stdout = NULL;
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if (gf_stderr != NULL) {
		fflush(stderr);
		e = gfs_pio_close(gf_stderr);
		gf_stderr = NULL;
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	e = gfarm_metadb_terminate();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
#endif /* not yet in gfarm v2 */

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
