/* daemon */

#ifndef HAVE_DAEMON
int gfarm_daemon(int, int);
#else
#define gfarm_daemon	daemon
#endif

/* limit */

void gfarm_unlimit_nofiles(int *);

/* logutil */

#ifndef GFARM_DEFAULT_FACILITY
#define GFARM_DEFAULT_FACILITY	LOG_LOCAL0
#endif

void gflog_message(int, const char *, const char *);
void gflog_error(const char *, const char *);
void gflog_warning(const char *, const char *);
void gflog_notice(const char *, const char *);
void gflog_info(const char *, const char *);
void gflog_debug(const char *, const char *);
void gflog_warning_errno(const char *);

void gflog_fatal(const char *, const char *);
void gflog_fatal_errno(const char *);

void gflog_set_identifier(const char *);
void gflog_set_auxiliary_info(char *);
char *gflog_get_auxiliary_info(void);
void gflog_syslog_open(int, int); 

int gflog_syslog_name_to_facility(const char *);

/* timeval */

#define GFARM_MILLISEC_BY_MICROSEC	1000
#define GFARM_SECOND_BY_MICROSEC	1000000

struct timeval;
int gfarm_timeval_cmp(const struct timeval *, const struct timeval *);
void gfarm_timeval_add(struct timeval *, const struct timeval *);
void gfarm_timeval_sub(struct timeval *, const struct timeval *);
void gfarm_timeval_add_microsec(struct timeval *, long);
