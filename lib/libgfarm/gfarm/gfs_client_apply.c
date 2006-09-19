#include <sys/socket.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "host.h"
#include "config.h"
#include "gfs_client.h"
#include "gfs_misc.h"

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
			fprintf(stderr, "%s: host %s: %s\n",
			    message, hostname, e);
		_exit(2);
	}

	e = gfs_client_connect(hostname, &addr, &conn);
	if (e != NULL) {
		/* if tolerant, we allow failure to connect the host */
		if (message != NULL && !tolerant)
			fprintf(stderr, "%s: connecting to %s: %s\n", message,
			    hostname, e);
		_exit(tolerant ? 0 : 3);
	}

	e = (*op)(conn, args);
	if (e != NULL) {
		/* if tolerant, we allow "no such file or directory" */
		if (message != NULL &&
		    (!tolerant || e != GFARM_ERR_NO_SUCH_OBJECT))
			fprintf(stderr, "%s on %s: %s\n", message, hostname, e);
		_exit(tolerant && e == GFARM_ERR_NO_SUCH_OBJECT ? 0 : 4);
	}

	e = gfs_client_disconnect(conn);
	if (e != NULL) {
		if (message != NULL)
			fprintf(stderr, "%s: disconnecting to %s: %s\n",
			    message, hostname, e);
		_exit(5);
	}

	_exit(0);
}

static char *
wait_pid(int pids[], int num, int *nhosts_succeed)
{
	char *e;
	int rv, s;

	e = NULL;
	while (--num >= 0) {
		while ((rv = waitpid(pids[num], &s, 0)) == -1 &&
		        errno == EINTR)
				;
		if (rv == -1) {
			if (e == NULL)
				e = gfarm_errno_to_error(errno);
		} else if (WIFEXITED(s)) {
			if (WEXITSTATUS(s) == 0)
				(*nhosts_succeed)++;
			else
				e = "error happened on the operation";
		} else {
			e = "operation aborted abnormally";
		}
	}
	return (e);
}

#define CONCURRENCY	25

char *
gfs_client_apply_all_hosts(
	char *(*op)(struct gfs_connection *, void *),
	void *args, char *message, int tolerant, int *nhosts_succeed)
{
	char *e;
	int i, j, nhosts, pids[CONCURRENCY];
	struct gfarm_host_info *hosts;

	e = gfarm_host_info_get_all(&nhosts, &hosts);
	if (e != NULL)
		return (e);

        j = 0;
	*nhosts_succeed = 0;
        for (i = 0; i < nhosts; i++) {
                pids[j] = apply_one_host(op, hosts[i].hostname, args,
		    message, tolerant);
                if (pids[j] < 0) /* fork error */
                        break;
                if (++j == CONCURRENCY) {
                        e = wait_pid(pids, j, nhosts_succeed);
                        j = 0;
                }
        }
        if (j > 0)
                e = wait_pid(pids, j, nhosts_succeed);

	gfarm_host_info_free_all(nhosts, hosts);
	return (e);
}
