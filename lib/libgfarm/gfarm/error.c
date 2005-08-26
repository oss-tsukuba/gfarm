/*
 * $Id$
 */

#include <sys/param.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <netdb.h>
#if defined(__linux__)
#include <features.h> /* __GNUC_PREREQ() */
#endif
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include "hash.h"

/*
 * address constants which represents error conditions
 */

/* classic errno (1..10, 12..34) */
char GFARM_ERR_OPERATION_NOT_PERMITTED[] = "operation not permitted";
char GFARM_ERR_NO_SUCH_OBJECT[] = "no such object";
char GFARM_ERR_INTERRUPTED_SYSTEM_CALL[] = "interrupted system call";
char GFARM_ERR_INPUT_OUTPUT[] = "input/output error";
char GFARM_ERR_NO_MEMORY[] = "not enough memory";
char GFARM_ERR_PERMISSION_DENIED[] = "permission denied";
char GFARM_ERR_ALREADY_EXISTS[] = "already exists";
char GFARM_ERR_NOT_A_DIRECTORY[] = "not a directory";
char GFARM_ERR_IS_A_DIRECTORY[] = "is a directory";
char GFARM_ERR_INVALID_ARGUMENT[] = "invalid argument";
char GFARM_ERR_TEXT_FILE_BUSY[] = "text file busy";
char GFARM_ERR_NO_SPACE[] = "no space";
char GFARM_ERR_READ_ONLY_FILE_SYSTEM[] = "read-only file system";
char GFARM_ERR_BROKEN_PIPE[] = "broken pipe";

/* non classic, non-blocking and interrupt i/o */
char GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE[] =
	"resource temporarily unavailable";

/* non classic, filesystem related errors */
char GFARM_ERR_DISK_QUOTA_EXCEEDED[] = "disk quota exceeded";
char GFARM_ERR_DIRECTORY_NOT_EMPTY[] = "directory not empty";

/* non classic, system call */
char GFARM_ERR_FUNCTION_NOT_IMPLEMENTED[] = "function not implemented";

/* math software */
char GFARM_ERR_NUMERICAL_RESULT_OUT_OF_RANGE[] =
	"numerical result out of range";

/* ipc/network errors */
char GFARM_ERR_OPERATION_NOT_SUPPORTED[] = "operation not supported";
char GFARM_ERR_AUTHENTICATION[] = "authentication error";
char GFARM_ERR_EXPIRED[] = "expired";
char GFARM_ERR_PROTOCOL_NOT_SUPPORTED[] = "protocol not supported";
char GFARM_ERR_PROTOCOL[] = "protocol error";
char GFARM_ERR_NETWORK_IS_UNREACHABLE[] = "network is unreachable";
char GFARM_ERR_NO_ROUTE_TO_HOST[] = "no route to host";
char GFARM_ERR_CONNECTION_TIMED_OUT[] = "connection timed out";
char GFARM_ERR_CONNECTION_REFUSED[] = "connection refused";
char GFARM_ERR_CONNECTION_RESET_BY_PEER[] = "connection reset by peer";
char GFARM_ERR_SOCKET_IS_NOT_CONNECTED[] = "socket is not connected";
char GFARM_ERR_UNKNOWN_HOST[] = "unknown host";

/* gfarm specific errors */
char GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING[] =
	"\"gfarm:\" URL prefix is missing";
char GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE[] =
	"fragment index not available";
char GFARM_ERR_FRAGMENT_NUMBER_DOES_NOT_MATCH[] =
	"fragment number does not match";
char GFARM_ERR_AMBIGUOUS_RESULT[] = "ambiguous result";
char GFARM_ERR_INCONSISTENT_RECOVERABLE[] =
	"inconsistent metadata fixed, try again";
char GFARM_ERR_NO_FRAGMENT_INFORMATION[] =
	"no fragment information";
char GFARM_ERR_NO_REPLICA_ON_HOST[] =
	"no file replica on the host";
extern char GFARM_ERR_UNEXPECTED_EOF[] =
	"unexpected EOF";
char GFARM_ERR_UNKNOWN[] = "unknown error";

/*
 * NOTE: The order of the following table is important.
 *
 * For cases that multiple GFARM_ERRs are mapped into same UNIX errno.
 * first map entry will be used to map UNIX errno to GFARM_ERR,
 * The implementation of gfarm_errno_to_error_initialize() and
 * gfarm_errno_to_error() ensures this.
 *
 * It is ok that different gfarm error strings are mapped into same errno
 * (because gfarm error string is more detailed than errno),
 * but it isn't ok that different errno's are mapped into same gfarm string
 * (because that means errno is more detailed than gfarm error string).
 */

