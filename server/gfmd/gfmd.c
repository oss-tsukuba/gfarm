/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/time.h>
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
#include "hostspec.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
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

gfarm_error_t
protocol_switch(struct peer *peer, int from_client, int skip, int level,
	gfarm_int32_t *requestp, gfarm_error_t *on_errorp)
{
	gfarm_error_t e;
	int eof;
	gfarm_int32_t request;

	e = gfp_xdr_recv(peer_get_conn(peer), 0, &eof, "i", &request);
	if (eof) {
		/* actually, this is not an error, but completion on eof */
		peer_record_protocol_error(peer);
		return (GFARM_ERR_NO_ERROR);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning("receiving request number: %s",
		    gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e); /* finish on error */
	}
	switch (request) {
	case GFM_PROTO_HOST_INFO_GET_ALL:
		e = gfm_server_host_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE:
		e = gfm_server_host_info_get_by_architecture(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_NAMES:
		e = gfm_server_host_info_get_by_names(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_GET_BY_NAMEALIASES:
		e = gfm_server_host_info_get_by_namealises(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_SET:
		e = gfm_server_host_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_MODIFY:
		e = gfm_server_host_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_HOST_INFO_REMOVE:
		e = gfm_server_host_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_GET_ALL:
		e = gfm_server_user_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_GET_BY_NAMES:
		e = gfm_server_user_info_get_by_names(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_SET:
		e = gfm_server_user_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_MODIFY:
		e = gfm_server_user_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_USER_INFO_REMOVE:
		e = gfm_server_user_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_GET_ALL:
		e = gfm_server_group_info_get_all(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_GET_BY_NAMES:
		e = gfm_server_group_info_get_by_names(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_SET:
		e = gfm_server_group_info_set(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_MODIFY:
		e = gfm_server_group_info_modify(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_REMOVE:
		e = gfm_server_group_info_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_ADD_USERS:
		e = gfm_server_group_info_add_users(peer, from_client, skip);
		break;
	case GFM_PROTO_GROUP_INFO_REMOVE_USERS:
		e = gfm_server_group_info_remove_users(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_GROUP_NAMES_GET_BY_USERS:
		e = gfm_server_group_names_get_by_users(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_COMPOUND_BEGIN:
		e = gfm_server_compound_begin(peer, from_client, skip, level);
		break;
	case GFM_PROTO_COMPOUND_END:
		e = gfm_server_compound_end(peer, from_client, skip, level);
		break;
	case GFM_PROTO_COMPOUND_ON_ERROR:
		e = gfm_server_compound_on_error(peer, from_client, skip,
		    level, on_errorp);
		break;
	case GFM_PROTO_GET_FD:
		e = gfm_server_get_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_PUT_FD:
		e = gfm_server_put_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_SAVE_FD:
		e = gfm_server_save_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_RESTORE_FD:
		e = gfm_server_restore_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_CREATE:
		e = gfm_server_create(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN:
		e = gfm_server_open(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN_ROOT:
		e = gfm_server_open_root(peer, from_client, skip);
		break;
	case GFM_PROTO_OPEN_PARENT:
		e = gfm_server_open_parent(peer, from_client, skip);
		break;
	case GFM_PROTO_CLOSE:
		e = gfm_server_close(peer, from_client, skip);
		break;
	case GFM_PROTO_VERIFY_TYPE:
		e = gfm_server_verify_type(peer, from_client, skip);
		break;
	case GFM_PROTO_VERIFY_TYPE_NOT:
		e = gfm_server_verify_type_not(peer, from_client, skip);
		break;
	case GFM_PROTO_BEQUEATH_FD:
		e = gfm_server_bequeath_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_INHERIT_FD:
		e = gfm_server_inherit_fd(peer, from_client, skip);
		break;
	case GFM_PROTO_FSTAT:
		e = gfm_server_fstat(peer, from_client, skip);
		break;
	case GFM_PROTO_FUTIMES:
		e = gfm_server_futimes(peer, from_client, skip);
		break;
	case GFM_PROTO_FCHMOD:
		e = gfm_server_fchmod(peer, from_client, skip);
		break;
	case GFM_PROTO_FCHOWN:
		e = gfm_server_fchown(peer, from_client, skip);
		break;
	case GFM_PROTO_CKSUM_GET:
		e = gfm_server_cksum_get(peer, from_client, skip);
		break;
	case GFM_PROTO_CKSUM_SET:
		e = gfm_server_cksum_set(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_FILE:
		e = gfm_server_schedule_file(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM:
		e = gfm_server_schedule_file_with_program(peer,
		    from_client, skip);
		break;
	case GFM_PROTO_REMOVE:
		e = gfm_server_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_RENAME:
		e = gfm_server_rename(peer, from_client, skip);
		break;
	case GFM_PROTO_FLINK:
		e = gfm_server_flink(peer, from_client, skip);
		break;
	case GFM_PROTO_MKDIR:
		e = gfm_server_mkdir(peer, from_client, skip);
		break;
	case GFM_PROTO_SYMLINK:
		e = gfm_server_symlink(peer, from_client, skip);
		break;
	case GFM_PROTO_READLINK:
		e = gfm_server_readlink(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRPATH:
		e = gfm_server_getdirpath(peer, from_client, skip);
		break;
	case GFM_PROTO_GETDIRENTS:
		e = gfm_server_getdirents(peer, from_client, skip);
		break;
	case GFM_PROTO_SEEK:
		e = gfm_server_seek(peer, from_client, skip);
		break;
	case GFM_PROTO_REOPEN:
		e = gfm_server_reopen(peer, from_client, skip);
		break;
	case GFM_PROTO_CLOSE_READ:
		e = gfm_server_close_read(peer, from_client, skip);
		break;
	case GFM_PROTO_CLOSE_WRITE:
		e = gfm_server_close_write(peer, from_client, skip);
		break;
	case GFM_PROTO_LOCK:
		e = gfm_server_lock(peer, from_client, skip);
		break;
	case GFM_PROTO_TRYLOCK:
		e = gfm_server_trylock(peer, from_client, skip);
		break;
	case GFM_PROTO_UNLOCK:
		e = gfm_server_unlock(peer, from_client, skip);
		break;
	case GFM_PROTO_LOCK_INFO:
		e = gfm_server_lock_info(peer, from_client, skip);
		break;
	case GFM_PROTO_GLOB:
		e = gfm_server_glob(peer, from_client, skip);
		break;
	case GFM_PROTO_SCHEDULE:
		e = gfm_server_schedule(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_OPEN:
		e = gfm_server_pio_open(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_SET_PATHS:
		e = gfm_server_pio_set_paths(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_CLOSE:
		e = gfm_server_pio_close(peer, from_client, skip);
		break;
	case GFM_PROTO_PIO_VISIT:
		e = gfm_server_pio_visit(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_LIST_BY_NAME:
		e = gfm_server_replica_list_by_name(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_LIST_BY_HOST:
		e = gfm_server_replica_list_by_host(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_REMOVE_BY_HOST:
		e = gfm_server_replica_remove_by_host(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADDING:
		e = gfm_server_replica_adding(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_ADDED:
		e = gfm_server_replica_added(peer, from_client, skip);
		break;
	case GFM_PROTO_REPLICA_REMOVE:
		e = gfm_server_replica_remove(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_ALLOC:
		e = gfm_server_process_alloc(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_ALLOC_CHILD:
		e = gfm_server_process_alloc_child(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_FREE:
		e = gfm_server_process_free(peer, from_client, skip);
		break;
	case GFM_PROTO_PROCESS_SET:
		e = gfm_server_process_set(peer, from_client, skip);
		break;
	case GFJ_PROTO_LOCK_REGISTER:
		e = gfj_server_lock_register(peer, from_client, skip); break;
	case GFJ_PROTO_UNLOCK_REGISTER:
		e = gfj_server_unlock_register(peer, from_client, skip); break;
	case GFJ_PROTO_REGISTER:
		e = gfj_server_register(peer, from_client, skip);
		break;
	case GFJ_PROTO_UNREGISTER:
		e = gfj_server_unregister(peer, from_client, skip);
		break;
	case GFJ_PROTO_REGISTER_NODE:
		e = gfj_server_register_node(peer, from_client, skip); break;
	case GFJ_PROTO_LIST:
		e = gfj_server_list(peer, from_client, skip); break;
	case GFJ_PROTO_INFO:
		e = gfj_server_info(peer, from_client, skip); break;
	case GFJ_PROTO_HOSTINFO:
		e = gfj_server_hostinfo(peer, from_client, skip); break;
	default:
		gflog_warning("unknown request: %d", request);
		e = GFARM_ERR_PROTOCOL;
	}
	if (e == GFARM_ERR_NO_ERROR) {
		/* XXX FIXME this shouldn't be done while COMPOUND loop. */
		e = gfp_xdr_flush(peer_get_conn(peer));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_warning("protocol flush: %s",
			    gfarm_error_string(e));
	}

	*requestp = request;
	/* continue unless protocol error happens */
	return (e);
}

void
compound_loop(struct peer *peer, int from_client, int skip_base, int level)
{
	gfarm_error_t e, cause = GFARM_ERR_NO_ERROR;
	gfarm_error_t current_block = GFARM_ERR_NO_ERROR;
	gfarm_int32_t request;
	int skip = skip_base;
	
	peer_fdpair_clear(peer);
	for (;;) {
		e = protocol_switch(peer, from_client, skip, level,
		    &request, &current_block);
		if (peer_had_protocol_error(peer))
			return; /* finish */
		if (e != GFARM_ERR_NO_ERROR) {
			if (!skip) {
				skip = 1;
				cause = e;
			}
		} else if (request == GFM_PROTO_COMPOUND_END) {
			return; /* finish */
		} else if (request == GFM_PROTO_COMPOUND_ON_ERROR) {
			break;
#if 0 /* We don't allow COMPOUND nesting to prevent stack overflow */
		} else if (request == GFM_PROTO_COMPOUND_BEGIN) {
			compound_loop(peer, from_client, skip, level + 1);
			if (peer_had_protocol_error(peer))
				return; /* finish */
#endif
		}
	}

	skip = skip_base || current_block != cause;
	for (;;) {
		e = protocol_switch(peer, from_client, skip, level,
		    &request, &current_block);
		if (peer_had_protocol_error(peer))
			return; /* finish */
		if (e != GFARM_ERR_NO_ERROR) {
			skip = 1;
		} else if (request == GFM_PROTO_COMPOUND_END) {
			break; /* finish */
		} else if (request == GFM_PROTO_COMPOUND_ON_ERROR) {
			skip = skip_base || current_block != cause;
#if 0 /* We don't allow COMPOUND nesting to prevent stack overflow */
		} else if (request == GFM_PROTO_COMPOUND_BEGIN) {
			compound_loop(peer, from_client, skip, level + 1);
			if (peer_had_protocol_error(peer))
				return; /* finish */
#endif
		}
	}
}

void
protocol_service(struct peer *peer, int from_client)
{
	gfarm_error_t e, current_block = GFARM_ERR_NO_ERROR;
	gfarm_int32_t request;

	for (;;) {
		peer_fdpair_clear(peer);
		e = protocol_switch(peer, from_client, 0, 0,
		    &request, &current_block);
		if (peer_had_protocol_error(peer))
			return; /* finish */
		if (e == GFARM_ERR_NO_ERROR &&
		    request == GFM_PROTO_COMPOUND_BEGIN) {
			compound_loop(peer, from_client, 0, 1);
			if (peer_had_protocol_error(peer))
				return; /* finish */
		}
	}
}

void *
protocol_main(void *arg)
{
	gfarm_error_t e;
	int rv;
	struct peer *peer = arg;
	enum gfarm_auth_id_type id_type;
	char *username, *hostname;
	enum gfarm_auth_method auth_method;
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);
	char addr_string[GFARM_SOCKADDR_STRLEN];

	rv = getpeername(gfp_xdr_fd(peer_get_conn(peer)), &addr, &addrlen);
	if (rv == -1) {
		gflog_error("authorize: getpeername: %s", strerror(errno));
		return (NULL);
	}
	e = gfarm_sockaddr_to_name(&addr, &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_sockaddr_to_string(&addr,
		    addr_string, GFARM_SOCKADDR_STRLEN);
		gflog_warning("%s: %s", gfarm_error_string(e), addr_string);
		hostname = strdup(addr_string);
		if (hostname == NULL) {
			gflog_warning("%s: %s", addr_string,
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (NULL);
		}
	}
	e = gfarm_authorize(peer_get_conn(peer), 0, GFM_SERVICE_TAG,
	    hostname, &addr,
	    &id_type, &username, &auth_method);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning("authorize: %s", gfarm_error_string(e));
	} else {
		peer_authorized(peer,
		    id_type, username, hostname, auth_method);
		protocol_service(peer, id_type == GFARM_AUTH_ID_TYPE_USER);
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
			gflog_warning("peer_alloc: %s", gfarm_error_string(e));
			close(client_socket);
		} else if ((e = peer_schedule(peer, protocol_main)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_warning("peer_schedule: authorize: %s",
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
		gflog_warning("setsockopt: %s", gfarm_error_string(e));
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
				gflog_fatal("%s: unknown syslog facility",
				    optarg);
			break;
		case 'v':
			gflog_auth_set_verbose(1);
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
