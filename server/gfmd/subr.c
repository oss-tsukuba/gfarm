#include <pthread.h>

#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "auth.h"

#include "subr.h"
#include "peer.h"

int debug_mode = 0;

static pthread_mutex_t giant_mutex;

void
giant_init(void)
{
	int err = pthread_mutex_init(&giant_mutex, NULL);

	if (err != 0)
		gflog_fatal("giant mutex init: %s", strerror(err));
}

void
giant_lock(void)
{
	int err = pthread_mutex_lock(&giant_mutex);

	if (err != 0)
		gflog_fatal("giant mutex lock: %s", strerror(err));
}

void
giant_unlock(void)
{
	int err = pthread_mutex_unlock(&giant_mutex);

	if (err != 0)
		gflog_fatal("giant mutex unlock: %s", strerror(err));
}

gfarm_error_t
create_detached_thread(void *(*thread_main)(void *), void *arg)
{
	int err;
	pthread_t thread_id;
	static int initialized = 0;
	static pthread_attr_t attr;

	/*
	 * currently, this function is only called from the main thread,
	 * so, it's safe to use mere static variable instead of pthread_once().
	 */
	if (!initialized) {
		err = pthread_attr_init(&attr);
		if (err != 0)
			gflog_fatal("pthread_attr_init(): %s", strerror(err));
		err = pthread_attr_setdetachstate(&attr,
		    PTHREAD_CREATE_DETACHED);
		if (err != 0)
			gflog_fatal("PTHREAD_CREATE_DETACHED: %s",
			    strerror(err));
		initialized = 1;
	}

	err = pthread_create(&thread_id, &attr, thread_main, arg);
	return (err == 0 ? GFARM_ERR_NO_ERROR : gfarm_errno_to_error(err));
}

int
accmode_to_op(gfarm_uint32_t flag)
{
	int op;

	switch (flag & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:	op = GFS_R_OK; break;
	case GFARM_FILE_WRONLY:	op = GFS_W_OK; break;
	case GFARM_FILE_RDWR:	op = GFS_R_OK|GFS_W_OK; break;
	case GFARM_FILE_LOOKUP:	op = 0; break;
	default:
		assert(0);
		op = 0;
	}
	return op;
}

gfarm_error_t
gfm_server_get_request(struct peer *peer, const char *diag,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int eof;
	struct gfp_xdr *client = peer_get_conn(peer);

	if (debug_mode)
		gflog_info("<%s> start receiving", diag);

	va_start(ap, format);
	e = gfp_xdr_vrecv(client, 0, &eof, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning("%s: %s", diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	if (eof) {
		gflog_warning("%s: missing RPC argument", diag);
		peer_record_protocol_error(peer);
		return (GFARM_ERR_PROTOCOL);
	}
	if (*format != '\0')
		gflog_fatal("%s: invalid format character to get request",
		    diag);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_put_reply(struct peer *peer, const char *diag,
	gfarm_error_t ecode, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);

	if (debug_mode)
		gflog_info("<%s> sending reply: %d", diag, (int)ecode);

	va_start(ap, format);
	e = gfp_xdr_send(client, "i", (gfarm_int32_t)ecode);
	if (e != GFARM_ERR_NO_ERROR) {
		va_end(ap);
		gflog_warning("%s: %s", diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	if (ecode == GFARM_ERR_NO_ERROR) {
		e = gfp_xdr_vsend(client, &format, &ap);
		if (e != GFARM_ERR_NO_ERROR) {
			va_end(ap);
			gflog_warning("%s: %s", diag, gfarm_error_string(e));
			peer_record_protocol_error(peer);
			return (e);
		}
		if (*format != '\0')
			gflog_fatal("%s: %s", diag,
			    "invalid format character to put reply");
	}
	va_end(ap);
	/* do not call gfp_xdr_flush() here for a compound protocol */

	return (ecode);
}
