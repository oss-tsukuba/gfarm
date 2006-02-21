/*
 * $Id$
 */

#include <stdlib.h>
#include <pthread.h>
#include "agent_ptable.h"

#define PTABLE_LEN	4096
#define PTABLE_OUT_OF_RANGE(p)	((p) < 0 || (p) >= PTABLE_LEN)

static pthread_key_t ptable_key;
static pthread_key_t ptable_free_ptr_key;
static pthread_once_t ptable_key_once = PTHREAD_ONCE_INIT;

static void
agent_ptable_key_alloc()
{
	pthread_key_create(&ptable_key, free);
	pthread_key_create(&ptable_free_ptr_key, free);
}		

static void
agent_ptable_free_ptr_set(int p)
{
	int *free_ptr;

	free_ptr = pthread_getspecific(ptable_free_ptr_key);
	*free_ptr = p;
}

static int
agent_ptable_free_ptr_get(void)
{
	int *free_ptr;

	free_ptr = pthread_getspecific(ptable_free_ptr_key);
	return (*free_ptr);
}

void
agent_ptable_alloc(void)
{
	pthread_once(&ptable_key_once, agent_ptable_key_alloc);
	pthread_setspecific(ptable_key, calloc(PTABLE_LEN, sizeof(void *)));
	pthread_setspecific(ptable_free_ptr_key, malloc(sizeof(int)));
	/* free pointer begins with 1 */
	agent_ptable_free_ptr_set(1);
}

static int
agent_ptable_entry_set(int p, void *ptr)
{
	void **ptable;

	/* adjust the range from [1:PTABLE_LEN] to [0:PTABLE_LEN-1] */
	p = p - 1;
	if (PTABLE_OUT_OF_RANGE(p))
		return (-1);

	ptable = pthread_getspecific(ptable_key);
	ptable[p] = ptr;
	return (0);
}

void *
agent_ptable_entry_get(int p)
{
	void **ptable;

	/* adjust the range from [1:PTABLE_LEN] to [0:PTABLE_LEN-1] */
	p = p - 1;
	if (PTABLE_OUT_OF_RANGE(p))
		return (0);

	ptable = pthread_getspecific(ptable_key);
	return (ptable[p]);
}

int
agent_ptable_entry_add(void *ptr)
{
	int saved_ptr, free_ptr;

	free_ptr = saved_ptr = agent_ptable_free_ptr_get();

	if (agent_ptable_entry_get(free_ptr))
		return (-1);

	agent_ptable_entry_set(free_ptr, ptr);
	while (agent_ptable_entry_get(++free_ptr));
	agent_ptable_free_ptr_set(free_ptr);

	return (saved_ptr);
}

int
agent_ptable_entry_delete(int p)
{
	int free_ptr;

	if (agent_ptable_entry_get(p))
		agent_ptable_entry_set(p, 0);
	else
		return (-1);

	free_ptr = agent_ptable_free_ptr_get();
	if (p < free_ptr)
		agent_ptable_free_ptr_set(p);

	return (0);
}
