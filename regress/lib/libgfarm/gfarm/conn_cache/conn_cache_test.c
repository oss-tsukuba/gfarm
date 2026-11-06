/*
 * stress test of the connection cache (conn_cache.c).
 *
 * several threads acquire, use, purge and free cached connections
 * concurrently, with a small cache size to make the garbage collection
 * of free cached connections happen frequently.
 *
 * some threads free connections with an outer "scheduler" mutex held,
 * as gfarm_schedule_select_host() does via search_idle(), and some threads
 * acquire the outer mutex with conn_lock held, so that acquiring conn_lock
 * in the connection cache with the outer mutex held is detected as
 * a deadlock or a lock-order-inversion.
 *
 * this should be run with ThreadSanitizer or AddressSanitizer enabled
 * to detect data races and use-after-free.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "lru_cache.h"
#include "thrsubr.h"

#include "conn_cache.h"

#define FAKE_CONN_MAGIC		0x636f6e6e	/* "conn" */
#define FAKE_CONN_DISPOSED	0x64656164	/* "dead" */

struct fake_connection {
	struct gfp_cached_connection *cache_entry;
	int magic;
	int users; /* protected by conn_lock */
};

static int num_cache = 1;
static struct gfp_conn_cache cache;
static pthread_mutex_t sched_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *hosts[] = { "host0", "host1", "host2", "host3" };
#define NHOSTS	((int)(sizeof(hosts) / sizeof(hosts[0])))

static int niterations = 20000;
static int nthreads = 8;

static long created, disposed; /* atomic */

static void
fake_check(struct fake_connection *fc, const char *diag)
{
	if (fc->magic != FAKE_CONN_MAGIC) {
		fprintf(stderr, "%s: corrupted connection: magic=0x%x\n",
		    diag, fc->magic);
		abort();
	}
}

static gfarm_error_t
fake_connection_dispose(void *connection_data)
{
	struct fake_connection *fc = connection_data;

	fake_check(fc, "dispose");
	if (fc->users != 0) {
		fprintf(stderr, "dispose: connection in use: %d\n", fc->users);
		abort();
	}
	fc->magic = FAKE_CONN_DISPOSED;
	gfp_uncached_connection_dispose(fc->cache_entry);
	free(fc);
	__atomic_add_fetch(&disposed, 1, __ATOMIC_RELAXED);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
fake_connection_acquire(unsigned int *seedp, struct fake_connection **fcp)
{
	gfarm_error_t e;
	struct gfp_cached_connection *cache_entry;
	struct fake_connection *fc;
	int created_now;

	e = gfp_cached_connection_acquire(&cache,
	    hosts[rand_r(seedp) % NHOSTS], 600, "user",
	    &cache_entry, &created_now);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!created_now) {
		*fcp = gfp_cached_connection_get_data(cache_entry);
		return (GFARM_ERR_NO_ERROR);
	}
	if (rand_r(seedp) % 16 == 0) {
		/* simulate an authentication failure */
		gfp_cached_connection_initialization_aborted(&cache,
		    cache_entry);
		gfp_cached_or_uncached_connection_free(&cache, cache_entry);
		return (GFARM_ERR_AUTHENTICATION);
	}
	fc = malloc(sizeof(*fc));
	if (fc == NULL) {
		fprintf(stderr, "no memory\n");
		abort();
	}
	fc->cache_entry = cache_entry;
	fc->magic = FAKE_CONN_MAGIC;
	fc->users = 0;
	gfp_cached_connection_set_data(cache_entry, fc);
	__atomic_add_fetch(&created, 1, __ATOMIC_RELAXED);
	gfp_cached_connection_initialization_succeeded(&cache, cache_entry);
	*fcp = fc;
	return (GFARM_ERR_NO_ERROR);
}

static void
fake_connection_free(struct fake_connection *fc)
{
	gfp_cached_or_uncached_connection_free(&cache, fc->cache_entry);
}

