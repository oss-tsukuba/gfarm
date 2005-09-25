/*
 * $Id$
 */

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

#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"

#include "xxx_proto.h"
#include "io_fd.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "gfm_proto.h"
#include "gfj_client.h"

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

char *
gfm_server_get_request(struct xxx_connection *client, char *diag,
	char *format, ...)
{
	va_list ap;
	char *e;
	int eof;

	va_start(ap, format);
	e = xxx_proto_vrecv(client, 0, &eof, &format, &ap);
	va_end(ap);

	if (e != NULL) {
		gflog_warning(diag, e);
		return (e);
	}
	if (eof) {
		gflog_warning("%s: missing RPC argument", diag);
		return (GFARM_ERR_PROTOCOL);
	}
	if (*format != '\0')
		gflog_fatal("%s: invalid format character to get request",
		    diag);
	return (NULL);
}

char *
gfm_server_put_reply(struct xxx_connection *client, char *diag,
	int ecode, char *format, ...)
{
	va_list ap;
	char *e;

	va_start(ap, format);
	e = xxx_proto_send(client, "i", (gfarm_int32_t)ecode);
	if (e != NULL) {
		gflog_warning("%s: %s", diag, e);
		return (e);
	}
	if (ecode == GFJ_ERROR_NOERROR) {
		e = xxx_proto_vsend(client, &format, &ap);
		if (e != NULL) {
			gflog_warning("%s: %s", diag, e);
			return (e);
		}
	}
	va_end(ap);

	if (ecode == 0 && *format != '\0')
		gflog_fatal("%s: invalid format character to put reply", diag);
	return (NULL);
}

#define gfj_server_get_request	gfm_server_get_request
#define gfj_server_put_reply	gfm_server_put_reply

void
gfarm_job_info_clear(struct gfarm_job_info *infos, int n)
{
	memset(infos, 0, sizeof(struct gfarm_job_info) * n);
}

void
gfarm_job_info_server_free_contents(struct gfarm_job_info *infos, int n)
{
	int i, j;
	struct gfarm_job_info *info;

	for (i = 0; i < n; i++) {
		info = &infos[i];
		/*
		 * DO NOT free info->user on gfmd,
		 * because it is shared with file_table[].user.
		 *
		 * if (info->user != NULL)
		 * 	free(info->user);
		 */
		free(info->job_type);
		free(info->originate_host);
		free(info->gfarm_url_for_scheduling);
		for (j = 0; j < info->argc; j++)
			free(info->argv[j]);
		free(info->argv);
		for (j = 0; j < info->total_nodes; j++)
			free(info->nodes[j].hostname);
		free(info->nodes);
	}
}

struct job_table_entry {
	/* linked list of jobs which were registered by same file descriptor */
	struct job_table_entry *next;

	int id;
	struct gfarm_job_info *info;
};

#define JOB_ID_MIN	1	/* we won't use job id 0 */
int job_table_free = JOB_ID_MIN;
int job_table_size = 2048;
struct job_table_entry **job_table;

void
job_table_init(int table_size)
{
	int i;

	job_table = malloc(sizeof(struct job_table_entry *)
			   * table_size);
	if (job_table == NULL) {
		errno = ENOMEM; gflog_fatal_errno("job table");
	}
	for (i = 0; i < table_size; i++)
		job_table[i] = NULL;
	job_table_size = table_size;
}

int
job_table_add(struct gfarm_job_info *info,
	      struct job_table_entry **listp)
{
	int id;

	if (job_table_free >= job_table_size) {
		for (job_table_free = JOB_ID_MIN;
		     job_table_free < job_table_size; job_table_free++)
			if (job_table[job_table_free] == NULL)
				break;
		if (job_table_free >= job_table_size)
			return (-1);
	}

	id = job_table_free;
	job_table[id] = malloc(sizeof(struct job_table_entry));
	if (job_table[id] == NULL)
		return (-1);
	job_table[id]->id = id;
	job_table[id]->info = info;
	job_table[id]->next = *listp;
	*listp = job_table[id];

