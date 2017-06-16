#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "thrsubr.h"

#include "subr.h"
#include "thrpool.h"

struct thread_job {
	void *(*thread_main)(void *);
	void *arg;
};

struct thread_jobq {
	pthread_mutex_t mutex;
	pthread_cond_t nonfull, nonempty;
	int size, n, in, out, low_priority_limit;
	struct thread_job *entries;
};

void
thrjobq_init(struct thread_jobq *q, int size)
{
	static const char diag[] = "thrjobq_init";

	gfarm_mutex_init(&q->mutex, diag, "thrjobq");
	gfarm_cond_init(&q->nonempty, diag, "nonempty");
	gfarm_cond_init(&q->nonfull, diag, "nonfull");
	q->size = size;
	q->low_priority_limit = size;
	q->n = q->in = q->out = 0;
	GFARM_MALLOC_ARRAY(q->entries, size);
	if (q->entries == NULL)
		gflog_fatal(GFARM_MSG_1000220,
		    "%s: jobq size: %s", diag, strerror(ENOMEM));
}

void
thrjobq_set_jobq_low_priority_limit(struct thread_jobq *q, int limit)
{
	q->low_priority_limit = limit;
}

void
thrjobq_add_job(struct thread_jobq *q, int low_priority,
	void *(*thread_main)(void *), void *arg)
{
	static const char diag[] = "thrjobq_add_job";

	gfarm_mutex_lock(&q->mutex, diag, "thrjobq");

	while (q->n >= (low_priority ? q->low_priority_limit : q->size)) {
		gfarm_cond_wait(&q->nonfull, &q->mutex, diag, "nonfull");
	}
	q->entries[q->in].thread_main = thread_main;
	q->entries[q->in].arg = arg;
	q->in++;
	if (q->in >= q->size)
		q->in = 0;
	q->n++;
	gfarm_cond_signal(&q->nonempty, diag, "nonempty");

	gfarm_mutex_unlock(&q->mutex, diag, "thrjobq");
}

void
thrjobq_get_job(struct thread_jobq *q, struct thread_job *job)
{
	static const char diag[] = "thrjobq_get_job";

	gfarm_mutex_lock(&q->mutex, diag, "thrjobq");

	while (q->n <= 0) {
		gfarm_cond_wait(&q->nonempty, &q->mutex, diag, "nonempty");
	}
	*job = q->entries[q->out++];
	if (q->out >= q->size)
		q->out = 0;
	q->n--;
	gfarm_cond_broadcast(&q->nonfull, diag, "nonfull");

	gfarm_mutex_unlock(&q->mutex, diag, "thrjobq");
}

struct thread_pool {
	pthread_mutex_t mutex;
	int pool_size;
	int threads;
	int idles;
	struct thread_jobq jobq;

	const char *name;
	struct thread_pool *next;
};

static pthread_mutex_t all_thrpools_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct thread_pool *all_thrpools = NULL;

struct thread_pool *
thrpool_new(int pool_size, int queue_length, const char *pool_name)
{
	struct thread_pool *p;
	static const char diag[] = "thrpool_new";

	GFARM_MALLOC(p);
	if (p == NULL)
		return (NULL);

	thrjobq_init(&p->jobq, queue_length);

	gfarm_mutex_init(&p->mutex, diag, "thrpool");
	p->pool_size = pool_size;
	p->threads = 0;
	p->idles = 0;
	p->name = pool_name;

	gfarm_mutex_lock(&all_thrpools_mutex, diag, "all_thrpools add");
	p->next = all_thrpools;
	all_thrpools = p;
	gfarm_mutex_unlock(&all_thrpools_mutex, diag, "all_thrpools add");

	return (p);
}

void *
thrpool_worker(void *arg)
{
	static const char diag[] = "thrpool_worker";
	struct thread_pool *p = arg;
	struct thread_job job;

	for (;;) {
		gfarm_mutex_lock(&p->mutex, diag, "to get job");
		p->idles++;
		gfarm_mutex_unlock(&p->mutex, diag, "to get job");

		thrjobq_get_job(&p->jobq, &job);

		gfarm_mutex_lock(&p->mutex, diag, "after job was gotten");
		p->idles--;
		gfarm_mutex_unlock(&p->mutex, diag, "after job was gotten");

		(*job.thread_main)(job.arg);
	}
	/*NOTREACHED*/

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}


static void
thrpool_add_job0(struct thread_pool *p, int low_priority,
	void *(*thread_main)(void *), void *arg)
{
	static const char diag[] = "thrpool_add_job";
	gfarm_error_t e;

	gfarm_mutex_lock(&p->mutex, diag, "thrpool");
	if (p->threads < p->pool_size && p->idles <= 0) {
		e = create_detached_thread(thrpool_worker, p);
		if (e == GFARM_ERR_NO_ERROR) {
			p->threads++;
		} else {
			gflog_warning(GFARM_MSG_1003563,
			    "%s: create thread (currently %d out of %d "
			    "threads in %s): %s\n", diag, p->threads,
			    p->pool_size, p->name, gfarm_error_string(e));
		}
	}
	gfarm_mutex_unlock(&p->mutex, diag, "thrpool");

	thrjobq_add_job(&p->jobq, low_priority, thread_main, arg);
}

void
thrpool_add_job(struct thread_pool *p, void *(*thread_main)(void *), void *arg)
{
	thrpool_add_job0(p, 0, thread_main, arg);
}

void
thrpool_add_job_low_priority(struct thread_pool *p,
	void *(*thread_main)(void *), void *arg)
{
	thrpool_add_job0(p, 1, thread_main, arg);
}

void
thrpool_set_jobq_low_priority_limit(struct thread_pool *p, int n)
{
	thrjobq_set_jobq_low_priority_limit(&p->jobq, n);
}

void
thrpool_info(void)
{
	static const char diag[] = "thrpool_info";
	struct thread_pool *p;
	int n, i;
	const char *name;

	gfarm_mutex_lock(&all_thrpools_mutex, diag, "all_thrpools access");
	p = all_thrpools;
	gfarm_mutex_unlock(&all_thrpools_mutex, diag, "all_thrpools access");

	/* this implementation depends on that p->next will be never changed */
	for (; p != NULL; p = p->next) {
		gfarm_mutex_lock(&p->mutex, diag, "thrpool");
		n = p->threads;
		i = p->idles;
		name = p->name;
		gfarm_mutex_unlock(&p->mutex, diag, "thrpool");

		gflog_info(GFARM_MSG_1000222,
		    "pool %s: number of worker threads: %d, idle threads: %d",
		    name, n, i);
	}
}
