#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <syslog.h>
#include <netdb.h>
#include <sys/socket.h>

#include <gfarm/gfarm.h>

#include "gfnetdb.h"

#include <sasl/sasl.h>

int
gfarm_sasl_log(void *context, int sasl_priority, const char *message)
{
	int prio;

	switch (sasl_priority) {
	case SASL_LOG_NONE:
		prio = LOG_DEBUG;
		break;
	case SASL_LOG_ERR:
		prio = LOG_ERR;
		break;
	case SASL_LOG_FAIL:
		prio = LOG_ERR;
		break;
	case SASL_LOG_WARN:
		prio = LOG_WARNING;
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
	gflog_message(GFARM_MSG_UNFIXED, prio, __FILE__, __LINE__, __func__,
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
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: getsockname: %s", diag, strerror(save_errno));
		return (save_errno);
	}

	peer_len = sizeof(peer_addr);
	if (getpeername(fd, (struct sockaddr *)&peer_addr, &peer_len) < 0) {
		save_errno = errno;
		gflog_error(GFARM_MSG_UNFIXED,
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
		gflog_notice(GFARM_MSG_UNFIXED,
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
		gflog_notice(GFARM_MSG_UNFIXED,
		    "%s: getnameinfo(peer): %s", diag, gai_strerror(ni_error));
		strcpy(peer_hbuf, "unknown_addr");
		strcpy(peer_sbuf, "unknown_port");
	}
	snprintf(peer_hsbuf, peer_sz, "%s;%s", peer_hbuf, peer_sbuf);

	return (0);
}
