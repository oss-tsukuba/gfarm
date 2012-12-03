/*
 * $Id$
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h> /* gfarm_host_is_in_domain() */

#include "gfnetdb.h"

#include "liberror.h"
#include "hostspec.h"

#define IS_DNS_LABEL_CHAR(c)	(isalnum(c) || (c) == '-')
#define AF_INET4_BIT	32

struct gfarm_hostspec {
	enum { GFHS_ANY, GFHS_NAME, GFHS_AF_INET4 } type;
	union gfhs_union {
		char name[1];
		struct gfhs_in4_addr {
			struct in_addr addr, mask;
		} in4_addr;
	} u;
};

gfarm_error_t
gfarm_hostspec_any_new(struct gfarm_hostspec **hostspecpp)
{
	/* allocation size never overflows */
	struct gfarm_hostspec *hsp = malloc(sizeof(struct gfarm_hostspec)
	    - sizeof(union gfhs_union));

	if (hsp == NULL) {
		gflog_debug(GFARM_MSG_1000854,
			"allocation of 'gfarm_hostspec' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	hsp->type = GFHS_ANY;
	*hostspecpp = hsp;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_hostspec_name_new(char *name, struct gfarm_hostspec **hostspecpp)
{
	/* never overflows, because huge name will never be passed here */
	struct gfarm_hostspec *hsp = malloc(sizeof(struct gfarm_hostspec)
	    - sizeof(union gfhs_union) + strlen(name) + 1);

	if (hsp == NULL) {
		gflog_debug(GFARM_MSG_1000855,
			"allocation of 'gfarm_hostspec' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	hsp->type = GFHS_NAME;
	strcpy(hsp->u.name, name);
	*hostspecpp = hsp;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_hostspec_af_inet4_new(gfarm_uint32_t addr, gfarm_uint32_t mask,
	struct gfarm_hostspec **hostspecpp)
{
	/* allocation size never overvlows */
	struct gfarm_hostspec *hsp = malloc(sizeof(struct gfarm_hostspec)
	    - sizeof(union gfhs_union) + sizeof(struct gfhs_in4_addr));

	if (hsp == NULL) {
		gflog_debug(GFARM_MSG_1000856,
			"allocation of 'gfarm_hostspec' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	hsp->type = GFHS_AF_INET4;
	hsp->u.in4_addr.addr.s_addr = addr;
	hsp->u.in4_addr.mask.s_addr = mask;
	*hostspecpp = hsp;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_hostspec_free(struct gfarm_hostspec *hostspecp)
{
	free(hostspecp);
}

/*
 * We don't use inet_addr(3)/inet_aton(3)/inet_pton(3),
 * because these library functions permit not only a.b.c.d,
 * but also a.b.c, a.b and a as inet address.
 *
 * NOTE: this function stores address of first invalid charactor to *endptr,
 *	even if this function reports error as its return value.
 */
gfarm_error_t
gfarm_string_to_in4addr(char *s, char **endptr, struct in_addr *addrp)
{
	gfarm_error_t e;
	char *ep;
	gfarm_int32_t addr;
	unsigned long byte;
	int i;

	byte = strtoul(s, &ep, 10);
	if (ep == s) {
		e = *s == '\0' ? GFARM_ERRMSG_IP_ADDRESS_EXPECTED :
		    GFARM_ERRMSG_INVALID_CHAR_IN_IP;
		goto bad;
	}
	if (byte >= 256) {
		ep = s;
		e = GFARM_ERRMSG_TOO_BIG_BYTE_IN_IP;
		goto bad;
	}
	addr = byte;
	for (i = 0; i < 3; i++) {
		if (*ep != '.') {
			e = GFARM_ERRMSG_IP_ADDRESS_TOO_SHORT;
			goto bad;
		}
		s = ep + 1;
		byte = strtoul(s, &ep, 10);
		if (ep == s) {
			e = GFARM_ERRMSG_INVALID_CHAR_IN_IP;
			goto bad;
		}
		if (byte >= 256) {
			ep = s;
			e = GFARM_ERRMSG_TOO_BIG_BYTE_IN_IP;
			goto bad;
		}
		addr = (addr << 8) | byte;
	}
	addrp->s_addr = htonl(addr);
	if (endptr != NULL)
		*endptr = ep;
	return (GFARM_ERR_NO_ERROR);
bad:
	if (endptr != NULL)
		*endptr = ep;
	gflog_debug(GFARM_MSG_1000857,
		"conversion from string to in4addr failed: %s",
		gfarm_error_string(e));
	return (e);
}

static int
gfarm_is_string_upper_case(char *s)
{
	unsigned char *t = (unsigned char *)s;

	for (; *t != '\0'; t++) {
		if (!isupper(*t))
			return (0);
	}
	return (1);
}

gfarm_error_t
gfarm_hostspec_parse(char *name, struct gfarm_hostspec **hostspecpp)
{
	char *end1p, *end2p;
	struct in_addr addr, mask;
	unsigned long masklen;

	if (strcmp(name, "*") == 0 || strcmp(name, "ALL") == 0)
		return (gfarm_hostspec_any_new(hostspecpp));

	if (gfarm_string_to_in4addr(name, &end1p, &addr)
	    == GFARM_ERR_NO_ERROR) {
		if (*end1p == '\0') {
			return (gfarm_hostspec_af_inet4_new(
			    addr.s_addr, INADDR_BROADCAST, hostspecpp));
		}
		if (*end1p == '/') {
			if (isdigit(((unsigned char *)end1p)[1]) &&
			    (masklen = strtoul(end1p + 1, &end2p, 10),
			     *end2p == '\0')) {
				if (masklen > AF_INET4_BIT) {
					gflog_debug(GFARM_MSG_1000858,
						"Netmask is too big");
					return (GFARM_ERRMSG_TOO_BIG_NETMASK);
				}
				if (masklen == 0) {
					mask.s_addr = INADDR_ANY;
				} else {
					masklen = AF_INET4_BIT - masklen;
					mask.s_addr = htonl(
					    ~((1 << (gfarm_int32_t)masklen)
					      - 1));
				}
				return (gfarm_hostspec_af_inet4_new(
				    addr.s_addr, mask.s_addr, hostspecpp));
			} else if (gfarm_string_to_in4addr(end1p + 1,
			    &end2p, &mask) == GFARM_ERR_NO_ERROR &&
			    *end2p == '\0') {
				return (gfarm_hostspec_af_inet4_new(
				    addr.s_addr, mask.s_addr,
				    hostspecpp));
			}
		}
		if (!IS_DNS_LABEL_CHAR(*(unsigned char *)end1p) &&
		    *end1p != '.') {
			gflog_debug(GFARM_MSG_1000859,
				"Invalid char in IP");
			return (GFARM_ERRMSG_INVALID_CHAR_IN_IP);
		}
	}
	if (*name == '\0') {
		gflog_debug(GFARM_MSG_1000860,
			"Host name or IP expected");
		return (GFARM_ERRMSG_HOSTNAME_OR_IP_EXPECTED);
	}
	if (!IS_DNS_LABEL_CHAR(*(unsigned char *)end1p) && *end1p != '.') {
		gflog_debug(GFARM_MSG_1000861,
			"Invalid char in host name");
		return (GFARM_ERRMSG_INVALID_CHAR_IN_HOSTNAME);
	}

	/*
	 * We don't allow all capital domain name.
	 * Such names are reserved for keywords like "*", "LISTENER".
	 */
	if (gfarm_is_string_upper_case(name)) {
		gflog_debug(GFARM_MSG_1000862,
			"capital name is not permitted(%s)", name);
		return (GFARM_ERRMSG_UNKNOWN_KEYWORD);
	}

	return (gfarm_hostspec_name_new(name, hostspecpp));
}

int
gfarm_host_is_in_domain(const char *hostname, const char *domainname)
{
	int hlen = strlen(hostname), dlen = strlen(domainname);

	if (hlen < dlen)
		return (0);
	if (hlen == dlen)
		return (strcasecmp(hostname, domainname) == 0);
	if (dlen == 0)
		return (1); /* null string matches with all hosts */
	if (hlen == dlen + 1)
		return (0);
	return (hostname[hlen - (dlen + 1)] == '.' &&
	    strcasecmp(&hostname[hlen - dlen], domainname) == 0);
}

int
gfarm_hostspec_match(struct gfarm_hostspec *hostspecp,
	const char *name, struct sockaddr *addr)
{
	switch (hostspecp->type) {
	case GFHS_ANY:
		return (1);
	case GFHS_NAME:
		if (name == NULL)
			return (0);
		if (hostspecp->u.name[0] == '.') {
			return (gfarm_host_is_in_domain(name,
			    &hostspecp->u.name[1]));
		} else {
			return (strcasecmp(name, hostspecp->u.name) == 0);
		}
	case GFHS_AF_INET4:
		if (addr == NULL)
			return (0);
		/* XXX */
		if (addr->sa_family == AF_UNIX)
			return (1);
		if (addr->sa_family != AF_INET)
			return (0);
		return ((((struct sockaddr_in *)addr)->sin_addr.s_addr &
			 hostspecp->u.in4_addr.mask.s_addr) ==
			hostspecp->u.in4_addr.addr.s_addr);
	}
	/* assert(0); */
	return (0);
}

void
gfarm_hostspec_to_string(struct gfarm_hostspec *hostspec,
	char *string, size_t size)
{
	unsigned char *a, *m;

	if (size <= 0)
		return;

	switch (hostspec->type) {
	case GFHS_ANY:
		string[0] = '\0';
		return;
	case GFHS_NAME:
		strncpy(string, hostspec->u.name, size);
		return;
	case GFHS_AF_INET4:
		a = (unsigned char *)&hostspec->u.in4_addr.addr.s_addr;
		m = (unsigned char *)&hostspec->u.in4_addr.mask.s_addr;
		snprintf(string, size, "%d.%d.%d.%d/%d.%d.%d.%d",
		    a[0], a[1], a[2], a[3], m[0], m[1], m[2], m[3]);
		return;
	}
	/* assert(0); */
	return;
}

gfarm_error_t
gfarm_sockaddr_to_name(struct sockaddr *addr, char **namep)
{
	struct addrinfo hints, *res, *res0;
	struct sockaddr_in *sin1, *sin2;
	char *s, name[NI_MAXHOST];

	if (gfarm_getnameinfo(addr, sizeof(*addr), name, sizeof(name),
	    NULL, 0, NI_NAMEREQD) != 0) {
		gflog_debug(GFARM_MSG_1000863,
			"Cannot get name info from IP address: %s",
			gfarm_error_string(
		GFARM_ERR_CANNOT_RESOLVE_AN_IP_ADDRESS_INTO_A_HOSTNAME));
		return (GFARM_ERR_CANNOT_RESOLVE_AN_IP_ADDRESS_INTO_A_HOSTNAME);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = addr->sa_family;
	if (gfarm_getaddrinfo(name, NULL, &hints, &res0) != 0) {
		gflog_debug(GFARM_MSG_1000864,
			"gfarm_getaddrinfo() failed: %s",
			gfarm_error_string(
			GFARM_ERRMSG_REVERSE_LOOKUP_NAME_IS_NOT_RESOLVABLE));
		return (GFARM_ERRMSG_REVERSE_LOOKUP_NAME_IS_NOT_RESOLVABLE);
	}
	for (res = res0; res; res = res->ai_next) {
		if (res->ai_family != addr->sa_family)
			continue;
		switch (res->ai_family) {
		case AF_INET:
			sin1 = (struct sockaddr_in *)res->ai_addr;
			sin2 = (struct sockaddr_in *)addr;
			if (sin1->sin_addr.s_addr == sin2->sin_addr.s_addr) {
				s = strdup(name);
				gfarm_freeaddrinfo(res0);
				if (s == NULL) {
					gflog_debug(GFARM_MSG_1000865,
						"allocation of string failed:"
						" %s",
						gfarm_error_string(
						GFARM_ERR_NO_MEMORY));
					return (GFARM_ERR_NO_MEMORY);
				}
				*namep = s;
				return (GFARM_ERR_NO_ERROR); /* success */
			}
			break;
		}
	}
	gfarm_freeaddrinfo(res0);
	return (GFARM_ERRMSG_REVERSE_LOOKUP_NAME_DOES_NOT_MATCH);
}

void
gfarm_sockaddr_to_string(struct sockaddr *addr, char *string, size_t size)
{
	unsigned char *a;
#ifndef HAVE_SNPRINTF
	char s[GFARM_SOCKADDR_STRLEN];

	if (size <= 0)
		return;
#endif
	switch (addr->sa_family) {
	case AF_INET:
		a = (unsigned char *)
		    &((struct sockaddr_in *)addr)->sin_addr.s_addr;
#ifdef HAVE_SNPRINTF
		snprintf(string, size, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
#else
		sprintf(s, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
		strncpy(string, s, size);
		string[size - 1] = '\0';
#endif
		break;
	default:
#ifdef HAVE_SNPRINTF
		snprintf(string, size, "unknown address family %d",
			 addr->sa_family);
#else
		sprintf(s, "unknown address family %d", addr->sa_family);
		strncpy(string, s, size);
		string[size - 1] = '\0';
#endif
		break;
	}
}
