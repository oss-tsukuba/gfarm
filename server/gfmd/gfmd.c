/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "liberror.h"
#include "gfutil.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "sockopt.h"
#include "config.h"
#include "auth.h"
#include "gfm_proto.h"
#include "gfj_client.h"

#include "subr.h"
#include "host.h"
#include "user.h"
#include "group.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "fs.h"
#include "job.h"

#ifdef SOMAXCONN
#define LISTEN_BACKLOG	SOMAXCONN
#else
#define LISTEN_BACKLOG	5
#endif

/* limit maximum connections, when system limit is very high */
#ifndef GFMD_CONNECTION_LIMIT
#define GFMD_CONNECTION_LIMIT	65536
#endif

char *program_name = "gfmd";

int debug_mode = 0;

int
protocol_service(struct peer *peer, int from_client)
{
	gfarm_error_t e;
	int eof;
	gfarm_int32_t request;

	e = gfp_xdr_recv(peer_get_conn(peer), 0, &eof, "i", &request);
	if (eof)
		return (0); /* finish on eof */
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning("receiving request number",
		    gfarm_error_string(e));
		return (0); /* finish on error */
	}
	switch (request) {
	case GFM_PROTO_HOST_INFO_GET_ALL:
		e = gfm_server_host_info_get_all(peer, from_client);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE:
		e = gfm_server_host_info_get_by_architecture(peer,
		    from_client);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_NAMES:
		e = gfm_server_host_info_get_by_names(peer, from_client);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_NAMEALISES:
		e = gfm_server_host_info_get_by_namealises(peer, from_client);
		break;
	case GFM_PROTO_HOST_INFO_SET:
		e = gfm_server_host_info_set(peer, from_client);
		break;
	case GFM_PROTO_HOST_INFO_MODIFY:
		e = gfm_server_host_info_modify(peer, from_client);
		break;
	case GFM_PROTO_HOST_INFO_REMOVE:
		e = gfm_server_host_info_remove(peer, from_client);
		break;
	case GFM_PROTO_USER_INFO_GET_ALL:
		e = gfm_server_user_info_get_all(peer, from_client);
		break;
	case GFM_PROTO_USER_INFO_GET_BY_NAMES:
		e = gfm_server_user_info_get_by_names(peer, from_client);
		break;
	case GFM_PROTO_USER_INFO_SET:
		e = gfm_server_user_info_set(peer, from_client);
		break;
	case GFM_PROTO_USER_INFO_MODIFY:
		e = gfm_server_user_info_modify(peer, from_client);
		break;
	case GFM_PROTO_USER_INFO_REMOVE:
		e = gfm_server_user_info_remove(peer, from_client);
		break;
	case GFM_PROTO_GROUP_INFO_GET_ALL:
		e = gfm_server_group_info_get_all(peer, from_client);
		break;
	case GFM_PROTO_GROUP_INFO_GET_BY_NAMES:
		e = gfm_server_group_info_get_by_names(peer, from_client);
		break;
	case GFM_PROTO_GROUP_INFO_SET:
		e = gfm_server_group_info_set(peer, from_client);
		break;
	case GFM_PROTO_GROUP_INFO_MODIFY:
		e = gfm_server_group_info_modify(peer, from_client);
		break;
	case GFM_PROTO_GROUP_INFO_REMOVE:
		e = gfm_server_group_info_remove(peer, from_client);
		break;
	case GFM_PROTO_GROUP_INFO_ADD_USERS:
		e = gfm_server_group_info_add_users(peer, from_client);
		break;
	case GFM_PROTO_GROUP_INFO_REMOVE_USERS:
		e = gfm_server_group_info_remove_users(peer, from_client);
		break;
	case GFM_PROTO_GROUP_NAMES_GET_BY_USERS:
		e = gfm_server_group_names_get_by_users(peer, from_client);
		break;
	case GFM_PROTO_CHMOD:
		e = gfm_server_chmod(peer, from_client);
		break;
	case GFM_PROTO_CHOWN:
		e = gfm_server_chown(peer, from_client);
		break;
	case GFM_PROTO_STAT:
		e = gfm_server_stat(peer, from_client);
		break;
	case GFM_PROTO_RENAME:
		e = gfm_server_rename(peer, from_client);
		break;
	case GFM_PROTO_REMOVE:
		e = gfm_server_remove(peer, from_client);
		break;
	case GFM_PROTO_MKDIR:
		e = gfm_server_mkdir(peer, from_client);
		break;
	case GFM_PROTO_RMDIR:
		e = gfm_server_rmdir(peer, from_client);
		break;
	case GFM_PROTO_CHDIR:
		e = gfm_server_chdir(peer, from_client);
		break;
	case GFM_PROTO_GETCWD:
		e = gfm_server_getcwd(peer, from_client);
		break;
	case GFM_PROTO_ABSPATH:
		e = gfm_server_abspath(peer, from_client);
		break;
	case GFM_PROTO_REALPATH:
		e = gfm_server_realpath(peer, from_client);
		break;
	case GFM_PROTO_GETDIRENTS:
		e = gfm_server_getdirents(peer, from_client);
		break;
	case GFM_PROTO_GLOB:
		e = gfm_server_glob(peer, from_client);
		break;
	case GFM_PROTO_REPLICA_LIST_BY_NAME:
		e = gfm_server_replica_list_by_name(peer, from_client);
		break;
	case GFM_PROTO_REPLICA_LIST_BY_HOST:
		e = gfm_server_replica_list_by_host(peer, from_client);
		break;
	case GFM_PROTO_REPLICA_REMOVE_BY_HOST:
		e = gfm_server_replica_remove_by_host(peer, from_client);
		break;
	case GFM_PROTO_MOUNT:
		e = gfm_server_mount(peer, from_client);
		break;
	case GFM_PROTO_MOUNT_LIST:
		e = gfm_server_mount_list(peer, from_client);
		break;
	case GFM_PROTO_OPEN:
		e = gfm_server_open(peer, from_client);
		break;
	case GFM_PROTO_CLOSE_READ:
		e = gfm_server_close_read(peer, from_client);
		break;
	case GFM_PROTO_CLOSE_WRITE:
		e = gfm_server_close_write(peer, from_client);
		break;
	case GFM_PROTO_FSTAT:
		e = gfm_server_fstat(peer, from_client);
		break;
	case GFM_PROTO_FUTIMES:
		e = gfm_server_futimes(peer, from_client);
		break;
	case GFM_PROTO_LOCK:
		e = gfm_server_lock(peer, from_client);
		break;
	case GFM_PROTO_TRYLOCK:
		e = gfm_server_trylock(peer, from_client);
		break;
	case GFM_PROTO_UNLOCK:
		e = gfm_server_unlock(peer, from_client);
		break;
	case GFM_PROTO_LOCK_INFO:
		e = gfm_server_lock_info(peer, from_client);
		break;
	case GFM_PROTO_REPLICA_ADD:
		e = gfm_server_replica_add(peer, from_client);
		break;
	case GFM_PROTO_REPLICA_REMOVE:
		e = gfm_server_replica_remove(peer, from_client);
		break;
	case GFM_PROTO_PIO_OPEN:
		e = gfm_server_pio_open(peer, from_client);
		break;
	case GFM_PROTO_PIO_SET_PATHS:
		e = gfm_server_pio_set_paths(peer, from_client);
		break;
	case GFM_PROTO_PIO_CLOSE:
		e = gfm_server_pio_close(peer, from_client);
		break;
	case GFM_PROTO_PIO_VISIT:
		e = gfm_server_pio_visit(peer, from_client);
		break;
	case GFM_PROTO_SCHEDULE:
		e = gfm_server_schedule(peer, from_client);
		break;
	case GFM_PROTO_PROCESS_ALLOC:
		e = gfm_server_process_alloc(peer, from_client);
		break;
	case GFM_PROTO_PROCESS_FREE:
		e = gfm_server_process_free(peer, from_client);
		break;
	case GFM_PROTO_PROCESS_SET:
		e = gfm_server_process_set(peer, from_client);
		break;
	case GFJ_PROTO_LOCK_REGISTER:
		e = gfj_server_lock_register(peer, from_client); break;
	case GFJ_PROTO_UNLOCK_REGISTER:
		e = gfj_server_unlock_register(peer, from_client); break;
	case GFJ_PROTO_REGISTER:
		e = gfj_server_register(peer, from_client);
		break;
	case GFJ_PROTO_UNREGISTER:
		e = gfj_server_unregister(peer, from_client);
		break;
	case GFJ_PROTO_REGISTER_NODE:
		e = gfj_server_register_node(peer, from_client); break;
	case GFJ_PROTO_LIST:
		e = gfj_server_list(peer, from_client); break;
	case GFJ_PROTO_INFO:
		e = gfj_server_info(peer, from_client); break;
	case GFJ_PROTO_HOSTINFO:
		e = gfj_server_hostinfo(peer, from_client); break;
	default:
		{
			char buffer[GFARM_INT32STRLEN];

			sprintf(buffer, "%d", request);
			gflog_warning("unknown request", buffer);
		}
		e = GFARM_ERR_PROTOCOL;
	}
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfp_xdr_flush(peer_get_conn(peer));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_warning("protocol flush", gfarm_error_string(e));
	}

	/* continue unless protocol error happens */
	return (e == GFARM_ERR_NO_ERROR); 
}

