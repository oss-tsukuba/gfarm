#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

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
	int size, n, in, out;
	struct thread_job *entries;
};

void
thrjobq_init(struct thread_jobq *q, int size)
{
	const char msg[] = "thrjobq_init";

	mutex_init(&q->mutex, msg, "thrjobq");
	cond_init(&q->nonempty, msg, "nonempty");
	cond_init(&q->nonfull, msg, "nonfull");
	q->size = size;
	q->n = q->in = q->out = 0;
	GFARM_MALLOC_ARRAY(q->entries, size);
	if (q->entries == NULL)
		gflog_fatal("%s: jobq size: %s", msg, strerror(ENOMEM));
}


void
thrjobq_add_job(struct thread_jobq *q, void *(*thread_main)(void *), void *arg)
{
	const char msg[] = "thrjobq_add_job";

	mutex_lock(&q->mutex, msg, "thrjobq");

	while (q->n >= q->size) {
		cond_wait(&q->nonfull, &q->mutex, msg, "nonfull");
	}
	q->entries[q->in].thread_main = thread_main;
	q->entries[q->in].arg = arg;
	q->in++;
	if (q->in >= q->size)
		q->in = 0;
	q->n++;
	cond_signal(&q->nonempty, msg, "nonempty");

	mutex_unlock(&q->mutex, msg, "thrjobq");
}

void
thrjobq_get_job(struct thread_jobq *q, struct thread_job *job)
{
	const char msg[] = "thrjobq_get_job";

	mutex_lock(&q->mutex, msg, "thrjobq");

	while (q->n <= 0) {
		cond_wait(&q->nonempty, &q->mutex, msg, "nonempty");
	}
	*job = q->entries[q->out++];
	if (q->out >= q->size)
		q->out = 0;
	q->n--;
	cond_signal(&q->nonfull, msg, "nonfull");

	mutex_unlock(&q->mutex, msg, "thrjobq");
}

#define THREAD_POOL_SIZE 4

struct thread_pool {
	pthread_mutex_t mutex;
	int threads;
	int idles;
	struct thread_jobq jobq;
} thrpool;

void
thrpool_init(void)
{
	const char msg[] = "thrpool_init";
	struct thread_pool *p = &thrpool;

	/*
	 * use THREAD_POOL_SIZE as jobq size
	 * to make threads in the pool be able to add jobs by themselves.
	 */
	thrjobq_init(&p->jobq, THREAD_POOL_SIZE * 2); /* XXX FIXME */

	mutex_init(&p->mutex, msg, "thrpool");
	p->threads = 0;
	p->idles = 0;
}

void *
thrpool_worker(void *arg)
{
	const char msg[] = "thrpool_worker";
	struct thread_pool *p = arg;
	struct thread_job job;

	for (;;) {
		mutex_lock(&p->mutex, msg, "to get job");
		p->idles++;
		mutex_unlock(&p->mutex, msg, "to get job");

		thrjobq_get_job(&p->jobq, &job);

		mutex_lock(&p->mutex, msg, "after job was gotten");
		p->idles--;
		mutex_unlock(&p->mutex, msg, "after job was gotten");

		(*job.thread_main)(job.arg);
	}
	/*NOTREACHED*/

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}


void
thrpool_add_job(void *(*thread_main)(void *), void *arg)
{
	const char msg[] = "thrpool_add_job";
	struct thread_pool *p = &thrpool;
	gfarm_error_t e;

	mutex_lock(&p->mutex, msg, "thrpool");
	if (p->threads < THREAD_POOL_SIZE && p->idles <= 0) {
		e = create_detached_thread(thrpool_worker, p);
		if (e == GFARM_ERR_NO_ERROR) {
			p->threads++;
		} else {
			gflog_warning("%s: create thread: %s\n",
			    msg, gfarm_error_string(e));
		}
	}
	mutex_unlock(&p->mutex, msg, "thrpool");

	thrjobq_add_job(&p->jobq, thread_main, arg);
}

void
thrpool_info(void)
{
	const char msg[] = "thrpool_info";
	struct thread_pool *p = &thrpool;
	int n, i;

	mutex_lock(&p->mutex, msg, "thrpool");
	n = p->threads;
	i = p->idles;
	mutex_unlock(&p->mutex, msg, "thrpool");

	gflog_info("number of worker threads: %d, idle threads: %d", n, i);
}
