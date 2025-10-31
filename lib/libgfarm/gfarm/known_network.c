#include <assert.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>

#include "context.h"
#include "hostspec.h"
#include "host.h" /* gfarm_get_ip_addresses() */
#include "known_network.h"

#define staticp	(gfarm_ctxp->known_network_static)

struct known_network {
	struct known_network *next;
	struct gfarm_hostspec *network;
};

struct gfarm_known_network_static {
	struct known_network *known_network_list;
	struct known_network **known_network_list_last;
};

gfarm_error_t
gfarm_known_network_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_known_network_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->known_network_list = NULL;
	s->known_network_list_last = &s->known_network_list;

	ctxp->known_network_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_known_network_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_known_network_static *s = ctxp->known_network_static;
	struct known_network *n, *next;

	if (s == NULL)
		return;

	for (n = s->known_network_list; n != NULL; n = next) {
		next = n->next;
		gfarm_hostspec_free(n->network);
		free(n);
	}
	free(s);
}

void
gfarm_known_network_list_dump(void)
{
	char network[GFARM_HOSTSPEC_STRLEN];
	struct known_network *n;

	for (n = staticp->known_network_list; n != NULL; n = n->next) {
		gfarm_hostspec_to_string(n->network, network, sizeof network);
		gflog_info(GFARM_MSG_1002445, "%s", network);
	}
}

gfarm_error_t
gfarm_known_network_list_add(struct gfarm_hostspec *network)
{
	struct known_network *known_network;

	GFARM_MALLOC(known_network);

	if (known_network == NULL)
		return (GFARM_ERR_NO_MEMORY);
	known_network->network = network;
	known_network->next = NULL;
	*staticp->known_network_list_last = known_network;
	staticp->known_network_list_last = &known_network->next;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_known_network_list_add_local_host(void)
{
	int count, i;
	struct in_addr *self_ip;
	gfarm_uint32_t addr_in, mask = 0xffffffff;
	struct gfarm_hostspec *net;
	gfarm_error_t e;

	e = gfarm_get_ip_addresses(&count, &self_ip);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < count; ++i) {
		addr_in = self_ip[i].s_addr;
		e = gfarm_hostspec_af_inet4_new(addr_in & mask, mask, &net);
		if (e == GFARM_ERR_NO_ERROR) {
			e = gfarm_known_network_list_add(net);
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
	}
	free(self_ip);
	return (e);
}

gfarm_error_t
gfarm_addr_network_get(struct sockaddr *addr,
	struct gfarm_hostspec **networkp)
{
	gfarm_uint32_t addr_in;
	struct known_network *n;
	struct gfarm_hostspec *network;
	gfarm_uint32_t mask;
	gfarm_error_t e;

	/* search in the known network list */
	for (n = staticp->known_network_list; n != NULL; n = n->next) {
		if (gfarm_hostspec_match(n->network, NULL, addr)) {
			if (networkp != NULL)
				*networkp = n->network;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	/* XXX - assume IPv4 class C network */
	assert(addr->sa_family == AF_INET);
	addr_in = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	mask = 0xffffff00;
	e = gfarm_hostspec_af_inet4_new(htonl(addr_in & mask), htonl(mask),
	    &network);
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfarm_known_network_list_add(network);
		if (e == GFARM_ERR_NO_ERROR)
			if (networkp != NULL)
				*networkp = network;
	}
	return (e);
}