struct {
	int unix_errno;
	char *gfarm_error;
} gfarm_errno_error_map[] = {
	/*
	 * classic errno (1..10, 12..34)
	 */
	{ EPERM,	GFARM_ERR_OPERATION_NOT_PERMITTED },
	{ ENOENT,	GFARM_ERR_NO_SUCH_OBJECT },
	{ EINTR,	GFARM_ERR_INTERRUPTED_SYSTEM_CALL },
	{ EIO,		GFARM_ERR_INPUT_OUTPUT },
	{ ENOMEM,	GFARM_ERR_NO_MEMORY },
	{ EACCES,	GFARM_ERR_PERMISSION_DENIED },
	{ EEXIST,	GFARM_ERR_ALREADY_EXISTS },
	{ ENOTDIR,	GFARM_ERR_NOT_A_DIRECTORY },
	{ EISDIR,	GFARM_ERR_IS_A_DIRECTORY },
	{ EINVAL,	GFARM_ERR_INVALID_ARGUMENT },
	{ ETXTBSY,	GFARM_ERR_TEXT_FILE_BUSY },
	{ ENOSPC,	GFARM_ERR_NO_SPACE },
	{ EROFS,	GFARM_ERR_READ_ONLY_FILE_SYSTEM },
	{ EPIPE,	GFARM_ERR_BROKEN_PIPE },

	/*
	 * non classic, non-blocking and interrupt i/o
	 */
	{ EAGAIN,	GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE },

	/*
	 * non classic, filesystem related errors
	 */
	{ EDQUOT,	GFARM_ERR_DISK_QUOTA_EXCEEDED },
	{ ENOTEMPTY,	GFARM_ERR_DIRECTORY_NOT_EMPTY },

	/*
	 * non classic, system call
	 */
	{ ENOSYS,	GFARM_ERR_FUNCTION_NOT_IMPLEMENTED },

	/*
	 * math software
	 */
	{ ERANGE,	GFARM_ERR_NUMERICAL_RESULT_OUT_OF_RANGE },

	/*
	 * ipc/network errors
	 */
	{ EOPNOTSUPP,	GFARM_ERR_OPERATION_NOT_SUPPORTED },
#ifdef EAUTH /* BSD */
	{ EAUTH,	GFARM_ERR_AUTHENTICATION },
#else
	/* GFARM_ERR_AUTHENTICATION may be mapped into EPERM, */
	/* but EPERM will be mapped into GFARM_ERR_OPERATION_NOT_PERMITTED. */
	{ EPERM,	GFARM_ERR_AUTHENTICATION },
#endif
#ifdef ETIME /* Linux */
	{ ETIME,	GFARM_ERR_EXPIRED },
#endif
	{ EPROTONOSUPPORT, GFARM_ERR_PROTOCOL_NOT_SUPPORTED },
#ifdef EPROTO /* SVR4 and Linux */
	{ EPROTO,	GFARM_ERR_PROTOCOL },
#else
	/* GFARM_ERR_PROTOCOL may be mapped into EPROTONOSUPPORT, */
	/* but EPROTONOSUPPORT will be mapped into GFARM_ERR_PROTOCOL_NOT_SUPPORTED. */
	{ EPROTONOSUPPORT, GFARM_ERR_PROTOCOL },
#endif
	{ ENETUNREACH,	GFARM_ERR_NETWORK_IS_UNREACHABLE },
	{ EHOSTUNREACH,	GFARM_ERR_NO_ROUTE_TO_HOST },
	{ ETIMEDOUT,	GFARM_ERR_CONNECTION_TIMED_OUT },
	{ ECONNREFUSED,	GFARM_ERR_CONNECTION_REFUSED },
	{ ECONNRESET,	GFARM_ERR_CONNECTION_RESET_BY_PEER },
	{ ENOTCONN,	GFARM_ERR_SOCKET_IS_NOT_CONNECTED },
};

/* prevent infinite loop caused by where IN_ERRNO(i) is always true. */
#ifndef MAX_ERRNO
#define MAX_ERRNO	1024
#endif

#define ERRLIST_HASHTAB_SIZE	251	/* prime number */

struct gfarm_hash_table *gfarm_errlist_hashtab;

