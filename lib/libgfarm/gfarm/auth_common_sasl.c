#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <netdb.h>
#include <sys/socket.h>

#include <gfarm/gfarm.h>

#include "gfnetdb.h"

#include <sasl/sasl.h>

#include "auth.h"

#define SASL_MECH_DELIMITER " \t"

static int
sasl_mechanism_is_in_list(const char *word, size_t len, const char *list)
{
	const char *q;
	size_t qlen;

	for (q = list; *q != '\0'; q += qlen) {
		q += strspn(q, SASL_MECH_DELIMITER);
		if (*q == '\0')
			break;
		qlen = strcspn(q, SASL_MECH_DELIMITER);
		if (len == qlen && strncasecmp(word, q, len) == 0)
			return (1);
	}
	return (0);
}

/*
 * returns 1, if the space-separated lists list1 and list2
 * have a common SASL mechanism name, otherwise returns 0.
 * mechanism names are compared case-insensitively.
 */
int
gfarm_sasl_mechanism_list_match(const char *list1, const char *list2)
{
	const char *p;
	size_t len;

	if (list1 == NULL || list2 == NULL)
		return (0);

	for (p = list1; *p != '\0'; p += len) {
		p += strspn(p, SASL_MECH_DELIMITER);
		if (*p == '\0')
			break;
		len = strcspn(p, SASL_MECH_DELIMITER);
		if (sasl_mechanism_is_in_list(p, len, list2))
			return (1);
	}
	return (0);
}

/*
 * remove SASL mechanism names which are not included in the list `allowed'
 * from the space-separated list `list', in place.
 * the order of the mechanism names in `list' is preserved.
 */
void
gfarm_sasl_mechanism_list_filter(char *list, const char *allowed)
{
	const char *p;
	char *q = list;
	size_t len;

	if (list == NULL || allowed == NULL)
		return;

	for (p = list; *p != '\0'; p += len) {
		p += strspn(p, SASL_MECH_DELIMITER);
		if (*p == '\0')
			break;
		len = strcspn(p, SASL_MECH_DELIMITER);
		if (!sasl_mechanism_is_in_list(p, len, allowed))
			continue;
		if (q != list)
			*q++ = ' ';
		memmove(q, p, len);
		q += len;
	}
	*q = '\0';
}

int
gfarm_sasl_log(void *context, int sasl_priority, const char *message)
{
	int prio;

	if (!gflog_auth_get_verbose())
		return (SASL_OK);

	switch (sasl_priority) {
	case SASL_LOG_NONE:
		prio = LOG_DEBUG;
		break;
	case SASL_LOG_ERR:
		prio = LOG_NOTICE; /* LOG_ERR raises zabbix alert: noisy */
		break;
	case SASL_LOG_FAIL:
		prio = LOG_NOTICE; /* LOG_ERR raises zabbix alert: noisy */
		break;
	case SASL_LOG_WARN:
		prio = LOG_NOTICE; /* LOG_WARNING raises zabbix alert: noisy */
		break;
	case SASL_LOG_NOTE:
		prio = LOG_INFO;
		break;
	case SASL_LOG_DEBUG:
		prio = LOG_DEBUG;
		break;
	case SASL_LOG_TRACE:
		prio = LOG_DEBUG;
		break;
	case SASL_LOG_PASS:
		prio = LOG_DEBUG;
		break;
	default:
		prio = LOG_DEBUG;
		break;
	}
	gflog_message(GFARM_MSG_1005366, prio, __FILE__, __LINE__, __func__,
	    "SASL: %s", message);

	return (SASL_OK);
}

int
gfarm_sasl_addr_string(int fd,
	char *self_hsbuf, size_t self_sz, char *peer_hsbuf, size_t peer_sz,
	const char *diag)
{
	int save_errno, ni_error;
	socklen_t self_len, peer_len;
	struct sockaddr_storage self_addr, peer_addr;
	char self_hbuf[NI_MAXHOST], peer_hbuf[NI_MAXHOST];
	char self_sbuf[NI_MAXSERV], peer_sbuf[NI_MAXSERV];

	self_len = sizeof(self_addr);
	if (getsockname(fd, (struct sockaddr *)&self_addr, &self_len) < 0) {
		save_errno = errno;
		gflog_error(GFARM_MSG_1005367,
		    "%s: getsockname: %s", diag, strerror(save_errno));
		return (save_errno);
	}

	if (self_addr.ss_family == AF_UNIX) {
		/* sasl_client_new() and sasl_server_new() doesn't work */
		gflog_debug(GFARM_MSG_1005735,
		    "%s: SASL authentication doesn't work with AF_UNIX", diag);
		return (EAFNOSUPPORT);
	}

	peer_len = sizeof(peer_addr);
	if (getpeername(fd, (struct sockaddr *)&peer_addr, &peer_len) < 0) {
		save_errno = errno;
		gflog_error(GFARM_MSG_1005368,
		    "%s: getpeername: %s", diag, strerror(save_errno));
		return (save_errno);
	}

	ni_error = gfarm_getnameinfo((struct sockaddr *)&self_addr, self_len,
	    self_hbuf, sizeof(self_hbuf), self_sbuf, sizeof(self_sbuf),
	    NI_NUMERICHOST | NI_NUMERICSERV | (
#ifdef NI_WITHSCOPEID
	    self_addr.ss_family == AF_INET ? NI_WITHSCOPEID :
#endif
	    0));
	if (ni_error != 0) { /* shouldn't happen */
		gflog_notice(GFARM_MSG_1005369,
		    "%s: getnameinfo(peer): %s", diag, gai_strerror(ni_error));
		strcpy(self_hbuf, "unknown_addr");
		strcpy(self_sbuf, "unknown_port");
	}
	snprintf(self_hsbuf, self_sz, "%s;%s", self_hbuf, self_sbuf);

	ni_error = gfarm_getnameinfo((struct sockaddr *)&peer_addr, peer_len,
	    peer_hbuf, sizeof(peer_hbuf), peer_sbuf, sizeof(peer_sbuf),
	    NI_NUMERICHOST | NI_NUMERICSERV | (
#ifdef NI_WITHSCOPEID
	    peer_addr.ss_family == AF_INET ? NI_WITHSCOPEID :
#endif
	    0));
	if (ni_error != 0) { /* shouldn't happen */
		gflog_notice(GFARM_MSG_1005370,
		    "%s: getnameinfo(peer): %s", diag, gai_strerror(ni_error));
		strcpy(peer_hbuf, "unknown_addr");
		strcpy(peer_sbuf, "unknown_port");
	}
	snprintf(peer_hsbuf, peer_sz, "%s;%s", peer_hbuf, peer_sbuf);

	return (0);
}
