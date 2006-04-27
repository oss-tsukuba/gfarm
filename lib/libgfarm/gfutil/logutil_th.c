/*
 * $Id$
 */

#ifdef _REENTRANT

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "gfutil.h"
#include "logutil.h"

struct gflog_specific {
	char *auxiliary_info;
};

static pthread_key_t log_key;
static pthread_once_t log_key_once = PTHREAD_ONCE_INIT;

static void
gflog_pthread_key_init(void)
{
	struct gflog_specific *p = malloc(sizeof(struct gflog_specific));

	if (p == NULL) {
		if (gflog_syslog_enabled())
			syslog(LOG_ERR,
			    "gfarm gflog: cannot allocate %d bytes, aborted",
			    sizeof(*p));
		else
			fprintf(stderr,
			    "gfarm gflog: cannot allocate %d bytes, aborted\n",
			    sizeof(*p));
		exit(2);
	}

	/* initialization */
	p->auxiliary_info = NULL;

	pthread_key_create(&log_key, free);
	pthread_setspecific(log_key, p);
}

static struct gflog_specific *
gflog_pthread_getspecific(void)
{
	pthread_once(&log_key_once, gflog_pthread_key_init);
	return (pthread_getspecific(log_key));
}

char **
gflog_log_auxiliary_info_location()
{
	return (&gflog_pthread_getspecific()->auxiliary_info);
}

#endif
