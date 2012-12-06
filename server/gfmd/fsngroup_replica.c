/*
 * $Id$
 */
#include <assert.h>

#include <stdio.h>
#include <stdint.h>		/* XXX */
#include <stdlib.h>
#include <time.h>		/* XXX */

#include <gfarm/gfarm.h>

#include "dir.h"
#include "fsngroup.h"
#include "fsngroup_replica.h"
#include "host.h"
#include "inode.h"
#include "process.h"
#include "replica_info.h"

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
find_and_set_replicainfo(struct inode *inode, struct process *process, int fd)
{
	int ret = 0;
	char *info = NULL;

	if ((ret = inode_has_replicainfo(inode, &info)) == 1) {
		/*
		 * The info is malloc'd in inode_has_replicainfo.
		 */
		if (info != NULL && *info != '\0') {
			(void)process_record_replicainfo(
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
		ret = find_and_set_replicainfo(inode, process, fd);
		break;
	case FIND_NEAREST:
		/*
		 * The replicainfo is prior to the ncopy.
		 */
		if ((ret = find_and_set_replicainfo(inode, process, fd)) == 0)
			ret = find_and_set_ncopy(inode, process, fd);
		break;
	case FIND_REPLICAINFO_UNTIL_FOUND: {
		/*
		 * Even if a ncopy is already found, keep on searching
		 * until replicainfo is found.
		 */
		if ((ret = find_and_set_replicainfo(inode, process, fd)) == 0
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

static int rnd_initialized = 0;
static pthread_mutex_t rndlock;

static void
init_random(void)
{
	if (rnd_initialized == 0) {
		struct timespec ts;
		unsigned long int seed;
		uint64_t seed64;

		gfarm_mutex_init(&rndlock, "init_random()", "init");

		(void)clock_gettime(CLOCK_REALTIME, &ts);

		seed64 = (uint64_t)ts.tv_sec ^ (uint64_t)ts.tv_nsec;

		if (sizeof(int32_t) == sizeof(int) &&
			sizeof(int) == sizeof(long int)) {
			/* 32 bit int. */
			seed = (unsigned long)(seed64 & 0xffffffff);
		} else {
			/* 64 bit int. */
			seed = seed64;
		}

		gfarm_mutex_lock(&rndlock, "init_random()", "lock");
		srandom(seed);
		gfarm_mutex_unlock(&rndlock, "init_random()", "unlock");

		rnd_initialized = 1;
	}
}
#define RND_INIT(...)	{ if (rnd_initialized == 0) init_random(); }

static size_t
sync_random(size_t from, size_t to)
{
	size_t ret;
	size_t range = to - from;

	gfarm_mutex_lock(&rndlock, "sync_random()", "lock");
	ret = (((size_t)random()) % range) + from;
	gfarm_mutex_unlock(&rndlock, "sync_random()", "unlock");

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

	assert(ncandidates >= nindices);

	if (ncandidates == nindices) {
		/*
		 * Schedule all.
		 */
		for (i = 0; i < nindices; i++)
			indices[i] = i;
	} else {
		size_t r = sync_random(0, ncandidates);

		for (i = 0; i < nindices; i++)
			indices[i] = (r + i) % ncandidates;
	}

	return (nindices);
}

/*
 * The scheduler wrpper.
 */
static size_t
schedule(gfarm_fsngroup_text_t t, size_t candmax, size_t **retindicesp,
	 size_t (*sched_proc)(const char * const [], size_t,
			size_t *, size_t))
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

	hosts = gfm_fsngroup_text_lines(t);
	ret = (*sched_proc)(hosts, nfsnghosts, indices, max);

	if (ret == 0 || retindicesp == NULL)
		free(indices);
	else
		*retindicesp = indices;

	return (ret);
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
 * Presume our having the giant_lock acquired.
 */
void
gfarm_server_fsngroup_replicate_file(struct inode *inode,
	struct host *src_host, const char *info,
	char **exclusions, size_t nexclusions,
	fsngroup_sched_proc_t sched_proc)
{
	gfarm_replicainfo_t *reps = NULL;
	gfarm_fsngroup_text_t ghosts;
	gfarm_fsngroup_text_t exs = NULL;
	size_t nreps = 0;
	char *fsngroupname;
	size_t i;
#define diag "gfarm_server_fsngroup_replicate_file(): "

	assert(info != NULL && src_host != NULL);

	/*
	 * What to do are:
	 *
	 *	1) Parse info and create (fsngroup, amount) tupples.
	 *		... Done.
	 *	2) For each the tupple:
	 *		a) Create a set (hosts:fsngroup - ehosts)
	 *			... Done.
	 *		b) For each a host in the set:
	 *			Select dstination hosts.
	 *				... Done.
	 *			Create a replica in each destination host.
	 */

	gflog_debug(GFARM_MSG_UNFIXED,
		"gfarm_server_fsngroup_replicate_file(): "
		"replicate inode %llu@%s to fsngroup '%s'.",
		(long long)inode_get_number(inode),
		host_name(src_host),
		info);

	nreps = gfarm_replicainfo_parse(info, &reps);
	if (nreps == 0) {
		gflog_error(GFARM_MSG_UNFIXED, diag
			"can't parse a replicainfo: '%s'.", info);
		goto done;
	}

	RND_INIT();

	exs = gfm_fsngroup_text_allocate(nexclusions, exclusions);

	/*
	 * XXX FIXME:
	 *
	 *	Assuming that each reps[i] has a unique fsngroup, at
	 *	least for now. It must be canonicalized that a
	 *	replicainfo consists of tuples with a unique fsngroup
	 *	name and an amount #.
	 */

	for (i = 0; i < nreps; i++) {
		/*
		 * Use unlock version since we should have the
		 * giant_lock very here very this moment.
		 */
		fsngroupname = (char *)gfarm_replicainfo_group(reps[i]);
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

			nindices = schedule(
				ghosts,
				gfarm_replicainfo_amount(reps[i]),
				&indices,
				(sched_proc != NULL) ?
					sched_proc : schedule_random);

			for (j = 0; j < nindices; j++) {
				idx = indices[j];
				gflog_debug(GFARM_MSG_UNFIXED, diag
					"%s dst %3zu [idx=%3zu]: '%s'",
					fsngroupname, j, idx,
					gfm_fsngroup_text_line(ghosts, idx));
			}

			free(indices);
		}

		gfm_fsngroup_text_destroy(ghosts);
	}

done:
	if (nreps > 0 && reps != NULL)
		for (i = 0; i < nreps; i++)
			gfarm_replicainfo_free(reps[i]);
	free(reps);
	free(exs);
#undef diag
}
