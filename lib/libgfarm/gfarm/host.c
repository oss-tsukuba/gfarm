/*
 * $Id$
 */

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

char *
gfarm_host_info_get_by_if_hostname(const char *if_hostname,
	struct gfarm_host_info *info)
{
	struct hostent *hp;
	int i;
	char *e, *n;

	e = gfarm_host_info_get_by_name_alias(if_hostname, info);
	if (e == NULL)
		return (NULL);

	hp = gethostbyname(if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET)
		return (GFARM_ERR_UNKNOWN_HOST);
	for (i = 0, n = hp->h_name; n != NULL; n = hp->h_aliases[i++]){
		if (gfarm_host_info_get_by_name_alias(n, info) == NULL)
			return (NULL);
	}
	return (e);
}

/*
 * The value returned to `*canonical_hostnamep' should be freed.
 */
char *
gfarm_host_get_canonical_name(const char *hostname, char **canonical_hostnamep)
{
	char *e;
	struct gfarm_host_info info;
	char *n;

	e = gfarm_host_info_get_by_if_hostname(hostname, &info);
	if (e != NULL)
		return (e);

	n = strdup(info.hostname);
	gfarm_host_info_free(&info);
	if (n == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*canonical_hostnamep = n;
	return (NULL);
}

char *
gfarm_host_get_canonical_names(int nhosts, char **hosts,
	char ***canonical_hostnamesp)
{
	int i;
	char *e, **canonical_hostnames;

	canonical_hostnames = malloc(sizeof(char *) * nhosts);
	if (canonical_hostnames == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < nhosts; i++) {
		e = gfarm_host_get_canonical_name(hosts[i],
		    &canonical_hostnames[i]);
		if (e != NULL) {
			while (--i >= 0)
				free(canonical_hostnames[i]);
			free(canonical_hostnames);
			return (e);
		}
	}
	*canonical_hostnamesp = canonical_hostnames;
	return (NULL);
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
char *
gfarm_host_get_canonical_self_name(char **canonical_hostnamep)
{
	char *e;
	static char *canonical_self_name = NULL;

	if (canonical_self_name == NULL) {
		e = gfarm_host_get_canonical_name(gfarm_host_get_self_name(),
		    &canonical_self_name);
		if (e != NULL)
			return (e);			
	}
	*canonical_hostnamep = canonical_self_name;
	return (NULL);
}

int
gfarm_canonical_hostname_is_local(char *canonical_hostname)
{
	char *e, *self_name;

	e = gfarm_host_get_canonical_self_name(&self_name);
	if (e != NULL)
		self_name = gfarm_host_get_self_name();
	return (strcasecmp(canonical_hostname, self_name) == 0);
}

int
gfarm_host_is_local(char *hostname)
{
	char *e;
	int is_local;

	e = gfarm_host_get_canonical_name(hostname, &hostname);
	is_local = gfarm_canonical_hostname_is_local(hostname);
	if (e == NULL)
		free(hostname);
	return (is_local);
}

#ifdef SUN_LEN
# ifndef linux
#  define NEW_SOCKADDR /* 4.3BSD-Reno or later */
# endif
#endif

#define ADDRESSES_DELTA 16
#define IFCBUFFER_SIZE	8192

char *
gfarm_get_ip_addresses(int *countp, struct in_addr **ip_addressesp)
{
	char *e = GFARM_ERR_NO_MEMORY;
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
	addresses = (struct in_addr *)malloc(sizeof(struct in_addr ) * size);
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
			p = realloc(addresses, sizeof(struct in_addr) * size);
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
		p = realloc(addresses, sizeof(struct in_addr) * count);
		if (p == NULL)
			goto err;
		addresses = p;
	}
	*ip_addressesp = addresses;
	*countp = count;
	close(fd);
	return (NULL);

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

char *
gfarm_host_address_use(struct gfarm_hostspec *hsp)
{
	struct gfarm_host_address_use_config *haucp = malloc(sizeof(*haucp));

	if (haucp == NULL)
		return (GFARM_ERR_NO_MEMORY);

	haucp->hostspec = hsp;
	haucp->next = NULL;

	*gfarm_host_address_use_config_last = haucp;
	gfarm_host_address_use_config_last = &haucp->next;
	return (NULL);
}

static char *
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
	return (NULL);
}

static char *
host_address_get_matched(char *name, int port,
	struct gfarm_hostspec *hostspec,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	/* sizeof(struct sockaddr_in) == sizeof(struct sockaddr) */
	struct sockaddr_in *peer_addr_in = (struct sockaddr_in *)peer_addr;
	struct hostent *hp = gethostbyname(name);
	char *n;
	int i;

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
			return (NULL);
		}
	}
	return (GFARM_ERR_NO_SUCH_OBJECT);
}

char *
gfarm_host_info_address_get(const char *host, int port,
	struct gfarm_host_info *info,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	int i;
	char *n;

	if (gfarm_host_address_use_config_list != NULL && info != NULL) {
		struct gfarm_host_address_use_config *config;

		for (config = gfarm_host_address_use_config_list;
		    config != NULL; config = config->next) {
			for (i = 0, n = info->hostname; n != NULL;
			    n = i < info->nhostaliases ?
			    info->hostaliases[i++] : NULL) {
				if (host_address_get_matched(n, port,
				    config->hostspec,
				    peer_addr, if_hostnamep) == NULL) {
					return (NULL);
				}
			}
		}
	}
	return (host_address_get(host, port, peer_addr, if_hostnamep));
}

char *
gfarm_host_address_get(const char *host, int port,
	struct sockaddr *peer_addr, char **if_hostnamep)
{
	char *e;
	struct gfarm_host_info info;

	if (gfarm_host_address_use_config_list != NULL &&
	    gfarm_host_info_get_by_if_hostname(host, &info) == NULL) {
		e = gfarm_host_info_address_get(host, port, &info,
		    peer_addr, if_hostnamep);
		gfarm_host_info_free(&info);
		return (e);
	}

	return (host_address_get(host, port, peer_addr, if_hostnamep));
}
