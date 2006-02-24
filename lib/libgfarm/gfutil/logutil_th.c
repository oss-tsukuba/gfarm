/*
 * $Id$
 */

#ifdef _REENTRANT

#include <stdlib.h>
#include <pthread.h>
#include "logutil.h"

struct log_key {
	char *identifier;
	char *auxiliary_info;
	int use_syslog;
	int verbose;
};

static pthread_key_t log_key;
static pthread_once_t log_key_once = PTHREAD_ONCE_INIT;

static void
gflog_pthread_key_create()
{
	pthread_key_create(&log_key, free);
}		

static void
gflog_pthread_key_init()
{
	pthread_once(&log_key_once, gflog_pthread_key_create);
	pthread_setspecific(log_key, malloc(sizeof(struct log_key)));

	/* initialization */
	log_identifier = "libgfarm";
	log_auxiliary_info = NULL;
	log_use_syslog = 0;
	authentication_verbose = 0;
}

static void *
gflog_pthread_getspecific(pthread_key_t key)
{
	void *r;

	r = pthread_getspecific(key);
	if (r == NULL) {
		gflog_pthread_key_init();
		r = pthread_getspecific(key);
	}
	return (r);
}

char **
gflog_log_identifier_location()
{
	struct log_key *key = gflog_pthread_getspecific(log_key);
	return (&key->identifier);
}

char **
gflog_log_auxiliary_info_location()
{
	struct log_key *key = gflog_pthread_getspecific(log_key);
	return (&key->auxiliary_info);
}

int *
gflog_log_use_syslog_location()
{
	struct log_key *key = gflog_pthread_getspecific(log_key);
	return (&key->use_syslog);
}

int *
gflog_authentication_verbose_location()
{
	struct log_key *key = gflog_pthread_getspecific(log_key);
	return (&key->verbose);
}

#endif
