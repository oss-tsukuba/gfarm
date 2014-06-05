#include <pthread.h>
#include <stddef.h>
#include <netdb.h>

#include <gfarm/gfarm_config.h>

#include "thrsubr.h"
#include "gfnetdb.h"

#ifndef HAVE_MTSAFE_NETDB
static pthread_mutex_t netdb_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char mutex_name[] = "netdb_mutex";
#endif

int
gfarm_getaddrinfo(const char *hostname,
	const char *servname,
	const struct addrinfo *hints,
	struct addrinfo **res)
{
	int rv;

#ifndef HAVE_MTSAFE_NETDB
	static const char diag[] = "gfarm_getaddrinfo";

	gfarm_mutex_lock(&netdb_mutex, diag, mutex_name);
#endif
	rv = getaddrinfo(hostname, servname, hints, res);
#ifndef HAVE_MTSAFE_NETDB
	gfarm_mutex_unlock(&netdb_mutex, diag, mutex_name);
#endif
	return (rv);
}

void
gfarm_freeaddrinfo(struct addrinfo *ai)
{
#ifndef HAVE_MTSAFE_NETDB
	static const char diag[] = "gfarm_freeaddrinfo";

	gfarm_mutex_lock(&netdb_mutex, diag, mutex_name);
#endif
	freeaddrinfo(ai);
#ifndef HAVE_MTSAFE_NETDB
	gfarm_mutex_unlock(&netdb_mutex, diag, mutex_name);
#endif
}

int
gfarm_getnameinfo(const struct sockaddr *sa, socklen_t salen,
	char *host, size_t hostlen, char *serv,
	size_t servlen, int flags)
{
	int rv;
#ifndef HAVE_MTSAFE_NETDB
	static const char diag[] = "gfarm_getnameinfo";

	gfarm_mutex_lock(&netdb_mutex, diag, mutex_name);
#endif
	rv = getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
#ifndef HAVE_MTSAFE_NETDB
	gfarm_mutex_unlock(&netdb_mutex, diag, mutex_name);
#endif
	return (rv);
}
