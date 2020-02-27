/*
 * $Id$
 */

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "hash.h"
#include "thrsubr.h"

#include "config.h"
#include "auth.h"
#include "gfm_proto.h"
#include "gfutil.h"
#include "gfp_xdr.h"
#include "repattr.h"

#include "db_access.h"
#include "fsngroup.h"
#include "host.h"
#include "inode.h"
#include "peer.h"
#include "rpcsubr.h"
#include "subr.h"
#include "user.h"
#include "replica_check.h"

struct fsngroup_tuple {
	char *hostname;
	char *fsngroupname;
};

static void
fsngroup_free_tuples(gfarm_int32_t n_tuples, struct fsngroup_tuple *tuples)
{
	gfarm_int32_t i;

	for (i = 0; i < n_tuples; i++)
		free(tuples[i].fsngroupname);
	free(tuples);
}

static int
record_fsngroup_tuple(struct host *h, void *closure, void *elemp)
{
	struct fsngroup_tuple *tuple = elemp;

	tuple->hostname = host_name(h);
	tuple->fsngroupname = strdup(host_fsngroup(h));
	return (1);
}

static gfarm_error_t
fsngroup_get_tuples(gfarm_int32_t *n_tuples, struct fsngroup_tuple **tuplesp)
{
	gfarm_error_t e;
	size_t i, n;
	void *ret;
	struct fsngroup_tuple *tuples;

	e = host_iterate(record_fsngroup_tuple, NULL,
	    sizeof(struct fsngroup_tuple), &n, &ret);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	tuples = ret;
	for (i = 0; i < n; i++) {
		if (tuples[i].fsngroupname == NULL) { /* strdup() failed */
			fsngroup_free_tuples(n, tuples);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	*n_tuples = n;
	*tuplesp = tuples;
	return (GFARM_ERR_NO_ERROR);
}

/*****************************************************************************/
/*
 * Replication scheduler:
 */

static gfarm_error_t
gfarm_repattr_parse_cached(const char *fsng,
	gfarm_repattr_t **repsp, size_t *nrepsp)
{
	/* cache */
	static char *last_fsng = NULL;
	static gfarm_repattr_t *last_reps = NULL;
	static size_t last_nreps = 0;

	gfarm_error_t e;
	char *fsng_tmp;
	static gfarm_repattr_t *reps_tmp;
	static size_t nreps_tmp;

	if (last_fsng != NULL && strcmp(fsng, last_fsng) == 0) {
		/* cache hit */
		*repsp = last_reps;
		*nrepsp = last_nreps;
		return (GFARM_ERR_NO_ERROR);
	}

	e = gfarm_repattr_parse(fsng, &reps_tmp, &nreps_tmp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/* update the cache */

	fsng_tmp = strdup(fsng);
	if (fsng_tmp == NULL) {
		gflog_error(GFARM_MSG_1005059, "no memory for '%s'", fsng);
		return (GFARM_ERR_NO_MEMORY);
	}

	if (last_reps != NULL)
		gfarm_repattr_free_all(last_nreps, last_reps);
	free(last_fsng);

	last_fsng = fsng_tmp;
	last_reps = reps_tmp;
	last_nreps = nreps_tmp;

	*repsp = last_reps;
	*nrepsp = last_nreps;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * Make us sure our having the giant_lock acquired.
 */
/*
 * srcs[] must be different from existing[].
 */
gfarm_error_t
fsngroup_schedule_replication(
	struct inode *inode, struct dirset *tdirset, const char *repattr,
	int n_srcs, struct host **srcs,
	int *n_existingp, struct hostset *existing, gfarm_time_t grace,
	int *n_being_removedp, struct hostset *being_removed, const char *diag,
	int *total_p, int *req_ok_nump)
{
	gfarm_error_t e, save_e = GFARM_ERR_NO_ERROR;
	int i, n_scope;
	size_t nreps = 0;
	gfarm_repattr_t *reps = NULL;
	struct hostset *scope;

	assert(repattr != NULL && srcs != NULL);

	if (debug_mode)
		gflog_debug(GFARM_MSG_1004050,
		    "%s: replicate inode %lld:%lld to fsngroup '%s'.",
		    diag, (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode), repattr);

	*total_p = 0;

	e = gfarm_repattr_parse_cached(repattr, &reps, &nreps);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004051,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (nreps == 0) {
		gflog_error(GFARM_MSG_1004052,
		    "%s: can't parse a repattr: '%s'.", diag, repattr);
		/* fall through */
	}

	if (gfarm_replicainfo_enabled) {
		int next_src_index = host_select_one(n_srcs, srcs, diag);

		for (i = 0; i < nreps; i++) {
			const char *group = gfarm_repattr_group(reps[i]);
			int num = gfarm_repattr_amount(reps[i]);

			*total_p = *total_p + num;

			scope = hostset_of_fsngroup_alloc(group, &n_scope);
			if (scope == NULL) {
				save_e = GFARM_ERR_NO_MEMORY;
				gflog_notice(GFARM_MSG_1004053,
				    "%s: hostset_of_fsngroup(%s): %s",
				    diag, group, gfarm_error_string(save_e));
				break;
			}
			e = inode_schedule_replication_within_scope(
			    inode, tdirset, num, n_srcs, srcs, &next_src_index,
			    &n_scope, scope, n_existingp, existing, grace,
			    n_being_removedp, being_removed, diag,
			    req_ok_nump);
			hostset_free(scope);
			if (e == GFARM_ERR_NO_MEMORY) {
				save_e = e;
				break;
			}
			if (save_e == GFARM_ERR_NO_ERROR)
				save_e = e;
		}
	} else {
		/* ignore hostgroups, but the total number is used. */
		for (i = 0; i < nreps; i++) {
			int num = gfarm_repattr_amount(reps[i]);

			*total_p = *total_p + num;
		}
	}

	/* gfarm_repattr_free_all(nreps, reps); -- shouldn't call (cached) */
	return (save_e);
}

/* return GFARM_ERR_NO_ERROR, if there is a spare */
gfarm_error_t
fsngroup_has_spare_for_repattr(struct inode *inode, int current_copy_count,
	const char *fsng, const char *repattr, int up_only)
{
	gfarm_error_t e;
	int n_scope;
	struct hostset *scope;
	int ncopy, n_desired, total, found;
	size_t i, nreps = 0;
	gfarm_repattr_t *reps = NULL;

	e = gfarm_repattr_parse_cached(repattr, &reps, &nreps);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004025,
		    "gfarm_repattr_parse(%s): %s",
		    repattr, gfarm_error_string(e));
		return (e); /* report shortage, for safety */
	}

	n_desired = 0;
	total = 0;
	found = 0;
	for (i = 0; i < nreps; i++) {
		int num = gfarm_repattr_amount(reps[i]);

		total += num;
		if (!found &&
		    strcmp(gfarm_repattr_group(reps[i]), fsng) == 0) {
			n_desired = num;
			found = 1;
		}
	}
	/* gfarm_repattr_free_all(nreps, reps); -- shouldn't call (cached) */

	if (current_copy_count <= total)
		return (GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS);

	if (!gfarm_replicainfo_enabled) {
		/*
		 * ignore 'n_desired' for the hostgroup
		 * (use 'total' only)
		 */
		return (GFARM_ERR_NO_ERROR); /* has spare */
	}

	/* no desired number for the host */
	if (n_desired <= 0)
		return (GFARM_ERR_NO_ERROR); /* has spare */

	scope = hostset_of_fsngroup_alloc(fsng, &n_scope);
	if (scope == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1004026,
		    "fsngroup_get_hosts(%s): %s",
		    fsng, gfarm_error_string(e));
		return (e); /* report shortage, for safety */
	}
	if (n_scope == 0) { /* unexpected */
		hostset_free(scope);
		gflog_error(GFARM_MSG_1005060,
		    "no host in fsngroup %s: unexpected", fsng);

		/* report shortage, for safety */
		return (GFARM_ERR_INTERNAL_ERROR);
	}

	ncopy = inode_count_replicas_within_scope(
	    inode, 1, up_only, 0, scope);

	hostset_free(scope);

	if (ncopy > n_desired)
		return (GFARM_ERR_NO_ERROR); /* has spare */
	return (GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS); /* shortage */
}

/*****************************************************************************/
/*
 * Server side RPC stubs:
 */

gfarm_error_t
gfm_server_fsngroup_get_all(
	struct peer *peer,
	int from_client, int skip)
{
	/*
	 * IN:
	 *	None
	 *
	 * OUT:
	 *	resultcode::integer
	 *	if resultcode == GFM_ERROR_NO_ERROR
	 *		n::integer
	 *		tuple{hostname::string, fsngroupname::string}[n]
	 */

	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_int32_t i, n = 0;
	struct fsngroup_tuple *t = NULL;
	static const char diag[] = "GFM_PROTO_FSNGROUP_GET_ALL";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	{ /* XXX */
		giant_lock();
		e = fsngroup_get_tuples(&n, &t);
		giant_unlock();

		if (e != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1004054,
			    "%s: get_fsngroup_tuples(): %s",
			    diag, gfarm_error_string(e));
	}

	e = gfm_server_put_reply(peer, diag, e, "i", n);

	if (e == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			e = gfp_xdr_send(client, "ss",
				t[i].hostname, t[i].fsngroupname);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1004055,
				    "%s@%s: %s: gfp_xdr_send() failed: %s",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    diag, gfarm_error_string(e));
				break;
			}
		}
	}

	if (t != NULL)
		fsngroup_free_tuples(n, t);

	return (e);
}

