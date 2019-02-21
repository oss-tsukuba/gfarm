#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <nl_types.h>
#include <time.h>
#include <pthread.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#define GFLOG_USE_STDARG
#include <gfarm/gflog.h>

#include "thrsubr.h"
#include "gfutil.h"
#include "gflog_reduced.h"

#define LOG_LENGTH_MAX	2048

#define GFARM_CATALOG_SET_NO 1

static const char *log_identifier = "libgfarm";
static char *log_auxiliary_info = NULL;
static int log_use_syslog = 0;
static int log_level = GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG;

static nl_catd catd = (nl_catd)-1;
static const char *catalog_file = "gfarm.cat";

static int log_message_verbose;
#define LOG_VERBOSE_COMPACT	0
#define LOG_VERBOSE_LINENO	1
#define LOG_VERBOSE_LINENO_FUNC	2

static int fatal_action = 0;

int
gflog_set_message_verbose(int new)
{
	int old = log_message_verbose;

	log_message_verbose = new;
	return (old);
}

int
gflog_syslog_enabled(void)
{
	return (log_use_syslog);
}

static void
gflog_catopen(const char *file)
{
	if (file == NULL)
		catd = catopen(catalog_file, 0);
	else
		catd = catopen(file, 0);
}

static void
gflog_catclose(void)
{
	catclose(catd);
}

void
gflog_initialize(void)
{
	gflog_catopen(NULL);
}

void
gflog_terminate(void)
{
	gflog_catclose();
}

#define GFLOG_PRIORITY_SIZE	8
static pthread_once_t gflog_priority_string_once = PTHREAD_ONCE_INIT;
static char *gflog_priority_string[GFLOG_PRIORITY_SIZE];

static struct {
	char *name;
	int priority;
} gflog_syslog_priorities[] = {
	{ "emerg",	LOG_EMERG },
	{ "alert",	LOG_ALERT },
	{ "crit",	LOG_CRIT },
	{ "err",	LOG_ERR },
	{ "warning",	LOG_WARNING },
	{ "notice",	LOG_NOTICE },
	{ "info",	LOG_INFO },
	{ "debug",	LOG_DEBUG },
};

static void
gflog_set_priority_string(int priority, char *string)
{
	if (priority >= 0 && priority < GFLOG_PRIORITY_SIZE)
		gflog_priority_string[priority] = string;
}

static void
gflog_init_priority_string(void)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gflog_syslog_priorities); i++)
		gflog_set_priority_string(
		    gflog_syslog_priorities[i].priority,
		    gflog_syslog_priorities[i].name);
}

static void
gflog_out(int priority, const char *str1, const char *str2)
{
#ifndef __KERNEL__	/* gflog_out :: printk */
	pthread_once(&gflog_priority_string_once, gflog_init_priority_string);
	if (log_use_syslog)
		syslog(priority, "<%s> %s%s",
		    gflog_priority_string[priority], str1, str2);
	else
		fprintf(stderr, "%s: <%s> %s%s\n", log_identifier,
		    gflog_priority_string[priority], str1, str2);
#else /* __KERNEL__ */
	if (log_use_syslog)
		printk("<%d>%s%s\n", priority, str1, str2);
	else
		printk("%s%s\n", str1, str2);
#endif /* __KERNEL__ */
}

#define GFLOG_SNPRINTF(buf, bp, endp, ...) \
{ \
	int s = snprintf(bp, (endp) - (bp), __VA_ARGS__); \
	if (s < 0 || s >= (endp) - (bp)) \
		break; \
	(bp) += s; \
}

/* "(priority <= log_level)" should be checked by caller */
static void
gflog_vmessage_out(int verbose, int msg_no, int priority,
	const char *file, int line_no, const char *func,
	const char *format, va_list ap)
{
	int rv;
	/* use static, because stack is too small (e.g. 4KB) if __KERNEL__ */
	static pthread_mutex_t buf_mutex = GFARM_MUTEX_INITIALIZER(buf_mutex);
	static char buf[LOG_LENGTH_MAX];
	char *bp = buf, *endp = buf + sizeof buf - 1;

	rv = pthread_mutex_lock(&buf_mutex);
	if (rv != 0) {
		gflog_out(LOG_ERR, "gflog_vmessage_out: pthread_mutex_lock: ",
		    strerror(rv));
		return;
	}

	/* the last one is used as a terminator */
	*endp = '\0';

	do { /* use do {...} while(0) to use break statement to discontinue */
		GFLOG_SNPRINTF(buf, bp, endp, "[%06d] ", msg_no);
		if (verbose >= LOG_VERBOSE_LINENO) {
			GFLOG_SNPRINTF(buf, bp, endp, "(%s:%d", file, line_no);
			if (verbose >= LOG_VERBOSE_LINENO_FUNC)
				GFLOG_SNPRINTF(buf, bp, endp, " %s()", func);
			GFLOG_SNPRINTF(buf, bp, endp, ") ");
		}
		if (log_auxiliary_info != NULL)
			GFLOG_SNPRINTF(buf, bp, endp, "(%s) ",
			    log_auxiliary_info);
	} while (0);

	vsnprintf(bp, endp - bp, format, ap);
	gflog_out(priority, "", buf);

	rv = pthread_mutex_unlock(&buf_mutex);
	if (rv != 0)
		gflog_out(LOG_ERR, "gflog_vmessage_out: pthread_mutex_unlock: ",
		    strerror(rv));
}

