/*
 * $Id$
 */

#ifdef _REENTRANT

extern char **gflog_log_identifier_location(void);
extern char **gflog_log_auxiliary_info_location(void);
extern int *gflog_log_use_syslog_location(void);
extern int *gflog_authentication_verbose_location(void);

#define log_identifier (*gflog_log_identifier_location())
#define log_auxiliary_info (*gflog_log_auxiliary_info_location())
#define log_use_syslog (*gflog_log_use_syslog_location())
#define authentication_verbose (*gflog_authentication_verbose_location())

#endif
