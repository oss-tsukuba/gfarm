#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "auth.h"
#include "gfj_client.h"

#include "subr.h"
#include "rpcsubr.h"
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
		errno = ENOMEM; gflog_fatal_errno(GFARM_MSG_1000294,
		    "job table");
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
	if (job_table[id] == NULL) {
		gflog_debug(GFARM_MSG_1001682,
			"allocation of job_table failed");
		return (-1);
	}
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

#if 0

gfarm_error_t
gfj_server_lock_register(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "GFJ_PROTO_LOCK_REGISTER";

	if (!from_client) {
		gflog_debug(GFARM_MSG_1001683,
			"lock operation is not permitted for from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	}

	/* XXX - NOT IMPLEMENTED */

	return (gfj_server_put_reply(peer, xid, sizep, diag, e, ""));
}

gfarm_error_t
gfj_server_unlock_register(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "GFJ_PROTO_UNLOCK_REGISTER";

	if (!from_client) {
		gflog_debug(GFARM_MSG_1001684,
			"unlock operation is not permitted for from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	}

	/* XXX - NOT IMPLEMENTED */

	return (gfj_server_put_reply(peer, xid, sizep, diag, e, ""));
}

gfarm_error_t
gfj_server_register(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	char *user = peer_get_username(peer);
	int i, eof;
	gfarm_int32_t flags, total_nodes, argc, error, job_id = 0;
	struct gfarm_job_info *info;
	static const char diag[] = "GFJ_PROTO_REGISTER";

	GFARM_MALLOC(info);
	if (info == NULL) {
		gflog_debug(GFARM_MSG_1001685,
			"allocation of gfarm_job_info failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	gfarm_job_info_clear(info, 1);
	e = gfj_server_get_request(peer, sizep, diag, "iisssi",
				   &flags,
				   &total_nodes,
				   &info->job_type,
				   &info->originate_host,
				   &info->gfarm_url_for_scheduling,
				   &argc);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001686,
			"gfj_server_get_request() failed");
		return (e);
	}

	/* XXX - currently `flags' is just igored */
	info->total_nodes = total_nodes;
	info->argc = argc;
	GFARM_MALLOC_ARRAY(info->argv, argc + 1);
	GFARM_MALLOC_ARRAY(info->nodes, total_nodes);
	if (info->argv == NULL || info->nodes == NULL) {
		gflog_debug(GFARM_MSG_1001687,
			"allocation of 'info->argv' or 'info->nodes' failed ");
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
			gflog_debug(GFARM_MSG_1001688,
				"gfp_xdr_recv(info->argv[i]) failed");
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
			gflog_debug(GFARM_MSG_1001689,
				"gfp_xdr_recv(hostname) failed");
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
		gflog_debug(GFARM_MSG_1001690,
			"operation is not permitted for from_client");
	} else {
		giant_lock();
		job_id = job_table_add(info, peer_get_jobs_ref(peer));
		giant_unlock();
		if (job_id < JOB_ID_MIN) {
			job_id = 0;
			error = GFARM_ERR_TOO_MANY_JOBS;
			gflog_debug(GFARM_MSG_1001691,
				"too many jobs");
		} else {
			error = GFARM_ERR_NO_ERROR;
		}
	}
	return (gfj_server_put_reply(peer, xid, sizep, diag,
	    error, "i", job_id));
}

gfarm_error_t
gfj_server_unregister(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	char *user = peer_get_username(peer);
	gfarm_error_t e;
	gfarm_int32_t error;
	gfarm_int32_t job_id;
	static const char diag[] = "GFJ_PROTO_UNREGISTER";

	e = gfj_server_get_request(peer, sizep, diag, "i", &job_id);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001692,
			"unregister request failure");
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (!from_client) {
		error = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1001693,
			"operation is not permitted for from_client");
	} else {
		giant_lock();
		error = job_table_remove(job_id, user,
		    peer_get_jobs_ref(peer));
		giant_unlock();
	}
	return (gfj_server_put_reply(peer, xid, sizep, diag, error, ""));
}

gfarm_error_t
gfj_server_register_node(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	static const char diag[] = "GFJ_PROTO_REGISTER_NODE";

	/* XXX - NOT IMPLEMENTED */
	gflog_fatal(GFARM_MSG_1000295, "%s: not implemented", diag);

	return (gfj_server_put_reply(peer, xid, sizep,
	    diag, GFARM_ERR_NO_ERROR, ""));
}

gfarm_error_t
gfj_server_list(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	char *user;
	int i;
	gfarm_int32_t n;
	static const char diag[] = "GFJ_PROTO_LIST";

	e = gfj_server_get_request(peer, sizep, diag, "s", &user);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001694,
			"list request failure");
		return (e);
	}
	if (skip) {
		free(user);
		return (GFARM_ERR_NO_ERROR);
	}

	if (!from_client) {
		e = gfj_server_put_reply(peer, xid, sizep, diag,
		    GFARM_ERR_OPERATION_NOT_PERMITTED, "");
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001695,
				"gfj_server_put_reply(list) failed");
			return (e);
		}
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

		/* XXXRELAY FIXME, reply size is not correct */
		e = gfj_server_put_reply(peer, xid, sizep, diag,
		    GFARM_ERR_NO_ERROR, "i", n);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001696,
				"gfj_server_put_reply(register) failed");
			giant_unlock();
			return (e);
		}

		for (i = 0; i < job_table_size; i++) {
			if (job_table[i] != NULL &&
			    (*user == '\0' ||
			     strcmp(user, job_table[i]->info->user) == 0)) {
				/* XXXRELAY FIXME */
				e = gfp_xdr_send(client, "i",
				    (gfarm_int32_t)i);
				if (e != GFARM_ERR_NO_ERROR) {
					gflog_debug(GFARM_MSG_1001697,
						"gfp_xdr_send() failed");
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
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001698,
			"gfp_xdr_send() failed");
		return (e);
	}
	for (i = 0; i < info->argc; i++) {
		e = gfp_xdr_send(client, "s", info->argv[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001699,
				"gfp_xdr_send(info->argv[%d]) failed", i);
			return (e);
		}
	}
	for (i = 0; i < info->total_nodes; i++) {
		e = gfp_xdr_send(client, "sii",
				   info->nodes[i].hostname,
				   info->nodes[i].pid, info->nodes[i].state);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001700,
				"gfp_xdr_send(info->nodes[%d]) failed", i);
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfj_server_info(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	int i, eof;
	gfarm_int32_t n, *jobs;
	static const char diag[] = "GFJ_PROTO_INFO";

	e = gfj_server_get_request(peer, sizep, diag, "i", &n);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001701,
			"info request failure");
		return (e);
	}

	GFARM_MALLOC_ARRAY(jobs, n);
	if (jobs == NULL) {
		gflog_debug(GFARM_MSG_1001702,
			"allocation of 'jobs' failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	for (i = 0; i < n; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "i", &jobs[i]);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			gflog_debug(GFARM_MSG_1001703,
				"gfp_xdr_recv(jobs[%d]) failed", i);
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
		e = gfj_server_put_reply(peer, xid, sizep, diag,
		    GFARM_ERR_OPERATION_NOT_PERMITTED, "");
		gflog_debug(GFARM_MSG_1001704,
			"operation is not permitted for from_client");
		return (e);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < n; i++) {
		if (jobs[i] < 0 || jobs[i] >= job_table_size ||
		    job_table[jobs[i]] == NULL) {
			e = gfj_server_put_reply(peer, xid, sizep, diag,
						 GFARM_ERR_NO_SUCH_OBJECT, "");
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001705,
					"gfj_server_put_reply(info) failed");
				giant_unlock();
				free(jobs);
				return (e);
			}
		} else {
			/* XXXRELAY FIXME, reply size is not correct */
			e = gfj_server_put_reply(peer, xid, sizep, diag,
						 GFARM_ERR_NO_ERROR, "");
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001706,
					"gfj_server_put_reply(info) failed");
				free(jobs);
				giant_unlock();
				return (e);
			}
			/* XXXRELAY FIXME */
			e = gfj_server_put_info_entry(peer_get_conn(peer),
			      job_table[jobs[i]]->info);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001707,
					"gfj_server_put_info_entry() failed");
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
gfj_server_hostinfo(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	static const char diag[] = "GFJ_PROTO_HOSTINFO";

	/* XXX - NOT IMPLEMENTED */

	gflog_fatal(GFARM_MSG_1000296, "%s: not implemented", diag);

	return (gfj_server_put_reply(peer, xid, sizep,
	    diag, GFARM_ERR_NO_ERROR, ""));
}
#endif
