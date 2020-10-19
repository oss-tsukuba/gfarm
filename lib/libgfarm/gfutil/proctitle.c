#include <gfarm/gfarm_config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>			/* for new BSD: setproctitle(3) */
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>			/* "BSD" symbol */
#ifdef HAVE_SYS_PSTAT_H			/* for HP-UX */
#include <sys/pstat.h>
#endif
#ifdef HAVE_PS_STRINGS			/* for old BSD */
#include <sys/exec.h>
#include <machine/vmparam.h>
#endif
#if defined(__darwin__) || defined(__APPLE__)
#include <crt_externs.h>		/* _NSGetArgv() */
#endif

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

/*
 * choose implmentation
 */
#if defined(HAVE_SETPROCTITLE)
#	define PROCTITLE_USE_SETPROCTITLE
#elif defined(HAVE_SYS_PSTAT_H) && defined(HAVE_PSTAT) && defined(PSTAT_SETCMD)
#	define PROCTITLE_USE_PSTAT_SETCMD
#elif defined(HAVE_PS_STRINGS)
#	define PROCTITLE_USE_PS_STRINGS
#elif (defined(BSD) || defined(__hurd__) || defined(__gnu_hurd__)) && \
    !(defined(__darwin__) || defined(__APPLE__))
#	define PROCTITLE_USE_ARGV0_PTR
#else /* default case */
#	define PROCTITLE_USE_ARGV_ENVIRON_SPACE

#	if defined(__linux__) || \
	   defined(_AIX) || \
	   defined(__darwin__) || defined(__APPLE__)
#		define PROCTITLE_PADDING '\0'
#	else
		/*
		 * when the length of modified proctitle is
		 * shorter than original,
		 * ps(1) discards the modification and displays original.
		 * so '\0' cannot be used as PROCTITLE_PADDING.
		 */
#		define PROCTITLE_PADDING ' '
#	endif

#endif /* default case */

#define PROCTITLE_BUFSIZE	2048

#ifdef PROCTITLE_USE_SETPROCTITLE

int
gfarm_proctitle_init(const char *progname, int argc, char ***argvp)
{
	/* do not have to save progname for setproctitle(3) */
	return (0);
}

int
gfarm_proctitle_set(const char *fmt, ...)
{
	va_list ap;
	char buf[PROCTITLE_BUFSIZE];

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	setproctitle(buf); /* setproctitle(3) adds progname internally */

	return (0);
}

#else /* !PROCTITLE_USE_SETPROCTITLE */

/*
 * save progname for implmenetations other than setproctitle(3).
 * setproctitle(3) on *BSD automatically prepends progname as prefix,
 * and we prepends it for all other implmenetations too.
 */
static char *proctitle_progname = NULL;

static int
proctitle_progname_init(const char *progname, char *argv0)
{
	if (argv0 != NULL) {
		const char *s;

		progname = argv0;

		/* non-destructive variant of basename(3) */
		for (s = progname; *s != '\0'; s++) {
			if (s[0] == '/' && s[1] != '\0' && s[1] != '/')
				progname = &s[1];
		}
	}
	proctitle_progname = strdup(progname);
	if (proctitle_progname == NULL)
		return (ENOMEM);
	return (0);
}

#endif /* !PROCTITLE_USE_SETPROCTITLE */

#ifdef PROCTITLE_USE_PSTAT_SETCMD

int
gfarm_proctitle_init(const char *progname, int argc, char ***argvp)
{
	return (proctitle_progname_init(progname, (*argvp)[0]));
}

int
gfarm_proctitle_set(const char *fmt, ...)
{
	va_list ap;
	union pstun pst;
	int len;
	char buf[PROCTITLE_BUFSIZE];

	len = snprintf(buf, sizeof buf, "%s: ", proctitle_progname);
	if (len >= 0 && len < sizeof buf) {
		va_list ap;

		va_start(ap, fmt);
		(void)vsnprintf(buf + len, sizeof buf - len, fmt, ap);
		va_end(ap);
	}

	pstat(PSTAT_SETCMD, buf, len, 0, 0);
	return (0);
}

#endif /* PROCTITLE_USE_PSTAT_SETCMD */

#ifdef PROCTITLE_USE_PS_STRINGS

int
gfarm_proctitle_init(const char *progname, int argc, char ***argvp)
{
	return (proctitle_progname_init(progname, (*argvp)[0]));
}

