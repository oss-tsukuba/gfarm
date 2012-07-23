/*
 * $Id$
 */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

/* for host_addr_lookup() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <pthread.h>

#include <gfarm/gfarm.h>

#include "internal_host_info.h"

#include "gfutil.h"
#include "hash.h"
#include "thrsubr.h"

#include "metadb_common.h"	/* gfarm_host_info_free_except_hostname() */
#include "gfp_xdr.h"
#include "gfm_proto.h" /* GFM_PROTO_SCHED_FLAG_* */
#include "gfs_proto.h" /* GFS_PROTOCOL_VERSION */
#include "auth.h"
#include "config.h"

#include "callout.h"
#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "host.h"
#include "mdhost.h"
#include "user.h"
#include "peer.h"
#include "process.h"
#include "inode.h"
#include "dir.h"
#include "abstract_host.h"
#include "abstract_host_impl.h"
#include "dead_file_copy.h"
#include "back_channel.h"
#include "relay.h"
#include "fsngroup.h"
#include "fsngroup_replica.h"
#include "repattr.h"

/*****************************************************************************/

typedef enum {
	FIND_UNKNOWN = 0,
	FIND_NCOPY_ONLY,
	FIND_REPLICAINFO_ONLY,
	FIND_NEAREST,
	FIND_REPLICAINFO_UNTIL_FOUND
} attribute_serach_t;
#define ATTR_SEARCH	FIND_NEAREST

typedef struct {
	struct process *process;
	int fd;
	attribute_serach_t how;
} visitor_arg;

/*****************************************************************************/
/*
 * Internal procedures: attribute search workhoses.
 */

static int
find_and_set_ncopy(struct inode *inode, struct process *process, int fd)
{
	int ret = 0;
	int ncopy = 0;

	if ((ret = inode_has_desired_number(inode, &ncopy)) == 1)
		(void)process_record_desired_number(
			process, fd, ncopy);

	return (ret);
}

static int
find_and_set_repattr(struct inode *inode, struct process *process, int fd)
{
	int ret = 0;
	char *info = NULL;

	if ((ret = inode_has_repattr(inode, &info)) == 1) {
		/*
		 * The info is malloc'd in inode_has_repattr.
		 */
		if (info != NULL && *info != '\0') {
			(void)process_record_repattr(
				process, fd, info);
		} else {
			ret = 0;
			free(info);
		}
	}

	return (ret);
}

static int
process_get_ncopy(struct process *process, int fd)
{
	struct file_opening *fo = NULL;
	int ret = 0;
	if (process_get_file_opening(process, fd, &fo) == GFARM_ERR_NO_ERROR &&
		fo != NULL && inode_is_file(fo->inode))
		ret = fo->u.f.desired_replica_number;
	return (ret);
}

static int
visitor(struct inode *inode, void *arg)
{
	visitor_arg *varg = (visitor_arg *)arg;
	attribute_serach_t how = varg->how;
	int fd = varg->fd;
	struct process *process = varg->process;
	int ret = 0;

	switch (how) {
	case FIND_NCOPY_ONLY:
		ret = find_and_set_ncopy(inode, process, fd);
		break;
	case FIND_REPLICAINFO_ONLY:
		ret = find_and_set_repattr(inode, process, fd);
		break;
	case FIND_NEAREST:
		/*
		 * The repattr is prior to the ncopy.
		 */
		if ((ret = find_and_set_repattr(inode, process, fd)) == 0)
			ret = find_and_set_ncopy(inode, process, fd);
		break;
	case FIND_REPLICAINFO_UNTIL_FOUND: {
		/*
		 * Even if a ncopy is already found, keep on searching
		 * until repattr is found.
		 */
		if ((ret = find_and_set_repattr(inode, process, fd)) == 0
			&& process_get_ncopy(process, fd) == 0)
			(void)find_and_set_ncopy(inode, process, fd);
		break;
	}
	default:
		break;
	}

	return (ret);
}

/*****************************************************************************/
/*
 * Internal procedures: subroutines for replication.
 */
static size_t
sync_random(size_t from, size_t to)
{
	size_t ret;
	size_t range = to - from;

	ret = (((size_t)gfarm_random()) % range) + from;

	return (ret);
}

/*
 * The default random host scheduler.
 */
static size_t
schedule_random(const char * const candidates[], size_t ncandidates,
	size_t *indices, size_t nindices)
{
	size_t i;
	size_t r;

	assert(ncandidates > nindices);

	r = sync_random(0, ncandidates);

	for (i = 0; i < nindices; i++)
		indices[i] = (r + i) % ncandidates;

	return (nindices);
}

/*
 * The scheduler wrapper.
 */
static size_t
schedule(gfarm_fsngroup_text_t t, size_t candmax, size_t **retindicesp,
	fsngroup_sched_proc_t sched_proc)
{
	size_t nfsnghosts = gfm_fsngroup_text_size(t);
	size_t max = (nfsnghosts > candmax) ? candmax : nfsnghosts;
	size_t *indices = NULL;
	size_t ret = 0;
	size_t i;
	const char * const *hosts;

	for (i = 0; i < nfsnghosts; i++) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"schedule(): candidate %3zu: '%s'",
			i, gfm_fsngroup_text_line(t, i));
	}

	if (retindicesp != NULL)
		*retindicesp = NULL;

	GFARM_MALLOC_ARRAY(indices, max);
	if (indices == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
			"schedule(): insufficient memory for allocating "
			"%zu size_t.", max);
		return (0);
	}

	if (nfsnghosts == max) {
		/*
		 * No need to call scheduler, just select all of the
		 * candidates.
		 */
		for (i = 0; i < max; i++)
			indices[i] = i;
		ret = max;
	} else {
		hosts = gfm_fsngroup_text_lines(t);
		ret = (sched_proc)(hosts, nfsnghosts, indices, max);
	}

	if (ret == 0 || retindicesp == NULL)
		free(indices);
	else
		*retindicesp = indices;

	return (ret);
}

