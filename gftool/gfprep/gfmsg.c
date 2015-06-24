/*
 * $Id$
 */

#include <stdio.h>
#include <stdarg.h> /* va_list */
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "gfmsg.h"

static const char *program_name;
static int on_debug, on_info, on_warn;

static void
gfmsg_vfprintf(FILE *out, const char *level, int quiet, gfarm_error_t *ep,
		const char *format, va_list ap)
{
	if (quiet)
		return;
	if (level)
		fprintf(out, "%s: ", level);
	vfprintf(out, format, ap);
	if (ep)
		fprintf(out, ": %s\n", gfarm_error_string(*ep));
	else
		fprintf(out, "\n");
}

static void
gfmsg_err_v(const char *level, int quiet, gfarm_error_t *ep,
	     const char *format, va_list ap)
{
	if (quiet)
		return;
	gfmsg_vfprintf(stderr, level, quiet, ep, format, ap);
}

static void
gfmsg_msg_v(int quiet, const char *format, va_list ap)
{
	if (quiet)
		return;
	gfmsg_vfprintf(stdout, NULL, quiet, NULL, format, ap);
}

void
gfmsg_init(const char *progname, enum gfmsg_level level)
{
	program_name = progname;

	switch (level) {
	case GFMSG_LEVEL_ERROR:
		on_debug = 0;
		on_info = 0;
		on_warn = 0;
		break;
	case GFMSG_LEVEL_DEBUG:
		on_debug = 1;
		on_info = 1;
		on_warn = 1;
		break;
	case GFMSG_LEVEL_INFO:
		on_debug = 0;
		on_info = 1;
		on_warn = 1;
		break;
	case GFMSG_LEVEL_WARNING:
	default:
		on_debug = 0;
		on_info = 0;
		on_warn = 1;
		break;
	};
}

void
gfmsg_fatal_e(gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	if (e == GFARM_ERR_NO_ERROR)
		return;
	va_start(ap, format);
	gfmsg_err_v("FATAL", 0, &e, format, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void
gfmsg_fatal(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfmsg_err_v("FATAL", 0, NULL, format, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void
gfmsg_error_e(gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	if (e == GFARM_ERR_NO_ERROR)
		return;
	va_start(ap, format);
	gfmsg_err_v("ERROR", 0, &e, format, ap);
	va_end(ap);
}

void
gfmsg_error(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfmsg_err_v("ERROR", 0, NULL, format, ap);
	va_end(ap);
}

void
gfmsg_warn_e(gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	if (e == GFARM_ERR_NO_ERROR)
		return;
	va_start(ap, format);
	gfmsg_err_v("WARN", !on_warn, &e, format, ap);
	va_end(ap);
}

void
gfmsg_warn(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfmsg_err_v("WARN", !on_warn, NULL, format, ap);
	va_end(ap);
}

void
gfmsg_info_e(gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	if (e == GFARM_ERR_NO_ERROR)
		return;
	va_start(ap, format);
	gfmsg_err_v("INFO", !on_info, &e, format, ap);
	va_end(ap);
}

void
gfmsg_info(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfmsg_err_v("INFO", !on_info, NULL, format, ap);
	va_end(ap);
}

void
gfmsg_debug_e(gfarm_error_t e, const char *format, ...)
{
	va_list ap;

	if (e == GFARM_ERR_NO_ERROR)
		return;
	va_start(ap, format);
	gfmsg_err_v("DEBUG", !on_debug, &e, format, ap);
	va_end(ap);
}

void
gfmsg_debug(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfmsg_err_v("DEBUG", !on_debug, NULL, format, ap);
	va_end(ap);
}

void
gfmsg_print(int quiet, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfmsg_msg_v(quiet, format, ap);
	va_end(ap);
}

void
gfmsg_nomem_check(const void *p)
{
	if (p == NULL)
		gfmsg_fatal("no memory @ %s:%d", __FILE__, __LINE__);
}
