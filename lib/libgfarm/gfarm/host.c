/*
 * $Id$
 */

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#define _SOCKADDR_LEN /* for __osf__ */
#include <sys/socket.h>
#include <sys/uio.h>
#define BSD_COMP /* for __svr4__ */
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/un.h> /* for SUN_LEN */
#include <errno.h>

#include <unistd.h>
#include <netdb.h>

#include <gfarm/gfarm.h>
#include "hostspec.h"
#include "host.h"

#include "metadb_server.h" /* XXX FIXME this shouldn't be needed here */

gfarm_error_t
gfarm_host_info_get_by_if_hostname(const char *if_hostname,
	struct gfarm_host_info *info)
{
	gfarm_error_t e;
	struct hostent *hp;
	int i;
	char *n;

	e = gfarm_host_info_get_by_name_alias(if_hostname, info);
	if (e == GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);

	hp = gethostbyname(if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (GFARM_ERR_UNKNOWN_HOST);
	for (i = 0, n = hp->h_name; n != NULL; n = hp->h_aliases[i++]){
		if (gfarm_host_info_get_by_name_alias(n, info) ==
		    GFARM_ERR_NO_ERROR)
			return (GFARM_ERR_NO_ERROR);
	}
	return (e);
}

/*
 * The value returned to `*canonical_hostnamep' should be freed.
 */
gfarm_error_t
gfarm_host_get_canonical_name(const char *hostname,
	char **canonical_hostnamep, int *portp)
{
	gfarm_error_t e;
	struct gfarm_host_info info;
	int port;
	char *n;

	e = gfarm_host_info_get_by_if_hostname(hostname, &info);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	n = strdup(info.hostname);
	port = info.port;
	gfarm_host_info_free(&info);
	if (n == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*canonical_hostnamep = n;
	*portp = port;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_host_get_canonical_names(int nhosts, char **hosts,
	char ***canonical_hostnamesp, int **portsp)
{
	gfarm_error_t e;
	int i;
	char **canonical_hostnames;
	int *ports;

	GFARM_MALLOC_ARRAY(canonical_hostnames, nhosts);
	if (canonical_hostnames == NULL)
		return (GFARM_ERR_NO_MEMORY);
	GFARM_MALLOC_ARRAY(ports, nhosts);
	if (ports == NULL) {
		free(canonical_hostnames);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; i < nhosts; i++) {
		e = gfarm_host_get_canonical_name(hosts[i],
		    &canonical_hostnames[i], &ports[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			while (--i >= 0)
				free(canonical_hostnames[i]);
			free(canonical_hostnames);
			free(ports);
			return (e);
		}
	}
	*canonical_hostnamesp = canonical_hostnames;
	*portsp = ports;
	return (GFARM_ERR_NO_ERROR);
}

char *
gfarm_host_get_self_name(void)
{
	static int initialized;
	static char hostname[MAXHOSTNAMELEN + 1];

	if (!initialized) {
		hostname[0] = hostname[MAXHOSTNAMELEN] = 0;
		/* gethostname(2) almost shouldn't fail */
		gethostname(hostname, MAXHOSTNAMELEN);
		if (hostname[0] == '\0')
			strcpy(hostname, "hostname-not-set");
		initialized = 1;
	}

	return (hostname);
}

/*
 * shouldn't free the return value of this function.
 *
 * NOTE: gfarm_error_initialize() and gfarm_metadb_initialize()
 *	should be called before this function.
 */
gfarm_error_t
gfarm_host_get_canonical_self_name(char **canonical_hostnamep, int *portp)
{
	gfarm_error_t e;
	static char *canonical_self_name = NULL;
	static int port;
	static gfarm_error_t error_save = GFARM_ERR_NO_ERROR;

	if (canonical_self_name == NULL) {
		if (error_save != GFARM_ERR_NO_ERROR)
			return (error_save);
		e = gfarm_host_get_canonical_name(gfarm_host_get_self_name(),
		    &canonical_self_name, &port);
		if (e != GFARM_ERR_NO_ERROR) {
			error_save = e;
			return (e);
		}
	}
	*canonical_hostnamep = canonical_self_name;
	*portp = port;
	return (GFARM_ERR_NO_ERROR);
}


#if 0 /* not yet in gfarm v2 */

static int
host_address_is_match(struct gfarm_hostspec *hostspec,
	const char *name, struct hostent *hp)
{
	struct sockaddr_in peer_addr_in;
	struct sockaddr *peer_addr = (struct sockaddr *)&peer_addr_in;
	int i, j;
	const char *n;

	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (gfarm_hostspec_match(hostspec, name, NULL));

	memset(&peer_addr_in, 0, sizeof(peer_addr_in));
	peer_addr_in.sin_port = 0;
	peer_addr_in.sin_family = hp->h_addrtype;
	for (i = 0; hp->h_addr_list[i] != NULL; i++) {
		memcpy(&peer_addr_in.sin_addr, hp->h_addr_list[i],
		    sizeof(peer_addr_in.sin_addr));
		if (gfarm_hostspec_match(hostspec, name, peer_addr))
			return (1);
		if (gfarm_hostspec_match(hostspec, hp->h_name, peer_addr))
			return (1);
		for (j = 0; (n = hp->h_aliases[j]) != NULL; j++) {
			if (gfarm_hostspec_match(hostspec, n, peer_addr))
				return (1);
		}
	}
	return (0);
}

struct gfarm_client_architecture_config {
	struct gfarm_client_architecture_config *next;

	char *architecture;
	struct gfarm_hostspec *hostspec;
};

struct gfarm_client_architecture_config
	*gfarm_client_architecture_config_list = NULL;
struct gfarm_client_architecture_config
	**gfarm_client_architecture_config_last =
	    &gfarm_client_architecture_config_list;

gfarm_error_t
gfarm_set_client_architecture(char *architecture, struct gfarm_hostspec *hsp)
{
	struct gfarm_client_architecture_config *cacp;

	GFARM_MALLOC(cacp);
	if (cacp == NULL)
		return (GFARM_ERR_NO_MEMORY);

	cacp->architecture = strdup(architecture);
	if (cacp->architecture == NULL) {
		free(cacp);
		return (GFARM_ERR_NO_MEMORY);
	}
	cacp->hostspec = hsp;
	cacp->next = NULL;

	*gfarm_client_architecture_config_last = cacp;
	gfarm_client_architecture_config_last = &cacp->next;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_get_client_architecture(const char *client_name, char **architecturep)
{
	struct hostent *hp;
	struct gfarm_client_architecture_config *cacp =
	    gfarm_client_architecture_config_list;

	if (cacp == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	hp = gethostbyname(client_name);
	for (; cacp != NULL; cacp = cacp->next) {
		if (host_address_is_match(cacp->hostspec, client_name, hp)) {
			*architecturep = cacp->architecture;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_NO_SUCH_OBJECT);
}


/*
 * shouldn't free the return value of this function.
 *
 * NOTE: gfarm_error_initialize() and gfarm_metadb_initialize()
 *	should be called before this function.
 */
gfarm_error_t
gfarm_host_get_self_architecture(char **architecture)
{
	gfarm_error_t e;
	char *canonical_self_name;
	static char *self_architecture = NULL;
	static gfarm_error_t error_save = GFARM_ERR_NO_ERROR;

	if (self_architecture == NULL) {
		if (error_save != GFARM_ERR_NO_ERROR)
			return (error_save);

		if ((self_architecture = getenv("GFARM_ARCHITECTURE"))!= NULL){
			/* do nothing */
		} else if ((e = gfarm_host_get_canonical_self_name(
		    &canonical_self_name)) == GFARM_ERR_NO_ERROR) {
			/* filesystem node case */
			self_architecture =
			    gfarm_host_info_get_architecture_by_host(
			    canonical_self_name);
			if (self_architecture == NULL) {
				error_save = GFARM_ERR_NO_SUCH_OBJECT;
				return (error_save);
			}
		} else if ((e = gfarm_get_client_architecture(
		    gfarm_host_get_self_name(), &self_architecture)) ==
		    GFARM_ERR_NO_ERROR) {
			/* client case */
			/* do nothing */
		} else {
			error_save = e;
			return (e);
		}
	}
	*architecture = self_architecture;
	return (GFARM_ERR_NO_ERROR);
}

#endif /* not yet in gfarm v2 */

int
gfarm_canonical_hostname_is_local(const char *canonical_hostname)
{
	gfarm_error_t e;
	char *self_name;
	int port;

	e = gfarm_host_get_canonical_self_name(&self_name, &port);
	if (e != GFARM_ERR_NO_ERROR)
		self_name = gfarm_host_get_self_name();
	return (strcasecmp(canonical_hostname, self_name) == 0);
}

int
gfarm_host_is_local(const char *hostname)
{
	gfarm_error_t e;
	char *canonical_hostname;
	int is_local, port;

	e = gfarm_host_get_canonical_name(hostname,
		&canonical_hostname, &port);
	is_local = gfarm_canonical_hostname_is_local(canonical_hostname);
	if (e == GFARM_ERR_NO_ERROR)
		free(canonical_hostname);
	return (is_local);
}

#ifdef SUN_LEN
# ifndef linux
#  define NEW_SOCKADDR /* 4.3BSD-Reno or later */
# endif
#endif

#define ADDRESSES_DELTA 16
#define IFCBUFFER_SIZE	8192

gfarm_error_t
gfarm_get_ip_addresses(int *countp, struct in_addr **ip_addressesp)
{
	gfarm_error_t e = GFARM_ERR_NO_MEMORY;
	int fd;
	int size, count;
#ifdef NEW_SOCKADDR
	int i;
#endif
	struct in_addr *addresses, *p;
	struct ifreq *ifr; /* pointer to interface address */
	struct ifconf ifc; /* buffer for interface addresses */
	char ifcbuffer[IFCBUFFER_SIZE];
	struct ifreq ifreq; /* buffer for interface flag */

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return (gfarm_errno_to_error(errno));
	ifc.ifc_len = sizeof(ifcbuffer);
	ifc.ifc_buf = ifcbuffer;
	if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
		e = gfarm_errno_to_error(errno);
		close(fd);
		return (e);
	}

	count = 0;
	size = 2; /* ethernet address + loopback interface address */
	GFARM_MALLOC_ARRAY(addresses,  size);
	if (addresses == NULL)
		goto err;

#ifdef NEW_SOCKADDR
	ifreq.ifr_name[0] = '\0';

	for (i = 0; i < ifc.ifc_len; )
#else
	for (ifr = ifc.ifc_req; (char *)ifr < ifc.ifc_buf+ifc.ifc_len; ifr++)
#endif
	{
#ifdef NEW_SOCKADDR
		ifr = (struct ifreq *)((char *)ifc.ifc_req + i);
		i += sizeof(ifr->ifr_name) +
			((ifr->ifr_addr.sa_len > sizeof(struct sockaddr) ?
			  ifr->ifr_addr.sa_len : sizeof(struct sockaddr)));
#endif
		if (ifr->ifr_addr.sa_family != AF_INET)
			continue;
#ifdef NEW_SOCKADDR
		if (strncmp(ifreq.ifr_name, ifr->ifr_name,
			    sizeof(ifr->ifr_name)) != 0)
#endif
		{
			/* if this is first entry of the interface, get flag */
			ifreq = *ifr;
			if (ioctl(fd, SIOCGIFFLAGS, &ifreq) < 0) {
				e = gfarm_errno_to_error(errno);
				goto err;
			}
		}
		if ((ifreq.ifr_flags & IFF_UP) == 0)
			continue;

		if (count + 1 > size) {
			size += ADDRESSES_DELTA;
			GFARM_REALLOC_ARRAY(p, addresses, size);
			if (p == NULL)
				goto err;
			addresses = p;
		}
		addresses[count++] =
			((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;

	}
	if (count == 0) {
		free(addresses);
		addresses = NULL;
	} else if (size != count) {
		GFARM_REALLOC_ARRAY(p, addresses, count);
		if (p == NULL)
			goto err;
		addresses = p;
	}
	*ip_addressesp = addresses;
	*countp = count;
	close(fd);
	return (GFARM_ERR_NO_ERROR);

err:
	if (addresses != NULL)
		free(addresses);
	close(fd);
	return (e);
}

struct gfarm_host_address_use_config {
	struct gfarm_host_address_use_config *next;

	struct gfarm_hostspec *hostspec;
};

struct gfarm_host_address_use_config *gfarm_host_address_use_config_list =
    NULL;
struct gfarm_host_address_use_config **gfarm_host_address_use_config_last =
    &gfarm_host_address_use_config_list;

gfarm_error_t
gfarm_host_address_use(struct gfarm_hostspec *hsp)
{
	struct gfarm_host_address_use_config *haucp;

	GFARM_MALLOC(haucp);
	if (haucp == NULL)
		return (GFARM_ERR_NO_MEMORY);

	haucp->hostspec = hsp;
	haucp->next = NULL;

	*gfarm_host_address_use_config_last = haucp;
	gfarm_host_address_use_config_last = &haucp->next;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
host_address_get(const char *name, int port,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	/* sizeof(struct sockaddr_in) == sizeof(struct sockaddr) */
	struct sockaddr_in *peer_addr_in = (struct sockaddr_in *)peer_addr;
	struct hostent *hp = gethostbyname(name);
	char *n;

	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (GFARM_ERR_UNKNOWN_HOST);
	if (if_hostnamep != NULL) {
		n = strdup(name); /* XXX - or strdup(hp->h_name)? */
		if (n == NULL)
			return (GFARM_ERR_NO_MEMORY);
		*if_hostnamep = n;
	}
	memset(peer_addr_in, 0, sizeof(*peer_addr_in));
	peer_addr_in->sin_port = htons(port);
	peer_addr_in->sin_family = hp->h_addrtype;
	memcpy(&peer_addr_in->sin_addr, hp->h_addr,
	    sizeof(peer_addr_in->sin_addr));
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
host_address_get_matched(const char *name, int port,
	struct gfarm_hostspec *hostspec,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	/* sizeof(struct sockaddr_in) == sizeof(struct sockaddr) */
	struct sockaddr_in *peer_addr_in = (struct sockaddr_in *)peer_addr;
	struct hostent *hp;
	char *n;
	int i;

	if (hostspec == NULL)
		return (host_address_get(name, port, peer_addr, if_hostnamep));

	hp = gethostbyname(name);
	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (GFARM_ERR_UNKNOWN_HOST);
	memset(peer_addr_in, 0, sizeof(*peer_addr_in));
	peer_addr_in->sin_port = htons(port);
	peer_addr_in->sin_family = hp->h_addrtype;
	for (i = 0; hp->h_addr_list[i] != NULL; i++) {
		memcpy(&peer_addr_in->sin_addr, hp->h_addr_list[i],
		    sizeof(peer_addr_in->sin_addr));
		if (gfarm_hostspec_match(hostspec, name, peer_addr)) {
			if (if_hostnamep != NULL) {
				/* XXX - or strdup(hp->h_name)? */
				n = strdup(name); 
				if (n == NULL)
					return (GFARM_ERR_NO_MEMORY);
				*if_hostnamep = n;
			}
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_NO_SUCH_OBJECT);
}

static gfarm_error_t
host_info_address_get_matched(struct gfarm_host_info *info, int port,
	struct gfarm_hostspec *hostspec,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	gfarm_error_t e;
	int i;

	e = host_address_get_matched(info->hostname, port, hostspec,
	    peer_addr, if_hostnamep);
	if (e == GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);
	for (i = 0; i < info->nhostaliases; i++) {
		e = host_address_get_matched(info->hostaliases[i], port,
		    hostspec, peer_addr, if_hostnamep);
		if (e == GFARM_ERR_NO_ERROR)
			return (GFARM_ERR_NO_ERROR);
	}
	return (e);
}

struct host_info_rec {
	struct gfarm_host_info *info;
	int tried, got;
};

static gfarm_error_t
address_get_matched(const char *name, struct host_info_rec *hir, int port,
	struct gfarm_hostspec *hostspec,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	gfarm_error_t e;

	e = host_address_get_matched(name, port, hostspec,
	    peer_addr, if_hostnamep);
	if (e == GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);
	if (!hir->tried) {
		hir->tried = 1;
		if (gfarm_host_info_get_by_if_hostname(name, hir->info) ==
		    GFARM_ERR_NO_ERROR)
			hir->got = 1;
	}
	if (hir->got) {
		e = host_info_address_get_matched(hir->info, port, hostspec,
		    peer_addr, if_hostnamep);
	}
	return (e);
}

static gfarm_error_t
address_get(const char *name, struct host_info_rec *hir, int port,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	if (gfarm_host_address_use_config_list != NULL) {
		struct gfarm_host_address_use_config *config;

		for (config = gfarm_host_address_use_config_list;
		    config != NULL; config = config->next) {
			if (address_get_matched(
			    name, hir, port, config->hostspec,
			    peer_addr, if_hostnamep) == GFARM_ERR_NO_ERROR)
				return (GFARM_ERR_NO_ERROR);
		}
	}
	return (address_get_matched(name, hir, port, NULL,
	    peer_addr, if_hostnamep));
}

gfarm_error_t
gfarm_host_info_address_get(const char *host, int port,
	struct gfarm_host_info *info,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	struct host_info_rec hir;

	hir.info = info;
	hir.tried = hir.got = 1;
	return (address_get(host, &hir, port, peer_addr, if_hostnamep));
}

gfarm_error_t
gfarm_host_address_get(const char *host, int port,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	gfarm_error_t e;
	struct gfarm_host_info info;
	struct host_info_rec hir;

	hir.info = &info;
	hir.tried = hir.got = 0;
	e = address_get(host, &hir, port, peer_addr, if_hostnamep);
	if (hir.got)
		gfarm_host_info_free(&info);
	return (e);
}

/*
 * `*widendep' is only set, when this function returns True.
 * `*widendep' means:
 * -1: the addr is adjacent to the lower bound of the min address.
 *  0: the addr is between min and max.
 *  1: the addr is adjacent to the upper bound of the max address.
 *
 * XXX mostly works, but by somewhat haphazard way, if wild_guess is set.
 */
int
gfarm_addr_is_same_net(struct sockaddr *addr,
	struct sockaddr *min, struct sockaddr *max, int wild_guess,
	int *widenedp)
{
	gfarm_uint32_t addr_in, min_in, max_in;
	gfarm_uint32_t addr_net, min_net, max_net;

	assert(addr->sa_family == AF_INET &&
	    min->sa_family == AF_INET &&
	    max->sa_family == AF_INET);
	addr_in = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	min_in = ntohl(((struct sockaddr_in *)min)->sin_addr.s_addr);
	max_in = ntohl(((struct sockaddr_in *)max)->sin_addr.s_addr);
	if (min_in <= addr_in && addr_in <= max_in) {
		*widenedp = 0;
		return (1);
	}
	if (!wild_guess) /* `*widenedp' is always false, if !wild_guess */
		return (0);
	/* do wild guess */

	/* XXX - get IPv4 C class part */
	addr_net = (addr_in >> 8) & 0xffffff;
	min_net = (min_in >> 8) & 0xffffff;
	max_net = (max_in >> 8) & 0xffffff;
	/* adjacent or same IPv4 C class? */
	if (addr_net == min_net - 1 ||
	    (addr_net == min_net && addr_in < min_in)) {
		*widenedp = -1;
		return (1);
	}
	if (addr_net == max_net + 1 ||
	    (addr_net == max_net && addr_in > max_in)) {
		*widenedp = 1;
		return (1);
	}
	return (0);
}

gfarm_error_t
gfarm_addr_range_get(struct sockaddr *addr,
	struct sockaddr *min, struct sockaddr *max)
{
	gfarm_uint32_t addr_in, addr_net;

	assert(addr->sa_family == AF_INET);
	addr_in = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	/* XXX - get IPv4 C class part */
	addr_net = addr_in & 0xffffff00;

	/* XXX - minimum & maximum address in the IPv4 C class net  */
	memset(min, 0, sizeof(*min));
	min->sa_family = AF_INET;
	((struct sockaddr_in *)min)->sin_addr.s_addr = htonl(addr_net);

	memset(max, 0, sizeof(*max));
	max->sa_family = AF_INET;
	((struct sockaddr_in *)max)->sin_addr.s_addr = htonl(addr_net | 0xff);
	return (GFARM_ERR_NO_ERROR);
}
