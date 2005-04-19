/*
 * $Id$
 */

void agent_lock(void);
void agent_unlock(void);

int agent_schedule(void *, void *(*)(void *));