/* similar to gfs_client_vrpc_result() */
static void
fake_rpc(unsigned int *seedp, struct fake_connection *fc)
{
	gfp_connection_lock(fc->cache_entry);
	fake_check(fc, "rpc");
	++fc->users;

	gfp_cached_connection_used(&cache, fc->cache_entry);
	(void)gfp_is_cached_connection(&cache, fc->cache_entry);
	if (rand_r(seedp) % 8 == 0) {
		/* simulate a connection error */
		/* acquire the outer mutex with conn_lock held */
		pthread_mutex_lock(&sched_mutex);
		pthread_mutex_unlock(&sched_mutex);
		gfp_cached_connection_purge_from_cache(&cache,
		    fc->cache_entry);
	}

	fake_check(fc, "rpc");
	--fc->users;
	gfp_connection_unlock(fc->cache_entry);
}

/* similar to search_idle() called from gfarm_schedule_select_host() */
static void
fake_schedule(unsigned int *seedp)
{
	struct fake_connection *fc;

	pthread_mutex_lock(&sched_mutex);
	if (fake_connection_acquire(seedp, &fc) == GFARM_ERR_NO_ERROR) {
		if (rand_r(seedp) % 8 == 0) {
			/* a multiplexed request fails without conn_lock */
			gfp_cached_connection_purge_from_cache(&cache,
			    fc->cache_entry);
		}
		fake_connection_free(fc);
	}
	pthread_mutex_unlock(&sched_mutex);
}

static void *
worker(void *arg)
{
	unsigned int seed = (unsigned int)(long)arg;
	struct fake_connection *fc;
	int i;

	for (i = 0; i < niterations; i++) {
		switch (rand_r(&seed) % 4) {
		case 0:
			fake_schedule(&seed);
			break;
		case 1:
			gfp_cached_connection_gc_all(&cache);
			break;
		default:
			if (fake_connection_acquire(&seed, &fc)
			    != GFARM_ERR_NO_ERROR)
				break;
			fake_rpc(&seed, fc);
			fake_connection_free(fc);
			break;
		}
	}
	return (NULL);
}

static void
timeout(int sig)
{
	static const char msg[] = "timeout, maybe deadlock\n";

	write(2, msg, sizeof(msg) - 1);
	_exit(1);
}

int
main(int argc, char **argv)
{
	pthread_t *threads;
	int c, i, err, timeout_sec = 300;

	while ((c = getopt(argc, argv, "c:n:t:T:")) != -1) {
		switch (c) {
		case 'c':
			num_cache = atoi(optarg);
			break;
		case 'n':
			niterations = atoi(optarg);
			break;
		case 't':
			nthreads = atoi(optarg);
			break;
		case 'T':
			timeout_sec = atoi(optarg);
			break;
		default:
			fprintf(stderr, "usage: %s [-c num_cache] "
			    "[-n iterations] [-t threads] [-T timeout]\n",
			    argv[0]);
			return (2);
		}
	}

	gfp_conn_cache_init(&cache, fake_connection_dispose, "connection",
	    NHOSTS * 2 + 1, &num_cache);

	signal(SIGALRM, timeout);
	alarm(timeout_sec);

	threads = malloc(sizeof(*threads) * nthreads);
	if (threads == NULL) {
		fprintf(stderr, "no memory\n");
		return (2);
	}
	for (i = 0; i < nthreads; i++) {
		err = pthread_create(&threads[i], NULL, worker,
		    (void *)(long)(i + 1));
		if (err != 0) {
			fprintf(stderr, "pthread_create: %s\n", strerror(err));
			return (2);
		}
	}
	for (i = 0; i < nthreads; i++)
		pthread_join(threads[i], NULL);
	alarm(0);

	gfp_cached_connection_gc_all(&cache);
	if (created != disposed) {
		fprintf(stderr, "leak: created %ld, disposed %ld\n",
		    created, disposed);
		return (1);
	}
	printf("created %ld, disposed %ld\n", created, disposed);
	return (0);
}
