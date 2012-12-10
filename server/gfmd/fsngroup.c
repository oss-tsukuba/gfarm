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

#define dup_or_null(X)	\
	((X) == NULL) ? NULL : strdup_ck((X), "dup_or_null")

/*****************************************************************************/
/*
 * Type definitions:
 */

typedef struct {
	char *hostname;
	char *fsngroupname;
} a_tuple_t;

struct gfarm_fsngroup_tuples_record {
	size_t n;
	a_tuple_t **tuples;
};

struct gfarm_fsngroup_text_record {
	size_t n;
	char **lines;
};

typedef struct {
	size_t nnames;
	char **names;
	gfarm_fsngroup_text_t exclusions;
	int flags;

#define CHECK_VALID(f)	((f) & FILTER_CHECK_VALID)
#define CHECK_UP(f)	((f) & FILTER_CHECK_UP)

#define MATCH_VALID(f, h) \
	((CHECK_VALID(f) && host_is_valid((h))) || !CHECK_VALID(f))
#define MATCH_UP(f, h) \
	((CHECK_UP(f) && host_is_up((h))) || !CHECK_UP(f))

#define MATCH_COND(f, h) \
	((MATCH_VALID((f), (h))) && (MATCH_UP((f), (h))))
} filter_arg_t;

/*****************************************************************************/
/*
 * Internal functions:
 */

/*
 * Basic objects constructors/destructors/methods:
 */

/*
 * Tuple/Tuples:
 */
static a_tuple_t *
allocate_tuple(const char *hostname, const char *fsngroupname)
{
	a_tuple_t *ret =
		(a_tuple_t *)malloc(sizeof(*ret));
	if (ret != NULL) {
		ret->hostname = dup_or_null(hostname);
		ret->fsngroupname = dup_or_null(fsngroupname);
	}

	return (ret);
}

static void
destroy_tuple(a_tuple_t *t)
{
	if (t != NULL) {
		free(t->hostname);
		free(t->fsngroupname);
		free(t);
	}
}

static gfarm_fsngroup_tuples_t
allocate_tuples(size_t n, a_tuple_t **tuples)
{
	struct gfarm_fsngroup_tuples_record *ret =
		(struct gfarm_fsngroup_tuples_record *)malloc(sizeof(*ret));
	if (ret != NULL) {
		ret->n = n;
		ret->tuples = tuples;
	}

	return ((gfarm_fsngroup_tuples_t)ret);
}

static void
destroy_tuples(gfarm_fsngroup_tuples_t t)
{
	struct gfarm_fsngroup_tuples_record *tr =
		(struct gfarm_fsngroup_tuples_record *)t;

	if (tr != NULL) {
		if (tr->tuples != NULL) {
			int i;
			for (i = 0; i < tr->n; i++) {
				destroy_tuple(tr->tuples[i]);
			}
			free(tr->tuples);
		}
		free(tr);
	}
}

static size_t
tuples_size(gfarm_fsngroup_tuples_t t)
{
	return (((struct gfarm_fsngroup_tuples_record *)t)->n);
}

static const char *
tuples_hostname(gfarm_fsngroup_tuples_t t, size_t i)
{
	struct gfarm_fsngroup_tuples_record *tr =
		(struct gfarm_fsngroup_tuples_record *)t;

	if (i < tr->n && tr->tuples[i] != NULL)
		return (const char *)(tr->tuples[i]->hostname);
	else
		return (NULL);
}

static const char *
tuples_fsngroup(gfarm_fsngroup_tuples_t t, size_t i)
{
	struct gfarm_fsngroup_tuples_record *tr =
		(struct gfarm_fsngroup_tuples_record *)t;

	if (i < tr->n && tr->tuples[i] != NULL)
		return (const char *)(tr->tuples[i]->fsngroupname);
	else
		return (NULL);
}

/*
 * Text:
 *	Actually, an array char * and # of its element.
 */
static gfarm_fsngroup_text_t
allocate_text(size_t n, char **lines)
{
	struct gfarm_fsngroup_text_record *ret =
		(struct gfarm_fsngroup_text_record *)malloc(sizeof(*ret));
	if (ret != NULL) {
		ret->n = n;
		ret->lines = lines;
	}

	return ((gfarm_fsngroup_text_t)ret);
}