#if defined(__linux__) && defined(__GNUC_PREREQ)
#if __GNUC_PREREQ(3,2) /* newer glibc warns against sys_errlist and sys_nerr */
# define STRERROR(i)	strerror(i)
# define IN_ERRNO(i)	(strerror(i) != NULL && (i) < MAX_ERRNO)
#endif
#endif

#ifndef STRERROR
#if defined(HAVE_SYS_NERR)
# define STRERROR(i)	sys_errlist[i]
# define IN_ERRNO(i)	((i) < sys_nerr)
#else /* Solaris doesn't export sys_nerr/sys_errlist, any more */
# define STRERROR(i)	strerror(i)
# define IN_ERRNO(i)	(strerror(i) != NULL && (i) < MAX_ERRNO)
#endif
#endif /* STRERROR */

void
gfarm_errlist_hashtab_initialize(void)
{
	int i, created;
	struct gfarm_hash_entry *p;

	gfarm_errlist_hashtab = gfarm_hash_table_alloc(ERRLIST_HASHTAB_SIZE,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	if (gfarm_errlist_hashtab == NULL) {
		fprintf(stderr, "gfarm_errlist_hashtab_initialize(): "
			"no memory\n");
		exit(1);
	}
	for (i = 1; IN_ERRNO(i); i++) {
		if (STRERROR(i) == NULL)
			continue;
		p = gfarm_hash_enter(gfarm_errlist_hashtab,
			STRERROR(i), strlen(STRERROR(i)) + 1,
			sizeof(int), &created);
		if (p == NULL) {
			fprintf(stderr, "gfarm_errlist_hashtab_initialize(): "
				"no memory for errno %d\n", i);
			exit(1);
		}
		if (created)
			*(int *)gfarm_hash_entry_data(p) = i;
	}
}

int
gfarm_error_to_errno(const char *error)
{
	struct gfarm_hash_entry *p;
	int i;

	if (gfarm_errlist_hashtab == NULL)
		gfarm_errlist_hashtab_initialize();

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_errno_error_map); i++) {
		if (error == gfarm_errno_error_map[i].gfarm_error)
			return (gfarm_errno_error_map[i].unix_errno);
	}

	/*
	 * The reason why we compare string content rather than string
	 * address here is that strerror(3) may always copy error string
	 * to its internal buffer.
	 *
	 * XXX - If we always use sys_errlist[] rather than strerror(3),
	 *	is it ok to just compare string address?
	 */
	/* XXX - if locale LC_MESSAGES is changed, this may not work. */
	p = gfarm_hash_lookup(gfarm_errlist_hashtab, error, strlen(error) + 1);
	if (p != NULL)
		return (*(int *)gfarm_hash_entry_data(p));

	return (EINVAL); /* last resort, cannot mapped into errno */
}

#ifndef ELAST /* sys_nerr isn't constant */
#define ELAST 257
#endif
char *gfarm_errno_to_error_map[ELAST + 1];
int gfarm_errno_to_error_initialized;
int gfarm_errno_to_error_table_overflow;

void
gfarm_errno_to_error_initialize(void)
{
	int i, unix_errno;

	if (gfarm_errno_to_error_initialized)
		return;
	gfarm_errno_to_error_initialized = 1;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_errno_error_map); i++) {
		unix_errno = gfarm_errno_error_map[i].unix_errno;
		if (unix_errno > ELAST) {
			gfarm_errno_to_error_table_overflow = 1;
		} else if (gfarm_errno_to_error_map[unix_errno] == NULL) {
			/* make sure to be first match */
			gfarm_errno_to_error_map[unix_errno] =
				gfarm_errno_error_map[i].gfarm_error;
		}
	}
}

char *
gfarm_errno_to_error(int unix_errno)
{
	int i;
	char *e;

	/* this function is critical, so make sure */
	if (!gfarm_errno_to_error_initialized)
		gfarm_errno_to_error_initialize();

	if (unix_errno <= ELAST) {
		e = gfarm_errno_to_error_map[unix_errno];
		if (e != NULL)
			return (e);
		else {
			/* XXX const */
			return ((char *)STRERROR(unix_errno));
		}
	}

	if (gfarm_errno_to_error_table_overflow) {
		for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_errno_error_map);
		     i++) {
			if (unix_errno == gfarm_errno_error_map[i].unix_errno)
				return (gfarm_errno_error_map[i].gfarm_error);
		}
	}

	/* XXX const */
	return ((char *)STRERROR(unix_errno));
}

void
gfarm_error_initialize(void)
{
	gfarm_errlist_hashtab_initialize();
	gfarm_errno_to_error_initialize();
}