/*
 * A xerox with an assistant.
 */
static void
replicate_file_asynchronously(struct inode *inode,
	struct host *src_host, const char *dst_hostname)
{
	struct host *dst_host = host_lookup(dst_hostname);
	struct file_replication *fr = NULL;
	gfarm_error_t e = GFARM_ERR_UNKNOWN;
	gfarm_ino_t i_number;
	gfarm_uint64_t i_gen;

	assert(inode != NULL && src_host != NULL && dst_host != NULL);

	i_number = inode_get_number(inode);
	i_gen = inode_get_gen(inode);

	gflog_debug(GFARM_MSG_UNFIXED,
		"replicate_file_asynchronously(): about to replicate: "
		"[inode %llu(gen %llu)]@%s -> %s",
		(long long)i_number,
		(long long)i_gen,
		host_name(src_host), dst_hostname);

	e = inode_replication_new(inode, src_host, dst_host, NULL, &fr);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_UNFIXED,
			"replicate_file_asynchronously(): "
			"file_replication_new() failed, for replication: "
			"[inode %llu(gen %llu)]@%s -> %s: %s",
			(long long)i_number,
			(long long)i_gen,
			host_name(src_host), dst_hostname,
			gfarm_error_string(e));
		return;
	}

	gflog_debug(GFARM_MSG_UNFIXED,
		"replicate_file_asynchronously(): replication scheduled: "
		"[inode %llu(gen %llu)]@%s -> %s",
		(long long)i_number,
		(long long)i_gen,
		host_name(src_host), dst_hostname);
}

/*****************************************************************************/
/*
 * Exported APIs:
 */

void
gfarm_server_process_record_replication_attribute(
	struct process *process, int fd,
	struct inode *inode, struct inode *base)
{
	visitor_arg arg = {
		process, fd, ATTR_SEARCH
	};

	if (visitor(inode, (void *)&arg) == 0)
		(void)inode_visit_directory_bottom_up(
			base, visitor, (void *)&arg);
}

/*
 * Make us sure our having the giant_lock acquired.
 */
void
gfarm_server_fsngroup_replicate_file(struct inode *inode,
	struct host *src_host, const char *info,
	char **exclusions, size_t nexclusions)
{
	gfarm_repattr_t *reps = NULL;
	gfarm_fsngroup_text_t ghosts;
	gfarm_fsngroup_text_t exs = NULL;
	size_t nreps = 0;
	char *fsngroupname;
	size_t i;
	gfarm_error_t e;
#define diag "gfarm_server_fsngroup_replicate_file(): "

	assert(info != NULL && src_host != NULL);

	gflog_debug(GFARM_MSG_UNFIXED,
		"gfarm_server_fsngroup_replicate_file(): "
		"replicate inode %llu@%s to fsngroup '%s'.",
		(long long)inode_get_number(inode),
		host_name(src_host),
		info);

	e = gfarm_repattr_parse(info, &reps, &nreps);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED, diag
			": %s", gfarm_error_string(e));
		goto done;
	}
	if (nreps == 0) {
		gflog_error(GFARM_MSG_UNFIXED, diag
			"can't parse a repattr: '%s'.", info);
		goto done;
	}

	/*
	 * Note:
	 *	Don't, even think, use the gfm_fsngroup_text_destroy()
	 *	to free up the exs. Use free() directly for that
	 *	instead, since the contents of the exclusions is NOT
	 *	owned by us.
	 */
	exs = gfm_fsngroup_text_allocate(nexclusions, exclusions);

	for (i = 0; i < nreps; i++) {
		/*
		 * Use unlock version since we should have the
		 * giant_lock very here very this moment.
		 */
		fsngroupname = (char *)gfarm_repattr_group(reps[i]);
		ghosts = gfm_fsngroup_get_hostnames_by_fsngroup_unlock(
			fsngroupname, exs,
			FILTER_CHECK_VALID | FILTER_CHECK_UP);
		if (ghosts == NULL) {
			continue;
		} else {
			/*
			 * Let the final selection begin.
			 */
			size_t j;
			size_t *indices = NULL;
			size_t nindices;
			size_t idx;
			const char *dst_hostname;

			nindices = schedule(
				ghosts,
				gfarm_repattr_amount(reps[i]),
				&indices,
				schedule_random);

			/*
			 * Then replicate the file.
			 */
			for (j = 0; j < nindices; j++) {
				idx = indices[j];
				dst_hostname =
					gfm_fsngroup_text_line(ghosts, idx);
				/*
				 * Finally, we are here.
				 */
				replicate_file_asynchronously(
					inode, src_host, dst_hostname);
			}

			free(indices);
		}

		gfm_fsngroup_text_destroy(ghosts);
	}

done:
	if (nreps > 0 && reps != NULL)
		for (i = 0; i < nreps; i++)
			gfarm_repattr_free(reps[i]);
	free(reps);
	free(exs);
#undef diag
}
