/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include "host.h"

#define IS_DNS_LABEL_CHAR(c)	(isalnum(c) || (c) == '-')
#define AF_INET4_BIT	32
#define AF_INET6_BIT	(GFARM_IN6_ADDR_LEN * 8)

struct gfarm_hostspec {
	enum { GFHS_ANY, GFHS_NAME, GFHS_AF_INET4, GFHS_AF_INET6 } type;
	union gfhs_union {
		char name[1];
		struct gfhs_in4_addr {
			struct in_addr addr, mask;
		} in4_addr;
		struct gfhs_in6_addr {
			struct in6_addr addr, mask;
		} in6_addr;
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
gfarm_hostspec_name_new(const char *name, struct gfarm_hostspec **hostspecpp)
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
	hsp->u.in4_addr.addr.s_addr = addr & mask;
	hsp->u.in4_addr.mask.s_addr = mask;
	*hostspecpp = hsp;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_hostspec_af_inet6_new(
	const unsigned char *addr, const unsigned char *mask,
	struct gfarm_hostspec **hostspecpp)
{
	/* allocation size never overvlows */
	struct gfarm_hostspec *hsp = malloc(sizeof(struct gfarm_hostspec)
	    - sizeof(union gfhs_union) + sizeof(struct gfhs_in6_addr));
	int i;