	for (++job_table_free;
	     job_table_free < job_table_size; ++job_table_free)
		if (job_table[job_table_free] == NULL)
			break;
	return (id);
}

int
job_table_remove(int id, char *user, struct job_table_entry **listp)
{
	struct job_table_entry **pp = listp;

	if (id >= job_table_size || job_table[id] == NULL)
		return (EBADF);
	if (strcmp(job_table[id]->info->user, user) != 0)
		return (EPERM);

	for (; *pp != NULL; pp = &(*pp)->next)
		if ((*pp)->id == id)
			break;
	if (*pp == NULL) /* cannot find the id on the list */
		return (EBADF);

	/* assert(*pp == job_table[id]); */
	*pp = job_table[id]->next;
	gfarm_job_info_server_free_contents(job_table[id]->info, 1);
	free(job_table[id]);
	job_table[id] = NULL;

	return (0);
}

struct file_table_entry {
	struct xxx_connection *conn;
	char *user, *host;
	struct job_table_entry *jobs;
} *file_table;
int file_table_size;
int file_table_max = -1;

void
file_table_init(int table_size)
{
	int i;

	file_table = malloc(sizeof(struct file_table_entry) * table_size);
	if (file_table == NULL) {
		errno = ENOMEM; gflog_fatal_errno("job table");
	}
	for (i = 0; i < table_size; i++) {
		file_table[i].conn = NULL;
		file_table[i].user = NULL;
		file_table[i].jobs = NULL;
	}
	file_table_size = table_size;	     
}

int
file_table_add(struct xxx_connection *client, char *username, char *hostname)
{
	int fd = xxx_connection_fd(client);

	if (fd < 0)
		return (EINVAL);
	if (fd >= file_table_size)
		return (EMFILE);

	file_table[fd].conn = client;
	file_table[fd].user = username;
	file_table[fd].host = hostname;
	if (fd > file_table_max)
		file_table_max = fd;
	return (0);
}

int
file_table_close(int fd)
{
	if (fd < 0 || fd >= file_table_size || file_table[fd].conn == NULL)
		return (EBADF);

	/* disconnect, do logging */
	gflog_notice("(%s@%s) disconnected",
	    file_table[fd].user, file_table[fd].host);

	while (file_table[fd].jobs != NULL)
		job_table_remove(file_table[fd].jobs->id, file_table[fd].user,
				 &file_table[fd].jobs);
	file_table[fd].jobs = NULL;

	free(file_table[fd].user);
	file_table[fd].user = NULL;

	free(file_table[fd].host);
	file_table[fd].host = NULL;

	xxx_connection_free(file_table[fd].conn);
	file_table[fd].conn = NULL;

	if (fd == file_table_max) {
		while (--file_table_max >= 0)
			if (file_table[file_table_max].conn != NULL)
				break;
	}
	return (0);
}

void
file_table_fd_set(fd_set *set)
{
	int fd;

	for (fd = 0; fd <= file_table_max; fd++) {
		if (file_table[fd].conn != NULL)
			FD_SET(fd, set);
	}
}

char *
gfj_server_lock_register(struct xxx_connection *client)
{
	/* XXX - NOT IMPLEMENTED */

	return (gfj_server_put_reply(client, "lock_register",
	    GFJ_ERROR_NOERROR, ""));
}

char *
gfj_server_unlock_register(struct xxx_connection *client)
{
	/* XXX - NOT IMPLEMENTED */

	return (gfj_server_put_reply(client, "unlock_register",
	    GFJ_ERROR_NOERROR, ""));
}

