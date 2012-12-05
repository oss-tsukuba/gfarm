/* alloc */
size_t gfarm_size_add(int *, size_t, size_t);
size_t gfarm_size_mul(int *, size_t, size_t);

/* backtrace */
void gfarm_log_backtrace_symbols(void);

/* daemon */

#ifndef HAVE_DAEMON
int gfarm_daemon(int, int);
#else
#define gfarm_daemon	daemon
#endif

/* limit */

int gfarm_limit_nofiles(int *);

/* logutil */

#define GFLOG_ERROR_INVALID_FATAL_ACTION_NAME -1
enum gflog_fatal_actions {
	GFLOG_FATAL_ACTION_EXIT_BACKTRACE,
	GFLOG_FATAL_ACTION_ABORT_BACKTRACE,
	GFLOG_FATAL_ACTION_EXIT,
	GFLOG_FATAL_ACTION_ABORT,
};
void gflog_set_fatal_action(int);
int gflog_fatal_action_name_to_number(const char *);

#ifdef GFLOG_PRINTF_ARG /* export this only if <gfarm/gflog.h> is included */

#include <time.h>
struct gflog_reduced_state {
	int reduced_mode;
	time_t stat_start, log_time;
	long stat_count, log_count;

	/* configuration constants per each log type */
	int trigger; /* check reduced mode, if count exceeds this */
	int threshold; /* reduce, if rate exceeds threshold/duration */
	int duration; /* seconds: see above */
	int log_interval; /* seconds: interval of reduced log */
};

#define GFLOG_REDUCED_STATE_INITIALIZER( \
	trigger, threshold, duration, log_interval) \
	{ \
		0, 0, 0, 0, 0, \
		trigger, threshold, duration, log_interval \
	}

void gflog_reduced_message(int, int, const char *, int, const char *,
	struct gflog_reduced_state *,
	const char *, ...) GFLOG_PRINTF_ARG(7, 8);

#define gflog_reduced_error(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_ERR,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
#define gflog_reduced_warning(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_WARNING,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
#define gflog_reduced_notice(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_NOTICE,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
#define gflog_reduced_info(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_INFO,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)
#define gflog_reduced_debug(msg_no, state, ...)	\
	gflog_reduced_message(msg_no, LOG_DEBUG,\
	    __FILE__, __LINE__, __func__, state, __VA_ARGS__)

#endif /* GFLOG_PRINTF_ARG */

/* random */

long gfarm_random(void);

/* send_no_sigpipe */

void gfarm_sigpipe_ignore(void);
ssize_t gfarm_send_no_sigpipe(int, const void *, size_t);

/* sleep */

void gfarm_sleep(long);

/* timeval */

#define GFARM_MILLISEC_BY_MICROSEC	1000
#define GFARM_SECOND_BY_MICROSEC	1000000

struct timeval;
int gfarm_timeval_cmp(const struct timeval *, const struct timeval *);
void gfarm_timeval_add(struct timeval *, const struct timeval *);
void gfarm_timeval_sub(struct timeval *, const struct timeval *);
void gfarm_timeval_add_microsec(struct timeval *, long);
int gfarm_timeval_is_expired(const struct timeval *);

/* utf8 */

int gfarm_utf8_validate_string(const char *);
int gfarm_utf8_validate_sequences(const char *s, size_t);
