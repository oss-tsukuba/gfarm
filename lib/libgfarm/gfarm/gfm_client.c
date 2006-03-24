/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

/*
 * $Id$
 */

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "hash.h"
#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "xxx_proto.h"
#include "io_fd.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "gfm_proto.h"
#include "gfj_client.h"

struct gfm_connection {
	struct xxx_connection *conn;
	enum gfarm_auth_method auth_method;
};

#define SERVER_HASHTAB_SIZE	3079	/* prime number */

static struct gfarm_hash_table *gfm_server_hashtab = NULL;

static char *
gfm_client_connection0(char *hostname, struct gfm_connection *gfm_server)
{
	char *e, *host_fqdn;
	int sock;
	struct hostent *hp;
	struct sockaddr_in peer_addr;

	hp = gethostbyname(hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (GFARM_ERR_UNKNOWN_HOST);
	memset(&peer_addr, 0, sizeof(peer_addr));
	memcpy(&peer_addr.sin_addr, hp->h_addr,
	       sizeof(peer_addr.sin_addr));
	peer_addr.sin_family = hp->h_addrtype;
	peer_addr.sin_port = htons(gfarm_metadb_server_port);

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		return (gfarm_errno_to_error(errno));
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	/* XXX - how to report setsockopt(2) failure ? */
	gfarm_sockopt_apply_by_name_addr(sock, hp->h_name,
	    (struct sockaddr *)&peer_addr);

	if (connect(sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr))
	    < 0) {
		close(sock);
		return (gfarm_errno_to_error(errno));
	}
	e = xxx_fd_connection_new(sock, &gfm_server->conn);
	if (e != NULL) {
		close(sock);
		return (e);
	}
	/*
	 * the reason why we call strdup() is because
	 * gfarm_auth_request() may break static work area of `*hp'
	 */
	host_fqdn = strdup(hp->h_name);
	if (host_fqdn == NULL) {
		xxx_connection_free(gfm_server->conn);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfarm_auth_request(gfm_server->conn,
	    GFM_SERVICE_TAG, host_fqdn, (struct sockaddr *)&peer_addr,
	    &gfm_server->auth_method);
	free(host_fqdn);
	if (e != NULL) {
		xxx_connection_free(gfm_server->conn);
		return (e);
	}
	return (NULL);
}

char *
gfm_client_connection(char *hostname, struct gfm_connection **gfm_serverp)
{
	char *e;
	struct gfarm_hash_entry *entry;
	struct gfm_connection *gfm_server;
	int created;

	if (gfm_server_hashtab == NULL) {
		gfm_server_hashtab =
		    gfarm_hash_table_alloc(SERVER_HASHTAB_SIZE,
			gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
		if (gfm_server_hashtab == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}

	entry = gfarm_hash_enter(gfm_server_hashtab,
				 hostname, strlen(hostname) + 1,
				 sizeof(struct gfm_connection), &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	gfm_server = gfarm_hash_entry_data(entry);
	if (created) {
		e = gfm_client_connection0(hostname, gfm_server);
		if (e != NULL) {
			gfarm_hash_purge(gfm_server_hashtab,
					 hostname, strlen(hostname) + 1);
			return (e);
		}
	}
	*gfm_serverp = gfm_server;
	return (NULL);
}

char *
gfm_proto_error_string(int error)
{
	switch (error) {
	case GFM_ERROR_NOERROR:
		return (NULL);
	case GFM_ERROR_NO_MEMORY:
		return (GFARM_ERR_NO_MEMORY);
	case GFM_ERROR_NO_SUCH_OBJECT:
		return (GFARM_ERR_NO_SUCH_OBJECT);
	case GFJ_ERROR_TOO_MANY_JOBS:
		return ("too many jobs");
	default:
		return (GFARM_ERR_UNKNOWN);
	}
}

struct gfm_connection *gfarm_metadb_server = NULL;

char *
gfj_initialize(void)
{
	char *e;

	if (gfarm_metadb_server_name == NULL)
		return ("\"metadb_serverhost\" is missing in configuration");
	e = gfm_client_connection(gfarm_metadb_server_name,
				  &gfarm_metadb_server);
	return (e);
}

char *
gfm_client_rpc_request(struct gfm_connection *gfm_server, int command,
		       char *format, ...)
{
	va_list ap;
	char *e;

	va_start(ap, format);
	e = xxx_proto_vrpc_request(gfm_server->conn, command, &format, &ap);
	va_end(ap);
	return (e);
}

char *
gfm_client_rpc_result(struct gfm_connection *gfm_server, int just,
		      char *format, ...)
{
	va_list ap;
	char *e;
	int error;

	va_start(ap, format);
	e = xxx_proto_vrpc_result(gfm_server->conn, just,
				  &error, &format, &ap);
	va_end(ap);

	if (e != NULL)
		return (e);
	if (error != 0)
		return (gfm_proto_error_string(error));
	return (NULL);
}

char *
gfm_client_rpc(struct gfm_connection *gfm_server, int just, int command,
	       char *format, ...)
{
	va_list ap;
	char *e;
	int error;

	va_start(ap, format);
	e = xxx_proto_vrpc(gfm_server->conn, just,
			   command, &error, &format, &ap);
	va_end(ap);

	if (e != NULL)
		return (e);
	if (error != 0)
		return (gfm_proto_error_string(error));
	return (NULL);
}

char *
gfj_client_lock_register(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc(gfm_server, 0, GFJ_PROTO_LOCK_REGISTER,
			       "/"));
}

char *
gfj_client_unlock_register(struct gfm_connection *gfm_server)
{
	return (gfm_client_rpc(gfm_server, 0, GFJ_PROTO_UNLOCK_REGISTER,
			       "/"));
}

char *
gfj_client_register(struct gfm_connection *gfm_server,
		    struct gfarm_job_info *job, int flags,
		    int *job_idp)
{
	char *e;
	int i;
	gfarm_int32_t job_id;

	e = gfm_client_rpc_request(gfm_server, GFJ_PROTO_REGISTER,
				   "iisssi",
				   (gfarm_int32_t)flags,
				   (gfarm_int32_t)job->total_nodes,
				   job->job_type,
				   job->originate_host,
				   job->gfarm_url_for_scheduling,
				   (gfarm_int32_t)job->argc);
	if (e != NULL)
		return (e);
	for (i = 0; i < job->argc; i++)
		e = xxx_proto_send(gfm_server->conn, "s", job->argv[i]);
	for (i = 0; i < job->total_nodes; i++)
		e = xxx_proto_send(gfm_server->conn, "s",
				   job->nodes[i].hostname);
	e = gfm_client_rpc_result(gfm_server, 0, "i", &job_id);
	if (e == NULL)
		*job_idp = job_id;
	return (e);
}

char *
gfj_client_unregister(struct gfm_connection *gfm_server, int job_id)
{
	return (gfm_client_rpc(gfm_server, 0, GFJ_PROTO_UNREGISTER, "i/",
			       job_id));
}

char *
gfj_client_list(struct gfm_connection *gfm_server, char *user,
		      int *np, int **jobsp)
{
	char *e;
	int i, n, eof, *jobs;
	gfarm_int32_t job_id;

	e = gfm_client_rpc(gfm_server, 0, GFJ_PROTO_LIST, "s/i", user, &n);
	if (e != NULL)
		return (e);
	jobs = malloc(sizeof(int) * n);
	if (jobs == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < n; i++) {
		e = xxx_proto_recv(gfm_server->conn, 0, &eof, "i", &job_id);
		if (e != NULL) {
			free(jobs);
			return (e);
		}
		if (eof) {
			free(jobs);
			return (GFARM_ERR_PROTOCOL);
		}
		jobs[i] = job_id;
	}
	*np = n;
	*jobsp = jobs;
	return (NULL);
}

char *
gfj_client_info_entry(struct xxx_connection *conn,
		      struct gfarm_job_info *info)
{
	char *e;
	int eof, i;
	gfarm_int32_t total_nodes, argc, node_pid, node_state;

	e = xxx_proto_recv(conn, 0, &eof, "issssi",
			   &total_nodes,
			   &info->user,
			   &info->job_type,
			   &info->originate_host,
			   &info->gfarm_url_for_scheduling,
			   &argc);
	if (e != NULL)
		return (e);
	if (eof)
		return (GFARM_ERR_PROTOCOL);
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
		return (GFARM_ERR_NO_MEMORY);
	}

	for (i = 0; i < argc; i++) {
		e = xxx_proto_recv(conn, 0, &eof, "s", &info->argv[i]);
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
	info->argv[argc] = NULL;

	for (i = 0; i < total_nodes; i++) {
		e = xxx_proto_recv(conn, 0, &eof, "sii",
				   &info->nodes[i].hostname,
				   &node_pid, &node_state);
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
		info->nodes[i].pid = node_pid;
		info->nodes[i].state = node_state;
	}
	info->total_nodes = total_nodes;
	info->argc = argc;
	return (NULL);
}

char *
gfj_client_info(struct gfm_connection *gfm_server, int n, int *jobs,
		      struct gfarm_job_info *infos)
{
	char *e;
	int i;

	e = gfm_client_rpc_request(gfm_server, GFJ_PROTO_INFO, "i", n);
	if (e != NULL)
		return (e);
	for (i = 0; i < n; i++) {
		e = xxx_proto_send(gfm_server->conn, "i",
				   (gfarm_int32_t)jobs[i]);
		if (e != NULL)
			return (e);
	}

	gfarm_job_info_clear(infos, n);
	for (i = 0; i < n; i++) {
		e = gfm_client_rpc_result(gfm_server, 0, "");
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			continue;
		if (e == NULL)
			e = gfj_client_info_entry(gfm_server->conn, &infos[i]);
		if (e != NULL) {
			gfarm_job_info_free_contents(infos, i - 1);
			return (e);
		}
	}
	return (NULL);
}

void
gfarm_job_info_clear(struct gfarm_job_info *infos, int n)
{
	memset(infos, 0, sizeof(struct gfarm_job_info) * n);
}

void
gfarm_job_info_free_contents(struct gfarm_job_info *infos, int n)
{
	int i, j;
	struct gfarm_job_info *info;

	for (i = 0; i < n; i++) {
		info = &infos[i];
		if (info->user == NULL) /* this entry is not valid */
			continue;
		free(info->user);
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

/*
 * convenience function
 */
char *
gfarm_user_job_register(int nhosts, char **hosts,
			char *job_type, char *sched_file,
			int argc, char **argv,
			int *job_idp)
{
	char *e;
	int i;
	struct gfarm_job_info job_info;

	gfarm_job_info_clear(&job_info, 1);
	job_info.total_nodes = nhosts;
	job_info.user = gfarm_get_global_username();
	job_info.job_type = job_type;
	e = gfarm_host_get_canonical_self_name(&job_info.originate_host);
	if (e == GFARM_ERR_UNKNOWN_HOST || e == GFARM_ERR_AMBIGUOUS_RESULT ||
	    e == GFARM_ERR_INVALID_ARGUMENT /* XXX - via gfarm_agent */) {
		/*
		 * gfarm client doesn't have to be a compute host,
		 * so, we should allow non canonical name here.
		 */
		job_info.originate_host = gfarm_host_get_self_name();
	} else if (e != NULL)
		return (e);
	job_info.gfarm_url_for_scheduling = sched_file;
	job_info.argc = argc;
	job_info.argv = argv;
	job_info.nodes = malloc(sizeof(struct gfarm_job_node_info) * nhosts);
	if (job_info.nodes == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < nhosts; i++) {
		e = gfarm_host_get_canonical_name(hosts[i],
		    &job_info.nodes[i].hostname);
		if (e != NULL) {
			while (--i >= 0)
				free(job_info.nodes[i].hostname);
			free(job_info.nodes);
			return (e);
		}
	}
	e = gfj_client_register(gfarm_jobmanager_server, &job_info, 0,
				job_idp);
	for (i = 0; i < nhosts; i++)
		free(job_info.nodes[i].hostname);
	free(job_info.nodes);
	return (e);
}