char *
gfj_server_register(int client_socket)
{
	struct xxx_connection *client = file_table[client_socket].conn;
	char *user = file_table[client_socket].user;
	char *e;
	int i, eof;
	gfarm_int32_t flags, total_nodes, argc, job_id, error;
	struct gfarm_job_info *info;

	info = malloc(sizeof(struct gfarm_job_info));
	if (info == NULL)
		return (GFARM_ERR_NO_MEMORY);
	gfarm_job_info_clear(info, 1);
	e = gfj_server_get_request(client, "register", "iisssi",
				   &flags,
				   &total_nodes,
				   &info->job_type,
				   &info->originate_host,
				   &info->gfarm_url_for_scheduling,
				   &argc);
	if (e != NULL)
		return (e);
	/* XXX - currently `flags' is just igored */
	info->total_nodes = total_nodes;
	info->argc = argc;
	info->argv = malloc(sizeof(char *) * (argc + 1));
	info->nodes = malloc(sizeof(struct gfarm_job_node_info) * total_nodes);
	if (info->argv == NULL || info->nodes == NULL) {
		free(info->job_type);
		free(info->originate_host);
		free(info->gfarm_url_for_scheduling);
		if (info->argv != NULL)
			free(info->argv);
		if (info->nodes != NULL)
			free(info->nodes);
		free(info);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; i < argc; i++) {
		e = xxx_proto_recv(client, 0, &eof, "s", &info->argv[i]);
		if (e != NULL || eof) {
			if (e == NULL)
				e = GFARM_ERR_PROTOCOL;
			while (--i >= 0)
				free(info->argv[i]);
			free(info->job_type);
			free(info->originate_host);
			free(info->gfarm_url_for_scheduling);
			free(info->argv);
			free(info->nodes);
			return (e);
		}
	}
	info->argv[i] = NULL;
	info->user = user; /* shared with file_table[].user */
	for (i = 0; i < total_nodes; i++) {
		e = xxx_proto_recv(client, 0, &eof, "s",
				   &info->nodes[i].hostname);
		if (e != NULL || eof) {
			if (e == NULL)
				e = GFARM_ERR_PROTOCOL;
			while (--i >= 0)
				free(info->nodes[i].hostname);
			for (i = 0; i < argc; i++)
				free(info->argv[i]);
			free(info->job_type);
			free(info->originate_host);
			free(info->gfarm_url_for_scheduling);
			free(info->argv);
			free(info->nodes);
			return (e);
		}
		info->nodes[i].pid = 0;
		info->nodes[i].state = GFJ_NODE_NONE;
	}

	job_id = job_table_add(info, &file_table[client_socket].jobs);
	if (job_id < JOB_ID_MIN) {
		job_id = 0;
		error = GFJ_ERROR_TOO_MANY_JOBS;
	} else {
		error = GFJ_ERROR_NOERROR;
	}
	return (gfj_server_put_reply(client, "register",
	    error, "i", job_id));
}

char *
gfj_server_unregister(int client_socket)
{
	struct xxx_connection *client = file_table[client_socket].conn;
	char *user = file_table[client_socket].user;
	char *e;
	gfarm_int32_t error;
	gfarm_int32_t job_id;

	e = gfj_server_get_request(client, "unregister", "i",
				   &job_id);
	if (e != NULL)
		return (e);
	error = job_table_remove(job_id, user,
				 &file_table[client_socket].jobs);
	return (gfj_server_put_reply(client, "unregister",
	    error, ""));
}

char *
gfj_server_register_node(struct xxx_connection *client)
{
	/* XXX - NOT IMPLEMENTED */
	gflog_fatal("register_node: not implemented");

	return (gfj_server_put_reply(client, "register_node",
	    GFJ_ERROR_NOERROR, ""));
}

char *
gfj_server_list(struct xxx_connection *client)
{
	char *e, *user;
	int i;
	gfarm_int32_t n;

	e = gfj_server_get_request(client, "list", "s", &user);
	if (e != NULL)
		return (e);

	n = 0;
	for (i = 0; i < job_table_size; i++) {
		if (job_table[i] != NULL &&
		    (*user == '\0' ||
		     strcmp(user, job_table[i]->info->user) == 0))
			n++;
	}

	e = gfj_server_put_reply(client, "register",
	    GFJ_ERROR_NOERROR, "i", n);
	if (e != NULL)
		return (e);

	for (i = 0; i < job_table_size; i++) {
		if (job_table[i] != NULL &&
		    (*user == '\0' ||
		     strcmp(user, job_table[i]->info->user) == 0)) {
			e = xxx_proto_send(client, "i", (gfarm_int32_t)i);
			if (e != NULL)
				return (e);
		}
	}
	free(user);
	return (NULL);
}