int
gfarm_proctitle_set(const char *fmt, ...)
{
	va_list ap;
	union pstun pst;
	int len;
	static char buf[PROCTITLE_BUFSIZE];

	len = snprintf(buf, sizeof buf, "%s: ", proctitle_progname);
	if (len >= 0 && len < sizeof buf) {
		va_list ap;

		va_start(ap, fmt);
		(void)vsnprintf(buf + len, sizeof buf - len, fmt, ap);
		va_end(ap);
	}

	PS_STRINGS->ps_nargvstr = 1;
	PS_STRINGS->ps_argvstr = buf;
	return (0);
}

#endif /* PROCTITLE_USE_PS_STRINGS */

#ifdef PROCTITLE_USE_ARGV0_PTR

static char **proctitle_old_argv;

int
gfarm_proctitle_init(const char *progname, int argc, char ***argvp)
{
	int err = proctitle_progname_init(progname, (*argvp)[0]);
	char **new_argv, **old_argv = *argvp;

	if (err != 0)
		return (err);

	new_argv = gfarm_strarray_dup(old_argv);
	if (new_argv == NULL)
		return (ENOMEM);

	proctitle_old_argv = old_argv;
	*argvp = new_argv;
	return (0);
}

int
gfarm_proctitle_set(const char *fmt, ...)
{
	int len;
	static char buf[PROCTITLE_BUFSIZE];

	if (proctitle_old_argv == NULL)
		return (EPERM);

	len = snprintf(buf, sizeof buf, "%s: ", proctitle_progname);
	if (len >= 0 && len < sizeof buf) {
		va_list ap;

		va_start(ap, fmt);
		(void)vsnprintf(buf + len, sizeof buf - len, fmt, ap);
		va_end(ap);
	}

	proctitle_old_argv[0] = buf;
	proctitle_old_argv[1] = NULL;
	return (0);
}

#endif /* PROCTITLE_USE_ARGV0_PTR */

#ifdef PROCTITLE_USE_ARGV_ENVIRON_SPACE

static char *proctitle_argv_environ_space;
static size_t proctitle_argv_environ_size;

int
gfarm_proctitle_init(const char *progname, int argc, char ***argvp)
{
	int i, err = proctitle_progname_init(progname, (*argvp)[0]);
	char *tail, **new_environ, **new_argv, **old_argv = *argvp;
#if defined(__darwin__) || defined(__APPLE__)
#	define environ	(*_NSGetEnviron())
#else
	extern char **environ;
#endif

	if (err != 0)
		return (err);

	/* tail = the end of contiguous space for argv and environ */
	tail = old_argv[0] + strlen(old_argv[0]);
	for (i = 1; i < argc; i++) {
		if (tail + 1 == old_argv[i])
			tail = old_argv[i] + strlen(old_argv[i]);
	}
	for (i = 0; environ[i] != NULL; i++) {
		if (tail + 1 == environ[i])
			tail = environ[i] + strlen(environ[i]);
	}
	new_argv = gfarm_strarray_dup(old_argv);
	new_environ = gfarm_strarray_dup(environ);
	if (new_argv == NULL || new_environ == NULL) {
		if (new_argv != NULL)
			gfarm_strarray_free(new_argv);
		if (new_environ != NULL)
			gfarm_strarray_free(new_environ);
		return (ENOMEM);
	}

	proctitle_argv_environ_space = old_argv[0];
	proctitle_argv_environ_size = tail - old_argv[0];

	for (i = 1; i < argc; i++)
		old_argv[i] = proctitle_argv_environ_space +
		    proctitle_argv_environ_size; /* this points '\0' */

	environ = new_environ;
#if defined(__darwin__) || defined(__APPLE__)
	*_NSGetArgv() = new_argv;
#endif
	*argvp = new_argv;
	return (0);
}

int
gfarm_proctitle_set(const char *fmt, ...)
{
	int len, len2 = 0;

	if (proctitle_argv_environ_space == NULL)
		return (EPERM);

	len = snprintf(proctitle_argv_environ_space,
	    proctitle_argv_environ_size, "%s: ", proctitle_progname);
	if (len >= 0 && len < proctitle_argv_environ_size) {
		va_list ap;

		va_start(ap, fmt);
		len2 = vsnprintf(proctitle_argv_environ_space + len,
		    proctitle_argv_environ_size - len, fmt, ap);
		va_end(ap);
	}

	if (len + len2 < proctitle_argv_environ_size)
		memset(proctitle_argv_environ_space + len + len2,
		    PROCTITLE_PADDING,
		    proctitle_argv_environ_size - len - len2 - 1);

	return (0);
}

#endif /* PROCTITLE_USE_ARGV_ENVIRON_SPACE */
