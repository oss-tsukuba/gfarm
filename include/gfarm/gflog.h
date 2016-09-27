#include <syslog.h>

#include <gfarm/gfarm_msg_enums.h>

#ifndef GFARM_DEFAULT_FACILITY
#define GFARM_DEFAULT_FACILITY	LOG_LOCAL0
#endif

#ifndef GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG
#define GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG	LOG_INFO
#endif

#ifdef __GNUC__
#define GFLOG_PRINTF_ARG(M, N)	__attribute__((__format__(__printf__, M, N)))
#else
#define GFLOG_PRINTF_ARG(M, N)
#endif

#if (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L) && !defined(__func__)
#if __GNUC__ >= 2
#define __func__ __FUNCTION__
#else
#define __func__ "<unknown>"
#endif
#endif

#ifdef GFLOG_USE_STDARG /* to make <stdarg.h> optional to use <gfutil.h> */
void gflog_vmessage(int, int, const char *, int, const char *,
		const char *, va_list) GFLOG_PRINTF_ARG(6, 0);
void gflog_vmessage_errno(int, int, const char *, int, const char*,
		const char *, va_list) GFLOG_PRINTF_ARG(6, 0);
#endif
void gflog_message(int, int, const char *, int, const char*,
		const char *, ...) GFLOG_PRINTF_ARG(6, 7);
void gflog_message_errno(int, int, const char *, int, const char*,
		const char *, ...) GFLOG_PRINTF_ARG(6, 7);
void gflog_fatal_message(int, int, const char *, int, const char*,
		const char *, ...) GFLOG_PRINTF_ARG(6, 7);
void gflog_fatal_message_errno(int, int, const char *, int, const char*,
		const char *, ...) GFLOG_PRINTF_ARG(6, 7);
void gflog_assert_message(int, const char *, int, const char *,
		const char *, ...) GFLOG_PRINTF_ARG(5, 6);

#define gflog_fatal(msg_no, ...) \
	gflog_fatal_message(msg_no, LOG_ERR,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_error(msg_no, ...) \
	gflog_message(msg_no, LOG_ERR,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_warning(msg_no, ...) \
	gflog_message(msg_no, LOG_WARNING,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_notice(msg_no, ...) \
	gflog_message(msg_no, LOG_NOTICE,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_info(msg_no, ...) \
	gflog_message(msg_no, LOG_INFO,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_debug(msg_no, ...) \
	gflog_message(msg_no, LOG_DEBUG,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)

#define gflog_fatal_errno(msg_no, ...) \
	gflog_fatal_message_errno(msg_no, LOG_ERR,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_error_errno(msg_no, ...) \
	gflog_message_errno(msg_no, LOG_ERR,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_warning_errno(msg_no, ...) \
	gflog_message_errno(msg_no, LOG_WARNING,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_notice_errno(msg_no, ...) \
	gflog_message_errno(msg_no, LOG_NOTICE,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_info_errno(msg_no, ...) \
	gflog_message_errno(msg_no, LOG_INFO,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_debug_errno(msg_no, ...) \
	gflog_message_errno(msg_no, LOG_DEBUG,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)

#define gflog_auth_info(msg_no, ...)\
	gflog_auth_message(msg_no, LOG_INFO,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_auth_notice(msg_no, ...)\
	gflog_auth_message(msg_no, LOG_NOTICE,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_auth_error(msg_no, ...)\
	gflog_auth_message(msg_no, LOG_ERR,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)
#define gflog_auth_warning(msg_no, ...)\
	gflog_auth_message(msg_no, LOG_WARNING,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)

#define gflog_trace(msg_no, ...) \
	gflog_message(msg_no, LOG_INFO,\
			__FILE__, __LINE__, __func__, __VA_ARGS__)

void gflog_initialize(void);
void gflog_terminate(void);
void gflog_set_priority_level(int);
int gflog_get_priority_level(void);
void gflog_set_identifier(const char *);
void gflog_set_auxiliary_info(char *);
char *gflog_get_auxiliary_info(void);
void gflog_syslog_open(int, int);
int gflog_syslog_enabled(void);
int gflog_set_message_verbose(int);

int gflog_syslog_name_to_facility(const char *);
int gflog_syslog_name_to_priority(const char *);

/* logutil - gflog_auth_*() */

int gflog_auth_set_verbose(int);
int gflog_auth_get_verbose(void);
void gflog_auth_message(int, int, const char *, int, const char *,
	const char *, ...) GFLOG_PRINTF_ARG(6, 7);