/* this routine is called from gfarm_authorize() */
/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_global_to_local_username(char *global_user, char **local_user_p)
{
	if (user_lookup(global_user) == NULL)
		return (GFARM_ERR_AUTHENTICATION);
	return (gfarm_username_map_global_to_local(global_user, local_user_p));
}

/* this routine is called from gfarm_authorize() */
/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_local_to_global_username(char *local_user, char **global_user_p)
{
	gfarm_error_t e = gfarm_username_map_local_to_global(local_user,
	    global_user_p);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (user_lookup(*global_user_p) == NULL)
		return (GFARM_ERR_AUTHENTICATION);
	return (GFARM_ERR_NO_ERROR);
}

void *
protocol_main(void *arg)
{
	gfarm_error_t e;
	struct peer *peer = arg;
	enum gfarm_auth_id_type id_type;
	char *username, *hostname;
	enum gfarm_auth_method auth_method;

	e = gfarm_authorize(peer_get_conn(peer), 0,
	    &id_type, &username, &hostname, &auth_method);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning("authorize", gfarm_error_string(e));
	} else {
		peer_authorized(peer,
		    id_type, username, hostname, auth_method);
		if (id_type == GFARM_AUTH_ID_TYPE_USER) {
			while (protocol_service(peer, 1))
				;
		} else {
			while (protocol_service(peer, 0))
				;
		}
	}
	peer_free(peer);
	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

