#include <pthread.h>

#include <stdarg.h>
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "auth.h"

#include "subr.h"
#include "peer.h"

static pthread_mutex_t giant_mutex;

void
giant_init(void)
{
	int err = pthread_mutex_init(&giant_mutex, NULL);

	if (err != 0)
		gflog_fatal("giant mutex init", strerror(err));
}

void
giant_lock(void)
{
	int err = pthread_mutex_lock(&giant_mutex);

	if (err != 0)
		gflog_fatal("giant mutex lock", strerror(err));
}

void
giant_unlock(void)
{
	int err = pthread_mutex_unlock(&giant_mutex);

	if (err != 0)
		gflog_fatal("giant mutex unlock", strerror(err));
}

gfarm_error_t
gfm_server_get_request(struct peer *peer, char *diag, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int eof;
	struct gfp_xdr *client = peer_get_conn(peer);

	va_start(ap, format);
	e = gfp_xdr_vrecv(client, 0, &eof, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(diag, gfarm_error_string(e));
		return (e);
	}
	if (eof) {
		gflog_warning(diag, "missing RPC argument");
		return (GFARM_ERR_PROTOCOL);
	}
	if (*format != '\0')
		gflog_fatal(diag, "invalid format character to get request");
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_put_reply(struct peer *peer, char *diag,
	int ecode, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);

	va_start(ap, format);
	e = gfp_xdr_send(client, "i", (gfarm_int32_t)ecode);
	if (e != GFARM_ERR_NO_ERROR) {
		va_end(ap);
		gflog_warning(diag, gfarm_error_string(e));
		return (e);
	}
	if (ecode == GFARM_ERR_NO_ERROR) {
		e = gfp_xdr_vsend(client, &format, &ap);
		if (e != GFARM_ERR_NO_ERROR) {
			va_end(ap);
			gflog_warning(diag, gfarm_error_string(e));
			return (e);
		}
		if (*format != '\0')
			gflog_fatal(diag,
			    "invalid format character to put reply");
	}
	va_end(ap);

	return (GFARM_ERR_NO_ERROR);
}
