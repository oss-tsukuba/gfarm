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

void gflog_message(int, char *, char *);
void gflog_error(char *, char *);
void gflog_warning(char *, char *);
void gflog_notice(char *, char *);
void gflog_info(char *, char *);
void gflog_debug(char *, char *);
void gflog_warning_errno(char *);

void gflog_fatal(char *, char *);
void gflog_fatal_errno(char *);

void gflog_set_identifier(char *);
void gflog_set_auxiliary_info(char *);
char *gflog_get_auxiliary_info(void);
void gflog_syslog_open(int, int); 

int gflog_syslog_name_to_facility(char *);

/* timeval */

#define GFARM_MILLISEC_BY_MICROSEC	1000
#define GFARM_SECOND_BY_MICROSEC	1000000

struct timeval;
int gfarm_timeval_cmp(const struct timeval *, const struct timeval *);
void gfarm_timeval_add(struct timeval *, const struct timeval *);
void gfarm_timeval_sub(struct timeval *, const struct timeval *);
void gfarm_timeval_add_microsec(struct timeval *, long);