void
main_loop(int accepting_socket)
{
	gfarm_error_t e;
	int client_socket;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size;
	struct peer *peer;

	for (;;) {
		client_addr_size = sizeof(client_addr);
		client_socket = accept(accepting_socket,
		   (struct sockaddr *)&client_addr, &client_addr_size);
		if (client_socket < 0) {
			if (errno != EINTR)
				gflog_warning_errno("accept");
		} else if ((e = peer_alloc(client_socket, &peer)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_warning("peer_alloc", gfarm_error_string(e));
			close(client_socket);
		} else if ((e = peer_schedule(peer, protocol_main)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_warning("peer_schedule: authorize",
			    gfarm_error_string(e));
			peer_free(peer);
		}
	}
}

int
open_accepting_socket(int port)
{
	gfarm_error_t e;
	struct sockaddr_in self_addr;
	socklen_t self_addr_size;
	int sock, sockopt;

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sin_family = AF_INET;
	self_addr.sin_addr.s_addr = INADDR_ANY;
	self_addr.sin_port = htons(port);
	self_addr_size = sizeof(self_addr);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		gflog_fatal_errno("accepting socket");
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1)
		gflog_warning_errno("SO_REUSEADDR");
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0)
		gflog_fatal_errno("bind accepting socket");
	e = gfarm_sockopt_apply_listener(sock);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning("setsockopt", gfarm_error_string(e));
	if (listen(sock, LISTEN_BACKLOG) < 0)
		gflog_fatal_errno("listen");
	return (sock);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-P <pid-file>\n");
	fprintf(stderr, "\t-f <gfarm-configuration-file>\n");
	fprintf(stderr, "\t-p <port>\n");
	fprintf(stderr, "\t-s <syslog-facility>\n");
	fprintf(stderr, "\t-v>\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	gfarm_error_t e;
	char *config_file = NULL, *port_number = NULL, *pid_file = NULL;
	FILE *pid_fp = NULL;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	int ch, sock, table_size;

	if (argc >= 1)
		program_name = basename(argv[0]);
	gflog_set_identifier(program_name);

	while ((ch = getopt(argc, argv, "P:df:p:s:v")) != -1) {
		switch (ch) {
		case 'P':
			pid_file = optarg;
			break;
		case 'd':
			debug_mode = 1;
			break;
		case 'f':
			config_file = optarg;
			break;
		case 'p':
			port_number = optarg;
			break;
		case 's':
			syslog_facility =
			    gflog_syslog_name_to_facility(optarg);
			if (syslog_facility == -1)
				gflog_fatal(optarg, "unknown syslog facility");
			break;
		case 'v':
			gfarm_authentication_verbose = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (config_file != NULL)
		gfarm_config_set_filename(config_file);
	e = gfarm_server_initialize();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_server_initialize: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}
	if (port_number != NULL)
		gfarm_metadb_server_port = strtol(port_number, NULL, 0);
	sock = open_accepting_socket(gfarm_metadb_server_port);

	if (pid_file != NULL) {
		/*
		 * We do this before calling gfarm_daemon()
		 * to print the error message to stderr.
		 */
		pid_fp = fopen(pid_file, "w");
		if (pid_fp == NULL)
			gflog_fatal_errno(pid_file);
	}
	if (!debug_mode) {
		gflog_syslog_open(LOG_PID, syslog_facility);
		gfarm_daemon(0, 0);
	}
	if (pid_file != NULL) {
		/*
		 * We do this after calling gfarm_daemon(),
		 * because it changes pid.
		 */
		fprintf(pid_fp, "%ld\n", (long)getpid());
		fclose(pid_fp);
	}

	giant_init();

	table_size = GFMD_CONNECTION_LIMIT;
	gfarm_unlimit_nofiles(&table_size);
	if (table_size > GFMD_CONNECTION_LIMIT)
		table_size = GFMD_CONNECTION_LIMIT;

	host_init();
	user_init();
	group_init();
	grpassign_init();
	inode_init();
	dir_entry_init();
	file_copy_init();
	dead_file_copy_init();

	peer_init(table_size);
	job_table_init(table_size);

	/*
	 * We don't want SIGPIPE, but want EPIPE on write(2)/close(2).
	 */
	signal(SIGPIPE, SIG_IGN);

	main_loop(sock);

	/*NOTREACHED*/
	return (0); /* to shut up warning */
}