static void
destroy_text(gfarm_fsngroup_text_t t)
{
	struct gfarm_fsngroup_text_record *tr =
		(struct gfarm_fsngroup_text_record *)t;

	if (tr != NULL) {
		if (tr->lines != NULL) {
			int i;
			for (i = 0; i < tr->n; i++) {
				free(tr->lines[i]);
			}
			free(tr->lines);
		}
		free(tr);
	}
}

static size_t
text_size(gfarm_fsngroup_text_t t)
{
	return (((struct gfarm_fsngroup_text_record *)t)->n);
}

static const char *
text_line(gfarm_fsngroup_text_t t, size_t i)
{
	struct gfarm_fsngroup_text_record *tr =
		(struct gfarm_fsngroup_text_record *)t;

	if (i < tr->n)
		return (const char *)(tr->lines[i]);
	else
		return (NULL);
}

static const char * const *
text_lines(gfarm_fsngroup_text_t t)
{
	struct gfarm_fsngroup_text_record *tr =
		(struct gfarm_fsngroup_text_record *)t;

	return (const char * const *)(tr->lines);
}

/*****************************************************************************/

/*
 * Matcher functions for the scanner:
 */

static void *
match_tuple_all(struct host *h, void *a, int *stopp)
{
	filter_arg_t *fa = (filter_arg_t *)a;
	int flags = fa->flags;
	gfarm_fsngroup_text_t exs = fa->exclusions;
	char *host = host_name(h);

	if (stopp != NULL)
		*stopp = 0;

	if (MATCH_COND(flags, h)) {
		if (exs == NULL) {
			return (allocate_tuple(host, host_fsngroup(h)));
		} else {
			size_t nexs = gfm_fsngroup_text_size(exs);
			size_t i;
			for (i = 0; i < nexs; i++) {
				if (strcmp(host,
					gfm_fsngroup_text_line(exs, i)) == 0)
					return (NULL);
			}
			return (allocate_tuple(host, host_fsngroup(h)));
		}
	}

	return (NULL);
}

static void *
match_tuple_by_hostnames(struct host *h, void *a, int *stopp)
{
	filter_arg_t *fa = (filter_arg_t *)a;
	int flags = fa->flags;
	gfarm_fsngroup_text_t exs = fa->exclusions;
	char *host = host_name(h);
	size_t i;

	if (stopp != NULL)
		*stopp = 0;

	if (MATCH_COND(flags, h)) {
		if (exs == NULL) {
			for (i = 0; i < fa->nnames; i++) {
				if (strcmp(host, fa->names[i]) == 0)
					return (allocate_tuple(fa->names[i],
							host_fsngroup(h)));
			}
		} else {
			size_t nexs = gfm_fsngroup_text_size(exs);
			for (i = 0; i < nexs; i++) {
				if (strcmp(host,
					gfm_fsngroup_text_line(exs, i)) == 0)
					return (NULL);
			}

			for (i = 0; i < fa->nnames; i++) {
				if (strcmp(host, fa->names[i]) == 0)
					return (allocate_tuple(fa->names[i],
							host_fsngroup(h)));
			}
		}
	}

	return (NULL);
}

static void *
match_tuple_by_fsngroups(struct host *h, void *a, int *stopp)
{
	filter_arg_t *fa = (filter_arg_t *)a;
	int flags = fa->flags;
	gfarm_fsngroup_text_t exs = fa->exclusions;
	char *fsngroup = host_fsngroup(h);
	size_t i;

	if (stopp != NULL)
		*stopp = 0;

	if (MATCH_COND(flags, h)) {
		if (exs == NULL) {
			for (i = 0; i < fa->nnames; i++) {
				if (strcmp(fsngroup, fa->names[i]) == 0)
					return (allocate_tuple(host_name(h),
							fa->names[i]));
			}
		} else {
			char *host = host_name(h);
			size_t nexs = gfm_fsngroup_text_size(exs);
			for (i = 0; i < nexs; i++) {
				if (strcmp(host,
					gfm_fsngroup_text_line(exs, i)) == 0)
					return (NULL);
			}

			for (i = 0; i < fa->nnames; i++) {
				if (strcmp(fsngroup, fa->names[i]) == 0)
					return (allocate_tuple(host,
							fa->names[i]));
			}
		}
	}

	return (NULL);
}

