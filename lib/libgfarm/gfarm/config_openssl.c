#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>

#include <gfarm/gfarm.h>
#include "config_openssl.h"

/*
 * CRYPTO_set_locking_callback() and CRYPTO_set_id_callback() are removed
 * since OpenSSL-1.1.0.
 */

#ifdef HAVE_CRYPTO_SET_LOCKING_CALLBACK

static pthread_mutex_t *config_openssl_mutexes;

static void
gfarm_openssl_lock(int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK)
		pthread_mutex_lock(&config_openssl_mutexes[n]);
	else
		pthread_mutex_unlock(&config_openssl_mutexes[n]);
}

#endif

#ifdef HAVE_CRYPTO_SET_ID_CALLBACK

static unsigned long
gfarm_openssl_threadid(void)
{
	/* XXX - check pthread_self() returns unsigned long or not */
	return ((unsigned long)pthread_self());
}

#endif

/*
 * this may be called again after gfarm_terminate(), in which we do
 * not destroy openssl_mutexes since it is considered to be sefe for
 * openssl to remain thread-safe.
 */
void
gfarm_openssl_initialize()
{
#ifdef HAVE_CRYPTO_SET_LOCKING_CALLBACK
	int num_locks, i;
	static const char diag[] = "gfarm_openssl_initialize";
#endif
	static int initialized = 0;

	if (initialized)
		return;
	SSL_library_init();

#ifdef HAVE_CRYPTO_SET_LOCKING_CALLBACK
	num_locks = CRYPTO_num_locks();
	GFARM_MALLOC_ARRAY(config_openssl_mutexes, num_locks);
	if (config_openssl_mutexes == NULL)
		gflog_fatal(GFARM_MSG_1004292, "%s: no memory", diag);
	for (i = 0; i < num_locks; ++i)
		pthread_mutex_init(&config_openssl_mutexes[i], NULL);
	CRYPTO_set_locking_callback(gfarm_openssl_lock);
#endif
#ifdef HAVE_CRYPTO_SET_ID_CALLBACK
	CRYPTO_set_id_callback(gfarm_openssl_threadid);
#endif
	initialized = 1;
}
