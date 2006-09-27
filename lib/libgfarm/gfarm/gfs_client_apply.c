#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "host.h"
#include "config.h"
#include "gfs_client.h"
#include "gfs_misc.h"
#include "schedule.h"

static int
apply_one_host(char *(*op)(struct gfs_connection *, void *),
	char *hostname, void *args, char *message, int tolerant)
{
	char *e;
	int pid;
	struct sockaddr addr;
	struct gfs_connection *conn;

	pid = fork();
	if (pid) {
		/* parent or error */
		return pid;
	}
	/* child */

	/* reflect "address_use" directive in the `hostname' */
	e = gfarm_host_address_get(hostname, gfarm_spool_server_port, &addr,
		NULL);
	if (e != NULL) {
		if (message != NULL)
			gflog_error("%s: host %s: %s", message, hostname, e);
		_exit(2);
	}

	e = gfs_client_connect(hostname, &addr, &conn);
	if (e != NULL) {
		/* if tolerant, we allow failure to connect the host */
		if (message != NULL && !tolerant)
			gflog_error("%s: connecting to %s: %s", message,
			    hostname, e);
		_exit(tolerant ? 0 : 3);
	}

	e = (*op)(conn, args);
	if (e != NULL) {
		/* if tolerant, we allow "no such file or directory" */
		if (message != NULL &&
		    (!tolerant || e != GFARM_ERR_NO_SUCH_OBJECT))
			gflog_error("%s on %s: %s", message, hostname, e);
		_exit(tolerant && e == GFARM_ERR_NO_SUCH_OBJECT ? 0 : 4);
	}

	e = gfs_client_disconnect(conn);
	if (e != NULL) {
		if (message != NULL)
			gflog_error("%s: disconnecting to %s: %s",
			    message, hostname, e);
		_exit(5);
	}

	_exit(0);
}

/* this is not an error, but a flag */
static char CHILD_IS_STILL_RUNNING[] = "(child still running)";

static char *
wait_pid(pid_t pid, int options, int *nhosts_succeed, char **statusp)
{
	int rv, status;

	while ((rv = waitpid(pid, &status, options)) == -1 && errno == EINTR)
		;
	if (rv == 0) /* WNOHANG case */
		return (CHILD_IS_STILL_RUNNING);
	if (rv == -1)
		return (gfarm_errno_to_error(errno));

	if (!WIFEXITED(status))
		*statusp = "operation aborted abnormally";
	else if (WEXITSTATUS(status) != 0)
		*statusp = "error happened on the operation";
	else {
		*statusp = NULL;
		(*nhosts_succeed)++;
	}
	return (NULL);
}

static char *
wait_pids(int *nprocsp, pid_t *pids, int *nhosts_succeed)
{
	char *e, *e2, *e_save;
	int i, nprocs = *nprocsp;

	assert(nprocs > 0);

	/*
	 * It's better to wait a first child which does exit(),
	 * but that's somewhat diffcult, as far as we won't steal
	 * any child which isn't in the pids[] array (i.e. a child
	 * that an application wants to wait(2)).
	 * Thus, instead of waiting the first child, we just wait pids[0].
	 */
	e = wait_pid(pids[0], 0, nhosts_succeed, &e2);
	if (e == NULL) {
		e_save = e2;
	} else {
		assert(e != CHILD_IS_STILL_RUNNING);
		e_save = e;
	}
	if (0 < --nprocs)
		pids[0] = pids[nprocs];

	/* sweep already exited children, if any. */
	for (i = 0; i < nprocs; ) {
		e = wait_pid(pids[i], WNOHANG, nhosts_succeed, &e2);
		if (e == CHILD_IS_STILL_RUNNING) {
			i++;
		} else {
			if (e == NULL) {
				if (e_save == NULL)
					e_save = e2;
			} else if (e_save == NULL)
				e_save = e;
			if (i < --nprocs) {
				pids[i] = pids[nprocs];
			}
		}
	}

	*nprocsp = nprocs;
	return (e_save);
}

#define CONCURRENCY	25

char *
gfs_client_apply_all_hosts(
	char *(*op)(struct gfs_connection *, void *),
	void *args, char *message, int tolerant, int *nhosts_succeed)
{
	char *e, *e_save = NULL;
	int i, nhosts, available_hosts_num, nprocs;
	pid_t pids[CONCURRENCY];
	struct gfarm_host_info *hosts;
	char **candidate_hosts, **available_hosts;

	e = gfarm_host_info_get_all(&nhosts, &hosts);
	if (e != NULL)
		return (e);
	GFARM_MALLOC_ARRAY(candidate_hosts, nhosts);
	GFARM_MALLOC_ARRAY(available_hosts, nhosts);
	if (candidate_hosts == NULL || available_hosts == NULL) {
		e_save = GFARM_ERR_NO_MEMORY;
	} else {
		for (i = 0; i < nhosts; i++)
			candidate_hosts[i] = hosts[i].hostname;
		available_hosts_num = nhosts;
		e_save = gfarm_schedule_search_idle_acyclic_hosts(
		    nhosts, candidate_hosts,
		    &available_hosts_num, available_hosts);
		if (e_save == NULL) {
			nprocs = 0;
			*nhosts_succeed = 0;
			for (i = 0; i < available_hosts_num; ) {
				pids[nprocs] = apply_one_host(op,
				    available_hosts[i], args,
				    message, tolerant);
				if (pids[nprocs] == -1) { /* fork error */
					if (errno != EAGAIN && errno != ENOMEM)
						break; /* seems to be fatal */
					if (nprocs <= 0) {
						e_save =
						   gfarm_errno_to_error(errno);
						break; /* really fatal */
					}
					e = wait_pids(&nprocs, pids,
					    nhosts_succeed);
					if (e_save == NULL)
						e_save = e;
					continue;
				}
				if (++nprocs >= CONCURRENCY) {
					e = wait_pids(&nprocs, pids,
					    nhosts_succeed);
					if (e_save == NULL)
						e_save = e;
				}
				++i;
			}
			while (nprocs > 0) {
				e = wait_pids(&nprocs, pids, nhosts_succeed);
				if (e_save == NULL)
					e_save = e;
			}
		}
	}
	if (available_hosts != NULL)
		free(available_hosts);
	if (candidate_hosts != NULL)
		free(candidate_hosts);

	gfarm_host_info_free_all(nhosts, hosts);
	return (e_save);
}
