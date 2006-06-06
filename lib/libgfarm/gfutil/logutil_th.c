/*
 * $Id$
 */

#ifdef _REENTRANT

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "gfutil.h"
#include "logutil.h"

struct gflog_thread_specific {
	char *auxiliary_info;
};

static pthread_key_t gflog_key;
static int gflog_key_created = 0; /* to workaround a problem on SunOS 5.9 */

static void
gflog_thread_fatal(const char *diag, const char *status)
{
	if (gflog_syslog_enabled())
		syslog(LOG_ERR, "gfarm gflog: %s%s", diag, status);
	else
		fprintf(stderr, "gfarm gflog: %s%s\n", diag, status);
	exit(2);
}

static struct gflog_thread_specific *
gflog_thread_specific_alloc(void)
{
	struct gflog_thread_specific *p;

	p = malloc(sizeof(*p));
	if (p == NULL)
		gflog_thread_fatal("cannot allocate memory", "");

	/* initialization */
	p->auxiliary_info = NULL;

	return (p);
}

static void
gflog_key_create(void)
{
	int rv = pthread_key_create(&gflog_key, free);

	if (rv != 0)
		gflog_thread_fatal("pthread_key_create(): ", strerror(rv));
	gflog_key_created = 1; /* to workaround a problem on SunOS 5.9 */
}

static struct gflog_thread_specific *
gflog_thread_specific_get(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	int rv = pthread_once(&once, gflog_key_create);
	struct gflog_thread_specific *p;

	if (rv != 0)
		gflog_thread_fatal("pthread_once(): ", strerror(rv));

	/*
	 * to workaround a problem on SunOS 5.9:
	 * The following condition never can be true, if pthread_once() works
	 * correctly. But it becomes true on SunOS 5.9, if the command is not
	 * linked with pthread library.
	 * We don't protect `gflog_key_created' by a mutex, because this
	 * only becomes true on a non-threaded program.
	 */
	if (!gflog_key_created)
		gflog_key_create();

	p = pthread_getspecific(gflog_key);
	if (p != NULL)
		return (p);

	p = gflog_thread_specific_alloc();
	rv = pthread_setspecific(gflog_key, p);
	if (rv != 0)
		gflog_thread_fatal("pthread_setspecific(): ", strerror(rv));

	return (p);
}

char **
gflog_log_auxiliary_info_location()
{
	return (&gflog_thread_specific_get()->auxiliary_info);
}

#endif
