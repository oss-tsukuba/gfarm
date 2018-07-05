#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "gfnetdb.h"

#define GFARM_HOST_ADDRESS_INTERNAL
#include "host_address.h"

void
gfarm_host_address_free(int addr_count, struct gfarm_host_address **addr_array)
{
	int i;

	for (i = 0; i < addr_count; i++)
		free(addr_array[i]);
	free(addr_array);
}

static gfarm_error_t
gfarm_host_address_get_internal(const char *name, int port, int extra_flags,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
	gfarm_error_t e;
	struct addrinfo hints, *res, *res0;
	int error;
	char sbuf[NI_MAXSERV];
	int af_not_supported = 0;
	int i, addr_count = 0;
	struct gfarm_host_address **addr_array, *sa;
	struct gfarm_host_address_ipv4 *sa_ipv4;
	struct gfarm_host_address_ipv6 *sa_ipv6;

	snprintf(sbuf, sizeof(sbuf), "%u", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM; /* XXX maybe used for SOCK_DGRAM */
	hints.ai_flags = AI_NUMERICSERV | extra_flags;
	error = gfarm_getaddrinfo(name, sbuf, &hints, &res0);
	if (error != 0) {
		gflog_debug(GFARM_MSG_UNFIXED, "Unknown host (%s): %s",
		    name, gfarm_error_string(GFARM_ERR_UNKNOWN_HOST));
		return (GFARM_ERR_UNKNOWN_HOST);
	}

	for (res = res0; res != NULL; res = res->ai_next) {
		if (res->ai_addr->sa_family != AF_INET &&
		    res->ai_addr->sa_family != AF_INET6) {
			af_not_supported = 1;
			continue;
		}
		++addr_count;
	}
	if (addr_count == 0) {
		if (af_not_supported) {
			e = GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "Address family not supported "
			    "by protocol family (%s): %s",
			    name, gfarm_error_string(e));
		} else {
			e = GFARM_ERR_NO_SUCH_OBJECT;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "failed to get host address (%s): %s",
			    name, gfarm_error_string(e));
		}
		gfarm_freeaddrinfo(res0);
		return (e);
	}

	GFARM_MALLOC_ARRAY(addr_array, addr_count);
	if (addr_array == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "host address (%s): %s",
		    name, gfarm_error_string(e));
		gfarm_freeaddrinfo(res0);
		return (e);
	}

	i = 0;
	for (res = res0; res != NULL; res = res->ai_next) {
		switch (res->ai_addr->sa_family) {
		case AF_INET:
			GFARM_MALLOC(sa_ipv4);
			if (sa_ipv4 == NULL) {
				sa = NULL;
				break;
			}
			memset(sa_ipv4, 0, sizeof(*sa_ipv4));
			memcpy(&sa_ipv4->sa_addr, res->ai_addr,
			    sizeof(sa_ipv4->sa_addr));
			sa_ipv4->sa_family = AF_INET;
			sa_ipv4->sa_addrlen = sizeof(sa_ipv4->sa_addr);
			sa = (struct gfarm_host_address *)sa_ipv4;
			break;
		case AF_INET6:
			GFARM_MALLOC(sa_ipv6);
			if (sa_ipv6 == NULL) {
				sa = NULL;
				break;
			}
			memset(sa_ipv6, 0, sizeof(*sa_ipv6));
			memcpy(&sa_ipv6->sa_addr, res->ai_addr,
			    sizeof(sa_ipv6->sa_addr));
			sa_ipv6->sa_family = AF_INET6;
			sa_ipv6->sa_addrlen = sizeof(sa_ipv6->sa_addr);
			sa = (struct gfarm_host_address *)sa_ipv6;
			break;
		default:
			/* i.e. af_not_supported */
			continue;
		}
		if (sa == NULL || i >= addr_count) {
			if (sa == NULL) {
				e = GFARM_ERR_NO_MEMORY;
			} else {
				e = GFARM_ERR_INTERNAL_ERROR;
				free(sa);
			}
			gfarm_host_address_free(i, addr_array);
			gflog_debug(GFARM_MSG_UNFIXED,
			    "host address (%s): %s",
			    name, gfarm_error_string(e));
			gfarm_freeaddrinfo(res0);
			return (e);
		}
		addr_array[i++] = sa;
	}
	assert(i > 0);
	gfarm_freeaddrinfo(res0);
	*addr_countp = i;
	*addr_arrayp = addr_array;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_host_address_get(const char *name, int port,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
	return (gfarm_host_address_get_internal(name, port, 0,
	    addr_countp, addr_arrayp));
}

gfarm_error_t
gfarm_passive_address_get(const char *name, int port,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
	return (gfarm_host_address_get_internal(name, port, AI_PASSIVE,
	    addr_countp, addr_arrayp));
}

gfarm_error_t
gfarm_sockaddr_get_port(struct sockaddr *sa, socklen_t sa_len, int *portp)
{
	struct sockaddr_in *sa_ipv4;
	struct sockaddr_in6 *sa_ipv6;
	int gai_err;
	char sbuf[NI_MAXSERV];

	switch (sa->sa_family) {
	case AF_INET:
		sa_ipv4 = (struct sockaddr_in *)sa;
		*portp = (int)ntohs(sa_ipv4->sin_port);
		return (GFARM_ERR_NO_ERROR);
	case AF_INET6:
		sa_ipv6 = (struct sockaddr_in6 *)sa;
		*portp = (int)ntohs(sa_ipv6->sin6_port);
		return (GFARM_ERR_NO_ERROR);
	default:
		/* verbose, but this code shouldn't be executed */
		gflog_notice(GFARM_MSG_UNFIXED, "unknown address family %d",
		    sa->sa_family);
		gai_err = gfarm_getnameinfo(sa, sa_len,
		    NULL, 0, sbuf, sizeof(sbuf), NI_NUMERICSERV);
		if (gai_err != 0) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "unknown address family %d; getnameinfo(): %s",
			    sa->sa_family, gai_strerror(gai_err));
			return (GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY);
		}
		*portp = atoi(sbuf);
		return (GFARM_ERR_NO_ERROR);
	}
}
