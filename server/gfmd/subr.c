#include <pthread.h>

#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "auth.h"

#include "thrsubr.h"
#include "subr.h"
#include "peer.h"

int debug_mode = 0;

static pthread_mutex_t giant_mutex;

void
giant_init(void)
{
	mutex_init(&giant_mutex, "giant_init", "giant");
}

void
giant_lock(void)
{
	mutex_lock(&giant_mutex, "giant_lock", "giant");
}

void
giant_unlock(void)
{
	mutex_unlock(&giant_mutex, "giant_unlock", "giant");
}

pthread_attr_t gfarm_pthread_attr;

void
gfarm_pthread_attr_init(void)
{
	int err;

	err = pthread_attr_init(&gfarm_pthread_attr);
	if (err != 0)
		gflog_fatal(GFARM_MSG_1000223,
		    "pthread_attr_init(): %s", strerror(err));
	err = pthread_attr_setdetachstate(&gfarm_pthread_attr,
	    PTHREAD_CREATE_DETACHED);
	if (err != 0)
		gflog_fatal(GFARM_MSG_1000224,
		    "PTHREAD_CREATE_DETACHED: %s", strerror(err));
	gfarm_pthread_attr_setstacksize(&gfarm_pthread_attr);
}


pthread_attr_t *
gfarm_pthread_attr_get(void)
{
	static pthread_once_t gfarm_pthread_attr_initialized =
	    PTHREAD_ONCE_INIT;

	pthread_once(&gfarm_pthread_attr_initialized,
	    gfarm_pthread_attr_init);

	return (&gfarm_pthread_attr);
}

gfarm_error_t
create_detached_thread(void *(*thread_main)(void *), void *arg)
{
	int err;
	pthread_t thread_id;

	err = pthread_create(&thread_id, gfarm_pthread_attr_get(),
	    thread_main, arg);
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
		gflog_info(GFARM_MSG_1000225, "<%s> start receiving", diag);

	va_start(ap, format);
	e = gfp_xdr_vrecv(client, 0, &eof, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000226,
		    "%s receiving request: %s", diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	if (eof) {
		gflog_warning(GFARM_MSG_1000227,
		    "%s receiving request: missing RPC argument", diag);
		peer_record_protocol_error(peer);
		return (GFARM_ERR_PROTOCOL);
	}
	if (*format != '\0')
		gflog_fatal(GFARM_MSG_1000228,
		    "%s receiving request: invalid format character",
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
		gflog_info(GFARM_MSG_1000229,
		    "<%s> sending reply: %d", diag, (int)ecode);

	va_start(ap, format);
	e = gfp_xdr_send(client, "i", (gfarm_int32_t)ecode);
	if (e != GFARM_ERR_NO_ERROR) {
		va_end(ap);
		gflog_warning(GFARM_MSG_1000230,
		    "%s sending reply: %s", diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	if (ecode == GFARM_ERR_NO_ERROR) {
		e = gfp_xdr_vsend(client, &format, &ap);
		if (e != GFARM_ERR_NO_ERROR) {
			va_end(ap);
			gflog_warning(GFARM_MSG_1000231,
			    "%s sending reply: %s",
			    diag, gfarm_error_string(e));
			peer_record_protocol_error(peer);
			return (e);
		}
		if (*format != '\0')
			gflog_fatal(GFARM_MSG_1000232,
			    "%s sending reply: %s", diag,
			    "invalid format character");
	}
	va_end(ap);
	/* do not call gfp_xdr_flush() here for a compound protocol */

	return (ecode);
}
