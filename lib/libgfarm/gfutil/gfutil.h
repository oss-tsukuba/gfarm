void unlimit_nofiles(int *);

/* logutil */

#ifndef GFARM_DEFAULT_FACILITY
#define GFARM_DEFAULT_FACILITY	LOG_LOCAL0
#endif

void log_message(int, char *, char *);
void log_error(char *, char *);
void log_warning(char *, char *);
void log_warning_errno(char *);

void fatal(char *, char *);
void fatal_errno(char *);

void log_set_identifier(char *);
void log_set_auxiliary_info(char *);
void log_open_syslog(int, int); 

int syslog_name_to_facility(char *);
