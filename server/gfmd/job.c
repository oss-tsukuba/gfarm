#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "auth.h"
#include "gfj_client.h"

#include "subr.h"
#include "peer.h"
#include "job.h"

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

int
job_get_id(struct job_table_entry *job)
{
	return (job->id);
}

void
job_table_init(int table_size)
{
	int i;

	GFARM_MALLOC_ARRAY(job_table, table_size);
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
	GFARM_MALLOC(job_table[id]);
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

gfarm_error_t
gfj_server_lock_register(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (!from_client)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;

	/* XXX - NOT IMPLEMENTED */

	return (gfj_server_put_reply(peer, "lock_register", e, ""));
}

gfarm_error_t
gfj_server_unlock_register(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (!from_client)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;

	/* XXX - NOT IMPLEMENTED */

	return (gfj_server_put_reply(peer, "unlock_register", e, ""));
}

gfarm_error_t
gfj_server_register(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	char *user = peer_get_username(peer);
	int i, eof;
	gfarm_int32_t flags, total_nodes, argc, error, job_id = 0;
	struct gfarm_job_info *info;

	GFARM_MALLOC(info);
	if (info == NULL)
		return (GFARM_ERR_NO_MEMORY);
	gfarm_job_info_clear(info, 1);
	e = gfj_server_get_request(peer, "register", "iisssi",
				   &flags,
				   &total_nodes,
				   &info->job_type,
				   &info->originate_host,
				   &info->gfarm_url_for_scheduling,
				   &argc);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/* XXX - currently `flags' is just igored */
	info->total_nodes = total_nodes;
	info->argc = argc;
	GFARM_MALLOC_ARRAY(info->argv, argc + 1);
	GFARM_MALLOC_ARRAY(info->nodes, total_nodes);
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
		e = gfp_xdr_recv(client, 0, &eof, "s", &info->argv[i]);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR)
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
		e = gfp_xdr_recv(client, 0, &eof, "s",
				   &info->nodes[i].hostname);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR)
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

	if (skip || !from_client) {
		for (i = 0; i < total_nodes; i++)
			free(info->nodes[i].hostname);
		for (i = 0; i < argc; i++)
			free(info->argv[i]);
		free(info->job_type);
		free(info->originate_host);
		free(info->gfarm_url_for_scheduling);
		free(info->argv);
		free(info->nodes);
		if (skip)
			return (GFARM_ERR_NO_ERROR);
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		giant_lock();
		job_id = job_table_add(info, peer_get_jobs_ref(peer));
		giant_unlock();
		if (job_id < JOB_ID_MIN) {
			job_id = 0;
			error = GFARM_ERR_TOO_MANY_JOBS;
		} else {
			error = GFARM_ERR_NO_ERROR;
		}
	}
	return (gfj_server_put_reply(peer, "register",
	    error, "i", job_id));
}

gfarm_error_t
gfj_server_unregister(struct peer *peer, int from_client, int skip)
{
	char *user = peer_get_username(peer);
	gfarm_error_t e;
	gfarm_int32_t error;
	gfarm_int32_t job_id;

	e = gfj_server_get_request(peer, "unregister", "i", &job_id);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (!from_client) {
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		giant_lock();
		error = job_table_remove(job_id, user,
		    peer_get_jobs_ref(peer));
		giant_unlock();
	}
	return (gfj_server_put_reply(peer, "unregister",
	    error, ""));
}

gfarm_error_t
gfj_server_register_node(struct peer *peer, int from_client, int skip)
{
	/* XXX - NOT IMPLEMENTED */
	gflog_fatal("register_node: not implemented");

	return (gfj_server_put_reply(peer, "register_node",
	    GFARM_ERR_NO_ERROR, ""));
}

gfarm_error_t
gfj_server_list(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	char *user;
	int i;
	gfarm_int32_t n;

	e = gfj_server_get_request(peer, "list", "s", &user);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(user);
		return (GFARM_ERR_NO_ERROR);
	}

	if (!from_client) {
		e = gfj_server_put_reply(peer, "list",
		    GFARM_ERR_OPERATION_NOT_PERMITTED, "");
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	} else {
		/* XXX FIXME too long giant lock */
		giant_lock();

		n = 0;
		for (i = 0; i < job_table_size; i++) {
			if (job_table[i] != NULL &&
			    (*user == '\0' ||
			     strcmp(user, job_table[i]->info->user) == 0))
				n++;
		}

		e = gfj_server_put_reply(peer, "register",
		    GFARM_ERR_NO_ERROR, "i", n);
		if (e != GFARM_ERR_NO_ERROR) {
			giant_unlock();
			return (e);
		}

		for (i = 0; i < job_table_size; i++) {
			if (job_table[i] != NULL &&
			    (*user == '\0' ||
			     strcmp(user, job_table[i]->info->user) == 0)) {
				e = gfp_xdr_send(client, "i",
				    (gfarm_int32_t)i);
				if (e != GFARM_ERR_NO_ERROR) {
					giant_unlock();
					return (e);
				}
			}
		}
		giant_unlock();
	}
	free(user);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfj_server_put_info_entry(struct gfp_xdr *client,
			  struct gfarm_job_info *info)
{
	gfarm_error_t e;
	int i;

	e = gfp_xdr_send(client, "issssi",
			   info->total_nodes,
			   info->user,
			   info->job_type,
			   info->originate_host,
			   info->gfarm_url_for_scheduling,
			   info->argc);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < info->argc; i++) {
		e = gfp_xdr_send(client, "s", info->argv[i]);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	for (i = 0; i < info->total_nodes; i++) {
		e = gfp_xdr_send(client, "sii",
				   info->nodes[i].hostname,
				   info->nodes[i].pid, info->nodes[i].state);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfj_server_info(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	int i, eof;
	gfarm_int32_t n, *jobs;

	e = gfj_server_get_request(peer, "info", "i", &n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC_ARRAY(jobs, n);
	if (jobs == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < n; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "i", &jobs[i]);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR)
				e = GFARM_ERR_PROTOCOL;
			free(jobs);
			return (e);
		}
	}

	if (skip || !from_client) {
		free(jobs);
		if (skip)
			return (GFARM_ERR_NO_ERROR);
		e = gfj_server_put_reply(peer, "info",
		    GFARM_ERR_OPERATION_NOT_PERMITTED, "");
		return (e);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < n; i++) {
		if (jobs[i] < 0 || jobs[i] >= job_table_size ||
		    job_table[jobs[i]] == NULL) {
			e = gfj_server_put_reply(peer, "info",
						 GFARM_ERR_NO_SUCH_OBJECT, "");
			if (e != GFARM_ERR_NO_ERROR) {
				giant_unlock();
				free(jobs);
				return (e);
			}
		} else {
			e = gfj_server_put_reply(peer, "info",
						 GFARM_ERR_NO_ERROR, "");
			if (e != GFARM_ERR_NO_ERROR) {
				free(jobs);
				giant_unlock();
				return (e);
			}
			e = gfj_server_put_info_entry(peer_get_conn(peer),
			      job_table[jobs[i]]->info);
			if (e != GFARM_ERR_NO_ERROR) {
				free(jobs);
				giant_unlock();
				return (e);
			}
		}
	}
	free(jobs);
	giant_unlock();
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfj_server_hostinfo(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (!from_client)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;

	/* XXX - NOT IMPLEMENTED */
	gflog_fatal("host_info: not implemented");

	return (gfj_server_put_reply(peer, "host_info",
	    GFARM_ERR_NO_ERROR, ""));
}

