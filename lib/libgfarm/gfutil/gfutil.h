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