/* "(priority <= log_level)" should be checked by caller */
static void
gflog_message_out(int verbose, int msg_no, int priority,
	const char *file, int line_no, const char *func,
	const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage_out(verbose, msg_no, priority, file, line_no, func,
	    format, ap);
	va_end(ap);
}

/* "(priority <= log_level)" should be checked by caller */
static void
gflog_vmessage_catalog_out(int verbose, int priority,
	int msg_no, const char *file, int line_no, const char *func,
	const char *format, va_list ap)
{
	char *catmsg;

	catmsg = catgets(catd, GFARM_CATALOG_SET_NO, msg_no, NULL);

	gflog_vmessage_out(verbose, priority,
	    msg_no, file, line_no, func, catmsg != NULL ? catmsg : format, ap);
}

void
gflog_vmessage(int msg_no, int priority, const char *file, int line_no,
	const char *func, const char *format, va_list ap)
{
	if (priority > log_level) /* not worth reporting */
		return;

	gflog_vmessage_catalog_out(log_message_verbose,
	    msg_no, priority, file, line_no, func, format, ap);
}

void
gflog_message(int msg_no, int priority, const char *file, int line_no,
	const char *func, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(msg_no, priority, file, line_no, func, format, ap);
	va_end(ap);
}

/*
 * fatal action
 */
void
gflog_set_fatal_action(int action)
{
	fatal_action = action;
}

static int
gflog_get_fatal_action(void)
{
	return (fatal_action);
}

static struct {
	char *name;
	int action;
} gflog_fatal_actions[] = {
	{ "backtrace_and_exit",		GFLOG_FATAL_ACTION_EXIT_BACKTRACE },
	{ "backtrace_and_abort",	GFLOG_FATAL_ACTION_ABORT_BACKTRACE },
	{ "exit",			GFLOG_FATAL_ACTION_EXIT },
	{ "abort",			GFLOG_FATAL_ACTION_ABORT },
};

int
gflog_fatal_action_name_to_number(const char *name)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gflog_fatal_actions); i++) {
		if (strcmp(gflog_fatal_actions[i].name, name) == 0)
			return (gflog_fatal_actions[i].action);
	}
	return (GFLOG_ERROR_INVALID_FATAL_ACTION_NAME); /* not found */
}

static void
gfarm_log_fatal_action(int msg_no)
{
	gflog_notice(GFARM_MSG_1005053,
	    "now aborting due to the message [%06d]", msg_no);

	switch (gflog_get_fatal_action()) {
	case GFLOG_FATAL_ACTION_EXIT_BACKTRACE:
		gfarm_log_backtrace_symbols();
		exit(2);
	case GFLOG_FATAL_ACTION_ABORT_BACKTRACE:
		gfarm_log_backtrace_symbols();
		abort();
	case GFLOG_FATAL_ACTION_EXIT:
		exit(2);
	case GFLOG_FATAL_ACTION_ABORT:
		abort();
	default:
		abort();
	}
}

void
gflog_fatal_message(int msg_no, int priority, const char *file, int line_no,
	const char *func, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage(msg_no, priority, file, line_no, func, format, ap);
	va_end(ap);

	gfarm_log_fatal_action(msg_no);
}

void
gflog_vmessage_errno(int msg_no, int priority, const char *file, int line_no,
	const char *func, const char *format, va_list ap)
{
	int rv, save_errno = errno;
	char *catmsg;
	/* use static, because stack is too small (e.g. 4KB) if __KERNEL__ */
	static char buf[LOG_LENGTH_MAX];
	static pthread_mutex_t buf_mutex = GFARM_MUTEX_INITIALIZER(buf_mutex);

	if (priority > log_level) /* not worth reporting */
		return;

	rv = pthread_mutex_lock(&buf_mutex);
	if (rv != 0) {
		gflog_out(LOG_ERR, "gflog_vmessage_errno: pthread_mutex_lock: ",
		    strerror(rv));
		return;
	}

	catmsg = catgets(catd, GFARM_CATALOG_SET_NO, msg_no, NULL);

	vsnprintf(buf, sizeof buf, catmsg != NULL ? catmsg : format, ap);
	gflog_message_out(log_message_verbose,
	    msg_no, priority, file, line_no, func,
	    "%s: %s", buf, strerror(save_errno));

	rv = pthread_mutex_unlock(&buf_mutex);
	if (rv != 0)
		gflog_out(LOG_ERR,
		    "gflog_vmessage_errno: pthread_mutex_unlock: ",
		    strerror(rv));
}