	if (hsp == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'gfarm_hostspec' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	hsp->type = GFHS_AF_INET6;
	memcpy(hsp->u.in6_addr.addr.s6_addr, addr,
	    sizeof(hsp->u.in6_addr.addr.s6_addr));
	memcpy(hsp->u.in6_addr.mask.s6_addr, mask,
	    sizeof(hsp->u.in6_addr.mask.s6_addr));
	for (i = 0; i < GFARM_IN6_ADDR_LEN; i++)
		hsp->u.in6_addr.addr.s6_addr[i] &= mask[i];
	*hostspecpp = hsp;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_hostspec_ifinfo_new(struct gfarm_ifinfo *ifinfo,
	struct gfarm_hostspec **hostspecp)
{
	if (ifinfo->ifi_addr->sa_family != ifinfo->ifi_netmask->sa_family) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "gfarm_hostspec_ifinfo_new: address family %d "
		    "differs from netmask address family %d",
		    ifinfo->ifi_addr->sa_family,
		    ifinfo->ifi_netmask->sa_family);
		return (GFARM_ERR_INTERNAL_ERROR);
	}
	switch (ifinfo->ifi_addr->sa_family) {
	case AF_INET:
		return (gfarm_hostspec_af_inet4_new(
		    ((struct sockaddr_in *)ifinfo->ifi_addr)->sin_addr.s_addr,
		    ((struct sockaddr_in *)ifinfo->ifi_netmask)->
		    sin_addr.s_addr, hostspecp));
	case AF_INET6:
		return (gfarm_hostspec_af_inet6_new(
		    ((struct sockaddr_in6 *)ifinfo->ifi_addr)->
		    sin6_addr.s6_addr,
		    ((struct sockaddr_in6 *)ifinfo->ifi_netmask)->
		    sin6_addr.s6_addr, hostspecp));
	default:
		break;
	}
	return (GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY);
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
gfarm_string_to_in4addr(const char *s, char **endptr, struct in_addr *addrp)
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
		ep = (char *)s; /* UNCONST */
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
			ep = (char *)s; /* UNCONST */
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

/*
 * copyright notice of gfarm_string_to_in6addr() is:
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996,1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * from: inet_pton()
 *
 * int
 * inet_pton6(src, dst)
 *	convert presentation level address to network order binary form.
 * return:
 *	1 if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *	(1) does not touch `dst' unless it's returning 1.
 *	(2) :: in a full address is silently ignored.
 * credit:
 *	inspired by Mark Andrews.
 * author:
 *	Paul Vixie, 1996.
 */
#define GFARM_NS_IN6ADDRSZ	GFARM_IN6_ADDR_LEN
#define GFARM_NS_INT16SZ	2

gfarm_error_t
gfarm_string_to_in6addr(const char *src, char **endptr, struct in6_addr *dst)
{
	static const char xdigits_l[] = "0123456789abcdef",
		xdigits_u[] = "0123456789ABCDEF";
	unsigned char tmp[GFARM_NS_IN6ADDRSZ], *tp, *endp, *colonp;
	const char *xdigits;
#if 0 /* disable IPv4 */
	const char *curtok;
#endif
	int ch, seen_xdigits;
	unsigned int val;

	memset((tp = tmp), '\0', GFARM_NS_IN6ADDRSZ);
	endp = tp + GFARM_NS_IN6ADDRSZ;
	colonp = NULL;
	/* Leading :: requires some special handling. */
	if (*src == ':') {
		if (*++src != ':') {
			if (endptr != NULL)
				*endptr = (char *)src - 1; /* UNCONST */
			return (GFARM_ERRMSG_IP_ADDRESS_EXPECTED);
		}
	}
#if 0 /* disable IPv4 */
	curtok = src;
#endif
	seen_xdigits = 0;
	val = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
			pch = strchr((xdigits = xdigits_u), ch);
		if (pch != NULL) {
			val <<= 4;
			val |= (int)(pch - xdigits);
			if (++seen_xdigits > 4)
				return (GFARM_ERRMSG_TOO_BIG_BYTE_IN_IP);
			continue;
		}
		if (ch == ':') {
#if 0 /* disable IPv4 */
			curtok = src;
#endif
			if (!seen_xdigits) {
				if (colonp) {
					if (endptr != NULL) /* UNCONST */
						*endptr = (char *)src - 1;
					return (
					    GFARM_ERRMSG_IP_ADDRESS_EXPECTED);
				}
				colonp = tp;
				continue;
			} else if (*src == '\0') {
				if (endptr != NULL)
					*endptr = (char *)src; /* UNCONST */
				return (GFARM_ERRMSG_IP_ADDRESS_TOO_SHORT);
			}
			if (tp + GFARM_NS_INT16SZ > endp) {
				if (endptr != NULL)
					*endptr = (char *)src; /* UNCONST */
				return (GFARM_ERRMSG_TOO_BIG_BYTE_IN_IP);
			}
			*tp++ = (unsigned char) (val >> 8) & 0xff;
			*tp++ = (unsigned char) val & 0xff;
			seen_xdigits = 0;
			val = 0;
			continue;
		}
#if 0 /* disable IPv4 */
		if (ch == '.' && ((tp + NS_INADDRSZ) <= endp) &&
		    inet_pton4(curtok, tp, 1) > 0) {
			tp += NS_INADDRSZ;
			seen_xdigits = 0;
			break;	/*%< '\\0' was seen by inet_pton4(). */
		}
#endif
		if (endptr != NULL)
			*endptr = (char *)src - 1; /* UNCONST */
		return (GFARM_ERRMSG_INVALID_CHAR_IN_IP);
	}
	if (seen_xdigits) {
		if (tp + GFARM_NS_INT16SZ > endp) {
			if (endptr != NULL)
				*endptr = (char *)src - 1; /* UNCONST */
			return (GFARM_ERRMSG_TOO_BIG_BYTE_IN_IP);
		}
		*tp++ = (unsigned char) (val >> 8) & 0xff;
		*tp++ = (unsigned char) val & 0xff;
	}
	if (colonp != NULL) {
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		const ptrdiff_t n = tp - colonp;
		int i;

		if (tp == endp)
			return (0);
		for (i = 1; i <= n; i++) {
			endp[-i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}
	if (tp != endp) {
		if (endptr != NULL)
			*endptr = (char *)src - 1; /* UNCONST */
		return (GFARM_ERRMSG_IP_ADDRESS_TOO_SHORT);
	}
	memcpy(dst, tmp, GFARM_NS_IN6ADDRSZ);
	if (endptr != NULL)
		*endptr = (char *)src - 1; /* UNCONST */
	return (GFARM_ERR_NO_ERROR);
}

static int
gfarm_is_string_upper_case(const char *s)
{
	unsigned char *t = (unsigned char *)s;

	for (; *t != '\0'; t++) {
		if (!isupper(*t))
			return (0);
	}
	return (1);
}

static void
gfarm_ipv6_netmask_gen(unsigned char *mask, int prefixlen)
{
	int i;

	assert(prefixlen <= AF_INET6_BIT);

	memset(mask, 0, GFARM_IN6_ADDR_LEN);
	for (i = 0; prefixlen > 0; i++) {
		if (prefixlen >= 8) {
			mask[i] = 0xff;
			prefixlen -= 8;
		} else {
			mask[i] = ((1 << prefixlen) - 1) << (8 - prefixlen);
		}
	}
}

gfarm_error_t
gfarm_hostspec_parse(const char *name, struct gfarm_hostspec **hostspecpp)
{
	char *end1p, *end2p;
	struct in_addr addr_ipv4, mask_ipv4;
	struct in6_addr addr_ipv6, mask_ipv6;
	unsigned char ipv6_mask128[GFARM_IN6_ADDR_LEN] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	unsigned long masklen;

	if (strcmp(name, "*") == 0 || strcmp(name, "ALL") == 0)
		return (gfarm_hostspec_any_new(hostspecpp));

	if (gfarm_string_to_in4addr(name, &end1p, &addr_ipv4)
	    == GFARM_ERR_NO_ERROR) {
		if (*end1p == '\0') {
			return (gfarm_hostspec_af_inet4_new(
			    addr_ipv4.s_addr, INADDR_BROADCAST, hostspecpp));
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
					mask_ipv4.s_addr = INADDR_ANY;
				} else {
					masklen = AF_INET4_BIT - masklen;
					mask_ipv4.s_addr = htonl(
					    ~((1 << (gfarm_int32_t)masklen)
					      - 1));
				}
				return (gfarm_hostspec_af_inet4_new(
				    addr_ipv4.s_addr, mask_ipv4.s_addr,
				    hostspecpp));
			} else if (gfarm_string_to_in4addr(end1p + 1,
			    &end2p, &mask_ipv4) == GFARM_ERR_NO_ERROR &&
			    *end2p == '\0') {
				return (gfarm_hostspec_af_inet4_new(
				    addr_ipv4.s_addr, mask_ipv4.s_addr,
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
	if (gfarm_string_to_in6addr(name, &end1p, &addr_ipv6)
	    == GFARM_ERR_NO_ERROR) {
		if (*end1p == '\0') {
			return (gfarm_hostspec_af_inet6_new(
			    addr_ipv6.s6_addr, ipv6_mask128, hostspecpp));
		}
		if (*end1p == '/') {
			if (isdigit(((unsigned char *)end1p)[1]) &&
			    (masklen = strtoul(end1p + 1, &end2p, 10),
			     *end2p == '\0')) {
				if (masklen > AF_INET6_BIT) {
					gflog_debug(GFARM_MSG_UNFIXED,
					    "Netmask %lu for ipv6 is too big",
					    masklen);
					return (GFARM_ERRMSG_TOO_BIG_NETMASK);
				}
				gfarm_ipv6_netmask_gen(
				    mask_ipv6.s6_addr, masklen);
				return (gfarm_hostspec_af_inet6_new(
				    addr_ipv6.s6_addr, mask_ipv6.s6_addr,
				    hostspecpp));
			} else if (gfarm_string_to_in6addr(end1p + 1,
			    &end2p, &mask_ipv6) == GFARM_ERR_NO_ERROR &&
			    *end2p == '\0') {
				return (gfarm_hostspec_af_inet6_new(
				    addr_ipv6.s6_addr, mask_ipv6.s6_addr,
				    hostspecpp));
			}
		}
		if (!IS_DNS_LABEL_CHAR(*(unsigned char *)end1p) &&
		    *end1p != '.') {
			gflog_debug(GFARM_MSG_UNFIXED,
				"Invalid char in IPv6");
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
	int i;

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
	case GFHS_AF_INET6:
		if (addr == NULL)
			return (0);
		for (i = 0; i < GFARM_IN6_ADDR_LEN; i++) {
			if ((((struct sockaddr_in6 *)addr)->
			     sin6_addr.s6_addr[i] &
			     hostspecp->u.in6_addr.mask.s6_addr[i]) !=
			    hostspecp->u.in6_addr.addr.s6_addr[i])
				return (0);
		}
		return (1);
	}
	/* assert(0); */
	return (0);
}

/* "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff" */
#define GFARM_IPV6_STRLEN	40

void
gfarm_hostspec_to_string(struct gfarm_hostspec *hostspec,
	char *string, size_t size)
{
	unsigned char *a, *m;
	char addr[GFARM_IPV6_STRLEN];
	char mask[GFARM_IPV6_STRLEN];

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
	case GFHS_AF_INET6:
		inet_ntop(AF_INET6, hostspec->u.in6_addr.addr.s6_addr,
		    addr, sizeof(addr));
		inet_ntop(AF_INET6, hostspec->u.in6_addr.mask.s6_addr,
		    mask, sizeof(mask));
		snprintf(string, size, "%s/%s", addr, mask);
		return;
	}
	/* assert(0); */
	return;
}

#ifndef __KERNEL__	/* gfarm_sockaddr_to_name:: apl */
static gfarm_error_t
gfarm_sockaddr_to_name(struct sockaddr *addr, socklen_t addrlen, char **namep)
{
	int rv;
	struct addrinfo hints, *res, *res0;
	struct sockaddr_in *sa_ipv4_1, *sa_ipv4_2;
	struct sockaddr_in6 *sa_ipv6_1, *sa_ipv6_2;
	char *s, name[NI_MAXHOST];

	rv = gfarm_getnameinfo(addr, addrlen, name, sizeof(name),
	    NULL, 0, NI_NAMEREQD);
	if (rv != 0) {
		gflog_debug(GFARM_MSG_1000863,
		    "Cannot get name info from IP address: %s",
		    gai_strerror(rv));
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
		if (res->ai_family != addr->sa_family) /* failsafe */
			continue;
		switch (res->ai_family) {
		case AF_INET:
			sa_ipv4_1 = (struct sockaddr_in *)res->ai_addr;
			sa_ipv4_2 = (struct sockaddr_in *)addr;
			if (sa_ipv4_1->sin_addr.s_addr ==
			    sa_ipv4_2->sin_addr.s_addr) {
				gfarm_freeaddrinfo(res0);
				s = strdup(name);
				if (s == NULL)
					return (GFARM_ERR_NO_MEMORY);
				*namep = s;
				return (GFARM_ERR_NO_ERROR); /* success */
			}
			break;
		case AF_INET6:
			sa_ipv6_1 = (struct sockaddr_in6 *)res->ai_addr;
			sa_ipv6_2 = (struct sockaddr_in6 *)addr;
			if (memcmp(sa_ipv6_1->sin6_addr.s6_addr,
				   sa_ipv6_2->sin6_addr.s6_addr,
				   GFARM_IN6_ADDR_LEN) == 0) {
				gfarm_freeaddrinfo(res0);
				s = strdup(name);
				if (s == NULL)
					return (GFARM_ERR_NO_MEMORY);
				*namep = s;
				return (GFARM_ERR_NO_ERROR); /* success */
			}
			break;
		}
	}
	gfarm_freeaddrinfo(res0);
	return (GFARM_ERRMSG_REVERSE_LOOKUP_NAME_DOES_NOT_MATCH);
}

/*
 * NOTE: this is a utility function,
 * and usually never returns an error except some kind of fatal one,
 * and if an error happens in this function, always log the error by itself.
 */
gfarm_error_t
gfarm_sockaddr_to_log_string(
	struct sockaddr *addr, int addrlen, char **log_stringp)
{
	gfarm_error_t e = gfarm_sockaddr_to_name(addr, addrlen, log_stringp);

	if (e == GFARM_ERR_NO_ERROR) {
		return (e);
	} else {
		char *s, numeric[NI_MAXHOST];
		int rv;

		/*
		 * if the address has no PTR record,
		 * or the PTR record points a forged name,
		 * or memory allocation failed,
		 * just use numeric address
		 */
		rv = gfarm_getnameinfo(addr, addrlen, numeric, sizeof(numeric),
		    NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV);
		if (rv != 0) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "address family %d: error \"%s\" happens, and "
			    "cannot convert it to a numeric address: %s",
			    addr->sa_family, gfarm_error_string(e),
			    gai_strerror(rv));
			return (GFARM_ERR_INTERNAL_ERROR);
		}
		if (e == GFARM_ERRMSG_REVERSE_LOOKUP_NAME_IS_NOT_RESOLVABLE ||
		    e == GFARM_ERRMSG_REVERSE_LOOKUP_NAME_DOES_NOT_MATCH) {
			gflog_info(GFARM_MSG_UNFIXED,
			    "notice: reverse lookup of address [%s] "
			    "does not match the address", numeric);
		}
		s = strdup(numeric);
		if (s == NULL) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "duplicating address [%s]: no memory", numeric);
			return (GFARM_ERR_NO_MEMORY);
		}
		*log_stringp = s;
		return (GFARM_ERR_NO_ERROR); /* success */
	}
}
#endif /* __KERNEL__ */