char *
gfj_server_put_info_entry(struct xxx_connection *client,
			  struct gfarm_job_info *info)
{
	char *e;
	int i;

	e = xxx_proto_send(client, "issssi",
			   info->total_nodes,
			   info->user,
			   info->job_type,
			   info->originate_host,
			   info->gfarm_url_for_scheduling,
			   info->argc);
	if (e != NULL)
		return (e);
	for (i = 0; i < info->argc; i++) {
		e = xxx_proto_send(client, "s", info->argv[i]);
		if (e != NULL)
			return (e);
	}
	for (i = 0; i < info->total_nodes; i++) {
		e = xxx_proto_send(client, "sii",
				   info->nodes[i].hostname,
				   info->nodes[i].pid, info->nodes[i].state);
		if (e != NULL)
			return (e);
	}
	return (NULL);
}

char *
gfj_server_info(struct xxx_connection *client)
{
	char *e;
	int i, eof;
	gfarm_int32_t n, *jobs;

	e = gfj_server_get_request(client, "info", "i", &n);
	if (e != NULL)
		return (e);

	jobs = malloc(sizeof(*jobs) * n);
	if (jobs == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < n; i++) {
		e = xxx_proto_recv(client, 0, &eof, "i", &jobs[i]);
		if (e != NULL || eof) {
			if (e == NULL)
				e = GFARM_ERR_PROTOCOL;
			free(jobs);
			return (e);
		}
	}

	for (i = 0; i < n; i++) {
		if (jobs[i] < 0 || jobs[i] >= job_table_size ||
		    job_table[jobs[i]] == NULL) {
			e = gfj_server_put_reply(client, "info",
						 GFJ_ERROR_NO_SUCH_OBJECT, "");
			if (e != NULL) {
				free(jobs);
				return (e);
			}
		} else {
			e = gfj_server_put_reply(client, "info",
						 GFJ_ERROR_NOERROR, "");
			if (e != NULL) {
				free(jobs);
				return (e);
			}
			e = gfj_server_put_info_entry(client,
			      job_table[jobs[i]]->info);
			if (e != NULL) {
				free(jobs);
				return (e);
			}
		}
	}
	free(jobs);
	return (NULL);
}

char *
gfj_server_hostinfo(struct xxx_connection *client)
{
	/* XXX - NOT IMPLEMENTED */
	gflog_fatal("host_info: not implemented");

	return (gfj_server_put_reply(client, "host_info",
	    GFJ_ERROR_NOERROR, ""));
}

void
service(int client_socket)
{
	struct xxx_connection *client;
	char *e;
	int eof;
	gfarm_int32_t request;

	client = file_table[client_socket].conn;
	e = xxx_proto_recv(client, 0, &eof, "i", &request);
	if (eof) {
		file_table_close(client_socket);
		return;
	}
	if (e != NULL) {
		gflog_warning("request number: %s", e);
		return;
	}
	switch (request) {
	case GFJ_PROTO_LOCK_REGISTER:
		e = gfj_server_lock_register(client); break;
	case GFJ_PROTO_UNLOCK_REGISTER:
		e = gfj_server_unlock_register(client); break;
	case GFJ_PROTO_REGISTER:
		e = gfj_server_register(client_socket);
		break;
	case GFJ_PROTO_UNREGISTER:
		e = gfj_server_unregister(client_socket);
		break;
	case GFJ_PROTO_REGISTER_NODE:
		e = gfj_server_register_node(client); break;
	case GFJ_PROTO_LIST:
		e = gfj_server_list(client); break;
	case GFJ_PROTO_INFO:
		e = gfj_server_info(client); break;
	case GFJ_PROTO_HOSTINFO:
		e = gfj_server_hostinfo(client); break;
	default:
		gflog_warning("unknown request: %d", request);
	}
	if (e == NULL)
		e = xxx_proto_flush(client);
	if (e != NULL)
		file_table_close(client_socket);
}

