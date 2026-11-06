/*
 * test of a race between search_idle() and another thread
 * which uses a gfsd connection.
 *
 * thread A schedules hosts by gfarm_schedule_select_host(), which
 * connects to gfsd and sends statfs via the multiplexed API without
 * conn_lock.  after `delay' seconds, the main thread acquires
 * a connection to the specified gfsd and calls an RPC on it.
 *
 * to make the window wide, stop the gfsd process which handles
 * the statfs of thread A (e.g. by gdb, see README) for a while,
 * which must be shorter than schedule_rpc_timeout.
 *
 * if the connection of search_idle() is reachable from the main thread
 * while the statfs is in progress, the main thread uses the same
 * connection concurrently, and waits until the gfsd resumes, and then
 * the requests and replies are mixed up.
 * otherwise, the main thread uses another connection, and finishes
 * immediately.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "gfm_client.h"
#include "gfs_client.h"
#include "lookup.h"
#include "schedule.h"

static struct gfm_connection *gfm_server;
static int nhosts;
static struct gfarm_host_sched_info *infos;
static struct timeval start;

static double
elapsed(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return ((now.tv_sec - start.tv_sec) +
	    (now.tv_usec - start.tv_usec) / 1000000.0);
}

static gfarm_error_t schedule_error;

static void *
schedule_thread(void *arg)
{
	char *host;
	int port;

	schedule_error = gfarm_schedule_select_host(gfm_server,
	    nhosts, infos, 1, &host, &port);
	if (schedule_error == GFARM_ERR_NO_ERROR) {
		printf("%6.2f: schedule: %s:%d\n", elapsed(), host, port);
		free(host);
	} else
		printf("%6.2f: schedule: %s\n", elapsed(),
		    gfarm_error_string(schedule_error));
	return (NULL);
}

static gfarm_error_t
rpc_on_gfsd(const char *host, int port)
{
	gfarm_error_t e;
	struct gfs_connection *gfs_server;
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;
	char path[] = ".";

	printf("%6.2f: rpc: acquiring %s:%d\n", elapsed(), host, port);
	/* this calls GFS_PROTO_PROCESS_SET, if it's a new connection */
	e = gfs_client_connection_and_process_acquire(&gfm_server,
	    host, port, &gfs_server, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		printf("%6.2f: rpc: acquire: %s\n", elapsed(),
		    gfarm_error_string(e));
		return (e);
	}
	printf("%6.2f: rpc: acquired, pid %lld\n", elapsed(),
	    (long long)gfs_client_pid(gfs_server));
	e = gfs_client_statfs(gfs_server, path, &bsize,
	    &blocks, &bfree, &bavail, &files, &ffree, &favail);
	if (e != GFARM_ERR_NO_ERROR)
		printf("%6.2f: rpc: statfs: %s\n", elapsed(),
		    gfarm_error_string(e));
	else
		printf("%6.2f: rpc: statfs: blocks %lld bavail %lld\n",
		    elapsed(), (long long)blocks, (long long)bavail);
	gfs_client_connection_free(gfs_server);
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e, e2;
	pthread_t thread;
	int c, err, delay = 2, port = 600;
	const char *host;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	while ((c = getopt(argc, argv, "d:p:")) != -1) {
		switch (c) {
		case 'd':
			delay = atoi(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		default:
			fprintf(stderr,
			    "usage: %s [-d delay] [-p port] gfsd_host\n",
			    argv[0]);
			return (2);
		}
	}
	if (optind + 1 != argc) {
		fprintf(stderr, "usage: %s [-d delay] [-p port] gfsd_host\n",
		    argv[0]);
		return (2);
	}
	host = argv[optind];

	e = gfm_client_connection_and_process_acquire_by_path(
	    GFARM_PATH_ROOT, &gfm_server);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfm connection: %s\n", gfarm_error_string(e));
		return (2);
	}
	e = gfarm_schedule_hosts_domain_all(GFARM_PATH_ROOT, "",
	    &nhosts, &infos);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "schedule_hosts_domain_all: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	gettimeofday(&start, NULL);
	err = pthread_create(&thread, NULL, schedule_thread, NULL);
	if (err != 0) {
		fprintf(stderr, "pthread_create: %s\n", strerror(err));
		return (2);
	}
	sleep(delay);
	e = rpc_on_gfsd(host, port);
	pthread_join(thread, NULL);

	gfarm_host_sched_info_free(nhosts, infos);
	gfm_client_connection_free(gfm_server);
	e2 = gfarm_terminate();
	if (e2 != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e2));

	return (e == GFARM_ERR_NO_ERROR && schedule_error == GFARM_ERR_NO_ERROR
	    ? 0 : 1);
}
