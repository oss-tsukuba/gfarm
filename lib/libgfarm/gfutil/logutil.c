#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <gfarm/gfarm_misc.h>
#include "gfutil.h"

static char *log_identifier = "libgfarm";
static char *log_auxiliary_info = NULL;
static int log_use_syslog = 0;

void
gflog_message(int priority, char *message, char *status)
{
	if (log_auxiliary_info != NULL) {
		if (status != NULL) {
			if (log_use_syslog)
				syslog(priority, "(%s) %s: %s",
				       log_auxiliary_info, message, status);
			else
				fprintf(stderr, "%s: (%s) %s: %s\n",
					log_identifier,
					log_auxiliary_info, message, status);
		} else {
			if (log_use_syslog)
				syslog(priority, "(%s) %s",
				       log_auxiliary_info, message);
			else
				fprintf(stderr, "%s: (%s) %s\n",
					log_identifier,
					log_auxiliary_info, message);
		}
	} else {
		if (status != NULL) {
			if (log_use_syslog)
				syslog(priority, "%s: %s",
				       message, status);
			else
				fprintf(stderr, "%s: %s: %s\n",
					log_identifier,
					message, status);
		} else {
			if (log_use_syslog)
				syslog(priority, "%s",
				       message);
			else
				fprintf(stderr, "%s: %s\n",
					log_identifier,
					message);
		}
	}
}

void
gflog_error(char *message, char *status)
{
	gflog_message(LOG_ERR, message, status);
}

void
gflog_warning(char *message, char *status)
{
	gflog_message(LOG_WARNING, message, status);
}

void
gflog_warning_errno(char *message)
{
	gflog_warning(message, strerror(errno));
}

void
gflog_fatal(char *message, char *status)
{
	gflog_error(message, status);
	exit(2);
}

void
gflog_fatal_errno(char *message)
{
	gflog_fatal(message, strerror(errno));
}

void
gflog_set_identifier(char *identifier)
{
	log_identifier = identifier;
}

void
gflog_set_auxiliary_info(char *aux_info)
{
	log_auxiliary_info = aux_info;
}

void
gflog_syslog_open(int syslog_option, int syslog_facility)
{
	openlog(log_identifier, syslog_option, syslog_facility);
	log_use_syslog = 1;
}

int
gflog_syslog_name_to_facility(char *name)
{
	int i;
	struct {
		char *name;
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
