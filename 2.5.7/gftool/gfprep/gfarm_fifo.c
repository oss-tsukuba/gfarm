/*
 * $Id$
 */

#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "thrsubr.h"

#include "gfarm_fifo.h"

struct gfarm_fifo {
	pthread_mutex_t mutex, mutex_in, mutex_out;
	pthread_cond_t nonempty, nonfull, finished;
	int n, in, out, quitting, quited, n_ents;
	void *ents;
	void *tmp;
	void (*set)(void *, int, void *);
	void (*get)(void *, int, void *);
};

gfarm_error_t
gfarm_fifo_init(gfarm_fifo_t **fifop, size_t num, size_t ent_size,
		void (*set)(void *, int, void *),
		void (*get)(void *, int, void *))
{
	static const char diag[] = "gfarm_fifo_init";
	gfarm_fifo_t *fifo;

	GFARM_MALLOC(fifo);
	if (fifo == NULL)
		return (GFARM_ERR_NO_MEMORY);
	fifo->n_ents = num;
	fifo->ents = gfarm_calloc_array(num, ent_size);
	if (fifo->ents == NULL) {
		free(fifo);
		return (GFARM_ERR_NO_MEMORY);
	}
	fifo->tmp = gfarm_calloc_array(1, ent_size);
	if (fifo->tmp == NULL) {
		free(fifo->ents);
		free(fifo);
		return (GFARM_ERR_NO_MEMORY);
	}

	gfarm_mutex_init(&fifo->mutex, diag, "mutex");
	gfarm_mutex_init(&fifo->mutex_in, diag, "mutex_in");
	gfarm_mutex_init(&fifo->mutex_out, diag, "mutex_out");
	gfarm_cond_init(&fifo->nonempty, diag, "nonempty");
	gfarm_cond_init(&fifo->nonfull, diag, "nonfull");
	gfarm_cond_init(&fifo->finished, diag, "finished");
	fifo->n = fifo->in = fifo->out = fifo->quitting = fifo->quited = 0;

	fifo->set = set;
	fifo->get = get;

	*fifop = fifo;

	return (GFARM_ERR_NO_ERROR);
}

/* for thread-1 */
gfarm_error_t
gfarm_fifo_wait_to_finish(gfarm_fifo_t *fifo)
{
	static const char diag[] = "gfarm_fifo_wait_to_finish";

	gfarm_mutex_lock(&fifo->mutex_in, diag, "mutex_in");
	gfarm_mutex_lock(&fifo->mutex, diag, "mutex");
	fifo->quitting = 1;
	while (!fifo->quited) {
		gfarm_cond_signal(&fifo->nonempty, diag, "nonempty");
		gfarm_cond_wait(&fifo->finished, &fifo->mutex, diag,
				"finished");
	}
	gfarm_mutex_unlock(&fifo->mutex, diag, "mutex");
	gfarm_mutex_unlock(&fifo->mutex_in, diag, "mutex_in");

	return (GFARM_ERR_NO_ERROR);
}

/* Need to call pthread_join() before this */
void
gfarm_fifo_free(gfarm_fifo_t *fifo)
{
	static const char diag[] = "gfarm_fifo_free";

	gfarm_mutex_destroy(&fifo->mutex, diag, "mutex");
	gfarm_mutex_destroy(&fifo->mutex_in, diag, "mutex_in");
	gfarm_mutex_destroy(&fifo->mutex_out, diag, "mutex_out");
	gfarm_cond_destroy(&fifo->nonempty, diag, "nonempty");
	gfarm_cond_destroy(&fifo->nonfull, diag, "nonfull");
	gfarm_cond_destroy(&fifo->finished, diag, "finished");
	free(fifo->ents);
	free(fifo->tmp);
	free(fifo);
}

/* for thread-1 */
gfarm_error_t
gfarm_fifo_enter(gfarm_fifo_t *fifo, void *entp)
{
	gfarm_error_t e;
	static const char diag[] = "gfarm_fifo_enter";

	gfarm_mutex_lock(&fifo->mutex_in, diag, "mutex_in");
	gfarm_mutex_lock(&fifo->mutex, diag, "mutex");
	if (fifo->quitting) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gfarm_cond_signal(&fifo->nonempty, diag, "nonempty");
	} else {
		e = GFARM_ERR_NO_ERROR;
		while (fifo->n >= fifo->n_ents) {
			gfarm_cond_wait(&fifo->nonfull, &fifo->mutex,
					diag, "nonfull");
		}
		fifo->set(fifo->ents, fifo->in, entp);
		fifo->in++;
		if (fifo->in >= fifo->n_ents)
			fifo->in = 0;
		fifo->n++;
		gfarm_cond_signal(&fifo->nonempty, diag, "nonempty");
	}
	gfarm_mutex_unlock(&fifo->mutex, diag, "mutex");
	gfarm_mutex_unlock(&fifo->mutex_in, diag, "mutex_in");

	return (e);
}

