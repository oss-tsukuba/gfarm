#ifndef _SYSLOG_H_
#define _SYSLOG_H_

#define LOG_EMERG       0       /* system is unusable */
#define LOG_ALERT       1       /* action must be taken immediately */
#define LOG_CRIT        2       /* critical conditions */
#define LOG_ERR         3       /* error conditions */
#define LOG_WARNING     4       /* warning conditions */
#define LOG_NOTICE      5       /* normal but significant condition */
#define LOG_INFO        6       /* informational */
#define LOG_DEBUG       7       /* debug-level messages */

#define openlog(ident, option, facility)
void syslog(int priority, const char *format, ...);
void closelog(void);

#endif /* _SYSLOG_H_ */