gfarm_error_t
gfm_server_fsngroup_get_by_hostname(
	struct peer *peer,
	int from_client, int skip)
{
	/*
	 * IN:
	 *	hostname::string
	 *
	 * OUT:
	 *	resultcode::integer
	 *	if resultcode == GFM_ERROR_NO_ERROR
	 *		fsngroupname::string
	 */

	gfarm_error_t e = GFARM_ERR_UNKNOWN;
	char *hostname = NULL;		/* Always need to be free'd */
	char *fsngroupname = NULL;	/* Always need to be free'd */
	static const char diag[] = "GFM_PROTO_FSNGROUP_GET_BY_HOSTNAME";

	(void)from_client;

	e = gfm_server_get_request(
		peer, diag,
		"s", &hostname);
	if (e != GFARM_ERR_NO_ERROR)
		goto bailout;
	if (skip) {
		e = GFARM_ERR_NO_ERROR;
		goto bailout;
	}

	{
		if (hostname != NULL && hostname[0] != '\0') {
			struct host *h = NULL;

			giant_lock();
			if ((h = host_lookup(hostname)) != NULL) {
				fsngroupname = strdup(host_fsngroup(h));
				if (fsngroupname == NULL)
					e = GFARM_ERR_NO_MEMORY;
			}
			giant_unlock();

			if (h == NULL) {
				gflog_debug(GFARM_MSG_1004056,
					"host \"%s\" does not exist.",
					hostname);
				e = GFARM_ERR_NO_SUCH_OBJECT;
			}
		} else {
			gflog_debug(GFARM_MSG_1004057,
				"an invalid hostname parameter (nul).");
			e = GFARM_ERR_INVALID_ARGUMENT;
		}

	}

	if (fsngroupname != NULL)
		e = gfm_server_put_reply(
			peer, diag, e, "s", fsngroupname);
	else
		e = gfm_server_put_reply(
			peer, diag, e, "");

bailout:
	free(hostname);
	free(fsngroupname);

	return (e);
}

