/*
 * $Id$
 */

#include <gfarm/gfarm_config.h>

#include <pthread.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
#ifdef HAVE_GETIFADDRS
#include <ifaddrs.h>
#endif
#include <errno.h>

#include <unistd.h>
#include <netdb.h>

#include <gfarm/gfarm.h>

#include "gfnetdb.h"

#include "context.h"
#include "hostspec.h"
#include "gfm_client.h"
#define GFARM_HOST_ADDRESS_INTERNAL
#include "host_address.h"
#include "host.h"

#ifndef __KERNEL__
#define free_gethost_buff(buf)
#else /* __KERNEL__ */
#define free_gethost_buff(buf) free(buf)
#endif /* __KERNEL__ */

#define staticp	(gfarm_ctxp->host_static)

struct known_network {
	struct known_network *next;
	struct gfarm_hostspec *network;
};

struct gfarm_host_static {
	struct known_network *known_network_list;
	struct known_network **known_network_list_last;

	/* gfarm_host_get_self_name() */
	int self_name_initialized;
	char self_name[MAXHOSTNAMELEN + 1];

	/* gfm_host_get_canonical_self_name() */
	char *canonical_self_name;
	int port;
	gfarm_error_t error_save;

	/* gfarm_host_is_local() and gfarm_host_address_is_local() */
	int self_ifinfos_asked;
	int self_ifinfos_count;
	struct gfarm_ifinfo **self_ifinfos;
};

gfarm_error_t
gfarm_host_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_host_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->known_network_list = NULL;
	s->known_network_list_last = &s->known_network_list;

	s->self_name_initialized = 0;
	memset(s->self_name, 0, sizeof(s->self_name));

	s->canonical_self_name = NULL;
	s->port = 0;
	s->error_save = GFARM_ERR_NO_ERROR;

	s->self_ifinfos_asked = 0;
	s->self_ifinfos_count = 0;
	s->self_ifinfos = NULL;

	ctxp->host_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_host_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_host_static *s = ctxp->host_static;
	struct known_network *n, *next;

	if (s == NULL)
		return;

	for (n = s->known_network_list; n != NULL; n = next) {
		next = n->next;
		gfarm_hostspec_free(n->network);
		free(n);
	}

	free(s->canonical_self_name);

	gfarm_free_ifinfos(s->self_ifinfos_count, s->self_ifinfos);

	free(s);
	ctxp->host_static = NULL;
}

static gfarm_error_t
host_info_get_by_name_alias(struct gfm_connection *gfm_server,
	const char *hostname, struct gfarm_host_info *info)
{
	gfarm_error_t e, e2;

	e = gfm_client_host_info_get_by_namealiases(gfm_server,
	    1, &hostname, &e2, info);
	if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000866,
			"gfm_client_host_info_get_by_namealiases(%s) failed: "
			"%s",
			hostname,
			gfarm_error_string(e != GFARM_ERR_NO_ERROR ? e : e2));
	}
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

/* NOTE: the caller should check gfmd failover */
gfarm_error_t
gfm_host_info_get_by_name_alias(struct gfm_connection *gfm_server,
	const char *if_hostname, struct gfarm_host_info *info)
{
	gfarm_error_t e;
	struct hostent *hp;
	int i;
	char *n;

	e = host_info_get_by_name_alias(gfm_server, if_hostname, info);
	if (e == GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);

	/* XXX should not use gethostbyname(3), but ...*/
	/*
	 * This interface is never called from gfmd,
	 * so MPSAFE-ness is not an issue for now.
	 * Unlike gethostbyname(), getaddrinfo() doesn't return multiple
	 * host aliases, so we cannot use getaddrino() here to see alias names.
	 *
	 * FIXME: This design must be revised, when we support IPv6.
	 */
	hp = gethostbyname(if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET) {
		gflog_debug(GFARM_MSG_1000867,
			"Unknown host (%s): %s",
			if_hostname,
			gfarm_error_string(GFARM_ERR_UNKNOWN_HOST));
		free_gethost_buff(hp);
		return (GFARM_ERR_UNKNOWN_HOST);
	}
	for (i = 0, n = hp->h_name; n != NULL; n = hp->h_aliases[i++]) {
		if (host_info_get_by_name_alias(gfm_server, n, info) ==
		    GFARM_ERR_NO_ERROR) {
			free_gethost_buff(hp);
			return (GFARM_ERR_NO_ERROR);
		}
	}
	free_gethost_buff(hp);
	return (e);
}