void
gflog_message_errno(int msg_no, int priority, const char *file, int line_no,
	const char *func, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage_errno(msg_no, priority, file, line_no, func, format, ap);
	va_end(ap);
}

void
gflog_fatal_message_errno(int msg_no, int priority, const char *file,
	int line_no, const char *func, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage_errno(msg_no, priority, file, line_no, func, format, ap);
	va_end(ap);

	gfarm_log_fatal_action(msg_no);
}

void
gflog_assert_message(int msg_no, const char *file, int line_no,
	const char *func, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gflog_vmessage_catalog_out(LOG_VERBOSE_LINENO_FUNC,
	    msg_no, LOG_ERR, file, line_no, func, format, ap);
	va_end(ap);

	gfarm_log_fatal_action(msg_no);
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
	return (log_auxiliary_info);
}

void
gflog_syslog_open(int syslog_option, int syslog_facility)
{
	openlog(log_identifier, syslog_option, syslog_facility);
	log_use_syslog = 1;
}

#ifndef __KERNEL__	/* gflog_syslog_name_to_facility :: not use */
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
#endif /* __KERNEL__ */

int
gflog_syslog_name_to_priority(const char *name)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gflog_syslog_priorities); i++) {
		if (strcmp(gflog_syslog_priorities[i].name, name) == 0)
			return (gflog_syslog_priorities[i].priority);
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
gflog_auth_message(int msg_no, int priority, const char *file, int line_no,
	const char *func, const char *format, ...)
{
	va_list ap;

	if (authentication_verbose) {
		va_start(ap, format);
		gflog_vmessage(msg_no, priority, file, line_no, func,
		    format, ap);
		va_end(ap);
	}
}

/*
 * reduced log
 */

static int
gflog_reduced_rate_is_low(struct gflog_reduced_state *state,
	time_t current_time)
{
	return (current_time > state->stat_start &&
	     1.0 * state->stat_count / (current_time - state->stat_start)
	     <= 1.0 * state->threshold / state->duration);
}

/*
 * If rate is less than or equal to a limit, just output the log
 * (limit == state->threshold times per state->duration seconds).
 * If the rate exceeds that, the log will be output only once
 * per state->log_interval seconds, until the rate drops to the limit.
 */
void
gflog_reduced_message(int msg_no, int priority, const char *file, int line_no,
	const char *func, struct gflog_reduced_state *state,
	const char *format, ...)
{
	time_t current_time;
	int rv;
	va_list ap;
	char *catmsg;
	/* use static, because stack is too small (e.g. 4KB) if __KERNEL__ */
	static char buf[LOG_LENGTH_MAX];
	static pthread_mutex_t buf_mutex = GFARM_MUTEX_INITIALIZER(buf_mutex);

	if (priority > log_level) /* not worth reporting */
		return;

	++state->log_count;
	++state->stat_count;
	current_time = time(NULL);
	if (state->stat_count >= state->trigger &&
	    state->stat_start != 0 &&
	    !gflog_reduced_rate_is_low(state, current_time)) {
		state->reduced_mode = 1;
	}
	if (state->reduced_mode) {
		if (state->log_time != 0 &&
		    current_time - state->log_time < state->log_interval)
			return;
		if (gflog_reduced_rate_is_low(state, current_time))
			state->reduced_mode = 0;
	}

	rv = pthread_mutex_lock(&buf_mutex);
	if (rv != 0) {
		gflog_out(LOG_ERR,
		    "gflog_reduced_message: pthread_mutex_lock: ",
		    strerror(rv));
		return;
	}

	va_start(ap, format);
	catmsg = catgets(catd, GFARM_CATALOG_SET_NO, msg_no, NULL);
	vsnprintf(buf, sizeof buf, catmsg != NULL ? catmsg : format, ap);
	va_end(ap);
	if (state->log_count == 1) {
		gflog_message_out(log_message_verbose, msg_no, priority,
		    file, line_no, func, "%s", buf);
	} else {
		gflog_message_out(log_message_verbose, msg_no, priority,
		    file, line_no, func,
		    "%s: %ld times in recent %ld seconds", buf,
		    state->log_count, (long)(current_time - state->log_time));
	}

	rv = pthread_mutex_unlock(&buf_mutex);
	if (rv != 0)
		gflog_out(LOG_ERR,
		    "gflog_reduced_message: pthread_mutex_unlock: ",
		    strerror(rv));

	state->log_count = 0;
	state->log_time = current_time;

	if (state->stat_start == 0) {
		state->stat_start = current_time;
	} else if (current_time - state->stat_start >= state->duration) {
		state->stat_count = 0;
		state->stat_start = current_time;
	}
}
