#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#define GFLOG_USE_STDARG
#include "gfutil.h"

static const char *log_identifier = "libgfarm";
static char *log_auxiliary_info = NULL;
static int log_use_syslog = 0;
static int log_level = GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG;

int
gflog_syslog_enabled(void)
{
	return (log_use_syslog);
}

void
gflog_vmessage(int priority, const char *format, va_list ap)
{
	char buffer[2048];

	if (priority > log_level) /* not worth reporting */
		return;

	vsnprintf(buffer, sizeof buffer, format, ap);

	if (log_auxiliary_info != NULL) {
		if (log_use_syslog)
			syslog(priority, "(%s) %s",
			    log_auxiliary_info, buffer);
		else
			fprintf(stderr, "%s: (%s) %s\n",
			    log_identifier,
			    log_auxiliary_info, buffer);
	} else {
		if (log_use_syslog)
			syslog(priority, "%s", buffer);
		else
			fprintf(stderr, "%s: %s\n", log_identifier, buffer);
	}
}

void
gflog_message(int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(priority, format, ap);
	va_end(ap);
}

void
gflog_error(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(LOG_ERR, format, ap);
	va_end(ap);
}

void
gflog_warning(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(LOG_WARNING, format, ap);
	va_end(ap);
}

void
gflog_notice(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(LOG_NOTICE, format, ap);
	va_end(ap);
}

void
gflog_info(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(LOG_INFO, format, ap);
	va_end(ap);
}

void
gflog_debug(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(LOG_DEBUG, format, ap);
	va_end(ap);
}

void
gflog_warning_errno(const char *format, ...)
{
	int save_errno = errno;
	char buffer[2048];

	va_list ap;

	va_start(ap, format);
	vsnprintf(buffer, sizeof buffer, format, ap);
	va_end(ap);
	gflog_warning("%s: %s", buffer, strerror(save_errno));
}

void
gflog_fatal(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(LOG_ERR, format, ap);
	va_end(ap);
#if 0
	abort();
#endif
	exit(2);
}

void
gflog_fatal_errno(const char *format, ...)
{
	int save_errno = errno;
	char buffer[2048];

	va_list ap;

	va_start(ap, format);
	vsnprintf(buffer, sizeof buffer, format, ap);
	va_end(ap);
	gflog_fatal("%s: %s", buffer, strerror(save_errno));
}

void
gflog_set_priority_level(int priority)
{
	log_level = priority;
}

void
gflog_set_identifier(const char *identifier)
{
	log_identifier = identifier;
}

void
gflog_set_auxiliary_info(char *aux_info)
{
	log_auxiliary_info = aux_info;
}

char *
gflog_get_auxiliary_info(void)
{
	return log_auxiliary_info;
}

void
gflog_syslog_open(int syslog_option, int syslog_facility)
{
	openlog(log_identifier, syslog_option, syslog_facility);
	log_use_syslog = 1;
}

int
gflog_syslog_name_to_facility(const char *name)
{
	int i;
	struct {
		const char *name;
		int facility;
	} syslog_facilities[] = {
		{ "kern",	LOG_KERN },
		{ "user",	LOG_USER },
		{ "mail",	LOG_MAIL },
		{ "daemon",	LOG_DAEMON },
		{ "auth",	LOG_AUTH },
		{ "syslog",	LOG_SYSLOG },
		{ "lpr",	LOG_LPR },
		{ "news",	LOG_NEWS },
		{ "uucp",	LOG_UUCP },
		{ "cron",	LOG_CRON },
#ifdef LOG_AUTHPRIV
		{ "authpriv",	LOG_AUTHPRIV },
#endif
#ifdef LOG_FTP
		{ "ftp",	LOG_FTP },
#endif
		{ "local0",	LOG_LOCAL0 },
		{ "local1",	LOG_LOCAL1 },
		{ "local2",	LOG_LOCAL2 },
		{ "local3",	LOG_LOCAL3 },
		{ "local4",	LOG_LOCAL4 },
		{ "local5",	LOG_LOCAL5 },
		{ "local6",	LOG_LOCAL6 },
		{ "local7",	LOG_LOCAL7 },
	};

	for (i = 0; i < GFARM_ARRAY_LENGTH(syslog_facilities); i++) {
		if (strcmp(syslog_facilities[i].name, name) == 0)
			return (syslog_facilities[i].facility);
	}
	return (-1); /* not found */
}

int
gflog_syslog_name_to_priority(const char *name)
{
	int i;
	struct {
		char *name;
		int priority;
	} syslog_priorities[] = {
		{ "emerg",	LOG_EMERG },
		{ "alert",	LOG_ALERT },
		{ "crit",	LOG_CRIT },
		{ "err",	LOG_ERR },
		{ "warning",	LOG_WARNING },
		{ "notice",	LOG_NOTICE },
		{ "info",	LOG_INFO },
		{ "debug",	LOG_DEBUG },
	};

	for (i = 0; i < GFARM_ARRAY_LENGTH(syslog_priorities); i++) {
		if (strcmp(syslog_priorities[i].name, name) == 0)
			return (syslog_priorities[i].priority);
	}
	return (-1); /* not found */
}

/*
 * authentication log
 */

static int authentication_verbose;

int
gflog_auth_set_verbose(int verbose)
{
	int old = authentication_verbose;

	authentication_verbose = verbose;
	return (old);
}

int
gflog_auth_get_verbose(void)
{
	return (authentication_verbose);
}

void
gflog_auth_error(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	if (authentication_verbose)
		gflog_vmessage(LOG_ERR, format, ap);
	va_end(ap);
}

void
gflog_auth_warning(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	if (authentication_verbose)
		gflog_vmessage(LOG_WARNING, format, ap);
	va_end(ap);
}