/*
 * The value returned to `*canonical_hostnamep' should be freed.
 *
 * NOTE: the caller should check gfmd failover
 */
gfarm_error_t
gfm_host_get_canonical_name(struct gfm_connection *gfm_server,
	const char *hostname, char **canonical_hostnamep, int *portp)
{
	gfarm_error_t e;
	struct gfarm_host_info info;
	int port;
	char *n;

	e = gfm_host_info_get_by_name_alias(gfm_server, hostname, &info);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000868,
			"gfm_host_info_get_by_name_alias(%s) failed: %s",
			hostname,
			gfarm_error_string(e));
		return (e);
	}

	n = strdup(info.hostname);
	port = info.port;
	gfarm_host_info_free(&info);
	if (n == NULL) {
		gflog_debug(GFARM_MSG_1000869,
			"allocation of hostname failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	*canonical_hostnamep = n;
	*portp = port;
	return (GFARM_ERR_NO_ERROR);
}

char *
gfarm_host_get_self_name(void)
{
	if (!staticp->self_name_initialized) {
		staticp->self_name[0] = staticp->self_name[MAXHOSTNAMELEN] = 0;
		/* gethostname(2) almost shouldn't fail */
		gethostname(staticp->self_name, MAXHOSTNAMELEN);
		if (staticp->self_name[0] == '\0')
			strcpy(staticp->self_name, "hostname-not-set");
		staticp->self_name_initialized = 1;
	}

	return (staticp->self_name);
}

/*
 * shouldn't free the return value of this function.
 *
 * XXX this assumes all metadata servers agree with this canonical hostname.
 *
 * NOTE: gfarm_error_initialize() and gfarm_metadb_initialize()
 *	should be called before this function.
 * NOTE: the caller should check gfmd failover
 */
gfarm_error_t
gfm_host_get_canonical_self_name(struct gfm_connection *gfm_server,
	char **canonical_hostnamep, int *portp)
{
	gfarm_error_t e;

	if (staticp->canonical_self_name == NULL) {
		if (staticp->error_save != GFARM_ERR_NO_ERROR)
			return (staticp->error_save);
		e = gfm_host_get_canonical_name(gfm_server,
		    gfarm_host_get_self_name(),
		    &staticp->canonical_self_name, &staticp->port);
		if (e != GFARM_ERR_NO_ERROR) {
			staticp->error_save = e;
			gflog_debug(GFARM_MSG_1000870,
				"gfm_host_get_canonical_name() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	*canonical_hostnamep = staticp->canonical_self_name;
	*portp = staticp->port;
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
			free_gethost_buff(hp);
			return (GFARM_ERR_NO_ERROR);
		}
	}
	free_gethost_buff(hp);
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

		if ((self_architecture =
		    getenv("GFARM_ARCHITECTURE")) != NULL) {
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

/* NOTE: this may cause gfmd failover */
int
gfm_canonical_hostname_is_local(struct gfm_connection *gfm_server,
	const char *canonical_hostname)
{
	gfarm_error_t e;
	char *self_name;
	int port;

	e = gfm_host_get_canonical_self_name(gfm_server, &self_name, &port);
	if (e != GFARM_ERR_NO_ERROR)
		self_name = gfarm_host_get_self_name();
	return (strcasecmp(canonical_hostname, self_name) == 0);
}

/* NOTE: this may cause gfmd failover */
int
gfm_host_is_local(struct gfm_connection *gfm_server, const char *hostname)
{
	gfarm_error_t e;
	char *canonical_hostname;
	int is_local, port;

	e = gfm_host_get_canonical_name(gfm_server, hostname,
	    &canonical_hostname, &port);
	if (e != GFARM_ERR_NO_ERROR)  {
		is_local = gfm_canonical_hostname_is_local(gfm_server,
		    hostname);
	} else {
		is_local = gfm_canonical_hostname_is_local(gfm_server,
		    canonical_hostname);
		free(canonical_hostname);
	}
	return (is_local);
}

struct gfarm_ifinfo_ipv4 {
	struct gfarm_ifinfo ifinfo; /* must be first member */
	struct sockaddr_in addr;
	struct sockaddr_in netmask;
};

struct gfarm_ifinfo_ipv6 {
	struct gfarm_ifinfo ifinfo; /* must be first member */
	struct sockaddr_in6 addr;
	struct sockaddr_in6 netmask;
};

void
gfarm_free_ifinfos(int count, struct gfarm_ifinfo **ifinfos)
{
	int i;

	for (i = 0; i < count; i++)
		free(ifinfos[i]);
	free(ifinfos);
}

#ifndef __KERNEL__
#ifdef HAVE_GETIFADDRS

gfarm_error_t
gfarm_get_ifinfos(int *countp, struct gfarm_ifinfo ***ifinfos_p)
{
	gfarm_error_t e;
	struct ifaddrs *ifa_head, *ifa;
	int i, n, save_errno;
	struct gfarm_ifinfo **ifinfos, *ifinfo;
	struct gfarm_ifinfo_ipv4 *ifi_ipv4;
	struct gfarm_ifinfo_ipv6 *ifi_ipv6;

	if (getifaddrs(&ifa_head) == -1) {
		save_errno = errno;
		gflog_notice_errno(GFARM_MSG_1004305, "getifaddrs");
		return (gfarm_errno_to_error(save_errno));
	}
	for (n = 0, ifa = ifa_head; ifa != NULL; ifa = ifa->ifa_next) {
		if ((ifa->ifa_flags & IFF_UP) != 0 &&
		    (ifa->ifa_addr->sa_family == AF_INET ||
		     ifa->ifa_addr->sa_family == AF_INET6)
		    ) {
			n++;
		}
	}
	if (n == 0)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	GFARM_MALLOC_ARRAY(ifinfos,  n);
	if (ifinfos == NULL) {
		gflog_debug(GFARM_MSG_1002523,
		    "gfarm_get_ifinfo: no memory for %d ifinfos", n);
		freeifaddrs(ifa_head);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0, ifa = ifa_head; ifa != NULL; ifa = ifa->ifa_next) {
		if ((ifa->ifa_flags & IFF_UP) == 0 ||
		    (ifa->ifa_addr->sa_family != AF_INET &&
		     ifa->ifa_addr->sa_family != AF_INET6))
			continue;
		e = GFARM_ERR_NO_ERROR;
		if (ifa->ifa_addr->sa_family !=
		    ifa->ifa_netmask->sa_family) {
			e = GFARM_ERR_INTERNAL_ERROR;
			gflog_notice(GFARM_MSG_UNFIXED,
			    "getifaddrs/%d family mismatch %d vs %d", i,
			    ifa->ifa_addr->sa_family,
			    ifa->ifa_netmask->sa_family);
#ifdef __GNUC__ /* shut up warning by gcc */
			ifinfo = NULL;
#endif
		} else {
			switch (ifa->ifa_addr->sa_family) {
			case AF_INET:
				GFARM_MALLOC(ifi_ipv4);
				if (ifi_ipv4 == NULL) {
					e = GFARM_ERR_NO_MEMORY;
					break;
				}
				ifinfo = &ifi_ipv4->ifinfo;
				ifinfo->ifi_addr =
				    (struct sockaddr *)&ifi_ipv4->addr;
				ifinfo->ifi_netmask =
				    (struct sockaddr *)&ifi_ipv4->netmask;
				memcpy(&ifi_ipv4->addr, ifa->ifa_addr,
				    sizeof(ifi_ipv4->addr));
				memcpy(&ifi_ipv4->netmask, ifa->ifa_netmask,
				    sizeof(ifi_ipv4->netmask));
				ifinfo->ifi_addrlen = sizeof(ifi_ipv4->addr);
				break;
			case AF_INET6:
				GFARM_MALLOC(ifi_ipv6);
				if (ifi_ipv6 == NULL) {
					e = GFARM_ERR_NO_MEMORY;
					break;
				}
				ifinfo = &ifi_ipv6->ifinfo;
				ifinfo->ifi_addr =
				    (struct sockaddr *)&ifi_ipv6->addr;
				ifinfo->ifi_netmask =
				    (struct sockaddr *)&ifi_ipv6->netmask;
				memcpy(&ifi_ipv6->addr, ifa->ifa_addr,
				    sizeof(ifi_ipv6->addr));
				memcpy(&ifi_ipv6->netmask, ifa->ifa_netmask,
				    sizeof(ifi_ipv6->netmask));
				ifinfo->ifi_addrlen = sizeof(ifi_ipv6->addr);
				break;
			default:
				continue;
			}
		}
		if (e != GFARM_ERR_NO_ERROR) {
			while (--i >= 0)
				free(ifinfos[i]);
			free(ifinfos);
			return (e);
		}
		ifinfos[i++] = ifinfo;
	}
	freeifaddrs(ifa_head);
	*ifinfos_p = ifinfos;
	*countp = n;
	return (GFARM_ERR_NO_ERROR);
}

#else /* HAVE_GETIFADDRS */

#ifdef SUN_LEN
# ifndef linux
#  define NEW_SOCKADDR /* 4.3BSD-Reno or later */
# endif
#endif

#define ADDRESSES_DELTA 16
#define IFCBUFFER_SIZE	8192

/* XXX SIOCGIFCONF version only supports IPv4 */

gfarm_error_t
gfarm_get_ifinfos(int *countp, struct gfarm_ifinfo ***ifinfos_p)
{
	gfarm_error_t e = GFARM_ERR_NO_MEMORY;
	int fd;
	int size, n;
#ifdef NEW_SOCKADDR
	int i;
#endif
	int save_errno;
	struct in_addr *addresses, *p;
	struct ifreq *ifr; /* pointer to interface address */
	struct ifconf ifc; /* buffer for interface addresses */
	char ifcbuffer[IFCBUFFER_SIZE];
	struct ifreq ifreq; /* buffer for interface flag */
	struct gfarm_ifinfo **ifinfos, *ifinfo;
	struct gfarm_ifinfo_ipv4 *ifi_ipv4;
	struct sockaddr_in ipv4_netmask_24;

	/* XXX fixed /24 netmask */
	memset(&ipv4_netmask_24, 0, sizeof(ipv4_netmask_24));
#ifdef SUN_LEN
	ipv4_netmask_24.sin_len = sizeof(ipv4_netmask_24);
#endif
	ipv4_netmask_24.sin_family = AF_INET;
	ipv4_netmask_24.sin_port = 0;
	ipv4_netmask_24.sin_port = htonl(0xffffff00);

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1000871, "creation of socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	ifc.ifc_len = sizeof(ifcbuffer);
	ifc.ifc_buf = ifcbuffer;
	if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
		save_errno = errno;
		close(fd);
		gflog_debug(GFARM_MSG_1000872,
			"ioctl() on socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	n = 0;
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
				save_errno = errno;
				gflog_debug(GFARM_MSG_1000874,
					"ioctl() on socket failed: %s",
					strerror(save_errno));
				break;
			}
		}
		if ((ifreq.ifr_flags & IFF_UP) == 0)
			continue;

		n++;
	}
	if (n == 0) {
		close(fd);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}

	GFARM_MALLOC_ARRAY(ifinfos,  n);
	if (ifinfos == NULL) {
		gflog_debug(GFARM_MSG_1002523,
		    "gfarm_get_ifinfo: no memory for %d ifinfos", n);
		close(fd);
		return (GFARM_ERR_NO_MEMORY);
	}
	size = n;

	n = 0;
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
				save_errno = errno;
				gflog_debug(GFARM_MSG_1000874,
					"ioctl() on socket failed: %s",
					strerror(save_errno));
				break;
			}
		}
		if ((ifreq.ifr_flags & IFF_UP) == 0)
			continue;

		if (n >= size) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "number of interface address increased from %d",
			    n);
			break;
		}
		GFARM_MALLOC(ifi_ipv4);
		if (ifi_ipv4 == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		memcpy(&ifi_ipv4->addr, (struct sockaddr_in *)&ifr->ifr_addr,
		    sizeof(ifi_ipv4->addr));
		/* XXX fixed /24 netmask */
		memcpy(&ifi_ipv4->netmask, &ipv4_netmask_24,
		    sizeof(ifi_ipv4->netmask));
		ifinfo = &ifi_ipv4->ifinfo;
		ininfo->ifi_addr = (struct sockaddr *)&ifi_ipv4->addr;
		ininfo->ifi_netmask = (struct sockaddr *)&ifi_ipv4->netmask;
		ininfo->ifi_addrlen = sizeof(ifi_ipv4->addr);
		ifinfos[n++] = ifinfo;
	}
	close(fd);
	if (n == 0) {
		free(ifinfos);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	if (size != n) {
		GFARM_REALLOC_ARRAY(p, ifinfos, n);
		if (p == NULL) {
			gflog_debug(GFARM_MSG_1000876,
			    "re-allocation of 'ifinfos' failed: %s",
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
			while (--n >= 0)
				free(ifinfos[i]);
			free(ifinfos);
			return (GFARM_ERR_NO_MEMORY);
		}
		ifinfos = p;
	}
	*ifinfos_p = ifinfos;
	*countp = n;
	return (GFARM_ERR_NO_ERROR);
}

#endif /* HAVE_GETIFADDRS */
#else /* __KERNEL__ */
gfarm_error_t
gfarm_get_ip_addresses(int *countp, struct in_addr **ip_addressesp)
{
	char name[128];
	struct hostent *ent;
	int i, n, err;
	struct in_addr *addresses;

	*countp = 0;
	*ip_addressesp = NULL;
	if ((err = gethostname(name, sizeof(name)))) {
		return (gfarm_errno_to_error(err));
	}
	if (!(ent = gethostbyname(name))) {
		return (gfarm_errno_to_error(errno));
	}

	for (n = 0; ent->h_addr_list[n]; n++)
		;
	GFARM_MALLOC_ARRAY(addresses,  n);
	if (addresses == NULL) {
		gflog_debug(GFARM_MSG_1002523,
		    "gfarm_get_ip_addresses: no memory for %d IPs", n);
		free_gethost_buff(ent);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; ent->h_addr_list[i]; i++) {
		addresses[i] = *(struct in_addr *)ent->h_addr_list[i];
	}
	free_gethost_buff(ent);
	*ip_addressesp = addresses;
	*countp = n;
	return (GFARM_ERR_NO_ERROR);
}
#endif /* __KERNEL__ */

gfarm_error_t
gfarm_self_address_get(int port,
	int *self_addresses_count_p,
	struct gfarm_host_address ***self_addresses_p)
{
	gfarm_error_t e;
	struct gfarm_ifinfo **ifinfos, *ifi;
	int i, j, addr_count;
	struct gfarm_host_address **addr_array, *sa;
	struct gfarm_host_address_ipv4 *sa_ipv4;
	struct gfarm_host_address_ipv6 *sa_ipv6;

	e = gfarm_get_ifinfos(&addr_count, &ifinfos);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(addr_array, addr_count);
	if (addr_array == NULL) {
		gfarm_free_ifinfos(addr_count, ifinfos);
		return (GFARM_ERR_NO_MEMORY);
	}
	j = 0;
	for (i = 0; i < addr_count; i++) {
		ifi = ifinfos[i];
		switch (ifi->ifi_addr->sa_family) {
		case AF_INET:
			GFARM_MALLOC(sa_ipv4);
			if (sa_ipv4 == NULL) {
				sa = NULL;
				break;
			}
			memset(sa_ipv4, 0, sizeof(*sa_ipv4));
			memcpy(&sa_ipv4->sa_addr, ifi->ifi_addr,
			    sizeof(sa_ipv4->sa_addr));
			sa_ipv4->sa_family = AF_INET;
			sa_ipv4->sa_addrlen = sizeof(sa_ipv4->sa_addr);
			sa_ipv4->sa_addr.sin_port = htons(port);
			sa = (struct gfarm_host_address *)sa_ipv4;
			break;
		case AF_INET6:
			GFARM_MALLOC(sa_ipv6);
			if (sa_ipv6 == NULL) {
				sa = NULL;
				break;
			}
			memset(sa_ipv6, 0, sizeof(*sa_ipv6));
			memcpy(&sa_ipv6->sa_addr, ifi->ifi_addr,
			    sizeof(sa_ipv6->sa_addr));
			sa_ipv6->sa_family = AF_INET6;
			sa_ipv6->sa_addrlen = sizeof(sa_ipv6->sa_addr);
			sa_ipv6->sa_addr.sin6_port = htons(port);
			sa = (struct gfarm_host_address *)sa_ipv6;
			break;
		default:
			/* i.e. af_not_supported */
			continue;
		}
		if (sa == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gfarm_host_address_free(j, addr_array);
			gflog_debug(GFARM_MSG_UNFIXED,
			    "self_addresses_from_ifinfos(): %s",
			    gfarm_error_string(e));
			gfarm_free_ifinfos(addr_count, ifinfos);
			return (e);
		}
		addr_array[j++] = sa;
	}
	gfarm_free_ifinfos(addr_count, ifinfos);
	*self_addresses_count_p = j;
	*self_addresses_p = addr_array;
	return (GFARM_ERR_NO_ERROR);
}

#if 0 /* "address_use" directive is disabled for now */

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
	if (haucp == NULL) {
		gflog_debug(GFARM_MSG_1000877,
			"allocation of host address use config failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	haucp->hostspec = hsp;
	haucp->next = NULL;

	*gfarm_host_address_use_config_last = haucp;
	gfarm_host_address_use_config_last = &haucp->next;
	return (GFARM_ERR_NO_ERROR);
}

#endif /* "address_use" directive is disabled for now */

static int
always_match(struct gfarm_hostspec *hostspecp,
	const char *name, struct sockaddr *addr)
{
	return (1);
}

static gfarm_error_t
host_address_get(const char *name, int port,
	int (*match)(struct gfarm_hostspec *, const char *, struct sockaddr *),
	struct gfarm_hostspec *hostspec,
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
	error = gfarm_getaddrinfo(name, sbuf, &hints, &res0);
	if (error != 0) {
		gflog_debug(GFARM_MSG_1000878,
			"Unknown host (%s): %s",
			name,
			gfarm_error_string(GFARM_ERR_UNKNOWN_HOST));
		return (GFARM_ERR_UNKNOWN_HOST);
	}

	for (res = res0; res != NULL; res = res->ai_next) {
		if (!(*match)(hostspec, name, res->ai_addr))
			continue;
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
			gflog_debug(GFARM_MSG_1000879,
			    "Address family not supported "
			    "by protocol family (%s): %s",
			    name, gfarm_error_string(e));
		} else {
			e = GFARM_ERR_NO_SUCH_OBJECT;
			gflog_debug(GFARM_MSG_1000881,
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
		if (!(*match)(hostspec, name, res->ai_addr))
			continue;
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
		addr_array[i] = sa;
	}
	gfarm_freeaddrinfo(res0);
	*addr_countp = addr_count;
	*addr_arrayp = addr_array;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
host_address_get_matched(const char *name, int port,
	struct gfarm_hostspec *hostspec,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
	return (host_address_get(name, port,
	    hostspec == NULL ? always_match : gfarm_hostspec_match, hostspec,
	    addr_countp, addr_arrayp));
}

static gfarm_error_t
host_info_address_get_matched(struct gfarm_host_info *info, int port,
	struct gfarm_hostspec *hostspec,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
	gfarm_error_t e;
	int i;

	e = host_address_get_matched(info->hostname, port, hostspec,
	    addr_countp, addr_arrayp);
	if (e == GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);
	for (i = 0; i < info->nhostaliases; i++) {
		e = host_address_get_matched(info->hostaliases[i], port,
		    hostspec, addr_countp, addr_arrayp);
		if (e == GFARM_ERR_NO_ERROR)
			return (GFARM_ERR_NO_ERROR);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000882,
		"failed to get matched host address: %s",
		gfarm_error_string(e));
	}
	return (e);
}

struct host_info_rec {
	struct gfarm_host_info *info;
	int tried, got;
};

static gfarm_error_t
address_get_matched(struct gfm_connection *gfm_server,
	const char *name, struct host_info_rec *hir, int port,
	struct gfarm_hostspec *hostspec,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
	gfarm_error_t e;

	e = host_address_get_matched(name, port, hostspec,
	    addr_countp, addr_arrayp);
	if (e == GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_ERROR);
	if (!hir->tried) {
		hir->tried = 1;
		if (gfm_host_info_get_by_name_alias(gfm_server,
		    name, hir->info) == GFARM_ERR_NO_ERROR)
			hir->got = 1;
	}
	if (hir->got) {
		e = host_info_address_get_matched(hir->info, port, hostspec,
		    addr_countp, addr_arrayp);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000883,
		"failed to get matched address: %s",
		gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
address_get(struct gfm_connection *gfm_server, const char *name,
	struct host_info_rec *hir, int port,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
#if 0 /* "address_use" directive is disabled for now */
	if (gfarm_host_address_use_config_list != NULL) {
		struct gfarm_host_address_use_config *config;

		for (config = gfarm_host_address_use_config_list;
		    config != NULL; config = config->next) {
			if (address_get_matched(gfm_server,
			    name, hir, port, config->hostspec,
			    peer_addr, if_hostnamep) == GFARM_ERR_NO_ERROR)
				return (GFARM_ERR_NO_ERROR);
		}
	}
#endif /* "address_use" directive is disabled for now */
	return (address_get_matched(gfm_server, name, hir, port, NULL,
	    addr_countp, addr_arrayp));
}

/* NOTE: the caller should check gfmd failover */
gfarm_error_t
gfm_host_info_address_get(struct gfm_connection *gfm_server,
	const char *host, int port,
	struct gfarm_host_info *info,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
	struct host_info_rec hir;

	hir.info = info;
	hir.tried = hir.got = 1;
	return (address_get(gfm_server, host, &hir, port,
	    addr_countp, addr_arrayp));
}

/* NOTE: the caller should check gfmd failover */
gfarm_error_t
gfm_host_address_get(struct gfm_connection *gfm_server,
	const char *host, int port,
	int *addr_countp, struct gfarm_host_address ***addr_arrayp)
{
	gfarm_error_t e;
	struct gfarm_host_info info;
	struct host_info_rec hir;

	hir.info = &info;
	hir.tried = hir.got = 0;
	e = address_get(gfm_server, host, &hir, port,
	    addr_countp, addr_arrayp);
	if (hir.got)
		gfarm_host_info_free(&info);
	return (e);
}

int
gfarm_sockaddr_is_local(struct sockaddr *peer_addr)
{
	struct sockaddr *ifi_addr;
	struct sockaddr_in *peer_in, *ifi_in;
	struct sockaddr_in6 *peer_in6, *ifi_in6;
	int i;

	if (!staticp->self_ifinfos_asked) {
		staticp->self_ifinfos_asked = 1;
		if (gfarm_get_ifinfos(&staticp->self_ifinfos_count,
		    &staticp->self_ifinfos) != GFARM_ERR_NO_ERROR) {
			/* self_ifinfos_count remains 0 */
			return (0);
		}
	}
	/* XXX if there are lots of IP addresses on this host, this is slow */
	switch (peer_addr->sa_family) {
	case AF_INET:
		peer_in = (struct sockaddr_in *)peer_addr;
		for (i = 0; i < staticp->self_ifinfos_count; i++) {
			ifi_addr = staticp->self_ifinfos[i]->ifi_addr;
			if (ifi_addr->sa_family != AF_INET)
				continue;
			ifi_in = (struct sockaddr_in *)ifi_addr;
			if (peer_in->sin_addr.s_addr ==
			    ifi_in->sin_addr.s_addr)
				return (1);
		}
		return (0);
	case AF_INET6:
		peer_in6 = (struct sockaddr_in6 *)peer_addr;
		for (i = 0; i < staticp->self_ifinfos_count; i++) {
			ifi_addr = staticp->self_ifinfos[i]->ifi_addr;
			if (ifi_addr->sa_family != AF_INET6)
				continue;
			ifi_in6 = (struct sockaddr_in6 *)ifi_addr;
			if (memcmp(&peer_in6->sin6_addr, &ifi_in6->sin6_addr,
			    sizeof(ifi_in6->sin6_addr)) == 0)
				return (1);
		}
		return (0);
	default:
		break;
	}
	return (0);
}

int
gfarm_host_is_local(struct gfm_connection *gfm_server,
	const char *hostname, int port)
{
	int addr_count, i, is_local = 0;
	struct gfarm_host_address **addr_array;
	gfarm_error_t e = gfm_host_address_get(gfm_server, hostname, port,
	    &addr_count, &addr_array);

	if (e != GFARM_ERR_NO_ERROR)
		return (0);
	for (i = 0; i < addr_count; i++) {
		if (gfarm_sockaddr_is_local(&addr_array[i]->sa_addr)) {
			is_local = 1;
			break;
		}
	}
	gfarm_host_address_free(addr_count, addr_array);
	return (is_local);
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
	struct gfarm_ifinfo **ifinfos;
	struct gfarm_hostspec *net;
	gfarm_error_t e;

	e = gfarm_get_ifinfos(&count, &ifinfos);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < count; ++i) {
		e = gfarm_hostspec_ifinfo_new(ifinfos[i], &net);
		if (e == GFARM_ERR_NO_ERROR) {
			e = gfarm_known_network_list_add(net);
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
	}
	gfarm_free_ifinfos(count, ifinfos);
	return (e);
}


gfarm_error_t
gfarm_addr_netmask_network_get(struct sockaddr *addr, struct sockaddr *mask,
	struct gfarm_hostspec **networkp)
{
	struct known_network *n;
	struct gfarm_hostspec *network;
	gfarm_uint32_t addr_ipv4, mask_ipv4;
	unsigned char *addr_ipv6, *mask_ipv6;
	gfarm_error_t e;

	/* search in the known network list */
	for (n = staticp->known_network_list; n != NULL; n = n->next) {
		if (gfarm_hostspec_match(n->network, NULL, addr)) {
			if (networkp != NULL)
				*networkp = n->network;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	if (addr->sa_family != mask->sa_family) {
		/* verbose, but this is probably a bug */
		gflog_warning(GFARM_MSG_UNFIXED,
		    "gfarm_addr_netmask_network_get(): "
		    "address family %d != netmask address family %d",
		    addr->sa_family, mask->sa_family);
		return (GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN);
	}
	switch (addr->sa_family) {
	case AF_INET:
		addr_ipv4 = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
		mask_ipv4 = ((struct sockaddr_in *)mask)->sin_addr.s_addr;
		e = gfarm_hostspec_af_inet4_new(addr_ipv4, mask_ipv4,
		    &network);
		break;
	case AF_INET6:
		addr_ipv6 = ((struct sockaddr_in6 *)addr)->sin6_addr.s6_addr;
		mask_ipv6 = ((struct sockaddr_in6 *)mask)->sin6_addr.s6_addr;
		e = gfarm_hostspec_af_inet6_new(addr_ipv6, mask_ipv6,
		    &network);
		break;
	default:
		e = GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY;
	}
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfarm_known_network_list_add(network);
		if (e == GFARM_ERR_NO_ERROR)
			if (networkp != NULL)
				*networkp = network;
	}
	return (e);
}

gfarm_error_t
gfarm_addr_network_get(struct sockaddr *addr,
	struct gfarm_hostspec **networkp)
{
	struct sockaddr_in mask_ipv4;
	struct sockaddr_in6 mask_ipv6;
	struct sockaddr *mask;

	switch (addr->sa_family) {
	case AF_INET:
		/* XXX - assume IPv4 class C network */
		memset(&mask_ipv4, 0, sizeof(mask_ipv4));
		mask_ipv4.sin_family = AF_INET;
		mask_ipv4.sin_addr.s_addr = htonl(0xffffff00);
		mask = (struct sockaddr *)&mask_ipv4;
		break;

	case AF_INET6:
		/* XXX - assume IPv6 /64 network */
		memset(&mask_ipv6, 0, sizeof(mask_ipv6));
		mask_ipv6.sin6_family = AF_INET6;
		mask_ipv6.sin6_addr.s6_addr[ 0] = 0xff;;
		mask_ipv6.sin6_addr.s6_addr[ 1] = 0xff;;
		mask_ipv6.sin6_addr.s6_addr[ 2] = 0xff;;
		mask_ipv6.sin6_addr.s6_addr[ 3] = 0xff;;
		mask_ipv6.sin6_addr.s6_addr[ 4] = 0xff;;
		mask_ipv6.sin6_addr.s6_addr[ 5] = 0xff;;
		mask_ipv6.sin6_addr.s6_addr[ 6] = 0xff;;
		mask_ipv6.sin6_addr.s6_addr[ 7] = 0xff;;
		mask_ipv6.sin6_addr.s6_addr[ 8] = 0;
		mask_ipv6.sin6_addr.s6_addr[ 9] = 0;
		mask_ipv6.sin6_addr.s6_addr[10] = 0;
		mask_ipv6.sin6_addr.s6_addr[11] = 0;
		mask_ipv6.sin6_addr.s6_addr[12] = 0;
		mask_ipv6.sin6_addr.s6_addr[13] = 0;
		mask_ipv6.sin6_addr.s6_addr[14] = 0;
		mask_ipv6.sin6_addr.s6_addr[15] = 0;
		mask = (struct sockaddr *)&mask_ipv6;
		break;

	default:
		return (
		    GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY);
	}
	return (gfarm_addr_netmask_network_get(addr, mask, networkp));
}