gfarm_error_t
gfm_server_fsngroup_modify(
	struct peer *peer,
	int from_client, int skip)
{
	/*
	 * IN:
	 *	hostname::string
	 *	fsngroupname::string
	 *
	 * OUT:
	 *	resultcode::integer
	 */

	static const char diag[] = "GFM_PROTO_FSNGROUP_MODIFY";
	gfarm_error_t e;
	char *hostname;
	char *fsngroupname;

	e = gfm_server_get_request(peer, diag,
		"ss", &hostname, &fsngroupname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		e = GFARM_ERR_NO_ERROR;
		goto bailout;
	}

	{
		struct host *h = NULL;
		struct user *user = peer_get_user(peer);

		giant_lock();

		if (!from_client || user == NULL || !user_is_admin(user)) {
			gflog_debug(GFARM_MSG_1004058,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((h = host_lookup(hostname)) == NULL) {
			gflog_debug(GFARM_MSG_1004059,
				"host does not exists");
			e = GFARM_ERR_NO_SUCH_OBJECT;
		} else if (gfarm_read_only_mode()) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s (%s@%s) during read_only",
			    diag,
			    peer_get_username(peer),
			    peer_get_hostname(peer));
			e = GFARM_ERR_READ_ONLY_FILE_SYSTEM;
		} else if ((e = host_fsngroup_modify(h, fsngroupname, diag)) !=
		    GFARM_ERR_NO_ERROR) {
			;
		} else if ((e = db_fsngroup_modify(hostname, fsngroupname)) !=
		    GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1004060,
				"db_fsngroup_modify failed: %s",
				gfarm_error_string(e));
			/* XXX - need to revert the change in memory? */
		}

		giant_unlock();

		if (e == GFARM_ERR_NO_ERROR)
			replica_check_start_fsngroup_modify();
	}
	e = gfm_server_put_reply(peer, diag, e, "");

bailout:
	free(hostname);
	free(fsngroupname);

	return (e);
}
