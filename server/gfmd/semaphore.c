#include <pthread.h>

#include "thrsubr.h"
#include "semaphore.h"

const char module[] = "semaphore";

void
semaphore_init(struct semaphore *sem, int count)
{
	mutex_init(&sem->mutex, module, "init");
	cond_init(&sem->posted, module, "init");
	sem->count = count;
}

void
semaphore_post(struct semaphore *sem)
{
	mutex_lock(&sem->mutex, module, "post");
	sem->count++;
	cond_signal(&sem->posted, module, "post");
	mutex_unlock(&sem->mutex, module, "post");
}

void
semaphore_wait(struct semaphore *sem)
{
	mutex_lock(&sem->mutex, module, "wait");
	while (sem->count <= 0)
		cond_wait(&sem->posted, &sem->mutex, module, "wait");
	--sem->count;
	mutex_unlock(&sem->mutex, module, "wait");
}

void
semaphore_destroy(struct semaphore *sem)
{
	mutex_destroy(&sem->mutex, module, "destroy");
	cond_destroy(&sem->posted, module, "destroy");
}