static void *
match_hostname_by_fsngroup(struct host *h, void *a, int *stopp)
{
	filter_arg_t *fa = (filter_arg_t *)a;
	const char *fsngroupname = (const char *)(fa->names[0]);
	int flags = fa->flags;
	gfarm_fsngroup_text_t exs = fa->exclusions;

	if (stopp != NULL)
		*stopp = 0;

	if (MATCH_COND(flags, h)) {
		if (exs == NULL) {
			if (strcmp(host_fsngroup(h), fsngroupname) == 0)
				return (strdup_ck(host_name(h),
					"get_hostname_by_fsngroup"));
		} else {
			char *host = host_name(h);
			size_t nexs = gfm_fsngroup_text_size(exs);
			size_t i;
			for (i = 0; i < nexs; i++) {
				if (strcmp(host,
					gfm_fsngroup_text_line(exs, i)) == 0)
					return (NULL);
			}

			if (strcmp(host_fsngroup(h), fsngroupname) == 0)
				return (strdup_ck(host,
					"get_hostname_by_fsngroup"));
		}
	}

	return (NULL);
}

/*
 * Scanners:
 */

static gfarm_fsngroup_tuples_t
get_tuples_all(gfarm_fsngroup_text_t exs, int flags)
{
	size_t n = 0;
	filter_arg_t arg = {
		0, NULL, exs, flags
	};

	a_tuple_t **tuples =
		(a_tuple_t **)host_iterate(
			match_tuple_all, (void *)&arg,
			sizeof(a_tuple_t *), 0, 0, &n);

	return (allocate_tuples(n, tuples));
}

static gfarm_fsngroup_tuples_t
get_tuples_by_hostnames(const char **hostnames, size_t nhostnames,
	gfarm_fsngroup_text_t exs, int flags)
{
	size_t n = 0;
	filter_arg_t arg = {
		nhostnames, (char **)hostnames, exs, flags
	};
	a_tuple_t **tuples =
		(a_tuple_t **)host_iterate(
			match_tuple_by_hostnames, (void *)&arg,
			sizeof(a_tuple_t *), 0, 0, &n);

	return (allocate_tuples(n, tuples));
}

static gfarm_fsngroup_tuples_t
get_tuples_by_fsngroups(const char **fsngroups, size_t nfsngroups,
	gfarm_fsngroup_text_t exs, int flags)
{
	size_t n = 0;
	filter_arg_t arg = {
		nfsngroups, (char **)fsngroups, exs, flags
	};
	a_tuple_t **tuples =
		(a_tuple_t **)host_iterate(
			match_tuple_by_fsngroups, (void *)&arg,
			sizeof(a_tuple_t *), 0, 0, &n);

	return (allocate_tuples(n, tuples));
}

static gfarm_fsngroup_text_t
get_hostnames_by_fsngroup(const char *fsngroup,
	gfarm_fsngroup_text_t exs, int flags)
{
	size_t n = 0;
	const char * const names[] = { fsngroup, NULL };
	filter_arg_t arg = {
		1, (char **)names, exs, flags
	};
	char **hostnames =
		(char **)host_iterate(
			match_hostname_by_fsngroup, (void *)&arg,
			sizeof(char *), 0, 0, &n);

	return (allocate_text(n, hostnames));
}

/*****************************************************************************/
/*
 * Exported functions:
 */

/*
 * Basic objects destructors/methods:
 */

size_t
gfm_fsngroup_tuples_size(gfarm_fsngroup_tuples_t t)
{
	return (tuples_size(t));
}

const char *
gfm_fsngroup_tuples_hostname(gfarm_fsngroup_tuples_t t, size_t i)
{
	return (tuples_hostname(t, i));
}

const char *
gfm_fsngroup_tuples_fsngroup(gfarm_fsngroup_tuples_t t, size_t i)
{
	return (tuples_fsngroup(t, i));
}

void
gfm_fsngroup_tuples_destroy(gfarm_fsngroup_tuples_t t)
{
	destroy_tuples(t);
}

size_t
gfm_fsngroup_text_size(gfarm_fsngroup_text_t t)
{
	return (text_size(t));
}

const char *
gfm_fsngroup_text_line(gfarm_fsngroup_text_t t, size_t i)
{
	return (text_line(t, i));
}

const char * const *
gfm_fsngroup_text_lines(gfarm_fsngroup_text_t t)
{
	return (text_lines(t));
}

void
gfm_fsngroup_text_destroy(gfarm_fsngroup_text_t t)
{
	destroy_text(t);
}

gfarm_fsngroup_text_t
gfm_fsngroup_text_allocate(size_t n, char **lines)
{
	return allocate_text(n, lines);
}

/*****************************************************************************/
/*
 * Scanners:
 */

