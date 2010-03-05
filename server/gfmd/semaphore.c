#include <pthread.h>

#include "thrsubr.h"
#include "semaphore.h"

static const char module[] = "semaphore";

void
semaphore_init(struct semaphore *sem, int count)
{
	static const char diag[] = "init";

	mutex_init(&sem->mutex, module, diag);
	cond_init(&sem->posted, module, diag);
	sem->count = count;
}

void
semaphore_post(struct semaphore *sem)
{
	static const char diag[] = "post";

	mutex_lock(&sem->mutex, module, diag);
	sem->count++;
	cond_signal(&sem->posted, module, diag);
	mutex_unlock(&sem->mutex, module, diag);
}

void
semaphore_wait(struct semaphore *sem)
{
	static const char diag[] = "wait";

	mutex_lock(&sem->mutex, module, diag);
	while (sem->count <= 0)
		cond_wait(&sem->posted, &sem->mutex, module, diag);
	--sem->count;
	mutex_unlock(&sem->mutex, module, diag);
}

int
semaphore_trywait(struct semaphore *sem)
{
	int locked;

	static const char diag[] = "trywait";

	mutex_lock(&sem->mutex, module, diag);
	if (sem->count <= 0) {
		locked = 0;
	} else {
		locked = 1;
		--sem->count;
	}
	mutex_unlock(&sem->mutex, module, diag);
	return (locked);
}

int
semaphore_is_waiting(struct semaphore *sem)
{
	int waiting;
	static const char diag[] = "waiting";

	mutex_lock(&sem->mutex, module, diag);
	waiting = (sem->count <= 0);
	mutex_unlock(&sem->mutex, module, diag);
	return (waiting);
}

void
semaphore_destroy(struct semaphore *sem)
{
	static const char diag[] = "destroy";

	mutex_destroy(&sem->mutex, module, diag);
	cond_destroy(&sem->posted, module, diag);
}
