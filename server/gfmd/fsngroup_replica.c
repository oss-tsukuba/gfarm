/*
 * $Id$
 */
#include <assert.h>

#include <stdio.h>
#include <stdlib.h>

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
	char **exclusions, size_t nexclusions)
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
	 *			Create a replica.
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
			fsngroupname, exs, 0);
		if (ghosts == NULL) {
			continue;
		} else {
			size_t j;
			size_t nghosts = gfm_fsngroup_text_size(ghosts);
			for (j = 0; j < nghosts; j++) {
				gflog_debug(GFARM_MSG_UNFIXED, diag
					"%s %3zu: '%s'",
					fsngroupname, j,
					gfm_fsngroup_text_line(ghosts, j));
			}
		}

		gfm_fsngroup_text_destroy(ghosts);
	}

done:
	if (nreps > 0 && reps != NULL)
		for (i = 0; i < nreps; i++)
			gfarm_replicainfo_free(reps[i]);
	free(reps);
	free(exs);
}
