/*
 * $Id$
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "hash.h"
#include "thrsubr.h"

#include "auth.h"
#include "gfm_proto.h"
#include "gfutil.h"
#include "gfp_xdr.h"

#include "db_access.h"
#include "fsngroup.h"
#include "host.h"
#include "peer.h"
#include "rpcsubr.h"
#include "subr.h"
#include "user.h"

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

static int
fsngroup_filter(struct host *h, void *closure)
{
	const char *fsngroup = closure;

	return (strcmp(host_fsngroup(h), fsngroup) == 0);
}

gfarm_error_t
fsngroup_get_hosts(const char *fsngroup, int *nhostsp, struct host ***hostsp)
{
	return (host_from_all(fsngroup_filter, (char *)fsngroup, /* UNCONST */
	    nhostsp, hostsp));
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
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: get_fsngroup_tuples(): %s",
			    diag, gfarm_error_string(e));
	}

	e = gfm_server_put_reply(peer, diag, e, "i", n);

	if (e == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			e = gfp_xdr_send(client, "ss",
				t[i].hostname, t[i].fsngroupname);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_UNFIXED,
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
				gflog_debug(GFARM_MSG_UNFIXED,
					"host \"%s\" does not exist.",
					hostname);
				e = GFARM_ERR_NO_SUCH_OBJECT;
			}
		} else {
			gflog_debug(GFARM_MSG_UNFIXED,
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
	gfarm_error_t e = GFARM_ERR_UNKNOWN;
	char *hostname = NULL;		/* need to be free'd always */
	char *fsngroupname = NULL;	/* need to be free'd always */

	e = gfm_server_get_request(peer, diag,
		"ss", &hostname, &fsngroupname);
	if (e != GFARM_ERR_NO_ERROR)
		goto bailout;
	if (skip) {
		e = GFARM_ERR_NO_ERROR;
		goto bailout;
	}

	{
		struct host *h = NULL;
		struct user *user = peer_get_user(peer);

		if (!from_client || user == NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			goto reply;
		}

		giant_lock();

		if ((h = host_lookup(hostname)) == NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"host does not exists");
			e = GFARM_ERR_NO_SUCH_OBJECT;
			goto unlock;
		}
		if (!user_is_admin(user)) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			goto unlock;
		}
		if ((e = db_fsngroup_modify(hostname, fsngroupname)) !=
			GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"db_fsngroup_modify failed: %s",
				gfarm_error_string(e));
			goto unlock;
		}
		host_fsngroup_modify(h, fsngroupname);

unlock:
		giant_unlock();
	}
reply:
	e = gfm_server_put_reply(peer, diag, e, "");

bailout:
	free(hostname);
	free(fsngroupname);

	return (e);
}