gfarm_fsngroup_tuples_t
gfm_fsngroup_get_tuples_all_unlock(gfarm_fsngroup_text_t exs, int flags)
{
	return (get_tuples_all(exs, flags));
}

gfarm_fsngroup_tuples_t
gfm_fsngroup_get_tuples_all(gfarm_fsngroup_text_t exs, int flags)
{
	gfarm_fsngroup_tuples_t ret;

	giant_lock();
	ret = get_tuples_all(exs, flags);
	giant_unlock();

	return (ret);
}

gfarm_fsngroup_tuples_t
gfm_fsngroup_get_tuples_by_hostnames_unlock(
	const char **hostnames, size_t nhostnames,
	gfarm_fsngroup_text_t exs, int flags)
{
	return (get_tuples_by_hostnames(hostnames, nhostnames,
			exs, flags));
}

gfarm_fsngroup_tuples_t
gfm_fsngroup_get_tuples_by_hostnames(
	const char **hostnames, size_t nhostnames,
	gfarm_fsngroup_text_t exs, int flags)
{
	gfarm_fsngroup_tuples_t ret;

	giant_lock();
	ret = get_tuples_by_hostnames(hostnames, nhostnames, exs, flags);
	giant_unlock();

	return (ret);
}

gfarm_fsngroup_tuples_t
gfm_fsngroup_get_tuples_by_fsngroups_unlock(
	const char **hostnames, size_t nhostnames,
	gfarm_fsngroup_text_t exs, int flags)
{
	return (get_tuples_by_fsngroups(hostnames, nhostnames,
			exs, flags));
}

gfarm_fsngroup_tuples_t
gfm_fsngroup_get_tuples_by_fsngroups(
	const char **hostnames, size_t nhostnames,
	gfarm_fsngroup_text_t exs, int flags)
{
	gfarm_fsngroup_tuples_t ret;

	giant_lock();
	ret = get_tuples_by_fsngroups(hostnames, nhostnames, exs, flags);
	giant_unlock();

	return (ret);
}

gfarm_fsngroup_text_t
gfm_fsngroup_get_hostnames_by_fsngroup_unlock(
	const char *fsngroup, gfarm_fsngroup_text_t exs, int flags)
{
	return (get_hostnames_by_fsngroup(fsngroup, exs, flags));
}

gfarm_fsngroup_text_t
gfm_fsngroup_get_hostnames_by_fsngroup(
	const char *fsngroup, gfarm_fsngroup_text_t exs, int flags)
{
	gfarm_fsngroup_text_t ret;

	giant_lock();
	ret = get_hostnames_by_fsngroup(fsngroup, exs, flags);
	giant_unlock();

	return (ret);
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

	gfarm_error_t e = GFARM_ERR_UNKNOWN;

	(void)from_client;

	if (!skip) {
		int i;
		int n = 0;
		gfarm_fsngroup_tuples_t t = NULL;
		gfarm_error_t e2;
		const char diag[] = "GFM_PROTO_FSNGROUP_GET_ALL";

		giant_lock();
		t = get_tuples_all(NULL, FILTER_CHECK_VALID);
		giant_unlock();

		if (t != NULL) {
			n = tuples_size(t);
			e = GFARM_ERR_NO_ERROR;
		} else {
			gflog_debug(
				GFARM_MSG_UNFIXED,
				"get_tuples_all() returns NULL");
			e = GFARM_ERR_NO_MEMORY;
		}

		e2 = gfm_server_put_reply(peer, diag, e, "i", n);
		if (e2 != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"gfm_server_put_reply(%s) failed: %s",
				diag, gfarm_error_string(e));
		}

		if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR)
			goto done;

		if (n > 0) {
			struct gfp_xdr *c = peer_get_conn(peer);

			for (i = 0; i < n; i++) {
				e = gfp_xdr_send(c, "ss",
					tuples_hostname(t, i),
					tuples_fsngroup(t, i));
				if (e != GFARM_ERR_NO_ERROR) {
					gflog_debug(GFARM_MSG_UNFIXED,
						"gfp_xdr_send(%s) failed: %s",
						diag, gfarm_error_string(e));
					goto done;
				}
			}
		}

done:
		if (t != NULL)
			destroy_tuples(t);
	} else {
		e = GFARM_ERR_NO_ERROR;
	}

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
			if ((h = host_lookup(hostname)) != NULL)
				fsngroupname = strdup(host_fsngroup(h));
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
