/**
 * @file  thrstatewait.c
 * @brief Utility to wait for another thread to finish a job.
 */
#include <stddef.h>
#include <pthread.h>

#include <gfarm/error.h>

#include "thrstatewait.h"
#include "thrsubr.h"

/**
 * Initialize a 'statewait' object.
 *
 * @param statewait  An object to be initialized.
 * @param diag       Title for diagnostics messages.
 */
void
gfarm_thr_statewait_initialize(struct gfarm_thr_statewait *statewait,
	const char *diag)
{
	static const char diag2[] = "gfarm_thr_statewait_initialize";

	gfarm_mutex_init(&statewait->mutex, diag, diag2);
	gfarm_cond_init(&statewait->arrived, diag, diag2);
	statewait->arrival = 0;
	statewait->result = GFARM_ERR_NO_ERROR;
}

/**
 * Wait until another thread finishes a job.
 *
 * @param statewait  A 'statewait' object.
 * @param diag       Title for diagnostics messages.
 * @return           Error code.
 *
 * It waits until another thread calls gfarm_thr_statewait_signal() for
 * the 'statewait' object.  Unlike pthread_cond_wait(), this function
 * never waits if gfarm_thr_statewait_signal() has been called prior to
 * gfarm_thr_statewait_wait().  In other words, the order of signal/wait
 * calls is not a matter.
 * 
 * It returns an error code set by gfarm_thr_statewait_signal().
 */
gfarm_error_t
gfarm_thr_statewait_wait(struct gfarm_thr_statewait *statewait,
	const char *diag)
{
	gfarm_error_t e;
	static const char diag2[] = "gfarm_thr_statewait_wait";

	gfarm_mutex_lock(&statewait->mutex, diag, diag2);
	while (!statewait->arrival)
		gfarm_cond_wait(&statewait->arrived, &statewait->mutex,
		    diag, diag2);
	e = statewait->result;
	gfarm_mutex_unlock(&statewait->mutex, diag, diag2);

	return (e);
}

/**
 * Notify finish of a job to another thread.
 *
 * @param statewait  A 'statewait' object.
 * @param e          The result status of the finished job.
 * @param diag       Title for diagnostics messages.
 *
 * A value of the argument 'e' will come to a return value of
 * gfarm_thr_statewait_wait().
 */
void
gfarm_thr_statewait_signal(struct gfarm_thr_statewait *statewait,
	gfarm_error_t e, const char *diag)
{
	static const char diag2[] = "gfarm_thr_statewait_signal";

	gfarm_mutex_lock(&statewait->mutex, diag, diag2);
	statewait->arrival = 1;
	statewait->result = e;
	gfarm_mutex_unlock(&statewait->mutex, diag, diag2);

	gfarm_cond_signal(&statewait->arrived, diag, diag2);
}

/**
 * Finalize a 'statewait' object.
 *
 * @param statewait  An object to be finalized.
 * @param diag       Title for diagnostics messages.
 */
void
gfarm_thr_statewait_terminate(struct gfarm_thr_statewait *statewait,
	const char *diag)
{
	static const char diag2[] = "gfarm_thr_statewait_terminate";

	gfarm_cond_destroy(&statewait->arrived, diag, diag2);
	gfarm_mutex_destroy(&statewait->mutex, diag, diag2);
}

/**
 * Reset a 'statewait' object.
 *
 * @param statewait  An object to be reset.
 * @param diag       Title for diagnostics messages.
 */
void
gfarm_thr_statewait_reset(struct gfarm_thr_statewait *statewait,
	const char *diag)
{
	static const char diag2[] = "gfarm_thr_statewait_reset";

	gfarm_mutex_lock(&statewait->mutex, diag, diag2);
	statewait->arrival = 0;
	statewait->result = GFARM_ERR_NO_ERROR;
	gfarm_mutex_unlock(&statewait->mutex, diag, diag2);
}
