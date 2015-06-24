/*
 * $Id$
 */

enum gfmsg_level {
	GFMSG_LEVEL_ERROR,
	GFMSG_LEVEL_WARNING,
	GFMSG_LEVEL_INFO,
	GFMSG_LEVEL_DEBUG
};

void gfmsg_init(const char *, enum gfmsg_level);

void gfmsg_fatal_e(gfarm_error_t, const char *, ...) GFLOG_PRINTF_ARG(2, 3);
void gfmsg_error_e(gfarm_error_t, const char *, ...) GFLOG_PRINTF_ARG(2, 3);
void gfmsg_warn_e(gfarm_error_t, const char *, ...)  GFLOG_PRINTF_ARG(2, 3);
void gfmsg_info_e(gfarm_error_t, const char *, ...)  GFLOG_PRINTF_ARG(2, 3);
void gfmsg_debug_e(gfarm_error_t, const char *, ...)  GFLOG_PRINTF_ARG(2, 3);

void gfmsg_fatal(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gfmsg_error(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gfmsg_warn(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gfmsg_info(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gfmsg_debug(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gfmsg_print(int, const char *, ...) GFLOG_PRINTF_ARG(2, 3);

void gfmsg_nomem_check(const void *);