void
main_loop(int accepting_socket)
{
	char *e, *username, *hostname;
	int max_fd, nfound, client_socket, fd;
	struct xxx_connection *client_conn;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size;
	fd_set readable;

	/*
	 * To deal with race condition which may be caused by RST,
	 * listening socket must be O_NONBLOCK, if the socket will be
	 * used as a file descriptor for select(2) .
	 * See section 15.6 of "UNIX NETWORK PROGRAMMING, Volume1,
	 * Second Edition" by W. Richard Stevens, for detail.
	 * We do report such case by gflog_warning_errno("accept");
	 */
	if (fcntl(accepting_socket, F_SETFL,
	    fcntl(accepting_socket, F_GETFL, NULL) | O_NONBLOCK) == -1)
		gflog_warning_errno("accepting_socket O_NONBLOCK");

	for (;;) {
		FD_ZERO(&readable);
		FD_SET(accepting_socket, &readable);
		file_table_fd_set(&readable);
		max_fd = file_table_max >= accepting_socket ?
			file_table_max : accepting_socket;
		nfound = select(max_fd + 1, &readable, NULL, NULL, 0);
		if (nfound <= 0)
			continue;
		if (FD_ISSET(accepting_socket, &readable)) {
			client_addr_size = sizeof(client_addr);
			client_socket = accept(accepting_socket,
			   (struct sockaddr *)&client_addr, &client_addr_size);
			if (client_socket < 0) {
				if (errno != EINTR)
					gflog_warning_errno("accept");
			} else if ((e = xxx_fd_connection_new(client_socket,
			    &client_conn)) != NULL) {
				gflog_warning("fd_connection_new: %s", e);
				close(client_socket);
			} else if ((e = gfarm_authorize(client_conn, 0,
			    GFM_SERVICE_TAG,
			    &username, &hostname, NULL)) != NULL) {
				gflog_warning("authorize: %s", e);
				xxx_connection_free(client_conn);
			} else if ((errno = file_table_add(client_conn,
			    username, hostname)) != 0) {
				gflog_warning_errno("file_table_add");
				xxx_connection_free(client_conn);
				free(username);
				free(hostname);
			} else {
				int sockopt = 1;

				/* deal with reboots or network problems */
				if (setsockopt(client_socket,
				    SOL_SOCKET, SO_KEEPALIVE,
				    &sockopt, sizeof(sockopt)) == -1)
					gflog_warning_errno("SO_KEEPALIVE");
			}
		}
		for (fd = 0; fd <= file_table_max; fd++) {
			if (file_table[fd].conn != NULL &&
			    FD_ISSET(fd, &readable))
				service(fd);
		}
	}
}

int
open_accepting_socket(int port)
{
	char *e;
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
	if (e != NULL)
		gflog_warning("setsockopt: %s", e);
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
	char *e, *config_file = NULL, *port_number = NULL, *pid_file = NULL;
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
	if (e != NULL) {
		fprintf(stderr, "gfarm_server_initialize: %s\n", e);
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

	table_size = GFMD_CONNECTION_LIMIT;
	gfarm_unlimit_nofiles(&table_size);
	if (table_size > GFMD_CONNECTION_LIMIT)
		table_size = GFMD_CONNECTION_LIMIT;
	file_table_init(table_size);
	job_table_init(table_size);

	/*
	 * We don't want SIGPIPE, but want EPIPE on write(2)/close(2).
	 */
	signal(SIGPIPE, SIG_IGN);

	main_loop(sock);

	/*NOTREACHED*/
	return (0); /* to shut up warning */
}