/* for thread-2 */
/* nonblocking check */
int
gfarm_fifo_can_get(gfarm_fifo_t *fifo)
{
	int retv;
	static const char diag[] = "gfarm_fifo_can_get";

	gfarm_mutex_lock(&fifo->mutex_out, diag, "mutex_out");
	gfarm_mutex_lock(&fifo->mutex, diag, "mutex");
	if (fifo->n > 0)
		retv = 1; /* can get */
	else if (fifo->quitting)
		retv = -1; /* finish */
	else
		retv = 0; /* empty */
	gfarm_mutex_unlock(&fifo->mutex, diag, "mutex");
	gfarm_mutex_unlock(&fifo->mutex_out, diag, "mutex_out");
	return (retv);
}

/* for thread-2 */
/* This do not delete from last pointer. */
/* Do not free members of entp. */
gfarm_error_t
gfarm_fifo_checknext(gfarm_fifo_t *fifo, void *entp)
{
	gfarm_error_t e;
	static const char diag[] = "gfarm_fifo_checknext";

	gfarm_mutex_lock(&fifo->mutex_out, diag, "mutex_out");
	gfarm_mutex_lock(&fifo->mutex, diag, "mutex");
	while (fifo->n <= 0 && !fifo->quitting)
		gfarm_cond_wait(&fifo->nonempty, &fifo->mutex, diag,
				"nonempty");
	if (fifo->n <= 0) {
		assert(fifo->quitting);
		fifo->quited = 1;
		gfarm_cond_signal(&fifo->finished, diag, "finished");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else { /* (fifo->n > 0) */
		e = GFARM_ERR_NO_ERROR;
		fifo->get(fifo->ents, fifo->out, entp);
	}
	gfarm_mutex_unlock(&fifo->mutex, diag, "mutex");
	gfarm_mutex_unlock(&fifo->mutex_out, diag, "mutex_out");
	return (e);
}

/* for thread-2 */
gfarm_error_t
gfarm_fifo_pending(gfarm_fifo_t *fifo)
{
	static const char diag[] = "gfarm_fifo_pending";

	gfarm_mutex_lock(&fifo->mutex_out, diag, "mutex_out");
	gfarm_mutex_lock(&fifo->mutex, diag, "mutex");
	if (fifo->n >= 2) {
		fifo->get(fifo->ents, fifo->out, fifo->tmp);
		fifo->set(fifo->ents, fifo->in, fifo->tmp);
		fifo->out++;
		fifo->in++;
		if (fifo->out >= fifo->n_ents)
			fifo->out = 0;
		if (fifo->in >= fifo->n_ents)
			fifo->in = 0;
	}
	gfarm_mutex_unlock(&fifo->mutex, diag, "mutex");
	gfarm_mutex_unlock(&fifo->mutex_out, diag, "mutex_out");
	return (GFARM_ERR_NO_ERROR);
}

/* for thread-2 */
/* Need to free members of entp */
gfarm_error_t
gfarm_fifo_delete(gfarm_fifo_t *fifo, void *entp)
{
	gfarm_error_t e;
	static const char diag[] = "gfarm_fifo_delete";

	gfarm_mutex_lock(&fifo->mutex_out, diag, "mutex_out");
	gfarm_mutex_lock(&fifo->mutex, diag, "mutex");
	while (fifo->n <= 0 && !fifo->quitting)
		gfarm_cond_wait(&fifo->nonempty, &fifo->mutex, diag,
				"nonempty");

	if (fifo->n <= 0) {
		assert(fifo->quitting);
		fifo->quited = 1;
		gfarm_cond_signal(&fifo->finished, diag, "finished");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else { /* (fifo->n > 0) */
		e = GFARM_ERR_NO_ERROR;
		fifo->get(fifo->ents, fifo->out, entp);
		fifo->out++;
		if (fifo->out >= fifo->n_ents)
			fifo->out = 0;
		if (fifo->n-- >= fifo->n_ents)
			gfarm_cond_signal(&fifo->nonfull, diag, "nonfull");
	}
	gfarm_mutex_unlock(&fifo->mutex, diag, "mutex");
	gfarm_mutex_unlock(&fifo->mutex_out, diag, "mutex_out");
	return (e);
}
